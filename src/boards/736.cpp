#include "mapinc.h"
#include "mmc3.h"

static void M736PW(uint32 A, uint8 V) {
	int prgAND = ~EXPREGS[0] & 3 ? 0x0F : 0x1F;
	int prgOR = EXPREGS[0] << 5 & 0x20 | EXPREGS[0] << 3 & 0x10;
	syncPRG(prgAND, prgOR & ~prgAND);
}

static void M736CW(uint32 A, uint8 V) {
	int chrAND = EXPREGS[0] & 3 ? 0x7F : 0xFF; // 256 KiB if reg ==0, otherwise 128 KiB
	int chrOR = EXPREGS[0] << 7 & 0x100 | ~EXPREGS[0] << 7 & 0x80;
	syncCHR_ROM(chrAND, chrOR & ~chrAND);

}
static DECLFW(M736Write) {
	EXPREGS[0] = A & 3;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
	//MMC3_CMDWrite(A, V);
}

static void M736_Reset(void) {
	EXPREGS[0] = 0;
	MMC3RegReset();
}
static void M736_Power(void) {
	M736_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M736Write);
}

void Mapper736_Init(CartInfo* info) {
	GenMMC3_Init(info, 128, 128, 8, info->battery);
	pwrap = M736PW;
	cwrap = M736CW;
	info->Power = M736_Power;
	info->Reset = M736_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}