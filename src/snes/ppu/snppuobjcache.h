#ifndef _SNPPUOBJCACHE_H
#define _SNPPUOBJCACHE_H

#include "types.h"

/*
 * Cache de linhas 4bpp de OBJ.
 *
 * Uma entrada guarda somente os oito indices de cor (0..15), sem os bits de
 * paleta OBJ. Assim a mesma arte pode ser reutilizada por carros/sprites com
 * paletas diferentes. O endereco fisico da linha e o H-flip formam a chave,
 * mas os quatro bytes-fonte da VRAM tambem sao comparados em todo acesso.
 * Portanto uma escrita em VRAM nunca consegue devolver pixels antigos, mesmo
 * quando ocorre entre duas scanlines e sem invalidacao global do cache.
 *
 * 512 entradas x 16 bytes = 8 KiB: pequeno o bastante para o EE e grande o
 * bastante para o conjunto quente de OBJ de uma tela cheia.
 */
#ifndef SNPPU_OBJ_CACHE
#define SNPPU_OBJ_CACHE 1
#endif

#define SNPPU_OBJ_ROW_CACHE_BITS       9
#define SNPPU_OBJ_ROW_CACHE_ENTRIES    (1u << SNPPU_OBJ_ROW_CACHE_BITS)
#define SNPPU_OBJ_ROW_CACHE_MASK       (SNPPU_OBJ_ROW_CACHE_ENTRIES - 1u)
#define SNPPU_OBJ_ROW_CACHE_TAG_MASK   0x0001FFFFu

struct SnesPPUObjRowCacheEntryT
{
	/* Bits 0..16: chave + 1 (zero = entrada vazia); bits 24..31: opacidade. */
	Uint32 uTagAndOpaque;
	Uint32 uSource;
	Uint32 uData0;
	Uint32 uData1;
};

struct SnesPPUObjRowCacheT
{
	SnesPPUObjRowCacheEntryT Entry[SNPPU_OBJ_ROW_CACHE_ENTRIES];
};

_INLINE Uint32 SnesPPUObjRowCacheKey(Uint32 uWordAddress, Bool bHFlip)
{
	return (uWordAddress & 0x7FFFu) |
	       ((Uint32)(bHFlip ? 1u : 0u) << 15);
}

_INLINE Uint32 SnesPPUObjRowCacheIndex(Uint32 uKey)
{
	/* Dobra todos os 16 bits da chave para evitar que tiles separados por
	   paginas de VRAM disputem sempre a mesma entrada direta. */
	Uint32 uHash = uKey;
	uHash ^= uHash >> 9;
	uHash ^= uHash >> 5;
	return uHash & SNPPU_OBJ_ROW_CACHE_MASK;
}

_INLINE Bool SnesPPUObjRowCacheLookup(SnesPPUObjRowCacheT *pCache,
	Uint32 uWordAddress, Bool bHFlip, Uint32 uSource,
	Uint32 *pData0, Uint32 *pData1, Uint32 *pOpaque,
	Bool *pSourceChanged)
{
	Uint32 uKey = SnesPPUObjRowCacheKey(uWordAddress, bHFlip);
	Uint32 uTag = uKey + 1u;
	SnesPPUObjRowCacheEntryT *pEntry =
		&pCache->Entry[SnesPPUObjRowCacheIndex(uKey)];

	if ((pEntry->uTagAndOpaque & SNPPU_OBJ_ROW_CACHE_TAG_MASK) != uTag)
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
	*pData0 = pEntry->uData0;
	*pData1 = pEntry->uData1;
	*pOpaque = pEntry->uTagAndOpaque >> 24;
	return TRUE;
}

_INLINE void SnesPPUObjRowCacheStore(SnesPPUObjRowCacheT *pCache,
	Uint32 uWordAddress, Bool bHFlip, Uint32 uSource,
	Uint32 uData0, Uint32 uData1, Uint32 uOpaque)
{
	Uint32 uKey = SnesPPUObjRowCacheKey(uWordAddress, bHFlip);
	SnesPPUObjRowCacheEntryT *pEntry =
		&pCache->Entry[SnesPPUObjRowCacheIndex(uKey)];

	/* Publica a tag por ultimo. Isso tambem deixa a entrada coerente caso o
	   codigo seja futuramente usado por um renderer assincrono. */
	pEntry->uSource = uSource;
	pEntry->uData0 = uData0;
	pEntry->uData1 = uData1;
	pEntry->uTagAndOpaque = (uKey + 1u) | ((uOpaque & 0xFFu) << 24);
}

#endif // _SNPPUOBJCACHE_H
