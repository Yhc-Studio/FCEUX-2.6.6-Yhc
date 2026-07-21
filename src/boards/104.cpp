/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 104 - Pegasus 5-in-1
 *
 * Ported from the Nintendulator/NRS Mapper 104 implementation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mapinc.h"

#include <string.h>

enum
{
	M104_CHRRAM_CHIP = 0x10,
	M104_CHRRAM_SIZE = 8 * 1024,
	M104_UNLOCK_CYCLES = 120000
};

static uint8 inner;
static uint8 outer;
static int32 cycles;

static uint8 *m104CHRRAM = NULL;

static SFORMAT M104StateRegs[] =
{
	{ &inner,  1,                    "INNR" },
	{ &outer,  1,                    "OUTR" },
	{ &cycles, 4 | FCEUSTATE_RLSB,   "CYCL" },
	{ 0 }
};

static void M104Sync(void)
{
	/*
	 * PRG bank numbers are in 16 KiB units.
	 *
	 * $8000-$BFFF:
	 *     outer supplies bits 7-4;
	 *     inner supplies bits 3-0.
	 *
	 * $C000-$FFFF:
	 *     fixed to bank $xF inside the selected outer block.
	 */
	setprg16(
		0x8000,
		((uint32)outer << 4) | (inner & 0x0F)
	);

	setprg16(
		0xC000,
		((uint32)outer << 4) | 0x0F
	);

	setchr8r(M104_CHRRAM_CHIP, 0);

	/*
	 * NRS calls iNES_SetMirroring().  FCEUX has already installed the
	 * hard-wired mirroring described by the iNES/NES 2.0 header, so no
	 * mapper-controlled setmirror() call is needed.
	 */
}

static DECLFW(M104WriteOuter)
{
	(void)A;

	/*
	 * The outer register may only be written after 120000 CPU cycles.
	 * Once outer bit 3 becomes set, the register is permanently locked
	 * until the next hard power cycle.
	 */
	if (!(outer & 0x08) && cycles >= M104_UNLOCK_CYCLES)
	{
		outer = V;
		M104Sync();
	}
}

static DECLFW(M104WriteInner)
{
	(void)A;

	inner = V;
	M104Sync();
}

static void M104CycleHook(int elapsedCycles)
{
	if (cycles < M104_UNLOCK_CYCLES)
	{
		cycles += elapsedCycles;

		if (cycles > M104_UNLOCK_CYCLES)
			cycles = M104_UNLOCK_CYCLES;
	}
}

static void M104InstallHandlers(void)
{
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xBFFF, M104WriteOuter);
	SetWriteHandler(0xC000, 0xFFFF, M104WriteInner);

	MapIRQHook = M104CycleHook;
}

static void M104Power(void)
{
	outer = 0;
	inner = 0;
	cycles = 0;

	if (m104CHRRAM)
		FCEU_MemoryRand(m104CHRRAM, M104_CHRRAM_SIZE);

	M104Sync();
	M104InstallHandlers();
}

static void M104Reset(void)
{
	/*
	 * NRS clears mapper state only on RESET_HARD.  FCEUX Reset is a soft
	 * reset, so outer/inner/cycles and the lock state are preserved.
	 */
	M104Sync();
	M104InstallHandlers();
}

static void M104StateRestore(int version)
{
	(void)version;

	M104Sync();
	M104InstallHandlers();
}

static void M104Close(void)
{
	if (m104CHRRAM)
	{
		FCEU_gfree(m104CHRRAM);
		m104CHRRAM = NULL;
	}
}

void Mapper104_Init(CartInfo *info)
{
	m104CHRRAM = (uint8 *)FCEU_gmalloc(M104_CHRRAM_SIZE);
	memset(m104CHRRAM, 0, M104_CHRRAM_SIZE);

	SetupCartCHRMapping(
		M104_CHRRAM_CHIP,
		m104CHRRAM,
		M104_CHRRAM_SIZE,
		1
	);

	AddExState(
		m104CHRRAM,
		M104_CHRRAM_SIZE,
		0,
		"CRAM"
	);

	info->Power = M104Power;
	info->Reset = M104Reset;
	info->Close = M104Close;

	GameStateRestore = M104StateRestore;

	AddExState(M104StateRegs, ~0, 0, 0);
}
