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

/* S-009. UNROM plus outer bank register at $6000-$7FFF. */

#include "mapinc.h"

static uint16 latch;
static uint8 reg[2];

static void Mapper439_Sync(void) {
	setprg16(0x8000, reg[0] >> 1 & ~7 | latch & 7);
	setprg16(0xC000, reg[0] >> 1 & ~7 | 7);
	setchr8(0);
	if (latch & 0x80)
		setmirror(MI_H);
	else
		setmirror(MI_V);
}

static DECLFW(Mapper439_WriteReg) {
	reg[A & 1] = V;
	Mapper439_Sync();
}

static DECLFW(Mapper439_WriteLatch) {
	if (~reg[0] & 0x80)
		latch = latch &~0x07 | V & 0x07;
	else
		latch = V;
	Mapper439_Sync();
}

static void Mapper439_Reset(void) {
	for (auto& c : reg) c = 0xFF;
	Mapper439_Sync();
}

static void Mapper439_Power(void) {
	latch = 0;
	for (auto& c : reg) c = 0xFF;
	Mapper439_Sync();
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x6000, 0x7FFF, Mapper439_WriteReg);
	SetWriteHandler(0x8000, 0xFFFF, Mapper439_WriteLatch);
}

void Mapper439_Init(CartInfo *info) {
	info->Reset = Mapper439_Reset;
	info->Power = Mapper439_Power;
	AddExState(&latch, 2, 0, "LATC");
	AddExState(&reg, 2, 0, "LATC");
}