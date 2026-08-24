/* AURORA_SNES9X2010_V1 */
#ifndef _SNES9X2010_BRIDGE_H
#define _SNES9X2010_BRIDGE_H

#include <stddef.h>
#include "types.h"

namespace Emu { struct SysInputT; }
class CRenderSurface;
class CMixBuffer;

bool Snes9x2010Bridge_Init(void);
void Snes9x2010Bridge_Shutdown(void);
bool Snes9x2010Bridge_LoadGame(const void *pData, size_t nBytes,
                               size_t nCapacity, const char *pName);
void Snes9x2010Bridge_UnloadGame(void);
void Snes9x2010Bridge_Reset(void);
void Snes9x2010Bridge_SoftReset(void);
/* AURORA_SNES9X2010_V4_PS2_PERF_20260824 */
void Snes9x2010Bridge_InvalidateGsResources(void);
bool Snes9x2010Bridge_CanDirectGsVideo(void);
bool Snes9x2010Bridge_DrawDirectGs(Uint32 outTexTBP, Float32 intensity);
void Snes9x2010Bridge_RunFrame(Emu::SysInputT *pInput,
                               CRenderSurface *pTarget,
                               CMixBuffer *pMix);
Int32 Snes9x2010Bridge_GetStateSize(void);
Int32 Snes9x2010Bridge_SaveState(void *pData, Int32 nBytes);
bool Snes9x2010Bridge_LoadState(const void *pData, Int32 nBytes);
Int32 Snes9x2010Bridge_GetSRAMBytes(void);
Uint8 *Snes9x2010Bridge_GetSRAMData(void);
Uint32 Snes9x2010Bridge_GetSampleRate(void);

#endif
