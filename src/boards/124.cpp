/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 124 - Super Game Mega Type 3
 *
 * Ported from the Nintendulator/NRS implementation.
 *
 * The board dynamically selects one of four mapper cores:
 *   0: UNROM
 *   1: AMROM
 *   2: MMC1A
 *   3: AX5202P-compatible MMC3
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mapinc.h"
#include "asic_latch.h"
#include "asic_mmc1.h"
#include "asic_mmc3.h"
#include "cartram.h"

#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

enum
{
	M124_UNROM = 0,
	M124_AMROM = 1,
	M124_MMC1 = 2,
	M124_MMC3 = 3
};

/*
 * NRS obtains this value from ROM->dipValue.  FCEUX has no generic
 * per-cartridge DIP-switch UI for this board, so keep an editable default.
 */
enum
{
	M124_DIP_DEFAULT = 0x00
};

static uint8 audioEnable;
static uint8 internalRAM[4096];
static uint8 mode[2];
static uint8 dipValue;

static SFORMAT StateRegs[] =
{
	{ &audioEnable, 1, "AUEN" },
	{ mode,         2, "MODE" },
	{ &dipValue,    1, "DIPV" },
	{ internalRAM,  sizeof(internalRAM), "IRAM" },
	{ 0 }
};

static void M124Sync(void);
static void M124ApplyMode(int clear);

/* ------------------------------------------------------------------------- */
/* Mode helpers                                                              */
/* ------------------------------------------------------------------------- */

static uint8 M124Mapper(void)
{
	return (mode[0] >> 4) & 0x03;
}

static int M124PRGOR(void)
{
	return ((int)mode[1] << 4) & 0x1F0;
}

static int M124CHROR(void)
{
	return ((int)mode[0] << 7) & 0x780;
}

static int M124ROM6Enabled(void)
{
	return (mode[1] & 0x20) != 0;
}

static int M124ROM8Enabled(void)
{
	return (mode[1] & 0x80) == 0;
}

static int M124CHRRAMEnabled(void)
{
	return (mode[1] & 0x40) == 0;
}

/* ------------------------------------------------------------------------- */
/* Banking                                                                   */
/* ------------------------------------------------------------------------- */

static void M124MapCHRRAM(void)
{
	/*
	 * With CHR-ROM present, CartRAM_init() creates the additional CHR-RAM
	 * as chip $10.  With a CHR-less image, the iNES loader already provides
	 * ordinary chip-0 CHR-RAM.
	 */
	if (CHRRAMSize)
		setchr8r(0x10, 0);
	else
		setchr8(0);
}

static void M124Sync(void)
{
	const int prgOR = M124PRGOR();
	const int chrOR = M124CHROR();

	/*
	 * iNES_SetSRAM() in NRS provides the board WRAM before the selected
	 * mapper core is synchronized.  Re-establish the same underlying map
	 * before applying the optional fixed-ROM overlay at $6000.
	 */
	if (WRAMSize)
		setprg8r(0x10, 0x6000, 0);

	switch (M124Mapper()) {
	case M124_UNROM:
		setprg16(
			0x8000,
			(prgOR >> 1) | (Latch_data & 0x07)
		);
		setprg16(
			0xC000,
			(prgOR >> 1) | 0x07
		);
		setmirror(MI_V);
		break;

	case M124_AMROM:
		setprg32(
			0x8000,
			(prgOR >> 2) | (Latch_data & 0x07)
		);
		setmirror((Latch_data & 0x10) ? MI_1 : MI_0);
		break;

	case M124_MMC1:
		MMC1_syncWRAM(0);
		MMC1_syncPRG(0x07, prgOR >> 1);
		MMC1_syncCHR(0x1F, chrOR >> 2);
		MMC1_syncMirror();
		break;

	case M124_MMC3:
		MMC3_syncWRAM(0);
		MMC3_syncPRG(
			(mode[1] & 0x20) ? 0x0F : 0x1F,
			prgOR
		);
		MMC3_syncCHR(
			(mode[0] & 0x40) ? 0x7F : 0xFF,
			chrOR
		);
		MMC3_syncMirror();
		break;
	}

	/*
	 * Supervisor/menu overlays.  These are deliberately applied after the
	 * active mapper core, matching the NRS implementation.
	 */
	setprg4(0x5000, 0x385);

	if (M124ROM6Enabled())
		setprg8(0x6000, 0x1C3);

	if (M124ROM8Enabled())
		setprg32(0x8000, 0x71);

	if (M124CHRRAMEnabled())
		M124MapCHRRAM();
}

/* ------------------------------------------------------------------------- */
/* Latch bus conflict                                                        */
/* ------------------------------------------------------------------------- */

static void M124BusConflictAND(uint16* address, uint8* value, uint8 romValue)
{
	(void)address;
	*value &= romValue;
}

/* ------------------------------------------------------------------------- */
/* ASIC, internal RAM, coin and DIP handlers                                 */
/* ------------------------------------------------------------------------- */

static DECLFR(M124ReadInternalRAM)
{
	return internalRAM[A & 0x0FFF];
}

static DECLFW(M124WriteInternalRAM)
{
	internalRAM[A & 0x0FFF] = V;
}

static uint8 M124CoinBit(void)
{
#ifdef _WIN32
	/*
	 * NRS uses the keyboard C key as the coin input.  GetAsyncKeyState()
	 * checks the currently-held state without depending on the message loop.
	 */
	return (GetAsyncKeyState('C') & 0x8000) ? 0x80 : 0x00;
#else
	return 0x00;
#endif
}

static DECLFR(M124ReadCoinDIP)
{
	(void)A;
	return M124CoinBit() | dipValue;
}

static DECLFW(M124WriteASIC)
{
	/*
	 * NRS receives an address relative to CPU page $5.  Testing A4 and A0
	 * on FCEUX's full CPU address gives the same decoding.
	 */
	if (A & 0x0010) {
		audioEnable = V;
	}
	else {
		mode[A & 0x0001] = V;

		/*
		 * A mode write only changes which core owns $8000-$FFFF.  It must
		 * not reset the inactive MMC1/MMC3/latch state.
		 */
		M124ApplyMode(0);
		M124Sync();
	}
}

static void M124InstallGlobalHandlers(void)
{
	int i;

	/* Board-local 4 KiB RAM replaces the normal $0000-$0FFF CPU RAM view. */
	SetReadHandler(0x0000, 0x0FFF, M124ReadInternalRAM);
	SetWriteHandler(0x0000, 0x0FFF, M124WriteInternalRAM);

	/* Fixed ROM is readable at $5000, while writes go to the ASIC. */
	SetReadHandler(0x5000, 0x5FFF, CartBR);
	SetWriteHandler(0x5000, 0x5FFF, M124WriteASIC);

	/*
	 * NRS intercepts only addresses satisfying:
	 *
	 *     (addr & $F0F) == $F0F
	 *
	 * in CPU page $4.  Installing only those 16 addresses preserves every
	 * ordinary APU/controller/open-bus handler without needing a wrapper.
	 */
	for (i = 0; i < 16; i++) {
		const uint32 address = 0x4F0F | (i << 4);
		SetReadHandler(address, address, M124ReadCoinDIP);
	}
}

/* ------------------------------------------------------------------------- */
/* Dynamic mapper-core selection                                             */
/* ------------------------------------------------------------------------- */

static void M124ApplyMode(int clear)
{
	/*
	 * MMC1 and MMC3 use different FCEUX timing hooks.  Clear stale ownership
	 * before activating the selected core.
	 */
	MapIRQHook = NULL;
	GameHBIRQHook = NULL;
	PPU_hook = NULL;

	switch (M124Mapper()) {
	case M124_UNROM:
	case M124_AMROM:
		Latch_activate(
			clear,
			M124Sync,
			0x8000,
			0xFFFF,
			M124BusConflictAND
		);
		break;

	case M124_MMC1:
		MMC1_activate(
			clear,
			M124Sync,
			MMC1_TYPE_MMC1A,
			NULL,
			NULL,
			NULL,
			NULL
		);
		break;

	case M124_MMC3:
		MMC3_activate(
			clear,
			M124Sync,
			MMC3_TYPE_AX5202P,
			NULL,
			NULL,
			NULL,
			NULL
		);
		break;
	}

	M124InstallGlobalHandlers();
}

/*
 * NRS resets all three helper cores on a hard reset, even though only one
 * owns the CPU bus.  Configure and clear each FCEUX core once, then restore
 * ownership to the mode selected by mode[0].
 */
static void M124HardResetCores(void)
{
	Latch_activate(
		1,
		M124Sync,
		0x8000,
		0xFFFF,
		M124BusConflictAND
	);

	MMC1_activate(
		1,
		M124Sync,
		MMC1_TYPE_MMC1A,
		NULL,
		NULL,
		NULL,
		NULL
	);

	MMC3_activate(
		1,
		M124Sync,
		MMC3_TYPE_AX5202P,
		NULL,
		NULL,
		NULL,
		NULL
	);
}

/* ------------------------------------------------------------------------- */
/* Power, reset and state restore                                            */
/* ------------------------------------------------------------------------- */

static void M124Power(void)
{
	mode[0] = 0;
	mode[1] = 0;
	audioEnable = 0;
	dipValue = M124_DIP_DEFAULT;

	X6502_IRQEnd(FCEU_IQEXT);

	M124HardResetCores();
	M124ApplyMode(0);
	M124Sync();
	M124InstallGlobalHandlers();
}

static void M124Reset(void)
{
	/*
	 * NRS clears mode[] only on RESET_HARD.  FCEUX's Reset callback is a
	 * soft reset, so preserve mode and all mapper-core registers.
	 */
	X6502_IRQEnd(FCEU_IQEXT);

	M124ApplyMode(0);
	M124Sync();
	M124InstallGlobalHandlers();
}

static void M124StateRestore(int version)
{
	(void)version;

	M124ApplyMode(0);
	M124Sync();
	M124InstallGlobalHandlers();
}

/* ------------------------------------------------------------------------- */
/* Mapper initialization                                                     */
/* ------------------------------------------------------------------------- */

void Mapper124_Init(CartInfo* info)
{
	memset(internalRAM, 0, sizeof(internalRAM));
	mode[0] = 0;
	mode[1] = 0;
	audioEnable = 0;
	dipValue = M124_DIP_DEFAULT;

	/*
	 * NRS calls iNES_SetSRAM().  Use FCEUX's common cartridge-RAM helper:
	 * 8 KiB WRAM plus an 8 KiB switchable CHR-RAM bank.
	 */
	CartRAM_init(info, 8, 8);

	Latch_addExState();
	MMC1_addExState();
	MMC3_addExState();

	info->Power = M124Power;
	info->Reset = M124Reset;
	GameStateRestore = M124StateRestore;

	AddExState(&StateRegs, ~0, 0, 0);
}