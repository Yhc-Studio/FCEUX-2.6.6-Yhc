#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"

static void M757PW(uint32 A, uint8 V) {
	if (EXPREGS[0] == 0x08)
		syncPRG_GNROM_67(2, 0x0F, EXPREGS[0] << 4);
	else
		syncPRG(0x0F, EXPREGS[0] << 4);
}

static void M757CW(uint32 A, uint8 V) {
	if (EXPREGS[0] == 0x0F) {
		setchr2(0x0000, getCHRBank(0) | 0x200);
		setchr2(0x0200, getCHRBank(1) | 0x200);
		setchr2(0x0400, getCHRBank(4) | 0x200);
		setchr2(0x0600, getCHRBank(7) | 0x200);
	}
	else
		syncCHR_ROM(0x7F, EXPREGS[0] << 7 & 0x380);
}
static DECLFW(M757Write) {
	EXPREGS[0] = A;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);

	MMC3_CMDWrite(A, V);
}

static void M757_Reset(void) {
	EXPREGS[0] = 0x00;
	MMC3RegReset();
}
static void M757_Power(void) {
	M757_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M757Write);
}

void Mapper757_Init(CartInfo *info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M757PW;
	cwrap = M757CW;
	info->Power = M757_Power;
	info->Reset = M757_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}