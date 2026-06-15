#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"


static void M545PW(uint32 A, uint8 V) {
	uint8 prgOR = (EXPREGS[0] & 3) << 4;
	if (EXPREGS[0] & 8) {	// 256 KiB Split-PRG mode
		for (int bank = 0; bank < 4; bank++) {
			int val = getPRGBank(bank) & 0x1F;
			val = val & 0x0F | (val & 0x10 ? prgOR : 0x40);
			setprg8(0x8000 + bank * 2000, val);
		}
	}
	else
		syncPRG(0x0F, prgOR);
}

static void M545CW(uint32 A, uint8 V) {
	int chrOR = (EXPREGS[0] & 3) << 7;
	int chrA16 = (~EXPREGS[0] & 4) << 4;
	syncCHR_ROM(0x7F, chrOR | chrA16);
}
static DECLFW(M545Write) {
	if (A & 0x020 && A & 0x100) {
		EXPREGS[0] = V;
	}
	else {
		MMC3_CMDWrite(A, V);
		MMC3_IRQWrite(A, V);
	}
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
}

static void M545_Reset(void) {
	EXPREGS[0] = 0;
	MMC3RegReset();
}
static void M545_Power(void) {
	M545_Reset();
	GenMMC3Power();
	SetWriteHandler(0xF000, 0xFFFF, M545Write);
}

void Mapper545_Init(CartInfo* info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M545PW;
	cwrap = M545CW;
	info->Power = M545_Power;
	info->Reset = M545_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}