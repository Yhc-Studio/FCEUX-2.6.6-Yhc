#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"


static void M735PW(uint32 A, uint8 V) {
	if (EXPREGS[0] & 0x01)
		syncPRG(0x1F, EXPREGS[0] & 0x60);
	else
		setprg32(0x8000, EXPREGS[0] >> 2);
}

static void M735CW(uint32 A, uint8 V) {
	syncCHR_ROM(EXPREGS[0] & 0x100 ? 0x7F : 0xFF, EXPREGS[0] >> 1 & 0x100 | EXPREGS[0] & 0x080);
}
static DECLFW(M735Write) {
	EXPREGS[0] = A ^ 0x1C;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
}

static void M735_Reset(void) {
	EXPREGS[0] = 0xFE;
	MMC3RegReset();
}
static void M735_Power(void) {
	M735_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M735Write);
}

void Mapper735_Init(CartInfo* info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M735PW;
	cwrap = M735CW;
	info->Power = M735_Power;
	info->Reset = M735_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}