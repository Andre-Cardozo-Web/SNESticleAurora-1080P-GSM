
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <kernel.h>
#include <libpad.h>
#include "types.h"
#include "font.h"
#include "poly.h"
#include "uiVideo.h"
#include "snrom.h"
#include "snes.h"
extern "C" {
#include "gskit_backend.h"
#include "gpfifo.h"
#include "ps2dma.h"
#include "audio.h"
}
#include "memcard.h"
#include "uiCover.h"
#include "mainloop_bgm.h"
#include "mainloop_state.h"
#include "mainloop_input.h"
#include "input.h"
#include "mainloop_smb.h"
#include "audmixbuffer.h"
#include "embedded_irx.h"   /* HddSupportIsEnabled / HddSupportSetEnabled */
#include "snppucolor.h"
#include "snppurender.h"
#include "nes/quicknes/quicknes_bridge.h" /* QUICKNES_FAMICLONE_HOOK */

/* mc0:/SNESticle (defined in mainloop_globals.cpp). */
extern Char _SramPath[256];
extern SnesSystem *_pSnes;

void MainResetEmulator(void);

/* ------------------------------------------------------------------ */
/* Persistence                                                         */
/* ------------------------------------------------------------------ */

#define VIDEOCFG_MAGIC   0x53564944u   /* 'SVID' */
#define VIDEOCFG_VERSION 26
/* AURORA_AUG19_BUNDLE_V3: v26 appends Turbo Speed + CPU Overclock. */
/* AURORA_CONFIG_V24_STORAGE_OBJ_LIMIT_MODE
 * v24 appends OBJ limiter mode. v23 imports its limiter/storage fields and
 * defaults the new mode to Per Scanline; v21/v22 retain exact import. */
/* AURORA_V85_SOFTWARE_HACKS_CONFIG
 * v20 only appends renderer-hack preferences. Old configs migrate with all
 * hacks disabled and all layers enabled. */

typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;     /* volume da trilha de menu: 0=off, 1..100 */
	Int32  bgmrate;    /* frequencia de sintese da trilha (Hz)     */
	Int32  gamevol;    /* volume do audio do jogo (SNES/NES): 0..100 */
	Int32  hddenable;  /* suporte ao HD interno (hdd0:): 0=off, 1=on  */
	Int32  mmceenable; /* suporte a MMCE (mmce0/1): 0=off, 1=on       */
	Int32  massenable; /* mass/USB (mass0/1): 0=off, 1=on             */
	Int32  smbenable;  /* historical host slot; now smb: 0=off, 1=on */
	Int32  mx4sioenable; /* MX4SIO (SD via SIO2): 0=off, 1=on         */
	Int32  colorprofile; /* SNPPU_COLOR_PROFILE_*                     */
	Int32  famicloneaudio;
	Int32  fakesramsize;
	Int32  forceregion;
	Int32  sneshacklayersoff;
	Int32  sneshackflags;
	Int32  compatflags;
	Int32  objlimit;
	Int32  sramdevice;
	Int32  objlimitmode;
	Int32  snesmousemode;
	Int32  turbospeed;
	Int32  cpuoverclock;
} VideoCfgT;

/* v16 is the exact prefix written by v1.0.4 and by the first video-fix
   test build. Keep it readable so installing this build never resets the
   user's mode, offsets, audio volumes or storage choices. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  hostenable;
	Int32  mx4sioenable;
} VideoCfgV16T;

typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
} VideoCfgV17T;

typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  famicloneaudio;
} VideoCfgV18T;

/* Exact on-card layout written by v19. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  famicloneaudio;
	Int32  fakesramsize;
	Int32  forceregion;
} VideoCfgV19T;

/* Exact on-card layout written by v20 (V8.5). */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  famicloneaudio;
	Int32  fakesramsize;
	Int32  forceregion;
	Int32  sneshacklayersoff;
	Int32  sneshackflags;
} VideoCfgV20T;

/* Exact byte layout written by v21 and v22. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  famicloneaudio;
	Int32  fakesramsize;
	Int32  forceregion;
	Int32  sneshacklayersoff;
	Int32  sneshackflags;
	Int32  compatflags;
} VideoCfgV22T;


/* Exact byte layout written by Aurora video.cfg v24. */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  famicloneaudio;
	Int32  fakesramsize;
	Int32  forceregion;
	Int32  sneshacklayersoff;
	Int32  sneshackflags;
	Int32  compatflags;
	Int32  objlimit;
	Int32  sramdevice;
	Int32  objlimitmode;
} VideoCfgV24T;

/* Exact byte layout written by Aurora video.cfg v25. */
typedef struct
{
	VideoCfgV24T v24;
	Int32 snesmousemode;
} VideoCfgV25T;

/* Exact byte layout written by Aurora V1/V1.1 (video.cfg v23). */
typedef struct
{
	Uint32 magic;
	Int32  version;
	Int32  mode;
	Int32  offx;
	Int32  offy;
	Int32  overscan;
	Int32  widescreen;
	Int32  covers;
	Int32  bgmvol;
	Int32  bgmrate;
	Int32  gamevol;
	Int32  hddenable;
	Int32  mmceenable;
	Int32  massenable;
	Int32  smbenable;
	Int32  mx4sioenable;
	Int32  colorprofile;
	Int32  famicloneaudio;
	Int32  fakesramsize;
	Int32  forceregion;
	Int32  sneshacklayersoff;
	Int32  sneshackflags;
	Int32  compatflags;
	Int32  objlimit;
	Int32  sramdevice;
} VideoCfgV23T;

typedef struct
{
	Uint32 magic;
	Int32  version;
} VideoCfgHeaderT;

static void _VideoCfgPath(char *pOut)
{
	strcpy(pOut, _SramPath);
	strcat(pOut, "/video.cfg");
}

static Bool g_FamicloneAudio = FALSE;

/* AURORA_COMPAT_PAGE_V21
 * Zero flags = exact V8.5 host behavior. */
#define VIDEO_COMPAT_GS_FULL_CACHE   (1 << 0)
#define VIDEO_COMPAT_GIF_LONG_WAIT   (1 << 1)
#define VIDEO_COMPAT_AUDIO_SMALL_RPC (1 << 2)
#define VIDEO_COMPAT_AUDIO_DEEP_Q    (1 << 3)
#define VIDEO_COMPAT_ALL             0x0F

static Int32 g_VideoCompatFlags = 0;

static void _VideoApplyCompatFlags(Int32 flags)
{
	g_VideoCompatFlags = flags & VIDEO_COMPAT_ALL;
	GPFifoSetCompatFullCache(
		(g_VideoCompatFlags & VIDEO_COMPAT_GS_FULL_CACHE) ? TRUE : FALSE);
	DmaSetGifCompatLongWait(
		(g_VideoCompatFlags & VIDEO_COMPAT_GIF_LONG_WAIT) ? TRUE : FALSE);
	Aud_SetCompatSmallChunks(
		(g_VideoCompatFlags & VIDEO_COMPAT_AUDIO_SMALL_RPC) ? 1 : 0);
	Aud_SetCompatDeepQueue(
		(g_VideoCompatFlags & VIDEO_COMPAT_AUDIO_DEEP_Q) ? 1 : 0);
}

void VideoSettingsSave(void)
{
	VideoCfgT cfg;
	char      path[300];

	cfg.magic   = VIDEOCFG_MAGIC;
	cfg.version = VIDEOCFG_VERSION;
	cfg.mode    = g_GskVideoMode;
	cfg.offx    = g_GskDispOffX;
	cfg.offy    = g_GskDispOffY;
	cfg.overscan   = g_GskOverscan;
	cfg.widescreen = g_GskWidescreen;
	cfg.covers     = CoverIsEnabled() ? 1 : 0;
	cfg.bgmvol     = BgmGetVolume();
	cfg.bgmrate    = BgmGetRate();
	cfg.gamevol    = AudMixGameGetVolume();
	cfg.hddenable  = HddSupportIsEnabled() ? 1 : 0;
	cfg.mmceenable = MmceSupportIsEnabled() ? 1 : 0;
	cfg.massenable = MassStorageIsEnabled() ? 1 : 0;
	cfg.smbenable  = SmbSupportIsEnabled() ? 1 : 0;
	cfg.mx4sioenable = Mx4sioIsEnabled() ? 1 : 0;
		cfg.colorprofile = SNPPUColorGetProfile();
	cfg.famicloneaudio = g_FamicloneAudio ? 1 : 0;
	cfg.fakesramsize = g_FakeSRAMSize;
	cfg.forceregion = g_SnesForceRegion;
	cfg.sneshacklayersoff =
		(~SNPPURenderGetSoftwareLayerMask()) &
		(SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2 | SNESPPU_MASK_BG3 |
		 SNESPPU_MASK_BG4 | SNESPPU_MASK_OBJ);
	cfg.sneshackflags = SNPPURenderGetSoftwareHackFlags();
	cfg.compatflags = g_VideoCompatFlags & VIDEO_COMPAT_ALL;
	cfg.objlimit = SNPPURenderGetObjLimitLevel();
	cfg.sramdevice = MainLoopSramGetDevice();
	cfg.objlimitmode = SNPPURenderGetObjLimitMode();
	cfg.snesmousemode = (Int32)InputSnesMouseGetMode();
	cfg.turbospeed = (Int32)MainLoopTurboGetSpeed();
	cfg.cpuoverclock = (Int32)SNCPUGetOverclockLevel();
	_VideoCfgPath(path);
	BgmIOBegin();
	MemCardWriteFile(path, (Uint8 *)&cfg, sizeof(cfg));
	BgmIOEnd();
}

void VideoSettingsLoad(void)
{
	VideoCfgT cfg;
	VideoCfgV16T oldcfg;
	VideoCfgHeaderT header;
	char      path[300];
	Bool      loaded = FALSE;

	memset(&cfg, 0, sizeof(cfg));
	SNPPURenderSetSoftwareLayerMask(
		SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2 | SNESPPU_MASK_BG3 |
		SNESPPU_MASK_BG4 | SNESPPU_MASK_OBJ);
	SNPPURenderSetSoftwareHackFlags(SNPPU_HACK_MODE7_HALF);
	SNPPURenderSetObjLimitLevel(SNPPU_OBJ_LIMIT_OFF);
	SNPPURenderSetObjLimitMode(SNPPU_OBJ_LIMIT_MODE_SCANLINE);
	MainLoopSramSetDevice(MAINLOOP_SRAMDEVICE_AUTO);
	InputSnesMouseSetMode(INPUT_SNES_MOUSE_OFF);
	MainLoopTurboSetSpeed(MAINLOOP_TURBO_SPEED_NORMAL);
	SNCPUSetOverclockLevel(_pSnes ? _pSnes->GetCpu() : NULL, SNCPU_OVERCLOCK_OFF);
	_VideoApplyCompatFlags(0);
	_VideoCfgPath(path);

	memset(&header, 0, sizeof(header));
	if (MemCardReadFile(path, (Uint8 *)&header, sizeof(header)) &&
	    header.magic == VIDEOCFG_MAGIC)
	{
				if (header.version == VIDEOCFG_VERSION)
		{
			loaded = MemCardReadFile(path, (Uint8 *)&cfg, sizeof(cfg));
		}
		else if (header.version == 25)
		{
			VideoCfgV25T oldcfg25;
			memset(&oldcfg25, 0, sizeof(oldcfg25));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg25, sizeof(oldcfg25)))
			{
				memcpy(&cfg, &oldcfg25, sizeof(oldcfg25));
				cfg.version = VIDEOCFG_VERSION;
				switch (oldcfg25.snesmousemode)
				{
					case 1: cfg.snesmousemode = INPUT_SNES_MOUSE_CONTROLLER; break;
					case 3: cfg.snesmousemode = INPUT_SNES_MOUSE_USB; break;
					default: cfg.snesmousemode = INPUT_SNES_MOUSE_OFF; break;
				}
				cfg.turbospeed = MAINLOOP_TURBO_SPEED_NORMAL;
				cfg.cpuoverclock = SNCPU_OVERCLOCK_OFF;
				loaded = TRUE;
			}
		}
		else if (header.version == 24)
		{
			VideoCfgV24T oldcfg24;
			memset(&oldcfg24, 0, sizeof(oldcfg24));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg24, sizeof(oldcfg24)))
			{
				memcpy(&cfg, &oldcfg24, sizeof(oldcfg24));
				cfg.version = VIDEOCFG_VERSION;
				cfg.snesmousemode = INPUT_SNES_MOUSE_OFF;
				loaded = TRUE;
			}
		}
		else if (header.version == 23)
		{
			VideoCfgV23T oldcfg23;
			memset(&oldcfg23, 0, sizeof(oldcfg23));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg23, sizeof(oldcfg23)))
			{
				memcpy(&cfg, &oldcfg23, sizeof(oldcfg23));
				cfg.version = VIDEOCFG_VERSION;
				cfg.objlimitmode = SNPPU_OBJ_LIMIT_MODE_SCANLINE;
				loaded = TRUE;
			}
		}
		else if (header.version == 22 || header.version == 21)
		{
			VideoCfgV22T oldcfg22;
			memset(&oldcfg22, 0, sizeof(oldcfg22));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg22, sizeof(oldcfg22)))
			{
				memcpy(&cfg, &oldcfg22, sizeof(oldcfg22));
				cfg.version = VIDEOCFG_VERSION;
				cfg.objlimit = SNPPU_OBJ_LIMIT_OFF;
				cfg.sramdevice = MAINLOOP_SRAMDEVICE_AUTO;
				cfg.objlimitmode = SNPPU_OBJ_LIMIT_MODE_SCANLINE;
				loaded = TRUE;
			}
		}
		else if (header.version == 20)
		{
			VideoCfgV20T oldcfg20;
			memset(&oldcfg20, 0, sizeof(oldcfg20));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg20, sizeof(oldcfg20)))
			{
				memcpy(&cfg, &oldcfg20, sizeof(oldcfg20));
				cfg.version = VIDEOCFG_VERSION;
				cfg.compatflags = 0;
				loaded = TRUE;
			}
		}
		else if (header.version == 19)
		{
			VideoCfgV19T oldcfg19;

			memset(&oldcfg19, 0, sizeof(oldcfg19));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg19, sizeof(oldcfg19)))
			{
				memcpy(&cfg, &oldcfg19, sizeof(oldcfg19));
				cfg.version = VIDEOCFG_VERSION;
				cfg.sneshacklayersoff = 0;
				cfg.sneshackflags = 0;
				loaded = TRUE;
			}
		}
		else if (header.version == 18)
		{
			VideoCfgV18T oldcfg18;

			memset(&oldcfg18, 0, sizeof(oldcfg18));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg18, sizeof(oldcfg18)))
			{
				memcpy(&cfg, &oldcfg18, sizeof(oldcfg18));
				cfg.version = VIDEOCFG_VERSION;
				cfg.fakesramsize = 0;
				cfg.forceregion = SNES_FORCE_REGION_OFF;
				loaded = TRUE;
			}
		}
		else if (header.version == 17)
		{
	VideoCfgV17T oldcfg17;

	memset(&oldcfg17, 0, sizeof(oldcfg17));
	if (MemCardReadFile(path, (Uint8 *)&oldcfg17, sizeof(oldcfg17)))
	{
		memcpy(&cfg, &oldcfg17, sizeof(oldcfg17));
		cfg.version = VIDEOCFG_VERSION;
		cfg.famicloneaudio = 0;
		loaded = TRUE;
	}
}
		else if (header.version == 16)
		{
			memset(&oldcfg, 0, sizeof(oldcfg));
			if (MemCardReadFile(path, (Uint8 *)&oldcfg, sizeof(oldcfg)))
			{
				/* VideoCfgT only appends colorprofile to the v16 prefix. */
				memcpy(&cfg, &oldcfg, sizeof(oldcfg));
				cfg.version = VIDEOCFG_VERSION;
				cfg.colorprofile = SNPPU_COLOR_PROFILE_ORIGINAL;
				loaded = TRUE;
			}
		}
	}

	/* New policy applies exactly once to every pre-v22 config. Once the
	 * user saves v22, a manual Full selection remains persistent. */
	if (loaded && header.version < 22)
		cfg.sneshackflags |= SNPPU_HACK_MODE7_HALF;

	if (loaded && cfg.magic == VIDEOCFG_MAGIC)
	{
		/* v1.0.2 allowed both SIO2 storage hooks to be saved at once.
		   Prefer MMCE when importing such a legacy config; all new changes
		   are mutually exclusive in the setters below. */
		if (cfg.mmceenable == 1 && cfg.mx4sioenable == 1)
			cfg.mx4sioenable = 0;

		if (cfg.mode == GSK_VIDMODE_240P ||
		    cfg.mode == GSK_VIDMODE_480I ||
		    cfg.mode == GSK_VIDMODE_1080I)
			g_GskVideoMode = cfg.mode;
		else
			g_GskVideoMode = GSK_VIDMODE_480I;

		if (cfg.offx >= -64 && cfg.offx <= 64) g_GskDispOffX = cfg.offx;
		if (cfg.offy >= -64 && cfg.offy <= 64) g_GskDispOffY = cfg.offy;
		if (cfg.overscan >= 0 && cfg.overscan <= 100) g_GskOverscan = cfg.overscan;
		if (cfg.widescreen == 0 || cfg.widescreen == 1) g_GskWidescreen = cfg.widescreen;
		if (cfg.covers == 0 || cfg.covers == 1) CoverSetEnabled(cfg.covers ? TRUE : FALSE);
		if (cfg.bgmvol >= 0 && cfg.bgmvol <= 100) BgmSetVolume(cfg.bgmvol);
		if (cfg.bgmrate >= 8000 && cfg.bgmrate <= 48000) BgmSetRate(cfg.bgmrate);
		if (cfg.gamevol >= 0 && cfg.gamevol <= 100) AudMixGameSetVolume(cfg.gamevol);
		if (cfg.hddenable == 0 || cfg.hddenable == 1) HddSupportSetEnabled(cfg.hddenable);
		if (cfg.mmceenable == 0 || cfg.mmceenable == 1) MmceSupportSetEnabled(cfg.mmceenable);
		if (cfg.massenable == 0 || cfg.massenable == 1) MassStorageSetEnabled(cfg.massenable);
		if (cfg.smbenable == 0 || cfg.smbenable == 1) SmbSupportSetEnabled(cfg.smbenable);
		if (cfg.mx4sioenable == 0 || cfg.mx4sioenable == 1) Mx4sioSetEnabled(cfg.mx4sioenable);
		if (cfg.colorprofile >= 0 && cfg.colorprofile < SNPPU_COLOR_PROFILE_COUNT)
			SNPPUColorSetProfile(cfg.colorprofile);
if (cfg.famicloneaudio == 0 || cfg.famicloneaudio == 1)
{
	g_FamicloneAudio = cfg.famicloneaudio ? TRUE : FALSE;
	QuicknesBridge_SetDutySwap(g_FamicloneAudio ? true : false);
}
		if (cfg.fakesramsize == 0 ||
		    cfg.fakesramsize == 8 ||
		    cfg.fakesramsize == 16 ||
		    cfg.fakesramsize == 32 ||
		    cfg.fakesramsize == 64 ||
		    cfg.fakesramsize == 128 ||
		    cfg.fakesramsize == 256 ||
		    cfg.fakesramsize == 512 ||
		    cfg.fakesramsize == 1024 ||
		    cfg.fakesramsize == 2048)
		{
			g_FakeSRAMSize = cfg.fakesramsize;
		}
		if (cfg.forceregion == SNES_FORCE_REGION_OFF ||
		    cfg.forceregion == SNES_FORCE_REGION_NTSC_U ||
		    cfg.forceregion == SNES_FORCE_REGION_NTSC_J ||
		    cfg.forceregion == SNES_FORCE_REGION_PAL)
		{
			g_SnesForceRegion = cfg.forceregion;
		}

		const Int32 uLayerBits =
			SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2 | SNESPPU_MASK_BG3 |
			SNESPPU_MASK_BG4 | SNESPPU_MASK_OBJ;
		if ((cfg.sneshacklayersoff & ~uLayerBits) == 0)
			SNPPURenderSetSoftwareLayerMask(
				(Uint8)(uLayerBits & ~cfg.sneshacklayersoff));
		if ((cfg.sneshackflags & ~SNPPU_HACK_ALL) == 0)
			SNPPURenderSetSoftwareHackFlags((Uint8)cfg.sneshackflags);
		if (cfg.objlimit >= SNPPU_OBJ_LIMIT_OFF && cfg.objlimit < SNPPU_OBJ_LIMIT_NUM)
			SNPPURenderSetObjLimitLevel((Uint8)cfg.objlimit);
		if (cfg.objlimitmode >= SNPPU_OBJ_LIMIT_MODE_SCANLINE &&
		    cfg.objlimitmode < SNPPU_OBJ_LIMIT_MODE_NUM)
			SNPPURenderSetObjLimitMode((Uint8)cfg.objlimitmode);
		if (cfg.sramdevice >= MAINLOOP_SRAMDEVICE_AUTO &&
		    cfg.sramdevice < MAINLOOP_SRAMDEVICE_NUM)
			MainLoopSramSetDevice((MainLoopSramDeviceE)cfg.sramdevice);
		if ((cfg.compatflags & ~VIDEO_COMPAT_ALL) == 0)
			_VideoApplyCompatFlags(cfg.compatflags);
		if (cfg.snesmousemode >= INPUT_SNES_MOUSE_OFF &&
		    cfg.snesmousemode < INPUT_SNES_MOUSE_MODE_NUM)
			InputSnesMouseSetMode(
				(InputSnesMouseModeE)cfg.snesmousemode);
		if (cfg.turbospeed >= MAINLOOP_TURBO_SPEED_NORMAL &&
		    cfg.turbospeed < MAINLOOP_TURBO_SPEED_NUM)
			MainLoopTurboSetSpeed((MainLoopTurboSpeedE)cfg.turbospeed);
		if (cfg.cpuoverclock >= SNCPU_OVERCLOCK_OFF &&
		    cfg.cpuoverclock < SNCPU_OVERCLOCK_NUM)
			SNCPUSetOverclockLevel(_pSnes ? _pSnes->GetCpu() : NULL,
			                         (Uint8)cfg.cpuoverclock);
	}
}

/* AURORA_MOUSE_VIDEO_COMPAT_V1 */
/* SNES Mouse lives on Video Config 4/4 and uses Left/Right. */

/* ------------------------------------------------------------------ */
/* Screen                                                              */
/* ------------------------------------------------------------------ */

CVideoScreen::CVideoScreen()
{
	m_iSelect = 0;
}

void CVideoScreen::Process()
{
}

static void _VideoCenter(int x, int y, const char *pStr)
{
	FontPuts(x - FontGetStrWidth(pStr) / 2, y, pStr);
}

static void _VideoRow(int vy, int idx, int sel, const char *pLabel, const char *pValue)
{
	if (idx == sel)
	{
		PolyColor4f(0.0f, 0.5f, 0.0f, 0.5f);
		PolyRect(48, vy - 1, 160, FontGetHeight() + 2);
	}

	FontColor4f(0.5f, 0.5f, 0.5f, 1.0f);
	FontPuts(56, vy, pLabel);

	FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	FontPuts(150, vy, pValue);
}

static void _VideoHeader(int vy, const char *pStr)
{
	PolyColor4f(0.0f, 0.2f, 0.2f, 0.5f);
	PolyRect(32, vy, 256 - 64, 9);
	FontColor4f(0.0f, 0.8f, 0.8f, 1.0f);
	_VideoCenter(128, vy, pStr);
}

static const char *_VideoMmceStatus()
{
	int slots;

	if (!MmceSupportIsEnabled()) return "Off";
	if (MmceNeedsRestart())      return "Restart";
	if (MmceGetLastError() < 0)  return "Driver Error";
	if (!MmceIsLoaded())         return "On";

	slots = MmceGetAvailableSlots();
	if (slots == 1) return "Slot 1";
	if (slots == 2) return "Slot 2";
	if (slots == 3) return "Slots 1+2";
	return "Not Found";
}

static const char *_VideoFakeSRAMStatus()
{
   switch (g_FakeSRAMSize)
{
case 8:    return "1 KB";
case 16:   return "2 KB";
case 32:   return "4 KB";
case 64:   return "8 KB";
case 128:  return "16 KB";
case 256:  return "32 KB";
case 512:  return "64 KB";
case 1024: return "128 KB";
case 2048: return "256 KB";
default:   return "Auto";
}
}

static const char *_VideoForceRegionStatus()
{
    switch (g_SnesForceRegion)
    {
        case SNES_FORCE_REGION_NTSC_U:
            return "NTSC-U";

        case SNES_FORCE_REGION_NTSC_J:
            return "NTSC-J";

        case SNES_FORCE_REGION_PAL:
            return "PAL";

        case SNES_FORCE_REGION_OFF:
        default:
            return "Auto";
    }
}

static const char *_VideoFamicloneAudioStatus()
{
	return g_FamicloneAudio ? "On" : "Off";
}

static const char *_VideoHackLayerStatus(Uint8 uLayer)
{
	return (SNPPURenderGetSoftwareLayerMask() & uLayer) ? "On" : "Off";
}

static const char *_VideoHackAccurateStatus(Uint8 uFlag)
{
	return (SNPPURenderGetSoftwareHackFlags() & uFlag) ? "Off" : "Accurate";
}

static const char *_VideoHackMode7Status()
{
	return (SNPPURenderGetSoftwareHackFlags() & SNPPU_HACK_MODE7_HALF)
		? "Half" : "Full";
}

static const char *_VideoHackSpriteLimiterStatus()
{
	switch (SNPPURenderGetObjLimitLevel())
	{
		case SNPPU_OBJ_LIMIT_LIGHT:   return "Light (28)";
		case SNPPU_OBJ_LIMIT_MEDIUM:  return "Medium (24)";
		case SNPPU_OBJ_LIMIT_STRONG:  return "Strong (20)";
		case SNPPU_OBJ_LIMIT_EXTREME: return "Extreme (16)";
		default:                      return "Off (34)";
	}
}

static const char *_VideoHackSpriteLimiterModeStatus()
{
	return SNPPURenderGetObjLimitMode() == SNPPU_OBJ_LIMIT_MODE_SCREEN
		? "Per Screen" : "Per Scanline";
}

static const char *_VideoHackFrameSkipStatus()
{
	return (SNPPURenderGetSoftwareHackFlags() & SNPPU_HACK_FRAME_SKIP)
		? "1" : "Off";
}

static const char *_VideoHackCpuOverclockStatus()
{
	switch (SNCPUGetOverclockLevel())
	{
		case SNCPU_OVERCLOCK_120: return "120%";
		case SNCPU_OVERCLOCK_150: return "150%";
		case SNCPU_OVERCLOCK_200: return "200%";
		case SNCPU_OVERCLOCK_300: return "300%";
		default:                  return "Off";
	}
}

static const char *_VideoCompatProfileStatus()
{
	if (g_VideoCompatFlags == 0) return "Standard";
	if (g_VideoCompatFlags == VIDEO_COMPAT_ALL) return "Conservative";
	return "Custom";
}
static const char *_VideoCompatGsCacheStatus()
{
	return (g_VideoCompatFlags & VIDEO_COMPAT_GS_FULL_CACHE) ? "Full" : "Range";
}
static const char *_VideoCompatGifWaitStatus()
{
	return (g_VideoCompatFlags & VIDEO_COMPAT_GIF_LONG_WAIT) ? "Long" : "Normal";
}
static const char *_VideoCompatAudioRpcStatus()
{
	return (g_VideoCompatFlags & VIDEO_COMPAT_AUDIO_SMALL_RPC) ? "1 KB" : "4 KB";
}
static const char *_VideoCompatAudioQueueStatus()
{
	return (g_VideoCompatFlags & VIDEO_COMPAT_AUDIO_DEEP_Q) ? "Deep" : "Normal";
}

static const char *_VideoMx4sioStatus()
{
	if (!Mx4sioIsEnabled())       return "Off";
	if (Mx4sioNeedsRestart())     return "Restart";
	if (Mx4sioGetLastError() < 0) return "Driver Error";
	return Mx4sioIsLoaded() ? "On" : "Enabled";
}

typedef struct
{
	Int32 mode;
	const char *name;
} VideoModeChoiceT;

static const VideoModeChoiceT _VideoModes[] =
{
	{ GSK_VIDMODE_240P,  "240p/288p (CRT)" },
	{ GSK_VIDMODE_480I,  "480i (default)" },
	{ GSK_VIDMODE_1080I, "1080i" }
};

static Int32 _VideoModeIndex(Int32 mode)
{
	Int32 i;
	for (i = 0; i < (Int32)(sizeof(_VideoModes) / sizeof(_VideoModes[0])); i++)
		if (_VideoModes[i].mode == mode)
			return i;
	return 0;
}

void CVideoScreen::Draw()
{
	Int32 vy = 15;
	char  buf[16];
	int   m = _VideoModeIndex(g_GskVideoMode);
	const char *pMode = _VideoModes[m].name;
	/* AURORA_V85_SOFTWARE_HACKS_PAGE */
	int   iPage = (m_iSelect >= 30) ? 3 :
	              ((m_iSelect >= 19) ? 2 : ((m_iSelect >= 10) ? 1 : 0));
	const char *pWide = "Off";
	const char *pColor = (SNPPUColorGetProfile() == SNPPU_COLOR_PROFILE_COMPOSITE)
	                   ? "Composite" : "Original";

	if (g_GskWidescreen)
		pWide = "On";

	FontSelect(0);

	_VideoHeader(vy,
		iPage == 0 ? "Video Config (1/4)" :
		iPage == 1 ? "Video Config (2/4)" :
		iPage == 2 ? "Video Config (3/4)" :
		             "Video Config (4/4)");
	vy += 18;

	if (iPage == 0) {
	_VideoHeader(vy, "Screen");
	vy += 14;

	_VideoRow(vy, 0, m_iSelect, "Video Mode", pMode);  vy += 12;

	_VideoRow(vy, 1, m_iSelect, "Widescreen", pWide); vy += 12;

	_VideoRow(vy, 2, m_iSelect, "SNES Colors", pColor); vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskOverscan);
	_VideoRow(vy, 3, m_iSelect, "Overscan", buf);      vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffX);
	_VideoRow(vy, 4, m_iSelect, "Offset X", buf);      vy += 12;

	snprintf(buf, sizeof(buf), "%d", g_GskDispOffY);
	_VideoRow(vy, 5, m_iSelect, "Offset Y", buf);      vy += 12;

	_VideoRow(vy, 6, m_iSelect, "Cover Art", CoverIsEnabled() ? "On" : "Off"); vy += 12;

	_VideoHeader(vy, "Audio"); vy += 14;

	snprintf(buf, sizeof(buf), "%d", AudMixGameGetVolume());
	_VideoRow(vy, 7, m_iSelect, "Game Volume", buf); vy += 12;

	{
		int bv = BgmGetVolume();
		if (bv <= 0)                 snprintf(buf, sizeof(buf), "Off");
		else if (BgmTrackCount() <= 0)
			snprintf(buf, sizeof(buf),
			         BgmIsSearching() ? "Searching" : "No Track");
		else                         snprintf(buf, sizeof(buf), "%d", bv);
	}
	_VideoRow(vy, 8, m_iSelect, "Menu Music", buf); vy += 12;

	snprintf(buf, sizeof(buf), "%d kHz", (BgmGetRate() + 500) / 1000);
	_VideoRow(vy, 9, m_iSelect, "Frequency", buf); vy += 12;
	}
	else if (iPage == 1)
	{
		_VideoHeader(vy, "Storage / Devices"); vy += 14;

		_VideoRow(vy, 10, m_iSelect, "Mass / USB",
		          MassStorageIsEnabled() ? "On" : "Off"); vy += 12;
		_VideoRow(vy, 11, m_iSelect, "HDD Support",
		          HddSupportIsEnabled() ? "On" : "Off"); vy += 12;
		_VideoRow(vy, 12, m_iSelect, "MMCE Cards",
		          _VideoMmceStatus()); vy += 12;
		_VideoRow(vy, 13, m_iSelect, "SMB (Network)",
		          SmbGetStatusText()); vy += 12;
		_VideoRow(vy, 14, m_iSelect, "MX4SIO (SD)",
		          _VideoMx4sioStatus()); vy += 12;

_VideoHeader(vy, "Misc."); vy += 14;

_VideoRow(vy, 15, m_iSelect, "SRAM Size",
          _VideoFakeSRAMStatus()); vy += 12;

_VideoRow(vy, 16, m_iSelect, "Force Region",
          _VideoForceRegionStatus()); vy += 12;

_VideoRow(vy, 17, m_iSelect, "Famiclone Audio",
          _VideoFamicloneAudioStatus()); vy += 12;

_VideoRow(vy, 18, m_iSelect, "Reset emulator", ""); vy += 12;

	}
	else if (iPage == 2)
	{
		_VideoHeader(vy, "Software Hacks"); vy += 14;
		_VideoRow(vy, 19, m_iSelect, "BG1 Layer",
			_VideoHackLayerStatus(SNESPPU_MASK_BG1)); vy += 12;
		_VideoRow(vy, 20, m_iSelect, "BG2 Layer",
			_VideoHackLayerStatus(SNESPPU_MASK_BG2)); vy += 12;
		_VideoRow(vy, 21, m_iSelect, "BG3 Layer",
			_VideoHackLayerStatus(SNESPPU_MASK_BG3)); vy += 12;
		_VideoRow(vy, 22, m_iSelect, "BG4 Layer",
			_VideoHackLayerStatus(SNESPPU_MASK_BG4)); vy += 12;
		_VideoRow(vy, 23, m_iSelect, "Sprites / OBJ",
			_VideoHackLayerStatus(SNESPPU_MASK_OBJ)); vy += 12;
		_VideoRow(vy, 24, m_iSelect, "Color Math",
			_VideoHackAccurateStatus(SNPPU_HACK_COLOR_MATH_OFF)); vy += 12;
		_VideoRow(vy, 25, m_iSelect, "Window Effects",
			_VideoHackAccurateStatus(SNPPU_HACK_WINDOWS_OFF)); vy += 12;
		_VideoRow(vy, 26, m_iSelect, "Mode 7 Quality",
			_VideoHackMode7Status()); vy += 12;
		_VideoRow(vy, 27, m_iSelect, "Sprite Limiter",
			_VideoHackSpriteLimiterStatus()); vy += 12;
		_VideoRow(vy, 28, m_iSelect, "Limiter Mode",
			_VideoHackSpriteLimiterModeStatus()); vy += 12;
		_VideoRow(vy, 29, m_iSelect, "CPU Overclock",
			_VideoHackCpuOverclockStatus()); vy += 12;
	}
	else
	{
		_VideoHeader(vy, "Compatibility"); vy += 14;
		_VideoRow(vy, 30, m_iSelect, "Profile",
			_VideoCompatProfileStatus()); vy += 12;
		_VideoRow(vy, 31, m_iSelect, "GS Cache Sync",
			_VideoCompatGsCacheStatus()); vy += 12;
		_VideoRow(vy, 32, m_iSelect, "GIF DMA Wait",
			_VideoCompatGifWaitStatus()); vy += 12;
		_VideoRow(vy, 33, m_iSelect, "Audio RPC Chunk",
			_VideoCompatAudioRpcStatus()); vy += 12;
		_VideoRow(vy, 34, m_iSelect, "Audio Queue",
			_VideoCompatAudioQueueStatus()); vy += 12;

		_VideoHeader(vy, "Performance"); vy += 14;
		_VideoRow(vy, 35, m_iSelect, "Frame Skip",
			_VideoHackFrameSkipStatus()); vy += 12;

		_VideoHeader(vy, "Controller options"); vy += 14;
		_VideoRow(vy, 36, m_iSelect, "SNES Mouse",
			InputSnesMouseGetModeName()); vy += 12;
		_VideoRow(vy, 37, m_iSelect, "Turbo Speed",
			MainLoopTurboGetSpeedName()); vy += 12;
	}

	/* controls / hints (clear of the vy=215 footer) */
	vy = 184;
	FontColor4f(0.6f, 0.6f, 0.6f, 1.0f);
	_VideoCenter(128, vy, "Up/Dn: select   L/R: change   X: save"); vy += 12;
	_VideoCenter(128, vy, "O: next   Square: previous"); vy += 12;

	if (g_GskVideoMode != GSK_GetActiveVideoMode())
	{
		FontColor4f(1.0f, 0.88f, 0.46f, 1.0f);
		_VideoCenter(128, vy, "mode applies after reboot");
	}
	else if (MmceNeedsRestart() || Mx4sioNeedsRestart())
	{
		FontColor4f(1.0f, 0.88f, 0.46f, 1.0f);
		_VideoCenter(128, vy, "storage applies after reboot");
	}
}

void CVideoScreen::Input(Uint32 buttons, Uint32 trigger)
{
	int dir = 0;

	/* Circle: Video/Audio -> Devices/Misc -> Software Hacks -> Compatibility. */
	if (trigger & PAD_CIRCLE)
	{
		if (m_iSelect < 10)       m_iSelect = 10;
		else if (m_iSelect < 19)  m_iSelect = 19;
		else if (m_iSelect < 30)  m_iSelect = 30;
		else                      m_iSelect = 0;
	}

	{
		int lo, hi;
		if (m_iSelect < 10)      { lo = 0;  hi = 9;  }
		else if (m_iSelect < 19) { lo = 10; hi = 18; }
		else if (m_iSelect < 30) { lo = 19; hi = 29; }
		else                     { lo = 30; hi = 37; }
		if (trigger & PAD_UP)    { m_iSelect--; if (m_iSelect < lo) m_iSelect = hi; }
		if (trigger & PAD_DOWN)  { m_iSelect++; if (m_iSelect > hi) m_iSelect = lo; }
	}

	if (trigger & PAD_LEFT)  dir = -1;
	if (trigger & PAD_RIGHT) dir = +1;

	if (dir != 0)
	{
		switch (m_iSelect)
		{
		case 0: /* video mode (applied on reboot) */
			{
				Int32 count = (Int32)(sizeof(_VideoModes) / sizeof(_VideoModes[0]));
				Int32 modeIndex = _VideoModeIndex(g_GskVideoMode) + dir;
				if (modeIndex < 0)      modeIndex = count - 1;
				if (modeIndex >= count) modeIndex = 0;
				g_GskVideoMode = _VideoModes[modeIndex].mode;
			}
			break;

		case 1: /* widescreen on/off (live) */
			g_GskWidescreen = !g_GskWidescreen;
			GSK_SetWidescreen(g_GskWidescreen);
			break;

		case 2: /* SNES colour profile (live) */
			SNPPUColorSetProfile(
				SNPPUColorGetProfile() == SNPPU_COLOR_PROFILE_ORIGINAL
				? SNPPU_COLOR_PROFILE_COMPOSITE
				: SNPPU_COLOR_PROFILE_ORIGINAL);
			break;

		case 3: /* overscan 0..100 (live, step 5) */
			g_GskOverscan += dir * 5;
			if (g_GskOverscan < 0)   g_GskOverscan = 0;
			if (g_GskOverscan > 100) g_GskOverscan = 100;
			GSK_SetOverscan(g_GskOverscan);
			break;

		case 4: /* offset X (live) */
			g_GskDispOffX += dir;
			if (g_GskDispOffX < -64) g_GskDispOffX = -64;
			if (g_GskDispOffX >  64) g_GskDispOffX =  64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;

		case 5: /* offset Y (live) */
			g_GskDispOffY += dir;
			if (g_GskDispOffY < -64) g_GskDispOffY = -64;
			if (g_GskDispOffY >  64) g_GskDispOffY =  64;
			GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY);
			break;

		case 6: /* cover art on/off (live; persisted on X like the rest) */
			CoverToggle();
			break;

		case 7: /* game (emulator) audio volume 0..100, step 1, live */
			{
				int v = AudMixGameGetVolume() + dir;
				if (v < 0)   v = 0;
				if (v > 100) v = 100;
				AudMixGameSetVolume(v);
			}
			break;

		case 8: /* menu music volume 0..100 (0 = off), step 1, live */
			{
				int v = BgmGetVolume() + dir;
				if (v < 0)   v = 0;
				if (v > 100) v = 100;
				BgmSetVolume(v);
			}
			break;

		case 9: /* frequencia de sintese da trilha (cicla a lista) */
			BgmCycleRate(dir);
			break;

		case 10: /* Mass / USB on/off -- lista mass0:/mass1: (USB).  O USB core
		           sobe no boot de qualquer forma (seguro); isto controla a
		           listagem.  O MX4SIO agora tem toggle proprio (case 14). */
			MassStorageSetEnabled(!MassStorageIsEnabled());
			break;

		case 11: /* HDD interno (hdd0:) on/off -- lista + carga preguicosa. */
			HddSupportSetEnabled(!HddSupportIsEnabled());
			break;

		case 12: /* MMCE (mmce0/1) on/off -- lista + carga preguicosa. */
			MmceSupportSetEnabled(!MmceSupportIsEnabled());
			if (MmceSupportIsEnabled())
			{
				BgmIOBegin();
				MmceProbeAvailableSlots();
				BgmIOEnd();
			}
			break;

		case 13: /* SMB on/off. Driver/network stay lazy until smb: is opened. */
			if (SmbSupportIsEnabled())
			{
				BgmIOBegin();
				SmbDisconnect();
				BgmIOEnd();
				SmbSupportSetEnabled(0);
			}
			else
			{
				SmbSupportSetEnabled(1);
			}
			break;

		case 14: /* MX4SIO (SD via SIO2) on/off -- carga preguicosa (deferida).
		            Padrao OFF: quem nao tem o adaptador evita o flood de
		            sondagem do SIO2.  Independente do Mass/USB. */
			Mx4sioSetEnabled(!Mx4sioIsEnabled());
			if (Mx4sioIsEnabled())
			{
				BgmIOBegin();
				Mx4sioLoadIfEnabled();
				BgmIOEnd();
			}
			break;

case 15: /* SRAM Size */
    switch (g_FakeSRAMSize)
    {
        case 0:
            g_FakeSRAMSize = (dir > 0) ? 8 : 2048;
            break;

        case 8:
            g_FakeSRAMSize = (dir > 0) ? 16 : 0;
            break;

        case 16:
            g_FakeSRAMSize = (dir > 0) ? 32 : 8;
            break;

        case 32:
            g_FakeSRAMSize = (dir > 0) ? 64 : 16;
            break;

        case 64:
            g_FakeSRAMSize = (dir > 0) ? 128 : 32;
            break;

        case 128:
            g_FakeSRAMSize = (dir > 0) ? 256 : 64;
            break;

        case 256:
            g_FakeSRAMSize = (dir > 0) ? 512 : 128;
            break;

        case 512:
            g_FakeSRAMSize = (dir > 0) ? 1024 : 256;
            break;

        case 1024:
            g_FakeSRAMSize = (dir > 0) ? 2048 : 512;
            break;

        case 2048:
        default:
            g_FakeSRAMSize = (dir > 0) ? 0 : 1024;
            break;
    }
    break;

case 16: /* Force Region */
    if (dir > 0)
    {
        switch (g_SnesForceRegion)
        {
            case SNES_FORCE_REGION_OFF:
                g_SnesForceRegion = SNES_FORCE_REGION_NTSC_U;
                break;

            case SNES_FORCE_REGION_NTSC_U:
                g_SnesForceRegion = SNES_FORCE_REGION_NTSC_J;
                break;

            case SNES_FORCE_REGION_NTSC_J:
                g_SnesForceRegion = SNES_FORCE_REGION_PAL;
                break;

            case SNES_FORCE_REGION_PAL:
            default:
                g_SnesForceRegion = SNES_FORCE_REGION_OFF;
                break;
        }
    }
    else
    {
        switch (g_SnesForceRegion)
        {
            case SNES_FORCE_REGION_OFF:
                g_SnesForceRegion = SNES_FORCE_REGION_PAL;
                break;

            case SNES_FORCE_REGION_NTSC_U:
                g_SnesForceRegion = SNES_FORCE_REGION_OFF;
                break;

            case SNES_FORCE_REGION_NTSC_J:
                g_SnesForceRegion = SNES_FORCE_REGION_NTSC_U;
                break;

            case SNES_FORCE_REGION_PAL:
            default:
                g_SnesForceRegion = SNES_FORCE_REGION_NTSC_J;
                break;
        }
    }
    break;
case 17: /* Famiclone Audio */
    g_FamicloneAudio = !g_FamicloneAudio;
    QuicknesBridge_SetDutySwap(g_FamicloneAudio ? true : false);
    break;

		case 19: case 20: case 21: case 22: case 23:
		{
			static const Uint8 kLayers[5] = {
				SNESPPU_MASK_BG1, SNESPPU_MASK_BG2, SNESPPU_MASK_BG3,
				SNESPPU_MASK_BG4, SNESPPU_MASK_OBJ
			};
			Uint8 uMask = SNPPURenderGetSoftwareLayerMask();
			uMask ^= kLayers[m_iSelect - 19];
			SNPPURenderSetSoftwareLayerMask(uMask);
			break;
		}
		case 24:
			SNPPURenderSetSoftwareHackFlags(
				SNPPURenderGetSoftwareHackFlags() ^ SNPPU_HACK_COLOR_MATH_OFF);
			break;
		case 25:
			SNPPURenderSetSoftwareHackFlags(
				SNPPURenderGetSoftwareHackFlags() ^ SNPPU_HACK_WINDOWS_OFF);
			break;
		case 26:
			SNPPURenderSetSoftwareHackFlags(
				SNPPURenderGetSoftwareHackFlags() ^ SNPPU_HACK_MODE7_HALF);
			break;
		case 27:
			{
				Int32 level = (Int32)SNPPURenderGetObjLimitLevel() + dir;
				if (level < 0) level = SNPPU_OBJ_LIMIT_NUM - 1;
				if (level >= SNPPU_OBJ_LIMIT_NUM) level = 0;
				SNPPURenderSetObjLimitLevel((Uint8)level);
			}
			break;
		case 28:
			{
				Int32 mode = (Int32)SNPPURenderGetObjLimitMode() + dir;
				if (mode < 0) mode = SNPPU_OBJ_LIMIT_MODE_NUM - 1;
				if (mode >= SNPPU_OBJ_LIMIT_MODE_NUM) mode = 0;
				SNPPURenderSetObjLimitMode((Uint8)mode);
			}
			break;
		case 29:
		{
			Int32 level = (Int32)SNCPUGetOverclockLevel() + dir;
			if (level < 0) level = SNCPU_OVERCLOCK_NUM - 1;
			if (level >= SNCPU_OVERCLOCK_NUM) level = SNCPU_OVERCLOCK_OFF;
			SNCPUSetOverclockLevel(_pSnes ? _pSnes->GetCpu() : NULL, (Uint8)level);
		}
			break;
		case 30:
			_VideoApplyCompatFlags(
				g_VideoCompatFlags == VIDEO_COMPAT_ALL ? 0 : VIDEO_COMPAT_ALL);
			break;
		case 31:
			_VideoApplyCompatFlags(
				g_VideoCompatFlags ^ VIDEO_COMPAT_GS_FULL_CACHE);
			break;
		case 32:
			_VideoApplyCompatFlags(
				g_VideoCompatFlags ^ VIDEO_COMPAT_GIF_LONG_WAIT);
			break;
		case 33:
			_VideoApplyCompatFlags(
				g_VideoCompatFlags ^ VIDEO_COMPAT_AUDIO_SMALL_RPC);
			break;
		case 34:
			_VideoApplyCompatFlags(
				g_VideoCompatFlags ^ VIDEO_COMPAT_AUDIO_DEEP_Q);
			break;
		case 35:
			SNPPURenderSetSoftwareHackFlags(
				SNPPURenderGetSoftwareHackFlags() ^ SNPPU_HACK_FRAME_SKIP);
			break;
		case 36:
			InputSnesMouseCycleModeDir(dir);
			break;
		case 37:
			MainLoopTurboCycleSpeedDir(dir);
			break;
		}


	}

	/* AURORA_RUNTIME_SAFE_VIDEO_PAGES_V1_4_1
	 * Square: previous page using the v24 0/10/19/30 boundaries. */
	if (trigger & PAD_SQUARE)
	{
		if (m_iSelect >= 30)      m_iSelect = 19;
		else if (m_iSelect >= 19) m_iSelect = 10;
		else if (m_iSelect >= 10) m_iSelect = 0;
		else                      m_iSelect = 30;
	}

/* Cross / Start: persist all video settings to the memory card. */
if (trigger & (PAD_CROSS | PAD_START))
{
    VideoSettingsSave();

if (m_iSelect == 18)
{
    MainResetEmulator();
}
}
}
