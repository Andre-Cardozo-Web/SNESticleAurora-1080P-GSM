/* SNESTICLE_QUICKNES_BRIDGE */
#ifndef _QUICKNES_BRIDGE_H
#define _QUICKNES_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

namespace Emu { struct SysInputT; }
class CRenderSurface;
class CMixBuffer;

bool QuicknesBridge_Init(void);
void QuicknesBridge_Shutdown(void);
bool QuicknesBridge_LoadGame(const void *pData, size_t nBytes, const char *pName);
void QuicknesBridge_UnloadGame(void);
void QuicknesBridge_Reset(void);
void QuicknesBridge_SoftReset(void);
void QuicknesBridge_SetDutySwap(bool enabled);
void QuicknesBridge_RunFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf);
int QuicknesBridge_GetStateSize(void);
bool QuicknesBridge_SaveState(void *pData, int nBytes);
bool QuicknesBridge_LoadState(const void *pData, int nBytes);
int QuicknesBridge_GetSRAMBytes(void);
uint8_t *QuicknesBridge_GetSRAMData(void);
unsigned QuicknesBridge_GetSampleRate(void);

#endif
