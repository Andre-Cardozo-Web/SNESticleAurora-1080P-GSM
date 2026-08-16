#ifndef _FCEUMM_BRIDGE_H
#define _FCEUMM_BRIDGE_H

#include "types.h"
#include "emuinput.h"

class CRenderSurface;
class CMixBuffer;

Bool FceummBridge_Init(void);
void FceummBridge_Shutdown(void);

Bool FceummBridge_LoadGame(const void *pData,
                           Uint32 nBytes,
                           const char *pszName);
void FceummBridge_UnloadGame(void);

void FceummBridge_Reset(void);

void FceummBridge_RunFrame(Emu::SysInputT *pInput,
                           CRenderSurface *pTarget,
                           CMixBuffer *pMixBuf);

Int32 FceummBridge_GetStateSize(void);
Bool FceummBridge_SaveState(void *pState, Int32 nStateBytes);
Bool FceummBridge_LoadState(const void *pState, Int32 nStateBytes);

Int32 FceummBridge_GetSRAMBytes(void);
Uint8 *FceummBridge_GetSRAMData(void);

Uint32 FceummBridge_GetSampleRate(void);

#endif
