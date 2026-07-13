/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2002 Xodnizel
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "types.h"
#include "x6502.h"
#include "fceu.h"
#include "fds.h"
#include "sound.h"
#include "file.h"
#include "utils/md5.h"
#include "utils/memory.h"
#include "state.h"
#include "file.h"
#include "cart.h"
#include "ines.h"
#include "netplay.h"
#include "driver.h"
#include "movie.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

 //	TODO:  Add code to put a delay in between the time a disk is inserted
 //	and the when it can be successfully read/written to.  This should
 //	prevent writes to wrong places OR add code to prevent disk ejects
 //	when the virtual motor is on (mmm...virtual motor).
extern int disableBatteryLoading;

bool isFDS = false; //flag for determining if a FDS game is loaded, movie.cpp needs this

static DECLFR(FDSRead4030);
static DECLFR(FDSRead4031);
static DECLFR(FDSRead4032);
static DECLFR(FDSRead4033);

static DECLFW(FDSWrite);

static DECLFW(FDSWaveWrite);
static DECLFR(FDSWaveRead);

static DECLFR(FDSSRead);
static DECLFW(FDSSWrite);

static void FDSInit(void);
static void FDSClose(void);

static void FDSFix(int a);

static uint8 FDSRegs[6];
static int32 IRQLatch, IRQCount;
static uint8 IRQa;

static uint8* FDSRAM = NULL;
static uint32 FDSRAMSize;
static uint8* FDSBIOS = NULL;
static uint32 FDSBIOSsize;
static uint8* CHRRAM = NULL;
static uint32 CHRRAMSize;

/* Original disk data backup, to help in creating save states. */
static uint8* diskdatao[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static uint8* diskdata[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

static int TotalSides; //mbg merge 7/17/06 - unsignedectomy
static uint8 DiskWritten = 0;    /* Set to 1 if disk was written to. */
static uint8 writeskip;
static int32 DiskPtr;
static int32 DiskSeekIRQ;
static uint8 SelectDisk, InDisk;

/* 4024(w), 4025(w), 4031(r) by dink(fbneo) */
enum FDS_DiskBlockIDs { DSK_INIT = 0, DSK_VOLUME, DSK_FILECNT, DSK_FILEHDR, DSK_FILEDATA };
static uint8  mapperFDS_control;    // 4025(w) control register
static uint16 mapperFDS_filesize;	// size of file being read/written
static uint8  mapperFDS_block;		// block-id of current block
static uint16 mapperFDS_blockstart;	// start-address of current block
static uint16 mapperFDS_blocklen;	// length of current block
static uint16 mapperFDS_diskaddr;   // current address relative to blockstart
static uint8  mapperFDS_diskaccess;	// disk needs to be accessed at least once before writing
#define fds_disk() (diskdata[InDisk][mapperFDS_blockstart + mapperFDS_diskaddr])
#define mapperFDS_diskinsert (InDisk != 255)


#define DC_INC    1

void FDSGI(GI h) {
	switch (h)
	{
	case GI_CLOSE: FDSClose(); break;
	case GI_POWER: FDSInit(); break;

		// Unhandled Cases
	case GI_RESETM2:
	case GI_RESETSAVE:
		break;
	}
}

static void FDSStateRestore(int version) {
	int x;

	setmirror(((FDSRegs[5] & 8) >> 3) ^ 1);

	if (version >= 9810)
		for (x = 0; x < TotalSides; x++) {
			int b;
			for (b = 0; b < 65500; b++)
				diskdata[x][b] ^= diskdatao[x][b];
		}
}

void FDSSound();
void FDSSoundReset(void);
void FDSSoundStateAdd(void);
static void RenderSound(void);
static void RenderSoundHQ(void);

static void FDSInit(void) {
	memset(FDSRegs, 0, sizeof(FDSRegs));
	writeskip = DiskPtr = DiskSeekIRQ = 0;

	setmirror(1);
	setprg8(0xE000, 0);			// BIOS
	setprg32r(1, 0x6000, 0);	// 32KB RAM
	setchr8(0);					// 8KB CHR RAM

	MapIRQHook = FDSFix;
	GameStateRestore = FDSStateRestore;

	SetReadHandler(0x4030, 0x4030, FDSRead4030);
	SetReadHandler(0x4031, 0x4031, FDSRead4031);
	SetReadHandler(0x4032, 0x4032, FDSRead4032);
	SetReadHandler(0x4033, 0x4033, FDSRead4033);

	SetWriteHandler(0x4020, 0x4025, FDSWrite);

	SetWriteHandler(0x6000, 0xDFFF, CartBW);
	SetReadHandler(0x6000, 0xFFFF, CartBR);

	IRQCount = IRQLatch = IRQa = 0;

	FDSSoundReset();
	InDisk = 0;
	SelectDisk = 0;

	mapperFDS_control = 0;
	mapperFDS_filesize = 0;
	mapperFDS_block = 0;
	mapperFDS_blockstart = 0;
	mapperFDS_blocklen = 0;
	mapperFDS_diskaddr = 0;
	mapperFDS_diskaccess = 0;
}

void FCEU_FDSInsert(void)
{
	if (TotalSides == 0)
	{
		FCEU_DispMessage("Not FDS; can't eject disk.", 0);
		return;
	}

	if (FCEUI_EmulationPaused())
		EmulationPaused |= EMULATIONPAUSED_FA;

	if (FCEUMOV_Mode(MOVIEMODE_RECORD))
		FCEUMOV_AddCommand(FCEUNPCMD_FDSINSERT);

	if (InDisk == 255)
	{
		FCEU_DispMessage("Disk %d Side %s Inserted", 0, SelectDisk >> 1, (SelectDisk & 1) ? "B" : "A");
		InDisk = SelectDisk;
	}
	else
	{
		FCEU_DispMessage("Disk %d Side %s Ejected", 0, SelectDisk >> 1, (SelectDisk & 1) ? "B" : "A");
		InDisk = 255;
	}
}
/*
void FCEU_FDSEject(void)
{
InDisk=255;
}
*/
void FCEU_FDSSelect(void)
{
	if (TotalSides == 0)
	{
		FCEU_DispMessage("Not FDS; can't select disk.", 0);
		return;
	}
	if (InDisk != 255)
	{
		FCEU_DispMessage("Eject disk before selecting.", 0);
		return;
	}

	if (FCEUI_EmulationPaused())
		EmulationPaused |= EMULATIONPAUSED_FA;

	if (FCEUMOV_Mode(MOVIEMODE_RECORD))
		FCEUMOV_AddCommand(FCEUNPCMD_FDSSELECT);

	SelectDisk = ((SelectDisk + 1) % TotalSides) & 3;
	FCEU_DispMessage("Disk %d Side %c Selected", 0, SelectDisk >> 1, (SelectDisk & 1) ? 'B' : 'A');
}

#define IRQ_Repeat  0x01
#define IRQ_Enabled 0x02

static void FDSFix(int a) {
	if (IRQa & IRQ_Enabled) {
		IRQCount -= a;
		if (IRQCount <= 0) {
			IRQCount = IRQLatch;
			/* Puff Puff Golf notes:
			Game freezes while music playing ingame after inserting Disk Side B.
			IRQ is usually fired at scanline 169 and 183 for music to work.

			At some point after inserting disk B, an IRQ is fired at scanline 174 which
			will just freeze game while music plays.

			If you ignore triggering IRQ altogether, game plays but no music
			*/
			X6502_IRQBegin(FCEU_IQEXT);
			if (!(IRQa & IRQ_Repeat)) {
				IRQa &= ~IRQ_Enabled;
			}
		}
	}
	if (DiskSeekIRQ > 0) {
		DiskSeekIRQ -= a;
		if (DiskSeekIRQ <= 0) {
			if (FDSRegs[5] & 0x80) {
				X6502_IRQBegin(FCEU_IQEXT2);
			}
		}
	}
}

static DECLFR(FDSRead4030) {
	uint8 ret = 0;

	/* from hardware testing (#789):
	4030(r) bit 3 = 4025(w) bit 3
	(nametable arrangement/mirroring)
	*/
	ret |= (mapperFDS_control & 0x08);

	/* Cheap hack. */
	if (X.IRQlow & FCEU_IQEXT) ret |= 1;
	if (X.IRQlow & FCEU_IQEXT2) ret |= 2;

	if (!fceuindbg) {
		X6502_IRQEnd(FCEU_IQEXT);
		X6502_IRQEnd(FCEU_IQEXT2);
	}
	return ret;
}

static DECLFR(FDSRead4031) {
	static uint8 ret = 0;

	ret = 0xff;
	if (mapperFDS_diskinsert && mapperFDS_control & 0x04) {
		mapperFDS_diskaccess = 1;

		ret = 0;

		switch (mapperFDS_block) {
		case DSK_FILEHDR:
			if (mapperFDS_diskaddr < mapperFDS_blocklen) {
				ret = fds_disk();
				switch (mapperFDS_diskaddr) {
				case 13: mapperFDS_filesize = ret; break;
				case 14:
					mapperFDS_filesize |= ret << 8;
					//char fdsfile[10];
					//strncpy(fdsfile, (char*)&diskdata[InDisk][mapperFDS_blockstart + 3], 8);
					//printf("Read file: %s (size: %d)\n"), fdsfile, mapperFDS_filesize);
					break;
				}
				mapperFDS_diskaddr++;
			}
			break;
		default:
			if (mapperFDS_diskaddr < mapperFDS_blocklen) {
				ret = fds_disk();
				mapperFDS_diskaddr++;
			}
			break;
		}

		DiskSeekIRQ = 150;
		X6502_IRQEnd(FCEU_IQEXT2);
	}

	return ret;
}

static DECLFR(FDSRead4032) {
	uint8 ret;

	ret = X.DB & ~7;
	if (InDisk == 255)
		ret |= 5;

	if (InDisk == 255 || !(FDSRegs[5] & 1) || (FDSRegs[5] & 2))
		ret |= 2;
	return ret;
}

static DECLFR(FDSRead4033) {
	return 0x80; // battery
}

/* Begin FDS sound */

/*
 * Split FDS sound core
 *
 * Normal FDS ROM and single-FDS NSF playback use the original FCEUX b19/b24
 * latch core, which is closer to the standalone FDS reference playback.
 * Multi-expansion NSF mux/direct playback uses the previous mixed/direct core
 * as a separate state machine.  Keeping the two cores physically separate is
 * important: the mux can be committed after NSF initialization has already
 * performed register writes/renders, and sharing the same count/env/phase state
 * can make only part of the stream advance with the mixed timing, producing the
 * remaining pitch drift heard in muxed FDS tracks.
 */

#define FDSClock (1789772.7272727272727272 / 2)

extern int FCEU_NSFIsExpSoundMuxEnabled(void);
extern int FCEU_NSFGetExpSoundCount(void);

static uint8 FDSNSFDirectMode = 0;
static uint8 FDSMixedCoreLatched = 0;

typedef struct {
	int64 cycles;
	int64 count;
	int64 envcount;

	uint32 b19shiftreg60;
	uint32 b24adder66;
	uint32 b24latch68;
	uint32 b17latch76;
	int32 clockcount;
	uint8 b8shiftreg88;

	uint8 amplitude[2];
	uint8 speedo[2];
	uint8 mwcount;
	uint8 mwstart;
	uint8 mwave[0x20];
	uint8 cwave[0x40];
	uint8 SPSG[0xB];
} FDSNORMALCORE;

typedef struct {
	int64 cycles;
	int64 count;
	int64 envcount;
	uint8 tick_div;

	uint8 amplitude[2];
	uint8 speedo[2];
	uint8 mwcount;
	uint8 mwstart;
	uint8 mwave[0x20];
	uint8 cwave[0x40];
	uint8 SPSG[0xB];

	uint32 cwave_freq;
	uint32 cwave_pos;
	uint32 mod_freq;
	uint32 mod_pos;
	uint8 mod_disabled;
	uint32 sweep_bias;
	int32 mod_out;
	uint32 sample_cache_out;
} FDSMIXEDCORE;

static FDSNORMALCORE fdsn;
static FDSMIXEDCORE fdsm;
static const int bias_tab[8] = { 0, 1, 2, 4, 0, -4, -2, -1 };
static int32 FBCNormal = 0;
static int32 FBCMixed = 0;


static INLINE int FDSUseMixedCore(void) {
	/*
	 * In NSF direct mode the mux can be committed after FDS has already
	 * installed its handlers, and in some players the per-chip register writes
	 * begin while the final mux flag has not yet been observed by FDS.  The
	 * previous good mixed version used one direct FDS core for the whole stream.
	 * Latch the mixed path as soon as this is a direct NSF with more than one
	 * expansion sound registered, not only after GameExpSound has already been
	 * replaced by the mux wrapper.  Single-FDS NSF/ROM playback still stays on
	 * the normal original FCEUX core.
	 */
	if (FDSNSFDirectMode && (FCEU_NSFIsExpSoundMuxEnabled() || FCEU_NSFGetExpSoundCount() > 1))
		FDSMixedCoreLatched = 1;
	return FDSMixedCoreLatched;
}

void FDSSoundStateAdd(void) {
	AddExState(fdsn.cwave, 64, 0, "WAVE");
	AddExState(fdsn.mwave, 32, 0, "MWAV");
	AddExState(fdsn.amplitude, 2, 0, "AMPL");
	AddExState(fdsn.SPSG, 0xB, 0, "SPSG");
	AddExState(&fdsn.b8shiftreg88, 1, 0, "B88");
	AddExState(&fdsn.clockcount, 4, 1, "CLOC");
	AddExState(&fdsn.b19shiftreg60, 4, 1, "B60");
	AddExState(&fdsn.b24adder66, 4, 1, "B66");
	AddExState(&fdsn.b24latch68, 4, 1, "B68");
	AddExState(&fdsn.b17latch76, 4, 1, "B76");

	AddExState(fdsm.cwave, 64, 0, "MCWV");
	AddExState(fdsm.mwave, 32, 0, "MMW2");
	AddExState(fdsm.amplitude, 2, 0, "MAMP");
	AddExState(fdsm.SPSG, 0xB, 0, "MSPG");
	AddExState(&fdsm.cwave_freq, 4, 1, "MCFQ");
	AddExState(&fdsm.cwave_pos, 4, 1, "MCP2");
	AddExState(&fdsm.mod_freq, 4, 1, "MMFQ");
	AddExState(&fdsm.mod_pos, 4, 1, "MMP2");
	AddExState(&fdsm.mod_disabled, 1, 1, "MDS2");
	AddExState(&fdsm.tick_div, 1, 1, "MTDV");
	AddExState(&fdsm.sweep_bias, 4, 1, "MSWB");
	AddExState(&fdsm.mod_out, 4, 1, "MMOU");
	AddExState(&fdsm.sample_cache_out, 4, 1, "MWOU");
}

static DECLFR(FDSSRead) {
	FDSNORMALCORE* n = &fdsn;
	FDSMIXEDCORE* m = &fdsm;
	if (FDSUseMixedCore()) {
		switch (A & 0xF) {
		case 0x0: return(m->amplitude[0] | (X.DB & 0xC0));
		case 0x2: return(m->amplitude[1] | (X.DB & 0xC0));
		}
	}
	switch (A & 0xF) {
	case 0x0: return(n->amplitude[0] | (X.DB & 0xC0));
	case 0x2: return(n->amplitude[1] | (X.DB & 0xC0));
	}
	return(X.DB);
}

static void DoEnvNormal(void) {
	static int counto[2] = { 0, 0 };
	int x;

	for (x = 0; x < 2; x++)
		if (!(fdsn.SPSG[x << 2] & 0x80) && !(fdsn.SPSG[0x3] & 0x40)) {
			if (counto[x] <= 0) {
				if (!(fdsn.SPSG[x << 2] & 0x80)) {
					if (fdsn.SPSG[x << 2] & 0x40) {
						if (fdsn.amplitude[x] < 0x3F)
							fdsn.amplitude[x]++;
					}
					else {
						if (fdsn.amplitude[x] > 0)
							fdsn.amplitude[x]--;
					}
				}
				counto[x] = (fdsn.SPSG[x << 2] & 0x3F);
			}
			else
				counto[x]--;
		}
}

static void DoEnvMixed(void) {
	static int counto[2] = { 0, 0 };
	int x;

	for (x = 0; x < 2; x++)
		if (!(fdsm.SPSG[x << 2] & 0x80) && !(fdsm.SPSG[0x3] & 0x40)) {
			if (counto[x] <= 0) {
				if (!(fdsm.SPSG[x << 2] & 0x80)) {
					if (fdsm.SPSG[x << 2] & 0x40) {
						if (fdsm.amplitude[x] < 0x3F)
							fdsm.amplitude[x]++;
					}
					else {
						if (fdsm.amplitude[x] > 0)
							fdsm.amplitude[x]--;
					}
				}
				counto[x] = (fdsm.SPSG[x << 2] & 0x3F);
			}
			else
				counto[x]--;
		}
}

static DECLFR(FDSWaveRead) {
	if (FDSUseMixedCore())
		return(fdsm.cwave[A & 0x3f] | (X.DB & 0xC0));
	return(fdsn.cwave[A & 0x3f] | (X.DB & 0xC0));
}

static DECLFW(FDSWaveWrite) {
	if (fdsn.SPSG[0x9] & 0x80)
		fdsn.cwave[A & 0x3f] = V & 0x3F;
	if (fdsm.SPSG[0x9] & 0x80)
		fdsm.cwave[A & 0x3f] = V & 0x3F;
}

static DECLFW(FDSSWrite) {
	if (FSettings.SndRate) {
		if (FSettings.soundq >= 1)
			RenderSoundHQ();
		else
			RenderSound();
	}

	A -= 0x4080;
	switch (A) {
	case 0x0:
	case 0x4:
		if (V & 0x80) {
			fdsn.amplitude[(A & 0xF) >> 2] = V & 0x3F;
			fdsm.amplitude[(A & 0xF) >> 2] = V & 0x3F;
		}
		break;

	case 0x2:
		fdsm.cwave_freq &= 0xFF00;
		fdsm.cwave_freq |= V << 0;
		break;

	case 0x3:
		if (V & 0x80)
			fdsm.cwave_pos = 0;
		fdsm.cwave_freq &= 0x00FF;
		fdsm.cwave_freq |= (V & 0xF) << 8;
		break;

	case 0x5:
		fdsm.sweep_bias = V & 0x7F;
		break;

	case 0x6:
		fdsm.mod_freq &= 0xFF00;
		fdsm.mod_freq |= V << 0;
		break;

	case 0x7:
		/* Original-core modulation table write pointer reset. */
		fdsn.b17latch76 = 0;
		fdsn.SPSG[0x5] = 0;

		/* Previous mixed/direct core behavior. */
		fdsm.mod_freq &= 0x00FF;
		fdsm.mod_freq |= (V & 0xF) << 8;
		if (V & 0x80)
			fdsm.mod_pos &= ~0x0FFFU;
		fdsm.mod_disabled = (bool)(V & 0x80);
		break;

	case 0x8:
		/* Original FCEUX core table, used by normal FDS ROM and single-FDS NSF. */
		fdsn.b17latch76 = 0;
		fdsn.mwave[fdsn.SPSG[0x5] & 0x1F] = V & 0x07;
		fdsn.SPSG[0x5] = (fdsn.SPSG[0x5] + 1) & 0x1F;

		/* Previous mixed/direct-mode table behavior, kept exact and isolated. */
		if (fdsm.mod_disabled) {
			fdsm.mwave[(fdsm.mod_pos >> 13) & 0x1F] = V & 0x07;
			fdsm.mod_pos = (fdsm.mod_pos + (1U << 13)) & 0x3FFFFU;
		}
		break;
	}
	fdsn.SPSG[A] = V;
	fdsm.SPSG[A] = V;
}

/* ---------- Original FCEUX core for normal FDS ROM / single FDS NSF ---------- */

static int ta;

static INLINE void ClockRiseNormal(void) {
	if (!fdsn.clockcount) {
		ta++;
		fdsn.b19shiftreg60 = (fdsn.SPSG[0x2] | ((fdsn.SPSG[0x3] & 0xF) << 8));
		fdsn.b17latch76 = (fdsn.SPSG[0x6] | ((fdsn.SPSG[0x07] & 0xF) << 8)) + fdsn.b17latch76;
		if (!(fdsn.SPSG[0x7] & 0x80)) {
			int t = fdsn.mwave[(fdsn.b17latch76 >> 13) & 0x1F] & 7;
			int t2 = fdsn.amplitude[1];
			int adj = 0;
			if ((t & 3)) {
				if ((t & 4)) adj -= (t2 * ((4 - (t & 3))));
				else adj += (t2 * ((t & 3)));
			}
			adj *= 2;
			if (adj > 0x7F) adj = 0x7F;
			if (adj < -0x80) adj = -0x80;
			fdsn.b8shiftreg88 = 0x80 + adj;
		}
		else {
			fdsn.b8shiftreg88 = 0x80;
		}
	}
	else {
		fdsn.b19shiftreg60 <<= 1;
		fdsn.b8shiftreg88 >>= 1;
	}

	fdsn.b24adder66 = (fdsn.b24latch68 + fdsn.b19shiftreg60) & 0x1FFFFFF;
}

static INLINE void ClockFallNormal(void) {
	if ((fdsn.b8shiftreg88 & 1))
		fdsn.b24latch68 = fdsn.b24adder66;
	fdsn.clockcount = (fdsn.clockcount + 1) & 7;
}

static INLINE int32 FDSDoSoundNormal(void) {
	fdsn.count += fdsn.cycles;
	if (fdsn.count >= ((int64)1 << 40)) {
	dogk:
		fdsn.count -= (int64)1 << 40;
		ClockRiseNormal();
		ClockFallNormal();
		fdsn.envcount--;
		if (fdsn.envcount <= 0) {
			fdsn.envcount += fdsn.SPSG[0xA] * 3;
			DoEnvNormal();
		}
	}
	if (fdsn.count >= 32768) goto dogk;

	{
		int k = fdsn.amplitude[0];
		if (k > 0x20) k = 0x20;
		return (fdsn.cwave[fdsn.b24latch68 >> 19] * k) * 4 / ((fdsn.SPSG[0x9] & 0x3) + 2);
	}
}

/* ---------- Mixed NSF/direct-mode core, matching the previous mixed/direct version ---------- */

static int32 sign_x_to_s32(int n, int32 v) {
	return ((int32)((uint32)v << (32 - n)) >> (32 - n));
}

static INLINE int32 FDSModCounterSignedMixed(void) {
	return sign_x_to_s32(7, (int32)(fdsm.sweep_bias & 0x7F));
}

static INLINE uint32 FDSCalcWavePitchMixed(void) {
	int32 temp = FDSModCounterSignedMixed() * (int32)(fdsm.amplitude[1] & 0x3F);

	if ((temp & 0x0F) && !(temp & 0x800))
		temp += 0x20;

	temp += 0x400;
	temp = (temp >> 4) & 0xFF;

	fdsm.mod_out = temp - 0x40;

	return (uint32)((fdsm.cwave_freq * (uint32)temp) & 0xFFFFF);
}

static INLINE void ClockModMixed(void) {
	if (!fdsm.mod_disabled) {
		uint32 old_mod_pos = fdsm.mod_pos;
		uint32 old_frac = old_mod_pos & 0x0FFFU;

		fdsm.mod_pos = (fdsm.mod_pos + (fdsm.mod_freq & 0x0FFFU)) & 0x3FFFFU;

		if ((fdsm.SPSG[0x7] & 0x40) || (old_frac + (fdsm.mod_freq & 0x0FFFU) >= 0x1000U)) {
			uint8 raw = fdsm.mwave[(fdsm.mod_pos >> 13) & 0x1F] & 0x07;

			if (raw == 4)
				fdsm.sweep_bias = 0;
			else
				fdsm.sweep_bias = (fdsm.sweep_bias + bias_tab[raw]) & 0x7F;
		}
	}
}

static INLINE void ClockCarrierMixed(void) {
	fdsm.cwave_pos = (fdsm.cwave_pos + FDSCalcWavePitchMixed()) & 0xFFFFFFU;
}

static INLINE int32 FDSDoSoundMixed(void) {
	uint32 prev_cwave_pos = fdsm.cwave_pos;

	fdsm.count += fdsm.cycles;

	while (fdsm.count >= ((int64)1 << 40)) {
		fdsm.count -= ((int64)1 << 40);

		fdsm.tick_div = (fdsm.tick_div + 1) & 7;
		if (!fdsm.tick_div) {
			ClockModMixed();
			if (!(fdsm.SPSG[0x3] & 0x80)) {
				ClockCarrierMixed();
			}
		}

		fdsm.envcount--;
		if (fdsm.envcount <= 0) {
			fdsm.envcount += fdsm.SPSG[0xA] * 3;
			DoEnvMixed();
		}
	}

	if ((fdsm.cwave_pos ^ prev_cwave_pos) & (0x3F << 18)) {
		int k = fdsm.amplitude[0];
		if (k > 0x20) k = 0x20;
		fdsm.sample_cache_out = (fdsm.cwave[(fdsm.cwave_pos >> 18) & 0x3F] * k) * 4 / ((fdsm.SPSG[0x9] & 0x3) + 2);
	}
	return fdsm.sample_cache_out;
}

static INLINE int32 FDSApplyVolume(int32 v)
{
	return (int32)(((int64)v * FSettings.FDSVolume) >> 8);
}

static void RenderSoundNormal(void) {
	int32 end, start;
	int32 x;

	start = FBCNormal;
	end = (SOUNDTS << 16) / soundtsinc;
	if (end <= start)
		return;
	FBCNormal = end;

	if (!(fdsn.SPSG[0x9] & 0x80))
		for (x = start; x < end; x++) {
			uint32 t = FDSDoSoundNormal();
			t += t >> 1;
			t >>= 4;
			Wave[x >> 4] += FDSApplyVolume((int32)t);
		}
}

static void RenderSoundMixed(void) {
	int32 end, start;
	int32 x;

	start = FBCMixed;
	end = (SOUNDTS << 16) / soundtsinc;
	if (end <= start)
		return;
	FBCMixed = end;

	if (!(fdsm.SPSG[0x9] & 0x80))
		for (x = start; x < end; x++) {
			uint32 t = FDSDoSoundMixed();
			t += t >> 1;
			t >>= 4;
			Wave[x >> 4] += FDSApplyVolume((int32)t);
		}
}

static void RenderSound(void) {
	if (FDSUseMixedCore())
		RenderSoundMixed();
	else
		RenderSoundNormal();
}

static void RenderSoundHQNormal(void) {
	uint32 x;

	if (!(fdsn.SPSG[0x9] & 0x80))
		for (x = FBCNormal; x < SOUNDTS; x++) {
			uint32 t = FDSDoSoundNormal();
			t += t >> 1;
			WaveHi[x] += FDSApplyVolume((int32)t);
		}
	FBCNormal = SOUNDTS;
}

static void RenderSoundHQMixed(void) {
	uint32 x;

	if (!(fdsm.SPSG[0x9] & 0x80))
		for (x = FBCMixed; x < SOUNDTS; x++) {
			uint32 t = FDSDoSoundMixed();
			t += t >> 1;
			WaveHi[x] += FDSApplyVolume((int32)t);
		}
	FBCMixed = SOUNDTS;
}

static void RenderSoundHQ(void) {
	if (FDSUseMixedCore())
		RenderSoundHQMixed();
	else
		RenderSoundHQNormal();
}

static void HQSync(int32 ts) {
	if (FDSUseMixedCore())
		FBCMixed = ts;
	else
		FBCNormal = ts;
}

void FDSSound(int c) {
	RenderSound();
	if (FDSUseMixedCore())
		FBCMixed = c;
	else
		FBCNormal = c;
}

void FDSNSFSetDirectMode(int enabled) {
	FDSNSFDirectMode = enabled ? 1 : 0;
	if (!FDSNSFDirectMode)
		FDSMixedCoreLatched = 0;
	else if (FCEU_NSFGetExpSoundCount() > 1 || FCEU_NSFIsExpSoundMuxEnabled())
		FDSMixedCoreLatched = 1;
}

static void FDS_ESI(void) {
	if (FSettings.SndRate) {
		if (FSettings.soundq >= 1) {
			fdsn.cycles = (int64)1 << 39;
			fdsm.cycles = (int64)1 << 39;
		}
		else {
			fdsn.cycles = ((int64)1 << 40) * FDSClock;
			fdsn.cycles /= FSettings.SndRate * 16;
			fdsm.cycles = fdsn.cycles;
		}
	}
	if (!FDSNSFDirectMode) {
		SetReadHandler(0x4040, 0x407f, FDSWaveRead);
		SetWriteHandler(0x4040, 0x407f, FDSWaveWrite);
		SetWriteHandler(0x4080, 0x408A, FDSSWrite);
		SetReadHandler(0x4090, 0x4092, FDSSRead);
	}
}

void FDSSoundReset(void) {
	memset(&fdsn, 0, sizeof(fdsn));
	memset(&fdsm, 0, sizeof(fdsm));
	FBCNormal = 0;
	FBCMixed = 0;
	FDSMixedCoreLatched = 0;
	FDS_ESI();
	GameExpSound.HiSync = HQSync;
	GameExpSound.HiFill = RenderSoundHQ;
	GameExpSound.Fill = FDSSound;
	GameExpSound.RChange = FDS_ESI;
}

uint8 FDSNSFRead(uint32 A) {
	if (A >= 0x4040 && A <= 0x407F)
		return FDSWaveRead(A);

	if (A >= 0x4090 && A <= 0x4092)
		return FDSSRead(A);

	return X.DB;
}

void FDSNSFWrite(uint32 A, uint8 V) {
	if (A >= 0x4040 && A <= 0x407F) {
		FDSWaveWrite(A, V);
		return;
	}

	if (A >= 0x4080 && A <= 0x408A) {
		FDSSWrite(A, V);
		return;
	}
}

void FDSNSFInstallSoundHandlers(void) {
	if (FDSNSFDirectMode && (FCEU_NSFGetExpSoundCount() > 1 || FCEU_NSFIsExpSoundMuxEnabled()))
		FDSMixedCoreLatched = 1;
	FDS_ESI();
}

static DECLFW(FDSWrite) {
	switch (A) {
	case 0x4020:
		IRQLatch &= 0xFF00;
		IRQLatch |= V;
		break;
	case 0x4021:
		IRQLatch &= 0xFF;
		IRQLatch |= V << 8;
		break;
	case 0x4022:
		if (FDSRegs[3] & 1) {
			IRQa = V & 0x03;
			if (IRQa & IRQ_Enabled) {
				IRQCount = IRQLatch;
			}
			else {
				X6502_IRQEnd(FCEU_IQEXT);
			}
		}
		break;
	case 0x4023:
		if (!(V & 0x01)) {
			IRQa &= ~IRQ_Enabled;
			X6502_IRQEnd(FCEU_IQEXT);
			X6502_IRQEnd(FCEU_IQEXT2);
		}
		break;
	case 0x4024:
		if (mapperFDS_diskinsert && ~mapperFDS_control & 0x04) {

			if (mapperFDS_diskaccess == 0) {
				mapperFDS_diskaccess = 1;
				break;
			}

			switch (mapperFDS_block) {
			case DSK_FILEHDR:
				if (mapperFDS_diskaddr < mapperFDS_blocklen) {
					fds_disk() = V;
					DiskWritten = 1;
					switch (mapperFDS_diskaddr) {
					case 13: mapperFDS_filesize = V; break;
					case 14:
						mapperFDS_filesize |= V << 8;
						//char fdsfile[10];
						//strncpy(fdsfile, (char*)&diskdata[InDisk][mapperFDS_blockstart + 3], 8);
						//printf("Write file: %s (size: %d)\n"), fdsfile, mapperFDS_filesize);
						break;
					}
					mapperFDS_diskaddr++;
				}
				break;
			default:
				if (mapperFDS_diskaddr < mapperFDS_blocklen) {
					fds_disk() = V;
					DiskWritten = 1;
					mapperFDS_diskaddr++;
				}
				break;
			}

		}
		break;
	case 0x4025:
		X6502_IRQEnd(FCEU_IQEXT2);
		if (mapperFDS_diskinsert) {
			if (V & 0x40 && ~mapperFDS_control & 0x40) {
				mapperFDS_diskaccess = 0;

				DiskSeekIRQ = 150;

				// blockstart  - address of block on disk
				// diskaddr    - address relative to blockstart
				// _block -> _blockID ?
				mapperFDS_blockstart += mapperFDS_diskaddr;
				mapperFDS_diskaddr = 0;

				mapperFDS_block++;
				if (mapperFDS_block > DSK_FILEDATA)
					mapperFDS_block = DSK_FILEHDR;

				switch (mapperFDS_block) {
				case DSK_VOLUME:
					mapperFDS_blocklen = 0x38;
					break;
				case DSK_FILECNT:
					mapperFDS_blocklen = 0x02;
					break;
				case DSK_FILEHDR:
					mapperFDS_blocklen = 0x10;
					break;
				case DSK_FILEDATA:		 // <blockid><filedata>
					mapperFDS_blocklen = 0x01 + mapperFDS_filesize;
					break;
				}
			}

			if (V & 0x02) { // transfer reset
				mapperFDS_block = DSK_INIT;
				mapperFDS_blockstart = 0;
				mapperFDS_blocklen = 0;
				mapperFDS_diskaddr = 0;
				DiskSeekIRQ = 150;
			}
			if (V & 0x40) { // turn on motor
				DiskSeekIRQ = 150;
			}
		}
		mapperFDS_control = V;
		setmirror(((V >> 3) & 1) ^ 1);
		break;
	}
	FDSRegs[A & 7] = V;
}

static void FreeFDSMemory(void) {
	int x;

	for (x = 0; x < TotalSides; x++)
		if (diskdata[x]) {
			free(diskdata[x]);
			diskdata[x] = 0;
		}
}

static int SubLoad(FCEUFILE* fp) {
	struct md5_context md5;
	uint8 header[16];
	int x;

	FCEU_fseek(fp, 0, SEEK_SET);
	FCEU_fread(header, 16, 1, fp);

	if (memcmp(header, "FDS\x1a", 4)) {
		if (!(memcmp(header + 1, "*NINTENDO-HVC*", 14))) {
			long t;
			t = FCEU_fgetsize(fp);
			if (t < 65500)
				t = 65500;
			TotalSides = t / 65500;
			FCEU_fseek(fp, 0, SEEK_SET);
		}
		else
			return 1;
	}
	else
		TotalSides = header[4];

	md5_starts(&md5);

	if (TotalSides > 8) TotalSides = 8;
	if (TotalSides < 1) TotalSides = 1;

	for (x = 0; x < TotalSides; x++) {
		if ((diskdata[x] = (uint8*)FCEU_malloc(65500)) == NULL) return 2;
		FCEU_fread(diskdata[x], 1, 65500, fp);
		md5_update(&md5, diskdata[x], 65500);
	}
	md5_finish(&md5, GameInfo->MD5.data);
	return 0;
}

static void PreSave(void) {
	int x;
	for (x = 0; x < TotalSides; x++) {
		int b;
		for (b = 0; b < 65500; b++)
			diskdata[x][b] ^= diskdatao[x][b];
	}
}

static void PostSave(void) {
	int x;
	for (x = 0; x < TotalSides; x++) {
		int b;
		for (b = 0; b < 65500; b++)
			diskdata[x][b] ^= diskdatao[x][b];
	}
}

int FDSLoad(const char* name, FCEUFILE* fp) {
	FILE* zp;
	int x;

	// try to load FDS image first
	FreeFDSMemory();
	int load_result = SubLoad(fp);
	switch (load_result)
	{
	case 1:
		FreeFDSMemory();
		return LOADER_INVALID_FORMAT;
	case 2:
		FreeFDSMemory();
		FCEU_PrintError("Unable to allocate memory.");
		return LOADER_HANDLED_ERROR;
	}

	// load FDS BIOS next
	char* fn = strdup(FCEU_MakeFName(FCEUMKF_FDSROM, 0, 0).c_str());

	if (!(zp = FCEUD_UTF8fopen(fn, "rb"))) {
		FCEU_PrintError("FDS BIOS ROM image missing: %s", FCEU_MakeFName(FCEUMKF_FDSROM, 0, 0).c_str());
		free(fn);
		FreeFDSMemory();
		return LOADER_HANDLED_ERROR;
	}
	free(fn);

	fseek(zp, 0L, SEEK_END);
	if (ftell(zp) != 8192) {
		fclose(zp);
		FreeFDSMemory();
		FCEU_PrintError("FDS BIOS ROM image incompatible: %s", FCEU_MakeFName(FCEUMKF_FDSROM, 0, 0).c_str());
		return LOADER_HANDLED_ERROR;
	}
	fseek(zp, 0L, SEEK_SET);

	ResetCartMapping();

	if (FDSBIOS)
		free(FDSBIOS);
	FDSBIOS = NULL;
	if (FDSRAM)
		free(FDSRAM);
	FDSRAM = NULL;
	if (CHRRAM)
		free(CHRRAM);
	CHRRAM = NULL;

	FDSBIOSsize = 8192;
	FDSBIOS = (uint8*)FCEU_gmalloc(FDSBIOSsize);
	SetupCartPRGMapping(0, FDSBIOS, FDSBIOSsize, 0);

	if (fread(FDSBIOS, 1, FDSBIOSsize, zp) != FDSBIOSsize) {
		if (FDSBIOS)
			free(FDSBIOS);
		FDSBIOS = NULL;
		fclose(zp);
		FreeFDSMemory();
		FCEU_PrintError("Error reading FDS BIOS ROM image.");
		return LOADER_HANDLED_ERROR;
	}

	fclose(zp);

	if (!disableBatteryLoading) {
		FCEUFILE* tp;
		char* fn = strdup(FCEU_MakeFName(FCEUMKF_FDS, 0, 0).c_str());

		int x;
		for (x = 0; x < TotalSides; x++) {
			diskdatao[x] = (uint8*)FCEU_malloc(65500);
			memcpy(diskdatao[x], diskdata[x], 65500);
		}

		if ((tp = FCEU_fopen(fn, 0, "rb", 0))) {
			FCEU_printf("Disk was written. Auxiliary FDS file open \"%s\".\n", fn);
			FreeFDSMemory();
			if (SubLoad(tp)) {
				FCEU_PrintError("Error reading auxiliary FDS file.");
				if (FDSBIOS)
					free(FDSBIOS);
				FDSBIOS = NULL;
				free(fn);
				FreeFDSMemory();
				return LOADER_HANDLED_ERROR;
			}
			FCEU_fclose(tp);
			DiskWritten = 1;  /* For save state handling. */
		}
		free(fn);
	}

	strcpy(LoadedRomFName, name); //For the debugger list

	GameInfo->type = GIT_FDS;
	GameInterface = FDSGI;
	isFDS = true;

	SelectDisk = 0;
	InDisk = 255;

	ResetExState(PreSave, PostSave);
	FDSSoundStateAdd();

	for (x = 0; x < TotalSides; x++) {
		char temp[8];
		snprintf(temp, sizeof(temp), "DDT%d", x);
		AddExState(diskdata[x], 65500, 0, temp);
	}

	AddExState(FDSRegs, sizeof(FDSRegs), 0, "FREG");
	AddExState(&IRQCount, 4, 1, "IRQC");
	AddExState(&IRQLatch, 4, 1, "IQL1");
	AddExState(&IRQa, 1, 0, "IRQA");
	AddExState(&writeskip, 1, 0, "WSKI");
	AddExState(&DiskPtr, 4, 1, "DPTR");
	AddExState(&DiskSeekIRQ, 4, 1, "DSIR");
	AddExState(&SelectDisk, 1, 0, "SELD");
	AddExState(&InDisk, 1, 0, "INDI");
	AddExState(&DiskWritten, 1, 0, "DSKW");
	AddExState(&mapperFDS_control, 1, 0, "CTRG");
	AddExState(&mapperFDS_filesize, 2, 1, "FLSZ");
	AddExState(&mapperFDS_block, 1, 0, "BLCK");
	AddExState(&mapperFDS_blockstart, 2, 1, "BLKS");
	AddExState(&mapperFDS_blocklen, 2, 1, "BLKL");
	AddExState(&mapperFDS_diskaddr, 2, 1, "DADR");
	AddExState(&mapperFDS_diskaccess, 1, 0, "DACC");

	CHRRAMSize = 8192;
	CHRRAM = (uint8*)FCEU_gmalloc(CHRRAMSize);
	SetupCartCHRMapping(0, CHRRAM, CHRRAMSize, 1);
	AddExState(CHRRAM, CHRRAMSize, 0, "CHRR");

	FDSRAMSize = 32768;
	FDSRAM = (uint8*)FCEU_gmalloc(FDSRAMSize);
	SetupCartPRGMapping(1, FDSRAM, FDSRAMSize, 1);
	AddExState(FDSRAM, FDSRAMSize, 0, "FDSR");

	SetupCartMirroring(0, 0, 0);

	FCEU_printf(" Sides: %d\n\n", TotalSides);

	FCEUI_SetVidSystem(0);

	return LOADER_OK;
}

void FDSClose(void) {
	FILE* fp;
	int x;
	isFDS = false;

	if (!DiskWritten) return;

	const std::string& fn = FCEU_MakeFName(FCEUMKF_FDS, 0, 0);
	if (!(fp = FCEUD_UTF8fopen(fn.c_str(), "wb"))) {
		return;
	}

	for (x = 0; x < TotalSides; x++) {
		if (fwrite(diskdata[x], 1, 65500, fp) != 65500) {
			FCEU_PrintError("Error saving FDS image!");
			fclose(fp);
			return;
		}
	}

	for (x = 0; x < TotalSides; x++)
		if (diskdatao[x]) {
			free(diskdatao[x]);
			diskdatao[x] = 0;
		}

	FreeFDSMemory();
	if (FDSBIOS)
		free(FDSBIOS);
	FDSBIOS = NULL;
	if (FDSRAM)
		free(FDSRAM);
	FDSRAM = NULL;
	if (CHRRAM)
		free(CHRRAM);
	CHRRAM = NULL;
	fclose(fp);
}
void FDSSoundPower(void) {
	FDSSoundReset();
	FDSSoundStateAdd();
}