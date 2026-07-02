#pragma once
#include "800_base.h"

namespace Mapper_800_0
{
    // base 8K prg register
    /*; PRG Bankswitching
    1111 1111 1111 1111
    ---- ---- ---- ----
    RAAA AAAA AAAA AAAA
    |||| |||| |||| ||||
    | ++ + ++++ ++++ ++++ - PRG ROM / RAM
    +------------------ - RAM / ROM toggle(0: ROM 1 : RAM)
    */
    constexpr uint16_t reg_prg_8k_8000_l = 0x4020;
    constexpr uint16_t reg_prg_8k_8000_h = 0x4021;
    constexpr uint16_t reg_prg_8k_A000_l = 0x4022;
    constexpr uint16_t reg_prg_8k_A000_h = 0x4023;
    constexpr uint16_t reg_prg_8k_C000_l = 0x4024;
    constexpr uint16_t reg_prg_8k_C000_h = 0x4025;
    constexpr uint16_t reg_prg_8k_E000_l = 0x4026;
    constexpr uint16_t reg_prg_8k_E000_h = 0x4027;
    constexpr uint16_t reg_prg_8k_6000_l = 0x4028;
    constexpr uint16_t reg_prg_8k_6000_h = 0x4029;

    // extern 16K prg register
    constexpr uint16_t reg_prg_16k_8000_l = 0x402A;
    constexpr uint16_t reg_prg_16k_8000_h = 0x402B;

    // irq mode register
    /*
    0: Relative scanline (like MMC3 scanline)
    1: Absolute scanline (like MMC5 scanline)
    2: cycle (like VRC4 cycle mode)
    3: scanline (like VRC4 scanline mode)
    */
    constexpr uint16_t reg_irq_mode = 0x402C;
    constexpr uint16_t reg_irq_latch = 0x402D;
    constexpr uint16_t reg_irq_disable = 0x402E;
    constexpr uint16_t reg_irq_enable = 0x402F;

    // base 1K chr register
    /*; CHR Bankswitching
    1111 1111 1111 1111
    ---- ---- ---- ----
    RAAA AAAA AAAA AAAA
    |||| |||| |||| ||||
    | ++ + ++++ ++++ ++++ - CHR ROM / RAM
    +------------------ - RAM / ROM toggle(0: ROM 1 : RAM)
    */
    constexpr uint16_t reg_chr_1k_0000_l = 0x4030;
    constexpr uint16_t reg_chr_1k_0000_h = 0x4031;
    constexpr uint16_t reg_chr_1k_0400_l = 0x4032;
    constexpr uint16_t reg_chr_1k_0400_h = 0x4033;
    constexpr uint16_t reg_chr_1k_0800_l = 0x4034;
    constexpr uint16_t reg_chr_1k_0800_h = 0x4035;
    constexpr uint16_t reg_chr_1k_0C00_l = 0x4036;
    constexpr uint16_t reg_chr_1k_0C00_h = 0x4037;
    constexpr uint16_t reg_chr_1k_1000_l = 0x4038;
    constexpr uint16_t reg_chr_1k_1000_h = 0x4039;
    constexpr uint16_t reg_chr_1k_1400_l = 0x403A;
    constexpr uint16_t reg_chr_1k_1400_h = 0x403B;
    constexpr uint16_t reg_chr_1k_1800_l = 0x403C;
    constexpr uint16_t reg_chr_1k_1800_h = 0x403D;
    constexpr uint16_t reg_chr_1k_1C00_l = 0x403E;
    constexpr uint16_t reg_chr_1k_1C00_h = 0x403F;

    // extern 2K chr register
    constexpr uint16_t reg_chr_2k_0000_l = 0x4040;
    constexpr uint16_t reg_chr_2k_0000_h = 0x4041;
    constexpr uint16_t reg_chr_2k_0800_l = 0x4042;
    constexpr uint16_t reg_chr_2k_0800_h = 0x4043;
    constexpr uint16_t reg_chr_2k_1000_l = 0x4044;
    constexpr uint16_t reg_chr_2k_1000_h = 0x4045;
    constexpr uint16_t reg_chr_2k_1800_l = 0x4046;
    constexpr uint16_t reg_chr_2k_1800_h = 0x4047;

    // extern 4K chr register
    constexpr uint16_t reg_chr_4k_0000_l = 0x4048;
    constexpr uint16_t reg_chr_4k_0000_h = 0x4049;
    constexpr uint16_t reg_chr_4k_1000_l = 0x404A;
    constexpr uint16_t reg_chr_4k_1000_h = 0x404B;

    constexpr uint16_t reg_chr_8k_0000_l = 0x404C;
    constexpr uint16_t reg_chr_8k_0000_h = 0x404D;

    // nametable mirroring register
    /*
    0: V
    1: H
    2: 1ScA
    3: 1ScB
    4: 1ScC
    5: 1ScD
    6: 4Sc
    */
    constexpr uint16_t reg_nt_mirroring = 0x404E;

    // ExRAM Nametable register
    /*
    0: Off
    1: Mapping $4800-4FFF as NTRAM
    2: Mapping $5000-57FF as NTRAM
    3: Mapping $5000-57FF as NTRAM and $5000-57FF as Extra NTRAM
    */
    constexpr uint16_t reg_nt_mapping = 0x404F;

    // dma register
    constexpr uint16_t reg_dma_src_l = 0x4050;
    constexpr uint16_t reg_dma_src_h = 0x4051;
    constexpr uint16_t reg_dma_dst_l = 0x4052;
    constexpr uint16_t reg_dma_dst_h = 0x4053;
    constexpr uint16_t reg_dma_size_l = 0x4054;
    constexpr uint16_t reg_dma_size_h = 0x4055;

    // dma exec mode
    /*
    0: CPU -> CPU
    1: CPU -> PPU
    2: PPU -> CPU
    3: PPU -> PPU
    4: Val -> CPU
    5: Val -> PPU
    */
    constexpr uint16_t reg_dma_exec = 0x4056;

    // ExRAM Page
    constexpr uint16_t reg_exram_4080 = 0x4058;
    constexpr uint16_t reg_exram_4100 = 0x4059;
    constexpr uint16_t reg_exram_4200 = 0x405A;
    constexpr uint16_t reg_exram_4400 = 0x405B;
    constexpr uint16_t reg_exram_4800 = 0x405C;
    constexpr uint16_t reg_exram_5000 = 0x405D;

    // 16Bit Multiply
    constexpr uint16_t reg_mul_op1_l = 0x4060;
    constexpr uint16_t reg_mul_op1_h = 0x4061;
    constexpr uint16_t reg_mul_op2_l = 0x4062;
    constexpr uint16_t reg_mul_op2_h = 0x4063;

    constexpr uint16_t reg_mul_out_ll = 0x4064;
    constexpr uint16_t reg_mul_out_lh = 0x4065;
    constexpr uint16_t reg_mul_out_hl = 0x4066;
    constexpr uint16_t reg_mul_out_hh = 0x4067;

    // 16Bit BDC
    constexpr uint16_t reg_bcd_in_l = 0x4068;
    constexpr uint16_t reg_bcd_in_h = 0x4069;
    constexpr uint16_t reg_bcd_out_1 = 0x406A;
    constexpr uint16_t reg_bcd_out_10 = 0x406B;
    constexpr uint16_t reg_bcd_out_100 = 0x406C;
    constexpr uint16_t reg_bcd_out_1000 = 0x406D;
    constexpr uint16_t reg_bcd_out_10000 = 0x406E;

    // Multicart PRG/CHR offset and mask
    constexpr uint16_t reg_prg_8K_offset_l = 0x4070;
    constexpr uint16_t reg_prg_8K_offset_h = 0x4071;
    constexpr uint16_t reg_prg_8K_mask_l = 0x4072;
    constexpr uint16_t reg_prg_8K_mask_h = 0x4073;
    constexpr uint16_t reg_chr_1K_offset_l = 0x4074;
    constexpr uint16_t reg_chr_1K_offset_h = 0x4075;
    constexpr uint16_t reg_chr_1K_mask_l = 0x4076;
    constexpr uint16_t reg_chr_1K_mask_h = 0x4077;

    constexpr uint16_t reg_begin = 0x4020;
    constexpr uint16_t reg_end = 0x407F;

    void DMA_Sync(int mode, uint32_t src, uint32_t dst, uint32_t size);
    void Mul_Sync() noexcept;
    void Bcd_Sync() noexcept;
    void Sync(void);
    uint8 MapperRead(uint32 A) noexcept;
    void MapperWrite(uint32 A, uint8 V) noexcept;
    uint8 ExRAM_5000_Read(uint32 A) noexcept;
    void ExRAM_5000_Write(uint32 A, uint8 V) noexcept;
    uint8 ExRAM_4800_Read(uint32 A) noexcept;
    void ExRAM_4800_Write(uint32 A, uint8 V) noexcept;
    uint8 ExRAM_4400_Read(uint32 A) noexcept;
    void ExRAM_4400_Write(uint32 A, uint8 V) noexcept;
    uint8 ExRAM_4200_Read(uint32 A) noexcept;
    void ExRAM_4200_Write(uint32 A, uint8 V) noexcept;
    uint8 ExRAM_4100_Read(uint32 A) noexcept;
    void ExRAM_4100_Write(uint32 A, uint8 V) noexcept;
    uint8 ExRAM_4080_Read(uint32 A) noexcept;
    void ExRAM_4080_Write(uint32 A, uint8 V) noexcept;
    void MapperReset(void);
    void MapperPower(void);
    void MapperClose(void);
    void StateRestore(int version);
    void HBlank(int scanline);
    void Init(CartInfo* info);
}
