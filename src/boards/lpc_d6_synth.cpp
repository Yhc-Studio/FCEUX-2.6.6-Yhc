/*******************************************************************************
 *
 *  LPC-10 D6 Synthesizer
 *
 *  Author:  <87430545@qq.com>
 *
 *  Create:  May/6/2021 by fanoble
 *
 *******************************************************************************
 */
#include <string.h>
#include <stdlib.h>
#include "lpc_d6_synth.h"

#define LPC_ORDER 10
#define LPC_SAMPLES_PER_FRAME 200 // speed

#define LPC_FRAC_BITS 15 // using Q15

typedef struct tag_lpc_frame {
	short energy;
	short pitch;
	short k[LPC_ORDER]; // K1-K10
} LPC_FRAME;

typedef struct tag_lpc_synth {
	LPC_FRAME frame_prev; // 0
	LPC_FRAME frame_curr; // 2
	LPC_FRAME frame_next; // 1

	short need_interp;
	short random_seed;
	short curr_pitch;
	short sample_index; // from 0 to LPC_SAMPLES_PER_FRAME - 1

	short x[LPC_ORDER + 1]; // filter buf
	short synth_out; // output sample

#define LPC_STATE_RESET			0
#define LPC_STATE_STARTUP		1
#define LPC_STATE_RUN			2
#define LPC_STATE_FINISHED		3
#define LPC_STATE_STOPPED		4

	int state;
	int need_data;

	// for bit reader
	int bits_left;
	unsigned short data_cache;

	void* host;
	int (*feed)(void* host, unsigned char* food);

	int variant;
	short LPC_D6;

} LPC_SYNTH;

#ifdef _MSC_VER
#pragma warning(disable:4305)
#endif

//------------------------------------------------------------------------------

// LPC-D6 tables from TSP50C04 manual
static const short lpc_gain_tab[] =
{
	0x0000, 0x0100, 0x0200, 0x0300, 0x0400, 0x0500, 0x0700, 0x0B00,
	0x1100, 0x1A00, 0x2900, 0x3F00, 0x5500, 0x7000, 0x7F00, 0x0000
};

static const short lpc_pitch_tab[] =
{
	0x0000,	0x0100, 0x0104, 0x0108, 0x0110, 0x0114, 0x0118, 0x011C,
	0x0124, 0x0128, 0x012C, 0x0134, 0x0138, 0x0140, 0x0144, 0x014C,
	0x0150, 0x0158, 0x015C, 0x0164, 0x016C, 0x0170, 0x0178, 0x0180,
	0x0184, 0x018C, 0x0194, 0x019C, 0x01A4, 0x01AC, 0x01B4, 0x01BC,
	0x01C4, 0x01CC, 0x01D4, 0x01DC, 0x01E4, 0x01F0, 0x01F8, 0x0200,
	0x020C, 0x0214, 0x021C, 0x0228, 0x0230, 0x023C, 0x0248, 0x0250,
	0x025C, 0x0268, 0x0274, 0x0280, 0x028C, 0x0298, 0x02A4, 0x02B0,
	0x02BC, 0x02C8, 0x02D4, 0x02E4, 0x02F0, 0x0300, 0x030C, 0x031C,
	0x0328, 0x0338, 0x0348, 0x0358, 0x0368, 0x0378, 0x0388, 0x0398,
	0x03A8, 0x03BC, 0x03CC, 0x03DC, 0x03F0, 0x0404, 0x0414, 0x0428,
	0x043C, 0x0450, 0x0464, 0x0478, 0x0490, 0x04A4, 0x04BC, 0x04D0,
	0x04E8, 0x0500, 0x0514, 0x052C, 0x0548, 0x0560, 0x0578, 0x0594,
	0x05AC, 0x05C8, 0x05E4, 0x0600, 0x061C, 0x0638, 0x0654, 0x0674,
	0x0690, 0x06B0, 0x06D0, 0x06F0, 0x0710, 0x0734, 0x0754, 0x0778,
	0x079C, 0x07C0, 0x07E4, 0x0808, 0x082C, 0x0854, 0x087C, 0x08A4,
	0x08CC, 0x08F8, 0x0920, 0x094C, 0x0978, 0x09A4, 0x09D0, 0x0A00,
};

static const short lpc_k1_tab[] =
{
	0x8100, 0x8240, 0x8340, 0x8480, 0x85C0, 0x8700, 0x8840, 0x89C0,
	0x8B40, 0x8CC0, 0x8E40, 0x9000, 0x91C0, 0x9380, 0x9580, 0x9740,
	0x9980, 0x9B80, 0x9D80, 0x9FC0, 0xA200, 0xA440, 0xA6C0, 0xA940,
	0xAB80, 0xAE00, 0xB0C0, 0xB380, 0xB640, 0xB900, 0xBC00, 0xBF40,
	0xC240, 0xC580, 0xC8C0, 0xCC40, 0xCFC0, 0xD380, 0xD780, 0xDB40,
	0xDF40, 0xE380, 0xE7C0, 0xEC00, 0xF040, 0xF4C0, 0xF9C0, 0xFEC0,
	0x0440, 0x09C0, 0x0F40, 0x1580, 0x1C80, 0x2380, 0x2AC0, 0x3280,
	0x3A80, 0x42C0, 0x4B80, 0x5400, 0x5C40, 0x6500, 0x6E00, 0x7880
};

static const short lpc_k2_tab[] =
{
	0x8A00, 0x9800, 0xA3C0, 0xADC0, 0xB480, 0xBA80, 0xC000, 0xC500,
	0xC9C0, 0xCE40, 0xD2C0, 0xD6C0, 0xDAC0, 0xDE80, 0xE200, 0xE5C0,
	0xE940, 0xECC0, 0xF000, 0xF340, 0xF680, 0xF9C0, 0xFD00, 0x0000,
	0x0340, 0x0640, 0x0940, 0x0C40, 0x0F40, 0x1280, 0x1580, 0x1880,
	0x1B80, 0x1E80, 0x2180, 0x24C0, 0x27C0, 0x2AC0, 0x2DC0, 0x30C0,
	0x3400, 0x3700, 0x3A40, 0x3D00, 0x4000, 0x4300, 0x4600, 0x4900,
	0x4C00, 0x4F40, 0x5240, 0x5540, 0x5840, 0x5B40, 0x5E00, 0x6100,
	0x63C0, 0x6680, 0x6940, 0x6C00, 0x6F00, 0x7200, 0x7640, 0x7C00 
};

static const short lpc_k3_tab[] =
{
	0x8B00, 0x9A00, 0xA200, 0xA900, 0xAF00, 0xB500, 0xBB00, 0xC000,
	0xC500, 0xCA00, 0xCF00, 0xD400, 0xD900, 0xDE00, 0xE200, 0xE700,
	0xEC00, 0xF100, 0xF600, 0xFB00, 0x0100, 0x0700, 0x0D00, 0x1400,
	0x1A00, 0x2200, 0x2900, 0x3200, 0x3B00, 0x4500, 0x5300, 0x6D00 
};

static const short lpc_k4_tab[] =
{
	0x9400, 0xB000, 0xC200, 0xCB00, 0xD300, 0xD900, 0xDF00, 0xE500,
	0xEA00, 0xEF00, 0xF400, 0xF900, 0xFE00, 0x0300, 0x0700, 0x0C00,
	0x1100, 0x1500, 0x1A00, 0x1F00, 0x2400, 0x2900, 0x2E00, 0x3300,
	0x3800, 0x3E00, 0x4400, 0x4B00, 0x5300, 0x5A00, 0x6400, 0x7400
};

static const short lpc_k5_tab[] =
{
	0xA300, 0xC500, 0xD400, 0xE000, 0xEA00, 0xF300, 0xFC00, 0x0400,
	0x0C00, 0x1500, 0x1E00, 0x2700, 0x3100, 0x3D00, 0x4C00, 0x6600
};

static const short lpc_k6_tab[] =
{
	0xAA00, 0xD700, 0xE700, 0xF200, 0xFC00, 0x0500, 0x0D00, 0x1400,
	0x1C00, 0x2400, 0x2D00, 0x3600, 0x4000, 0x4A00, 0x5500, 0x6A00
};

static const short lpc_k7_tab[] =
{
	0xA300, 0xC800, 0xD700, 0xE300, 0xED00, 0xF500, 0xFD00, 0x0500,
	0x0D00, 0x1400, 0x1D00, 0x2600, 0x3100, 0x3C00, 0x4B00, 0x6700
};

static const short lpc_k8_tab[] =
{
	0xC500, 0xE400, 0xF600, 0x0500, 0x1400, 0x2700, 0x3E00, 0x5800
};

static const short lpc_k9_tab[] =
{
	0xB900, 0xDC00, 0xEC00, 0xF900, 0x0400, 0x1000, 0x1F00, 0x4500
};

static const short lpc_k10_tab[] =
{
	0xC300, 0xE600, 0xF300, 0xFD00, 0x0600, 0x1100, 0x1E00, 0x4300
};

//------------------------------------------------------------------------------

// LPC-PE tables from MAME's tms5110r.hxx
static const short pe_energy[] ={
	     0,   256,   512,   768,  1024,  1536,  2048,  2816,
	  4096,  5888,  8448, 12032, 16128, 21760, 29184,     0
};

static const short pe_pitchPeriod[] ={
	     0,   240,   256,   272,   288,   304,   320,   336,
	   352,   368,   384,   400,   416,   432,   448,   464,
	   480,   496,   512,   528,   544,   560,   576,   592,
	   608,   624,   640,   656,   672,   704,   736,   768,
	   800,   832,   848,   896,   928,   960,   992,  1040,
	  1088,  1120,  1152,  1216,  1248,  1280,  1344,  1376,
	  1456,  1504,  1568,  1616,  1680,  1744,  1824,  1888,
	  1952,  2032,  2112,  2192,  2272,  2368,  2448,  2544
};

static const short pe_k1[] ={
	-32064,-31872,-31808,-31680,-31552,-31424,-31232,-30848,
	-30592,-30336,-30016,-29696,-29376,-28928,-28480,-27968,
	-26368,-24320,-21696,-18432,-14528,-10112, -5184,   -64,
	  5120, 10048, 14464, 18368, 21568, 24256, 26304, 27904
};
static const short pe_k2[] ={
	-20992,-19392,-17536,-15616,-13504,-11200, -8832, -6336,
	 -3776, -1152,  1536,  4096,  6720,  9152, 11520, 13760,
	 15872, 17792, 19584, 21184, 22656, 23936, 25088, 26112,
	 27008, 27840, 28480, 29120, 29632, 30080, 30464, 32384
};
static const short pe_k3[] ={
	-28224,-24768,-21312,-17856,-14400,-10944, -7488, -4032,
	  -576,  2880,  6272,  9728, 13184, 16640, 20096, 23552
};
static const short pe_k4[] ={
	-20992,-17472,-13888,-10304, -6784, -3200,   320,  3904,
	  7424, 11008, 14592, 18112, 21696, 25216, 28800, 32384
};
static const short pe_k5[] ={
	-20992,-18048,-15040,-12096, -9088, -6144, -3200,  -192,
	  2752,  5760,  8704, 11648, 14656, 17600, 20608, 23552
};
static const short pe_k6[] ={
	-16384,-13568,-10752, -7872, -5056, -2240,   640,  3456,
	  6272,  9152, 11968, 14848, 17664, 20480, 23360, 26176
};
static const short pe_k7[] ={
	-19712,-16640,-13568,-10496, -7488, -4416, -1344,  1728,
	  4800,  7808, 10880, 13952, 17024, 20096, 23104, 26176
};
static const short pe_k8[] ={
	-16384,-10304, -4224,  1856,  7936, 14016, 20096, 26176
};
static const short pe_k9[] ={
	-16384,-11264, -6144,  -960,  4160,  9344, 14464, 19648
};
static const short pe_k10[] ={
	-13120, -8448, -3776,   896,  5568, 10240, 14976, 19648
};

//------------------------------------------------------------------------------

static const short lpc_excit[] =
{
	0x00A2, 0x00AF, 0x00BA, 0x00C2, 0x00C7, 0x00C9, 0x00CA, 0x00C6,
	0x00C2, 0x00BC, 0x00B5, 0x00AD, 0x00A5, 0x009E, 0x009A, 0x0095,
	0x0095, 0x0098, 0x009F, 0x00A8, 0x00B8, 0x00CA, 0x00E3, 0x00FE,
	0x011F, 0x0141, 0x0169, 0x0191, 0x01BD, 0x01E8, 0x0216, 0x0240,
	0x026C, 0x0292, 0x02B9, 0x02D9, 0x02F8, 0x030F, 0x0325, 0x0332,
	0x033F, 0x0343, 0x0347, 0x0345, 0x0345, 0x033F, 0x033D, 0x033A,
	0x033D, 0x0341, 0x034E, 0x035F, 0x037B, 0x03A0, 0x03D2, 0x040D,
	0x0457, 0x04AD, 0x0511, 0x0582, 0x0600, 0x068A, 0x071F, 0x07BD,
	0x0864, 0x0911, 0x09C1, 0x0A74, 0x0B26, 0x0BD5, 0x0C7F, 0x0D20,
	0x0DB7, 0x0E40, 0x0EBB, 0x0F24, 0x0F7A, 0x0FBC, 0x0FE9, 0x0FFF,
	0x0FFF, 0x0FE9, 0x0FBC, 0x0F7A, 0x0F24, 0x0EBB, 0x0E40, 0x0DB7,
	0x0D20, 0x0C7F, 0x0BD5, 0x0B26, 0x0A74, 0x09C1, 0x0911, 0x0864,
	0x07BD, 0x071F, 0x068A, 0x0600, 0x0582, 0x0511, 0x04AD, 0x0457,
	0x040D, 0x03D2, 0x03A0, 0x037B, 0x035F, 0x034E, 0x0341, 0x033D,
	0x033A, 0x033D, 0x033F, 0x0345, 0x0345, 0x0347, 0x0343, 0x033F,
	0x0332, 0x0325, 0x030F, 0x02F8, 0x02D9, 0x02B9, 0x0292, 0x026C,
	0x0240, 0x0216, 0x01E8, 0x01BD, 0x0191, 0x0169, 0x0141, 0x011F,
	0x00FE, 0x00E3, 0x00CA, 0x00B8, 0x00A8, 0x009F, 0x0098, 0x0095,
	0x0095, 0x009A, 0x009E, 0x00A5, 0x00AD, 0x00B5, 0x00BC, 0x00C2,
	0x00C6, 0x00CA, 0x00C9, 0x00C7, 0x00C2, 0x00BA, 0x00AF, 0x00A2
};

//------------------------------------------------------------------------------

static unsigned char byte_rev(unsigned char a)
{
	// 76543210
	a = (a >> 4) | (a << 4); // Swap in groups of 4

	// 32107654
	a = ((a & 0xCC) >> 2) | ((a & 0x33) << 2); // Swap in groups of 2

	// 10325476
	a = ((a & 0xAA) >> 1) | ((a & 0x55) << 1); // Swap bit pairs

	// 01234567
	return a;
}

// bits <= 8
static short get_nbits(LPC_SYNTH* s, short bits)
{
	unsigned short data;

	if (LPC_STATE_RESET == s->state ||
		LPC_STATE_STOPPED == s->state)
		return -1;

	if (s->bits_left < bits) {
		int cmd;
		unsigned char food;

		s->data_cache <<= 8;
		
		cmd = s->feed(s->host, &food);

		if (LPC_CMD_NONE == cmd) {
			/*
			 * Non-blocking emulator integration: leave the decoder state
			 * untouched.  lpc_d6_synth_do() will roll back the whole call,
			 * and the host restores its FIFO read position.
			 */
			s->need_data = 1;
			return 0;
		}

		if (cmd < 0) {// stop
			s->state = LPC_STATE_STOPPED;
			return -1;
		}

		if (LPC_CMD_RESET == cmd) {
			s->state = LPC_STATE_RESET;
			return -1;
		}

		// LPC_CMD_PAYLOAD
		s->data_cache |= byte_rev(food);
		s->bits_left += 8;
	}

	data = s->data_cache >> (s->bits_left - bits);
	data &= (1 << bits) - 1;
	s->bits_left -= bits;
	
	return (short)data;
}

//------------------------------------------------------------------------------

static int lpc_get_frame(LPC_SYNTH* s, LPC_FRAME* dst, LPC_FRAME* ref)
{
	int pitch_i;
	int energy_i;
	int repeat;

	energy_i = get_nbits(s, 4);
	if (0 == energy_i) {
		// silent
		dst->energy = 0;
		dst->pitch = 0;
		memset(dst->k, 0, sizeof(ref->k));
		return 0;
	}

	// end of stream
	if (15 == energy_i)
		return -1;

	repeat = get_nbits(s, 1); // repeat
	pitch_i = get_nbits(s, s->LPC_D6? 7: 6);

	// coded -> uncoded
	dst->energy = s->LPC_D6? lpc_gain_tab[energy_i]: pe_energy[energy_i];
	dst->pitch  = s->LPC_D6? lpc_pitch_tab[pitch_i]: pe_pitchPeriod[pitch_i];

	if (repeat) {
		dst->k[0] = ref->k[0];
		dst->k[1] = ref->k[1];
		dst->k[2] = ref->k[2];
		dst->k[3] = ref->k[3];
	} else {
		dst->k[0] = s->LPC_D6? lpc_k1_tab[get_nbits(s, 6)]: pe_k1[get_nbits(s, 5)]; // K1
		dst->k[1] = s->LPC_D6? lpc_k2_tab[get_nbits(s, 6)]: pe_k2[get_nbits(s, 5)]; // K2
		dst->k[2] = s->LPC_D6? lpc_k3_tab[get_nbits(s, 5)]: pe_k3[get_nbits(s, 4)]; // K3
		dst->k[3] = s->LPC_D6? lpc_k4_tab[get_nbits(s, 5)]: pe_k4[get_nbits(s, 4)]; // K4
	}

	if (0 == pitch_i) {
		// lpc unvoiced frame
		dst->k[4] = 0;
		dst->k[5] = 0;
		dst->k[6] = 0;
		dst->k[7] = 0;
		dst->k[8] = 0;
		dst->k[9] = 0;
	} else {
		// voiced
		if (repeat) {
			memcpy(dst->k, ref->k, sizeof(ref->k));
		} else {
			dst->k[4] = s->LPC_D6? lpc_k5_tab[get_nbits(s, 4)]: pe_k5 [get_nbits(s, 4)]; // K5
			dst->k[5] = s->LPC_D6? lpc_k6_tab[get_nbits(s, 4)]: pe_k6 [get_nbits(s, 4)]; // K6
			dst->k[6] = s->LPC_D6? lpc_k7_tab[get_nbits(s, 4)]: pe_k7 [get_nbits(s, 4)]; // K7
			dst->k[7] = s->LPC_D6? lpc_k8_tab[get_nbits(s, 3)]: pe_k8 [get_nbits(s, 3)]; // K8
			dst->k[8] = s->LPC_D6? lpc_k9_tab[get_nbits(s, 3)]: pe_k9 [get_nbits(s, 3)]; // K9
			dst->k[9] = s->LPC_D6? lpc_k10_tab[get_nbits(s, 3)]: pe_k10 [get_nbits(s, 3)]; // K10
		}
	}

	return 0;
}

static void lpc_set_interp_flag(LPC_SYNTH* s)
{
	s->need_interp = 1;

	if (s->frame_prev.energy == 0) {
		if (s->frame_next.pitch == 0) {
			if (s->frame_next.energy)
				s->need_interp = 0;
		}
	} else {
		if (s->frame_prev.pitch) {
			if (s->frame_next.pitch == 0) {
				if (s->frame_next.energy)
					s->need_interp = 0;
			}
		} else {
			if (s->frame_next.pitch)
				s->need_interp = 0;
		}
	}
}

static void lpc_synth_init(LPC_SYNTH* s)
{
	// clear reset
	memset(s, 0, sizeof(LPC_SYNTH));

	s->random_seed = 0xFFFF;
}

static int lpc_synth_preload(LPC_SYNTH* s)
{
	int magic;

	magic = get_nbits(s, 8);

	s->LPC_D6 = 1;

	if (LPC_STD_VARIANT_Subor == s->variant) {
		if (magic == 0xD0) s->LPC_D6 = 0;	//Subor SB97 LPC-10_PE
		if ((0x50 != magic)&&(0xD0 != magic))	// 0x50 = bit rev 0x0A
			return 0;
	} else if (LPC_STD_VARIANT_BBK == s->variant) {
		if (0x6B != magic)	// 0x6B = bit rev 0xD6
			return 0;
	} else if (LPC_STD_VARIANT_Generic == s->variant) {	//LPC-10_PE
		s->LPC_D6 = 0;
	}

	s->state = LPC_STATE_RUN;

	lpc_get_frame(s, &s->frame_prev, &s->frame_prev);
	lpc_get_frame(s, &s->frame_next, &s->frame_prev);

	s->frame_curr = s->frame_prev;

	lpc_set_interp_flag(s);

	return 0;
}

static short lpc_reload_pitch(LPC_SYNTH* s)
{
	short curr_pitch;

	curr_pitch = s->curr_pitch;

	if (s->need_interp) {
		int i;
		int ratio;
		short* vector0;
		short* vector1;
		short* vector2;

		ratio = (s->sample_index << LPC_FRAC_BITS) / LPC_SAMPLES_PER_FRAME;

		vector0 = (short*)&s->frame_prev;
		vector1 = (short*)&s->frame_next;
		vector2 = (short*)&s->frame_curr;

		// do interp
		for (i = 0; i < sizeof(LPC_FRAME) / sizeof(short); i++)
		{
			short v;

			v = vector1[i] - vector0[i];
			v = (short)((v * ratio) >> LPC_FRAC_BITS);
			vector2[i] = v + vector0[i];
		}

		curr_pitch += s->frame_curr.pitch;
		if (curr_pitch > 0)
			return curr_pitch;
	}

	if (s->frame_curr.pitch != 0) {
		curr_pitch += s->frame_curr.pitch;
		return curr_pitch;
	}

	return 0x80;
}

#define LPC_CLAMP_P 27500 // to be fixed
#define LPC_CLAMP_N -(LPC_CLAMP_P + 1)

static void lpc_synth_do_filter(LPC_SYNTH* s)
{
	int i;
	int sample;
	short* x;
	short* k;

	x = s->x;
	k = s->frame_curr.k;
	sample = s->synth_out;

	for (i = 0; i < LPC_ORDER; i++) {
		int index;

		index = LPC_ORDER - 1 - i;

		sample -= (k[index] * x[index]) >> LPC_FRAC_BITS;

		if (sample > LPC_CLAMP_P)
			sample = LPC_CLAMP_P;
		else if (sample < LPC_CLAMP_N)
			sample = LPC_CLAMP_N;

		x[index + 1] = x[index] + ((sample * k[index]) >> LPC_FRAC_BITS);
	}

	x[0] = s->synth_out = (short)sample;
}

static short random_gen(LPC_SYNTH* s)
{
	short r;
	short seed;

	r = 0;

	seed = s->random_seed << 1;

	r = ((seed >> 12) ^ (seed >> 13)) & 1;

	s->random_seed = seed | r;

	return r;
}

static short* lpc_synth_run(LPC_SYNTH* s, short* pcm_out, int* eos)
{
	int i;
	int excit;

	if (LPC_STATE_STOPPED == s->state) {
		if (eos) *eos = 1;
		return pcm_out;
	}

	excit = 0;
	if (eos) *eos = 0;

	for (i = 0; i < LPC_SAMPLES_PER_FRAME; i++) {
		s->sample_index = i;
		s->curr_pitch -= 16;

		if (s->curr_pitch < 0)
			s->curr_pitch = lpc_reload_pitch(s);

		if (0 == s->frame_curr.pitch) {
			// unvoiced
			if (s->frame_curr.energy) {
				excit = random_gen(s) ? 1408 : -1408;
				excit = (excit * s->frame_curr.energy) >> LPC_FRAC_BITS;
			} else {
				// silent
				excit = 0;
			}
		} else if (s->curr_pitch >= 160) {
			excit = 0;
		} else {
			// lpc voiced excit
			excit = lpc_excit[s->curr_pitch];
			excit = (excit * s->frame_curr.energy) >> LPC_FRAC_BITS;
		}

		excit *= 8;

		// setup lpc
		s->synth_out = (short)excit;

		lpc_synth_do_filter(s);

		*pcm_out++ = s->synth_out;
	}

	// prepare for the next frame
	s->frame_prev = s->frame_next;
	s->frame_curr = s->frame_next;

	if (lpc_get_frame(s, &s->frame_next, &s->frame_prev)) {
		// end of stream
		if (eos) *eos = 1;

		// need restart
		if (s->variant == LPC_STD_VARIANT_Subor) {
			s->bits_left = 0; // clear cache
			s->state = LPC_STATE_FINISHED;
		}
	}

	if (LPC_STATE_RESET == s->state) {
		// do not end stream for SB2K
		if (eos) *eos = 0;
	}

	lpc_set_interp_flag(s);

	return pcm_out;
}

void* lpc_d6_synth_new(int (*feed)(void*, unsigned char*), void* host, int variant)
{
	LPC_SYNTH* lpc;

	lpc = (LPC_SYNTH*)malloc(sizeof(LPC_SYNTH));
	if (!lpc) return 0;

	lpc_synth_init(lpc);

	lpc->feed = feed;
	lpc->host = host;
	lpc->variant = variant;

	lpc->state = LPC_STATE_STARTUP;

	return lpc;
}

int lpc_d6_synth_do(void* lpc, short* pcm, int* pcm_size, int* restart)
{
	LPC_SYNTH* s;
	LPC_SYNTH backup;
	int eos;
	short* pcm_base;

	if (!lpc)
		return LPC_RESULT_ERROR;

	s = (LPC_SYNTH*)lpc;
	backup = *s;

	if (pcm_size)
		*pcm_size = 0;
	if (restart)
		*restart = 0;

	s->need_data = 0;

	if (LPC_STATE_FINISHED == s->state) {
		int cmd;

		cmd = get_nbits(s, 8);

		if (s->need_data) {
			*s = backup;
			return LPC_RESULT_NEED_DATA;
		}

		if (0xF0 == cmd) {
			if (restart)
				*restart = 1;
			s->state = LPC_STATE_RESET;
		}

		return LPC_RESULT_OK;
	}

	if (LPC_STATE_RESET == s->state)
		lpc_d6_synth_reset(s);

	if (LPC_STATE_STARTUP == s->state) {
		lpc_synth_preload(s);

		if (s->need_data) {
			*s = backup;
			return LPC_RESULT_NEED_DATA;
		}

		return LPC_RESULT_OK;
	}

	pcm_base = pcm;
	pcm = lpc_synth_run(s, pcm, &eos);

	if (s->need_data) {
		/*
		 * lpc_synth_run() generates the current 200-sample frame before
		 * parsing the following frame.  On FIFO underflow both the
		 * generated PCM and decoder progress must be discarded so the
		 * same frame can be reproduced after more bytes arrive.
		 */
		*s = backup;
		if (pcm_size)
			*pcm_size = 0;
		return LPC_RESULT_NEED_DATA;
	}

	if (pcm_size)
		*pcm_size = (int)(pcm - pcm_base);

	return eos ? LPC_RESULT_EOS : LPC_RESULT_OK;
}

int lpc_d6_synth_reset(void* lpc)
{
	lpc_feed_t feed;
	void* host;
	LPC_SYNTH* s;
	int variant;

	if (!lpc) return -1;

	s = (LPC_SYNTH*)lpc;

	feed = s->feed;
	host = s->host;
	variant = s->variant;

	lpc_synth_init(s);

	s->feed = feed;
	s->host = host;
	s->variant = variant;

	s->state = LPC_STATE_STARTUP;

	s->LPC_D6 = 1;

	return 0;
}

void lpc_d6_synth_delete(void* lpc)
{
	LPC_SYNTH* s;

	if (!lpc) return;

	s = (LPC_SYNTH*)lpc;

	free(s);
}


int lpc_d6_synth_size(void)
{
	return (int)sizeof(LPC_SYNTH);
}

void lpc_d6_synth_rebind(void* lpc, lpc_feed_t feed, void* host)
{
	LPC_SYNTH* s;

	if (!lpc)
		return;

	s = (LPC_SYNTH*)lpc;
	s->feed = feed;
	s->host = host;
	s->need_data = 0;
}

/*******************************************************************************
                           E N D  O F  F I L E
*******************************************************************************/
