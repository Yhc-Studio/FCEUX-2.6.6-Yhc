#include "800_0.h"
#include "debug.h"

namespace Mapper_800_0
{
	struct _reg
	{
		struct {
			union {
				struct {
					uint16_t data : 15;
					uint16_t ram : 1;
				};

				struct {
					uint16_t low : 8;
					uint16_t high : 8;
				};
			};

			uint16_t reserved : 16;

		}prg8k[5], prg16k[1], chr1k[8], chr2k[4], chr4k[2], chr8k[1];

		struct _nt_mirroring {
			uint8_t data : 3;
			uint8_t unused : 5;
		}nt_mirroring;

		struct _irq_mode {
			uint8_t data : 2;
			uint8_t unused : 6;
		}irq_mode;

		struct _irq_enable {
			uint8_t data : 1;
			uint8_t unused : 7;
		}irq_enable;

		union _vram_dma {
			struct {
				uint16_t low : 8;
				uint16_t high : 8;
			};
			uint16_t data;
		}dma_src, dma_dst, dma_size;

		struct {
			union _multi {
				struct {
					uint16_t low : 8;
					uint16_t high : 8;
				};
				uint16_t data;
			}offset, mask;
		}multi_prg, multi_chr;

		int16_t irq_count;
		int16_t irq_acount;
		uint8_t irq_latch;
		uint8_t pal_count;
		uint8_t dma_exec;

		uint8_t exram_5000;
		uint8_t exram_4800;
		uint8_t exram_4400;
		uint8_t exram_4200;
		uint8_t exram_4100;
		uint8_t exram_4080;
		uint8_t nt_mapping;

		uint8_t mul_in[4];
		uint8_t mul_out[4];
		uint8_t bcd_in[2];
		uint8_t bcd_out[5];

	}reg;

	uint32_t WRAM_8K_COUNT = 1024;
	uint32_t WRAM_8K_MASK = WRAM_8K_COUNT - 1;
	uint32_t WRAM_SIZE = 0x2000 * WRAM_8K_COUNT;
	uint32_t WRAM_PAGE_INDEX = 0x10;
	uint8_t* WRAM = nullptr;

	uint32_t CHR_RAM_1K_COUNT = 1024;
	uint32_t CHR_RAM_1K_MASK = CHR_RAM_1K_COUNT - 1;
	uint32_t CHR_RAM_SIZE = 0x1024 * CHR_RAM_1K_COUNT;
	uint32_t CHR_RAM_PAGE_INDEX = 0x10;
	uint8_t* CHR_RAM = nullptr;

	uint8_t* ExRAM_5000 = nullptr;
	uint32_t ExRAM_5000_BANK_SIZE = 0x1000;
	uint32_t ExRAM_5000_BANK_SIZE_MASK = ExRAM_5000_BANK_SIZE - 1;
	uint32_t ExRAM_5000_BANK_COUNT = 1024;
	uint32_t ExRAM_5000_BANK_MASK = ExRAM_5000_BANK_COUNT - 1;
	uint32_t ExRAM_5000_DATA_SIZE = ExRAM_5000_BANK_SIZE * ExRAM_5000_BANK_COUNT;

	uint8_t* ExRAM_4800 = nullptr;
	uint32_t ExRAM_4800_BANK_SIZE = 0x0800;
	uint32_t ExRAM_4800_BANK_SIZE_MASK = ExRAM_4800_BANK_SIZE - 1;
	uint32_t ExRAM_4800_BANK_COUNT = 1024;
	uint32_t ExRAM_4800_BANK_MASK = ExRAM_4800_BANK_COUNT - 1;
	uint32_t ExRAM_4800_DATA_SIZE = ExRAM_4800_BANK_SIZE * ExRAM_4800_BANK_COUNT;

	uint8_t* ExRAM_4400 = nullptr;
	uint32_t ExRAM_4400_BANK_SIZE = 0x0400;
	uint32_t ExRAM_4400_BANK_SIZE_MASK = ExRAM_4400_BANK_SIZE - 1;
	uint32_t ExRAM_4400_BANK_COUNT = 1024;
	uint32_t ExRAM_4400_BANK_MASK = ExRAM_4400_BANK_COUNT - 1;
	uint32_t ExRAM_4400_DATA_SIZE = ExRAM_4400_BANK_SIZE * ExRAM_4400_BANK_COUNT;

	uint8_t* ExRAM_4200 = nullptr;
	uint32_t ExRAM_4200_BANK_SIZE = 0x0200;
	uint32_t ExRAM_4200_BANK_SIZE_MASK = ExRAM_4200_BANK_SIZE - 1;
	uint32_t ExRAM_4200_BANK_COUNT = 1024;
	uint32_t ExRAM_4200_BANK_MASK = ExRAM_4200_BANK_COUNT - 1;
	uint32_t ExRAM_4200_DATA_SIZE = ExRAM_4200_BANK_SIZE * ExRAM_4200_BANK_COUNT;

	uint8_t* ExRAM_4100 = nullptr;
	uint32_t ExRAM_4100_BANK_SIZE = 0x0100;
	uint32_t ExRAM_4100_BANK_SIZE_MASK = ExRAM_4100_BANK_SIZE - 1;
	uint32_t ExRAM_4100_BANK_COUNT = 1024;
	uint32_t ExRAM_4100_BANK_MASK = ExRAM_4100_BANK_COUNT - 1;
	uint32_t ExRAM_4100_DATA_SIZE = ExRAM_4100_BANK_SIZE * ExRAM_4100_BANK_COUNT;

	uint8_t* ExRAM_4080 = nullptr;
	uint32_t ExRAM_4080_BANK_SIZE = 0x0080;
	uint32_t ExRAM_4080_BANK_SIZE_MASK = ExRAM_4080_BANK_SIZE - 1;
	uint32_t ExRAM_4080_BANK_COUNT = 1024;
	uint32_t ExRAM_4080_BANK_MASK = ExRAM_4080_BANK_COUNT - 1;
	uint32_t ExRAM_4080_DATA_SIZE = ExRAM_4080_BANK_SIZE * ExRAM_4080_BANK_COUNT;

	uint32 prg_8k_count = 0;
	uint32 prg_4k_count = 0;
	uint32 chr_1k_count = 0;

	int battery = 0;

	SFORMAT StateRegs[] =
	{
		{ &reg.multi_prg, sizeof(reg.multi_prg),					"MPRG" },
		{ &reg.multi_chr, sizeof(reg.multi_chr),					"MCHR" },

		{ reg.prg8k, sizeof(reg.prg8k),								"PRG8" },
		{ reg.prg16k, sizeof(reg.prg16k),							"PRG16" },
		{ reg.chr1k, sizeof(reg.chr1k),								"CHR1" },
		{ reg.chr2k, sizeof(reg.chr2k),								"CHR2" },
		{ reg.chr4k, sizeof(reg.chr4k),								"CHR4" },
		{ reg.chr8k, sizeof(reg.chr8k),								"CHR8" },

		{ &reg.nt_mirroring, sizeof(reg.nt_mirroring),				"MIRR" },
		{ &reg.irq_mode, sizeof(reg.irq_mode),						"IRQM" },
		{ &reg.irq_latch, sizeof(reg.irq_latch),					"IRQL" },
		{ &reg.irq_enable, sizeof(reg.irq_enable),					"IRQE" },
		{ &reg.irq_count, sizeof(reg.irq_count),					"IRQC" },
		{ &reg.irq_acount, sizeof(reg.irq_acount),					"IRQA" },
		{ &reg.pal_count, sizeof(reg.pal_count),					"PALC" },

		{ &reg.dma_src, sizeof(reg.dma_src),						"DMAS" },
		{ &reg.dma_dst, sizeof(reg.dma_dst),						"DMAD" },
		{ &reg.dma_size, sizeof(reg.dma_size),						"DMAL" },
		{ &reg.dma_exec, sizeof(reg.dma_exec),						"DMAE" },

		{ &reg.exram_5000, sizeof(reg.exram_5000),					"5000" },
		{ &reg.exram_4800, sizeof(reg.exram_4800),					"4800" },
		{ &reg.exram_4400, sizeof(reg.exram_4400),					"4400" },
		{ &reg.exram_4200, sizeof(reg.exram_4200),					"4200" },
		{ &reg.exram_4100, sizeof(reg.exram_4100),					"4100" },
		{ &reg.exram_4080, sizeof(reg.exram_4080),					"4080" },

		{ &reg.nt_mapping, sizeof(reg.nt_mapping),					"NTMP" },

		{ &reg.mul_in, sizeof(reg.mul_in),							"MULI" },
		{ &reg.mul_out, sizeof(reg.mul_out),						"MULO" },

		{ &reg.bcd_in, sizeof(reg.bcd_in),							"BCDI" },
		{ &reg.bcd_out, sizeof(reg.bcd_out),						"BCDO" },

		{ 0 }
	};

	void MirrorSync()
	{
		FCEUPPU_LineUpdate();

		uint8* NtRAM = NTARAM;
		uint8* ExNtRAM = ExtraNTARAM;

		if (1 == reg.nt_mapping)
		{
			NtRAM = &ExRAM_4800[reg.exram_4800 * ExRAM_4800_BANK_SIZE];
		}

		if (2 == reg.nt_mapping)
		{
			NtRAM = &ExRAM_5000[reg.exram_5000 * ExRAM_5000_BANK_SIZE];
		}

		if (3 == reg.nt_mapping)
		{
			NtRAM = &ExRAM_5000[reg.exram_5000 * ExRAM_5000_BANK_SIZE];
			ExNtRAM = &ExRAM_5000[reg.exram_5000 * ExRAM_5000_BANK_SIZE + 0x0800];
		}

		switch (reg.nt_mirroring.data)
		{
		case 0:
			//setmirror(MI_V);
			vnapage[0] = vnapage[2] = NtRAM;
			vnapage[1] = vnapage[3] = NtRAM + 0x400;
			break;
		case 1:
			//setmirror(MI_H);
			vnapage[0] = vnapage[1] = NtRAM;
			vnapage[2] = vnapage[3] = NtRAM + 0x400;
			break;
		case 2:
			//setmirror(MI_0);
			vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = NtRAM;
			break;
		case 3:
			//setmirror(MI_1);
			vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = NtRAM + 0x400;
			break;
		case 4:
			if (ExNtRAM)
			{
				vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = ExNtRAM;
			}
			break;
		case 5:
			if (ExNtRAM)
			{
				vnapage[0] = vnapage[1] = vnapage[2] = vnapage[3] = ExNtRAM + 0x400;
			}
			break;
		case 6:
			vnapage[0] = &NtRAM[0x000];
			vnapage[1] = &NtRAM[0x400];
			if (ExNtRAM)
			{
				vnapage[2] = ExNtRAM;
				vnapage[3] = ExNtRAM + 0x400;
			}
			break;
		default:
			break;
		}
	}

	void Sync(void)
	{
		MirrorSync();

		constexpr int reg_prg16_count = sizeof(reg.prg16k) / sizeof(reg.prg16k[0]);
		constexpr int reg_prg8_count = sizeof(reg.prg8k) / sizeof(reg.prg8k[0]);

		for (int i = 0; i < reg_prg16_count; i++)
		{
			const uint32_t bank = reg.prg16k[i].data;
			const uint32_t ram = reg.prg16k[i].ram;
			if (0 != reg.prg16k[i].reserved)
			{
				for (int j = 0; j < (1 << 1); j++)
				{
					reg.prg8k[1 + (i << 1) + j].data = (bank << 1) | j;
					reg.prg8k[1 + (i << 1) + j].ram = ram;
				}

				reg.prg16k[i].reserved = 0;
			}
		}

		for (int i = 0; i < reg_prg8_count; i++)
		{
			const uint32_t bank = reg.prg8k[i].data % prg_8k_count;
			const uint32_t ram = reg.prg8k[i].ram;
			const uint32_t addr = 0x6000 + i * 0x2000;

			const uint32_t reg_prg_8K_offset = reg.multi_prg.offset.data;
			const uint32_t reg_prg_8K_mask = reg.multi_prg.mask.data;
			if (reg.prg8k[i].ram)
			{
				setprg8r(WRAM_PAGE_INDEX, addr, bank & WRAM_8K_MASK);
			}
			else
			{
				setprg8(addr, reg_prg_8K_offset + (bank & reg_prg_8K_mask));
			}
		}

		constexpr int reg_chr8_count = sizeof(reg.chr8k) / sizeof(reg.chr8k[0]);
		constexpr int reg_chr4_count = sizeof(reg.chr4k) / sizeof(reg.chr4k[0]);
		constexpr int reg_chr2_count = sizeof(reg.chr2k) / sizeof(reg.chr2k[0]);
		constexpr int reg_chr1_count = sizeof(reg.chr1k) / sizeof(reg.chr1k[0]);

		for (int i = 0; i < reg_chr8_count; i++)
		{
			const uint32_t bank = reg.chr8k[i].data;
			const uint32_t ram = reg.chr8k[i].ram;
			if (0 != reg.chr8k[i].reserved)
			{
				for (int j = 0; j < (1 << 1); j++)
				{
					reg.chr4k[(i << 1) + j].data = (bank << 1) | j;
					reg.chr4k[(i << 1) + j].ram = ram;
				}

				for (int j = 0; j < (1 << 2); j++)
				{
					reg.chr2k[(i << 2) + j].data = (bank << 2) | j;
					reg.chr2k[(i << 2) + j].ram = ram;
				}

				for (int j = 0; j < (1 << 3); j++)
				{
					reg.chr1k[(i << 3) + j].data = (bank << 3) | j;
					reg.chr1k[(i << 3) + j].ram = ram;
				}

				reg.chr8k[i].reserved = 0;
			}
		}

		for (int i = 0; i < reg_chr4_count; i++)
		{
			const uint32_t bank = reg.chr4k[i].data;
			const uint32_t ram = reg.chr4k[i].ram;
			if (0 != reg.chr4k[i].reserved)
			{
				for (int j = 0; j < (1 << 1); j++)
				{
					reg.chr2k[(i << 1) + j].data = (bank << 1) | j;
					reg.chr2k[(i << 1) + j].ram = ram;
				}

				for (int j = 0; j < (1 << 2); j++)
				{
					reg.chr1k[(i << 2) + j].data = (bank << 2) | j;
					reg.chr1k[(i << 2) + j].ram = ram;
				}

				reg.chr4k[i].reserved = 0;
			}
		}

		for (int i = 0; i < reg_chr2_count; i++)
		{
			const uint32_t bank = reg.chr2k[i].data;
			const uint32_t ram = reg.chr2k[i].ram;

			if (0 != reg.chr2k[i].reserved)
			{
				for (int j = 0; j < (1 << 1); j++)
				{
					reg.chr1k[(i << 1) + j].data = (bank << 1) | j;
					reg.chr1k[(i << 1) + j].ram = ram;
				}

				reg.chr2k[i].reserved = 0;
			}
		}

		constexpr int reg_chr_count = sizeof(reg.chr1k) / sizeof(reg.chr1k[0]);
		if (chr_1k_count)
		{
			for (int i = 0; i < reg_chr_count; i++)
			{
				const uint32_t bank = reg.chr1k[i].data;
				const uint32_t addr = i * 0x0400;

				const uint32_t reg_chr_1K_offset = reg.multi_chr.offset.data;
				const uint32_t reg_chr_1K_mask = reg.multi_chr.mask.data;

				if (reg.chr1k[i].ram)
				{
					setchr1r(CHR_RAM_PAGE_INDEX, addr, bank & CHR_RAM_1K_MASK);
				}
				else
				{
					setchr1(addr, reg_chr_1K_offset + (bank & reg_chr_1K_mask));
				}
			}
		}
		else
		{
			for (int i = 0; i < reg_chr_count; i++)
			{
				const uint32_t bank = reg.chr1k[i].data / CHR_RAM_1K_MASK;
				const uint32_t addr = i * 0x0400;
				setchr1r(CHR_RAM_PAGE_INDEX, addr, bank & CHR_RAM_1K_MASK);
			}
		}
	}

	void DMA_Sync(int mode, uint32_t src, uint32_t dst, uint32_t size)
	{
		// CPU -> CPU
		if (0 == reg.dma_exec)
		{
			for (uint32_t i = 0; i < size; i++)
			{
				const uint32_t cpu_src = src + i;
				const uint32_t cpu_dst = dst + i;
				if (cpu_src >= 0x2000 && cpu_src <= 0x401F)
				{
					continue;
				}

				if (cpu_src >= reg_begin && cpu_src <= reg_end)
				{
					continue;
				}

				if (cpu_dst >= 0x2000 && cpu_dst <= 0x401F)
				{
					continue;
				}

				if (cpu_dst >= reg_begin && cpu_dst <= reg_end)
				{
					continue;
				}

				const uint8_t data = ARead[cpu_src](cpu_src);
				BWrite[cpu_dst](cpu_dst, data);
			}
		}

		// CPU -> PPU
		if (1 == reg.dma_exec)
		{
			for (uint32_t i = 0; i < size; i++)
			{
				const uint32_t cpu_src = src + i;
				const uint32_t ppu_dst = dst + i;

				if (cpu_src >= 0x2000 && cpu_src <= 0x401F)
				{
					continue;
				}

				if (cpu_src >= reg_begin && cpu_src <= reg_end)
				{
					continue;
				}

				if (ppu_dst >= 0x4000)
				{
					continue;
				}

				const uint8_t data = GetMem(cpu_src);
				FFCEUX_PPUWrite_Default(ppu_dst, data);
			}
		}

		// PPU -> CPU
		if (2 == reg.dma_exec)
		{
			for (uint32_t i = 0; i < size; i++)
			{
				const uint32_t ppu_src = src + i;
				const uint32_t cpu_dst = dst + i;

				if (ppu_src >= 0x4000)
				{
					continue;
				}

				if (cpu_dst >= 0x2000 && cpu_dst <= 0x401F)
				{
					continue;
				}

				if (cpu_dst >= reg_begin && cpu_dst <= reg_end)
				{
					continue;
				}

				const uint8_t data = FFCEUX_PPURead_Default(ppu_src);
				BWrite[cpu_dst](cpu_dst, data);
			}
		}

		// PPU -> PPU
		if (3 == reg.dma_exec)
		{
			for (uint32_t i = 0; i < size; i++)
			{
				const uint32_t ppu_src = src + i;
				const uint32_t ppu_dst = dst + i;

				if (ppu_src >= 0x4000)
				{
					continue;
				}

				if (ppu_dst >= 0x4000)
				{
					continue;
				}

				const uint8_t data = FFCEUX_PPURead_Default(ppu_src);
				FFCEUX_PPUWrite_Default(ppu_dst, data);
			}
		}

		// CPU -> value
		if (4 == reg.dma_exec)
		{
			const uint8_t data = src & 0xFF;
			for (uint32_t i = 0; i < size; i++)
			{
				const uint32_t cpu_dst = dst + i;
				if (cpu_dst >= 0x2000 && cpu_dst <= 0x401F)
				{
					continue;
				}

				if (cpu_dst >= reg_begin && cpu_dst <= reg_end)
				{
					continue;
				}

				BWrite[cpu_dst](cpu_dst, data);
			}
		}

		// CPU -> value
		if (5 == reg.dma_exec)
		{
			const uint8_t data = src & 0xFF;
			for (uint32_t i = 0; i < size; i++)
			{
				const uint32_t ppu_dst = dst + i;
				if (ppu_dst >= 0x4000)
				{
					continue;
				}

				FFCEUX_PPUWrite_Default(ppu_dst, data);
			}
		}
	}

	uint8 MapperRead(uint32 A) noexcept
	{
		switch (A)
		{
		case reg_prg_8k_6000_l:
			return reg.prg8k[0].low;
			break;
		case reg_prg_8k_8000_l:
			return reg.prg8k[1].low;
			break;
		case reg_prg_8k_A000_l:
			return reg.prg8k[2].low;
			break;
		case reg_prg_8k_C000_l:
			return reg.prg8k[3].low;
			break;
		case reg_prg_8k_E000_l:
			return reg.prg8k[4].low;
			break;

		case reg_prg_8k_6000_h:
			return reg.prg8k[0].high;
			break;
		case reg_prg_8k_8000_h:
			return reg.prg8k[1].high;
			break;
		case reg_prg_8k_A000_h:
			return reg.prg8k[2].high;
			break;
		case reg_prg_8k_C000_h:
			return reg.prg8k[3].high;
			break;
		case reg_prg_8k_E000_h:
			return reg.prg8k[4].high;
			break;

		case reg_prg_16k_8000_l:
			return reg.prg16k[0].low;
			break;
		case reg_prg_16k_8000_h:
			return reg.prg16k[0].high;
			break;

		case reg_chr_1k_0000_l:
			return reg.chr1k[0x00].low;
			break;
		case reg_chr_1k_0400_l:
			return reg.chr1k[0x01].low;
			break;
		case reg_chr_1k_0800_l:
			return reg.chr1k[0x02].low;
			break;
		case reg_chr_1k_0C00_l:
			return reg.chr1k[0x03].low;
			break;
		case reg_chr_1k_1000_l:
			return reg.chr1k[0x04].low;
			break;
		case reg_chr_1k_1400_l:
			return reg.chr1k[0x05].low;
			break;
		case reg_chr_1k_1800_l:
			return reg.chr1k[0x06].low;
			break;
		case reg_chr_1k_1C00_l:
			return reg.chr1k[0x07].low;
			break;
		case reg_chr_1k_0000_h:
			return reg.chr1k[0x00].high;
			break;
		case reg_chr_1k_0400_h:
			return reg.chr1k[0x01].high;
			break;
		case reg_chr_1k_0800_h:
			return reg.chr1k[0x02].high;
			break;
		case reg_chr_1k_0C00_h:
			return reg.chr1k[0x03].high;
			break;
		case reg_chr_1k_1000_h:
			return reg.chr1k[0x04].high;
			break;
		case reg_chr_1k_1400_h:
			return reg.chr1k[0x05].high;
			break;
		case reg_chr_1k_1800_h:
			return reg.chr1k[0x06].high;
			break;
		case reg_chr_1k_1C00_h:
			return reg.chr1k[0x07].high;
			break;

		case reg_chr_2k_0000_l:
			return reg.chr2k[0x00].low;
			break;
		case reg_chr_2k_0800_l:
			return reg.chr2k[0x01].low;
			break;
		case reg_chr_2k_1000_l:
			return reg.chr2k[0x02].low;
			break;
		case reg_chr_2k_1800_l:
			return reg.chr2k[0x03].low;
			break;
		case reg_chr_2k_0000_h:
			return reg.chr2k[0x00].high;
			break;
		case reg_chr_2k_0800_h:
			return reg.chr2k[0x01].high;
			break;
		case reg_chr_2k_1000_h:
			return reg.chr2k[0x02].high;
			break;
		case reg_chr_2k_1800_h:
			return reg.chr2k[0x03].high;
			break;

		case reg_chr_4k_0000_l:
			return reg.chr4k[0x00].low;
			break;
		case reg_chr_4k_1000_l:
			return reg.chr4k[0x01].low;
			break;
		case reg_chr_4k_0000_h:
			return reg.chr4k[0x00].high;
			break;
		case reg_chr_4k_1000_h:
			return reg.chr4k[0x01].high;
			break;

		case reg_chr_8k_0000_l:
			return reg.chr8k[0x00].low;
			break;
		case reg_chr_8k_0000_h:
			return reg.chr8k[0x00].high;
			break;

		case reg_nt_mirroring:
			return reg.nt_mirroring.data;
			break;
		case reg_irq_latch:
			return reg.irq_latch;
			break;
		case reg_irq_mode:
			return reg.irq_mode.data;
			break;
		case reg_irq_enable:
			return reg.irq_enable.data;
			break;

		case reg_exram_4080:
			return reg.exram_4080;
			break;

		case reg_exram_4100:
			return reg.exram_4100;
			break;

		case reg_exram_4200:
			return reg.exram_4200;
			break;

		case reg_exram_4400:
			return reg.exram_4400;
			break;

		case reg_exram_4800:
			return reg.exram_4800;
			break;

		case reg_exram_5000:
			return reg.exram_5000;
			break;

		case reg_dma_src_l:
			return reg.dma_src.low;
			break;
		case reg_dma_src_h:
			return reg.dma_src.high;
			break;
		case reg_dma_dst_l:
			return reg.dma_dst.low;
			break;
		case reg_dma_dst_h:
			return reg.dma_dst.high;
			break;
		case reg_dma_size_l:
			return reg.dma_size.low;
			break;
		case reg_dma_size_h:
			return reg.dma_size.high;
			break;
		case reg_dma_exec:
			return reg.dma_exec;
			break;

		case reg_nt_mapping:
			return reg.nt_mapping;
			break;

		case reg_mul_op1_l:
			return reg.mul_in[0];
			break;

		case reg_mul_op1_h:
			return reg.mul_in[1];
			break;

		case reg_mul_op2_l:
			return reg.mul_in[2];
			break;

		case reg_mul_op2_h:
			return reg.mul_in[3];
			break;

		case reg_mul_out_ll:
			return reg.mul_out[0];
			break;

		case reg_mul_out_lh:
			return reg.mul_out[1];
			break;

		case reg_mul_out_hl:
			return reg.mul_out[2];
			break;

		case reg_mul_out_hh:
			return reg.mul_out[3];
			break;

		case reg_bcd_in_l:
			return reg.bcd_in[0];
			break;

		case reg_bcd_in_h:
			return reg.bcd_in[1];
			break;

		case reg_bcd_out_1:
			return reg.bcd_out[0];
			break;

		case reg_bcd_out_10:
			return reg.bcd_out[1];
			break;

		case reg_bcd_out_100:
			return reg.bcd_out[2];
			break;

		case reg_bcd_out_1000:
			return reg.bcd_out[3];
			break;

		case reg_bcd_out_10000:
			return reg.bcd_out[4];
			break;

		case reg_prg_8K_offset_l:
			return reg.multi_prg.offset.low;
			break;
		case reg_prg_8K_offset_h:
			return reg.multi_prg.offset.high;
			break;
		case reg_prg_8K_mask_l:
			return reg.multi_prg.mask.low;
			break;
		case reg_prg_8K_mask_h:
			return reg.multi_prg.mask.high;
			break;
		case reg_chr_1K_offset_l:
			return reg.multi_chr.offset.low;
			break;
		case reg_chr_1K_offset_h:
			return reg.multi_chr.offset.high;
			break;
		case reg_chr_1K_mask_l:
			return reg.multi_chr.mask.low;
			break;
		case reg_chr_1K_mask_h:
			return reg.multi_chr.mask.high;
			break;

		default:
			break;
		}

		return 0;
	}

	void Mul_Sync() noexcept
	{
		const uint32_t res = (reg.mul_in[0] | (reg.mul_in[1] << 8)) * (reg.mul_in[2] | (reg.mul_in[3] << 8));
		reg.mul_out[0] = (res & 0x000000FF);
		reg.mul_out[1] = (res & 0x0000FF00) >> 8;
		reg.mul_out[2] = (res & 0x00FF0000) >> 16;
		reg.mul_out[3] = (res & 0xFF000000) >> 24;
	}

	void Bcd_Sync() noexcept
	{
		uint16_t data = (reg.bcd_in[0] | (reg.bcd_in[1] << 8));
		int idx = 0;
		memset(reg.bcd_out, 0, sizeof(reg.bcd_out));
		while (data > 0)
		{
			const uint16_t remainder = data % 10;
			reg.bcd_out[idx++] = remainder;
			data /= 10;
		}
	}

	void MapperWrite(uint32 A, uint8 V) noexcept
	{
		switch (A)
		{
			/*
			 * PRG 8KB low 8 bits
			 */
		case reg_prg_8k_6000_l:
			reg.prg8k[0].low = V;
			reg.prg8k[0].reserved = 1;

			Sync();
			break;
		case reg_prg_8k_8000_l:
			reg.prg8k[1].low = V;
			reg.prg8k[1].reserved = 1;

			reg.prg16k[0].reserved = 0;
			Sync();
			break;
		case reg_prg_8k_A000_l:
			reg.prg8k[2].low = V;
			reg.prg8k[2].reserved = 1;

			reg.prg16k[0].reserved = 0;
			Sync();
			break;
		case reg_prg_8k_C000_l:
			reg.prg8k[3].low = V;
			reg.prg8k[3].reserved = 1;
			Sync();
			break;
		case reg_prg_8k_E000_l:
			reg.prg8k[4].low = V;
			reg.prg8k[4].reserved = 1;
			Sync();
			break;

			/*
			 * PRG 8KB high 8 bits
			 */
		case reg_prg_8k_6000_h:
			reg.prg8k[0].high = V;
			reg.prg8k[0].reserved = 1;

			Sync();
			break;
		case reg_prg_8k_8000_h:
			reg.prg8k[1].high = V;
			reg.prg8k[1].reserved = 1;

			reg.prg16k[0].reserved = 0;
			Sync();
			break;
		case reg_prg_8k_A000_h:
			reg.prg8k[2].high = V;
			reg.prg8k[2].reserved = 1;

			reg.prg16k[0].reserved = 0;
			Sync();
			break;
		case reg_prg_8k_C000_h:
			reg.prg8k[3].high = V;
			reg.prg8k[3].reserved = 1;
			Sync();
			break;
		case reg_prg_8k_E000_h:
			reg.prg8k[4].high = V;
			reg.prg8k[4].reserved = 1;
			Sync();
			break;

			/*
			 * PRG 16KB low 8 bits
			 */
		case reg_prg_16k_8000_l:
			reg.prg16k[0].low = V;
			reg.prg16k[0].reserved = 1;

			reg.prg8k[0].reserved = 0;
			reg.prg8k[1].reserved = 0;
			Sync();
			break;
			/*
			 * PRG 16KB high 8 bits
			 */
		case reg_prg_16k_8000_h:
			reg.prg16k[0].high = V;
			reg.prg16k[0].reserved = 1;

			reg.prg8k[0].reserved = 0;
			reg.prg8k[1].reserved = 0;
			Sync();
			break;

			/*
			 * CHR 1KB low 8 bits
			 */
		case reg_chr_1k_0000_l:
			reg.chr1k[0x00].low = V;
			Sync();
			break;
		case reg_chr_1k_0400_l:
			reg.chr1k[0x01].low = V;
			Sync();
			break;
		case reg_chr_1k_0800_l:
			reg.chr1k[0x02].low = V;
			Sync();
			break;
		case reg_chr_1k_0C00_l:
			reg.chr1k[0x03].low = V;
			Sync();
			break;
		case reg_chr_1k_1000_l:
			reg.chr1k[0x04].low = V;
			Sync();
			break;
		case reg_chr_1k_1400_l:
			reg.chr1k[0x05].low = V;
			Sync();
			break;
		case reg_chr_1k_1800_l:
			reg.chr1k[0x06].low = V;
			Sync();
			break;
		case reg_chr_1k_1C00_l:
			reg.chr1k[0x07].low = V;
			Sync();
			break;

			/*
			 * CHR 2KB low 8 bits
			 */
		case reg_chr_2k_0000_l:
			reg.chr2k[0x00].low = V;
			reg.chr2k[0x00].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x00].reserved = 0;
			Sync();
			break;
		case reg_chr_2k_0800_l:
			reg.chr2k[0x01].low = V;
			reg.chr2k[0x01].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x00].reserved = 0;
			Sync();
			break;
		case reg_chr_2k_1000_l:
			reg.chr2k[0x02].low = V;
			reg.chr2k[0x02].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x01].reserved = 0;
			Sync();
			break;
		case reg_chr_2k_1800_l:
			reg.chr2k[0x03].low = V;
			reg.chr2k[0x03].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x01].reserved = 0;
			Sync();
			break;

			/*
			 * CHR 4KB low 8 bits
			 */
		case reg_chr_4k_0000_l:
			reg.chr4k[0x00].low = V;
			reg.chr4k[0x00].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr2k[0x00].reserved = 0;
			reg.chr2k[0x01].reserved = 0;
			Sync();
			break;
		case reg_chr_4k_1000_l:
			reg.chr4k[0x01].low = V;
			reg.chr4k[0x01].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr2k[0x02].reserved = 0;
			reg.chr2k[0x03].reserved = 0;
			Sync();
			break;

			/*
			 * CHR 8KB low 8 bits
			 */
		case reg_chr_8k_0000_l:
			reg.chr8k[0x00].low = V;
			reg.chr8k[0x00].reserved = 1;
			reg.chr4k[0x00].reserved = 0;
			reg.chr4k[0x01].reserved = 0;
			reg.chr2k[0x00].reserved = 0;
			reg.chr2k[0x01].reserved = 0;
			reg.chr2k[0x02].reserved = 0;
			reg.chr2k[0x03].reserved = 0;
			Sync();
			break;

			/*
			 * CHR 1KB high 8 bits
			 */
		case reg_chr_1k_0000_h:
			reg.chr1k[0x00].high = V;
			Sync();
			break;
		case reg_chr_1k_0400_h:
			reg.chr1k[0x01].high = V;
			Sync();
			break;
		case reg_chr_1k_0800_h:
			reg.chr1k[0x02].high = V;
			Sync();
			break;
		case reg_chr_1k_0C00_h:
			reg.chr1k[0x03].high = V;
			Sync();
			break;
		case reg_chr_1k_1000_h:
			reg.chr1k[0x04].high = V;
			Sync();
			break;
		case reg_chr_1k_1400_h:
			reg.chr1k[0x05].high = V;
			Sync();
			break;
		case reg_chr_1k_1800_h:
			reg.chr1k[0x06].high = V;
			Sync();
			break;
		case reg_chr_1k_1C00_h:
			reg.chr1k[0x07].high = V;
			Sync();
			break;

			/*
			 * CHR 2KB high 8 bits
			 */
		case reg_chr_2k_0000_h:
			reg.chr2k[0x00].high = V;
			reg.chr2k[0x00].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x00].reserved = 0;
			Sync();
			break;
		case reg_chr_2k_0800_h:
			reg.chr2k[0x01].high = V;
			reg.chr2k[0x01].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x00].reserved = 0;
			Sync();
			break;
		case reg_chr_2k_1000_h:
			reg.chr2k[0x02].high = V;
			reg.chr2k[0x02].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x01].reserved = 0;
			Sync();
			break;
		case reg_chr_2k_1800_h:
			reg.chr2k[0x03].high = V;
			reg.chr2k[0x03].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr4k[0x01].reserved = 0;
			Sync();
			break;

			/*
			 * CHR 4KB high 8 bits
			 */
		case reg_chr_4k_0000_h:
			reg.chr4k[0x00].high = V;
			reg.chr4k[0x00].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr2k[0x00].reserved = 0;
			reg.chr2k[0x01].reserved = 0;
			Sync();
			break;
		case reg_chr_4k_1000_h:
			reg.chr4k[0x01].high = V;
			reg.chr4k[0x01].reserved = 1;

			reg.chr8k[0x00].reserved = 0;
			reg.chr2k[0x02].reserved = 0;
			reg.chr2k[0x03].reserved = 0;
			Sync();
			break;

			/*
			 * CHR 8KB high 8 bits
			 */
		case reg_chr_8k_0000_h:
			reg.chr8k[0x00].high = V;
			reg.chr8k[0x00].reserved = 1;

			reg.chr4k[0x00].reserved = 0;
			reg.chr4k[0x01].reserved = 0;
			reg.chr2k[0x00].reserved = 0;
			reg.chr2k[0x01].reserved = 0;
			reg.chr2k[0x02].reserved = 0;
			reg.chr2k[0x03].reserved = 0;
			Sync();
			break;

			/*
			 * nt_mirroringoring
			 */
		case reg_nt_mirroring:
			reg.nt_mirroring.data = V;
			Sync();
			break;

		case reg_exram_4080:
			reg.exram_4080 = V;
			Sync();
			break;

		case reg_exram_4100:
			reg.exram_4100 = V;
			Sync();
			break;

		case reg_exram_4200:
			reg.exram_4200 = V;
			Sync();
			break;

		case reg_exram_4400:
			reg.exram_4400 = V;
			Sync();
			break;

		case reg_exram_4800:
			reg.exram_4800 = V;
			Sync();
			break;

		case reg_exram_5000:
			reg.exram_5000 = V;
			Sync();
			break;

			/*
			 * IRQ mode
			 */
		case reg_irq_mode:
			reg.irq_mode.data = V;
			break;

			/*
			 * IRQ latch
			 */
		case reg_irq_latch:
			reg.irq_latch = V;
			reg.irq_count = V;
			break;

			/*
			 * IRQ disable and acknowledge
			 */
		case reg_irq_disable:
			reg.irq_enable.data = 0;
			X6502_IRQEnd(FCEU_IQEXT);
			break;

			/*
			 * IRQ enable
			 */
		case reg_irq_enable:
			reg.irq_enable.data = 1;
			reg.irq_count = reg.irq_latch;
			X6502_IRQEnd(FCEU_IQEXT);
			break;

			/*
			 * DMA
			 */
		case reg_dma_src_l:
			reg.dma_src.low = V;
			break;
		case reg_dma_src_h:
			reg.dma_src.high = V;
			break;
		case reg_dma_dst_l:
			reg.dma_dst.low = V;
			break;
		case reg_dma_dst_h:
			reg.dma_dst.high = V;
			break;
		case reg_dma_size_l:
			reg.dma_size.low = V;
			break;
		case reg_dma_size_h:
			reg.dma_size.high = V;
			break;
		case reg_dma_exec:
		{
			reg.dma_exec = V;
			DMA_Sync(V, reg.dma_src.data, reg.dma_dst.data, reg.dma_size.data);
		}
		break;

		case reg_nt_mapping:
			reg.nt_mapping = V;
			Sync();
			break;

		case reg_mul_op1_l:
			reg.mul_in[0] = V;
			Mul_Sync();
			break;

		case reg_mul_op1_h:
			reg.mul_in[1] = V;
			Mul_Sync();
			break;

		case reg_mul_op2_l:
			reg.mul_in[2] = V;
			Mul_Sync();
			break;

		case reg_mul_op2_h:
			reg.mul_in[3] = V;
			Mul_Sync();
			break;

		case reg_bcd_in_l:
			reg.bcd_in[0] = V;
			Bcd_Sync();
			break;

		case reg_bcd_in_h:
			reg.bcd_in[1] = V;
			Bcd_Sync();
			break;

		case reg_prg_8K_offset_l:
			reg.multi_prg.offset.low = V;
			Sync();
			break;
		case reg_prg_8K_offset_h:
			reg.multi_prg.offset.high = V;
			Sync();
			break;
		case reg_prg_8K_mask_l:
			reg.multi_prg.mask.low = V;
			Sync();
			break;
		case reg_prg_8K_mask_h:
			reg.multi_prg.mask.high = V;
			Sync();
			break;
		case reg_chr_1K_offset_l:
			reg.multi_chr.offset.low = V;
			Sync();
			break;
		case reg_chr_1K_offset_h:
			reg.multi_chr.offset.high = V;
			Sync();
			break;
		case reg_chr_1K_mask_l:
			reg.multi_chr.mask.low = V;
			Sync();
			break;
		case reg_chr_1K_mask_h:
			reg.multi_chr.mask.high = V;
			Sync();
			break;
		default:
			break;
		}
	}

	uint8 ExRAM_5000_Read(uint32 A) noexcept
	{
		//uint32 tmp = A & 0x1FFF;
		//return VPage[tmp >> 10][tmp];

		return ExRAM_5000[A & ExRAM_5000_BANK_SIZE_MASK + reg.exram_5000 * ExRAM_5000_BANK_SIZE];
	}

	void ExRAM_5000_Write(uint32 A, uint8 V) noexcept
	{
		//uint32 tmp = A & 0x1FFF;
		//VPage[tmp >> 10][tmp] = V;
		ExRAM_5000[A & ExRAM_5000_BANK_SIZE_MASK + reg.exram_5000 * ExRAM_5000_BANK_SIZE] = V;
	}

	uint8 ExRAM_4800_Read(uint32 A) noexcept
	{
		return ExRAM_4800[A & ExRAM_4800_BANK_SIZE_MASK + reg.exram_4800 * ExRAM_4800_BANK_SIZE];
	}

	void ExRAM_4800_Write(uint32 A, uint8 V) noexcept
	{
		ExRAM_4800[A & ExRAM_4800_BANK_SIZE_MASK + reg.exram_4800 * ExRAM_4800_BANK_SIZE] = V;
	}

	uint8 ExRAM_4400_Read(uint32 A) noexcept
	{
		return ExRAM_4400[A & ExRAM_4400_BANK_SIZE_MASK + reg.exram_4400 * ExRAM_4400_BANK_SIZE];
	}

	void ExRAM_4400_Write(uint32 A, uint8 V) noexcept
	{
		ExRAM_4400[A & ExRAM_4400_BANK_SIZE_MASK + reg.exram_4400 * ExRAM_4400_BANK_SIZE] = V;
	}

	uint8 ExRAM_4200_Read(uint32 A) noexcept
	{
		return ExRAM_4200[A & ExRAM_4200_BANK_SIZE_MASK + reg.exram_4200 * ExRAM_4200_BANK_SIZE];
	}

	void ExRAM_4200_Write(uint32 A, uint8 V) noexcept
	{
		ExRAM_4200[A & ExRAM_4200_BANK_SIZE_MASK + reg.exram_4200 * ExRAM_4200_BANK_SIZE] = V;
	}

	uint8 ExRAM_4100_Read(uint32 A) noexcept
	{
		return ExRAM_4100[A & ExRAM_4100_BANK_SIZE_MASK + reg.exram_4100 * ExRAM_4100_BANK_SIZE];
	}

	void ExRAM_4100_Write(uint32 A, uint8 V) noexcept
	{
		ExRAM_4100[A & ExRAM_4100_BANK_SIZE_MASK + reg.exram_4100 * ExRAM_4100_BANK_SIZE] = V;
	}

	uint8 ExRAM_4080_Read(uint32 A) noexcept
	{
		return ExRAM_4080[A & ExRAM_4080_BANK_SIZE_MASK + reg.exram_4080 * ExRAM_4080_BANK_SIZE];
	}

	void ExRAM_4080_Write(uint32 A, uint8 V) noexcept
	{
		ExRAM_4080[A & ExRAM_4080_BANK_SIZE_MASK + reg.exram_4080 * ExRAM_4080_BANK_SIZE] = V;
	}

	void SetPRGReg(int index, uint32 bank) noexcept
	{
		constexpr int reg_prg8_count = sizeof(reg.prg8k) / sizeof(reg.prg8k[0]);
		if (index < reg_prg8_count)
		{
			reg.prg8k[index].data = bank;
			reg.prg8k[index].reserved = 1;
		}
	}

	void SetCHRReg(int index, uint32 bank) noexcept
	{
		constexpr int reg_chr1_count = sizeof(reg.chr1k) / sizeof(reg.chr1k[0]);
		if (index < reg_chr1_count)
		{
			reg.chr1k[index].data = bank;
			reg.chr1k[index].reserved = 1;
		}
	}

	void HBIRQHook(void)
	{
		if (!reg.irq_enable.data || 0 != reg.irq_mode.data)
		{
			return;
		}

		const int32_t count = reg.irq_count;
		if (!count)
		{
			X6502_IRQBegin(FCEU_IQEXT);
		}
		else
		{
			reg.irq_count--;
		}
	}

	void HBlank(int scanline)
	{
		if (!reg.irq_enable.data || 1 != reg.irq_mode.data)
		{
			return;
		}

		const int sl = scanline + 1;
		const int ppuon = (PPU[1] & 0x18);

		if (!ppuon || sl >= 241)
		{
			reg.irq_enable.data = 0;
			reg.irq_latch = 0;
			X6502_IRQEnd(FCEU_IQEXT);
			return;
		}

		if (scanline == reg.irq_latch)
		{
			X6502_IRQBegin(FCEU_IQEXT);
		}
	}

	void IRQHook(int a)
	{
		if (!reg.irq_enable.data || !(2 == reg.irq_mode.data || 3 == reg.irq_mode.data))
		{
			return;
		}

		/*if (vblankScanLines > 0)
		{
			return;
		}*/

		if (2 == reg.irq_mode.data)
		{
			if (FSettings.PAL)
			{
				constexpr int irq_cycle = 341;
				reg.irq_acount += a * 3;
				reg.pal_count += a;
				while (reg.pal_count >= 5)
				{
					reg.pal_count -= 5;
					reg.irq_acount += 1;
				}

				while (reg.irq_acount >= irq_cycle)
				{
					reg.irq_acount -= irq_cycle;
					reg.irq_count--;
					if (reg.irq_count < 0)
					{
						reg.irq_count = 0;
						X6502_IRQBegin(FCEU_IQEXT);
					}
				}
			}
			else
			{
				constexpr int irq_cycle = 341;
				reg.irq_acount += a * 3;
				while (reg.irq_acount >= irq_cycle)
				{
					reg.irq_acount -= irq_cycle;
					reg.irq_count--;
					if (reg.irq_count < 0)
					{
						reg.irq_count = 0;
						X6502_IRQBegin(FCEU_IQEXT);
					}
				}
			}
		}

		if (3 == reg.irq_mode.data)
		{
			reg.irq_acount += a;
			while (reg.irq_acount > 0)
			{
				reg.irq_acount--;
				reg.irq_count--;
				if (reg.irq_count < 0)
				{
					reg.irq_count = 0;
					X6502_IRQBegin(FCEU_IQEXT);
				}
			}
		}
	}

	void MapperReset(void)
	{
		uint32 last_prg = 0;
		uint32 second_last_prg = 0;

		memset(&reg, 0, sizeof(reg));

		if (prg_8k_count)
		{
			last_prg = prg_8k_count - 1;
		}
		else
		{
			last_prg = 0;
		}

		if (prg_8k_count >= 2)
		{
			second_last_prg = prg_8k_count - 2;
		}
		else
		{
			second_last_prg = last_prg;
		}

		SetPRGReg(1, 0);
		SetPRGReg(2, 1);
		SetPRGReg(3, second_last_prg);
		SetPRGReg(4, last_prg);

		reg.prg8k[0].ram = 1;
		reg.prg8k[0].reserved = 1;

		reg.multi_prg.offset.data = 0;
		reg.multi_prg.mask.data = 0xFFFF;

		reg.multi_chr.offset.data = 0;
		reg.multi_chr.mask.data = 0xFFFF;

		for (int i = 0; i < 8; i++)
		{
			SetCHRReg(i, i);
		}

		X6502_IRQEnd(FCEU_IQEXT);
		Sync();
	}

	void MapperPower(void)
	{
		prg_8k_count = ROM_size << 1;
		prg_4k_count = ROM_size << 2;
		chr_1k_count = VROM_size << 3;

		if (CHR_RAM)
		{
			const int32_t chr_bytes_size = 1024 * chr_1k_count;
			const int32_t copy_bytes_size = (chr_bytes_size < CHR_RAM_SIZE) ? chr_bytes_size : CHR_RAM_SIZE;
			memcpy_s(CHRptr[CHR_RAM_PAGE_INDEX], CHR_RAM_SIZE, CHRptr[0], copy_bytes_size);
		}

		MapperReset();

		SetReadHandler(reg_begin, reg_end, MapperRead);
		SetWriteHandler(reg_begin, reg_end, MapperWrite);

		SetReadHandler(0x6000, 0xFFFF, CartBR);
		SetWriteHandler(0x6000, 0xFFFF, CartBW);

		SetReadHandler(0x5000, 0x5FFF, ExRAM_5000_Read);
		SetWriteHandler(0x5000, 0x5FFF, ExRAM_5000_Write);

		SetReadHandler(0x4800, 0x4FFF, ExRAM_4800_Read);
		SetWriteHandler(0x4800, 0x4FFF, ExRAM_4800_Write);

		SetReadHandler(0x4400, 0x47FF, ExRAM_4400_Read);
		SetWriteHandler(0x4400, 0x47FF, ExRAM_4400_Write);

		SetReadHandler(0x4200, 0x43FF, ExRAM_4200_Read);
		SetWriteHandler(0x4200, 0x43FF, ExRAM_4200_Write);

		SetReadHandler(0x4100, 0x41FF, ExRAM_4100_Read);
		SetWriteHandler(0x4100, 0x41FF, ExRAM_4100_Write);

		SetReadHandler(0x4080, 0x40FF, ExRAM_4080_Read);
		SetWriteHandler(0x4080, 0x40FF, ExRAM_4080_Write);

		FCEU_MemoryRand(ExRAM_5000, ExRAM_5000_DATA_SIZE, true);
		FCEU_MemoryRand(ExRAM_4800, ExRAM_4800_DATA_SIZE, true);
		FCEU_MemoryRand(ExRAM_4400, ExRAM_4400_DATA_SIZE, true);
		FCEU_MemoryRand(ExRAM_4200, ExRAM_4200_DATA_SIZE, true);
		FCEU_MemoryRand(ExRAM_4100, ExRAM_4100_DATA_SIZE, true);
		FCEU_MemoryRand(ExRAM_4080, ExRAM_4080_DATA_SIZE, true);

		if (!battery)
		{
			FCEU_MemoryRand(WRAM, WRAM_SIZE, true);
		}
	}

	void MapperClose(void)
	{
		if (WRAM)
		{
			FCEU_gfree(WRAM);
			WRAM = nullptr;
		}

		if (CHR_RAM)
		{
			FCEU_gfree(CHR_RAM);
			CHR_RAM = nullptr;
		}

		if (ExtraNTARAM)
		{
			FCEU_gfree(ExtraNTARAM);
			ExtraNTARAM = nullptr;
		}

		if (ExRAM_5000)
		{
			FCEU_gfree(ExRAM_5000);
			ExRAM_5000 = nullptr;
		}

		if (ExRAM_4800)
		{
			FCEU_gfree(ExRAM_4800);
			ExRAM_4800 = nullptr;
		}

		if (ExRAM_4400)
		{
			FCEU_gfree(ExRAM_4400);
			ExRAM_4400 = nullptr;
		}

		if (ExRAM_4200)
		{
			FCEU_gfree(ExRAM_4200);
			ExRAM_4200 = nullptr;
		}

		if (ExRAM_4100)
		{
			FCEU_gfree(ExRAM_4100);
			ExRAM_4100 = nullptr;
		}

		if (ExRAM_4080)
		{
			FCEU_gfree(ExRAM_4080);
			ExRAM_4080 = nullptr;
		}
	}

	void StateRestore(int version)
	{
		Sync();
	}

	void Init(CartInfo* info)
	{
		battery = info->battery;

		info->Reset = MapperReset;
		info->Power = MapperPower;
		info->Close = MapperClose;

		MapIRQHook = IRQHook;
		GameHBIRQHook = HBIRQHook;
		GameStateRestore = StateRestore;

		if (!ExtraNTARAM)
		{
			ExtraNTARAM = (uint8*)FCEU_gmalloc(2048);
		}

		if (ExtraNTARAM)
		{
			AddExState(ExtraNTARAM, 2048, 0, "EXNR");
		}

		WRAM = static_cast<uint8_t*>(FCEU_gmalloc(WRAM_SIZE));
		if (WRAM)
		{
			SetupCartPRGMapping(WRAM_PAGE_INDEX, WRAM, WRAM_SIZE, 1);
			AddExState(WRAM, WRAM_SIZE, 0, "WRAM");
			if (info->battery)
			{
				info->addSaveGameBuf(WRAM, WRAM_SIZE);
			}
		}

		ExRAM_5000 = static_cast<uint8_t*>(FCEU_gmalloc(ExRAM_5000_DATA_SIZE));
		if (ExRAM_5000)
		{
			AddExState(ExRAM_5000, ExRAM_5000_DATA_SIZE, 0, "5000");
		}

		ExRAM_4800 = static_cast<uint8_t*>(FCEU_gmalloc(ExRAM_4800_DATA_SIZE));
		if (ExRAM_4800)
		{
			AddExState(ExRAM_4800, ExRAM_4800_DATA_SIZE, 0, "4800");
		}

		ExRAM_4400 = static_cast<uint8_t*>(FCEU_gmalloc(ExRAM_4400_DATA_SIZE));
		if (ExRAM_4400)
		{
			AddExState(ExRAM_4800, ExRAM_4400_DATA_SIZE, 0, "4400");
		}

		ExRAM_4200 = static_cast<uint8_t*>(FCEU_gmalloc(ExRAM_4200_DATA_SIZE));
		if (ExRAM_4200)
		{
			AddExState(ExRAM_4200, ExRAM_4200_DATA_SIZE, 0, "4200");
		}

		ExRAM_4100 = static_cast<uint8_t*>(FCEU_gmalloc(ExRAM_4100_DATA_SIZE));
		if (ExRAM_4100)
		{
			AddExState(ExRAM_4100, ExRAM_4100_DATA_SIZE, 0, "4100");
		}

		ExRAM_4080 = static_cast<uint8_t*>(FCEU_gmalloc(ExRAM_4080_DATA_SIZE));
		if (ExRAM_4080)
		{
			AddExState(ExRAM_4080, ExRAM_4080_DATA_SIZE, 0, "4080");
		}

		CHR_RAM = static_cast<uint8_t*>(FCEU_gmalloc(CHR_RAM_SIZE));
		if (CHR_RAM)
		{
			SetupCartCHRMapping(CHR_RAM_PAGE_INDEX, CHR_RAM, CHR_RAM_SIZE, 1);
			AddExState(CHR_RAM, CHR_RAM_SIZE, 0, "CHRR");
		}

		AddExState(StateRegs, ~0, 0, 0);
	}
}
