/* AURORA_FCEUMM_FDS_V0_6_RUNTIME
 * Runtime bridge for the pinned, FDS-only FCEUmm libretro archive.
 *
 * v0_6 adds two host-only services while leaving FCEUmm's emulated state
 * authoritative:
 *   - libretro serialize/unserialize for Aurora save states;
 *   - non-blocking opposite-side swap: eject now, emulate 60 FDS frames,
 *     select A<->B of the SAME disk, then insert.
 *
 * The core itself serializes SelectDisk and InDisk. The small host envelope
 * additionally serializes the pending 60-frame swap countdown so a state made
 * while the disk is ejected resumes the same virtual drive operation.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "nes/fceumm/fceumm_fds_bridge.h"
#include "types.h"
#include "emuinput.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"
#include "audmixbuffer.h"
#include "snio.h"

extern AudMixBuffer *_AudMix;

extern "C" {
#include "libretro.h"
}

extern "C" {
unsigned FDS_retro_api_version(void);
void FDS_retro_set_environment(retro_environment_t);
void FDS_retro_set_video_refresh(retro_video_refresh_t);
void FDS_retro_set_audio_sample(retro_audio_sample_t);
void FDS_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void FDS_retro_set_input_poll(retro_input_poll_t);
void FDS_retro_set_input_state(retro_input_state_t);
void FDS_retro_init(void);
void FDS_retro_deinit(void);
bool FDS_retro_load_game(const struct retro_game_info *);
void FDS_retro_unload_game(void);
void FDS_retro_reset(void);
void FDS_retro_run(void);
size_t FDS_retro_serialize_size(void);
bool FDS_retro_serialize(void *, size_t);
bool FDS_retro_unserialize(const void *, size_t);
void FDS_retro_get_system_av_info(struct retro_system_av_info *);
void FDS_aurora_fds_set_system_directory(const char *);
void FDS_aurora_fds_set_skip_video(int);
void FDS_FCEU_FDSEject(void);
void FDS_FCEU_FDSSelect(void);
void FDS_FCEU_FDSInsert(int);
}

static bool s_Initialized = false;
static bool s_GameLoaded = false;
static bool s_SkipVideoNext = false;
static Emu::SysInputT *s_pInput = NULL;
static CRenderSurface *s_pTarget = NULL;
static CMixBuffer *s_pMix = NULL;
static unsigned s_SampleRate = 32050;
static int s_StateBytes = 0;

/* AURORA_FCEUMM_FDS_V0_6_DRIVE_STATE */
static unsigned s_TotalSides = 0;
static unsigned s_SelectedSide = 0;
static bool s_DiskInserted = false;
static unsigned s_SwapFramesRemaining = 0;
static unsigned s_SwapTargetSide = 0;
enum { FDS_SIDE_SWAP_DELAY_FRAMES = 60 };

enum { FDS_AUDIO_CHUNK = 1024 };
static Int16 s_AudioL[FDS_AUDIO_CHUNK];
static Int16 s_AudioR[FDS_AUDIO_CHUNK];
static Uint32 s_R5[32], s_G5[32], s_B5[32];
static bool s_ColorTablesReady = false;

static void fdsResetDriveHostState(void)
{
    s_TotalSides = 0;
    s_SelectedSide = 0;
    s_DiskInserted = false;
    s_SwapFramesRemaining = 0;
    s_SwapTargetSide = 0;
}

static void fdsInitColorTables(void)
{
    if (s_ColorTablesReady) return;
    for (unsigned i = 0; i < 32; ++i)
    {
        Uint32 v = (i << 3) | (i >> 2);
        s_R5[i] = v;
        s_G5[i] = v << 8;
        s_B5[i] = v << 16;
    }
    s_ColorTablesReady = true;
}

static bool fdsEnvironment(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            return true;
        default:
            return false;
    }
}

static void fdsVideo(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || !s_pTarget || !width || !height || pitch < width * 2)
        return;

    PixelFormatT *fmt = s_pTarget->GetFormat();
    if (!fmt || fmt->uBitDepth != 32)
        return;

    fdsInitColorTables();

    unsigned w = width;
    unsigned h = height;
    if (w > s_pTarget->GetWidth()) w = s_pTarget->GetWidth();
    if (h > s_pTarget->GetHeight()) h = s_pTarget->GetHeight();
    if (w > 256) w = 256;
    if (h > 240) h = 240;

    for (unsigned y = 0; y < h; ++y)
    {
        const Uint16 *src = (const Uint16 *)((const Uint8 *)data + (size_t)y * pitch);
        Uint32 *dst = (Uint32 *)s_pTarget->GetLinePtr((Int32)y);
        if (!dst) continue;
        for (unsigned x = 0; x < w; ++x)
        {
            Uint16 c = src[x];
            dst[x] = 0xff000000u |
                     s_R5[(c >> 10) & 31u] |
                     s_G5[(c >> 5) & 31u] |
                     s_B5[c & 31u];
        }
    }
}

static void fdsAudioSample(int16_t left, int16_t right)
{
    if (!s_pMix) return;
    Int16 l = (Int16)left, r = (Int16)right;
    s_pMix->OutputSamplesStereo(&l, &r, 1);
}

static size_t fdsAudioBatch(const int16_t *data, size_t frames)
{
    if (!data || !frames || !s_pMix)
        return frames;

    if (_AudMix && s_pMix == _AudMix && frames <= 0x7fffffffU)
    {
        size_t pos = 0;
        while (pos < frames)
        {
            size_t n = frames - pos;
            if (n > FDS_AUDIO_CHUNK) n = FDS_AUDIO_CHUNK;
            _AudMix->OutputLibretroInterleaved(
                (const Int16 *)(data + pos * 2), (Int32)n);
            pos += n;
        }
        return frames;
    }

    size_t pos = 0;
    while (pos < frames)
    {
        size_t n = frames - pos;
        if (n > FDS_AUDIO_CHUNK) n = FDS_AUDIO_CHUNK;
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

static void fdsInputPoll(void) {}

static bool fdsPadHas(unsigned port, Uint16 bit)
{
    if (!s_pInput || port >= 2)
        return false;
    Uint16 pad = s_pInput->uPad[port];
    return pad != EMUSYS_DEVICE_DISCONNECTED && (pad & bit) != 0;
}

static int16_t fdsInputState(unsigned port, unsigned device,
                             unsigned index, unsigned id)
{
    (void)index;
    if (device != RETRO_DEVICE_JOYPAD || port >= 2)
        return 0;

    switch (id)
    {
        case RETRO_DEVICE_ID_JOYPAD_A:      return fdsPadHas(port, SNESIO_JOY_B) ? 1 : 0; /* Cross */
        case RETRO_DEVICE_ID_JOYPAD_B:      return fdsPadHas(port, SNESIO_JOY_Y) ? 1 : 0; /* Square */
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return fdsPadHas(port, SNESIO_JOY_SELECT) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_START:  return fdsPadHas(port, SNESIO_JOY_START) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_UP:     return fdsPadHas(port, SNESIO_JOY_UP) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:   return fdsPadHas(port, SNESIO_JOY_DOWN) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:   return fdsPadHas(port, SNESIO_JOY_LEFT) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return fdsPadHas(port, SNESIO_JOY_RIGHT) ? 1 : 0;
        default: return 0;
    }
}

bool FceummFdsBridge_Init(void)
{
    if (s_Initialized)
        return true;

    FDS_retro_set_environment(fdsEnvironment);
    FDS_retro_set_video_refresh(fdsVideo);
    FDS_retro_set_audio_sample(fdsAudioSample);
    FDS_retro_set_audio_sample_batch(fdsAudioBatch);
    FDS_retro_set_input_poll(fdsInputPoll);
    FDS_retro_set_input_state(fdsInputState);

    if (FDS_retro_api_version() != RETRO_API_VERSION)
    {
        printf("[FCEUmm/FDS] libretro API mismatch\n");
        return false;
    }

    FDS_retro_init();
    s_Initialized = true;
    return true;
}

void FceummFdsBridge_UnloadGame(void)
{
    if (s_Initialized && s_GameLoaded)
        FDS_retro_unload_game();
    s_GameLoaded = false;
    s_SkipVideoNext = false;
    s_StateBytes = 0;
    s_pInput = NULL;
    s_pTarget = NULL;
    s_pMix = NULL;
    fdsResetDriveHostState();
}

void FceummFdsBridge_Shutdown(void)
{
    if (!s_Initialized)
        return;
    FceummFdsBridge_UnloadGame();
    FDS_retro_deinit();
    s_Initialized = false;
}

bool FceummFdsBridge_LoadDisk(const char *path, const char *systemPath,
                              unsigned totalSides)
{
    if (!path || !*path || !systemPath || !*systemPath ||
        totalSides < 1 || totalSides > 8)
        return false;

    if (s_GameLoaded)
        FceummFdsBridge_UnloadGame();

    FDS_aurora_fds_set_system_directory(systemPath);
    if (!FceummFdsBridge_Init())
        return false;

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = path;
    info.data = NULL;
    info.size = 0;
    info.meta = NULL;

    if (!FDS_retro_load_game(&info))
    {
        printf("[FCEUmm/FDS] load failed: %s (SYSTEM=%s)\n", path, systemPath);
        return false;
    }

    s_GameLoaded = true;
    s_StateBytes = 0;
    s_TotalSides = totalSides;
    s_SelectedSide = 0;
    s_DiskInserted = true;
    s_SwapFramesRemaining = 0;
    s_SwapTargetSide = 0;

    struct retro_system_av_info av;
    memset(&av, 0, sizeof(av));
    FDS_retro_get_system_av_info(&av);
    if (av.timing.sample_rate >= 8000.0 && av.timing.sample_rate <= 96000.0)
        s_SampleRate = (unsigned)(av.timing.sample_rate + 0.5);

    printf("[FCEUmm/FDS] LOAD OK: %s; sides=%u; audio=%u Hz; SYSTEM=%s\n",
           path, s_TotalSides, s_SampleRate, systemPath);
    return true;
}

void FceummFdsBridge_Reset(void)
{
    if (s_GameLoaded) FDS_retro_reset();
}

void FceummFdsBridge_SoftReset(void)
{
    if (s_GameLoaded) FDS_retro_reset();
}

void FceummFdsBridge_SetSkipVideo(bool skip)
{
    s_SkipVideoNext = skip;
}

static bool fdsAdvanceSelectionTo(unsigned target)
{
    unsigned simulated;
    unsigned steps;

    if (!s_GameLoaded || s_DiskInserted || !s_TotalSides ||
        target >= s_TotalSides)
        return false;

    simulated = s_SelectedSide;
    steps = 0;
    while (simulated != target && steps < 16)
    {
        /* Match the pinned FCEUmm selector exactly. Its historical &3 keeps
         * the selectable drive positions in the four ordinary A/B slots. */
        simulated = ((simulated + 1u) % s_TotalSides) & 3u;
        ++steps;
    }
    if (simulated != target)
        return false;

    while (steps--)
        FDS_FCEU_FDSSelect();
    s_SelectedSide = target;
    return true;
}

bool FceummFdsBridge_BeginSideSwap(void)
{
    unsigned target;

    if (!s_GameLoaded || !s_DiskInserted || s_SwapFramesRemaining ||
        s_TotalSides < 2)
        return false;

    target = s_SelectedSide ^ 1u;
    if (target >= s_TotalSides)
        return false;

    FDS_FCEU_FDSEject();
    s_DiskInserted = false;
    s_SwapTargetSide = target;
    s_SwapFramesRemaining = FDS_SIDE_SWAP_DELAY_FRAMES;
    return true;
}

bool FceummFdsBridge_IsSideSwapPending(void)
{
    return s_GameLoaded && s_SwapFramesRemaining != 0;
}

unsigned FceummFdsBridge_GetSelectedSide(void)
{
    return s_SelectedSide;
}

bool FceummFdsBridge_IsDiskInserted(void)
{
    return s_GameLoaded && s_DiskInserted;
}

void FceummFdsBridge_GetDriveState(unsigned *selectedSide,
                                   bool *inserted,
                                   unsigned *swapFramesRemaining,
                                   unsigned *swapTargetSide)
{
    if (selectedSide) *selectedSide = s_SelectedSide;
    if (inserted) *inserted = s_DiskInserted;
    if (swapFramesRemaining) *swapFramesRemaining = s_SwapFramesRemaining;
    if (swapTargetSide) *swapTargetSide = s_SwapTargetSide;
}

bool FceummFdsBridge_SetDriveState(unsigned selectedSide,
                                   bool inserted,
                                   unsigned swapFramesRemaining,
                                   unsigned swapTargetSide)
{
    if (!s_GameLoaded || !s_TotalSides || selectedSide >= s_TotalSides ||
        swapTargetSide >= s_TotalSides ||
        swapFramesRemaining > FDS_SIDE_SWAP_DELAY_FRAMES ||
        (swapFramesRemaining && inserted))
        return false;

    s_SelectedSide = selectedSide;
    s_DiskInserted = inserted;
    s_SwapFramesRemaining = swapFramesRemaining;
    s_SwapTargetSide = swapTargetSide;
    return true;
}

int FceummFdsBridge_GetStateSize(void)
{
    size_t n;
    if (!s_GameLoaded)
        return 0;
    if (s_StateBytes > 0)
        return s_StateBytes;
    n = FDS_retro_serialize_size();
    if (!n || n > (size_t)INT_MAX)
        return 0;
    s_StateBytes = (int)n;
    return s_StateBytes;
}

int FceummFdsBridge_SaveState(void *data, int bytes)
{
    int n = FceummFdsBridge_GetStateSize();
    if (!data || n <= 0 || bytes < n)
        return 0;
    return FDS_retro_serialize(data, (size_t)n) ? n : 0;
}

bool FceummFdsBridge_LoadState(const void *data, int bytes)
{
    int n = FceummFdsBridge_GetStateSize();
    if (!data || n <= 0 || bytes != n)
        return false;
    s_SwapFramesRemaining = 0;
    s_SwapTargetSide = s_SelectedSide;
    return FDS_retro_unserialize(data, (size_t)n);
}

void FceummFdsBridge_RunFrame(Emu::SysInputT *input,
                              CRenderSurface *target,
                              CMixBuffer *mix)
{
    if (!s_GameLoaded)
        return;

    const bool skipVideo = s_SkipVideoNext;
    s_SkipVideoNext = false;
    s_pInput = input;
    s_pTarget = target;
    s_pMix = mix;

    FDS_aurora_fds_set_skip_video(skipVideo ? 1 : 0);
    FDS_retro_run();

    if (s_SwapFramesRemaining)
    {
        --s_SwapFramesRemaining;
        if (!s_SwapFramesRemaining)
        {
            if (!fdsAdvanceSelectionTo(s_SwapTargetSide))
            {
                /* Selection should be reachable for every valid A/B pair.
                 * Fail safe by reinserting the original selected side. */
                s_SwapTargetSide = s_SelectedSide;
            }
            FDS_FCEU_FDSInsert(0);
            s_DiskInserted = true;
        }
    }

    if (s_pMix)
        s_pMix->Flush();
    s_pMix = NULL;
    s_pTarget = NULL;
    s_pInput = NULL;
}

unsigned FceummFdsBridge_GetSampleRate(void)
{
    return s_SampleRate;
}
