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
void FceummFdsBridge_UnloadGame(void);
void FceummFdsBridge_Reset(void);
void FceummFdsBridge_SoftReset(void);
void FceummFdsBridge_SetSkipVideo(bool skip);
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
