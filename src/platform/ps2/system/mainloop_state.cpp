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

extern "C" {
#include "netplay_ee.h"
}

#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "nes/quicknes/quicknes_bridge.h" /* AURORA_QN_TURBOFILE_SAVE_V2_20260828 */
#include "sega/picodrive/picodrive_bridge.h" /* AURORA_CD_STATE_V1_SAFE_20260903 */
#include "pce/beetle/pce_bridge.h" /* AURORA_CD_STATE_V1_SAFE_20260903 */

/* AURORA_PCE_EXPERIMENTAL_V1 */

/* AURORA_RUNTIME_LEAN_V1_MCSAVE_20260824
 * The old MCSAVE.IRX path is unreachable in Aurora: it is never loaded or
 * initialized. mainloop_state therefore contains only the supported
 * synchronous newlib/iomanX storage path. */

#if MAINLOOP_HISTORY
extern Uint32 _nHistory;
#endif

static Uint32 _PathCalcHash(const char *pStr)
{
    Uint32 hash = 0;

    while (*pStr)
    {
        hash *= 33;
        hash += *pStr;
        pStr++;
    }

    return hash;
}

void PathTruncFileName(Char *pOut, Char *pStr, Int32 nMaxChars)
{
    Uint32 hash;
    Int32 nOriginalMax = nMaxChars;

    /* AURORA_RUNTIME_SAFE_PATH_TRUNC_V1_4_2 */
    if (!pOut)
        return;
    if (!pStr || nMaxChars <= 0)
    {
        *pOut = '\0';
        return;
    }

    hash = _PathCalcHash(pStr);

    // copy string up to maxchars length
    while (*pStr && nMaxChars > 0)
    {
        *pOut++ = *pStr++;
        nMaxChars--;
    }

    // terminate
    *pOut = 0;

    /* pOut-3 is valid only after at least three characters were copied. */
    if (nMaxChars <= 0 && nOriginalMax >= 3)
    {
        sprintf(pOut - 3, "%03u", (unsigned int)(hash % 1000));
    }
}

int PathGetMaxFileNameLength(const char *pPath)
{
    if ((pPath[0] == 'm' && pPath[1] == 'c') ||
        (pPath[0] == 'm' && pPath[1] == 'm' &&
         pPath[2] == 'c' && pPath[3] == 'e'))
    {
        return 32;
    }

    return 256;
}

/* AURORA_FINAL_V1_7_D88_DUAL_SRAM_PERSIST_20260901 */
/* AURORA_SRAM_STORAGE_V1
 * _SramPath stays the settings/legacy root. SRAM data has its own policy. */
static MainLoopSramDeviceE _MainLoop_SramDevice = MAINLOOP_SRAMDEVICE_AUTO;

static const Char *_MainLoopSramGetSystemDirectoryName()
{
    if (_pSystem == _pNes)  return "NES";
    if (_pSystem == _pSega) return "SEGA";
    if (_pSystem == _pPce)  return "PCE";
    return "SNES";
}

static const Char *_MainLoopSramRoot(MainLoopSramDeviceE eDevice)
{
    return eDevice == MAINLOOP_SRAMDEVICE_USB ? "mass0:/SNESticle" : _SramPath;
}

static Bool _MainLoopSramUsbReady()
{
    struct stat Status;
    if (!MassStorageIsEnabled())
        return FALSE;
    return stat("mass0:/", &Status) == 0 ? TRUE : FALSE;
}

void MainLoopSramSetDevice(MainLoopSramDeviceE eDevice)
{
    if (eDevice < MAINLOOP_SRAMDEVICE_AUTO || eDevice >= MAINLOOP_SRAMDEVICE_NUM)
        eDevice = MAINLOOP_SRAMDEVICE_AUTO;
    _MainLoop_SramDevice = eDevice;
}

void MainLoopSramCycleDevice()
{
    _MainLoop_SramDevice = (MainLoopSramDeviceE)(_MainLoop_SramDevice + 1);
    if (_MainLoop_SramDevice >= MAINLOOP_SRAMDEVICE_NUM)
        _MainLoop_SramDevice = MAINLOOP_SRAMDEVICE_AUTO;
}

MainLoopSramDeviceE MainLoopSramGetDevice()
{
    return _MainLoop_SramDevice;
}

const Char *MainLoopSramGetDeviceName()
{
    switch (_MainLoop_SramDevice)
    {
        case MAINLOOP_SRAMDEVICE_USB:     return "USB only";
        case MAINLOOP_SRAMDEVICE_MEMCARD: return "Memory Card";
        default:                          return "USB -> MC fallback";
    }
}

const Char *MainLoopSramGetBrowseRoot()
{
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_USB)
        return _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_USB);
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_MEMCARD)
        return _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_MEMCARD);
    return _MainLoopSramUsbReady()
        ? _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_USB)
        : _MainLoopSramRoot(MAINLOOP_SRAMDEVICE_MEMCARD);
}

Bool MainLoopSramNeedsMemoryCardPreflight()
{
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_USB)
        return FALSE;
    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_MEMCARD)
        return TRUE;
    return _MainLoopSramUsbReady() ? FALSE : TRUE;
}

static Bool _MainLoopSramEnsureOneDir(const Char *pPath)
{
    struct stat Status;

    /* AURORA_RUNTIME_SAFE_SRAM_DIR_V1_4_1
     * EEXIST alone is insufficient: the path could be a regular file. */
    if (mkdir(pPath, 0777) == 0)
        return TRUE;
    return stat(pPath, &Status) == 0 && S_ISDIR(Status.st_mode)
        ? TRUE : FALSE;
}

static Bool _MainLoopSystemCopyDirectory(Char *pOut, Int32 nOutBytes,
                                         const Char *pDirectory)
{
    int nChars;

    if (!pOut || nOutBytes <= 0 || !pDirectory || !*pDirectory)
        return FALSE;

    nChars = snprintf(pOut, (size_t)nOutBytes, "%s", pDirectory);
    if (nChars < 0 || nChars >= nOutBytes)
    {
        pOut[0] = '\0';
        return FALSE;
    }
    return TRUE;
}

static Bool _MainLoopSystemTryWritableRoot(Char *pOut, Int32 nOutBytes,
                                           const Char *pRoot)
{
    Char Directory[512];
    int nChars;

    if (!pRoot || !*pRoot)
        return FALSE;

    if (!_MainLoopSramEnsureOneDir(pRoot))
        return FALSE;

    nChars = snprintf(Directory, sizeof(Directory), "%s/SYSTEM", pRoot);
    if (nChars < 0 || nChars >= (int)sizeof(Directory))
        return FALSE;

    if (!_MainLoopSramEnsureOneDir(Directory))
        return FALSE;

    return _MainLoopSystemCopyDirectory(
        pOut, nOutBytes, Directory);
}
static Bool _MainLoopSystemFileAtRoot(Char *pOut, Int32 nOutBytes,
                                      const Char *pRoot,
                                      const Char *pFileName)
{
    struct stat Status;
    Char Directory[512];
    Char FilePath[1024];
    int nChars;

    if (!pOut || nOutBytes <= 0 ||
        !pRoot || !*pRoot || !pFileName || !*pFileName)
        return FALSE;

    nChars = snprintf(Directory, sizeof(Directory), "%s/SYSTEM", pRoot);
    if (nChars < 0 || nChars >= (int)sizeof(Directory))
        return FALSE;

    nChars = snprintf(FilePath, sizeof(FilePath),
                      "%s/%s", Directory, pFileName);
    if (nChars < 0 || nChars >= (int)sizeof(FilePath))
        return FALSE;

    if (stat(FilePath, &Status) != 0 || S_ISDIR(Status.st_mode))
        return FALSE;

    return _MainLoopSystemCopyDirectory(
        pOut, nOutBytes, Directory);
}

Bool MainLoopFindSystemFileDirectory(Char *pOut, Int32 nOutBytes,
                                     const Char *pFileName)
{
    static const Char *pPreferredRoots[] =
    {
        "mass0:/SNESticle",
        "mass1:/SNESticle",
        "mass:/SNESticle",
        NULL
    };
    static const Char *pFallbackRoots[] =
    {
        "mc0:/SNESticle",
        "mc1:/SNESticle",
        "mmce0:/SNESticle",
        "mmce1:/SNESticle",
        NULL
    };
    const Char *pActiveRoot;
    Int32 i;

    if (!pOut || nOutBytes <= 0 || !pFileName || !*pFileName)
        return FALSE;
    pOut[0] = '\0';

    for (i = 0; pPreferredRoots[i]; ++i)
    {
        if (_MainLoopSystemFileAtRoot(
                pOut, nOutBytes, pPreferredRoots[i], pFileName))
        {
            printf("[SYSTEM] found %s in %s\n", pFileName, pOut);
            return TRUE;
        }
    }

    pActiveRoot = MainLoopSramGetBrowseRoot();
    if (pActiveRoot && *pActiveRoot &&
        _MainLoopSystemFileAtRoot(
            pOut, nOutBytes, pActiveRoot, pFileName))
    {
        printf("[SYSTEM] found %s in %s\n", pFileName, pOut);
        return TRUE;
    }

    for (i = 0; pFallbackRoots[i]; ++i)
    {
        if (_MainLoopSystemFileAtRoot(
                pOut, nOutBytes, pFallbackRoots[i], pFileName))
        {
            printf("[SYSTEM] found %s in %s\n", pFileName, pOut);
            return TRUE;
        }
    }

    pOut[0] = '\0';
    return FALSE;
}

Bool MainLoopEnsureSystemDirectory(Char *pOut, Int32 nOutBytes)
{
    static const Char *pPreferredRoots[] =
    {
        "mass0:/SNESticle",
        "mass1:/SNESticle",
        "mass:/SNESticle",
        NULL
    };
    const Char *pRoot;
    Bool bMemCard;
    Char Directory[512];
    int nChars;
    Int32 i;

    if (!pOut || nOutBytes <= 0)
        return FALSE;
    pOut[0] = '\0';

    for (i = 0; pPreferredRoots[i]; ++i)
    {
        if (_MainLoopSystemTryWritableRoot(
                pOut, nOutBytes, pPreferredRoots[i]))
        {
            printf("[SYSTEM] directory: %s\n", pOut);
            return TRUE;
        }
    }

    pRoot = MainLoopSramGetBrowseRoot();
    bMemCard = MainLoopSramNeedsMemoryCardPreflight();
    if (!pRoot || !*pRoot)
        return FALSE;

    if (!bMemCard && !_MainLoopSramEnsureOneDir(pRoot))
        return FALSE;

    nChars = snprintf(Directory, sizeof(Directory), "%s/SYSTEM", pRoot);
    if (nChars < 0 || nChars >= (int)sizeof(Directory))
        return FALSE;

    if (!_MainLoopSramEnsureOneDir(Directory))
    {
        if (!bMemCard)
            return FALSE;

        {
            int Result = MemCardCreateSave(
                (char *)pRoot, _MainLoop_SaveTitle, TRUE);
            if (Result < 0)
            {
                printf("[SYSTEM] MemCardCreateSave('%s') failed: %d\n",
                       pRoot, Result);
                return FALSE;
            }
        }

        if (!_MainLoopSramEnsureOneDir(Directory))
            return FALSE;
    }

    if (!_MainLoopSystemCopyDirectory(
            pOut, nOutBytes, Directory))
        return FALSE;

    printf("[SYSTEM] fallback directory: %s\n", pOut);
    return TRUE;
}

Bool MainLoopEnsureSwcDirectory(Char *pOut, Int32 nOutBytes)
{
    Char SystemDirectory[512];
    Char Root[512];
    size_t n;
    int nChars;

    if (!pOut || nOutBytes <= 0)
        return FALSE;
    pOut[0] = 0;

    if (!MainLoopEnsureSystemDirectory(
            SystemDirectory, (Int32)sizeof(SystemDirectory)))
        return FALSE;

    n = strlen(SystemDirectory);
    if (n < 7 || strcmp(SystemDirectory + n - 7, "/SYSTEM") != 0 ||
        n - 7 >= sizeof(Root))
        return FALSE;

    memcpy(Root, SystemDirectory, n - 7);
    Root[n - 7] = 0;

    nChars = snprintf(pOut, (size_t)nOutBytes, "%s/DSK", Root);
    if (nChars < 0 || nChars >= nOutBytes)
    {
        pOut[0] = 0;
        return FALSE;
    }

    if (!_MainLoopSramEnsureOneDir(pOut))
    {
        pOut[0] = 0;
        return FALSE;
    }

    printf("[DSK] directory: %s\n", pOut);
    return TRUE;
}

static void _MainLoopSramBuildPath(Char *pPath, Int32 nPathBytes,
                                   const Char *pRoot, Bool bLegacyRoot)
{
    Char Directory[512];
    Char SaveName[256];
    const Char *pExtension =
        _pSystem->GetString(Emu::System::StringE::STRING_SRAMEXT);
    Int32 nSuffixBytes = (Int32)strlen(pExtension) + 1;

    if (bLegacyRoot)
        snprintf(Directory, sizeof(Directory), "%s", pRoot);
    else
        snprintf(Directory, sizeof(Directory), "%s/%s", pRoot,
                 _MainLoopSramGetSystemDirectoryName());

    PathTruncFileName(SaveName, _RomName,
        PathGetMaxFileNameLength(Directory) - nSuffixBytes);
    snprintf(pPath, nPathBytes, "%s/%s.%s", Directory, SaveName, pExtension);
}

static void _MainLoopSramBuildCopiedMcPath(Char *pPath, Int32 nPathBytes,
                                           const Char *pRoot,
                                           Bool bLegacyRoot)
{
    Char Directory[512];
    Char SaveName[256];
    const Char *pExtension =
        _pSystem->GetString(Emu::System::StringE::STRING_SRAMEXT);
    const Int32 nMcMaxFileName = 32;
    Int32 nSuffixBytes = (Int32)strlen(pExtension) + 1;
    Int32 nBaseMax = nMcMaxFileName - nSuffixBytes;

    if (bLegacyRoot)
        snprintf(Directory, sizeof(Directory), "%s", pRoot);
    else
        snprintf(Directory, sizeof(Directory), "%s/%s", pRoot,
                 _MainLoopSramGetSystemDirectoryName());

    PathTruncFileName(SaveName, _RomName, nBaseMax);
    snprintf(pPath, nPathBytes, "%s/%s.%s",
             Directory, SaveName, pExtension);
}

static Bool _MainLoopSramEnsureSystemDirectory(const Char *pRoot, Bool bMemCard)
{
    Char Directory[512];

    if (!bMemCard && !_MainLoopSramEnsureOneDir(pRoot))
        return FALSE;

    snprintf(Directory, sizeof(Directory), "%s/%s", pRoot,
             _MainLoopSramGetSystemDirectoryName());
    if (_MainLoopSramEnsureOneDir(Directory))
        return TRUE;

    if (bMemCard)
    {
        int Result = MemCardCreateSave((char *)pRoot, _MainLoop_SaveTitle, TRUE);
        if (Result < 0)
        {
            printf("[SRAM] MemCardCreateSave('%s') failed: %d\n", pRoot, Result);
            return FALSE;
        }
    }
    return _MainLoopSramEnsureOneDir(Directory);
}
static Bool _MainLoopSramReadFile(const Char *pPath, Uint8 *pData, Uint32 nBytes)
{
    FILE *pFile;
    Uint8 *pTemp;
    size_t nRead;
    struct stat Status;

    if (stat(pPath, &Status) != 0 || (Uint32)Status.st_size != nBytes)
        return FALSE;

    pTemp = (Uint8 *)malloc(nBytes);
    if (!pTemp)
        return FALSE;

    pFile = fopen(pPath, "rb");
    if (!pFile)
    {
        free(pTemp);
        return FALSE;
    }
    nRead = fread(pTemp, 1, nBytes, pFile);
    fclose(pFile);
    if (nRead == nBytes)
        memcpy(pData, pTemp, nBytes);
    free(pTemp);
    return nRead == nBytes ? TRUE : FALSE;
}

static Bool _MainLoopSramWriteFile(const Char *pPath, Uint8 *pData, Uint32 nBytes)
{
    FILE *pFile = fopen(pPath, "wb");
    size_t nWritten;
    Bool bOK;
    if (!pFile)
        return FALSE;
    nWritten = fwrite(pData, 1, nBytes, pFile);
    bOK = fflush(pFile) == 0 ? TRUE : FALSE;
    if (fclose(pFile) != 0)
        bOK = FALSE;
    return nWritten == nBytes && bOK ? TRUE : FALSE;
}

static Char s_SwcCartSRAMName[512] = {0};
static Bool s_SwcCartSRAMMigrationPending = FALSE;

static Bool _MainLoopSwcCartSramBuildPath(
    Char *pPath, Int32 nPathBytes,
    const Char *pRoot, Bool bLegacyRoot,
    Bool bCopiedMcName)
{
    Char Directory[512];
    Char SaveName[512];
    const Char *pExtension;
    Int32 nSuffixBytes;
    Int32 nBaseMax;
    int n;

    if (!pPath || nPathBytes <= 0 || !pRoot ||
        !s_SwcCartSRAMName[0] || !_pSnes)
        return FALSE;

    pExtension =
        _pSnes->GetString(Emu::System::StringE::STRING_SRAMEXT);
    if (!pExtension || !*pExtension)
        return FALSE;

    if (bLegacyRoot)
        snprintf(Directory, sizeof(Directory), "%s", pRoot);
    else
        snprintf(Directory, sizeof(Directory), "%s/%s", pRoot,
                 _MainLoopSramGetSystemDirectoryName());

    nSuffixBytes = (Int32)strlen(pExtension) + 1;
    nBaseMax = bCopiedMcName
        ? (32 - nSuffixBytes)
        : (PathGetMaxFileNameLength(Directory) - nSuffixBytes);

    if (nBaseMax <= 0)
        return FALSE;

    PathTruncFileName(SaveName, s_SwcCartSRAMName, nBaseMax);
    n = snprintf(
        pPath, (size_t)nPathBytes,
        "%s/%s.%s", Directory, SaveName, pExtension);
    return n >= 0 && n < nPathBytes ? TRUE : FALSE;
}

/* --- Adicionando as variaveis necessarias para evitar o erro do escopo --- */
static Uint32 _uInputFrame = 0;
static Uint8 _uInputChecksum[512];

void _MainLoopResetInputChecksums()
/* ---- Definição externa das variáveis de sincronismo de entrada ---- */
extern Uint32 _uInputFrame;
extern Uint8  _uInputChecksum[];

void _MainLoopResetInputChecksums()
/* ---- Definição externa das variáveis de sincronismo de entrada ---- */
extern Uint32 _uInputFrame;
extern Uint8  _uInputChecksum[];

void _MainLoopResetInputChecksums()
/* ---- Definição externa das variáveis de sincronismo de entrada ---- */
extern Uint32 _uInputFrame;
extern Uint8  _uInputChecksum[];

void _MainLoopResetInputChecksums()
/* ---- Definição externa das variáveis de sincronismo de entrada ---- */
extern Uint32 _uInputFrame;
extern Uint8  _uInputChecksum[];

void _MainLoopResetInputChecksums()
{
	_uInputFrame = 0;
	memset(_uInputChecksum, 0, 16); // Zera o buffer de checksum de forma segura
}

#if MAINLOOP_HISTORY
Uint32 _History[16384 * 2];
Uint32 _nHistory = 0;
#endif

#if MAINLOOP_HISTORY
void _MainLoopSaveHistory()
{
    FileWriteMem("host:game.hst", _History, _nHistory * sizeof(Uint32));
    printf("History written\n");
}
#endif
