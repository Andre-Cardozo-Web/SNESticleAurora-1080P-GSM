#include <cstdio>
#include <cstring>

#include "types.h"
#include "snppumode7.h"

static Uint16 g_Vram[0x4000] _ALIGN(64);
static Uint32 g_Random = 0x4D374F50u;

static Uint32 NextRandom()
{
	g_Random = g_Random * 1664525u + 1013904223u;
	return g_Random;
}

static void FetchMode7Reference(Uint8 *pLine, Uint8 *pPriority,
	Uint8 *pOpaque, const Uint16 *pVram, Int32 x, Int32 y,
	Int32 dx, Int32 dy, Uint32 uScreenOver, Bool bExtendedBG)
{
	Int32 iPixel;

	std::memset(pPriority, bExtendedBG ? 0 : 0xFF, 32);
	std::memset(pOpaque, 0, 32);

	for (iPixel = 0; iPixel < 256; iPixel++)
	{
		Int32 x2 = x >> 8;
		Int32 y2 = y >> 8;
		Uint8 uPixel;
		Uint32 uTileAddr;
		Uint32 uCharAddr;

		x += dx;
		y += dy;

		if (uScreenOver == 0)
		{
			x2 &= 0x3FF;
			y2 &= 0x3FF;
		}

		uTileAddr = (((Uint32)(y2 >> 3) << 7) |
		             (Uint32)(x2 >> 3)) & 0x3FFFu;
		uCharAddr = pVram[uTileAddr] & 0xFFu;
		if (uScreenOver == 3 && ((x2 | y2) >> 10))
			uCharAddr = 0;
		uCharAddr = (uCharAddr << 6) + (Uint32)(x2 & 7) +
		            ((Uint32)(y2 & 7) << 3);
		uPixel = (Uint8)(pVram[uCharAddr] >> 8);
		if (uScreenOver != 0 && uScreenOver != 3 &&
		    ((x2 | y2) >> 10))
			uPixel = 0;

		if (bExtendedBG)
		{
			if (uPixel & 0x80)
				pPriority[iPixel >> 3] |= (Uint8)(1u << (iPixel & 7));
			uPixel &= 0x7F;
		}
		pLine[iPixel] = uPixel;
		if (uPixel)
			pOpaque[iPixel >> 3] |= (Uint8)(1u << (iPixel & 7));
	}
}

static int RunCase(Int32 x, Int32 y, Int32 dx, Int32 dy,
	Uint32 uScreenOver, Bool bExtendedBG, Uint32 uCase)
{
	Uint8 expectedLine[256] _ALIGN(16);
	Uint8 actualLine[256] _ALIGN(16);
	Uint8 expectedPriority[32] _ALIGN(16);
	Uint8 actualPriority[32] _ALIGN(16);
	Uint8 expectedOpaque[32] _ALIGN(16);
	Uint8 actualOpaque[32] _ALIGN(16);

	std::memset(actualLine, 0xCD, sizeof(actualLine));
	std::memset(actualPriority, 0xCD, sizeof(actualPriority));
	std::memset(actualOpaque, 0xCD, sizeof(actualOpaque));
	FetchMode7Reference(expectedLine, expectedPriority, expectedOpaque,
		g_Vram, x, y, dx, dy, uScreenOver, bExtendedBG);

	switch (uScreenOver)
	{
	case 0:
		SnesPPUMode7FetchRepeat(actualLine, actualPriority, actualOpaque,
			256, g_Vram, x, y, dx, dy, bExtendedBG);
		break;
	case 3:
		SnesPPUMode7FetchClamp(actualLine, actualPriority, actualOpaque,
			256, g_Vram, x, y, dx, dy, bExtendedBG);
		break;
	default:
		SnesPPUMode7FetchBlack(actualLine, actualPriority, actualOpaque,
			256, g_Vram, x, y, dx, dy, bExtendedBG);
		break;
	}

	if (std::memcmp(expectedLine, actualLine, sizeof(actualLine)) ||
	    std::memcmp(expectedPriority, actualPriority,
	                sizeof(actualPriority)) ||
	    std::memcmp(expectedOpaque, actualOpaque, sizeof(actualOpaque)))
	{
		Int32 i;
		for (i = 0; i < 256; i++)
		{
			if (expectedLine[i] != actualLine[i])
			{
				std::printf("FAIL Mode7 case=%u mode=%u ext=%u pixel=%d "
				            "line=%02X/%02X x/y=%d/%d d=%d/%d\n",
				            (unsigned)uCase, (unsigned)uScreenOver,
				            (unsigned)bExtendedBG, (int)i,
				            (unsigned)expectedLine[i],
				            (unsigned)actualLine[i], (int)x, (int)y,
				            (int)dx, (int)dy);
				return 1;
			}
		}
		std::printf("FAIL Mode7 masks case=%u mode=%u ext=%u "
		            "x/y=%d/%d d=%d/%d\n", (unsigned)uCase,
		            (unsigned)uScreenOver, (unsigned)bExtendedBG,
		            (int)x, (int)y, (int)dx, (int)dy);
		return 1;
	}
	return 0;
}

int main()
{
	Uint32 i;
	Uint32 uCase = 0;
	Uint32 uMode;
	Uint32 uExtended;

	for (i = 0; i < 0x4000; i++)
		g_Vram[i] = (Uint16)NextRandom();

	/* Identidade, espelhos e coordenadas externas pegam as bordas antes dos
	   casos pseudoaleatorios. */
	for (uMode = 0; uMode < 4; uMode++)
	{
		for (uExtended = 0; uExtended < 2; uExtended++)
		{
			if (RunCase(0, 0, 0x100, 0, uMode,
			              uExtended != 0, uCase++)) return 1;
			if (RunCase(0x3FF00, 0x3FF00, -0x100, -0x80, uMode,
			              uExtended != 0, uCase++)) return 1;
			if (RunCase(-0x20000, 0x50000, 0x180, -0x140, uMode,
			              uExtended != 0, uCase++)) return 1;
		}
	}

	for (uMode = 0; uMode < 4; uMode++)
	{
		for (uExtended = 0; uExtended < 2; uExtended++)
		{
			for (i = 0; i < 512; i++)
			{
				Int32 x = (Int32)(NextRandom() & 0x1FFFFF) - 0x100000;
				Int32 y = (Int32)(NextRandom() & 0x1FFFFF) - 0x100000;
				Int32 dx = (Int32)(NextRandom() & 0x3FFF) - 0x2000;
				Int32 dy = (Int32)(NextRandom() & 0x3FFF) - 0x2000;
				if (RunCase(x, y, dx, dy, uMode,
				              uExtended != 0, uCase++)) return 1;
			}
		}
	}

	std::printf("mode7_test: OK (%u casos equivalentes)\n",
	            (unsigned)uCase);
	return 0;
}
