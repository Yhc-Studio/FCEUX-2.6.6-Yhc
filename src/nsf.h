#ifndef NSF_H
#define NSF_H

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

typedef struct {
	char ID[5]; /*NESM^Z*/
	uint8 Version;
	uint8 TotalSongs;
	uint8 StartingSong;
	uint8 LoadAddressLow;
	uint8 LoadAddressHigh;
	uint8 InitAddressLow;
	uint8 InitAddressHigh;
	uint8 PlayAddressLow;
	uint8 PlayAddressHigh;
	uint8 SongName[32];
	uint8 Artist[32];
	uint8 Copyright[32];
	uint8 NTSCspeed[2];        // microseconds between play calls
	uint8 BankSwitch[8];
	uint8 PALspeed[2];         // microseconds between play calls
	uint8 VideoSystem;
	uint8 SoundChip;
	uint8 Expansion[4];
	uint8 reserve[8];
} NSF_HEADER;

/*
 * UI-facing NSF information snapshot.
 * Keep this separate from NSF_HEADER so frontends do not need to touch
 * nsf.cpp internal static variables such as CurrentSong, LoadAddr, BSon, etc.
 */
typedef struct {
	char songName[32];
	char artist[32];
	char copyright[32];

	int totalSongs;
	int startingSong;
	int currentSong;

	uint16 loadAddr;
	uint16 initAddr;
	uint16 playAddr;

	uint8 videoSystem;
	uint8 soundChip;
	uint8 bankSwitch[8];

	int bankSwitched;
	int maxBank;
	int nsfSize;

	int paused;
	int stopped;
	int outputMuted;

	/* NSFe metadata, when available. */
	int isNSFe;
	char trackLabel[64];
	int trackTimeMs;
	int trackFadeMs;
	char ripper[64];
} FCEU_NSFInfo;

void NSF_init(void);
void DrawNSF(uint8* XBuf);
extern NSF_HEADER NSFHeader; //mbg merge 6/29/06
extern uint8* NSFDATA;
extern int NSFMaxBank;
void NSFDealloc(void);
void NSFDodo(void);
void DoNSFFrame(void);

/* NSF player frontend/control helpers. */
void FCEUI_NSFSetVis(int mode);
int FCEUI_NSFChange(int amount);
int FCEUI_NSFSetSong(int song);
int FCEUI_NSFGetCurrentSong(void);
int FCEUI_NSFGetTotalSongs(void);
void FCEUI_NSFReloadSong(void);
void FCEUI_NSFTogglePause(void);
void FCEUI_NSFStopSong(void);
int FCEUI_NSFIsPaused(void);
int FCEUI_NSFIsStopped(void);
int FCEUI_NSFIsOutputMuted(void);
void FCEUI_NSFGetFullInfo(FCEU_NSFInfo* info);
int FCEUI_NSFGetInfo(uint8* name, uint8* artist, uint8* copyright, int maxlen);

#endif
