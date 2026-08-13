#ifndef _SNPPUOBJCACHE_H
#define _SNPPUOBJCACHE_H

#include "types.h"

/*
 * Cache de linhas 4bpp de OBJ.
 *
 * O S-PPU enxerga duas tabelas logicas de 256 tiles; cada tile possui oito
 * linhas. Isso forma exatamente 4096 slots, portanto nao e necessario usar
 * hash nem expulsar uma linha quente para colocar outra. A revisao anterior
 * usava 512 entradas diretas e o grid de Top Gear fazia essas entradas
 * colidirem continuamente, mesmo sem nenhuma escrita em VRAM.
 *
 * Cada slot guarda a linha decodificada na orientacao canonica (sem H-flip),
 * sem os bits de paleta OBJ. O H-flip e apenas uma inversao dos oito bytes e
 * as paletas continuam sendo aplicadas depois do lookup. Os quatro bytes da
 * VRAM ainda sao comparados em todo acesso, entao troca de OBSEL, DMA,
 * animacao, pause ou load state nunca devolve pixels antigos.
 *
 * 4096 entradas x 12 bytes = 48 KiB. O bit 31 de uData0 e o bit de validade:
 * ele e livre porque cada byte decodificado contem apenas um indice 0..15.
 */
#ifndef SNPPU_OBJ_CACHE
#define SNPPU_OBJ_CACHE 1
#endif

#define SNPPU_OBJ_ROW_CACHE_ENTRIES    (2u * 256u * 8u)
#define SNPPU_OBJ_ROW_CACHE_VALID       0x80000000u

struct SnesPPUObjRowCacheEntryT
{
	Uint32 uSource;
	Uint32 uData0;
	Uint32 uData1;
};

struct SnesPPUObjRowCacheT
{
	SnesPPUObjRowCacheEntryT Entry[SNPPU_OBJ_ROW_CACHE_ENTRIES];
};

_INLINE Uint32 SnesPPUObjRowCacheIndex(Bool bSecondTable,
	Uint32 uTile, Uint32 uYoff)
{
	Uint32 uLogicalTile = (uTile & 0xFFu) |
		(bSecondTable ? 0x100u : 0u);
	return (uLogicalTile << 3) | (uYoff & 7u);
}

/* Inverte os oito pixels ja decodificados. Mantem o cache independente do
   H-flip e reduz pela metade o numero de slots necessarios. */
_INLINE Uint64 SnesPPUObjRowHFlip64(Uint64 uData)
{
	uData = ((uData & 0x00FF00FF00FF00FFULL) << 8) |
	        ((uData & 0xFF00FF00FF00FF00ULL) >> 8);
	uData = ((uData & 0x0000FFFF0000FFFFULL) << 16) |
	        ((uData & 0xFFFF0000FFFF0000ULL) >> 16);
	return (uData << 32) | (uData >> 32);
}

_INLINE Bool SnesPPUObjRowCacheLookup(SnesPPUObjRowCacheT *pCache,
	Bool bSecondTable, Uint32 uTile, Uint32 uYoff, Uint32 uSource,
	Uint32 *pData0, Uint32 *pData1, Bool *pSourceChanged)
{
	SnesPPUObjRowCacheEntryT *pEntry = &pCache->Entry[
		SnesPPUObjRowCacheIndex(bSecondTable, uTile, uYoff)];
	Uint32 uStoredData0 = pEntry->uData0;

	if (!(uStoredData0 & SNPPU_OBJ_ROW_CACHE_VALID))
	{
		if (pSourceChanged) *pSourceChanged = FALSE;
		return FALSE;
	}

	if (pEntry->uSource != uSource)
	{
		if (pSourceChanged) *pSourceChanged = TRUE;
		return FALSE;
	}

	if (pSourceChanged) *pSourceChanged = FALSE;
	*pData0 = uStoredData0 & ~SNPPU_OBJ_ROW_CACHE_VALID;
	*pData1 = pEntry->uData1;
	return TRUE;
}

_INLINE void SnesPPUObjRowCacheStore(SnesPPUObjRowCacheT *pCache,
	Bool bSecondTable, Uint32 uTile, Uint32 uYoff, Uint32 uSource,
	Uint32 uData0, Uint32 uData1)
{
	SnesPPUObjRowCacheEntryT *pEntry = &pCache->Entry[
		SnesPPUObjRowCacheIndex(bSecondTable, uTile, uYoff)];

	pEntry->uSource = uSource;
	pEntry->uData1 = uData1;
	/* Publica a validade por ultimo. */
	pEntry->uData0 = (uData0 & ~SNPPU_OBJ_ROW_CACHE_VALID) |
	                SNPPU_OBJ_ROW_CACHE_VALID;
}

#endif // _SNPPUOBJCACHE_H
