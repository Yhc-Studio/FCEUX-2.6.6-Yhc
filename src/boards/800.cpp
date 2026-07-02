#include "800_0.h"

int Mapper800Hack = 0;

void Mapper800_Init(CartInfo* info)
{
	Mapper800Hack = 1 << 0;

	switch (info->submapper)
	{
	case 0:
		Mapper_800_0::Init(info);
		break;
	default:
		break;
	}
}

void Mapper800_HBlank(int scanline)
{
	switch (Mapper800Hack)
	{
	case (1 << 0):
		Mapper_800_0::HBlank(scanline);
		break;
	default:
		break;
	}
}
