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

/* AURORA_QN_DIRECT_T8_GS_V4
 * QuickNES already renders native palette indices. Keep them indexed all the
 * way to GS instead of expanding 256x240 pixels to RGBA32 on the EE. */
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

extern "C" void quicknes_snesticle_set_duty_swap(int enable);

static bool s_Initialized = false;
static bool s_GameLoaded  = false;
static bool s_DutySwap    = false;
static bool s_TurboPhase  = false;
/* AURORA_CONTROLLER_OPTIONS_V2: Max/Half/Quarter cadence. */
static unsigned s_TurboSpeedShift = 0;
static uint32_t s_TurboFrame = 0;

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
static Uint8 s_Video[QN_VIDEO_W * QN_VIDEO_H]
    __attribute__((aligned(64)));

/* SNESticle's framebuffer is 256x256 RGBA8. Cache the 256-entry mapping
 * from QuickNES host-palette indices to native RGBA8 words. */
static Uint32 s_RgbaPalette[256];
static short  s_LastFramePalette[Nes_Emu::max_palette_size];
static bool   s_PaletteValid = false;

/* AURORA_QN_DIRECT_T8_GS_V4_STATE
 * Share Aurora's existing direct-video scratch slab with PicoDrive. Only one
 * system is active at once. QuickNES' native pitch is 272 bytes, so a 320-px
 * GS TBW is the smallest 64-pixel-aligned row that contains it.
 * The palette remains CT32; V3's final CT16 target is the only quantisation. */
enum
{
    QN_GS_T8_TBP_OFFSET   = 0x400,
    QN_GS_CLUT_TBP_OFFSET = 0x580,
    QN_GS_T8_TBW          = 320
};
static Uint32 s_DirectClut[256] __attribute__((aligned(64)));
static short  s_DirectLastPalette[Nes_Emu::max_palette_size];
static bool   s_DirectPaletteValid = false;
static bool   s_DirectFrameValid = false;
static const Uint8 *s_DirectPixels = NULL;
static int    s_DirectPitch = 0;
static Uint32 s_DirectVideoSerial = 0;
static Uint32 s_DirectUploadedSerial = 0;
static Uint32 s_DirectPaletteSerial = 0;
static Uint32 s_DirectUploadedPaletteSerial = 0;
static bool   s_DirectPixelsResident = false;

/* Audio scratch is BSS/static, never EE thread stack. QuickNES's default
 * non-linear Nes_Buffer emits mono; SNESticle's AudMixBuffer duplicates it
 * to stereo and performs the existing 32 -> 48 kHz conversion. */
static Int16 s_Audio[QN_AUDIO_MAX];
static Int16 s_AudioOut[QN_AUDIO_MAX + 4];
static Int16 s_Pending[4];
static int   s_PendingCount = 0;


static void qResetDirectVideo(void)
{
    memset(s_DirectClut, 0, sizeof(s_DirectClut));
    memset(s_DirectLastPalette, 0, sizeof(s_DirectLastPalette));
    s_DirectPaletteValid = false;
    s_DirectFrameValid = false;
    s_DirectPixels = NULL;
    s_DirectPitch = 0;
    s_DirectVideoSerial = 0;
    s_DirectUploadedSerial = 0;
    s_DirectPaletteSerial = 0;
    s_DirectUploadedPaletteSerial = 0;
    s_DirectPixelsResident = false;
}

static void qResetTransient(void)
{
    memset(s_Video, 0, sizeof(s_Video));
    memset(s_LastFramePalette, 0, sizeof(s_LastFramePalette));
    memset(s_Pending, 0, sizeof(s_Pending));
    s_PendingCount = 0;
    s_PaletteValid = false;
    s_TurboPhase = false;
    s_TurboFrame = 0;
    qResetDirectVideo();
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

/* AURORA_QN_DIRECT_T8_GS_V4_PREP */
static unsigned qPs2ClutSlot(unsigned logical)
{
    unsigned slot = logical & 0xffu;
    if ((slot & 0x18u) == 0x08u) slot += 8u;
    else if ((slot & 0x18u) == 0x10u) slot -= 8u;
    return slot;
}

static bool qPrepareDirectFrame(void)
{
    if (!s_pEmu) return false;
    const Nes_Emu::frame_t &frame = s_pEmu->frame();

    /* GPPrimUploadTexture expects packed rows. If QuickNES ever changes the
     * 272-byte pitch contract, fail closed into the old RGBA path. */
    if (!frame.pixels || frame.pitch != QN_VIDEO_W)
    {
        s_DirectFrameValid = false;
        s_DirectPixels = NULL;
        s_DirectPitch = 0;
        return false;
    }

    if (!s_DirectPaletteValid ||
        memcmp(s_DirectLastPalette, frame.palette,
               sizeof(s_DirectLastPalette)) != 0)
    {
        for (unsigned i = 0; i < 256; ++i)
        {
            unsigned ci = (unsigned)(unsigned short)frame.palette[i];
            if (ci >= (unsigned)Nes_Emu::color_table_size) ci = 0;
            const Nes_Emu::rgb_t &rgb = Nes_Emu::nes_colors[ci];
            Uint32 c = 0x80000000u |
                       ((Uint32)rgb.blue << 16) |
                       ((Uint32)rgb.green << 8) |
                       (Uint32)rgb.red;
            s_DirectClut[qPs2ClutSlot(i)] = c;
        }
        memcpy(s_DirectLastPalette, frame.palette, sizeof(s_DirectLastPalette));
        s_DirectPaletteValid = true;
        if (++s_DirectPaletteSerial == 0) s_DirectPaletteSerial = 1;
    }

    s_DirectPixels = frame.pixels;
    s_DirectPitch = (int)frame.pitch;
    s_DirectFrameValid = true;
    if (++s_DirectVideoSerial == 0)
    {
        s_DirectVideoSerial = 1;
        s_DirectPixelsResident = false;
    }
    return true;
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

            const Nes_Emu::rgb_t &rgb = Nes_Emu::nes_colors[ci];

            /* CRenderSurface PIXELFORMAT_RGBA8 on little-endian EE is
             * bytes R,G,B,A -> integer 0xAABBGGRR. */
            s_RgbaPalette[i] =
                0xff000000u |
                ((Uint32)rgb.blue  << 16) |
                ((Uint32)rgb.green << 8)  |
                (Uint32)rgb.red;
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

    count = s_pEmu->read_samples((short *)s_Audio, QN_AUDIO_MAX);
    if (count <= 0)
    {
        pMix->Flush();
        return;
    }
    if (count > QN_AUDIO_MAX)
        count = QN_AUDIO_MAX;

    /* AURORA_MEGA_V4_QUICKNES_BRIDGE_BULK_COPY
     * These three arrays are independent host-side PCM buffers. The old
     * loops performed straight Int16 copies with no mapper/APU side effects,
     * so libc bulk copies are byte-identical and avoid per-sample loop work. */
    int n = s_PendingCount + (int)count;
    if (s_PendingCount > 0)
        memcpy(s_AudioOut, s_Pending,
               (size_t)s_PendingCount * sizeof(s_Pending[0]));
    memcpy(s_AudioOut + s_PendingCount, s_Audio,
           (size_t)count * sizeof(s_Audio[0]));

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
    s_pEmu->set_pixels(s_Video, QN_VIDEO_W);

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
    s_pEmu->set_pixels(s_Video, QN_VIDEO_W);

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
    qResetTransient();
}

void QuicknesBridge_Reset(void)
{
    if (s_GameLoaded)
        s_pEmu->reset(true, false);       /* power-cycle style reset */
    s_PendingCount = 0;
    s_PaletteValid = false;
    s_TurboPhase = false;
    s_TurboFrame = 0;
    qResetDirectVideo();
}

void QuicknesBridge_SoftReset(void)
{
    if (s_GameLoaded)
        s_pEmu->reset(false, false);      /* NES RESET button */
    s_PendingCount = 0;
    s_PaletteValid = false;
    s_TurboPhase = false;
    s_TurboFrame = 0;
    qResetDirectVideo();
}

void QuicknesBridge_SetDutySwap(bool enabled)
{
    s_DutySwap = enabled;
    quicknes_snesticle_set_duty_swap(enabled ? 1 : 0);
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

/* AURORA_QN_DIRECT_T8_GS_V4_API */
void QuicknesBridge_InvalidateGsResources(void)
{
    s_DirectPixelsResident = false;
    s_DirectUploadedSerial = 0;
    s_DirectUploadedPaletteSerial = 0;
}

bool QuicknesBridge_CanDirectGsVideo(void)
{
    return s_GameLoaded && s_DirectFrameValid && s_DirectPaletteValid &&
           s_DirectPixels != NULL && s_DirectPitch == QN_VIDEO_W;
}

bool QuicknesBridge_DrawDirectGs(unsigned int auroraOutBaseTBP,
                                 float intensity,
                                 float logicalY)
{
    if (!QuicknesBridge_CanDirectGsVideo()) return false;

    Uint32 texTBP  = auroraOutBaseTBP + QN_GS_T8_TBP_OFFSET;
    Uint32 clutTBP = auroraOutBaseTBP + QN_GS_CLUT_TBP_OFFSET;

    if (!s_DirectPixelsResident || s_DirectUploadedSerial != s_DirectVideoSerial)
    {
        GPPrimUploadTexture((int)texTBP, QN_GS_T8_TBW, 0, 0, GS_PSMT8,
                            (void *)s_DirectPixels, s_DirectPitch,
                            Nes_Emu::image_height);
        s_DirectUploadedSerial = s_DirectVideoSerial;
        s_DirectPixelsResident = true;
    }

    if (s_DirectUploadedPaletteSerial != s_DirectPaletteSerial)
    {
        GPPrimUploadTexture((int)clutTBP, 64, 0, 0, GS_PSMCT32,
                            s_DirectClut, 16, 16);
        s_DirectUploadedPaletteSerial = s_DirectPaletteSerial;
    }

    GPPrimSetTex(texTBP, QN_GS_T8_TBW, 9, 8, GS_PSMT8,
                 clutTBP, 64, GS_PSMCT32, 0);

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    unsigned mod = (unsigned)(128.0f * intensity + 0.5f);
    if (mod > 128U) mod = 128U;
    Uint32 modColor = 0x80000000U | (mod << 16) | (mod << 8) | mod;

    Uint32 y0 = (Uint32)(logicalY * 16.0f + 0.5f);
    Uint32 y1 = y0 + (240U << 4);
    GPPrimTexRect(0, y0, 8, 8,
                  256U << 4, y1,
                  (256U << 4) + 8, (240U << 4) + 8,
                  10U << 4, modColor, 0);
    return true;
}

void QuicknesBridge_RunFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf)
{
    if (!s_GameLoaded || !s_pEmu)
        return;

    Uint8 p1 = 0;
    Uint8 p2 = 0;
    s_TurboPhase = (((s_TurboFrame >> s_TurboSpeedShift) & 1U) == 0U);
    ++s_TurboFrame;

    if (pInput)
    {
        p1 = qMapPad(pInput->uPad[0]);
        p2 = qMapPad(pInput->uPad[1]);
    }

    const char *err = s_pEmu->emulate_frame((int)p1, (int)p2);
    if (err)
    {
        printf("[QuickNES/native] emulate_frame failed: %s\n", err);
        qDrainAudio(pMixBuf);
        return;
    }

    /* AURORA_QN_DIRECT_T8_GS_V4_RUN */
    if (!qPrepareDirectFrame())
        qRenderFrame(pTarget); /* exact old RGBA32 fallback */
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

unsigned QuicknesBridge_GetSampleRate(void)
{
    return 32000;
}
