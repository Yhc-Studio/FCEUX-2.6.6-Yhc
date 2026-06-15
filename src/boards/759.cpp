/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2023
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "mapinc.h"
#include "latch.h"

static void Sync(void) {
	setprg16(0x8000, latch.data);
	setprg16(0xc000, latch.data | 7 | latch.data >> 1 & 8);
	setchr8(0);
}

static DECLFW(M759WriteReg) {
	if ((A/1000) == 0xF && (A & 0xF00) == 0xE00)
		latch.data |= 0x80;
	else
		if ((A / 1000) == 0xF && (A & 0xF00) == 0xF00)
			latch.data = latch.data & 0x0F | A << 3 & 0x18;
		else
			latch.data = latch.data & 0x18 | V & 0x0F;
	Sync();
}

static void M759Power() {
	LatchPower();
	SetReadHandler(0x8000, 0xfFFF, CartBR);
	SetWriteHandler(0x8000, 0xfFFF, M759WriteReg);
}
static void M759Reset() {
	LatchHardReset();
}
static void StateRestore(int version) {
	Sync();
}
void Mapper759_Init(CartInfo* info) {
	Latch_Init(info, Sync, NULL, 0, 0);
	info->Power = M759Power;
	info->Reset = M759Reset;
	GameStateRestore = StateRestore;
	AddExState(&latch, 2, 0, "LATC");
}