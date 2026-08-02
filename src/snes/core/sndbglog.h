/*
 * sndbglog.h - Instrumentacao TEMPORARIA de diagnostico de TIMING.
 *
 *  Build temporaria de diagnostico abrangente. Mede os blocos quentes da
 *  emulacao, atividade do GSU, PPU/OBJ e trafego dos ports OAM/VRAM/CGRAM.
 *  Os contadores sao baratos e a saida e' resumida a cada 60 frames para o
 *  proprio logger nao virar o gargalo que estamos tentando encontrar.
 *
 *  Saida via DLog() (EE SIO) -> logs.txt do NetherSX2, prefixo [snes-tmg].
 *
 *  >>> definir SNDBG_LOG 0 (ou remover) antes de release. <<<
 */
#ifndef _SNDBGLOG_H
#define _SNDBGLOG_H

#include "types.h"

#define SNDBG_LOG 1

// resume a cada N frames (60 = ~1 s)
#define SNDBG_FRAME_PERIOD 60

#ifdef __cplusplus
extern "C" {
#endif
void DLog(const char *fmt, ...);   // definido em src/modules/sjpcm/sjpcm_rpc.c
#ifdef __cplusplus
}
#endif

#if SNDBG_LOG
// acumuladores de ciclos por frame das secoes quentes do render.
// Definidos em snes.cpp, alimentados em snppurender8.cpp (RenderLine8).
extern Uint32 g_TmgCycM7;    // ciclos no _FetchMode7 (Mode-7)
extern Uint32 g_TmgCycObj;   // ciclos em FetchOBJ + RenderOBJ8 (sprites)
extern Uint32 g_TmgCycPPU;   // RenderLine completo
extern Uint32 g_TmgCycCPU;   // loop 65816 por scanline (inclusivo)
extern Uint32 g_TmgCycGSU;   // execucao SuperFX/GSU
extern Uint32 g_TmgCycMDMA;  // DMA geral (inclui uploads de OAM/VRAM)
extern Uint32 g_TmgCycHDMA;  // HDMA por scanline
extern Uint32 g_TmgCycAPU;   // execucao SPC700
extern Uint32 g_TmgCycMix;   // mixer DSP de audio
extern Uint32 g_TmgCycBlend; // composicao final main/sub da PPU

// Totais da janela atual (60 frames), alimentados pela PPU/render.
extern Uint32 g_DbgOAMWrites;
extern Uint32 g_DbgVRAMWrites;
extern Uint32 g_DbgCGRAMWrites;
extern Uint32 g_DbgObjEnabledLines;
extern Uint32 g_DbgObjOamRefs;
extern Uint32 g_DbgObjTiles;
extern Uint32 g_DbgObjOpaqueTiles;
extern Uint32 g_DbgObjCandidatePixels;
extern Uint32 g_DbgObjDrawnPixels;
extern Uint32 g_DbgObjEmptyLines;
extern Uint32 g_DbgObjRangeLimitLines;
extern Uint32 g_DbgObjLimitLines;
extern Uint8  g_DbgObjOBSEL;
extern Uint8  g_DbgObjTM;
extern Uint8  g_DbgObjTS;
extern Uint16 g_DbgObjPriority;
#endif

#endif // _SNDBGLOG_H
