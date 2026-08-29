/* AURORA_FCEUMM_FDS_V0_6_RUNTIME */
#ifndef _AURORA_FCEUMM_FDS_BRIDGE_H
#define _AURORA_FCEUMM_FDS_BRIDGE_H

#include "types.h"
namespace Emu { struct SysInputT; }
class CRenderSurface;
class CMixBuffer;

bool FceummFdsBridge_Init(void);
void FceummFdsBridge_Shutdown(void);
bool FceummFdsBridge_LoadDisk(const char *path, const char *systemPath,
                              unsigned totalSides);
/* AURORA_FDS_V4_ZIP_BRIDGE_MEMORY_20260828 */
bool FceummFdsBridge_LoadDiskMemory(const void *data, Uint32 bytes,
                                    const char *contentName,
                                    const char *systemPath,
                                    unsigned totalSides);
void FceummFdsBridge_UnloadGame(void);
void FceummFdsBridge_Reset(void);
void FceummFdsBridge_SoftReset(void);
void FceummFdsBridge_SetSkipVideo(bool skip);
/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: shared 192-byte NES RGB palette. */
bool FceummFdsBridge_SetPalette(const Uint8 *rgb192);
/* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827: direct native indexed-video path. */
void FceummFdsBridge_InvalidateGsResources(void);
bool FceummFdsBridge_CanDirectGsVideo(void);
bool FceummFdsBridge_DrawDirectGs(Uint32 auroraOutBaseTBP,
                                  Int32 logicalY,
                                  Float32 intensity);
void FceummFdsBridge_RunFrame(Emu::SysInputT *input,
                              CRenderSurface *target,
                              CMixBuffer *mix);
int FceummFdsBridge_GetStateSize(void);
int FceummFdsBridge_SaveState(void *data, int bytes);
bool FceummFdsBridge_LoadState(const void *data, int bytes);
bool FceummFdsBridge_BeginSideSwap(void);
bool FceummFdsBridge_IsSideSwapPending(void);
unsigned FceummFdsBridge_GetSelectedSide(void);
bool FceummFdsBridge_IsDiskInserted(void);
/* AURORA_FCEUMM_FDS_V12_3B_BRIDGE_HOTPATH_FIX_20260827: rare drive-state synchronization. */
bool FceummFdsBridge_ConsumeDriveStateChange(unsigned *selectedSide,
                                              bool *inserted);
void FceummFdsBridge_GetDriveState(unsigned *selectedSide,
                                   bool *inserted,
                                   unsigned *swapFramesRemaining,
                                   unsigned *swapTargetSide);
bool FceummFdsBridge_SetDriveState(unsigned selectedSide,
                                   bool inserted,
                                   unsigned swapFramesRemaining,
                                   unsigned swapTargetSide);
unsigned FceummFdsBridge_GetSampleRate(void);

#endif

