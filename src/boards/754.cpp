#include "mapinc.h"
#include "mmc3.h"

static void M754PW(uint32 A, uint8 V) {
	A001B = 0x80;
	int prg = EXPREGS[0] & 0x1F | EXPREGS[0] >> 1 & 0x20;
	if (EXPREGS[0] & 0x80)
		syncPRG(0x0F, prg << 2);
	else
		if (EXPREGS[0] & 0x20)
			setprg32(0x8000, prg >> 1);
		else {
			setprg16(0x8000, prg);
			setprg16(0xC000, prg);
		}
}

static void M754CW(uint32 A, uint8 V) {
	setchr8(0);
}
static DECLFW(M754Write) {
	EXPREGS[A & 1] = V;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
	//MMC3_CMDWrite(A, V);
}

static void M754_Reset(void) {
	EXPREGS[0] = 0x60;
	EXPREGS[1] = 0x00;
	MMC3RegReset();
}
static void M754_Power(void) {
	M754_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M754Write);
}

void Mapper754_Init(CartInfo* info) {
	GenMMC3_Init(info, 128, 128, 8, info->battery);
	pwrap = M754PW;
	cwrap = M754CW;
	info->Power = M754_Power;
	info->Reset = M754_Reset;
	AddExState(EXPREGS, 2, 0, "EXPR");
}