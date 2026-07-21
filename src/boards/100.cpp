/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 100 - Nesticle MMC3
 *
 * Ported from the Nintendulator/NRS Mapper 100 implementation.
 *
 * This board uses the AX5202P MMC3 IRQ and mirroring core, but replaces
 * the normal MMC3 PRG/CHR bank layout with Nesticle-compatible commands
 * and keeps one fixed 8 KiB RAM bank at $6000-$7FFF.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mapinc.h"
#include "asic_mmc3.h"
#include "cartram.h"

#include <string.h>

/* Globals provided by the FCEUX iNES loader. */
extern uint8 *trainerpoo;
extern uint8 *MiscROM;
extern uint32 MiscROM_size;

static uint8 pointer;
static uint8 prg[4];
static uint8 chr[8];

/*
 * Set while the reset vector must be redirected to the trainer/bootstrap at
 * $7000.  The vector-read wrapper clears it after returning the high byte.
 */
static uint8 bootstrapVectorActive;

static SFORMAT M100StateRegs[] =
{
	{ &pointer,               1, "PNTR" },
	{ prg,                    4, "PRGB" },
	{ chr,                    8, "CHRB" },
	{ &bootstrapVectorActive, 1, "BOOT" },
	{ 0 }
};

static void M100Sync(void)
{
	int bank;

	for (bank = 0; bank < 4; bank++)
		setprg8(0x8000 + (bank << 13), prg[bank]);

	for (bank = 0; bank < 8; bank++)
		setchr1(bank << 10, chr[bank]);

	/*
	 * NRS fixes one 8 KiB RAM bank at $6000-$7FFF.  Do not route this
	 * through MMC3_syncWRAM(), because Mapper 100 ignores MMC3 WRAM
	 * enable/write-protect banking for the actual memory map.
	 */
	setprg8r(0x10, 0x6000, 0);
	MMC3_syncMirror();
}

static DECLFW(M100WriteBank)
{
	if (!(A & 1))
	{
		pointer = V;
	}
	else
	{
		switch (pointer)
		{
		case 0x00:
			chr[0] = V & 0xFE;
			chr[1] = V | 0x01;
			break;

		case 0x01:
			chr[2] = V & 0xFE;
			chr[3] = V | 0x01;
			break;

		case 0x02:
			chr[4] = V;
			break;

		case 0x03:
			chr[5] = V;
			break;

		case 0x04:
			chr[6] = V;
			break;

		case 0x05:
			chr[7] = V;
			break;

		case 0x06:
			prg[0] = V;
			break;

		case 0x07:
			prg[1] = V;
			break;

		case 0x46:
			prg[2] = V;
			break;

		case 0x47:
			/*
			 * This is intentionally PRG[1], matching the NRS source.
			 * It is not a transcription error.
			 */
			prg[1] = V;
			break;

		case 0x80:
			chr[4] = V & 0xFE;
			chr[5] = V | 0x01;
			break;

		case 0x81:
			chr[6] = V & 0xFE;
			chr[7] = V | 0x01;
			break;

		case 0x82:
			chr[0] = V;
			break;

		case 0x83:
			chr[1] = V;
			break;

		case 0x84:
			chr[2] = V;
			break;

		case 0x85:
			chr[3] = V;
			break;
		}
	}

	M100Sync();
}

static DECLFR(M100ReadResetVector)
{
	if (bootstrapVectorActive)
	{
		if (A == 0xFFFC)
			return 0x00;

		if (A == 0xFFFD)
		{
			bootstrapVectorActive = 0;
			return 0x70;
		}
	}

	return CartBR(A);
}

static void M100InstallHandlers(void)
{
	/*
	 * MMC3_activate() owns the normal $8000-$FFFF register range.
	 * Mapper 100 replaces only $8000-$9FFF; $A000-$FFFF remains connected
	 * to the MMC3 core for mirroring, WRAM control and IRQ registers.
	 */
	SetReadHandler(0x6000, 0xFFFF, CartBR);
	SetWriteHandler(0x6000, 0x7FFF, CartBW);
	SetWriteHandler(0x8000, 0x9FFF, M100WriteBank);

	/* One-shot reset-vector redirection for the optional trainer/bootstrap. */
	SetReadHandler(0xFFFC, 0xFFFD, M100ReadResetVector);
}

static void M100ActivateMMC3(int clear)
{
	MMC3_activate(
		clear,
		M100Sync,
		MMC3_TYPE_AX5202P,
		NULL,
		NULL,
		NULL,
		NULL
	);

	M100InstallHandlers();
}

static void M100ResetBanks(void)
{
	int bank;

	for (bank = 0; bank < 4; bank++)
		prg[bank] = (bank & 2) ? (uint8)(0xFC | bank) : (uint8)bank;

	for (bank = 0; bank < 8; bank++)
		chr[bank] = (uint8)bank;
}

static void M100CopyBootstrap(void)
{
	const uint8 *source = NULL;
	uint32 size = 0;
	uint32 i;

	bootstrapVectorActive = 0;

	/*
	 * Older iNES images expose a conventional 512-byte trainer through
	 * trainerpoo.  NES 2.0 images may expose the same bootstrap as Misc ROM.
	 */
	if (trainerpoo)
	{
		source = trainerpoo;
		size = 512;
	}
	else if (MiscROM && MiscROM_size)
	{
		source = MiscROM;
		size = MiscROM_size;
	}

	if (!source || !size)
		return;

	/*
	 * The bootstrap destination is one 4 KiB CPU page.  Normal images use
	 * 512 bytes; cap larger malformed Misc ROM payloads at $7000-$7FFF.
	 */
	if (size > 0x1000)
		size = 0x1000;

	for (i = 0; i < size; i++)
		X6502_DMW(0x7000 + i, source[i]);

	if (source[0] == 0x4C)
		bootstrapVectorActive = 1;
}

static void M100Power(void)
{
	pointer = 0;
	bootstrapVectorActive = 0;

	M100ResetBanks();
	M100ActivateMMC3(1);
	M100Sync();
	M100CopyBootstrap();
}

static void M100Reset(void)
{
	/*
	 * NRS reinitializes the external PRG/CHR arrays on every reset, while
	 * allowing the MMC3 helper core to perform a soft reset.
	 */
	M100ResetBanks();
	M100ActivateMMC3(0);
	M100Sync();
	M100CopyBootstrap();
}

static void M100StateRestore(int version)
{
	(void)version;

	M100ActivateMMC3(0);
	M100Sync();
}

void Mapper100_Init(CartInfo *info)
{
	memset(prg, 0, sizeof(prg));
	memset(chr, 0, sizeof(chr));
	pointer = 0;
	bootstrapVectorActive = 0;

	MMC3_addExState();

	/*
	 * Equivalent to NRS iNES_SetSRAM(): use the NES 2.0 declared WRAM size,
	 * or an 8 KiB fallback for older headers.
	 */
	WRAM_init(info, 8);

	info->Power = M100Power;
	info->Reset = M100Reset;
	info->Close = CartRAM_close;

	GameStateRestore = M100StateRestore;

	AddExState(M100StateRegs, ~0, 0, 0);
}
