/* FCEUmm - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 2020
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
 *
 */

#include "mapinc.h"
#include "mmc3.h"

static uint8 submapper;

static void M432CW(uint32 A, uint8 V) {
	int chrAND = (EXPREGS[1] & 0x04) ? 0x7F : 0xFF;
	int chrOR = (EXPREGS[1] << 7) & 0x080 | (EXPREGS[1] << 5) & 0x100 | (EXPREGS[1] << 4) & 0x200;
	if (submapper == 3 && EXPREGS[1] & 0x20) {
		chrAND |= 0x100;
		chrAND >>= 1;
		chrOR >>= 1;
		setchr2(0x0000, getCHRBank(0) & chrAND | chrOR & ~chrAND);
		setchr2(0x0800, getCHRBank(3) & chrAND | chrOR & ~chrAND);
		setchr2(0x1000, getCHRBank(4) & chrAND | chrOR & ~chrAND);
		setchr2(0x1800, getCHRBank(7) & chrAND | chrOR & ~chrAND);
	}
	else
		setchr1(A, (V & chrAND) | (chrOR & ~chrAND));
}

static void M432PW(uint32 A, uint8 V) {
	int prgAND = (EXPREGS[1] & 0x02) ? 0x0F : 0x1F;
	int prgOR = ((EXPREGS[1] << 4) & 0x10) | (EXPREGS[1] << 1) & 0x60;

	bool nrom = !!(EXPREGS[1] & 0x40);
	bool nrom256 = submapper == 2 ? !!(EXPREGS[1] & 0x20) : !!(EXPREGS[1] & 0x80);

	if (nrom)
		syncPRG_GNROM_67(nrom256 ? 2 : 0, prgAND, prgOR & ~prgAND);
	else
		syncPRG(prgAND, prgOR & ~prgAND);
}

static DECLFR(M432Read) {
	if (submapper == 1 ? !!(EXPREGS[1] & 0x20) : !!(EXPREGS[0] & 0x01)) return EXPREGS[2];
	return CartBR(A);
}

static DECLFW(M432Write) {
	if (submapper == 3 && EXPREGS[1] & 0x80)
		;
	else
		EXPREGS[A & 1] = V;
	FixMMC3PRG(MMC3_cmd);
	FixMMC3CHR(MMC3_cmd);
}

static void M432Reset(void) {
	EXPREGS[0] = 0;
	EXPREGS[1] = 0;
	EXPREGS[2]++;
	MMC3RegReset();
}

static void M432Power(void) {
	EXPREGS[0] = 0;
	EXPREGS[1] = 0;
	EXPREGS[2] = 0;
	GenMMC3Power();
	SetReadHandler(0x8000, 0xFFFF, M432Read);
	SetWriteHandler(0x6000, 0x7FFF, M432Write);
}

void Mapper432_Init(CartInfo* info) {
	submapper = info->submapper;
	GenMMC3_Init(info, 256, 256, 0, 0);
	cwrap = M432CW;
	pwrap = M432PW;
	info->Power = M432Power;
	info->Reset = M432Reset;
	AddExState(EXPREGS, 3, 0, "EXPR");
}