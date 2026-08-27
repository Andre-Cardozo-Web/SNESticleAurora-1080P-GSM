#pragma once

#include "types.h"

/* AURORA_SAFE_FRAMESKIP_GG_ZOOM_V2_2
 * Safe Frameskip is host scheduling only; it is never part of emulated state. */
Int32 MainLoopSafeFrameskipGetLevel(void);
void MainLoopSafeFrameskipSetLevel(Int32 level);
Bool MainLoopSafeFrameskipGetEnabled(void);
void MainLoopSafeFrameskipSetEnabled(Bool enabled);
/* AURORA_SAFE_FRAMESKIP_PICODRIVE_AUTO_V1: PicoDrive-style Auto decision once per host tick. */
Bool MainLoopSafeFrameskipTake(Bool allowed);
Bool MainLoopSafeFrameskipConsumePresentationSkip(void);
