/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 55 - NCN-35A
 *
 * Ported from the Nintendulator/NRS Mapper 55 implementation.
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
	M55_WRAM_CHIP = 0x10,
	M55_WRAM_SIZE = 4 * 1024
};

static uint8 *m55WRAM = NULL;

static void M55Sync(void)
{
	/* $6000-$6FFF: PRG-ROM 4 KiB bank 8. */
	setprg4(0x6000, 8);

	/* $7000-$7FFF: PRG-RAM 4 KiB bank 0. */
	setprg4r(M55_WRAM_CHIP, 0x7000, 0);

	/* $8000-$FFFF: PRG-ROM 32 KiB bank 0. */
	setprg32(0x8000, 0);

	/* $0000-$1FFF: CHR-ROM 8 KiB bank 0. */
	setchr8(0);

	/*
	 * NRS calls iNES_SetMirroring().  FCEUX already installed the fixed
	 * mirroring from the cartridge header.
	 */
}

static void M55InstallHandlers(void)
{
	SetReadHandler(0x6000, 0xFFFF, CartBR);

	/*
	 * Only $7000-$7FFF is writable RAM.  $6000-$6FFF remains read-only ROM.
	 */
	SetWriteHandler(0x7000, 0x7FFF, CartBW);
}

static void M55Power(void)
{
	M55Sync();
	M55InstallHandlers();
}

static void M55Reset(void)
{
	M55Sync();
	M55InstallHandlers();
}

static void M55StateRestore(int version)
{
	(void)version;

	M55Sync();
	M55InstallHandlers();
}

static void M55Close(void)
{
	if (m55WRAM)
	{
		FCEU_gfree(m55WRAM);
		m55WRAM = NULL;
	}
}

void Mapper55_Init(CartInfo *info)
{
	m55WRAM = (uint8 *)FCEU_gmalloc(M55_WRAM_SIZE);
	memset(m55WRAM, 0, M55_WRAM_SIZE);

	SetupCartPRGMapping(
		M55_WRAM_CHIP,
		m55WRAM,
		M55_WRAM_SIZE,
		1
	);

	AddExState(
		m55WRAM,
		M55_WRAM_SIZE,
		0,
		"WRAM"
	);

	if (info->battery)
		info->addSaveGameBuf(m55WRAM, M55_WRAM_SIZE);

	info->Power = M55Power;
	info->Reset = M55Reset;
	info->Close = M55Close;

	GameStateRestore = M55StateRestore;
}
