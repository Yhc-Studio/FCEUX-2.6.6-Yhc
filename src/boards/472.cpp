#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"
static uint8 flag;
static void M472PW(uint32 A, uint8 V) {
	setprg8(A, V & 0x0F | EXPREGS[0] & 0xF0);
}

static void M472CW(uint32 A, uint8 V) {
	int chrAND = EXPREGS[0] & 0x20 ? 0x7F : 0xFF;
	setchr1(A, V & chrAND | EXPREGS[0] << 3 & ~chrAND);
}
static DECLFW(M472Write) {
	EXPREGS[0] = EXPREGS[0] & 0x80 | V & 0x7F;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
}
static DECLFR(M472Read) {
	return EXPREGS[0];
}

static void M472Reset(void) {
	EXPREGS[0] = (EXPREGS[0] ^ 0x80) & 0x80;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
}
static void M472Power(void) {
	EXPREGS[0] = 0;
	GenMMC3Power();
	SetReadHandler(0x6000, 0x7FFF, M472Read);
	SetWriteHandler(0x6000, 0x7FFF, M472Write);
}

void Mapper472_Init(CartInfo *info) {
	GenMMC3_Init(info, 128, 256, 8, 0);
	//GenMMC3_Init(info, 256, 256, 8, 0);
	pwrap = M472PW;
	cwrap = M472CW;
	info->Reset = M472Reset;
	info->Power = M472Power;
	AddExState(EXPREGS, 1, 0, "EXPR");
}