#include "mapinc.h"
#include "mmc3.h"
#include "../ines.h"

#define prgAND    (EXPREGS[0] &0x10? 0x1F: 0x0F)
#define chrAND    (EXPREGS[0] &0x10? 0xFF: 0x7F)
#define prgOR     (EXPREGS[0] <<1 &0x3E)
#define chrOR     (EXPREGS[0] <<4 &0x180)
#define prg       (EXPREGS[0] &0x1F)
#define nrom     !(EXPREGS[0] &0x20)
#define nrom256 !!(EXPREGS[0] &0x06)
#define lock    !!(EXPREGS[0] &0x20)

static void M747PW(uint32 A, uint8 V) {
	if (nrom) {
		setprg16(0x8000, prg &~nrom256);
		setprg16(0xC000, prg | nrom256);
	}
	else
		syncPRG(prgAND, prgOR &~prgAND);
}

static void M747CW(uint32 A, uint8 V) {
	syncCHR_ROM(chrAND, chrOR &~chrAND);
}
static DECLFW(M747Write) {
	if (!lock) {
		EXPREGS[0] = A & 0xFF;
		FixMMC3PRG(MMC3_cmd);
		FixMMC3CHR(MMC3_cmd);
	}

	MMC3_CMDWrite(A, V);
}

static void M747_Reset(void) {
	EXPREGS[0] = 0x00;
	MMC3RegReset();
}
static void M747_Power(void) {
	M747_Reset();
	GenMMC3Power();
	SetWriteHandler(0x6000, 0x7FFF, M747Write);
}

void Mapper747_Init(CartInfo *info) {
	GenMMC3_Init(info, 256, 256, 8, info->battery);
	pwrap = M747PW;
	cwrap = M747CW;
	info->Power = M747_Power;
	info->Reset = M747_Reset;
	AddExState(EXPREGS, 1, 0, "EXPR");
}