#pragma once

#include "types.h"

/* AURORA_CONTROLLER_OPTIONS_V2
 * User-facing Max is the historical one-frame ON / one-frame OFF cadence. */
enum MainLoopTurboSpeedE
{
	MAINLOOP_TURBO_SPEED_NORMAL = 0,
	MAINLOOP_TURBO_SPEED_HALF,
	MAINLOOP_TURBO_SPEED_QUARTER,
	MAINLOOP_TURBO_SPEED_NUM
};

void MainLoopTurboSetSpeed(MainLoopTurboSpeedE eSpeed);
MainLoopTurboSpeedE MainLoopTurboGetSpeed(void);
void MainLoopTurboCycleSpeedDir(Int32 dir);
const char *MainLoopTurboGetSpeedName(void);

Uint16 _MainLoopInput(Uint32 pad);
void _MainLoopInputProcess(Uint32 buttons);
void _MainLoopInputSuppressUntilRelease();
void _MainLoopQuickStateExecuteConfirmed(Bool bSave);
