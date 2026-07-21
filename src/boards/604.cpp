/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 604 - Dancing Expert
 *
 * Implementation based on the NRS/Nintendulator mapper description.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mapinc.h"

static uint8 reg4016;
static uint8 reg[4];
static writefunc old4016Write;

static SFORMAT StateRegs[] =
{
	{ reg, 4, "REGS" },
	{ &reg4016, 1, "4016" },
	{ 0 }
};

static void M604Sync(void)
{
	/* $8000-$BFFF: switchable 16 KiB PRG bank. */
	setprg16(0x8000, reg[0]);

	/* $C000-$FFFF: fixed 16 KiB PRG bank $FF. */
	setprg16(0xC000, 0xFF);

	/*
	 * CHR bank bits:
	 *   A1-A0 = register 1 bits 1-0
	 *   higher bits = complete value last written to $4016
	 */
	setchr8((reg[1] & 0x03) | (reg4016 << 2));

	/*
	 * Nintendulator calls iNES_SetMirroring() here. FCEUX already installs
	 * the hard-wired iNES/NES 2.0 mirroring before the mapper is initialized,
	 * so no mapper-controlled mirroring operation is required.
	 */
}

static DECLFW(M604Write4016)
{
	/* Preserve the normal controller-strobe/APU handling at $4016. */
	if (old4016Write && old4016Write != M604Write4016)
		old4016Write(A, V);

	reg4016 = V;
	M604Sync();
}

static DECLFW(M604WriteReg)
{
	/*
	 * NRS address decoding:
	 *   mapper handler is installed for CPU page $5xxx;
	 *   A11=0 enables writes, giving $5000-$57FF;
	 *   A9-A8 select one of four registers and repeat at $5400-$57FF.
	 */
	reg[(A >> 8) & 0x03] = V;
	M604Sync();
}

static void M604Reset(void)
{
	reg4016 = 0;
	reg[0] = 0;
	reg[1] = 0;
	reg[2] = 0;
	reg[3] = 0;
	M604Sync();
}

static void M604Power(void)
{
	writefunc current4016Write;

	M604Reset();

	SetReadHandler(0x8000, 0xFFFF, CartBR);

	/*
	 * Hook only $4016 instead of the whole $4000-$4FFF page. This reproduces
	 * Mapper 604's extra CHR latch while leaving every other APU/I/O handler
	 * untouched.
	 */
	current4016Write = GetWriteHandler(0x4016);
	if (current4016Write != M604Write4016)
		old4016Write = current4016Write;
	SetWriteHandler(0x4016, 0x4016, M604Write4016);

	/* A11=0 portion of the $5xxx mapper register page. */
	SetWriteHandler(0x5000, 0x57FF, M604WriteReg);
}

static void StateRestore(int version)
{
	//(void)version;
	M604Sync();
}

void Mapper604_Init(CartInfo* info)
{
	old4016Write = 0;

	info->Power = M604Power;
	info->Reset = M604Reset;
	GameStateRestore = StateRestore;

	AddExState(StateRegs, ~0, 0, 0);
}