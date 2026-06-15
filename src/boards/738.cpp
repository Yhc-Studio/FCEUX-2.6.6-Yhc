#include "mapinc.h"
#include "mmc3.h"

static void M738PW(uint32 A, uint8 V) {
	if (EXPREGS[0] & 0x20)
		syncPRG(0x0F, EXPREGS[0] << 1 & 0x30);
	else
		if (EXPREGS[0] & 0x18)
			setprg32(0x8000, EXPREGS[0] >> 1);
		else {
			setprg16(0x8000, EXPREGS[0]);
			setprg16(0xc000, EXPREGS[0]);
		}
	
}

static void M738CW(uint32 A, uint8 V) {
	syncCHR_ROM(0x7F, EXPREGS[0] << 4 & 0x180);

}
static DECLFW(M738Write) {
	EXPREGS[0] = A & 0xFF;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
	//MMC3_CMDWrite(A, V);
}

static void M738_Reset(void) {
	EXPREGS[0] = 0;
	MMC3RegReset();
}
static void M738_Power(void) {
	M738_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M738Write);
}

void Mapper738_Init(CartInfo* info) {
	GenMMC3_Init(info, 128, 128, 8, info->battery);
	pwrap = M738PW;
	cwrap = M738CW;
	info->Power = M738_Power;
	info->Reset = M738_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}