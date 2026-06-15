#include "mapinc.h"

static uint8 prgHi;
static uint8 chrHi;
static uint8 innerBank;

static SFORMAT StateRegs[] =
{
	{ &innerBank, 1, "INNER" },
	{ &prgHi, 1, "PRG" },
	{ &chrHi, 1, "CHR" },
	{ 0 }
};

static void Sync(void) {
	if (chrHi & 2) {
		setprg16(0x8000, prgHi >> 2 | innerBank >> 2 & 3);
		setprg16(0xC000, prgHi >> 2 | 3);
	}
	else
		setprg32(0x8000, prgHi >> 3);
	setchr8(chrHi >> 1 & 0xFC | innerBank & 0x03);
	if (prgHi & 1)
		setmirror(MI_H);
	else
		setmirror(MI_V);

}


static DECLFW(M389WritePRG) {
	prgHi = A & 0xFF;
	Sync();
}

static DECLFW(M389WriteCHR) {
	chrHi = A & 0xFF;
	Sync();
}

static DECLFW(M389WriteInnerBank) {
	innerBank = A & 0xFF;
	Sync();
}

static void M389Reset() {
	prgHi = 0;
	chrHi = 0;
	innerBank = 0;
	Sync();
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0x8fff, M389WritePRG);
	SetWriteHandler(0x9000, 0x9fff, M389WriteCHR);
	SetWriteHandler(0xa000, 0xffff, M389WriteInnerBank);
}
static void M389Power(void) {
	M389Reset();
}



static void StateRestore(int version) {
	Sync();
}

void Mapper389_Init(CartInfo *info) {
	info->Power = M389Power;
	info->Reset = M389Reset;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}