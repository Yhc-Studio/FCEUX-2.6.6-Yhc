/* FCE Ultra - NES/Famicom Emulator
 *
 * Copyright notice for this file:
 *  Copyright (C) 1998 BERO
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

#include "types.h"
#include "x6502.h"
#include "fceu.h"
#include "cart.h"
#include "ppu.h"

#include "ines.h"
#include "unif.h"
#include "state.h"
#include "file.h"
#include "utils/general.h"
#include "utils/memory.h"
#include "utils/crc32.h"
#include "utils/md5.h"
#include "utils/xstring.h"
#include "cheat.h"
#include "vsuni.h"
#include "driver.h"
#include "input.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern SFORMAT FCEUVSUNI_STATEINFO[];

//mbg merge 6/29/06 - these need to be global
uint8 *trainerpoo = NULL;
uint8 *ROM = NULL;
uint8 *VROM = NULL;
uint8* MiscROM = NULL;
uint8 *ExtraNTARAM = NULL;
iNES_HEADER head;

static CartInfo iNESCart;

uint8 Mirroring = 0;
uint8 MirroringAs2bits = 0;
uint32 ROM_size = 0;
uint32 VROM_size = 0;
uint32 MiscROM_size = 0;
uint32 Temp_Rom_Size = 0;
char LoadedRomFName[4096]; //mbg merge 7/17/06 added
char LoadedRomFNamePatchToUse[4096];

static int CHRRAMSize = -1;
static int iNES_Init(int num);

static int MapperNo = 0;

int iNES2 = 0;

static DECLFR(TrainerRead) {
	return(trainerpoo[A & 0x1FF]);
}

static void iNES_ExecPower() {
	if (CHRRAMSize != -1)
		FCEU_MemoryRand(VROM, CHRRAMSize);

	if (iNESCart.Power)
		iNESCart.Power();

	if (trainerpoo) {
		int x;
		for (x = 0; x < 512; x++) {
			X6502_DMW(0x7000 + x, trainerpoo[x]);
			if (X6502_DMR(0x7000 + x) != trainerpoo[x]) {
				SetReadHandler(0x7000, 0x71FF, TrainerRead);
				break;
			}
		}
	}
}

void iNESGI(GI h) { //bbit edited: removed static keyword
	switch (h) {
	case GI_RESETSAVE:
		FCEU_ClearGameSave(&iNESCart);
		break;

	case GI_RESETM2:
		if (iNESCart.Reset)
			iNESCart.Reset();
		break;
	case GI_POWER:
		iNES_ExecPower();
		break;
	case GI_CLOSE:
	{
		FCEU_SaveGameSave(&iNESCart);
		if (iNESCart.Close)
			iNESCart.Close();
		if (ROM) {
			FCEU_free(ROM);
			ROM = NULL;
		}
		if (VROM) {
			FCEU_free(VROM);
			VROM = NULL;
		}
		if (trainerpoo) {
			free(trainerpoo);
			trainerpoo = NULL;
		}
		if (ExtraNTARAM) {
			free(ExtraNTARAM);
			ExtraNTARAM = NULL;
		}
	}
	break;
	}
}

uint32 iNESGameCRC32 = 0;

struct CRCMATCH {
	uint32 crc;
	char *name;
};

struct INPSEL {
	uint32 crc32;
	ESI input1;
	ESI input2;
	ESIFC inputfc;
};

/*
* Function to set input controllers based on CRC
*/
static void SetInput(void) {
	static struct INPSEL moo[] =
	{
		{0x19b0a9f1,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// 6-in-1 (MGC-023)(Unl)[!]
		{0x29de87af,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Aerobics Studio
		{0xd89e5a67,	SI_UNSET,		SI_UNSET,		SIFC_ARKANOID	},	// Arkanoid (J)
		{0x0f141525,	SI_UNSET,		SI_UNSET,		SIFC_ARKANOID	},	// Arkanoid 2(J)
		{0x32fb0583,	SI_UNSET,		SI_ARKANOID,	SIFC_NONE		},	// Arkanoid(NES)
		{0x60ad090a,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Athletic World
		{0x48ca0ee1,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_BWORLD		},	// Barcode World
		{0x4318a2f8,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Barker Bill's Trick Shooting
		{0x6cca1c1f,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Dai Undoukai
		{0x24598791,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// Duck Hunt
		{0xd5d6eac4,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Edu (As)
		{0xe9a7fe9e,	SI_UNSET,		SI_MOUSE,		SIFC_SUBORKB	},	// Educational Computer 2000
		{0x8f7b1669,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// FP BASIC 3.3 by maxzhou88
		{0xf7606810,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Family BASIC 2.0A
		{0x895037bc,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Family BASIC 2.1a
		{0xb2530afc,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Family BASIC 3.0
		{0xea90f3e2,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Family Trainer:  Running Stadium
		{0xbba58be5,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Family Trainer: Manhattan Police
		{0x3e58a87e,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Freedom Force
		{0xd9f45be9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_QUIZKING	},	// Gimme a Break ...
		{0x1545bd13,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_QUIZKING	},	// Gimme a Break ... 2
		{0x4e959173,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Gotcha! - The Sport!
		{0xbeb8ab01,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Gumshoe
		{0xff24d794,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Hogan's Alley
		{0x21f85681,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_HYPERSHOT	},	// Hyper Olympic (Gentei Ban)
		{0x980be936,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_HYPERSHOT	},	// Hyper Olympic
		{0x915a53a7,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_HYPERSHOT	},	// Hyper Sports
		{0x9fae4d46,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_MAHJONG	},	// Ide Yousuke Meijin no Jissen Mahjong
		{0x7b44fb2a,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_MAHJONG	},	// Ide Yousuke Meijin no Jissen Mahjong 2
		{0x2f128512,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Jogging Race
		{0xbb33196f,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Keyboard Transformer
		{0x8587ee00,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Keyboard Transformer
		{0x543ab532,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// LIKO Color Lines
		{0x368c19a8,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// LIKO Study Cartridge
		{0x5ee6008e,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Mechanized Attack
		{0x370ceb65,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Meiro Dai Sakusen
		{0x3a1694f9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_4PLAYER	},	// Nekketsu Kakutou Densetsu
		{0x9d048ea4,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_OEKAKIDS	},	// Oeka Kids
		{0x2a6559a1,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Operation Wolf (J)
		{0xedc3662b,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Operation Wolf
		{0x912989dc,	SI_UNSET,		SI_UNSET,		SIFC_FKB		},	// Playbox BASIC
		{0x9044550e,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Rairai Kyonshizu
		{0xea90f3e2,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Running Stadium
		{0x851eb9be,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// Shooting Range
		{0x6435c095,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// Short Order/Eggsplode
		{0xc043a8df,	SI_UNSET,		SI_MOUSE,		SIFC_NONE		},	// Shu Qi Yu - Shu Xue Xiao Zhuan Yuan (Ch)
		{0x2cf5db05,	SI_UNSET,		SI_MOUSE,		SIFC_NONE		},	// Shu Qi Yu - Zhi Li Xiao Zhuan Yuan (Ch)
		{0xad9c63e2,	SI_GAMEPAD,		SI_UNSET,		SIFC_SHADOW		},	// Space Shadow
		{0x61d86167,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// Street Cop
		{0xabb2f974,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Study and Game 32-in-1
		{0x41ef9ac4,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Subor
		{0x8b265862,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Subor
		{0x82f1fb96,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Subor 1.0 Russian
		{0x9f8f200a,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Super Mogura Tataki!! - Pokkun Moguraa (bad dump)
		{0xc7bcc981,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERA	},	// Super Mogura Tataki!! - Pokkun Moguraa
		{0xd74b2719,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// Super Team Games
		{0x74bea652,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// Supergun 3-in-1
		{0x5e073a1b,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// Supor English (Chinese)
		{0x589b6b0d,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// SuporV20
		{0x41401c6d,	SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	},	// SuporV40
		{0x23d17f5e,	SI_GAMEPAD,		SI_ZAPPER,		SIFC_NONE		},	// The Lone Ranger
		{0xc3c0811d,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_OEKAKIDS	},	// The two "Oeka Kids" games
		{0xde8fd935,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// To the Earth
		{0x47232739,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_TOPRIDER	},	// Top Rider
		{0x8a12a7d9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FTRAINERB	},	// Totsugeki Fuuun Takeshi Jou
		{0xb8b9aca3,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Wild Gunman
		{0x5112dc21,	SI_UNSET,		SI_ZAPPER,		SIFC_NONE		},	// Wild Gunman
		{0xaf4010ea,	SI_GAMEPAD,		SI_POWERPADB,	SIFC_UNSET		},	// World Class Track Meet
		{0x67b126b9,	SI_GAMEPAD,		SI_GAMEPAD,		SIFC_FAMINETSYS },	// Famicom Network System
		{0x00000000,	SI_UNSET,		SI_UNSET,		SIFC_UNSET		}
	};

	int x = 0;

	while (moo[x].input1 >= 0 || moo[x].input2 >= 0 || moo[x].inputfc >= 0) {
		if (moo[x].crc32 == iNESGameCRC32) {
			GameInfo->input[0] = moo[x].input1;
			GameInfo->input[1] = moo[x].input2;
			GameInfo->inputfc = moo[x].inputfc;
			break;
		}
		x++;
	}
}

struct INPSEL_NES20 {
	uint8 expansion_id;
	ESI input1;
	ESI input2;
	ESIFC inputfc;
};

/*
* Function to set input controllers based on NES 2.0 header
*/
extern int eoptions;
static void SetInputNes20(uint8 expansion) {
	static struct INPSEL_NES20 moo[] =
	{
		{0x01,			SI_GAMEPAD,		SI_GAMEPAD,		SIFC_UNSET		}, // Standard NES/Famicom controllers
		{0x02,			SI_GAMEPAD,		SI_GAMEPAD,		SIFC_NONE		}, // NES Four Score/Satellite with two additional standard controllers
		{0x03,			SI_GAMEPAD,		SI_GAMEPAD,		SIFC_4PLAYER	}, // Famicom Four Players Adapter with two additional standard controllers using the "simple" protocol
		{0x04,			SI_GAMEPAD,		SI_GAMEPAD,		SIFC_NONE		}, // Vs. System (1P via $4016)
		{0x05,			SI_GAMEPAD,		SI_GAMEPAD,		SIFC_NONE		}, // Vs. System (1P via $4017)
		{0x07,			SI_ZAPPER,		SI_NONE,		SIFC_NONE		}, // Vs. Zapper
		{0x08,			SI_UNSET,		SI_ZAPPER,		SIFC_NONE		}, // Zapper ($4017)
		{0x0A,			SI_UNSET,		SI_UNSET,		SIFC_SHADOW		}, // Bandai Hyper Shot Lightgun
		{0x0B,			SI_UNSET,		SI_POWERPADA,	SIFC_UNSET		}, // Power Pad Side A
		{0x0C,			SI_UNSET,		SI_POWERPADB,	SIFC_UNSET		}, // Power Pad Side B
		{0x0D,			SI_UNSET,		SI_UNSET,		SIFC_FTRAINERA	}, // Family Trainer Side A
		{0x0E,			SI_UNSET,		SI_UNSET,		SIFC_FTRAINERB	}, // Family Trainer Side B
		{0x0F,			SI_UNSET,		SI_ARKANOID,	SIFC_UNSET		}, // Arkanoid Vaus Controller (NES)
		{0x10,			SI_UNSET,		SI_UNSET,		SIFC_ARKANOID	}, // Arkanoid Vaus Controller (Famicom)
		{0x12,			SI_UNSET,		SI_UNSET,		SIFC_HYPERSHOT	}, // Konami Hyper Shot Controller
		{0x15,			SI_UNSET,		SI_UNSET,		SIFC_MAHJONG	}, // Jissen Mahjong Controller
		{0x17,			SI_UNSET,		SI_UNSET,		SIFC_OEKAKIDS	}, // Oeka Kids Tablet
		{0x18,			SI_UNSET,		SI_UNSET,		SIFC_BWORLD		}, // Sunsoft Barcode Battler
		{0x1B,			SI_UNSET,		SI_UNSET,		SIFC_TOPRIDER	}, // Top Rider (Inflatable Bicycle)
		{0x23,			SI_UNSET,		SI_UNSET,		SIFC_FKB		}, // Family BASIC Keyboard plus Famicom Data Recorder
		{0x24,			SI_UNSET,		SI_UNSET,		SIFC_PEC586KB	}, // Dongda PEC-586 Keyboard
		{0x26,			SI_UNSET,		SI_UNSET,		SIFC_SUBORKB	}, // Subor Keyboard
		//{0x27,			SI_UNSET,		SI_MOUSE,		SIFC_SUBORKB	}, // Subor Keyboard plus mouse (3x8-bit protocol)
		{0x28,			SI_UNSET,		SI_MOUSE,		SIFC_SUBORKB	}, // Subor Keyboard plus mouse (24-bit protocol)
		{0x29,			SI_UNSET,		SI_SNES_MOUSE,	SIFC_UNSET		}, // SNES Mouse
		{0,				SI_UNSET,		SI_UNSET,		SIFC_UNSET		}
	};

	int x = 0;

	if (expansion == 0x02) 
		eoptions |= 32768; // dirty hack to enable Four-Score
	GameInfo->vs_cswitch = expansion == 0x05;		

	while (moo[x].expansion_id) {
		if (moo[x].expansion_id == expansion) {
			GameInfo->input[0] = moo[x].input1;
			GameInfo->input[1] = moo[x].input2;
			GameInfo->inputfc = moo[x].inputfc;
			break;		}
		x++;
	}
}

#define INESB_INCOMPLETE  1
#define INESB_CORRUPT     2
#define INESB_HACKED      4

struct BADINF {
	uint64 md5partial;
	const char *name;
	uint32 type;
};

static struct BADINF BadROMImages[] =
{
	#include "ines-bad.h"
};

void CheckBad(uint64 md5partial) {
	int32 x = 0;
	while (BadROMImages[x].name) {
		if (BadROMImages[x].md5partial == md5partial) {
			FCEU_PrintError("The copy game you have loaded, \"%s\", is bad, and will not work properly in FCEUX.", BadROMImages[x].name);
			return;
		}
		x++;
	}
}


struct CHINF {
	uint32 crc32;
	int32 mapper;
	int32 mirror;
	const char* params;
};

static const TMasterRomInfo sMasterRomInfo[] = {
	{ 0x62b51b108a01d2beULL, "bonus=0" }, //4-in-1 (FK23C8021)[p1][!].nes
	{ 0x8bb48490d8d22711ULL, "bonus=0" }, //4-in-1 (FK23C8033)[p1][!].nes
	{ 0xc75888d7b48cd378ULL, "bonus=0" }, //4-in-1 (FK23C8043)[p1][!].nes
	{ 0xf81a376fa54fdd69ULL, "bonus=0" }, //4-in-1 (FK23Cxxxx, S-0210A PCB)[p1][!].nes
	{ 0xa37eb9163e001a46ULL, "bonus=0" }, //4-in-1 (FK23C8026) [p1][!].nes
	{ 0xde5ce25860233f7eULL, "bonus=0" }, //4-in-1 (FK23C8045) [p1][!].nes
	{ 0x5b3aa4cdc484a088ULL, "bonus=0" }, //4-in-1 (FK23C8056) [p1][!].nes
	{ 0x9342bf9bae1c798aULL, "bonus=0" }, //4-in-1 (FK23C8079) [p1][!].nes
	{ 0x164eea6097a1e313ULL, "busc=1" }, //Cybernoid - The Fighting Machine (U)[!].nes -- needs bus conflict emulation
};
const TMasterRomInfo* MasterRomInfo;
TMasterRomInfoParams MasterRomInfoParams;

static void CheckHInfo(uint64 partialmd5) {
	/* ROM images that have the battery-backed bit set in the header that really
	don't have battery-backed RAM is not that big of a problem, so I'll
	treat this differently by only listing games that should have battery-backed RAM.

	Lower 64 bits of the MD5 hash.
	*/

	static uint64 savie[] =
	{
		0x6922d92ce10967a8ULL,	/* AD&D Heroes of the Lance (J) */
		0x4f46d1de7d0afba7ULL,	/* AD&D Heroes of the Lance (U) */
		0x8c30921b9328f409ULL,	/* AD&D Hillsfar (J) */
		0xb1c2df446373777bULL,	/* AD&D Hillsfar (U) */
		0x3ca38a30f1ec4110ULL,	/* AD&D Pool of Radiance (J) */
		0x859542620628afccULL,	/* AD&D Pool of Radiance (U) */
		0xf3cb6e1a6022e503ULL,	/* Battle Fleet */
		0xc8df134ce18a7d2fULL,	/* Downtown Special: Kunio-kun no Jidaigeki Dayo Zenin Shuugou! */
		0xe8382f82570bc616ULL,	/* DW 1 (PRG0) */
		0xe0413c76f69f5acaULL,	/* DW 1 (PRG1) */
		0xccdb4563c9004d86ULL,	/* DW 2 */
		0xb4b5416962ac800aULL,	/* DQ 3 (PRG0) */
		0x5a0c984006b088c2ULL,	/* DQ 3 (PRG1) */
		0x16a03048ce659d3dULL,	/* Dw 3 */
		0x80d5683ac9553740ULL,	/* DQ 4 (PRG0) */
		0x192f2997b7f5123eULL,	/* DQ 4 (PRG1) */
		0xd8a1d610c93b96adULL,	/* Dw 4 (U) */
		0xbf22d20bd7c5fc50ULL,	/* Famista '90 */
		0x4f17d28ef72c5e2fULL,	/* Faria (J) */
		0x7a01e068ab53e51bULL,	/* Faria (U) */
		0x97a660fb70152637ULL,	/* Feng Shen Bang */
		0x881ecc27f0d3b10eULL,	/* FF 1 (J) (PRG0) */
		0x4dd4c6f2ff32da4dULL,	/* FF 1 (J) (PRG1) */
		0x24ae5edf8375162fULL,	/* FF 1 (U) */
		0x4b3342b2c143dadeULL,	/* FF 1+2 */
		0x374ed97be8bfd628ULL,	/* FF 2 */
		0x45a7d02ed0dc9266ULL,	/* FF 3 */
		0x7155ba08492a466cULL,	/* Hydlide 3 */
		0x8a60538025e37dbcULL,	/* Jing Ke Xin Zhuan */
		0xc138b82de57f616cULL,	/* Justbreed */
		0xc064782f5729f32eULL,	/* Kaijuu Monogatari */
		0xb7843f2d0a4df4f5ULL,	/* Kyonshiizu 2 */
		0xd9a1631d5c32d355ULL,	/* Legend of Zelda (PRG0) */
		0xd3f453931146e95bULL,	/* Legend of Zelda (PRG1) */
		0xb8b6caf3795468cbULL,	/* Legend of Zelda (J) */
		0x7d98c75301dbcd72ULL,	/* Magical Taruruuto 1 (PRG0) */
		0x4d46756da82fec49ULL,	/* Magical Taruruuto 1 (PRG1) */
		0x88b3ab0e9aceb751ULL,	/* Magical Taruruuto 2 */
		0xf2ef7e357127bfb0ULL,	/* Mindseeker */
		0xc9329c7401ce9b8aULL,	/* Mirai Shinwa Jarvas */
		0x2d9bc331e80cb239ULL,	/* Mouryou Senki Madara */
		0x2391e4e11a70eed4ULL,	/* Ninjara Hoi! */
		0x74a2aecb64165e71ULL,	/* RPG Jinsei Game */
		0xb1eddbb71994db07ULL,	/* Startropics */
		0x54a19f2911aa51ddULL,	/* Startropics 2 */
		0xb8306e8dad7d0299ULL,	/* Taito Grand Prix */
		0x41da606caba53bdeULL,	/* Ys 1 */
		0xe5bee2f0beebd32bULL,	/* Ys 2 */
		0x3f24ad48065c8c62ULL,	/* Ys 3 */
		0x88c0493fb1146834ULL,	/* Zelda 2 */
		0						/* Abandon all hope if the game has 0 in the lower 64-bits of its MD5 hash */
	};

	static struct CHINF moo[] =
	{
		#include "ines-correct.h"
	};
	int32 tofix = 0, x, mask;

	MasterRomInfo = NULL;
	for (size_t i = 0; i < ARRAY_SIZE(sMasterRomInfo); i++) {
		const TMasterRomInfo& info = sMasterRomInfo[i];
		if (info.md5lower != partialmd5)
			continue;

		MasterRomInfo = &info;
		if (!info.params) break;

		std::vector<std::string> toks = tokenize_str(info.params, ",");
		for (size_t j = 0; j < toks.size(); j++) {
			std::vector<std::string> parts = tokenize_str(toks[j], "=");
			MasterRomInfoParams[parts[0]] = parts[1];
		}
		break;
	}

	x = 0;
	do {
		if (moo[x].crc32 == iNESGameCRC32) {
			if (moo[x].mapper >= 0) {
				if (moo[x].mapper & 0x800 && VROM_size) {
					VROM_size = 0;
					free(VROM);
					VROM = NULL;
					tofix |= 8;
				}
				if (moo[x].mapper & 0x1000)
					mask = 0xFFF;
				else
					mask = 0xFF;
				if (MapperNo != (moo[x].mapper & mask)) {
					tofix |= 1;
					MapperNo = moo[x].mapper & mask;
				}
			}
			if (moo[x].mirror >= 0) {
				if (moo[x].mirror == 8) {
					if (Mirroring == 2) {	/* Anything but hard-wired(four screen). */
						tofix |= 2;
						Mirroring = 0;
					}
				} else if (Mirroring != moo[x].mirror) {
					if (Mirroring != (moo[x].mirror & ~4))
						if ((moo[x].mirror & ~4) <= 2)	/* Don't complain if one-screen mirroring
														needs to be set(the iNES header can't
														hold this information).
														*/
							tofix |= 2;
					Mirroring = moo[x].mirror;
				}
			}
			break;
		}
		x++;
	} while (moo[x].mirror >= 0 || moo[x].mapper >= 0);

	x = 0;
	while (savie[x] != 0) {
		if (savie[x] == partialmd5) {
			if (!(head.ROM_type & 2)) {
				tofix |= 4;
				head.ROM_type |= 2;
			}
		}
		x++;
	}

	/* Games that use these iNES mappers tend to have the four-screen bit set
	when it should not be.
	*/
	if ((MapperNo == 118 || MapperNo == 24 || MapperNo == 26) && (Mirroring == 2)) {
		Mirroring = 0;
		tofix |= 2;
	}

	/* Four-screen mirroring implicitly set. */
	if (MapperNo == 99)
		Mirroring = 2;

	if (tofix) {
		char gigastr[768];
		strcpy(gigastr, "The iNES header contains incorrect information.  For now, the information will be corrected in RAM.  ");
		if (tofix & 1)
			sprintf(gigastr + strlen(gigastr), "The mapper number should be set to %d.  ", MapperNo);
		if (tofix & 2) {
			const char *mstr[3] = { "Horizontal", "Vertical", "Four-screen" };
			sprintf(gigastr + strlen(gigastr), "Mirroring should be set to \"%s\".  ", mstr[Mirroring & 3]);
		}
		if (tofix & 4)
			strcat(gigastr, "The battery-backed bit should be set.  ");
		if (tofix & 8)
			strcat(gigastr, "This game should not have any CHR ROM.  ");
		strcat(gigastr, "\n");
		FCEU_printf("%s", gigastr);
	}
}

typedef struct {
	int32 mapper;
	void (*init)(CartInfo *);
} NewMI;

//this is for games that is not the a power of 2
//mapper based for now...
//not really accurate but this works since games
//that are not in the power of 2 tends to come
//in obscure mappers themselves which supports such
//size
//Cah4e3 25.10.19: iNES 2.0 attempts to cover all
// boards including UNIF boards with non power 2 
// total rom size (a lot of them with a couple of 
// roms different sizes (may vary a lot)
// so we need either add here ALL ines 2.0 mappers 
// with not power2 roms or change logic here
// to something more unified for ines 2.0 specific
static int not_power2[] =
{
	53, 198, 228, 547
};

BMAPPINGLocal bmap[] = {
	{"NROM",				  0, NROM_Init},
	{"MMC1",				  1, Mapper1_Init},
	{"UNROM",				  2, UNROM_Init},
	{"CNROM",				  3, CNROM_Init},
	{"MMC3",				  4, Mapper4_Init},
	{"MMC5",				  5, Mapper5_Init},
	{"FFE Rev. A",			  6, FFE_Init},
	{"ANROM",				  7, ANROM_Init},
	{"",					  8, FFE_Init},		
	{"MMC2",				  9, Mapper9_Init},
	{"MMC4",				 10, Mapper10_Init},
	{"Color Dreams",		 11, Mapper11_Init},
	{"REX DBZ 5",			 12, Mapper12_Init},
	{"CPROM",				 13, CPROM_Init},
	{"REX SL-1632",			 14, UNLSL1632_Init},
	{"100-in-1",			 15, Mapper15_Init},
	{"BANDAI 24C02",		 16, Mapper16_Init},
	{"FFE Rev. B",			 17, FFE_Init},
	{"JALECO SS880006",		 18, Mapper18_Init},	// JF-NNX (EB89018-30007) boards
	{"Namcot 106",			 19, Mapper19_Init},
//	{"",					 20, Mapper20_Init},
	{"Konami VRC2/VRC4 A",	 21, Mapper21_Init},
	{"Konami VRC2/VRC4 B",	 22, Mapper22_Init},
	{"Konami VRC2/VRC4 C",	 23, Mapper23_Init},
	{"Konami VRC6 Rev. A",	 24, Mapper24_Init},
	{"Konami VRC2/VRC4 D",	 25, Mapper25_Init},
	{"Konami VRC6 Rev. B",	 26, Mapper26_Init},
	{"CC-21 MI HUN CHE",	 27, UNLCC21_Init},		// Former dupe for VRC2/VRC4 mapper, redefined with crc to mihunche boards
	{"",					 28, Mapper28_Init},
	{"RET-CUFROM",			 29, Mapper29_Init},
	{"UNROM 512",			 30, UNROM512_Init},
	{"infiniteneslives-NSF", 31, Mapper31_Init},
	{"IREM G-101",			 32, Mapper32_Init},
	{"TC0190FMC/TC0350FMR",	 33, Mapper33_Init},
	{"IREM I-IM/BNROM",		 34, Mapper34_Init},
	{"Wario Land 2",		 35, UNLSC127_Init},
	{"TXC Policeman",		 36, Mapper36_Init},
	{"PAL-ZZ SMB/TETRIS/NWC",37, Mapper37_Init},
	{"Bit Corp.",			 38, Mapper38_Init},	// Crime Busters
//	{"",					 39, Mapper39_Init},
	{"SMB2j FDS",			 40, Mapper40_Init},
	{"CALTRON 6-in-1",		 41, Mapper41_Init},
	{"BIO MIRACLE FDS",		 42, Mapper42_Init},
	{"FDS SMB2j LF36",		 43, Mapper43_Init},
	{"MMC3 BMC PIRATE A",	 44, Mapper44_Init},
	{"MMC3 BMC PIRATE B",	 45, Mapper45_Init},
	{"RUMBLESTATION 15-in-1",46, Mapper46_Init},
	{"NES-QJ SSVB/NWC",		 47, Mapper47_Init},
	{"TAITO TCxxx",			 48, Mapper48_Init},
	{"MMC3 BMC PIRATE C",	 49, Mapper49_Init},
	{"SMB2j FDS Rev. A",	 50, Mapper50_Init},
	{"11-in-1 BALL SERIES",	 51, Mapper51_Init},	// 1993 year version
	{"MMC3 BMC PIRATE D",	 52, Mapper52_Init},
	{"SUPERVISION 16-in-1",	 53, Supervision16_Init},
//	{"",					 54, Mapper54_Init},
//	{"",					 55, Mapper55_Init},
//	{"",					 56, Mapper56_Init},
	{"SIMBPLE BMC PIRATE A", 57, Mapper57_Init},
	{"SIMBPLE BMC PIRATE B", 58, BMCGK192_Init},
	{"SIMBPLE BMC PIRATE C", 59, BMCD1038_Init},	// Check this out
	{"Reset-based NROM-128", 60, Mapper60_Init},
	{"20-in-1 KAISER Rev. A",61, Mapper61_Init},
	{"700-in-1",			 62, Mapper62_Init},
	{ "NTDEC 2291",			 63, NTDEC2291_Init },
	{"TENGEN RAMBO1",		 64, Mapper64_Init},
	{"IREM-H3001",			 65, Mapper65_Init},
	{"MHROM",				 66, MHROM_Init},
	{"SUNSOFT-FZII",		 67, Mapper67_Init},
	{"Sunsoft Mapper #4",	 68, Mapper68_Init},
	{"SUNSOFT-5/FME-7",		 69, Mapper69_Init},
	{"BA KAMEN DISCRETE",	 70, Mapper70_Init},
	{"CAMERICA BF9093",		 71, Mapper71_Init},
	{"JALECO JF-17",		 72, Mapper72_Init},
	{"KONAMI VRC3",			 73, Mapper73_Init},
	{"TW MMC3+VRAM Rev. A",	 74, Mapper74_Init},
	{"KONAMI VRC1",			 75, Mapper75_Init},
	{"NAMCOT 108 Rev. A",	 76, Mapper76_Init},
	{"IREM LROG017",		 77, Mapper77_Init},
	{"Irem 74HC161/32",		 78, Mapper78_Init},
	{"AVE/C&E/TXC BOARD",	 79, Mapper79_Init},
	{"TAITO X1-005 Rev. A",	 80, Mapper80_Init},
	{"Super Gun (NTDEC N715021)", 81, Mapper81_Init},
	{"TAITO X1-017",		 82, Mapper82_Init},
	{"YOKO VRC Rev. B",		 83, Mapper83_Init},
//	{"",					 84, Mapper84_Init},
	{"KONAMI VRC7",			 85, Mapper85_Init},
	{"JALECO JF-13",		 86, Mapper86_Init},
	{"74*139/74 DISCRETE",	 87, Mapper87_Init},
	{"NAMCO 3433",			 88, Mapper88_Init},
	{"SUNSOFT-3",			 89, Mapper89_Init},	// SUNSOFT-2 mapper
	{"HUMMER/JY BOARD",		 90, Mapper90_Init},
	{"EARLY HUMMER/JY BOARD",91, Mapper91_Init},
	{"JALECO JF-19",		 92, Mapper92_Init},
	{"SUNSOFT-3R",			 93, SUNSOFT_UNROM_Init},// SUNSOFT-2 mapper with VRAM, different wiring
	{"HVC-UN1ROM",			 94, Mapper94_Init},
	{"NAMCOT 108 Rev. B",	 95, Mapper95_Init},
	{"BANDAI OEKAKIDS",		 96, Mapper96_Init},
	{"IREM TAM-S1",			 97, Mapper97_Init},
//	{"",					 98, Mapper98_Init},
	{"VS Uni/Dual- system",	 99, Mapper99_Init},
//	{"",					100, Mapper100_Init},
	{"",					101, Mapper101_Init},
//	{"",					102, Mapper102_Init},
	{"FDS DOKIDOKI FULL",	103, Mapper103_Init},
//	{"",					104, Mapper104_Init},
	{"NES-EVENT NWC1990",	105, Mapper105_Init},
	{"SMB3 PIRATE A",		106, Mapper106_Init},
	{"MAGIC CORP A",		107, Mapper107_Init},
	{"FDS UNROM BOARD",		108, Mapper108_Init},
//	{"",					109, Mapper109_Init},
//	{"",					110, Mapper110_Init},
	{"Cheapocabra",			111, Mapper111_Init},
	{"ASDER/NTDEC BOARD",	112, Mapper112_Init},
	{"HACKER/SACHEN BOARD",	113, Mapper113_Init},
	{"MMC3 SG PROT. A",		114, Mapper114_Init},
	{"MMC3 PIRATE A",		115, Mapper115_Init},
	{"MMC1/MMC3/VRC PIRATE",116, UNLSL12_Init},
	{"FUTURE MEDIA BOARD",	117, Mapper117_Init},
	{"TSKROM",				118, TKSROM_Init},
	{"NES-TQROM",			119, Mapper119_Init},
	{"FDS TOBIDASE",		120, Mapper120_Init},
	{"MMC3 PIRATE PROT. A",	121, Mapper121_Init},
	{"JY043",				122, Mapper122_Init},
	{"MMC3 PIRATE H2288",	123, UNLH2288_Init},
//	{"",					124, Mapper124_Init},
	{"FDS LH32",			125, LH32_Init},
	{ "PowerJoy 84-in-1 PJ-008",					126, Mapper126_Init },
//	{"",					127, Mapper127_Init},
	{"1994 Super HiK 4-in-1",					128, Mapper128_Init},
//	{"",					129, Mapper129_Init},
//	{"",					130, Mapper130_Init},
//	{"",					131, Mapper131_Init},
	{"TXC/MGENIUS 22111",	132, UNL22211_Init},
	{"SA72008",				133, SA72008_Init},
	{ "T4A54A/BS-5652/WX-KB4K", 134, Mapper134_Init },
//	{"",					135, Mapper135_Init},
	{"TCU02",				136, TCU02_Init},
	{"S8259D",				137, S8259D_Init},
	{"S8259B",				138, S8259B_Init},
	{"S8259C",				139, S8259C_Init},
	{"JALECO JF-11/14",		140, Mapper140_Init},
	{"S8259A",				141, S8259A_Init},
	{"UNLKS7032",			142, UNLKS7032_Init},
	{"TCA01",				143, TCA01_Init},
	{"AGCI 50282",			144, Mapper144_Init},
	{"SA72007",				145, SA72007_Init},
	{"SA0161M",				146, SA0161M_Init},
	{"TCU01",				147, TCU01_Init},
	{"SA0037",				148, SA0037_Init},
	{"SA0036",				149, SA0036_Init},
	{"S74LS374N",			150, S74LS374N_Init},
	{"",					151, Mapper151_Init},
	{"",					152, Mapper152_Init},
	{"BANDAI SRAM",			153, Mapper153_Init},	// Bandai board 16 with SRAM instead of EEPROM
	{"",					154, Mapper154_Init},
	{"",					155, Mapper155_Init},
	{"",					156, Mapper156_Init},
	{"BANDAI BARCODE",		157, Mapper157_Init},
//	{"",					158, Mapper158_Init},
	{"BANDAI 24C01",		159, Mapper159_Init},	// Different type of EEPROM on the  bandai board
	{"SA009",				160, SA009_Init},
//	{"",					161, Mapper161_Init},
	{"",					162, UNLFS304_Init},
	{"",					163, Mapper163_Init},
	{"",					164, Mapper164_Init},
	{"",					165, Mapper165_Init},
	{"SUBOR Rev. A",		166, Mapper166_Init},
	{"SUBOR Rev. B",		167, Mapper167_Init},
	{"",					168, Mapper168_Init},
//	{"",					169, Mapper169_Init},
	{"",					170, Mapper170_Init},
	//{"",					171, Mapper171_Init},
	{"",					172, Mapper172_Init},
	{"",					173, Mapper173_Init},
	{"NTDec 5-in-1",		174, Mapper174_Init},
	{"",					175, Mapper175_Init},
	{"BMCFK23C",			176, Mapper176_Init},	// zero 26-may-2012 - well, i have some WXN junk games that use 176 for instance ????. i dont know what game uses this BMCFK23C as mapper 176. we'll have to make a note when we find it.
	{"",					177, Mapper177_Init},
	{"FS305/NJ0430",		178, Mapper178_Init},
//	{"",					179, Mapper179_Init},
	{"",					180, Mapper180_Init},
	{"",					181, Mapper181_Init},
	{"YH-001",				182, Mapper182_Init},	// Deprecated, dupe
	{"",					183, Mapper183_Init},
	{"",					184, Mapper184_Init},
	{"",					185, Mapper185_Init},
	{"",					186, Mapper186_Init},
	{"",					187, Mapper187_Init},
	{"",					188, Mapper188_Init},
	{"",					189, Mapper189_Init},
	{"",					190, Mapper190_Init},
	{"",					191, Mapper191_Init},
	{"WaiXing FS308",		192, Mapper192_Init},
	{"NTDEC TC-112",		193, Mapper193_Init},	// War in the Gulf
	{"TW MMC3+VRAM Rev. C",	194, Mapper194_Init},
	{"WaiXing FS303",		195, Mapper195_Init},
	{"",					196, Mapper196_Init},
	{"",					197, Mapper197_Init},
	{"TW MMC3+VRAM Rev. E",	198, Mapper198_Init},
	{"WaiXing FS309",		199, Mapper199_Init},
	{"36-in-1",				200, Mapper200_Init},
	{"",					201, Mapper201_Init},
	{"SP60 150-in-1",		202, Mapper202_Init},
	{"35-in-1",				203, Mapper203_Init},
	{"",					204, Mapper204_Init},
	{"BMC 15/3-IN-1/4-IN-1",205, Mapper205_Init},
	{"Nintendo DE(1)ROM",	206, Mapper206_Init},	// Deprecated, Used to be "DEIROM" whatever it means, but actually simple version of MMC3
	{"TAITO X1-005 Rev. B",	207, Mapper207_Init},
	{"",					208, Mapper208_Init},
	{"HUMMER/JY BOARD",		209, Mapper209_Init},
	{"",					210, Mapper210_Init},
	{"HUMMER/JY BOARD",		211, Mapper211_Init},
	{"",					212, Mapper212_Init},
	{"EJ-3003/820428-C",	213, Mapper213_Init},
	{"",					214, Mapper214_Init},
	{"UNL-8237",			215, UNL8237_Init},
	{"",					216, Mapper216_Init},
	{"GI 9549, ET-450",		217, Mapper217_Init},	// Redefined to a new Discrete BMC mapper
	{"Magic Floor",			218, Mapper218_Init},
	{"UNLA9746",			219, UNLA9746_Init},
	{"Debug Mapper",		220, QTAi_Init},
	{"UNLN625092",			221, UNLN625092_Init},
	{"",					222, Mapper222_Init},
//	{"",					223, Mapper223_Init},
	{"¾§¿ÆÌ© KT-008",		224, AA6023_Init   },
	{"",					225, Mapper225_Init},
	{"BMC 22+20-in-1",		226, Mapper226_Init},
	{"ÍâÐÇ FW01",			227, Mapper227_Init},
	{"Action 52",			228, Mapper228_Init},
	{"SC 0892",				229, Mapper229_Init},
	{"BMC Contra+22-in-1",	230, Mapper230_Init},
	{"",					231, Mapper231_Init},
	{"BMC QUATTRO",			232, Mapper232_Init},
	{"BMC 22+20-in-1 RST",	233, Mapper233_Init},
	{"BMC MAXI",			234, Mapper234_Init},
	{"",					235, Mapper235_Init},
	{ "Realtec 8155",		236, Mapper236_Init},
	{"Teletubbies 420-in-1",237, Mapper237_Init},
	{"UNL6035052",			238, UNL6035052_Init},
	{"OK-043",				239, Mapper239_Init},
	{"",					240, Mapper240_Init},
	{"",					241, Mapper241_Init},
	{"1200-in-1",			242, Mapper242_Init},
	{"S74LS374NA",			243, S74LS374NA_Init},
	{"DECATHLON",			244, Mapper244_Init},
	{"",					245, Mapper245_Init},
	{"FONG SHEN BANG",		246, Mapper246_Init},
//	{"",					247, Mapper247_Init},
//	{"",					248, Mapper248_Init},
	{"",					249, Mapper249_Init},
	{"",					250, Mapper250_Init},
//	{"",					251, Mapper251_Init},	// No good dumps for this mapper, use UNIF version
	{"SAN GUO ZHI PIRATE",	252, Mapper252_Init},
	{"DRAGON BALL PIRATE",	253, Mapper253_Init},
	{"",					254, Mapper254_Init},
	{"",					255, Mapper255_Init},	// dupe of 225

//-------- Mappers 256-511 is the Supplementary Multilingual Plane ----------
    {"ONE-BUS Systems",		256, UNLOneBus_Init},
	{ "PEC-586 Computer",	257, UNLPEC586Init },
	{ "158B Prot Board",		258, UNL158B_Init },
	{ "F-15 MMC3 Based",		259, BMCF15_Init },
	{ "HP10xx/H20xx Boards",	260, BMCHPxx_Init },
	{ "810544-C-A1/NTDEC 2746",	261, BMC810544CA1_Init },
	{ "SHERO", 262, UNLSHeroes_Init },
	{ "KOF97", 263, UNLKOF97_Init },
	{ "YOKO", 264, UNLYOKO_Init },
	{ "T-262", 265, BMCT262_Init },
	{ "CITYFIGHT", 266, UNLCITYFIGHT_Init },
	{ "8-in-1 JY-119",  267, Mapper267_Init },
	{ "AA6023/AA6023B",		268, AA6023_Init },
	{ "Games Xplosion 121-in-1", 269, Mapper269_Init },
	{ "MGC-026", 271, Mapper271_Init },
	{ "J-3?-C", 273, Mapper273_Init },
	{ "80013-B", 274, BMC80013B_Init },
	{ "09-078",	277, Mapper277_Init },
	{ "K-3017",	280, Mapper280_Init },
	{ "¾§Ì« YY860417C",	281, Mapper281_Init },
	{ "¾§Ì« 860224C",	282, Mapper282_Init },
	{ "GS-2004/GS-2013", 283, BMCGS2004_Init },
	{ "A65AS", 285, BMCA65AS_Init },
	{ "BS-5", 286, BMCBS5_Init },
	{ "811120-C/810849-C", 287, BMC411120C_Init },
	{ "GKCXIN1", 288, Mapper288_Init },
	{ "60311C", 289, BMC60311C_Init },
	{ "NTD-03", 290, BMCNTD03_Init },
	{ "Super 2-in-1", 291, Mapper291_Init },
	{ "DRAGONFIGHTER", 292, UNLBMW8544_Init },
	{ "NewStar 12-in-1/7-in-1", 293, Mapper293_Init },
	{ "63-1601 ",	294, Mapper294_Init },
	{ "¾§Ì« YY860216C",	295, Mapper295_Init },
	{ "TXC 01-22110-000",    297, Mapper297_Init },
	{ "TF1201", 298, UNLTF1201_Init },
	{ "11160", 299, BMC11160_Init },
	{ "190in1", 300, BMC190in1_Init },
	{ "8157", 301, UNL8157_Init },
	{ "KS7057", 302, UNLKS7057_Init },
	{ "KS7017", 303, UNLKS7017_Init },
	{ "SMB2J", 304, UNLSMB2J_Init },
	{ "KS7031", 305, UNLKS7031_Init },
	{ "KS7016", 306, UNLKS7016_Init },
	{ "KS7037", 307, UNLKS7037_Init },
	{ "TH2131-1", 308, UNLTH21311_Init },
	{ "LH51", 309, LH51_Init },
	{ "K-1053", 310, Mapper310_Init },
	{ "KS7013B", 312, UNLKS7013B_Init },
	{ "Reset-based TKROM multicart", 313, BMCRESETTXROM_Init },
	{ "64in1NoRepeat", 314, BMC64in1nr_Init },
	{ "830134C",	315, Mapper315_Init },
	{ "HP-898F",	319, Mapper319_Init },
	{ "BMC-830425C-4391T",	320, BMC830425C4391T_Init },
	{ "810849-C", 321, Mapper321_Init },
	{ "K-3033", 322, BMCK3033_Init },
	{ "FRAID SLROM 8-IN-1",	323, FARIDSLROM8IN1_Init },
	{ "FRAID UNROM 8-IN-1",	324, FRAID_UNROM_Init },
	{ "MALISB", 325, UNLMaliSB_Init },
	{ "Contra/Gryzor", 326, Mapper326_Init },
	{ "10-24-C-A1", 327, BMC1024CA1_Init },
	{ "RT-01", 328, UNLRT01_Init },
	{ "EDU2000", 329, UNLEDU2000_Init },
	{ "Sangokushi II: Ha¨­ no Tairiku", 330, Mapper330_Init },
	{ "12-IN-1", 331, BMC12IN1_Init },
	{ "BMC-WS/WS-1001",	332, BMCWS_Init },
	{ "NEWSTAR-GRM070-8IN1", 333, BMC8IN1_Init },
	{ "821202C", 334, Mapper334_Init },
	{ "CTC-09", 335, BMCCTC09_Init },
	{ "K-3046",	336, BMCK3046_Init },
	{ "CTC-12IN1", 337, BMCCTC12IN1_Init },
	{ "SA005-A", 338, BMCSA005A_Init },
	{ "K-3006",	339, BMCK3006_Init },
	{ "K-3036", 340, BMCK3036_Init },
	{ "TJ-03", 341, BMCTJ03_Init },
	{ "COOLGIRL", 342, COOLGIRL_Init },
	{ "21-in-1", 343, Mapper343_Init },
	{ "BMC-GN-26",	344, BMCGN26_Init },
	{ "L6IN1",	345, BMCL6IN1_Init },
	{ "KS7012", 346, UNLKS7012_Init },
	{ "KS7030", 347, UNLKS7030_Init },
	{ "830118C", 348, BMC830118C_Init },
	{ "G-146",  349, BMCG146_Init },
	{ "891227", 350, BMC891227_Init },
	{ "9-in-1 MMC3 multicart", 351, Mapper351_Init },
	{ "Reset-based NROM-256", 352, Mapper352_Init },
	{ "Super Mario Family", 353, Mapper353_Init },
	{ "FAM250/81-01-39-C/SCHI-24",			354, Mapper354_Init },
	{ "3D-BLOCK", 355, UNL3DBlock_Init },
	{ "7-in-1 Rockman (JY-208)", 356, Mapper356_Init },
	{ "Bit Corp 4-in-1", 357, Mapper357_Init },
	{ "¾§Ì« YY860606C",	358, Mapper358_Init },
	{ "SB-5013/GCL8050/841242C", 359, Mapper359_Init },
	{ "Bitcorp 31-in-1",         360, Mapper360_Init },
	{ "OK-411",	361, Mapper361_Init },
	{ "830506C",	362, Mapper362_Init },
	{ "5069", 363, Mapper363_Init },
	{ "¾§Ì« JY830832C",	364, Mapper364_Init },
	{ "GN-45", 366, Mapper366_Init },
	{ "JC-016-2 variant", 367, Mapper367_Init },
	{ "Yung-08", 368, Mapper368_Init },
	{ "N49C-300", 369, Mapper369_Init },
	{ "Golden Mario Party II - Around the World 6-in-1", 370, Mapper370_Init },
	{ "MMC3 PIRATE SFC-12", 372, Mapper372_Init },
	{ "MMC3 PIRATE SFC-13", 373, Mapper373_Init },
	{ "Reset-based SLROM multicart",	374, Mapper374_Init },
	{ "135-in-1", 375, Mapper375_Init },
	{ "¾§Ì« YY841155C", 376, Mapper376_Init },
	{ "JY-111/JY-112", 377, Mapper377_Init },
	{ "8-in-1 AOROM+UNROM", 378, Mapper378_Init },
	{ "35-in-1", 379, Mapper379_Init },
	{ "42 to 80,000 (970630C)",	380, Mapper380_Init },
	{ "KN-42",  381, Mapper381_Init },
	{ "830928C",	382, Mapper382_Init },
	{ "¾§Ì« YY840708C",	383, Mapper383_Init },
	{ "L1A16",	384, Mapper384_Init },
	{ "NTDEC 2779",	385, Mapper385_Init },
	{ "¾§Ì« YY860729C",	386, Mapper386_Init },
	{ "¾§Ì« YY850735C",	387, Mapper387_Init },
	{ "¾§Ì« YY850835C",	388, Mapper388_Init },
	{ "Caltron 9-in-1",	389, Mapper389_Init },
	{ "Realtec 8031", 390, Mapper390_Init },
	{ "NC7000M", 391, NC7000M_Init },
	{ "00202650", 392, Mapper392_Init },
	{ "820720C", 393, Mapper393_Init },
	{ "HSK007", 394, Mapper394_Init },
	{ "Realtec 8210",	395, Mapper395_Init },
	{ "¾§Ì« YY850437C",	396, Mapper396_Init },
	{ "¾§Ì« YY850439C",	397, Mapper397_Init },
	{ "¾§Ì« YY840820C",	398, Mapper398_Init },
	{ "BATMAP-000",	399, Mapper399_Init },
	{ "8BIT-XMAS", 400, Mapper400_Init },
	{ "BMC Super 19-in-1 (VIP19)", 401, Mapper401_Init },
	{ "831019C J-2282", 402, J2282_Init },
	{ "89433", 403, Mapper403_Init },
	{ "¾§Ì« JY012005",      404, Mapper404_Init },
	{ "Impact Soft",	406, Mapper406_Init },
	{ "retroUSB DPCMcart", 409, Mapper409_Init },
	{ "JY-302",  410, Mapper410_Init },
	{ "A88S-1",	411, Mapper411_Init },
	{ "ºã¸ñ FK-206 JG",	412, Mapper412_Init },
	{ "BATMAP-SRR-X",	413, Mapper413_Init },
	{ "9999999-in-1",	414, Mapper414_Init },
	{ "0353",	415, Mapper415_Init },
	{ "4-in-1/N-32", 416, Mapper416_Init },
	{ "", 417, Mapper417_Init },
	{ "820106-C/821007C", 418, Mapper418_Init },
	{ "A971210",	420, Mapper420_Init },
	{ "¾§Ì« SC871115C",	421, Mapper421_Init },
	{ "BS-400R/BS-4040",	422, Mapper422_Init },
	{ "BB-002A/TF2740",	428, Mapper428_Init },
	{ "LIKO BBG-235-8-1B",	429, Mapper429_Init },
	{ "831031C/T-308",	430, Mapper430_Init },
	{ "Realtec GN-91B",	431, Mapper431_Init },
	{ "Realtec 8090",  432, Mapper432_Init },
	{ "Realtec NC-20MB",	433, Mapper433_Init },
	{ "BMC-S-2009/S-009",	434, Mapper434_Init },
	{ "F-1002",	435, Mapper435_Init },
	{ "820401/T-217",	436, Mapper436_Init },
	{ "NTDEC TH2348",   437, Mapper437_Init },
	{ "K-3010",	438, Mapper438_Init },
	{ "YS2309",	439, Mapper439_Init },
	{ "850335C",   441, Mapper441_Init },
	{ "NC-3000M",   443, Mapper443_Init },
	{ "NC7000M",	444, Mapper444_Init },
	{ "DG574B",	445, Mapper445_Init },
	{ "SMD172B_FPGA",	446, Mapper446_Init },
	{ "KL-06",	447, Mapper447_Init },
	{ "830768C", 448, Mapper448_Init },
	{ "22-in-1 King Series",	449, Mapper449_Init },
	{ "¾§Ì« YY841157C",         450, Mapper450_Init },
	{ "Haratyler HP/MP",		451, Mapper451_Init },
	{ "DS-9-27",	452, Mapper452_Init },
	{ "Realtec 8042",	453, Mapper453_Init },
	{ "110-in-1",	454, Mapper454_Init },
	{ "N625836", 455, Mapper455_Init },
	{ "K6C3001A",   456, Mapper456_Init },
	{ "810431C",   457, Mapper457_Init },
	{ "GN-23C",   458, Mapper458_Init },
	{ "8-in-1",   459, Mapper459_Init },
	{ "FC-29-40/K-3101",   460, Mapper460_Init },
	{ "CM-9309",   461, Mapper461_Init },
	{ "971107-00G",   462, BMC_971107_00G_Init },
	{ "YH810X1",   463, Mapper463_Init },
	{ "NTDEC 9012",   464, Mapper464_Init },
	{ "ET-120",   465, Mapper465_Init },
	{ "Keybyte Computer",   466, Mapper466_Init },
	{ "47-2",   467, Mapper467_Init },
	{ "BlazePro CPLD",      468, Mapper468_Init },
	{ "INX_007T_V01",		470, INX_007T_Init },
	{ "Impact Soft IM1",		471, Mapper471_Init },
	{ "830947",		472, Mapper472_Init },
	{ "KJ01A-18",	473, Mapper473_Init },
	{ "NTDEC N625231",		474, Mapper474_Init },
	{ "820215-C-A2",		475, Mapper475_Init },
	{ "Croaky Karaoke",		476, Mapper476_Init },
	{ "15-in-1",		477, Mapper477_Init },
	{ "WE7HGX",		478, Mapper478_Init },
	{ "480",		480, Mapper480_Init },
	{ "K-1079",		482, Mapper482_Init },
	{ "045N",		481, Mapper481_Init },
	{ "3927",       483, Mapper483_Init },
	{ "ESTIQUE",	484, Mapper484_Init },
	{ "0359",		485, Mapper485_Init },
	{ "KS7009",		486, Mapper486_Init },
	{ "AVE NINA-08", 487, Mapper487_Init },
	{ "HC001", 488, Mapper488_Init },
	{ "N-80", 489, Mapper489_Init },
	{ "K-3101", 490, Mapper490_Init },
	{ "Sane Ting 5-in-1", 491, Mapper491_Init },
	{ "K-3069/12-28",  492,  Mapper492_Init },
	{ "AVE-NTDEC 30-in-1",  493,  Mapper493_Init },
	{ "CH512K",  494,  Mapper494_Init },
	{ "N-46", 495, Mapper495_Init },
	{ "K-3011",  498, Mapper498_Init },
	{ "FC-41", 499, Mapper499_Init },
	{ "Yhc Unrom",	500, Mapper500_Init },
	{ "Yhc Axrom",	501, Mapper501_Init },
	{ "Yhc A/B/Uxrom",	502, Mapper502_Init },
	{ "ET-170", 503, Mapper503_Init },
	{ "K-3054", 504, Mapper504_Init },
	{ "5426757A-Y2-230630", 505, Mapper505_Init },
	{ "GA-009", 506, Mapper506_Init },
	{ "A-018",  507, Mapper507_Init },
	{ "JY-014", 508, Mapper508_Init },
	{ "K-3022", 509, Mapper509_Init },
	{ "FC-53A", 510, Mapper510_Init },
	{ "1n4148", 511, Mapper511_Init },
//-------- Mappers 512-767 is the Supplementary Ideographic Plane -----------
//-------- Mappers 3840-4095 are for rom dumps not publicly released --------

//	An attempt to make working the UNIF BOARD ROMs in INES FORMAT
//  I don't know if there a complete ines 2.0 mapper list exist, so if it does,
//  just redefine these numbers to any others which isn't used before
//  see the ines-correct.h files for the ROMs CHR list

	
	{ "Zhonggguo Daheng", 512, Mapper512_Init },
	{ "SA-9602B", 513, SA9602B_Init },
	{ "Subor Karaoke", 514, Mapper514_Init },
	{ "Brilliant Com Cocoma Pack", 516, Mapper516_Init },
	{ "Kkachi-wa Nolae Chingu", 517, UNROM_Init },
	{ "DANCE2000", 518, UNLD2000_Init },
	{ "EH8813A",	519, UNLEH8813A_Init },
	{ "YuYuHakusho+DBZ", 520, Mapper520_Init },
	{ "DREAMTECH01", 521, DreamTech01_Init },
	{ "LH10", 522, LH10_Init },
	{ "Jncota KT-???", 523, Mapper523_Init },
	{ "900218", 524, BTL900218_Init },
	{ "KS7021A", 525, UNLKS7021A_Init},
	{ "BJ-56", 526, UNLBJ56_Init },
	{ "AX-40G", 527, UNLAX40G_Init },
	{ "831128C", 528, Mapper528_Init },
	{ "T-230", 529, UNLT230_Init },
	{ "AX5705", 530, UNLAX5705_Init },
	{ "CHINA_ER_SAN2", 532, Mapper19_Init },
	{ "Sachen 3014",  533, Mapper533_Init },
	{ "NJ064",	534, Mapper534_Init },
	{ "LH53", 535, LH53_Init },
	{ "N42S-2", 536, Mapper536_Init },
	{ "JY4M4", 537, Mapper537_Init },
	{ "60-1064-16L (FDS)", 538, Mapper538_Init },
	{ "Kid Ikarus (FDS)", 539, Mapper539_Init },
	{ "82112C",   540, Mapper540_Init },
	{ "LittleCom 160",	541, Mapper541_Init },
	{ "JYV610 830626C", 542, Mapper542_Init },
	{ "5-in-1 (CH-501)",	543, Mapper543_Init },
	{ "WAIXING FS306",  544, Mapper544_Init },
	{ "ST-80",	545, Mapper545_Init },
	{ "03-101",  546, Mapper546_Init },
	{ "KONAMI QTAi Board",	547, QTAi_Init },
	{ "CTC-15", 548, Mapper548_Init },
	{ "Kaiser KS-7016B", 549, Mapper549_Init },
	{ "¾§Ì« JY820845C",	550, Mapper550_Init },
	{ "¾§¿ÆÌ© KT-xxx",	551, Mapper178_Init },
	{ "TAITO X1-017", 552, Mapper552_Init },
	{ "SACHEN 3013",    553, Mapper553_Init },
	{ "KS-7010", 554, Mapper554_Init },
	{ "KS-7010", 555, Mapper555_Init },
	{ "JY-215", 556, Mapper556_Init },
	{ "NTDEC 2718", 557, Mapper557_Init },
	{ "YC-03-09", 558, Mapper558_Init },
	{ "Subor Sango II", 559, Mapper559_Init },
	{ "Bung Super Game Doctor",   561, Mapper561_562_Init },
	{ "Venus Turbo Game Doctor",   562, Mapper561_562_Init },
	{ "J-2020", 563, Mapper563_Init },
	{ "bd23.pcb", 564, Mapper564_Init },
	{ "J-33-C", 565, Mapper565_Init },
	{ "ET-149", 566, Mapper566_Init },
	{ "Top Ten Variety (SF III)", 567, Mapper567_Init },
	{ "T-227", 568, Mapper568_Init },
	{ "820315-C", 569, Mapper569_Init },
	{ "9052", 570, Mapper570_Init },
	{ "JC-011", 571, Mapper571_Init },
	{ "F-648", 572, Mapper572_Init },
	{ "5068", 573, Mapper573_Init },
	{ "FC-40", 574, Mapper574_Init },
	{ "W-03", 575, Mapper575_Init },
	{ "J-2096", 576, Mapper576_Init },
	{ "KN-29", 577, Mapper577_Init },
	{ "910610", 578, Mapper578_Init },
	{ "T-215", 579, Mapper579_Init },
	{ "ET-156", 580, Mapper580_Init },
	{ "ET-82", 581, Mapper581_Init },
	{ "A9778", 582, Mapper582_Init },
	{ "8203",  583, Mapper583_Init },
	{ "ST-32", 584, Mapper584_Init },
	{ "FE-01-1", 585, Mapper585_Init },
	{ "HN-02", 586, Mapper586_Init },
	{ "3355",  587, Mapper587_Init },
	{ "ET-81", 588, Mapper588_Init },
	{ "810706", 589, Mapper589_Init },
	{ "810430", 590, Mapper590_Init },
	{ "07027/810543", 591, Mapper591_Init },
	{ "8 in 1 1991", 592, Mapper592_Init },
	{ "Rinco FSG2", 594, Mapper594_Init },
	{ "4MROM-512", 595, Mapper595_Init },
	{ "FC-49", 596, Mapper596_Init },
	{ "GN-27", 597, Mapper597_Init },
	{ "3936", 598, Mapper598_Init },
	{ "ET-133A", 599, Mapper599_Init },
	{ "J-2061", 603, Mapper603_Init },
	{ "New Star TX5/8IN1", 605, Mapper605_Init },
	{ "New Star T4IN1", 606, Mapper606_Init },
	{ "4705", 607, Mapper607_Init },
	{ "A-23", 608, Mapper608_Init },
	{ "63-100", 609, Mapper609_Init },
	{ "J-2042", 610, Mapper610_Init },
	{ "T-124/43-117/831049", 611, Mapper611_Init },
	{ "K-3004", 612, Mapper612_Init },
	{ "S5668 3366", 613, Mapper613_Init },
	{ "New Star 9135", 614, Mapper614_Init },
	{ "LB12in1", 615, Mapper615_Init },
	{ "K-3044", 616, Mapper616_Init },
	{ "AD-301", 617, Mapper617_Init },
	{ "FC 4-in-1 (NS32)", 618, Mapper618_Init },
	{ "ET-156",  731,  Mapper731_Init },
	{ "W-03",  732,  Mapper732_Init },
	{ "K-3054",  733,  Mapper733_Init },
	{ "FE-01-1",  734,  Mapper734_Init },
	{ "5426757A-Y2-230630",  735,  Mapper735_Init },
	{ "GA-009",  736,  Mapper736_Init },
	{ "K-3011",  737, Mapper737_Init },
	{ "K-3091/GN-16",  738, Mapper738_Init },
	{ "HC004 LV103",  739,  Mapper739_Init },
	{ "N-80",  740,  Mapper740_Init },
	{ "HN-02",  743,  Mapper743_Init },
	{ "JY-014",  745,  Mapper745_Init },
	{ "ET-82",  746,  Mapper746_Init },
	{ "K-3022",  747,  Mapper747_Init },
	{ "K-3069",  748,  Mapper748_Init },
	{ "ST-32",  749,  Mapper749_Init },
	{ "FC-53A",  750,  Mapper750_Init },
	{ "T-215",  751,  Mapper751_Init },
	{ "N42S-2",  754,  Mapper754_Init },
	{ " ",  757,  Mapper757_Init },
	{ "5068",  759,  Mapper759_Init },
	{ "90-in-1", 762, Mapper762_Init },
	{ "BS-N032", 763, Mapper763_Init },
	{ "43-236/841134C", 764, Mapper764_Init },
	{ "820315-C", 765, Mapper765_Init },
	{ "FlameCyclone_Mapper", 800, Mapper800_Init },
	{"",					0, NULL}
};
int iNESLoad(const char* name, FCEUFILE* fp, int OverwriteVidMode) {
	int result;
	struct md5_context md5;
	uint64 partialmd5 = 0;
	const char* mappername = "Not Listed";
	size_t filesize = FCEU_fgetsize(fp);

	if (FCEU_fread(&head, 1, 16, fp) != 16 || memcmp(&head, "NES\x1A", 4))
		return LOADER_INVALID_FORMAT;
	// Remove header size from filesize
	filesize -= 16;

	head.cleanup();

	iNESCart.clear();

	iNES2 = ((head.ROM_type2 & 0x0C) == 0x08);
	if (iNES2)
	{
		iNESCart.ines2 = true;
		iNESCart.wram_size = (head.RAM_size & 0x0F) ? (64 << (head.RAM_size & 0x0F)) : 0;
		iNESCart.battery_wram_size = (head.RAM_size & 0xF0) ? (64 << ((head.RAM_size & 0xF0) >> 4)) : 0;
		iNESCart.vram_size = (head.VRAM_size & 0x0F) ? (64 << (head.VRAM_size & 0x0F)) : 0;
		iNESCart.battery_vram_size = (head.VRAM_size & 0xF0) ? (64 << ((head.VRAM_size & 0xF0) >> 4)) : 0;
		iNESCart.submapper = head.ROM_type3 >> 4;
	}

	MapperNo = (head.ROM_type >> 4);
	MapperNo |= (head.ROM_type2 & 0xF0);
	if (iNES2) MapperNo |= ((head.ROM_type3 & 0x0F) << 8);

	if (head.ROM_type & 8) {
		Mirroring = 2;
	}
	else
		Mirroring = (head.ROM_type & 1);

	MirroringAs2bits = head.ROM_type & 1;
	if (head.ROM_type & 8) MirroringAs2bits |= 2;

	int not_round_size = 0;
	int rom_size_bytes = 0;
	int vrom_size_bytes = 0;

	if (!iNES2) {
		not_round_size = head.ROM_size << 14;
	}
	else {
		if ((head.Upper_ROM_VROM_size & 0x0F) != 0x0F)
			// simple notation
			not_round_size = (head.ROM_size | ((head.Upper_ROM_VROM_size & 0x0F) << 8)) << 14;
		else
			// exponent-multiplier notation
			not_round_size = ((1 << (head.ROM_size >> 2)) * ((head.ROM_size & 0b11) * 2 + 1));
	}

	if (!head.ROM_size && !iNES2)
		rom_size_bytes = 256 << 14;
	else
		rom_size_bytes = uppow2(not_round_size);

	if (!iNES2) {
		vrom_size_bytes = uppow2(head.VROM_size << 13);
	}
	else {
		if ((head.Upper_ROM_VROM_size & 0xF0) != 0xF0)
			// simple notation
			vrom_size_bytes = uppow2((head.VROM_size | ((head.Upper_ROM_VROM_size & 0xF0) << 4)) << 13);
		else
			vrom_size_bytes = ((1 << (head.VROM_size >> 2)) * ((head.VROM_size & 0b11) * 2 + 1));
	}

	int round = true;
	for (int i = 0; i != sizeof(not_power2) / sizeof(not_power2[0]); ++i) {
		//for games not to the power of 2, so we just read enough
		//prg rom from it, but we have to keep ROM_size to the power of 2
		//since PRGCartMapping wants ROM_size to be to the power of 2
		//so instead if not to power of 2, we just use head.ROM_size when
		//we use FCEU_read
		if (not_power2[i] == MapperNo) {
			round = false;
			break;
		}
	}
	ROM_size = rom_size_bytes >> 14;
	Temp_Rom_Size = ROM_size;
	VROM_size = vrom_size_bytes >> 13;

	ROM = (uint8*)FCEU_malloc(rom_size_bytes);
	memset(ROM, 0xFF, rom_size_bytes);

	if (vrom_size_bytes) {
		VROM = (uint8*)FCEU_malloc(vrom_size_bytes);
		memset(VROM, 0xFF, vrom_size_bytes);
	}

	// Set Vs. System flag if need
	if (!iNES2) {
		GameInfo->type = !(head.ROM_type2 & 1) ? GIT_CART : GIT_VSUNI;
	}
	else {
		switch (!(head.ROM_type2 & 2) ? (head.ROM_type2 & 3) : (head.VS_hardware & 0xF)) {
		case 0:
			GameInfo->type = GIT_CART;
			break;
		case 1:
			GameInfo->type = GIT_VSUNI;
			break;
		default:
			FCEU_PrintError("Game type is not supported at all.");
			goto init_error;
		}
	}

	iNESCart.totalFileSize = filesize;

	// Set Vs. System PPU type if need
	if (GameInfo->type == GIT_VSUNI && !(head.ROM_type2 & 2)) {
		switch (head.VS_hardware & 0xF) {
		case 0x0: GameInfo->vs_ppu = GIPPU_RC2C03B; break;
			//case 0x1: GameInfo->vs_ppu = GIPPU_RPC2C03C; break;
		case 0x2: GameInfo->vs_ppu = GIPPU_RP2C04_0001; break;
		case 0x3: GameInfo->vs_ppu = GIPPU_RP2C04_0002; break;
		case 0x4: GameInfo->vs_ppu = GIPPU_RP2C04_0003; break;
		case 0x5: GameInfo->vs_ppu = GIPPU_RP2C04_0004; break;
		case 0x6: GameInfo->vs_ppu = GIPPU_RC2C03B; break;
			//case 0x7: GameInfo->ppu = GIPPU_RPC2C03C; break;
		case 0x8: GameInfo->vs_ppu = GIPPU_RC2C05_01; break;
		case 0x9: GameInfo->vs_ppu = GIPPU_RC2C05_02; break;
		case 0xA: GameInfo->vs_ppu = GIPPU_RC2C05_03; break;
		case 0xB: GameInfo->vs_ppu = GIPPU_RC2C05_04; break;
			//case 0xC: GameInfo->ppu = GIPPU_RPC2C05_05; break;
		default:
			FCEU_PrintError("Vs. System PPU type is not supported at all.");
			goto init_error;
		}

		switch (head.VS_hardware >> 4) {
		case 0x0: GameInfo->vs_type = EGIVS_NORMAL; break;
		case 0x1: GameInfo->vs_type = EGIVS_RBI; break;
		case 0x2: GameInfo->vs_type = EGIVS_TKO; break;
		case 0x3: GameInfo->vs_type = EGIVS_XEVIOUS; break;
		default:
			FCEU_PrintError("Vs. System type is not supported at all.");
			goto init_error;
		}
	}

	if (head.ROM_type & 4) {	/* Trainer */
		trainerpoo = (uint8*)FCEU_gmalloc(512);
		FCEU_fread(trainerpoo, 512, 1, fp);
		filesize -= 512;
	}

	ResetCartMapping();
	ResetExState(0, 0);

	SetupCartPRGMapping(0, ROM, rom_size_bytes, 0);

	FCEU_fread(ROM, 1, (round) ? rom_size_bytes : not_round_size, fp);

	if (vrom_size_bytes)
		FCEU_fread(VROM, 1, vrom_size_bytes, fp);

	// Misc ROMS
	if ((head.misc_roms & 0x03) && !(head.ROM_type & 4)) {
		MiscROM_size = filesize - rom_size_bytes - vrom_size_bytes;
		MiscROM = (uint8*)FCEU_malloc(MiscROM_size);
		memset(MiscROM, 0xFF, MiscROM_size);
		FCEU_fread(MiscROM, 1, MiscROM_size, fp);
		FCEU_printf(" Misc ROM size : %d\n", MiscROM_size);
	}

	md5_starts(&md5);
	md5_update(&md5, ROM, rom_size_bytes);

	iNESGameCRC32 = CalcCRC32(0, ROM, rom_size_bytes);

	if (vrom_size_bytes) {
		iNESGameCRC32 = CalcCRC32(iNESGameCRC32, VROM, vrom_size_bytes);
		md5_update(&md5, VROM, vrom_size_bytes);
	}
	md5_finish(&md5, iNESCart.MD5);
	memcpy(&GameInfo->MD5, &iNESCart.MD5, sizeof(iNESCart.MD5));
	for (int x = 0; x < 8; x++)
		partialmd5 |= (uint64)iNESCart.MD5[7 - x] << (x * 8);
	iNESCart.PRGRomSize = ROM_size >= 0xF00 ? (pow(2, head.ROM_size >> 2) * ((head.ROM_size & 3) * 2 + 1)) : (ROM_size * 0x4000);
	iNESCart.CHRRomSize = VROM_size >= 0xF00 ? (pow(2, head.VROM_size >> 2) * ((head.VROM_size & 3) * 2 + 1)) : (VROM_size * 0x2000);
	iNESCart.PRGCRC32 = CalcCRC32(0, ROM, iNESCart.PRGRomSize);
	iNESCart.CHRCRC32 = CalcCRC32(0, VROM, iNESCart.CHRRomSize);
	iNESCart.CRC32 = iNESGameCRC32;

	FCEU_printf(" PRG ROM: %d x 16KiB = %d KiB\n", (round ? rom_size_bytes : not_round_size) >> 14, ((round ? rom_size_bytes : not_round_size) >> 14) * 16);
	FCEU_printf(" CHR ROM: %d x  8KiB = %d KiB\n", (vrom_size_bytes >> 13), (vrom_size_bytes >> 13) * 8);
	FCEU_printf(" PRG-ROM CRC32:  0x%08X\n", iNESCart.PRGCRC32);
	FCEU_printf(" CHR-ROM CRC32:  0x%08X\n", iNESCart.CHRCRC32);
	FCEU_printf(" ROM CRC32: 0x%08x\n", iNESGameCRC32);
	{
		int x;
		FCEU_printf(" ROM MD5:  0x");
		for (x = 0; x < 16; x++)
			FCEU_printf("%02x", iNESCart.MD5[x]);
		FCEU_printf("\n");
	}

	for (size_t mappertest = 0; mappertest < (sizeof bmap / sizeof bmap[0]) - 1; mappertest++) {
		if (bmap[mappertest].number == MapperNo) {
			mappername = bmap[mappertest].name;
			break;
		}
	}

	FCEU_printf(" Mapper #: %d\n", MapperNo);
	FCEU_printf(" Mapper name: %s\n", mappername);
	FCEU_printf(" Mirroring: %s\n", Mirroring == 2 ? "None (Four-screen)" : Mirroring ? "Vertical" : "Horizontal");
	FCEU_printf(" Battery-backed: %s\n", (head.ROM_type & 2) ? "Yes" : "No");
	FCEU_printf(" Trained: %s\n", (head.ROM_type & 4) ? "Yes" : "No");
	if (iNES2)
	{
		ROM_size |= ((head.Upper_ROM_VROM_size >> 0) & 0xF) << 8;
		if (head.RAM_size & 0x0F) iNESCart.PRGRamSize = 64 << ((head.RAM_size >> 0) & 0x0F);
		if (head.RAM_size & 0xF0) iNESCart.PRGRamSaveSize = 64 << ((head.RAM_size >> 4) & 0x0F);
		if (head.VRAM_size & 0x0F) iNESCart.CHRRamSize = 64 << ((head.VRAM_size >> 0) & 0x0F);
		if (head.VRAM_size & 0xF0) iNESCart.CHRRamSaveSize = 64 << ((head.VRAM_size >> 4) & 0x0F);
		FCEU_printf(" NES2.0 Extensions\n");
		FCEU_printf(" Sub Mapper #: %d\n", iNESCart.submapper);
		FCEU_printf(" Total WRAM size: %d KiB\n", (iNESCart.wram_size + iNESCart.battery_wram_size) / 1024);
		FCEU_printf(" Total VRAM size: %d KiB\n", (iNESCart.vram_size + iNESCart.battery_vram_size) / 1024);
		iNESCart.PRGRomSize = ROM_size >= 0xF00 ? (pow(2, head.ROM_size >> 2) * ((head.ROM_size & 3) * 2 + 1)) : (ROM_size * 0x4000);
		iNESCart.CHRRomSize = VROM_size >= 0xF00 ? (pow(2, head.VROM_size >> 2) * ((head.VROM_size & 3) * 2 + 1)) : (VROM_size * 0x2000);
		iNESCart.miscROMNumber = head.misc_roms;
		iNESCart.miscROMSize = iNESCart.miscROMNumber ? (iNESCart.totalFileSize - 16 - (head.ROM_type & 4 ? 512 : 0) - iNESCart.PRGRomSize - iNESCart.CHRRomSize) : 0;
		if (iNESCart.miscROMSize & 0x8000000) {
			iNESCart.miscROMSize = 0;
		}
		else {
			//iNESCart.submapper = iNESCart.miscROMNumber = iNESCart.miscROMSize = 0
			iNESCart.miscROMNumber = iNESCart.miscROMSize = 0;
			iNESCart.PRGRomSize = ROM_size * 0x4000;
			iNESCart.CHRRomSize = VROM_size * 0x2000;
		}
		if (head.ROM_type & 2)
		{
			FCEU_printf(" WRAM backed by battery: %d KiB\n", iNESCart.battery_wram_size / 1024);
			FCEU_printf(" VRAM backed by battery: %d KiB\n", iNESCart.battery_vram_size / 1024);
		}
		if (head.misc_roms & 0x03) FCEU_printf(" Misc ROM: %d KiB\n", MiscROM_size / 1024);
	}

	SetInput();
	// Input can be overriden by NES 2.0 header
	if (iNES2) SetInputNes20(head.expansion);
	CheckHInfo(partialmd5);
	FCEU_VSUniCheck(partialmd5, &MapperNo, &Mirroring);
	CheckBad(partialmd5);

	/* Must remain here because above functions might change value of
	VROM_size and free(VROM).
	*/
	if (vrom_size_bytes)
		SetupCartCHRMapping(0, VROM, vrom_size_bytes, 0);

	if (Mirroring == 2) {
		ExtraNTARAM = (uint8*)FCEU_gmalloc(2048);
		SetupCartMirroring(4, 1, ExtraNTARAM);
	}
	else if (Mirroring >= 0x10)
		SetupCartMirroring(2 + (Mirroring & 1), 1, 0);
	else
		SetupCartMirroring(Mirroring & 1, (Mirroring & 4) >> 2, 0);

	iNESCart.battery = (head.ROM_type & 2) ? 1 : 0;
	iNESCart.mirror = Mirroring;
	iNESCart.mirrorAs2Bits = MirroringAs2bits;

	result = iNES_Init(MapperNo);
	switch (result)
	{
	case 0:
		goto init_ok;
	case 1:
		FCEU_PrintError("iNES mapper #%d is not supported at all.", MapperNo);
		break;
	case 2:
		FCEU_PrintError("Unable to allocate CHR-RAM.");
		break;
	}

init_error:
	if (ROM) free(ROM);
	if (VROM) free(VROM);
	if (trainerpoo) free(trainerpoo);
	if (ExtraNTARAM) free(ExtraNTARAM);
	ROM = NULL;
	VROM = NULL;
	trainerpoo = NULL;
	ExtraNTARAM = NULL;
	return LOADER_HANDLED_ERROR;

init_ok:

	GameInfo->mappernum = MapperNo;
	FCEU_LoadGameSave(&iNESCart);

	strcpy(LoadedRomFName, name); //bbit edited: line added

	// Extract Filename only. Should account for Windows/Unix this way.
	if (strrchr(name, '/')) {
		name = strrchr(name, '/') + 1;
	}
	else if (strrchr(name, '\\')) {
		name = strrchr(name, '\\') + 1;
	}

	GameInterface = iNESGI;
	currCartInfo = &iNESCart;
	FCEU_printf("\n");

	// since apparently the iNES format doesn't store this information,
	// guess if the settings should be PAL or NTSC from the ROM name
	// TODO: MD5 check against a list of all known PAL games instead?
	if (iNES2) {
		FCEUI_SetVidSystem(((head.TV_system & 3) == 1) ? 1 : 0);
	}
	else if (OverwriteVidMode) {
		if (strstr(name, "(E)") || strstr(name, "(e)")
			|| strstr(name, "(Europe)") || strstr(name, "(PAL)")
			|| strstr(name, "(F)") || strstr(name, "(f)")
			|| strstr(name, "(G)") || strstr(name, "(g)")
			|| strstr(name, "(I)") || strstr(name, "(i)"))
			FCEUI_SetVidSystem(1);
		else
			FCEUI_SetVidSystem(0);
	}
	return LOADER_OK;
}

// bbit edited: the whole function below was added
int iNesSave(void) {
	char name[2048];

	strcpy(name, LoadedRomFName);
	if (strcmp(name + strlen(name) - 4, ".nes") != 0) { //para edit
		strcat(name, ".nes");
	}

	return iNesSaveAs(name);
}

int iNesSaveAs(const char* name)
{
	//adelikat: TODO: iNesSave() and this have pretty much the same code, outsource the common code to a single function
	//caitsith2: done. iNesSave() now gets filename and calls iNesSaveAs with that filename.
	FILE *fp;

	if ((GameInfo->type != GIT_CART) && (GameInfo->type != GIT_VSUNI)) return 0;
	if (GameInterface != iNESGI) return 0;

	fp = fopen(name, "wb");
	if (!fp)
		return 0;

	if (fwrite(&head, 1, 16, fp) != 16)
	{
		fclose(fp);
		return 0;
	}

	if (head.ROM_type & 4)
	{
		/* Trainer */
		fwrite(trainerpoo, 512, 1, fp);
	}

	//fwrite(ROM, 0x4000, ROM_size, fp);
	fwrite(ROM, 0x4000, Temp_Rom_Size, fp);

	if (head.VROM_size)
		fwrite(VROM, 0x2000, head.VROM_size, fp);

	fclose(fp);
	return 1;
}

//para edit: added function below
char *iNesShortFName(void) {
	char *ret;

	if (!(ret = strrchr(LoadedRomFName, '\\')))
	{
		if (!(ret = strrchr(LoadedRomFName, '/')))
			return 0;
	}
	return ret + 1;
}

static int iNES_Init(int num) {
	BMAPPINGLocal *tmp = bmap;

	CHRRAMSize = -1;

	if (GameInfo->type == GIT_VSUNI)
		AddExState(FCEUVSUNI_STATEINFO, ~0, 0, 0);

	while (tmp->init) {
		if (num == tmp->number) {
			UNIFchrrama = NULL;	// need here for compatibility with UNIF mapper code
			if (!VROM_size) {
				if(!iNESCart.ines2)
				{
					switch (num) {	// FIXME, mapper or game data base with the board parameters and ROM/RAM sizes
					case 13:  CHRRAMSize = 16 * 1024; break;
					case 6:
					case 29:
					case 30:
					case 45:
					case 96:  CHRRAMSize = 32 * 1024; break;
					case 176: CHRRAMSize = 128 * 1024; break;
					default:  CHRRAMSize = 8 * 1024; break;
					}
					iNESCart.vram_size = CHRRAMSize;
				}
				else
				{
					CHRRAMSize = iNESCart.battery_vram_size + iNESCart.vram_size;
				}
				if (CHRRAMSize > 0)
				{
					int mCHRRAMSize = (CHRRAMSize < 1024) ? 1024 : CHRRAMSize; // VPage has a resolution of 1k banks, ensure minimum allocation to prevent malicious access from NES software
					if ((UNIFchrrama = VROM = (uint8*)FCEU_dmalloc(mCHRRAMSize)) == NULL) return 2;
					FCEU_MemoryRand(VROM, CHRRAMSize);
					SetupCartCHRMapping(0, VROM, CHRRAMSize, 1);
					AddExState(VROM, CHRRAMSize, 0, "CHRR");
				}
				else {
					// mapper 256 (OneBus) has not CHR-RAM _and_ has not CHR-ROM region in iNES file
					// so zero-sized CHR should be supported at least for this mapper
					VROM = NULL;
				}
			}
			if (head.ROM_type & 8)
			{
				if (ExtraNTARAM != NULL)
				{
					AddExState(ExtraNTARAM, 2048, 0, "EXNR");
				}
			}
			tmp->init(&iNESCart);
			return 0;
		}
		tmp++;
	}
	return 1;
}
