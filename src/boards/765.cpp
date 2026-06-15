#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"

static void M765PW(uint32 A, uint8 V) {
	if (EXPREGS[0] == 0x08)
	{
		setprg8(0x8000, (getPRGBank(0) &~2 & ~1) & 0x0f | EXPREGS[0] << 4);
		setprg8(0xA000, (getPRGBank(0) &~2 | 1) & 0x0f | EXPREGS[0] << 4);
		setprg8(0xC000, (getPRGBank(0) | 2 & ~1) & 0x0f | EXPREGS[0] << 4);
		setprg8(0xE000, (getPRGBank(0) | 2 | 1) & 0x0f | EXPREGS[0] << 4);
	}
	else
		syncPRG(0x0F, EXPREGS[0] << 4);
}

static void M765CW(uint32 A, uint8 V) {
	if (EXPREGS[0] & 0x04) {
		setchr2(0x0000, getCHRBank(0) | 0x300);
		setchr2(0x0200, getCHRBank(3) | 0x300);
		setchr2(0x0400, getCHRBank(4) | 0x300);
		setchr2(0x0600, getCHRBank(7) | 0x300);
	}
	else
		syncCHR_ROM(EXPREGS[0] & 0x02 ? 0xFF : 0x7F, EXPREGS[0] << 7);
}
static DECLFW(M765Write) {
	EXPREGS[0] = A;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);

	MMC3_CMDWrite(A, V);
}

static void M765_Reset(void) {
	EXPREGS[0] = 0x00;
	MMC3RegReset();
}
static void M765_Power(void) {
	M765_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M765Write);
}

void Mapper765_Init(CartInfo *info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M765PW;
	cwrap = M765CW;
	info->Power = M765_Power;
	info->Reset = M765_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}