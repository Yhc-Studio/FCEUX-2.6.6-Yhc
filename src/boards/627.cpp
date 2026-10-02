/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 627
 *
 * Re-adapted from the supplied NRS sync() implementation.
 *
 * NRS reference behavior:
 *
 *   SetPRG_RAM8($6000, 0)
 *
 *   if (Latch::addr & $40)
 *       PRG32 $8000 = Latch::addr >> 3
 *   else {
 *       PRG16 $8000 = Latch::addr >> 2
 *       PRG16 $C000 = Latch::addr >> 2
 *   }
 *
 *   CHR8 = ((Latch::addr >> 1) & $1C) | (Latch::addr & $03)
 *
 *   A7 = 0: Vertical mirroring
 *   A7 = 1: Horizontal mirroring
 *
 * The written data byte is not used; the CPU write ADDRESS is the latch.
 */

#include "mapinc.h"
#include <string.h>

enum
{
	M627_WRAM_CHIP = 0x10,
	M627_WRAM_SIZE = 8 * 1024
};

static uint16 latchAddr;

static uint8* WRAM = NULL;
static uint32 WRAMSIZE;
static uint8 m627Battery;

static SFORMAT StateRegs[] =
{
	{ &latchAddr, 2 | FCEUSTATE_RLSB, "LADR" },
	{ 0 }
};

static uint32 M627CHRBank(void)
{
	/*
	 * NRS:
	 *   Latch::addr >> 1 & $1C | Latch::addr & $03
	 *
	 * Wiring:
	 *   CHR b0 <- CPU A0
	 *   CHR b1 <- CPU A1
	 *   CHR b2 <- CPU A3
	 *   CHR b3 <- CPU A4
	 *   CHR b4 <- CPU A5
	 */
	return
		((latchAddr >> 1) & 0x1C) |
		(latchAddr & 0x03);
}

static void M627Sync(void)
{
	/*
	 * NRS always maps one 8 KiB PRG-RAM bank at $6000-$7FFF.
	 */
	setprg8r(M627_WRAM_CHIP, 0x6000, 0);

	if (latchAddr & 0x0040)
	{
		/*
		 * A6 = 1:
		 * 32 KiB PRG mode.
		 *
		 * For a 256 KiB PRG image, the effective bank bits are A3-A5.
		 * Higher address bits are naturally masked by FCEUX to ROM size.
		 */
		setprg32(0x8000, latchAddr >> 3);
	}
	else
	{
		/*
		 * A6 = 0:
		 * 16 KiB mirrored PRG mode.
		 *
		 * IMPORTANT:
		 * Both CPU windows map THE SAME 16 KiB bank.
		 * There is no fixed upper bank.
		 *
		 * This also explains why a menu stored in the first 16 KiB boots
		 * correctly when latchAddr resets to zero:
		 *
		 *   $8000-$BFFF -> PRG16 bank 0
		 *   $C000-$FFFF -> PRG16 bank 0
		 */
		const uint32 bank = latchAddr >> 2;

		setprg16(0x8000, bank);
		setprg16(0xC000, bank);
	}

	/*
	 * NRS uses iNES_SetCHR_Auto8().
	 * FCEUX's normal chip-0 mapping already represents CHR-ROM when present
	 * and CHR-RAM for CHR-less images, so setchr8() gives the corresponding
	 * auto behavior.
	 */
	setchr8(M627CHRBank());

	/*
	 * A7 directly controls mirroring.
	 */
	setmirror((latchAddr & 0x0080) ? MI_H : MI_V);
}

static DECLFW(M627Write)
{
	(void)V;

	/*
	 * This mapper latches the CPU write ADDRESS, not the data byte.
	 *
	 * Do not restrict this to $8000-$803F:
	 *   A6 ($40) selects 32 KiB PRG mode.
	 *   A7 ($80) selects horizontal mirroring.
	 *
	 * The NRS Latch helper normally owns the complete cartridge write area,
	 * so mirror the register throughout $8000-$FFFF.
	 */
	latchAddr = (uint16)A;
	M627Sync();
}

static void M627InstallHandlers(void)
{
	SetReadHandler(0x6000, 0xFFFF, CartBR);
	SetWriteHandler(0x6000, 0x7FFF, CartBW);

	SetWriteHandler(0x8000, 0xFFFF, M627Write);
}

static void M627Power(void)
{
	/*
	 * NRS Latch reset state is zero on hard reset.
	 *
	 * latchAddr = 0 gives:
	 *   PRG16 bank 0 mirrored at $8000/$C000
	 *   CHR8 bank 0
	 *   Vertical mirroring
	 */
	latchAddr = 0;

	if (WRAM && !m627Battery)
		FCEU_MemoryRand(WRAM, WRAMSIZE);

	M627Sync();
	M627InstallHandlers();
}

static void M627Reset(void)
{
	/*
	 * Preserve the address latch on FCEUX soft reset.
	 * Hard reset/power clears it through M627Power().
	 */
	latchAddr = 0;
	M627Sync();
	M627InstallHandlers();
}

static void M627StateRestore(int version)
{
	(void)version;

	M627Sync();
	M627InstallHandlers();
}

static void M627Close(void)
{
	if (WRAM)
	{
		FCEU_gfree(WRAM);
		WRAM = NULL;
	}

	WRAMSIZE = 0;
}

void Mapper627_Init(CartInfo* info)
{
	m627Battery = info->battery ? 1 : 0;

	WRAMSIZE = M627_WRAM_SIZE;
	WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);

	if (WRAM)
	{
		memset(WRAM, 0, WRAMSIZE);

		SetupCartPRGMapping(
			M627_WRAM_CHIP,
			WRAM,
			WRAMSIZE,
			1
		);

		AddExState(
			WRAM,
			WRAMSIZE,
			0,
			"WRAM"
		);

		if (info->battery)
			info->addSaveGameBuf(WRAM, WRAMSIZE);
	}

	info->Power = M627Power;
	info->Reset = M627Reset;
	info->Close = M627Close;

	GameStateRestore = M627StateRestore;

	AddExState(StateRegs, ~0, 0, 0);
}