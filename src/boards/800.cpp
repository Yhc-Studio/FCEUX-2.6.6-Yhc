/* FCE Ultra - NES/Famicom Emulator
 *
 * Mapper 800
 *
 * Register layout:
 *
 * PRG low 8 bits:
 *   4020: 8KB PRG at $8000-$9FFF
 *   4021: 8KB PRG at $A000-$BFFF
 *   4022: 8KB PRG at $C000-$DFFF
 *   4023: 8KB PRG at $E000-$FFFF
 *
 * PRG high 8 bits:
 *   4030: high 8 bits for $8000-$9FFF
 *   4031: high 8 bits for $A000-$BFFF
 *   4032: high 8 bits for $C000-$DFFF
 *   4033: high 8 bits for $E000-$FFFF
 *
 * CHR low 8 bits:
 *   4040-4047: 1KB CHR banks at PPU $0000-$1FFF
 *
 * CHR high 8 bits:
 *   4050-4057: high 8 bits for 1KB CHR banks
 *
 * nt_mirroringoring:
 *   4060:
 *     bit 0-1:
 *       0 = vertical
 *       1 = horizontal
 *       2 = single-screen A
 *       3 = single-screen B
 *
 * IRQ:
 *   4080: IRQ latch / scanline value
 *   4081: IRQ disable and acknowledge
 *   4082: IRQ enable
 */

#include "mapinc.h"

extern uint32 ROM_size;
extern uint32 VROM_size;

constexpr uint16_t this_mapper_prg_8000_l = 0x4020;
constexpr uint16_t this_mapper_prg_8000_h = 0x4021;
constexpr uint16_t this_mapper_prg_A000_l = 0x4022;
constexpr uint16_t this_mapper_prg_A000_h = 0x4023;
constexpr uint16_t this_mapper_prg_C000_l = 0x4024;
constexpr uint16_t this_mapper_prg_C000_h = 0x4025;
constexpr uint16_t this_mapper_prg_E000_l = 0x4026;
constexpr uint16_t this_mapper_prg_E000_h = 0x4027;

constexpr uint16_t this_mapper_nt_mirroring = 0x4028;
constexpr uint16_t this_mapper_prg_ram_page = 0x4029;
constexpr uint16_t this_mapper_prg_ram_mode = 0x402A;

constexpr uint16_t this_mapper_irq_latch = 0x402C;
constexpr uint16_t this_mapper_irq_disable = 0x402E;
constexpr uint16_t this_mapper_irq_enable = 0x402F;

constexpr uint16_t this_mapper_chr_0000_l = 0x4030;
constexpr uint16_t this_mapper_chr_0000_h = 0x4031;
constexpr uint16_t this_mapper_chr_0400_l = 0x4032;
constexpr uint16_t this_mapper_chr_0400_h = 0x4033;
constexpr uint16_t this_mapper_chr_0800_l = 0x4034;
constexpr uint16_t this_mapper_chr_0800_h = 0x4035;
constexpr uint16_t this_mapper_chr_0C00_l = 0x4036;
constexpr uint16_t this_mapper_chr_0C00_h = 0x4037;
constexpr uint16_t this_mapper_chr_1000_l = 0x4038;
constexpr uint16_t this_mapper_chr_1000_h = 0x4039;
constexpr uint16_t this_mapper_chr_1400_l = 0x403A;
constexpr uint16_t this_mapper_chr_1400_h = 0x403B;
constexpr uint16_t this_mapper_chr_1800_l = 0x403C;
constexpr uint16_t this_mapper_chr_1800_h = 0x403D;
constexpr uint16_t this_mapper_chr_1C00_l = 0x403E;
constexpr uint16_t this_mapper_chr_1C00_h = 0x403F;

constexpr uint16_t this_mapper_reg_begin = 0x4020;
constexpr uint16_t this_mapper_reg_end = 0x403F;

static struct _this_mapper_reg
{
	typedef union _mapper_value_16
	{
		uint16_t data;
		struct
		{
			uint16_t low : 8;
			uint16_t high : 8;
		};
	}mapper_value_16;

	mapper_value_16 prg[4];
	mapper_value_16 chr[8];

	uint8_t prg_ram_page;
	uint8_t prg_ram_mode;
	uint8_t nt_mirroring;
	uint8_t irq_acknowledge;
	uint8_t irq_pending;
	int16_t irq_count;
	int16_t irq_latch;
}this_mapper_reg;

static uint32 prg_8k_count;
static uint32 chr_1k_count;
static int32_t WRAMSIZE = 0x4000;
static uint8_t* WRAM = nullptr;

static SFORMAT StateRegs[] =
{
	{ this_mapper_reg.prg, sizeof(this_mapper_reg.prg), "PRG"},
	{ this_mapper_reg.chr, sizeof(this_mapper_reg.chr), "CHR" },

	{ &this_mapper_reg.prg_ram_page, 1, "PRGP" },
	{ &this_mapper_reg.prg_ram_mode, 1, "PRGM" },
	{ &this_mapper_reg.nt_mirroring, 1, "MIRR" },

	{ &this_mapper_reg.irq_acknowledge, 1, "IRQA" },
	{ &this_mapper_reg.irq_pending, 1, "IRQP" },
	{ &this_mapper_reg.irq_count, 2, "IRQC" },
	{ &this_mapper_reg.irq_latch, 2, "IRQL" },

	{ 0 }
};

static uint32 WrapBank(uint32 bank, uint32 count) noexcept
{
	if (!count)
	{
		return 0;
	}

	return bank % count;
}

static void SetPRGReg(int index, uint32 bank) noexcept
{
	this_mapper_reg.prg[index].data = bank;
}

static void SetCHRReg(int index, uint32 bank) noexcept
{
	this_mapper_reg.chr->data = bank;
}

static uint32 GetPRGBank(int index) noexcept
{
	return (uint32)this_mapper_reg.prg[index].data;
}

static uint32 GetCHRBank(int index) noexcept
{
	return (uint32)this_mapper_reg.chr[index].data;
}

static uint8_t ThisMapperPrgRamRead(uint32_t A) noexcept
{
	return WRAM[this_mapper_reg.prg_ram_page * 0x2000 + (A & 0x1FFF)];
}

static void ThisMapperPrgRamWrite(uint32_t A, uint8_t V) noexcept
{
	WRAM[this_mapper_reg.prg_ram_page * 0x2000 + (A & 0x1FFF)] = V;
}

static void Sync(void)
{
	int i = 0;
	uint32 bank = 0;

	/*
	 * PRG-ROM mapping
	 *
	 * 8KB * 4:
	 *   $8000-$9FFF
	 *   $A000-$BFFF
	 *   $C000-$DFFF
	 *   $E000-$FFFF
	 */
	for (i = 0; i < 4; i++)
	{
		bank = GetPRGBank(i);
		bank = WrapBank(bank, prg_8k_count);
		setprg8(0x8000 + i * 0x2000, bank);
	}

	/*
	 * CHR-ROM mapping
	 *
	 * 1KB * 8:
	 *   $0000-$03FF
	 *   $0400-$07FF
	 *   $0800-$0BFF
	 *   $0C00-$0FFF
	 *   $1000-$13FF
	 *   $1400-$17FF
	 *   $1800-$1BFF
	 *   $1C00-$1FFF
	 */
	if (chr_1k_count)
	{
		for (i = 0; i < 8; i++)
		{
			bank = GetCHRBank(i);
			bank = WrapBank(bank, chr_1k_count);
			setchr1(i * 0x0400, bank);
		}
	}
	else
	{
		/*
		 * CHR-RAM fallback.
		 *
		 * ��� ROM header �� CHR size = 0��
		 * FCEUX ͨ���ᰴ CHR-RAM ������
		 *
		 * ���������ʵ���Ǵ� CHR-ROM������ 8MB / 64Mbit��
		 * ��������� else����˵�� header ����ʲ��ԡ�
		 */
		for (i = 0; i < 8; i++)
		{
			setchr1(i * 0x0400, i);
		}
	}

	switch (this_mapper_reg.nt_mirroring & 0x03)
	{
	case 0:
		setmirror(MI_V);
		break;

	case 1:
		setmirror(MI_H);
		break;

	case 2:
		setmirror(MI_0);
		break;

	case 3:
		setmirror(MI_1);
		break;
	default:
		break;
	}

	if (this_mapper_reg.prg_ram_mode & 0x01)
	{
		setprg8(0x6000, this_mapper_reg.prg_ram_page);
		SetReadHandler(0x6000, 0x7FFF, CartBR);
		SetWriteHandler(0x6000, 0x7FFF, CartBW);
	}
	else
	{
		SetReadHandler(0x6000, 0x7FFF, ThisMapperPrgRamRead);
		SetWriteHandler(0x6000, 0x7FFF, ThisMapperPrgRamWrite);
	}
}

static DECLFW(MapperWrite)
{
	switch (A)
	{
		/*
		 * PRG low 8 bits
		 */
	case this_mapper_prg_8000_l:
		this_mapper_reg.prg[0].low = V;
		Sync();
		break;
	case this_mapper_prg_A000_l:
		this_mapper_reg.prg[1].low = V;
		Sync();
		break;
	case this_mapper_prg_C000_l:
		this_mapper_reg.prg[2].low = V;
		Sync();
		break;
	case this_mapper_prg_E000_l:
		this_mapper_reg.prg[3].low = V;
		Sync();
		break;

		/*
		 * PRG high 8 bits
		 */
	case this_mapper_prg_8000_h:
		this_mapper_reg.prg[0].high = V;
		Sync();
		break;
	case this_mapper_prg_A000_h:
		this_mapper_reg.prg[1].high = V;
		Sync();
		break;
	case this_mapper_prg_C000_h:
		this_mapper_reg.prg[2].high = V;
		Sync();
		break;
	case this_mapper_prg_E000_h:
		this_mapper_reg.prg[3].high = V;
		Sync();
		break;

		/*
		 * CHR low 8 bits
		 */
	case this_mapper_chr_0000_l:
		this_mapper_reg.chr[0x00].low = V;
		Sync();
		break;
	case this_mapper_chr_0400_l:
		this_mapper_reg.chr[0x01].low = V;
		Sync();
		break;
	case this_mapper_chr_0800_l:
		this_mapper_reg.chr[0x02].low = V;
		Sync();
		break;
	case this_mapper_chr_0C00_l:
		this_mapper_reg.chr[0x03].low = V;
		Sync();
		break;
	case this_mapper_chr_1000_l:
		this_mapper_reg.chr[0x04].low = V;
		Sync();
		break;
	case this_mapper_chr_1400_l:
		this_mapper_reg.chr[0x05].low = V;
		Sync();
		break;
	case this_mapper_chr_1800_l:
		this_mapper_reg.chr[0x06].low = V;
		Sync();
		break;
	case this_mapper_chr_1C00_l:
		this_mapper_reg.chr[0x07].low = V;
		Sync();
		break;

		/*
		 * CHR high 8 bits
		 */
	case this_mapper_chr_0000_h:
		this_mapper_reg.chr[0x00].high = V;
		Sync();
		break;
	case this_mapper_chr_0400_h:
		this_mapper_reg.chr[0x01].high = V;
		Sync();
		break;
	case this_mapper_chr_0800_h:
		this_mapper_reg.chr[0x02].high = V;
		Sync();
		break;
	case this_mapper_chr_0C00_h:
		this_mapper_reg.chr[0x03].high = V;
		Sync();
		break;
	case this_mapper_chr_1000_h:
		this_mapper_reg.chr[0x04].high = V;
		Sync();
		break;
	case this_mapper_chr_1400_h:
		this_mapper_reg.chr[0x05].high = V;
		Sync();
		break;
	case this_mapper_chr_1800_h:
		this_mapper_reg.chr[0x06].high = V;
		Sync();
		break;
	case this_mapper_chr_1C00_h:
		this_mapper_reg.chr[0x07].high = V;
		Sync();
		break;

		/*
		 * nt_mirroringoring
		 */
	case this_mapper_nt_mirroring:
		this_mapper_reg.nt_mirroring = V & 0x03;
		Sync();
		break;

		/*
		 * Prg Ram Page
		 */
	case this_mapper_prg_ram_page:
		this_mapper_reg.prg_ram_page = V;
		Sync();
		break;

		/*
		 * Prg Ram Mode
		 */
	case this_mapper_prg_ram_mode:
		this_mapper_reg.prg_ram_mode = V;
		Sync();
		break;

		/*
		 * IRQ latch
		 */
	case this_mapper_irq_latch:
		this_mapper_reg.irq_latch = V;
		break;

		/*
		 * IRQ disable and acknowledge
		 */
	case this_mapper_irq_disable:
		this_mapper_reg.irq_acknowledge = 0;
		this_mapper_reg.irq_pending = 0;
		this_mapper_reg.irq_count = this_mapper_reg.irq_latch;
		X6502_IRQEnd(FCEU_IQEXT);
		break;

		/*
		 * IRQ enable
		 */
	case this_mapper_irq_enable:
		this_mapper_reg.irq_acknowledge = 1;
		this_mapper_reg.irq_pending = 0;
		this_mapper_reg.irq_count = this_mapper_reg.irq_latch;
		X6502_IRQEnd(FCEU_IQEXT);
		break;
	default:
		break;
	}
}

static void MapperIRQHook(void)
{
	if (!this_mapper_reg.irq_acknowledge || this_mapper_reg.irq_pending)
	{
		return;
	}

	if (this_mapper_reg.irq_count <= 0)
	{
		this_mapper_reg.irq_pending = 1;
		X6502_IRQBegin(FCEU_IQEXT);
	}
	else
	{
		this_mapper_reg.irq_count--;
	}
}

static void MapperReset(void)
{
	int i;
	uint32 last_prg;
	uint32 second_last_prg;

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

	/*
	 * Initial PRG mapping:
	 *
	 * $8000-$9FFF = bank 0
	 * $A000-$BFFF = bank 1
	 * $C000-$DFFF = second-last 8KB bank
	 * $E000-$FFFF = last 8KB bank
	 *
	 * �������Ա�֤ Reset Vector λ����ȷ��ĩβ ROM ����
	 */
	SetPRGReg(0, 0);
	SetPRGReg(1, 1);
	SetPRGReg(2, second_last_prg);
	SetPRGReg(3, last_prg);

	/*
	 * Initial CHR mapping:
	 *
	 * PPU $0000-$1FFF = CHR bank 0-7
	 */
	for (i = 0; i < 8; i++)
	{
		SetCHRReg(i, i);
	}

	this_mapper_reg.nt_mirroring = 0;
	this_mapper_reg.irq_acknowledge = 0;
	this_mapper_reg.irq_pending = 0;
	this_mapper_reg.irq_count = 0;
	this_mapper_reg.irq_latch = 0;

	X6502_IRQEnd(FCEU_IQEXT);
	Sync();
}

static void MapperPower(void)
{
	/*
	 * FCEUX �У�
	 *
	 * ROM_size  = PRG-ROM size in 16KB units
	 * VROM_size = CHR-ROM size in 8KB units
	 *
	 * ��ǰ mapper ʹ�ã�
	 *   PRG = 8KB bank
	 *   CHR = 1KB bank
	 */

	prg_8k_count = ROM_size << 1;
	chr_1k_count = VROM_size << 3;

	MapperReset();

	/*
	 * $4020-$4082 �� mapper ��չ�Ĵ�����
	 * ��Ҫ���� $4000-$401F����Ϊ���� APU / I/O��
	 */
	SetWriteHandler(this_mapper_reg_begin, this_mapper_reg_end, MapperWrite);

	/*
	 * PRG-ROM read area.
	 */
	SetReadHandler(0x8000, 0xFFFF, CartBR);

	/*
	 * WRAM read/write area.
	 */
	SetReadHandler(0x6000, 0x7FFF, ThisMapperPrgRamRead);
	SetWriteHandler(0x6000, 0x7FFF, ThisMapperPrgRamWrite);
}

static void MapperClose(void)
{
	if (WRAM)
	{
		FCEU_gfree(WRAM);
	}
}

static void StateRestore(int version)
{
	/*
	 * savestate restore ������ͬ�� PRG / CHR / nt_mirroringoring��
	 */
	Sync();

	if (this_mapper_reg.irq_pending)
	{
		X6502_IRQBegin(FCEU_IQEXT);
	}
	else
	{
		X6502_IRQEnd(FCEU_IQEXT);
	}
}

void Mapper800_Init(CartInfo* info)
{
	info->Reset = MapperReset;
	info->Power = MapperPower;
	info->Close = MapperClose;

	GameHBIRQHook = MapperIRQHook;
	GameStateRestore = StateRestore;

	WRAMSIZE = 0x2000 * 256;
	WRAM = static_cast<uint8_t*>(FCEU_gmalloc(WRAMSIZE));
	if (WRAM)
	{
		SetupCartPRGMapping(0x10, WRAM, WRAMSIZE, 1);
		AddExState(WRAM, WRAMSIZE, 0, "WRAM");

		if (info->battery) {
			info->addSaveGameBuf(WRAM, WRAMSIZE);
		}
	}

	AddExState(StateRegs, ~0, 0, 0);
}