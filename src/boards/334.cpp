#include "mapinc.h"
#include "mmc3.h"
static uint8 hrd_flag;
static SFORMAT StateRegs[] =
{
	{ &hrd_flag, 1, "DPSW" },
	{ 0 }
};

static void M334PW(uint32 A, uint8 V) {
	setprg32(0x8000, EXPREGS[0] >> 1);
}
static void M334CW(uint32 A, uint8 V) {
	syncCHR_ROM(0xFF, 0);
}
static DECLFR(ReadDIP) {
	if (A & 2) {
		return hrd_flag;
	}
}
static DECLFW(WritePRG) {
	if (~A & 1) {
		EXPREGS[0] = V;
	}
	FixMMC3CHR(MMC3_cmd);
	FixMMC3PRG(MMC3_cmd);
}
static void M334Reset(void) {
	hrd_flag++;
	hrd_flag & 2;
	MMC3RegReset();
	SetWriteHandler(0x6000, 0x7fff, WritePRG);
}

static void M334Power(void) {
	hrd_flag = 0;
	//M334Reset();
	GenMMC3Power();
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x6000, 0x7fff, WritePRG);
	SetReadHandler(0x6002, 0x6002, ReadDIP);
}

void Mapper334_Init(CartInfo *info) {
	GenMMC3_Init(info, 128, 128, 8, info->battery);
	pwrap = M334PW;
	cwrap = M334CW;
	info->Power = M334Power;
	info->Reset = M334Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
	AddExState(&StateRegs, ~0, 0, 0);
}