#include "mapinc.h"
#include "mmc3.h"

#define prgAND ~EXPREGS[3] &0x3F
#define chrAND  0xFF >>(~EXPREGS[2] &0xF)
#define prgOR   EXPREGS[1] | EXPREGS[2] <<2 &0x300
#define chrOR   EXPREGS[0] | EXPREGS[2] <<4 &0xF00
#define gnrom   EXPREGS[2] &0x20
#define locked (EXPREGS[3] &0x40)

static uint8 index;

static SFORMAT StateRegs[] =
{
	{ &index, 1, "INDEX" },
	{ 0 }
};

static void M373CW(uint32 A, uint8 V) {
	syncCHR_ROM(chrAND, chrOR);
}

static void M373PW(uint32 A, uint8 V) {
	if (gnrom)
		syncPRG_GNROM_67(0x02, prgAND, prgOR);
	else
		syncPRG(prgAND, prgOR);
}

static DECLFW(M373Write6000) {
	if (!locked) {
		EXPREGS[index++ & 3] = V;
		FixMMC3PRG(MMC3_cmd);
		FixMMC3CHR(MMC3_cmd);
	}
}

static void M373Reset(void) {
	index = 0;
	EXPREGS[0] = 0x00;
	EXPREGS[1] = 0x00;
	EXPREGS[2] = 0x0F;
	EXPREGS[3] = 0x00;
	MMC3RegReset();
}

static void M373Power(void) {
	M373Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M373Write6000);
}

void Mapper373_Init(CartInfo* info) {
	GenMMC3_Init(info, 512, 256, 8, info->battery);
	pwrap = M373PW;
	cwrap = M373CW;
	info->Power = M373Power;
	info->Reset = M373Reset;
	AddExState(EXPREGS, 4, 0, "EXPR");
	AddExState(&StateRegs, ~0, 0, 0);
}