/* FCE Ultra - NES/Famicom Emulator
*
* Copyright notice for this file:
*  Copyright (C) 2002 Xodnizel and zeromus
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

//todo - config synchronization guards
//todo - use correct framerate instead of 60
//todo - find out why fast forwarding at 96khz causes buffering to glitch?

#include <list>
#include "common.h"
#include "main.h"
#include "sound.h"
#include "../../fceu.h"

// Step18: detect Windows default audio endpoint changes and automatically reopen DirectSound output.
// This fixes cases where switching to headphones/Bluetooth/HDMI leaves the old DirectSound device silent.
#include <objbase.h>
#include <mmdeviceapi.h>
#include <wchar.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

extern bool turbo;		//If turbo is running

/// controls whether playback is muted
static bool mute = false;
/// indicates whether we've been coerced into outputting 8bit audio
static int bits;

#undef min
#undef max
#include "oakra.h"

OAKRA_Module_OutputDS* dsout;
bool muteTurbo = true;

int soundVRC6vol = 256;
int soundVRC7vol = 256;
int soundFDSvol = 256;
int soundMMC5vol = 256;
int soundN163vol = 256;
int soundS5Bvol = 256;

static void ApplyExpansionVolumeSettings(void)
{
	FSettings.VRC6Volume = soundVRC6vol;
	FSettings.VRC7Volume = soundVRC7vol;
	FSettings.FDSVolume = soundFDSvol;
	FSettings.MMC5Volume = soundMMC5vol;
	FSettings.N163Volume = soundN163vol;
	FSettings.S5BVolume = soundS5Bvol;
}

static int soundVolumeEditChanging = 0;

static int ClampSoundValue(int value, int maxValue)
{
	if (value < 0) return 0;
	if (value > maxValue) return maxValue;
	return value;
}

static void SetSoundVolumeEditValue(HWND hwndDlg, int controlId, int value)
{
	char tbuf[16];
	sprintf(tbuf, "%d", value);
	soundVolumeEditChanging++;
	SetDlgItemText(hwndDlg, controlId, (LPTSTR)tbuf);
	soundVolumeEditChanging--;
}

static void UpdateSoundVolumeEditBoxes(HWND hwndDlg)
{
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_MASTER, soundvolume);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_TRIANGLE, soundTrianglevol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_SQUARE1, soundSquare1vol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_SQUARE2, soundSquare2vol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_NOISE, soundNoisevol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_PCM, soundPCMvol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_VRC6, soundVRC6vol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_VRC7, soundVRC7vol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_FDS, soundFDSvol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_MMC5, soundMMC5vol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_N163, soundN163vol);
	SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_S5B, soundS5Bvol);
}

static void EnableSoundVolumeEditControls(HWND hwndDlg, BOOL enabled)
{
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_MASTER), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_TRIANGLE), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_SQUARE1), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_SQUARE2), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_NOISE), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_PCM), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_VRC6), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_VRC7), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_FDS), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_MMC5), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_N163), enabled);
	EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_S5B), enabled);
}

static void UpdateAPUVolumeTrackbars(HWND hwndDlg)
{
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR, TBM_SETPOS, 1, soundvolume);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE, TBM_SETPOS, 1, soundTrianglevol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1, TBM_SETPOS, 1, soundSquare1vol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2, TBM_SETPOS, 1, soundSquare2vol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE, TBM_SETPOS, 1, soundNoisevol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_PCM, TBM_SETPOS, 1, soundPCMvol);
}

static void UpdateExpansionVolumeTrackbars(HWND hwndDlg)
{
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6, TBM_SETPOS, 1, soundVRC6vol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7, TBM_SETPOS, 1, soundVRC7vol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_FDS, TBM_SETPOS, 1, soundFDSvol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5, TBM_SETPOS, 1, soundMMC5vol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_N163, TBM_SETPOS, 1, soundN163vol);
	SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_S5B, TBM_SETPOS, 1, soundS5Bvol);
}

static int HandleSoundVolumeScroll(HWND hwndDlg, HWND hScroll)
{
	if (!hScroll) return 0;

	if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR) == hScroll)
	{
		soundvolume = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR, TBM_GETPOS, 0, 0);
		FCEUI_SetSoundVolume(soundvolume);
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_MASTER, soundvolume);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE) == hScroll)
	{
		soundTrianglevol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE, TBM_GETPOS, 0, 0);
		FCEUI_SetTriangleVolume(soundTrianglevol);
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_TRIANGLE, soundTrianglevol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1) == hScroll)
	{
		soundSquare1vol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1, TBM_GETPOS, 0, 0);
		FCEUI_SetSquare1Volume(soundSquare1vol);
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_SQUARE1, soundSquare1vol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2) == hScroll)
	{
		soundSquare2vol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2, TBM_GETPOS, 0, 0);
		FCEUI_SetSquare2Volume(soundSquare2vol);
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_SQUARE2, soundSquare2vol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE) == hScroll)
	{
		soundNoisevol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE, TBM_GETPOS, 0, 0);
		FCEUI_SetNoiseVolume(soundNoisevol);
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_NOISE, soundNoisevol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_PCM) == hScroll)
	{
		soundPCMvol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_PCM, TBM_GETPOS, 0, 0);
		FCEUI_SetPCMVolume(soundPCMvol);
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_PCM, soundPCMvol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6) == hScroll)
	{
		soundVRC6vol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6, TBM_GETPOS, 0, 0);
		ApplyExpansionVolumeSettings();
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_VRC6, soundVRC6vol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7) == hScroll)
	{
		soundVRC7vol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7, TBM_GETPOS, 0, 0);
		ApplyExpansionVolumeSettings();
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_VRC7, soundVRC7vol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_FDS) == hScroll)
	{
		soundFDSvol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_FDS, TBM_GETPOS, 0, 0);
		ApplyExpansionVolumeSettings();
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_FDS, soundFDSvol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5) == hScroll)
	{
		soundMMC5vol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5, TBM_GETPOS, 0, 0);
		ApplyExpansionVolumeSettings();
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_MMC5, soundMMC5vol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_N163) == hScroll)
	{
		soundN163vol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_N163, TBM_GETPOS, 0, 0);
		ApplyExpansionVolumeSettings();
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_N163, soundN163vol);
		return 1;
	}
	else if (GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_S5B) == hScroll)
	{
		soundS5Bvol = (int)SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_S5B, TBM_GETPOS, 0, 0);
		ApplyExpansionVolumeSettings();
		SetSoundVolumeEditValue(hwndDlg, IDC_VOLUME_EDIT_S5B, soundS5Bvol);
		return 1;
	}

	return 0;
}

static int HandleSoundVolumeEditChange(HWND hwndDlg, int controlId)
{
	if (soundVolumeEditChanging)
		return 1;

	BOOL ok = FALSE;
	int value = (int)GetDlgItemInt(hwndDlg, controlId, &ok, FALSE);
	if (!ok)
		return 1;

	switch (controlId)
	{
	case IDC_VOLUME_EDIT_MASTER:
		soundvolume = ClampSoundValue(value, 150);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR, TBM_SETPOS, 1, soundvolume);
		FCEUI_SetSoundVolume(soundvolume);
		if (soundvolume != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundvolume);
		return 1;
	case IDC_VOLUME_EDIT_TRIANGLE:
		soundTrianglevol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE, TBM_SETPOS, 1, soundTrianglevol);
		FCEUI_SetTriangleVolume(soundTrianglevol);
		if (soundTrianglevol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundTrianglevol);
		return 1;
	case IDC_VOLUME_EDIT_SQUARE1:
		soundSquare1vol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1, TBM_SETPOS, 1, soundSquare1vol);
		FCEUI_SetSquare1Volume(soundSquare1vol);
		if (soundSquare1vol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundSquare1vol);
		return 1;
	case IDC_VOLUME_EDIT_SQUARE2:
		soundSquare2vol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2, TBM_SETPOS, 1, soundSquare2vol);
		FCEUI_SetSquare2Volume(soundSquare2vol);
		if (soundSquare2vol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundSquare2vol);
		return 1;
	case IDC_VOLUME_EDIT_NOISE:
		soundNoisevol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE, TBM_SETPOS, 1, soundNoisevol);
		FCEUI_SetNoiseVolume(soundNoisevol);
		if (soundNoisevol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundNoisevol);
		return 1;
	case IDC_VOLUME_EDIT_PCM:
		soundPCMvol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_PCM, TBM_SETPOS, 1, soundPCMvol);
		FCEUI_SetPCMVolume(soundPCMvol);
		if (soundPCMvol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundPCMvol);
		return 1;
	case IDC_VOLUME_EDIT_VRC6:
		soundVRC6vol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6, TBM_SETPOS, 1, soundVRC6vol);
		ApplyExpansionVolumeSettings();
		if (soundVRC6vol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundVRC6vol);
		return 1;
	case IDC_VOLUME_EDIT_VRC7:
		soundVRC7vol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7, TBM_SETPOS, 1, soundVRC7vol);
		ApplyExpansionVolumeSettings();
		if (soundVRC7vol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundVRC7vol);
		return 1;
	case IDC_VOLUME_EDIT_FDS:
		soundFDSvol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_FDS, TBM_SETPOS, 1, soundFDSvol);
		ApplyExpansionVolumeSettings();
		if (soundFDSvol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundFDSvol);
		return 1;
	case IDC_VOLUME_EDIT_MMC5:
		soundMMC5vol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5, TBM_SETPOS, 1, soundMMC5vol);
		ApplyExpansionVolumeSettings();
		if (soundMMC5vol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundMMC5vol);
		return 1;
	case IDC_VOLUME_EDIT_N163:
		soundN163vol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_N163, TBM_SETPOS, 1, soundN163vol);
		ApplyExpansionVolumeSettings();
		if (soundN163vol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundN163vol);
		return 1;
	case IDC_VOLUME_EDIT_S5B:
		soundS5Bvol = ClampSoundValue(value, 256);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_S5B, TBM_SETPOS, 1, soundS5Bvol);
		ApplyExpansionVolumeSettings();
		if (soundS5Bvol != value) SetSoundVolumeEditValue(hwndDlg, controlId, soundS5Bvol);
		return 1;
	}

	return 0;
}


//prototypes
void UpdateSoundChannelQualityMode(HWND hwndDlg);	//Updates the sound channel volume sliders, disables and renames them for low quality

//manages a set of small buffers which together work as one large buffer capable of resizing itsself and 
//shrinking when a larger buffer is no longer necessary
class BufferSet {
public:

	static const int BufferSize = 1024;
	static const int BufferSizeBits = 10;
	static const int BufferSizeBitmask = 1023;

	class Buffer {
	public:
		int decay, size, length;
		Buffer(int size) { length = 0; this->size = size; data = OAKRA_Module::malloc(size); }
		int getRemaining() { return size - length; }
		void* data;
		~Buffer() { if (data) { OAKRA_Module::free(data); data = nullptr; } }
	};

	std::vector<Buffer*> liveBuffers;
	std::vector<Buffer*> freeBuffers;
	int length;
	int offset; //offset of beginning of addressing into current buffer
	int bufferCount;

	BufferSet() {
		offset = length = bufferCount = 0;
	}

	//causes the oldest free buffer to decay one unit. kills it if it gets too old
	void decay(int threshold) {
		if (freeBuffers.empty()) return;
		if (freeBuffers[0]->decay++ >= threshold) {
			delete freeBuffers[0];
			freeBuffers.erase(freeBuffers.begin());
		}
	}

	Buffer* getBuffer() {
		//try to get a buffer from the free pool first
		//if theres nothing in the free pool, get a new buffer
		if (!freeBuffers.size()) return getNewBuffer();
		//otherwise, return the last thing in the free pool (most recently freed)
		Buffer* ret = *--freeBuffers.end();
		freeBuffers.erase(--freeBuffers.end());
		return ret;
	}

	//adds the buffer to the free list
	void freeBuffer(Buffer* buf) {
		freeBuffers.push_back(buf);
		buf->decay = 0;
		buf->length = 0;
	}

	Buffer* getNewBuffer() {
		bufferCount++;
		return new Buffer(BufferSize);
	}

	short getShortAtIndex(int addr) {
		addr <<= 1; //shorts are 2bytes
		int buffer = (addr + offset) >> BufferSizeBits;
		int ofs = (addr + offset) & BufferSizeBitmask;
		return *(short*)((char*)liveBuffers[buffer]->data + ofs);
	}

	//dequeues the specified number of bytes
	void dequeue(int length) {
		offset += length;
		while (offset >= BufferSize) {
			Buffer* front = liveBuffers[0];
			freeBuffer(front);
			liveBuffers.erase(liveBuffers.begin());
			offset -= BufferSize;
		}
		this->length -= length;
	}

	//not being used now:

	//tries to lock the specified number of bytes for reading.
	//not all the bytes you asked for will be locked (no more than one buffer-full)
	//try again if you didnt get enough.
	//returns the number of bytes locked.
	//int lock(int length, void **ptr) {
	//	int remain = BufferSize-offset;
	//	*ptr = (char*)liveBuffers[0]->data + offset;
	//	if(length<remain)
	//		return length;
	//	else
	//		return remain;
	//}

	void enqueue(int length, void* data) {
		int todo = length;
		int done = 0;

		//if there are no buffers in the queue, then we definitely need one before we start
		if (liveBuffers.empty()) liveBuffers.push_back(getBuffer());

		while (todo) {
			//check the frontmost buffer
			Buffer* end = *--liveBuffers.end();
			int available = std::min(todo, end->getRemaining());
			memcpy((char*)end->data + end->length, (char*)data + done, available);
			end->length += available;
			todo -= available;
			done += available;

			//we're going to need another buffer
			if (todo != 0)
				liveBuffers.push_back(getBuffer());
		}

		this->length += length;
	}
};

class Player : public OAKRA_Module {
public:

	int cursor;
	BufferSet buffers;
	int scale;

	//not interpolating! someday it will! 
	int generate(int samples, void* buf) {

		int64 incr = 256;
		int64 bufferSamples = buffers.length >> 1;

		//if we're we're too far behind, playback faster
		if (bufferSamples > soundrate * 3 / 60) {
			int64 behind = bufferSamples - soundrate / 60;
			incr = behind * 256 * 60 / soundrate / 2;
			//we multiply our playback rate by 1/2 the number of frames we're behind
		}
		if (incr < 256)
		{
			//sanity check: should never be less than 256
			FCEU_printf("OHNO -- %d -- shouldnt be less than 256!\n", incr);
		}

		incr = (incr * scale) >> 8; //apply scaling factor

		//figure out how many dest samples we can generate at this rate without running out of source samples
		int destSamplesCanGenerate = (bufferSamples << 8) / incr;

		int todo = std::min(destSamplesCanGenerate, samples);
		short* sbuf = (short*)buf;
		for (int i = 0; i < todo; i++) {
			sbuf[i] = buffers.getShortAtIndex(cursor >> 8);
			cursor += incr;
		}
		buffers.dequeue((cursor >> 8) << 1);
		cursor &= 255;

		//perhaps mute
		if (mute) memset(sbuf, 0, samples << 1);
		else memset(sbuf + todo, 0, (samples - todo) << 1);

		return samples;
	}

	void receive(int bytes, void* buf) {
		dsout->lock();
		buffers.enqueue(bytes, buf);
		buffers.decay(60);
		dsout->unlock();
	}

	void throttle() {
		//wait for the buffer to be satisfactorily low before continuing
		for (;;) {
			dsout->lock();
			int remain = buffers.length >> 1;
			dsout->unlock();
			if (remain < soundrate / 60) break;
			//if(remain<soundrate*scale/256/60) break; ??
			Sleep(1);
		}
	}

	void setScale(int scale) {
		dsout->lock();
		this->scale = scale;
		dsout->unlock();
	}

	Player() {
		scale = 256;
		cursor = 0;
	}
};

//this class just converts the primary 16bit audio stream to 8bit
class Player8 : public OAKRA_Module {
public:
	Player* player;
	Player8(Player* player) { this->player = player; }
	int generate(int samples, void* buf) {
		int half = samples >> 1;
		//retrieve first half of 16bit samples
		player->generate(half, buf);
		//and convert to 8bit
		unsigned char* dbuf = (unsigned char*)buf;
		short* sbuf = (short*)buf;
		for (int i = 0; i < half; i++)
			dbuf[i] = (sbuf[i] >> 8) ^ 0x80;
		//now retrieve second half
		int remain = samples - half;
		short* halfbuf = (short*)alloca(remain << 1);
		player->generate(remain, halfbuf);
		dbuf += half;
		for (int i = 0; i < remain; i++)
			dbuf[i] = (halfbuf[i] >> 8) ^ 0x80;
		return samples;
	}
};

static Player* player;
static Player8* player8;

static int soundDeviceReopenInProgress = 0;
static int soundDeviceAutoReopenEnabled = 1;
static int soundDevicePollCounter = 0;
static wchar_t soundDefaultDeviceId[512];
static int soundDefaultDeviceIdValid = 0;
static int soundComInitialized = 0;

static HRESULT SoundEnsureCOMInitialized(void)
{
	HRESULT hr;

	if (soundComInitialized)
		return S_OK;

	hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE)
	{
		// RPC_E_CHANGED_MODE means the thread already has COM initialized in another apartment.
		// MMDevice usage still works from that initialized COM apartment, so treat it as usable.
		soundComInitialized = 1;
		return S_OK;
	}

	return hr;
}

static int SoundGetDefaultRenderDeviceId(wchar_t* outId, int outCount)
{
	IMMDeviceEnumerator* enumerator = 0;
	IMMDevice* device = 0;
	LPWSTR deviceId = 0;
	HRESULT hr;

	if (!outId || outCount <= 0)
		return 0;
	outId[0] = 0;

	if (FAILED(SoundEnsureCOMInitialized()))
		return 0;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER,
		__uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (FAILED(hr) || !enumerator)
		return 0;

	hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
	if (SUCCEEDED(hr) && device)
		hr = device->GetId(&deviceId);

	if (SUCCEEDED(hr) && deviceId)
	{
		wcsncpy(outId, deviceId, outCount - 1);
		outId[outCount - 1] = 0;
	}

	if (deviceId)
		CoTaskMemFree(deviceId);
	if (device)
		device->Release();
	if (enumerator)
		enumerator->Release();

	return (SUCCEEDED(hr) && outId[0]) ? 1 : 0;
}

static void SoundRememberCurrentDefaultDevice(void)
{
	wchar_t id[512];

	if (!SoundGetDefaultRenderDeviceId(id, sizeof(id) / sizeof(id[0])))
		return;

	wcsncpy(soundDefaultDeviceId, id, sizeof(soundDefaultDeviceId) / sizeof(soundDefaultDeviceId[0]) - 1);
	soundDefaultDeviceId[sizeof(soundDefaultDeviceId) / sizeof(soundDefaultDeviceId[0]) - 1] = 0;
	soundDefaultDeviceIdValid = 1;
}

static int SoundDefaultDeviceChanged(void)
{
	wchar_t id[512];

	if (!SoundGetDefaultRenderDeviceId(id, sizeof(id) / sizeof(id[0])))
		return 0;

	if (!soundDefaultDeviceIdValid)
	{
		wcsncpy(soundDefaultDeviceId, id, sizeof(soundDefaultDeviceId) / sizeof(soundDefaultDeviceId[0]) - 1);
		soundDefaultDeviceId[sizeof(soundDefaultDeviceId) / sizeof(soundDefaultDeviceId[0]) - 1] = 0;
		soundDefaultDeviceIdValid = 1;
		return 0;
	}

	if (wcscmp(soundDefaultDeviceId, id))
	{
		wcsncpy(soundDefaultDeviceId, id, sizeof(soundDefaultDeviceId) / sizeof(soundDefaultDeviceId[0]) - 1);
		soundDefaultDeviceId[sizeof(soundDefaultDeviceId) / sizeof(soundDefaultDeviceId[0]) - 1] = 0;
		return 1;
	}

	return 0;
}

void RequestSoundDeviceReopen(void)
{
	if (!soundDeviceAutoReopenEnabled || soundDeviceReopenInProgress || !soundo)
		return;

	soundDeviceReopenInProgress = 1;
	TrashSoundNow();
	soundo = InitSound();
	soundDeviceReopenInProgress = 0;
}

void CheckSoundDeviceChange(void)
{
	if (!soundDeviceAutoReopenEnabled || soundDeviceReopenInProgress || !soundo)
		return;

	// Poll only about once per second.  This catches switching the default output
	// device in Windows settings as well as plugging/unplugging headphones.
	if (++soundDevicePollCounter < 60)
		return;
	soundDevicePollCounter = 0;

	if (SoundDefaultDeviceChanged())
		RequestSoundDeviceReopen();
}

static bool trashPending = false;

void TrashSound() {
	trashPending = true;
}

void DoTrashSound() {
	if (dsout) delete dsout;
	if (player) delete player;
	if (player8) delete player8;
	dsout = 0;
	player = 0;
	player8 = 0;
	trashPending = false;
}


void TrashSoundNow() {
	DoTrashSound();
	//is this safe?
}



bool CheckTrashSound() {
	if (trashPending)
	{
		DoTrashSound();
		return true;
	}
	else return false;
}

void win_Throttle() {
	CheckSoundDeviceChange();
	if (CheckTrashSound()) return;
	if (player)
		player->throttle();
}

static bool killsound;
void win_SoundInit(int bits) {
	killsound = false;
	dsout = new OAKRA_Module_OutputDS();
	if (soundoptions & SO_GFOCUS)
		dsout->start(0);
	else
		dsout->start(hAppWnd);

	dsout->beginThread();
	OAKRA_Format fmt;
	fmt.format = bits == 8 ? OAKRA_U8 : OAKRA_S16;
	fmt.channels = 1;
	fmt.rate = soundrate;
	fmt.size = OAKRA_Module::calcSize(fmt);
	OAKRA_Voice* voice = dsout->getVoice(fmt);
	if (!voice)
	{
		killsound = true;
		FCEUD_PrintError("Couldn't initialize sound buffers. Sound disabled");
	}

	player = new Player();
	player8 = new Player8(player);

	if (voice)
	{
		dsout->lock();
		if (bits == 8) voice->setSource(player8);
		else voice->setSource(player);
		dsout->unlock();
	}
}


int InitSound() {
	bits = 8;

	if (!(soundoptions & SO_FORCE8BIT))
	{
		//no modern system should have this problem, and we dont use primary buffer
		/*if( (!(dscaps.dwFlags&DSCAPS_PRIMARY16BIT) && !(soundoptions&SO_SECONDARY)) ||
		(!(dscaps.dwFlags&DSCAPS_SECONDARY16BIT) && (soundoptions&SO_SECONDARY)))
		FCEUD_PrintError("DirectSound: 16-bit sound is not supported.  Forcing 8-bit sound.");*/

		//if(dscaps.dwFlags&DSCAPS_SECONDARY16BIT)
		bits = 16;
		//else 
		//	FCEUD_PrintError("DirectSound: 16-bit sound is not supported.  Forcing 8-bit sound.")
	}

	win_SoundInit(bits);
	if (killsound)
		TrashSound();

	ApplyExpansionVolumeSettings();
	SoundRememberCurrentDefaultDevice();
	FCEUI_Sound(soundrate);
	return 1;
}


void win_SoundSetScale(int scale) {
	CheckSoundDeviceChange();
	if (CheckTrashSound()) return;
	if (player)
		player->scale = scale;
}

void win_SoundWriteData(int32* buffer, int count) {
	//mbg 8/30/07 - this used to be done here, but now its gtting called from somewhere else...
	//FCEUI_AviSoundUpdate((void*)MBuffer, Count);
	CheckSoundDeviceChange();
	if (CheckTrashSound()) return;
	void* tempbuf = alloca(2 * count);
	short* sbuf = (short*)tempbuf;
	for (int i = 0; i < count; i++)
		sbuf[i] = buffer[i];
	if (player)
		player->receive(count * 2, tempbuf);
}


//--------
//GUI and control APIs

HWND uug = 0;

static void UpdateSD(HWND hwndDlg)
{
	int t;

	CheckDlgButton(hwndDlg, CHECK_SOUND_ENABLED, soundo ? BST_CHECKED : BST_UNCHECKED);
	// The option formerly flagged by SO_SECONDARY can no longer be disabled.
	// CheckDlgButton(hwndDlg,123,(soundoptions&SO_SECONDARY)?BST_CHECKED:BST_UNCHECKED);
	CheckDlgButton(hwndDlg, CHECK_SOUND_GLOBAL_FOCUS, (soundoptions & SO_GFOCUS) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, CHECK_SOUND_MUTEFA, (soundoptions & SO_MUTEFA) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, CHECK_SOUND_MUTETURBO, (muteTurbo) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, CHECK_SOUND_SWAPDUTY, (swapDuty) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, CHECK_SOUND_LINEARMIXER, (linearMixer) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, CHECK_SOUND_REVERSEDPCM, (reverseDPCM) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwndDlg, CHECK_SOUND_NOTRESETPHASE, (notResetPhase) ? BST_CHECKED : BST_UNCHECKED);
	// The option formerly flagged by SO_OLDUP can no longer be enabled.
	// CheckDlgButton(hwndDlg,131,(soundoptions&SO_OLDUP)?BST_CHECKED:BST_UNCHECKED);
	SendDlgItemMessage(hwndDlg, COMBO_SOUND_QUALITY, CB_SETCURSEL, soundquality, (LPARAM)(LPSTR)0);
	t = 0;
	if (soundrate == 22050) t = 1;
	else if (soundrate == 44100) t = 2;
	else if (soundrate == 48000) t = 3;
	else if (soundrate == 96000) t = 4;
	SendDlgItemMessage(hwndDlg, COMBO_SOUND_RATE, CB_SETCURSEL, t, (LPARAM)(LPSTR)0);
	t = 0;
	if (soundoptions & SO_FORCE8BIT) t = 1;
	SendDlgItemMessage(hwndDlg, COMBO_SOUND_8BIT, CB_SETCURSEL, t, (LPARAM)(LPSTR)0);

	if (!soundo)
	{
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_NOTRESETPHASE), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_REVERSEDPCM), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_LINEARMIXER), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_SWAPDUTY), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_MUTETURBO), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_MUTEFA), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, COMBO_SOUND_QUALITY), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, COMBO_SOUND_RATE), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, COMBO_SOUND_8BIT), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_LATENCY_TRACKBAR), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_PCM), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_FDS), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_N163), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_S5B), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_SOUND_RESTOREAPUDEFAULTVOL), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_SOUND_RESTOREEXPDEFAULTVOL), FALSE);
		EnableSoundVolumeEditControls(hwndDlg, FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 124), FALSE);
		//Slider group boxes
		EnableWindow(GetDlgItem(hwndDlg, 125), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 131), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 132), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 133), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 134), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 135), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_VRC6), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_VRC7), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_FDS), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_MMC5), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_N163), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_S5B), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_EXP_VOLUME_GROUP), FALSE);
		//Static text boxes in volume group box
		EnableWindow(GetDlgItem(hwndDlg, ID_SOUND_QUALITYNOTIFY), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, ID_SOUND_TRITOP), FALSE);
		//Volume group box
		EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUMEGROUP), FALSE);
		//Buffer group box
		EnableWindow(GetDlgItem(hwndDlg, 127), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 65459), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 65456), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 65458), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 65457), FALSE);
		//Misc. Output Format group
		EnableWindow(GetDlgItem(hwndDlg, 65455), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 65462), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 65461), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 65460), FALSE);

	}
	else
	{
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_NOTRESETPHASE), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_REVERSEDPCM), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_LINEARMIXER), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_SWAPDUTY), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_MUTETURBO), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CHECK_SOUND_MUTEFA), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, COMBO_SOUND_QUALITY), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, COMBO_SOUND_RATE), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, COMBO_SOUND_8BIT), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_LATENCY_TRACKBAR), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_PCM), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_FDS), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_N163), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_S5B), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_SOUND_RESTOREAPUDEFAULTVOL), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_SOUND_RESTOREEXPDEFAULTVOL), TRUE);
		EnableSoundVolumeEditControls(hwndDlg, TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 124), TRUE);
		//Slider group boxes
		EnableWindow(GetDlgItem(hwndDlg, 125), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 131), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 132), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 133), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 134), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 135), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_VRC6), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_VRC7), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_FDS), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_MMC5), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_N163), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_GROUP_SOUND_S5B), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_EXP_VOLUME_GROUP), TRUE);
		//Static text boxes in volume group box
		EnableWindow(GetDlgItem(hwndDlg, ID_SOUND_QUALITYNOTIFY), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, ID_SOUND_TRITOP), TRUE);
		//Volume group box
		EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUMEGROUP), TRUE);
		//Buffer group box
		EnableWindow(GetDlgItem(hwndDlg, 127), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 65459), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 65456), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 65458), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 65457), TRUE);
		//Misc. Output Format group
		EnableWindow(GetDlgItem(hwndDlg, 65455), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 65462), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 65461), TRUE);
		EnableWindow(GetDlgItem(hwndDlg, 65460), TRUE);

	}

	UpdateSoundChannelQualityMode(hwndDlg);
}

INT_PTR CALLBACK SoundConCallB(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{

	switch (uMsg) {
	case WM_NCRBUTTONDOWN:
	case WM_NCMBUTTONDOWN:
	case WM_NCLBUTTONDOWN:break;

	case WM_INITDIALOG:
		//Volume Trackbars--------------------------------------------------------------
		//Master
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR, TBM_SETRANGE, 1, MAKELONG(0, 150));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR, TBM_SETTICFREQ, 25, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR, TBM_SETPOS, 1, soundvolume);
		//Triangle
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_TRIANGLE, TBM_SETPOS, 1, soundTrianglevol);
		//Square1
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE1, TBM_SETPOS, 1, soundSquare1vol);

		//Square2
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2, TBM_SETPOS, 1, soundSquare2vol);

		//Noise
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE, TBM_SETPOS, 1, soundNoisevol);

		//PCM
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_PCM, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_PCM, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_PCM, TBM_SETPOS, 1, soundPCMvol);

		//Expansion chips
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC6, TBM_SETPOS, 1, soundVRC6vol);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_VRC7, TBM_SETPOS, 1, soundVRC7vol);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_FDS, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_FDS, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_FDS, TBM_SETPOS, 1, soundFDSvol);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_MMC5, TBM_SETPOS, 1, soundMMC5vol);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_N163, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_N163, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_N163, TBM_SETPOS, 1, soundN163vol);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_S5B, TBM_SETRANGE, 1, MAKELONG(0, 256));
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_S5B, TBM_SETTICFREQ, 32, 0);
		SendDlgItemMessage(hwndDlg, CTL_VOLUME_TRACKBAR_S5B, TBM_SETPOS, 1, soundS5Bvol);
		UpdateSoundVolumeEditBoxes(hwndDlg);
		ApplyExpansionVolumeSettings();

		/* buffer size time trackbar */
		SendDlgItemMessage(hwndDlg, CTL_LATENCY_TRACKBAR, TBM_SETRANGE, 1, MAKELONG(15, 200));
		SendDlgItemMessage(hwndDlg, CTL_LATENCY_TRACKBAR, TBM_SETTICFREQ, 1, 0);
		SendDlgItemMessage(hwndDlg, CTL_LATENCY_TRACKBAR, TBM_SETPOS, 1, soundbuftime);

		{
			char tbuf[8];
			sprintf(tbuf, "%d", soundbuftime);
			SetDlgItemText(hwndDlg, 666, (LPTSTR)tbuf);
		}

		SendDlgItemMessage(hwndDlg, COMBO_SOUND_QUALITY, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"Low");
		SendDlgItemMessage(hwndDlg, COMBO_SOUND_QUALITY, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"High");
		SendDlgItemMessage(hwndDlg, COMBO_SOUND_QUALITY, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"Highest");

		SendDlgItemMessage(hwndDlg, COMBO_SOUND_RATE, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"11025");
		SendDlgItemMessage(hwndDlg, COMBO_SOUND_RATE, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"22050");
		SendDlgItemMessage(hwndDlg, COMBO_SOUND_RATE, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"44100");
		SendDlgItemMessage(hwndDlg, COMBO_SOUND_RATE, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"48000");
		SendDlgItemMessage(hwndDlg, COMBO_SOUND_RATE, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"96000");

		SendDlgItemMessage(hwndDlg, COMBO_SOUND_8BIT, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"16-Bit");
		SendDlgItemMessage(hwndDlg, COMBO_SOUND_8BIT, CB_ADDSTRING, 0, (LPARAM)(LPSTR)"8-Bit");

		UpdateSD(hwndDlg);
		break;
	case WM_VSCROLL:
	{
		if (LOWORD(wParam) == SB_ENDSCROLL)
			return true;

		if (HandleSoundVolumeScroll(hwndDlg, (HWND)lParam))
			return true;

		return true;
	}
	case WM_HSCROLL:
	{
		if (LOWORD(wParam) == SB_ENDSCROLL)
			return true;

		if (HandleSoundVolumeScroll(hwndDlg, (HWND)lParam))
			return true;

		if (GetDlgItem(hwndDlg, CTL_LATENCY_TRACKBAR) == (HWND)lParam)
		{
			char tbuf[8];
			soundbuftime = SendDlgItemMessage(hwndDlg, CTL_LATENCY_TRACKBAR, TBM_GETPOS, 0, 0);
			sprintf(tbuf, "%d", soundbuftime);
			SetDlgItemText(hwndDlg, 666, (LPTSTR)tbuf);
			//soundbufsize=(soundbuftime*soundrate/1000);
		}
	}
	break;
	case WM_CLOSE:
	case WM_QUIT: goto gornk;
	case WM_COMMAND:
		switch (HIWORD(wParam))
		{
		case CBN_SELENDOK:
			switch (LOWORD(wParam))
			{
			case COMBO_SOUND_RATE:
			{
				int tmp;
				tmp = SendDlgItemMessage(hwndDlg, COMBO_SOUND_RATE, CB_GETCURSEL, 0, (LPARAM)(LPSTR)0);
				if (tmp == 0) tmp = 11025;
				else if (tmp == 1) tmp = 22050;
				else if (tmp == 2) tmp = 44100;
				else if (tmp == 3) tmp = 48000;
				else tmp = 96000;
				if (tmp != soundrate)
				{
					soundrate = tmp;
					if (soundrate < 44100)
					{
						soundquality = 0;
						if (!turbo) FCEUI_SetSoundQuality(0);	///If turbo is running, don't do this call, turbo will handle it instead
						UpdateSD(hwndDlg);
					}
					if (soundo)
					{
						TrashSoundNow();
						soundo = InitSound();
						UpdateSD(hwndDlg);
					}
				}
			}
			break;

			case COMBO_SOUND_QUALITY:
				soundquality = SendDlgItemMessage(hwndDlg, COMBO_SOUND_QUALITY, CB_GETCURSEL, 0, (LPARAM)(LPSTR)0);
				if (soundrate < 44100) soundquality = 0;
				if (!turbo) FCEUI_SetSoundQuality(soundquality); //If turbo is running, don't do this call, turbo will handle it instead
				UpdateSD(hwndDlg);
				break;

			case COMBO_SOUND_8BIT:
			{
				int tmp = SendDlgItemMessage(hwndDlg, COMBO_SOUND_8BIT, CB_GETCURSEL, 0, (LPARAM)(LPSTR)0);
				soundoptions &= ~SO_FORCE8BIT;
				if (tmp == 1) soundoptions |= SO_FORCE8BIT;
				if (soundo)
				{
					TrashSoundNow();
					soundo = InitSound();
					UpdateSD(hwndDlg);
				}
			}
			break;
			}
			break;

		case EN_CHANGE:
			HandleSoundVolumeEditChange(hwndDlg, LOWORD(wParam));
			break;

		case BN_CLICKED:
			switch (LOWORD(wParam))
			{
				// The option formerly flagged by SO_SECONDARY can no longer be disabled.
#if 0
			case 123:soundoptions ^= SO_SECONDARY;
				if (soundo)
				{
					TrashSoundNow();
					soundo = InitSound();
					UpdateSD(hwndDlg);
				}
				break;
#endif
			case CHECK_SOUND_GLOBAL_FOCUS:soundoptions ^= SO_GFOCUS;
				if (soundo)
				{
					TrashSoundNow();
					soundo = InitSound();
					UpdateSD(hwndDlg);
				}
				break;
			case CHECK_SOUND_MUTEFA:soundoptions ^= SO_MUTEFA;
				break;
				// The option formerly flagged by SO_OLDUP can no longer be enabled.
#if 0
			case 131:soundoptions ^= SO_OLDUP;
				if (soundo)
				{
					TrashSoundNow();
					soundo = InitSound();
					UpdateSD(hwndDlg);
				}
				break;
#endif
			case CHECK_SOUND_MUTETURBO:
				muteTurbo ^= 1;
				break;
			case CHECK_SOUND_SWAPDUTY:
				swapDuty ^= 1;
				break;
			case CHECK_SOUND_LINEARMIXER:
				linearMixer ^= 1;
				break;
			case CHECK_SOUND_REVERSEDPCM:
				reverseDPCM ^= 1;
				break;
			case CHECK_SOUND_NOTRESETPHASE:
				notResetPhase ^= 1;
				break;
			case CHECK_SOUND_ENABLED:soundo = !soundo;
				if (!soundo) TrashSound();
				else soundo = InitSound();
				UpdateSD(hwndDlg);
				break;
			case IDC_SOUND_RESTOREAPUDEFAULTVOL:
				//Restore APU default values
				soundvolume = 150;
				soundTrianglevol = 256;
				soundSquare1vol = 256;
				soundSquare2vol = 256;
				soundNoisevol = 256;
				soundPCMvol = 256;
				//Update APU trackbars and edit boxes
				UpdateAPUVolumeTrackbars(hwndDlg);
				UpdateSoundVolumeEditBoxes(hwndDlg);

				//Set APU sound volumes
				FCEUI_SetSoundVolume(soundvolume);
				FCEUI_SetTriangleVolume(soundTrianglevol);
				FCEUI_SetSquare1Volume(soundSquare1vol);
				FCEUI_SetSquare2Volume(soundSquare2vol);
				FCEUI_SetNoiseVolume(soundNoisevol);
				FCEUI_SetPCMVolume(soundPCMvol);
				break;
			case IDC_SOUND_RESTOREEXPDEFAULTVOL:
				//Restore expansion chip default values
				soundVRC6vol = 256;
				soundVRC7vol = 256;
				soundFDSvol = 256;
				soundMMC5vol = 256;
				soundN163vol = 256;
				soundS5Bvol = 256;
				//Update expansion chip trackbars and edit boxes
				UpdateExpansionVolumeTrackbars(hwndDlg);
				UpdateSoundVolumeEditBoxes(hwndDlg);
				ApplyExpansionVolumeSettings();
				break;
			}
		}

		if (!(wParam >> 16))
			switch (wParam & 0xFFFF)
			{
			case BTN_CLOSE:
			gornk:
				DestroyWindow(hwndDlg);
				uug = 0;
				break;
			}
	}
	return 0;
}

void UpdateSoundChannelQualityMode(HWND hwndDlg)
{
	//If high quality, enable all
	//If low quality, we only have two sliders, sq1 and triangle, rename them and disable the others

	if (soundquality)	//If high or highest
	{
		//Enable sliders & corresponding group boxes
		if (soundo)
		{
			EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_PCM), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_SQUARE2), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_NOISE), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_PCM), TRUE);
			//Enable group boxes
			EnableWindow(GetDlgItem(hwndDlg, 133), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, 134), TRUE);
			EnableWindow(GetDlgItem(hwndDlg, 135), TRUE);
		}
		//Set text for group boxes
		SetDlgItemText(hwndDlg, ID_SOUND_TRITOP, "");	//Hacky, a static text box above the group box so I have more text space
		SetDlgItemText(hwndDlg, 131, "Triangle");
		SetDlgItemText(hwndDlg, 132, "Square 1");
		SetDlgItemText(hwndDlg, 133, "Square 2");
		SetDlgItemText(hwndDlg, 134, "Noise");
		SetDlgItemText(hwndDlg, 135, "PCM");
		//Set quality message off
		SetDlgItemText(hwndDlg, ID_SOUND_QUALITYNOTIFY, "");
	}
	else				//If low
	{
		//Disable sliders
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_SQUARE2), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_NOISE), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, CTL_VOLUME_TRACKBAR_PCM), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_SQUARE2), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_NOISE), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_VOLUME_EDIT_PCM), FALSE);
		//Disable group boxes
		EnableWindow(GetDlgItem(hwndDlg, 133), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 134), FALSE);
		EnableWindow(GetDlgItem(hwndDlg, 135), FALSE);
		//Set text for group boxes
			//These disabled until there is some control for pcm & triangle in low quality setting
			//SetDlgItemText(hwndDlg, ID_SOUND_TRITOP, "Triangle/");	//Hacky, a static text box above the group box so I have more text space
			//SetDlgItemText(hwndDlg, 131, "noise/pcm");
		SetDlgItemText(hwndDlg, 131, "Noise");
		SetDlgItemText(hwndDlg, 132, "Square");
		SetDlgItemText(hwndDlg, 133, "Disabled");	//Set Square 2 to disabled
		SetDlgItemText(hwndDlg, 134, "Disabled");	//Set Noise to disabled
		SetDlgItemText(hwndDlg, 135, "Disabled");	//Set PCM to disabled
		//Set quality message on
		SetDlgItemText(hwndDlg, ID_SOUND_QUALITYNOTIFY, "(To enable these, use a higher quality setting)");
	}

	return;
}



/// Shows the sounds configuration dialog.
void ConfigSound()
{
	if (!uug)
	{
		uug = CreateDialog(fceu_hInstance, "SOUNDCONFIG", 0, SoundConCallB);
	}
	else
	{
		SetFocus(uug);
	}
}

void FCEUD_SoundToggle(void)
{
	if (mute)
	{
		mute = false;
		FCEU_DispMessage("Sound unmuted", 0);
	}
	else
	{
		mute = true;
		FCEU_DispMessage("Sound muted", 0);
	}
}

void FCEUD_SoundVolumeAdjust(int n)
{
	switch (n)
	{
	case -1:	soundvolume -= 10; if (soundvolume < 0) soundvolume = 0; break;
	case 0:		soundvolume = 100; break;
	case 1:		soundvolume += 10; if (soundvolume > 150) soundvolume = 150; break;
	}
	mute = false;
	FCEUI_SetSoundVolume(soundvolume);
	FCEU_DispMessage("Sound volume %d.", 0, soundvolume);
}
