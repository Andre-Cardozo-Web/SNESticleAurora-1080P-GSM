/* AURORA_SNES9X2010_V1
 * Standalone libretro host bridge for Snes9x 2010 on PS2/Aurora.
 * No RetroArch dependency: Aurora directly owns callbacks, input, video,
 * audio, SRAM and serialization.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <malloc.h>

/* AURORA_SNES9X2010_V1_PS2_POSIX_MEMALIGN
 * PS2SDK provides memalign(), but its libc does not export posix_memalign().
 * Keep this compatibility shim in the Aurora host rather than modifying the
 * pinned Snes9x2010 submodule.
 */
extern "C" int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!memptr ||
        alignment < sizeof(void *) ||
        (alignment & (alignment - 1)) != 0 ||
        (alignment % sizeof(void *)) != 0)
        return EINVAL;

    void *ptr = memalign(alignment, size);
    if (!ptr)
        return ENOMEM;

    *memptr = ptr;
    return 0;
}

#include "snes9x2010_bridge.h"
#include "types.h"
#include "emuinput.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"
/* AURORA_SNES9X2010_V4_PS2_PERF_20260824 */
#include "audmixbuffer.h"
extern AudMixBuffer *_AudMix;
extern "C" {
#include "gs.h"
#include "gpprim.h"
}
#include "snio.h"

/* AURORA_SNES9X2010_V3_MENU_BRIDGE_20260824 */
#include "snrom.h"
#include "snppurender.h"
#include "input.h"
#ifndef AURORA_SNES9X2010
#define AURORA_SNES9X2010 0
#endif

/* AURORA_SNES9X2010_BUILD_OPTION_V1_20260824
 * Compile the real host only when the archive is part of this build. */
#if AURORA_SNES9X2010
extern "C" {
#include "../../third_party/snes9x2010/libretro/libretro-common/include/libretro.h"
}

extern "C" {
unsigned S9X2010_retro_api_version(void);
void S9X2010_retro_set_environment(retro_environment_t);
void S9X2010_retro_set_video_refresh(retro_video_refresh_t);
void S9X2010_retro_set_audio_sample(retro_audio_sample_t);
void S9X2010_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void S9X2010_retro_set_input_poll(retro_input_poll_t);
void S9X2010_retro_set_input_state(retro_input_state_t);
void S9X2010_retro_set_controller_port_device(unsigned, unsigned);
void S9X2010_retro_init(void);
void S9X2010_retro_deinit(void);
bool S9X2010_retro_load_game(const struct retro_game_info *);
void S9X2010_retro_unload_game(void);
void S9X2010_retro_reset(void);
void S9X2010_retro_run(void);
size_t S9X2010_retro_serialize_size(void);
bool S9X2010_retro_serialize(void *, size_t);
bool S9X2010_retro_unserialize(const void *, size_t);
void *S9X2010_retro_get_memory_data(unsigned);
size_t S9X2010_retro_get_memory_size(unsigned);
void S9X2010_retro_get_system_av_info(struct retro_system_av_info *);
/* AURORA_SNES9X2010_V1_RUNTIMEFIX_20260824 */
void S9X2010_S9xAuroraSetRomCapacity(uint32_t, int);
void S9X2010_S9xAuroraSetMenuOptions(
    uint32_t, int, unsigned, unsigned, unsigned, unsigned);
bool S9X2010_AuroraS9x2010InitOK(void);
}

static bool s_Initialized = false;
static bool s_GameLoaded = false;
static Emu::SysInputT *s_pInput = NULL;
static CRenderSurface *s_pTarget = NULL;
static CMixBuffer *s_pMix = NULL;
static const void *s_VideoData = NULL;
static unsigned s_VideoW = 0;
static unsigned s_VideoH = 0;
static size_t s_VideoPitch = 0;
static bool s_HaveNewVideo = false;
static Uint8 *s_pSramData = NULL;
static Int32 s_SramBytes = 0;
static Uint32 s_SampleRate = 32040;

/* AURORA_SNES9X2010_V6_3_SAFEHOST_20260824
 * The V4 direct-GS texture path can leave the real PS2 GS/DMA pipeline in a
 * black, non-recoverable state.  Keep Snes9x2010 on Aurora's established
 * RGBA8 surface + TextureUpload path, and keep its original deinterleaved
 * mixer path.  Other cores retain their independently tested fast paths. */
#define AURORA_SNES9X2010_HW_FASTPATH 0



/* AURORA_SNES9X2010_V4_PS2_PERF_20260824 */
static Uint16 *s_DirectStage = NULL;
static Uint32 s_DirectStageSerial = 0;
static Uint32 s_DirectUploadSerial = 0;
static bool s_DirectReady = false;
/* AURORA_SNES9X2010_V3_MENU_BRIDGE_20260824 */
static bool s_MouseDevice = false;
static Int32 s_MouseDX = 0;
static Int32 s_MouseDY = 0;
static Uint32 s_MouseButtons = 0;

/* AURORA_V18_SAFE_PERF_S9X_MENU_CACHE_20260825
 * These six values are Aurora-owned menu state. The typed setter also
 * reapplies SRAM policy and checks PPU/OBJ state, so repeating identical
 * values every frame is pure host overhead. */
static bool s_MenuOptionsValid = false;
static Uint32 s_LastMenuSramKbits = 0;
static int s_LastMenuRegion = 0;
static unsigned s_LastMenuLayerMask = 0;
static unsigned s_LastMenuHackFlags = 0;
static unsigned s_LastMenuObjLevel = 0;
static unsigned s_LastMenuObjMode = 0;

static const void *s_ContentData = NULL;
static size_t s_ContentBytes = 0;
static char s_ContentName[1024] = "game.sfc";
static char s_ContentBaseName[1024] = "game";
static char s_ContentExt[16] = "sfc";
static struct retro_game_info_ext s_ContentInfoExt;
static const char s_DotPath[] = ".";

enum { S9X2010_AUDIO_CHUNK = 1024 };
static Int16 s_AudioL[S9X2010_AUDIO_CHUNK];
static Int16 s_AudioR[S9X2010_AUDIO_CHUNK];

/* RGB565 -> Aurora RGBA8 (AABBGGRR in the 32-bit framebuffer). */
static Uint32 s_R[32], s_G[64], s_B[32];
static bool s_ColorTablesReady = false;

static void s9xInitColorTables(void)
{
    if (s_ColorTablesReady)
        return;
    for (unsigned i = 0; i < 32; ++i)
    {
        Uint32 v = (i << 3) | (i >> 2);
        s_R[i] = v;
        s_B[i] = v << 16;
    }
    for (unsigned i = 0; i < 64; ++i)
    {
        Uint32 v = (i << 2) | (i >> 4);
        s_G[i] = v << 8;
    }
    s_ColorTablesReady = true;
}

static inline Uint32 s9x565(Uint16 c)
{
    return 0xff000000u |
           s_R[(c >> 11) & 31u] |
           s_G[(c >> 5) & 63u] |
           s_B[c & 31u];
}

static inline void s9x565parts(Uint16 c, Uint32 *r, Uint32 *g, Uint32 *b)
{
    *r = s_R[(c >> 11) & 31u];
    *g = s_G[(c >> 5) & 63u] >> 8;
    *b = s_B[c & 31u] >> 16;
}

static void s9xLog(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    if (!fmt)
        return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/* V1 exposes no Snes9x-specific settings yet. Give the core conservative,
 * deterministic defaults; V2 will map Aurora's menu to these variables. */
static const char *s9xVariable(const char *key)
{
    if (!key) return NULL;
    if (!strcmp(key, "snes9x_2010_region")) return "auto";
    if (!strcmp(key, "snes9x_2010_audio_interpolation")) return "gaussian";
    if (!strcmp(key, "snes9x_2010_msu1_enhanced_audio")) return "disabled";
    if (!strcmp(key, "snes9x_2010_aspect")) return "auto";
    if (!strcmp(key, "snes9x_2010_blargg")) return "disabled";
    if (!strcmp(key, "snes9x_2010_frameskip")) return "disabled";
    if (!strcmp(key, "snes9x_2010_frameskip_threshold")) return "33";
    if (!strcmp(key, "snes9x_2010_overclock")) return "10 MHz (Default)";
    if (!strcmp(key, "snes9x_2010_overclock_cycles")) return "disabled";
    if (!strcmp(key, "snes9x_2010_superfx_timing")) return "legacy";
    if (!strcmp(key, "snes9x_2010_superfx_cycle_accuracy")) return "enabled";
    if (!strcmp(key, "snes9x_2010_reduce_sprite_flicker")) return "disabled";
    if (!strcmp(key, "snes9x_2010_block_invalid_vram_access")) return "enabled";
    if (!strcmp(key, "snes9x_2010_mode7_hires")) return "disabled";
    if (!strcmp(key, "snes9x_2010_mode7_hires_bilinear")) return "stable";
    if (!strcmp(key, "snes9x_2010_pseudo_hires_blend")) return "disabled";
    return NULL;
}

static bool s9xEnvironment(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            if (!data) return false;
            ((struct retro_log_callback *)data)->log = s9xLog;
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return data &&
                   *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            struct retro_variable *v = (struct retro_variable *)data;
            if (!v || !v->key) return false;
            v->value = s9xVariable(v->key);
            return v->value != NULL;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (!data) return false;
            *(bool *)data = false;
            return true;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            if (!data) return false;
            *(const char **)data = s_DotPath;
            return true;

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *(bool *)data = true;
            return true;

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            if (data) *(unsigned *)data = 0;
            return true;

        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return true;

        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            if (!data) return false;
            *(unsigned *)data = RETRO_LANGUAGE_ENGLISH;
            return true;

        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
            if (!data) return false;
            *(bool *)data = false;
            return true;

        case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
            if (!data) return false;
            *(int *)data = (s_pTarget ? 1 : 0) | (s_pMix ? 2 : 0);
            return true;

        case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
            if (!data || !s_ContentData || !s_ContentBytes) return false;
            *(const struct retro_game_info_ext **)data = &s_ContentInfoExt;
            return true;

        case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER:
        case RETRO_ENVIRONMENT_GET_VFS_INTERFACE:
        case RETRO_ENVIRONMENT_GET_PERF_INTERFACE:
            return false;

        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        case RETRO_ENVIRONMENT_SET_MESSAGE:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
        case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
        case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK:
            return true;

        default:
            return false;
    }
}

static void s9xVideoRefresh(const void *data, unsigned w, unsigned h, size_t pitch)
{
    /* NULL means "duplicate the previous frame" in libretro. */
    if (!data)
    {
        s_HaveNewVideo = false;
        return;
    }
    if (!w || !h || pitch < (size_t)w * 2)
    {
        s_HaveNewVideo = false;
        return;
    }
    s_VideoData = data;
    s_VideoW = w;
    s_VideoH = h;
    s_VideoPitch = pitch;
    s_HaveNewVideo = true;
}

static void s9xAudioSample(int16_t l, int16_t r)
{
    if (!s_pMix) return;
    Int16 L = (Int16)l, R = (Int16)r;
    s_pMix->OutputSamplesStereo(&L, &R, 1);
}

static size_t s9xAudioBatch(const int16_t *data, size_t frames)
{
    if (!data || !frames || !s_pMix)
        return frames;

    if (AURORA_SNES9X2010_HW_FASTPATH &&
        _AudMix && s_pMix == _AudMix && frames <= 0x7fffffffU)
    {
        size_t directPos = 0;
        while (directPos < frames)
        {
            size_t n = frames - directPos;
            if (n > S9X2010_AUDIO_CHUNK) n = S9X2010_AUDIO_CHUNK;
            _AudMix->OutputLibretroInterleaved(
                (const Int16 *)(data + directPos * 2), (Int32)n);
            directPos += n;
        }
        return frames;
    }

    size_t pos = 0;
    while (pos < frames)
    {
        size_t n = frames - pos;
        if (n > S9X2010_AUDIO_CHUNK)
            n = S9X2010_AUDIO_CHUNK;
        for (size_t i = 0; i < n; ++i)
        {
            s_AudioL[i] = (Int16)data[(pos + i) * 2 + 0];
            s_AudioR[i] = (Int16)data[(pos + i) * 2 + 1];
        }
        s_pMix->OutputSamplesStereo(s_AudioL, s_AudioR, (Int32)n);
        pos += n;
    }
    return frames;
}

/* Push every SNES-relevant menu value through one typed ABI. */
static void s9xApplyAuroraMenuOptions(void)
{
    const Uint32 sramKbits = (Uint32)g_FakeSRAMSize;
    const int region = (int)g_SnesForceRegion;
    const unsigned layerMask =
        (unsigned)SNPPURenderGetSoftwareLayerMask();
    const unsigned hackFlags =
        (unsigned)SNPPURenderGetSoftwareHackFlags();
    const unsigned objLevel =
        (unsigned)SNPPURenderGetObjLimitLevel();
    const unsigned objMode =
        (unsigned)SNPPURenderGetObjLimitMode();

    if (s_MenuOptionsValid &&
        sramKbits == s_LastMenuSramKbits &&
        region == s_LastMenuRegion &&
        layerMask == s_LastMenuLayerMask &&
        hackFlags == s_LastMenuHackFlags &&
        objLevel == s_LastMenuObjLevel &&
        objMode == s_LastMenuObjMode)
        return;

    S9X2010_S9xAuroraSetMenuOptions(
        (uint32_t)sramKbits, region, layerMask, hackFlags,
        objLevel, objMode);

    s_LastMenuSramKbits = sramKbits;
    s_LastMenuRegion = region;
    s_LastMenuLayerMask = layerMask;
    s_LastMenuHackFlags = hackFlags;
    s_LastMenuObjLevel = objLevel;
    s_LastMenuObjMode = objMode;
    s_MenuOptionsValid = true;
}

static int16_t s9xMouseDelta(Int32 value)
{
    if (value < -127) value = -127;
    if (value >  127) value =  127;
    return (int16_t)value;
}

static void s9xInputPoll(void) {}

static bool s9xPadHas(Uint16 pad, Uint16 bit)
{
    return pad != EMUSYS_DEVICE_DISCONNECTED && (pad & bit);
}

static int16_t s9xJoyMask(unsigned port)
{
    if (!s_pInput || port >= 2)
        return 0;
    Uint16 p = s_pInput->uPad[port];
    if (p == EMUSYS_DEVICE_DISCONNECTED)
        return 0;

    unsigned m = 0;
    if (s9xPadHas(p, SNESIO_JOY_B))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
    if (s9xPadHas(p, SNESIO_JOY_Y))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_Y;
    if (s9xPadHas(p, SNESIO_JOY_SELECT)) m |= 1u << RETRO_DEVICE_ID_JOYPAD_SELECT;
    if (s9xPadHas(p, SNESIO_JOY_START))  m |= 1u << RETRO_DEVICE_ID_JOYPAD_START;
    if (s9xPadHas(p, SNESIO_JOY_UP))     m |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (s9xPadHas(p, SNESIO_JOY_DOWN))   m |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (s9xPadHas(p, SNESIO_JOY_LEFT))   m |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (s9xPadHas(p, SNESIO_JOY_RIGHT))  m |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
    if (s9xPadHas(p, SNESIO_JOY_A))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
    if (s9xPadHas(p, SNESIO_JOY_X))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_X;
    if (s9xPadHas(p, SNESIO_JOY_L))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_L;
    if (s9xPadHas(p, SNESIO_JOY_R))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_R;
    return (int16_t)m;
}

static int16_t s9xInputState(unsigned port, unsigned device,
                             unsigned index, unsigned id)
{
    (void)index;
    if (device == RETRO_DEVICE_MOUSE && port == 0 && s_MouseDevice)
    {
        switch (id)
        {
            case RETRO_DEVICE_ID_MOUSE_X:     return s9xMouseDelta(s_MouseDX);
            case RETRO_DEVICE_ID_MOUSE_Y:     return s9xMouseDelta(s_MouseDY);
            case RETRO_DEVICE_ID_MOUSE_LEFT:  return (s_MouseButtons & 0x01U) ? 1 : 0;
            case RETRO_DEVICE_ID_MOUSE_RIGHT: return (s_MouseButtons & 0x02U) ? 1 : 0;
            default: return 0;
        }
    }
    if (device != RETRO_DEVICE_JOYPAD || port >= 2)
        return 0;
    int16_t mask = s9xJoyMask(port);
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return mask;
    if (id > RETRO_DEVICE_ID_JOYPAD_R3)
        return 0;
    return (mask & (1u << id)) ? 1 : 0;
}

/* AURORA_SNES9X2010_V4_PS2_PERF_20260824
 * Conventional libretro RGB565 -> GS A1B5G5R5.  The source's high five
 * green bits are retained; alpha is always opaque. */
static inline Uint16 s9x565ToGs16(Uint16 c)
{
    return (Uint16)(0x8000u |
                    ((c >> 11) & 0x001fu) |
                    ((c >> 1) & 0x03e0u) |
                    ((c & 0x001fu) << 10));
}

/* AURORA_SNES9X2010_V5_ALLCORES_PERF_20260824
 * Four independent little-endian RGB565 lanes -> GS A1B5G5R5 lanes. Masks
 * remove all cross-lane shift bits, so this is exactly four scalar calls. */
static inline Uint64 s9x565x4ToGs16(Uint64 p)
{
    const Uint64 m5 = 0x001f001f001f001fULL;
    const Uint64 mg = 0x03e003e003e003e0ULL;
    return 0x8000800080008000ULL |
           ((p >> 11) & m5) |
           ((p >> 1) & mg) |
           ((p & m5) << 10);
}

static inline Uint32 s9x565x2ToGs16(Uint32 p)
{
    const Uint32 m5 = 0x001f001fu;
    const Uint32 mg = 0x03e003e0u;
    return 0x80008000u |
           ((p >> 11) & m5) |
           ((p >> 1) & mg) |
           ((p & m5) << 10);
}

static inline Uint32 s9xAverage565Pairs4(Uint64 p)
{
    const Uint64 lanes02 = 0x0000ffff0000ffffULL;
    const Uint64 avgMask = 0x0000f7de0000f7deULL;
    const Uint64 carry = 0x0000082100000821ULL;
    Uint64 a = p & lanes02;
    Uint64 b = (p >> 16) & lanes02;
    Uint64 avg = ((a & avgMask) >> 1) +
                 ((b & avgMask) >> 1) +
                 (a & b & carry);
    return (Uint32)(avg & 0xffffu) |
           (Uint32)((avg >> 16) & 0xffff0000u);
}


static bool s9xStageDirectGs(CRenderSurface *target)
{
    PixelFormatT *fmt;
    Uint16 *dst;
    int sw, sh, dstH, y;

    if (!target || !s_HaveNewVideo || !s_VideoData)
        return false;
    fmt = target->GetFormat();
    if (!fmt || fmt->uBitDepth != 32 ||
        target->GetWidth() != 256 || target->GetHeight() < 240)
        return false;

    sw = (int)s_VideoW;
    sh = (int)s_VideoH;
    if ((sw != 256 && sw != 512) || sh <= 0)
        return false;

    dst = (Uint16 *)target->GetLinePtr(0);
    if (!dst)
        return false;

    dstH = sh;
    if (dstH > 240) dstH = (sh + 1) / 2;
    if (dstH > 240) dstH = 240;

    for (y = 0; y < 240; ++y)
    {
        Uint16 *out = dst + y * 256;
        int x;
        if (y >= dstH)
        {
            if (((uintptr_t)out & 7u) == 0u)
            {
                Uint64 *out64 = (Uint64 *)out;
                for (x = 0; x < 64; ++x)
                    out64[x] = 0x8000800080008000ULL;
            }
            else
            {
                for (x = 0; x < 256; ++x)
                    out[x] = 0x8000u;
            }
            continue;
        }

        {
            int sy = (int)(((uint64_t)y * (uint64_t)sh) /
                           (unsigned)dstH);
            const Uint16 *src;
            if (sy >= sh) sy = sh - 1;
            src = (const Uint16 *)((const Uint8 *)s_VideoData +
                                   (size_t)sy * s_VideoPitch);

            if (sw == 256 &&
                ((((uintptr_t)src | (uintptr_t)out) & 7u) == 0u))
            {
                const Uint64 *in64 = (const Uint64 *)src;
                Uint64 *out64 = (Uint64 *)out;
                for (x = 0; x < 64; ++x)
                    out64[x] = s9x565x4ToGs16(in64[x]);
            }
            else if (sw == 256)
            {
                for (x = 0; x < 256; ++x)
                    out[x] = s9x565ToGs16(src[x]);
            }
            else if ((((uintptr_t)src & 7u) == 0u) &&
                     (((uintptr_t)out & 3u) == 0u))
            {
                const Uint64 *in64 = (const Uint64 *)src;
                Uint32 *out32 = (Uint32 *)out;
                for (x = 0; x < 128; ++x)
                    out32[x] = s9x565x2ToGs16(
                        s9xAverage565Pairs4(in64[x]));
            }
            else
            {
                for (x = 0; x < 256; ++x)
                {
                    Uint16 a = src[x * 2 + 0];
                    Uint16 b = src[x * 2 + 1];
                    Uint16 avg = (Uint16)(((a & 0xf7deu) >> 1) +
                                          ((b & 0xf7deu) >> 1) +
                                          (a & b & 0x0821u));
                    out[x] = s9x565ToGs16(avg);
                }
            }
        }
    }

    s_DirectStage = dst;
    if (++s_DirectStageSerial == 0) ++s_DirectStageSerial;
    s_DirectReady = true;
    return true;
}

static void s9xRender(CRenderSurface *target)
{
    if (!target || !s_HaveNewVideo || !s_VideoData)
        return;

    PixelFormatT *fmt = target->GetFormat();
    if (!fmt || fmt->uBitDepth != 32)
        return;

    s9xInitColorTables();

    const int tw = (int)target->GetWidth();
    const int th = (int)target->GetHeight();
    const int visH = th < 240 ? th : 240;
    const int sw = (int)s_VideoW;
    const int sh = (int)s_VideoH;
    if (tw <= 0 || visH <= 0 || sw <= 0 || sh <= 0)
        return;

    /* Normal SNES is 224/239 lines. Interlace is 448/478; collapse it to
     * 224/239 instead of cropping half of the field. */
    int dstH = sh;
    if (dstH > visH)
        dstH = (sh + 1) / 2;
    if (dstH > visH)
        dstH = visH;
    /* Match SNESticle's surface convention: active SNES scanlines begin at
     * row 0. MainLoopRender applies Aurora's established SNES Y offset. */
    const int dstY = 0;
    const Uint32 black = 0xff000000u;

    for (int y = 0; y < visH; ++y)
    {
        Uint32 *dst = (Uint32 *)target->GetLinePtr(y);
        if (!dst) continue;

        if (y < dstY || y >= dstY + dstH)
        {
            for (int x = 0; x < tw; ++x) dst[x] = black;
            continue;
        }

        int oy = y - dstY;
        int sy = (int)(((uint64_t)oy * (uint64_t)sh) / (unsigned)dstH);
        if (sy >= sh) sy = sh - 1;
        const Uint16 *src = (const Uint16 *)(
            (const Uint8 *)s_VideoData + (size_t)sy * s_VideoPitch);

        if (sw == tw)
        {
            int x = 0;
            for (; x + 3 < tw; x += 4)
            {
                dst[x + 0] = s9x565(src[x + 0]);
                dst[x + 1] = s9x565(src[x + 1]);
                dst[x + 2] = s9x565(src[x + 2]);
                dst[x + 3] = s9x565(src[x + 3]);
            }
            for (; x < tw; ++x) dst[x] = s9x565(src[x]);
        }
        else if (sw < tw)
        {
            int dx = (tw - sw) / 2;
            for (int x = 0; x < dx; ++x) dst[x] = black;
            for (int x = 0; x < sw; ++x) dst[dx + x] = s9x565(src[x]);
            for (int x = dx + sw; x < tw; ++x) dst[x] = black;
        }
        else if (sw == tw * 2)
        {
            /* AURORA_SNES9X2010_V1_PS2LEAN_20260824
             * Native SNES hires is exactly 512 -> Aurora 256. Average each
             * RGB565 pair directly instead of running the generic fractional
             * bilinear scaler (multiplications/divisions for every pixel). */
            for (int x = 0; x < tw; ++x)
            {
                Uint16 a = src[x * 2 + 0];
                Uint16 b = src[x * 2 + 1];
                Uint16 avg = (Uint16)(((a & 0xF7DEu) >> 1) +
                                      ((b & 0xF7DEu) >> 1) +
                                      (a & b & 0x0821u));
                dst[x] = s9x565(avg);
            }
        }
        else
        {
            /* Unusual wider experimental geometry: generic uniform reduction. */
            Uint32 step = (Uint32)(((uint64_t)sw << 16) / (unsigned)tw);
            Uint32 pos = step / 2;
            for (int x = 0; x < tw; ++x, pos += step)
            {
                int sx = (int)(pos >> 16);
                if (sx >= sw) sx = sw - 1;
                int sx1 = sx + 1 < sw ? sx + 1 : sx;
                Uint32 f = (pos >> 8) & 255u;
                Uint32 r0, g0, b0, r1, g1, b1;
                s9x565parts(src[sx], &r0, &g0, &b0);
                s9x565parts(src[sx1], &r1, &g1, &b1);
                Uint32 r = (r0 * (256 - f) + r1 * f + 128) >> 8;
                Uint32 g = (g0 * (256 - f) + g1 * f + 128) >> 8;
                Uint32 b = (b0 * (256 - f) + b1 * f + 128) >> 8;
                dst[x] = 0xff000000u | (b << 16) | (g << 8) | r;
            }
        }
    }
}

void Snes9x2010Bridge_InvalidateGsResources(void)
{
    /* The EE stage remains valid; only residency belongs to this GS epoch. */
    s_DirectUploadSerial = 0;
}

bool Snes9x2010Bridge_CanDirectGsVideo(void)
{
    /* V6.3 hard safety gate: MainLoop must always select TextureUpload. */
    return false;
}

bool Snes9x2010Bridge_DrawDirectGs(Uint32 outTexTBP, Float32 intensity)
{
    Uint32 mod, modColor;

    if (!outTexTBP || !Snes9x2010Bridge_CanDirectGsVideo())
        return false;

    if (s_DirectUploadSerial != s_DirectStageSerial)
    {
        GPPrimUploadTexture((int)outTexTBP, 256, 0, 0, GS_PSMCT16,
                            s_DirectStage, 256, 240);
        s_DirectUploadSerial = s_DirectStageSerial;
    }
    GPPrimSetTex(outTexTBP, 256, 8, 8, GS_PSMCT16,
                 0, 0, GS_PSMCT16, 0);

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    mod = (Uint32)(128.0f * intensity + 0.5f);
    if (mod > 128u) mod = 128u;
    modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;

    /* Bit-for-bit the same logical rectangle/half-texel UV bias emitted by:
       PolyUV(0,0,256,240); PolyRect(0,8,256,240).  GPPrim applies the
       common logical->physical transform in 240p, 480i and 1080i. */
    GPPrimTexRect(0, 8u << 4, 8, 8,
                  256u << 4, 248u << 4,
                  (256u << 4) + 8u, (240u << 4) + 8u,
                  10u << 4, modColor, 0);
    return true;
}


static void s9xBuildInfo(const char *name, const void *data, size_t bytes)
{
    if (name && *name)
    {
        strncpy(s_ContentName, name, sizeof(s_ContentName) - 1);
        s_ContentName[sizeof(s_ContentName) - 1] = 0;
    }
    else
        strcpy(s_ContentName, "game.sfc");

    const char *base = strrchr(s_ContentName, '/');
    const char *base2 = strrchr(s_ContentName, '\\');
    if (!base || (base2 && base2 > base)) base = base2;
    base = base ? base + 1 : s_ContentName;
    const char *dot = strrchr(base, '.');
    size_t n = (dot && dot > base) ? (size_t)(dot - base) : strlen(base);
    if (n >= sizeof(s_ContentBaseName)) n = sizeof(s_ContentBaseName) - 1;
    memcpy(s_ContentBaseName, base, n);
    s_ContentBaseName[n] = 0;

    if (dot && dot[1])
    {
        strncpy(s_ContentExt, dot + 1, sizeof(s_ContentExt) - 1);
        s_ContentExt[sizeof(s_ContentExt) - 1] = 0;
    }
    else
        strcpy(s_ContentExt, "sfc");

    s_ContentData = data;
    s_ContentBytes = bytes;
    memset(&s_ContentInfoExt, 0, sizeof(s_ContentInfoExt));
    s_ContentInfoExt.full_path = s_ContentName;
    s_ContentInfoExt.dir = s_DotPath;
    s_ContentInfoExt.name = s_ContentBaseName;
    s_ContentInfoExt.ext = s_ContentExt;
    s_ContentInfoExt.data = data;
    s_ContentInfoExt.size = bytes;
    s_ContentInfoExt.file_in_archive = false;
    s_ContentInfoExt.persistent_data = true;
}

bool Snes9x2010Bridge_Init(void)
{
    if (s_Initialized)
        return true;

    S9X2010_retro_set_environment(s9xEnvironment);
    S9X2010_retro_set_video_refresh(s9xVideoRefresh);
    S9X2010_retro_set_audio_sample(s9xAudioSample);
    S9X2010_retro_set_audio_sample_batch(s9xAudioBatch);
    S9X2010_retro_set_input_poll(s9xInputPoll);
    S9X2010_retro_set_input_state(s9xInputState);

    if (S9X2010_retro_api_version() != RETRO_API_VERSION)
    {
        printf("[Snes9x2010] libretro API mismatch\n");
        return false;
    }

    S9X2010_retro_init();
    if (!S9X2010_AuroraS9x2010InitOK())
    {
        printf("[Snes9x2010] core init failed (EE memory)\n");
        return false;
    }
    s_Initialized = true;
    return true;
}

void Snes9x2010Bridge_Shutdown(void)
{
    if (!s_Initialized)
        return;
    Snes9x2010Bridge_UnloadGame();
    S9X2010_retro_deinit();
    s_Initialized = false;
}

static bool s9xAuroraLooksBsx(const void *data, size_t bytes, const char *name)
{
    if (name)
    {
        const char *dot = strrchr(name, '.');
        if (dot && dot[1])
        {
            const char *e = dot + 1;
            if ((e[0] == 'b' || e[0] == 'B') &&
                (e[1] == 's' || e[1] == 'S') &&
                (e[2] == 'x' || e[2] == 'X') && e[3] == 0)
                return true;
        }
    }
    if (data && bytes == 0x100000U && bytes >= 0x7fc0U + 21U &&
        memcmp((const Uint8 *)data + 0x7fc0U, "Satellaview BS-X     ", 21) == 0)
        return true;
    return false;
}

bool Snes9x2010Bridge_LoadGame(const void *data, size_t bytes,
                               size_t capacity, const char *name)
{
    (void)capacity;
    if (!data || !bytes || bytes > 0x800200U)
        return false;

    /* AURORA_SNES9X2010_V1_PS2LEAN_20260824
     * ROM heap size is chosen inside retro_init(). A previous failed load can
     * leave the libretro instance initialised but without content, so fully
     * deinit this core before selecting the new cartridge's exact capacity. */
    if (s_Initialized)
        Snes9x2010Bridge_Shutdown();

    /* Fresh core/content lifetime: never inherit a valid host cache. */
    s_MenuOptionsValid = false;

    /* Apply region before LoadROM and SRAM policy before discovery. */
    s9xApplyAuroraMenuOptions();
    S9X2010_S9xAuroraSetRomCapacity(
        (uint32_t)bytes, s9xAuroraLooksBsx(data, bytes, name) ? 1 : 0);
    if (!Snes9x2010Bridge_Init())
        return false;

    s9xBuildInfo(name, data, bytes);
    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = s_ContentName;
    info.data = data;
    info.size = bytes;

    if (!S9X2010_retro_load_game(&info))
    {
        printf("[Snes9x2010] retro_load_game failed: %s (%u bytes)\n",
               s_ContentName, (unsigned)bytes);
        s_ContentData = NULL;
        s_ContentBytes = 0;
        return false;
    }

    S9X2010_retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    S9X2010_retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
    s_MouseDevice = false;
    s_MouseDX = s_MouseDY = 0;
    s_MouseButtons = 0;

    s_GameLoaded = true;
    /* Pre-load ran before cartridge SRAM allocation. Force one first-frame
     * push so S9xAuroraApplySramOverride() sees the allocated SRAM. */
    s_MenuOptionsValid = false;
    size_t sb = S9X2010_retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *sp = S9X2010_retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    s_pSramData = (sp && sb && sb <= 0x7fffffffU) ? (Uint8 *)sp : NULL;
    s_SramBytes = s_pSramData ? (Int32)sb : 0;

    struct retro_system_av_info av;
    memset(&av, 0, sizeof(av));
    S9X2010_retro_get_system_av_info(&av);
    if (av.timing.sample_rate >= 8000.0 && av.timing.sample_rate <= 96000.0)
        s_SampleRate = (Uint32)(av.timing.sample_rate + 0.5);

    printf("[Snes9x2010] loaded %s; SRAM=%d; audio=%u Hz\n",
           s_ContentName, (int)s_SramBytes, (unsigned)s_SampleRate);

    /* AURORA_SNES9X2010_V2_PS2LEAN_20260824: MSU/HD sidecars are absent; the core no longer has
     * any legitimate post-load use for the frontend cartridge pointer. */
    s_ContentData = NULL;
    s_ContentBytes = 0;
    s_ContentInfoExt.data = NULL;
    s_ContentInfoExt.size = 0;
    s_ContentInfoExt.persistent_data = false;
    return true;
}

void Snes9x2010Bridge_UnloadGame(void)
{
    if (s_Initialized && s_GameLoaded)
        S9X2010_retro_unload_game();
    s_GameLoaded = false;
    s_pInput = NULL;
    s_pTarget = NULL;
    s_pMix = NULL;
    s_pSramData = NULL;
    s_SramBytes = 0;
    s_VideoData = NULL;
    s_VideoW = s_VideoH = 0;
    s_VideoPitch = 0;
    s_HaveNewVideo = false;
    s_DirectStage = NULL;
    s_DirectStageSerial = 0;
    s_DirectUploadSerial = 0;
    s_DirectReady = false;
    s_MouseDevice = false;
    s_MouseDX = s_MouseDY = 0;
    s_MouseButtons = 0;
    s_ContentData = NULL;
    s_ContentBytes = 0;
    s_MenuOptionsValid = false;
}

void Snes9x2010Bridge_Reset(void)
{
    if (s_GameLoaded)
    {
        S9X2010_retro_reset();
        s_MenuOptionsValid = false;
    }
}

void Snes9x2010Bridge_SoftReset(void)
{
    if (s_GameLoaded)
    {
        S9X2010_retro_reset();
        s_MenuOptionsValid = false;
    }
}

void Snes9x2010Bridge_RunFrame(Emu::SysInputT *input,
                               CRenderSurface *target,
                               CMixBuffer *mix)
{
    if (!s_GameLoaded)
        return;

    s_pInput = input;
    s_pTarget = target;
    s_pMix = mix;
    s_HaveNewVideo = false;

    s9xApplyAuroraMenuOptions();
    const bool useMouse = InputSnesMouseShouldUse() ? true : false;
    if (useMouse)
        InputGetMouseData(&s_MouseDX, &s_MouseDY, &s_MouseButtons);
    else
    {
        s_MouseDX = s_MouseDY = 0;
        s_MouseButtons = 0;
    }
    if (useMouse != s_MouseDevice)
    {
        S9X2010_retro_set_controller_port_device(
            0, useMouse ? RETRO_DEVICE_MOUSE : RETRO_DEVICE_JOYPAD);
        s_MouseDevice = useMouse;
    }
    S9X2010_retro_run();
    /* Do not stage 16-bit pixels in Aurora's 32-bit render surface. */
    s_DirectReady = false;
    if (target && s_HaveNewVideo)
        s9xRender(target);
    s_pInput = NULL;
    s_pTarget = NULL;
    s_pMix = NULL;
}

Int32 Snes9x2010Bridge_GetStateSize(void)
{
    if (!s_GameLoaded)
        return 0;
    size_t n = S9X2010_retro_serialize_size();
    return n && n <= 0x7fffffffU ? (Int32)n : 0;
}

Int32 Snes9x2010Bridge_SaveState(void *data, Int32 bytes)
{
    Int32 need = Snes9x2010Bridge_GetStateSize();
    if (!data || need <= 0 || bytes < need)
        return 0;
    return S9X2010_retro_serialize(data, (size_t)need) ? need : 0;
}

bool Snes9x2010Bridge_LoadState(const void *data, Int32 bytes)
{
    Int32 need = Snes9x2010Bridge_GetStateSize();
    if (!data || need <= 0 || bytes != need)
        return false;
    return S9X2010_retro_unserialize(data, (size_t)bytes);
}

Int32 Snes9x2010Bridge_GetSRAMBytes(void)
{
    return s_GameLoaded ? s_SramBytes : 0;
}

Uint8 *Snes9x2010Bridge_GetSRAMData(void)
{
    return s_GameLoaded ? s_pSramData : NULL;
}

Uint32 Snes9x2010Bridge_GetSampleRate(void)
{
    return s_SampleRate;
}

#else  /* !AURORA_SNES9X2010 */

/* AURORA_SNES9X2010_BUILD_OPTION_V1_20260824: disabled-build stubs. */
bool Snes9x2010Bridge_Init(void) { return false; }
void Snes9x2010Bridge_Shutdown(void) {}
bool Snes9x2010Bridge_LoadGame(const void *pData, size_t nBytes,
                               size_t nCapacity, const char *pName)
{
    (void)pData; (void)nBytes; (void)nCapacity; (void)pName;
    return false;
}
/* Kept as an extra compatibility symbol for trees that still carry V6.1. */
bool Snes9x2010Bridge_FinishInit(void) { return false; }
void Snes9x2010Bridge_UnloadGame(void) {}
void Snes9x2010Bridge_Reset(void) {}
void Snes9x2010Bridge_SoftReset(void) {}
void Snes9x2010Bridge_RunFrame(Emu::SysInputT *pInput,
                               CRenderSurface *pTarget,
                               CMixBuffer *pMix)
{
    (void)pInput; (void)pTarget; (void)pMix;
}
Int32 Snes9x2010Bridge_GetStateSize(void) { return 0; }
Int32 Snes9x2010Bridge_SaveState(void *pData, Int32 nBytes)
{
    (void)pData; (void)nBytes; return 0;
}
bool Snes9x2010Bridge_LoadState(const void *pData, Int32 nBytes)
{
    (void)pData; (void)nBytes; return false;
}
Int32 Snes9x2010Bridge_GetSRAMBytes(void) { return 0; }
Uint8 *Snes9x2010Bridge_GetSRAMData(void) { return NULL; }
Uint32 Snes9x2010Bridge_GetSampleRate(void) { return 32040; }
void Snes9x2010Bridge_InvalidateGsResources(void) {}
bool Snes9x2010Bridge_CanDirectGsVideo(void) { return false; }
bool Snes9x2010Bridge_DrawDirectGs(Uint32 outTexTBP, Float32 intensity)
{
    (void)outTexTBP; (void)intensity; return false;
}

#endif /* AURORA_SNES9X2010 */
