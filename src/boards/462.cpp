#include "mapinc.h"
#include "../ines.h"
static uint8 exReg[2];
static SFORMAT StateRegs[] =
{
	{ &exReg, 2, "REGS" },
	{ 0 }
};
static void Sync(void) {
	if (exReg[0] & 0x40)
	{
		unsigned int AND = 0x07;
		unsigned int OR = ((exReg[0] >> 6) & 3) << 3;
		setprg32(0x8000, OR | (exReg[1] & AND));
		if ((exReg[1] & 0x10))
			setmirror(MI_1);
		else
			setmirror(MI_0);
		setchr8r(0x0000, 0);
	}
	else
	{
		unsigned int AND = 0x07;
		unsigned int OR = ((exReg[0] >> 5) & 3) << 3;
		setprg16(0x8000, OR | (exReg[1] & AND));
		setprg16(0xC000, OR | 7);
		if (exReg[0] & 0x10)
			setmirror(MI_V);
		else
			setmirror(MI_H);
		setchr8r(0x0000, 0);
	}
}
static DECLFW(WriteReg1) {
	exReg[1] = V;
	Sync();
}
static DECLFW(WriteReg0) {
	exReg[0] = V;
	Sync();
}
static void BMC_971107_00G_Reset() {
	exReg[1] = 0;
	exReg[0] = 0;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xffff, WriteReg1);
	SetWriteHandler(0xbd00, 0xbdff, WriteReg0);
	Sync();
}
static void BMC_971107_00G_Power() {
	exReg[1] = 0;
	exReg[0] = 0;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xffff, WriteReg1);
	SetWriteHandler(0xbd00, 0xbdff, WriteReg0);
	Sync();
}
static void StateRestore(int version) {
	Sync();
}
void BMC_971107_00G_Init(CartInfo *info) {
	info->Reset = BMC_971107_00G_Reset;
	info->Power = BMC_971107_00G_Power;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}