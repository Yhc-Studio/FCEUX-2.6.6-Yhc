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

/// \file
/// \brief implements a built-in NSF player.  This is a perk--not a part of the emu core

#include "types.h"
#include "x6502.h"
#include "fceu.h"
#include "video.h"
#include "sound.h"
#include "nsf.h"
#include "utils/general.h"
#include "utils/memory.h"
#include "file.h"
#include "fds.h"
#include "cart.h"
#include "ines.h"
#include "input.h"
#include "state.h"
#include "driver.h"
#ifdef _S9XLUA_H
#include "fceulua.h"
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static const int FIXED_EXWRAM_SIZE = 32768 + 8192;

#define NSF_MAX_TRACKS 256
#define NSF_TRACK_LABEL_LEN 64
#define NSF_AUTH_FIELD_LEN 64

static uint8 SongReload;
static int32 CurrentSong;
static uint32 NSFPlayFrames;
static uint32 NSFPlayRateRemainderUS;
static uint8 NSFPlayCallCount;
static uint8 NSFPaused;
static uint8 NSFStopped;
static uint8 NSFOutputMuteActive;
static int NSFSavedSoundVolume;

static uint8 NSFIsNSFe;
static char NSFTrackLabels[NSF_MAX_TRACKS][NSF_TRACK_LABEL_LEN];
static int32 NSFTrackTimesMs[NSF_MAX_TRACKS];
static int32 NSFTrackFadeMs[NSF_MAX_TRACKS];
static char NSFRipper[NSF_AUTH_FIELD_LEN];
static char NSFText[512];

static DECLFW(NSF_write);
static DECLFR(NSF_read);
static void NSFTrackAPUWrite(uint32 A, uint8 V);
static void NSFInstallAPUWriteHook(void);
static void NSFRemoveAPUWriteHook(void);
static void NSFResetAPUShadow(void);
static void NSFResetPlayTimer(void);
static void NSFResetPlayScheduler(void);
static uint8 NSFComputePlayCallsForFrame(void);
static uint32 NSFGetPlaybackSpeedUS(void);
static uint32 NSFGetFrameDurationUS(void);
static double NSFGetPlaybackRateHz(void);
static void NSFResetPlaybackControl(void);
static void NSFSetOutputMute(int mute);
static void NSFSilenceAllChannels(void);
static const char* NSFGetPlaybackStateName(void);
static void NSFResetNSFeMetadata(void);
static int NSFLoadNSFe(const char* name, FCEUFILE* fp);
static int NSFFinishLoad(const char* name);
static int NSFGetCurrentTrackTimeMs(void);
static int NSFGetCurrentTrackFadeMs(void);
static const char* NSFGetCurrentTrackLabel(void);
static void NSFResetWaveViewState(void);
static void NSFResetStarfield(void);
static void NSFTrackExpWrite(uint32 A, uint8 V);
static void NSFInstallExpWriteHook(void);
static void NSFRemoveExpWriteHook(void);
static void NSFInstallExpReadHook(void);
static void NSFRemoveExpReadHook(void);
static void NSFResetExpShadow(void);
static DECLFW(NSF_APUWrite);
static DECLFW(NSF_ExpWrite);
static DECLFR(NSF_ExpRead);

static int vismode = 1; //we cant consider this state, because the UI may be controlling it and wouldnt know we loadstated it

// Shadow copy of 2A03 APU registers used only by the NSF visual keyboard.
// The APU registers are write-only from the CPU side, so the visualizer tracks
// CPU writes by wrapping the existing $4000-$4017 write handlers.
// This avoids depending on newer X6502_MemHook APIs that older FCEUX branches may not have.
static uint8 NSFAPUShadow[0x18];
static uint8 NSFAPUHookInstalled = 0;
static void (*NSFOldAPUWrite[0x18])(uint32 A, uint8 V);

// Shadow copy of expansion chip registers used only by the NSF visual keyboard.
// These are not used for emulation; they only let the on-screen keyboard estimate
// pitches for VRC6, VRC7, FDS, MMC5, Namco 106/N163 and Sunsoft FME-7.
static uint8 NSFVRC6Shadow[3][3];
static uint8 NSFVRC7Regs[0x40];
static uint8 NSFVRC7Addr;
static uint8 NSFFDSShadow[0x0B];
static uint8 NSFFDSSeenShadow;
static uint16 NSFFDSLastFreqShadow;
static uint8 NSFMMC5Shadow[0x16];
static uint8 NSFMMC5RunningShadow;
static uint8 NSFMMC5SeenShadow;
static uint8 NSFN163RAM[0x80];
static uint8 NSFN163Addr;
static uint8 NSFFME7Regs[16];
static uint8 NSFFME7Addr;

#define NSF_EXP_HOOK_MAX 96
#define NSF_EXP_HOOK_WRITER_MAX 8
#define NSF_EXP_SOUND_MAX 6

typedef struct {
	uint32 addr;
	void (*restoreWrite)(uint32 A, uint8 V);
	void (*write[NSF_EXP_HOOK_WRITER_MAX])(uint32 A, uint8 V);
	int writeCount;
} NSF_EXP_HOOK;

static uint8 NSFExpHookInstalled = 0;
static int NSFExpHookCount = 0;
static NSF_EXP_HOOK NSFExpHooks[NSF_EXP_HOOK_MAX];

static uint8 NSFFDSReadHookInstalled = 0;
static uint8(*NSFOldFDSRead[0x4092 - 0x4040 + 1])(uint32 A);

// FCEUX's legacy expansion sound path exposes one global EXPSOUND slot.
// For combined-chip NSF/NSFe files, each chip init overwrites GameExpSound.
// Keep the individual chip callbacks here and install a small multiplexer
// after all requested expansion chips have been initialized.
static EXPSOUND NSFExpSoundList[NSF_EXP_SOUND_MAX];
static int NSFExpSoundCount = 0;

//mbg 7/31/06 todo - no reason this couldnt be assembled on the fly from actual asm source code. thatd be less obscure.
//here it is disassembled, for reference
/*
00:8000:8D F4 3F  STA $3FF4 = #$00
00:8003:A2 FF     LDX #$FF
00:8005:9A        TXS
00:8006:AD F0 3F  LDA $3FF0 = #$00
00:8009:F0 09     BEQ $8014
00:800B:AD F1 3F  LDA $3FF1 = #$00
00:800E:AE F3 3F  LDX $3FF3 = #$00
00:8011:20 00 00  JSR $0000
00:8014:A9 00     LDA #$00
00:8016:AA        TAX
00:8017:A8        TAY
00:8018:20 00 00  JSR $0000
00:801B:8D F5 EF  STA $EFF5 = #$FF
00:801E:90 FE     BCC $801E
00:8020:8D F3 3F  STA $3FF3 = #$00
00:8023:18        CLC
00:8024:90 FE     BCC $8024
*/
enum {
	NSFROM_INIT_ADDR_LO = 0x12,
	NSFROM_INIT_ADDR_HI = 0x13,
	NSFROM_PLAY_ADDR_LO = 0x21,
	NSFROM_PLAY_ADDR_HI = 0x22,
	NSFROM_RESET_ENTRY = 0x2D
};

static uint8 NSFROM[] =
{
	/* 0x00 - NMI */
	0x8D,0xF4,0x3F,       /* Stop play routine NMIs. */
	0xA2,0xFF,0x9A,       /* Initialize the stack pointer. */
	0xAD,0xF0,0x3F,       /* See if we need to init. */
	0xF0,0x09,            /* If 0, skip init and go to play counter. */

	0xAD,0xF1,0x3F,       /* Confirm and load A with song index. */
	0xAE,0xF3,0x3F,       /* Load X with PAL/NTSC byte. */

	0x20,0x00,0x00,       /* JSR to init routine. */

	/* 0x14 - play routine loop, count is supplied by $3FF2. */
	0xAD,0xF2,0x3F,       /* LDA $3FF2: number of play calls this video frame. */
	0xF0,0x0F,            /* BEQ done: allow 0 calls for sub-frame rate scheduling. */
	0x8D,0xF2,0x3F,       /* STA $3FF2: loop counter used by DEC below. */

	0xA9,0x00,            /* play_loop: LDA #$00 */
	0xAA,                 /* TAX */
	0xA8,                 /* TAY */
	0x20,0x00,0x00,       /* JSR to play routine. */
	0xCE,0xF2,0x3F,       /* DEC $3FF2 */
	0xD0,0xF4,            /* BNE play_loop */

	0x8D,0xF5,0x3F,       /* Start play routine NMIs. */
	0x90,0xFE,            /* Loopie time. */

	/* 0x2D - Reset */
	0x8D,0xF3,0x3F,       /* Init init NMIs. */
	0x18,
	0x90,0xFE             /* Loopie time. */
};

static DECLFR(NSFROMRead)
{
	return (NSFROM - 0x3800)[A];
}

static uint8 doreset = 0; //state
static uint8 NSFNMIFlags; //state
uint8* NSFDATA = 0; //configration, loaded from rom?
int NSFMaxBank; //configuration

static int32 NSFSize; //configuration
static uint8 BSon; //configuration
static uint8 BankCounter; //configuration

static uint16 PlayAddr; //configuration
static uint16 InitAddr; //configuration
static uint16 LoadAddr; //configuration

NSF_HEADER NSFHeader; //mbg merge 6/29/06 - needs to be global

void NSFMMC5_Close(void);
static uint8* ExWRAM = 0;

static void NSFResetAPUShadow(void)
{
	memset(NSFAPUShadow, 0, sizeof(NSFAPUShadow));
}

static void NSFResetPlayTimer(void)
{
	NSFPlayFrames = 0;
}

static void NSFResetPlayScheduler(void)
{
	NSFPlayRateRemainderUS = 0;
	NSFPlayCallCount = 0;
}

static uint16 NSFReadLE16Bytes(const uint8* p)
{
	return (uint16)(p[0] | (p[1] << 8));
}

static int NSFIsPALPlayback(void)
{
	return (NSFHeader.VideoSystem & 1) ? 1 : 0;
}

static uint32 NSFGetFrameDurationUS(void)
{
	return NSFIsPALPlayback() ? 20000U : 16666U;
}

static uint32 NSFGetPlaybackSpeedUS(void)
{
	uint32 speed;

	if (NSFIsPALPlayback())
		speed = NSFReadLE16Bytes(NSFHeader.PALspeed);
	else
		speed = NSFReadLE16Bytes(NSFHeader.NTSCspeed);

	/* NSF speed fields are microseconds between play calls.
	 * A zero value means the default rate for the selected video system.
	 */
	if (!speed)
		speed = NSFIsPALPlayback() ? 20000U : 16666U;

	return speed;
}

static double NSFGetPlaybackRateHz(void)
{
	uint32 speed = NSFGetPlaybackSpeedUS();
	if (!speed)
		return NSFIsPALPlayback() ? 50.0 : 60.0;
	return 1000000.0 / (double)speed;
}

static uint8 NSFComputePlayCallsForFrame(void)
{
	uint32 speed = NSFGetPlaybackSpeedUS();
	uint32 calls;

	if (!speed)
		speed = NSFGetFrameDurationUS();

	NSFPlayRateRemainderUS += NSFGetFrameDurationUS();
	calls = NSFPlayRateRemainderUS / speed;
	NSFPlayRateRemainderUS %= speed;

	/* Avoid locking the emulator on malformed NSF headers with tiny speed values.
	 * Real high-rate NSFs such as 120Hz/180Hz/240Hz stay well below this.
	 */
	if (calls > 16)
		calls = 16;

	return (uint8)calls;
}

static void NSFResetPlaybackControl(void)
{
	NSFSetOutputMute(0);
	NSFPaused = 0;
	NSFStopped = 0;
}

static void NSFSetOutputMute(int mute)
{
	/*
	 * Pause should behave like muting the speaker/output, not like clearing
	 * APU or expansion-chip registers.  Many NSF engines only rewrite channel
	 * registers when a note changes; clearing registers on pause would make
	 * sustained notes disappear until the next note event.
	 *
	 * This lightweight implementation uses FCEUX's master sound volume as an
	 * output gate for NSF pause.  It does not touch APU/expansion registers,
	 * so resume can continue from the same musical state.  B/Stop still uses
	 * explicit channel silencing.
	 */
	if (mute)
	{
		if (!NSFOutputMuteActive)
		{
			NSFSavedSoundVolume = FSettings.SoundVolume;
			FSettings.SoundVolume = 0;
			NSFOutputMuteActive = 1;
		}
	}
	else
	{
		if (NSFOutputMuteActive)
		{
			FSettings.SoundVolume = NSFSavedSoundVolume;
			NSFOutputMuteActive = 0;
		}
	}
}

static void NSFSafeWrite(uint32 A, uint8 V)
{
	if (BWrite[A])
		BWrite[A](A, V);
}

static void NSFSilenceAllChannels(void)
{
	int i;

	// Internal 2A03 channels.
	NSFSafeWrite(0x4015, 0x00);
	for (i = 0; i < 0x14; i++)
		NSFSafeWrite(0x4000 + i, 0x00);
	NSFSafeWrite(0x4015, 0x00);

	// Best-effort expansion-chip silencing for NSF pause/stop. These writes go
	// through the original sound handlers via the visualizer wrappers when installed.
	// SoundChip is a bitmask, so silence every active expansion chip.
	if (NSFHeader.SoundChip & 0x01)
	{
		NSFSafeWrite(0x9000, 0x00);
		NSFSafeWrite(0x9002, 0x00);
		NSFSafeWrite(0xA000, 0x00);
		NSFSafeWrite(0xA002, 0x00);
		NSFSafeWrite(0xB000, 0x00);
		NSFSafeWrite(0xB002, 0x00);
	}
	if (NSFHeader.SoundChip & 0x02)
	{
		for (i = 0; i < 6; i++)
		{
			NSFSafeWrite(0x9010, (uint8)(0x20 + i));
			NSFSafeWrite(0x9030, 0x00);
			NSFSafeWrite(0x9010, (uint8)(0x30 + i));
			NSFSafeWrite(0x9030, 0x0F);
		}
	}
	if (NSFHeader.SoundChip & 0x04)
	{
		NSFSafeWrite(0x4080, 0x80);
		NSFSafeWrite(0x4083, 0x80);
		NSFSafeWrite(0x4087, 0x80);
		NSFSafeWrite(0x4089, 0x80);
		NSFSafeWrite(0x408A, 0xE8);
	}
	if (NSFHeader.SoundChip & 0x08)
	{
		NSFSafeWrite(0x5000, 0x00);
		NSFSafeWrite(0x5004, 0x00);
		NSFSafeWrite(0x5015, 0x00);
	}
	if (NSFHeader.SoundChip & 0x10)
	{
		for (i = 0; i < 8; i++)
		{
			uint8 addr = (uint8)(0x7F - i * 8);
			NSFSafeWrite(0xF800, addr);
			NSFSafeWrite(0x4800, 0x00);
		}
	}
	if (NSFHeader.SoundChip & 0x20)
	{
		for (i = 0; i < 3; i++)
		{
			NSFSafeWrite(0xC000, (uint8)(8 + i));
			NSFSafeWrite(0xE000, 0x00);
		}
	}

	NSFResetAPUShadow();
	NSFResetExpShadow();
}

static void NSFTrackAPUWrite(uint32 A, uint8 V)
{
	if (A >= 0x4000 && A <= 0x4017)
		NSFAPUShadow[A - 0x4000] = V;
}

static DECLFW(NSF_APUWrite)
{
	void (*oldWrite)(uint32 A, uint8 V) = 0;

	NSFTrackAPUWrite(A, V);

	if (A >= 0x4000 && A <= 0x4017)
		oldWrite = NSFOldAPUWrite[A - 0x4000];

	if (oldWrite && oldWrite != NSF_APUWrite)
		oldWrite(A, V);
}

static void NSFInstallAPUWriteHook(void)
{
	if (!NSFAPUHookInstalled)
	{
		uint32 A;

		for (A = 0x4000; A <= 0x4017; A++)
			NSFOldAPUWrite[A - 0x4000] = BWrite[A];

		SetWriteHandler(0x4000, 0x4017, NSF_APUWrite);
		NSFAPUHookInstalled = 1;
	}
}

static void NSFRemoveAPUWriteHook(void)
{
	if (NSFAPUHookInstalled)
	{
		uint32 A;

		for (A = 0x4000; A <= 0x4017; A++)
			SetWriteHandler(A, A, NSFOldAPUWrite[A - 0x4000]);

		memset(NSFOldAPUWrite, 0, sizeof(NSFOldAPUWrite));
		NSFAPUHookInstalled = 0;
	}
}


static void NSFResetExpShadow(void)
{
	memset(NSFVRC6Shadow, 0, sizeof(NSFVRC6Shadow));
	memset(NSFVRC7Regs, 0, sizeof(NSFVRC7Regs));
	NSFVRC7Addr = 0;
	memset(NSFFDSShadow, 0, sizeof(NSFFDSShadow));
	NSFFDSSeenShadow = 0;
	NSFFDSLastFreqShadow = 0;
	memset(NSFMMC5Shadow, 0, sizeof(NSFMMC5Shadow));
	NSFMMC5RunningShadow = 0;
	NSFMMC5SeenShadow = 0;
	memset(NSFN163RAM, 0, sizeof(NSFN163RAM));
	NSFN163Addr = 0;
	memset(NSFFME7Regs, 0, sizeof(NSFFME7Regs));
	NSFFME7Addr = 0;
}


static uint16 NSFGetFDSRawFreqShadow(void)
{
	return (uint16)NSFFDSShadow[0x02] | (uint16)((NSFFDSShadow[0x03] & 0x0F) << 8);
}

static void NSFUpdateFDSLastFreqShadow(void)
{
	uint16 rawfreq = NSFGetFDSRawFreqShadow();
	if (rawfreq)
		NSFFDSLastFreqShadow = rawfreq;
}

static void NSFMarkFDSVisualSeen(void)
{
	NSFFDSSeenShadow = 1;
	NSFUpdateFDSLastFreqShadow();
}

static void NSFTrackExpWrite(uint32 A, uint8 V)
{
	// VRC6: use exact Mesen2-style register ranges.
	// $9003 is a VRC6 control register and is forwarded to the real handler,
	// but it is not part of the 3-channel visual shadow table.
	if ((A >= 0x9000 && A <= 0x9003) ||
		(A >= 0xA000 && A <= 0xA002) ||
		(A >= 0xB000 && A <= 0xB002))
	{
		int ch = -1;
		int reg = -1;

		if (A >= 0x9000 && A <= 0x9002)
		{
			ch = 0;
			reg = (int)(A - 0x9000);
		}
		else if (A >= 0xA000 && A <= 0xA002)
		{
			ch = 1;
			reg = (int)(A - 0xA000);
		}
		else if (A >= 0xB000 && A <= 0xB002)
		{
			ch = 2;
			reg = (int)(A - 0xB000);
		}

		if (ch >= 0 && reg >= 0)
			NSFVRC6Shadow[ch][reg] = V;
		return;
	}

	// VRC7 / YM2413: $9010 latches register, $9030 writes data.
	if (A == 0x9010)
	{
		NSFVRC7Addr = V & 0x3F;
		return;
	}
	if (A == 0x9030)
	{
		NSFVRC7Regs[NSFVRC7Addr & 0x3F] = V;
		return;
	}

	// FDS complete sound range.  Keep $4080-$408A for UI shadow,
	// but let the real handler receive $4040-$407F wave RAM as well.
	//
	// In three-chip mixes such as VRC6+FDS+MMC5, some initialization paths can
	// leave $4089 bit7 set or fail to mirror every FDS register write in a way
	// the visualizer can infer activity from.  Track a conservative "seen" flag
	// whenever the music writes actual FDS sound registers.  This affects only the
	// on-screen keyboard/level meter; the real FDS audio path is still handled by
	// FDSNSFWrite() below.
	if (A >= 0x4040 && A <= 0x4092)
	{
		if (A >= 0x4080 && A <= 0x408A)
		{
			uint32 r = A - 0x4080;
			NSFFDSShadow[r] = V;

			// $4089/$408A are mostly master/write-enable/envelope-speed controls and
			// are also written by the NSF bootstrap.  Do not let those two alone
			// create a false note; mark activity on wave/mod volume and frequency writes.
			if (r == 0x00 || r == 0x02 || r == 0x03 || r == 0x04 ||
				r == 0x05 || r == 0x06 || r == 0x07 || r == 0x08)
				NSFMarkFDSVisualSeen();
		}
		return;
	}

	// MMC5 square channels: $5000-$5007, enable/status at $5015.
	//
	// For mixed expansion NSFs, especially VRC6+FDS+MMC5, relying only on
	// $5015 is not robust enough for the visualizer.  FCEUX's MMC5 sound core
	// treats high-timer writes ($5003/$5007) as the channel-start/running latch,
	// while $5015 can clear the running bits.  Some NSF engines also leave $5015
	// stale or do not mirror it in the way a simple register display expects.
	// Track both the real running trigger and a conservative "seen channel data"
	// bit so the keyboard/meter can still show MMC5 activity in multi-chip mixes.
	if (A >= 0x5000 && A <= 0x5015)
	{
		uint32 r = A - 0x5000;
		NSFMMC5Shadow[r] = V;

		if (r <= 0x03)
			NSFMMC5SeenShadow |= 0x01;
		else if (r >= 0x04 && r <= 0x07)
			NSFMMC5SeenShadow |= 0x02;

		if (r == 0x03)
			NSFMMC5RunningShadow |= 0x01;
		else if (r == 0x07)
			NSFMMC5RunningShadow |= 0x02;
		else if (r == 0x15)
		{
			// $5015 only keeps enabled/running bits that are set.
			// Clearing it should also clear the visual fallback until the channel
			// receives new register/timer data.
			NSFMMC5RunningShadow &= (V & 0x03);
			NSFMMC5SeenShadow &= (V & 0x03);
		}

		return;
	}

	// Namco 106 / N163: $F800 selects internal address, $4800 writes data.
	if (A == 0xF800)
	{
		NSFN163Addr = V;
		return;
	}
	if (A == 0x4800)
	{
		NSFN163RAM[NSFN163Addr & 0x7F] = V;
		if (NSFN163Addr & 0x80)
			NSFN163Addr = (uint8)((NSFN163Addr + 1) | 0x80);
		return;
	}

	// Sunsoft FME-7 / 5B: $C000 latches PSG register, $E000 writes data.
	if (A == 0xC000)
	{
		NSFFME7Addr = V & 0x0F;
		return;
	}
	if (A == 0xE000)
	{
		NSFFME7Regs[NSFFME7Addr & 0x0F] = V;
		return;
	}
}

static void NSFResetExpSoundMux(void)
{
	FCEU_NSFClearExpSounds();
}

static int NSFExpSoundHasCallbacks(const EXPSOUND* es)
{
	return es && (es->Fill || es->NeoFill || es->HiFill || es->HiSync || es->RChange || es->Kill);
}

static void NSFPrepareCaptureExpSound(void)
{
	memset(&GameExpSound, 0, sizeof(GameExpSound));
}

static void NSFCaptureCurrentExpSound(void)
{
	if (!NSFExpSoundHasCallbacks(&GameExpSound))
		return;

	FCEU_NSFAddExpSound(&GameExpSound);
	memset(&GameExpSound, 0, sizeof(GameExpSound));
}

static void NSFCommitExpSoundMux(void)
{
	FCEU_NSFCommitExpSoundMux();
}

static NSF_EXP_HOOK* NSFGetOrCreateExpHook(uint32 A)
{
	int i;

	for (i = 0; i < NSFExpHookCount; i++)
		if (NSFExpHooks[i].addr == A)
			return &NSFExpHooks[i];

	if (NSFExpHookCount >= NSF_EXP_HOOK_MAX)
		return 0;

	NSFExpHooks[NSFExpHookCount].addr = A;
	NSFExpHooks[NSFExpHookCount].restoreWrite = 0;
	NSFExpHooks[NSFExpHookCount].writeCount = 0;
	memset(NSFExpHooks[NSFExpHookCount].write, 0, sizeof(NSFExpHooks[NSFExpHookCount].write));
	return &NSFExpHooks[NSFExpHookCount++];
}

static void NSFCaptureOneExpWriteHook(uint32 A)
{
	NSF_EXP_HOOK* hook;
	void (*oldWrite)(uint32 A, uint8 V);
	int i;

	oldWrite = BWrite[A];
	if (!oldWrite || oldWrite == NSF_ExpWrite)
		return;

	hook = NSFGetOrCreateExpHook(A);
	if (!hook)
		return;

	for (i = 0; i < hook->writeCount; i++)
		if (hook->write[i] == oldWrite)
			return;

	if (hook->writeCount < NSF_EXP_HOOK_WRITER_MAX)
		hook->write[hook->writeCount++] = oldWrite;
}

static void NSFCaptureVRC6WriteHooks(void)
{
	uint32 A;
	for (A = 0x9000; A <= 0x9003; A++) NSFCaptureOneExpWriteHook(A);
	for (A = 0xA000; A <= 0xA002; A++) NSFCaptureOneExpWriteHook(A);
	for (A = 0xB000; A <= 0xB002; A++) NSFCaptureOneExpWriteHook(A);
}

static void NSFCaptureVRC7WriteHooks(void)
{
	NSFCaptureOneExpWriteHook(0x9010);
	NSFCaptureOneExpWriteHook(0x9030);
}

static void NSFCaptureFDSWriteHooks(void)
{
	uint32 A;
	for (A = 0x4040; A <= 0x4092; A++)
	{
		NSF_EXP_HOOK* hook;
		NSFCaptureOneExpWriteHook(A);

		/*
		 * Always keep a visual/direct FDS hook, even if the FDS core did not expose
		 * a previous write callback at capture time.  This is important after the
		 * Step24 FDS direct/modulation changes and in multi-expansion NSFs, where
		 * FDS sound may still be produced through FDSNSFWrite() but the UI shadow
		 * would otherwise miss the register writes.
		 */
		hook = NSFGetOrCreateExpHook(A);
		(void)hook;
	}
}

static void NSFCaptureMMC5WriteHooks(void)
{
	uint32 A;
	for (A = 0x5000; A <= 0x5015; A++)
	{
		NSF_EXP_HOOK* hook;
		NSFCaptureOneExpWriteHook(A);

		/*
		 * Keep a visual hook even if this address did not expose a previous write
		 * callback at capture time.  This is harmless for normal MMC5 because the
		 * real callback is captured when present, and it makes the NSF keyboard/meter
		 * more robust in unusual multi-expansion initialization orders.
		 */
		hook = NSFGetOrCreateExpHook(A);
		(void)hook;
	}
}

static void NSFCaptureN163WriteHooks(void)
{
	NSFCaptureOneExpWriteHook(0x4800);
	NSFCaptureOneExpWriteHook(0xF800);
}

static void NSFCaptureFME7WriteHooks(void)
{
	NSFCaptureOneExpWriteHook(0xC000);
	NSFCaptureOneExpWriteHook(0xE000);
}

static void NSFResetExpWriteMux(void)
{
	memset(NSFExpHooks, 0, sizeof(NSFExpHooks));
	NSFExpHookCount = 0;
	NSFExpHookInstalled = 0;
}

static void NSFInstallExpWriteHook(void)
{
	int i;

	if (NSFExpHookInstalled)
		return;

	for (i = 0; i < NSFExpHookCount; i++)
	{
		NSFExpHooks[i].restoreWrite = BWrite[NSFExpHooks[i].addr];
		SetWriteHandler(NSFExpHooks[i].addr, NSFExpHooks[i].addr, NSF_ExpWrite);
	}

	NSFExpHookInstalled = 1;
}

static void NSFRemoveExpWriteHook(void)
{
	if (NSFExpHookInstalled)
	{
		int i;
		for (i = 0; i < NSFExpHookCount; i++)
			SetWriteHandler(NSFExpHooks[i].addr, NSFExpHooks[i].addr, NSFExpHooks[i].restoreWrite);
	}

	NSFResetExpWriteMux();
}

static DECLFW(NSF_ExpWrite)
{
	int i;
	int j;

	NSFTrackExpWrite(A, V);

	if ((NSFHeader.SoundChip & 0x04) && A >= 0x4040 && A <= 0x4092)
	{
		FDSNSFWrite(A, V);
		return;
	}

	for (i = 0; i < NSFExpHookCount; i++)
	{
		if (NSFExpHooks[i].addr != A)
			continue;

		for (j = 0; j < NSFExpHooks[i].writeCount; j++)
		{
			void (*oldWrite)(uint32 A, uint8 V) = NSFExpHooks[i].write[j];
			if (oldWrite && oldWrite != NSF_ExpWrite)
				oldWrite(A, V);
		}
		return;
	}
}

static void NSFInstallExpReadHook(void)
{
	uint32 A;

	if (NSFFDSReadHookInstalled)
		return;

	if (!(NSFHeader.SoundChip & 0x04))
		return;

	for (A = 0x4040; A <= 0x4092; A++)
	{
		NSFOldFDSRead[A - 0x4040] = ARead[A];
		SetReadHandler(A, A, NSF_ExpRead);
	}

	NSFFDSReadHookInstalled = 1;
}

static void NSFRemoveExpReadHook(void)
{
	uint32 A;

	if (!NSFFDSReadHookInstalled)
		return;

	for (A = 0x4040; A <= 0x4092; A++)
		SetReadHandler(A, A, NSFOldFDSRead[A - 0x4040]);

	memset(NSFOldFDSRead, 0, sizeof(NSFOldFDSRead));
	NSFFDSReadHookInstalled = 0;
}

static DECLFR(NSF_ExpRead)
{
	if ((NSFHeader.SoundChip & 0x04) && A >= 0x4040 && A <= 0x4092)
		return FDSNSFRead(A);

	if (A >= 0x4040 && A <= 0x4092)
	{
		uint8(*oldRead)(uint32 A) = NSFOldFDSRead[A - 0x4040];
		if (oldRead && oldRead != NSF_ExpRead)
			return oldRead(A);
	}

	return X.DB;
}

void NSFGI(GI h)
{
	switch (h)
	{
	case GI_CLOSE:
		NSFRemoveExpReadHook();
		NSFRemoveExpWriteHook();
		NSFRemoveAPUWriteHook();
		/* Leave NSF direct mode before the next normal FDS disk/game is loaded.
		 * While NSF direct mode is enabled, fds.cpp deliberately does not install
		 * its own $4040-$4092 handlers; nsf.cpp owns those addresses and forwards
		 * writes through FDSNSFWrite().
		 */
		FDSNSFSetDirectMode(0);
		NSFResetExpSoundMux();
		memset(&GameExpSound, 0, sizeof(GameExpSound));
		if (NSFDATA) { free(NSFDATA); NSFDATA = 0; }
		if (ExWRAM) { free(ExWRAM); ExWRAM = 0; }
		// SoundChip is a bitmask, so handle every active expansion chip.
		if (NSFHeader.SoundChip & 1) {
			//   NSFVRC6_Init();
		}
		if (NSFHeader.SoundChip & 2) {
			//   NSFVRC7_Init();
		}
		if (NSFHeader.SoundChip & 4) {
			//   FDSSoundReset();
		}
		if (NSFHeader.SoundChip & 8) {
			NSFMMC5_Close();
		}
		if (NSFHeader.SoundChip & 0x10) {
			//   NSFN106_Init();
		}
		if (NSFHeader.SoundChip & 0x20) {
			//   NSFAY_Init();
		}
		break;
	case GI_RESETM2:
	case GI_POWER:
		NSF_init();
		break;
	default:
		//Unhandled cases
		break;
	}
}

// First 32KB is reserved for sound chip emulation in the iNES mapper code.

static INLINE void BANKSET(uint32 A, uint32 bank)
{
	bank &= NSFMaxBank;
	if (NSFHeader.SoundChip & 4)
		memcpy(ExWRAM + (A - 0x6000), NSFDATA + (bank << 12), 4096);
	else
		setprg4(A, bank);
}

static uint32 NSFReadLE32Bytes(const uint8* p)
{
	return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
}

static int32 NSFReadSLE32Bytes(const uint8* p)
{
	return (int32)NSFReadLE32Bytes(p);
}

static void NSFCopyCString(char* dst, int dstSize, const uint8* src, int srcLen)
{
	int n;

	if (!dst || dstSize <= 0)
		return;

	dst[0] = 0;
	if (!src || srcLen <= 0)
		return;

	for (n = 0; n < dstSize - 1 && n < srcLen; n++)
	{
		if (src[n] == 0)
			break;
		dst[n] = (char)src[n];
	}
	dst[n] = 0;
}

static const uint8* NSFNextCString(const uint8* p, const uint8* end, char* out, int outSize)
{
	const uint8* start = p;

	if (!p || p >= end)
	{
		if (out && outSize > 0)
			out[0] = 0;
		return end;
	}

	while (p < end && *p)
		p++;

	NSFCopyCString(out, outSize, start, (int)(p - start));

	if (p < end && *p == 0)
		p++;

	return p;
}

static void NSFResetNSFeMetadata(void)
{
	int i;

	NSFIsNSFe = 0;
	for (i = 0; i < NSF_MAX_TRACKS; i++)
	{
		NSFTrackLabels[i][0] = 0;
		NSFTrackTimesMs[i] = -1;
		NSFTrackFadeMs[i] = -1;
	}
	NSFRipper[0] = 0;
	NSFText[0] = 0;
}

static int NSFGetCurrentTrackIndex(void)
{
	int idx = CurrentSong - 1;
	if (idx < 0 || idx >= NSF_MAX_TRACKS)
		return -1;
	return idx;
}

static const char* NSFGetCurrentTrackLabel(void)
{
	int idx = NSFGetCurrentTrackIndex();
	if (idx < 0)
		return "";
	return NSFTrackLabels[idx];
}

static int NSFGetCurrentTrackTimeMs(void)
{
	int idx = NSFGetCurrentTrackIndex();
	if (idx < 0)
		return -1;
	return NSFTrackTimesMs[idx];
}

static int NSFGetCurrentTrackFadeMs(void)
{
	int idx = NSFGetCurrentTrackIndex();
	if (idx < 0)
		return -1;
	return NSFTrackFadeMs[idx];
}

static int NSFIsKnownNSFeChunk(const char* id)
{
	return !memcmp(id, "INFO", 4) || !memcmp(id, "DATA", 4) || !memcmp(id, "NEND", 4) ||
		!memcmp(id, "BANK", 4) || !memcmp(id, "RATE", 4) || !memcmp(id, "NSF2", 4) ||
		!memcmp(id, "VRC7", 4) || !memcmp(id, "plst", 4) || !memcmp(id, "psfx", 4) ||
		!memcmp(id, "time", 4) || !memcmp(id, "fade", 4) || !memcmp(id, "tlbl", 4) ||
		!memcmp(id, "taut", 4) || !memcmp(id, "auth", 4) || !memcmp(id, "text", 4) ||
		!memcmp(id, "mixe", 4) || !memcmp(id, "regn", 4);
}

static int NSFPrepareDataImage(const uint8* data, uint32 dataSize)
{
	if (LoadAddr < 0x6000)
	{
		FCEUD_PrintError("Invalid load address.");
		return LOADER_HANDLED_ERROR;
	}

	NSFSize = (int32)dataSize;
	NSFMaxBank = ((NSFSize + (LoadAddr & 0xfff) + 4095) / 4096);
	NSFMaxBank = PRGsize[0] = uppow2(NSFMaxBank);

	if (!(NSFDATA = (uint8*)FCEU_malloc(NSFMaxBank * 4096)))
	{
		FCEU_PrintError("Unable to allocate memory.");
		return LOADER_HANDLED_ERROR;
	}

	memset(NSFDATA, 0x00, NSFMaxBank * 4096);
	if (data && dataSize)
		memcpy(NSFDATA + (LoadAddr & 0xfff), data, dataSize);

	NSFMaxBank--;
	return LOADER_OK;
}

static const char* NSFGetExpansionHardwareName(int bit)
{
	static const char* tab[6] = { "Konami VRCVI", "Konami VRCVII", "Nintendo FDS", "Nintendo MMC5", "Namco 106", "Sunsoft FME-07" };

	if (bit < 0 || bit >= 6)
		return "Unknown";
	return tab[bit];
}

static void NSFPrintExpansionHardware(void)
{
	char line[192];
	int x;
	int first = 1;

	if (!NSFHeader.SoundChip)
		return;

	line[0] = 0;
	for (x = 0; x < 6; x++)
	{
		if (!(NSFHeader.SoundChip & (1 << x)))
			continue;

		if (!first)
			strncat(line, " + ", sizeof(line) - strlen(line) - 1);
		strncat(line, NSFGetExpansionHardwareName(x), sizeof(line) - strlen(line) - 1);
		first = 0;
	}

	if (line[0])
		FCEU_printf(" Expansion hardware:  %s\n", line);
}

static int NSFFinishLoad(const char* name)
{
	int x;

	BSon = 0;
	for (x = 0; x < 8; x++)
	{
		BSon |= NSFHeader.BankSwitch[x];
	}

	if (BSon == 0)
	{
		BankCounter = 0x00;

		if ((NSFHeader.LoadAddressHigh & 0x70) >= 0x70)
		{
			//Ice Climber, and other F000 base address tunes need this
			BSon = 0xFF;
		}
		else {
			for (x = (NSFHeader.LoadAddressHigh & 0x70) / 0x10; x < 8; x++)
			{
				NSFHeader.BankSwitch[x] = BankCounter;
				BankCounter += 0x01;
			}
			BSon = 0;
		}
	}

	for (x = 0; x < 8; x++)
		BSon |= NSFHeader.BankSwitch[x];

	GameInfo->type = GIT_NSF;
	GameInfo->input[0] = GameInfo->input[1] = SI_GAMEPAD;
	GameInfo->cspecial = SIS_NSF;

	NSFROM[NSFROM_INIT_ADDR_LO] = InitAddr & 0xFF;
	NSFROM[NSFROM_INIT_ADDR_HI] = InitAddr >> 8;
	NSFROM[NSFROM_PLAY_ADDR_LO] = PlayAddr & 0xFF;
	NSFROM[NSFROM_PLAY_ADDR_HI] = PlayAddr >> 8;

	if (NSFHeader.VideoSystem == 0)
		GameInfo->vidsys = GIV_NTSC;
	else if (NSFHeader.VideoSystem == 1)
		GameInfo->vidsys = GIV_PAL;

	GameInterface = NSFGI;

	strcpy(LoadedRomFName, name);

	FCEU_printf("\n%s Loaded.\nFile information:\n", NSFIsNSFe ? "NSFe" : "NSF");
	FCEU_printf(" Name:       %s\n Artist:     %s\n Copyright:  %s\n", NSFHeader.SongName, NSFHeader.Artist, NSFHeader.Copyright);
	if (NSFIsNSFe && NSFRipper[0])
		FCEU_printf(" Ripper:     %s\n", NSFRipper);
	FCEU_printf("\n");

	NSFPrintExpansionHardware();
	if (BSon)
		FCEU_printf(" Bank-switched.\n");
	FCEU_printf(" Load address:  $%04x\n Init address:  $%04x\n Play address:  $%04x\n", LoadAddr, InitAddr, PlayAddr);
	FCEU_printf(" %s\n", (NSFHeader.VideoSystem & 1) ? "PAL" : "NTSC");
	FCEU_printf(" Playback rate: %.2f Hz\n", NSFGetPlaybackRateHz());
	FCEU_printf(" Starting song:  %d / %d\n\n", NSFHeader.StartingSong, NSFHeader.TotalSongs);

	//choose exwram size and allocate
	int exwram_size = 8192;
	if (NSFHeader.SoundChip & 4)
		exwram_size = 32768 + 8192;
	//lets just always use this size, for savestate simplicity
	exwram_size = FIXED_EXWRAM_SIZE;
	ExWRAM = (uint8*)FCEU_gmalloc(exwram_size);

	FCEUI_SetVidSystem(NSFHeader.VideoSystem);

	return LOADER_OK;
}

static int NSFLoadStandard(const char* name, FCEUFILE* fp)
{
	uint8* rawData;
	uint32 rawSize;
	int ret;

	NSFResetNSFeMetadata();

	FCEU_fseek(fp, 0, SEEK_SET);
	FCEU_fread(&NSFHeader, 1, 0x80, fp);
	if (memcmp(NSFHeader.ID, "NESM\x1a", 5))
		return LOADER_INVALID_FORMAT;
	NSFHeader.SongName[31] = NSFHeader.Artist[31] = NSFHeader.Copyright[31] = 0;

	LoadAddr = NSFHeader.LoadAddressLow;
	LoadAddr |= NSFHeader.LoadAddressHigh << 8;

	InitAddr = NSFHeader.InitAddressLow;
	InitAddr |= NSFHeader.InitAddressHigh << 8;

	PlayAddr = NSFHeader.PlayAddressLow;
	PlayAddr |= NSFHeader.PlayAddressHigh << 8;

	rawSize = (uint32)(FCEU_fgetsize(fp) - 0x80);
	rawData = (uint8*)FCEU_malloc(rawSize ? rawSize : 1);
	if (!rawData)
	{
		FCEU_PrintError("Unable to allocate memory.");
		return LOADER_HANDLED_ERROR;
	}

	FCEU_fseek(fp, 0x80, SEEK_SET);
	if (rawSize)
		FCEU_fread(rawData, 1, rawSize, fp);

	ret = NSFPrepareDataImage(rawData, rawSize);
	free(rawData);
	if (ret != LOADER_OK)
		return ret;

	return NSFFinishLoad(name);
}

static int NSFLoadNSFe(const char* name, FCEUFILE* fp)
{
	int32 fileSize;
	uint8* fileData = 0;
	uint8* dataChunk = 0;
	uint32 dataChunkSize = 0;
	uint32 pos;
	int gotInfo = 0;
	int gotData = 0;
	int gotNend = 0;
	int ret;

	NSFResetNSFeMetadata();
	NSFIsNSFe = 1;
	memset(&NSFHeader, 0, sizeof(NSFHeader));
	memcpy(NSFHeader.ID, "NESM\x1a", 5);
	NSFHeader.Version = 1;
	NSFHeader.TotalSongs = 1;
	NSFHeader.StartingSong = 1;
	NSFHeader.NTSCspeed[0] = 0;
	NSFHeader.NTSCspeed[1] = 0;
	NSFHeader.PALspeed[0] = 0;
	NSFHeader.PALspeed[1] = 0;

	fileSize = FCEU_fgetsize(fp);
	if (fileSize < 12)
		return LOADER_INVALID_FORMAT;

	fileData = (uint8*)FCEU_malloc(fileSize);
	if (!fileData)
	{
		FCEU_PrintError("Unable to allocate memory.");
		return LOADER_HANDLED_ERROR;
	}

	FCEU_fseek(fp, 0, SEEK_SET);
	FCEU_fread(fileData, 1, fileSize, fp);

	if (memcmp(fileData, "NSFE", 4))
	{
		free(fileData);
		return LOADER_INVALID_FORMAT;
	}

	pos = 4;
	while (pos + 8 <= (uint32)fileSize)
	{
		uint32 len = NSFReadLE32Bytes(fileData + pos);
		char id[5];
		const uint8* chunk;
		uint32 i;

		memcpy(id, fileData + pos + 4, 4);
		id[4] = 0;
		pos += 8;

		if (pos + len > (uint32)fileSize)
		{
			FCEUD_PrintError("Truncated NSFe chunk.");
			free(fileData);
			if (dataChunk) free(dataChunk);
			return LOADER_HANDLED_ERROR;
		}

		chunk = fileData + pos;

		if (!memcmp(id, "NEND", 4))
		{
			gotNend = 1;
			break;
		}
		else if (!memcmp(id, "INFO", 4))
		{
			if (len < 9)
			{
				FCEUD_PrintError("Invalid NSFe INFO chunk.");
				free(fileData);
				if (dataChunk) free(dataChunk);
				return LOADER_HANDLED_ERROR;
			}

			LoadAddr = NSFReadLE16Bytes(chunk + 0);
			InitAddr = NSFReadLE16Bytes(chunk + 2);
			PlayAddr = NSFReadLE16Bytes(chunk + 4);
			NSFHeader.LoadAddressLow = LoadAddr & 0xFF;
			NSFHeader.LoadAddressHigh = LoadAddr >> 8;
			NSFHeader.InitAddressLow = InitAddr & 0xFF;
			NSFHeader.InitAddressHigh = InitAddr >> 8;
			NSFHeader.PlayAddressLow = PlayAddr & 0xFF;
			NSFHeader.PlayAddressHigh = PlayAddr >> 8;
			NSFHeader.VideoSystem = chunk[6] & 0x03;
			NSFHeader.SoundChip = chunk[7] & 0x3F;
			NSFHeader.TotalSongs = chunk[8] ? chunk[8] : 1;
			NSFHeader.StartingSong = (len >= 10) ? (uint8)(chunk[9] + 1) : 1; // NSFe is 0-based; FCEUX UI/control is 1-based.
			gotInfo = 1;
		}
		else if (!memcmp(id, "DATA", 4))
		{
			if (!gotInfo)
			{
				FCEUD_PrintError("NSFe DATA chunk appeared before INFO.");
				free(fileData);
				if (dataChunk) free(dataChunk);
				return LOADER_HANDLED_ERROR;
			}

			if (dataChunk)
				free(dataChunk);
			dataChunk = (uint8*)FCEU_malloc(len ? len : 1);
			if (!dataChunk)
			{
				FCEU_PrintError("Unable to allocate memory.");
				free(fileData);
				return LOADER_HANDLED_ERROR;
			}
			if (len)
				memcpy(dataChunk, chunk, len);
			dataChunkSize = len;
			gotData = 1;
		}
		else if (!memcmp(id, "BANK", 4))
		{
			memset(NSFHeader.BankSwitch, 0, sizeof(NSFHeader.BankSwitch));
			for (i = 0; i < 8 && i < len; i++)
				NSFHeader.BankSwitch[i] = chunk[i];
		}
		else if (!memcmp(id, "RATE", 4))
		{
			if (len >= 2)
			{
				NSFHeader.NTSCspeed[0] = chunk[0];
				NSFHeader.NTSCspeed[1] = chunk[1];
			}
			if (len >= 4)
			{
				NSFHeader.PALspeed[0] = chunk[2];
				NSFHeader.PALspeed[1] = chunk[3];
			}
		}
		else if (!memcmp(id, "auth", 4))
		{
			const uint8* p = chunk;
			const uint8* end = chunk + len;
			char tmp[NSF_AUTH_FIELD_LEN];

			p = NSFNextCString(p, end, tmp, sizeof(tmp));
			strncpy((char*)NSFHeader.SongName, tmp, 31);
			NSFHeader.SongName[31] = 0;

			p = NSFNextCString(p, end, tmp, sizeof(tmp));
			strncpy((char*)NSFHeader.Artist, tmp, 31);
			NSFHeader.Artist[31] = 0;

			p = NSFNextCString(p, end, tmp, sizeof(tmp));
			strncpy((char*)NSFHeader.Copyright, tmp, 31);
			NSFHeader.Copyright[31] = 0;

			NSFNextCString(p, end, NSFRipper, sizeof(NSFRipper));
		}
		else if (!memcmp(id, "tlbl", 4))
		{
			const uint8* p = chunk;
			const uint8* end = chunk + len;
			int total = NSFHeader.TotalSongs;
			if (total > NSF_MAX_TRACKS) total = NSF_MAX_TRACKS;

			for (i = 0; i < (uint32)total && p < end; i++)
				p = NSFNextCString(p, end, NSFTrackLabels[i], NSF_TRACK_LABEL_LEN);
		}
		else if (!memcmp(id, "time", 4))
		{
			int total = NSFHeader.TotalSongs;
			if (total > NSF_MAX_TRACKS) total = NSF_MAX_TRACKS;
			for (i = 0; i < (uint32)total && (i * 4 + 4) <= len; i++)
				NSFTrackTimesMs[i] = NSFReadSLE32Bytes(chunk + i * 4);
		}
		else if (!memcmp(id, "fade", 4))
		{
			int total = NSFHeader.TotalSongs;
			if (total > NSF_MAX_TRACKS) total = NSF_MAX_TRACKS;
			for (i = 0; i < (uint32)total && (i * 4 + 4) <= len; i++)
				NSFTrackFadeMs[i] = NSFReadSLE32Bytes(chunk + i * 4);
		}
		else if (!memcmp(id, "text", 4))
		{
			NSFCopyCString(NSFText, sizeof(NSFText), chunk, len);
		}
		else
		{
			// Unknown optional chunks may be skipped.  Unknown mandatory chunks should not be played.
			if (id[0] >= 'A' && id[0] <= 'Z' && !NSFIsKnownNSFeChunk(id))
			{
				FCEUD_PrintError("Unsupported mandatory NSFe chunk.");
				free(fileData);
				if (dataChunk) free(dataChunk);
				return LOADER_HANDLED_ERROR;
			}
		}

		pos += len;
	}

	if (!gotInfo || !gotData)
	{
		FCEUD_PrintError("Invalid NSFe file: missing INFO or DATA chunk.");
		free(fileData);
		if (dataChunk) free(dataChunk);
		return LOADER_HANDLED_ERROR;
	}

	if (!gotNend)
		FCEU_printf("Warning: NSFe file has no NEND chunk; using data parsed to EOF.\n");

	ret = NSFPrepareDataImage(dataChunk, dataChunkSize);
	free(fileData);
	free(dataChunk);
	if (ret != LOADER_OK)
		return ret;

	return NSFFinishLoad(name);
}

int NSFLoad(const char* name, FCEUFILE* fp)
{
	uint8 magic[5];

	FCEU_fseek(fp, 0, SEEK_SET);
	memset(magic, 0, sizeof(magic));
	FCEU_fread(magic, 1, 5, fp);

	if (!memcmp(magic, "NESM\x1a", 5))
		return NSFLoadStandard(name, fp);

	if (!memcmp(magic, "NSFE", 4))
		return NSFLoadNSFe(name, fp);

	return LOADER_INVALID_FORMAT;
}

static DECLFR(NSFVectorRead)
{
	if (((NSFNMIFlags & 1) && SongReload) || (NSFNMIFlags & 2) || doreset)
	{
		if (A == 0xFFFA) return(0x00);
		else if (A == 0xFFFB) return(0x38);
		else if (A == 0xFFFC) return(NSFROM_RESET_ENTRY);
		else if (A == 0xFFFD) { doreset = 0; return(0x38); }
		return(X.DB);
	}
	else
		return(CartBR(A));
}

void NSFVRC6_Init(void);
void NSFVRC7_Init(void);
void NSFMMC5_Init(void);
void NSFN106_Init(void);
void NSFAY_Init(void);

//zero 17-apr-2013 - added
static SFORMAT StateRegs[] = {
	{&SongReload, 1, "SREL"},
	{&CurrentSong, 4 | FCEUSTATE_RLSB, "CURS"},
	{&doreset, 1, "DORE"},
	{&NSFNMIFlags, 1, "NMIF"},
	{&NSFPlayRateRemainderUS, 4 | FCEUSTATE_RLSB, "NPRM"},
	{&NSFPlayCallCount, 1, "NPCN"},
	{ 0 }
};

void NSF_init(void)
{
	doreset = 1;
	NSFRemoveExpReadHook();
	NSFRemoveExpWriteHook();
	NSFRemoveAPUWriteHook();

	/* FDS NSF must run in direct mode from the very beginning of NSF_init().
	 * Otherwise FDSSoundReset()/GameExpSound.RChange can reinstall fds.cpp's
	 * normal $4040-$4092 handlers after nsf.cpp installs the visual hook.  In
	 * multi-expansion mixes this makes the real FDS audio keep playing while the
	 * NSF keyboard/meter misses the writes until the file is reopened.
	 */
	FDSNSFSetDirectMode((NSFHeader.SoundChip & 0x04) ? 1 : 0);

	NSFResetAPUShadow();
	NSFResetExpShadow();
	NSFResetExpWriteMux();
	NSFResetExpSoundMux();
	NSFResetPlayTimer();
	NSFResetPlayScheduler();
	NSFResetPlaybackControl();
	NSFResetWaveViewState();
	NSFResetStarfield();

	ResetCartMapping();
	if (NSFHeader.SoundChip & 4)
	{
		SetupCartPRGMapping(0, ExWRAM, 32768 + 8192, 1);
		setprg32(0x6000, 0);
		setprg8(0xE000, 4);
		memset(ExWRAM, 0x00, 32768 + 8192);
		SetWriteHandler(0x6000, 0xDFFF, CartBW);
		SetReadHandler(0x6000, 0xFFFF, CartBR);
	}
	else
	{
		memset(ExWRAM, 0x00, 8192);
		SetReadHandler(0x6000, 0x7FFF, CartBR);
		SetWriteHandler(0x6000, 0x7FFF, CartBW);
		SetupCartPRGMapping(0, NSFDATA, ((NSFMaxBank + 1) * 4096), 0);
		SetupCartPRGMapping(1, ExWRAM, 8192, 1);
		setprg8r(1, 0x6000, 0);
		SetReadHandler(0x8000, 0xFFFF, CartBR);
	}

	if (BSon)
	{
		int32 x;
		for (x = 0; x < 8; x++)
		{
			if (NSFHeader.SoundChip & 4 && x >= 6)
				BANKSET(0x6000 + (x - 6) * 4096, NSFHeader.BankSwitch[x]);
			BANKSET(0x8000 + x * 4096, NSFHeader.BankSwitch[x]);
		}
	}
	else
	{
		int32 x;
		for (x = (LoadAddr & 0xF000); x < 0x10000; x += 0x1000)
			BANKSET(x, ((x - (LoadAddr & 0x7000)) >> 12));
	}

	SetReadHandler(0xFFFA, 0xFFFD, NSFVectorRead);

	SetWriteHandler(0x2000, 0x3fff, 0);
	SetReadHandler(0x2000, 0x37ff, 0);
	SetReadHandler(0x3800 + sizeof(NSFROM), 0x3FFF, 0);
	SetReadHandler(0x3800, 0x3800 + sizeof(NSFROM) - 1, NSFROMRead);

	SetWriteHandler(0x5ff6, 0x5fff, NSF_write);

	SetWriteHandler(0x3ff0, 0x3fff, NSF_write);
	SetReadHandler(0x3ff0, 0x3fff, NSF_read);


	// NSF SoundChip is a bitmask.  Initialize every requested expansion chip,
	// but capture each chip's write handlers and EXPSOUND callbacks immediately
	// after its init.  Some mapper init functions install broad handlers or
	// overwrite the single global GameExpSound slot; without this capture step,
	// combined VRC6+other-chip NSFs can corrupt the VRC6 saw channel or lose one
	// chip's audio callback.
	if (NSFHeader.SoundChip & 1) {
		NSFPrepareCaptureExpSound();
		NSFVRC6_Init();
		NSFCaptureCurrentExpSound();
		NSFCaptureVRC6WriteHooks();
	}
	if (NSFHeader.SoundChip & 2) {
		NSFPrepareCaptureExpSound();
		NSFVRC7_Init();
		NSFCaptureCurrentExpSound();
		NSFCaptureVRC7WriteHooks();
	}
	if (NSFHeader.SoundChip & 4) {
		NSFPrepareCaptureExpSound();
		FDSSoundReset();
		NSFCaptureCurrentExpSound();
		NSFCaptureFDSWriteHooks();
	}
	if (NSFHeader.SoundChip & 8) {
		NSFPrepareCaptureExpSound();
		NSFMMC5_Init();
		NSFCaptureCurrentExpSound();
		NSFCaptureMMC5WriteHooks();
	}
	if (NSFHeader.SoundChip & 0x10) {
		NSFPrepareCaptureExpSound();
		NSFN106_Init();
		NSFCaptureCurrentExpSound();
		NSFCaptureN163WriteHooks();
	}
	if (NSFHeader.SoundChip & 0x20) {
		NSFPrepareCaptureExpSound();
		NSFAY_Init();
		NSFCaptureCurrentExpSound();
		NSFCaptureFME7WriteHooks();
	}
	NSFCommitExpSoundMux();

	/* Keep FDS direct mode asserted after expansion sound mux setup.  The mux RChange
	 * path may call FDS_ESI(), and direct mode prevents it from taking $4040-$4092
	 * away from NSF_ExpWrite.
	 */
	if (NSFHeader.SoundChip & 0x04)
		FDSNSFSetDirectMode(1);

	CurrentSong = NSFHeader.StartingSong;
	SongReload = 0xFF;
	NSFNMIFlags = 0;

	NSFInstallAPUWriteHook();
	NSFInstallExpWriteHook();
	NSFInstallExpReadHook();

	//zero 17-apr-2013 - added
	AddExState(StateRegs, ~0, 0, 0);
	AddExState(ExWRAM, FIXED_EXWRAM_SIZE, 0, "ERAM");
}

static DECLFW(NSF_write)
{
	switch (A)
	{
	case 0x3FF2:NSFPlayCallCount = V; break;
	case 0x3FF3:NSFNMIFlags |= 1; break;
	case 0x3FF4:NSFNMIFlags &= ~2; break;
	case 0x3FF5:NSFNMIFlags |= 2; break;

	case 0x5FF6:
	case 0x5FF7:
		if (!(NSFHeader.SoundChip & 4))
			return;
		A &= 0xF;
		BANKSET((A * 4096), V);
		break;

	case 0x5FF8:
	case 0x5FF9:
	case 0x5FFA:
	case 0x5FFB:
	case 0x5FFC:
	case 0x5FFD:
	case 0x5FFE:
	case 0x5FFF:
		if (!BSon && !(NSFHeader.SoundChip & 4))
			return;
		A &= 0xF;
		BANKSET((A * 4096), V);
		break;
	}
}

static DECLFR(NSF_read)
{
	int x;

	switch (A)
	{
	case 0x3ff0:x = SongReload;
		if (!fceuindbg)
			SongReload = 0;
		return x;
	case 0x3ff1:
		if (!fceuindbg)
		{
			memset(RAM, 0x00, 0x800);

			NSFTrackAPUWrite(0x4015, 0x0);
			BWrite[0x4015](0x4015, 0x0);
			for (x = 0; x < 0x14; x++)
			{
				NSFTrackAPUWrite(0x4000 + x, 0);
				BWrite[0x4000 + x](0x4000 + x, 0);
			}
			NSFTrackAPUWrite(0x4015, 0xF);
			BWrite[0x4015](0x4015, 0xF);

			if (NSFHeader.SoundChip & 4)
			{
				NSFTrackAPUWrite(0x4017, 0xC0);
				BWrite[0x4017](0x4017, 0xC0);  /* FDS BIOS writes $C0 */
				BWrite[0x4089](0x4089, 0x80);
				BWrite[0x408A](0x408A, 0xE8);
			}
			else
			{
				memset(ExWRAM, 0x00, 8192);
				NSFTrackAPUWrite(0x4017, 0xC0);
				BWrite[0x4017](0x4017, 0xC0);
				NSFTrackAPUWrite(0x4017, 0xC0);
				BWrite[0x4017](0x4017, 0xC0);
				NSFTrackAPUWrite(0x4017, 0x40);
				BWrite[0x4017](0x4017, 0x40);
			}

			if (BSon)
			{
				for (x = 0; x < 8; x++)
				{
					if (NSFHeader.SoundChip & 4 && x >= 6)
						BANKSET(0x6000 + (x - 6) * 4096, NSFHeader.BankSwitch[x]);
					BANKSET(0x8000 + x * 4096, NSFHeader.BankSwitch[x]);
				}
			}
#ifdef _S9XLUA_H
			//CallRegisteredLuaMemHook(A, 1, V, LUAMEMHOOK_WRITE); FIXME
#endif
			return (CurrentSong - 1);
		}
	case 0x3ff2:
		return NSFPlayCallCount;
	case 0x3FF3:return PAL;
	}
	return 0;
}

uint8 FCEU_GetJoyJoy(void);

static int special = 0;

// NES palette indices used by the NSF on-screen visualizer.
// $30 is the standard bright white entry in the NES/FCEUX palette.
static const int kNSFColorWhite = 0xA0;
static const int kNSFColorDimGray = 0x90;
static const int kNSFColorBlack = 0x00;
static const int kNSFColorWave = 0xA0;
static const int kNSFColorPulse1 = 0x96;
static const int kNSFColorPulse2 = 0xAA;
static const int kNSFColorTriangle = 0xA7;
static const int kNSFColorNoise = 0xB8;
static const int kNSFColorDPCM = 0xB6;
// Expansion chip visual colors.
// Keep each chip visually distinct in the right level meter, legend text,
// and piano keyboard highlights.
static const int kNSFColorVRC6 = 0xB1;  // VRC6: blue
static const int kNSFColorVRC7 = 0xB4;  // VRC7: purple / magenta
static const int kNSFColorN163 = 0xA9;  // N163/N106: green
static const int kNSFColorMMC5 = 0xA7;  // MMC5: amber / yellow
static const int kNSFColorFDS = 0xAB;   // FDS: cyan / aqua
static const int kNSFColorFME7 = 0xB6;  // S5B/FME-7: pink / peach
static const int kNSFColorLampOff = 0x90;
static const int kNSFColorPeak = 0xB0;
static const int kNSFColorRMS = 0x96;
// Filled body color for piano white keys.  The key outline remains pure white,
// while the main body uses a gray-white tone so the keyboard is easier to read.
static const int kNSFColorKeyEdge = 0x80;

#define NSF_STAR_MAX 96

typedef struct
{
	int x;
	int y;
	int z;
} NSF_STAR;

static NSF_STAR NSFStars[NSF_STAR_MAX];
static uint8 NSFStarsInited = 0;
static uint32 NSFStarSeed = 0x12345678;

static int NSFWaveLastY[256];
static int NSFWaveLastCount = 0;
static int NSFWaveHaveLast = 0;
static int NSFWavePeakHold = 0;
static int NSFWaveRMSHold = 0;
static int NSFWaveMeterAmp = 1;
static int NSFWavePeakHoldDecay = 0;

static void NSFResetWaveViewState(void)
{
	memset(NSFWaveLastY, 0, sizeof(NSFWaveLastY));
	NSFWaveLastCount = 0;
	NSFWaveHaveLast = 0;
	NSFWavePeakHold = 0;
	NSFWaveRMSHold = 0;
	NSFWavePeakHoldDecay = 0;
}

static void NSFDrawText(uint8* XBuf, int x, int y, const char* text, int color)
{
	char tmp[64];
	int maxChars;

	if (!XBuf || !text)
		return;

	if (x < 0)
		x = 0;
	if (y < 0)
		y = 0;
	if (x >= 256 || y >= 240)
		return;

	maxChars = (256 - x) >> 3;
	if (maxChars <= 0)
		return;
	if (maxChars > (int)sizeof(tmp) - 1)
		maxChars = (int)sizeof(tmp) - 1;

	strncpy(tmp, text, maxChars);
	tmp[maxChars] = 0;

	DrawTextTrans(ClipSidesOffset + XBuf + y * 256 + x, 256, (uint8*)tmp, color);
}

static void NSFPutPixel(uint8* XBuf, int x, int y, int color)
{
	if (!XBuf)
		return;
	if (x < 0 || x >= 256 || y < 0 || y >= 240)
		return;

	XBuf[x + y * 256] = (uint8)color;
}

static uint32 NSFStarRand(void)
{
	NSFStarSeed = NSFStarSeed * 1103515245 + 12345;
	return (NSFStarSeed >> 16) & 0x7FFF;
}

static void NSFResetOneStar(NSF_STAR* star, int farZ)
{
	if (!star)
		return;

	// Keep the virtual star field wider than the screen so that the stars
	// move outward from the center, similar to VirtuaNES' NSF background.
	star->x = ((int)(NSFStarRand() % 257) - 128) * 256;
	star->y = ((int)(NSFStarRand() % 241) - 120) * 256;
	star->z = farZ ? 255 : ((int)(NSFStarRand() % 220) + 35);
}

static void NSFResetStarfield(void)
{
	int i;

	// Mix the current song and NSF size into the seed so each file/track has a
	// slightly different star layout without relying on platform RNG APIs.
	NSFStarSeed = 0x12345678 ^ ((uint32)CurrentSong << 16) ^ (uint32)NSFSize;
	if (NSFStarSeed == 0)
		NSFStarSeed = 0x12345678;

	for (i = 0; i < NSF_STAR_MAX; i++)
		NSFResetOneStar(&NSFStars[i], 0);

	NSFStarsInited = 1;
}

static int NSFStarInUIPanel(int x, int y)
{
	// Do not draw stars over the main readable UI blocks.  This keeps the
	// background dynamic without making text, meters, Wave View or Keyboard noisy.
	if (x >= 8 && x < 160 && y >= 8 && y < 100)
		return 1;
	if (x >= 160 && x < 252 && y >= 0 && y < 72)
		return 1;
	if (x >= 16 && x < 242 && y >= 96 && y < 158)
		return 1;
	if (x >= 16 && x < 242 && y >= 156 && y < 232)
		return 1;
	if (y >= 224)
		return 1;

	return 0;
}

static void NSFDrawStar(uint8* XBuf, int x, int y, int z)
{
	int color;

	if (NSFStarInUIPanel(x, y))
		return;

	if (z > 170)
		color = 0x80;
	else if (z > 80)
		color = kNSFColorDimGray;
	else
		color = kNSFColorWhite;

	NSFPutPixel(XBuf, x, y, color);

	// Near stars become slightly larger, giving the background a VNes-like
	// moving starfield feel without adding bitmap data.
	if (z <= 80)
	{
		NSFPutPixel(XBuf, x - 1, y, color);
		NSFPutPixel(XBuf, x + 1, y, color);
	}
	if (z <= 40)
	{
		NSFPutPixel(XBuf, x, y - 1, color);
		NSFPutPixel(XBuf, x, y + 1, color);
	}
}

static void NSFDrawStarfield(uint8* XBuf)
{
	int i;

	if (!XBuf)
		return;
	if (!NSFStarsInited)
		NSFResetStarfield();

	for (i = 0; i < NSF_STAR_MAX; i++)
	{
		int sx;
		int sy;

		// Freeze the background during Pause/Stop so it behaves like the Wave View:
		// the screen becomes a paused player rather than a still-updating animation.
		if (!NSFPaused && !NSFStopped)
			NSFStars[i].z -= 2;

		if (NSFStars[i].z < 8)
		{
			NSFResetOneStar(&NSFStars[i], 1);
		}

		sx = 128 + NSFStars[i].x / NSFStars[i].z;
		sy = 120 + NSFStars[i].y / NSFStars[i].z;

		if (sx < 1 || sx >= 255 || sy < 1 || sy >= 239)
		{
			NSFResetOneStar(&NSFStars[i], 1);
			continue;
		}

		NSFDrawStar(XBuf, sx, sy, NSFStars[i].z);
	}
}

static void NSFDrawRect(uint8* XBuf, int x, int y, int w, int h, int color)
{
	int i;

	if (w < 0)
	{
		x += w;
		w = -w;
	}
	if (h < 0)
	{
		y += h;
		h = -h;
	}

	for (i = 0; i <= w; i++)
	{
		NSFPutPixel(XBuf, x + i, y, color);
		NSFPutPixel(XBuf, x + i, y + h, color);
	}

	for (i = 0; i <= h; i++)
	{
		NSFPutPixel(XBuf, x, y + i, color);
		NSFPutPixel(XBuf, x + w, y + i, color);
	}
}

static void NSFDrawFillRect(uint8* XBuf, int x, int y, int w, int h, int color)
{
	int yy;
	int xx;

	if (w < 0)
	{
		x += w;
		w = -w;
	}
	if (h < 0)
	{
		y += h;
		h = -h;
	}

	for (yy = 0; yy <= h; yy++)
	{
		for (xx = 0; xx <= w; xx++)
			NSFPutPixel(XBuf, x + xx, y + yy, color);
	}
}


static double NSFGetCPUClockForVisualizer(void)
{
	// Use the same basic distinction as the NSF header: bit0 means PAL.
	// Dendy is intentionally not handled here; this is only for the visualizer.
	return (NSFHeader.VideoSystem & 1) ? 1662607.125 : 1789772.7272727272727;
}

static int NSFFreqToKeyIndex(double freq)
{
	int midi;
	int key;

	if (freq <= 0.0)
		return -1;

	midi = (int)floor(69.0 + 12.0 * (log(freq / 440.0) / log(2.0)) + 0.5);
	key = midi - 24; // C1 -> 0, A4 -> 45, B8 -> 95.

	if (key < 0 || key >= 96)
		return -1;

	return key;
}

static int NSFGetPulseKeyIndex(int pulse)
{
	int base = pulse ? 0x04 : 0x00;
	uint16 timer;
	double freq;

	if (!(NSFAPUShadow[0x15] & (pulse ? 0x02 : 0x01)))
		return -1;

	// If constant volume is selected and volume is zero, treat it as silent.
	if ((NSFAPUShadow[base + 0] & 0x10) && ((NSFAPUShadow[base + 0] & 0x0F) == 0))
		return -1;

	timer = (uint16)(((NSFAPUShadow[base + 3] & 0x07) << 8) | NSFAPUShadow[base + 2]);
	if (timer < 8)
		return -1;

	freq = NSFGetCPUClockForVisualizer() / (16.0 * ((double)timer + 1.0));
	return NSFFreqToKeyIndex(freq);
}

static int NSFGetTriangleKeyIndex(void)
{
	uint16 timer;
	double freq;

	if (!(NSFAPUShadow[0x15] & 0x04))
		return -1;

	// Linear counter reload value zero usually means no audible triangle note.
	if ((NSFAPUShadow[0x08] & 0x7F) == 0)
		return -1;

	timer = (uint16)(((NSFAPUShadow[0x0B] & 0x07) << 8) | NSFAPUShadow[0x0A]);
	if (timer < 2)
		return -1;

	freq = NSFGetCPUClockForVisualizer() / (32.0 * ((double)timer + 1.0));
	return NSFFreqToKeyIndex(freq);
}


static int NSFIsNoiseActive(void)
{
	if (!(NSFAPUShadow[0x15] & 0x08))
		return 0;

	// If constant volume is selected and volume is zero, treat it as silent.
	if ((NSFAPUShadow[0x0C] & 0x10) && ((NSFAPUShadow[0x0C] & 0x0F) == 0))
		return 0;

	return 1;
}

static int NSFIsDPCMActive(void)
{
	// The DPCM channel is not a pitched keyboard channel.  For the NSF UI,
	// use the $4015 enable bit as a simple activity lamp.
	return (NSFAPUShadow[0x15] & 0x10) ? 1 : 0;
}

static void NSFDrawStatusLamp(uint8* XBuf, int x, int y, const char* label, int active, int onColor)
{
	int textColor = active ? onColor : kNSFColorLampOff;
	int lampColor = active ? onColor : kNSFColorBlack;
	int borderColor = active ? kNSFColorWhite : kNSFColorLampOff;

	NSFDrawText(XBuf, x, y, label, textColor);
	NSFDrawFillRect(XBuf, x + 26, y + 2, 4, 4, lampColor);
	NSFDrawRect(XBuf, x + 26, y + 2, 4, 4, borderColor);
}


static int NSFGetVRC6KeyIndex(int ch)
{
	uint16 timer;
	double freq;

	if (ch < 0 || ch > 2)
		return -1;

	// Register 2 bit7 is the channel enable bit on VRC6.
	if (!(NSFVRC6Shadow[ch][2] & 0x80))
		return -1;

	// Pulse channels use low 4 bits as volume. Saw uses register 0 low 6 bits.
	if (ch < 2)
	{
		if ((NSFVRC6Shadow[ch][0] & 0x0F) == 0)
			return -1;
	}
	else
	{
		if ((NSFVRC6Shadow[ch][0] & 0x3F) == 0)
			return -1;
	}

	timer = (uint16)(((NSFVRC6Shadow[ch][2] & 0x0F) << 8) | NSFVRC6Shadow[ch][1]);
	if (timer < 2)
		return -1;

	// VRC6 pulse is similar to square timing. Saw is slightly different; this is
	// close enough for keyboard visualization.
	if (ch < 2)
		freq = NSFGetCPUClockForVisualizer() / (16.0 * ((double)timer + 1.0));
	else
		freq = NSFGetCPUClockForVisualizer() / (14.0 * ((double)timer + 1.0));

	return NSFFreqToKeyIndex(freq);
}

static int NSFGetVRC7KeyIndex(int ch)
{
	uint16 fnum;
	int block;
	int volume;
	double clock;
	double freq;

	if (ch < 0 || ch > 5)
		return -1;

	// YM2413/VRC7 melodic channel registers:
	// $10-$15 = f-number low, $20-$25 = f-number high/block/key,
	// $30-$35 = instrument/volume.  Bit4 of $20-$25 is key-on.
	if (!(NSFVRC7Regs[0x20 + ch] & 0x10))
		return -1;

	volume = NSFVRC7Regs[0x30 + ch] & 0x0F;
	// VRC7 volume is inverted: 0 is loudest, 15 is silent.
	if (volume == 0x0F)
		return -1;

	fnum = (uint16)NSFVRC7Regs[0x10 + ch] | (uint16)((NSFVRC7Regs[0x20 + ch] & 0x01) << 8);
	block = (NSFVRC7Regs[0x20 + ch] >> 1) & 0x07;
	if (fnum == 0)
		return -1;

	// VRC7 uses an OPLL clock close to twice the NES CPU clock.  This formula is
	// an approximate visualizer mapping, not part of audio emulation.
	clock = NSFGetCPUClockForVisualizer() * 2.0;
	freq = ((double)fnum * (clock / 72.0) * (double)(1 << block)) / 524288.0;
	return NSFFreqToKeyIndex(freq);
}

static int NSFIsFDSVisualActive(void)
{
	uint16 rawfreq;

	// $4082 = frequency low, $4083 low nibble = frequency high.
	rawfreq = NSFGetFDSRawFreqShadow();
	if (rawfreq)
		NSFFDSLastFreqShadow = rawfreq;
	else
		rawfreq = NSFFDSLastFreqShadow;

	if (rawfreq == 0)
		return 0;

	/*
	 * Do not use $4083 bit7 or $4089 bit7 as hard visual mutes here.
	 * In mixed VRC6+FDS+MMC5 / VRC7+FDS+MMC5 NSFs, the FDS audio core can still
	 * produce audible output while the visual shadow sees stale write-enable or
	 * halt bits inherited from bootstrap/setup writes.  The visualizer should show
	 * FDS activity once real wave/mod/frequency registers have been written.
	 */
	if (!NSFFDSSeenShadow)
		return 0;

	// When volume envelope is disabled, low 6 bits of $4080 are direct volume.
	if ((NSFFDSShadow[0x00] & 0x80) && ((NSFFDSShadow[0x00] & 0x3F) == 0))
		return 0;

	return 1;
}

static int NSFGetFDSKeyIndex(void)
{
	uint16 rawfreq;
	double freq;

	if (!NSFIsFDSVisualActive())
		return -1;

	rawfreq = NSFGetFDSRawFreqShadow();
	if (!rawfreq)
		rawfreq = NSFFDSLastFreqShadow;

	// FDS frequency behaves like a wavetable phase increment.  This approximation
	// is sufficient for keyboard visualization.
	freq = (NSFGetCPUClockForVisualizer() * (double)rawfreq) / (65536.0 * 64.0);
	return NSFFreqToKeyIndex(freq);
}

static int NSFMMC5ChannelHasTone(int ch)
{
	int base;
	uint16 timer;
	uint8 bit;

	if (ch < 0 || ch > 1)
		return 0;

	base = ch ? 0x04 : 0x00;
	bit = ch ? 0x02 : 0x01;

	timer = (uint16)(((NSFMMC5Shadow[base + 3] & 0x07) << 8) | NSFMMC5Shadow[base + 2]);
	if (timer < 8)
		return 0;

	// Constant-volume mode with volume 0 is definitely silent.
	if ((NSFMMC5Shadow[base + 0] & 0x10) && ((NSFMMC5Shadow[base + 0] & 0x0F) == 0))
		return 0;

	if (NSFMMC5RunningShadow & bit)
		return 1;
	if (NSFMMC5Shadow[0x15] & bit)
		return 1;

	/*
	 * Visual fallback for multi-chip NSF playback.  In VRC6+FDS+MMC5 mixes the
	 * MMC5 audio core may be audibly active even when the simple $5015 mirror
	 * is not a reliable activity flag for the UI.  If valid MMC5 tone registers
	 * were seen for this channel, show the note/meter rather than leaving M5
	 * blank.  Stop/silence writes clear NSFMMC5SeenShadow through $5015=0 and
	 * volume=0 writes, so this should not keep normal stops stuck forever.
	 */
	return (NSFMMC5SeenShadow & bit) ? 1 : 0;
}

static int NSFGetMMC5KeyIndex(int ch)
{
	int base;
	uint16 timer;
	double freq;

	if (ch < 0 || ch > 1)
		return -1;

	if (!NSFMMC5ChannelHasTone(ch))
		return -1;

	base = ch ? 0x04 : 0x00;
	timer = (uint16)(((NSFMMC5Shadow[base + 3] & 0x07) << 8) | NSFMMC5Shadow[base + 2]);

	freq = NSFGetCPUClockForVisualizer() / (16.0 * ((double)timer + 1.0));
	return NSFFreqToKeyIndex(freq);
}

static int NSFGetN163ChannelCount(void)
{
	int count = ((NSFN163RAM[0x7F] >> 4) & 0x07) + 1;
	if (count < 1) count = 1;
	if (count > 8) count = 8;
	return count;
}

static int NSFGetN163KeyIndex(int ch)
{
	int count;
	int base;
	uint32 rawfreq;
	int volume;
	double freq;

	if (ch < 0 || ch > 7)
		return -1;

	count = NSFGetN163ChannelCount();
	if (ch >= count)
		return -1;

	// N163 channel registers are 8-byte blocks in internal RAM.  This follows the
	// common NSF layout used by players: freq low at +0, mid at +2, high bits at +4,
	// volume at +7.  Some engines update channels in reverse order, so this is a
	// best-effort visualizer rather than part of sound emulation.
	base = 0x78 - ch * 8;
	if (base < 0 || base + 7 >= 0x80)
		return -1;

	volume = NSFN163RAM[base + 7] & 0x0F;
	if (volume == 0)
		return -1;

	rawfreq = (uint32)NSFN163RAM[base + 0] | ((uint32)NSFN163RAM[base + 2] << 8) | (((uint32)NSFN163RAM[base + 4] & 0x03) << 16);
	if (rawfreq == 0)
		return -1;

	freq = (NSFGetCPUClockForVisualizer() * (double)rawfreq) / (15.0 * 65536.0 * (double)count);
	return NSFFreqToKeyIndex(freq);
}

static int NSFGetFME7KeyIndex(int ch)
{
	uint16 period;
	double freq;

	if (ch < 0 || ch > 2)
		return -1;

	// Register 7 bits 0-2 disable tone when set.
	if (NSFFME7Regs[7] & (1 << ch))
		return -1;

	if ((NSFFME7Regs[8 + ch] & 0x1F) == 0)
		return -1;

	period = (uint16)(((NSFFME7Regs[ch * 2 + 1] & 0x0F) << 8) | NSFFME7Regs[ch * 2]);
	if (period == 0)
		return -1;

	freq = NSFGetCPUClockForVisualizer() / (16.0 * (double)period);
	return NSFFreqToKeyIndex(freq);
}

static int NSFClampLevel15(int v)
{
	if (v < 0)
		return 0;
	if (v > 15)
		return 15;
	return v;
}

static int NSFScaleTo15(int v, int maxv)
{
	if (maxv <= 0)
		return 0;
	if (v <= 0)
		return 0;
	if (v >= maxv)
		return 15;
	return NSFClampLevel15((v * 15 + (maxv / 2)) / maxv);
}

static int NSFMaxLevel(int a, int b)
{
	return (a > b) ? a : b;
}

static int NSFGetPulseLevel(int pulse)
{
	int base = pulse ? 0x04 : 0x00;

	if (!(NSFAPUShadow[0x15] & (pulse ? 0x02 : 0x01)))
		return 0;

	// This is a register-level visual meter, similar to simple NSF Lua displays.
	// It uses the envelope/constant-volume nibble rather than internal envelope state.
	return NSFClampLevel15(NSFAPUShadow[base + 0] & 0x0F);
}

static int NSFGetTriangleLevel(void)
{
	if (NSFGetTriangleKeyIndex() < 0)
		return 0;
	return 15;
}

static int NSFGetNoiseLevel(void)
{
	if (!NSFIsNoiseActive())
		return 0;
	return NSFClampLevel15(NSFAPUShadow[0x0C] & 0x0F);
}

static int NSFGetDPCMLevel(void)
{
	if (!NSFIsDPCMActive())
		return 0;
	// $4011 is the 7-bit DMC DAC output level.  Map 0..127 to 0..15.
	return NSFScaleTo15(NSFAPUShadow[0x11] & 0x7F, 127);
}

static int NSFGetVRC6Level(void)
{
	int level = 0;
	int ch;

	for (ch = 0; ch < 3; ch++)
	{
		int chLevel;

		if (!(NSFVRC6Shadow[ch][2] & 0x80))
			continue;

		if (ch < 2)
			chLevel = NSFVRC6Shadow[ch][0] & 0x0F;
		else
			chLevel = NSFScaleTo15(NSFVRC6Shadow[ch][0] & 0x3F, 0x3F);

		level = NSFMaxLevel(level, chLevel);
	}

	return NSFClampLevel15(level);
}

static int NSFGetVRC7Level(void)
{
	int level = 0;
	int ch;

	for (ch = 0; ch < 6; ch++)
	{
		int vol;

		if (!(NSFVRC7Regs[0x20 + ch] & 0x10))
			continue;

		// VRC7/OPLL volume is inverted: 0 loudest, 15 silent.
		vol = 15 - (NSFVRC7Regs[0x30 + ch] & 0x0F);
		level = NSFMaxLevel(level, vol);
	}

	return NSFClampLevel15(level);
}

static int NSFGetFDSLevel(void)
{
	int vol = NSFFDSShadow[0x00] & 0x3F;

	if (!NSFIsFDSVisualActive())
		return 0;

	// Direct-volume mode: low 6 bits are the actual direct volume.
	if (NSFFDSShadow[0x00] & 0x80)
		return NSFScaleTo15(vol, 0x3F);

	/*
	 * Envelope mode: low 6 bits are envelope speed/control, not the current
	 * envelope output.  nsf.cpp only shadows CPU writes; it does not read the
	 * internal FDS envelope accumulator from fds.cpp.  Use the register value
	 * when it is non-zero, and use a conservative activity fallback otherwise so
	 * audible FDS notes do not show a dead meter.
	 */
	if (vol > 0)
		return NSFScaleTo15(vol, 0x3F);

	return 10;
}

static int NSFGetMMC5Level(void)
{
	int level = 0;
	int ch;

	for (ch = 0; ch < 2; ch++)
	{
		int base = ch ? 0x04 : 0x00;
		if (!NSFMMC5ChannelHasTone(ch))
			continue;
		level = NSFMaxLevel(level, NSFMMC5Shadow[base + 0] & 0x0F);
	}

	return NSFClampLevel15(level);
}

static int NSFGetN163Level(void)
{
	int level = 0;
	int count = NSFGetN163ChannelCount();
	int ch;

	for (ch = 0; ch < count && ch < 8; ch++)
	{
		int base = 0x78 - ch * 8;
		if (base < 0 || base + 7 >= 0x80)
			continue;
		level = NSFMaxLevel(level, NSFN163RAM[base + 7] & 0x0F);
	}

	return NSFClampLevel15(level);
}

static int NSFGetFME7Level(void)
{
	int level = 0;
	int ch;

	for (ch = 0; ch < 3; ch++)
	{
		int vol;
		if (NSFFME7Regs[7] & (1 << ch))
			continue;
		vol = (NSFFME7Regs[8 + ch] & 0x10) ? 15 : (NSFFME7Regs[8 + ch] & 0x0F);
		level = NSFMaxLevel(level, vol);
	}

	return NSFClampLevel15(level);
}

static void NSFDrawChannelLevelRow(uint8* XBuf, int x, int y, const char* label, int level, int color)
{
	int i;
	char num[4];
	int barX = x + 26;
	int numX = x + 62;

	level = NSFClampLevel15(level);

	NSFDrawText(XBuf, x, y, label, level ? color : kNSFColorLampOff);

	// Compact 15-step meter.  This visually corresponds to "IIII... + number"
	// while fitting into the top-right empty area.
	for (i = 0; i < 15; i++)
	{
		int px = barX + i * 2;
		int c = (i < level) ? color : kNSFColorDimGray;
		NSFDrawRect(XBuf, px, y + 2, 0, 6, c);
	}

	snprintf(num, sizeof(num), "%d", level);
	NSFDrawText(XBuf, numX, y, num, level ? kNSFColorWhite : kNSFColorLampOff);
}

static void NSFDrawChannelLevelPanel(uint8* XBuf, int x, int y)
{
	int row = 0;
	int extOff = 10;

	NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8, "SQ1", NSFGetPulseLevel(0), kNSFColorPulse1);
	NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + 2, "SQ2", NSFGetPulseLevel(1), kNSFColorPulse2);
	NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + 4, "TRI", NSFGetTriangleLevel(), kNSFColorTriangle);
	NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + 6, "NOI", NSFGetNoiseLevel(), kNSFColorNoise);
	NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + 8, "DMC", NSFGetDPCMLevel(), kNSFColorDPCM);

	if (NSFHeader.SoundChip & 0x01)
	{
		NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + extOff, "V6 ", NSFGetVRC6Level(), kNSFColorVRC6);
		extOff += 2;
	}

	if (NSFHeader.SoundChip & 0x02)
	{
		NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + extOff, "V7 ", NSFGetVRC7Level(), kNSFColorVRC7);
		extOff += 2;
	}

	if (NSFHeader.SoundChip & 0x04)
	{
		NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + extOff, "FDS", NSFGetFDSLevel(), kNSFColorFDS);
		extOff += 2;
	}

	if (NSFHeader.SoundChip & 0x08)
	{
		NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + extOff, "M5 ", NSFGetMMC5Level(), kNSFColorMMC5);
		extOff += 2;
	}

	if (NSFHeader.SoundChip & 0x10)
	{
		NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + extOff, "N1 ", NSFGetN163Level(), kNSFColorN163);
		extOff += 2;
	}

	if (NSFHeader.SoundChip & 0x20)
	{
		NSFDrawChannelLevelRow(XBuf, x, y + row++ * 8 + extOff, "S5B", NSFGetFME7Level(), kNSFColorFME7);
		extOff += 2;
	}
}

static void NSFDrawKeyboardHighlightKey(uint8* XBuf, int x, int y, int w, int h, int key, int color)
{
	static const int kOctaveW = 49;
	static const int kKeyH = 16;
	static const int whiteOfs[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
	static const int blackAfterWhite[12] = { -1, 0, -1, 1, -1, -1, 3, -1, 4, -1, 5, -1 };
	int startX;
	int upperY;
	int lowerY;
	int octave;
	int note;
	int rowY;
	int octaveX;
	int whiteW;
	int blackW;
	int blackH;

	if (key < 0 || key >= 96 || w < 80 || h < 38)
		return;

	startX = x + ((w - (kOctaveW * 4)) / 2);
	upperY = y + 4;
	lowerY = y + 25;
	octave = key / 12;
	note = key % 12;
	rowY = (octave < 4) ? lowerY : upperY;
	octaveX = startX + (octave % 4) * kOctaveW;
	whiteW = (kOctaveW - 1) / 7;
	if (whiteW < 3)
		whiteW = 3;

	blackW = whiteW - 2;
	blackH = (kKeyH * 3) / 5;
	if (blackW < 2)
		blackW = 2;
	if (blackH < 4)
		blackH = 4;

	if (blackAfterWhite[note] >= 0)
	{
		int bx = octaveX + (blackAfterWhite[note] + 1) * whiteW - (blackW / 2);
		NSFDrawFillRect(XBuf, bx, rowY, blackW, blackH, color);
		NSFDrawRect(XBuf, bx, rowY, blackW, blackH, color);
	}
	else
	{
		int wx = octaveX + whiteOfs[note] * whiteW;
		// Highlight the lower part of white keys so nearby black keys remain visible.
		NSFDrawFillRect(XBuf, wx + 1, rowY + blackH + 1, whiteW - 2, kKeyH - blackH - 2, color);
		NSFDrawRect(XBuf, wx, rowY, whiteW, kKeyH, color);
	}
}

static void NSFDrawKeyboardHighlights(uint8* XBuf, int x, int y, int w, int h)
{
	int i;

	// Internal 2A03 melodic channels.
	NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetPulseKeyIndex(0), kNSFColorPulse1);
	NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetPulseKeyIndex(1), kNSFColorPulse2);
	NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetTriangleKeyIndex(), kNSFColorTriangle);

	// Expansion chips. These are independent because NSF SoundChip is a bitmask.
	if (NSFHeader.SoundChip & 0x01)
	{
		for (i = 0; i < 3; i++)
			NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetVRC6KeyIndex(i), kNSFColorVRC6);
	}
	if (NSFHeader.SoundChip & 0x02)
	{
		for (i = 0; i < 6; i++)
			NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetVRC7KeyIndex(i), kNSFColorVRC7);
	}
	if (NSFHeader.SoundChip & 0x04)
	{
		NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetFDSKeyIndex(), kNSFColorFDS);
	}
	if (NSFHeader.SoundChip & 0x08)
	{
		for (i = 0; i < 2; i++)
			NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetMMC5KeyIndex(i), kNSFColorMMC5);
	}
	if (NSFHeader.SoundChip & 0x10)
	{
		for (i = 0; i < 8; i++)
			NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetN163KeyIndex(i), kNSFColorN163);
	}
	if (NSFHeader.SoundChip & 0x20)
	{
		for (i = 0; i < 3; i++)
			NSFDrawKeyboardHighlightKey(XBuf, x, y, w, h, NSFGetFME7KeyIndex(i), kNSFColorFME7);
	}
}

static void NSFDrawKeyboardOctave(uint8* XBuf, int x, int y, int octaveW, int keyH)
{
	static const int kWhiteColor = kNSFColorKeyEdge;
	static const int kWhiteFillColor = kNSFColorWhite;
	static const int kBlackColor = kNSFColorKeyEdge;
	static const int kBlackFillColor = kNSFColorBlack;
	int whiteW = octaveW / 7;
	int i;

	if (whiteW < 3)
		whiteW = 3;

	// White keys: C D E F G A B.
	// Fill the large lower key body with gray-white, then draw a white outline.
	// Black/sharp keys are drawn after this and remain black.
	for (i = 0; i < 7; i++)
	{
		NSFDrawFillRect(XBuf, x + i * whiteW + 1, y + 1, whiteW - 1, keyH - 1, kWhiteFillColor);
		NSFDrawRect(XBuf, x + i * whiteW, y, whiteW, keyH, kWhiteColor);
	}

	// Black keys: C#, D#, F#, G#, A#.
	// Draw as filled small rectangles, placed between white keys.
	{
		static const int blackAfterWhite[5] = { 0, 1, 3, 4, 5 };
		int blackW = whiteW - 2;
		int blackH = (keyH * 3) / 5;
		if (blackW < 2)
			blackW = 2;
		if (blackH < 4)
			blackH = 4;

		for (i = 0; i < 5; i++)
		{
			int bx = x + (blackAfterWhite[i] + 1) * whiteW - (blackW / 2);
			NSFDrawFillRect(XBuf, bx, y, blackW, blackH, kBlackFillColor);
			NSFDrawRect(XBuf, bx, y, blackW, blackH, kBlackColor);
		}
	}
}


static void NSFDrawExpansionLegend(uint8* XBuf, int x, int y)
{
	int dx = 0;

	if (!(NSFHeader.SoundChip & 0x3F))
		return;

	// Expansion-chip tags use their own legend line.  Keep a wider spacing than
	// 8 pixels because DrawTextTrans draws a small colored backing around each
	// short label; using only one character cell of vertical/horizontal gap can
	// make the tags visually overlap after scaling.
	NSFDrawText(XBuf, x + dx, y, "EXP", kNSFColorDimGray);
	dx += 32;

	if (NSFHeader.SoundChip & 0x01)
	{
		NSFDrawText(XBuf, x + dx, y, "V6", kNSFColorVRC6);
		dx += 24;
	}
	if (NSFHeader.SoundChip & 0x02)
	{
		NSFDrawText(XBuf, x + dx, y, "V7", kNSFColorVRC7);
		dx += 24;
	}
	if (NSFHeader.SoundChip & 0x04)
	{
		NSFDrawText(XBuf, x + dx, y, "FD", kNSFColorFDS);
		dx += 24;
	}
	if (NSFHeader.SoundChip & 0x08)
	{
		NSFDrawText(XBuf, x + dx, y, "M5", kNSFColorMMC5);
		dx += 24;
	}
	if (NSFHeader.SoundChip & 0x10)
	{
		NSFDrawText(XBuf, x + dx, y, "N1", kNSFColorN163);
		dx += 24;
	}
	if (NSFHeader.SoundChip & 0x20)
	{
		NSFDrawText(XBuf, x + dx, y, "S5", kNSFColorFME7);
	}
}

static void NSFDrawKeyboardView(uint8* XBuf, int x, int y, int w, int h)
{
	static const int kFgColor = kNSFColorWhite;
	static const int kOctaveW = 49;
	static const int kKeyH = 16;
	int startX;
	int upperY;
	int lowerY;
	int i;

	if (!XBuf || w < 80 || h < 38)
		return;

	// Keep only the Keyboard title.  The per-channel legend/status labels
	// (P1/P2/TR/NOI/DMC and EXP/V6/V7/FD/M5/N1/S5) are intentionally hidden
	// to make the piano area cleaner.  Actual key highlights are still drawn
	// with their chip-specific colors below.
	NSFDrawText(XBuf, x, y - 10, "Keyboard", kFgColor);

	NSFDrawRect(XBuf, x, y, w, h, kFgColor);

	// Similar layout idea to VirtuaNES: upper row O5-O8, lower row O1-O4.
	// Dynamic highlights are generated from tracked 2A03 timer register writes.
	startX = x + ((w - (kOctaveW * 4)) / 2);
	upperY = y + 4;
	lowerY = y + 25;

	for (i = 0; i < 4; i++)
	{
		NSFDrawKeyboardOctave(XBuf, startX + i * kOctaveW, upperY, kOctaveW - 1, kKeyH);
		NSFDrawKeyboardOctave(XBuf, startX + i * kOctaveW, lowerY, kOctaveW - 1, kKeyH);
	}

	NSFDrawKeyboardHighlights(XBuf, x, y, w, h);
}

static void NSFDrawLine(uint8* XBuf, int x0, int y0, int x1, int y1, int color)
{
	int dx = abs(x1 - x0);
	int sx = (x0 < x1) ? 1 : -1;
	int dy = -abs(y1 - y0);
	int sy = (y0 < y1) ? 1 : -1;
	int err = dx + dy;

	for (;;)
	{
		int e2;

		NSFPutPixel(XBuf, x0, y0, color);

		if (x0 == x1 && y0 == y1)
			break;

		e2 = err << 1;
		if (e2 >= dy)
		{
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx)
		{
			err += dx;
			y0 += sy;
		}
	}
}

static int NSFClampInt(int v, int minv, int maxv)
{
	if (v < minv)
		return minv;
	if (v > maxv)
		return maxv;
	return v;
}

static void NSFDrawMiniBar(uint8* XBuf, int x, int y, int w, const char* label, int level, int color)
{
	int fill;
	int i;

	if (level < 0) level = 0;
	if (level > 100) level = 100;
	fill = (w * level) / 100;

	NSFDrawText(XBuf, x, y, label, kNSFColorWhite);
	NSFDrawRect(XBuf, x + 24, y + 2, w, 4, kNSFColorDimGray);
	for (i = 0; i < fill; i++)
		NSFDrawRect(XBuf, x + 25 + i, y + 3, 0, 2, color);
}

static void NSFDrawCachedWave(uint8* XBuf, int x, int y, int w, int h)
{
	int i;
	int plotW = w - 2;
	int centerY = y + (h >> 1);

	NSFDrawRect(XBuf, x + 1, centerY, w - 2, 0, kNSFColorDimGray);

	if (!NSFWaveHaveLast || NSFWaveLastCount <= 0)
		return;

	if (plotW > NSFWaveLastCount)
		plotW = NSFWaveLastCount;

	for (i = 1; i < plotW; i++)
		NSFDrawLine(XBuf, x + i, NSFWaveLastY[i - 1], x + 1 + i, NSFWaveLastY[i], kNSFColorWave);
}

static void NSFDrawWaveView(uint8* XBuf, int x, int y, int w, int h)
{
	static const int kFgColor = kNSFColorWhite;
	static const int kGridColor = kNSFColorDimGray;
	static const int kWaveColor = kNSFColorWave;
	int32* Bufpl = 0;
	int len;
	int boxY;
	int boxH;
	int centerY;
	int amp;
	int plotW;
	int i;
	int prevX = 0;
	int prevY = 0;
	int havePrev = 0;
	int32 mul = 0;
	double sumSq = 0.0;
	int peakY = 0;
	int rmsY = 0;
	int peakPct;
	int rmsPct;

	if (!XBuf || w < 8 || h < 18)
		return;

	// Draw the title above the white Wave View frame instead of inside/overlapping it.
	// The input y is now treated as the title line; the actual scope box starts below it.
	NSFDrawText(XBuf, x + 4, y, "Wave View", kFgColor);
	boxY = y + 10;
	boxH = h - 10;
	if (boxH < 8)
		boxH = 8;

	NSFDrawRect(XBuf, x, boxY, w, boxH, kFgColor);

	centerY = boxY + (boxH >> 1);
	amp = (boxH >> 1) - 3;
	if (amp < 1)
		amp = 1;
	NSFWaveMeterAmp = amp;

	// Pause uses output mute, so the live audio buffer may be flat.  Keep the
	// oscilloscope frozen instead; Stop intentionally falls back to a flat view.
	if (NSFPaused && NSFWaveHaveLast)
	{
		NSFDrawCachedWave(XBuf, x, boxY, w, boxH);
		return;
	}

	// Center line, similar to VirtuaNES' Wave View baseline.
	NSFDrawRect(XBuf, x + 1, centerY, w - 2, 0, kGridColor);

	len = GetSoundBuffer(&Bufpl);
	if (!Bufpl || len <= 0 || NSFStopped)
		return;

	if (FSettings.SoundVolume > 0)
	{
		int denom = (16384 * FSettings.SoundVolume) / 50;
		if (denom <= 0)
			denom = 16384;
		mul = (8192 * (amp * 4)) / denom;
	}

	plotW = w - 2;
	if (plotW > 256)
		plotW = 256;

	for (i = 0; i < plotW; i++)
	{
		int sampleIndex = (i * len) / plotW;
		int yp = 0;
		int px = x + 1 + i;
		int py;

		if (sampleIndex >= len)
			sampleIndex = len - 1;

		if (mul)
			yp = -((Bufpl[sampleIndex] * mul) >> 14);

		yp = NSFClampInt(yp, -amp, amp);
		py = centerY + yp;

		if (havePrev)
			NSFDrawLine(XBuf, prevX, prevY, px, py, kWaveColor);
		else
			NSFPutPixel(XBuf, px, py, kWaveColor);

		if (abs(yp) > peakY)
			peakY = abs(yp);
		sumSq += (double)(yp * yp);
		NSFWaveLastY[i] = py;

		prevX = px;
		prevY = py;
		havePrev = 1;
	}

	NSFWaveLastCount = plotW;
	NSFWaveHaveLast = 1;

	if (plotW > 0)
		rmsY = (int)(sqrt(sumSq / (double)plotW) + 0.5);

	// Peak hold with slow decay, similar to a simple scope meter.
	if (peakY >= NSFWavePeakHold)
	{
		NSFWavePeakHold = peakY;
		NSFWavePeakHoldDecay = 10;
	}
	else if (NSFWavePeakHoldDecay > 0)
		NSFWavePeakHoldDecay--;
	else if (NSFWavePeakHold > 0)
		NSFWavePeakHold--;

	// RMS hold is smoothed to make the level bar readable.
	NSFWaveRMSHold = (NSFWaveRMSHold * 3 + rmsY) / 4;

	if (NSFWavePeakHold > 0)
	{
		int py1 = centerY - NSFWavePeakHold;
		int py2 = centerY + NSFWavePeakHold;
		NSFDrawRect(XBuf, x + w - 12, py1, 9, 0, kNSFColorPeak);
		NSFDrawRect(XBuf, x + w - 12, py2, 9, 0, kNSFColorPeak);
	}

	peakPct = (amp > 0) ? (NSFWavePeakHold * 100) / amp : 0;
	rmsPct = (amp > 0) ? (NSFWaveRMSHold * 100) / amp : 0;
	(void)peakPct;
	(void)rmsPct;
}

static void NSFDrawWaveMeterInfo(uint8* XBuf, int x, int y)
{
	int amp = NSFWaveMeterAmp;
	int peakPct;
	int rmsPct;

	if (amp <= 0)
		amp = 1;

	if (NSFStopped)
	{
		peakPct = 0;
		rmsPct = 0;
	}
	else
	{
		peakPct = (NSFWavePeakHold * 100) / amp;
		rmsPct = (NSFWaveRMSHold * 100) / amp;
	}

	NSFDrawMiniBar(XBuf, x, y, 28, "PK", peakPct, kNSFColorPeak);
	NSFDrawMiniBar(XBuf, x + 64, y, 28, "RMS", rmsPct, kNSFColorRMS);
}


static int NSFGetVisualizerFPS(void)
{
	return NSFIsPALPlayback() ? 50 : 60;
}

static void NSFFormatMS(char* out, int outSize, int ms)
{
	uint32 totalSeconds;
	uint32 minutes;
	uint32 seconds;

	if (!out || outSize <= 0)
		return;

	if (ms < 0)
	{
		snprintf(out, outSize, "--:--");
		return;
	}

	totalSeconds = (uint32)(ms / 1000);
	minutes = totalSeconds / 60;
	seconds = totalSeconds % 60;
	snprintf(out, outSize, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
}

static void NSFFormatPlayTime(char* out, int outSize)
{
	uint32 fps;
	uint32 totalSeconds;
	uint32 minutes;
	uint32 seconds;

	if (!out || outSize <= 0)
		return;

	fps = NSFGetVisualizerFPS();
	if (!fps)
		fps = 60;

	totalSeconds = NSFPlayFrames / fps;
	minutes = totalSeconds / 60;
	seconds = totalSeconds % 60;

	snprintf(out, outSize, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
}

static void NSFDrawInfoPanel(uint8* XBuf)
{
	static const int kFgColor = kNSFColorWhite;
	char line[128];

	NSFDrawText(XBuf, 8, 8, "FCEUX NSF Player", kFgColor);

	snprintf(line, sizeof(line), "Title : %.28s", NSFHeader.SongName[0] ? (char*)NSFHeader.SongName : "(unknown)");
	NSFDrawText(XBuf, 8, 24, line, kFgColor);

	snprintf(line, sizeof(line), "Artist: %.28s", NSFHeader.Artist[0] ? (char*)NSFHeader.Artist : "(unknown)");
	NSFDrawText(XBuf, 8, 36, line, kFgColor);

	snprintf(line, sizeof(line), "Copyr: %.28s", NSFHeader.Copyright[0] ? (char*)NSFHeader.Copyright : "(unknown)");
	NSFDrawText(XBuf, 8, 48, line, kFgColor);

	if (NSFIsNSFe && NSFGetCurrentTrackLabel()[0])
	{
		snprintf(line, sizeof(line), "Label : %.24s", NSFGetCurrentTrackLabel());
		NSFDrawText(XBuf, 8, 60, line, kFgColor);
	}

	snprintf(line, sizeof(line), "Track : %02d/%02d", CurrentSong, NSFHeader.TotalSongs);
	NSFDrawText(XBuf, 8, 72, line, kFgColor);

	snprintf(line, sizeof(line), "State : %s", NSFGetPlaybackStateName());
	NSFDrawText(XBuf, 96, 72, line, kFgColor);

	snprintf(line, sizeof(line), "Start : %02d", NSFHeader.StartingSong);
	NSFDrawText(XBuf, 8, 84, line, kFgColor);

	{
		char timebuf[16];
		char totalbuf[16];
		int totalMs = NSFGetCurrentTrackTimeMs();
		NSFFormatPlayTime(timebuf, sizeof(timebuf));
		if (NSFIsNSFe && totalMs >= 0)
		{
			NSFFormatMS(totalbuf, sizeof(totalbuf), totalMs);
			snprintf(line, sizeof(line), "Time  : %s/%s", timebuf, totalbuf);
		}
		else
			snprintf(line, sizeof(line), "Time  : %s", timebuf);
		NSFDrawText(XBuf, 96, 84, line, kFgColor);
	}

	NSFDrawWaveMeterInfo(XBuf, 96, 96);
}

static int NSFClampSong(int song)
{
	if (NSFHeader.TotalSongs <= 0)
		return 1;

	if (song < 1)
		return 1;

	if (song > NSFHeader.TotalSongs)
		return NSFHeader.TotalSongs;

	return song;
}

static void NSFRequestSongReload(void)
{
	SongReload = 0xFF;
	NSFResetAPUShadow();
	NSFResetExpShadow();
	NSFResetPlayTimer();
	NSFResetPlayScheduler();
	NSFResetPlaybackControl();
	NSFResetWaveViewState();
}

static const char* NSFGetPlaybackStateName(void)
{
	if (NSFStopped)
		return "STOP";
	if (NSFPaused)
		return "PAUSE";
	return "PLAY";
}

int FCEUI_NSFGetCurrentSong(void)
{
	return CurrentSong;
}

int FCEUI_NSFGetTotalSongs(void)
{
	return NSFHeader.TotalSongs;
}

int FCEUI_NSFSetSong(int song)
{
	int newSong = NSFClampSong(song);

	if (newSong != CurrentSong)
	{
		CurrentSong = newSong;
		NSFRequestSongReload();
	}

	return CurrentSong;
}

int FCEUI_NSFChange(int amount)
{
	return FCEUI_NSFSetSong(CurrentSong + amount);
}

void FCEUI_NSFReloadSong(void)
{
	NSFRequestSongReload();
}

void FCEUI_NSFTogglePause(void)
{
	if (NSFStopped)
	{
		// Treat A after Stop as Play from the beginning of the current track.
		NSFRequestSongReload();
		return;
	}

	// Important: do not silence APU/expansion registers when pausing.
	// Several NSF engines only write channel registers when notes change. If we
	// clear the registers on pause, resume will miss sustained notes until the
	// engine writes the next note.  Therefore A/Pause only stops the NSF play
	// routine and mutes the final output volume.  B/Stop is the only command
	// that explicitly clears channel registers.
	NSFPaused = !NSFPaused;
	NSFSetOutputMute(NSFPaused ? 1 : 0);
}

void FCEUI_NSFStopSong(void)
{
	NSFSetOutputMute(0);
	NSFStopped = 1;
	NSFPaused = 0;
	NSFResetPlayTimer();
	NSFResetWaveViewState();
	NSFSilenceAllChannels();
}

int FCEUI_NSFIsPaused(void)
{
	return NSFPaused ? 1 : 0;
}

int FCEUI_NSFIsStopped(void)
{
	return NSFStopped ? 1 : 0;
}

int FCEUI_NSFIsOutputMuted(void)
{
	return NSFOutputMuteActive ? 1 : 0;
}

void FCEUI_NSFGetFullInfo(FCEU_NSFInfo* info)
{
	if (!info)
		return;

	memset(info, 0, sizeof(FCEU_NSFInfo));

	strncpy(info->songName, (const char*)NSFHeader.SongName, sizeof(info->songName) - 1);
	strncpy(info->artist, (const char*)NSFHeader.Artist, sizeof(info->artist) - 1);
	strncpy(info->copyright, (const char*)NSFHeader.Copyright, sizeof(info->copyright) - 1);

	info->totalSongs = NSFHeader.TotalSongs;
	info->startingSong = NSFHeader.StartingSong;
	info->currentSong = CurrentSong;

	info->loadAddr = LoadAddr;
	info->initAddr = InitAddr;
	info->playAddr = PlayAddr;

	info->videoSystem = NSFHeader.VideoSystem;
	info->soundChip = NSFHeader.SoundChip;

	for (int i = 0; i < 8; i++)
		info->bankSwitch[i] = NSFHeader.BankSwitch[i];

	info->bankSwitched = BSon ? 1 : 0;
	info->maxBank = NSFMaxBank;
	info->nsfSize = NSFSize;

	info->paused = NSFPaused ? 1 : 0;
	info->stopped = NSFStopped ? 1 : 0;
	info->outputMuted = NSFOutputMuteActive ? 1 : 0;

	info->isNSFe = NSFIsNSFe ? 1 : 0;
	strncpy(info->trackLabel, NSFGetCurrentTrackLabel(), sizeof(info->trackLabel) - 1);
	info->trackLabel[sizeof(info->trackLabel) - 1] = 0;
	info->trackTimeMs = NSFGetCurrentTrackTimeMs();
	info->trackFadeMs = NSFGetCurrentTrackFadeMs();
	strncpy(info->ripper, NSFRipper, sizeof(info->ripper) - 1);
	info->ripper[sizeof(info->ripper) - 1] = 0;
}

void DrawNSF(uint8* XBuf)
{
	if (vismode == 0) return;

	memset(XBuf, 0, 256 * 240);
	memset(XDBuf, 0, 256 * 240);

	NSFDrawStarfield(XBuf);

	NSFDrawInfoPanel(XBuf);
	NSFDrawChannelLevelPanel(XBuf, 166, 8);
	NSFDrawWaveView(XBuf, 16, 104, 224, 50);
	NSFDrawKeyboardView(XBuf, 16, 178, 224, 50);

	{
		static uint8 last = 0;
		uint8 tmp;
		tmp = FCEU_GetJoyJoy();
		if ((tmp & JOY_RIGHT) && !(last & JOY_RIGHT))
		{
			FCEUI_NSFChange(1);
		}
		else if ((tmp & JOY_LEFT) && !(last & JOY_LEFT))
		{
			FCEUI_NSFChange(-1);
		}
		else if ((tmp & JOY_UP) && !(last & JOY_UP))
		{
			FCEUI_NSFChange(10);
		}
		else if ((tmp & JOY_DOWN) && !(last & JOY_DOWN))
		{
			FCEUI_NSFChange(-10);
		}
		else if ((tmp & JOY_START) && !(last & JOY_START))
			FCEUI_NSFReloadSong();
		else if ((tmp & JOY_A) && !(last & JOY_A))
			FCEUI_NSFTogglePause();
		else if ((tmp & JOY_B) && !(last & JOY_B))
			FCEUI_NSFStopSong();
		last = tmp;
	}
}

void DoNSFFrame(void)
{
	if (!NSFPaused && !NSFStopped)
	{
		uint8 calls = NSFComputePlayCallsForFrame();

		/* On song reload we must enter the NSF trampoline so the init routine is
		 * called even if the rate accumulator would otherwise produce 0 play calls
		 * for this video frame.
		 */
		if (SongReload && calls == 0)
			calls = 1;

		NSFPlayCallCount = calls;

		if (((NSFNMIFlags & 1) && SongReload) || ((NSFNMIFlags & 2) && calls > 0))
			TriggerNMI();

		// Count playback time after the NSF play routine has started.
		// This is UI-only timing, reset whenever the track is changed/reloaded.
		if ((NSFNMIFlags & 2) && !SongReload)
			NSFPlayFrames++;
	}
}

void FCEUI_NSFSetVis(int mode)
{
	vismode = mode;
}

//Returns total songs
int FCEUI_NSFGetInfo(uint8* name, uint8* artist, uint8* copyright, int maxlen)
{
	if (maxlen > 0)
	{
		if (name)
		{
			strncpy((char*)name, (char*)NSFHeader.SongName, maxlen - 1); //mbg merge 7/17/06 added casts
			name[maxlen - 1] = 0;
		}

		if (artist)
		{
			strncpy((char*)artist, (char*)NSFHeader.Artist, maxlen - 1); //mbg merge 7/17/06 added casts
			artist[maxlen - 1] = 0;
		}

		if (copyright)
		{
			strncpy((char*)copyright, (char*)NSFHeader.Copyright, maxlen - 1); //mbg merge 7/17/06 added casts
			copyright[maxlen - 1] = 0;
		}
	}

	return(NSFHeader.TotalSongs);
}
