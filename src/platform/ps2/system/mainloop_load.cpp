#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#define NEWLIB_PORT_AWARE
#include <io_common.h>
#include <fileXio.h>
#include <fileXio_rpc.h>
#undef NEWLIB_PORT_AWARE
#include "types.h"
#include "console.h"
#include "file.h"
#include "dataio.h"
#include "pathext.h"
#include "snppucolor.h"
#include "emurom.h"
#include "mainloop.h"
#include "mainloop_shared.h"
/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827 */
#include "nes/quicknes/quicknes_bridge.h"
#include "nes/fceumm/fceumm_fds_bridge.h"
#include "sega/picodrive/picodrive_bridge.h"
/* AURORA_PCE_EXPERIMENTAL_V1 */
#include "pce/beetle/pce_bridge.h"
/* AURORA_SNES9X2010_V1 */
#include "snes/snes9x2010/snes9x2010_bridge.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "mainloop_bgm.h" /* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830 */
#include "snes.h"
#include "rendersurface.h"
#include "texture.h"
#include "audmixbuffer.h"
#include "emumovie.h"
#include "mainloop_load.h"
#include "mainloop_menu.h"
#include "embedded_irx.h"   /* HddMapPath (hdd0:/PART -> pfs0:) */
#include "sndbglog.h"

extern "C" {
#include "miniz.h"
#include "miniz_compat.h"
}

/* AURORA_SNES9X2010_V1
 * V1 intentionally does not alter video.cfg v37. This selector is runtime
 * only and defaults to the original SNESticle every boot. V2 will persist it. */
static MainLoopSnesCoreE s_MainLoopSnesCore = MAINLOOP_SNESCORE_SNESTICLE;

MainLoopSnesCoreE MainLoopSnesCoreGet()
{
#if AURORA_SNES9X2010 /* AURORA_SNES9X2010_BUILD_OPTION_V1_20260824 */
    return s_MainLoopSnesCore;
#else
    return MAINLOOP_SNESCORE_SNESTICLE;
#endif
}

void MainLoopSnesCoreSet(MainLoopSnesCoreE eCore)
{
#if AURORA_SNES9X2010 /* AURORA_SNES9X2010_BUILD_OPTION_V1_20260824 */
    if (eCore < MAINLOOP_SNESCORE_SNESTICLE || eCore >= MAINLOOP_SNESCORE_NUM)
        eCore = MAINLOOP_SNESCORE_SNESTICLE;
    s_MainLoopSnesCore = eCore;
#else
    (void)eCore;
    s_MainLoopSnesCore = MAINLOOP_SNESCORE_SNESTICLE;
#endif
}

void MainLoopSnesCoreCycleDir(Int32 dir)
{
#if AURORA_SNES9X2010 /* AURORA_SNES9X2010_BUILD_OPTION_V1_20260824 */
    if (dir == 0) return;
    MainLoopSnesCoreSet(
        s_MainLoopSnesCore == MAINLOOP_SNESCORE_SNESTICLE
            ? MAINLOOP_SNESCORE_SNES9X2010
            : MAINLOOP_SNESCORE_SNESTICLE);
#else
    (void)dir;
    s_MainLoopSnesCore = MAINLOOP_SNESCORE_SNESTICLE;
#endif
}

const Char *MainLoopSnesCoreGetName()
{
#if AURORA_SNES9X2010 /* AURORA_SNES9X2010_BUILD_OPTION_V1_20260824 */
    return s_MainLoopSnesCore == MAINLOOP_SNESCORE_SNES9X2010
        ? "SNES9x" : "SNESticle";
#else
    return "SNESticle";
#endif
}

/* AURORA_SNES9X2010_V3_MENU_BRIDGE_20260824
 * Stable integer wrappers keep uiVideo.cpp independent of the enum type. */
Int32 MainLoopSnesCoreGetPersisted()
{
    return (Int32)MainLoopSnesCoreGet();
}

void MainLoopSnesCoreSetPersisted(Int32 value)
{
    if (value < (Int32)MAINLOOP_SNESCORE_SNESTICLE ||
        value >= (Int32)MAINLOOP_SNESCORE_NUM)
        value = (Int32)MAINLOOP_SNESCORE_SNESTICLE;
    MainLoopSnesCoreSet((MainLoopSnesCoreE)value);
}


void _MainLoopGetName(Char *pName, const Char *pPath)
{
        const Char *pFileName;

        pFileName = strrchr(pPath, '/');
        if (pFileName==NULL)
        {
                pFileName = pPath;
        } else
        {
                // skip /
                pFileName = pFileName + 1;
        }
        strcpy(pName, pFileName);
}

/* AURORA_MD_PRELOAD_RASTER_PROBE_V2
 * Decide the 240p physical raster before PicoDrive is initialised.
 *
 * This is deliberately conservative. Known 8-bit/32X images stay on
 * Aurora's 256x240 raster. Standard MD images use 320x240. Ambiguous
 * .bin images get one additional 68000-vector probe (V4) before the safe
 * 256 fallback, so 240p does not unnecessarily lose PicoDrive's direct-T8
 * path while the same title can use it in 480i. */
static int _MainLoopAsciiLower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

static Bool _MainLoopSegaExtEquals(const char *pName, const char *pExt)
{
    const char *p;

    if (!pName || !pExt)
        return FALSE;

    p = strrchr(pName, '.');
    if (!p || !p[1])
        return FALSE;
    ++p;

    while (*p && *pExt)
    {
        if (_MainLoopAsciiLower((unsigned char)*p) !=
            _MainLoopAsciiLower((unsigned char)*pExt))
            return FALSE;
        ++p;
        ++pExt;
    }

    return (*p == '\0' && *pExt == '\0') ? TRUE : FALSE;
}

/* AURORA_MD_PRELOAD_VECTOR_PROBE_V4_2
 *
 * Mega Drive begins with big-endian 68000 reset vectors. A normal initial
 * stack pointer is in $FF0000-$FFFFFF, and the even reset PC points into the
 * cartridge image. This is only a fallback for ambiguous .bin content:
 * explicit SMS/GG/SG/SC/32X tests below still win first.
 */
static Uint32 _MainLoopReadBE32(const Uint8 *p)
{
    return ((Uint32)p[0] << 24) |
           ((Uint32)p[1] << 16) |
           ((Uint32)p[2] << 8)  |
           (Uint32)p[3];
}

static Bool _MainLoopLooksLikeMegaDriveVectors(
    const Uint8 *pData, Int32 nBytes, Uint32 uFileBase)
{
    Uint32 sp, pc, payloadBytes;

    if (!pData || nBytes <= 0 ||
        uFileBase > (Uint32)nBytes ||
        (Uint32)nBytes - uFileBase < 8U)
        return FALSE;

    payloadBytes = (Uint32)nBytes - uFileBase;
    sp = _MainLoopReadBE32(pData + uFileBase);
    pc = _MainLoopReadBE32(pData + uFileBase + 4U);

    if ((sp & 0xFFFF0000U) != 0x00FF0000U)
        return FALSE;

    if ((pc & 1U) != 0U || pc < 0x100U || pc >= payloadBytes)
        return FALSE;

    return TRUE;
}

/* AURORA_PCE_CDRDAO_TOC_SUPPORT_V4_8_20260830
 *
 * Load-time-only syntax sniff. cdrdao TOC and MMC CUE are different
 * grammars even when somebody has accidentally named the TOC ".cue".
 *
 * Strong TOC-only signatures:
 *   CD_ROM / CD_DA / CD_ROM_XA
 *   DATAFILE / AUDIOFILE / ZERO
 *   TRACK <format>        (cdrdao)
 * versus CUE:
 *   TRACK <number> <format>
 *
 * No frame-time checks and no HuCard path changes.
 */
static Bool _MainLoopLooksLikeCdrdaoToc(const char *pPath)
{
    FILE *fp;
    char line[1024];
    int lines = 0;

    if (!pPath || !*pPath)
        return FALSE;

    if (_MainLoopSegaExtEquals(pPath, "toc"))
        return TRUE;

    fp = fopen(pPath, "rb");
    if (!fp)
        return FALSE;

    while (lines++ < 48 && fgets(line, sizeof(line), fp))
    {
        char *p = line;
        char token[64];
        char *comment;

        if (lines == 1 &&
            (unsigned char)p[0] == 0xEF &&
            (unsigned char)p[1] == 0xBB &&
            (unsigned char)p[2] == 0xBF)
            p += 3;

        while (*p == ' ' || *p == '\t' ||
               *p == '\r' || *p == '\n')
            ++p;

        comment = strstr(p, "//");
        if (comment)
            *comment = '\0';

        if (!*p)
            continue;

        if (!strncasecmp(p, "CD_ROM", 6) &&
            (p[6] == '\0' || p[6] == ' ' || p[6] == '\t' ||
             p[6] == '\r' || p[6] == '\n'))
        {
            fclose(fp);
            return TRUE;
        }
        if (!strncasecmp(p, "CD_DA", 5) &&
            (p[5] == '\0' || p[5] == ' ' || p[5] == '\t' ||
             p[5] == '\r' || p[5] == '\n'))
        {
            fclose(fp);
            return TRUE;
        }
        if (!strncasecmp(p, "CD_ROM_XA", 9) &&
            (p[9] == '\0' || p[9] == ' ' || p[9] == '\t' ||
             p[9] == '\r' || p[9] == '\n'))
        {
            fclose(fp);
            return TRUE;
        }

        if (!strncasecmp(p, "DATAFILE", 8) &&
            (p[8] == ' ' || p[8] == '\t'))
        {
            fclose(fp);
            return TRUE;
        }
        if (!strncasecmp(p, "AUDIOFILE", 9) &&
            (p[9] == ' ' || p[9] == '\t'))
        {
            fclose(fp);
            return TRUE;
        }
        if (!strncasecmp(p, "ZERO", 4) &&
            (p[4] == ' ' || p[4] == '\t'))
        {
            fclose(fp);
            return TRUE;
        }

        if (!strncasecmp(p, "TRACK", 5) &&
            (p[5] == ' ' || p[5] == '\t'))
        {
            token[0] = '\0';
            if (sscanf(p + 5, "%63s", token) == 1 &&
                (token[0] < '0' || token[0] > '9'))
            {
                fclose(fp);
                return TRUE;
            }
        }
    }

    fclose(fp);
    return FALSE;
}

static Bool _MainLoopSegaWantsNative320(
    const Uint8 *pData, Int32 nBytes, const char *pName)
{
    static const Uint32 smsHeaderOffsets[] =
    {
        0x7ff0U, 0x3ff0U, 0x1ff0U
    };

    /* Explicit extensions can choose the raster before ROM allocation. */
    if (_MainLoopSegaExtEquals(pName, "sms") ||
        _MainLoopSegaExtEquals(pName, "gg")  ||
        _MainLoopSegaExtEquals(pName, "sg")  ||
        _MainLoopSegaExtEquals(pName, "sc")  ||
        _MainLoopSegaExtEquals(pName, "pco"))
        return FALSE;

    /* AURORA_SEGA_CD_32X_MD_SCALING_V2R1_20260828
     * 32X uses the same H32/H40 physical presentation as Mega Drive. */
    if (_MainLoopSegaExtEquals(pName, "md")  ||
        _MainLoopSegaExtEquals(pName, "gen") ||
        _MainLoopSegaExtEquals(pName, "smd") ||
        _MainLoopSegaExtEquals(pName, "32x"))
        return TRUE;

    if (!pData || nBytes <= 0)
        return FALSE;

    /* A .bin may actually be SMS/GG. Mirror PicoDrive's TMR SEGA test
     * before assuming Mega Drive. Also accept a conventional 0x200-byte
     * copier header when probing ambiguous .bin content. */
    for (unsigned i = 0;
         i < sizeof(smsHeaderOffsets) / sizeof(smsHeaderOffsets[0]);
         ++i)
    {
        Uint32 off = smsHeaderOffsets[i];

        if ((Uint32)nBytes >= off + 8U &&
            memcmp(pData + off, "TMR SEGA", 8) == 0)
            return FALSE;

        if ((Uint32)nBytes >= off + 0x200U + 8U &&
            memcmp(pData + off + 0x200U, "TMR SEGA", 8) == 0)
            return FALSE;
    }

    /* 32X/Pico use Mega-Drive-style headers too, so reject their explicit
     * signatures before the generic SEGA/vector tests. Check both raw and
     * conventional +0x200 header locations. */
    if ((nBytes >= 0x108 &&
         memcmp(pData + 0x100, "SEGA 32X", 8) == 0) ||
        (nBytes >= 0x308 &&
         memcmp(pData + 0x300, "SEGA 32X", 8) == 0))
        return FALSE;

    if ((nBytes >= 0x109 &&
         memcmp(pData + 0x100, "SEGA PICO", 9) == 0) ||
        (nBytes >= 0x309 &&
         memcmp(pData + 0x300, "SEGA PICO", 9) == 0))
        return FALSE;

    /* Normal commercial .bin MD dumps identify themselves here. */
    if (_MainLoopSegaExtEquals(pName, "bin") &&
        nBytes >= 0x104 &&
        (memcmp(pData + 0x100, "SEGA", 4) == 0 ||
         memcmp(pData + 0x100, " SEG", 4) == 0))
        return TRUE;

    /* AURORA_MD_PRELOAD_VECTOR_PROBE_V4_2
     *
     * Some valid/headerless MD .bin files do not carry the SEGA console
     * string. 480i can learn they are MD after PicoDrive starts and still
     * use direct T8; 240p must know before core init so its physical raster
     * is already 320 wide.
     *
     * Try raw 68000 vectors first. A conventional +0x200 copier header is
     * accepted only when the file-size shape also matches +0x200.
     * Ambiguous content that fails both probes deliberately stays 256/RGBA.
     */
    if (_MainLoopSegaExtEquals(pName, "bin"))
    {
        if (_MainLoopLooksLikeMegaDriveVectors(pData, nBytes, 0))
            return TRUE;

        if ((((Uint32)nBytes & 0x3FFFU) == 0x0200U) &&
            _MainLoopLooksLikeMegaDriveVectors(
                pData, nBytes, 0x200U))
        {
            return TRUE;
        }
    }

    /* Safe fallback: 256/RGBA is preferable to rebuilding the GS
     * after PicoDrive has already allocated its machine. */
    return FALSE;
}


int _MainLoopReadBinaryData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile)
{
        int fd;
        int total = 0;

        if (!pBuffer || nBufferBytes <= 0)
                return -1;

        fd = fileXioOpen(pRomFile, FIO_O_RDONLY, 0);
        if (fd < 0)
                return -1;

        /* AURORA_DYNAMIC_ROM_BUFFER_V1_20260823
         * fileXioRead may legally return short; finish the payload. */
        while (total < nBufferBytes)
        {
                int n = fileXioRead(fd, pBuffer + total, nBufferBytes - total);
                if (n < 0)
                {
                        fileXioClose(fd);
                        return -1;
                }
                if (n == 0)
                        break;
                total += n;
        }

        fileXioClose(fd);
        return total;
}

int _MainLoopReadGZData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile)
{
        return MinizReadGZToBuffer(pRomFile, pBuffer, nBufferBytes);
}

/* AURORA_PD_MEGA_FIX_20260820
 * ZIP selection must be stricter than "PathExtResolve knows the suffix".
 * Otherwise a recognised auxiliary file can win MinizReadZipFirstMatch()
 * before the actual cartridge. */
static int _MainLoopZipNameIsRom(const char *pName)
{
        PathExtTypeE eType;

        if (!pName || !PathExtResolve((char *)pName, &eType, FALSE))
                return 0;

        switch (eType)
        {
                case MAINLOOP_ENTRYTYPE_SNESROM:
                case MAINLOOP_ENTRYTYPE_NESROM:
                case MAINLOOP_ENTRYTYPE_NESFDSDISK:
                case MAINLOOP_ENTRYTYPE_NESFDSBIOS:
                case MAINLOOP_ENTRYTYPE_SEGAROM:
                case MAINLOOP_ENTRYTYPE_PCEROM:
                        return 1;
                default:
                        return 0;
        }
}


/* AURORA_DYNAMIC_ROM_BUFFER_V1_20260823 */
#define MAINLOOP_LEGACY_ROM_MAX_BYTES (8U * 1024U * 1024U + 1024U)
#define MAINLOOP_SEGA_ROM_MAX_BYTES   (16U * 1024U * 1024U)
#define MAINLOOP_PCE_ROM_MAX_BYTES    (4U * 1024U * 1024U + 512U)
#define MAINLOOP_SEGA_PROBE_BYTES     0x8200U
/* AURORA_FDS_V4_ZIP_LIMITS_20260828 */
#define MAINLOOP_FDS_SIDE_BYTES       65500U
#define MAINLOOP_FDS_MAX_SIDES        8U
#define MAINLOOP_FDS_ZIP_MIN_BYTES    MAINLOOP_FDS_SIDE_BYTES
#define MAINLOOP_FDS_ZIP_MAX_BYTES    (16U + MAINLOOP_FDS_MAX_SIDES * MAINLOOP_FDS_SIDE_BYTES)


/* AURORA_FDS_ZIP_FULLPATH_TMP_V2_20260828
 * This pinned FCEUmm advertises need_fullpath=true and retro_load_game()
 * calls FCEUI_LoadGame(game->path), ignoring game->data. ZIP therefore has
 * to become a real temporary .fds path for the lifetime of the loaded disk.
 */
static Char s_FdsZipTempPath[1024] = {0};

static void _MainLoopRemoveFdsZipTemp(void)
{
    if (s_FdsZipTempPath[0])
    {
        remove(s_FdsZipTempPath);
        s_FdsZipTempPath[0] = 0;
    }
}

static void _MainLoopFreeRomBuffer(void)
{
    if (_RomData)
        free(_RomData);
    _RomData = NULL;
    _RomDataCapacity = 0;
}

static Bool _MainLoopAllocRomBuffer(Uint32 capacity)
{
    Uint8 *p;
    if (capacity == 0 || _RomData || _RomDataCapacity)
        return FALSE;

    p = (Uint8 *)memalign(64, (size_t)capacity);
    if (!p)
        return FALSE;

    _RomData = p;
    _RomDataCapacity = capacity;
    return TRUE;
}

static Int32 _MainLoopGetBinarySize(const char *pRomFile)
{
    int fd = fileXioOpen(pRomFile, FIO_O_RDONLY, 0);
    int size;
    if (fd < 0)
        return -1;
    size = fileXioLseek(fd, 0, FIO_SEEK_END);
    fileXioClose(fd);
    return (size > 0) ? (Int32)size : -1;
}

static Int32 _MainLoopReadBinaryPrefix(
    Uint8 *pBuffer, Int32 nBufferBytes, const char *pRomFile)
{
    int fd, total = 0;
    if (!pBuffer || nBufferBytes <= 0)
        return -1;

    fd = fileXioOpen(pRomFile, FIO_O_RDONLY, 0);
    if (fd < 0)
        return -1;

    while (total < nBufferBytes)
    {
        int n = fileXioRead(fd, pBuffer + total, nBufferBytes - total);
        if (n <= 0)
            break;
        total += n;
    }
    fileXioClose(fd);
    return total;
}

static Uint32 _MainLoopRomPayloadLimit(PathExtTypeE eType)
{
    if (eType == MAINLOOP_ENTRYTYPE_SEGAROM) return MAINLOOP_SEGA_ROM_MAX_BYTES;
    if (eType == MAINLOOP_ENTRYTYPE_PCEROM) return MAINLOOP_PCE_ROM_MAX_BYTES;
    return MAINLOOP_LEGACY_ROM_MAX_BYTES;
}

static int _MainLoopZipDynamicEntryFilter(
    const char *pName, unsigned int nBytes)
{
    PathExtTypeE eType;
    if (!pName || !PathExtResolve((char *)pName, &eType, FALSE))
        return 0;

    switch (eType)
    {
        case MAINLOOP_ENTRYTYPE_SNESROM:
        case MAINLOOP_ENTRYTYPE_NESROM:
        case MAINLOOP_ENTRYTYPE_NESFDSBIOS:
            return nBytes <= MAINLOOP_LEGACY_ROM_MAX_BYTES;
        case MAINLOOP_ENTRYTYPE_NESFDSDISK:
            /* AURORA_FDS_V4_ZIP_FILTER_20260828 */
            return nBytes >= MAINLOOP_FDS_ZIP_MIN_BYTES &&
                   nBytes <= MAINLOOP_FDS_ZIP_MAX_BYTES;
        case MAINLOOP_ENTRYTYPE_SEGAROM:
            return nBytes <= MAINLOOP_SEGA_ROM_MAX_BYTES;
        case MAINLOOP_ENTRYTYPE_PCEROM:
            return nBytes <= MAINLOOP_PCE_ROM_MAX_BYTES;
        default:
            return 0;
    }
}

static void _MainLoopAbortPreCoreLoad(void)
{
    _MainLoopFreeRomBuffer();
    if (!MainLoopEnsureGameplayRasterWidth(256))
        printf("[video] warning: could not restore 256 raster after load failure\n");
}


int _MainLoopReadZipData(Uint8 *pBuffer, Int32 nBufferBytes, const char *pZipFile, char *pFileName)
{
        int nBytes;

        nBytes = MinizReadZipFirstMatch(
                pZipFile,
                pBuffer,
                nBufferBytes,
                pFileName,
                256,
                _MainLoopZipNameIsRom);

        if (nBytes > 0)
        {
                printf("ZIP: read %s (%d)\n", pFileName ? pFileName : "", nBytes);
        }
        else
        {
                printf("ZIP: no compatible entry in %s\n", pZipFile ? pZipFile : "");
        }
        return nBytes;
}

Bool _MainLoopLoadRomData(Emu::Rom *pRom, Uint8 *pRomData, Int32 nRomBytes)
{
        CMemFileIO romfile;
        Emu::Rom::LoadErrorE eError;

        // open memoryfile for rom data
        romfile.Open(pRomData, nRomBytes);

        // load rom
        /* AURORA_PICODRIVE_STAGE2_ROM_BUFFER: Sega attaches Aurora's
           already-loaded _RomData directly. No duplicate buffer and no
           in-place CMemFileIO memcpy. SNES/NES retain the old path. */
        if (pRom == _pSegaRom)
            eError = _pSegaRom->AttachBuffer(
                pRomData, (Uint32)nRomBytes, _RomDataCapacity);
        else if (pRom == _pPceRom)
            eError = _pPceRom->AttachBuffer(
                pRomData, (Uint32)nRomBytes, _RomDataCapacity);
        else if (pRom == _pSnes9x2010Rom)
            eError = _pSnes9x2010Rom->AttachBuffer(
                pRomData, (Uint32)nRomBytes, _RomDataCapacity);
        else
            eError = pRom->LoadRom(&romfile);
        romfile.Close();

        if (eError!=Emu::Rom::LoadErrorE::LOADERROR_NONE)
        {
                ConPrint("ERROR: loading rom %d\n", eError);
                return FALSE;
        }
        return TRUE;
}

Bool _MainLoopLoadBios(Emu::Rom *pRom, const Char *pFilePath)
{
        CFileIO romfile;
        Emu::Rom::LoadErrorE eError;

        // open memoryfile for rom data
        if (!romfile.Open(pFilePath, "rb"))
        {
                ConPrint("ERROR: loading fds bios!\n");
                return FALSE;
        }

        // load rom
        eError = pRom->LoadRom(&romfile);
        romfile.Close();

        if (eError!=Emu::Rom::LoadErrorE::LOADERROR_NONE)
        {
                ConPrint("ERROR: loading rom %d\n", eError);
                return FALSE;
        }
        return TRUE;
}

/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: shared standard NES .pal, exactly 64 RGB triples. */
Bool _MainLoopLoadNesPalette(const char *pFileName)
{
    Uint8 rgb[64 * 3], extra;
    FILE *fp;
    size_t got;
    if (!pFileName) return FALSE;
    fp = fopen(pFileName, "rb");
    if (!fp) { MainLoopModalPrintf(60 * 3, "ERROR: cannot open NES palette"); return FALSE; }
    got = fread(rgb, 1, sizeof(rgb), fp);
    if (got != sizeof(rgb) || fread(&extra, 1, 1, fp) != 0)
    {
        fclose(fp);
        MainLoopModalPrintf(60 * 3, "ERROR: NES .pal must be exactly 192 bytes");
        return FALSE;
    }
    fclose(fp);
    if (!QuicknesBridge_SetPalette(rgb) || !FceummFdsBridge_SetPalette(rgb))
    {
        MainLoopModalPrintf(60 * 3, "ERROR: could not apply NES palette");
        return FALSE;
    }
    MainLoopStatusPrintf(120, "NES palette loaded.");
    return TRUE;
}

Bool _MainLoopLoadSnesPalette(const char *pFileName)
{
        Uint32 *pPalData;
        pPalData = SNPPUColorGetPalette();

        return _MainLoopReadBinaryData((Uint8 *)pPalData, SNPPUCOLOR_NUM * sizeof(Uint32), pFileName) > 0;
}


/* AURORA_V4_12_PRIVATE_FILEXIO_CDDA_PCE_TOC2CUE_20260830 */
static char s_PceCdrdaoTempCue[1024];

void _MainLoopUnloadRom()
{
    /* AURORA_AUDIO_HARDCUT_ROM_UNLOAD_V1 */
    MainLoopAudioHardCut();

    /* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830
     * BgmStop intentionally retains libxmp for fast menu reopen.
     * A real game switch prefers maximum free EE heap. */
    BgmReleaseDecoderForGameSwitch();

    // stop recording if we are recording
    if (s_pMovieClip->IsRecording())
    {
        printf("Movie: Record End\n");
        s_pMovieClip->RecordEnd();
    } 
    // stop playing if we are playing
    if (s_pMovieClip->IsPlaying())
    {
        printf("Movie: Play End\n");
        s_pMovieClip->PlayEnd();
    } 
    /* V6.2: recordings belong to the departing ROM. Releasing their lazy
     * buffers here also guarantees maximum EE heap before the next core. */
    s_pMovieClip->Discard();

	// unload old rom
	_pSnes->SetRom(NULL);
	_pSnesRom->Unload();
	/* AURORA_SNES9X2010_V1 */
	if (_pSnes9x2010) _pSnes9x2010->SetRom(NULL);
	if (_pSnes9x2010Rom) _pSnes9x2010Rom->Unload();

	/* Phase 2: NES unload mirrors the SNES path. NesDisk is unloaded
	   even though disk-swap input is still gated for Phase 5 - the
	   wrapper itself exists and owns memory. */
	_pNes->SetRom(NULL);
	if (_pFds) _pFds->SetRom(NULL); /* AURORA_FCEUMM_FDS_V0_5_UNLOAD */
    /* AURORA_FDS_ZIP_FULLPATH_TMP_V2_20260828: FCEUmm has closed the
     * disk now, so the extracted full-path backing file is no longer needed. */
    _MainLoopRemoveFdsZipTemp();
	_pNesRom->Unload();
	_pNesFDSDisk->Unload();
	/* AURORA_PICODRIVE_STAGE2_UNLOAD: SetRom(NULL) fully deinitializes PicoDrive. */
	if (_pSega) _pSega->SetRom(NULL);
	if (_pSegaRom) _pSegaRom->Unload();
	if (_pPce) _pPce->SetRom(NULL);
	if (_pPceRom) _pPceRom->Unload();

    if (s_PceCdrdaoTempCue[0])
    {
        remove(s_PceCdrdaoTempCue);
        s_PceCdrdaoTempCue[0] = 0;
    }

    /* The cores are detached now; release ROM RAM before gsKit reallocates. */
    _MainLoopFreeRomBuffer();

    /* AURORA_MD_STABLE_RASTER_V2
     * Only rebuild the 240p GS after PicoDrive has been shut down.
     * Browser/SNES/NES/SMS/GG use Aurora's normal physical raster. */
    /* AURORA_GAME_SWITCH_CLEAR_V1_20260823
     * System-agnostic: every ROM switch starts from black presentation state.
     *
     * The raster helper re-uploads _fbTexture[0] after a gsKit rebuild.
     * Therefore clear BEFORE the rebuild, otherwise the previous game's last
     * frame gets copied into the new _OutTex and flashes during next boot. */
    if (_fbTexture[0]) _fbTexture[0]->Clear();
    if (_fbTexture[1]) _fbTexture[1]->Clear();

    /* Also cover same-raster switches where no GS rebuild occurs. */
    if (_fbTexture[0])
        TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));

    if (!MainLoopEnsureGameplayRasterWidth(256))
        printf("[video] warning: could not restore 256 raster after unload\n");

    _bStateSaved = FALSE;
    _pSystem = NULL;
    _RomPath[0] = 0;
    MainLoopStateOnRomChanged();

}

/* AURORA_V4_12_PRIVATE_FILEXIO_CDDA_PCE_TOC2CUE_20260830 */
typedef struct AuroraPceTocTrack
{
    char cueType[16];
    char dataFile[768];
    long fileOffsetBytes;
    long dataSectors;
    int hasOffset;
    int generatedPregap;
    int startSectors;
    int hasStart;
    int haveData;
} AuroraPceTocTrack;

static int _MainLoopPceParseMsf(const char *s)
{
    unsigned m, sec, f;
    if (!s || sscanf(s, "%u:%u:%u", &m, &sec, &f) != 3 ||
        sec > 59 || f > 74)
        return -1;
    return (int)((m * 60u + sec) * 75u + f);
}

static void _MainLoopPceFormatMsf(long sectors,
                                  char *out, size_t outBytes)
{
    long m, sec, f;
    if (sectors < 0) sectors = 0;
    m = sectors / (60 * 75);
    sectors %= 60 * 75;
    sec = sectors / 75;
    f = sectors % 75;
    snprintf(out, outBytes, "%02ld:%02ld:%02ld", m, sec, f);
}

static const char *_MainLoopPceTocToken(const char *p,
                                        char *out, size_t outBytes)
{
    size_t n = 0;
    if (!p || !out || !outBytes)
        return NULL;

    while (*p == ' ' || *p == '\t')
        ++p;

    if (*p == '"')
    {
        ++p;
        while (*p && *p != '"')
        {
            if (n + 1 < outBytes)
                out[n++] = *p;
            ++p;
        }
        if (*p == '"') ++p;
    }
    else
    {
        while (*p && *p != ' ' && *p != '\t' &&
               *p != '\r' && *p != '\n')
        {
            if (n + 1 < outBytes)
                out[n++] = *p;
            ++p;
        }
    }

    out[n] = 0;
    return p;
}

static Bool _MainLoopBuildPceCueFromCdrdao(
    const char *pTocPath,
    const char *pSystemDirectory,
    char *pOutCue,
    size_t nOutCue)
{
    AuroraPceTocTrack tr[99];
    FILE *in = NULL, *out = NULL;
    char line[1536];
    char commonFile[768];
    char binPath[1024];
    char tocDir[1024];
    int trackCount = 0;
    int active = -1;
    long expectedOffset = 0;      /* cdrdao virtual byte cursor */
    long generatedGapBytes = 0;
    long physicalCueSectors = 0;
    long actualBinBytes = -1;
    Bool compactGeneratedGaps = FALSE;
    const char *slash;
    int i;
    Bool ok = FALSE;

    memset(tr, 0, sizeof(tr));
    commonFile[0] = 0;

    if (!pTocPath || !*pTocPath ||
        !pSystemDirectory || !*pSystemDirectory ||
        !pOutCue || nOutCue < 32)
        return FALSE;

    in = fopen(pTocPath, "rb");
    if (!in)
        return FALSE;

    while (fgets(line, sizeof(line), in))
    {
        const char *p = line;
        char *comment = strstr(line, "//");
        char a0[768], a1[128], a2[128];

        if (comment) *comment = 0;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '\r' || *p == '\n')
            continue;

        if (!strncasecmp(p, "TRACK", 5) &&
            (p[5] == ' ' || p[5] == '\t'))
        {
            char fmt[64];
            p = _MainLoopPceTocToken(p + 5, fmt, sizeof(fmt));
            if (!p || trackCount >= 99)
                goto done;

            active = trackCount++;
            if (!strcasecmp(fmt, "AUDIO"))
                strcpy(tr[active].cueType, "AUDIO");
            else if (!strcasecmp(fmt, "MODE1_RAW"))
                strcpy(tr[active].cueType, "MODE1/2352");
            else if (!strcasecmp(fmt, "MODE2_RAW"))
                strcpy(tr[active].cueType, "MODE2/2352");
            else
                goto done;
            continue;
        }

        if (active < 0)
            continue;

        if ((!strncasecmp(p, "ZERO", 4) &&
             (p[4] == ' ' || p[4] == '\t')) ||
            (!strncasecmp(p, "SILENCE", 7) &&
             (p[7] == ' ' || p[7] == '\t')))
        {
            const char *q = strchr(p, ' ');
            int msf = -1;
            if (!q) q = strchr(p, '\t');
            if (!q) goto done;

            q = _MainLoopPceTocToken(q, a0, sizeof(a0));
            q = _MainLoopPceTocToken(q, a1, sizeof(a1));

            if (a1[0] && strchr(a1, ':'))
                msf = _MainLoopPceParseMsf(a1);
            else if (a0[0] && strchr(a0, ':'))
                msf = _MainLoopPceParseMsf(a0);

            if (msf < 0)
                goto done;
            tr[active].generatedPregap += msf;
            continue;
        }

        if (!strncasecmp(p, "START", 5) &&
            (p[5] == ' ' || p[5] == '\t'))
        {
            int msf;
            p = _MainLoopPceTocToken(p + 5, a0, sizeof(a0));
            msf = _MainLoopPceParseMsf(a0);
            if (msf < 0) goto done;
            tr[active].startSectors = msf;
            tr[active].hasStart = 1;
            continue;
        }

        if (!strncasecmp(p, "DATAFILE", 8) &&
            (p[8] == ' ' || p[8] == '\t'))
        {
            const char *q = _MainLoopPceTocToken(
                p + 8, a0, sizeof(a0));
            q = _MainLoopPceTocToken(q, a1, sizeof(a1));
            q = _MainLoopPceTocToken(q, a2, sizeof(a2));

            if (!a0[0])
                goto done;

            snprintf(tr[active].dataFile,
                     sizeof(tr[active].dataFile), "%s", a0);

            if (!commonFile[0])
                snprintf(commonFile, sizeof(commonFile), "%s", a0);
            else if (strcmp(commonFile, a0))
                goto done;

            if (a1[0] == '#')
            {
                char *endp = NULL;
                tr[active].fileOffsetBytes =
                    strtol(a1 + 1, &endp, 10);
                if (!endp || *endp || tr[active].fileOffsetBytes < 0)
                    goto done;
                tr[active].hasOffset = 1;
                tr[active].dataSectors = _MainLoopPceParseMsf(a2);
            }
            else
            {
                tr[active].fileOffsetBytes = expectedOffset;
                tr[active].hasOffset = 0;
                tr[active].dataSectors = _MainLoopPceParseMsf(a1);
            }

            if (tr[active].dataSectors <= 0)
                goto done;

            tr[active].haveData = 1;

            if (tr[active].hasOffset &&
                tr[active].fileOffsetBytes != expectedOffset)
                goto done;

            expectedOffset += tr[active].dataSectors * 2352L;
            continue;
        }

        if (!strncasecmp(p, "INDEX", 5) &&
            (p[5] == ' ' || p[5] == '\t'))
            goto done;
    }

    if (trackCount < 2 || !commonFile[0])
        goto done;

    for (i = 0; i < trackCount; ++i)
    {
        if (!tr[i].haveData)
            goto done;

        if (tr[i].generatedPregap < 0 ||
            tr[i].generatedPregap > tr[i].dataSectors)
            goto done;

        generatedGapBytes +=
            (long)tr[i].generatedPregap * 2352L;
    }

    for (i = 0; i < trackCount; ++i)
    {
        if (tr[i].generatedPregap && tr[i].hasStart &&
            tr[i].generatedPregap != tr[i].startSectors)
            goto done;
    }

    slash = strrchr(pTocPath, '/');
    if (strchr(commonFile, ':') || commonFile[0] == '/')
    {
        snprintf(binPath, sizeof(binPath), "%s", commonFile);
    }
    else
    {
        size_t dirLen;
        if (!slash)
            snprintf(tocDir, sizeof(tocDir), ".");
        else
        {
            dirLen = (size_t)(slash - pTocPath);
            if (dirLen >= sizeof(tocDir))
                goto done;
            memcpy(tocDir, pTocPath, dirLen);
            tocDir[dirLen] = 0;
        }

        if (snprintf(binPath, sizeof(binPath), "%s/%s",
                     tocDir, commonFile) >= (int)sizeof(binPath))
            goto done;
    }

    /* Decide from the real BIN size whether generated ZERO/SILENCE
     * sectors are physically stored or only exist in the logical TOC. */
    {
        FILE *bin = fopen(binPath, "rb");
        const long virtualBytes = expectedOffset;
        const long compactBytes = virtualBytes - generatedGapBytes;

        if (!bin)
            goto done;
        if (fseek(bin, 0, SEEK_END) != 0)
        {
            fclose(bin);
            goto done;
        }

        actualBinBytes = ftell(bin);
        fclose(bin);

        if (actualBinBytes == compactBytes)
            compactGeneratedGaps = TRUE;
        else if (actualBinBytes == virtualBytes)
            compactGeneratedGaps = FALSE;
        else
        {
            printf("[PCE/CD] cdrdao size mismatch: actual=%ld virtual=%ld compact=%ld\n",
                   actualBinBytes, virtualBytes, compactBytes);
            goto done;
        }

        printf("[PCE/CD] cdrdao geometry: actual=%ld virtual=%ld compact=%ld mode=%s\n",
               actualBinBytes, virtualBytes, compactBytes,
               compactGeneratedGaps ? "compact-gaps" : "stored-gaps");
    }

    if (snprintf(pOutCue, nOutCue,
                 "%s/__AURORA_PCE_CDRDAO_TMP__.cue",
                 pSystemDirectory) >= (int)nOutCue)
        goto done;

    remove(pOutCue);
    out = fopen(pOutCue, "wb");
    if (!out)
        goto done;

    fprintf(out, "FILE \"%s\" BINARY\n", binPath);

    /* AURORA_V4_15_PCE_CDRDAO_AUDIO_ENDIAN_20260830
     * Ordinary CUE has no flag for cdrdao raw AUDIO byte order.
     * cdrdao raw PCM is MSB-first; preserve it explicitly. */
    fprintf(out, "REM AURORA_CDRDAO_RAW_AUDIO_MSB_FIRST\n");

    physicalCueSectors = 0;
    for (i = 0; i < trackCount; ++i)
    {
        char msf0[32], msf1[32], pg[32];
        long index0 = physicalCueSectors;
        long index1 = index0;
        long storedSectors = tr[i].dataSectors;

        fprintf(out, "  TRACK %02d %s\n", i + 1, tr[i].cueType);

        if (tr[i].generatedPregap > 0)
        {
            if (compactGeneratedGaps)
            {
                _MainLoopPceFormatMsf(
                    tr[i].generatedPregap, pg, sizeof(pg));
                fprintf(out, "    PREGAP %s\n", pg);
                storedSectors -= tr[i].generatedPregap;
            }
            else
            {
                /* AURORA_V4_14_FILEXIO_OPENFLAG_PCE_STORED_PREGAPS_20260830
                 * Gap sectors are physically stored in this single BIN.
                 * Represent them as INDEX 00 -> INDEX 01. */
                index1 += tr[i].generatedPregap;
                _MainLoopPceFormatMsf(index0, msf0, sizeof(msf0));
                fprintf(out, "    INDEX 00 %s\n", msf0);
            }
        }
        else if (tr[i].hasStart && tr[i].startSectors > 0)
        {
            index1 += tr[i].startSectors;
            _MainLoopPceFormatMsf(index0, msf0, sizeof(msf0));
            fprintf(out, "    INDEX 00 %s\n", msf0);
        }

        if (storedSectors < 0)
            goto done;

        _MainLoopPceFormatMsf(index1, msf1, sizeof(msf1));
        fprintf(out, "    INDEX 01 %s\n", msf1);

        printf("[PCE/CD] cue track %02d: INDEX01=%s logical=%ld stored=%ld pregap=%d\n",
               i + 1, msf1, tr[i].dataSectors,
               storedSectors, tr[i].generatedPregap);

        physicalCueSectors += storedSectors;
    }

    if (physicalCueSectors * 2352L != actualBinBytes)
    {
        printf("[PCE/CD] generated CUE geometry mismatch: sectors=%ld bytes=%ld actual=%ld\n",
               physicalCueSectors,
               physicalCueSectors * 2352L,
               actualBinBytes);
        goto done;
    }

    if (fflush(out) != 0 || fclose(out) != 0)
    {
        out = NULL;
        remove(pOutCue);
        goto done;
    }
    out = NULL;

    printf("[PCE/CD] cdrdao single-BIN converted to CUE: %s -> %s\n",
           pTocPath, pOutCue);
    ok = TRUE;

done:
    if (out) fclose(out);
    if (in) fclose(in);
    if (!ok && pOutCue && *pOutCue)
    {
        remove(pOutCue);
        pOutCue[0] = 0;
    }
    return ok;
}

/* AURORA_SNES9X2010_V6_CD_SRAM_NOTICES_20260824
 * A CUE and its BIN/audio tracks stay on storage. This path allocates no
 * frontend ROM buffer, which is essential inside the EE's 32 MiB budget. */
static Bool _MainLoopExecuteDisc(const char *pMappedPath,
                                 const char *pOriginalPath,
                                 const char *pBasePath,
                                 Bool bLoadSRAM)
{
    Char SystemDirectory[512];
    Emu::System *pSystem = NULL;
    const char *pName;
    const char *pPceLoadPath = pMappedPath;
    int eDisc;
    Bool bLoaded;

    if (!pMappedPath || !*pMappedPath ||
        !pOriginalPath || !pBasePath)
        return FALSE;

    if (!MainLoopEnsureSystemDirectory(
            SystemDirectory, (Int32)sizeof(SystemDirectory)))
    {
        MainLoopModalPrintf(60 * 4,
            "ERROR: cannot create SNESticle/SYSTEM");
        return FALSE;
    }

    /* AURORA_PCE_CDRDAO_TOC_SUPPORT_V4_8_20260830
     * PicoDrive's classifier is intentionally CUE-only. A cdrdao TOC is a
     * PCE-supported image format and must reach Beetle without being parsed
     * by PicoDrive first. Mislabeled TOC-as-.cue is accepted by syntax too. */
    if (_MainLoopLooksLikeCdrdaoToc(pMappedPath))
        eDisc = 0;
    else
        eDisc = PicoDriveBridge_ProbeSegaCd(pMappedPath);

    if (eDisc < 0)
    {
        MainLoopModalPrintf(60 * 4,
            "ERROR: CUE/TOC or its first track is unreadable");
        return FALSE;
    }

    if (eDisc == 0 && _MainLoopLooksLikeCdrdaoToc(pMappedPath))
    {
        s_PceCdrdaoTempCue[0] = 0;
        if (_MainLoopBuildPceCueFromCdrdao(
                pMappedPath, SystemDirectory,
                s_PceCdrdaoTempCue,
                sizeof(s_PceCdrdaoTempCue)))
        {
            pPceLoadPath = s_PceCdrdaoTempCue;
        }
        else
        {
            pPceLoadPath = pMappedPath;
            printf("[PCE/CD] cdrdao TOC not convertible; using native parser\n");
        }
    }

    /* AURORA_SEGA_CD_32X_MD_SCALING_V2R1_20260828
     * Sega CD uses the same Genesis/MD H32/H40 output. Choose the native
     * 320-wide raster before PicoDrive allocates MCD; PCE CD stays 256. */
    /* AURORA_PCE_PRECORE512_V13_20260830
     * Sega CD keeps MD's 320 raster; PCE CD uses the same fixed 512 storage
     * raster as HuCards before Beetle is initialized. */
    if (!MainLoopEnsureGameplayRasterWidth(eDisc > 0 ? 320 : 512))
    {
        MainLoopModalPrintf(60 * 3,
            "ERROR: cannot configure CD video raster");
        return FALSE;
    }

    _MainLoop_fOutputIntensity = 1.0f;
    if (eDisc > 0)
    {
        pSystem = _pSega;
        PicoDriveBridge_SetRegion((int)g_SnesForceRegion);
        bLoaded = _pSega && _pSega->LoadDisc(
            pMappedPath, SystemDirectory);
    }
    else
    {
        pSystem = _pPce;
        bLoaded = _pPce && _pPce->LoadDisc(
            pPceLoadPath, SystemDirectory);
    }

    if (!bLoaded || !pSystem ||
        (pSystem == _pSega && !_pSega->IsRomReady()) ||
        (pSystem == _pPce && !_pPce->IsRomReady()))
    {
        _MainLoopUnloadRom();
        if (eDisc > 0)
            MainLoopModalPrintf(60 * 5,
                "ERROR: Sega CD failed; put its region BIOS .bin in SYSTEM");
        else
            MainLoopModalPrintf(60 * 5,
                "ERROR: PCE CD failed; put syscard3.pce in SYSTEM");
        return FALSE;
    }

    _pSystem = pSystem;
    pSystem->Reset();

    pName = strrchr(pBasePath, '/');
    if (!pName)
        pName = strrchr(pBasePath, '\\');
    pName = pName ? pName + 1 : pBasePath;
    snprintf(_RomName, sizeof(_RomName), "%s", pName);
    snprintf(_RomPath, sizeof(_RomPath), "%s", pOriginalPath);
    MainLoopStateOnRomChanged();

    ConPrint("CD Loaded: %s (%s)\n", pMappedPath,
             pSystem == _pSega ? "Sega CD" : "PC Engine CD");
    _MainLoopSetSampleRate(pSystem->GetSampleRate());
    if (bLoadSRAM)
        _MainLoopLoadSRAM();

    _fbTexture[0]->Clear();
    TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));
    return TRUE;
}


/* AURORA_FCEUMM_FDS_V0_5_PATH_BOOT
 * FCEUmm's pinned libretro frontend requires a real full path. Keep the FDS
 * image on storage and point the core at Aurora's existing SYSTEM directory.
 */
static Bool _MainLoopExecuteFdsPath(const char *pMappedPath,
                                    const char *pOriginalPath,
                                    Bool bLoadSRAM)
{
    Char SystemDirectory[512];
    Char BiosPath[1024];
    FILE *pBios;
    long nBiosBytes;

    if (!pMappedPath || !*pMappedPath || !pOriginalPath || !*pOriginalPath || !_pFds)
        return FALSE;

    if (!MainLoopEnsureGameplayRasterWidth(256))
    {
        MainLoopModalPrintf(60 * 3, "ERROR: cannot configure FDS video raster");
        return FALSE;
    }

    /* AURORA_SYSTEM_BIOS_PATH_FIX_V1_20260826
     * Prefer an already-existing disksys.rom, with mass0:/SNESticle/SYSTEM
     * first. Only if no BIOS is found do we fall back to ensuring the normal
     * firmware directory, so the subsequent error reports the intended path.
     */
    if (!MainLoopFindSystemFileDirectory(
            SystemDirectory, (Int32)sizeof(SystemDirectory), "disksys.rom") &&
        !MainLoopEnsureSystemDirectory(
            SystemDirectory, (Int32)sizeof(SystemDirectory)))
    {
        MainLoopModalPrintf(60 * 4, "ERROR: cannot create SNESticle/SYSTEM");
        return FALSE;
    }

    if (snprintf(BiosPath, sizeof(BiosPath), "%s/disksys.rom", SystemDirectory) >=
        (int)sizeof(BiosPath))
    {
        MainLoopModalPrintf(60 * 4, "ERROR: FDS BIOS path is too long");
        return FALSE;
    }

    printf("[FDS] BIOS probe: %s\n", BiosPath);
    pBios = fopen(BiosPath, "rb");
    if (!pBios)
    {
        printf("[FDS] BIOS fopen failed: %s\n", BiosPath);
        MainLoopModalPrintf(60 * 5, "ERROR: put disksys.rom in SNESticle/SYSTEM");
        return FALSE;
    }
    if (fseek(pBios, 0, SEEK_END) != 0)
    {
        fclose(pBios);
        MainLoopModalPrintf(60 * 5, "ERROR: cannot read SYSTEM/disksys.rom");
        return FALSE;
    }
    nBiosBytes = ftell(pBios);
    fclose(pBios);
    if (nBiosBytes != 8192)
    {
        MainLoopModalPrintf(60 * 5, "ERROR: disksys.rom must be exactly 8192 bytes");
        return FALSE;
    }

    _MainLoop_fOutputIntensity = 0.8f;
    if (!_pFds->LoadDisk(pMappedPath, SystemDirectory) || !_pFds->IsRomReady())
    {
        _MainLoopUnloadRom();
        MainLoopModalPrintf(60 * 5,
            "ERROR: FCEUmm could not boot FDS; check SYSTEM/disksys.rom");
        return FALSE;
    }

    _pSystem = _pFds;
    _pFds->Reset();
    _MainLoopGetName(_RomName, pOriginalPath);
    snprintf(_RomPath, sizeof(_RomPath), "%s", pOriginalPath);
    MainLoopStateOnRomChanged();
    _MainLoopSetSampleRate(_pFds->GetSampleRate());
    if (bLoadSRAM)
        _MainLoopLoadSRAM();

    if (_fbTexture[0]) _fbTexture[0]->Clear();
    if (_fbTexture[1]) _fbTexture[1]->Clear();
    if (_fbTexture[0]) TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));

    _MainLoop_iDisk = 0;
    _MainLoop_bDiskInserted = TRUE;
    ConPrint("FDS Loaded via FCEUmm: %s\n", pMappedPath);
    return TRUE;
}


/* AURORA_FDS_V4_ZIP_LOADER_20260828 */
/* AURORA_FDS_ZIP_FULLPATH_TMP_V2_20260828 */
static Bool _MainLoopExecuteFdsZip(const char *pZipPath,
                                   const char *pOriginalZipPath,
                                   const char *pMemberName,
                                   unsigned int uZipIndex,
                                   Int32 nExpectedBytes,
                                   Bool bLoadSRAM)
{
    Char SystemDirectory[512];
    Char BiosPath[1024];
    Char TempPath[1024];
    char LoadedName[512];
    FILE *pBios;
    FILE *pTemp;
    long nBiosBytes;
    Uint8 *pData;
    Int32 nRead;
    size_t nWritten;
    Bool bWriteOK;
    Bool bLoaded;

    if (!pZipPath || !*pZipPath ||
        !pOriginalZipPath || !*pOriginalZipPath ||
        !pMemberName || !*pMemberName || !_pFds ||
        nExpectedBytes < (Int32)MAINLOOP_FDS_ZIP_MIN_BYTES ||
        (Uint32)nExpectedBytes > MAINLOOP_FDS_ZIP_MAX_BYTES)
        return FALSE;

    if (!MainLoopEnsureGameplayRasterWidth(256))
        return FALSE;

    if (!MainLoopFindSystemFileDirectory(
            SystemDirectory, (Int32)sizeof(SystemDirectory), "disksys.rom") &&
        !MainLoopEnsureSystemDirectory(
            SystemDirectory, (Int32)sizeof(SystemDirectory)))
        return FALSE;

    if (snprintf(BiosPath, sizeof(BiosPath), "%s/disksys.rom",
                 SystemDirectory) >= (int)sizeof(BiosPath))
        return FALSE;

    pBios = fopen(BiosPath, "rb");
    if (!pBios)
    {
        MainLoopModalPrintf(60 * 5,
            "ERROR: put disksys.rom in SNESticle/SYSTEM");
        return FALSE;
    }
    if (fseek(pBios, 0, SEEK_END) != 0)
    {
        fclose(pBios);
        return FALSE;
    }
    nBiosBytes = ftell(pBios);
    fclose(pBios);
    if (nBiosBytes != 8192)
    {
        MainLoopModalPrintf(60 * 5,
            "ERROR: disksys.rom must be exactly 8192 bytes");
        return FALSE;
    }

    pData = (Uint8 *)memalign(64, (size_t)nExpectedBytes);
    if (!pData)
    {
        MainLoopModalPrintf(60 * 3,
            "ERROR: not enough memory for FDS ZIP");
        return FALSE;
    }

    LoadedName[0] = 0;
    nRead = MinizReadZipEntryToBuffer(
        pZipPath, uZipIndex, pData, nExpectedBytes,
        LoadedName, (int)sizeof(LoadedName));

    if (nRead != nExpectedBytes || strcmp(LoadedName, pMemberName) != 0)
    {
        free(pData);
        MainLoopModalPrintf(60 * 4,
            "ERROR: cannot extract complete FDS from ZIP");
        return FALSE;
    }

    if (snprintf(TempPath, sizeof(TempPath), "%s/__AURORA_FDS_TMP__.fds",
                 SystemDirectory) >= (int)sizeof(TempPath))
    {
        free(pData);
        return FALSE;
    }

    /* A previous crash may have left the temp file behind. It is never a
     * user's source image, so replacing this fixed private name is safe. */
    remove(TempPath);
    pTemp = fopen(TempPath, "wb");
    if (!pTemp)
    {
        free(pData);
        MainLoopModalPrintf(60 * 4,
            "ERROR: SYSTEM must be writable for FDS ZIP");
        return FALSE;
    }

    nWritten = fwrite(pData, 1, (size_t)nRead, pTemp);
    bWriteOK = (nWritten == (size_t)nRead && fflush(pTemp) == 0)
        ? TRUE : FALSE;
    if (fclose(pTemp) != 0)
        bWriteOK = FALSE;
    free(pData);

    if (!bWriteOK)
    {
        remove(TempPath);
        MainLoopModalPrintf(60 * 4,
            "ERROR: cannot write temporary FDS image");
        return FALSE;
    }

    _MainLoop_fOutputIntensity = 0.8f;
    bLoaded = _pFds->LoadDisk(TempPath, SystemDirectory);

    if (!bLoaded || !_pFds->IsRomReady())
    {
        _MainLoopUnloadRom();
        remove(TempPath);
        MainLoopModalPrintf(60 * 5,
            "ERROR: FCEUmm could not boot FDS from ZIP");
        return FALSE;
    }

    snprintf(s_FdsZipTempPath, sizeof(s_FdsZipTempPath), "%s", TempPath);

    _pSystem = _pFds;
    _pFds->Reset();
    _MainLoopGetName(_RomName, pMemberName);
    snprintf(_RomPath, sizeof(_RomPath), "%s", pOriginalZipPath);
    MainLoopStateOnRomChanged();
    _MainLoopSetSampleRate(_pFds->GetSampleRate());
    if (bLoadSRAM)
        _MainLoopLoadSRAM();

    if (_fbTexture[0]) _fbTexture[0]->Clear();
    if (_fbTexture[1]) _fbTexture[1]->Clear();
    if (_fbTexture[0])
        TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));

    _MainLoop_iDisk = 0;
    _MainLoop_bDiskInserted = TRUE;
    ConPrint("FDS Loaded from ZIP/fullpath: %s -> %s (%d bytes)\n",
             pZipPath, pMemberName, nRead);
    return TRUE;
}


Bool _MainLoopExecuteFile(const char *pFileName, Bool bLoadSRAM)
{
    PathExtTypeE eType, eSourceType;
    Emu::Rom *pRom = NULL;
    Emu::System *pSystem = NULL;
    Emu::Rom *pBios = NULL;
    char FileName[1024], OriginalPath[1024], SegaContentName[1024];
    char hddPath[1024], ZipMemberName[512];
    unsigned int uZipIndex = 0;
    Bool bZipIndexValid = FALSE;
    Int32 nExpectedRomBytes = 0, nRomBytes = 0;
    Uint32 uRomIdentityCRC = 0;
#if SNDBG_LOG
    Uint32 uRomCRC = 0;
#endif

    if (!pFileName)
        return FALSE;

    snprintf(OriginalPath, sizeof(OriginalPath), "%s", pFileName);
    if (HddMapPath(pFileName, hddPath, sizeof(hddPath)) == 1)
        pFileName = hddPath;

    snprintf(FileName, sizeof(FileName), "%s", pFileName);
    snprintf(SegaContentName, sizeof(SegaContentName), "%s", pFileName);
    ZipMemberName[0] = 0;

    if (!PathExtResolve(FileName, &eType, TRUE))
        return FALSE;

    if (eType == MAINLOOP_ENTRYTYPE_SNESPALETTE)
        return _MainLoopLoadSnesPalette(pFileName);
    if (eType == MAINLOOP_ENTRYTYPE_NESPALETTE)
        return _MainLoopLoadNesPalette(pFileName); /* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827 */

    /* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830
     * Defensive guard for non-browser launch paths. */
    if (MainLoopSramSaveBusy())
        return FALSE;

    _MainLoopUnloadRom();

#if MAINLOOP_HISTORY
    _MainLoopResetHistory();
#endif
    _MainLoopResetInputChecksums();

    /* AURORA_FCEUMM_FDS_V0_5_PATH_BOOT: only a plain .fds full path enters FCEUmm. */
    if (eType == MAINLOOP_ENTRYTYPE_NESFDSDISK)
        return _MainLoopExecuteFdsPath(pFileName, OriginalPath, bLoadSRAM);

    /* AURORA_SNES9X2010_V6_CD_SRAM_NOTICES_20260824: never size/read a CUE or its tracks as a cartridge. */
    if (eType == MAINLOOP_ENTRYTYPE_CDIMAGE)
        return _MainLoopExecuteDisc(
            pFileName, OriginalPath, FileName, bLoadSRAM);

    eSourceType = eType;

    /* Preflight size/name with no large frontend ROM allocation. */
    if (eSourceType == MAINLOOP_ENTRYTYPE_GZ)
    {
        snprintf(SegaContentName, sizeof(SegaContentName), "%s", FileName);
        if (!PathExtResolve(FileName, &eType, TRUE))
            return FALSE;
        nExpectedRomBytes = MinizGetGZUncompressedSize(pFileName);
    }
    else if (eSourceType == MAINLOOP_ENTRYTYPE_ZIP)
    {
        nExpectedRomBytes = MinizProbeZipFirstMatchInfo(
            pFileName, &uZipIndex,
            ZipMemberName, (int)sizeof(ZipMemberName),
            _MainLoopZipDynamicEntryFilter);

        if (nExpectedRomBytes > 0)
        {
            bZipIndexValid = TRUE;
            snprintf(SegaContentName, sizeof(SegaContentName),
                     "%s", ZipMemberName);
            snprintf(FileName, sizeof(FileName), "%s", ZipMemberName);
            if (!PathExtResolve(FileName, &eType, TRUE))
                return FALSE;
        }
    }
    else
    {
        nExpectedRomBytes = _MainLoopGetBinarySize(pFileName);
    }

    /* AURORA_FDS_V4_ZIP_DISPATCH_20260828 */
    if (eType == MAINLOOP_ENTRYTYPE_NESFDSDISK)
    {
        if (eSourceType == MAINLOOP_ENTRYTYPE_ZIP && bZipIndexValid)
            return _MainLoopExecuteFdsZip(
                pFileName, OriginalPath, ZipMemberName,
                uZipIndex, nExpectedRomBytes, bLoadSRAM);

        MainLoopModalPrintf(60 * 4,
            "FDS in GZ is not supported; use plain .fds or ZIP");
        return FALSE;
    }

    if (nExpectedRomBytes <= 0 ||
        (Uint32)nExpectedRomBytes > _MainLoopRomPayloadLimit(eType))
    {
        MainLoopModalPrintf(60 * 3, "ERROR: ROM is too large or unreadable");
        return FALSE;
    }

    switch (eType)
    {
        case MAINLOOP_ENTRYTYPE_NESROM:
            pSystem = _pNes; pRom = _pNesRom; pBios = NULL;
            _MainLoop_fOutputIntensity = 0.8f;
            break;
        case MAINLOOP_ENTRYTYPE_NESFDSDISK:
            pSystem = _pNes; pRom = _pNesFDSDisk; pBios = _pNesFDSBios;
            _MainLoop_fOutputIntensity = 0.8f;
            break;
        case MAINLOOP_ENTRYTYPE_NESFDSBIOS:
            pSystem = _pNes; pRom = NULL; pBios = _pNesFDSBios;
            _MainLoop_fOutputIntensity = 0.8f;
            break;
        case MAINLOOP_ENTRYTYPE_SEGAROM:
            pSystem = _pSega; pRom = _pSegaRom; pBios = NULL;
            _MainLoop_fOutputIntensity = 1.0f;
            break;
        case MAINLOOP_ENTRYTYPE_SNESROM:
            /* AURORA_SNES9X2010_V1 */
            if (MainLoopSnesCoreGet() == MAINLOOP_SNESCORE_SNES9X2010)
            {
                pSystem = _pSnes9x2010; pRom = _pSnes9x2010Rom; pBios = NULL;
            }
            else
            {
                pSystem = _pSnes; pRom = _pSnesRom; pBios = NULL;
            }
            _MainLoop_fOutputIntensity = 1.0f;
            break;
        case MAINLOOP_ENTRYTYPE_PCEROM:
            pSystem = _pPce; pRom = _pPceRom; pBios = NULL;
            _MainLoop_fOutputIntensity = 1.0f;
            break;
        default:
            return FALSE;
    }

    /* Resolve 240p physical raster before final ROM RAM is committed. */
    {
        Int32 rasterWidth = 256;

        /* AURORA_PCE_PRECORE512_V13_20260830
         * PCE Fast host surface has a 512-pixel pitch and may expose
         * 256/352/512 visible widths. Enter the core with the GS already on
         * one aligned 512-wide storage raster; later presentation selects only
         * the visible window, never rebuilds to a non-64-aligned width. */
        if (eType == MAINLOOP_ENTRYTYPE_PCEROM)
            rasterWidth = 512;

        if (eType == MAINLOOP_ENTRYTYPE_SEGAROM)
        {
            Bool native320 = FALSE;

            if (_MainLoopSegaExtEquals(SegaContentName, "bin"))
            {
                Uint32 nProbe = (Uint32)nExpectedRomBytes;
                Uint8 *pProbe;
                Int32 got;

                if (nProbe > MAINLOOP_SEGA_PROBE_BYTES)
                    nProbe = MAINLOOP_SEGA_PROBE_BYTES;

                pProbe = (Uint8 *)malloc((size_t)nProbe);
                if (!pProbe)
                {
                    MainLoopModalPrintf(60 * 3,
                        "ERROR: not enough memory for ROM probe");
                    return FALSE;
                }

                if (eSourceType == MAINLOOP_ENTRYTYPE_GZ)
                    got = MinizReadGZPrefix(
                        pFileName, pProbe, (Int32)nProbe);
                else if (eSourceType == MAINLOOP_ENTRYTYPE_ZIP)
                    got = bZipIndexValid
                        ? MinizReadZipEntryPrefix(
                            pFileName, uZipIndex, pProbe, (Int32)nProbe)
                        : -1;
                else
                    got = _MainLoopReadBinaryPrefix(
                        pProbe, (Int32)nProbe, pFileName);

                if (got != (Int32)nProbe)
                {
                    free(pProbe);
                    MainLoopModalPrintf(60 * 3,
                        "ERROR: cannot probe Sega ROM");
                    return FALSE;
                }

                native320 = _MainLoopSegaWantsNative320(
                    pProbe, nExpectedRomBytes, SegaContentName);
                free(pProbe);
            }
            else
            {
                native320 = _MainLoopSegaWantsNative320(
                    NULL, nExpectedRomBytes, SegaContentName);
            }

            if (native320)
                rasterWidth = 320;
        }

        if (!MainLoopEnsureGameplayRasterWidth(rasterWidth))
        {
            if (rasterWidth != 256)
                MainLoopEnsureGameplayRasterWidth(256);
            MainLoopModalPrintf(60 * 3,
                "ERROR: cannot configure video raster");
            return FALSE;
        }
    }

    /* Allocate exact frontend backing policy for this cartridge. */
    {
        size_t required;
        if (eType == MAINLOOP_ENTRYTYPE_SEGAROM)
            required = PicoDriveBridge_RequiredRomCapacity(
                (size_t)nExpectedRomBytes);
        else
            required = (size_t)nExpectedRomBytes + 1024U;

        if (required < (size_t)nExpectedRomBytes ||
            required > 0xFFFFFFFFU ||
            !_MainLoopAllocRomBuffer((Uint32)required))
        {
            _MainLoopAbortPreCoreLoad();
            MainLoopModalPrintf(60 * 3,
                "ERROR: not enough memory for ROM");
            return FALSE;
        }
    }

    if (eSourceType == MAINLOOP_ENTRYTYPE_GZ)
    {
        nRomBytes = _MainLoopReadGZData(
            _RomData, (Int32)_RomDataCapacity, pFileName);
    }
    else if (eSourceType == MAINLOOP_ENTRYTYPE_ZIP)
    {
        char loadedName[512];
        loadedName[0] = 0;
        nRomBytes = bZipIndexValid
            ? MinizReadZipEntryToBuffer(
                pFileName, uZipIndex,
                _RomData, (Int32)_RomDataCapacity,
                loadedName, (int)sizeof(loadedName))
            : -1;
        if (nRomBytes > 0 &&
            strcmp(loadedName, ZipMemberName) != 0)
            nRomBytes = -1;
    }
    else
    {
        nRomBytes = _MainLoopReadBinaryData(
            _RomData, (Int32)_RomDataCapacity, pFileName);
    }

    if (nRomBytes != nExpectedRomBytes)
    {
        _MainLoopAbortPreCoreLoad();
        MainLoopModalPrintf(60 * 3, "ERROR: Cannot read complete ROM");
        return FALSE;
    }

    /* Save-state identity: same pristine payload boundary as before. */
    uRomIdentityCRC = (Uint32)mz_crc32(
        MZ_CRC32_INIT, _RomData, (size_t)nRomBytes);
#if SNDBG_LOG
    uRomCRC = (Uint32)mz_crc32(
        MZ_CRC32_INIT, _RomData, (size_t)nRomBytes);
#endif

    {
        Int32 guard = (Int32)(_RomDataCapacity - (Uint32)nRomBytes);
        if (guard > 1024) guard = 1024;
        if (guard > 0)
            memset(_RomData + nRomBytes, 0, (size_t)guard);
    }

    printf("ROM data read: %s (%d/%u bytes backing)\n",
           pFileName, nRomBytes, (unsigned)_RomDataCapacity);
    _MainLoopGetName(_RomName, FileName);
    printf("ROMName: '%s'\n", _RomName);

    if (pBios)
    {
        if (pRom == NULL)
        {
            if (!_MainLoopLoadBios(pBios, pFileName))
            {
                _MainLoopUnloadRom();
                MainLoopModalPrintf(60 * 5,
                    "ERROR: Cannot load disksys.rom");
                return FALSE;
            }
        }
        else if (!pBios->IsLoaded())
        {
            char diskrompath[1024];
            Char *pDiskFileName;

            snprintf(diskrompath, sizeof(diskrompath), "%s", FileName);
            pDiskFileName = strrchr(diskrompath, '/');
            if (!pDiskFileName)
                pDiskFileName = strrchr(diskrompath, ':');
            if (!pDiskFileName)
            {
                _MainLoopUnloadRom();
                return FALSE;
            }

            strcpy(pDiskFileName + 1, "disksys.rom");
            printf("FDSRom: '%s'\n", diskrompath);

            if (!_MainLoopLoadBios(pBios, diskrompath))
            {
                _MainLoopUnloadRom();
                MainLoopModalPrintf(60 * 5,
                    "ERROR: Cannot load disksys.rom");
                return FALSE;
            }
        }
    }

    if (pRom && !_MainLoopLoadRomData(pRom, _RomData, nRomBytes))
    {
        _MainLoopUnloadRom();
        return FALSE;
    }

    if (pBios)
    {
        pSystem->SetRom(pBios);
        if (pSystem == _pNes)
            _pNes->SetNesDisk(_pNesFDSDisk);
        else
            _pSnes->SetSnesRom(_pSnesRom);
    }
    else
    {
        if (pSystem == _pSega && _pSegaRom)
        {
            _pSegaRom->SetSourceName(SegaContentName);
            PicoDriveBridge_SetRegion((int)g_SnesForceRegion);
        }
        else if (pSystem == _pPce && _pPceRom)
            _pPceRom->SetSourceName(SegaContentName);
        else if (pSystem == _pSnes9x2010 && _pSnes9x2010Rom)
            _pSnes9x2010Rom->SetSourceName(SegaContentName);
        pSystem->SetRom(pRom);
    }

    pSystem->Reset();

    if (pSystem == _pNes && !_pNes->IsRomReady())
    {
        printf("[QuickNES] ROM rejected; aborting launch before mainloop\n");
        _MainLoopUnloadRom();
        MainLoopModalPrintf(60 * 3,
            "ERROR: QuickNES cannot run this NES image (format/mapper/FDS)");
        return FALSE;
    }

    if (pSystem == _pSega && !_pSega->IsRomReady())
    {
        printf("[PicoDrive] ROM rejected; aborting launch before mainloop\n");
        _MainLoopUnloadRom();
        MainLoopModalPrintf(60 * 3,
            "ERROR: PicoDrive cannot run this SEGA image");
        return FALSE;
    }

    if (pSystem == _pPce && !_pPce->IsRomReady())
    {
        printf("[PCE] ROM rejected; aborting launch before mainloop\n");
        _MainLoopUnloadRom();
        MainLoopModalPrintf(60 * 3, "ERROR: Beetle PCE Fast cannot run this HuCard");
        return FALSE;
    }

    if (pSystem == _pSnes9x2010 && !_pSnes9x2010->IsRomReady())
    {
        printf("[Snes9x2010] ROM rejected; aborting launch before mainloop\n");
        _MainLoopUnloadRom();
        MainLoopModalPrintf(60 * 3, "ERROR: Snes9x 2010 cannot run this SNES image");
        return FALSE;
    }

    _pSystem = pSystem;
    snprintf(_RomPath, sizeof(_RomPath), "%s", OriginalPath);
    MainLoopStateOnRomChanged();
    MainLoopStatePrimeRomIdentityCRC(uRomIdentityCRC);

    /* AURORA_SNES9X2010_V2_PS2LEAN_20260824
     * Snes9x LoadROM owns a complete native Memory.ROM copy now. Release the
     * pristine Aurora buffer immediately; save-state identity already has its
     * CRC and the wrapper keeps byte count/name without retaining the bytes. */
    if (pSystem == _pSnes9x2010 && _pSnes9x2010Rom && _RomData)
    {
        Uint32 released = _RomDataCapacity;
        _pSnes9x2010Rom->DetachFrontendBacking();
        _MainLoopFreeRomBuffer();
        printf("[Snes9x2010] released frontend ROM backing: %u KiB\n",
               (unsigned)(released >> 10));
    }

    ConPrint("ROM Loaded: %s\n", pFileName);

    if (pRom)
    {
        int nRegions, iRegion;
        Char *pRomTitle = pRom->GetRomTitle();
        Char *pRomMapper = pRom->GetMapperName();

        if (pRomMapper && !strcmp(pRomMapper, "<unknown>"))
            MainLoopModalPrintf(60, "WARNING: Unsupported NES Mapper");
        if (pRomTitle)
            printf("Rom Title: %s\n", pRomTitle);

#if SNDBG_LOG
        DLog("[rom] file='%s' bytes=%d crc32=%08X title='%s' mapper='%s'",
             _RomName, (int)nRomBytes, (unsigned)uRomCRC,
             pRomTitle ? pRomTitle : "<none>",
             pRomMapper ? pRomMapper : "<none>");
#endif

        nRegions = pRom->GetNumRomRegions();
        for (iRegion = 0; iRegion < nRegions; ++iRegion)
            printf("%s: %d bytes\n",
                   pRom->GetRomRegionName(iRegion),
                   pRom->GetRomRegionSize(iRegion));
    }

    _MainLoopSetSampleRate(pSystem->GetSampleRate());
    if (bLoadSRAM)
        _MainLoopLoadSRAM();

    _fbTexture[0]->Clear();
    TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));

    if (eType == MAINLOOP_ENTRYTYPE_NESFDSDISK)
    {
        _MainLoop_iDisk = 0;
        _MainLoop_bDiskInserted = TRUE;
    }
    return TRUE;
}

void _MainLoopSetSampleRate(Uint32 uSampleRate)
{
    _AudMix->SetSampleRate(uSampleRate);
}


/* AURORA_PCE_PRECORE512_V13_20260830 */

/* AURORA_PCE_CDRDAO_TOC_SUPPORT_V4_8_20260830 */

/* AURORA_V4_12_PRIVATE_FILEXIO_CDDA_PCE_TOC2CUE_20260830 */

/* AURORA_V4_13_CDDA_STARVATION_PCE_TRACK_GEOMETRY_20260830 */

/* AURORA_V4_14_FILEXIO_OPENFLAG_PCE_STORED_PREGAPS_20260830 */

/* AURORA_V4_14_1_RESUME_COMPILEFIX_20260830 */

/* AURORA_V4_15_PCE_CDRDAO_AUDIO_ENDIAN_20260830 */

/* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830 */
