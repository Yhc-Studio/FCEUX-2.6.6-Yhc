#include "mapinc.h"

static uint8 preg, creg, latch, dipSwitch;

static SFORMAT StateRegs[] =
{
	{ &preg, 1, "PREG" },
	{ &creg, 1, "CREG" },
	{ &latch, 1, "LATC" },
	{ &dipSwitch, 1, "DPSW" },
	{ 0 }
};

static void Sync(void) {
	int prg = (preg & 7) | ((preg >> 3) & 0x08);		/* There is a high bit 3 of the PRG register that applies both to PRG and CHR */
	int chr = (creg & 7) | ((preg >> 3) & 0x08);    	/* There is a high bit 3 of the PRG register that applies both to PRG and CHR */
	int mask = (creg & 0x10) ? 0 : (creg & 0x20) ? 1 : 3;	/* There is an CNROM mode that takes either two or four inner CHR banks from a CNROM-like latch register at $8000-$FFFF. */

	if (preg & 8) {
		setprg16(0x8000, prg);
		setprg16(0xc000, prg);
	}
	else
		setprg32(0x8000, prg >> 1);

	setchr8((chr &~mask) | (latch &mask));          	/* This "inner CHR bank" substitutes the respective bit(s) of the creg register. */

	setmirror(((preg >> 4) & 1) ^ 1);
}

static DECLFR(BMCWSRead) {
	if ((creg >> 6) & (dipSwitch & 3))
		return X.DB;
	return CartBR(A);
}

static DECLFW(BMCWSWrite) {
	if (preg & 0x20)
		return;

	switch (A & 1) {
	case 0: preg = V; Sync(); break;
	case 1: creg = V; Sync(); break;
	}
}

static DECLFW(LatchWrite) {
	latch = V;
	Sync();
}

static void MBMCWSPower(void) {
	dipSwitch = 0;
	Sync();
	SetReadHandler(0x8000, 0xFFFF, BMCWSRead);
	SetWriteHandler(0x6000, 0x7FFF, BMCWSWrite);
	SetWriteHandler(0x8000, 0xFFFF, LatchWrite);
}

static void BMCWSReset(void) {
	dipSwitch++;		/* Soft-resetting cycles through solder pad or DIP switch settings */
	if (dipSwitch == 3)
		dipSwitch = 0; 	/* Only 00b, 01b and 10b settings are valid */

						/* Always reset to menu */
	preg = 0;
	creg = 0;
	latch = 0;
	Sync();
}

static void StateRestore(int version) {
	Sync();
}

void BMCWS_Init(CartInfo *info) {
	info->Reset = BMCWSReset;
	info->Power = MBMCWSPower;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}