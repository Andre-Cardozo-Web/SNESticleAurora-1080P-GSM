#include <string.h>
#include <stdio.h>
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
#include "sega/picodrive/picodrive_bridge.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
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

static Bool _MainLoopSegaWantsNative320(
    const Uint8 *pData, Int32 nBytes, const char *pName)
{
    static const Uint32 smsHeaderOffsets[] =
    {
        0x7ff0U, 0x3ff0U, 0x1ff0U
    };

    if (!pData || nBytes <= 0)
        return FALSE;

    /* Explicit non-MD extensions always keep Aurora's 256 raster. */
    if (_MainLoopSegaExtEquals(pName, "sms") ||
        _MainLoopSegaExtEquals(pName, "gg")  ||
        _MainLoopSegaExtEquals(pName, "sg")  ||
        _MainLoopSegaExtEquals(pName, "sc")  ||
        _MainLoopSegaExtEquals(pName, "32x") ||
        _MainLoopSegaExtEquals(pName, "pco"))
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

    if (_MainLoopSegaExtEquals(pName, "md")  ||
        _MainLoopSegaExtEquals(pName, "gen") ||
        _MainLoopSegaExtEquals(pName, "smd"))
        return TRUE;

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
        int nBytes;

        /* One fileXio read becomes one EE->IOP RPC; the IOP-side fileXio
           server chunks and DMA-copies the request internally until EOF.
           newlib fread() uses its own small buffering and is substantially
           slower for multi-megabyte ROMs, especially on cdfs/mass/SMB. */
        /* fileXioOpen() is an IOP API, so it must receive the IOP/FIO flag.
           newlib's O_RDONLY is zero; passing it directly makes drivers such
           as cdfs reject every ROM before the first byte is read. */
        fd = fileXioOpen(pRomFile, FIO_O_RDONLY, 0);
        if (fd < 0)
        {
                return -1;
        }

        nBytes = fileXioRead(fd, pBuffer, nBufferBytes);
        fileXioClose(fd);

        return nBytes;
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
                        return 1;
                default:
                        return 0;
        }
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
            eError = _pSegaRom->AttachBuffer(pRomData, (Uint32)nRomBytes);
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

Bool _MainLoopLoadSnesPalette(const char *pFileName)
{
        Uint32 *pPalData;
        pPalData = SNPPUColorGetPalette();

        return _MainLoopReadBinaryData((Uint8 *)pPalData, SNPPUCOLOR_NUM * sizeof(Uint32), pFileName) > 0;
}


void _MainLoopUnloadRom()
{
    /* AURORA_AUDIO_HARDCUT_ROM_UNLOAD_V1 */
    MainLoopAudioHardCut();


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

	// unload old rom
	_pSnes->SetRom(NULL);
	_pSnesRom->Unload();

	/* Phase 2: NES unload mirrors the SNES path. NesDisk is unloaded
	   even though disk-swap input is still gated for Phase 5 - the
	   wrapper itself exists and owns memory. */
	_pNes->SetRom(NULL);
	_pNesRom->Unload();
	_pNesFDSDisk->Unload();
	/* AURORA_PICODRIVE_STAGE2_UNLOAD: SetRom(NULL) fully deinitializes PicoDrive. */
	if (_pSega) _pSega->SetRom(NULL);
	if (_pSegaRom) _pSegaRom->Unload();

    /* AURORA_MD_STABLE_RASTER_V2
     * Only rebuild the 240p GS after PicoDrive has been shut down.
     * Browser/SNES/NES/SMS/GG use Aurora's normal physical raster. */
    if (!MainLoopEnsureGameplayRasterWidth(256))
        printf("[video] warning: could not restore 256 raster after unload\n");

    _bStateSaved = FALSE;
    _pSystem = NULL;
    _RomPath[0] = 0;
    MainLoopStateOnRomChanged();

	_fbTexture[0]->Clear();
	_fbTexture[1]->Clear();
}


Bool _MainLoopExecuteFile(const char *pFileName, Bool bLoadSRAM)
{
	PathExtTypeE eType;
	Emu::Rom *pRom = NULL;
	Emu::System *pSystem = NULL;
	Emu::Rom *pBios = NULL;
	/* CBrowserScreen now builds paths into a 1024-byte buffer (m_Dir up
	   to 512 + a per-entry name up to 255), so the bespoke copy that
	   _MainLoopExecuteFile keeps for PathExtResolve()'s in-place
	   truncation has to match that size. Otherwise a long ROM path
	   silently overflows the old FileName[256] in strcpy() below. */
	char FileName[1024];
	char OriginalPath[1024];
	/* AURORA_PICODRIVE_STAGE2_CONTENT_NAME */
	char SegaContentName[1024];

	if (pFileName==NULL)
	{
		return FALSE;
	}

	/* Keep the browser-facing path as well as the mapped I/O path.  For an
	   internal-HDD ROM this preserves hdd0:/PARTITION/... so save states
	   can remount that exact APA partition later; pFileName itself becomes
	   pfs0:/... below and no longer contains the partition identity. */
	snprintf(OriginalPath, sizeof(OriginalPath), "%s", pFileName);

	/* HD interno (APA): traduz "hdd0:/PARTICAO/.../rom" -> "pfs0:/.../rom"
	   (monta a particao em pfs0:).  Para os demais dispositivos e' no-op,
	   entao pFileName segue inalterado. */
	char hddPath[1024];
	if (HddMapPath(pFileName, hddPath, sizeof(hddPath)) == 1)
		pFileName = hddPath;

	// make copy of filename
	snprintf(FileName, sizeof(FileName), "%s", pFileName);
	snprintf(SegaContentName, sizeof(SegaContentName), "%s", pFileName);

	// resolve file extension of filename
	if (!PathExtResolve(FileName, &eType, TRUE))
	{
		return FALSE;
  	}

	if (eType == MAINLOOP_ENTRYTYPE_SNESPALETTE)
	{
		return _MainLoopLoadSnesPalette(pFileName);
	}

	// unload existing game
    _MainLoopUnloadRom();

    #if MAINLOOP_HISTORY
    _MainLoopResetHistory();
    #endif
	_MainLoopResetInputChecksums();

	int nRomBytes = 0;
	Uint8 *pBuffer = _RomData;
	Int32 nBufferBytes = sizeof(_RomData);
	Uint32 uRomIdentityCRC = 0;
#if SNDBG_LOG
	Uint32 uRomCRC = 0;
#endif

	// load rom data from disk into our buffer
	if (eType == MAINLOOP_ENTRYTYPE_GZ)
	{
		// if its a GZ file, then the next extension is the one we use
		/* AURORA_PICODRIVE_STAGE2_GZ_NAME: FileName still has .md/.sms/etc here. */
		snprintf(SegaContentName, sizeof(SegaContentName), "%s", FileName);
		if (!PathExtResolve(FileName, &eType, TRUE))
		{
			return FALSE;
		}

		// load GZ-ipped data
		nRomBytes = _MainLoopReadGZData(pBuffer, nBufferBytes, pFileName);

	} else
	if (eType == MAINLOOP_ENTRYTYPE_ZIP)
	{
		// if it is a ZIP file then we have to look in the file to find the right file to load
		nRomBytes = _MainLoopReadZipData(pBuffer, nBufferBytes, pFileName, FileName);
		if (nRomBytes > 0)
		{
			/* AURORA_PICODRIVE_STAGE2_ZIP_NAME */
			snprintf(SegaContentName, sizeof(SegaContentName), "%s", FileName);
			// resolve extension of unzipped file
			if (!PathExtResolve(FileName, &eType, TRUE))
			{
				return FALSE;
			}
		}

	} else
	{
		// read as binary data
		nRomBytes = _MainLoopReadBinaryData(pBuffer, nBufferBytes, pFileName);
	}

	// was load successful?
	if (nRomBytes <= 0)
	{
		return FALSE;
	}

	/* Save-state identity is the pristine file/archive payload. PicoDrive is
	 * allowed to byte-swap or patch its borrowed buffer after this point. */
	uRomIdentityCRC = (Uint32)mz_crc32(
		MZ_CRC32_INIT, pBuffer, (size_t)nRomBytes);

#if SNDBG_LOG
	/* Hash the bytes exactly as they came from disk/archive, before a ROM
	   parser removes a copier header or deinterleaves the image. */
	uRomCRC = (Uint32)mz_crc32(MZ_CRC32_INIT, pBuffer, (size_t)nRomBytes);
#endif

    /* Parsers receive the exact byte count, so clearing all 8 MiB before
       every launch was redundant. Keep only a small zero guard for old
       cartridge code that may legally perform aligned look-ahead reads. */
    {
        Int32 nGuardBytes = nBufferBytes - nRomBytes;
        if (nGuardBytes > 1024)
            nGuardBytes = 1024;
        if (nGuardBytes > 0)
            memset(pBuffer + nRomBytes, 0, nGuardBytes);
    }

    printf("ROM data read: %s (%d bytes)\n", pFileName, nRomBytes);

	_MainLoopGetName(_RomName, FileName);
	printf("ROMName: '%s'\n", _RomName);

	// determine what kind of system to use for this rom
	switch (eType)
	{
		/* Phase 2 of the NES integration: route .nes/.fds/disksys.rom
		   to _pNes (the NesSystem). FDS support is enabled here so
		   the loader accepts the file, but ExecuteFrame is a stub
		   today and disk-swap input is still gated until Phase 5. */
		case MAINLOOP_ENTRYTYPE_NESROM:
			pSystem = _pNes;
			pRom    = _pNesRom;
			pBios   = NULL;
			_MainLoop_fOutputIntensity = 0.8f;
			break;

		case MAINLOOP_ENTRYTYPE_NESFDSDISK:
			pSystem = _pNes;
			pRom    = _pNesFDSDisk;
			pBios   = _pNesFDSBios;
			_MainLoop_fOutputIntensity = 0.8f;
			break;

		case MAINLOOP_ENTRYTYPE_NESFDSBIOS:
			pSystem = _pNes;
			pRom    = NULL;
			pBios   = _pNesFDSBios;
			_MainLoop_fOutputIntensity = 0.8f;
			break;
		case MAINLOOP_ENTRYTYPE_SEGAROM:
			pSystem = _pSega;
			pRom    = _pSegaRom;
			pBios   = NULL;
			_MainLoop_fOutputIntensity = 1.0f;
			break;
		case MAINLOOP_ENTRYTYPE_SNESROM:
			pSystem = _pSnes;
			pRom    = _pSnesRom;
			pBios   = NULL;
			_MainLoop_fOutputIntensity = 1.0f;
			break;
		default:
			return FALSE;
	}

	if (pBios)
	{
		if (pRom==NULL)
		{
			// try to load disksys.rom directly
			if (!_MainLoopLoadBios(pBios, pFileName))
			{
				MainLoopModalPrintf(60*5, "ERROR: Cannot load disksys.rom");
				return FALSE;
			}
		} else
		{
			// can't run disks unless we have the FDS Bios loaded
			if (!pBios->IsLoaded())
			{
				char diskrompath[1024];
                            Char *pFileName;
				snprintf(diskrompath, sizeof(diskrompath), "%s", FileName);
				pFileName = strrchr(diskrompath, '/');
				if (!pFileName) 
					pFileName = strrchr(diskrompath, ':');
				if (!pFileName)
					return FALSE;

				// 
				strcpy(pFileName + 1, "disksys.rom");

				printf("FDSRom: '%s'\n", diskrompath);

				// try to load disksys.rom
				if (!_MainLoopLoadBios(pBios, diskrompath))
				{
					MainLoopModalPrintf(60*5, "ERROR: Cannot load disksys.rom");
					return FALSE;
				}
			}
		}
	}

    /* AURORA_MD_PRELOAD_RASTER_V2
     * Select the final physical 240p raster BEFORE the ROM wrapper/core
     * gets initialised. GSK_ReinitVideo must not run while PicoDrive owns
     * live EE allocations. */
    {
        Int32 rasterWidth = 256;

        if (eType == MAINLOOP_ENTRYTYPE_SEGAROM &&
            _MainLoopSegaWantsNative320(
                pBuffer, nRomBytes, SegaContentName))
        {
            rasterWidth = 320;
        }

        if (!MainLoopEnsureGameplayRasterWidth(rasterWidth))
        {
            printf("[video] failed to prepare %d raster before ROM init\n",
                   (int)rasterWidth);

            /* Best-effort recovery so the error/UI remains usable. */
            if (rasterWidth != 256)
                MainLoopEnsureGameplayRasterWidth(256);

            MainLoopModalPrintf(60 * 3,
                "ERROR: cannot configure video raster");
            return FALSE;
        }
    }

	if (pRom)
	{
		// attempt to load rom for that system
		if (!_MainLoopLoadRomData(pRom, _RomData, nRomBytes))
		{
			return FALSE;
		}
	}

	if (pBios)
	{
		// setup disk system
		pSystem->SetRom(pBios);
		/* Phase 2: NesSystem accepts the FDS disk pointer but the
		   real swap mux (NesMMU) is still a Phase 5 task, so this
		   stores the pointer without actually selecting a disk. The
		   SNES SetSnesRom path is kept as a safety net in case the
		   ROM that triggered pBios was somehow a SNES image. */
		if (pSystem == _pNes)
		{
			_pNes->SetNesDisk(_pNesFDSDisk);
		}
		else
		{
			_pSnes->SetSnesRom(_pSnesRom);
		}
	} 
	else
	{
		/* AURORA_PICODRIVE_STAGE2_SETROM */
		if (pSystem == _pSega && _pSegaRom)
		{
			_pSegaRom->SetSourceName(SegaContentName);
			PicoDriveBridge_SetRegion((int)g_SnesForceRegion);
		}
		pSystem->SetRom(pRom);
	}

	pSystem->Reset();

    /* SNESTICLE_QUICKNES_REQUIRE_ROM_READY
     * QuickNES only runs a successfully opened .nes cartridge. SetRom()
     * deliberately rejects FDS/non-cartridge images and can also reject an
     * unsupported mapper. Do not publish that half-loaded NesSystem to the
     * frame loop. */
    if (pSystem == _pNes && !_pNes->IsRomReady())
    {
        printf("[QuickNES] ROM rejected; aborting launch before mainloop\n");
        _MainLoopUnloadRom();
        MainLoopModalPrintf(60 * 3,
            "ERROR: QuickNES cannot run this NES image (format/mapper/FDS)");
        return FALSE;
    }

    /* AURORA_PICODRIVE_STAGE2_REQUIRE_READY */
    if (pSystem == _pSega && !_pSega->IsRomReady())
    {
        printf("[PicoDrive] ROM rejected; aborting launch before mainloop\n");
        _MainLoopUnloadRom();
        MainLoopModalPrintf(60 * 3,
            "ERROR: PicoDrive cannot run this SEGA image");
        return FALSE;
    }

    /* AURORA_MD_PRELOAD_RASTER_V2
     * Physical raster was selected before SetRom(). Do not rebuild the
     * GS here: PicoDrive is now live and must keep a stable host video
     * environment until the ROM is unloaded. */

    _pSystem = pSystem;
    snprintf(_RomPath, sizeof(_RomPath), "%s", OriginalPath);
    MainLoopStateOnRomChanged();
    MainLoopStatePrimeRomIdentityCRC(uRomIdentityCRC);

	ConPrint("ROM Loaded: %s\n", pFileName);

	if (pRom)
	{
		int nRegions, iRegion;
		Char *pRomTitle;
		Char *pRomMapper;

		// print mapper info
		pRomMapper = pRom->GetMapperName();
		if (pRomMapper && !strcmp(pRomMapper, "<unknown>"))
		{
			MainLoopModalPrintf(60*1, "WARNING: Unsupported NES Mapper");
		}

		// print rom title
		pRomTitle = pRom->GetRomTitle();
		if (pRomTitle)
		{
		    printf("Rom Title: %s\n", pRomTitle);
		}

#if SNDBG_LOG
		DLog("[rom] file='%s' bytes=%d crc32=%08X title='%s' mapper='%s'",
			_RomName, (int)nRomBytes, (unsigned)uRomCRC,
			pRomTitle ? pRomTitle : "<none>",
			pRomMapper ? pRomMapper : "<none>");
#endif

		// print info about rom regions
		nRegions = pRom->GetNumRomRegions();
		for (iRegion=0; iRegion < nRegions; iRegion++)
		{
			printf("%s: %d bytes\n", pRom->GetRomRegionName(iRegion), pRom->GetRomRegionSize(iRegion));
		}
	}

    _MainLoopSetSampleRate(pSystem->GetSampleRate());

	if (bLoadSRAM)
		_MainLoopLoadSRAM();

	// clear screen
    _fbTexture[0]->Clear();
    TextureUpload(&_OutTex, _fbTexture[0]->GetLinePtr(0));
	if (eType == MAINLOOP_ENTRYTYPE_NESFDSDISK)
	{
		/* Phase 2: track disk-inserted state so the SRAM/state path
		   builder picks up the right name, but the real disk swap
		   (NesMMU::InsertDisk) is a Phase 5 task. _pNes->GetMMU()
		   currently returns NULL so we deliberately skip that call. */
		_MainLoop_iDisk         = 0;
		_MainLoop_bDiskInserted = TRUE;
	}
	return TRUE;
}

void _MainLoopSetSampleRate(Uint32 uSampleRate)
{
    _AudMix->SetSampleRate(uSampleRate);
}
