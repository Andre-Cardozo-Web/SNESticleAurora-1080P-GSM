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
int MCSave_Write(char *pPath, char *pData, int nBytes);
int MCSave_WriteSync(int block, int *pResult);
#include "netplay_ee.h"
}

#include "mainloop_shared.h"
#include "mainloop_state.h"

/* AURORA_PCE_EXPERIMENTAL_V1 */

/* The iaddis-era custom MCSAVE.IRX async memory-card writer has been
   retired -- see embedded_irx.cpp.  mainloop_iop.cpp no longer
   attempts to IOPLoadModule("MCSAVE.IRX") and never calls
   MCSave_Init(), so _MainLoop_bMCSaveReady always stays FALSE and
   the synchronous newlib-stdio-via-iomanX path below is always
   taken.  The flag itself is kept as a vestigial extern so the
   build doesn't have to touch every call site that still tests it;
   it is effectively dead code that branch predictors will fold out.
   Defined in mainloop_iop.cpp. */
extern Bool _MainLoop_bMCSaveReady;

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

/* AURORA_SRAM_MC_COPY_ALIAS_V1
 * A .srm created on a PS2 Memory Card has a filename ceiling of 32 chars.
 * PathTruncFileName therefore shortens long ROM names and replaces the last
 * three base-name chars with its stable decimal hash. A normal file copy to
 * USB preserves that short filename, while the canonical USB path uses the
 * full long filename. Build the exact MC spelling so copied saves remain
 * discoverable without relying on MC attributes or timestamps. */
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

/* Transactional read: a short/corrupt USB file cannot partially overwrite
 * live SRAM before AUTO falls back to MC. */
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

static Uint32 _CalcChecksum(Uint32 *pData, Uint32 nWords)
{
    Uint32 uSum = 0;

    while (nWords > 0)
    {
        uSum += pData[0];
        pData++;
        nWords--;
    }

    return uSum;
}

Bool _MainLoopHasSRAM()
{
    return _pSystem ? (_pSystem->GetSRAMBytes() > 0) : FALSE;
}

static Bool _MainLoopSaveSRAMTo(MainLoopSramDeviceE eDevice, Bool bSync)
{
    const Char *pRoot = _MainLoopSramRoot(eDevice);
    Bool bMemCard = eDevice == MAINLOOP_SRAMDEVICE_MEMCARD ? TRUE : FALSE;
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;
    Char Path[1024];
    Uint8 *pSRAM;

    if (nSramBytes <= 0)
        return FALSE;
    pSRAM = _pSystem->GetSRAMData();
    if (!pSRAM || !_MainLoopSramEnsureSystemDirectory(pRoot, bMemCard))
        return FALSE;

    _MainLoopSramBuildPath(Path, sizeof(Path), pRoot, FALSE);

    if (_pSystem == _pSnes && g_FakeSRAMSize)
    {
        struct stat Status;
        if (stat(Path, &Status) == 0 &&
            (Uint32)Status.st_size != (Uint32)nSramBytes)
        {
            printf("[SRAM] Force SRAM size mismatch: file=%ld expected=%d\n",
                   (long)Status.st_size, (int)nSramBytes);
            memset(pSRAM, 0, nSramBytes);
        }
    }

    ML_TRACE("SRAM save path: %s", Path);

    /* The retired MCSAVE path is MC-specific. Never pass mass0 to it. */
    if (bMemCard && _MainLoop_bMCSaveReady)
    {
        MCSave_WriteSync(TRUE, NULL);
        MCSave_Write((char *)Path, (char *)pSRAM, nSramBytes);
        if (bSync)
        {
            int Result = 0;
            MCSave_WriteSync(TRUE, &Result);
            if (Result)
                _MainLoop_SRAMUpdated = FALSE;
            return Result ? TRUE : FALSE;
        }
        return TRUE;
    }

    if (_MainLoopSramWriteFile(Path, pSRAM, (Uint32)nSramBytes))
    {
        _MainLoop_SRAMUpdated = FALSE;
        return TRUE;
    }
    return FALSE;
}

Bool _MainLoopSaveSRAM(Bool bSync)
{
    if (!_MainLoopHasSRAM())
        return FALSE;

    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_USB)
        return _MainLoopSramUsbReady()
            ? _MainLoopSaveSRAMTo(MAINLOOP_SRAMDEVICE_USB, bSync) : FALSE;

    if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_MEMCARD)
        return _MainLoopSaveSRAMTo(MAINLOOP_SRAMDEVICE_MEMCARD, bSync);

    if (_MainLoopSramUsbReady() &&
        _MainLoopSaveSRAMTo(MAINLOOP_SRAMDEVICE_USB, bSync))
        return TRUE;

    return _MainLoopSaveSRAMTo(MAINLOOP_SRAMDEVICE_MEMCARD, bSync);
}

static Bool _MainLoopLoadSRAMFrom(MainLoopSramDeviceE eDevice,
                                  Uint8 *pSRAM, Int32 nSramBytes,
                                  Bool *pbLegacy)
{
    const Char *pRoot = _MainLoopSramRoot(eDevice);
    Char Path[1024];
    Char McCopyPath[1024];
    *pbLegacy = FALSE;

    _MainLoopSramBuildPath(Path, sizeof(Path), pRoot, FALSE);
    if (_MainLoopSramReadFile(Path, pSRAM, (Uint32)nSramBytes))
    {
        ConPrint("SRAM loaded: %s\n", Path);
        return TRUE;
    }

    /* AURORA_SRAM_MC_COPY_ALIAS_V1
     * A literal copy from mc0:/mc1: to USB preserves the Memory Card's
     * shortened filename. Search that exact spelling as a USB alias. */
    if (eDevice == MAINLOOP_SRAMDEVICE_USB)
    {
        _MainLoopSramBuildCopiedMcPath(
            McCopyPath, sizeof(McCopyPath), pRoot, FALSE);

        if (strcmp(McCopyPath, Path) != 0 &&
            _MainLoopSramReadFile(
                McCopyPath, pSRAM, (Uint32)nSramBytes))
        {
            *pbLegacy = TRUE;
            ConPrint("SRAM loaded (MC-copy alias): %s\n", McCopyPath);
            return TRUE;
        }
    }

    if (_pSystem == _pSnes)
    {
        _MainLoopSramBuildPath(Path, sizeof(Path), pRoot, TRUE);
        if (_MainLoopSramReadFile(Path, pSRAM, (Uint32)nSramBytes))
        {
            *pbLegacy = TRUE;
            ConPrint("SRAM loaded (legacy): %s\n", Path);
            return TRUE;
        }

        if (eDevice == MAINLOOP_SRAMDEVICE_USB)
        {
            _MainLoopSramBuildCopiedMcPath(
                McCopyPath, sizeof(McCopyPath), pRoot, TRUE);

            if (strcmp(McCopyPath, Path) != 0 &&
                _MainLoopSramReadFile(
                    McCopyPath, pSRAM, (Uint32)nSramBytes))
            {
                *pbLegacy = TRUE;
                ConPrint(
                    "SRAM loaded (legacy MC-copy alias): %s\n",
                    McCopyPath
                );
                return TRUE;
            }
        }
    }

    return FALSE;
}

void _MainLoopLoadSRAM()
{
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;
    Uint8 *pSRAM = nSramBytes > 0 ? _pSystem->GetSRAMData() : NULL;
    Bool bLoaded = FALSE;
    Bool bLegacy = FALSE;
    Bool bMcFallback = FALSE;

    if (pSRAM && nSramBytes > 0)
    {
        if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_USB)
        {
            if (_MainLoopSramUsbReady())
                bLoaded = _MainLoopLoadSRAMFrom(
                    MAINLOOP_SRAMDEVICE_USB, pSRAM, nSramBytes, &bLegacy);
        }
        else if (_MainLoop_SramDevice == MAINLOOP_SRAMDEVICE_MEMCARD)
        {
            bLoaded = _MainLoopLoadSRAMFrom(
                MAINLOOP_SRAMDEVICE_MEMCARD, pSRAM, nSramBytes, &bLegacy);
        }
        else
        {
            if (_MainLoopSramUsbReady())
                bLoaded = _MainLoopLoadSRAMFrom(
                    MAINLOOP_SRAMDEVICE_USB, pSRAM, nSramBytes, &bLegacy);

            if (!bLoaded)
            {
                Bool bMcLegacy = FALSE;
                bLoaded = _MainLoopLoadSRAMFrom(
                    MAINLOOP_SRAMDEVICE_MEMCARD, pSRAM, nSramBytes, &bMcLegacy);
                if (bLoaded)
                {
                    bLegacy = bMcLegacy;
                    bMcFallback = TRUE;
                }
            }
        }

        _MainLoop_SRAMChecksum =
            _CalcChecksum((Uint32 *)pSRAM, nSramBytes / 4);

        /* Never delete the source. Mark only for copy/migration. */
        _MainLoop_SRAMUpdated = bLoaded &&
            (bLegacy || (bMcFallback && _MainLoopSramUsbReady()));
    }

    _MainLoop_SaveCounter = 0;
    _bStateSaved = FALSE;
}

/* Force-update the SRAM dirty flag (_MainLoop_SRAMUpdated) right now,
   ignoring the throttle in _MainLoopCheckSRAM. Used by _MenuEnable
   before it decides whether to fire the synchronous save: if the
   user wrote to SRAM in the last <CHECK_INTERVAL frames and pressed
   L2+R2 before _MainLoopCheckSRAM ran its next sampled checksum,
   the dirty flag would still be FALSE and the menu-open save would
   be skipped without this. The cost is one full-SRAM checksum at
   menu-open time, which is already a moment we accept a hitch for
   (the modal "Saving SRAM..." is already shown there). */
Bool _MainLoopForceCheckSRAM()
{
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;

    if (nSramBytes > 0)
    {
        Uint8 *pSRAM = _pSystem->GetSRAMData();
        Uint32 uChecksum;

        /* AURORA_RUNTIME_SAFE_SRAM_PTR_V1_4_1 */
        if (!pSRAM)
            return FALSE;
        uChecksum = _CalcChecksum((Uint32 *)pSRAM, nSramBytes / 4);

        if (_MainLoop_SRAMChecksum != uChecksum)
        {
            ML_TRACE(
                "SRAM force-check: dirty (old=%08X new=%08X)",
                (unsigned int)_MainLoop_SRAMChecksum,
                (unsigned int)uChecksum
            );
            _MainLoop_SRAMUpdated = TRUE;
            _MainLoop_SRAMChecksum = uChecksum;
        }
    }

    return TRUE;
}

Bool _MainLoopCheckSRAM()
{
    Int32 nSramBytes = _pSystem ? _pSystem->GetSRAMBytes() : 0;

    /* AURORA_MEGA_V2_SNES_SRAM_NO_POLL
       SNES SRAM is force-checked immediately when the in-game menu
       opens, before the save decision. Therefore a 30-frame full
       memory sweep during gameplay is redundant and can create a
       small periodic EE workload spike on large SRAM carts. */
    if (_pSystem == _pSnes)
        return TRUE;

    if (nSramBytes > 0)
    {
        /* The inline auto-save trigger (decrement SaveCounter -> call
           _MainLoopSaveSRAM(FALSE) when it hits zero) was removed
           deliberately. On the !_MainLoop_bMCSaveReady fallback path
           (NetherSX2 / any setup without MCSAVE.IRX next to the ELF)
           _MainLoopSaveSRAM ends up in MemCardWriteFile, which
           drives fopen/fwrite/fclose on the EE main thread and
           blocks the per-frame loop for the full duration of the
           memcard write. Games that keep the SRAM continuously
           dirty (RPG stats counters, HUD timers, etc.) caused this
           to fire at unpredictable moments and showed up as a
           gameplay hitch, while games that don't keep it dirty just
           saved at a different unpredictable point.

           The user-visible save path is now exclusively the
           synchronous one in _MenuEnable(TRUE) (mainloop_menu_runtime.cpp):
           opening the in-game menu with L2+R2 still calls
           _MainLoopSaveSRAM(TRUE) and shows the "Saving SRAM..." modal,
           so the save still happens at a deterministic, user-driven
           moment. _MainLoop_SRAMUpdated and _MainLoop_SRAMChecksum
           below are still maintained because _MenuEnable reads
           _MainLoop_SRAMUpdated to decide whether to actually run
           the save block at all. */

        /* The full-SRAM checksum used to run every frame (60Hz).
           For larger carts (up to MAINLOOP_MAXSRAMSIZE = 64 KB,
           i.e. 16k u32 adds) that's pure busywork: the only
           consumer is the _MainLoop_SRAMUpdated dirty bit that
           _MenuEnable polls when the user opens the menu, which
           never needs frame-accurate freshness. Run the check
           once every CHECK_INTERVAL frames (~0.5s @ 60Hz) so the
           dirty flag is still set well before any plausible
           L2+R2 press, without paying the cost on every frame. */
        static Uint32 sCheckFrame = 0;
        const Uint32 CHECK_INTERVAL = 30;
        if ((sCheckFrame++ % CHECK_INTERVAL) != 0)
        {
            return TRUE;
        }

        Uint8 *pSRAM = _pSystem->GetSRAMData();
        Uint32 uChecksum;

        if (!pSRAM)
            return FALSE;

        PROF_ENTER("_MainLoopCheckSRAM");

        uChecksum = _CalcChecksum((Uint32 *)pSRAM, nSramBytes / 4);

        if (_MainLoop_SRAMChecksum != uChecksum)
        {
#if CODE_DEBUG
            printf("SRAM changed!\n");
#endif
            ML_TRACE(
                "SRAM checksum changed: old=%08X new=%08X",
                (unsigned int)_MainLoop_SRAMChecksum,
                (unsigned int)uChecksum
            );

            _MainLoop_SRAMUpdated = TRUE;
            _MainLoop_SRAMChecksum = uChecksum;
        }

        PROF_LEAVE("_MainLoopCheckSRAM");
    }

    return TRUE;
}

/* ---- Versioned SNES/NES save states --------------------------------
 *
 * The recovered iaddis code wrote SnesStateT directly to host0:.  Besides
 * being a development-only path, that format had no version, ROM identity
 * or integrity check, and its load path could restore stale RAM after a
 * failed read.  The PS2 front-end now wraps the core payload in a small
 * header and keeps two banks per slot.  A bank only becomes visible after
 * its full payload has been flushed and the committed header is written.
 * The other bank remains untouched, so a reset/power loss during saving
 * cannot destroy the last known-good state.
 */

#define MAINLOOP_STATE_SLOT_NUM       5
#define MAINLOOP_STATE_BANK_NUM       2
/* The outer container remains version 1 for compatibility with existing SNES
   banks. Reserved[2] identifies the core; NesStateT has its own version. */
#define MAINLOOP_STATE_FORMAT_VERSION 1
#define MAINLOOP_STATE_HEADER_BYTES   64
#define MAINLOOP_STATE_MAX_ROOTS      8
#define MAINLOOP_STATE_MAX_CANDIDATES (MAINLOOP_STATE_MAX_ROOTS * MAINLOOP_STATE_BANK_NUM)
#define MAINLOOP_STATE_PAYLOAD_RAW     0
#define MAINLOOP_STATE_PAYLOAD_DEFLATE 1
#define MAINLOOP_STATE_SYSTEM_SNES      0
#define MAINLOOP_STATE_SYSTEM_NES       1
#define MAINLOOP_STATE_SYSTEM_SEGA      2
#define MAINLOOP_STATE_SYSTEM_PCE       3
#define MAINLOOP_STATE_RAW_BYTES \
    (sizeof(SnesStateT) > sizeof(NesStateT) \
        ? sizeof(SnesStateT) \
        : sizeof(NesStateT))
/* mz_compressBound() currently uses a conservative 110% + 128 bound.
   Keeping the buffer static avoids heap fragmentation on the 32 MB PS2. */
#define MAINLOOP_STATE_COMPRESS_BYTES \
    ((MAINLOOP_STATE_RAW_BYTES * 110) / 100 + 128)

struct MainLoopStateFileHeaderT
{
    Uint8  Magic[8];
    Uint32 uVersion;
    Uint32 nHeaderBytes;
    Uint32 nPayloadBytes;
    Uint32 uPayloadCRC;
    Uint32 uRomCRC;
    Uint32 nRomBytes;
    Uint32 uRomFlags;
    Uint32 iSlot;
    Uint32 uGeneration;
    /* Reserved[0] = payload encoding (raw/deflate).
       Reserved[1] = CRC32 of the stored compressed bytes. The public
       uPayloadCRC remains the CRC32 of the uncompressed core state.
       Reserved[2] = core ID (0 SNES, 1 NES). Old SNES banks were
       zero-initialised, so they remain valid. */
    Uint32 Reserved[5];
};

struct MainLoopStateConfigT
{
    Uint8  Magic[8];
    Uint32 uVersion;
    Uint32 nConfigBytes;
    Uint32 eDevice;
    Uint32 iSlot;
    Uint32 Reserved[2];
};

struct MainLoopStateRootT
{
    Char Root[16];
    Char DeviceName[24];
    Bool bMemCard;
};

struct MainLoopStateCandidateT
{
    Char Path[1024];
    Char DeviceName[16];
    MainLoopStateFileHeaderT Header;
};

typedef char MainLoopStateHeaderSizeCheck[
    sizeof(MainLoopStateFileHeaderT) == MAINLOOP_STATE_HEADER_BYTES ? 1 : -1
];
typedef char MainLoopStateConfigSizeCheck[
    sizeof(MainLoopStateConfigT) == 32 ? 1 : -1
];

static const Uint8 _MainLoop_StateMagic[8] =
{
    'S', 'N', 'R', 'S', 'T', 'A', 'T', 'E'
};
static const Uint8 _MainLoop_StateConfigMagic[8] =
{
    'S', 'N', 'R', 'S', 'C', 'F', 'G', '1'
};

static MainLoopStateDeviceE _MainLoop_StateDevice = MAINLOOP_STATEDEVICE_AUTO;
static Int32 _MainLoop_StateSlot = 0;
static Bool _MainLoop_StateDeviceChosen = FALSE;
static Char _MainLoop_StateLastMessage[192] = "No save-state operation yet.";
static Char _MainLoop_StateAvailability[192];
static Bool _MainLoop_StateRomCRCValid = FALSE;
static Uint32 _MainLoop_StateRomCRC = 0;
static MainLoopStateCandidateT _MainLoop_StateCandidates[MAINLOOP_STATE_MAX_CANDIDATES];
static Uint8 _MainLoop_StateCompressed[MAINLOOP_STATE_COMPRESS_BYTES]
    __attribute__((aligned(64)));

/* AURORA_PICODRIVE_STAGE2_DYNAMIC_STATE
 * Never tax SNES/NES BSS for PicoDrive's variable-size state. These buffers
 * appear only after a Sega state operation and are released on ROM change. */
static Uint8 *_MainLoop_SegaStateData = NULL;
static Uint32 _MainLoop_SegaStateCapacity = 0;
static Uint8 *_MainLoop_SegaCompressed = NULL;
static Uint32 _MainLoop_SegaCompressedCapacity = 0;

static void _MainLoopStateReleaseSegaScratch()
{
    if (_MainLoop_SegaStateData) free(_MainLoop_SegaStateData);
    if (_MainLoop_SegaCompressed) free(_MainLoop_SegaCompressed);
    _MainLoop_SegaStateData = NULL;
    _MainLoop_SegaStateCapacity = 0;
    _MainLoop_SegaCompressed = NULL;
    _MainLoop_SegaCompressedCapacity = 0;
}

/* AURORA_PD_STATE_SCRATCH_RELEASE_V3
 *
 * PicoDrive state data is temporary working memory. Keep the raw state and
 * optional compression buffer alive for the whole Save/Load operation, then
 * release them automatically on every exit path. This avoids leaving both
 * buffers pinned on the 32 MiB EE heap after a Sega state operation.
 */
class MainLoopSegaStateScratchGuard
{
public:
    MainLoopSegaStateScratchGuard()
        : m_bActive((_pSystem == _pSega || _pSystem == _pPce) ? TRUE : FALSE)
    {
    }

    ~MainLoopSegaStateScratchGuard()
    {
        if (m_bActive)
            _MainLoopStateReleaseSegaScratch();
    }

private:
    Bool m_bActive;

    MainLoopSegaStateScratchGuard(
        const MainLoopSegaStateScratchGuard &);
    MainLoopSegaStateScratchGuard &operator=(
        const MainLoopSegaStateScratchGuard &);
};

static Uint8 *_MainLoopStateEnsureSegaStateData(Uint32 nBytes)
{
    if (!nBytes)
        return NULL;
    if (_MainLoop_SegaStateCapacity < nBytes)
    {
        void *p = realloc(_MainLoop_SegaStateData, nBytes);
        if (!p)
            return NULL;
        _MainLoop_SegaStateData = (Uint8 *)p;
        _MainLoop_SegaStateCapacity = nBytes;
    }
    return _MainLoop_SegaStateData;
}

static Uint32 _MainLoopStateCompressedLimit(Uint32 nRawBytes)
{
    if (_pSystem != _pSega && _pSystem != _pPce)
        return (Uint32)sizeof(_MainLoop_StateCompressed);

    unsigned long long n =
        ((unsigned long long)nRawBytes * 110ULL) / 100ULL + 128ULL;
    return n <= 0xffffffffULL ? (Uint32)n : 0;
}

static Uint8 *_MainLoopStateGetCompressedBuffer(Uint32 nNeed, Uint32 *pCapacity)
{
    if (_pSystem != _pSega && _pSystem != _pPce)
    {
        if (pCapacity) *pCapacity = (Uint32)sizeof(_MainLoop_StateCompressed);
        return _MainLoop_StateCompressed;
    }

    if (!nNeed)
        return NULL;
    if (_MainLoop_SegaCompressedCapacity < nNeed)
    {
        void *p = realloc(_MainLoop_SegaCompressed, nNeed);
        if (!p)
            return NULL;
        _MainLoop_SegaCompressed = (Uint8 *)p;
        _MainLoop_SegaCompressedCapacity = nNeed;
    }
    if (pCapacity) *pCapacity = _MainLoop_SegaCompressedCapacity;
    return _MainLoop_SegaCompressed;
}
static Int32 _MainLoop_StateUnformattedCard = -1;
static Char _MainLoop_StateConfigPath[1024] = "";

static Bool _MainLoopStateEnsureOneDir(const Char *pPath);
static void _MainLoopStateDeleteSettings();
static void _MainLoopStateLoadSettingsFromRomDevice();

static Uint32 _MainLoopStateGetSystemId()
{
    if (_pSystem == _pNes)  return MAINLOOP_STATE_SYSTEM_NES;
    if (_pSystem == _pSega) return MAINLOOP_STATE_SYSTEM_SEGA;
    if (_pSystem == _pPce)  return MAINLOOP_STATE_SYSTEM_PCE;
    return MAINLOOP_STATE_SYSTEM_SNES;
}

static Uint32 _MainLoopStateGetPayloadBytes()
{
    /* AURORA_PICODRIVE_STAGE2_STATE_SIZE */
    if (_pSystem == _pSega)
    {
        Int32 nBytes = _pSega ? _pSega->GetStateSize() : 0;
        return nBytes > 0 ? (Uint32)nBytes : 0;
    }
    if (_pSystem == _pPce)
    {
        Int32 nBytes = _pPce ? _pPce->GetStateSize() : 0;
        return nBytes > 0 ? (Uint32)nBytes : 0;
    }
    if (_pSystem == _pNes)
    {
        /*
         * Let the active NES implementation describe its state envelope.
         *
         * InfoNES deliberately returns sizeof(NesStateT), preserving its
         * existing file format. QuickNES returns its compact native envelope.
         */
        Int32 nBytes = _pNes ? _pNes->GetStateSize() : 0;

        if (nBytes > 0 && nBytes <= (Int32)sizeof(_NesState))
        {
            return (Uint32)nBytes;
        }

        /* Defensive compatibility fallback. */
        return (Uint32)sizeof(_NesState);
    }

    return (Uint32)sizeof(_SnesState);
}

static Uint8 *_MainLoopStateGetPayloadData()
{
    if (_pSystem == _pNes)
        return (Uint8 *)&_NesState;
    if (_pSystem == _pSega || _pSystem == _pPce)
        return _MainLoopStateEnsureSegaStateData(
            _MainLoopStateGetPayloadBytes());
    return (Uint8 *)&_SnesState;
}

static void _MainLoopStateSetMessage(const Char *pFormat, ...)
{
    va_list Args;

    va_start(Args, pFormat);
    vsnprintf(
        _MainLoop_StateLastMessage,
        sizeof(_MainLoop_StateLastMessage),
        pFormat,
        Args
    );
    va_end(Args);
}

void MainLoopStateOnRomChanged()
{
    /* AURORA_PICODRIVE_STAGE2_RELEASE_STATE */
    _MainLoopStateReleaseSegaScratch();
    _MainLoop_StateRomCRCValid = FALSE;
    _MainLoop_StateRomCRC = 0;
    _MainLoop_StateUnformattedCard = -1;
    _bStateSaved = FALSE;

    /* A disc/ISO boot may have no writable config device until the user opens
       a ROM from mass2+, MMCE or HDD. Once that device is known, recover its
       saved default without doing a broad probe or directory scan. */
    if (_pSystem && _RomPath[0] && !_MainLoop_StateDeviceChosen)
    {
        _MainLoopStateLoadSettingsFromRomDevice();
    }
}

/* AURORA_PD_MEGA_FIX_20260820
 * Called immediately after MainLoopStateOnRomChanged(), with the CRC taken
 * before the active core can transform the shared ROM buffer. */
void MainLoopStatePrimeRomIdentityCRC(Uint32 uCRC)
{
    _MainLoop_StateRomCRC = uCRC;
    _MainLoop_StateRomCRCValid = TRUE;
}

Int32 MainLoopStateGetSlot()
{
    return _MainLoop_StateSlot;
}

MainLoopStateDeviceE MainLoopStateGetDevice()
{
    return _MainLoop_StateDevice;
}

const Char *MainLoopStateGetDeviceName()
{
    switch (_MainLoop_StateDevice)
    {
        case MAINLOOP_STATEDEVICE_USB:     return "USB";
        case MAINLOOP_STATEDEVICE_MEMCARD: return "Memory Card";
        case MAINLOOP_STATEDEVICE_MMCE:    return "MMCE";
        case MAINLOOP_STATEDEVICE_HDD:     return "Internal HDD";
        default:                           return "Auto";
    }
}

const Char *MainLoopStateGetLastMessage()
{
    return _MainLoop_StateLastMessage;
}

Int32 MainLoopStateGetUnformattedCard()
{
    return _MainLoop_StateUnformattedCard;
}

Bool MainLoopStateHasDeviceChoice()
{
    return _MainLoop_StateDeviceChosen;
}

void MainLoopStateForgetDeviceChoice()
{
    _MainLoop_StateDevice = MAINLOOP_STATEDEVICE_AUTO;
    _MainLoop_StateSlot = 0;
    _MainLoop_StateDeviceChosen = FALSE;
    _MainLoopStateDeleteSettings();
}

void MainLoopStateSetDevice(MainLoopStateDeviceE eDevice)
{
    if (eDevice >= MAINLOOP_STATEDEVICE_AUTO &&
        eDevice < MAINLOOP_STATEDEVICE_NUM)
    {
        _MainLoop_StateDevice = eDevice;
        if (eDevice == MAINLOOP_STATEDEVICE_AUTO)
        {
            _MainLoop_StateSlot = 0;
        }
    }
}

Bool MainLoopStateDeviceAvailable(MainLoopStateDeviceE eDevice)
{
    switch (eDevice)
    {
        case MAINLOOP_STATEDEVICE_AUTO:
            return TRUE;

        case MAINLOOP_STATEDEVICE_USB:
            return MassStorageIsEnabled() ? TRUE : FALSE;

        case MAINLOOP_STATEDEVICE_MEMCARD:
            return TRUE;

        case MAINLOOP_STATEDEVICE_MMCE:
            return MmceProbeAvailableSlots() ? TRUE : FALSE;

        case MAINLOOP_STATEDEVICE_HDD:
            return HddSupportIsEnabled() &&
                   (!strncmp(_RomPath, "hdd0:", 5) ||
                    !strncmp(_RomPath, "pfs0:", 5));

        default:
            return FALSE;
    }
}

void MainLoopStateCycleSlot()
{
    /* Auto is intentionally a zero-configuration quick-save mode. Its
       quick slot is always the first slot, including configurations
       written by older builds. Explicit devices retain all five slots. */
    if (_MainLoop_StateDevice == MAINLOOP_STATEDEVICE_AUTO)
    {
        _MainLoop_StateSlot = 0;
        return;
    }

    _MainLoop_StateSlot++;
    if (_MainLoop_StateSlot >= MAINLOOP_STATE_SLOT_NUM)
    {
        _MainLoop_StateSlot = 0;
    }
}

void MainLoopStateCycleDevice()
{
    _MainLoop_StateDevice = (MainLoopStateDeviceE)(_MainLoop_StateDevice + 1);
    if (_MainLoop_StateDevice >= MAINLOOP_STATEDEVICE_NUM)
    {
        _MainLoop_StateDevice = MAINLOOP_STATEDEVICE_AUTO;
    }
    if (_MainLoop_StateDevice == MAINLOOP_STATEDEVICE_AUTO)
    {
        _MainLoop_StateSlot = 0;
    }
}

static Bool _MainLoopStateConfigValid(const MainLoopStateConfigT *pConfig)
{
    return !memcmp(
                pConfig->Magic,
                _MainLoop_StateConfigMagic,
                sizeof(pConfig->Magic)) &&
           pConfig->uVersion == 2 &&
           pConfig->nConfigBytes == sizeof(*pConfig) &&
           pConfig->eDevice < MAINLOOP_STATEDEVICE_NUM &&
           pConfig->iSlot < MAINLOOP_STATE_SLOT_NUM;
}

static Bool _MainLoopStateConfigPathIsWritable(const Char *pPath)
{
    if (!pPath || !pPath[0])
    {
        return FALSE;
    }

    /* host: is a development filesystem and smb:/cdfs: are intentionally
       read-only in this frontend. Never pick one as the sole persistence
       location for the user's quick-save default. */
    return strncmp(pPath, "host:", 5) &&
           strncmp(pPath, "smb:", 4) &&
           strncmp(pPath, "cdfs:", 6) &&
           strncmp(pPath, "cdrom", 5) &&
           strncmp(pPath, "rom", 3);
}

static Bool _MainLoopStateConfigMapPath(
    const Char *pPath,
    Char *pMapped,
    Int32 nMappedBytes)
{
    if (!pPath || !pMapped || nMappedBytes <= 0)
    {
        return FALSE;
    }

    if (!strncmp(pPath, "hdd0:", 5))
    {
        return HddMapPath(pPath, pMapped, nMappedBytes) == 1;
    }

    return snprintf(pMapped, nMappedBytes, "%s", pPath) < nMappedBytes;
}

static void _MainLoopStateConfigEnsureParent(const Char *pPath)
{
    Char Directory[1024];
    Char *pSlash;
    size_t nLength;

    if (!pPath || strlen(pPath) >= sizeof(Directory))
    {
        return;
    }

    strcpy(Directory, pPath);
    pSlash = strrchr(Directory, '/');
    if (!pSlash)
    {
        return;
    }
    *pSlash = 0;
    nLength = strlen(Directory);
    if (nLength > 0 && Directory[nLength - 1] != ':')
    {
        _MainLoopStateEnsureOneDir(Directory);
    }
}

static Bool _MainLoopStateConfigRead(
    const Char *pPath,
    MainLoopStateConfigT *pConfig)
{
    Char MappedPath[1024];
    FILE *pFile;
    size_t nRead;

    if (!_MainLoopStateConfigMapPath(
            pPath,
            MappedPath,
            sizeof(MappedPath)))
    {
        return FALSE;
    }

    pFile = fopen(MappedPath, "rb");
    if (!pFile)
    {
        return FALSE;
    }
    nRead = fread(pConfig, 1, sizeof(*pConfig), pFile);
    fclose(pFile);

    if (nRead != sizeof(*pConfig) || !_MainLoopStateConfigValid(pConfig))
    {
        return FALSE;
    }

    snprintf(
        _MainLoop_StateConfigPath,
        sizeof(_MainLoop_StateConfigPath),
        "%s",
        pPath
    );
    return TRUE;
}

static Bool _MainLoopStateConfigWrite(
    const Char *pPath,
    const MainLoopStateConfigT *pConfig)
{
    Char MappedPath[1024];
    FILE *pFile;
    size_t nWritten;
    Bool bOK;

    if (!_MainLoopStateConfigPathIsWritable(pPath) ||
        !_MainLoopStateConfigMapPath(
            pPath,
            MappedPath,
            sizeof(MappedPath)))
    {
        return FALSE;
    }

    _MainLoopStateConfigEnsureParent(MappedPath);
    pFile = fopen(MappedPath, "wb");
    if (!pFile)
    {
        return FALSE;
    }
    nWritten = fwrite(pConfig, 1, sizeof(*pConfig), pFile);
    bOK = fflush(pFile) == 0;
    if (fclose(pFile) != 0)
    {
        bOK = FALSE;
    }
    if (nWritten != sizeof(*pConfig) || !bOK)
    {
        return FALSE;
    }

    snprintf(
        _MainLoop_StateConfigPath,
        sizeof(_MainLoop_StateConfigPath),
        "%s",
        pPath
    );
    return TRUE;
}

static Bool _MainLoopStateConfigApply(const MainLoopStateConfigT *pConfig)
{
    if (!_MainLoopStateConfigValid(pConfig))
    {
        return FALSE;
    }

    _MainLoop_StateDevice = (MainLoopStateDeviceE)pConfig->eDevice;
    _MainLoop_StateSlot = (Int32)pConfig->iSlot;
    if (_MainLoop_StateDevice == MAINLOOP_STATEDEVICE_AUTO)
    {
        _MainLoop_StateSlot = 0;
    }
    _MainLoop_StateDeviceChosen = TRUE;
    return TRUE;
}

static Bool _MainLoopStateConfigBuildRomPath(Char *pPath, Int32 nPathBytes)
{
    const Char *pColon;
    const Char *pPartitionEnd;
    Int32 nRootBytes;

    if (!_RomPath[0] || !pPath || nPathBytes <= 0)
    {
        return FALSE;
    }

    if (!strncmp(_RomPath, "hdd0:/", 6))
    {
        pPartitionEnd = strchr(_RomPath + 6, '/');
        if (!pPartitionEnd)
        {
            pPartitionEnd = _RomPath + strlen(_RomPath);
        }
        nRootBytes = (Int32)(pPartitionEnd - _RomPath);
    }
    else if (!strncmp(_RomPath, "pfs0:", 5))
    {
        nRootBytes = 5;
    }
    else if (!strncmp(_RomPath, "mass", 4) ||
             !strncmp(_RomPath, "mc", 2) ||
             !strncmp(_RomPath, "mmce", 4))
    {
        pColon = strchr(_RomPath, ':');
        if (!pColon)
        {
            return FALSE;
        }
        nRootBytes = (Int32)(pColon - _RomPath) + 1;
    }
    else
    {
        /* cdfs, smb and host are deliberately read-only/non-persistent. */
        return FALSE;
    }

    return snprintf(
               pPath,
               nPathBytes,
               "%.*s/SNESticle/state.cfg",
               nRootBytes,
               _RomPath) < nPathBytes;
}

static void _MainLoopStateLoadSettingsFromRomDevice()
{
    MainLoopStateConfigT Config;
    Char RomConfigPath[1024];

    if (_MainLoopStateConfigBuildRomPath(
            RomConfigPath,
            sizeof(RomConfigPath)) &&
        _MainLoopStateConfigRead(RomConfigPath, &Config))
    {
        _MainLoopStateConfigApply(&Config);
    }
}

void MainLoopStateSettingsLoad()
{
    MainLoopStateConfigT Config;
    static const Char *pMemoryCardPaths[] =
    {
        "mc0:/SNESticle/state.cfg",
        "mc1:/SNESticle/state.cfg",
        NULL
    };
    static const Char *pMassPaths[] =
    {
        "mass0:/SNESticle/state.cfg",
        "mass1:/SNESticle/state.cfg",
        "mass:/SNESticle/state.cfg",
        NULL
    };
    static const Char *pMMCEPaths[] =
    {
        "mmce0:/SNESticle/state.cfg",
        "mmce1:/SNESticle/state.cfg",
        NULL
    };
    Char BootPath[1024];
    Int32 i;

    _MainLoop_StateDevice = MAINLOOP_STATEDEVICE_AUTO;
    _MainLoop_StateSlot = 0;
    _MainLoop_StateDeviceChosen = FALSE;
    _MainLoop_StateConfigPath[0] = 0;

    /* Preserve the existing mc0/mc1 priority so upgrades keep their chosen
       target. A standalone ELF directory and every enabled writable local
       backend are fallbacks for consoles without a usable memory card. */
    for (i = 0; pMemoryCardPaths[i]; i++)
    {
        if (_MainLoopStateConfigRead(pMemoryCardPaths[i], &Config) &&
            _MainLoopStateConfigApply(&Config))
        {
            return;
        }
    }

    if (_MainLoop_BootDir[0] &&
        _MainLoopStateConfigPathIsWritable(_MainLoop_BootDir) &&
        snprintf(
            BootPath,
            sizeof(BootPath),
            "%sstate.cfg",
            _MainLoop_BootDir) < (Int32)sizeof(BootPath) &&
        _MainLoopStateConfigRead(BootPath, &Config) &&
        _MainLoopStateConfigApply(&Config))
    {
        return;
    }

    if (MassStorageIsEnabled() || Mx4sioIsEnabled())
    {
        for (i = 0; pMassPaths[i]; i++)
        {
            if (_MainLoopStateConfigRead(pMassPaths[i], &Config) &&
                _MainLoopStateConfigApply(&Config))
            {
                return;
            }
        }
    }

    if (MmceSupportIsEnabled() && !MmceNeedsRestart())
    {
        Int32 iSlots = MmceGetAvailableSlots();
        if (!iSlots)
        {
            iSlots = MmceProbeAvailableSlots();
        }
        for (i = 0; pMMCEPaths[i]; i++)
        {
            if ((iSlots & (1 << i)) &&
                _MainLoopStateConfigRead(pMMCEPaths[i], &Config) &&
                _MainLoopStateConfigApply(&Config))
            {
                return;
            }
        }
    }
}

Bool MainLoopStateSettingsSave()
{
    MainLoopStateConfigT Config;
    static const Char *pMemoryCardPaths[] =
    {
        "mc0:/SNESticle/state.cfg",
        "mc1:/SNESticle/state.cfg",
        NULL
    };
    static const Char *pMassPaths[] =
    {
        "mass0:/SNESticle/state.cfg",
        "mass1:/SNESticle/state.cfg",
        "mass:/SNESticle/state.cfg",
        NULL
    };
    static const Char *pMMCEPaths[] =
    {
        "mmce0:/SNESticle/state.cfg",
        "mmce1:/SNESticle/state.cfg",
        NULL
    };
    Char BootPath[1024];
    Char RomConfigPath[1024];
    Int32 i;
    Bool bSaved = FALSE;

    memset(&Config, 0, sizeof(Config));
    memcpy(Config.Magic, _MainLoop_StateConfigMagic, sizeof(Config.Magic));
    /* Version 2 deliberately invalidates the earlier menu-based target
       choice so the redesigned one-time chooser is shown once after update. */
    Config.uVersion = 2;
    Config.nConfigBytes = sizeof(Config);
    Config.eDevice = (Uint32)_MainLoop_StateDevice;
    Config.iSlot = (Uint32)_MainLoop_StateSlot;
    _MainLoop_StateDeviceChosen = TRUE;

    /* Update the location that supplied the config first. If none exists,
       prefer the writable ELF directory, then mc, mass/MX4SIO and MMCE.
       This makes the one-time choice persistent even without a memory card. */
    BgmIOBegin();
    if (_MainLoop_StateConfigPath[0])
    {
        bSaved = _MainLoopStateConfigWrite(
            _MainLoop_StateConfigPath,
            &Config
        );
    }

    if (!bSaved && _MainLoop_BootDir[0] &&
        _MainLoopStateConfigPathIsWritable(_MainLoop_BootDir) &&
        snprintf(
            BootPath,
            sizeof(BootPath),
            "%sstate.cfg",
            _MainLoop_BootDir) < (Int32)sizeof(BootPath))
    {
        bSaved = _MainLoopStateConfigWrite(BootPath, &Config);
    }

    if (!bSaved && _MainLoopStateConfigBuildRomPath(
            RomConfigPath,
            sizeof(RomConfigPath)))
    {
        bSaved = _MainLoopStateConfigWrite(RomConfigPath, &Config);
    }

    for (i = 0; !bSaved && pMemoryCardPaths[i]; i++)
    {
        bSaved = _MainLoopStateConfigWrite(pMemoryCardPaths[i], &Config);
    }

    if (!bSaved && (MassStorageIsEnabled() || Mx4sioIsEnabled()))
    {
        for (i = 0; !bSaved && pMassPaths[i]; i++)
        {
            bSaved = _MainLoopStateConfigWrite(pMassPaths[i], &Config);
        }
    }

    if (!bSaved && MmceSupportIsEnabled() && !MmceNeedsRestart())
    {
        Int32 iSlots = MmceGetAvailableSlots();
        if (!iSlots)
        {
            iSlots = MmceProbeAvailableSlots();
        }
        for (i = 0; !bSaved && pMMCEPaths[i]; i++)
        {
            if (iSlots & (1 << i))
            {
                bSaved = _MainLoopStateConfigWrite(pMMCEPaths[i], &Config);
            }
        }
    }
    BgmIOEnd();

    return bSaved;
}

static void _MainLoopStateDeleteSettings()
{
    static const Char *pConfigPaths[] =
    {
        "mc0:/SNESticle/state.cfg",
        "mc1:/SNESticle/state.cfg",
        "mass0:/SNESticle/state.cfg",
        "mass1:/SNESticle/state.cfg",
        "mass:/SNESticle/state.cfg",
        "mmce0:/SNESticle/state.cfg",
        "mmce1:/SNESticle/state.cfg",
        NULL
    };
    Char MappedPath[1024];
    Int32 i;

    if (_MainLoop_StateConfigPath[0] &&
        _MainLoopStateConfigMapPath(
            _MainLoop_StateConfigPath,
            MappedPath,
            sizeof(MappedPath)))
    {
        remove(MappedPath);
    }
    for (i = 0; pConfigPaths[i]; i++)
    {
        remove(pConfigPaths[i]);
    }
    _MainLoop_StateConfigPath[0] = 0;
}

static const Char *_MainLoopStateGetUnsupportedChip(Uint32 uFlags)
{
    /* AURORA_SPECIAL_CHIP_STATE_V1
     * DSP-1/2/4, OBC1, SuperFX, S-DD1 and S-RTC have a tagged snapshot in
     * the unused SRAM-state tail. Keep refusing them only if the current
     * cartridge cannot fit that envelope safely. */
    if (uFlags & SNROM_FLAG_GAMEBOY) return "Super Game Boy";
    if (uFlags & SNROM_FLAG_DSP3)    return "DSP-3";

    if ((uFlags & SNROM_FLAG_SUPERFX) &&
        (!_pSnes || !_pSnes->CanSerializeSpecialChipState()))
        return "SuperFX";
    if ((uFlags & SNROM_FLAG_DSP1) &&
        (!_pSnes || !_pSnes->CanSerializeSpecialChipState()))
        return "DSP-1";
    if ((uFlags & SNROM_FLAG_DSP2) &&
        (!_pSnes || !_pSnes->CanSerializeSpecialChipState()))
        return "DSP-2";
    if ((uFlags & SNROM_FLAG_DSP4) &&
        (!_pSnes || !_pSnes->CanSerializeSpecialChipState()))
        return "DSP-4";
    if ((uFlags & SNROM_FLAG_OBC1) &&
        (!_pSnes || !_pSnes->CanSerializeSpecialChipState()))
        return "OBC1";
    if ((uFlags & SNROM_FLAG_SDD1) &&
        (!_pSnes || !_pSnes->CanSerializeSpecialChipState()))
        return "S-DD1";
    if ((uFlags & SNROM_FLAG_SRTC) &&
        (!_pSnes || !_pSnes->CanSerializeSpecialChipState()))
        return "S-RTC";

    /* Existing CX4 format/offset is intentionally unchanged. */
    if ((uFlags & SNROM_FLAG_CX4) &&
        (!_pSnes || !_pSnes->CanSerializeCX4State()))
        return "CX4";

    return NULL;
}

static Bool _MainLoopStateCheckAvailability(Char *pReason, Int32 nReasonBytes)
{
    const Char *pChip;
    NetPlayRPCStatusT NetStatus;

    if (!_pSystem)
    {
        snprintf(pReason, nReasonBytes, "No game loaded.");
        return FALSE;
    }

    if (_pSystem != _pSnes && _pSystem != _pNes &&
        _pSystem != _pSega && _pSystem != _pPce)
    {
        snprintf(pReason, nReasonBytes, "This system cannot save states.");
        return FALSE;
    }

    if (_pSystem == _pNes)
    {
        if (!_pNesRom || !_pNesRom->IsLoaded() ||
            !_pNes || !_pNes->IsRomReady())
        {
            snprintf(pReason, nReasonBytes,
                     "NES state unavailable for this cartridge/mapper.");
            return FALSE;
        }
    }
    else if (_pSystem == _pSega)
    {
        /* AURORA_PICODRIVE_STAGE2_STATE_AVAILABLE */
        if (!_pSegaRom || !_pSegaRom->IsLoaded() ||
            !_pSega || !_pSega->IsRomReady())
        {
            snprintf(pReason, nReasonBytes, "PicoDrive state unavailable.");
            return FALSE;
        }
    }
    else if (_pSystem == _pPce)
    {
        if (!_pPceRom || !_pPceRom->IsLoaded() || !_pPce || !_pPce->IsRomReady())
        {
            snprintf(pReason, nReasonBytes, "Beetle PCE Fast state unavailable.");
            return FALSE;
        }
    }
    else if (!_pSnesRom || !_pSnesRom->IsLoaded())
    {
        snprintf(pReason, nReasonBytes, "No SNES ROM loaded.");
        return FALSE;
    }

    pChip = _pSystem == _pSnes
        ? _MainLoopStateGetUnsupportedChip(_pSnesRom->m_Flags)
        : NULL;
    if (pChip)
    {
        snprintf(pReason, nReasonBytes, "%s state is not serialized yet.", pChip);
        return FALSE;
    }

    if (s_pMovieClip &&
        (s_pMovieClip->IsRecording() || s_pMovieClip->IsPlaying()))
    {
        snprintf(pReason, nReasonBytes, "Stop movie recording/playback first.");
        return FALSE;
    }

    memset(&NetStatus, 0, sizeof(NetStatus));
    NetPlayGetStatus(&NetStatus);
    if (NetStatus.eServerStatus != NETPLAY_STATUS_IDLE ||
        NetStatus.eClientStatus != NETPLAY_STATUS_IDLE)
    {
        snprintf(pReason, nReasonBytes, "Save states are disabled during netplay.");
        return FALSE;
    }

    if (_pSystem == _pSega)
        snprintf(pReason, nReasonBytes, "Ready: PicoDrive cartridge state.");
    else if (_pSystem == _pPce)
        snprintf(pReason, nReasonBytes, "Ready: PC Engine HuCard state.");
    else
        snprintf(
            pReason,
            nReasonBytes,
            _pSystem == _pNes
                ? "Ready: NES cartridge and mapper state."
                : "Ready: base SNES hardware."
        );
    return TRUE;
}

const Char *MainLoopStateGetAvailability()
{
    _MainLoopStateCheckAvailability(
        _MainLoop_StateAvailability,
        sizeof(_MainLoop_StateAvailability)
    );
    return _MainLoop_StateAvailability;
}

static Bool _MainLoopStateGetRomIdentity(
    Uint32 *puCRC,
    Uint32 *pnBytes,
    Uint32 *puFlags)
{
    Uint8 *pRomData;
    Uint32 nRomBytes;

    if (_pSystem == _pNes)
    {
        if (!_pNesRom || !_pNesRom->IsLoaded())
        {
            return FALSE;
        }
        pRomData = _pNesRom->GetData();
        nRomBytes = _pNesRom->GetBytes();
    }
    else if (_pSystem == _pSega)
    {
        /* AURORA_PICODRIVE_STAGE2_ROM_ID */
        if (!_pSegaRom || !_pSegaRom->IsLoaded())
            return FALSE;
        pRomData = _pSegaRom->GetData();
        nRomBytes = _pSegaRom->GetBytes();
    }
    else if (_pSystem == _pPce)
    {
        if (!_pPceRom || !_pPceRom->IsLoaded()) return FALSE;
        pRomData = _pPceRom->GetData(); nRomBytes = _pPceRom->GetBytes();
    }
    else if (_pSnesRom && _pSnesRom->IsLoaded())
    {
        pRomData = _pSnesRom->GetData();
        nRomBytes = _pSnesRom->GetBytes();
    }
    else
    {
        return FALSE;
    }
    if (!pRomData || !nRomBytes)
    {
        return FALSE;
    }

    if (!_MainLoop_StateRomCRCValid)
    {
        _MainLoop_StateRomCRC = (Uint32)mz_crc32(
            MZ_CRC32_INIT,
            pRomData,
            nRomBytes
        );
        _MainLoop_StateRomCRCValid = TRUE;
    }

    *puCRC = _MainLoop_StateRomCRC;
    *pnBytes = nRomBytes;
    if (_pSystem == _pNes)
        *puFlags = _pNesRom->GetMapperNumber();
    else if (_pSystem == _pSega || _pSystem == _pPce)
        *puFlags = 0;
    else
        *puFlags = _pSnesRom->m_Flags;
    return TRUE;
}

static Bool _MainLoopStateIsMassRoot(const Char *pPath, Char *pRoot, Int32 nRootBytes)
{
    const Char *pColon;
    Int32 nLength;
    Int32 i;

    if (!pPath || strncmp(pPath, "mass", 4))
    {
        return FALSE;
    }

    pColon = strchr(pPath, ':');
    if (!pColon)
    {
        return FALSE;
    }

    nLength = (Int32)(pColon - pPath) + 1;
    if (nLength < 5 || nLength >= nRootBytes)
    {
        return FALSE;
    }

    for (i = 4; i < nLength - 1; i++)
    {
        if (pPath[i] < '0' || pPath[i] > '9')
        {
            return FALSE;
        }
    }

    memcpy(pRoot, pPath, nLength);
    pRoot[nLength] = 0;
    return TRUE;
}

static Bool _MainLoopStateIsNumberedRoot(
    const Char *pPath,
    const Char *pPrefix,
    Int32 nPrefixBytes,
    Char *pRoot,
    Int32 nRootBytes)
{
    const Char *pColon;
    Int32 nLength;
    Int32 i;

    if (!pPath || strncmp(pPath, pPrefix, nPrefixBytes))
    {
        return FALSE;
    }

    pColon = strchr(pPath, ':');
    if (!pColon)
    {
        return FALSE;
    }

    nLength = (Int32)(pColon - pPath) + 1;
    if (nLength <= nPrefixBytes + 1 || nLength >= nRootBytes)
    {
        return FALSE;
    }

    for (i = nPrefixBytes; i < nLength - 1; i++)
    {
        if (pPath[i] < '0' || pPath[i] > '9')
        {
            return FALSE;
        }
    }

    memcpy(pRoot, pPath, nLength);
    pRoot[nLength] = 0;
    return TRUE;
}

static Bool _MainLoopStateIsMemCardRoot(
    const Char *pPath,
    Char *pRoot,
    Int32 nRootBytes)
{
    return _MainLoopStateIsNumberedRoot(
        pPath,
        "mc",
        2,
        pRoot,
        nRootBytes
    );
}

static Bool _MainLoopStateIsMMCERoot(
    const Char *pPath,
    Char *pRoot,
    Int32 nRootBytes)
{
    return _MainLoopStateIsNumberedRoot(
        pPath,
        "mmce",
        4,
        pRoot,
        nRootBytes
    );
}

static Bool _MainLoopStateGetHddRoot(Char *pRoot, Int32 nRootBytes)
{
    Char MappedPath[1024];

    if (!_RomPath[0])
    {
        return FALSE;
    }

    if (!strncmp(_RomPath, "pfs0:", 5))
    {
        snprintf(pRoot, nRootBytes, "pfs0:");
        return TRUE;
    }

    if (strncmp(_RomPath, "hdd0:", 5) ||
        !HddSupportIsEnabled() ||
        HddLoadEmbeddedIrx() < 0)
    {
        return FALSE;
    }

    if (HddMapPath(_RomPath, MappedPath, sizeof(MappedPath)) != 1)
    {
        return FALSE;
    }

    snprintf(pRoot, nRootBytes, "pfs0:");
    return TRUE;
}

static void _MainLoopStateAddRoot(
    MainLoopStateRootT *pRoots,
    Int32 *pnRoots,
    const Char *pRoot,
    const Char *pDeviceName,
    Bool bMemCard)
{
    Int32 i;

    for (i = 0; i < *pnRoots; i++)
    {
        if (!strcmp(pRoots[i].Root, pRoot))
        {
            return;
        }
    }

    if (*pnRoots >= MAINLOOP_STATE_MAX_ROOTS)
    {
        return;
    }

    snprintf(pRoots[*pnRoots].Root, sizeof(pRoots[*pnRoots].Root), "%s", pRoot);
    snprintf(
        pRoots[*pnRoots].DeviceName,
        sizeof(pRoots[*pnRoots].DeviceName),
        "%s",
        pDeviceName
    );
    pRoots[*pnRoots].bMemCard = bMemCard;
    (*pnRoots)++;
}

static Int32 _MainLoopStateBuildRoots(
    MainLoopStateDeviceE eDevice,
    MainLoopStateRootT *pRoots)
{
    Int32 nRoots = 0;
    Char Root[16];
    Bool bAuto = eDevice == MAINLOOP_STATEDEVICE_AUTO;
    Int32 iMMCESlots = 0;

    if ((bAuto || eDevice == MAINLOOP_STATEDEVICE_MMCE) &&
        MmceSupportIsEnabled())
    {
        iMMCESlots = MmceProbeAvailableSlots();
    }

    /* Auto starts with the ROM's own device.  This also covers mass2+,
       mc2+ and future MMCE unit numbers without hard-coding them. */
    if (bAuto)
    {
        if (_MainLoopStateIsMassRoot(_RomPath, Root, sizeof(Root)))
        {
            _MainLoopStateAddRoot(pRoots, &nRoots, Root, Root, FALSE);
        }
        else if (_MainLoopStateIsMemCardRoot(_RomPath, Root, sizeof(Root)))
        {
            _MainLoopStateAddRoot(pRoots, &nRoots, Root, Root, TRUE);
        }
        else if (_MainLoopStateIsMMCERoot(_RomPath, Root, sizeof(Root)) &&
                 Root[4] >= '0' && Root[4] <= '1' &&
                 (iMMCESlots & (1 << (Root[4] - '0'))))
        {
            _MainLoopStateAddRoot(pRoots, &nRoots, Root, Root, TRUE);
        }
        else if (_MainLoopStateGetHddRoot(Root, sizeof(Root)))
        {
            _MainLoopStateAddRoot(
                pRoots,
                &nRoots,
                Root,
                "Internal HDD",
                FALSE
            );
        }
    }
    else if (eDevice == MAINLOOP_STATEDEVICE_USB &&
             _MainLoopStateIsMassRoot(_RomPath, Root, sizeof(Root)))
    {
        _MainLoopStateAddRoot(pRoots, &nRoots, Root, Root, FALSE);
    }
    else if (eDevice == MAINLOOP_STATEDEVICE_MEMCARD &&
             _MainLoopStateIsMemCardRoot(_RomPath, Root, sizeof(Root)))
    {
        _MainLoopStateAddRoot(pRoots, &nRoots, Root, Root, TRUE);
    }
    else if (eDevice == MAINLOOP_STATEDEVICE_MMCE &&
             _MainLoopStateIsMMCERoot(_RomPath, Root, sizeof(Root)) &&
             Root[4] >= '0' && Root[4] <= '1' &&
             (iMMCESlots & (1 << (Root[4] - '0'))))
    {
        _MainLoopStateAddRoot(pRoots, &nRoots, Root, Root, TRUE);
    }
    else if (eDevice == MAINLOOP_STATEDEVICE_HDD &&
             _MainLoopStateGetHddRoot(Root, sizeof(Root)))
    {
        _MainLoopStateAddRoot(
            pRoots,
            &nRoots,
            Root,
            "Internal HDD",
            FALSE
        );
    }

    if (bAuto || eDevice == MAINLOOP_STATEDEVICE_USB)
    {
        _MainLoopStateAddRoot(pRoots, &nRoots, "mass0:", "mass0:", FALSE);
        _MainLoopStateAddRoot(pRoots, &nRoots, "mass1:", "mass1:", FALSE);
        _MainLoopStateAddRoot(pRoots, &nRoots, "mass:", "mass:", FALSE);
    }

    if (bAuto || eDevice == MAINLOOP_STATEDEVICE_MEMCARD)
    {
        _MainLoopStateAddRoot(pRoots, &nRoots, "mc0:", "mc0:", TRUE);
        _MainLoopStateAddRoot(pRoots, &nRoots, "mc1:", "mc1:", TRUE);
    }

    if (bAuto || eDevice == MAINLOOP_STATEDEVICE_MMCE)
    {
        if (iMMCESlots & 1)
            _MainLoopStateAddRoot(pRoots, &nRoots, "mmce0:", "mmce0:", TRUE);
        if (iMMCESlots & 2)
            _MainLoopStateAddRoot(pRoots, &nRoots, "mmce1:", "mmce1:", TRUE);
    }

    return nRoots;
}

static Bool _MainLoopStateEnsureOneDir(const Char *pPath)
{
    struct stat Status;

    if (mkdir(pPath, 0777) == 0 || errno == EEXIST)
    {
        return TRUE;
    }

    return stat(pPath, &Status) == 0 && S_ISDIR(Status.st_mode);
}

static Bool _MainLoopStateEnsureRoot(const MainLoopStateRootT *pRoot)
{
    Char Path[1024];

    /* MMCE also uses the short memory-card filename rules, hence
       bMemCard, but only real mcN: roots have PS2-card format state. */
    if (pRoot->Root[0] == 'm' &&
        pRoot->Root[1] == 'c' &&
        pRoot->Root[2] >= '0' &&
        pRoot->Root[2] <= '1' &&
        pRoot->Root[3] == ':')
    {
        Int32 iPort = pRoot->Root[2] - '0';
        MemCardStatusE eStatus = MemCardGetStatus(iPort);

        if (eStatus == MEMCARD_STATUS_UNFORMATTED)
        {
            if (_MainLoop_StateUnformattedCard < 0)
            {
                _MainLoop_StateUnformattedCard = iPort;
            }
            return FALSE;
        }
        /* For READY, absent and unknown results, retain the established
           mkdir/stat write probe below. It is the compatibility fallback
           for unusual drivers that support stdio but not GetStat on "/". */
    }

    snprintf(Path, sizeof(Path), "%s/SNESticle", pRoot->Root);
    if (!_MainLoopStateEnsureOneDir(Path))
    {
        return FALSE;
    }

    if (!pRoot->bMemCard)
    {
        snprintf(Path, sizeof(Path), "%s/SNESticle/states", pRoot->Root);
        if (!_MainLoopStateEnsureOneDir(Path))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static void _MainLoopStateBuildBankPath(
    const MainLoopStateRootT *pRoot,
    Int32 iSlot,
    Int32 iBank,
    Char *pPath,
    Int32 nPathBytes)
{
    Char SaveName[256];
    Char Directory[256];
    Int32 nMaxName;

    if (pRoot->bMemCard)
    {
        snprintf(Directory, sizeof(Directory), "%s/SNESticle", pRoot->Root);
    }
    else
    {
        snprintf(Directory, sizeof(Directory), "%s/SNESticle/states", pRoot->Root);
    }

    nMaxName = PathGetMaxFileNameLength(Directory) - 4;
    PathTruncFileName(SaveName, _RomName, nMaxName);
    snprintf(
        pPath,
        nPathBytes,
        "%s/%s.%c%d%c",
        Directory,
        SaveName,
        _pSystem == _pNes ? 'n' : (_pSystem == _pSega ? 'g' : 's'),
        iSlot + 1,
        iBank ? 'b' : 'a'
    );
}

/* Header result: 1 = valid/current ROM, 0 = missing,
   -1 = incomplete/corrupt, -2 = valid format but another ROM. */
static Int32 _MainLoopStateReadHeader(
    const Char *pPath,
    Int32 iSlot,
    Uint32 uRomCRC,
    Uint32 nRomBytes,
    Uint32 uRomFlags,
    MainLoopStateFileHeaderT *pHeader)
{
    FILE *pFile;
    size_t nRead;
    Bool bPayloadLayoutValid;
    Uint32 nExpectedPayloadBytes = _MainLoopStateGetPayloadBytes();
    Uint32 uExpectedSystem = _MainLoopStateGetSystemId();

    pFile = fopen(pPath, "rb");
    if (!pFile)
    {
        return 0;
    }

    nRead = fread(pHeader, 1, sizeof(*pHeader), pFile);
    fclose(pFile);
    if (nRead != sizeof(*pHeader))
    {
        return -1;
    }

    bPayloadLayoutValid =
        (pHeader->Reserved[0] == MAINLOOP_STATE_PAYLOAD_RAW &&
         pHeader->nPayloadBytes == nExpectedPayloadBytes) ||
        (pHeader->Reserved[0] == MAINLOOP_STATE_PAYLOAD_DEFLATE &&
         pHeader->nPayloadBytes > 0 &&
         pHeader->nPayloadBytes <= _MainLoopStateCompressedLimit(nExpectedPayloadBytes));

    if (memcmp(pHeader->Magic, _MainLoop_StateMagic, sizeof(pHeader->Magic)) ||
        pHeader->uVersion != MAINLOOP_STATE_FORMAT_VERSION ||
        pHeader->nHeaderBytes != sizeof(*pHeader) ||
        !bPayloadLayoutValid ||
        pHeader->Reserved[2] != uExpectedSystem ||
        pHeader->iSlot != (Uint32)iSlot)
    {
        return -1;
    }

    if (pHeader->uRomCRC != uRomCRC ||
        pHeader->nRomBytes != nRomBytes ||
        pHeader->uRomFlags != uRomFlags)
    {
        return -2;
    }

    return 1;
}

static Bool _MainLoopStateReadPayload(
    const Char *pPath,
    const MainLoopStateFileHeaderT *pExpectedHeader)
{
    MainLoopStateFileHeaderT Header;
    FILE *pFile;
    size_t nRead;
    Uint32 uCRC;
    Bool bDecoded = FALSE;
    Uint8 *pStateData = _MainLoopStateGetPayloadData();
    Uint32 nStateBytes = _MainLoopStateGetPayloadBytes();

    /* AURORA_PICODRIVE_STAGE2_STATE_DATA_GUARD */
    if (!pStateData || !nStateBytes)
        return FALSE;

    pFile = fopen(pPath, "rb");
    if (!pFile)
    {
        return FALSE;
    }

    nRead = fread(&Header, 1, sizeof(Header), pFile);
    if (nRead != sizeof(Header) ||
        memcmp(&Header, pExpectedHeader, sizeof(Header)))
    {
        fclose(pFile);
        return FALSE;
    }

    if (Header.Reserved[0] == MAINLOOP_STATE_PAYLOAD_RAW)
    {
        nRead = fread(pStateData, 1, nStateBytes, pFile);
        bDecoded = nRead == nStateBytes;
    }
    else if (Header.Reserved[0] == MAINLOOP_STATE_PAYLOAD_DEFLATE)
    {
        /* AURORA_PICODRIVE_STAGE2_READ_COMPRESSED */
        mz_ulong nDecodedBytes = nStateBytes;
        Uint32 nCompressedCapacity = 0;
        Uint8 *pCompressed = _MainLoopStateGetCompressedBuffer(
            Header.nPayloadBytes, &nCompressedCapacity);

        if (pCompressed && Header.nPayloadBytes <= nCompressedCapacity)
        {
            nRead = fread(
                pCompressed,
                1,
                Header.nPayloadBytes,
                pFile
            );
            if (nRead == Header.nPayloadBytes &&
                mz_uncompress(
                    pStateData,
                    &nDecodedBytes,
                    pCompressed,
                    Header.nPayloadBytes
                ) == MZ_OK &&
                nDecodedBytes == nStateBytes)
            {
                bDecoded = TRUE;
            }
        }
    }

    fclose(pFile);
    if (!bDecoded)
    {
        return FALSE;
    }

    uCRC = (Uint32)mz_crc32(
        MZ_CRC32_INIT,
        pStateData,
        nStateBytes
    );
    return uCRC == Header.uPayloadCRC;
}

static Bool _MainLoopStateWriteBank(
    const Char *pPath,
    MainLoopStateFileHeaderT *pHeader,
    const Uint8 *pPayload,
    Uint32 nPayloadBytes)
{
    MainLoopStateFileHeaderT PendingHeader;
    FILE *pFile;
    Bool bOK = TRUE;

    PendingHeader = *pHeader;
    memset(PendingHeader.Magic, 0, sizeof(PendingHeader.Magic));

    pFile = fopen(pPath, "wb");
    if (!pFile)
    {
        return FALSE;
    }

    if (fwrite(&PendingHeader, 1, sizeof(PendingHeader), pFile) != sizeof(PendingHeader) ||
        fwrite(pPayload, 1, nPayloadBytes, pFile) != nPayloadBytes ||
        fflush(pFile) != 0 ||
        fseek(pFile, 0, SEEK_SET) != 0 ||
        fwrite(pHeader, 1, sizeof(*pHeader), pFile) != sizeof(*pHeader) ||
        fflush(pFile) != 0)
    {
        bOK = FALSE;
    }

    if (fclose(pFile) != 0)
    {
        bOK = FALSE;
    }

    return bOK;
}

static Bool _MainLoopStateGenerationNewer(Uint32 uA, Uint32 uB)
{
    return (Int32)(uA - uB) > 0;
}

static Int32 _MainLoopStateScanCandidates(
    MainLoopStateDeviceE eDevice,
    Int32 iSlot,
    Uint32 uRomCRC,
    Uint32 nRomBytes,
    Uint32 uRomFlags,
    Bool *pbWrongRom,
    Bool *pbCorrupt)
{
    MainLoopStateRootT Roots[MAINLOOP_STATE_MAX_ROOTS];
    Int32 nRoots;
    Int32 nCandidates = 0;
    Int32 iRoot;
    Int32 iBank;

    nRoots = _MainLoopStateBuildRoots(eDevice, Roots);
    for (iRoot = 0; iRoot < nRoots; iRoot++)
    {
        for (iBank = 0; iBank < MAINLOOP_STATE_BANK_NUM; iBank++)
        {
            MainLoopStateFileHeaderT Header;
            Char Path[1024];
            Int32 Result;

            _MainLoopStateBuildBankPath(
                &Roots[iRoot],
                iSlot,
                iBank,
                Path,
                sizeof(Path)
            );
            Result = _MainLoopStateReadHeader(
                Path,
                iSlot,
                uRomCRC,
                nRomBytes,
                uRomFlags,
                &Header
            );

            if (Result == 1 && nCandidates < MAINLOOP_STATE_MAX_CANDIDATES)
            {
                MainLoopStateCandidateT *pCandidate =
                    &_MainLoop_StateCandidates[nCandidates++];
                snprintf(pCandidate->Path, sizeof(pCandidate->Path), "%s", Path);
                snprintf(
                    pCandidate->DeviceName,
                    sizeof(pCandidate->DeviceName),
                    "%s",
                    Roots[iRoot].DeviceName
                );
                pCandidate->Header = Header;
            }
            else if (Result == -2)
            {
                *pbWrongRom = TRUE;
            }
            else if (Result == -1)
            {
                *pbCorrupt = TRUE;
            }
        }
    }

    return nCandidates;
}

static void _MainLoopStateSortCandidates(Int32 nCandidates)
{
    Int32 i;
    Int32 j;

    for (i = 0; i < nCandidates; i++)
    {
        for (j = i + 1; j < nCandidates; j++)
        {
            if (_MainLoopStateGenerationNewer(
                    _MainLoop_StateCandidates[j].Header.uGeneration,
                    _MainLoop_StateCandidates[i].Header.uGeneration))
            {
                MainLoopStateCandidateT Temp = _MainLoop_StateCandidates[i];
                _MainLoop_StateCandidates[i] = _MainLoop_StateCandidates[j];
                _MainLoop_StateCandidates[j] = Temp;
            }
        }
    }
}

Bool _MainLoopLoadState()
{
    MainLoopSegaStateScratchGuard SegaScratchGuard;
    Char Reason[192];
    Uint32 uRomCRC;
    Uint32 nRomBytes;
    Uint32 uRomFlags;
    Bool bWrongRom = FALSE;
    Bool bCorrupt = FALSE;
    Int32 nCandidates;
    Int32 iCandidate;

    _bStateSaved = FALSE;

    if (!_MainLoopStateCheckAvailability(Reason, sizeof(Reason)))
    {
        _MainLoopStateSetMessage("%s", Reason);
        return FALSE;
    }

    if (!_MainLoopStateGetRomIdentity(&uRomCRC, &nRomBytes, &uRomFlags))
    {
        _MainLoopStateSetMessage("Cannot identify the loaded ROM.");
        return FALSE;
    }

    nCandidates = _MainLoopStateScanCandidates(
        _MainLoop_StateDevice,
        _MainLoop_StateSlot,
        uRomCRC,
        nRomBytes,
        uRomFlags,
        &bWrongRom,
        &bCorrupt
    );
    _MainLoopStateSortCandidates(nCandidates);

    for (iCandidate = 0; iCandidate < nCandidates; iCandidate++)
    {
        MainLoopStateCandidateT *pCandidate =
            &_MainLoop_StateCandidates[iCandidate];

        Bool bPayloadOK = _MainLoopStateReadPayload(
            pCandidate->Path,
            &pCandidate->Header
        );
        Bool bRestoreOK = FALSE;
        if (bPayloadOK)
        {
            /* AURORA_PICODRIVE_STAGE2_STATE_RESTORE */
            if (_pSystem == _pNes)
                bRestoreOK = _pNes->RestoreState(&_NesState);
            else if (_pSystem == _pSega)
            {
                Uint8 *pSegaStateData = _MainLoopStateGetPayloadData();
                Uint32 nSegaStateBytes = _MainLoopStateGetPayloadBytes();
                bRestoreOK = pSegaStateData && nSegaStateBytes &&
                    _pSega->RestoreStateChecked(pSegaStateData, (Int32)nSegaStateBytes);
            }
            else if (_pSystem == _pPce)
            {
                Uint8 *pPceStateData = _MainLoopStateGetPayloadData();
                Uint32 nPceStateBytes = _MainLoopStateGetPayloadBytes();
                bRestoreOK = pPceStateData && nPceStateBytes &&
                    _pPce->RestoreStateChecked(pPceStateData, (Int32)nPceStateBytes);
            }
            else
                bRestoreOK = _pSnes->RestoreState(&_SnesState);
        }

        if (bRestoreOK)
        {
            Int32 nSramBytes = _pSystem->GetSRAMBytes();

            _bStateSaved = TRUE;
            _MainLoop_SaveCounter = 0;
            if (nSramBytes > 0)
            {
                _MainLoop_SRAMChecksum = _CalcChecksum(
                    (Uint32 *)_pSystem->GetSRAMData(),
                    nSramBytes / 4
                );
                _MainLoop_SRAMUpdated = TRUE;
            }

            if (_AudMix)
            {
                _AudMix->Reset();
            }

            _MainLoopResetInputChecksums();
#if MAINLOOP_HISTORY
            _MainLoopResetHistory();
#endif

            _MainLoopStateSetMessage(
                "Loaded slot %d from %s.",
                _MainLoop_StateSlot + 1,
                pCandidate->DeviceName
            );
            ConPrint("State loaded: %s\n", pCandidate->Path);
            ML_TRACE("State load ok: %s", pCandidate->Path);
            return TRUE;
        }

        bCorrupt = TRUE;
    }

    if (bCorrupt)
    {
        _MainLoopStateSetMessage(
            "Slot %d is incomplete or corrupt.",
            _MainLoop_StateSlot + 1
        );
    }
    else if (bWrongRom)
    {
        _MainLoopStateSetMessage(
            "Slot %d belongs to another ROM.",
            _MainLoop_StateSlot + 1
        );
    }
    else
    {
        _MainLoopStateSetMessage(
            "No state found in slot %d.",
            _MainLoop_StateSlot + 1
        );
    }

    ML_TRACE("State load failed: %s", _MainLoop_StateLastMessage);
    return FALSE;
}

Bool _MainLoopSaveState()
{
    MainLoopSegaStateScratchGuard SegaScratchGuard;
    Char Reason[192];
    Uint32 uRomCRC;
    Uint32 nRomBytes;
    Uint32 uRomFlags;
    Uint32 uGeneration = 1;
    Uint32 uPayloadCRC;
    Uint32 uStoredCRC;
    Uint32 nPayloadBytes;
    Uint32 ePayloadEncoding;
    const Uint8 *pPayload;
    mz_ulong nCompressedBytes;
    Bool bWrongRom = FALSE;
    Bool bCorrupt = FALSE;
    Int32 nAllCandidates;
    Int32 i;
    MainLoopStateRootT Roots[MAINLOOP_STATE_MAX_ROOTS];
    Int32 nRoots;
    Int32 iRoot;
    Uint8 *pStateData;
    Uint32 nStateBytes;

    _bStateSaved = FALSE;
    _MainLoop_StateUnformattedCard = -1;

    if (!_MainLoopStateCheckAvailability(Reason, sizeof(Reason)))
    {
        _MainLoopStateSetMessage("%s", Reason);
        return FALSE;
    }

    if (!_MainLoopStateGetRomIdentity(&uRomCRC, &nRomBytes, &uRomFlags))
    {
        _MainLoopStateSetMessage("Cannot identify the loaded ROM.");
        return FALSE;
    }

    /* Scan the selected target set for its next generation.  Keeping this
       scoped avoids loading optional MMCE/HDD modules when the user chose
       an unrelated explicit target such as USB. */
    nAllCandidates = _MainLoopStateScanCandidates(
        _MainLoop_StateDevice,
        _MainLoop_StateSlot,
        uRomCRC,
        nRomBytes,
        uRomFlags,
        &bWrongRom,
        &bCorrupt
    );
    for (i = 0; i < nAllCandidates; i++)
    {
        Uint32 uCandidateGeneration =
            _MainLoop_StateCandidates[i].Header.uGeneration;
        if (uGeneration == 1 ||
            _MainLoopStateGenerationNewer(uCandidateGeneration + 1, uGeneration))
        {
            uGeneration = uCandidateGeneration + 1;
            if (!uGeneration)
            {
                uGeneration = 1;
            }
        }
    }

    nRoots = _MainLoopStateBuildRoots(_MainLoop_StateDevice, Roots);

    /* Snapshot and compress exactly once while the game is paused. Older
       code repeated SaveState for every fallback root, then wrote the full
       ~500 KB structure. Fast deflate substantially cuts slow memory-card
       I/O while keeping the on-disk format backward compatible: version-1
       raw banks still load, and Reserved[0] advertises compressed banks. */
    pStateData = _MainLoopStateGetPayloadData();
    nStateBytes = _MainLoopStateGetPayloadBytes();
    if (!pStateData || !nStateBytes)
    {
        _MainLoopStateSetMessage(
            "Could not allocate core state buffer (%u bytes).",
            (unsigned)nStateBytes);
        return FALSE;
    }
    if (_pSystem == _pNes)
    {
        _pNes->SaveState(&_NesState);
        /* SNESTICLE_NES_CORE_STATE_MAGIC
         * Do not hard-code InfoNES's NSST payload magic here. Every NesSystem
         * implementation owns and validates its inner state format. Both the
         * InfoNES and QuickNES wrappers memset the envelope to zero first and
         * write a non-zero magic only after a complete snapshot succeeds. */
        if (_NesState.uMagic == 0)
        {
            _MainLoopStateSetMessage("Could not snapshot the NES core state.");
            return FALSE;
        }
    }
    else if (_pSystem == _pSega)
    {
        /* AURORA_PICODRIVE_STAGE2_STATE_SAVE */
        if (!_pSega->SaveStateChecked(pStateData, (Int32)nStateBytes))
        {
            _MainLoopStateSetMessage("Could not snapshot the PicoDrive state.");
            return FALSE;
        }
    }
    else if (_pSystem == _pPce)
    {
        if (!_pPce->SaveStateChecked(pStateData, (Int32)nStateBytes))
        {
            _MainLoopStateSetMessage("Could not snapshot the Beetle PCE Fast state.");
            return FALSE;
        }
    }
    else
    {
        _pSnes->SaveState(&_SnesState);
    }
    uPayloadCRC = (Uint32)mz_crc32(
        MZ_CRC32_INIT,
        pStateData,
        nStateBytes
    );

    pPayload = pStateData;
    nPayloadBytes = nStateBytes;
    ePayloadEncoding = MAINLOOP_STATE_PAYLOAD_RAW;
    {
        /* AURORA_PICODRIVE_STAGE2_SAVE_COMPRESSED */
        Uint32 nCompressionCapacity = _MainLoopStateCompressedLimit(nStateBytes);
        Uint32 nActualCapacity = 0;
        Uint8 *pCompression = _MainLoopStateGetCompressedBuffer(
            nCompressionCapacity, &nActualCapacity);
        nCompressedBytes = nCompressionCapacity;
        if (pCompression && nCompressionCapacity > 0 &&
            nActualCapacity >= nCompressionCapacity &&
            mz_compress2(
                pCompression,
                &nCompressedBytes,
                pStateData,
                nStateBytes,
                MZ_BEST_SPEED) == MZ_OK &&
            nCompressedBytes < nStateBytes)
        {
            pPayload = pCompression;
            nPayloadBytes = (Uint32)nCompressedBytes;
            ePayloadEncoding = MAINLOOP_STATE_PAYLOAD_DEFLATE;
        }
    }
    uStoredCRC = (Uint32)mz_crc32(
        MZ_CRC32_INIT,
        pPayload,
        nPayloadBytes
    );

    ML_TRACE(
        "State payload: raw=%u stored=%u encoding=%s",
        (unsigned int)nStateBytes,
        (unsigned int)nPayloadBytes,
        ePayloadEncoding == MAINLOOP_STATE_PAYLOAD_DEFLATE
            ? "deflate"
            : "raw"
    );

    for (iRoot = 0; iRoot < nRoots; iRoot++)
    {
        MainLoopStateFileHeaderT BankHeader[MAINLOOP_STATE_BANK_NUM];
        Int32 BankResult[MAINLOOP_STATE_BANK_NUM];
        Int32 iBank;
        Int32 iTargetBank;
        Char Path[1024];
        MainLoopStateFileHeaderT Header;

        if (!_MainLoopStateEnsureRoot(&Roots[iRoot]))
        {
            continue;
        }

        /* The generation scan above already opened every candidate header.
           Reuse those copies here instead of opening both target files a
           second time -- directory and file-open latency is noticeable on
           a PS2 memory card even when only 64 bytes are read. */
        for (iBank = 0; iBank < MAINLOOP_STATE_BANK_NUM; iBank++)
        {
            _MainLoopStateBuildBankPath(
                &Roots[iRoot],
                _MainLoop_StateSlot,
                iBank,
                Path,
                sizeof(Path)
            );
            BankResult[iBank] = 0;
            for (i = 0; i < nAllCandidates; i++)
            {
                if (!strcmp(Path, _MainLoop_StateCandidates[i].Path))
                {
                    BankHeader[iBank] =
                        _MainLoop_StateCandidates[i].Header;
                    BankResult[iBank] = 1;
                    break;
                }
            }
        }

        if (BankResult[0] == 1 && BankResult[1] == 1)
        {
            /* Overwrite the older bank and preserve the newest one. CRC is
               checked when loading, where a damaged newest bank naturally
               falls back to the other bank. Avoiding a full pre-save read
               is the main latency win on mc0:/mc1:. */
            iTargetBank = _MainLoopStateGenerationNewer(
                BankHeader[0].uGeneration,
                BankHeader[1].uGeneration
            ) ? 1 : 0;
        }
        else if (BankResult[0] == 1)
        {
            iTargetBank = 1;
        }
        else
        {
            iTargetBank = 0;
        }

        _MainLoopStateBuildBankPath(
            &Roots[iRoot],
            _MainLoop_StateSlot,
            iTargetBank,
            Path,
            sizeof(Path)
        );

        memset(&Header, 0, sizeof(Header));
        memcpy(Header.Magic, _MainLoop_StateMagic, sizeof(Header.Magic));
        Header.uVersion = MAINLOOP_STATE_FORMAT_VERSION;
        Header.nHeaderBytes = sizeof(Header);
        Header.nPayloadBytes = nPayloadBytes;
        Header.uPayloadCRC = uPayloadCRC;
        Header.uRomCRC = uRomCRC;
        Header.nRomBytes = nRomBytes;
        Header.uRomFlags = uRomFlags;
        Header.iSlot = (Uint32)_MainLoop_StateSlot;
        Header.uGeneration = uGeneration;
        Header.Reserved[0] = ePayloadEncoding;
        Header.Reserved[1] =
            ePayloadEncoding == MAINLOOP_STATE_PAYLOAD_DEFLATE
                ? uStoredCRC
                : 0;
        Header.Reserved[2] = _MainLoopStateGetSystemId();

        ML_TRACE("State save path: %s", Path);
        if (_MainLoopStateWriteBank(
                Path,
                &Header,
                pPayload,
                nPayloadBytes))
        {
            _bStateSaved = TRUE;
            _MainLoopStateSetMessage(
                "Saved slot %d to %s.",
                _MainLoop_StateSlot + 1,
                Roots[iRoot].DeviceName
            );
            ConPrint("State saved: %s\n", Path);
            ML_TRACE("State save ok: %s", Path);
            return TRUE;
        }

        ML_TRACE("State save failed: %s", Path);
    }

    if (_MainLoop_StateUnformattedCard >= 0)
    {
        _MainLoopStateSetMessage(
            "mc%d: is not formatted.",
            _MainLoop_StateUnformattedCard
        );
    }
    else
    {
        _MainLoopStateSetMessage(
            "Could not save slot %d to %s.",
            _MainLoop_StateSlot + 1,
            MainLoopStateGetDeviceName()
        );
    }
    return FALSE;
}


void _MainLoopResetHistory()
{
#if MAINLOOP_HISTORY
    _nHistory = 0;
#endif
}


void _MainLoopResetInputChecksums()
{
	_uInputFrame =0;
	memset(_uInputChecksum, 0, sizeof(_uInputChecksum));
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
