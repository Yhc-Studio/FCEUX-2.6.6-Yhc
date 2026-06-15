#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"


static void M745PW(uint32 A, uint8 V) {
	if (EXPREGS[0] & 0x80)
		syncPRG(0x1F, EXPREGS[0] >> 1 & 0x20);
	else
		if (EXPREGS[0] & 0x20)
			setprg32(0x8000, (EXPREGS[0] & 0x0F | EXPREGS[0] >> 2 & 0x10) >> 1);
		else {
			setprg16(0x8000, EXPREGS[0] & 0x0F | EXPREGS[0] >> 2 & 0x10);
			setprg16(0xC000, EXPREGS[0] & 0x0F | EXPREGS[0] >> 2 & 0x10);
		}
}

static void M745CW(uint32 A, uint8 V) {
	setchr8(0);
}
static DECLFW(M745Write) {
	EXPREGS[0] = V;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);

	//MMC3_CMDWrite(A, V);
}

static void M745_Reset(void) {
	EXPREGS[0] = 0x0f;
	MMC3RegReset();
}
static void M745_Power(void) {
	M745_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M745Write);
}

void Mapper745_Init(CartInfo *info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M745PW;
	cwrap = M745CW;
	info->Power = M745_Power;
	info->Reset = M745_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}