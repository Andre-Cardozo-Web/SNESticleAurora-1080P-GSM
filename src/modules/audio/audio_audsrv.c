/*
    ---------------------------------------------------------------------
    audio_audsrv.c - audsrv backend for the Aud_* EE-side audio API.
    ---------------------------------------------------------------------

    The original SjPCM library (Nick Van Veen "Sjeep", 2002) talked to a
    custom IOP-side IRX (SJPCM2.IRX) over SIF RPC. That precompiled IRX
    pre-dates a number of changes in modern PS2SDK / IOP rom builds and
    its RPC server either fails to register or hangs SifBindRpc on
    emulators (NetherSX2/PCSX2 Qt) and stripped-down PS2 setups, which
    is why the project had to disable the embedded SJPCM2.IRX entirely
    (see src/platform/ps2/system/embedded_irx.cpp). Result: silent audio.

    The PS2DEV team replaced SjPCM with **audsrv** back in 2005:
        https://forums.ps2dev.org/viewtopic.php?t=1500
            "Audsrv comes to replace sjpcm, and provide an easy and
             stable way to utilize the SPU2."
    audsrv.irx ships with every modern PS2SDK at
        $(PS2SDK)/iop/irx/audsrv.irx
    and is the standard audio service used by SDL, ScummVM, OPL, etc.

    This file keeps the Aud_* API surface that AudMixBuffer and
    mainloop_iop.cpp call into, but the backend is now audsrv. Sample
    format is fixed at the SPU2's native 48000 Hz / 16 bit / stereo,
    which matches what AudMixBuffer already converts the SNES output
    to (see src/common/render/sjpcmbuffer.cpp). Left/right separated
    channels are interleaved into a stereo buffer before being passed
    to audsrv_play_audio().

    audsrv exposes audsrv_queued() / audsrv_available() (in bytes), so
    Aud_Buffered() / Aud_Available() map directly without needing
    any time-based estimation.
*/

#include <tamtypes.h>
#include <kernel.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <audsrv.h>
#include <sio.h>

#include "audio.h"

/* ScrPrintf goes to the on-screen log (and stays there during the
   splash). Plain printf on EE-side never reaches the emulator console
   in this project, but ScrPrintf does survive long enough to be seen
   in a screenshot of the boot screen. Forward-declare it here so we
   don't have to drag the C++ mainloop_ui.h into this C file. */
extern void ScrPrintf(const char *pFormat, ...);

/* Diagnostic printf helper for this project.

   Plain printf() on the EE never seems to reach NetherSX2 / PCSX2's
   emulator log file in this codebase (some piece of the libc->SIF->IOP
   stdout wiring is missing). What *does* reach the emulator log is the
   EE SIO TX FIFO at 0x1000f180: PCSX2 captures bytes written to it and
   emits them on the EE_SIO log channel, which lands in the same console
   /log file as the IOP "loadmodule:" / "audsrv_adpcm_init()" lines.

   We therefore route diagnostics through sio_putsn() (writes to EE SIO
   TX FIFO byte-by-byte) and also mirror them to ScrPrintf so the user
   sees them on the on-screen splash log. sio_init() is called lazily
   on first use with the standard 38400 8N1 setting.

   Tag: each line is prefixed with "[snes-aud] " so the user can grep
   the log file. */
static int   _sio_inited = 0;
static char  _dlog_buf[256];

/* Non-static so other translation units can extern it for one-off
   audio-path tracing. Mirror of the local prototype:
       extern void DLog(const char *fmt, ...);
   See mainloop_process.cpp / sjpcmbuffer.cpp where this is called
   via that extern declaration. */
void DLog(const char *fmt, ...)
{
    va_list ap;
    int n;

    if (!_sio_inited)
    {
        sio_init(38400, 0, 0, 0, 0);
        _sio_inited = 1;
    }

    va_start(ap, fmt);
    n = vsnprintf(_dlog_buf, sizeof(_dlog_buf) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(_dlog_buf) - 2) n = sizeof(_dlog_buf) - 2;

    /* Make sure the line ends with \n so the emulator log flushes it. */
    if (n == 0 || _dlog_buf[n - 1] != '\n')
    {
        _dlog_buf[n++] = '\n';
        _dlog_buf[n]   = '\0';
    }

    sio_putsn(_dlog_buf);
}


/*
    Output is fixed 48000 Hz / 16 bit / stereo (SPU2 native).
    AudMixBuffer already up-samples 32000 Hz SNES audio to 48000 Hz
    before calling Aud_Enqueue, so audsrv runs without any internal
    upsampling.
*/
#define AUD_AUDSRV_FREQ      48000
#define AUD_AUDSRV_BITS      16
#define AUD_AUDSRV_CHANNELS  2
#define AUD_BYTES_PER_SAMPLE (AUD_AUDSRV_CHANNELS * (AUD_AUDSRV_BITS / 8)) /* 4 */


/*
    Static interleave scratch sized for the worst case the engine will
    ever pass into Aud_Enqueue. AUDMIXBUFFER_MAXENQUEUE in
    sjpcmbuffer.h is currently (800 * 5) = 4000 samples per channel.
    Round up to 4096 for alignment headroom.
*/
#define AUD_MAX_ENQUEUE_SAMPLES 4096
static short _interleave_buf[AUD_MAX_ENQUEUE_SAMPLES * AUD_AUDSRV_CHANNELS]
    __attribute__((aligned(64)));

/* AURORA_GAMEPLAY_HEADROOM_V1
 * Dedicated BSS-zero buffer: never clobber the real interleave scratch.
 * 2048 frames = 8192 bytes ~= 42.7 ms at 48 kHz stereo. */
#define AUD_GAMEPLAY_HEADROOM_SAMPLES 2048
static short _gameplay_silence[
    AUD_GAMEPLAY_HEADROOM_SAMPLES * AUD_AUDSRV_CHANNELS]
    __attribute__((aligned(64)));

/* AURORA_AUDIO_ASYNC_FIFO_V2
 *
 * The audsrv port called Aud_EnqueueAsync() but still blocked in
 * audsrv_wait_audio(). Stage gameplay PCM here instead.
 *
 * AURORA_AUDIO_OVERFLOW_NOBLOCK_V4_1
 * 65536 stereo frames ~= 1365 ms @ 48 kHz, 256 KiB total.
 * Normal occupancy should still stay around one frame (~800 samples).
 * The extra 64 KiB is only shock-absorber headroom for rare host/IOP stalls.
 */
#define AUD_ASYNC_FIFO_SAMPLES 65536
static short _async_left[AUD_ASYNC_FIFO_SAMPLES]
    __attribute__((aligned(64)));
static short _async_right[AUD_ASYNC_FIFO_SAMPLES]
    __attribute__((aligned(64)));
static int aud_async_head = 0;
static int aud_async_count = 0;

/* AURORA_AUDIO_RPC_JITTER_V3
 *
 * audsrv_available() is itself a synchronous SIF RPC. Cache only a LOWER
 * BOUND of free ring space: after a probe subtract every frame sent, but
 * never add frames the IOP consumed meanwhile. The cache may become
 * pessimistic, never optimistic.
 */
#define AUD_ASYNC_RING_MARGIN_SAMPLES  32
#define AUD_ASYNC_MAX_BURST_SAMPLES  2048
#define AUD_ASYNC_AVAIL_PROBE_BUDGET     4
static int aud_async_cached_avail = -1;
static int aud_async_probe_budget = 0;


/* AURORA_V83_AUDIO_PACKED_STEREO
 * The EE build is little-endian and the destination is 64-byte aligned.
 * One 32-bit store writes exactly the same L-low/L-high/R-low/R-high bytes
 * as the two 16-bit assignments it replaces.  This changes no PCM sample,
 * queue size, scheduler decision or audsrv call. */
static inline void Aud_StoreStereoFrame(int index, short left, short right)
{
    ((u32 *)(void *)_interleave_buf)[index] =
        (u32)(u16)left | ((u32)(u16)right << 16);
}


static int sjpcm_inited = 0;
static int sjpcm_playing = 0;

/* AURORA_V7_AUDSRV_GUARD
 * PS2SDK audsrv uses a pointer-only ring buffer and cannot distinguish a
 * completely empty queue from a completely full one.  Keep generous valid
 * headroom and apply an extremely small, bounded queue servo only when the
 * stream drifts well outside its normal band.  Nominal frames are untouched. */
#define AUD_WAKE_PRIME_SAMPLES       768
#define AUD_GUARD_WARMUP_ENQUEUES    120
#define AUD_GUARD_POLL_ENQUEUES      16
#define AUD_GUARD_LOW_SAMPLES        2560
#define AUD_GUARD_HIGH_SAMPLES       3840
#define AUD_GUARD_ADJUST_SAMPLES     2
static unsigned int aud_guard_enqueues = 0;

/* AURORA_COMPAT_AUDIO_STATE
 * 0/0 is exactly the V8.5 path. */
static int aud_compat_small_chunks = 0;
static int aud_compat_deep_queue = 0;

void Aud_SetCompatSmallChunks(int enabled)
{
    aud_compat_small_chunks = enabled ? 1 : 0;
}
int Aud_GetCompatSmallChunks(void)
{
    return aud_compat_small_chunks;
}
void Aud_SetCompatDeepQueue(int enabled)
{
    aud_compat_deep_queue = enabled ? 1 : 0;
}
int Aud_GetCompatDeepQueue(void)
{
    return aud_compat_deep_queue;
}

/* audsrv_stop_audio() does more than empty its queue: it leaves the IOP
   mixer stopped until the next audsrv_play_audio() call.  Keep that state
   explicit so the legacy Aud_Play() API really resumes playback instead of
   relying on the first emulator/BGM block to do it by accident. */
static void Aud_WakeAudsrv(void)
{
    int ret;

    if (!sjpcm_inited || sjpcm_playing)
        return;

    /* AURORA_V7_AUDSRV_PRIME
       Keep ~16 ms of known-valid silence beyond audsrv's built-in half-ring
       startup cushion.  The old 64-frame prime was only ~1.3 ms and left too
       little protection against an isolated slow EE frame. */
    {
        const int prime_samples =
            aud_compat_deep_queue ? 1024 : AUD_WAKE_PRIME_SAMPLES;
        memset(_interleave_buf, 0,
               prime_samples * AUD_BYTES_PER_SAMPLE);
        ret = audsrv_play_audio((const char *)_interleave_buf,
                                prime_samples * AUD_BYTES_PER_SAMPLE);
    }
    if (ret >= 0)
        sjpcm_playing = 1;
}


static void Aud_AsyncReset(void)
{
    aud_async_head = 0;
    aud_async_count = 0;
    aud_async_cached_avail = -1;
    aud_async_probe_budget = 0;
}

void Aud_AsyncDiscardPending(void)
{
    Aud_AsyncReset();
}

static void Aud_AsyncCopyIn(short *left, short *right, int size)
{
    int tail;
    int first;
    int second;

    if (size <= 0)
        return;

    tail = aud_async_head + aud_async_count;
    if (tail >= AUD_ASYNC_FIFO_SAMPLES)
        tail -= AUD_ASYNC_FIFO_SAMPLES;

    first = AUD_ASYNC_FIFO_SAMPLES - tail;
    if (first > size)
        first = size;
    second = size - first;

    memcpy(&_async_left[tail], left, (size_t)first * sizeof(short));
    memcpy(&_async_right[tail], right, (size_t)first * sizeof(short));

    if (second > 0)
    {
        memcpy(&_async_left[0], left + first,
               (size_t)second * sizeof(short));
        memcpy(&_async_right[0], right + first,
               (size_t)second * sizeof(short));
    }

    aud_async_count += size;
}

/* Drain at most one contiguous FIFO span.
 * wait=0 is the normal gameplay path and NEVER calls audsrv_wait_audio().
 * Keep two sample-frames free because the existing servo may add at most 2.
 * wait=1 is only a deep-backlog / explicit-Wait safety valve.
 */
static int Aud_AsyncDrainOne(int wait)
{
    int n;

    if (!sjpcm_inited || aud_async_count <= 0)
        return 0;

    n = AUD_ASYNC_FIFO_SAMPLES - aud_async_head;
    if (n > aud_async_count)
        n = aud_async_count;
    if (n > AUD_MAX_ENQUEUE_SAMPLES)
        n = AUD_MAX_ENQUEUE_SAMPLES;

    if (wait)
    {
        /* Rare overflow / explicit Aud_Wait fallback. Keep V2's lossless
           blocking behavior away from the normal gameplay path. */
        Aud_Enqueue(&_async_left[aud_async_head],
                    &_async_right[aud_async_head],
                    n, 1);

        aud_async_head += n;
        if (aud_async_head >= AUD_ASYNC_FIFO_SAMPLES)
            aud_async_head -= AUD_ASYNC_FIFO_SAMPLES;
        aud_async_count -= n;
        aud_async_cached_avail = -1;
        aud_async_probe_budget = 0;
        return n;
    }

    {
        int usable;
        int i;
        int bytes;
        int sent_bytes;
        int sent_frames;

        /* Probe only when the conservative cache is unknown/low or after
           several drains. Aud_Available() is a synchronous EE<->IOP RPC. */
        if (aud_async_cached_avail < 0 ||
            aud_async_cached_avail <= AUD_ASYNC_RING_MARGIN_SAMPLES ||
            aud_async_probe_budget <= 0)
        {
            aud_async_cached_avail = Aud_Available();
            aud_async_probe_budget = AUD_ASYNC_AVAIL_PROBE_BUDGET;
        }

        if (aud_async_cached_avail <= AUD_ASYNC_RING_MARGIN_SAMPLES)
            return 0;

        usable = aud_async_cached_avail - AUD_ASYNC_RING_MARGIN_SAMPLES;
        if (n > usable)
            n = usable;
        if (n > AUD_ASYNC_MAX_BURST_SAMPLES)
            n = AUD_ASYNC_MAX_BURST_SAMPLES;
        if (aud_compat_small_chunks && n > 256)
            n = 256;

        if (n <= 0)
            return 0;

        /* Direct interleave for the exact contiguous FIFO prefix.
           This bypasses Aud_Enqueue's periodic audsrv_queued() servo RPC
           on the hot async path. The rational frame scheduler remains the
           emulated-audio clock authority. */
        for (i = 0; i < n; i++)
        {
            Aud_StoreStereoFrame(
                i,
                _async_left[aud_async_head + i],
                _async_right[aud_async_head + i]
            );
        }

        bytes = n * AUD_BYTES_PER_SAMPLE;

        /* <= 8192 bytes in Standard mode, below audsrv RPC's ~16 KiB
           internal copy limit: one PLAY_AUDIO RPC. audsrv may accept only
           a prefix; consume exactly the complete stereo frames reported. */
        sent_bytes = audsrv_play_audio((const char *)_interleave_buf, bytes);
        if (sent_bytes <= 0)
        {
            aud_async_cached_avail = -1;
            aud_async_probe_budget = 0;
            return 0;
        }

        sent_frames = sent_bytes / AUD_BYTES_PER_SAMPLE;
        if (sent_frames > n)
            sent_frames = n;
        if (sent_frames <= 0)
        {
            aud_async_cached_avail = -1;
            aud_async_probe_budget = 0;
            return 0;
        }

        aud_async_head += sent_frames;
        if (aud_async_head >= AUD_ASYNC_FIFO_SAMPLES)
            aud_async_head -= AUD_ASYNC_FIFO_SAMPLES;
        aud_async_count -= sent_frames;

        aud_async_cached_avail -= sent_frames;
        if (aud_async_cached_avail < 0)
            aud_async_cached_avail = 0;
        if (aud_async_probe_budget > 0)
            aud_async_probe_budget--;

        sjpcm_playing = 1;
        return sent_frames;
    }
}


/* Prepare a bounded reservoir before the first gameplay frame.
 *
 * audsrv is shared by menu BGM and game audio, so queue depth at the exact
 * menu->game transition can vary. Inspect it once and append only missing
 * ZERO PCM. Existing queued audio is untouched.
 *
 * No real PCM is dropped, repeated, resampled or time-stretched.
 */
void Aud_PrepareGameplayHeadroom(void)
{
    int queued_bytes;
    int queued_samples;
    int need_samples;
    int remaining_bytes;
    const int target_samples = aud_compat_deep_queue
        ? 2560 : AUD_GAMEPLAY_HEADROOM_SAMPLES;

    if (!sjpcm_inited)
        return;

    queued_bytes = audsrv_queued();
    if (queued_bytes < 0)
        return;

    queued_samples = queued_bytes / AUD_BYTES_PER_SAMPLE;
    need_samples = target_samples - queued_samples;
    if (need_samples <= 0)
        return;

    remaining_bytes = need_samples * AUD_BYTES_PER_SAMPLE;

    while (remaining_bytes > 0)
    {
        int chunk = remaining_bytes;
        int sent;

        /* Same 4 KiB maximum used by V8.5's normal lossless enqueue path. */
        if (chunk > 4096)
            chunk = 4096;

        if (audsrv_wait_audio(chunk) != AUDSRV_ERR_NOERROR)
            break;

        /* The source is all zeroes; each iteration may safely reuse it. */
        sent = audsrv_play_audio((const char *)_gameplay_silence, chunk);
        if (sent <= 0)
            break;

        remaining_bytes -= sent;
        sjpcm_playing = 1;
    }
}


int Aud_Init(int sync, int numsamples, int maxenqueuesamples)
{
    struct audsrv_fmt_t fmt;
    int ret;

    (void)sync;
    (void)numsamples;
    (void)maxenqueuesamples;

    if (sjpcm_inited)
    {
        return 0;
    }

    /* Mirror init progression to the on-screen splash log too -- on
       real PS2 hardware without an SIO cable, the screen is the only
       way to see where init died.  If audsrv_init() blocks inside
       SifBindRpc (because the IOP audsrv RPC server never registered),
       the user will see "audsrv_init..." on screen as the last line
       and we know exactly which step deadlocked. */
    // DLog("[snes-aud] audsrv_init() ...");
    ret = audsrv_init();
    // DLog("[snes-aud] audsrv_init() = %d", ret);
    if (ret != 0)
    {
        // DLog("[snes-aud] init FAILED %d (%s)",
        //      ret, audsrv_get_error_string());
        return -1;
    }

    fmt.freq     = AUD_AUDSRV_FREQ;
    fmt.bits     = AUD_AUDSRV_BITS;
    fmt.channels = AUD_AUDSRV_CHANNELS;

    ret = audsrv_set_format(&fmt);
    // DLog("[snes-aud] set_format(48000,16,2) = %d", ret);
    if (ret != 0)
    {
        // DLog("[snes-aud] set_format FAILED %d (%s)",
        //      ret, audsrv_get_error_string());
        audsrv_quit();
        return -1;
    }

    /* Default to full volume. Aud_Setvol() may override. */
    ret = audsrv_set_volume(MAX_VOLUME);
    // DLog("[snes-aud] set_volume(%d) = %d", MAX_VOLUME, ret);

    /* Prime audsrv before a producer asks queued()/available(). */
    sjpcm_inited = 1;
    sjpcm_playing = 0;
    /* AURORA_V7_AUD_GUARD_RESET_INIT */
    aud_guard_enqueues = 0;
    Aud_AsyncReset();
    Aud_WakeAudsrv();
    return 0;
}


void Aud_Quit(void)
{
    if (!sjpcm_inited) return;
    audsrv_stop_audio();
    audsrv_quit();
    sjpcm_inited = 0;
    sjpcm_playing = 0;
    Aud_AsyncReset();
}


/*
    The original API was pause/play; audsrv plays continuously while
    samples are queued, so Play is effectively "make sure not stopped"
    and Pause is "drop the queue". AudMixBuffer calls
    Aud_Clearbuff() + Aud_Play() once at boot.
*/
void Aud_Play(void)
{
    Aud_WakeAudsrv();
}


void Aud_Pause(void)
{
    if (!sjpcm_inited) return;
    audsrv_stop_audio();
    sjpcm_playing = 0;
    /* AURORA_V7_AUD_GUARD_RESET_PAUSE */
    aud_guard_enqueues = 0;
    Aud_AsyncReset();
}


void Aud_Clearbuff(void)
{
    if (!sjpcm_inited) return;
    audsrv_stop_audio();
    sjpcm_playing = 0;
    /* AURORA_V7_AUD_GUARD_RESET_CLEAR */
    aud_guard_enqueues = 0;
    Aud_AsyncReset();
}


/*
    Aud_Setvol took a 14-bit hardware-style volume (0..0x3FFF) where
    0x3FFF was full scale. audsrv's volume is 0..MAX_VOLUME (100), so
    rescale.
*/
void Aud_Setvol(unsigned int volume)
{
    int v;

    if (!sjpcm_inited) return;

    volume &= 0x3FFF;
    v = (int)((volume * MAX_VOLUME) / 0x3FFF);
    if (v < MIN_VOLUME) v = MIN_VOLUME;
    if (v > MAX_VOLUME) v = MAX_VOLUME;

    audsrv_set_volume(v);
}


/*
    Bytes already queued in audsrv's IOP-side ring buffer, expressed as
    stereo sample-frames so the math in AudMixBuffer::GetOutputSamples
    keeps working unchanged.
*/
int Aud_Buffered(void)
{
    int bytes;

    if (!sjpcm_inited) return 0;

    bytes = audsrv_queued();
    if (bytes < 0) return 0;

    return bytes / AUD_BYTES_PER_SAMPLE;
}


int Aud_Available(void)
{
    int bytes;

    if (!sjpcm_inited) return 0;

    bytes = audsrv_available();
    if (bytes < 0) return 0;

    return bytes / AUD_BYTES_PER_SAMPLE;
}


/*
    Interleave separate left/right channels and push to audsrv. `wait`
    selects between blocking until enough room is available
    (audsrv_wait_audio) and best-effort (drop overflow if the IOP ring
    is full).
*/
void Aud_Enqueue(short *left, short *right, int size, int wait)
{
    int i;
    int bytes;
    const char *src;
    int remaining;

    if (!sjpcm_inited) return;
    if (size <= 0) return;
    if (size > AUD_MAX_ENQUEUE_SAMPLES) size = AUD_MAX_ENQUEUE_SAMPLES;

    /* AURORA_V81_AUD_ONE_SHOT_SERVO
     * The rational 59.94/50-Hz frame scheduler remains the clock authority.
     * Poll audsrv only once every 16 enqueues after warm-up, and make at most
     * ONE +/-2-sample-frame correction on that poll block.  This restores
     * queue margin slowly without creating a periodic pitch correction. */
    int aud_adjust_now = 0;
    if (aud_guard_enqueues >= AUD_GUARD_WARMUP_ENQUEUES &&
        (aud_guard_enqueues % AUD_GUARD_POLL_ENQUEUES) == 0)
    {
        int queued_bytes = audsrv_queued();
        if (queued_bytes >= 0)
        {
            int queued_samples = queued_bytes / AUD_BYTES_PER_SAMPLE;
            const int guard_low =
                aud_compat_deep_queue ? 3072 : AUD_GUARD_LOW_SAMPLES;
            const int guard_high =
                aud_compat_deep_queue ? 4096 : AUD_GUARD_HIGH_SAMPLES;
            if (queued_samples < guard_low)
                aud_adjust_now = AUD_GUARD_ADJUST_SAMPLES;
            else if (queued_samples > guard_high)
                aud_adjust_now = -AUD_GUARD_ADJUST_SAMPLES;
        }
    }
    aud_guard_enqueues++;

    if (aud_adjust_now > 0 &&
        size > aud_adjust_now &&
        size + aud_adjust_now <= AUD_MAX_ENQUEUE_SAMPLES)
    {
        int out = 0;
        int acc = size / 2;
        const int add = aud_adjust_now;
        for (i = 0; i < size; i++)
        {
            Aud_StoreStereoFrame(out, left[i], right[i]);
            out++;
            acc += add;
            if (acc >= size)
            {
                acc -= size;
                _interleave_buf[out * 2 + 0] = left[i];
                _interleave_buf[out * 2 + 1] = right[i];
                out++;
            }
        }
        size = out;
    }
    else if (aud_adjust_now < 0 && size > -aud_adjust_now)
    {
        int out = 0;
        int acc = size / 2;
        const int remove = -aud_adjust_now;
        for (i = 0; i < size; i++)
        {
            acc += remove;
            if (acc >= size)
            {
                acc -= size;
                continue;
            }
            Aud_StoreStereoFrame(out, left[i], right[i]);
            out++;
        }
        size = out;
    }
    else
    {
        for (i = 0; i < size; i++)
        {
            Aud_StoreStereoFrame(i, left[i], right[i]);
        }
    }

    bytes = size * AUD_BYTES_PER_SAMPLE;

    if (wait)
    {
        /* AURORA_MEGA_V4_AUDSRV_LOSSLESS_CHUNKS
         * PS2SDK audsrv truncates play_audio() to the room available in its
         * IOP ring. wait_audio() guarantees its requested amount, but the
         * ring starts only half-free after set_format(). Keep each request
         * <= 4096 bytes: below the initial free window and normally one whole
         * emulator frame. A rare short positive write is retried from the
         * exact unsent byte, so PCM is neither dropped nor duplicated. */
        src = (const char *)_interleave_buf;
        remaining = bytes;
        {
            const int chunk_limit = aud_compat_small_chunks ? 1024 : 4096;
            while (remaining > 0)
            {
                int chunk = remaining;
                int sent;

                if (chunk > chunk_limit)
                    chunk = chunk_limit;

            if (audsrv_wait_audio(chunk) != AUDSRV_ERR_NOERROR)
                return;

            sent = audsrv_play_audio(src, chunk);
            if (sent < 0)
                return;
            if (sent == 0)
                continue;

                src += sent;
                remaining -= sent;
                sjpcm_playing = 1;
            }
        }
        return;
    }

    if (audsrv_play_audio((const char *)_interleave_buf, bytes) >= 0)
        sjpcm_playing = 1;
}


/*
 * AURORA_AUDIO_ASYNC_FIFO_V2
 * Gameplay emulation must not wait for IOP ring SPACE inside a frame.
 */
void Aud_BufferedAsyncStart(void)
{
    int budget = aud_compat_small_chunks ? 4 : 1;

    while (budget-- > 0 && aud_async_count > 0)
    {
        if (Aud_AsyncDrainOne(0) <= 0)
            break;
    }
}


int Aud_BufferedAsyncGet(void)
{
    return Aud_Buffered() + aud_async_count;
}


void Aud_EnqueueAsync(short *left, short *right, int size)
{
    if (!sjpcm_inited || !left || !right || size <= 0)
        return;

    if (size > AUD_MAX_ENQUEUE_SAMPLES)
        size = AUD_MAX_ENQUEUE_SAMPLES;

    /* V3: producer is RAM-only. The post-frame hook owns IOP RPC. */

    /* AURORA_AUDIO_OVERFLOW_NOBLOCK_V4
     *
     * Last-resort only. If >~1.36 s of exact gameplay PCM is already staged,
     * the audio backend is catastrophically behind. The former safety path
     * could block waiting for IOP ring space.
     *
     * Do NOT turn an audio failure into a CPU/video hitch. Preserve every
     * already-staged sample in order and drop only this NEW block. Under
     * normal operation this branch is never reached. */
    if (aud_async_count + size > AUD_ASYNC_FIFO_SAMPLES)
        return;

    Aud_AsyncCopyIn(left, right, size);
}


void Aud_Wait(void)
{
    while (aud_async_count > 0)
    {
        if (Aud_AsyncDrainOne(1) <= 0)
            break;
    }
}


int Aud_IsInitialized(void)
{
    return sjpcm_inited;
}
