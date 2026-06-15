#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"

int  prgOR = (EXPREGS[0] & 0x08 ? 0x020 : 0x000) | (EXPREGS[0] & 0x40 ? 0x010 : 0x000);
int  chrOR = (EXPREGS[0] & 0x04 ? 0x100 : 0x000) | (EXPREGS[0] & 0x02 ? 0x080 : 0x000);
int  prgAND = EXPREGS[0] & 0x20 ? 0x0F : 0x1F;
int  chrAND = EXPREGS[0] & 0x01 ? 0xFF : 0x7F;

static void M750PW(uint32 A, uint8 V) {
	syncPRG(prgAND, prgOR &~prgAND);
}

static void M750CW(uint32 A, uint8 V) {
	syncCHR_ROM(chrAND, chrOR &~chrAND);
}
static DECLFW(M750Write) {
	if (~EXPREGS[0] & 0x80) {
		EXPREGS[0] = V | 0x80;
		FixMMC3PRG(MMC3_cmd);
		FixMMC3CHR(MMC3_cmd);
	}

	MMC3_CMDWrite(A, V);
}

static void M750_Reset(void) {
	EXPREGS[0] = 0x00;
	MMC3RegReset();
}
static void M750_Power(void) {
	M750_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M750Write);
}

void Mapper750_Init(CartInfo *info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M750PW;
	cwrap = M750CW;
	info->Power = M750_Power;
	info->Reset = M750_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}