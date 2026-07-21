/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 682 - Rainbow Mapper
 * Mapper 3872 - Rainbow Mapper v1.3 compatibility alias
 *
 * This is a port of the current NRS/Nintendulator implementation supplied
 * with the mapper, with three obvious PRG mapping copy/paste errors corrected:
 *   - mode 1 now selects PRG-RAM when C=1;
 *   - mode 2 maps ROM (not RAM) when the first window has C=0;
 *   - mode 2 maps the final 8 KiB window at $E000 (not $C000).
 *
 * The NRS implementation is intentionally incomplete compared with the full
 * Rainbow specification.  It currently implements only:
 *   - PRG modes 0-4 at CPU $8000-$FFFF;
 *   - PRG-ROM/PRG-RAM chip selection for those windows;
 *   - fixed CHR-RAM bank 0;
 *   - vertical mirroring;
 *   - the Mapper 3872 / Rainbow v1.3 $4120 -> $4118 compatibility write.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mapinc.h"

#define M682_RAM_CHIP 0x10
#define M682_DEFAULT_RAM_SIZE (32 * 1024)

static uint8 reg[0x100];
static uint8 legacyV13;

static uint8* PRGRAM = NULL;
static uint32 PRGRAMSIZE;
static uint8* CHRRAM = NULL;
static uint32 CHRRAMSIZE;

static SFORMAT StateRegs[] =
{
	{ reg, 0x100, "REGS" },
	{ 0 }
};

static uint32 M682Bank(uint32 upperIndex, uint32 lowerIndex)
{
	/*
	 * Bit 7 of the upper register selects PRG-RAM.  FCEUX masks the resulting
	 * bank number to the selected ROM/RAM chip size, so retaining the complete
	 * byte here also preserves bank upper bits 2-0 for the 4 KiB/8 KiB modes.
	 */
	return ((uint32)reg[upperIndex] << 8) | reg[lowerIndex];
}

static void M682MapPRG4(uint32 address, uint32 upperIndex, uint32 lowerIndex)
{
	uint32 bank = M682Bank(upperIndex, lowerIndex);

	if (reg[upperIndex] & 0x80)
		setprg4r(M682_RAM_CHIP, address, bank);
	else
		setprg4(address, bank);
}

static void M682MapPRG8(uint32 address, uint32 upperIndex, uint32 lowerIndex)
{
	uint32 bank = M682Bank(upperIndex, lowerIndex);

	if (reg[upperIndex] & 0x80)
		setprg8r(M682_RAM_CHIP, address, bank);
	else
		setprg8(address, bank);
}

static void M682MapPRG16(uint32 address, uint32 upperIndex, uint32 lowerIndex)
{
	uint32 bank = M682Bank(upperIndex, lowerIndex);

	if (reg[upperIndex] & 0x80)
		setprg16r(M682_RAM_CHIP, address, bank);
	else
		setprg16(address, bank);
}

static void M682MapPRG32(uint32 address, uint32 upperIndex, uint32 lowerIndex)
{
	uint32 bank = M682Bank(upperIndex, lowerIndex);

	if (reg[upperIndex] & 0x80)
		setprg32r(M682_RAM_CHIP, address, bank);
	else
		setprg32(address, bank);
}

static void M682Sync(void)
{
	uint32 bank;

	switch (reg[0x00] & 0x07)
	{
	case 0x00:
		/* $8000-$FFFF: one 32 KiB PRG-ROM/PRG-RAM window. */
		M682MapPRG32(0x8000, 0x08, 0x18);
		break;

	case 0x01:
		/* $8000-$BFFF and $C000-$FFFF: two 16 KiB windows. */
		M682MapPRG16(0x8000, 0x08, 0x18);
		M682MapPRG16(0xC000, 0x0C, 0x1C);
		break;

	case 0x02:
		/* 16 KiB + 8 KiB + 8 KiB. */
		M682MapPRG16(0x8000, 0x08, 0x18);
		M682MapPRG8(0xC000, 0x0C, 0x1C);
		M682MapPRG8(0xE000, 0x0E, 0x1E);
		break;

	case 0x03:
		/* Four independently selectable 8 KiB windows. */
		for (bank = 0; bank < 4; bank++)
		{
			uint32 address = 0x8000 + bank * 0x2000;
			uint32 upper = 0x08 + bank * 2;
			M682MapPRG8(address, upper, upper + 0x10);
		}
		break;

	case 0x04:
	case 0x05:
	case 0x06:
	case 0x07:
		/* Eight independently selectable 4 KiB windows. */
		for (bank = 0; bank < 8; bank++)
		{
			uint32 address = 0x8000 + bank * 0x1000;
			uint32 upper = 0x08 + bank;
			M682MapPRG4(address, upper, upper + 0x10);
		}
		break;
	}

	/* Match the currently supplied NRS implementation. */
	setchr8r(M682_RAM_CHIP, 0);
	setmirror(MI_V);
}

static DECLFW(M682WriteReg)
{
	uint32 address = A;

	/*
	 * Rainbow v1.3 used $4120 for the register now located at $4118.
	 * NRS exposes that older revision as NES 2.0 Mapper 3872.
	 */
	if (legacyV13 && address == 0x4120)
		address = 0x4118;

	/*
	 * NRS installs one handler over CPU page $4xxx, forwards normal APU writes,
	 * and accepts a mapper write whenever address bit A8 is set.  FCEUX keeps
	 * its normal APU handlers and installs this mapper handler only on the eight
	 * matching 256-byte alias ranges, so this test is normally already true.
	 */
	if (address & 0x0100)
	{
		reg[address & 0x00FF] = V;
		M682Sync();
	}
}

static void M682InstallHandlers(void)
{
	uint32 address;

	/* PRG-ROM and PRG-RAM may both occupy $8000-$FFFF. */
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xFFFF, CartBW);

	/*
	 * Exact NRS address decode: A8=1, A11-A9 ignored.
	 * This produces aliases at $4100, $4300, ... $4F00.
	 * Installing only those ranges avoids replacing FCEUX's APU/I/O handlers.
	 */
	for (address = 0x4100; address <= 0x4F00; address += 0x0200)
		SetWriteHandler(address, address + 0x00FF, M682WriteReg);
}

static void M682ResetRegisters(void)
{
	memset(reg, 0, sizeof(reg));

	if (legacyV13)
	{
		/* Initial bank values used by Rainbow Mapper v1.3. */
		reg[0x18] = 0xFF;
		reg[0x1C] = 0xFF;
	}

	M682Sync();
}

static void M682Power(void)
{
	M682InstallHandlers();
	M682ResetRegisters();
}

static void M682Reset(void)
{
	M682ResetRegisters();
}

static void M682Close(void)
{
	if (PRGRAM)
		FCEU_gfree(PRGRAM);
	if (CHRRAM)
		FCEU_gfree(CHRRAM);

	PRGRAM = NULL;
	CHRRAM = NULL;
	PRGRAMSIZE = 0;
	CHRRAMSIZE = 0;
}

static void M682StateRestore(int version)
{
	(void)version;
	M682Sync();
}

static void M682CommonInit(CartInfo* info, uint8 useLegacyV13)
{
	uint32 prgVolatileSize = info->PRGRamSize;
	uint32 prgSaveSize = info->PRGRamSaveSize;
	uint32 chrVolatileSize = info->CHRRamSize;
	uint32 chrSaveSize = info->CHRRamSaveSize;

	legacyV13 = useLegacyV13;
	PRGRAM = NULL;
	CHRRAM = NULL;

	/*
	 * Rainbow boards provide 32, 128 or 256 KiB RAM.  A 32 KiB fallback keeps
	 * old iNES headers usable when they omit the NES 2.0 RAM-size fields.
	 */
	PRGRAMSIZE = prgVolatileSize + prgSaveSize;
	if (!PRGRAMSIZE)
		PRGRAMSIZE = M682_DEFAULT_RAM_SIZE;

	CHRRAMSIZE = chrVolatileSize + chrSaveSize;
	if (!CHRRAMSIZE)
		CHRRAMSIZE = M682_DEFAULT_RAM_SIZE;

	PRGRAM = (uint8*)FCEU_gmalloc(PRGRAMSIZE);
	CHRRAM = (uint8*)FCEU_gmalloc(CHRRAMSIZE);
	memset(PRGRAM, 0, PRGRAMSIZE);
	memset(CHRRAM, 0, CHRRAMSIZE);

	SetupCartPRGMapping(M682_RAM_CHIP, PRGRAM, PRGRAMSIZE, 1);
	SetupCartCHRMapping(M682_RAM_CHIP, CHRRAM, CHRRAMSIZE, 1);

	/*
	 * NES 2.0 normally declares either volatile RAM or NVRAM for this board.
	 * If both are declared, the NVRAM portion is kept at the beginning of the
	 * single mapper-visible RAM chip and only that portion is battery-saved.
	 */
	if (prgSaveSize)
		info->addSaveGameBuf(PRGRAM, prgSaveSize);
	else if (info->battery)
		info->addSaveGameBuf(PRGRAM, PRGRAMSIZE);

	if (chrSaveSize)
		info->addSaveGameBuf(CHRRAM, chrSaveSize);

	info->Power = M682Power;
	info->Reset = M682Reset;
	info->Close = M682Close;
	GameStateRestore = M682StateRestore;

	AddExState(StateRegs, ~0, 0, 0);
	AddExState(PRGRAM, PRGRAMSIZE, 0, "PRAM");
	AddExState(CHRRAM, CHRRAMSIZE, 0, "CRAM");
}

void Mapper682_Init(CartInfo* info)
{
	M682CommonInit(info, 0);
}

void Mapper3872_Init(CartInfo* info)
{
	M682CommonInit(info, 1);
}