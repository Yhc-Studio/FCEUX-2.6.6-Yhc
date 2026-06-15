#include "mapinc.h"

static uint8 reg[2];

static void Sync(void) {
	if (reg[0] & 0x20)
		setprg32(0x8000, reg[0] >> 1);
	else {
		setprg16(0x8000, reg[0] & 0x1F);
		setprg16(0xC000, reg[0] & 0x1F);
	}
	setchr8(reg[1] & 0x1F);
	if (reg[1] & 0x20)
		setmirror(MI_V);
	else
		setmirror(MI_H);
}

static DECLFW(M749Write) {
	if (A & 0x100) {
		uint8 bank = A >> 12;
		reg[(bank >> 1) & 1] = V;
	}
	Sync();
}

static void M749Power(void) {
	for (auto& c : reg) c = 0;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x6000, 0x6FFF, M749Write);
	Sync();
}

static void M749Reset() {
	for (auto& c : reg) c = 0;
	Sync();
}

static void StateRestore(int version) {
	Sync();
}

void Mapper749_Init(CartInfo* info) {
	info->Power = M749Power;
	info->Reset = M749Reset;
	GameStateRestore = StateRestore;
	AddExState(reg, ~0, 0, 0);
}

