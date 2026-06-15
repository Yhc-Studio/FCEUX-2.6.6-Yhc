#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"


static void M291PW(uint32 A, uint8 V) {
	if (EXPREGS[0] & 0x20)
		setprg32(0x8000, ((EXPREGS[0] & 0x1F) >> 1) | ((EXPREGS[0] & 0x40) ? 0x04 : 0x00));
	else
		syncPRG(0x0F, (EXPREGS[0] & 0x40) ? 0x10 : 0x00);
}

static void M291CW(uint32 A, uint8 V) {
	syncCHR_ROM(0xFF, (EXPREGS[0] & 0x40) ? 0x100 : 0x000);
}

static DECLFW(M291Write) {
	EXPREGS[0] = V;
	FixMMC3CHR(MMC3_cmd);
	FixMMC3PRG(MMC3_cmd);
}

static void M291Reset(void) {
	EXPREGS[0] = 0;
	MMC3RegReset();
}

static void M291Power(void) {
	M291Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x6fff, M291Write);
}

void Mapper291_Init(CartInfo *info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M291PW;
	cwrap = M291CW;
	info->Power = M291Power;
	info->Reset = M291Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}