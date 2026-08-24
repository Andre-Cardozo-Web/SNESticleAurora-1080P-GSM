#ifndef _PCE_BRIDGE_H
#define _PCE_BRIDGE_H
/* AURORA_PCE_EXPERIMENTAL_V1 */
#include <stddef.h>
#include "types.h"
namespace Emu { struct SysInputT; }
class CRenderSurface;
class CMixBuffer;
bool PceBridge_Init(void);
void PceBridge_Shutdown(void);
bool PceBridge_LoadGame(const void *pData, size_t nBytes, size_t nCapacity, const char *pName);
void PceBridge_UnloadGame(void);
void PceBridge_Reset(void);
void PceBridge_SoftReset(void);
void PceBridge_RunFrame(Emu::SysInputT *pInput, CRenderSurface *pTarget, CMixBuffer *pMixBuf);
int PceBridge_GetStateSize(void);
int PceBridge_SaveState(void *pData, int nBytes);
bool PceBridge_LoadState(const void *pData, int nBytes);
int PceBridge_GetSRAMBytes(void);
Uint8 *PceBridge_GetSRAMData(void);
void PceBridge_InvalidateGsResources(void);
bool PceBridge_CanDirectGsVideo(void);
bool PceBridge_DrawDirectGs(Uint32 auroraOutBaseTBP, Float32 intensity);
unsigned PceBridge_GetSampleRate(void);
#endif
