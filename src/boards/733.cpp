#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"


static void M733PW(uint32 A, uint8 V) {
	int prgAND = EXPREGS[0] & 2 ? 0x0F : 0x1F;
	int prgOR = EXPREGS[0] << 4;
	if ((EXPREGS[0] & 3) == 3 && ~EXPREGS[0] & 8)
		syncPRG_GNROM_67(0x00, prgAND, prgOR & ~prgAND);
	else
		syncPRG(prgAND, prgOR & ~prgAND);
}

static void M733CW(uint32 A, uint8 V) {
	int chrAND = EXPREGS[0] & 2 ? 0x7F : 0xFF;
	int chrOR = EXPREGS[0] << 7;
	syncCHR_ROM(chrAND, chrOR & ~chrAND);
}
static DECLFW(M733Write) {
	EXPREGS[0] = A & 0xFF;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
}

static void M733_Reset(void) {
	EXPREGS[0] = 0;
	MMC3RegReset();
}
static void M733_Power(void) {
	M733_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M733Write);
}

void Mapper733_Init(CartInfo* info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M733PW;
	cwrap = M733CW;
	info->Power = M733_Power;
	info->Reset = M733_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}