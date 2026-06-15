/* FCE Ultra - NES/Famicom Emulator
*
* Copyright notice for this file:
*  Copyright (C) 2012 CaH4e3
*  Copyright (C) 2002 Xodnizel
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
*/

#include "mapinc.h"

static uint32 latch_addr;

#define cpuA14  !!(latch_addr &0x001)
#define mirrorH !!(latch_addr &0x002)
#define protCHR !!(latch_addr &0x080)
#define unrom   !!(latch_addr &0x200)
#define prg       (latch_addr >>2 &0x1F) 

static void Mapper751_Sync(void) {
	setprg8(0x6000, 0);
	setprg16(0x8000, prg & ~cpuA14);
	setprg16(0xC000, (prg | cpuA14) | 7 * unrom);
	setchr8(0);
	if (mirrorH)
		setmirror(MI_H);
	else
		setmirror(MI_V);
}

static DECLFW(M751Write) {
	if (unrom) {
		latch_addr = latch_addr & ~0x1C | A & 0x1C;
	}
	else {
		latch_addr = A;
	}
	Mapper751_Sync();
}

static void Mapper751_Reset(void) {
	latch_addr = 0;
	Mapper751_Sync();
}

static void Mapper751_Power(void) {
	latch_addr = 0;
	Mapper751_Sync();
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xFFFF, M751Write);
}

static void StateRestore(int version) {
	Mapper751_Sync();
}

void Mapper751_Init(CartInfo* info) {
	info->Reset = Mapper751_Reset;
	info->Power = Mapper751_Power;
	GameStateRestore = StateRestore;
	AddExState(&latch_addr, ~0, 0, 0);
}