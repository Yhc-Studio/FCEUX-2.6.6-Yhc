/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2012 CaH4e3
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

#include "mapinc.h"

static uint8 vrc7idx, preg[3], creg[8], mirr;
static uint8 IRQLatch, IRQa, IRQd, IRQMode;
static int32 IRQCount, CycleCount;
static uint8* WRAM = NULL;
static uint32 WRAMSIZE = 0;

#include "emu2413.h"

static int32 dwave = 0;
static OPLL* VRC7Sound = NULL;
static OPLL** VRC7Sound_saveptr = &VRC7Sound;

static SFORMAT StateRegs[] =
{
	{ &vrc7idx, 1, "VRCI" },
	{ preg, 3, "PREG" },
	{ creg, 8, "CREG" },
	{ &mirr, 1, "MIRR" },
	{ &IRQa, 1, "IRQA" },
	{ &IRQd, 1, "IRQD" },
	{ &IRQLatch, 1, "IRQL" },
	{ &IRQCount, 4, "IRQC" },
	{ &CycleCount, 4, "CYCC" },
	{ (void**)VRC7Sound_saveptr, sizeof(*VRC7Sound) | FCEUSTATE_INDIRECT, "VRC7"  },
	{ &IRQMode, 1, "IRQM" },
	{0}
};

// VRC7 Sound


#define VRC7_MIXBUF_SIZE 4096
static int32 VRC7MixBuf[VRC7_MIXBUF_SIZE];

/*
 * VRC7/OPLL has long continuous FM output.
 * If the UI changes FSettings.VRC7Volume in the middle of playback and we
 * immediately jump from old gain to new gain, the waveform amplitude becomes
 * discontinuous for one buffer, which is heard as a pop/click.
 *
 * Keep a private applied gain and ramp it to the target gain inside the next
 * generated audio block.  Other chip state is untouched.
 */
static int32 VRC7CurrentVolume = -1;

/*
 * VRC7 is used in two different situations:
 *  1) normal Mapper 85 cartridges, such as Lagrange Point;
 *  2) NSF/NSFe playback, where VRC7 may be mixed with VRC6/FDS/MMC5/etc.
 *
 * The NSF multi-expansion mixer needs a temporary buffer and additive mixing so
 * that VRC7 does not overwrite other expansion chips.  However, using that path
 * for normal Mapper 85 games can subtly change OPLL buffer timing/behavior and
 * may cause short notes/SFX to sound jumpy.  Therefore Mapper 85 cartridges are
 * kept on the exact legacy OPLL_fillbuf path, while NSF/NSFe uses the safe
 * additive/scaled path.
 */
static int VRC7NSFMode = 0;

static INLINE int32 VRC7ClampVolume(int32 v)
{
	if (v < 0) return 0;
	if (v > 512) return 512;
	return v;
}

static void VRC7FillScaled(int32* dst, int count, int quality)
{
	int done = 0;
	int32 targetVolume = VRC7ClampVolume(FSettings.VRC7Volume);
	int32 startVolume;

	if (!dst || !VRC7Sound || count <= 0)
		return;

	if (VRC7CurrentVolume < 0)
		VRC7CurrentVolume = targetVolume;

	startVolume = VRC7CurrentVolume;

	while (done < count)
	{
		int todo = count - done;
		if (todo > VRC7_MIXBUF_SIZE)
			todo = VRC7_MIXBUF_SIZE;

		memset(VRC7MixBuf, 0, sizeof(int32) * todo);
		OPLL_fillbuf(VRC7Sound, VRC7MixBuf, todo, quality);

		for (int i = 0; i < todo; i++)
		{
			int sampleIndex = done + i + 1;
			int32 vol = startVolume;

			if (targetVolume != startVolume)
				vol = startVolume + (int32)(((int64)(targetVolume - startVolume) * sampleIndex) / count);

			dst[done + i] += (int32)(((int64)VRC7MixBuf[i] * vol) >> 8);
		}

		done += todo;
	}

	VRC7CurrentVolume = targetVolume;
}

void DoVRC7Sound(void) {
	int32 z, a;
	if (FSettings.soundq >= 1)
		return;
	z = ((SOUNDTS << 16) / soundtsinc) >> 4;
	a = z - dwave;

	if (VRC7Sound && a)
	{
		/*
		 * Mapper85/VRC7 cartridge path:
		 * keep the exact legacy OPLL_fillbuf() call style.  This avoids the
		 * one-frame skip/jump that can be heard in Lagrange Point's short menu
		 * confirmation SFX when the NSF additive/scaled path is used for carts.
		 */
		if (!VRC7NSFMode)
			OPLL_fillbuf(VRC7Sound, &Wave[dwave], a, 1);
		else
			VRC7FillScaled(&Wave[dwave], a, 1);
	}
	dwave += a;
}

void UpdateOPLNEO(int32* Wave, int Count) {
	if (VRC7Sound && Count)
	{
		if (!VRC7NSFMode)
			OPLL_fillbuf(VRC7Sound, Wave, Count, 4);
		else
			VRC7FillScaled(Wave, Count, 4);
	}
}

void UpdateOPL(int Count) {
	int32 z, a;
	z = ((SOUNDTS << 16) / soundtsinc) >> 4;
	a = z - dwave;
	if (VRC7Sound && a)
	{
		if (!VRC7NSFMode)
			OPLL_fillbuf(VRC7Sound, &Wave[dwave], a, 1);
		else
			VRC7FillScaled(&Wave[dwave], a, 1);
	}
	dwave = 0;
}

static void VRC7SC(void) {
	if (VRC7Sound)
		OPLL_set_rate(VRC7Sound, FSettings.SndRate);
}

static void VRC7SKill(void) {
	if (VRC7Sound)
		OPLL_delete(VRC7Sound);
	VRC7Sound = NULL;
	VRC7CurrentVolume = -1;
	VRC7NSFMode = 0;
}

static void VRC7_ESI(void) {
	GameExpSound.RChange = VRC7SC;
	GameExpSound.Kill = VRC7SKill;
	VRC7Sound = OPLL_new(3579545, FSettings.SndRate ? FSettings.SndRate : 48000);
	OPLL_reset(VRC7Sound);
	OPLL_reset(VRC7Sound);
	dwave = 0;
	VRC7CurrentVolume = VRC7ClampVolume(FSettings.VRC7Volume);
}

// VRC7 Sound

static void Sync(void) {
	uint8 i;
	setprg8r(0x10, 0x6000, 0);
	setprg8(0x8000, preg[0]);
	setprg8(0xA000, preg[1]);
	setprg8(0xC000, preg[2]);
	setprg8(0xE000, ~0);
	for (i = 0; i < 8; i++)
		setchr1(i << 10, creg[i]);
	switch (mirr & 3) {
	case 0: setmirror(MI_V); break;
	case 1: setmirror(MI_H); break;
	case 2: setmirror(MI_0); break;
	case 3: setmirror(MI_1); break;
	}
}

static DECLFW(VRC7SW) {
	if (FSettings.SndRate) {
		OPLL_writeReg(VRC7Sound, vrc7idx, V);
		GameExpSound.Fill = UpdateOPL;
		GameExpSound.NeoFill = UpdateOPLNEO;
	}
}

static DECLFW(VRC7Write) {
	A |= (A & 8) << 1;  // another two-in-oooone
	if (A >= 0xA000 && A <= 0xDFFF) {
		A &= 0xF010;
		creg[((A >> 4) & 1) | ((A - 0xA000) >> 11)] = V;
		Sync();
	}
	else if (A == 0x9030) {
		VRC7SW(A, V);
	}
	else switch (A & 0xF010) {
	case 0x8000: preg[0] = V; Sync(); break;
	case 0x8010: preg[1] = V; Sync(); break;
	case 0x9000: preg[2] = V; Sync(); break;
	case 0x9010: vrc7idx = V; break;
	case 0xE000: mirr = V & 3; Sync(); break;
	case 0xE010: IRQLatch = V; X6502_IRQEnd(FCEU_IQEXT); break;
	case 0xF000:
		IRQMode = V & 4;
		IRQa = V & 2;
		IRQd = V & 1;
		if (V & 2)
			IRQCount = IRQLatch;
		CycleCount = 0;
		X6502_IRQEnd(FCEU_IQEXT);
		break;
	case 0xF010:
		IRQa = IRQd;
		X6502_IRQEnd(FCEU_IQEXT);
		break;
	}
}

static void VRC7Power(void) {
	Sync();
	SetWriteHandler(0x6000, 0x7FFF, CartBW);
	SetReadHandler(0x6000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xFFFF, VRC7Write);
	FCEU_CheatAddRAM(WRAMSIZE >> 10, 0x6000, WRAM);
}

static void VRC7Close(void)
{
	if (WRAM)
		FCEU_gfree(WRAM);
	WRAM = NULL;
}

static void VRC7IRQHook(int a) {
	if (IRQa) {
		if (IRQMode) {
			CycleCount += a;
			while (CycleCount > 0) {
				CycleCount--;
				IRQCount++;
				if (IRQCount & 0x100) {
					X6502_IRQBegin(FCEU_IQEXT);
					IRQCount = IRQLatch;
				}
			}
		}
		else {
			CycleCount += a * 3;
			while (CycleCount >= 341) {
				CycleCount -= 341;
				IRQCount++;
				if (IRQCount == 0x100) {
					IRQCount = IRQLatch;
					X6502_IRQBegin(FCEU_IQEXT);
				}
			}
		}
	}
}

static void StateRestore(int version) {
	Sync();
}

void Mapper85_Init(CartInfo* info) {
	info->Power = VRC7Power;
	info->Close = VRC7Close;
	MapIRQHook = VRC7IRQHook;
	WRAMSIZE = 8192;
	WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
	SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, 1);
	AddExState(WRAM, WRAMSIZE, 0, "WRAM");
	if (info->battery) {
		info->addSaveGameBuf(WRAM, WRAMSIZE);
	}
	GameStateRestore = StateRestore;
	VRC7NSFMode = 0;
	VRC7_ESI();
	AddExState(&StateRegs, ~0, 0, 0);
}

void NSFVRC7_Init(void) {
	SetWriteHandler(0x9010, 0x901F, VRC7Write);
	SetWriteHandler(0x9030, 0x903F, VRC7Write);
	VRC7NSFMode = 1;
	VRC7_ESI();
}
