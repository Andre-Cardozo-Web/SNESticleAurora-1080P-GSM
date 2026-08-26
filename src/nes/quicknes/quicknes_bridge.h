/* SNESTICLE_QUICKNES_BRIDGE */
#ifndef _QUICKNES_BRIDGE_H
#define _QUICKNES_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#include "types.h"
namespace Emu { struct SysInputT; }
class CRenderSurface;
class CMixBuffer;

/*
 * Native QuickNES snapshots are currently well below this bound.
 * Keep a fixed capacity so asking for the state size never needs to
 * serialize the machine merely to discover the length.
 */
enum { QUICKNES_STATE_CAPACITY = 64 * 1024 };

bool QuicknesBridge_Init(void);
void QuicknesBridge_Shutdown(void);
bool QuicknesBridge_LoadGame(const void *pData, size_t nBytes, const char *pName);
void QuicknesBridge_UnloadGame(void);
void QuicknesBridge_Reset(void);
void QuicknesBridge_SoftReset(void);
void QuicknesBridge_SetDutySwap(bool enabled);
/* AURORA_CONTROLLER_OPTIONS_V2 */
void QuicknesBridge_SetTurboSpeed(unsigned speedShift);
void QuicknesBridge_SetSkipVideo(bool skip); /* AURORA_SAFE_FRAMESKIP_GG_ZOOM_V2_2 */
/* AURORA_SNES9X2010_V5_ALLCORES_PERF_20260824 */
void QuicknesBridge_InvalidateGsResources(void);
bool QuicknesBridge_CanDirectGsVideo(void);
bool QuicknesBridge_DrawDirectGs(Uint32 auroraOutBaseTBP,
                                 Int32 logicalY,
                                 Float32 intensity);
void QuicknesBridge_RunFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf);
int QuicknesBridge_GetStateSize(void);
int QuicknesBridge_SaveState(void *pData, int nBytes);
bool QuicknesBridge_LoadState(const void *pData, int nBytes);
int QuicknesBridge_GetSRAMBytes(void);
uint8_t *QuicknesBridge_GetSRAMData(void);
unsigned QuicknesBridge_GetSampleRate(void);

#endif
