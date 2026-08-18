/* gpfifo.c
 *
 * The original GPFifo was a hand-rolled double-buffered DMA queue
 * for GIF path-3 commands, with the body built incrementally via
 * gslist.c (GSGifTagOpenAD / GSGifRegAD / GSDmaCntOpen / ...).
 *
 * After the Fase 1 GS->gsKit migration, regular UI / font / poly
 * drawing goes through gsKit's own queue (see gpprim.c). The legacy
 * gslist mechanism is still required by the SNES blender
 * (snes/ppu/snppublend_gs.cpp), which builds raw GIF chains for its
 * Begin / End register writes. To keep the blender working without
 * touching it, we keep the old double-buffered gslist alive here and
 * simply make sure gsKit's DMA is drained any time we flip from the
 * gsKit queue to the legacy chain (or vice versa) on the GIF
 * channel.
 */

#include <stdio.h>

#include <tamtypes.h>
#include <kernel.h>

#include "types.h"
#include "gs.h"
#include "gslist.h"
#include "ps2dma.h"
#include "gpfifo.h"
#include "gskit_backend.h"

static Uint128 *_GPFifo_pLists[2];
static Uint32   _GPFifo_nListQwords;
static Uint32   _GPFifo_iCurList;
static Bool     _GPFifo_bInited = FALSE;

/* AURORA_COMPAT_GPFIFO_STATE */
static Bool _GPFifo_bCompatFullCache = FALSE;

void GPFifoSetCompatFullCache(Bool enabled)
{
    _GPFifo_bCompatFullCache = enabled ? TRUE : FALSE;
}

Bool GPFifoGetCompatFullCache(void)
{
    return _GPFifo_bCompatFullCache;
}

/* Kept for debugging the bridged gslist path. Marked unused so
   GCC stops warning about it; flip the call site in GPFifoPause to
   (re-)enable. */
static void _GPFifoDumpList(void) __attribute__((unused));
static void _GPFifoDumpList(void)
{
    Uint32 *pStart, *pEnd;

    pStart = (Uint32 *)GSListGetStart();
    pEnd   = (Uint32 *)GSListGetPtr();

    printf("GPFifo: List %08X -> %08X\n",
        (Uint32)pStart, (Uint32)pEnd);
}

void GPFifoFlush(void)
{
    /* AURORA_GPFIFO_EMPTY_FASTPATH_V2
     *
     * GPFifoResume() begins a GSList and immediately opens one DMA CNT tag.
     * Therefore GSListGetSize()==1 is the exact idle representation: there
     * are no GIF commands and no REF payload after that open CNT.
     *
     * We still submit/drain gsKit because this function is the end-of-frame
     * host flush. We simply avoid closing, cache-syncing, DMA-kicking,
     * swapping and reopening a raw chain that contains no commands.
     *
     * Any actual blender/setup write advances the list beyond qword 1 and
     * falls through to the historical Pause/Resume path unchanged. */
    if (_GPFifo_bInited &&
        GSListGetSize() == 1 &&
        !GSListHasDmaRefs())
    {
        GSK_DrainForRawGif();
        return;
    }

    GPFifoPause();
    GPFifoResume();
}

void GPFifoPause(void)
{
    if (!_GPFifo_bInited) {
        /* Nothing to flush - just make sure gsKit's DMA has finished feeding
           path 3 before a raw producer can use the same GIF channel.
           AURORA_GS_RAWGIF_DRAIN_V1 */
        GSK_DrainForRawGif();
        return;
    }

    /* AURORA_GS_RAWGIF_DRAIN_V1
     * Serialize the producer switch at GIF-DMA completion, not full GS
     * raster completion. Command order remains FIFO on the same path 3. */
    GSK_DrainForRawGif();

    /* close current dma cnt */
    GSDmaCntClose();

    /* add end tag */
    GSDmaEnd();

    /* AURORA_V83_GPFIFO_RANGE_DCACHE
     * GSK_DrainAndWait() above has already serialized gsKit and GIF DMA.
     * The current bridge list is ordinarily just Begin/End GS register
     * commands.  Writing back+invalidating the entire EE D-cache here threw
     * away hot emulator state for a few dozen qwords.
     *
     * Keep a conservative future-proof guard: if GSDmaRef() was used, retain
     * the old FlushCache(0), because the REF payload can live outside this
     * list.  With no REF, synchronize only the qwords the DMAC will read. */
    if (_GPFifo_bCompatFullCache || GSListHasDmaRefs())
    {
        /* Full compatibility mode restores the old whole-cache fallback. */
        FlushCache(0);
    }
    else
    {
        Uint128 *pStart = GSListGetStart();
        Int32 nQwords = GSListGetSize();
        Uint32 nBytes = nQwords > 0 ? (Uint32)nQwords * sizeof(Uint128) : 0;
        if (pStart && nBytes > 0)
        {
            /* The EE D-cache is 8 KiB.  Walking a range larger than the
               complete cache cannot save work, so retain FlushCache(0) for
               unusually large lists. */
            if (nBytes >= 8192U)
                FlushCache(0);
            else
                SyncDCache(pStart, (Uint8 *)pStart + nBytes - 1);
        }
    }

    /* end list */
    GSListEnd();

    /* Keep this second guard even though GSK_DrainAndWait already syncs GIF:
       DmaSyncGIF has a bounded timeout in this project, so retaining it keeps
       the pre-V8.3 failure tolerance exactly unchanged. */
    DmaSyncGIF();

    /* transfer current list */
    DmaExecGIFChain(_GPFifo_pLists[_GPFifo_iCurList]);

    /* swap lists */
    _GPFifo_iCurList ^= 1;

    /* GS state has been touched outside gsKit; force a TEXFLUSH on
       the next gsKit textured prim. */
    GSK_InvalidateTextureCache();
}

void GPFifoResume(void)
{
    if (!_GPFifo_bInited) {
        return;
    }

    /* start new list */
    GSListBegin(_GPFifo_pLists[_GPFifo_iCurList],
                _GPFifo_nListQwords,
                (GSListFlushFuncT)GPFifoFlush);

    /* open a dma cnt */
    GSDmaCntOpen();
}

void GPFifoSync(void)
{
    DmaSyncGIF();
    GSK_DrainAndWait();
}

Uint64 *GPFifoOpen(Uint32 nMinQwords)
{
    if (!_GPFifo_bInited) {
        return NULL;
    }

    if (GSListSpace(nMinQwords)) {
        return (Uint64 *)GSListGetPtr();
    }
    return NULL;
}

void GPFifoClose(Uint64 *pPtr)
{
    if (!_GPFifo_bInited) {
        return;
    }
    GSListSetPtr((Uint128 *)pPtr);
}

void GPFifoInit(Uint128 *pMem, Int32 nBytes)
{
    assert(!(((Uint32)pMem) & 0xF));

    /* calculate size of each list */
    _GPFifo_nListQwords = (nBytes / sizeof(Uint128)) / 2;

    /* set address of each list */
    _GPFifo_pLists[0] = pMem;
    _GPFifo_pLists[1] = pMem + _GPFifo_nListQwords;

    _GPFifo_iCurList = 0;
    _GPFifo_bInited  = TRUE;

    GPFifoResume();

    printf("GPFifo: Init %08X %08X (%d qwords) [gsKit-bridge]\n",
        (Uint32)_GPFifo_pLists[0],
        (Uint32)_GPFifo_pLists[1],
        _GPFifo_nListQwords);
}

void GPFifoShutdown(void)
{
    printf("GPFifo: Shutdown\n");
}
