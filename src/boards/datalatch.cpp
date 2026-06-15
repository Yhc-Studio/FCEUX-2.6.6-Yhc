/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
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
#include "cart.h"
#include "../ines.h"
#include "../fds.h"


static uint8 latche=0, latcheinit=0, bus_conflict=0;
static uint16 addrreg0=0, addrreg1=0;
static uint8 *WRAM = NULL;
static uint32 WRAMSIZE=0;
static void (*WSync)(void) = nullptr;
static uint8 submapper;
static uint16 OuterBank;
static uint32 addlatche;

static DECLFW(LatchWrite) {
//	FCEU_printf("bs %04x %02x\n",A,V);
	if (bus_conflict)
		latche = V & CartBR(A);
	else
		latche = V;
	//后增加
	addlatche = A;
	WSync();
}

static void LatchPower(void) {
	latche = latcheinit;
	WSync();
	if (WRAM) {
		SetReadHandler(0x6000, 0xFFFF, CartBR);
		SetWriteHandler(0x6000, 0x7FFF, CartBW);
		FCEU_CheatAddRAM(WRAMSIZE >> 10, 0x6000, WRAM);
	} else {
		SetReadHandler(0x8000, 0xFFFF, CartBR);
	}
	SetWriteHandler(addrreg0, addrreg1, LatchWrite);
}

static void LatchClose(void) {
	if (WRAM)
		FCEU_gfree(WRAM);
	WRAM = NULL;
}

static void StateRestore(int version) {
	WSync();
}

static void Latch_Init(CartInfo *info, void (*proc)(void), uint8 init, uint16 adr0, uint16 adr1, uint8 wram, uint8 busc) {
	bus_conflict = busc;
	latcheinit = init;
	addrreg0 = adr0;
	addrreg1 = adr1;
	WSync = proc;
	info->Power = LatchPower;
	info->Close = LatchClose;
	GameStateRestore = StateRestore;
	submapper = info->submapper;
	if(info->ines2)
		if(info->battery_wram_size + info->wram_size > 0)
			wram = 1;
	if (wram)
	{
		if(info->ines2)
		{
			//I would like to do it in this way, but FCEUX is woefully inadequate
			//for instance if WRAMSIZE is large, the cheat pointers may get overwritten. and it's just a giant mess.
			//WRAMSIZE = info->battery_wram_size + info->wram_size;
			//WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
			//if(!info->wram_size && !info->battery_wram_size) {}
			//else if(info->wram_size && !info->battery_wram_size)
			//	SetupCartPRGMapping(0x10, WRAM, info->wram_size, 1);
			//else if(!info->wram_size && info->battery_wram_size)
			//{
			//	SetupCartPRGMapping(0x10, WRAM, info->battery_wram_size, 1);
			//	info->addSaveGameBuf( WRAM, info->battery_wram_size );
			//} else {
			//	//well, this is annoying
			//	SetupCartPRGMapping(0x10, WRAM, info->wram_size, 1);
			//	SetupCartPRGMapping(0x11, WRAM, info->battery_wram_size, 1); //? ? ? there probably isnt even a way to select this
			//	info->addSaveGameBuf( WRAM + info->wram_size, info->battery_wram_size );
			//}
			
			//this is more likely the only practical scenario
			WRAMSIZE = 8192;
			WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
			SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, 1);
			SetReadHandler(0x6000, 0x7FFF, CartBR);
			SetWriteHandler(0x6000, 0x7FFF, CartBW);
			setprg8r(0x10, 0x6000, 0);
			if(info->battery_wram_size)
			{
				info->addSaveGameBuf( WRAM, 8192 );
			}
		}
		else
		{
			WRAMSIZE = 8192;
			WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
			SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, 1);
			if (info->battery) {
				info->addSaveGameBuf( WRAM, WRAMSIZE );
			}
			AddExState(WRAM, WRAMSIZE, 0, "WRAM");
		}
		
	}
	AddExState(&latche, 1, 0, "LATC");
}

//------------------ Map 0 ---------------------------

#ifdef DEBUG_MAPPER
static DECLFW(NROMWrite) {
	FCEU_printf("bs %04x %02x\n", A, V);
	CartBW(A, V);
}
#endif

static void NROMPower(void) {
	setprg8r(0x10, 0x6000, 0);	// Famili BASIC (v3.0) need it (uses only 4KB), FP-BASIC uses 8KB
	setprg16(0x8000, ~1);
	setprg16(0xC000, ~0);
	setchr8(0);

	SetReadHandler(0x6000, 0x7FFF, CartBR);
	SetWriteHandler(0x6000, 0x7FFF, CartBW);
	SetReadHandler(0x8000, 0xFFFF, CartBR);

	FCEU_CheatAddRAM(WRAMSIZE >> 10, 0x6000, WRAM);

#ifdef DEBUG_MAPPER
	SetWriteHandler(0x4020, 0xFFFF, NROMWrite);
	#endif
}

void NROM_Init(CartInfo *info) {
	info->Power = NROMPower;
	info->Close = LatchClose;

	WRAMSIZE = 8192;
	WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
	SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, 1);
	if (info->battery) {
		info->addSaveGameBuf( WRAM, WRAMSIZE );
	}
	AddExState(WRAM, WRAMSIZE, 0, "WRAM");
}

//------------------ Map 2 ---------------------------

static void UNROMSync(void) {
//	static uint32 mirror_in_use = 0;
//	if (PRGsize[0] <= 128 * 1024) {
//		setprg16(0x8000, latche & 0x7);
//		if (latche & 8) mirror_in_use = 1;
//		if (mirror_in_use)
//			setmirror(((latche >> 3) & 1) ^ 1);	// Higway Star Hacked mapper, disabled till new mapper defined
//	} else
	setprg16(0x8000, latche);
	setprg16(0xc000, ~0);
	setchr8(0);
}

void UNROM_Init(CartInfo *info) {
	Latch_Init(info, UNROMSync, 0, 0x8000, 0xFFFF, 0, info->ines2 && info->submapper == 2);
}

//------------------ Map 3 ---------------------------

static void CNROMSync(void) {
	setchr8(latche);
	setprg32(0x8000, 0);
	setprg8r(0x10, 0x6000, 0);	// Hayauchy IGO uses 2Kb or RAM
}

void CNROM_Init(CartInfo *info) {
	Latch_Init(info, CNROMSync, 0, 0x8000, 0xFFFF, 1, info->ines2 && info->submapper == 2);
}

//------------------ Map 7 ---------------------------

static void ANROMSync() {
	setprg32(0x8000, latche & 0xF);
	setmirror(MI_0 + ((latche >> 4) & 1));
	setchr8(0);
}

void ANROM_Init(CartInfo *info) {
	Latch_Init(info, ANROMSync, 0, 0x4020, 0xFFFF, 0, 0);
}

//------------------ Map 8 ---------------------------

static void M8Sync() {
	setprg16(0x8000, latche >> 3);
	setprg16(0xc000, 1);
	setchr8(latche & 3);
}

void Mapper8_Init(CartInfo *info) {
	Latch_Init(info, M8Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 11 ---------------------------

static void M11Sync(void) {
	setprg32(0x8000, latche & 0xF);
	setchr8(latche >> 4);
}

void Mapper11_Init(CartInfo *info) {
	Latch_Init(info, M11Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

void Mapper144_Init(CartInfo *info) {
	Latch_Init(info, M11Sync, 0, 0x8001, 0xFFFF, 0, 0);
}

//------------------ Map 13 ---------------------------

static void CPROMSync(void) {
	setchr4(0x0000, 0);
	setchr4(0x1000, latche & 3);
	setprg32(0x8000, 0);
}

void CPROM_Init(CartInfo *info) {
	Latch_Init(info, CPROMSync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 29 ---------------------------	//Used by Glider, http://www.retrousb.com/product_info.php?cPath=30&products_id=58

static void M29Sync() {
	setprg16(0x8000, (latche & 0x1C) >> 2);
	setprg16(0xc000, ~0);
	setchr8r(0, latche & 3);
	setprg8r(0x10, 0x6000, 0);
}

void Mapper29_Init(CartInfo *info) {
	Latch_Init(info, M29Sync, 0, 0x8000, 0xFFFF, 1, 0);
}


//------------------ Map 38 ---------------------------

static void M38Sync(void) {
	setprg32(0x8000, latche & 3);
	setchr8(latche >> 2);
}

void Mapper38_Init(CartInfo *info) {
	Latch_Init(info, M38Sync, 0, 0x7000, 0x7FFF, 0, 0);
}

//------------------ Map 66 ---------------------------

static void MHROMSync(void) {
	setprg32(0x8000, latche >> 4);
	setchr8(latche & 0xF);
}

void MHROM_Init(CartInfo *info) {
	Latch_Init(info, MHROMSync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 70 ---------------------------

static void M70Sync() {
	setprg16(0x8000, latche >> 4);
	setprg16(0xc000, ~0);
	setchr8(latche & 0xf);
}

void Mapper70_Init(CartInfo *info) {
	Latch_Init(info, M70Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 78 ---------------------------
/* Should be two separate emulation functions for this "mapper".  Sigh.  URGE TO KILL RISING. */
static void M78Sync() {
	setprg16(0x8000, (latche & 7));
	setprg16(0xc000, ~0);
	setchr8(latche >> 4);
	if (submapper == 3) {
		setmirror((latche >> 3) & 1);	
	} else {
		setmirror(MI_0 + ((latche >> 3) & 1));
	}
}

void Mapper78_Init(CartInfo *info) {
	if (info->CRC32 == 0xba51ac6f) info->submapper = 3;
	Latch_Init(info, M78Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 86 ---------------------------

static void M86Sync(void) {
	setprg32(0x8000, (latche >> 4) & 3);
	setchr8((latche & 3) | ((latche >> 4) & 4));
}

void Mapper86_Init(CartInfo *info) {
	Latch_Init(info, M86Sync, ~0, 0x6000, 0x6FFF, 0, 0);
}

//------------------ Map 87 ---------------------------

static void M87Sync(void) {
	setprg32(0x8000, 0);
	setchr8(((latche >> 1) & 1) | ((latche << 1) & 2));
}

void Mapper87_Init(CartInfo *info) {
	Latch_Init(info, M87Sync, ~0, 0x6000, 0xFFFF, 0, 0);
}

//------------------ Map 89 ---------------------------

static void M89Sync(void) {
	setprg16(0x8000, (latche >> 4) & 7);
	setprg16(0xc000, ~0);
	setchr8((latche & 7) | ((latche >> 4) & 8));
	setmirror(MI_0 + ((latche >> 3) & 1));
}

void Mapper89_Init(CartInfo *info) {
	Latch_Init(info, M89Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 93 ---------------------------

static void SSUNROMSync(void) {
	setprg16(0x8000, latche >> 4);
	setprg16(0xc000, ~0);
	setchr8(0);
}

void SUNSOFT_UNROM_Init(CartInfo *info) {
	Latch_Init(info, SSUNROMSync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 94 ---------------------------

static void M94Sync(void) {
	setprg16(0x8000, latche >> 2);
	setprg16(0xc000, ~0);
	setchr8(0);
}

void Mapper94_Init(CartInfo *info) {
	Latch_Init(info, M94Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 97 ---------------------------

static void M97Sync(void) {
	setchr8(0);
	setprg16(0x8000, ~0);
	setprg16(0xc000, latche & 15);
	switch (latche >> 6) {
	case 0: break;
	case 1: setmirror(MI_H); break;
	case 2: setmirror(MI_V); break;
	case 3: break;
	}
	setchr8(((latche >> 1) & 1) | ((latche << 1) & 2));
}

void Mapper97_Init(CartInfo *info) {
	Latch_Init(info, M97Sync, ~0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 101 ---------------------------

static void M101Sync(void) {
	setprg32(0x8000, 0);
	setchr8(latche);
}

void Mapper101_Init(CartInfo *info) {
	Latch_Init(info, M101Sync, ~0, 0x6000, 0x7FFF, 0, 0);
}

//------------------ Map 107 ---------------------------

static void M107Sync(void) {
	setprg32(0x8000, (latche >> 1) & 3);
	setchr8(latche & 7);
}

void Mapper107_Init(CartInfo *info) {
	Latch_Init(info, M107Sync, ~0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 113 ---------------------------

static void M113Sync(void) {
	setprg32(0x8000, (latche >> 3) & 7);
	setchr8(((latche >> 3) & 8) | (latche & 7));
//	setmirror(latche>>7); // only for HES 6in1
}

void Mapper113_Init(CartInfo *info) {
	Latch_Init(info, M113Sync, 0, 0x4100, 0x7FFF, 0, 0);
}

//------------------ Map 140 ---------------------------

void Mapper140_Init(CartInfo *info) {
	Latch_Init(info, MHROMSync, 0, 0x6000, 0x7FFF, 0, 0);
}

//------------------ Map 152 ---------------------------

static void M152Sync() {
	setprg16(0x8000, (latche >> 4) & 7);
	setprg16(0xc000, ~0);
	setchr8(latche & 0xf);
	setmirror(MI_0 + ((latche >> 7) & 1));	/* Saint Seiya...hmm. */
}

void Mapper152_Init(CartInfo *info) {
	Latch_Init(info, M152Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 180 ---------------------------

static void M180Sync(void) {
	setprg16(0x8000, 0);
	setprg16(0xc000, latche);
	setchr8(0);
}

void Mapper180_Init(CartInfo *info) {
	Latch_Init(info, M180Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 184 ---------------------------

static void M184Sync(void) {
	setchr4(0x0000, latche);
	setchr4(0x1000, latche >> 4);
	setprg32(0x8000, 0);
}

void Mapper184_Init(CartInfo *info) {
	Latch_Init(info, M184Sync, 0, 0x6000, 0x7FFF, 0, 0);
}

//------------------ Map 203 ---------------------------

static void M203Sync(void) {
	setprg16(0x8000, latche >> 2);
	setprg16(0xC000, latche >> 2);
	setchr8(latche & 3);
}

void Mapper203_Init(CartInfo *info) {
	Latch_Init(info, M203Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

//------------------ Map 218 ---------------------------

static void Mapper218_Power()
{
	//doesn't really matter
	SetReadHandler(0x6000, 0xFFFF, &CartBROB);
}

void Mapper218_Init(CartInfo *info)
{
	info->Power = &Mapper218_Power;

	//fixed PRG mapping
	setprg32(0x8000, 0);

	//this mapper is supposed to interpret the iNES header bits specially
	static const uint8 mirrorings[] = {MI_V,MI_H,MI_0,MI_1};
	SetupCartMirroring(mirrorings[info->mirrorAs2Bits],1,nullptr);

	//cryptic logic to effect the CHR RAM mappings by mapping 1k blocks to NTARAM according to how the pins are wired
	//this could be done by bit logic, but this is self-documenting
	static const uint8 mapping[] = {
		0,1,0,1,0,1,0,1, //mirrorAs2Bits==0
		0,0,1,1,0,0,1,1, //mirrorAs2Bits==1
		0,0,0,0,1,1,1,1, //mirrorAs2Bits==2
		0,0,0,0,0,0,0,0  //mirrorAs2Bits==3
	};
	for(int i=0;i<8;i++)
		VPageR[i] = &NTARAM[mapping[info->mirrorAs2Bits*8+i]];

	PPUCHRRAM = 0xFF;
}

//------------------ Map 240 ---------------------------

static void M240Sync(void) {
	setprg8r(0x10, 0x6000, 0);
	setprg32(0x8000, latche >> 4);
	setchr8(latche & 0xF);
}

void Mapper240_Init(CartInfo *info) {
	Latch_Init(info, M240Sync, 0, 0x4020, 0x5FFF, 1, 0);
}

//------------------ Map 241 ---------------------------
// Mapper 7 mostly, but with SRAM or maybe prot circuit
// figure out, which games do need 5xxx area reading

static void M241Sync(void) {
	setchr8(0);
	setprg8r(0x10, 0x6000, 0);
	if (latche & 0x80)
		setprg32(0x8000, latche | 8);	// no 241 actually, but why not afterall?
	else
		setprg32(0x8000, latche);
}

void Mapper241_Init(CartInfo *info) {
	Latch_Init(info, M241Sync, 0, 0x8000, 0xFFFF, 1, 0);
}

//------------------ BMC-11160 ---------------------------
// Simple BMC discrete mapper by TXC

static void BMC11160Sync(void) {
	uint32 bank = (latche >> 4) & 7;
	setprg32(0x8000, bank);
	setchr8((bank << 2) | (latche & 3));
	setmirror((latche >> 7) & 1);
}

void BMC11160_Init(CartInfo *info) {
	Latch_Init(info, BMC11160Sync, 0, 0x8000, 0xFFFF, 0, 0);
}
//后增加
/*------------------ Map 538 ---------------------------*/
/* NES 2.0 Mapper 538 denotes the 60-1064-16L PCB, used for a
* bootleg cartridge conversion named Super Soccer Champion
* of the Konami FDS game Exciting Soccer.
*/
static uint8 M538Banks[16] = { 0, 1, 2, 1, 3, 1, 4, 1, 5, 5, 1, 1, 6, 6, 7, 7 };
static void M538Sync(void) {
	setprg8(0x6000, (latche >> 1) | 8);
	setprg8(0x8000, M538Banks[latche & 15]);
	setprg8(0xA000, 14);
	setprg8(0xC000, 7);
	setprg8(0xE000, 15);
	setchr8(0);
	setmirror(1);
}

static void M538Power(void) {
	FDSSoundPower();
	LatchPower();
}

void Mapper538_Init(CartInfo *info) {
	Latch_Init(info, M538Sync, 0, 0xC000, 0xCFFF, 1, 0);
	info->Power = M538Power;
}

/*------------------ BMC-K-3046 ---------------------------*/
/* NES 2.0 mapper 336 is used for an 11-in-1 multicart
* http://wiki.nesdev.com/w/index.php/NES_2.0_Mapper_336 */

static void BMCK3046Sync(void) {
	setprg16(0x8000, latche);
	setprg16(0xC000, latche | 0x07);
	setchr8(0);
	setmirror(latche & (submapper == 2 ? 0x08 : 0x20) ? MI_H : MI_V);
}

static DECLFW(BMCK3046_Write1) { /* Special bus conflict: 300 Ohm resistor placed so that for D3, ROM always wins. */
	latche = CartBR(A) & 0x08 | V & CartBR(A) & ~0x08;
	WSync();
}

static void BMCK3046_Power(void) {
	LatchPower();
	if (submapper == 1) SetWriteHandler(0x8000, 0xFFFF, BMCK3046_Write1);
}

static void BMCK3046_Reset(void) {
	latche = 0;
	WSync();
}

void BMCK3046_Init(CartInfo* info) {
	submapper = info->submapper;
	Latch_Init(info, BMCK3046Sync, 0, 0x8000, 0xFFFF, 0, 0);
	info->Reset = BMCK3046_Reset;
	info->Power = BMCK3046_Power;
}

/*------------------ Map 381 ---------------------------*/
/* 2-in-1 High Standard Game (BC-019), reset-based */
static uint8 reset = 0;
static void M381Sync(void) {
	setprg16(0x8000, ((latche & 0x10) >> 4) | ((latche & 7) << 1) | (reset << 4));
	setprg16(0xC000, 15 | (reset << 4));
	setchr8(0);
}

static void M381Reset(void) {
	reset ^= 1;
	M381Sync();
}

void Mapper381_Init(CartInfo *info) {
	info->Reset = M381Reset;
	Latch_Init(info, M381Sync, 0, 0x8000, 0xFFFF, 1, 0);
	AddExState(&reset, 1, 0, "RST0");
}

/*------------------ Mapper 415 ---------------------------*/

static void Mapper415_Sync(void) {
	setprg8(0x6000, latche & 0x0F);
	setprg32(0x8000, ~0);
	setchr8(0);
	setmirror(((latche >> 4) & 1) ^ 1);
}

static void M415Power(void) {
	LatchPower();
	SetReadHandler(0x6000, 0x7FFF, CartBR);
}

void Mapper415_Init(CartInfo *info) {
	Latch_Init(info, Mapper415_Sync, 0, 0x8000, 0xFFFF, 0, 0);
	info->Power = M415Power;
}

/*------------------ Mapper 462 ---------------------------*/
static uint8 M462OuterBank;

static void Mapper462_Sync(void) {
	if (M462OuterBank & 0x40) {
		setprg32(0x8000, latche & 7 | M462OuterBank >> 3 & ~7);
		setmirror(latche & 0x10 ? MI_1 : MI_0);
	}
	else {
		setprg16(0x8000, latche & 7 | M462OuterBank >> 2 & ~7);
		setprg16(0xC000, 7 | M462OuterBank >> 2 & ~7);
		setmirror(M462OuterBank & 0x10 ? MI_V : MI_H);
	}
	setchr8(0);
}

static DECLFW(M462_WriteExtra) {
	M462OuterBank = V;
	Mapper462_Sync();
}

static void M462Power(void) {
	M462OuterBank = 0;
	LatchPower();
	SetWriteHandler(0xA000, 0xBFFF, M462_WriteExtra);
}

static void M462Reset(void) {
	M462OuterBank = 0;
	Mapper462_Sync();
}

void BMC_971107_00G_Init(CartInfo* info) {
	Latch_Init(info, Mapper462_Sync, 0, 0x8000, 0xFFFF, 0, 0);
	info->Power = M462Power;
	info->Reset = M462Reset;
	AddExState(&M462OuterBank, 1, 0, "EXP0");
}

/* Mapper 429: LIKO BBG-235-8-1B/Milowork FCFC1 */

static void Mapper429_Sync(void) {
	
	setprg32(0x8000, latche >> 2);
	if (submapper == 2)
		setchr8(latche & 3 | (latche & 4 && latche & 8 ? 4 : 0));
	else
		setchr8(latche);
	if (submapper == 1) {
		if (latche & 0x80)
			setmirror(MI_1);
		else
			setmirror(MI_0);
	}
}

static void Mapper429_Reset(void) {
	latche = 4; /* Initial CHR bank 0, initial PRG bank 1 */
	Mapper429_Sync();
}

void Mapper429_Init(CartInfo *info) {
	info->Reset = Mapper429_Reset;
	submapper = info->submapper;
	Latch_Init(info, Mapper429_Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

/* -------------- Mapper 764 ------------------------ */
static void M764Sync(void) {

	if (latche & 0x40)
		setprg32(0x8000, latche >> 1);
	else {
		setprg16(0x8000, latche);
		setprg16(0xC000, latche | 7);
	}
	setchr8r(0x0000, 0);
	if (latche & 0x20)
		setmirror(MI_H);
	else
		setmirror(MI_V);
}

void Mapper764_Init(CartInfo *info) {
	Latch_Init(info, M764Sync, NULL, 0x0000, 0x8000, 0xFFFF, 0);
}
/*------------------ Map 271 ---------------------------*/

static void M271Sync(void) {
	setchr8(latche & 0x0F);
	setprg32(0x8000, latche >> 4);
	setmirror((latche >> 5) & 1);
}

void Mapper271_Init(CartInfo *info) {
	Latch_Init(info, M271Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

/*------------------ Map 740 ---------------------------*/

static void M740Sync(void) {

	if ((latche & 0x1F) == 2)
		setprg32(0x8000, latche >> 1 & 0x0f);
	else {
		setprg16(0x8000, latche & 0x1F);
		setprg16(0xC000, latche & 0x1F);
	}
	setchr8(latche & 0x1F);
	switch (latche >> 6) {
	case 0: setmirrorw(0, 0, 0, 1); break;
	case 1:	setmirror(MI_V); break;
	case 2:	setmirror(MI_H); break;
	case 3:	setmirror(MI_1); break;
	}
}

void Mapper740_Init(CartInfo *info) {
	Latch_Init(info, M740Sync, 0, 0x8000, 0xFFFF, 0, 0);
}

/*------------------ Map 481 ---------------------------*/

static void M481Sync(void) {
	setprg16(0x8000, latche >> 4 & ~7 | latche & 7);
	setprg16(0xc000, latche >> 4 | 7);
	setchr8(0);
}
void Mapper481_Init(CartInfo* info) {
	Latch_Init(info, M481Sync, 0, 0x8000, 0xFFFF, 1, 0);
}

/*------------------ Mapper 477 ---------------------------*/
static void Mapper477_Sync(void) {
	if (latche & 0xE) {
		setprg16(0x8000, 0xFF);
		setprg16(0xC000, latche);
	}
	else
		setprg32(0x8000, latche >> 1);
	setchr8(latche);
}

static DECLFW(Mapper477_Write) { /* Resistors placed on the first 128 KiB PRG ROM chip (banks 8-15) cause ROM to always win D0..D3. Otherwise, normal AND-type bus conflicts. */
	latche = latche & 8 ? (CartBR(A) & 0x0F | V & CartBR(A) & ~0x0F) : (CartBR(A) & V);
	WSync();
}

static void Mapper477_Power(void) {
	LatchPower();
	SetWriteHandler(0x8000, 0xFFFF, Mapper477_Write);
}

static void Mapper477_Reset(void) {
	latche = 0;
	WSync();
}

void Mapper477_Init(CartInfo* info) {
	submapper = info->submapper;
	Latch_Init(info, Mapper477_Sync, 0, 0x8000, 0xFFFF, 0, 0);
	info->Reset = Mapper477_Reset;
	info->Power = Mapper477_Power;
}