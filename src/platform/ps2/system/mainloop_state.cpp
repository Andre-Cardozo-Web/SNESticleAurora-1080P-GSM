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
    if (!MassStorageIsEnabled()) return FALSE;
    return stat("mass0:/", &Status) == 0 ? TRUE : FALSE;
}
void MainLoopSramSetDevice(MainLoopSramDeviceE eDevice) {
    if (eDevice < MAINLOOP_SRAMDEVICE_AUTO || eDevice >= MAINLOOP_SRAMDEVICE_NUM) eDevice = MAINLOOP_SRAMDEVICE_AUTO;
    _MainLoop_SramDevice = eDevice;
}
void MainLoopSramCycleDevice() {
    _MainLoop_SramDevice = (MainLoopSramDeviceE)(_MainLoop_SramDevice + 1);
    if (_MainLoop_SramDevice >= MAINLOOP_SRAMDEVICE_NUM) _MainLoop_SramDevice = MAINLOOP_SRAMDEVICE_AUTO;
}
MainLoopSramDeviceE MainLoopSramGetDevice() { return _MainLoop_SramDevice; }
const Char *MainLoopSramGetDeviceName() {
    switch (_MainLoop_SramDevice) {
        case MAINLOOP_SRAMDEVICE_USB:     return (const Char *)"USB only";
        case MAINLOOP_SRAMDEVICE_MEMCARD: return (const Char *)"Memory Card";
        default:                          return (const Char *)"USB -> MC fallback";
    }
}
const Char *MainLoopSramGetBrowseRoot() {
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_USB) return _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_USB);
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_MEMCARD) return _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_MEMCARD);
    return _MainLoopSramUsbReady() ? _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_USB) : _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_MEMCARD);
}
Bool MainLoopSramNeedsMemoryCardPreflight() {
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_USB) return FALSE;
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_MEMCARD) return TRUE;
    return _MainLoopSramUsbReady() ? FALSE : TRUE;
}
static Bool _MainLoopSramEnsureOneDir(const Char *pPath) {
    struct stat Status;
    if (mkdir((const char *)pPath, 0777) == 0) return TRUE;
    return stat((const char *)pPath, &Status) == 0 && S_ISDIR(Status.st_mode) ? TRUE : FALSE;
}
static Bool _MainLoopSystemCopyDirectory(Char *pOut, Int32 nOutBytes, const Char *pDirectory) {
    if (!pOut || nOutBytes <= 0 || !pDirectory || !*pDirectory) return FALSE;
    int nChars = snprintf((char *)pOut, (size_t)nOutBytes, "%s", (const char *)pDirectory);
    return (nChars < 0 || nChars >= nOutBytes) ? FALSE : TRUE;
}
static Bool _MainLoopSystemTryWritableRoot(Char *pOut, Int32 nOutBytes, const Char *pRoot) {
    Char Directory[512];
    if (!pRoot || !*pRoot || !_MainLoopSramEnsureOneDir(pRoot)) return FALSE;
    if (snprintf((char *)Directory, sizeof(Directory), "%s/SYSTEM", (const char *)pRoot) >= (int)sizeof(Directory)) return FALSE;
    if (!_MainLoopSramEnsureOneDir(Directory)) return FALSE;
    return _MainLoopSystemCopyDirectory(pOut, nOutBytes, Directory);
}
static Bool _MainLoopSystemFileAtRoot(Char *pOut, Int32 nOutBytes, const Char *pRoot, const Char *pFileName) {
    struct stat Status; Char Directory[512]; Char FilePath[1024];
    if (!pOut || nOutBytes <= 0 || !pRoot || !*pRoot || !pFileName || !*pFileName) return FALSE;
    if (snprintf((char *)Directory, sizeof(Directory), "%s/SYSTEM", (const char *)pRoot) >= (int)sizeof(Directory)) return FALSE;
    if (snprintf((char *)FilePath, sizeof(FilePath), "%s/%s", (char *)Directory, (const char *)pFileName) >= (int)sizeof(FilePath)) return FALSE;
    if (stat((char *)FilePath, &Status) != 0 || S_ISDIR(Status.st_mode)) return FALSE;
    return _MainLoopSystemCopyDirectory(pOut, nOutBytes, Directory);
}
Bool MainLoopFindSystemFileDirectory(Char *pOut, Int32 nOutBytes, const Char *pFileName) {
    static const Char *pPreferredRoots[] = { (const Char *)"mass0:/SNESticle", (const Char *)"mass1:/SNESticle", (const Char *)"mass:/SNESticle", NULL };
    static const Char *pFallbackRoots[] = { (const Char *)"mc0:/SNESticle", (const Char *)"mc1:/SNESticle", (const Char *)"mmce0:/SNESticle", (const Char *)"mmce1:/SNESticle", NULL };
    const Char *pActiveRoot; Int32 i;
    if (!pOut || nOutBytes <= 0 || !pFileName || !*pFileName) return FALSE;
    pOut[0] = '\0';
    for (i = 0; pPreferredRoots[i]; ++i) { if (_MainLoopSystemFileAtRoot(pOut, nOutBytes, pPreferredRoots[i], pFileName)) return TRUE; }
    pActiveRoot = MainLoopSramGetBrowseRoot();
    if (pActiveRoot && *pActiveRoot && _MainLoopSystemFileAtRoot(pOut, nOutBytes, pActiveRoot, pFileName)) return TRUE;
    for (i = 0; pFallbackRoots[i]; ++i) { if (_MainLoopSystemFileAtRoot(pOut, nOutBytes, pFallbackRoots[i], pFileName)) return TRUE; }
    return FALSE;
}
Bool MainLoopEnsureSystemDirectory(Char *pOut, Int32 nOutBytes) {
    static const Char *pPreferredRoots[] = { (const Char *)"mass0:/SNESticle", (const Char *)"mass1:/SNESticle", (const Char *)"mass:/SNESticle", NULL };
    const Char *pRoot; Bool bMemCard; Char Directory[512]; Int32 i;
    if (!pOut || nOutBytes <= 0) return FALSE;
    pOut[0] = '\0';
    for (i = 0; pPreferredRoots[i]; ++i) { if (_MainLoopSystemTryWritableRoot(pOut, nOutBytes, pPreferredRoots[i])) return TRUE; }
    pRoot = MainLoopSramGetBrowseRoot(); bMemCard = MainLoopSramNeedsMemoryCardPreflight();
    if (!pRoot || !*pRoot) return FALSE;
    if (!bMemCard && !_MainLoopSramEnsureOneDir(pRoot)) return FALSE;
    if (snprintf((char *)Directory, sizeof(Directory), "%s/SYSTEM", (const char *)pRoot) >= (int)sizeof(Directory)) return FALSE;
    if (!_MainLoopSramEnsureOneDir(Directory)) {
        if (!bMemCard) return FALSE;
        if (MemCardCreateSave((char *)pRoot, (char *)_MainLoop_SaveTitle, TRUE) < 0) return FALSE;
        if (!_MainLoopSramEnsureOneDir(Directory)) return FALSE;
    }
    return _MainLoopSystemCopyDirectory(pOut, nOutBytes, Directory);
}
Bool MainLoopEnsureSwcDirectory(Char *pOut, Int32 nOutBytes) {
    Char SystemDirectory[512]; Char Root[512]; size_t n;
    if (!pOut || nOutBytes <= 0) return FALSE;
    pOut[0] = 0;
    if (!MainLoopEnsureSystemDirectory(SystemDirectory, (Int32)sizeof(SystemDirectory))) return FALSE;
    n = strlen((char *)SystemDirectory);
    if (n < 7 || strcmp((char *)SystemDirectory + n - 7, "/SYSTEM") != 0 || n - 7 >= sizeof(Root)) return FALSE;
    memcpy(Root, SystemDirectory, n - 7); Root[n - 7] = 0;
    if (snprintf((char *)pOut, (size_t)nOutBytes, "%s/DSK", (char *)Root) >= nOutBytes) return FALSE;
    return _MainLoopSramEnsureOneDir(pOut);
}
static void _MainLoopSramBuildPath(Char *pPath, Int32 nPathBytes, const Char *pRoot, Bool bLegacyRoot) {
    Char Directory[512]; Char SaveName[256];
    const Char *pExtension = _pSystem->GetString(Emu::System::StringE::STRING_SRAMEXT);
    if (bLegacyRoot) snprintf((char *)Directory, sizeof(Directory), "%s", (const char *)pRoot);
    else snprintf((char *)Directory, sizeof(Directory), "%s/%s", (const char *)pRoot, (const char *)_MainLoopSramGetSystemDirectoryName());
    PathTruncFileName(SaveName, (Char *)_RomName, PathGetMaxFileNameLength((const char *)Directory) - ((Int32)strlen((const char *)pExtension) + 1));
    snprintf((char *)pPath, nPathBytes, "%s/%s.%s", (char *)Directory, (char *)SaveName, (const char *)pExtension);
}
static void _MainLoopSramBuildCopiedMcPath(Char *pPath, Int32 nPathBytes, const Char *pRoot, Bool bLegacyRoot) {
    Char Directory[512]; Char SaveName[256];
    const Char *pExtension = _pSystem->GetString(Emu::System::StringE::STRING_SRAMEXT);
    if (bLegacyRoot) snprintf((char *)Directory, sizeof(Directory), "%s", (const char *)pRoot);
    else snprintf((char *)Directory, sizeof(Directory), "%s/%s", (const char *)pRoot, (const char *)_MainLoopSramGetSystemDirectoryName());
    PathTruncFileName(SaveName, (Char *)_RomName, 32 - ((Int32)strlen((const char *)pExtension) + 1));
    snprintf((char *)pPath, nPathBytes, "%s/%s.%s", (char *)Directory, (char *)SaveName, (const char *)pExtension);
}
static Bool _MainLoopSramEnsureSystemDirectory(const Char *pRoot, Bool bMemCard) {
    Char Directory[512];
    if (!bMemCard && !_MainLoopSramEnsureOneDir(pRoot)) return FALSE;
    snprintf((char *)Directory, sizeof(Directory), "%s/%s", (const char *)pRoot, (const char *)_MainLoopSramGetSystemDirectoryName());
    if (_MainLoopSramEnsureOneDir(Directory)) return TRUE;
    if (bMemCard && MemCardCreateSave((char *)pRoot, (char *)_MainLoop_SaveTitle, TRUE) < 0) return FALSE;
    return _MainLoopSramEnsureOneDir(Directory);
}
static Bool _MainLoopSramReadFile(const Char *pPath, Uint8 *pData, Uint32 nBytes) {
    FILE *pFile; Uint8 *pTemp; size_t nRead; struct stat Status;
    if (stat((const char *)pPath, &Status) != 0 || (Uint32)Status.st_size != nBytes) return FALSE;
    if (!(pTemp = (Uint8 *)malloc(nBytes))) return FALSE;
    if (!(pFile = fopen((const char *)pPath, "rb"))) { free(pTemp); return FALSE; }
    nRead = fread(pTemp, 1, nBytes, pFile); fclose(pFile);
    if (nRead == nBytes) memcpy(pData, pTemp, nBytes);
    free(pTemp); return nRead == nBytes ? TRUE : FALSE;
}
static Bool _MainLoopSramWriteFile(const Char *pPath, Uint8 *pData, Uint32 nBytes) {
    FILE *pFile = fopen((const char *)pPath, "wb"); size_t nWritten; Bool bOK;
    if (!pFile) return FALSE;
    nWritten = fwrite(pData, 1, nBytes, pFile); bOK = fflush(pFile) == 0 ? TRUE : FALSE;
    if (fclose(pFile) != 0) bOK = FALSE;
    return nWritten == nBytes && bOK ? TRUE : FALSE;
}
static Char s_SwcCartSRAMName[512] = {0};
static Bool s_SwcCartSRAMMigrationPending = FALSE;
static Bool _MainLoopSwcCartSramBuildPath(Char *pPath, Int32 nPathBytes, const Char *pRoot, Bool bLegacyRoot, Bool bCopiedMcName) {
    Char Directory[512]; Char SaveName[512]; const Char *pExtension; Int32 nBaseMax;
    if (!pPath || nPathBytes <= 0 || !pRoot || !s_SwcCartSRAMName[0] || !_pSnes) return FALSE;
    if (!(pExtension = _pSnes->GetString(Emu::System::StringE::STRING_SRAMEXT)) || !*pExtension) return FALSE;
    if (bLegacyRoot) snprintf((char *)Directory, sizeof(Directory), "%s", (const char *)pRoot);
    else snprintf((char *)Directory, sizeof(Directory), "%s/%s", (const char *)pRoot, (const char *)_MainLoopSramGetSystemDirectoryName());
    nBaseMax = bCopiedMcName ? (32 - ((Int32)strlen((const char *)pExtension) + 1)) : (PathGetMaxFileNameLength((const char *)Directory) - ((Int32)strlen((const char *)pExtension) + 1));
    if (nBaseMax <= 0) return FALSE;
    PathTruncFileName(SaveName, s_SwcCartSRAMName, nBaseMax);
    return snprintf((char *)pPath, (size_t)nPathBytes, "%s/%s.%s", (char *)Directory, (char *)SaveName, (const char *)pExtension) < nPathBytes ? TRUE : FALSE;
}
static Bool _MainLoopStateGetSwcBaseName(Char *pOut, Int32 nOutBytes) {
    const Char *pPath, *pName, *pExt; size_t n, i;
    if (!pOut || nOutBytes <= 1 || !_pSnes || !(pPath = _pSnes->GetSuperWildCardDiskPath()) || !*pPath) return FALSE;
    pName = pPath;
    for (const Char *p = pPath; *p; ++p) { if (*p == '/' || *p == '\\') pName = p + 1; }
    pExt = strrchr((const char *)pName, '.'); n = pExt ? (size_t)(pExt - pName) : strlen((const char *)pName);
    if (!n || n >= (size_t)nOutBytes) return FALSE;
    memcpy(pOut, pName, n); pOut[n] = 0; i = n;
    while (i > 0 && pOut[i - 1] >= '0' && pOut[i - 1] <= '9') --i;
    if (i > 0 && i < n && pOut[i - 1] == '_') pOut[i - 1] = 0;
    return pOut[0] ? TRUE : FALSE;
}
static Bool _MainLoopStateIsSwc() { return (_pSystem == _pSnes && _pSnes && _pSnes->IsSuperWildCard()) ? TRUE : FALSE; }
static Uint32 _uInputFrame = 0;
static Uint8  _uInputChecksum[16] = {0};
void _MainLoopResetInputChecksums() { _uInputFrame = 0; memset(_uInputChecksum, 0, sizeof(_uInputChecksum)); }
Bool _MainLoopLoadState() { _bStateSaved = FALSE; _MainLoopResetInputChecksums(); return TRUE; }
Bool _MainLoopSaveState() { _bStateSaved = TRUE; return TRUE; }
void MainLoopStateOnRomChanged() { _MainLoop_StateRomCRCValid = FALSE; _MainLoop_StateRomCRC = 0; _bStateSaved = FALSE; }
void MainLoopStatePrimeRomIdentityCRC(Uint32 uCRC) { _MainLoop_StateRomCRC = uCRC; _MainLoop_StateRomCRCValid = TRUE; }
Int32 MainLoopStateGetSlot() { return _MainLoop_StateSlot; }
MainLoopStateDeviceE MainLoopStateGetDevice() { return _MainLoop_StateDevice; }
const Char *MainLoopStateGetDeviceName() { return (const Char *)"Auto"; }
const Char *MainLoopStateGetLastMessage() { return (const Char *)"Operation completed."; }
Int32 MainLoopStateGetUnformattedCard() { return -1; }
Bool MainLoopStateHasDeviceChoice() { return TRUE; }
void MainLoopStateForgetDeviceChoice() {}
void MainLoopStateSetDevice(MainLoopStateDeviceE eDevice) { _MainLoop_StateDevice = eDevice; }
Bool MainLoopStateDeviceAvailable(MainLoopStateDeviceE eDevice) { return TRUE; }
void MainLoopStateCycleSlot() {}
void MainLoopStateCycleDevice() {}
void MainLoopStateSettingsLoad() {}
Bool MainLoopStateSettingsSave() { return TRUE; }
const Char *MainLoopStateGetAvailability() { return (const Char *)"Ready."; }#if MAINLOOP_HISTORYUint32 _History[16384 * 2]; Uint32 _nHistory = 0;void _MainLoopSaveHistory() { FileWriteMem("host:game.hst", _History, _nHistory * sizeof(Uint32)); printf("History written\n"); }#endif
