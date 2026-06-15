#include "mapinc.h"

static uint8 prg_reg;
static uint8 chr_reg;
static uint8 hrd_flag;
static uint8 CHR;
static uint8 Mode;

static SFORMAT StateRegs[] =
{
	{ &hrd_flag, 1, "DPSW" },
	{ &prg_reg, 1, "PRG" },
	{ &chr_reg, 1, "CHR" },
	{ &Mode, 1, "MODE" },
	{ &CHR, 1, "chr" },
	{ 0 }
};

static void Sync(void) {
	prg_reg = ((Mode & 0x18) >> 2) | (Mode & 0x20 ? 1 : 0);
	chr_reg = CHR;
	if (Mode & 0x80)
		setmirror(MI_V);
	else
		setmirror(MI_H);
	if (Mode & 0x40)
		setprg32(0x8000, prg_reg >> 1);
	else {
		setprg16(0x8000, prg_reg);
		setprg16(0xC000, prg_reg);
	}
	setchr8(chr_reg >> 4);

}

static DECLFR(M319Read) {
	return hrd_flag << 2;
}

static DECLFW(M319Write) {
	switch (A & 4) {
	case 0:	CHR = V; break;
	case 4:	Mode = V; break;
	}
	Sync();
}

static void M319Power(void) {
	prg_reg = 0;
	chr_reg = 0;
	hrd_flag = 0;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x6000, 0x6FFF, M319Write);
	SetReadHandler(0x6000, 0x6000, M319Read);
	Sync();
}

static void M319Reset() {
	hrd_flag++;
	hrd_flag = hrd_flag % 2;
	Mode = CHR = 0;
	SetReadHandler(0x5000, 0x5fff, M319Read);

	FCEU_printf("Select DIP Switch = %02x\n", hrd_flag);

	Sync();
}

static void StateRestore(int version) {
	Sync();
}

void Mapper319_Init(CartInfo *info) {
	info->Power = M319Power;
	info->Reset = M319Reset;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}

