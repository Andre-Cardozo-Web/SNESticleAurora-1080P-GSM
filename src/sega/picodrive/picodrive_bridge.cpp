/* AURORA_PICODRIVE_STAGE2
 * PicoDrive libretro bridge + small native hardware hooks for PS2/Aurora.
 *
 * Runtime core execution/state/SRAM is libretro. Native PicoDrive symbols are
 * used only where libretro deliberately hides hardware details we need:
 * physical port type, Mega Mouse coordinates, active system and SMS backdrop.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "picodrive_bridge.h"
#include "types.h"
#include "emuinput.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"
#include "audmixbuffer.h"
#include "snio.h"
#include "snrom.h"

extern "C" {
#include "gpprim.h"
}

extern "C" {
#include <libretro.h>
#include <libretro_gskit_ps2.h>
#include <pico/pico.h>
#include <pico/pico_int.h>

/* AURORA_PD_BORROW_AURORA_ROM_V1 */
void PicoCartSetExternalRomBuffer(const unsigned char *rom,
                                  unsigned int capacity);
}

static bool s_Initialized = false;
static bool s_GameLoaded = false;
static bool s_Use6Button = false;
static int  s_AuroraRegion = SNES_FORCE_REGION_OFF;
static bool s_VariablesChanged = false;

/* Cache SRAM metadata at load time. PicoDrive's libretro API intentionally
 * reports size 0 after frame 0 while SRAM is still all-zero; Aurora needs a
 * stable size for later dirty checks and menu saves. */
static Uint8 *s_pSramData = NULL;
static Int32  s_SramBytes = 0;

/* AURORA_MD_MENU_MAPPING_SRAM_FIX_V4
 * PicoDrive cria 0x200000..0x203fff como janela SRAM genérica quando o
 * header não oferece uma faixa válida. Isso é útil internamente para
 * compatibilidade, mas não deve sozinho significar "este cartucho tem
 * bateria" para o frontend.
 *
 * Mantemos save real quando:
 *   - o header Sega contém "RA" (mesmo se a faixa do header for ruim);
 *   - carthw detectou EEPROM;
 *   - carthw selecionou uma faixa diferente do fallback genérico.
 *
 * SMS/GG/Pico/MCD/32X mantêm a política própria do PicoDrive. */
static bool pdHasFrontendSaveMemory(void)
{
    if (PicoIn.AHW & (PAHW_SMS | PAHW_PICO | PAHW_MCD | PAHW_32X))
        return true;

    if (!(Pico.sv.flags & SRF_ENABLED) ||
        !Pico.sv.data || Pico.sv.size <= 0)
        return false;

    if (Pico.sv.flags & SRF_EEPROM)
        return true;

    /* Pico.rom está word-swapped dentro do core. */
    if (Pico.rom && Pico.romsize > 0x1b1 &&
        Pico.rom[MEM_BE2(0x1b0)] == 'R' &&
        Pico.rom[MEM_BE2(0x1b1)] == 'A')
        return true;

    if (Pico.sv.start != 0x200000 || Pico.sv.end != 0x203fff)
        return true;

    return false;
}

static Emu::SysInputT *s_pInput = NULL;
static CMixBuffer *s_pMix = NULL;

static bool s_MouseActive = false;
static int s_MouseX = 160;
static int s_MouseY = 120;
static unsigned s_MouseButtons = 0;

static GSTEXTURE s_CoreTexture;
static RETRO_HW_RENDER_INTEFACE_GSKIT_PS2 s_Hw;

static char s_ContentName[1024] = "game.md";
static const void *s_ContentData = NULL;
static size_t s_ContentBytes = 0;
static char s_ContentBaseName[1024] = "game";
static char s_ContentExt[16] = "md";
static struct retro_game_info_ext s_ContentInfoExt;
static const char s_DotPath[] = ".";

/* Keep in sync with _RomData in mainloop_globals.cpp. */
enum { PD_AURORA_ROM_BUFFER_CAPACITY = 8 * 1024 * 1024 + 1024 };

enum { PD_AUDIO_CHUNK = 1024 };
static Int16 s_AudioL[PD_AUDIO_CHUNK];
static Int16 s_AudioR[PD_AUDIO_CHUNK];

/* AURORA_PICODRIVE_AUDIO_ODD_TAIL_V1
 * PicoDrive at 32 kHz/60 Hz naturally emits odd-sized batches
 * (typically 533/533/534 stereo frames). Aurora's fast 32->48 kHz
 * converter consumes input in pairs, so keep one unmatched stereo
 * frame for the next callback instead of dropping it. */
static bool  s_AudioTailValid = false;
static Int16 s_AudioTailL = 0;
static Int16 s_AudioTailR = 0;

static void pdAudioTailReset()
{
    s_AudioTailValid = false;
    s_AudioTailL = 0;
    s_AudioTailR = 0;
}


/* AURORA_PICODRIVE_LAST_CHANCE_PERF_V1
 * The fast PicoDrive renderer emits 8-bit palette indices. Keep conversion
 * state small/hot and precompute scale coordinates only when geometry changes. */
static Uint32 s_PaletteRGBA[256];
/* AURORA_MD_DIRECT_CLUT_CACHE_V1 */
static Uint16 s_DirectLastClut[256];
static bool s_DirectClutValid = false;
static Uint16 s_H40SharpMap[256];
static bool s_H40SharpMapReady = false;
static int s_MapLeft = -1, s_MapTop = -1;
static int s_MapSrcW = -1, s_MapSrcH = -1;
static int s_MapDstW = -1, s_MapDstH = -1;
static int s_LastPortType[2] = { -1, -1 };


static void pdLog(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;
    if (!fmt) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static Uint32 pdColor555ToRGBA(Uint16 c)
{
    Uint32 r5 = c & 31u;
    Uint32 g5 = (c >> 5) & 31u;
    Uint32 b5 = (c >> 10) & 31u;
    Uint32 r = (r5 << 3) | (r5 >> 2);
    Uint32 g = (g5 << 3) | (g5 >> 2);
    Uint32 b = (b5 << 3) | (b5 >> 2);
    return 0xff000000u | (b << 16) | (g << 8) | r;
}

static const char *pdRegionName()
{
    switch (s_AuroraRegion)
    {
        case SNES_FORCE_REGION_NTSC_U: return "US";
        case SNES_FORCE_REGION_NTSC_J: return "Japan NTSC";
        case SNES_FORCE_REGION_PAL:    return "Europe";
        default:                       return "Auto";
    }
}

static bool pdEnvironment(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE:
            if (!data) return false;
            *(void **)data = &s_Hw;
            return true;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            if (!data) return false;
            ((struct retro_log_callback *)data)->log = pdLog;
            return true;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            return true;

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            if (!data) return false;
            struct retro_variable *var = (struct retro_variable *)data;
            if (!var->key) return false;

            if (!strcmp(var->key, "picodrive_sound_rate"))
                var->value = "32000"; /* must match Aurora AudMixBuffer */
            else if (!strcmp(var->key, "picodrive_renderer"))
                var->value = "fast";  /* intermediate 8-bit renderer */
            else if (!strcmp(var->key, "picodrive_fm_filter"))
                var->value = "off";
            else if (!strcmp(var->key, "picodrive_audio_filter"))
                var->value = "disabled";
            else if (!strcmp(var->key, "picodrive_ggghost"))
                var->value = "off";
            else if (!strcmp(var->key, "picodrive_smstms"))
            {
                /* AURORA_PD_SG1000_TMS_PALETTE_V6
                 * PicoDrive's SMS VDP can run the legacy TMS graphics modes
                 * used by SG-1000/SC-3000 software. The SMS replacement
                 * palette is substantially darker; ask PicoDrive to use its
                 * original SG-1000/TMS palette for those modes instead.
                 *
                 * This core option only changes TMS-mode palette selection:
                 * normal SMS mode 4, Game Gear and Mega Drive rendering are
                 * unaffected. */
                var->value = "SG-1000";
            }
            else if (!strcmp(var->key, "picodrive_drc"))
                var->value = "enabled";
            else if (!strcmp(var->key, "picodrive_frameskip"))
                var->value = "disabled";
            else if (!strcmp(var->key, "picodrive_sprlim"))
                var->value = "disabled";
            else if (!strcmp(var->key, "picodrive_region"))
                var->value = pdRegionName();
            else
                var->value = NULL;

            return var->value != NULL;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            if (data)
            {
                *(bool *)data = s_VariablesChanged;
                s_VariablesChanged = false;
                return true;
            }
            return false;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY:
            if (data)
            {
                *(const char **)data = s_DotPath;
                return true;
            }
            return false;

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *(bool *)data = true;
            return true;

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            if (data) *(unsigned *)data = 0;
            return true;

        /* Registration / notification calls that Aurora can safely accept
         * without implementing a desktop RetroArch UI. */
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        case RETRO_ENVIRONMENT_SET_MESSAGE:
        case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
            return true;

        /* One callback per pad instead of ~16 callbacks per pad/frame. */
        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return true;

        /* AURORA_PICODRIVE_STAGE3_GAME_INFO_EXT
         * PicoDrive already supports persistent in-memory content through
         * this standard libretro query. Keep the submodule source pristine. */
        case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
            if (!data || !s_ContentData || !s_ContentBytes)
                return false;
            *(const struct retro_game_info_ext **)data = &s_ContentInfoExt;
            return true;

        default:
            return false;
    }
}

static void pdVideoRefresh(const void *data, unsigned width,
                           unsigned height, size_t pitch)
{
    (void)data;
    (void)width;
    (void)height;
    (void)pitch;
    /* PS2 PicoDrive renders into s_CoreTexture.Mem through the GSKit
     * hardware interface. The bridge copies it after retro_run(). */
}

static void pdAudioOutputInterleaved(const int16_t *data, size_t frames)
{
    size_t pos = 0;

    if (!data || frames == 0)
        return;

    /* If a frame is deliberately run without a mixer, its audio is
     * discarded. Do not carry an older tail across that discontinuity. */
    if (!s_pMix)
    {
        pdAudioTailReset();
        return;
    }

    /* Prepend a pending frame to an odd number of new frames. This makes
     * an even chunk while retaining normal lookahead for interpolation. */
    if (s_AudioTailValid)
    {
        size_t take = frames;
        if (take > PD_AUDIO_CHUNK - 1)
            take = PD_AUDIO_CHUNK - 1;
        if ((take & 1u) == 0)
            --take;

        s_AudioL[0] = s_AudioTailL;
        s_AudioR[0] = s_AudioTailR;

        for (size_t i = 0; i < take; ++i)
        {
            s_AudioL[i + 1] = data[i * 2 + 0];
            s_AudioR[i + 1] = data[i * 2 + 1];
        }

        s_AudioTailValid = false;
        s_pMix->OutputSamplesStereo(
            s_AudioL, s_AudioR, (Int32)(take + 1));

        pos = take;
    }

    /* Aurora's fast 2:3 converter is pair-based. Feed it only even
     * input counts and retain, rather than discard, a final odd frame. */
    while (pos < frames)
    {
        size_t n = frames - pos;

        if (n > PD_AUDIO_CHUNK)
            n = PD_AUDIO_CHUNK;

        n &= ~(size_t)1;

        if (n == 0)
            break;

        for (size_t i = 0; i < n; ++i)
        {
            s_AudioL[i] = data[(pos + i) * 2 + 0];
            s_AudioR[i] = data[(pos + i) * 2 + 1];
        }

        s_pMix->OutputSamplesStereo(
            s_AudioL, s_AudioR, (Int32)n);

        pos += n;
    }

    if (pos < frames)
    {
        s_AudioTailL = data[pos * 2 + 0];
        s_AudioTailR = data[pos * 2 + 1];
        s_AudioTailValid = true;
    }
}

static void pdAudioSample(int16_t left, int16_t right)
{
    const int16_t frame[2] = { left, right };
    pdAudioOutputInterleaved(frame, 1);
}

static size_t pdAudioBatch(const int16_t *data, size_t frames)
{
    pdAudioOutputInterleaved(data, frames);
    return frames;
}

static void pdInputPoll()
{
}

static bool pdPadHas(Uint16 p, Uint16 bit)
{
    return p != EMUSYS_DEVICE_DISCONNECTED && (p & bit) != 0;
}

static bool pdIs8Bit();
static bool pdIsMasterSystem();

static int16_t pdJoyMask(unsigned port)
{
    unsigned int m = 0;
    Uint16 p;

    if (port >= EMUSYS_DEVICE_NUM)
        return 0;

    if (port == 0 && s_MouseActive)
    {
        if (s_MouseButtons & 1u) m |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
        if (s_MouseButtons & 2u) m |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
        return (int16_t)m;
    }

    if (!s_pInput)
        return 0;
    p = s_pInput->uPad[port];
    if (p == EMUSYS_DEVICE_DISCONNECTED)
        return 0;

    if (pdPadHas(p, SNESIO_JOY_UP))    m |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
    if (pdPadHas(p, SNESIO_JOY_DOWN))  m |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    if (pdPadHas(p, SNESIO_JOY_LEFT))  m |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
    if (pdPadHas(p, SNESIO_JOY_RIGHT)) m |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;

    if (pdIs8Bit())
    {
        if (pdPadHas(p, SNESIO_JOY_B))     m |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
        if (pdPadHas(p, SNESIO_JOY_A))     m |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
        if (pdPadHas(p, SNESIO_JOY_START)) m |= 1u << RETRO_DEVICE_ID_JOYPAD_START;
    }
    else
    {
        if (pdPadHas(p, SNESIO_JOY_A))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_Y;
        if (pdPadHas(p, SNESIO_JOY_B))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_B;
        if (pdPadHas(p, SNESIO_JOY_Y))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_A;
        if (pdPadHas(p, SNESIO_JOY_L))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_L;
        if (pdPadHas(p, SNESIO_JOY_X))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_X;
        if (pdPadHas(p, SNESIO_JOY_R))      m |= 1u << RETRO_DEVICE_ID_JOYPAD_R;
        if (pdPadHas(p, SNESIO_JOY_SELECT)) m |= 1u << RETRO_DEVICE_ID_JOYPAD_SELECT;
        if (pdPadHas(p, SNESIO_JOY_START))  m |= 1u << RETRO_DEVICE_ID_JOYPAD_START;
    }

    return (int16_t)m;
}

static int16_t pdInputState(unsigned port, unsigned device,
                            unsigned index, unsigned id)
{
    (void)index;
    if (device != RETRO_DEVICE_JOYPAD || port >= EMUSYS_DEVICE_NUM)
        return 0;

    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return pdJoyMask(port);

    /* Mega Mouse still uses PicoIn.pad for B/C/Start button state. */
    if (port == 0 && s_MouseActive)
    {
        if (id == RETRO_DEVICE_ID_JOYPAD_B)
            return (s_MouseButtons & 1u) ? 1 : 0; /* left -> Mega Mouse B */
        if (id == RETRO_DEVICE_ID_JOYPAD_A)
            return (s_MouseButtons & 2u) ? 1 : 0; /* right -> Mega Mouse C */
        return 0;
    }

    if (!s_pInput)
        return 0;

    Uint16 p = s_pInput->uPad[port];
    if (p == EMUSYS_DEVICE_DISCONNECTED)
        return 0;

    /* AURORA_PICODRIVE_8BIT_INPUT_V1
     * NES-like host layout is converted to SMS/GG B/C here. */
    if (pdIs8Bit())
    {
        switch (id)
        {
            case RETRO_DEVICE_ID_JOYPAD_UP:    return pdPadHas(p, SNESIO_JOY_UP);
            case RETRO_DEVICE_ID_JOYPAD_DOWN:  return pdPadHas(p, SNESIO_JOY_DOWN);
            case RETRO_DEVICE_ID_JOYPAD_LEFT:  return pdPadHas(p, SNESIO_JOY_LEFT);
            case RETRO_DEVICE_ID_JOYPAD_RIGHT: return pdPadHas(p, SNESIO_JOY_RIGHT);
            case RETRO_DEVICE_ID_JOYPAD_B:     return pdPadHas(p, SNESIO_JOY_B); /* Cross / turbo Circle */
            case RETRO_DEVICE_ID_JOYPAD_A:     return pdPadHas(p, SNESIO_JOY_A); /* Square / turbo Triangle */
            case RETRO_DEVICE_ID_JOYPAD_START: return pdPadHas(p, SNESIO_JOY_START);
            default: return 0;
        }
    }

    switch (id)
    {
        case RETRO_DEVICE_ID_JOYPAD_UP:     return pdPadHas(p, SNESIO_JOY_UP);
        case RETRO_DEVICE_ID_JOYPAD_DOWN:   return pdPadHas(p, SNESIO_JOY_DOWN);
        case RETRO_DEVICE_ID_JOYPAD_LEFT:   return pdPadHas(p, SNESIO_JOY_LEFT);
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return pdPadHas(p, SNESIO_JOY_RIGHT);

        /* Carrier mapping produced by _MainLoopSegaInput:
         * Square=A, Cross=B, Circle=C, L1=X, Triangle=Y, R1=Z,
         * R3=MODE, Start=Start. */
        case RETRO_DEVICE_ID_JOYPAD_Y:      return pdPadHas(p, SNESIO_JOY_A);      /* MD A */
        case RETRO_DEVICE_ID_JOYPAD_B:      return pdPadHas(p, SNESIO_JOY_B);      /* MD B */
        case RETRO_DEVICE_ID_JOYPAD_A:      return pdPadHas(p, SNESIO_JOY_Y);      /* MD C */
        case RETRO_DEVICE_ID_JOYPAD_L:      return pdPadHas(p, SNESIO_JOY_L);      /* MD X */
        case RETRO_DEVICE_ID_JOYPAD_X:      return pdPadHas(p, SNESIO_JOY_X);      /* MD Y */
        case RETRO_DEVICE_ID_JOYPAD_R:      return pdPadHas(p, SNESIO_JOY_R);      /* MD Z */
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return pdPadHas(p, SNESIO_JOY_SELECT); /* MODE */
        case RETRO_DEVICE_ID_JOYPAD_START:  return pdPadHas(p, SNESIO_JOY_START);
        default: return 0;
    }
}

static void pdApplyPortTypes()
{
    int desired0, desired1;
    bool is8bit;

    if (!s_GameLoaded)
        return;

    is8bit = (PicoIn.AHW & PAHW_8BIT) != 0;

    if (s_MouseActive && !is8bit)
        desired0 = PICO_INPUT_MOUSE;
    else if (s_pInput && s_pInput->uPad[0] != EMUSYS_DEVICE_DISCONNECTED)
        desired0 = (!is8bit && s_Use6Button)
                 ? PICO_INPUT_PAD_6BTN : PICO_INPUT_PAD_3BTN;
    else
        desired0 = PICO_INPUT_NOTHING;

    if (s_pInput && s_pInput->uPad[1] != EMUSYS_DEVICE_DISCONNECTED)
        desired1 = (!is8bit && s_Use6Button)
                 ? PICO_INPUT_PAD_6BTN : PICO_INPUT_PAD_3BTN;
    else
        desired1 = PICO_INPUT_NOTHING;

    if (desired0 != s_LastPortType[0])
    {
        PicoSetInputDevice(0, (enum input_device)desired0);
        s_LastPortType[0] = desired0;
    }
    if (desired1 != s_LastPortType[1])
    {
        PicoSetInputDevice(1, (enum input_device)desired1);
        s_LastPortType[1] = desired1;
    }
}

static bool pdIsGameGear()
{
    return (PicoIn.AHW & PAHW_GG) != 0 ||
           (Pico.m.hardware & PMS_HW_LCD) != 0;
}

static bool pdIs8Bit()
{
    return (PicoIn.AHW & PAHW_8BIT) != 0;
}

static bool pdIsMasterSystem()
{
    return (PicoIn.AHW & PAHW_SMS) != 0 && !pdIsGameGear();
}

/* AURORA_MD_UNIFORM_SCALE_V1 */
static Uint32 pdSmsBorderRGBA()
{
    if (!pdIsMasterSystem())
        return 0xff000000u;

    /* Mode 4 backdrop is VDP R7 low nibble in palette 1 (0x10..0x1f).
     * TMS modes use the low-nibble palette directly. HighPal is the renderer's
     * authoritative converted palette; PicoCramHigh can be stale/16-bit-path
     * specific when the libretro "good" 8-bit renderer is active. */
    if (Pico.m.dirtyPal)
        PicoDrawUpdateHighPal();

    unsigned idx = (unsigned)Pico.video.reg[7] & 0x0fu;
    if (!(Pico.m.hardware & PMS_HW_TMS))
        idx |= 0x10u;

    return pdColor555ToRGBA(Pico.est.HighPal[idx & 0xffu]);
}


static Uint32 pdFetchRGBA(int x, int y, int texW)
{
    if (s_CoreTexture.PSM == GS_PSM_T8)
    {
        const Uint8 *p = (const Uint8 *)s_CoreTexture.Mem + y * texW + x;
        return s_PaletteRGBA[*p];
    }
    else
    {
        const Uint16 *p = (const Uint16 *)s_CoreTexture.Mem + y * texW + x;
        return pdColor555ToRGBA(*p);
    }
}

static void pdRenderToAurora(CRenderSurface *pTarget)
{
    PixelFormatT *fmt;
    Uint32 tw, th, visH, border;
    int texW, texH, left, right, top, bottom;
    int srcW, srcH, dstW, dstH, dstX, dstY, cropX, cropY;
    int x, y;

    if (!pTarget || !s_CoreTexture.Mem)
        return;

    fmt = pTarget->GetFormat();
    if (!fmt || fmt->uBitDepth != 32)
        return;

    tw = pTarget->GetWidth();
    th = pTarget->GetHeight();
    if (!tw || !th)
        return;
    visH = th < 240 ? th : 240;

    if (s_CoreTexture.PSM != GS_PSM_CT16 && s_CoreTexture.PSM != GS_PSM_T8)
        return;

    texW = (int)s_CoreTexture.Width;
    texH = (int)s_CoreTexture.Height;
    left   = (int)(s_Hw.padding.left   + 0.5f);
    right  = (int)(s_Hw.padding.right  + 0.5f);
    top    = (int)(s_Hw.padding.top    + 0.5f);
    bottom = (int)(s_Hw.padding.bottom + 0.5f);

    if (left < 0) left = 0;
    if (right < 0) right = 0;
    if (top < 0) top = 0;
    if (bottom < 0) bottom = 0;

    srcW = texW - left - right;
    srcH = texH - top - bottom;
    if (srcW <= 0 || srcH <= 0 || left + srcW > texW || top + srcH > texH)
        return;

    if (s_CoreTexture.PSM == GS_PSM_T8)
    {
        if (Pico.m.dirtyPal)
            PicoDrawUpdateHighPal();
        for (x = 0; x < 256; ++x)
            s_PaletteRGBA[x] = pdColor555ToRGBA(Pico.est.HighPal[x]);
    }

    border = pdIsMasterSystem() ? pdSmsBorderRGBA() : 0xff000000u;

    /* Vertical pixels are never rescaled. 224-line MD is centred 1:1 in the
       240-line logical raster; SMS 192/224/240 and GG 144 follow the same rule. */
    cropY = srcH > (int)visH ? (srcH - (int)visH) / 2 : 0;
    dstH = srcH > (int)visH ? (int)visH : srcH;
    dstY = ((int)visH - dstH) / 2;

    cropX = 0;
    if (srcW == 320 && tw >= 256)
    {
        /* H40: exact 5 source pixels -> 4 logical pixels. This is an AREA
         * resample, not nearest-neighbour skipping, so the screen no longer
         * has periodically wider/narrower source-pixel groups. */
        dstW = 256;
        dstX = ((int)tw - 256) / 2;
    }
    else if (srcW <= (int)tw)
    {
        dstW = srcW;
        /* SMS first-column mask reports a 248-pixel active span. Hardware's
           missing 8 pixels are on the LEFT, not 4 on each side. */
        if (pdIsMasterSystem() && srcW == 248 && tw >= 256)
            dstX = 8;
        else
            dstX = ((int)tw - dstW) / 2;
    }
    else
    {
        /* Unknown wider modes: centre-crop 1:1 rather than introducing an
           irregular nearest-neighbour scale. */
        dstW = (int)tw;
        dstX = 0;
        cropX = (srcW - dstW) / 2;
    }

    for (y = 0; y < (int)th; ++y)
    {
        Uint32 *dst = (Uint32 *)pTarget->GetLinePtr(y);
        if (!dst) continue;

        if (y >= (int)visH || y < dstY || y >= dstY + dstH)
        {
            Uint32 fill = y < (int)visH ? border : 0xff000000u;
            for (x = 0; x < (int)tw; ++x) dst[x] = fill;
            continue;
        }

        for (x = 0; x < dstX; ++x) dst[x] = border;
        for (x = dstX + dstW; x < (int)tw; ++x) dst[x] = border;

        {
            int sy = top + cropY + (y - dstY);
            Uint32 *out = dst + dstX;

            if (srcW == 320 && dstW == 256)
            {
                /* AURORA_PD_FAST_SHARP_240P_V2
                 * Nearest source-centre map for 320 -> 256.
                 * No bilinear/area blend: every output column is one exact
                 * PicoDrive pixel colour. Map is built once, not per frame. */
                if (!s_H40SharpMapReady)
                {
                    for (x = 0; x < 256; ++x)
                        s_H40SharpMap[x] =
                            (Uint16)(((unsigned)x * 5u + 2u) >> 2);
                    s_H40SharpMapReady = true;
                }

                for (x = 0; x < 256; ++x)
                    out[x] = pdFetchRGBA(
                        left + (int)s_H40SharpMap[x], sy, texW);
            }
            else
            {
                int sx0 = left + cropX;
                for (x = 0; x < dstW; ++x)
                    out[x] = pdFetchRGBA(sx0 + x, sy, texW);
            }
        }
    }
}

/* AURORA_GS_VRAM_EPOCH_V4_2
 * Direct T8 rows are uploaded every frame, but the CLUT has a residency cache.
 * Reinitialising gsKit destroys that residency even if PicoDrive stays alive. */
void PicoDriveBridge_InvalidateGsResources(void)
{
    s_DirectClutValid = false;
}

bool PicoDriveBridge_Init(void)
{
    if (s_Initialized)
        return true;

    memset(&s_CoreTexture, 0, sizeof(s_CoreTexture));
    memset(&s_Hw, 0, sizeof(s_Hw));
    s_Hw.interface_type = RETRO_HW_RENDER_INTERFACE_GSKIT_PS2;
    s_Hw.interface_version = RETRO_HW_RENDER_INTERFACE_GSKIT_PS2_VERSION;
    s_Hw.coreTexture = &s_CoreTexture;

    retro_set_environment(pdEnvironment);
    retro_set_video_refresh(pdVideoRefresh);
    retro_set_audio_sample(pdAudioSample);
    retro_set_audio_sample_batch(pdAudioBatch);
    retro_set_input_poll(pdInputPoll);
    retro_set_input_state(pdInputState);

    if (retro_api_version() != RETRO_API_VERSION)
    {
        printf("[PicoDrive] libretro API mismatch\n");
        return false;
    }

    retro_init();
    s_Initialized = true;
    return true;
}

void PicoDriveBridge_Shutdown(void)
{
    if (!s_Initialized)
        return;

    PicoDriveBridge_UnloadGame();
    retro_deinit();
    memset(&s_CoreTexture, 0, sizeof(s_CoreTexture));
    s_Initialized = false;
}

bool PicoDriveBridge_LoadGame(const void *pData, size_t nBytes, const char *pName)
{
    if (!pData || !nBytes || !PicoDriveBridge_Init())
        return false;

    if (s_GameLoaded)
        PicoDriveBridge_UnloadGame();

    if (pName && *pName)
    {
        strncpy(s_ContentName, pName, sizeof(s_ContentName) - 1);
        s_ContentName[sizeof(s_ContentName) - 1] = 0;
    }
    else
    {
        strcpy(s_ContentName, "game.md");
    }

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = s_ContentName;
    info.data = pData;
    info.size = nBytes;

    /* The buffer belongs to SegaRom/Aurora and remains alive until unload. */
    s_ContentData = pData;
    s_ContentBytes = nBytes;

    /* PicoDrive's GET_GAME_INFO_EXT path also needs canonical path metadata. */
    {
        const char *base = strrchr(s_ContentName, '/');
        const char *base2 = strrchr(s_ContentName, '\\');
        if (!base || (base2 && base2 > base)) base = base2;
        base = base ? base + 1 : s_ContentName;
        const char *dot = strrchr(base, '.');
        size_t baseLen = dot && dot > base ? (size_t)(dot - base) : strlen(base);
        if (baseLen >= sizeof(s_ContentBaseName)) baseLen = sizeof(s_ContentBaseName) - 1;
        memcpy(s_ContentBaseName, base, baseLen);
        s_ContentBaseName[baseLen] = 0;
        if (dot && dot[1])
        {
            strncpy(s_ContentExt, dot + 1, sizeof(s_ContentExt) - 1);
            s_ContentExt[sizeof(s_ContentExt) - 1] = 0;
        }
        else
        {
            strcpy(s_ContentExt, "md");
        }

        memset(&s_ContentInfoExt, 0, sizeof(s_ContentInfoExt));
        s_ContentInfoExt.full_path = s_ContentName;
        s_ContentInfoExt.dir = s_DotPath;
        s_ContentInfoExt.name = s_ContentBaseName;
        s_ContentInfoExt.ext = s_ContentExt;
        s_ContentInfoExt.data = s_ContentData;
        s_ContentInfoExt.size = s_ContentBytes;
        s_ContentInfoExt.file_in_archive = false;
        s_ContentInfoExt.persistent_data = true;
    }

    /* AURORA_PD_BORROW_AURORA_ROM_V1
     * SegaRom already points at Aurora's _RomData. Lend that storage
     * to PicoDrive instead of allocating/copying a second cartridge. */
    PicoCartSetExternalRomBuffer(
        (const unsigned char *)pData,
        (unsigned int)PD_AURORA_ROM_BUFFER_CAPACITY);

    s_VariablesChanged = true;
    bool loadOk = retro_load_game(&info);

    /* Offer is only for this load; cart.c separately tracks ownership
     * of the active borrowed Pico.rom until PicoCartUnload(). */
    PicoCartSetExternalRomBuffer(NULL, 0);

    if (!loadOk)
    {
        printf("[PicoDrive] retro_load_game failed: %s (%u bytes)\n",
               s_ContentName, (unsigned)nBytes);
        PicoCartUnload();
        PicoIn.AHW = 0;
        PicoIn.quirks = 0;
        s_pSramData = NULL;
        s_SramBytes = 0;
        s_ContentData = NULL;
        s_ContentBytes = 0;
        return false;
    }

    pdAudioTailReset();
    s_GameLoaded = true;
    /* AURORA_MD_CLUT_GAME_LIFETIME_V1 */
    s_DirectClutValid = false;
    AudMixSetFastResample(1);
    s_LastPortType[0] = s_LastPortType[1] = -1;
    s_MapLeft = s_MapTop = -1;
    s_MapSrcW = s_MapSrcH = s_MapDstW = s_MapDstH = -1;
    {
        size_t nSram = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
        void *pSram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
        if (pSram && nSram > 0 && nSram <= 0x7fffffffU)
        {
            s_pSramData = (Uint8 *)pSram;
            s_SramBytes = (Int32)nSram;
        }
        else
        {
            s_pSramData = NULL;
            s_SramBytes = 0;
        }
    }
    s_MouseX = 160;
    s_MouseY = 120;
    s_MouseActive = false;
    s_MouseButtons = 0;

    /* First frame will refine this from the real PS2 plug state. */
    PicoSetInputDevice(0, s_Use6Button ? PICO_INPUT_PAD_6BTN
                                      : PICO_INPUT_PAD_3BTN);
    PicoSetInputDevice(1, s_Use6Button ? PICO_INPUT_PAD_6BTN
                                      : PICO_INPUT_PAD_3BTN);

    printf("[PicoDrive] loaded %s; AHW=%04x, SRAM=%d\n",
           s_ContentName, (unsigned)PicoIn.AHW,
           PicoDriveBridge_GetSRAMBytes());
    return true;
}

void PicoDriveBridge_UnloadGame(void)
{
    if (s_Initialized)
    {
        if (s_GameLoaded)
            retro_unload_game();

        /* PicoDrive's libretro retro_unload_game() is currently empty.
         * Our routed systems are cartridge based (MD/SMS/GG/32X), so this
         * native cleanup is the missing ownership boundary. It frees the
         * copied cartridge before Aurora can start SNES. */
        PicoCartUnload();
        PicoIn.AHW = 0;
        PicoIn.quirks = 0;
    }

    s_GameLoaded = false;
    s_DirectClutValid = false;
    pdAudioTailReset();
    AudMixSetFastResample(0);
    s_LastPortType[0] = s_LastPortType[1] = -1;
    s_MapLeft = s_MapTop = -1;
    s_MapSrcW = s_MapSrcH = s_MapDstW = s_MapDstH = -1;
    s_ContentData = NULL;
    s_ContentBytes = 0;
    s_pSramData = NULL;
    s_SramBytes = 0;
    s_pInput = NULL;
    s_pMix = NULL;
    s_MouseActive = false;
    s_MouseButtons = 0;
    memset(&s_CoreTexture, 0, sizeof(s_CoreTexture));
    s_Hw.coreTexture = &s_CoreTexture;
}

void PicoDriveBridge_Reset(void)
{
    if (s_GameLoaded)
        retro_reset();
    pdAudioTailReset();
}

void PicoDriveBridge_SoftReset(void)
{
    if (s_GameLoaded)
        retro_reset();
    pdAudioTailReset();
}

void PicoDriveBridge_Set6Button(bool enabled)
{
    if (s_Use6Button == enabled)
        return;
    s_Use6Button = enabled;
}

bool PicoDriveBridge_Get6Button(void)
{
    return s_Use6Button;
}

bool PicoDriveBridge_IsMasterSystem(void)
{
    return s_GameLoaded && pdIsMasterSystem();
}

bool PicoDriveBridge_Is8Bit(void)
{
    return s_GameLoaded && pdIs8Bit();
}

void PicoDriveBridge_SetRegion(int auroraRegion)
{
    if (s_AuroraRegion == auroraRegion)
        return;
    s_AuroraRegion = auroraRegion;
    s_VariablesChanged = true;

    /* A region change may switch the 50/60 Hz audio cadence. */
    pdAudioTailReset();
}

void PicoDriveBridge_SetMouseInput(bool active, int dx, int dy, unsigned buttons)
{
    s_MouseActive = active;
    s_MouseButtons = buttons & 3u;

    if (!active)
        return;

    s_MouseX += dx;
    s_MouseY += dy;
    if (s_MouseX < 0) s_MouseX = 0;
    if (s_MouseX > 320) s_MouseX = 320;
    if (s_MouseY < 0) s_MouseY = 0;
    if (s_MouseY > 240) s_MouseY = 240;

    PicoIn.mouse[0] = (short)s_MouseX;
    PicoIn.mouse[1] = (short)s_MouseY;
}

void PicoDriveBridge_RunFrame(Emu::SysInputT *pInput,
                              CRenderSurface *pTarget,
                              CMixBuffer *pMixBuf)
{
    if (!s_GameLoaded)
        return;

    s_pInput = pInput;
    s_pMix = pMixBuf;

    pdApplyPortTypes();
    if (s_MouseActive)
    {
        PicoIn.mouse[0] = (short)s_MouseX;
        PicoIn.mouse[1] = (short)s_MouseY;
    }

    /* AURORA_PD_FORCE_HW_SPRITE_LIMIT */
    PicoIn.opt &= ~POPT_DIS_SPRITE_LIM;

    retro_run();
    /* Aurora copies the core framebuffer itself: never request blur. */
    s_CoreTexture.Filter = GS_FILTER_NEAREST;

    pdRenderToAurora(pTarget);

    if (s_pMix)
        s_pMix->Flush();
    s_pMix = NULL;
}

/* AURORA_PD_NATIVE320_DIRECT_T8_V1
 *
 * Plain Mega Drive Fast renderer path.
 * PicoDrive/PS2 already produces a GS-ready T8 framebuffer + rotated CLUT.
 * Avoid EE T8->RGBA32 conversion and the later RGBA upload entirely.
 */
enum
{
    PD_GS_T8_TBP_OFFSET   = 0x400,
    PD_GS_CLUT_TBP_OFFSET = 0x580,
    PD_GS_T8_TBW          = 384
};

bool PicoDriveBridge_IsMegaDrive(void)
{
    if (!s_GameLoaded)
        return false;

    return (PicoIn.AHW &
            (PAHW_SMS | PAHW_PICO | PAHW_MCD | PAHW_32X)) == 0;
}

bool PicoDriveBridge_CanDirectGsVideo(void)
{
    /* Called before retro_run(); the first run installs Mem/Clut. */
    return PicoDriveBridge_IsMegaDrive();
}

static Uint32 pdPs2ClutBackdropRGBA(unsigned logicalIndex)
{
    unsigned slot;

    logicalIndex &= 0x3fU;
    slot = logicalIndex;

    /* PicoDrive PS2 swaps CLUT blocks 0x08 and 0x10. The swap is
     * self-inverse, so this maps VDP index -> physical CLUT slot. */
    if ((slot & 0x18U) == 0x08U)
        slot += 8U;
    else if ((slot & 0x18U) == 0x10U)
        slot -= 8U;

    if (s_CoreTexture.Clut)
    {
        const Uint16 *pal = (const Uint16 *)s_CoreTexture.Clut;
        return pdColor555ToRGBA(pal[slot]);
    }

    if (Pico.m.dirtyPal)
        PicoDrawUpdateHighPal();
    return pdColor555ToRGBA(Pico.est.HighPal[logicalIndex]);
}

static Uint32 pdScaleDirectColor(Uint32 c, Float32 intensity)
{
    Uint32 r, g, b;

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    r = (Uint32)((Float32)( c        & 0xffU) * intensity + 0.5f);
    g = (Uint32)((Float32)((c >>  8) & 0xffU) * intensity + 0.5f);
    b = (Uint32)((Float32)((c >> 16) & 0xffU) * intensity + 0.5f);

    return 0x80000000U | (b << 16) | (g << 8) | r;
}

bool PicoDriveBridge_DrawDirectGs(Uint32 auroraOutBaseTBP, Float32 intensity)
{
    int texW, texH;
    int left, right, top, bottom;
    int srcW, srcH;
    int fbW, fbH, dstW, dstH, dstX, dstY;
    Uint32 texTBP, clutTBP;
    Uint32 backdrop, modColor;
    unsigned mod;

    if (!PicoDriveBridge_IsMegaDrive())
        return false;

    if (!s_CoreTexture.Mem || !s_CoreTexture.Clut ||
        s_CoreTexture.PSM != GS_PSM_T8)
        return false;

    texW = (int)s_CoreTexture.Width;
    texH = (int)s_CoreTexture.Height;
    left   = (int)(s_Hw.padding.left   + 0.5f);
    right  = (int)(s_Hw.padding.right  + 0.5f);
    top    = (int)(s_Hw.padding.top    + 0.5f);
    bottom = (int)(s_Hw.padding.bottom + 0.5f);

    if (left < 0) left = 0;
    if (right < 0) right = 0;
    if (top < 0) top = 0;
    if (bottom < 0) bottom = 0;

    srcW = texW - left - right;
    srcH = texH - top - bottom;
    if (srcW <= 0 || srcH <= 0 ||
        left + srcW > texW || top + srcH > texH)
        return false;

    if (srcW != 320 && srcW != 256)
        return false;

    texTBP  = auroraOutBaseTBP + PD_GS_T8_TBP_OFFSET;
    clutTBP = auroraOutBaseTBP + PD_GS_CLUT_TBP_OFFSET;

    /* AURORA_MD_ACTIVE_ROWS_DMA_V1
     * Upload only active MD rows, not the whole 328x256 backing store. */
    GPPrimUploadTexture(
        (int)texTBP, PD_GS_T8_TBW, 0, 0, GS_PSM_T8,
        ((Uint8 *)s_CoreTexture.Mem) + top * texW,
        texW, srcH);

    /* Avoid a second GS upload when PicoDrive's 512-byte CLUT is identical. */
    if (!s_DirectClutValid ||
        memcmp(s_DirectLastClut, s_CoreTexture.Clut,
               sizeof(s_DirectLastClut)) != 0)
    {
        GPPrimUploadTexture((int)clutTBP, 64, 0, 0,
                            GS_PSM_CT16, s_CoreTexture.Clut, 16, 16);
        memcpy(s_DirectLastClut, s_CoreTexture.Clut,
               sizeof(s_DirectLastClut));
        s_DirectClutValid = true;
    }

    GPPrimSetTex(texTBP, PD_GS_T8_TBW, 9, 8, GS_PSM_T8,
                 clutTBP, 64, GS_PSM_CT16, 0);

    fbW = (int)(256.0f * GPPrimGetScaleX() + 0.5f);
    fbH = (int)(240.0f * GPPrimGetScaleY() + 0.5f);
    if (fbW <= 0 || fbH <= 0)
        return false;

    /* AURORA_MD_H32_FULL_SCANLINE_V2
     * H40 and H32 both represent a full-width Mega Drive scanline.
     * H32 has 256 active samples because the VDP uses a lower dot clock;
     * those pixels must therefore occupy the same physical display width
     * as H40's 320 samples. Keep NEAREST sampling: this changes geometry,
     * not filtering, and introduces no blur. */
    if (srcW == 320 || srcW == 256)
        dstW = fbW;
    else
        dstW = (fbW * srcW + 160) / 320;

    dstH = (fbH * srcH + 120) / 240;
    dstX = (fbW - dstW) / 2;
    dstY = (fbH - dstH) / 2;

    /* VDP backdrop/border = CRAM index R7[5:0], using the exact CLUT
     * that PicoDrive prepared for the PS2 GS. */
    backdrop = pdScaleDirectColor(
        pdPs2ClutBackdropRGBA((unsigned)Pico.video.reg[7] & 0x3fU),
        intensity);

    GPPrimRect(0, 0, backdrop,
               256U << 4, 240U << 4, backdrop,
               0, 0);

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    mod = (unsigned)(128.0f * intensity + 0.5f);
    if (mod > 128U) mod = 128U;
    modColor = 0x80000000U | (mod << 16) | (mod << 8) | mod;

    GPPrimTexRectAbs(
        (Uint32)(dstX << 4), (Uint32)(dstY << 4),
        (Uint32)(left << 4), 0,
        (Uint32)((dstX + dstW) << 4), (Uint32)((dstY + dstH) << 4),
        (Uint32)((left + srcW) << 4), (Uint32)(srcH << 4),
        0, modColor, 0);

    return true;
}

int PicoDriveBridge_GetStateSize(void)
{
    if (!s_GameLoaded)
        return 0;
    size_t n = retro_serialize_size();
    return n > 0x7fffffffU ? 0 : (int)n;
}

int PicoDriveBridge_SaveState(void *pData, int nBytes)
{
    if (!s_GameLoaded || !pData || nBytes <= 0)
        return 0;
    size_t need = retro_serialize_size();
    if (!need || need > (size_t)nBytes)
        return 0;
    return retro_serialize(pData, need) ? (int)need : 0;
}

bool PicoDriveBridge_LoadState(const void *pData, int nBytes)
{
    bool ok;
    if (!s_GameLoaded || !pData || nBytes <= 0)
        return false;

    ok = retro_unserialize(pData, (size_t)nBytes);
    if (ok)
    {
        /* AURORA_MD_STATE_CLUT_INVALIDATE_V1 */
        s_DirectClutValid = false;

        /* Pending PCM belongs to the pre-load timeline. */
        pdAudioTailReset();
    }
    return ok;
}

int PicoDriveBridge_GetSRAMBytes(void)
{
    if (!s_GameLoaded || !pdHasFrontendSaveMemory())
        return 0;
    return s_SramBytes;
}

Uint8 *PicoDriveBridge_GetSRAMData(void)
{
    return PicoDriveBridge_GetSRAMBytes() > 0 ? s_pSramData : NULL;
}

unsigned PicoDriveBridge_GetSampleRate(void)
{
    return 32000;
}
