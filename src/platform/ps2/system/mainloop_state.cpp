#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include "types.h"
#include "console.h"
#include "file.h"
#include "prof.h"
#include "memcard.h"
#include "miniz.h"
#include "mainloop_debug.h"
#include "mainloop_bgm.h"
#include "embedded_irx.h"
#include "mainloop_iop.h"
extern "C" { #include "netplay_ee.h" }
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "nes/quicknes/quicknes_bridge.h"
#include "sega/picodrive/picodrive_bridge.h"
#include "pce/beetle/pce_bridge.h"

static MainLoopStateDeviceE _MainLoop_StateDevice = MAINLOOP_STATEDEVICE_AUTO;
static MainLoopSramDeviceE  _MainLoop_SramDevice  = MAINLOOP_SRAMDEVICE_AUTO;
static Int32 _MainLoop_StateSlot = 0;
Bool _bStateSaved = FALSE;
Bool _MainLoop_StateRomCRCValid = FALSE;
Uint32 _MainLoop_StateRomCRC = 0;
extern const char *_RomName;
extern const char *_SramPath;
extern const char *_MainLoop_SaveTitle;
#if MAINLOOP_HISTORY
extern Uint32 _nHistory;
#endif

static Uint32 _PathCalcHash(const char *pStr) {
    Uint32 hash = 0;
    while (*pStr) { hash *= 33; hash += *pStr++; }
    return hash;
}
void PathTruncFileName(Char *pOut, Char *pStr, Int32 nMaxChars) {
    Uint32 hash; Int32 nOriginalMax = nMaxChars;
    if (!pOut) return;
    if (!pStr || nMaxChars <= 0) { *pOut = '\0'; return; }
    hash = _PathCalcHash(pStr);
    while (*pStr && nMaxChars > 0) { *pOut++ = *pStr++; nMaxChars--; }
    *pOut = 0;
    if (nMaxChars <= 0 && nOriginalMax >= 3) { sprintf(pOut - 3, "%03u", (unsigned int)(hash % 1000)); }
}
int PathGetMaxFileNameLength(const char *pPath) {
    if ((pPath[0] == 'm' && pPath[1] == 'c') || (pPath[0] == 'm' && pPath[1] == 'm' && pPath[2] == 'c' && pPath[3] == 'e')) return 32;
    return 256;
}
static const Char *_MainLoopSramGetSystemDirectoryName() {
    if (_pSystem == _pNes)  return (const Char *)"NES";
    if (_pSystem == _pSega) return (const Char *)"SEGA";
    if (_pSystem == _pPce)  return (const Char *)"PCE";
    return (const Char *)"SNES";
}
static const Char *_MainLoopSramRoot(MainLoopSramDeviceE eDevice) {
    return eDevice == MAINLOOP_SRAMDEVICE_USB ? (const Char *)"mass0:/SNESticle" : (const Char *)_SramPath;
}
static Bool _MainLoopSramUsbReady() {
    struct stat Status;
    if (!MassStorageIsEnabled()) return FALSE;const Char *MainLoopStateGetAvailability() 
{ 
    return (const Char *)"Ready."; 
}

#if MAINLOOP_HISTORY
Uint32 _History[16384 * 2]; 
Uint32 _nHistory = 0;
void _MainLoopSaveHistory() 
{ 
    FileWriteMem("host:game.hst", _History, _nHistory * sizeof(Uint32)); 
    printf("History written\n"); 
}
#endif

