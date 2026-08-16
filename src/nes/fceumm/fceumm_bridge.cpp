/*
 * fceumm_bridge.cpp
 *
 * Minimal libretro frontend used to embed FCEUmm inside SNESticleRevive.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#include "fceumm_symbol_prefix.h"
#include "libretro.h"

#include "fceumm_bridge.h"
#include "rendersurface.h"
#include "mixbuffer.h"
#include "snio.h"

static Bool s_bInitialized = FALSE;
static Bool s_bGameLoaded  = FALSE;

static Emu::SysInputT *s_pInput  = NULL;
static CRenderSurface *s_pTarget = NULL;
static CMixBuffer     *s_pMix    = NULL;

static struct retro_game_info_ext s_GameInfoExt;
static Bool s_bGameInfoExtValid = FALSE;
static Bool s_bTurboPhase = FALSE;

static void RETRO_CALLCONV
FceummLog(enum retro_log_level level, const char *fmt, ...)
{
    (void)level;

    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static bool FceummEnvironment(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            if (!data)
                return false;

            enum retro_pixel_format fmt = *(enum retro_pixel_format *)data;
            return fmt == RETRO_PIXEL_FORMAT_RGB565;
        }

        case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
        {
            if (!data || !s_bGameInfoExtValid)
                return false;

            *(const struct retro_game_info_ext **)data = &s_GameInfoExt;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            if (!data)
                return false;

            struct retro_log_callback *cb = (struct retro_log_callback *)data;
            cb->log = FceummLog;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            if (!data)
                return false;

            struct retro_variable *var = (struct retro_variable *)data;
            if (!var->key)
                return false;

            if (strcmp(var->key, "fceumm_sndrate_hint") == 0)
            {
                var->value = "32KHz";
                return true;
            }

            if (strcmp(var->key, "fceumm_ramstate") == 0)
            {
                var->value = "random";
                return true;
            }

            return false;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        {
            if (!data)
                return false;
            *(bool *)data = false;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        {
            if (!data)
                return false;
            *(bool *)data = false;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
            return true;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            return false;

        case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
            return false;

        default:
            return false;
    }
}

static void FceummVideoRefresh(const void *data,
                               unsigned width,
                               unsigned height,
                               size_t pitch)
{

    if (!data || !s_pTarget)
        return;

    if (data == RETRO_HW_FRAME_BUFFER_VALID)
        return;

    Uint32 targetWidth  = s_pTarget->GetWidth();
    Uint32 targetHeight = s_pTarget->GetHeight();
    Uint32 copyWidth = width < targetWidth ? (Uint32)width : targetWidth;
    Uint32 copyHeight = height < targetHeight ? (Uint32)height : targetHeight;

    for (Uint32 y = 0; y < copyHeight; y++)
    {
        const Uint16 *src =
            (const Uint16 *)((const Uint8 *)data + ((size_t)y * pitch));
        Uint32 *dst = (Uint32 *)s_pTarget->GetLinePtr((Int32)y);

        if (!dst)
            continue;

        for (Uint32 x = 0; x < copyWidth; x++)
        {
            Uint16 p = src[x];
            Uint32 r5 = (p >> 11) & 0x1f;
            Uint32 g6 = (p >> 5)  & 0x3f;
            Uint32 b5 =  p        & 0x1f;
            Uint32 r = (r5 << 3) | (r5 >> 2);
            Uint32 g = (g6 << 2) | (g6 >> 4);
            Uint32 b = (b5 << 3) | (b5 >> 2);

            dst[x] = 0xff000000u | (b << 16) | (g << 8) | r;
        }

        if (copyWidth < targetWidth)
        {
            memset(((Uint8 *)dst) + copyWidth * 4,
                   0,
                   (targetWidth - copyWidth) * 4);
        }
    }

    for (Uint32 y = copyHeight; y < targetHeight; y++)
    {
        Uint8 *dst = s_pTarget->GetLinePtr((Int32)y);
        if (dst)
            memset(dst, 0, targetWidth * 4);
    }
}

static void FceummAudioSample(int16_t left, int16_t right)
{
    if (!s_pMix)
        return;

    Int16 l[1];
    Int16 r[1];
    l[0] = (Int16)left;
    r[0] = (Int16)right;
    s_pMix->OutputSamplesStereo(l, r, 1);
}

static size_t FceummAudioBatch(const int16_t *data, size_t frames)
{
    if (!data || !s_pMix)
        return frames;

    enum { CHUNK = 1024 };
    static Int16 left[CHUNK];
    static Int16 right[CHUNK];

    size_t done = 0;
    while (done < frames)
    {
        size_t count = frames - done;
        if (count > CHUNK)
            count = CHUNK;

        for (size_t i = 0; i < count; i++)
        {
            left[i]  = (Int16)data[(done + i) * 2 + 0];
            right[i] = (Int16)data[(done + i) * 2 + 1];
        }

        s_pMix->OutputSamplesStereo(left, right, (Int32)count);
        done += count;
    }

    return frames;
}

static void FceummInputPoll(void)
{
    s_bTurboPhase = !s_bTurboPhase;
}

static int16_t FceummInputState(unsigned port,
                                unsigned device,
                                unsigned index,
                                unsigned id)
{
    if (!s_pInput || port >= 2 || index != 0)
        return 0;

    if ((device & 0xffu) != RETRO_DEVICE_JOYPAD)
        return 0;

    Uint16 pad = s_pInput->uPad[port];
    if (pad == EMUSYS_DEVICE_DISCONNECTED)
        return 0;

    switch (id)
    {
        case RETRO_DEVICE_ID_JOYPAD_A:
            return (pad & SNESIO_JOY_B) ||
                   (s_bTurboPhase && (pad & SNESIO_JOY_A));

        case RETRO_DEVICE_ID_JOYPAD_B:
            return (pad & SNESIO_JOY_Y) ||
                   (s_bTurboPhase && (pad & SNESIO_JOY_X));

        case RETRO_DEVICE_ID_JOYPAD_SELECT:
            return (pad & SNESIO_JOY_SELECT) != 0;
        case RETRO_DEVICE_ID_JOYPAD_START:
            return (pad & SNESIO_JOY_START) != 0;
        case RETRO_DEVICE_ID_JOYPAD_UP:
            return (pad & SNESIO_JOY_UP) != 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:
            return (pad & SNESIO_JOY_DOWN) != 0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:
            return (pad & SNESIO_JOY_LEFT) != 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:
            return (pad & SNESIO_JOY_RIGHT) != 0;
        default:
            return 0;
    }
}

Bool FceummBridge_Init(void)
{
    if (s_bInitialized)
        return TRUE;

    retro_set_environment(FceummEnvironment);
    retro_set_video_refresh(FceummVideoRefresh);
    retro_set_audio_sample(FceummAudioSample);
    retro_set_audio_sample_batch(FceummAudioBatch);
    retro_set_input_poll(FceummInputPoll);
    retro_set_input_state(FceummInputState);
    retro_init();

    s_bInitialized = TRUE;
    printf("[FCEUmm] bridge initialized\n");
    return TRUE;
}

void FceummBridge_Shutdown(void)
{
    if (!s_bInitialized)
        return;

    if (s_bGameLoaded)
    {
        retro_unload_game();
        s_bGameLoaded = FALSE;
    }

    retro_deinit();
    s_bInitialized = FALSE;
    s_pInput  = NULL;
    s_pTarget = NULL;
    s_pMix    = NULL;

    printf("[FCEUmm] bridge shutdown\n");
}

Bool FceummBridge_LoadGame(const void *pData,
                           Uint32 nBytes,
                           const char *pszName)
{
    (void)pszName;

    if (!pData || nBytes < 16)
        return FALSE;

    if (!s_bInitialized && !FceummBridge_Init())
        return FALSE;

    if (s_bGameLoaded)
    {
        retro_unload_game();
        s_bGameLoaded = FALSE;
    }

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    memset(&s_GameInfoExt, 0, sizeof(s_GameInfoExt));

    s_GameInfoExt.full_path       = "memory.nes";
    s_GameInfoExt.archive_path    = NULL;
    s_GameInfoExt.archive_file    = NULL;
    s_GameInfoExt.dir             = ".";
    s_GameInfoExt.name            = "memory";
    s_GameInfoExt.ext             = "nes";
    s_GameInfoExt.meta            = NULL;
    s_GameInfoExt.data            = pData;
    s_GameInfoExt.size            = (size_t)nBytes;
    s_GameInfoExt.file_in_archive = true;
    s_GameInfoExt.persistent_data = true;

    info.path = "memory.nes";
    info.data = pData;
    info.size = (size_t)nBytes;
    info.meta = NULL;

    s_bGameInfoExtValid = TRUE;
    bool ok = retro_load_game(&info);
    s_bGameInfoExtValid = FALSE;

    if (!ok)
    {
        printf("[FCEUmm] retro_load_game failed\n");
        return FALSE;
    }

    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);

    s_bGameLoaded = TRUE;
    printf("[FCEUmm] game loaded: %u bytes\n", (unsigned)nBytes);
    return TRUE;
}

void FceummBridge_UnloadGame(void)
{
    if (!s_bGameLoaded)
        return;

    retro_unload_game();
    s_bGameLoaded = FALSE;
}

void FceummBridge_Reset(void)
{
    if (s_bGameLoaded)
        retro_reset();
}

void FceummBridge_RunFrame(Emu::SysInputT *pInput,
                           CRenderSurface *pTarget,
                           CMixBuffer *pMixBuf)
{
    if (!s_bGameLoaded)
        return;

    s_pInput  = pInput;
    s_pTarget = pTarget;
    s_pMix    = pMixBuf;

    retro_run();

    if (s_pMix)
        s_pMix->Flush();

    s_pInput  = NULL;
    s_pTarget = NULL;
    s_pMix    = NULL;
}

Int32 FceummBridge_GetStateSize(void)
{
    if (!s_bGameLoaded)
        return 0;

    size_t bytes = retro_serialize_size();
    if (bytes > 0x7fffffffUL)
        return 0;

    return (Int32)bytes;
}

Bool FceummBridge_SaveState(void *pState, Int32 nStateBytes)
{
    if (!s_bGameLoaded || !pState || nStateBytes <= 0)
        return FALSE;

    size_t required = retro_serialize_size();
    if ((size_t)nStateBytes < required)
        return FALSE;

    return retro_serialize(pState, required) ? TRUE : FALSE;
}

Bool FceummBridge_LoadState(const void *pState, Int32 nStateBytes)
{
    if (!s_bGameLoaded || !pState || nStateBytes <= 0)
        return FALSE;

    return retro_unserialize(pState, (size_t)nStateBytes) ? TRUE : FALSE;
}

Int32 FceummBridge_GetSRAMBytes(void)
{
    if (!s_bGameLoaded)
        return 0;

    size_t bytes = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (bytes > 0x7fffffffUL)
        return 0;

    return (Int32)bytes;
}

Uint8 *FceummBridge_GetSRAMData(void)
{
    if (!s_bGameLoaded)
        return NULL;

    return (Uint8 *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
}

Uint32 FceummBridge_GetSampleRate(void)
{
    return 32000;
}
