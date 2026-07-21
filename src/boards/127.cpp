/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 127 - Double Dragon pirate
 *
 * Ported from the Nintendulator/NRS Mapper 127 implementation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mapinc.h"

static uint8 PRG[4];
static uint8 CHR[8];
static uint8 IRQenabled;
static uint8 IRQcounter;
static uint8 Mirror[4];

static SFORMAT StateRegs[] =
{
	{ PRG,         4, "PRG0" },
	{ CHR,         8, "CHR0" },
	{ &IRQenabled, 1, "IRQA" },
	{ &IRQcounter, 1, "IRQC" },
	{ Mirror,      4, "MIRR" },
	{ 0 }
};

/* ------------------------------------------------------------------------- */
/* Banking and nametable mapping                                             */
/* ------------------------------------------------------------------------- */

static void M127Sync(void)
{
	int i;

	setprg8(0x8000, PRG[0]);
	setprg8(0xA000, PRG[1]);
	setprg8(0xC000, PRG[2]);
	setprg8(0xE000, PRG[3]);

	for (i = 0; i < 8; i++)
		setchr1(i << 10, CHR[i]);

	/*
	 * Each logical nametable independently selects CIRAM page 0 or 1.
	 *
	 * Nintendulator maps both $2000-$2FFF and its $3000-$3EFF mirror.
	 * FCEUX only needs the four base pages here; the PPU core handles the
	 * $3000 mirror automatically.
	 */
	for (i = 0; i < 4; i++) {
		setntamem(
			NTARAM + ((Mirror[i] & 1) << 10),
			1,
			i
		);
	}
}

/* ------------------------------------------------------------------------- */
/* Register writes                                                           */
/* ------------------------------------------------------------------------- */

static DECLFW(M127Write)
{
	switch (A & 0x73) {
	case 0x00:
		PRG[0] = V & 0x0F;
		break;

	case 0x01:
		PRG[1] = V & 0x0F;
		break;

	case 0x02:
		PRG[2] = V & 0x0F;
		break;

	case 0x03:
		PRG[3] = (V & 0x03) | 0x0C;
		break;

	case 0x10:
		CHR[0] = V & 0x7F;
		break;

	case 0x11:
		CHR[1] = V & 0x7F;
		break;

	case 0x12:
		CHR[2] = V & 0x7F;
		break;

	case 0x13:
		CHR[3] = V & 0x7F;
		break;

	case 0x20:
		CHR[4] = V & 0x7F;
		break;

	case 0x21:
		CHR[5] = V & 0x7F;
		break;

	case 0x22:
		CHR[6] = V & 0x7F;
		break;

	case 0x23:
		CHR[7] = V & 0x7F;
		break;

	case 0x30:
	case 0x31:
	case 0x32:
	case 0x33:
		IRQenabled = 1;
		break;

	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
		IRQenabled = 0;
		IRQcounter = 0;
		X6502_IRQEnd(FCEU_IQEXT);
		break;

	case 0x50:
		Mirror[0] = V & 1;
		break;

	case 0x51:
		Mirror[1] = V & 1;
		break;

	case 0x52:
		Mirror[2] = V & 1;
		break;

	case 0x53:
		Mirror[3] = V & 1;
		break;
	}

	/*
	 * The Nintendulator implementation calls Sync() after every register
	 * write, including IRQ control writes.
	 */
	M127Sync();
}

/* ------------------------------------------------------------------------- */
/* CPU-cycle IRQ                                                             */
/* ------------------------------------------------------------------------- */

static void M127IRQHook(int cycles)
{
	/*
	 * Nintendulator calls CPUCycle() once per CPU cycle:
	 *
	 *     if (IRQenabled && !--IRQcounter)
	 *         assert IRQ;
	 *
	 * FCEUX supplies the number of elapsed CPU cycles at once, so process
	 * each cycle to preserve uint8 wraparound and the exact 256-cycle period.
	 */
	while (cycles-- > 0) {
		if (IRQenabled && !--IRQcounter)
			X6502_IRQBegin(FCEU_IQEXT);
	}
}

/* ------------------------------------------------------------------------- */
/* Power, reset and state restoration                                        */
/* ------------------------------------------------------------------------- */

static void M127InstallHandlers(void)
{
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xFFFF, M127Write);
}

static void M127Power(void)
{
	int i;

	for (i = 0; i < 4; i++)
		PRG[i] = 0x0F;

	for (i = 0; i < 8; i++)
		CHR[i] = 0;

	IRQenabled = 0;
	IRQcounter = 0;
	X6502_IRQEnd(FCEU_IQEXT);

	for (i = 0; i < 4; i++)
		Mirror[i] = 0;

	M127Sync();
	M127InstallHandlers();
}

static void M127Reset(void)
{
	/*
	 * Nintendulator only clears the registers on RESET_HARD.
	 * A normal FCEUX Reset therefore preserves all mapper registers.
	 */
	M127Sync();
	M127InstallHandlers();
}

static void M127StateRestore(int version)
{
	(void)version;
	M127Sync();
	M127InstallHandlers();
}

/* ------------------------------------------------------------------------- */
/* Mapper initialization                                                     */
/* ------------------------------------------------------------------------- */

void Mapper127_Init(CartInfo* info)
{
	info->Power = M127Power;
	info->Reset = M127Reset;

	MapIRQHook = M127IRQHook;
	GameStateRestore = M127StateRestore;

	AddExState(&StateRegs, ~0, 0, 0);
}