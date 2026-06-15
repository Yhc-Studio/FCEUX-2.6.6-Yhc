#include "mapinc.h"
#include "../ines.h"
static uint8 latch;
static uint8 reg;
static SFORMAT StateRegs[] =
{
	{ &reg, 1, "REG" },
	{ &latch, 1, "LATCH" },
	{ 0 }
};
static void Sync(void) {
	setprg16(0x8000, latch & 7 | (reg & 0x0f) << 3);
	setprg16(0xC000, 7 | (reg & 0x0f) << 3);
	setchr8r(0x0000, 0);
	if ((reg & 0x20) >> 5 == 1)
		setmirror(MI_H);
	else
		setmirror(MI_V);
}
static DECLFW(Write) {
	reg = V & 0x2f;
	Sync();
}
static DECLFW(LatchWrite) {
	latch = V;
	Sync();
}
static void Golden_Spc001_Reset() {
	reg = 0;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0xbfe0, 0xbfff, Write);
	SetWriteHandler(0xc000, 0xffff, LatchWrite);
	Sync();
}
static void Golden_Spc001_Power() {
	reg = 0;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0xbfe0, 0xbfff, Write);
	SetWriteHandler(0xc000, 0xffff, LatchWrite);
	Sync();
}
static void StateRestore(int version) {
	Sync();
}
void Golden_Spc001_Init(CartInfo *info) {
	info->Reset = Golden_Spc001_Reset;
	info->Power = Golden_Spc001_Power;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}