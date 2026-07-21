/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2005 CaH4e3
 *
 * Mapper 167 corrections based on the NRS implementation:
 *  - Exact PRG banking modes
 *  - Register-controlled mirroring
 *  - 8 KiB PRG-RAM at $6000-$7FFF
 *  - 8 KiB CHR-RAM
 *  - Soft reset preserves mapper registers
 *
 * Mapper 166 behavior is intentionally kept separate and unchanged.
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
	SUBOR_RAM_CHIP = 0x10
};

static uint8 is167;
static uint8 regs[4];

static uint8* WRAM = NULL;
static uint32 WRAMSIZE;

static uint8* CHRRAM = NULL;
static uint32 CHRRAMSIZE;

static SFORMAT StateRegs[] =
{
	{ regs, 4, "DREG" },
	{ 0 }
};

/* ------------------------------------------------------------------------- */
/* Mapper 166: preserve the original FCEUX behavior                          */
/* ------------------------------------------------------------------------- */

static void Sync166(void)
{
	int base;
	int bank;

	base = ((regs[0] ^ regs[1]) & 0x10) << 1;
	bank = (regs[2] ^ regs[3]) & 0x1F;

	if (regs[1] & 0x08) {
		bank &= 0xFE;
		setprg16(0x8000, base + bank);
		setprg16(0xC000, base + bank + 1);
	}
	else if (regs[1] & 0x04) {
		setprg16(0x8000, 0x1F);
		setprg16(0xC000, base + bank);
	}
	else {
		setprg16(0x8000, base + bank);
		setprg16(0xC000, 0x07);
	}

	setchr8(0);
}

/* ------------------------------------------------------------------------- */
/* Mapper 167: match the NRS implementation exactly                          */
/* ------------------------------------------------------------------------- */

static void Sync167(void)
{
	uint32 prg;

	/*
	 * NRS:
	 *   prg = ((reg[0] ^ reg[1]) << 1 & $20)
	 *       | ((reg[2] ^ reg[3])      & $1F);
	 */
	prg =
		(((uint32)(regs[0] ^ regs[1]) << 1) & 0x20) |
		((regs[2] ^ regs[3]) & 0x1F);

	/*
	 * NRS selects the mode with both bits 2 and 3 of register 1.
	 *
	 * Mode 0:
	 *   $8000 = selected bank
	 *   $C000 = bank $20
	 *
	 * Mode 1:
	 *   $8000 = bank $1F
	 *   $C000 = selected bank
	 *
	 * Modes 2 and 3:
	 *   reversed 32 KiB pair
	 *   $8000 = odd bank
	 *   $C000 = even bank
	 */
	switch ((regs[1] >> 2) & 0x03) {
	case 0:
		setprg16(0x8000, prg);
		setprg16(0xC000, 0x20);
		break;

	case 1:
		setprg16(0x8000, 0x1F);
		setprg16(0xC000, prg);
		break;

	default:
		setprg16(0x8000, prg | 1);
		setprg16(0xC000, prg & ~1);
		break;
	}

	/* NRS maps bank 0 of the cartridge's 8 KiB PRG-RAM and CHR-RAM. */
	setprg8r(SUBOR_RAM_CHIP, 0x6000, 0);
	setchr8r(SUBOR_RAM_CHIP, 0);

	/*
	 * Register 0 bit 0:
	 *   0 = vertical mirroring
	 *   1 = horizontal mirroring
	 */
	setmirror((regs[0] & 0x01) ? MI_H : MI_V);
}

static void Sync(void)
{
	if (is167)
		Sync167();
	else
		Sync166();
}

/* ------------------------------------------------------------------------- */
/* CPU handlers                                                              */
/* ------------------------------------------------------------------------- */

static DECLFW(M166Write)
{
	/*
	 * $8000-$9FFF -> reg 0
	 * $A000-$BFFF -> reg 1
	 * $C000-$DFFF -> reg 2
	 * $E000-$FFFF -> reg 3
	 *
	 * This is equivalent to NRS' bank >> 1 & 3 decoding.
	 */
	regs[(A >> 13) & 0x03] = V;
	Sync();
}

static void InstallHandlers(void)
{
	if (is167) {
		SetReadHandler(0x6000, 0x7FFF, CartBR);
		SetWriteHandler(0x6000, 0x7FFF, CartBW);
	}

	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xFFFF, M166Write);
}

/* ------------------------------------------------------------------------- */
/* Reset/state/close                                                         */
/* ------------------------------------------------------------------------- */

static void M166Power(void)
{
	regs[0] = 0;
	regs[1] = 0;
	regs[2] = 0;
	regs[3] = 0;

	Sync();
	InstallHandlers();
}

static void M166Reset(void)
{
	/*
	 * NRS only clears the four registers on RESET_HARD.
	 * FCEUX Power corresponds to hard power-on, while Reset is a soft reset,
	 * so preserve the registers here and only reapply the mapping.
	 */
	Sync();
	InstallHandlers();
}

static void StateRestore(int version)
{
	(void)version;
	Sync();
	InstallHandlers();
}

static void M167Close(void)
{
	if (WRAM)
		FCEU_gfree(WRAM);

	if (CHRRAM)
		FCEU_gfree(CHRRAM);

	WRAM = NULL;
	CHRRAM = NULL;
	WRAMSIZE = 0;
	CHRRAMSIZE = 0;
}

/* ------------------------------------------------------------------------- */
/* Initialization                                                            */
/* ------------------------------------------------------------------------- */

void Mapper166_Init(CartInfo* info)
{
	is167 = 0;

	info->Power = M166Power;
	info->Reset = M166Reset;

	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}

void Mapper167_Init(CartInfo* info)
{
	is167 = 1;

	/*
	 * NRS explicitly maps one 8 KiB PRG-RAM bank and one 8 KiB CHR-RAM
	 * bank.  Allocate them explicitly instead of relying on the presence or
	 * absence of CHR-ROM in the image header.
	 */
	WRAMSIZE = 8 * 1024;
	CHRRAMSIZE = 8 * 1024;

	WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
	CHRRAM = (uint8*)FCEU_gmalloc(CHRRAMSIZE);

	memset(WRAM, 0, WRAMSIZE);
	memset(CHRRAM, 0, CHRRAMSIZE);

	SetupCartPRGMapping(
		SUBOR_RAM_CHIP,
		WRAM,
		WRAMSIZE,
		1
	);

	SetupCartCHRMapping(
		SUBOR_RAM_CHIP,
		CHRRAM,
		CHRRAMSIZE,
		1
	);

	info->Power = M166Power;
	info->Reset = M166Reset;
	info->Close = M167Close;

	GameStateRestore = StateRestore;

	AddExState(&StateRegs, ~0, 0, 0);
	AddExState(WRAM, WRAMSIZE, 0, "WRAM");
	AddExState(CHRRAM, CHRRAMSIZE, 0, "CRAM");

	if (info->battery)
		info->addSaveGameBuf(WRAM, WRAMSIZE);
}