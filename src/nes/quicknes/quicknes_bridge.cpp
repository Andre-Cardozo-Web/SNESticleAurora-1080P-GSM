/* SNESTICLE_QUICKNES_BRIDGE
 * SNESTICLE_QUICKNES_NATIVE_DIRECT_V1
 *
 * Direct QuickNES Nes_Emu integration for SNESticle/PS2.
 *
 * IMPORTANT: This intentionally does NOT call the libretro frontend at
 * runtime. The QuickNES archive still contains libretro.o, but normal static
 * archive linking only extracts objects referenced by SNESticle. Since this
 * file references Nes_Emu directly and no retro_* symbol, libretro.o stays
 * out of the final ELF.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "quicknes_bridge.h"
#include "types.h"
#include "emuinput.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"
#include "snio.h"
#include "snppurender.h"
#include "input.h" /* AURORA_QN_ARKANOID_ANALOG_V2_20260828 */

/* AURORA_SNES9X2010_V5_ALLCORES_PERF_20260824 */
extern "C" {
#include "gs.h"
#include "gpprim.h"
}
/* SNESTICLE_QUICKNES_PS2_ALIGNMENT_V1
 * Keep the bridge under the same unaligned-access contract used by
 * QuickNES's PS2 objects. */
#ifndef NO_UNALIGNED_ACCESS
#define NO_UNALIGNED_ACCESS 1
#endif

#include "Nes_Emu.h"
#include "Nes_Buffer.h"
#include "Data_Reader.h"
#include "abstract_file.h"
#include "nes_ntsc.h" /* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827 */

extern "C" void quicknes_snesticle_set_duty_swap(int enable);
extern "C" void quicknes_snesticle_set_microphone(int enable); /* AURORA_FAMICOM_MIC_CFG41_20260828 */

/* AURORA_QN_EXT_HOST_V2_20260828 */
extern "C" void quicknes_snesticle_ext_set_arkanoid(int enable);
extern "C" void quicknes_snesticle_ext_set_arkanoid_state(unsigned int paddle, int fire);
extern "C" void quicknes_snesticle_ext_reset_bus(void);
extern "C" unsigned char *quicknes_snesticle_ext_turbofile_data(void);
extern "C" int quicknes_snesticle_ext_turbofile_dirty(void);
extern "C" void quicknes_snesticle_ext_turbofile_clear_dirty(void);

static bool s_Initialized = false;
static bool s_GameLoaded  = false;
static bool s_DutySwap    = false;
static bool s_TurboPhase  = false;
static bool s_SkipVideoNext = false; /* AURORA_SAFE_FRAMESKIP_GG_ZOOM_V2_2 */
static bool s_ArkanoidVaus = false; /* AURORA_QN_EXT_HOST_V2_20260828 */
/* AURORA_CONTROLLER_OPTIONS_V2: Max/Half/Quarter cadence. */
static unsigned s_TurboSpeedShift = 0;
static uint32_t s_TurboFrame = 0;

/* AURORA_V18_SAFE_PERF_QUICKNES_LIMITER_CACHE_20260825
 * set_sprite_limits() only writes the two rendering-limit fields after
 * clamping. Cache the exact final arguments and touch the core only when
 * those derived budgets really change. */
static int s_LastSpriteScanlineLimit = -1;
static int s_LastSpriteScreenLimit = -1;

/* Reuse one native emulator for the process lifetime. Its constructor does
 * not allocate the cart/audio buffers; those are initialized lazily by
 * QuicknesBridge_Init()/LoadGame(). Reuse avoids libretro's global
 * current_buffer lifecycle entirely. */
static Nes_Emu    *s_pEmu = NULL;
static Nes_Buffer *s_pAudioBuffer = NULL;

enum
{
    QN_VIDEO_W = Nes_Emu::buffer_width,
    /* This matches QuickNES's own libretro software frontend allocation. */
    QN_VIDEO_H = Nes_Emu::image_height + 2,
    QN_AUDIO_MAX = 4096
};

/* QuickNES renders palette indices into this buffer. The +16 horizontal
 * padding and +2 vertical rows are required by its native PPU renderer. */
/* Keep eight bytes before the QuickNES allocation so the active
 * frame pointer (base + one 272-byte guard row + left 8) is
 * 16-byte aligned for the GIF DMA source. */
static Uint8 s_Video[QN_VIDEO_W * QN_VIDEO_H + 16]
    __attribute__((aligned(64)));

/* SNESticle's framebuffer is 256x256 RGBA8. Cache the 256-entry mapping
 * from QuickNES host-palette indices to native RGBA8 words. */
static Uint32 s_RgbaPalette[256];
static short  s_LastFramePalette[Nes_Emu::max_palette_size];
static bool   s_PaletteValid = false;

/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: raw 64xRGB .pal expanded by QuickNES's own nes_ntsc. */
static bool s_CustomPaletteValid = false;
static Uint8 s_CustomBasePalette[64 * 3];
static Uint8 s_CustomExpandedPalette[Nes_Emu::color_table_size * 3];
static void qGetRgb(unsigned ci, Uint8 *r, Uint8 *g, Uint8 *b)
{
    if (ci >= (unsigned)Nes_Emu::color_table_size) ci = 0;
    if (s_CustomPaletteValid)
    {
        const Uint8 *p = s_CustomExpandedPalette + ci * 3;
        *r=p[0]; *g=p[1]; *b=p[2];
    }
    else
    {
        const Nes_Emu::rgb_t &rgb = Nes_Emu::nes_colors[ci];
        *r=rgb.red; *g=rgb.green; *b=rgb.blue;
    }
}


/* AURORA_SNES9X2010_V5_ALLCORES_PERF_20260824
 * Direct indexed-video state. Palette entries are kept in GS
 * CSM1 order; texture and CLUT live in Aurora's reserved output slab. */
static Uint32 s_GsPalette[256] __attribute__((aligned(64)));
static short s_DirectLastPalette[Nes_Emu::max_palette_size];
static bool s_DirectPaletteValid = false;
static bool s_DirectClutResident = false;
static bool s_DirectReady = false;
static Uint32 s_DirectFrameSerial = 0;
static Uint32 s_DirectUploadSerial = 0;
/* Audio scratch is BSS/static, never EE thread stack. QuickNES's default
 * non-linear Nes_Buffer emits mono; SNESticle's AudMixBuffer duplicates it
 * to stereo and performs the existing 32 -> 48 kHz conversion. */
/* AURORA_V3_SAFE_QN_DIRECT_AUDIO_DECL_20260828 */
static Int16 s_AudioOut[QN_AUDIO_MAX + 4];
static Int16 s_Pending[4];
static int   s_PendingCount = 0;



/* AURORA_QN_EXT_HOST_V2_20260828
 * Mednafen/NesCartDB identify the Japanese Famicom Arkanoid payload as
 * CRC32 D89E5A67. Compute the CRC over PRG+CHR (not the 16-byte iNES header),
 * matching the combined cartridge-ROM CRC convention used by that database.
 */
static Uint32 qCrc32(const Uint8 *pData, size_t nBytes)
{
    Uint32 crc = 0xFFFFFFFFU;
    while (nBytes--)
    {
        crc ^= *pData++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

static Uint32 qNesPayloadCrc32(const void *pData, size_t nBytes)
{
    const Uint8 *rom = (const Uint8 *)pData;
    size_t offset, payload;

    if (!rom || nBytes < 16 ||
        rom[0] != 'N' || rom[1] != 'E' ||
        rom[2] != 'S' || rom[3] != 0x1A)
        return 0;

    offset = 16U + ((rom[6] & 0x04U) ? 512U : 0U);
    payload = (size_t)rom[4] * 16384U +
              (size_t)rom[5] * 8192U;
    if (payload == 0 || offset > nBytes || payload > nBytes - offset)
        return 0;

    return qCrc32(rom + offset, payload);
}

static void qUpdateArkanoidVaus(Emu::SysInputT *pInput)
{
    unsigned axis = 0x80U;
    unsigned paddle;
    int fire = 0;

    if (!s_ArkanoidVaus)
        return;

    /* Absolute left-stick X -> authentic Vaus working range 0x54..0xF4. */
    if (InputIsPadConnected(0))
        axis = (InputGetPadAnalog(0) >> 16) & 0xFFU;

    paddle = 0x54U +
        (axis * (0xF4U - 0x54U) + 127U) / 255U;

    /* PS2 X/Cross is sampled directly as Vaus Fire. RunFrame removes its
     * ordinary NES-A mirror while Arkanoid J is active. */
    if (pInput && pInput->uPad[0] != EMUSYS_DEVICE_DISCONNECTED &&
        (pInput->uPad[0] & SNESIO_JOY_B))
        fire = 1;

    quicknes_snesticle_ext_set_arkanoid_state(paddle, fire);
}

static void qResetDirectVideo(void)
{
    memset(s_DirectLastPalette, 0, sizeof(s_DirectLastPalette));
    s_DirectPaletteValid = false;
    s_DirectClutResident = false;
    s_DirectReady = false;
    s_DirectFrameSerial = 0;
    s_DirectUploadSerial = 0;
}

static void qResetTransient(void)
{
    memset(s_Video, 0, sizeof(s_Video));
    memset(s_LastFramePalette, 0, sizeof(s_LastFramePalette));
    memset(s_Pending, 0, sizeof(s_Pending));
    s_PendingCount = 0;
    s_PaletteValid = false;
    qResetDirectVideo();
    s_TurboPhase = false;
    s_TurboFrame = 0;
    s_SkipVideoNext = false;
    s_LastSpriteScanlineLimit = -1;
    s_LastSpriteScreenLimit = -1;
}

static Uint8 qMapPad(Uint16 pad)
{
    Uint8 nes = 0;

    /* SysInputT uses this sentinel for a physically absent controller.
     * Treating it as a bitfield would otherwise press every NES button. */
    if (pad == EMUSYS_DEVICE_DISCONNECTED)
        return 0;

    if (pad & SNESIO_JOY_B)      nes |= 0x01; /* Cross  -> NES A */
    if (pad & SNESIO_JOY_Y)      nes |= 0x02; /* Square -> NES B */

    if ((pad & SNESIO_JOY_A) && s_TurboPhase)
        nes |= 0x01;                         /* Circle   -> turbo A */
    if ((pad & SNESIO_JOY_X) && s_TurboPhase)
        nes |= 0x02;                         /* Triangle -> turbo B */

    if (pad & SNESIO_JOY_SELECT) nes |= 0x04;
    if (pad & SNESIO_JOY_START)  nes |= 0x08;
    if (pad & SNESIO_JOY_UP)     nes |= 0x10;
    if (pad & SNESIO_JOY_DOWN)   nes |= 0x20;
    if (pad & SNESIO_JOY_LEFT)   nes |= 0x40;
    if (pad & SNESIO_JOY_RIGHT)  nes |= 0x80;

    /* Same policy as the old QuickNES option "up_down_allowed=disabled". */
    if ((nes & 0x10) && (nes & 0x20))
        nes &= (Uint8)~(0x10 | 0x20);
    if ((nes & 0x40) && (nes & 0x80))
        nes &= (Uint8)~(0x40 | 0x80);

    return nes;
}

static void qRenderFrame(CRenderSurface *pTarget)
{
    if (!pTarget)
        return;

    PixelFormatT *fmt = pTarget->GetFormat();
    if (!fmt || fmt->uBitDepth != 32)
        return;

    Uint32 width  = pTarget->GetWidth();
    Uint32 height = pTarget->GetHeight();
    if (width < (Uint32)Nes_Emu::image_width ||
        height < (Uint32)Nes_Emu::image_height)
        return;

    const Nes_Emu::frame_t &frame = s_pEmu->frame();
    if (!frame.pixels || frame.pitch <= 0)
        return;

    if (!s_PaletteValid ||
        memcmp(s_LastFramePalette, frame.palette,
               sizeof(s_LastFramePalette)) != 0)
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            unsigned ci = (unsigned)(unsigned short)frame.palette[i];
            if (ci >= (unsigned)Nes_Emu::color_table_size)
                ci = 0;

            Uint8 r, g, b;
            qGetRgb(ci, &r, &g, &b);

            /* CRenderSurface PIXELFORMAT_RGBA8 on little-endian EE is
             * bytes R,G,B,A -> integer 0xAABBGGRR. */
            s_RgbaPalette[i] = 0xff000000u | ((Uint32)b << 16) | ((Uint32)g << 8) | (Uint32)r;
        }

        memcpy(s_LastFramePalette, frame.palette,
               sizeof(s_LastFramePalette));
        s_PaletteValid = true;
    }

    for (int y = 0; y < Nes_Emu::image_height; ++y)
    {
        const Uint8 *src = frame.pixels + (long)y * frame.pitch;
        Uint32 *dst = (Uint32 *)pTarget->GetLinePtr(y);
        if (!dst)
            continue;

        for (int x = 0; x < Nes_Emu::image_width; ++x)
            dst[x] = s_RgbaPalette[src[x]];
    }

    /* SNESticle uploads a 256x256 texture; NES only owns rows 0..239. */
    Uint32 clearWidth = width;
    if (clearWidth > (Uint32)Nes_Emu::image_width)
        clearWidth = (Uint32)Nes_Emu::image_width;
    for (Uint32 y = Nes_Emu::image_height; y < height && y < 256; ++y)
    {
        Uint8 *dst = pTarget->GetLinePtr((Int32)y);
        if (dst)
            memset(dst, 0, clearWidth * 4);
    }
}

enum
{
    QN_GS_TEX_TBP_OFFSET = 0x400,
    QN_GS_CLUT_TBP_OFFSET = 0x580,
    QN_GS_T8_TBW = 320
};

void QuicknesBridge_InvalidateGsResources(void)
{
    s_DirectUploadSerial = 0;
    s_DirectClutResident = false;
}

bool QuicknesBridge_CanDirectGsVideo(void)
{
    return s_GameLoaded && s_DirectReady && s_DirectFrameSerial != 0;
}

static void qRefreshGsPalette(const Nes_Emu::frame_t &frame)
{
    if (s_DirectPaletteValid &&
        memcmp(s_DirectLastPalette, frame.palette,
               sizeof(s_DirectLastPalette)) == 0)
        return;

    for (unsigned i = 0; i < 256; ++i)
    {
        unsigned ci = (unsigned)(unsigned short)frame.palette[i];
        unsigned dest = i;
        unsigned block = i & 0x3fU;
        if (ci >= (unsigned)Nes_Emu::color_table_size)
            ci = 0;

        /* GS CSM1 swaps each 8-entry 0x08/0x10 CLUT block. */
        if ((block & 0x18U) == 0x08U)
            dest += 8U;
        else if ((block & 0x18U) == 0x10U)
            dest -= 8U;

        Uint8 r, g, b;
        qGetRgb(ci, &r, &g, &b);
        s_GsPalette[dest] = 0x80000000u | ((Uint32)b << 16) | ((Uint32)g << 8) | (Uint32)r;
    }

    memcpy(s_DirectLastPalette, frame.palette,
           sizeof(s_DirectLastPalette));
    s_DirectPaletteValid = true;
    s_DirectClutResident = false;
}

bool QuicknesBridge_DrawDirectGs(Uint32 auroraOutBaseTBP,
                                 Int32 logicalY,
                                 Float32 intensity)
{
    Uint32 texTBP, clutTBP, mod, modColor;
    const Nes_Emu::frame_t &frame = s_pEmu->frame();

    if (!auroraOutBaseTBP || !QuicknesBridge_CanDirectGsVideo() ||
        !frame.pixels || frame.pitch != QN_VIDEO_W)
        return false;

    texTBP = auroraOutBaseTBP + QN_GS_TEX_TBP_OFFSET;
    clutTBP = auroraOutBaseTBP + QN_GS_CLUT_TBP_OFFSET;

    if (s_DirectUploadSerial != s_DirectFrameSerial)
    {
        /* Transfer the native 272-byte stride. UV 0..256 selects only the
         * active image; the final 16 bytes are QuickNES's guard/padding. */
        GPPrimUploadTexture((int)texTBP, QN_GS_T8_TBW,
                            0, 0, GS_PSMT8,
                            frame.pixels, QN_VIDEO_W,
                            Nes_Emu::image_height);
        s_DirectUploadSerial = s_DirectFrameSerial;
    }

    qRefreshGsPalette(frame);
    if (!s_DirectClutResident)
    {
        GPPrimUploadTexture((int)clutTBP, 64,
                            0, 0, GS_PSMCT32,
                            s_GsPalette, 16, 16);
        s_DirectClutResident = true;
    }

    GPPrimSetTex(texTBP, QN_GS_T8_TBW, 9, 8, GS_PSMT8,
                 clutTBP, 64, GS_PSMCT32, 0);

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    mod = (Uint32)(128.0f * intensity + 0.5f);
    if (mod > 128u) mod = 128u;
    modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;

    /* Same logical rectangles and half-texel UVs as the old generic branch:
       Y=2 in 240p, Y=4 in 480i/1080i, always native 256x240. */
    GPPrimTexRect(0, (Uint32)logicalY << 4, 8, 8,
                  256u << 4, (Uint32)(logicalY + 240) << 4,
                  (256u << 4) + 8u, (240u << 4) + 8u,
                  10u << 4, modColor, 0);
    return true;
}


static void qDrainAudio(CMixBuffer *pMix)
{
    long count;

    if (!pMix)
    {
        /* QuickNES explicitly supports NULL here; drain the complete frame so
         * the internal Blip buffer never accumulates into the next frame. */
        s_pEmu->read_samples(NULL, QN_AUDIO_MAX);
        s_PendingCount = 0;
        return;
    }

    /* AURORA_V3_SAFE_QN_DIRECT_AUDIO_DRAIN_20260828:
     * preserve the 0..3-sample prefix and let QuickNES write directly after it. */
    if (s_PendingCount > 0)
        memcpy(s_AudioOut, s_Pending,
               (size_t)s_PendingCount * sizeof(s_Pending[0]));

    count = s_pEmu->read_samples(
        (short *)(s_AudioOut + s_PendingCount), QN_AUDIO_MAX);
    if (count <= 0)
    {
        pMix->Flush();
        return;
    }
    if (count > QN_AUDIO_MAX)
        count = QN_AUDIO_MAX;

    int n = s_PendingCount + (int)count;

    /* AudMixBuffer's existing 32->48 kHz path expects input counts divisible
     * by four. Keep only the tail (0..3 samples) for the next frame. */
    int flush = n & ~3;
    s_PendingCount = n - flush;
    if (s_PendingCount > 0)
        memcpy(s_Pending, s_AudioOut + flush,
               (size_t)s_PendingCount * sizeof(s_Pending[0]));

    if (flush > 0)
        pMix->OutputSamplesMono(s_AudioOut, flush);
    pMix->Flush();
}

bool QuicknesBridge_Init(void)
{
    if (s_Initialized)
        return true;

    s_pEmu = new Nes_Emu();
    if (!s_pEmu)
        return false;

    s_pAudioBuffer = new Nes_Buffer();
    if (!s_pAudioBuffer)
    {
        delete s_pEmu;
        s_pEmu = NULL;
        return false;
    }

    const char *err = s_pEmu->set_sample_rate(32000, s_pAudioBuffer);
    if (err)
    {
        printf("[QuickNES/native] set_sample_rate failed: %s\n", err);
        delete s_pAudioBuffer;
        s_pAudioBuffer = NULL;
        delete s_pEmu;
        s_pEmu = NULL;
        return false;
    }

    s_pEmu->set_palette_range(0);
    s_pEmu->set_sprite_mode(Nes_Emu::sprites_visible);
    s_pEmu->set_pixels(s_Video + 8, QN_VIDEO_W);

    quicknes_snesticle_set_duty_swap(s_DutySwap ? 1 : 0);
    qResetTransient();
    s_Initialized = true;
    return true;
}

void QuicknesBridge_Shutdown(void)
{
    if (s_pEmu)
        s_pEmu->close();

    delete s_pAudioBuffer;
    s_pAudioBuffer = NULL;

    delete s_pEmu;
    s_pEmu = NULL;

    s_GameLoaded = false;
    qResetTransient();
    s_Initialized = false;
}

bool QuicknesBridge_LoadGame(const void *pData, size_t nBytes, const char *pName)
{
    (void)pName;

    if (!pData || nBytes < 16 || !QuicknesBridge_Init())
        return false;

    if (s_GameLoaded)
        QuicknesBridge_UnloadGame();

    qResetTransient();
    s_pEmu->set_pixels(s_Video + 8, QN_VIDEO_W);

    Mem_File_Reader reader(pData, (long)nBytes);
    const char *err = s_pEmu->load_ines(reader);
    if (err)
    {
        printf("[QuickNES/native] load_ines failed: %s\n", err);
        s_pEmu->close();
        s_GameLoaded = false;
        return false;
    }

    quicknes_snesticle_set_duty_swap(s_DutySwap ? 1 : 0);

    /* AURORA_QN_EXT_HOST_V2_20260828: Japanese Arkanoid only.
     * US Arkanoid (32FB0583) and Arkanoid II (0F141525) intentionally do
     * not trigger this automatic EXT exception. */
    {
        const Uint32 payloadCrc = qNesPayloadCrc32(pData, nBytes);
        s_ArkanoidVaus = (payloadCrc == 0xD89E5A67U ||
                      payloadCrc == 0x0F141525U);
        quicknes_snesticle_ext_set_arkanoid(s_ArkanoidVaus ? 1 : 0);
        quicknes_snesticle_ext_reset_bus();
        if (s_ArkanoidVaus)
            printf("[QuickNES/EXT] Arkanoid (Japan) Vaus enabled; CRC=%08X\n",
                   (unsigned)payloadCrc);
    }

    s_GameLoaded = true;

    /* Do not serialize merely to discover state size during ROM load. */
    printf("[QuickNES/native] loaded %u bytes; SRAM=%d\n",
           (unsigned)nBytes, QuicknesBridge_GetSRAMBytes());
    return true;
}

void QuicknesBridge_UnloadGame(void)
{
    if (s_Initialized && s_pEmu)
        s_pEmu->close();
    s_GameLoaded = false;
    s_ArkanoidVaus = false;
    quicknes_snesticle_ext_set_arkanoid(0);
    qResetTransient();
}

void QuicknesBridge_Reset(void)
{
    if (s_GameLoaded)
        s_pEmu->reset(true, false);       /* power-cycle style reset */
    s_PendingCount = 0;
    s_PaletteValid = false;
    qResetDirectVideo();
    s_TurboPhase = false;
    s_TurboFrame = 0;
    s_LastSpriteScanlineLimit = -1;
    s_LastSpriteScreenLimit = -1;
    quicknes_snesticle_ext_reset_bus();
}

void QuicknesBridge_SoftReset(void)
{
    if (s_GameLoaded)
        s_pEmu->reset(false, false);      /* NES RESET button */
    s_PendingCount = 0;
    s_PaletteValid = false;
    qResetDirectVideo();
    s_TurboPhase = false;
    s_TurboFrame = 0;
    s_LastSpriteScanlineLimit = -1;
    s_LastSpriteScreenLimit = -1;
    quicknes_snesticle_ext_reset_bus();
}

void QuicknesBridge_SetDutySwap(bool enabled)
{
    s_DutySwap = enabled;
    quicknes_snesticle_set_duty_swap(enabled ? 1 : 0);
}

/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: exact QuickNES libretro-style custom palette expansion. */
bool QuicknesBridge_SetPalette(const Uint8 *rgb192)
{
    nes_ntsc_setup_t setup;
    if (!rgb192) return false;
    memcpy(s_CustomBasePalette, rgb192, sizeof(s_CustomBasePalette));
    setup = nes_ntsc_rgb;
    setup.palette = NULL;
    setup.base_palette = s_CustomBasePalette;
    setup.palette_out = s_CustomExpandedPalette;
    nes_ntsc_init(NULL, &setup);
    s_CustomPaletteValid = true;
    s_PaletteValid = false;
    s_DirectPaletteValid = false;
    s_DirectClutResident = false;
    return true;
}

void QuicknesBridge_SetTurboSpeed(unsigned speedShift)
{
    /* AURORA_TURBO_SPEED_REARM_V1 */
    unsigned next = (speedShift <= 2U) ? speedShift : 0U;
    if (s_TurboSpeedShift != next)
    {
        s_TurboSpeedShift = next;
        s_TurboFrame = 0;
        s_TurboPhase = true;
    }
}

void QuicknesBridge_SetSkipVideo(bool skip)
{
    s_SkipVideoNext = skip;
}

void QuicknesBridge_RunFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf)
{
    const bool skipVideo = s_SkipVideoNext;
    s_SkipVideoNext = false;

    if (!s_GameLoaded || !s_pEmu)
        return;

    /* AURORA_V15_MULTICORE_SPRITE_LIMIT_20260824
     * Off keeps the NES hardware renderer at 8 sprites/scanline and all 64
     * OAM entries. Scanline levels are the SNES severity ratios scaled to 8.
     * Screen mode keeps the native 8/line rule and caps the first distinct
     * OAM entries for the whole picture, matching the intent of SNES mode. */
    {
        static const int scanlineBudget[SNPPU_OBJ_LIMIT_NUM] =
            { 8, 7, 6, 5, 4, 3, 2 };
        static const int screenBudget[SNPPU_OBJ_LIMIT_NUM] =
            { 64, 28, 24, 20, 16, 12, 8 };
        int level = (int)SNPPURenderGetObjLimitLevel();
        int mode = (int)SNPPURenderGetObjLimitMode();
        int scanline = 8;
        int screen = 64;

        if (level > SNPPU_OBJ_LIMIT_OFF && level < SNPPU_OBJ_LIMIT_NUM)
        {
            if (mode == SNPPU_OBJ_LIMIT_MODE_SCREEN)
                screen = screenBudget[level];
            else
                scanline = scanlineBudget[level];
        }
        if (scanline != s_LastSpriteScanlineLimit ||
            screen != s_LastSpriteScreenLimit)
        {
            s_pEmu->set_sprite_limits(scanline, screen);
            s_LastSpriteScanlineLimit = scanline;
            s_LastSpriteScreenLimit = screen;
        }
    }

    Uint8 p1 = 0;
    Uint8 p2 = 0;
    s_TurboPhase = (((s_TurboFrame >> s_TurboSpeedShift) & 1U) == 0U);
    ++s_TurboFrame;

    /* AURORA_FAMICOM_MIC_CFG41_20260828:
     * SNESIO_JOY_L is a host-only NES carrier; qMapPad intentionally ignores
     * it. The QuickNES core exposes it as the Famicom pad-II mic on $4016 D2. */
    quicknes_snesticle_set_microphone(
        (pInput &&
         pInput->uPad[0] != EMUSYS_DEVICE_DISCONNECTED &&
         (pInput->uPad[0] & SNESIO_JOY_L)) ? 1 : 0);

    if (pInput)
    {
        p1 = qMapPad(pInput->uPad[0]);
        p2 = qMapPad(pInput->uPad[1]);
    }

    /* AURORA_QN_EXT_HOST_V2_20260828: Vaus is an absolute analog device.
     * Cross belongs to the paddle Fire line for Arkanoid J; do not mirror
     * that same press onto the ordinary controller's NES-A bit. */
    if (s_ArkanoidVaus)
        p1 &= (Uint8)~0x01U;
    qUpdateArkanoidVaus(pInput);

    const char *err = skipVideo
        ? s_pEmu->emulate_skip_frame((int)p1, (int)p2)
        : s_pEmu->emulate_frame((int)p1, (int)p2);
    if (err)
    {
        printf("[QuickNES/native] emulate_frame failed: %s\n", err);
        qDrainAudio(pMixBuf);
        return;
    }

    if (!skipVideo)
    {
        const Nes_Emu::frame_t &frame = s_pEmu->frame();
        s_DirectReady =
            frame.pixels && frame.pitch == QN_VIDEO_W &&
            (((uintptr_t)frame.pixels & 15u) == 0u);
        if (s_DirectReady)
        {
            if (++s_DirectFrameSerial == 0)
            {
                s_DirectFrameSerial = 1;
                s_DirectUploadSerial = 0;
            }
        }
        else
            qRenderFrame(pTarget);
    }
    /* On skip, DirectReady/serial and the fallback surface intentionally
     * remain the last displayed frame. Audio is still drained. */
    qDrainAudio(pMixBuf);
}

int QuicknesBridge_GetStateSize(void)
{
    /*
     * Capacity only. Do NOT invoke save_state() here.
     *
     * The previous implementation serialized once to determine the
     * size and then serialized a second time for the actual save.
     */
    return s_GameLoaded ? QUICKNES_STATE_CAPACITY : 0;
}

int QuicknesBridge_SaveState(void *pData, int nBytes)
{
    if (!s_GameLoaded || !s_pEmu || !pData || nBytes <= 0)
        return 0;

    if (nBytes > QUICKNES_STATE_CAPACITY)
        nBytes = QUICKNES_STATE_CAPACITY;

    /*
     * Exactly one native serialization.
     *
     * Mem_Writer is fixed-size here: no realloc, no expanding heap
     * buffer, and an oversized state fails cleanly.
     */
    Mem_Writer writer(pData, (long)nBytes);

    printf("[QuickNES/native] save_state begin; capacity=%d\n", nBytes);

    const char *err = s_pEmu->save_state(writer);
    long written = writer.size();

    if (err || written <= 0 || written > nBytes)
    {
        printf("[QuickNES/native] save_state failed: %s (%ld/%d)\n",
               err ? err : "invalid size", written, nBytes);
        return 0;
    }

    printf("[QuickNES/native] save_state OK; bytes=%ld\n", written);
    return (int)written;
}

bool QuicknesBridge_LoadState(const void *pData, int nBytes)
{
    if (!s_GameLoaded || !pData || nBytes <= 0)
        return false;

    Mem_File_Reader reader(pData, (long)nBytes);
    const char *err = s_pEmu->load_state(reader);
    if (err)
    {
        printf("[QuickNES/native] load_state failed: %s\n", err);
        return false;
    }

    /* Nes_Emu::load_state() clears/fades its sound buffer itself. Do not
     * restore a process-local audio snapshot that is not part of the state
     * file (it could belong to a different save slot). */
    s_PendingCount = 0;
    s_PaletteValid = false;
    qResetDirectVideo();
    /* EXT serial position is host peripheral state, not part of the legacy
     * QuickNES snapshot. Restart the transfer bus deterministically. */
    quicknes_snesticle_ext_reset_bus();
    return true;
}

int QuicknesBridge_GetSRAMBytes(void)
{
    if (!s_GameLoaded || !s_pEmu->cart() || !s_pEmu->has_battery_ram())
        return 0;
    return (int)s_pEmu->battery_ram_size();
}

uint8_t *QuicknesBridge_GetSRAMData(void)
{
    if (QuicknesBridge_GetSRAMBytes() <= 0)
        return NULL;
    return s_pEmu->high_mem();
}

bool QuicknesBridge_IsArkanoidVaus(void)
{
    return s_ArkanoidVaus;
}

/* AURORA_QN_EXT_HOST_V2_20260828: 8 KiB ASCII Turbo File battery RAM. */
int QuicknesBridge_GetTurboFileBytes(void)
{
    return 0x2000;
}

uint8_t *QuicknesBridge_GetTurboFileData(void)
{
    return quicknes_snesticle_ext_turbofile_data();
}

bool QuicknesBridge_TurboFileDirty(void)
{
    return quicknes_snesticle_ext_turbofile_dirty() != 0;
}

void QuicknesBridge_ClearTurboFileDirty(void)
{
    quicknes_snesticle_ext_turbofile_clear_dirty();
}

unsigned QuicknesBridge_GetSampleRate(void)
{
    return 32000;
}
