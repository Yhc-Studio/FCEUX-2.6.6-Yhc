#include "mapinc.h"
#include "../ines.h"
static uint8 latch = 0;
static uint8 reg[3];
static SFORMAT StateRegs[] =
{
	{ &reg, 3, "REG" },
	{ &latch, 1, "LATCH" },
	{ 0 }
};
static void Sync(void) {
	unsigned int isPRG, isMirror, CHRMode;
	//Sync PRG
	isPRG = (reg[2] & 0x04) >> 2;
	if (isPRG == 0) {
		setprg16(0x8000, reg[0]);
		setprg16(0xc000, reg[0]);
	}
	else {
		setprg16(0x8000, reg[0]);
		setprg16(0xc000, reg[0]+1);
	}
	//Sync CHR
	CHRMode = reg[2] & 0x03;
	if (CHRMode == 0) {
		setchr8(reg[1]);
	}
	else {
		setchr8(reg[1] + latch);
	}
	//Sync Mirror
	isMirror = (reg[2] & 0x08) >> 3;
	if (isMirror == 1)
		setmirror(MI_H);
	else
		setmirror(MI_V);
}
static DECLFW(Write) {
	//uint8 temp = 0;
	 if ((reg[2] & 0x80) >> 7 != 1) {
		switch (A & 3)
		{
			case 0: reg[0] = V & 0x3f; break;
			case 1: reg[1] = V & 0x7f; break;
			case 2: reg[2] = V & 0x8f; break;
		}
		Sync();
	}
}
static DECLFW(LatchWrite) {
	unsigned int CHRMode;
	CHRMode = reg[2] & 0x03;
	if (CHRMode == 1) {
		latch = V & 0x02;
		Sync();
	}
	else if (CHRMode == 2) {
		latch = V & 0x03;
		Sync();
	}
}
static void Nrom_Cnrom_Cart_Reset() {
	reg[0] = 0;
	reg[1] = 0;
	reg[2] = 0x0c;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x5000, 0x5fff, Write);
	SetWriteHandler(0x8000, 0xffff, LatchWrite);
	Sync();
}
static void Nrom_Cnrom_Cart_Power() {
	reg[0] = 0;
	reg[1] = 0;
	reg[2] = 0x0c;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x5000, 0x5fff, Write);
	SetWriteHandler(0x8000, 0xffff, LatchWrite);
	Sync();
}
static void StateRestore(int version) {
	Sync();
}
void Nrom_Cnrom_Cart_Init(CartInfo *info) {
	info->Reset = Nrom_Cnrom_Cart_Reset;
	info->Power = Nrom_Cnrom_Cart_Power;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}