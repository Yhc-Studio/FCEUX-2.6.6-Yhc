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

static uint16 latch;
static uint32 latch_addr;
static SFORMAT StateRegs[] =
{
	{ &latch_addr, 4, "ADDR" },
	{ &latch, 1, "DATA" },
	{ 0 }
};

static void Mapper746_Sync(void) {
	
	if (latch_addr & 0x20) {
		if (~latch_addr & 0x01)
			setprg32(0x8000, latch_addr >> 2 & 0x10 | latch_addr >> 1 & 0x0F);
		else {
			setprg16(0x8000, latch_addr >> 1 & 0x20 | latch_addr & 0x1F);
			setprg16(0xC000, latch_addr >> 1 & 0x20 | latch_addr & 0x1F);
		}
	}
	else {
		setprg16(0x8000, latch_addr >> 1 & 0x20 | latch_addr & 0x1F | latch);
		setprg16(0xC000, latch_addr >> 1 & 0x20 | latch_addr & 0x1F | 7);
	}
	setchr8(0);
	if (latch_addr & 0x80)
		setmirror(MI_H);
	else
		setmirror(MI_V);
}

static DECLFW(M746Write) {
	if (latch_addr & 0x20 && ~A & 0x20) {
		latch_addr = A;
		latch = V;
	}
	else {
		latch_addr = latch_addr & 0xC0 | A & 0x3F;
		latch = V;
	}
	Mapper746_Sync();
}

static void Mapper746_Reset(void) {
	latch = 0;
	latch_addr = 0;
	Mapper746_Sync();
}

static void Mapper746_Power(void) {
	latch = 0;
	latch_addr = 0;
	Mapper746_Sync();
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xFFFF, M746Write);
}

static void StateRestore(int version) {
	Mapper746_Sync();
}

void Mapper746_Init(CartInfo* info) {
	info->Reset = Mapper746_Reset;
	info->Power = Mapper746_Power;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}