/* FCE Ultra - NES/Famicom Emulator
 *
 * NES 2.0 Mapper 742 -- TQY FPGA compatibility supervisor
 * Stage 3 integration implementation for FCEUX-2.6.6-Yhc.
 *
 * This revision follows the updated Mapper 742 specification:
 *   - $4104 bits 2/3 select the small PRG/CHR outer-bank ranges;
 *   - $4109 is the small CHR outer-bank selector;
 *   - Mapper 118 mirroring has priority over Mapper 154 mirroring,
 *     which in turn has priority over the native MMC3 mirroring register.
 *
 * Stage 3 adds a native-core bridge for MMC5, Bandai FCG, Namco 163,
 * Mapper 80, Sunsoft FME-7/Mapper 112, J.Y. Company, Magic Dragon, GTROM
 * and UNROM 512, plus a private Mapper 33/48 compatibility core. It also
 * routes the independently-selected MMC5,
 * Namco 163, Sunsoft 5B and VRC7 expansion-audio interfaces without giving
 * those audio engines ownership of the active primary mapper.
 *
 * Copyright (C) 2026 Yhc-Studio contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include "mapinc.h"

#include "asic_latch.h"
#include "asic_mmc1.h"
#include "asic_mmc2and4.h"
#include "asic_mmc3.h"
#include "asic_vrc1.h"
#include "asic_vrc2and4.h"
#include "asic_vrc3.h"
#include "asic_vrc6.h"
#include "asic_vrc7.h"
#include "cartram.h"
#include "../ines.h"
#include "../sound.h"

#include <string.h>

static uint8 supervisor[0x10];
static uint8 supervisorLocked;
static uint8 activeMapperMode;
static uint8 unsupportedMode;
static uint8 mapper154Mirroring;

/* MMC3-family supervisor state not exposed by the generic ASIC module. */
static uint8 mapper742MMC3Command;
static uint8 mapper742MMC3WRAMControl;
static uint8 mapper742ExtendedReg[11]; /* R0-R9, RF at index 10 */
static uint8 mapper742NesticleCHR[8];
static uint8 mapper742NesticlePRG[3];
static uint8 mapper118Mirroring[8];
static uint8 mapper118PPUSlot;

/* RAMBO-1 has a different IRQ counter and therefore uses its own core. */
static uint8 ramboCommand;
static uint8 ramboReg[11];             /* R0-R9, RF at index 10 */
static uint8 ramboMirroring;
static uint8 ramboIRQMode;
static uint8 ramboIRQCount;
static uint8 ramboIRQEnabled;
static uint8 ramboIRQLatch;
static uint8 ramboIRQReload;
static int32 ramboPrescaler;

/* Mapper 33/48 share one source core in FCEUX, so keep a private instance. */
static uint8 mapper33Is48;
static uint8 mapper33Regs[8];
static uint8 mapper33Mirroring;
static uint8 mapper48IRQEnabled;
static int16 mapper48IRQCount;
static int16 mapper48IRQLatch;

/* Mapper 119 needs an independently-selectable CHR-RAM chip. */
static uint8* mapper742FallbackCHRRAM = NULL;
static uint32 mapper742FallbackCHRRAMSize = 0;

/* -------------------------------------------------------------------------
 * Stage 3 native-core bridge
 * ---------------------------------------------------------------------- */

enum Mapper742NativeCoreID
{
    M742_NATIVE_MMC5 = 0,
    M742_NATIVE_BANDAI,
    M742_NATIVE_N163,
    M742_NATIVE_VRC7_AUDIO,
    M742_NATIVE_MAPPER80,
    M742_NATIVE_FME7,
    M742_NATIVE_MAPPER112,
    M742_NATIVE_JY,
    M742_NATIVE_MAGIC_DRAGON,
    M742_NATIVE_GTROM,
    M742_NATIVE_UNROM512,
    M742_NATIVE_COUNT
};

typedef void (*Mapper742NativeInit)(CartInfo* info);

struct Mapper742NativeCore
{
    void (*power)(void);
    void (*reset)(void);
    void (*close)(void);
    void (*restore)(int version);
    void (*cpuHook)(int cycles);
    void (*hblankHook)(void);
    void (*ppuHook)(uint32 address);
    EXPSOUND initialSound;
    uint8* prgPtr[32];
    uint32 prgSize[32];
    uint8 prgRAM[32];
    uint8* chrPtr[32];
    uint32 chrSize[32];
    uint8 chrRAM[32];
    uint8 initialized;
    uint8 powered;
};

static Mapper742NativeCore mapper742Native[M742_NATIVE_COUNT];
static int mapper742ActiveNative = -1;
static CartInfo* mapper742CartInfo = NULL;

static uint8* mapper742BasePRGPtr[32];
static uint32 mapper742BasePRGSize[32];
static uint8 mapper742BasePRGRAM[32];
static uint8* mapper742BaseCHRPtr[32];
static uint32 mapper742BaseCHRSize[32];
static uint8 mapper742BaseCHRRAM[32];

/* Bandai FCG's FPGA compatibility ExRAM/stack. */
static uint8 mapper742ExRAM[0x800];
static uint16 mapper742ExRAMStackPointer;
static void (*mapper742BandaiWrite)(uint32 A, uint8 V) = NULL;
static uint8(*mapper742BandaiRead)(uint32 A) = NULL;

/* $4102 expansion-audio routing. */
struct Mapper742AudioRoute
{
    uint16 start;
    uint16 end;
    void (*mapperWrite)(uint32 A, uint8 V);
    void (*audioWrite)(uint32 A, uint8 V);
    uint8(*mapperRead)(uint32 A);
    uint8(*audioRead)(uint32 A);
};

struct Mapper742CapturedAudio
{
    EXPSOUND sound;
    Mapper742AudioRoute route[3];
    uint8 routeCount;
    uint8 captured;
};

static Mapper742CapturedAudio mapper742Audio[5];
static Mapper742AudioRoute mapper742InstalledAudio[3];
static uint8 mapper742InstalledAudioCount;
static uint8 activeAudioMode;

static void (*mapperSync)(int, int, int, int) = NULL;

static void Mapper742_Sync(void);
static void Mapper742_ApplyMode(uint8 clear);
static void Mapper742_ApplyAudio(uint8 clear);
static void Mapper742_ResetHandlers(void);
static void Mapper742_StateRestore(int version);
static DECLFW(Mapper742_WriteSupervisor);

/* -------------------------------------------------------------------------
 * Supervisor helpers
 * ---------------------------------------------------------------------- */

static int Mapper742_CompatibilitySupervisorEnabled(void)
{
    return (supervisor[0x04] & 0x80) != 0;
}

static uint8 Mapper742_EffectiveMapperMode(void)
{
    if (!Mapper742_CompatibilitySupervisorEnabled())
        return supervisor[0x01];

    switch (supervisor[0x00] & 0x03)
    {
    case 0:  return 0x03; /* MMC3 */
    case 1:  return 0x09; /* VRC2b */
    default: return 0x01; /* MMC1 */
    }
}

static uint8 Mapper742_EffectiveOptions(void)
{
    return Mapper742_CompatibilitySupervisorEnabled() ? 0x00 : supervisor[0x03];
}

/* $4105 bits 4 and 5 may be exchanged by $4104 bit 0. */
static uint8 Mapper742_EffectiveCHRFeatures(void)
{
    uint8 value = supervisor[0x05] & 0xF0;

    if (supervisor[0x04] & 0x01)
    {
        uint8 bit4 = value & 0x10;
        uint8 bit5 = value & 0x20;
        value &= (uint8)~0x30;
        if (bit4) value |= 0x20;
        if (bit5) value |= 0x10;
    }

    return value;
}

/*
 * $4104 bit 2 selects the small PRG outer-bank range and bit 3 selects
 * the small CHR outer-bank range. $4108/$4109 are used in the small range,
 * while $410A/$410B are used in the 128 KiB-and-larger range.
 */
static int Mapper742_PRGWindowKB(void)
{
    int size = supervisor[0x05] & 0x03;
    return (supervisor[0x04] & 0x04) ? (8 << size) : (128 << size);
}

static int Mapper742_CHRWindowKB(void)
{
    int size = (supervisor[0x05] >> 2) & 0x03;
    return (supervisor[0x04] & 0x08) ? (8 << size) : (128 << size);
}

static int Mapper742_PRGBase8K(void)
{
    int base = (int)supervisor[0x0C] << 12; /* 32 MiB / 8 KiB */

    if (supervisor[0x04] & 0x04)
        base |= supervisor[0x08] & 0x3F;      /* 8 KiB units */
    else
        base |= (int)supervisor[0x0A] << 4;  /* 128 KiB units */

    return base;
}

static int Mapper742_CHRBase1K(void)
{
    int base = (int)supervisor[0x0C] << 15; /* 32 MiB / 1 KiB */

    if (supervisor[0x04] & 0x08)
        base |= (int)(supervisor[0x09] & 0x3F) << 3; /* 8 KiB units */
    else
        base |= (int)supervisor[0x0B] << 7;         /* 128 KiB units */

    if (Mapper742_CompatibilitySupervisorEnabled() && (supervisor[0x00] & 0x04))
        base += Mapper742_CHRWindowKB();

    return base;
}

static void Mapper742_GetOuterBanks(int* prgAND, int* prgOR,
    int* chrAND, int* chrOR)
{
    int prgWindow8K = Mapper742_PRGWindowKB() >> 3;
    int chrWindow1K = Mapper742_CHRWindowKB();

    *prgAND = prgWindow8K - 1;
    *chrAND = chrWindow1K - 1;
    *prgOR = Mapper742_PRGBase8K() & ~*prgAND;
    *chrOR = Mapper742_CHRBase1K() & ~*chrAND;
}

static void Mapper742_SaveBaseMappings(void)
{
    int i;
    for (i = 0; i < 32; ++i)
    {
        mapper742BasePRGPtr[i] = PRGptr[i];
        mapper742BasePRGSize[i] = PRGsize[i];
        mapper742BasePRGRAM[i] = PRGram[i];
        mapper742BaseCHRPtr[i] = CHRptr[i];
        mapper742BaseCHRSize[i] = CHRsize[i];
        mapper742BaseCHRRAM[i] = CHRram[i];
    }
}

static void Mapper742_RestoreBaseMappings(void)
{
    int i;
    for (i = 0; i < 32; ++i)
    {
        if (mapper742BasePRGPtr[i] || mapper742BasePRGSize[i])
            SetupCartPRGMapping(i, mapper742BasePRGPtr[i],
                mapper742BasePRGSize[i], mapper742BasePRGRAM[i]);
        if (mapper742BaseCHRPtr[i] || mapper742BaseCHRSize[i])
            SetupCartCHRMapping(i, mapper742BaseCHRPtr[i],
                mapper742BaseCHRSize[i], mapper742BaseCHRRAM[i]);
    }
}

static uint32 Mapper742_ClampWindow(uint32 requested, uint32 total)
{
    if (!total)
        return 0;
    if (!requested || requested > total)
        return total;
    return requested;
}

static uint32 Mapper742_AlignedBase(uint32 requestedBase, uint32 window,
    uint32 total)
{
    uint32 maximumBase;

    if (!total || !window || window >= total)
        return 0;

    requestedBase %= total;
    requestedBase -= requestedBase % window;
    maximumBase = total - window;
    if (requestedBase > maximumBase)
        requestedBase = maximumBase - (maximumBase % window);
    return requestedBase;
}

static void Mapper742_MapNativeOuterWindow(void)
{
    uint32 prgWindow = Mapper742_ClampWindow(
        (uint32)Mapper742_PRGWindowKB() << 10, mapper742BasePRGSize[0]);
    uint32 chrWindow = Mapper742_ClampWindow(
        (uint32)Mapper742_CHRWindowKB() << 10, mapper742BaseCHRSize[0]);
    uint32 prgBase = Mapper742_AlignedBase(
        (uint32)Mapper742_PRGBase8K() << 13, prgWindow,
        mapper742BasePRGSize[0]);
    uint32 chrBase = Mapper742_AlignedBase(
        (uint32)Mapper742_CHRBase1K() << 10, chrWindow,
        mapper742BaseCHRSize[0]);

    if (mapper742BasePRGPtr[0] && prgWindow)
        SetupCartPRGMapping(0, mapper742BasePRGPtr[0] + prgBase, prgWindow, 0);
    else
        SetupCartPRGMapping(0, mapper742BasePRGPtr[0], mapper742BasePRGSize[0],
            mapper742BasePRGRAM[0]);

    if (mapper742BaseCHRPtr[0] && chrWindow)
        SetupCartCHRMapping(0, mapper742BaseCHRPtr[0] + chrBase, chrWindow,
            mapper742BaseCHRRAM[0]);
    else
        SetupCartCHRMapping(0, mapper742BaseCHRPtr[0], mapper742BaseCHRSize[0],
            mapper742BaseCHRRAM[0]);
}

static void Mapper742_ApplyNativeMappings(Mapper742NativeCore* core)
{
    int i;

    Mapper742_RestoreBaseMappings();
    for (i = 1; i < 32; ++i)
    {
        if (core->prgPtr[i] || core->prgSize[i])
            SetupCartPRGMapping(i, core->prgPtr[i], core->prgSize[i],
                core->prgRAM[i]);
        if (core->chrPtr[i] || core->chrSize[i])
            SetupCartCHRMapping(i, core->chrPtr[i], core->chrSize[i],
                core->chrRAM[i]);
    }
    Mapper742_MapNativeOuterWindow();
}

static void Mapper742_MergeSaveGame(CartInfo* destination,
    const CartInfo* source)
{
    size_t i;
    size_t j;

    for (i = 0; i < source->SaveGame.size(); ++i)
    {
        int duplicate = 0;
        for (j = 0; j < destination->SaveGame.size(); ++j)
        {
            if (destination->SaveGame[j].bufptr == source->SaveGame[i].bufptr &&
                destination->SaveGame[j].buflen == source->SaveGame[i].buflen)
            {
                duplicate = 1;
                break;
            }
        }
        if (!duplicate)
            destination->SaveGame.push_back(source->SaveGame[i]);
    }
}

static void Mapper742_RegisterNativeCore(int id, Mapper742NativeInit init)
{
    Mapper742NativeCore* core = &mapper742Native[id];
    CartInfo child = *mapper742CartInfo;
    void (*savedRestore)(int) = GameStateRestore;
    void (*savedCPUHook)(int) = MapIRQHook;
    void (*savedHBlankHook)(void) = GameHBIRQHook;
    void (*savedPPUHook)(uint32) = PPU_hook;
    EXPSOUND savedSound = GameExpSound;
    int i;

    memset(core, 0, sizeof(*core));
    child.Power = NULL;
    child.Reset = NULL;
    child.Close = NULL;
    child.SaveGame.clear();

    memset(&GameExpSound, 0, sizeof(GameExpSound));
    GameStateRestore = NULL;
    MapIRQHook = NULL;
    GameHBIRQHook = NULL;
    PPU_hook = NULL;

    init(&child);

    core->power = child.Power;
    core->reset = child.Reset;
    core->close = child.Close;
    core->restore = GameStateRestore;
    core->cpuHook = MapIRQHook;
    core->hblankHook = GameHBIRQHook;
    core->ppuHook = PPU_hook;
    core->initialSound = GameExpSound;
    core->initialized = 1;

    for (i = 0; i < 32; ++i)
    {
        core->prgPtr[i] = PRGptr[i];
        core->prgSize[i] = PRGsize[i];
        core->prgRAM[i] = PRGram[i];
        core->chrPtr[i] = CHRptr[i];
        core->chrSize[i] = CHRsize[i];
        core->chrRAM[i] = CHRram[i];
    }

    Mapper742_MergeSaveGame(mapper742CartInfo, &child);
    Mapper742_RestoreBaseMappings();

    GameStateRestore = savedRestore;
    MapIRQHook = savedCPUHook;
    GameHBIRQHook = savedHBlankHook;
    PPU_hook = savedPPUHook;
    GameExpSound = savedSound;
}

/* -------------------------------------------------------------------------
 * Mapping callbacks
 * ---------------------------------------------------------------------- */

static void Mapper742_SyncNROM(int prgAND, int prgOR, int chrAND, int chrOR)
{
    int i;

    for (i = 0; i < 4; ++i)
        setprg8(0x8000 + i * 0x2000, (i & prgAND) | prgOR);

    for (i = 0; i < 8; ++i)
        setchr1(i * 0x0400, (i & chrAND) | chrOR);
}

static void Mapper742_SyncMMC1(int prgAND, int prgOR, int chrAND, int chrOR)
{
    prgAND >>= 1;
    prgOR >>= 1;
    chrAND >>= 2;
    chrOR >>= 2;

    MMC1_syncWRAM(0);
    MMC1_syncPRG(prgAND, prgOR & ~prgAND);
    MMC1_syncCHR(chrAND, chrOR & ~chrAND);
    MMC1_syncMirror();
}

static void Mapper742_SyncMMC2(int prgAND, int prgOR, int chrAND, int chrOR)
{
    MMC24_syncWRAM(0);
    MMC2_syncPRG(prgAND, prgOR & ~prgAND);
    MMC24_syncCHR(chrAND, chrOR & ~chrAND);
    MMC24_syncMirror();
}

static void Mapper742_SyncMMC4(int prgAND, int prgOR, int chrAND, int chrOR)
{
    MMC24_syncWRAM(0);
    MMC4_syncPRG(prgAND, prgOR & ~prgAND);
    MMC24_syncCHR(chrAND, chrOR & ~chrAND);
    MMC24_syncMirror();
}

static int Mapper742_MergeBank(int innerBank, int innerMask, int outerBase)
{
    return (innerBank & innerMask) | (outerBase & ~innerMask);
}

static void Mapper742_RecordMapper118(int slot, int bank)
{
    if (Mapper742_EffectiveCHRFeatures() & 0x10)
        mapper118Mirroring[slot & 7] = (bank >> 7) & 1;
}

static void Mapper742_MapCHR1K(int slot, int bank, int chrAND, int chrOR)
{
    uint8 features = Mapper742_EffectiveCHRFeatures();
    int localAND = chrAND;
    int localOR = chrOR;
    int chip = 0;

    Mapper742_RecordMapper118(slot, bank);

    if (features & 0x10) /* Mapper 118: CHR register bit 7 selects CIRAM. */
    {
        bank &= ~0x80;
        localAND &= ~0x80;
    }

    if (features & 0x20) /* Mapper 119: CHR register bit 6 selects RAM. */
    {
        chip = (bank & 0x40) ? 0x10 : 0;
        bank &= ~0x40;
        localAND &= ~0x40;
    }

    if ((features & 0x80) && slot >= 4) /* Mapper 88 split CHR halves. */
        localOR += Mapper742_CHRWindowKB();

    setchr1r(chip, slot << 10,
        Mapper742_MergeBank(bank, localAND, localOR));
}

static void Mapper742_MapCHR2K(int slot, int bank, int chrAND, int chrOR)
{
    uint8 features = Mapper742_EffectiveCHRFeatures();
    int localAND = chrAND >> 1;
    int localOR = chrOR >> 1;
    int chip = 0;

    Mapper742_RecordMapper118(slot * 2 + 0, bank);
    Mapper742_RecordMapper118(slot * 2 + 1, bank);

    if (features & 0x10)
    {
        bank &= ~0x80;
        localAND &= ~0x80;
    }

    if (features & 0x20)
    {
        chip = (bank & 0x40) ? 0x10 : 0;
        bank &= ~0x40;
        localAND &= ~0x40;
    }

    if ((features & 0x80) && slot >= 2)
        localOR += Mapper742_CHRWindowKB() >> 1;

    setchr2r(chip, slot << 11,
        Mapper742_MergeBank(bank, localAND, localOR));
}

static void Mapper742_PPUHook(uint32 A)
{
    if (A < 0x2000)
    {
        mapper118PPUSlot = (A >> 10) & 7;
        setmirror(mapper118Mirroring[mapper118PPUSlot] ? MI_1 : MI_0);
    }
}

static void Mapper742_ApplyMMC3Mirroring(uint8 value)
{
    uint8 features = Mapper742_EffectiveCHRFeatures();
    uint8 options = Mapper742_EffectiveOptions();
    uint8 submode = options & 0x0F;

    /* Priority: Mapper 118 > Mapper 154 > extended/native MMC3. */
    if (features & 0x10)
    {
        setmirror(mapper118Mirroring[mapper118PPUSlot & 7] ? MI_1 : MI_0);
    }
    else if (options & 0x40)
    {
        setmirror(mapper154Mirroring ? MI_1 : MI_0);
    }
    else if (submode == 2 || (options & 0x10))
    {
        switch (value & 3)
        {
        case 0: setmirror(MI_V); break;
        case 1: setmirror(MI_H); break;
        case 2: setmirror(MI_0); break;
        case 3: setmirror(MI_1); break;
        }
    }
    else
    {
        setmirror(value & 1 ? MI_H : MI_V);
    }
}

static void Mapper742_SyncMMC3WRAM(void)
{
    uint8 options = Mapper742_EffectiveOptions();
    uint8 submode = options & 0x0F;
    int bank = 0;

    if (submode == 2 || (options & 0x20))
        bank = mapper742MMC3WRAMControl & 3;

    MMC3_syncWRAM(bank);
}

static void Mapper742_SyncMMC3CHRStandard(int chrAND, int chrOR)
{
    uint8 features = Mapper742_EffectiveCHRFeatures();
    int bank[8];
    int i;

    if (features & 0x40) /* Mapper 76: four independently-selected 2 KiB banks. */
    {
        for (i = 0; i < 4; ++i)
            Mapper742_MapCHR2K(i, mapper742ExtendedReg[2 + i], chrAND, chrOR);
        return;
    }

    if (mapper742MMC3Command & 0x80)
    {
        bank[0] = mapper742ExtendedReg[2];
        bank[1] = mapper742ExtendedReg[3];
        bank[2] = mapper742ExtendedReg[4];
        bank[3] = mapper742ExtendedReg[5];
        bank[4] = mapper742ExtendedReg[0] & ~1;
        bank[5] = mapper742ExtendedReg[0] | 1;
        bank[6] = mapper742ExtendedReg[1] & ~1;
        bank[7] = mapper742ExtendedReg[1] | 1;
    }
    else
    {
        bank[0] = mapper742ExtendedReg[0] & ~1;
        bank[1] = mapper742ExtendedReg[0] | 1;
        bank[2] = mapper742ExtendedReg[1] & ~1;
        bank[3] = mapper742ExtendedReg[1] | 1;
        bank[4] = mapper742ExtendedReg[2];
        bank[5] = mapper742ExtendedReg[3];
        bank[6] = mapper742ExtendedReg[4];
        bank[7] = mapper742ExtendedReg[5];
    }

    for (i = 0; i < 8; ++i)
        Mapper742_MapCHR1K(i, bank[i], chrAND, chrOR);
}

static void Mapper742_SyncMMC3CHR176(int chrAND, int chrOR)
{
    uint8 features = Mapper742_EffectiveCHRFeatures();
    int bank[8];
    int physical;
    int i;

    if (features & 0x40)
    {
        for (i = 0; i < 4; ++i)
            Mapper742_MapCHR2K(i, mapper742ExtendedReg[2 + i], chrAND, chrOR);
        return;
    }

    if (mapper742MMC3Command & 0x20)
    {
        bank[0] = mapper742ExtendedReg[0];
        bank[1] = mapper742ExtendedReg[8];
        bank[2] = mapper742ExtendedReg[1];
        bank[3] = mapper742ExtendedReg[9];
    }
    else
    {
        bank[0] = mapper742ExtendedReg[0] & ~1;
        bank[1] = mapper742ExtendedReg[0] | 1;
        bank[2] = mapper742ExtendedReg[1] & ~1;
        bank[3] = mapper742ExtendedReg[1] | 1;
    }

    bank[4] = mapper742ExtendedReg[2];
    bank[5] = mapper742ExtendedReg[3];
    bank[6] = mapper742ExtendedReg[4];
    bank[7] = mapper742ExtendedReg[5];

    for (i = 0; i < 8; ++i)
    {
        physical = i ^ ((mapper742MMC3Command & 0x80) ? 4 : 0);
        Mapper742_MapCHR1K(physical, bank[i], chrAND, chrOR);
    }
}

static void Mapper742_SyncMMC3(int prgAND, int prgOR, int chrAND, int chrOR)
{
    int bank0;
    int bank2;

    Mapper742_SyncMMC3WRAM();

    bank0 = (mapper742MMC3Command & 0x40) ? 0xFE : mapper742ExtendedReg[6];
    bank2 = (mapper742MMC3Command & 0x40) ? mapper742ExtendedReg[6] : 0xFE;

    setprg8(0x8000, Mapper742_MergeBank(bank0, prgAND, prgOR));
    setprg8(0xA000, Mapper742_MergeBank(mapper742ExtendedReg[7], prgAND, prgOR));
    setprg8(0xC000, Mapper742_MergeBank(bank2, prgAND, prgOR));
    setprg8(0xE000, Mapper742_MergeBank(0xFF, prgAND, prgOR));

    Mapper742_SyncMMC3CHRStandard(chrAND, chrOR);
    Mapper742_ApplyMMC3Mirroring(MMC3_getMirroring());
}

static void Mapper742_SyncMMC3176(int prgAND, int prgOR, int chrAND, int chrOR)
{
    int bank0;
    int bank2;

    Mapper742_SyncMMC3WRAM();

    bank0 = (mapper742MMC3Command & 0x40) ? mapper742ExtendedReg[10]
        : mapper742ExtendedReg[6];
    bank2 = (mapper742MMC3Command & 0x40) ? mapper742ExtendedReg[6]
        : mapper742ExtendedReg[10];

    setprg8(0x8000, Mapper742_MergeBank(bank0, prgAND, prgOR));
    setprg8(0xA000, Mapper742_MergeBank(mapper742ExtendedReg[7], prgAND, prgOR));
    setprg8(0xC000, Mapper742_MergeBank(bank2, prgAND, prgOR));
    setprg8(0xE000, Mapper742_MergeBank(0xFF, prgAND, prgOR));

    Mapper742_SyncMMC3CHR176(chrAND, chrOR);
    Mapper742_ApplyMMC3Mirroring(MMC3_getMirroring());
}

static void Mapper742_SyncNesticle(int prgAND, int prgOR,
    int chrAND, int chrOR)
{
    int i;

    Mapper742_SyncMMC3WRAM();

    setprg8(0x8000, Mapper742_MergeBank(mapper742NesticlePRG[0], prgAND, prgOR));
    setprg8(0xA000, Mapper742_MergeBank(mapper742NesticlePRG[1], prgAND, prgOR));
    setprg8(0xC000, Mapper742_MergeBank(mapper742NesticlePRG[2], prgAND, prgOR));
    setprg8(0xE000, Mapper742_MergeBank(0xFF, prgAND, prgOR));

    for (i = 0; i < 8; ++i)
        Mapper742_MapCHR1K(i, mapper742NesticleCHR[i], chrAND, chrOR);

    Mapper742_ApplyMMC3Mirroring(MMC3_getMirroring());
}

/* RAMBO-1 / Mapper 64 --------------------------------------------------- */

static void Mapper742_SyncRAMBO(int prgAND, int prgOR, int chrAND, int chrOR)
{
    uint8 features = Mapper742_EffectiveCHRFeatures();
    int bank[8];
    int physical;
    int bank0;
    int bank2;
    int i;

    if (PRGsize[0x10])
        setprg8r(0x10, 0x6000, 0);

    bank0 = (ramboCommand & 0x40) ? ramboReg[10] : ramboReg[6];
    bank2 = (ramboCommand & 0x40) ? ramboReg[6] : ramboReg[10];

    setprg8(0x8000, Mapper742_MergeBank(bank0, prgAND, prgOR));
    setprg8(0xA000, Mapper742_MergeBank(ramboReg[7], prgAND, prgOR));
    setprg8(0xC000, Mapper742_MergeBank(bank2, prgAND, prgOR));
    setprg8(0xE000, Mapper742_MergeBank(0xFF, prgAND, prgOR));

    if (features & 0x40)
    {
        for (i = 0; i < 4; ++i)
            Mapper742_MapCHR2K(i, ramboReg[2 + i], chrAND, chrOR);
    }
    else
    {
        if (ramboCommand & 0x20)
        {
            bank[0] = ramboReg[0];
            bank[1] = ramboReg[8];
            bank[2] = ramboReg[1];
            bank[3] = ramboReg[9];
        }
        else
        {
            bank[0] = ramboReg[0] & ~1;
            bank[1] = ramboReg[0] | 1;
            bank[2] = ramboReg[1] & ~1;
            bank[3] = ramboReg[1] | 1;
        }

        bank[4] = ramboReg[2];
        bank[5] = ramboReg[3];
        bank[6] = ramboReg[4];
        bank[7] = ramboReg[5];

        for (i = 0; i < 8; ++i)
        {
            physical = i ^ ((ramboCommand & 0x80) ? 4 : 0);
            Mapper742_MapCHR1K(physical, bank[i], chrAND, chrOR);
        }
    }

    Mapper742_ApplyMMC3Mirroring(ramboMirroring);
}

static void Mapper742_RAMBOCycleHook(int cycles)
{
    if (!ramboIRQMode)
        return;

    ramboPrescaler += cycles;
    while (ramboPrescaler >= 4)
    {
        ramboPrescaler -= 4;
        --ramboIRQCount;
        if (ramboIRQCount == 0xFF && ramboIRQEnabled)
            X6502_IRQBegin(FCEU_IQEXT);
    }
}

static void Mapper742_RAMBOScanlineHook(void)
{
    if (!ramboIRQMode && scanline != 240)
    {
        ramboIRQReload = 0;
        --ramboIRQCount;
        if (ramboIRQCount == 0xFF && ramboIRQEnabled)
        {
            ramboIRQReload = 1;
            X6502_IRQBegin(FCEU_IQEXT);
        }
    }
}

static DECLFW(Mapper742_WriteRAMBO)
{
    uint8 features = Mapper742_EffectiveCHRFeatures();
    uint8 options = Mapper742_EffectiveOptions();
    uint8 index;
    int mapper154 = !(features & 0x10) && (options & 0x40);

    if (mapper154)
        mapper154Mirroring = (V >> 6) & 1;

    switch (A & 0xF001)
    {
    case 0x8000:
        ramboCommand = (features & 0x40) ? (V & 0x3F) : V;
        Mapper742_Sync();
        break;

    case 0x8001:
        index = ramboCommand & 0x0F;
        if ((Mapper742_EffectiveCHRFeatures() & 0x40) && index < 2)
            break;
        if (index < 10)
            ramboReg[index] = V;
        else if (index == 0x0F)
            ramboReg[10] = V;
        Mapper742_Sync();
        break;

    case 0xA000:
        ramboMirroring = V;
        Mapper742_Sync();
        break;

    case 0xC000:
        ramboIRQLatch = V;
        if (ramboIRQReload)
            ramboIRQCount = ramboIRQLatch;
        break;

    case 0xC001:
        ramboIRQReload = 1;
        ramboIRQCount = ramboIRQLatch;
        ramboIRQMode = V & 1;
        ramboPrescaler = 0;
        break;

    case 0xE000:
        ramboIRQEnabled = 0;
        X6502_IRQEnd(FCEU_IQEXT);
        if (ramboIRQReload)
            ramboIRQCount = ramboIRQLatch;
        break;

    case 0xE001:
        ramboIRQEnabled = 1;
        if (ramboIRQReload)
            ramboIRQCount = ramboIRQLatch;
        break;
    }

    if (mapper154 && A >= 0xC000)
        Mapper742_Sync();
}

static void Mapper742_SyncVRC1(int prgAND, int prgOR, int chrAND, int chrOR)
{
    VRC1_syncPRG(prgAND, prgOR & ~prgAND);
    VRC1_syncCHR(chrAND, chrOR & ~chrAND);
    VRC1_syncMirror();
}

static void Mapper742_SyncVRC24(int prgAND, int prgOR, int chrAND, int chrOR)
{
    VRC24_syncWRAM(0);
    VRC24_syncPRG(prgAND, prgOR & ~prgAND);
    VRC24_syncCHR(chrAND, chrOR & ~chrAND);
    VRC24_syncMirror();
}

static void Mapper742_SyncVRC3(int prgAND, int prgOR, int chrAND, int chrOR)
{
    prgAND >>= 1;
    prgOR >>= 1;

    VRC3_syncWRAM(0);
    VRC3_syncPRG(prgAND, prgOR & ~prgAND);
    VRC3_syncCHR(chrAND, chrOR & ~chrAND);
}

static void Mapper742_SyncVRC6(int prgAND, int prgOR, int chrAND, int chrOR)
{
    VRC6_syncWRAM(0);
    VRC6_syncPRG(prgAND, prgOR & ~prgAND);
    VRC6_syncCHR(chrAND, chrOR & ~chrAND);
    VRC6_syncMirror();
}

static void Mapper742_SyncVRC7(int prgAND, int prgOR, int chrAND, int chrOR)
{
    VRC7_syncWRAM(0);
    VRC7_syncPRG(prgAND, prgOR & ~prgAND);
    VRC7_syncCHR(chrAND, chrOR & ~chrAND);
    VRC7_syncMirror();
}

static void Mapper742_SyncGNROM(int prgAND, int prgOR, int chrAND, int chrOR)
{
    int pMask = prgAND >> 2;
    int pBase = prgOR >> 2;
    int cMask = chrAND >> 3;
    int cBase = chrOR >> 3;

    setprg32(0x8000, ((Latch_data >> 4) & pMask) | (pBase & ~pMask));
    setchr8((Latch_data & cMask) | (cBase & ~cMask));
}

static void Mapper742_SyncColorDreams(int prgAND, int prgOR,
    int chrAND, int chrOR)
{
    int pMask = prgAND >> 2;
    int pBase = prgOR >> 2;
    int cMask = chrAND >> 3;
    int cBase = chrOR >> 3;

    setprg32(0x8000, (Latch_data & pMask) | (pBase & ~pMask));
    setchr8(((Latch_data >> 4) & cMask) | (cBase & ~cMask));
}

static void Mapper742_SyncAxROM(int prgAND, int prgOR, int chrAND, int chrOR)
{
    (void)chrAND;
    int pMask = prgAND >> 2;
    int pBase = prgOR >> 2;

    setprg32(0x8000, (Latch_data & pMask) | (pBase & ~pMask));
    setchr8(chrOR >> 3);
    setmirror(Latch_data & 0x10 ? MI_1 : MI_0);
}

static void Mapper742_SyncMapper70(int prgAND, int prgOR,
    int chrAND, int chrOR)
{
    int pMask = prgAND >> 1;
    int pBase = prgOR >> 1;
    int cMask = chrAND >> 3;
    int cBase = chrOR >> 3;

    setprg16(0x8000, ((Latch_data >> 4) & pMask) | (pBase & ~pMask));
    setprg16(0xC000, pBase | pMask);
    setchr8((Latch_data & cMask) | (cBase & ~cMask));
}

static void Mapper742_SyncMapper152(int prgAND, int prgOR,
    int chrAND, int chrOR)
{
    Mapper742_SyncMapper70(prgAND, prgOR, chrAND, chrOR);
    setmirror(Latch_data & 0x80 ? MI_1 : MI_0);
}

static void Mapper742_SyncBNROM(int prgAND, int prgOR, int chrAND, int chrOR)
{
    (void)chrAND;
    int pMask = prgAND >> 2;
    int pBase = prgOR >> 2;

    setprg32(0x8000, (Latch_data & pMask) | (pBase & ~pMask));
    setchr8(chrOR >> 3);
}

static void Mapper742_SyncUNROM(int prgAND, int prgOR, int chrAND, int chrOR)
{
    (void)chrAND;
    int pMask = prgAND >> 1;
    int pBase = prgOR >> 1;

    setprg16(0x8000, (Latch_data & pMask) | (pBase & ~pMask));
    setprg16(0xC000, pBase | pMask);
    setchr8(chrOR >> 3);
}

static void Mapper742_SyncMapper33Core(void)
{
    int prgAND;
    int prgOR;
    int chrAND;
    int chrOR;

    Mapper742_GetOuterBanks(&prgAND, &prgOR, &chrAND, &chrOR);
    setprg8(0x8000, Mapper742_MergeBank(mapper33Regs[0], prgAND, prgOR));
    setprg8(0xA000, Mapper742_MergeBank(mapper33Regs[1], prgAND, prgOR));
    setprg8(0xC000, prgOR | (prgAND - 1));
    setprg8(0xE000, prgOR | prgAND);

    setchr2(0x0000, Mapper742_MergeBank(mapper33Regs[2], chrAND >> 1,
        chrOR >> 1));
    setchr2(0x0800, Mapper742_MergeBank(mapper33Regs[3], chrAND >> 1,
        chrOR >> 1));
    setchr1(0x1000, Mapper742_MergeBank(mapper33Regs[4], chrAND, chrOR));
    setchr1(0x1400, Mapper742_MergeBank(mapper33Regs[5], chrAND, chrOR));
    setchr1(0x1800, Mapper742_MergeBank(mapper33Regs[6], chrAND, chrOR));
    setchr1(0x1C00, Mapper742_MergeBank(mapper33Regs[7], chrAND, chrOR));
    setmirror(mapper33Mirroring);
}

static DECLFW(Mapper742_WriteMapper33)
{
    switch (A & 0xF003)
    {
    case 0x8000:
        mapper33Regs[0] = V & 0x3F;
        if (!mapper33Is48)
            mapper33Mirroring = (((V >> 6) & 1) ^ 1) ? MI_V : MI_H;
        break;
    case 0x8001: mapper33Regs[1] = V & 0x3F; break;
    case 0x8002: mapper33Regs[2] = V; break;
    case 0x8003: mapper33Regs[3] = V; break;
    case 0xA000: mapper33Regs[4] = V; break;
    case 0xA001: mapper33Regs[5] = V; break;
    case 0xA002: mapper33Regs[6] = V; break;
    case 0xA003: mapper33Regs[7] = V; break;
    default: return;
    }
    Mapper742_SyncMapper33Core();
}

static DECLFW(Mapper742_WriteMapper48IRQ)
{
    switch (A & 0xF003)
    {
    case 0xC000: mapper48IRQLatch = V; break;
    case 0xC001: mapper48IRQCount = mapper48IRQLatch; break;
    case 0xC002: mapper48IRQEnabled = 1; break;
    case 0xC003:
        mapper48IRQEnabled = 0;
        X6502_IRQEnd(FCEU_IQEXT);
        break;
    case 0xE000:
        mapper33Mirroring = (((V >> 6) & 1) ^ 1) ? MI_V : MI_H;
        Mapper742_SyncMapper33Core();
        break;
    default: break;
    }
}

static void Mapper742_Mapper48HBlank(void)
{
    if (mapper48IRQEnabled)
    {
        ++mapper48IRQCount;
        if (mapper48IRQCount == 0x100)
        {
            X6502_IRQBegin(FCEU_IQEXT);
            mapper48IRQEnabled = 0;
        }
    }
}

static void Mapper742_ActivateMapper33(uint8 clear, uint8 is48)
{
    if (clear)
    {
        memset(mapper33Regs, 0, sizeof(mapper33Regs));
        mapper33Mirroring = MI_V;
        mapper48IRQEnabled = 0;
        mapper48IRQCount = 0;
        mapper48IRQLatch = 0;
    }

    mapper33Is48 = is48;
    mapperSync = NULL;
    Mapper742_SyncMapper33Core();
    SetWriteHandler(0x8000, is48 ? 0xBFFF : 0xFFFF,
        Mapper742_WriteMapper33);
    if (is48)
    {
        SetWriteHandler(0xC000, 0xFFFF, Mapper742_WriteMapper48IRQ);
        GameHBIRQHook = Mapper742_Mapper48HBlank;
    }
}

static void Mapper742_Sync(void)
{
    int prgAND;
    int prgOR;
    int chrAND;
    int chrOR;

    if (mapper742ActiveNative >= 0)
    {
        Mapper742NativeCore* core = &mapper742Native[mapper742ActiveNative];
        Mapper742_ApplyNativeMappings(core);
        if (core->restore)
            core->restore(0);
        return;
    }

    Mapper742_GetOuterBanks(&prgAND, &prgOR, &chrAND, &chrOR);

    if (mapperSync)
        mapperSync(prgAND, prgOR, chrAND, chrOR);
}

/* -------------------------------------------------------------------------
 * Core selection
 * ---------------------------------------------------------------------- */

static DECLFW(Mapper742_WriteNothing)
{
    (void)A;
    (void)V;
}

static void Mapper742_ResetHandlers(void)
{
    PPU_hook = NULL;
    MapIRQHook = NULL;
    GameHBIRQHook = NULL;
    mapper742ActiveNative = -1;

    Mapper742_RestoreBaseMappings();
    SetReadHandler(0x4800, 0x5FFF, CartBROB);
    SetWriteHandler(0x4800, 0x5FFF, Mapper742_WriteNothing);
    SetReadHandler(0x6000, 0xFFFF, CartBR);
    SetWriteHandler(0x6000, 0xFFFF, Mapper742_WriteNothing);
}

static void Mapper742_GetVRCPins(int* a0, int* a1)
{
    uint8 value = Mapper742_EffectiveOptions();
    int first = -1;
    int second = -1;
    int bit;

    /* Compatibility mode 1 explicitly means VRC2b. */
    if (Mapper742_CompatibilitySupervisorEnabled() &&
        ((supervisor[0x00] & 0x03) == 1))
    {
        *a0 = 0x02;
        *a1 = 0x01;
        return;
    }

    for (bit = 0; bit < 8; ++bit)
    {
        if (value & (1 << bit))
        {
            if (first < 0)
                first = bit;
            else
            {
                second = bit;
                break;
            }
        }
    }

    if (first < 0)
    {
        *a0 = 0x01;
        *a1 = 0x02;
    }
    else
    {
        if (second < 0)
            second = first == 0 ? 1 : 0;

        *a0 = 1 << first;
        *a1 = 1 << second;
    }
}

static void Mapper742_ActivateNROM(void)
{
    mapperSync = Mapper742_SyncNROM;
    Mapper742_Sync();
}

/* MMC3-compatible write layer ---------------------------------------- */

static void Mapper742_ResetMMC3Banks(void)
{
    int i;

    mapper742MMC3Command = 0;
    mapper742MMC3WRAMControl = 0;

    for (i = 0; i < 11; ++i)
        mapper742ExtendedReg[i] = 0;

    mapper742ExtendedReg[0] = 0;
    mapper742ExtendedReg[1] = 2;
    mapper742ExtendedReg[2] = 4;
    mapper742ExtendedReg[3] = 5;
    mapper742ExtendedReg[4] = 6;
    mapper742ExtendedReg[5] = 7;
    mapper742ExtendedReg[6] = 0;
    mapper742ExtendedReg[7] = 1;
    mapper742ExtendedReg[8] = 1;
    mapper742ExtendedReg[9] = 3;
    mapper742ExtendedReg[10] = 0xFE;

    for (i = 0; i < 8; ++i)
        mapper742NesticleCHR[i] = i;

    mapper742NesticlePRG[0] = 0;
    mapper742NesticlePRG[1] = 1;
    mapper742NesticlePRG[2] = 0xFE;

    mapper154Mirroring = 0;
    mapper118PPUSlot = 0;
    memset(mapper118Mirroring, 0, sizeof(mapper118Mirroring));
}

static void Mapper742_WriteNesticleBank(uint8 command, uint8 value)
{
    switch (command)
    {
    case 0x00:
        mapper742NesticleCHR[0] = value & 0xFE;
        mapper742NesticleCHR[1] = value | 0x01;
        break;
    case 0x01:
        mapper742NesticleCHR[2] = value & 0xFE;
        mapper742NesticleCHR[3] = value | 0x01;
        break;
    case 0x02: mapper742NesticleCHR[4] = value; break;
    case 0x03: mapper742NesticleCHR[5] = value; break;
    case 0x04: mapper742NesticleCHR[6] = value; break;
    case 0x05: mapper742NesticleCHR[7] = value; break;
    case 0x06: mapper742NesticlePRG[0] = value; break;
    case 0x07: mapper742NesticlePRG[1] = value; break;
    case 0x46: mapper742NesticlePRG[2] = value; break;
    case 0x47: mapper742NesticlePRG[1] = value; break;
    case 0x80:
        mapper742NesticleCHR[4] = value & 0xFE;
        mapper742NesticleCHR[5] = value | 0x01;
        break;
    case 0x81:
        mapper742NesticleCHR[6] = value & 0xFE;
        mapper742NesticleCHR[7] = value | 0x01;
        break;
    case 0x82: mapper742NesticleCHR[0] = value; break;
    case 0x83: mapper742NesticleCHR[1] = value; break;
    case 0x84: mapper742NesticleCHR[2] = value; break;
    case 0x85: mapper742NesticleCHR[3] = value; break;
    }
}

static DECLFW(Mapper742_WriteMMC3)
{
    uint8 options = Mapper742_EffectiveOptions();
    uint8 features = Mapper742_EffectiveCHRFeatures();
    uint8 submode = options & 0x0F;
    uint8 index;
    uint16 reg = A & 0xE001;
    int mapper118 = (features & 0x10) != 0;
    int mapper154 = !mapper118 && ((options & 0x40) != 0);

    if (mapper154)
        mapper154Mirroring = (V >> 6) & 1;

    if (submode == 4 && A >= 0xA000)
    {
        if (mapper154)
            Mapper742_Sync();
        return;
    }

    if (reg == 0x8000)
    {
        /* Mapper 76 disables PRG/CHR mode inversion. */
        mapper742MMC3Command = (features & 0x40) ? (V & 0x3F) : V;
        MMC3_writeReg(A, mapper742MMC3Command);
        Mapper742_Sync();
        return;
    }

    if (reg == 0x8001)
    {
        if (submode == 5)
        {
            Mapper742_WriteNesticleBank(mapper742MMC3Command, V);
            Mapper742_Sync();
            return;
        }

        index = (submode == 2) ? (mapper742MMC3Command & 0x0F)
            : (mapper742MMC3Command & 0x07);

        if ((features & 0x40) && index < 2)
            return;

        if (index < 10)
        {
            if (submode != 2 && index >= 8)
                return;
            if (submode == 2 && (index == 8 || index == 9) &&
                !(mapper742MMC3Command & 0x20))
                return;
            mapper742ExtendedReg[index] = V;
        }
        else if (submode == 2 && index == 0x0F)
        {
            mapper742ExtendedReg[10] = V;
        }
        else
        {
            return;
        }

        if (index < 8)
            MMC3_writeReg(A, V);
        else
            Mapper742_Sync();
        return;
    }

    if (reg == 0xA001)
        mapper742MMC3WRAMControl = V;

    MMC3_writeReg(A, V);

    /* MMC3_writeReg does not sync for IRQ registers; mirroring override does. */
    if (mapper154 && A >= 0xC000)
        Mapper742_Sync();
}

static void Mapper742_ResetRAMBO(void)
{
    int i;

    ramboCommand = 0;
    ramboMirroring = 0;
    ramboIRQMode = 0;
    ramboIRQCount = 0;
    ramboIRQEnabled = 0;
    ramboIRQLatch = 0;
    ramboIRQReload = 0;
    ramboPrescaler = 0;

    for (i = 0; i < 11; ++i)
        ramboReg[i] = 0xFF;

    X6502_IRQEnd(FCEU_IQEXT);
}

static void Mapper742_ActivateRAMBO(uint8 clear)
{
    if (clear)
        Mapper742_ResetRAMBO();

    mapperSync = Mapper742_SyncRAMBO;
    SetReadHandler(0x6000, 0xFFFF, CartBR);
    SetWriteHandler(0x6000, 0x7FFF, CartBW);
    SetWriteHandler(0x8000, 0xFFFF, Mapper742_WriteRAMBO);
    GameHBIRQHook = Mapper742_RAMBOScanlineHook;
    MapIRQHook = Mapper742_RAMBOCycleHook;
    PPU_hook = (Mapper742_EffectiveCHRFeatures() & 0x10)
        ? Mapper742_PPUHook : NULL;
    Mapper742_Sync();
}

static void Mapper742_ActivateMMC3(uint8 clear)
{
    uint8 options = Mapper742_EffectiveOptions();
    uint8 submode = options & 0x0F;
    uint8 features = Mapper742_EffectiveCHRFeatures();
    uint8 type = submode == 3 ? MMC3_TYPE_MMC6 : MMC3_TYPE_SHARP;

    if (submode == 1)
    {
        Mapper742_ActivateRAMBO(clear);
        return;
    }

    if (clear)
        Mapper742_ResetMMC3Banks();

    if (submode == 2)
    {
        mapperSync = Mapper742_SyncMMC3176;
        if (clear)
            mapper742MMC3WRAMControl = 0x80;
    }
    else if (submode == 5)
    {
        mapperSync = Mapper742_SyncNesticle;
    }
    else
    {
        mapperSync = Mapper742_SyncMMC3;
    }

    MMC3_activate(clear, Mapper742_Sync, type, NULL, NULL, NULL, NULL);

    /* Keep internal MMC3 WRAM/mirroring/IRQ state, but own all bank layouts. */
    SetWriteHandler(0x8000, 0xFFFF, Mapper742_WriteMMC3);

    if (submode == 2 && clear)
        MMC3_writeReg(0xA001, mapper742MMC3WRAMControl);

    PPU_hook = (features & 0x10) ? Mapper742_PPUHook : NULL;
    Mapper742_Sync();
}

static void Mapper742_ActivateLatch(uint8 clear,
    void (*syncFunction)(int, int, int, int))
{
    uint16 start = (Mapper742_EffectiveOptions() & 0x01) ? 0x6000 : 0x8000;

    mapperSync = syncFunction;
    Latch_activate(clear, Mapper742_Sync, start, 0xFFFF, NULL);
}


static DECLFR(Mapper742_ReadExRAM)
{
    return mapper742ExRAM[(A - 0x5800) & 0x07FF];
}

static DECLFW(Mapper742_WriteExRAM)
{
    mapper742ExRAM[(A - 0x5800) & 0x07FF] = V;
}

static DECLFR(Mapper742_ReadMMC5ExRAMMirror)
{
    uint32 mapped = (A < 0x5C00) ? A + 0x0400 : A;
    uint8(*handler)(uint32) = ARead[mapped];
    if (handler && handler != Mapper742_ReadMMC5ExRAMMirror)
        return handler(mapped);
    return X.DB;
}

static DECLFW(Mapper742_WriteMMC5ExRAMMirror)
{
    uint32 mapped = (A < 0x5C00) ? A + 0x0400 : A;
    void (*handler)(uint32, uint8) = BWrite[mapped];
    if (handler && handler != Mapper742_WriteMMC5ExRAMMirror)
        handler(mapped, V);
}

static DECLFR(Mapper742_ReadBandai6000)
{
    uint8 options = Mapper742_EffectiveOptions();

    if (options & 0x01)
        return CartBR(A);

    if (options & 0x18)
    {
        mapper742ExRAMStackPointer =
            (mapper742ExRAMStackPointer - 1) & 0x07FF;
        return mapper742ExRAM[mapper742ExRAMStackPointer];
    }

    if (mapper742BandaiRead && mapper742BandaiRead != Mapper742_ReadBandai6000)
        return mapper742BandaiRead(A);
    return X.DB;
}

static DECLFW(Mapper742_WriteBandai6000)
{
    uint8 options = Mapper742_EffectiveOptions();

    if (options & 0x01)
    {
        CartBW(A, V);
        return;
    }

    if (mapper742BandaiWrite && mapper742BandaiWrite != Mapper742_WriteBandai6000)
        mapper742BandaiWrite(A, V);

    if (options & 0x18)
    {
        mapper742ExRAM[mapper742ExRAMStackPointer] = V;
        mapper742ExRAMStackPointer =
            (mapper742ExRAMStackPointer + 1) & 0x07FF;
    }
}

static void Mapper742_InstallNativeOptions(int id)
{
    if (id == M742_NATIVE_MMC5)
    {
        /* Native MMC5 owns $5C00-$5FFF. Mirror it down to $5800-$5BFF. */
        SetReadHandler(0x5800, 0x5BFF, Mapper742_ReadMMC5ExRAMMirror);
        SetWriteHandler(0x5800, 0x5BFF, Mapper742_WriteMMC5ExRAMMirror);
    }
    else if (id == M742_NATIVE_BANDAI)
    {
        if (Mapper742_EffectiveOptions() & 0x01)
        {
            SetupCartPRGMapping(0x10, mapper742BasePRGPtr[0x10],
                mapper742BasePRGSize[0x10],
                mapper742BasePRGRAM[0x10]);
            setprg8r(0x10, 0x6000, 0);
        }
        mapper742BandaiRead = ARead[0x6000];
        mapper742BandaiWrite = BWrite[0x6000];
        SetReadHandler(0x5800, 0x5FFF, Mapper742_ReadExRAM);
        SetWriteHandler(0x5800, 0x5FFF, Mapper742_WriteExRAM);
        SetReadHandler(0x6000, 0x7FFF, Mapper742_ReadBandai6000);
        SetWriteHandler(0x6000, 0x7FFF, Mapper742_WriteBandai6000);
    }
}

static void Mapper742_ActivateNative(int id, uint8 clear)
{
    Mapper742NativeCore* core = &mapper742Native[id];

    if (!core->initialized || !core->power)
    {
        unsupportedMode = 1;
        mapper742ActiveNative = -1;
        Mapper742_ActivateNROM();
        return;
    }

    mapper742ActiveNative = id;
    Mapper742_ApplyNativeMappings(core);

    GameStateRestore = core->restore;
    MapIRQHook = core->cpuHook;
    GameHBIRQHook = core->hblankHook;
    PPU_hook = core->ppuHook;
    GameExpSound = core->initialSound;

    if (clear || !core->powered)
        core->power();
    else if (core->restore)
        core->restore(0);
    else
        core->power();

    core->powered = 1;
    core->cpuHook = MapIRQHook;
    core->hblankHook = GameHBIRQHook;
    core->ppuHook = PPU_hook;
    core->restore = GameStateRestore;
    core->initialSound = GameExpSound;
    GameStateRestore = Mapper742_StateRestore;

    /* $4102, not the primary mapper mode, owns expansion sound selection. */
    memset(&GameExpSound, 0, sizeof(GameExpSound));
    Mapper742_InstallNativeOptions(id);
    SetWriteHandler(0x4100, 0x410F, Mapper742_WriteSupervisor);
}

static DECLFW(Mapper742_AudioWrite)
{
    int i;
    for (i = 0; i < mapper742InstalledAudioCount; ++i)
    {
        Mapper742AudioRoute* route = &mapper742InstalledAudio[i];
        if (A >= route->start && A <= route->end)
        {
            if (route->mapperWrite && route->mapperWrite != Mapper742_AudioWrite)
                route->mapperWrite(A, V);
            if (route->audioWrite && route->audioWrite != route->mapperWrite &&
                route->audioWrite != Mapper742_AudioWrite)
                route->audioWrite(A, V);
            return;
        }
    }
}

static DECLFR(Mapper742_AudioRead)
{
    int i;
    for (i = 0; i < mapper742InstalledAudioCount; ++i)
    {
        Mapper742AudioRoute* route = &mapper742InstalledAudio[i];
        if (A >= route->start && A <= route->end)
        {
            if (route->audioRead && route->audioRead != Mapper742_AudioRead)
                return route->audioRead(A);
            if (route->mapperRead && route->mapperRead != Mapper742_AudioRead)
                return route->mapperRead(A);
            return X.DB;
        }
    }
    return X.DB;
}

static void Mapper742_ClearAudioRoutes(void)
{
    int i;
    for (i = 0; i < mapper742InstalledAudioCount; ++i)
    {
        Mapper742AudioRoute* route = &mapper742InstalledAudio[i];
        SetWriteHandler(route->start, route->end, route->mapperWrite);
        SetReadHandler(route->start, route->end, route->mapperRead);
    }
    mapper742InstalledAudioCount = 0;
    memset(&GameExpSound, 0, sizeof(GameExpSound));
}

static void Mapper742_CaptureRoute(Mapper742CapturedAudio* audio,
    int slot, uint16 start, uint16 end)
{
    audio->route[slot].start = start;
    audio->route[slot].end = end;
    audio->route[slot].audioWrite = BWrite[start];
    audio->route[slot].audioRead = ARead[start];
}

static void Mapper742_CaptureNativeAudio(uint8 mode, int nativeID)
{
    Mapper742CapturedAudio* audio = &mapper742Audio[mode];

    Mapper742_ResetHandlers();
    Mapper742_ActivateNative(nativeID, 1);
    audio->sound = mapper742Native[nativeID].initialSound;

    if (mode == 1)
    {
        /* $4102 selects expansion audio only.  MMC5 multiplier registers
         * $5205-$5206 remain owned by the primary mapper core. */
        audio->routeCount = 1;
        Mapper742_CaptureRoute(audio, 0, 0x5000, 0x5015);
    }
    else if (mode == 2)
    {
        audio->routeCount = 2;
        Mapper742_CaptureRoute(audio, 0, 0x4800, 0x4FFF);
        Mapper742_CaptureRoute(audio, 1, 0xF800, 0xFFFF);
    }
    else if (mode == 3)
    {
        audio->routeCount = 2;
        Mapper742_CaptureRoute(audio, 0, 0xC000, 0xDFFF);
        Mapper742_CaptureRoute(audio, 1, 0xE000, 0xFFFF);
    }

    audio->captured = 1;
    Mapper742_ApplyMode(0);
}

static void Mapper742_CaptureVRC7Audio(void)
{
    Mapper742CapturedAudio* audio = &mapper742Audio[4];

    Mapper742_ResetHandlers();
    Mapper742_ActivateNative(M742_NATIVE_VRC7_AUDIO, 1);
    audio->sound = mapper742Native[M742_NATIVE_VRC7_AUDIO].initialSound;
    audio->routeCount = 2;
    Mapper742_CaptureRoute(audio, 0, 0x9010, 0x901F);
    Mapper742_CaptureRoute(audio, 1, 0x9030, 0x903F);
    audio->captured = 1;
    Mapper742_ApplyMode(0);
}

static void Mapper742_EnsureAudioCaptured(uint8 mode)
{
    if (mode > 4 || mapper742Audio[mode].captured)
        return;

    switch (mode)
    {
    case 1: Mapper742_CaptureNativeAudio(1, M742_NATIVE_MMC5); break;
    case 2: Mapper742_CaptureNativeAudio(2, M742_NATIVE_N163); break;
    case 3: Mapper742_CaptureNativeAudio(3, M742_NATIVE_FME7); break;
    case 4: Mapper742_CaptureVRC7Audio(); break;
    default: break;
    }
}

static void Mapper742_ApplyAudio(uint8 clear)
{
    uint8 mode = Mapper742_CompatibilitySupervisorEnabled() ? 0 : supervisor[0x02];
    Mapper742CapturedAudio* audio;
    int i;

    (void)clear;
    Mapper742_ClearAudioRoutes();
    activeAudioMode = mode;

    if (!mode || mode > 4)
        return;

    Mapper742_EnsureAudioCaptured(mode);
    audio = &mapper742Audio[mode];
    if (!audio->captured)
        return;

    mapper742InstalledAudioCount = audio->routeCount;
    for (i = 0; i < audio->routeCount; ++i)
    {
        mapper742InstalledAudio[i] = audio->route[i];
        mapper742InstalledAudio[i].mapperWrite =
            BWrite[mapper742InstalledAudio[i].start];
        mapper742InstalledAudio[i].mapperRead =
            ARead[mapper742InstalledAudio[i].start];
        SetWriteHandler(mapper742InstalledAudio[i].start,
            mapper742InstalledAudio[i].end,
            Mapper742_AudioWrite);
        SetReadHandler(mapper742InstalledAudio[i].start,
            mapper742InstalledAudio[i].end,
            Mapper742_AudioRead);
    }
    GameExpSound = audio->sound;
    SetWriteHandler(0x4100, 0x410F, Mapper742_WriteSupervisor);
}

static void Mapper742_ApplyMode(uint8 clear)
{
    uint8 mode = Mapper742_EffectiveMapperMode();
    int a0;
    int a1;

    activeMapperMode = mode;
    unsupportedMode = 0;
    mapperSync = NULL;

    Mapper742_ClearAudioRoutes();
    Mapper742_ResetHandlers();

    switch (mode)
    {
    case 0x00: /* NROM */
        Mapper742_ActivateNROM();
        break;

    case 0x01: /* MMC1 */
        mapperSync = Mapper742_SyncMMC1;
        MMC1_activate(clear, Mapper742_Sync, MMC1_TYPE_MMC1B,
            NULL, NULL, NULL, NULL);
        break;

    case 0x02: /* MMC2 */
        mapperSync = Mapper742_SyncMMC2;
        MMC24_activate(clear, Mapper742_Sync);
        break;

    case 0x03: /* MMC3 family */
        Mapper742_ActivateMMC3(clear);
        break;

    case 0x04: /* MMC4 */
        mapperSync = Mapper742_SyncMMC4;
        MMC24_activate(clear, Mapper742_Sync);
        break;

    case 0x08: /* VRC1 */
        mapperSync = Mapper742_SyncVRC1;
        VRC1_activate(clear, Mapper742_Sync);
        break;

    case 0x09: /* VRC2/4 */
        Mapper742_GetVRCPins(&a0, &a1);
        mapperSync = Mapper742_SyncVRC24;

        if ((supervisor[0x04] & 0x02) ||
            (Mapper742_CompatibilitySupervisorEnabled() &&
                ((supervisor[0x00] & 0x03) == 1)))
        {
            VRC2_activate(clear, Mapper742_Sync, a0, a1,
                NULL, NULL, NULL, NULL);
        }
        else
        {
            VRC4_activate(clear, Mapper742_Sync, a0, a1, 1,
                NULL, NULL, NULL, NULL, NULL);
        }
        break;

    case 0x0A: /* VRC3 */
        mapperSync = Mapper742_SyncVRC3;
        VRC3_activate(clear, Mapper742_Sync);
        break;

    case 0x0B: /* VRC6 mapper core */
        Mapper742_GetVRCPins(&a0, &a1);
        mapperSync = Mapper742_SyncVRC6;
        VRC6_activate(clear, Mapper742_Sync, a0, a1,
            NULL, NULL, NULL, NULL);
        break;

    case 0x0C: /* VRC7 mapper core */
        Mapper742_GetVRCPins(&a0, &a1);
        mapperSync = Mapper742_SyncVRC7;
        VRC7_activate(clear, Mapper742_Sync, a0 | a1);
        break;

    case 0x05: /* MMC5 */
        Mapper742_ActivateNative(M742_NATIVE_MMC5, clear);
        break;

    case 0x06: /* Bandai FCG */
        Mapper742_ActivateNative(M742_NATIVE_BANDAI, clear);
        break;

    case 0x07: /* Namco 163 */
        Mapper742_ActivateNative(M742_NATIVE_N163, clear);
        break;

    case 0x0D: /* Mapper 33 */
        Mapper742_ActivateMapper33(clear, 0);
        break;

    case 0x0E: /* Mapper 48 */
        Mapper742_ActivateMapper33(clear, 1);
        break;

    case 0x0F: /* Mapper 80 */
        Mapper742_ActivateNative(M742_NATIVE_MAPPER80, clear);
        break;

    case 0x10: /* FME-7, optionally Mapper 112 register scrambling */
        Mapper742_ActivateNative((Mapper742_EffectiveOptions() & 0x01) ?
            M742_NATIVE_MAPPER112 : M742_NATIVE_FME7, clear);
        break;

    case 0x11: /* J.Y. Company */
        Mapper742_ActivateNative(M742_NATIVE_JY, clear);
        break;

    case 0x80: /* GNROM */
        Mapper742_ActivateLatch(clear, Mapper742_SyncGNROM);
        break;

    case 0x81: /* Color Dreams */
        Mapper742_ActivateLatch(clear, Mapper742_SyncColorDreams);
        break;

    case 0x82: /* AOROM */
        Mapper742_ActivateLatch(clear, Mapper742_SyncAxROM);
        break;

    case 0x83: /* Mapper 70 */
        Mapper742_ActivateLatch(clear, Mapper742_SyncMapper70);
        break;

    case 0x84: /* Mapper 152 */
        Mapper742_ActivateLatch(clear, Mapper742_SyncMapper152);
        break;

    case 0x85: /* BNROM */
        Mapper742_ActivateLatch(clear, Mapper742_SyncBNROM);
        break;

    case 0x86: /* UNROM */
        Mapper742_ActivateLatch(clear, Mapper742_SyncUNROM);
        break;

    case 0x87: /* Magic Dragon / Mapper 107 */
        Mapper742_ActivateNative(M742_NATIVE_MAGIC_DRAGON, clear);
        break;

    case 0x88: /* GTROM / Mapper 111 */
        Mapper742_ActivateNative(M742_NATIVE_GTROM, clear);
        break;

    case 0x89: /* UNROM 512 / Mapper 30; flash programming unsupported */
        Mapper742_ActivateNative(M742_NATIVE_UNROM512, clear);
        break;

    default:
        unsupportedMode = 1;
        Mapper742_ActivateNROM();
        break;
    }

    Mapper742_Sync();
    if (mapper742ActiveNative < 0)
        SetReadHandler(0x8000, 0xFFFF, CartBR);
    GameStateRestore = Mapper742_StateRestore;
}

/* -------------------------------------------------------------------------
 * Supervisor registers and lifecycle
 * ---------------------------------------------------------------------- */

static DECLFW(Mapper742_WriteSupervisor)
{
    uint8 index = A & 0x0F;

    if (index == 0x0F)
    {
        /* Bits 3 and 7 set, all other bits clear: $88 toggles the lock. */
        if (V == 0x88)
            supervisorLocked ^= 1;
        return;
    }

    if (supervisorLocked)
        return;

    supervisor[index] = V;

    switch (index)
    {
    case 0x00:
    case 0x01:
    case 0x03:
    case 0x04:
    case 0x05:
        Mapper742_ApplyMode(1);
        Mapper742_ApplyAudio(1);
        break;

    case 0x08:
    case 0x09:
    case 0x0A:
    case 0x0B:
    case 0x0C:
        Mapper742_Sync();
        break;

    case 0x02:
        Mapper742_ApplyAudio(1);
        break;

    default:
        break;
    }

    SetWriteHandler(0x4100, 0x410F, Mapper742_WriteSupervisor);
}

static void Mapper742_ResetRegisters(void)
{
    int i;

    for (i = 0; i < 0x10; ++i)
        supervisor[i] = 0x00;

    supervisor[0x08] = 0xFF;
    supervisor[0x09] = 0xFF;
    supervisor[0x0A] = 0xFF;
    supervisor[0x0B] = 0xFF;
    supervisor[0x0C] = 0xFF;
    supervisor[0x0F] = 0xFF;

    supervisorLocked = 0;
    activeMapperMode = 0xFF;
    activeAudioMode = 0;
    unsupportedMode = 0;
    mapper154Mirroring = 0;
    mapper742ActiveNative = -1;
    mapper742InstalledAudioCount = 0;
    mapper742ExRAMStackPointer = 0;
    Mapper742_ResetMMC3Banks();
    Mapper742_ResetRAMBO();
}

static void Mapper742_Power(void)
{
    int audioMode;

    Mapper742_ResetRegisters();

    /* Capture all expansion-audio handlers before the emulated program starts.
     * Lazy capture would have to power a complete native mapper the first time
     * $4102 is written and could reset the currently-running mapper if both
     * selections use the same ASIC. */
    for (audioMode = 1; audioMode <= 4; ++audioMode)
        Mapper742_EnsureAudioCaptured((uint8)audioMode);

    Mapper742_ApplyMode(1);
    Mapper742_ApplyAudio(1);

    if (mapper742ActiveNative < 0)
        SetReadHandler(0x8000, 0xFFFF, CartBR);
    GameStateRestore = Mapper742_StateRestore;
    SetWriteHandler(0x4100, 0x410F, Mapper742_WriteSupervisor);
}

static void Mapper742_Reset(void)
{
    /* The specification gives reset values and explicitly unlocks on reset. */
    Mapper742_ResetRegisters();
    Mapper742_ApplyMode(1);
    Mapper742_ApplyAudio(1);

    if (mapper742ActiveNative < 0)
        SetReadHandler(0x8000, 0xFFFF, CartBR);
    GameStateRestore = Mapper742_StateRestore;
    SetWriteHandler(0x4100, 0x410F, Mapper742_WriteSupervisor);
}

static void Mapper742_StateRestore(int version)
{
    (void)version;
    Mapper742_ApplyMode(0);
    Mapper742_Sync();
    Mapper742_ApplyAudio(0);
    SetWriteHandler(0x4100, 0x410F, Mapper742_WriteSupervisor);
}


static void Mapper742_Close(void)
{
    int i;

    Mapper742_ClearAudioRoutes();
    for (i = 0; i < M742_NATIVE_COUNT; ++i)
    {
        if (mapper742Native[i].initialized && mapper742Native[i].close)
        {
            mapper742Native[i].close();
            mapper742Native[i].close = NULL;
        }
    }

    CartRAM_close();

    if (mapper742FallbackCHRRAM)
    {
        FCEU_gfree(mapper742FallbackCHRRAM);
        mapper742FallbackCHRRAM = NULL;
        mapper742FallbackCHRRAMSize = 0;
    }
}

void Mapper742_Init(CartInfo* info)
{
    Latch_addExState();
    MMC1_addExState();
    MMC24_addExState();
    MMC3_addExState();
    VRC1_addExState();
    VRC24_addExState();
    VRC3_addExState();
    VRC6_addExState();
    VRC7_addExState();

    CartRAM_init(info, 32, 8);

    /* NES 2.0 images using Mapper 119 should declare CHR-RAM.  Keep an
     * 8 KiB fallback chip for early test ROMs that omit that field. */
    if (!CHRRAMSize && CHRsize[0])
    {
        mapper742FallbackCHRRAMSize = 8 * 1024;
        mapper742FallbackCHRRAM = (uint8*)FCEU_gmalloc(mapper742FallbackCHRRAMSize);
        memset(mapper742FallbackCHRRAM, 0, mapper742FallbackCHRRAMSize);
        SetupCartCHRMapping(0x10, mapper742FallbackCHRRAM,
            mapper742FallbackCHRRAMSize, 1);
        AddExState(mapper742FallbackCHRRAM, mapper742FallbackCHRRAMSize,
            0, "M7CR");
    }

    mapper742CartInfo = info;
    Mapper742_SaveBaseMappings();
    memset(mapper742Native, 0, sizeof(mapper742Native));
    memset(mapper742Audio, 0, sizeof(mapper742Audio));
    memset(mapper742ExRAM, 0, sizeof(mapper742ExRAM));

    Mapper742_RegisterNativeCore(M742_NATIVE_MMC5, Mapper5_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_BANDAI, Mapper16_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_N163, Mapper19_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_VRC7_AUDIO, Mapper85_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_MAPPER80, Mapper80_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_FME7, Mapper69_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_MAPPER112, Mapper112_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_JY, Mapper90_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_MAGIC_DRAGON, Mapper107_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_GTROM, Mapper111_Init);
    Mapper742_RegisterNativeCore(M742_NATIVE_UNROM512, UNROM512_Init);
    Mapper742_RestoreBaseMappings();

    info->Power = Mapper742_Power;
    info->Reset = Mapper742_Reset;
    info->Close = Mapper742_Close;
    GameStateRestore = Mapper742_StateRestore;

    AddExState(supervisor, sizeof(supervisor), 0, "SUPR");
    AddExState(&supervisorLocked, 1, 0, "LOCK");
    AddExState(&activeMapperMode, 1, 0, "AMOD");
    AddExState(&activeAudioMode, 1, 0, "AAUD");
    AddExState(&unsupportedMode, 1, 0, "UNSP");
    AddExState(mapper742ExRAM, sizeof(mapper742ExRAM), 0, "7EXR");
    AddExState(&mapper742ExRAMStackPointer, 2, 0, "7EXP");
    AddExState(&mapper154Mirroring, 1, 0, "M154");

    AddExState(&mapper742MMC3Command, 1, 0, "7MCM");
    AddExState(&mapper742MMC3WRAMControl, 1, 0, "7MWR");
    AddExState(mapper742ExtendedReg, sizeof(mapper742ExtendedReg), 0, "7MRG");
    AddExState(mapper742NesticleCHR, sizeof(mapper742NesticleCHR), 0, "7NCH");
    AddExState(mapper742NesticlePRG, sizeof(mapper742NesticlePRG), 0, "7NPR");
    AddExState(mapper118Mirroring, sizeof(mapper118Mirroring), 0, "7M18");
    AddExState(&mapper118PPUSlot, 1, 0, "7PPS");

    AddExState(&ramboCommand, 1, 0, "7RBC");
    AddExState(ramboReg, sizeof(ramboReg), 0, "7RBR");
    AddExState(&ramboMirroring, 1, 0, "7RBM");
    AddExState(&ramboIRQMode, 1, 0, "7RIM");
    AddExState(&ramboIRQCount, 1, 0, "7RIC");
    AddExState(&ramboIRQEnabled, 1, 0, "7RIE");
    AddExState(&ramboIRQLatch, 1, 0, "7RIL");
    AddExState(&ramboIRQReload, 1, 0, "7RIR");
    AddExState(&ramboPrescaler, 4, 0, "7RIP");

    AddExState(&mapper33Is48, 1, 0, "733T");
    AddExState(mapper33Regs, sizeof(mapper33Regs), 0, "733R");
    AddExState(&mapper33Mirroring, 1, 0, "733M");
    AddExState(&mapper48IRQEnabled, 1, 0, "748E");
    AddExState(&mapper48IRQCount, 2, 0, "748C");
    AddExState(&mapper48IRQLatch, 2, 0, "748L");
}