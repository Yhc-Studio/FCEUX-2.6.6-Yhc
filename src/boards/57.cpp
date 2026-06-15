/* FCE Ultra - NES/Famicom Emulator
*
* Copyright notice for this file:
*  Copyright (C) 2005 CaH4e3
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

static uint8 reg[2];

#define prg                (reg[1] >>5)
#define outerCHR           (reg[1] &0x07 | reg[0] >>3 &0x08)
#define innerCHR           (reg[0] &0x03)
#define cnrom              ~reg[0] &0x80
#define	nrom256             reg[1] &0x10
#define horizontalMirroring reg[1] &0x08

static uint8 hrd_flag = 0;

static SFORMAT StateRegs[] =
{
	{ &hrd_flag, 1, "DPSW" },
	{ &reg, 2, "REG" },
	{ 0 }
};

static void Sync(void) {
	if (nrom256)
		setprg32(0x8000, prg >> 1);
	else {
		setprg16(0x8000, prg);
		setprg16(0xC000, prg);
	}
	if (cnrom)
		setchr8(innerCHR | outerCHR &~0x3);
	else
		setchr8(outerCHR);

	if (horizontalMirroring)
		setmirror(MI_H);
	else
		setmirror(MI_V);

}

static DECLFR(M57Read) {
	return hrd_flag;
}

static DECLFW(M57Write) {
	//reg[A >> 11] = V;
	if ((A & 0x8800) == 0x8800)
		reg[1] = V;
	else
		reg[0] = V;
	
	Sync();
}

static void M57Power(void) {
	hrd_flag = 0;
	reg[0] = 0;
	reg[1] = 0;
	SetReadHandler(0x8000, 0xFFFF, CartBR);
	SetWriteHandler(0x8000, 0xFFFF, M57Write);
	SetReadHandler(0x6000, 0x6000, M57Read);
	Sync();
}

static void M57Reset() {
	hrd_flag++;
	hrd_flag &= 3;
	FCEU_printf("Select Register = %02x\n", hrd_flag);
}

static void StateRestore(int version) {
	Sync();
}

void Mapper57_Init(CartInfo *info) {
	info->Power = M57Power;
	info->Reset = M57Reset;
	GameStateRestore = StateRestore;
	AddExState(&StateRegs, ~0, 0, 0);
}