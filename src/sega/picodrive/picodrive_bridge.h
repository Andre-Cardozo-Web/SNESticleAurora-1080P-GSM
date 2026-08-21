#ifndef _PICODRIVE_BRIDGE_H
#define _PICODRIVE_BRIDGE_H

#include <stddef.h>
#include "types.h"

namespace Emu { struct SysInputT; }
class CRenderSurface;
class CMixBuffer;

bool PicoDriveBridge_Init(void);
void PicoDriveBridge_Shutdown(void);
bool PicoDriveBridge_LoadGame(const void *pData, size_t nBytes, const char *pName);
void PicoDriveBridge_UnloadGame(void);
void PicoDriveBridge_Reset(void);
void PicoDriveBridge_SoftReset(void);

void PicoDriveBridge_Set6Button(bool enabled);
bool PicoDriveBridge_Get6Button(void);
void PicoDriveBridge_SetRenderingMode(int mode);
int  PicoDriveBridge_GetRenderingMode(void);
void PicoDriveBridge_SetAudioRate(int hz);
int  PicoDriveBridge_GetAudioRate(void);
/* AURORA_PD_HOST_CADENCE_V1_20260821 */
int  PicoDriveBridge_GetNominalFrameRate(void);
/* AURORA_PD_MEGA_FIX_20260820 */
bool PicoDriveBridge_IsMasterSystem(void);
bool PicoDriveBridge_Is8Bit(void);

/* Aurora Region Select values are passed straight in:
 * Off/Auto=0, NTSC-U, NTSC-J, PAL. */
void PicoDriveBridge_SetRegion(int auroraRegion);

void PicoDriveBridge_SetMouseInput(bool active, int dx, int dy, unsigned buttons);
/* AURORA_PD_SKIP_DISCARDED_VIDEO_V2_H_20260821 */
void PicoDriveBridge_SetSkipVideo(bool skip);

void PicoDriveBridge_RunFrame(Emu::SysInputT *pInput,
                              CRenderSurface *pTarget,
                              CMixBuffer *pMixBuf);

int PicoDriveBridge_GetStateSize(void);
int PicoDriveBridge_SaveState(void *pData, int nBytes);
bool PicoDriveBridge_LoadState(const void *pData, int nBytes);

int PicoDriveBridge_GetSRAMBytes(void);
Uint8 *PicoDriveBridge_GetSRAMData(void);
unsigned PicoDriveBridge_GetSampleRate(void);

/* AURORA_GS_VRAM_EPOCH_V4_2
 * A new gsKit VRAM epoch invalidates direct-T8 CLUT residency. */
void PicoDriveBridge_InvalidateGsResources(void);

/* AURORA_PD_NATIVE320_DIRECT_T8_V1 */
bool PicoDriveBridge_IsMegaDrive(void);
bool PicoDriveBridge_CanDirectGsVideo(void);
bool PicoDriveBridge_DrawDirectGs(Uint32 auroraOutBaseTBP, Float32 intensity);

#endif
