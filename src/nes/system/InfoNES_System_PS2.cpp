/* InfoNES_System_PS2.cpp - PS2 platform layer for InfoNES.
 *
 * InfoNES_System.h declares a contract that every InfoNES platform must
 * satisfy: input poll, framebuffer flip, sound output, memcpy/memset
 * trampolines, debug print, etc. This file is the PS2 implementation
 * plus a one-frame stepper used by NesSystem::ExecuteFrame.
 *
 * Current status:
 *   - InfoNES_PadState  - reads g_pNesInputState (SNES bit layout) and
 *                         remaps to NES PAD1_Latch / PAD2_Latch.
 *   - InfoNES_LoadFrame - converts WorkFrame[256*240] (RGB555) into
 *                         g_pNesTargetSurface (RGBA8 256x256).
 *   - InfoNES_RunOneFrame - inlined InfoNES_Cycle body, bounded to a
 *                         single NES frame (262 scanlines).  Called by
 *                         NesSystem::ExecuteFrame.
 *   - InfoNES_MemoryCopy / MemorySet - libc trampolines.
 *   - InfoNES_DebugPrint / MessageBox - printf.
 *   - InfoNES_Sound*    - mixes and resamples pAPU into CMixBuffer.
 *
 * The NesPalette[] table is the 64-entry NES master palette in RGB555
 * form that InfoNES uses internally. Values from upstream InfoNES.
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "types.h"

#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_Types.h"
#include "K6502.h"

#include "emuinput.h"     /* Emu::SysInputT */
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"    /* CMixBuffer (audsrv-backed audio sink) */

/* SNES bit layout we need to translate FROM.  _MainLoopInput is still
   wired to _MainLoopSnesInput at this point so every connected pad in
   SysInputT.uPad[i] uses these bits regardless of which emulator is
   running. Phase 4 will route per-system. */
#include "snio.h"
#include "prof.h"   /* PROF_ENTER/LEAVE (no-ops unless PROFILE=1) */


/* Per-frame state owned by nessystem.cpp; we just read it.

   InfoNES is compiled as C++ (.cpp files), so plain `extern` is enough
   here - everything links with C++ linkage. We deliberately don't use
   `extern "C"` because the InfoNES headers themselves don't, and any
   linkage mismatch on these globals would silently break at link time. */
extern CRenderSurface       *g_pNesTargetSurface;
extern Emu::SysInputT       *g_pNesInputState;
extern CMixBuffer           *g_pNesMixBuffer;

/* SpriteJustHit lives in InfoNES.cpp but isn't externed by InfoNES.h.
   Declare it here so InfoNES_RunOneFrame can mirror the sprite-0 hit
   timing exactly the way InfoNES_Cycle does. */
extern int SpriteJustHit;


/* ---- NES master palette, RGB555 (standard NES palette, R high bits) -
 *
 * Upstream InfoNES ships an idiosyncratic palette (NesPalette[0]=0x39ce
 * for the universal backdrop, which decodes to a magenta/pink instead
 * of the black/dark-grey every other NES emulator uses).  That was the
 * cause of the bright-pink sky in Super Mario Bros 3 vs. the black sky
 * shown by RetroArch / FCEUX / Mesen.
 *
 * The values below are the standard FCEUX-style master palette
 * (8-bit RGB triples documented at https://emudev.de and
 * https://www.nesdev.org/nespal.txt) converted to RGB555 with R in the
 * top 5 bits, G in the middle 5, B in the bottom 5 -- the same bit
 * order InfoNES's WorkFrame[] uses.
 */
WORD NesPalette[ 64 ] =
{
  0x3def, 0x001f, 0x0017, 0x20b7, 0x4810, 0x5404, 0x5440, 0x4440,
  0x28c0, 0x01e0, 0x01a0, 0x0160, 0x010b, 0x0000, 0x0000, 0x0000,
  0x5ef7, 0x01ff, 0x017f, 0x351f, 0x6c19, 0x700b, 0x7ce0, 0x7162,
  0x55e0, 0x02e0, 0x02a0, 0x02a8, 0x0231, 0x0000, 0x0000, 0x0000,
  0x7fff, 0x1eff, 0x363f, 0x4dff, 0x7dff, 0x7d73, 0x7deb, 0x7e88,
  0x7ee0, 0x5fe3, 0x2f6a, 0x2ff3, 0x03bb, 0x3def, 0x0000, 0x0000,
  0x7fff, 0x539f, 0x5eff, 0x6eff, 0x7eff, 0x7e98, 0x7b56, 0x7f95,
  0x7f6f, 0x6fef, 0x5ff7, 0x5ffb, 0x03ff, 0x7f7f, 0x0000, 0x0000
};


/* (A 5-bit->8-bit expansion LUT used to live here. The PS2 profiler
 * showed the LUT was actually the bottleneck -- 3 dependent byte loads
 * per pixel stall on the EE's load-use latency -- so InfoNES_LoadFrame
 * now expands inline with (v<<3)|(v>>2), which is cheaper. LUT removed.)
 */


/* ------------------------------------------------------------------ *
 *  InfoNES_RunOneFrame                                                *
 * ------------------------------------------------------------------ *
 * NesSystem::ExecuteFrame calls this to advance the emulator by
 * exactly one NES frame.  It's a stripped-down copy of InfoNES_Cycle
 * from InfoNES.cpp - same instruction stream + HSync calls - but
 * bounded by scanline count so it returns at end-of-frame instead of
 * looping forever waiting for a PAD_SYS_QUIT.
 *
 * One frame = 263 scanlines (SCAN_VBLANK_END is 262 inclusive).  At
 * scanline 240 InfoNES_HSync calls InfoNES_LoadFrame, which writes the
 * fully rendered WorkFrame[] into the target surface.  At scanline
 * 243 (SCAN_VBLANK_START) InfoNES_HSync calls InfoNES_PadState; we
 * also handle NMI on VBlank there.
 *
 * If InfoNES_HSync ever returns -1 (PAD_SYS_QUIT) we break early.
 * Our PadState never sets QUIT so this is just a safety net.
 */
void InfoNES_RunOneFrame(void)
{
    /* One NES frame.  PPU_Scanline wraps from SCAN_VBLANK_END (262)
       back to 0 inside InfoNES_HSync; we just need to step enough
       scanlines that we land back at the start of the next frame. */
    /* Render direto: aponta o WorkFrame para a surface de saida, entao
       o InfoNES_DrawLine escreve RGBA8 direto na textura (sem passada de
       conversao separada). Surface e' 256-wide RGBA8, pitch = 256*4, logo
       WorkFrame[y*256 + x] mapeia exatamente o pixel (x,y). */
    if (g_pNesTargetSurface)
        WorkFrame = (unsigned int *)g_pNesTargetSurface->GetLinePtr(0);

    for (int sl = 0; sl < 263; sl++)
    {
        int nStep;

        if (SpriteJustHit == PPU_Scanline &&
            PPU_ScanTable[PPU_Scanline] == SCAN_ON_SCREEN)
        {
            /* Sprite-0 hit needs the CPU to be advanced to the correct
               X position within the scanline before R2_HIT_SP fires.
               STEP_PER_SCANLINE is 113 PPU dots; SPR_X = sprite-0 X. */
            nStep = SPRRAM[SPR_X] * STEP_PER_SCANLINE / NES_DISP_WIDTH;
            K6502_Step((WORD)nStep);

            if ((PPU_R1 & R1_SHOW_SP) && (PPU_R1 & R1_SHOW_SCR))
                PPU_R2 |= R2_HIT_SP;

            if ((PPU_R0 & R0_NMI_SP) && (PPU_R1 & R1_SHOW_SP))
                NMI_REQ;

            K6502_Step((WORD)(STEP_PER_SCANLINE - nStep));
        }
        else
        {
            K6502_Step((WORD)STEP_PER_SCANLINE);
        }

        /* Frame IRQ counter tick (matches InfoNES.cpp:629-635). */
        FrameStep += STEP_PER_SCANLINE;
        if (FrameStep > STEP_PER_FRAME && FrameIRQ_Enable)
        {
            FrameStep %= STEP_PER_FRAME;
            IRQ_REQ;
            APU_Reg[0x15] |= 0x40;
        }

        /* Per-mapper hsync callback. */
        MapperHSync();

        /* Standard InfoNES per-scanline housekeeping (also draws the
           visible scanline, polls input at VBlank, etc.). */
        if (InfoNES_HSync() == -1)
            break;
    }
}


/* ------------------------------------------------------------------ *
 *  InfoNES_LoadFrame                                                  *
 * ------------------------------------------------------------------ *
 * Called once per visible NES frame from inside InfoNES_HSync (at
 * SCAN_UNKNOWN_START, after every scanline 0..239 has been rendered
 * by InfoNES_DrawLine).  WorkFrame[256*240] is in RGB555 (5 bits per
 * channel, LSB = blue, MSB = unused) thanks to NesPalette being in
 * that format and PalTable[] mirroring it.
 *
 * Target is a 256x256 RGBA8 surface (mainloop_init.cpp:274 allocates
 * _fbTexture[] as PIXELFORMAT_RGBA8).  We write the NES visible 240
 * lines and leave 16 padding lines below as black (they're outside
 * the on-screen quad in mainloop_render.cpp anyway).
 *
 * Bit layout:
 *   RGB555: 0 RRRRR GGGGG BBBBB
 *   RGBA8:  RR GG BB AA (little-endian: R is first byte at offset 0).
 */
void InfoNES_LoadFrame(void)
{
    CRenderSurface *pTarget = g_pNesTargetSurface;
    if (!pTarget) return;

    PixelFormatT *pFmt = pTarget->GetFormat();
    if (!pFmt || pFmt->uBitDepth != 32) return;

    Uint32 uWidth  = pTarget->GetWidth();
    Uint32 uHeight = pTarget->GetHeight();
    if (uWidth < NES_DISP_WIDTH || uHeight < NES_DISP_HEIGHT) return;

    PROF_ENTER("NesLoadFrame");

    /* InfoNES_DrawLine ja' escreveu RGBA8 direto nas linhas 0..239 desta
       surface (WorkFrame aponta pra ela) -- entao NAO ha' mais passada de
       conversao (era o custo de ~1.7M ciclos que removemos). So' apagamos
       as linhas de padding abaixo da imagem de 240 linhas do NES. */
    for (Uint32 iY = NES_DISP_HEIGHT; iY < uHeight; iY++)
    {
        Uint8 *pDst = pTarget->GetLinePtr((Int32)iY);
        if (pDst) memset(pDst, 0, uWidth * 4);
    }

    PROF_LEAVE("NesLoadFrame");
}


/* ------------------------------------------------------------------ *
 *  InfoNES_PadState                                                   *
 * ------------------------------------------------------------------ *
 * Polled once per frame at SCAN_VBLANK_START from InfoNES_HSync.
 * We read g_pNesInputState (SNES bit layout) and remap into NES
 * controller bits.
 *
 * Standard NES bit order (lowest first, matches the serial protocol
 * that K6502_rw.h::PAD1 reads bit-by-bit):
 *
 *   bit 0 = A         bit 4 = UP
 *   bit 1 = B         bit 5 = DOWN
 *   bit 2 = SELECT    bit 6 = LEFT
 *   bit 3 = START     bit 7 = RIGHT
 *
 * Player mapping (matches how _MainLoopSnesInput already turned the
 * PS2 buttons into SNES bits).  PS2 convention is that the bottom of
 * the diamond (Cross) is the primary action button -- in NES Mario
 * games that's the JUMP button, which is NES A.  Triangle (top) maps
 * to NES A too so a SF-style 4-face controller still works.  Square /
 * Circle map to NES B (run/secondary).
 *
 *   PS2 Cross  / Triangle (= SNES B / SNES X) -> NES A (jump)
 *   PS2 Square / Circle   (= SNES Y / SNES A) -> NES B (run)
 *   PS2 Select / Start                         -> NES Select / Start
 *
 * PAD_System is for emulator-level commands like PAD_SYS_QUIT; we
 * never set it so InfoNES_HSync never breaks out of InfoNES_Cycle on
 * QUIT.  Menu return is handled by _MainLoopInputProcess instead.
 */
static DWORD MapSnesToNes(Uint16 snes)
{
    DWORD nes = 0;
    if (snes == EMUSYS_DEVICE_DISCONNECTED) return 0;

    if (snes & (SNESIO_JOY_B | SNESIO_JOY_X))      nes |= 0x01; /* A=jump */
    if (snes & (SNESIO_JOY_A | SNESIO_JOY_Y))      nes |= 0x02; /* B=run  */
    if (snes &  SNESIO_JOY_SELECT)                  nes |= 0x04; /* SELECT */
    if (snes &  SNESIO_JOY_START)                   nes |= 0x08; /* START  */
    if (snes &  SNESIO_JOY_UP)                      nes |= 0x10;
    if (snes &  SNESIO_JOY_DOWN)                    nes |= 0x20;
    if (snes &  SNESIO_JOY_LEFT)                    nes |= 0x40;
    if (snes &  SNESIO_JOY_RIGHT)                   nes |= 0x80;
    return nes;
}

void InfoNES_PadState( DWORD *pdwPad1, DWORD *pdwPad2, DWORD *pdwSystem )
{
    Emu::SysInputT *pInput = g_pNesInputState;
    DWORD p1 = 0, p2 = 0;

    if (pInput)
    {
        p1 = MapSnesToNes(pInput->uPad[0]);
        p2 = MapSnesToNes(pInput->uPad[1]);
    }

    if (pdwPad1)   *pdwPad1   = p1;
    if (pdwPad2)   *pdwPad2   = p2;
    if (pdwSystem) *pdwSystem = 0;
}


/* ---------------------------------------------------------------- *
 *  Other platform stubs                                              *
 * ---------------------------------------------------------------- */

int InfoNES_Menu( void )
{
    /* InfoNES_Main() calls this in a loop. Returning -1 tells the core
       to exit gracefully. NesSystem::ExecuteFrame never enters
       InfoNES_Main, so this is dead code today. */
    return -1;
}

int InfoNES_ReadRom( const char *pszFileName )
{
    /* NesSystem hands ROM data directly to the InfoNES globals,
       bypassing InfoNES_Load entirely. Provided only so the link
       resolves. */
    (void)pszFileName;
    return -1;
}

void InfoNES_ReleaseRom( void )
{
}

void *InfoNES_MemoryCopy( void *dest, const void *src, int count )
{
    return memcpy(dest, src, (size_t)count);
}

void *InfoNES_MemorySet( void *dest, int c, int count )
{
    return memset(dest, c, (size_t)count);
}

void InfoNES_DebugPrint( char *pszMsg )
{
    if (pszMsg) printf("%s", pszMsg);
}

void InfoNES_Wait( void )
{
}

/* NES-only mixer state.  The SNES still reaches CMixBuffer through its own
   SPC700 path; none of these filters or resampler variables are shared. */
#define NES_MIX_PEAK       16000
#define NES_DC_BLOCK_Q15   32604  /* 0.995 in Q15 */

static int    s_NesSampleRate = 44100;
static Uint32 s_NesOutputRate;
static Int16  s_NesPulseLut[31];
static Int16  s_NesTndLut[203];
static BYTE   s_NesLutsReady;
static Int32  s_NesDcPrevInput;
static Int32  s_NesDcPrevOutput;
static Int64  s_NesResamplePos;
static Int16  s_NesResamplePrev;
static BYTE   s_NesHaveResamplePrev;
static Int16  s_NesPending[4];
static int    s_NesPendingCount;

static void InfoNES_BuildMixerLuts( void )
{
    int i;
    if (s_NesLutsReady)
        return;

    /* NESdev's nonlinear 2A03 mixer approximations, precomputed once so the
       PS2 executes only two table lookups per PCM sample. */
    for (i = 0; i <= 30; i++)
    {
        Int64 numerator = (Int64)9588 * i * NES_MIX_PEAK;
        Int64 denominator = (Int64)100 * (8128 + 100 * i);
        s_NesPulseLut[i] = denominator ? (Int16)(numerator / denominator) : 0;
    }
    for (i = 0; i <= 202; i++)
    {
        Int64 numerator = (Int64)16367 * i * NES_MIX_PEAK;
        Int64 denominator = (Int64)100 * (24329 + 100 * i);
        s_NesTndLut[i] = denominator ? (Int16)(numerator / denominator) : 0;
    }
    s_NesLutsReady = 1;
}

void InfoNES_SoundReset( void )
{
    InfoNES_BuildMixerLuts();
    s_NesOutputRate = 0;
    s_NesDcPrevInput = 0;
    s_NesDcPrevOutput = 0;
    s_NesResamplePos = 0;
    s_NesResamplePrev = 0;
    s_NesHaveResamplePrev = 0;
    s_NesPendingCount = 0;
}

void InfoNES_SoundInit( void )
{
    InfoNES_SoundReset();
}

int InfoNES_SoundOpen( int samples_per_sync, int sample_rate )
{
    /* The audsrv/SPU2 stream is already open.  Keep the native pAPU rate and
       reset interpolation history whenever a ROM resets. */
    (void)samples_per_sync;
    s_NesSampleRate = (sample_rate > 0) ? sample_rate : 44100;
    InfoNES_SoundReset();
    return 1;
}

void InfoNES_SoundClose( void )
{
    InfoNES_SoundReset();
}

/* InfoNES supplies the five base 2A03 channels once per video frame.  Mix
   with the NES nonlinear response, remove DAC DC with a cheap one-pole
   blocker, then continuously resample to CMixBuffer's rate.  The old code
   restarted interpolation at every frame and always emitted floor(533.33)
   samples, causing a boundary discontinuity and a small pitch error. */
void InfoNES_SoundOutput( int samples, BYTE *wave1, BYTE *wave2,
                          BYTE *wave3, BYTE *wave4, BYTE *wave5 )
{
    CMixBuffer *pMix = g_pNesMixBuffer;
    static Int16 s_NesMix[1024];
    static Int16 s_NesOut[2048];
    const int capMix = (int)(sizeof(s_NesMix) / sizeof(s_NesMix[0]));
    const int capOut = (int)(sizeof(s_NesOut) / sizeof(s_NesOut[0]));
    Uint32 mixRate = 32000, mixBits = 16, mixCh = 2;
    int nesRate = (s_NesSampleRate > 0) ? s_NesSampleRate : 44100;
    Uint64 step;
    Int64 limit;
    int nOut = 0;
    int i;

    if (!pMix || samples <= 1)
        return;
    if (samples > capMix)
        samples = capMix;

    /* 1) Nonlinear channel mix at the native 44.1 kHz pAPU rate. */
    for (i = 0; i < samples; i++)
    {
        int pulse = (int)wave1[i] + (int)wave2[i];
        int tnd = 3 * (int)wave3[i] + 2 * (int)wave4[i] + (int)wave5[i];
        Int32 input, output;

        if (pulse > 30) pulse = 30;
        if (tnd > 202) tnd = 202;
        input = (Int32)s_NesPulseLut[pulse] + (Int32)s_NesTndLut[tnd];

        output = input - s_NesDcPrevInput +
            (Int32)(((Int64)s_NesDcPrevOutput * NES_DC_BLOCK_Q15) >> 15);
        s_NesDcPrevInput = input;
        if (output > 32767) output = 32767;
        if (output < -32768) output = -32768;
        s_NesDcPrevOutput = output;
        s_NesMix[i] = (Int16)output;
    }

    /* 2) Continuous 32.32 resampler.  A negative position denotes the one
          interval joining the previous block to this block, so no sample is
          repeated and no frame-edge click is introduced. */
    pMix->GetFormat(&mixRate, &mixBits, &mixCh);
    if (mixRate == 0)
        mixRate = 32000;
    if (mixRate != s_NesOutputRate)
    {
        s_NesOutputRate = mixRate;
        s_NesResamplePos = 0;
        s_NesHaveResamplePrev = 0;
        s_NesPendingCount = 0;
    }

    for (i = 0; i < s_NesPendingCount; i++)
        s_NesOut[nOut++] = s_NesPending[i];

    step = ((Uint64)(unsigned int)nesRate << 32) / mixRate;
    if (!step)
        step = 1;
    limit = (Int64)(samples - 1) << 32;

    while (s_NesResamplePos < limit && nOut < capOut)
    {
        Int32 a, b;
        Uint32 frac;

        if (s_NesResamplePos < 0 && s_NesHaveResamplePrev)
        {
            Uint64 rel = (Uint64)(s_NesResamplePos + ((Int64)1 << 32));
            a = s_NesResamplePrev;
            b = s_NesMix[0];
            frac = (Uint32)(rel >> 16) & 0xffff;
        }
        else
        {
            int index = (int)(s_NesResamplePos >> 32);
            Uint64 rel;
            if (index < 0) index = 0;
            if (index >= samples - 1) index = samples - 2;
            rel = (Uint64)(s_NesResamplePos - ((Int64)index << 32));
            a = s_NesMix[index];
            b = s_NesMix[index + 1];
            frac = (Uint32)(rel >> 16) & 0xffff;
        }

        s_NesOut[nOut++] = (Int16)(a +
            (Int32)(((Int64)(b - a) * frac) >> 16));
        s_NesResamplePos += (Int64)step;
    }

    s_NesResamplePrev = s_NesMix[samples - 1];
    s_NesHaveResamplePrev = 1;
    s_NesResamplePos -= (Int64)samples << 32;

    /* AudMixBuffer's 32->48 kHz converter consumes input pairs and Flush
       requires an even output count.  Four-sample batches satisfy both;
       retain at most three samples for the following frame. */
    {
        int nFlush = nOut & ~3;
        s_NesPendingCount = nOut - nFlush;
        for (i = 0; i < s_NesPendingCount; i++)
            s_NesPending[i] = s_NesOut[nFlush + i];
        if (nFlush > 0)
            pMix->OutputSamplesMono(s_NesOut, nFlush);
    }

    pMix->Flush();
}

void InfoNES_MessageBox( const char *pszMsg, ... )
{
    va_list ap;
    char Buf[1024];

    va_start(ap, pszMsg);
    vsnprintf(Buf, sizeof(Buf), pszMsg, ap);
    va_end(ap);

    printf("[InfoNES] %s\n", Buf);
}
