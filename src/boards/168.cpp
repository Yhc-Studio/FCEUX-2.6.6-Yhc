/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 168
 *
 * Submapper 0:
 *   Original RacerMate Challenge II implementation used by FCEUX.
 *
 * Submapper 1 (private compatibility assignment):
 *   VirtuaNES/VirtuaNESex "Mapper 168" compatibility mode for the
 *   Subor Karaoke / UNL-DANCE2000 family.
 *
 * NOTE:
 *   The Subor boards have since received official NES 2.0 mapper
 *   numbers 514 and 518.  Submapper 1 here is intentionally kept as
 *   a private compatibility alias, as requested.
 *
 * This file implements:
 *   - Original mapper 168 behavior unchanged for submapper 0
 *   - CRC-based VirtuaNES variant detection under submapper 1
 *   - PRG-ROM 16/32 KiB banking
 *   - 8 KiB WRAM at $6000-$7FFF
 *   - 128 KiB secondary PRG-RAM banking used by the SB97 mode
 *   - 8 KiB CHR-RAM
 *   - Mapper 518 CIRAM-A10-dependent background CHR selection
 *   - Horizontal/vertical mirroring
 *   - Save-state support
 *
 * REQUIRED ROM/EMULATOR SETTING:
 *   NES 2.0 header byte 12 bits 0-1 must be 3 (Dendy timing), or the
 *   emulator region must be manually set to Dendy.  Subor V freezes
 *   under NTSC timing.
 *
 * Not yet implemented:
 *   - uPD765-compatible floppy controller and disk image handling
 */

#include "mapinc.h"
#include "lpc_d6_synth.h"
#include <string.h>

 /* ------------------------------------------------------------------------- */
 /* Common state                                                              */
 /* ------------------------------------------------------------------------- */

enum {
	M168_SUB0 = 0,
	M168_SUB1 = 1
};

enum {
	M168_SUBOR_GENERIC = 0,
	M168_SUBOR_KARAOKE = 1,
	M168_SUBOR_SB97 = 2
};

enum {
	M168_CHR_CHIP = 0x10,
	M168_WRAM_CHIP = 0x11,
	M168_PRAM_CHIP = 0x12
};

enum {
	M168_LPC_SOURCE_RATE = 10000,
	M168_LPC_FIFO_SIZE = 16,
	M168_LPC_FIFO_MASK = M168_LPC_FIFO_SIZE - 1,
	M168_LPC_PCM_SIZE = 32768,
	M168_LPC_PCM_MASK = M168_LPC_PCM_SIZE - 1,
	M168_LPC_VOLUME_SHIFT = 7
};

static uint8 submapper;
static uint8 suborType;

/* Original mapper-168 register. */
static uint8 reg;

/* Submapper-1 registers. */
static uint8 reg5000;
static uint8 reg5200;
static uint8 reg5300;
static uint8 karaokeReg;
static uint8 ppuSwitch;

/* RAM areas. */
static uint8* CHRRAM = NULL;
static uint32 CHRRAMSIZE;

static uint8* WRAM = NULL;
static uint32 WRAMSIZE;

static uint8* PRAM = NULL;
static uint32 PRAMSIZE;

/*
 * Mapper 518-style CHR auto banking is implemented through PPU_hook.
 * Updating the normal FCEUX VPage mapping from the hook makes the behavior
 * available to both the classic PPU renderer and the New PPU renderer.
 */
static void (*oldPPUHook)(uint32 A) = NULL;
static void (*oldPPUWrite)(uint32 A, uint8 V) = NULL;
static uint8 chrLatch;

/*
 * FCEUX's classic PPU calls PPU_hook after it has captured the pattern
 * pointer, while the New PPU calls it before reading VPage.  This flag is
 * global in ppu.cpp but is not declared in older ppu.h revisions.
 */
extern int newppu;


/* ------------------------------------------------------------------------- */
/* Subor LPC-D6 / LPC-10 speech state                                        */
/* ------------------------------------------------------------------------- */

static void* lpcSynth = NULL;

/*
 * Hardware FIFO.  Like VirtuaNES, one slot remains empty, so the visible
 * capacity is 15 bytes and full is (write + 1) == read.
 */
static uint8 lpcFIFO[M168_LPC_FIFO_SIZE];
static uint8 lpcReadPos;
static uint8 lpcWritePos;
static uint8 lpcSpeechEnd;
static uint8 lpcDecoderEnd;

/*
 * Decoded 10 kHz mono PCM waiting for the FCEUX output-rate resampler.
 * The queue is deliberately independent from the emulated input FIFO:
 * the game may continue sending the next phrase while old audio drains.
 */
static int16 lpcPCM[M168_LPC_PCM_SIZE];
static uint32 lpcPCMReadPos;
static uint32 lpcPCMWritePos;
static uint32 lpcPCMCount;

/*
 * Number of decoded 10 kHz source samples whose emulated playback
 * time has not elapsed yet.  This is advanced from MapIRQHook, not
 * from the host audio callback, so $5300 cannot hang when the frontend
 * stops requesting audio buffers.
 */
static uint32 lpcEmuSamplesPending;
static uint32 lpcCycleRemainder;
static void (*oldMapIRQHook)(int cycles) = NULL;

/* Output-rate resampler and low-quality FCEUX sound cursor. */
static uint32 lpcOutputRate;
static uint32 lpcResamplePhase;
static int16 lpcResampleCurrent;
static int16 lpcResampleNext;
static uint8 lpcResamplePrimed;
static int32 lpcDwave;

/* ------------------------------------------------------------------------- */
/* Save-state data                                                           */
/* ------------------------------------------------------------------------- */

static SFORMAT M168StateRegs[] =
{
	{ &reg,        1, "REGS" },
	{ &reg5000,    1, "R500" },
	{ &reg5200,    1, "R520" },
	{ &reg5300,    1, "R530" },
	{ &karaokeReg, 1, "KREG" },
	{ &ppuSwitch,  1, "PPSW" },
	{ &suborType,  1, "STYP" },
	{ &chrLatch,    1, "CHRL" },
	{ 0 }
};


static SFORMAT M168LPCStateRegs[] =
{
	{ lpcFIFO,                  M168_LPC_FIFO_SIZE, "SPBF" },
	{ &lpcReadPos,              1,                   "SPRP" },
	{ &lpcWritePos,             1,                   "SPWP" },
	{ &lpcSpeechEnd,            1,                   "SPEN" },
	{ &lpcDecoderEnd,           1,                   "SPDE" },
	{ lpcPCM,                   sizeof(lpcPCM),      "SPCM" },
	{ &lpcPCMReadPos,           4 | FCEUSTATE_RLSB, "PCRP" },
	{ &lpcPCMWritePos,          4 | FCEUSTATE_RLSB, "PCWP" },
	{ &lpcPCMCount,             4 | FCEUSTATE_RLSB, "PCCN" },
	{ &lpcEmuSamplesPending,    4 | FCEUSTATE_RLSB, "EPND" },
	{ &lpcCycleRemainder,       4 | FCEUSTATE_RLSB, "ECYR" },
	{ &lpcResamplePhase,        4 | FCEUSTATE_RLSB, "RSPH" },
	{ &lpcResampleCurrent,      2 | FCEUSTATE_RLSB, "RSCU" },
	{ &lpcResampleNext,         2 | FCEUSTATE_RLSB, "RSNX" },
	{ &lpcResamplePrimed,       1,                   "RSPR" },
	{ &lpcDwave,                4 | FCEUSTATE_RLSB, "LDWV" },
	{ 0 }
};

/* ------------------------------------------------------------------------- */
/* Submapper 0: original FCEUX Mapper 168                                    */
/* ------------------------------------------------------------------------- */

static void M168Sub0Sync(void)
{
	setchr4r(M168_CHR_CHIP, 0x0000, 0);
	setchr4r(M168_CHR_CHIP, 0x1000, reg & 0x0F);
	setprg16(0x8000, reg >> 6);
	setprg16(0xC000, ~0);
}

static DECLFW(M168Sub0Write)
{
	reg = V;
	M168Sub0Sync();
}

static DECLFW(M168Dummy)
{
}

/* ------------------------------------------------------------------------- */
/* Submapper 1: VirtuaNES-compatible Subor mode                              */
/* ------------------------------------------------------------------------- */

static void M168SetLowerCHRBank(uint8 bank)
{
	bank &= 1;

	if (chrLatch != bank) {
		chrLatch = bank;
		setchr4r(M168_CHR_CHIP, 0x0000, chrLatch);
	}
}

static uint8 M168GetMirrorMode(void)
{
	/*
	 * Mode bit M:
	 *   0 = horizontal nametable arrangement / vertical mirroring
	 *       CIRAM A10 follows PPU A10
	 *
	 *   1 = vertical nametable arrangement / horizontal mirroring
	 *       CIRAM A10 follows PPU A11
	 */
	if (suborType == M168_SUBOR_KARAOKE)
		return (karaokeReg >> 6) & 1;

	return reg5200 & 1;
}

static uint8 M168GetCIRAMA10(uint32 A)
{
	/*
	 * The mapper latches CIRAM A10, not one fixed raw PPU address line.
	 *
	 * Vertical mirroring:   CIRAM A10 = PPU A10
	 * Horizontal mirroring: CIRAM A10 = PPU A11
	 */
	return (A >> (10 + M168GetMirrorMode())) & 1;
}

static void M168SyncCHR(void)
{
	/*
	 * Physical CHR-RAM mapping seen by CPU $2007 writes:
	 *
	 *   $0000-$0FFF = physical page 0
	 *   $1000-$1FFF = physical page 1
	 *
	 * During background reads from the lower pattern table, the mapper
	 * temporarily substitutes CHR A12 with the latched CIRAM A10.
	 */
	chrLatch = 0;
	setchr4r(M168_CHR_CHIP, 0x0000, 0);
	setchr4r(M168_CHR_CHIP, 0x1000, 1);
}

static void M168Sub1Sync(void)
{
	/* Cartridge work RAM. */
	setprg8r(M168_WRAM_CHIP, 0x6000, 0);

	if (suborType == M168_SUBOR_KARAOKE) {
		/*
		 * VirtuaNES CRC 0x0A9808AE:
		 *   D0-D4 = 32 KiB PRG bank
		 *   D6    = mirroring
		 *   D6-D7 = alternate background CHR selection enable
		 */
		setprg32(0x8000, karaokeReg & 0x1F);
		setmirror((karaokeReg & 0x40) ? MI_H : MI_V);
		ppuSwitch = (karaokeReg & 0xC0) ? 1 : 0;
	}
	else {
		/*
		 * $5000:
		 *   bit 7 clear: PRG-ROM mode
		 *   bit 7 set:   secondary PRG-RAM mode
		 *
		 * $5200 bit 2:
		 *   clear: 16 KiB at $8000 and fixed ROM bank 0 at $C000
		 *   set:   32 KiB at $8000
		 */
		if (reg5000 & 0x80) {
			if (reg5200 & 0x04) {
				setprg32r(M168_PRAM_CHIP, 0x8000, reg5000 & 0x03);
			}
			else {
				setprg16r(M168_PRAM_CHIP, 0x8000, reg5000 & 0x07);
				setprg16(0xC000, 0);
			}
		}
		else {
			if (reg5200 & 0x04) {
				setprg32(0x8000, reg5000);
			}
			else {
				setprg16(0x8000, reg5000);
				setprg16(0xC000, 0);
			}
		}

		setmirror((reg5200 & 0x01) ? MI_H : MI_V);
		ppuSwitch = (reg5200 & 0x02) ? 1 : 0;
	}

	/*
	 * Do not leave a rendering-selected page active after register writes.
	 * CPU $2007 writes must always see the physical 0/1 page arrangement.
	 */
	M168SyncCHR();
}

/*
 * Mapper 518 background CHR latch.
 *
 * FCEUX's classic renderer calls this once with the nametable address
 * before obtaining the pattern pointer.  Selecting the lower 4 KiB page
 * here therefore affects the following tile fetch.
 *
 * The selected page is CIRAM A10:
 *
 *   vertical mirroring   -> use PPU A10 (left/right nametable)
 *   horizontal mirroring -> use PPU A11 (top/bottom nametable)
 *
 * This is why an A11-only implementation can fix a vertically arranged
 * Staff page but corrupt a horizontally arranged menu halfway across the
 * screen.
 */
static void M168Sub1PPUHook(uint32 A)
{
	if (oldPPUHook && oldPPUHook != M168Sub1PPUHook)
		oldPPUHook(A);

	A &= 0x3FFF;

	if (!ppuSwitch) {
		M168SetLowerCHRBank(0);
		return;
	}

	if ((A & 0x3000) == 0x2000) {
		M168SetLowerCHRBank(M168GetCIRAMA10(A));
		return;
	}

	/*
	 * In the classic PPU, pputile.inc has already captured C=VRAMADR(vadr)
	 * before it calls PPU_hook(vadr).  It is therefore safe—and necessary—
	 * to restore the physical lower page here so later CPU $2007 writes do
	 * not accidentally modify CHR page 1.
	 *
	 * The New PPU calls the hook before reading VPage, so it must retain the
	 * selected page until that read completes.
	 */
	if (!newppu && A < 0x1000)
		M168SetLowerCHRBank(0);
}

/*
 * New-PPU CHR writes must ignore the rendering latch.  Mapper 518 only
 * substitutes CHR A12 during reads from $0000-$0FFF; writes retain their
 * physical A12-selected destination.
 */
static void M168Sub1PPUWrite(uint32 A, uint8 V)
{
	A &= 0x3FFF;

	if (A < 0x2000) {
		CHRRAM[A & 0x1FFF] = V;
		return;
	}

	if (oldPPUWrite)
		oldPPUWrite(A, V);
	else
		FFCEUX_PPUWrite_Default(A, V);
}

static DECLFW(M168Sub1Write5000)
{
	reg5000 = V;
	M168Sub1Sync();
}

static DECLFW(M168Sub1Write5200)
{
	reg5200 = V;
	M168Sub1Sync();
}

/*
 * LPC decoder input callback.
 *
 * LPC_CMD_NONE means that the emulated FIFO is temporarily empty.  The
 * modified decoder rolls its complete state back and returns
 * LPC_RESULT_NEED_DATA, so no partial LPC frame is lost.
 */
static int M168LPCFeed(void* host, unsigned char* food)
{
	(void)host;

	if (lpcReadPos == lpcWritePos)
		return LPC_CMD_NONE;

	*food = lpcFIFO[lpcReadPos];
	lpcReadPos = (lpcReadPos + 1) & M168_LPC_FIFO_MASK;
	return LPC_CMD_PAYLOAD;
}

static int M168LPCFIFOFull(void)
{
	return (((lpcWritePos + 1) & M168_LPC_FIFO_MASK) == lpcReadPos);
}

static void M168LPCUpdateEndStatus(void)
{
	/*
	 * Decoder EOS only means that all LPC frames have been converted to
	 * PCM.  The hardware-visible end flag is raised only after the same
	 * amount of emulated 10 kHz playback time has elapsed.
	 */
	lpcSpeechEnd =
		(lpcDecoderEnd && !lpcEmuSamplesPending) ? 1 : 0;
}

static uint8 M168LPCGetStatus(void)
{
	M168LPCUpdateEndStatus();

	if (lpcSpeechEnd)
		return 0x8F;

	if (M168LPCFIFOFull())
		return 0x00;

	return 0x80;
}

static uint32 M168LPCPCMFree(void)
{
	return M168_LPC_PCM_SIZE - lpcPCMCount;
}

static void M168LPCPushPCM(const int16* pcm, int count)
{
	int i;

	for (i = 0; i < count && lpcPCMCount < M168_LPC_PCM_SIZE; i++) {
		lpcPCM[lpcPCMWritePos] = pcm[i];
		lpcPCMWritePos = (lpcPCMWritePos + 1) & M168_LPC_PCM_MASK;
		lpcPCMCount++;
	}
}


static uint32 M168LPCCPUFrequency(void)
{
	/*
	 * Integer approximations are more than sufficient for a 10 kHz speech
	 * device.  Mapper 518 software normally runs with Dendy timing.
	 */
	if (dendy)
		return 1773448;

	if (PAL)
		return 1662607;

	return 1789773;
}

static void M168LPCClock(int cycles)
{
	uint64 total;
	uint32 elapsed;
	uint32 cpuFrequency;

	if (oldMapIRQHook && oldMapIRQHook != M168LPCClock)
		oldMapIRQHook(cycles);

	if (cycles <= 0 || !lpcEmuSamplesPending) {
		M168LPCUpdateEndStatus();
		return;
	}

	cpuFrequency = M168LPCCPUFrequency();
	total = (uint64)lpcCycleRemainder +
		(uint64)(uint32)cycles * M168_LPC_SOURCE_RATE;

	elapsed = (uint32)(total / cpuFrequency);
	lpcCycleRemainder = (uint32)(total % cpuFrequency);

	if (elapsed >= lpcEmuSamplesPending)
		lpcEmuSamplesPending = 0;
	else
		lpcEmuSamplesPending -= elapsed;

	M168LPCUpdateEndStatus();
	reg5300 = M168LPCGetStatus();
}

static void M168LPCInstallClock(void)
{
	if (MapIRQHook != M168LPCClock)
		oldMapIRQHook = MapIRQHook;

	MapIRQHook = M168LPCClock;
}

static void M168LPCFlushOutput(void)
{
	lpcPCMReadPos = 0;
	lpcPCMWritePos = 0;
	lpcPCMCount = 0;
	lpcEmuSamplesPending = 0;
	lpcCycleRemainder = 0;

	lpcResamplePhase = 0;
	lpcResampleCurrent = 0;
	lpcResampleNext = 0;
	lpcResamplePrimed = 0;
	lpcDwave = 0;
}

/*
 * Decode every complete frame currently available in the emulated FIFO.
 *
 * With sound disabled the PCM is intentionally discarded, but the decoder
 * and $5300 status continue to advance.  This is essential: several Subor
 * programs wait for end-of-speech before entering the next screen.
 */
static void M168LPCDecodeAvailable(void)
{
	int guard;

	/*
	 * Once the end frame has been decoded, the chip waits for the CPU to
	 * observe energy $F and write the mandatory $0F terminator.  Do not
	 * consume any following byte as though it were a new stream header.
	 */
	if (lpcDecoderEnd) {
		M168LPCUpdateEndStatus();
		reg5300 = M168LPCGetStatus();
		return;
	}

	if (!lpcSynth)
		lpcSynth = lpc_d6_synth_new(M168LPCFeed, NULL,
			LPC_STD_VARIANT_Subor);

	if (!lpcSynth)
		return;

	for (guard = 0; guard < 128; guard++) {
		int result;
		int pcmSize = 0;
		int restart = 0;
		uint8 savedReadPos = lpcReadPos;
		int16 pcm[200];

		if (FSettings.SndRate && M168LPCPCMFree() < 200)
			break;

		result = lpc_d6_synth_do(lpcSynth, pcm, &pcmSize, &restart);

		if (result == LPC_RESULT_NEED_DATA) {
			/* The decoder restored itself; restore the external FIFO too. */
			lpcReadPos = savedReadPos;
			break;
		}

		if (result == LPC_RESULT_ERROR)
			break;

		if (pcmSize > 0) {
			/* Speech completion follows emulated time, not host buffering. */
			lpcEmuSamplesPending += (uint32)pcmSize;

			if (FSettings.SndRate)
				M168LPCPushPCM(pcm, pcmSize);
		}

		if (restart) {
			/*
			 * CPU byte $0F is bit-reversed by the chip and appears to the
			 * decoder as the $F0 restart command.  The byte has already been
			 * consumed; do not reinterpret it as the next codec header.
			 */
			lpcDecoderEnd = 0;
			lpcSpeechEnd = 0;
			lpcEmuSamplesPending = 0;
			lpcCycleRemainder = 0;
			M168LPCFlushOutput();
			reg5300 = 0x80;
		}

		if (result == LPC_RESULT_EOS) {
			/*
			 * Do not expose $8F yet.  The duration represented by this frame
			 * still has to elapse on the emulated CPU-time playback clock.
			 */
			lpcDecoderEnd = 1;
			M168LPCUpdateEndStatus();
			break;
		}

		/*
		 * Startup consumes the header and produces no PCM.  Continue once
		 * so the first real frame can be generated.  Any later no-progress
		 * result is stopped by the guard or FIFO underflow.
		 */
		if (pcmSize == 0 && !restart && savedReadPos == lpcReadPos)
			break;
	}

	reg5300 = M168LPCGetStatus();
}

static int M168LPCPopPCM(int16* sample)
{
	if (lpcPCMCount < 400)
		M168LPCDecodeAvailable();

	if (!lpcPCMCount) {
		*sample = 0;
		return 0;
	}

	*sample = lpcPCM[lpcPCMReadPos];
	lpcPCMReadPos = (lpcPCMReadPos + 1) & M168_LPC_PCM_MASK;
	lpcPCMCount--;
	return 1;
}

/*
 * Linear 10 kHz -> current FCEUX output-rate conversion.
 *
 * Speech follows FSettings.PCMVolume.  Shift 7 is eight times the level
 * of the first integration (which used shift 10).  FCEUX mixes into an
 * int32 buffer, so final output clipping is handled by the normal mixer.
 */
static int32 M168LPCNextOutputSample(void)
{
	int32 sample;
	int32 delta;

	if (!lpcOutputRate)
		lpcOutputRate = FSettings.SndRate ? FSettings.SndRate : 48000;

	if (!lpcResamplePrimed) {
		if (!M168LPCPopPCM(&lpcResampleCurrent))
			lpcResampleCurrent = 0;

		if (!M168LPCPopPCM(&lpcResampleNext))
			lpcResampleNext = lpcResampleCurrent;

		lpcResamplePhase = 0;
		lpcResamplePrimed = 1;
	}

	delta = (int32)lpcResampleNext - (int32)lpcResampleCurrent;
	sample = (int32)lpcResampleCurrent +
		(int32)(((int64)delta * lpcResamplePhase) / lpcOutputRate);

	lpcResamplePhase += M168_LPC_SOURCE_RATE;

	while (lpcResamplePhase >= lpcOutputRate) {
		lpcResamplePhase -= lpcOutputRate;


		lpcResampleCurrent = lpcResampleNext;

		if (!M168LPCPopPCM(&lpcResampleNext))
			lpcResampleNext = 0;
	}

	M168LPCUpdateEndStatus();

	return (int32)(((int64)sample * FSettings.PCMVolume) >>
		M168_LPC_VOLUME_SHIFT);
}

/* Low-quality FCEUX expansion-sound path. */
static void M168LPCDoSoundLQ(void)
{
	int32 end;
	int32 i;

	if (!FSettings.SndRate || FSettings.soundq >= 1 || !soundtsinc)
		return;

	end = ((SOUNDTS << 16) / soundtsinc) >> 4;

	if (end < lpcDwave)
		lpcDwave = 0;

	for (i = lpcDwave; i < end; i++)
		Wave[i] += M168LPCNextOutputSample();

	lpcDwave = end;
}

static void M168LPCFill(int Count)
{
	(void)Count;
	M168LPCDoSoundLQ();
	lpcDwave = 0;
}

/* High-quality path: NeoFilterSound has already converted to output rate. */
static void M168LPCNeoFill(int32* wave, int Count)
{
	int i;

	if (!wave || Count <= 0)
		return;

	for (i = 0; i < Count; i++)
		wave[i] += M168LPCNextOutputSample();
}

static void M168LPCSoundRateChanged(void)
{
	uint32 newRate = FSettings.SndRate ? FSettings.SndRate : 48000;

	/*
	 * If sound output is disabled after decoding has started, discard the
	 * already-decoded playback queue.  Status reads will continue decoding
	 * the remaining LPC stream silently and can still reach $8F.
	 */
	if (!FSettings.SndRate) {
		lpcPCMReadPos = 0;
		lpcPCMWritePos = 0;
		lpcPCMCount = 0;
		lpcResamplePhase = 0;
		lpcResampleCurrent = 0;
		lpcResampleNext = 0;
		lpcResamplePrimed = 0;
		lpcOutputRate = newRate;
		lpcDwave = 0;
		M168LPCUpdateEndStatus();
		return;
	}

	/* Preserve the fractional position and prefetched samples mid-speech. */
	if (lpcOutputRate && lpcResamplePrimed)
		lpcResamplePhase = (uint32)(((int64)lpcResamplePhase *
			newRate) / lpcOutputRate);
	else
		lpcResamplePhase = 0;

	lpcOutputRate = newRate;
	lpcDwave = 0;
}

static void M168LPCKill(void)
{
	if (lpcSynth) {
		lpc_d6_synth_delete(lpcSynth);
		lpcSynth = NULL;
	}
}

static void M168LPCInstallSound(void)
{
	GameExpSound.Fill = M168LPCFill;
	GameExpSound.NeoFill = M168LPCNeoFill;
	GameExpSound.HiFill = NULL;
	GameExpSound.HiSync = NULL;
	GameExpSound.RChange = M168LPCSoundRateChanged;
	GameExpSound.Kill = M168LPCKill;
	M168LPCSoundRateChanged();
}

static void M168LPCReset(void)
{
	if (!lpcSynth)
		lpcSynth = lpc_d6_synth_new(M168LPCFeed, NULL,
			LPC_STD_VARIANT_Subor);
	else {
		lpc_d6_synth_rebind(lpcSynth, M168LPCFeed, NULL);
		lpc_d6_synth_reset(lpcSynth);
	}

	memset(lpcFIFO, 0, sizeof(lpcFIFO));
	memset(lpcPCM, 0, sizeof(lpcPCM));

	lpcReadPos = 0;
	lpcWritePos = 0;
	lpcSpeechEnd = 0;
	lpcDecoderEnd = 0;

	lpcPCMReadPos = 0;
	lpcPCMWritePos = 0;
	lpcPCMCount = 0;
	lpcEmuSamplesPending = 0;
	lpcCycleRemainder = 0;

	lpcResamplePhase = 0;
	lpcResampleCurrent = 0;
	lpcResampleNext = 0;
	lpcResamplePrimed = 0;
	lpcDwave = 0;

	reg5300 = 0x80;
}

static int M168LPCConsumeTerminator(void)
{
	int result;
	int pcmSize = 0;
	int restart = 0;
	uint8 savedReadPos;
	int16 pcm[200];

	if (!lpcSynth || !lpcDecoderEnd || !lpcSpeechEnd)
		return 0;

	savedReadPos = lpcReadPos;
	result = lpc_d6_synth_do(lpcSynth, pcm, &pcmSize, &restart);

	if (result == LPC_RESULT_NEED_DATA) {
		lpcReadPos = savedReadPos;
		return 0;
	}

	if (result == LPC_RESULT_ERROR || !restart)
		return 0;

	/*
	 * The FINISHED-state decoder consumed CPU byte $0F as its bit-reversed
	 * $F0 restart command.  Start the next message from a clean playback
	 * timeline; the following byte, not $0F, is the codec selector.
	 */
	lpcDecoderEnd = 0;
	lpcSpeechEnd = 0;
	lpcEmuSamplesPending = 0;
	lpcCycleRemainder = 0;
	M168LPCFlushOutput();
	reg5300 = 0x80;
	return 1;
}

static DECLFW(M168Sub1Write5300)
{
	(void)A;

	/*
	 * Render low-quality audio up to this register timestamp before changing
	 * the FIFO.
	 */
	M168LPCDoSoundLQ();

	/*
	 * After energy $F is visible, the real chip accepts only the mandatory
	 * terminating CPU byte $0F.  It must be consumed by the decoder's
	 * FINISHED state; resetting first would misread $0F as a codec header
	 * and produce the chaotic noise seen in the previous version.
	 */
	if (lpcSpeechEnd) {
		if (V != 0x0F) {
			reg5300 = 0x8F;
			return;
		}

		if (M168LPCFIFOFull()) {
			reg5300 = 0x8F;
			return;
		}

		lpcFIFO[lpcWritePos] = V;
		lpcWritePos = (lpcWritePos + 1) & M168_LPC_FIFO_MASK;

		if (!M168LPCConsumeTerminator()) {
			reg5300 = 0x8F;
			return;
		}

		reg5300 = M168LPCGetStatus();
		return;
	}

	/*
	 * The end frame may already have been decoded but may not yet have
	 * reached the emulated playback head.  Software should wait for $8F;
	 * rejecting writes here prevents premature next-message data from being
	 * appended to a FINISHED decoder stream.
	 */
	if (lpcDecoderEnd) {
		reg5300 = 0x80;
		return;
	}

	if (M168LPCFIFOFull()) {
		reg5300 = 0x00;
		return;
	}

	lpcFIFO[lpcWritePos] = V;
	lpcWritePos = (lpcWritePos + 1) & M168_LPC_FIFO_MASK;

	M168LPCDecodeAvailable();
	reg5300 = M168LPCGetStatus();
}

static DECLFR(M168Sub1Read5300)
{
	(void)A;

	/*
	 * Decode available FIFO bytes here as well.  Actual speech time and the
	 * transition to energy $F are advanced independently by MapIRQHook.
	 */
	M168LPCDecodeAvailable();
	reg5300 = M168LPCGetStatus();
	return reg5300;
}

/*
 * uPD765/FDC placeholders for the SB97 built-in-computer configuration.
 * The VirtuaNES mapper file only calls nes->fdc; it does not contain that
 * controller implementation.  Keep the addresses reserved without
 * pretending that disk operations are implemented.
 */
static DECLFW(M168Sub1FDCWrite)
{
	(void)A;
	(void)V;
}

static DECLFR(M168Sub1FDCRead)
{
	(void)A;
	return X.DB;
}

static DECLFW(M168Sub1WriteHigh)
{
	if (suborType == M168_SUBOR_KARAOKE) {
		karaokeReg = V;
		M168Sub1Sync();
		return;
	}

	/*
	 * VirtuaNES only permits writes to the 128 KiB secondary PRG-RAM in
	 * the SB97 configuration.  CartBW uses the mapping created by
	 * setprg16r/setprg32r above.
	 */
	if (suborType == M168_SUBOR_SB97 && (reg5000 & 0x80))
		CartBW(A, V);

	M168Sub1Sync();
}

/* ------------------------------------------------------------------------- */
/* Power/reset/restore                                                       */
/* ------------------------------------------------------------------------- */

static void M168InstallSub1PPUHook(void)
{
	if (PPU_hook != M168Sub1PPUHook)
		oldPPUHook = PPU_hook;

	PPU_hook = M168Sub1PPUHook;

	if (FFCEUX_PPUWrite != M168Sub1PPUWrite)
		oldPPUWrite = FFCEUX_PPUWrite;

	FFCEUX_PPUWrite = M168Sub1PPUWrite;
}

static void M168Sub1ResetRegisters(void)
{
	reg5000 = 0;
	reg5200 = 0;
	karaokeReg = 0;
	ppuSwitch = 0;
	chrLatch = 0;
	M168LPCReset();
	M168Sub1Sync();
}

static void M168Power(void)
{
	if (submapper == M168_SUB1) {
		M168Sub1ResetRegisters();
		M168LPCInstallClock();
		M168InstallSub1PPUHook();

		SetWriteHandler(0x5000, 0x5000, M168Sub1Write5000);
		SetWriteHandler(0x5200, 0x5200, M168Sub1Write5200);
		SetWriteHandler(0x5300, 0x5300, M168Sub1Write5300);
		SetReadHandler(0x5300, 0x5300, M168Sub1Read5300);

		SetReadHandler(0x6000, 0x7FFF, CartBR);
		SetWriteHandler(0x6000, 0x7FFF, CartBW);

		SetReadHandler(0x8000, 0xFFFF, CartBR);
		SetWriteHandler(0x8000, 0xFFFF, M168Sub1WriteHigh);

		if (suborType == M168_SUBOR_SB97) {
			SetWriteHandler(0x5500, 0x5507, M168Sub1FDCWrite);
			SetReadHandler(0x5600, 0x5607, M168Sub1FDCRead);
		}
	}
	else {
		reg = 0;
		M168Sub0Sync();

		SetWriteHandler(0x4020, 0x7FFF, M168Dummy);
		SetWriteHandler(0xB000, 0xB000, M168Sub0Write);
		SetWriteHandler(0xF000, 0xF000, M168Dummy);
		SetWriteHandler(0xF080, 0xF080, M168Dummy);
		SetReadHandler(0x8000, 0xFFFF, CartBR);
	}
}

static void M168Reset(void)
{
	if (submapper == M168_SUB1)
		M168Sub1ResetRegisters();
	else {
		reg = 0;
		M168Sub0Sync();
	}
}

static void M168StateRestore(int version)
{
	(void)version;

	if (submapper == M168_SUB1) {
		if (lpcSynth)
			lpc_d6_synth_rebind(lpcSynth, M168LPCFeed, NULL);
		M168LPCInstallSound();
		M168LPCInstallClock();
		M168Sub1Sync();
		M168InstallSub1PPUHook();
		reg5300 = M168LPCGetStatus();
	}
	else {
		M168Sub0Sync();
	}
}

static void M168Close(void)
{
	M168LPCKill();

	if (MapIRQHook == M168LPCClock)
		MapIRQHook = oldMapIRQHook;
	oldMapIRQHook = NULL;

	if (PPU_hook == M168Sub1PPUHook)
		PPU_hook = oldPPUHook;
	oldPPUHook = NULL;

	if (FFCEUX_PPUWrite == M168Sub1PPUWrite)
		FFCEUX_PPUWrite = oldPPUWrite;
	oldPPUWrite = NULL;

	if (CHRRAM)
		FCEU_gfree(CHRRAM);
	if (WRAM)
		FCEU_gfree(WRAM);
	if (PRAM)
		FCEU_gfree(PRAM);

	CHRRAM = NULL;
	WRAM = NULL;
	PRAM = NULL;

	CHRRAMSIZE = 0;
	WRAMSIZE = 0;
	PRAMSIZE = 0;
}

/* ------------------------------------------------------------------------- */
/* Initialization                                                            */
/* ------------------------------------------------------------------------- */

void Mapper168_Init(CartInfo* info)
{
	submapper = (info->ines2 && info->submapper == 1) ? M168_SUB1 : M168_SUB0;
	suborType = M168_SUBOR_GENERIC;

	info->Power = M168Power;
	info->Reset = M168Reset;
	info->Close = M168Close;
	GameStateRestore = M168StateRestore;

	AddExState(&M168StateRegs, ~0, 0, 0);

	if (submapper == M168_SUB1) {
		/*
		 * VirtuaNES identifies the two special cases by PRG CRC32.
		 * All other submapper-1 images use the generic UNL-DANCE2000
		 * register behavior.
		 */
		switch (info->CRC32) {
		case 0x0A9808AE:
			suborType = M168_SUBOR_KARAOKE;
			break;

		case 0x40A4C574:
			suborType = M168_SUBOR_SB97;
			break;

		default:
			suborType = M168_SUBOR_GENERIC;
			break;
		}

		CHRRAMSIZE = 8 * 1024;
		WRAMSIZE = 8 * 1024;
		PRAMSIZE = 128 * 1024;

		CHRRAM = (uint8*)FCEU_gmalloc(CHRRAMSIZE);
		WRAM = (uint8*)FCEU_gmalloc(WRAMSIZE);
		PRAM = (uint8*)FCEU_gmalloc(PRAMSIZE);

		memset(CHRRAM, 0, CHRRAMSIZE);
		memset(WRAM, 0, WRAMSIZE);
		memset(PRAM, 0, PRAMSIZE);

		SetupCartCHRMapping(M168_CHR_CHIP, CHRRAM, CHRRAMSIZE, 1);
		SetupCartPRGMapping(M168_WRAM_CHIP, WRAM, WRAMSIZE, 1);
		SetupCartPRGMapping(M168_PRAM_CHIP, PRAM, PRAMSIZE, 1);

		AddExState(CHRRAM, CHRRAMSIZE, 0, "CRAM");
		AddExState(WRAM, WRAMSIZE, 0, "WRAM");
		AddExState(PRAM, PRAMSIZE, 0, "PRAM");

		M168LPCReset();
		M168LPCInstallSound();
		AddExState(&M168LPCStateRegs, ~0, 0, 0);
		AddExState(&lpcSynth,
			(uint32)lpc_d6_synth_size() | FCEUSTATE_INDIRECT,
			0, "LPCS");

		if (info->battery)
			info->addSaveGameBuf(WRAM, WRAMSIZE);
	}
	else {
		/* Preserve the original FCEUX mapper-168 CHR-RAM capacity. */
		CHRRAMSIZE = 8192 * 8;
		CHRRAM = (uint8*)FCEU_gmalloc(CHRRAMSIZE);

		SetupCartCHRMapping(M168_CHR_CHIP, CHRRAM, CHRRAMSIZE, 1);
		AddExState(CHRRAM, CHRRAMSIZE, 0, "CRAM");
	}
}