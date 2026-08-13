#ifndef _SNPPUMODE7_H
#define _SNPPUMODE7_H

#include "types.h"

/* O mapa de Mode 7 guarda o numero do tile no byte baixo de cada palavra e
   os pixels no byte alto da area de caracteres. */
_INLINE Uint32 SnesPPUMode7CharBase(const Uint16 *pVram,
	Uint32 uTileAddr, Uint32 *pLastTileAddr, Uint32 *pLastCharBase)
{
	if (uTileAddr != *pLastTileAddr)
	{
		*pLastTileAddr = uTileAddr;
		*pLastCharBase = (pVram[uTileAddr] & 0xFFu) << 6;
	}
	return *pLastCharBase;
}

_INLINE Uint8 SnesPPUMode7PriorityMask8(Uint64 uData)
{
	Uint64 uPri64 = uData & 0x8080808080808080ULL;
	Uint64 uPriority = 0;

	uPriority |= (uPri64 >> (0x00 + 7)) << 0;
	uPriority |= (uPri64 >> (0x08 + 7)) << 1;
	uPriority |= (uPri64 >> (0x10 + 7)) << 2;
	uPriority |= (uPri64 >> (0x18 + 7)) << 3;
	uPriority |= (uPri64 >> (0x20 + 7)) << 4;
	uPriority |= (uPri64 >> (0x28 + 7)) << 5;
	uPriority |= (uPri64 >> (0x30 + 7)) << 6;
	uPriority |= (uPri64 >> (0x38 + 7)) << 7;
	return (Uint8)uPriority;
}

_INLINE Uint8 SnesPPUMode7OpaqueMask8(Uint64 uData)
{
#if CODE_PLATFORM == CODE_PS2
	Uint64 uZero = 0;
	Uint64 uOne = 0xFFFFFFFFFFFFFFFFULL;
	Uint64 uCompare = uData;
	Uint64 uOpaque = 0;

	__asm__ (
		"pceqb      %0,%0,$0        \n"
		: "+r" (uCompare)
		);

	if (uCompare == uZero)
		return 0xFF;
	if (uCompare == uOne)
		return 0x00;

	uCompare &= 0x8080808080808080ULL;
	uOpaque |= (uCompare >> (0x00 + 7)) << 0;
	uOpaque |= (uCompare >> (0x08 + 7)) << 1;
	uOpaque |= (uCompare >> (0x10 + 7)) << 2;
	uOpaque |= (uCompare >> (0x18 + 7)) << 3;
	uOpaque |= (uCompare >> (0x20 + 7)) << 4;
	uOpaque |= (uCompare >> (0x28 + 7)) << 5;
	uOpaque |= (uCompare >> (0x30 + 7)) << 6;
	uOpaque |= (uCompare >> (0x38 + 7)) << 7;
	return (Uint8)(uOpaque ^ 0xFFu);
#else
	Uint8 uOpaque = 0;
	Int32 iPixel;
	for (iPixel = 0; iPixel < 8; iPixel++)
	{
		if ((uData >> (iPixel * 8)) & 0xFFu)
			uOpaque |= (Uint8)(1u << iPixel);
	}
	return uOpaque;
#endif
}

_INLINE void SnesPPUMode7Store8(Uint64 *pLine, Uint8 *pPriority,
	Uint8 *pOpaque, Uint64 uData, Bool bExtendedBG)
{
	if (bExtendedBG)
	{
		if (pPriority)
			*pPriority = SnesPPUMode7PriorityMask8(uData);
		uData &= 0x7F7F7F7F7F7F7F7FULL;
	}
	else if (pPriority)
	{
		*pPriority = 0xFF;
	}

	*pLine = uData;
	*pOpaque = SnesPPUMode7OpaqueMask8(uData);
}

/* Cada rotina produz oito pixels por iteracao. Alem de evitar a segunda
   passagem de 256 bytes usada para montar a mascara de opacidade, o ultimo
   tile do mapa fica em registrador: transformacoes normais permanecem varios
   pixels dentro do mesmo tile 8x8 e deixam de reler o numero do tile para
   cada pixel. */
_INLINE void SnesPPUMode7FetchRepeat(Uint8 *pLine, Uint8 *pPriority,
	Uint8 *pOpaque, Int32 nPixels, const Uint16 *pVram,
	Int32 x, Int32 y, Int32 dx, Int32 dy, Bool bExtendedBG)
{
	Uint64 *pLine64 = (Uint64 *)pLine;
	Uint32 uLastTileAddr = 0xFFFFFFFFu;
	Uint32 uLastCharBase = 0;

	while (nPixels > 0)
	{
		Uint64 uData = 0;
		Int32 iPixel;
		#if defined(__GNUC__) && (__GNUC__ >= 8)
		#pragma GCC unroll 8
		#endif
		for (iPixel = 0; iPixel < 8; iPixel++)
		{
			Int32 x2 = x >> 8;
			Int32 y2 = y >> 8;
			Uint32 uTileAddr;
			Uint32 uCharBase;
			Uint32 uCharAddr;

			x += dx;
			y += dy;
			x2 &= 0x3FF;
			y2 &= 0x3FF;
			uTileAddr = ((Uint32)(y2 >> 3) << 7) |
			            (Uint32)(x2 >> 3);
			uCharBase = SnesPPUMode7CharBase(pVram, uTileAddr,
				&uLastTileAddr, &uLastCharBase);
			uCharAddr = uCharBase + (Uint32)(x2 & 7) +
			            ((Uint32)(y2 & 7) << 3);
			uData |= (Uint64)(pVram[uCharAddr] >> 8) << (iPixel * 8);
		}

		SnesPPUMode7Store8(pLine64, pPriority, pOpaque, uData,
			bExtendedBG);
		pLine64++;
		if (pPriority) pPriority++;
		pOpaque++;
		nPixels -= 8;
	}
}

_INLINE void SnesPPUMode7FetchClamp(Uint8 *pLine, Uint8 *pPriority,
	Uint8 *pOpaque, Int32 nPixels, const Uint16 *pVram,
	Int32 x, Int32 y, Int32 dx, Int32 dy, Bool bExtendedBG)
{
	Uint64 *pLine64 = (Uint64 *)pLine;
	Uint32 uLastTileAddr = 0xFFFFFFFFu;
	Uint32 uLastCharBase = 0;

	while (nPixels > 0)
	{
		Uint64 uData = 0;
		Int32 iPixel;
		#if defined(__GNUC__) && (__GNUC__ >= 8)
		#pragma GCC unroll 8
		#endif
		for (iPixel = 0; iPixel < 8; iPixel++)
		{
			Int32 x2 = x >> 8;
			Int32 y2 = y >> 8;
			Uint32 uCharBase;
			Uint32 uCharAddr;

			x += dx;
			y += dy;
			if ((x2 | y2) >> 10)
			{
				uCharBase = 0;
			}
			else
			{
				Uint32 uTileAddr = ((Uint32)(y2 >> 3) << 7) |
				                       (Uint32)(x2 >> 3);
				uCharBase = SnesPPUMode7CharBase(pVram, uTileAddr,
					&uLastTileAddr, &uLastCharBase);
			}
			uCharAddr = uCharBase + (Uint32)(x2 & 7) +
			            ((Uint32)(y2 & 7) << 3);
			uData |= (Uint64)(pVram[uCharAddr] >> 8) << (iPixel * 8);
		}

		SnesPPUMode7Store8(pLine64, pPriority, pOpaque, uData,
			bExtendedBG);
		pLine64++;
		if (pPriority) pPriority++;
		pOpaque++;
		nPixels -= 8;
	}
}

_INLINE void SnesPPUMode7FetchBlack(Uint8 *pLine, Uint8 *pPriority,
	Uint8 *pOpaque, Int32 nPixels, const Uint16 *pVram,
	Int32 x, Int32 y, Int32 dx, Int32 dy, Bool bExtendedBG)
{
	Uint64 *pLine64 = (Uint64 *)pLine;
	Uint32 uLastTileAddr = 0xFFFFFFFFu;
	Uint32 uLastCharBase = 0;

	while (nPixels > 0)
	{
		Uint64 uData = 0;
		Int32 iPixel;
		#if defined(__GNUC__) && (__GNUC__ >= 8)
		#pragma GCC unroll 8
		#endif
		for (iPixel = 0; iPixel < 8; iPixel++)
		{
			Int32 x2 = x >> 8;
			Int32 y2 = y >> 8;
			Uint8 uPixel = 0;

			x += dx;
			y += dy;
			if (!((x2 | y2) >> 10))
			{
				Uint32 uTileAddr = ((Uint32)(y2 >> 3) << 7) |
				                       (Uint32)(x2 >> 3);
				Uint32 uCharBase = SnesPPUMode7CharBase(pVram,
					uTileAddr, &uLastTileAddr, &uLastCharBase);
				Uint32 uCharAddr = uCharBase + (Uint32)(x2 & 7) +
				                       ((Uint32)(y2 & 7) << 3);
				uPixel = (Uint8)(pVram[uCharAddr] >> 8);
			}
			uData |= (Uint64)uPixel << (iPixel * 8);
		}

		SnesPPUMode7Store8(pLine64, pPriority, pOpaque, uData,
			bExtendedBG);
		pLine64++;
		if (pPriority) pPriority++;
		pOpaque++;
		nPixels -= 8;
	}
}

#endif // _SNPPUMODE7_H
