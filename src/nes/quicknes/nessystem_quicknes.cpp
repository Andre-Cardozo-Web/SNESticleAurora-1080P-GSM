/* SNESTICLE_QUICKNES_SYSTEM
 * NesSystem implementation backed by QuickNES.
 * The original InfoNES nessystem.cpp remains present but is not compiled
 * while this file is selected by the Makefile.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nessystem.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "nes/quicknes/quicknes_bridge.h"
#include "nes/fceumm/fceumm_bridge.h"

extern NesRom *_pNesRom;

#define QUICKNES_STATE_MAGIC   0x54534e51u /* QNST */
#define QUICKNES_STATE_VERSION 1u

struct QuicknesStateHeaderT
{
    Uint32 uMagic;
    Uint32 uVersion;
    Uint32 nPayloadBytes;
    Uint32 uFrame;
    Uint32 uLine;
    Uint32 Reserved[3];
};

/* AURORA_NES_HYBRID_FCEUMM_V1
 *
 * QuickNES is still the default/fast path. FCEUmm is instantiated only
 * for a cartridge QuickNES rejects. This extends mapper coverage without
 * changing QuickNES execution for already-supported games.
 */
#define FCEUMM_STATE_MAGIC   0x54534346u /* FCST */
#define FCEUMM_STATE_VERSION 1u

struct FceummStateHeaderT
{
    Uint32 uMagic;
    Uint32 uVersion;
    Uint32 nPayloadBytes;
    Uint32 uFrame;
    Uint32 uLine;
    Uint32 Reserved[3];
};

enum NesBackendE
{
    NES_BACKEND_NONE = 0,
    NES_BACKEND_QUICKNES,
    NES_BACKEND_FCEUMM
};

static NesBackendE s_eNesBackend = NES_BACKEND_NONE;


NesSystem::NesSystem()
{
    m_pNesRom = NULL;
    m_pNesDisk = NULL;
    m_pCHRRam = NULL;
    m_bInitialized = FALSE;
    m_bRomReady = FALSE;
    m_uFrameTick = 0;
    m_uFrame = 0;
    m_uLine = 0;
    s_eNesBackend = NES_BACKEND_NONE;
}

NesSystem::~NesSystem()
{
    QuicknesBridge_Shutdown();
    FceummBridge_Shutdown();
    s_eNesBackend = NES_BACKEND_NONE;

    if (m_pCHRRam)
    {
        free(m_pCHRRam);
        m_pCHRRam = NULL;
    }
}

void NesSystem::SetRom(Emu::Rom *pRom)
{
    m_pNesRom = NULL;
    m_pNesDisk = NULL;
    m_bRomReady = FALSE;
    m_uFrameTick = m_uFrame = m_uLine = 0;

    QuicknesBridge_UnloadGame();
    FceummBridge_UnloadGame();
    s_eNesBackend = NES_BACKEND_NONE;

    if (!pRom)
        return;

    if (pRom != _pNesRom)
    {
        printf("[NesSystem/hybrid] FDS/non-cartridge unsupported\n");
        return;
    }

    NesRom *rom = (NesRom *)pRom;
    Uint8 *data = rom->GetData();
    Uint32 bytes = rom->GetBytes();

    if (!data || bytes < 16 ||
        data[0] != 'N' || data[1] != 'E' ||
        data[2] != 'S' || data[3] != 0x1a)
    {
        printf("[NesSystem/hybrid] invalid iNES/NES2 ROM (%u bytes)\n",
               (unsigned)bytes);
        return;
    }

    /* Fast path: preserve current QuickNES behavior for every ROM it knows. */
    if (QuicknesBridge_LoadGame(data, (size_t)bytes, NULL))
    {
        s_eNesBackend = NES_BACKEND_QUICKNES;
        printf("[NesSystem/hybrid] mapper %u -> QuickNES\n",
               (unsigned)rom->GetMapperNumber());
    }
    else
    {
        /* QuickNES can leave its native emulator initialized after a rejected
         * cart. Unload it before giving the exact same raw image to FCEUmm. */
        QuicknesBridge_UnloadGame();

        printf("[NesSystem/hybrid] QuickNES rejected mapper %u; trying FCEUmm\n",
               (unsigned)rom->GetMapperNumber());

        if (!FceummBridge_LoadGame(data, bytes, NULL))
        {
            printf("[NesSystem/hybrid] LOAD FAILED in both NES cores\n");
            return;
        }

        s_eNesBackend = NES_BACKEND_FCEUMM;
        printf("[NesSystem/hybrid] mapper %u -> FCEUmm fallback\n",
               (unsigned)rom->GetMapperNumber());
    }

    m_pNesRom = rom;
    m_bInitialized = TRUE;
    m_bRomReady = TRUE;

    printf("[NesSystem/hybrid] LOAD OK; backend=%s SRAM=%d\n",
           s_eNesBackend == NES_BACKEND_QUICKNES ? "QuickNES" : "FCEUmm",
           GetSRAMBytes());
}

void NesSystem::Reset()
{
    m_uFrameTick = m_uFrame = m_uLine = 0;

    if (!m_bRomReady)
        return;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        QuicknesBridge_Reset();
    else if (s_eNesBackend == NES_BACKEND_FCEUMM)
        FceummBridge_Reset();
}

void NesSystem::SoftReset()
{
    m_uFrameTick = m_uFrame = m_uLine = 0;

    if (!m_bRomReady)
        return;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        QuicknesBridge_SoftReset();
    else if (s_eNesBackend == NES_BACKEND_FCEUMM)
        FceummBridge_Reset();
}

void NesSystem::ExecuteFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf,
                             ModeE eMode)
{
    (void)eMode;

    if (!m_bRomReady || !pTarget)
    {
        if (pTarget)
            DiagnosticPaint(pTarget);
        ++m_uFrameTick;
        ++m_uFrame;
        return;
    }

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        QuicknesBridge_RunFrame(pInput, pTarget, pMixBuf);
    else if (s_eNesBackend == NES_BACKEND_FCEUMM)
        FceummBridge_RunFrame(pInput, pTarget, pMixBuf);

    ++m_uFrameTick;
    ++m_uFrame;
    m_uLine = 0;
}

Int32 NesSystem::GetStateSize()
{
    if (!m_bRomReady)
        return 0;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
    {
        Int32 bytes = (Int32)(
            sizeof(QuicknesStateHeaderT) + QUICKNES_STATE_CAPACITY
        );
        return bytes <= (Int32)sizeof(NesStateT) ? bytes : 0;
    }

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
    {
        Int32 payload = FceummBridge_GetStateSize();
        if (payload <= 0)
            return 0;

        unsigned long total =
            sizeof(FceummStateHeaderT) + (unsigned long)payload;

        /* _NesState in mainloop_state.cpp is still a NesStateT. Refuse a
         * pathological core state rather than ever writing past it. */
        if (total > sizeof(NesStateT))
        {
            printf("[FCEUmm/state] state too large: %lu > %u\n",
                   total, (unsigned)sizeof(NesStateT));
            return 0;
        }

        return (Int32)total;
    }

    return 0;
}

void NesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    Int32 required = GetStateSize();
    if (pState && required > 0 && nStateBytes >= required)
        SaveState((NesStateT *)pState);
}

void NesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    Int32 required = GetStateSize();
    if (pState && required > 0 && nStateBytes >= required)
        RestoreState((NesStateT *)pState);
}

void NesSystem::SaveState(NesStateT *pState)
{
    if (!pState || !m_bRomReady)
        return;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
    {
        const size_t head = sizeof(QuicknesStateHeaderT);
        const Int32 stateBytes = (Int32)(
            head + QUICKNES_STATE_CAPACITY
        );

        memset(pState, 0, (size_t)stateBytes);

        int payload = QuicknesBridge_SaveState(
            ((Uint8 *)pState) + head,
            QUICKNES_STATE_CAPACITY
        );

        if (payload <= 0 || payload > QUICKNES_STATE_CAPACITY)
        {
            memset(pState, 0, (size_t)stateBytes);
            printf("[QuickNES/state] serialize failed\n");
            return;
        }

        QuicknesStateHeaderT h;
        memset(&h, 0, sizeof(h));
        h.uMagic = QUICKNES_STATE_MAGIC;
        h.uVersion = QUICKNES_STATE_VERSION;
        h.nPayloadBytes = (Uint32)payload;
        h.uFrame = m_uFrame;
        h.uLine = m_uLine;

        /* Header LAST: a failed/partial snapshot never looks valid. */
        memcpy(pState, &h, sizeof(h));

        printf("[QuickNES/state] snapshot committed: native=%d envelope=%d\n",
               payload, (int)stateBytes);
        return;
    }

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
    {
        Int32 payload = FceummBridge_GetStateSize();
        if (payload <= 0)
            return;

        size_t head = sizeof(FceummStateHeaderT);
        size_t total = head + (size_t)payload;

        if (total > sizeof(NesStateT))
        {
            printf("[FCEUmm/state] state too large\n");
            return;
        }

        memset(pState, 0, total);

        if (!FceummBridge_SaveState(
                ((Uint8 *)pState) + head, payload))
        {
            memset(pState, 0, total);
            printf("[FCEUmm/state] serialize failed\n");
            return;
        }

        FceummStateHeaderT h;
        memset(&h, 0, sizeof(h));
        h.uMagic = FCEUMM_STATE_MAGIC;
        h.uVersion = FCEUMM_STATE_VERSION;
        h.nPayloadBytes = (Uint32)payload;
        h.uFrame = m_uFrame;
        h.uLine = m_uLine;

        memcpy(pState, &h, sizeof(h));

        printf("[FCEUmm/state] snapshot committed: native=%d envelope=%u\n",
               payload, (unsigned)total);
    }
}

Bool NesSystem::RestoreState(NesStateT *pState)
{
    if (!pState || !m_bRomReady)
        return FALSE;

    Uint32 magic = 0;
    memcpy(&magic, pState, sizeof(magic));

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
    {
        if (magic != QUICKNES_STATE_MAGIC)
            return FALSE;

        QuicknesStateHeaderT h;
        memcpy(&h, pState, sizeof(h));

        const size_t head = sizeof(h);
        const size_t stateBytes =
            head + (size_t)QUICKNES_STATE_CAPACITY;

        if (h.uVersion != QUICKNES_STATE_VERSION ||
            h.nPayloadBytes == 0 ||
            h.nPayloadBytes > QUICKNES_STATE_CAPACITY ||
            head + (size_t)h.nPayloadBytes > stateBytes)
        {
            return FALSE;
        }

        if (!QuicknesBridge_LoadState(
                ((const Uint8 *)pState) + head,
                (int)h.nPayloadBytes))
        {
            return FALSE;
        }

        m_uFrame = h.uFrame;
        m_uLine = h.uLine;
        m_uFrameTick = h.uFrame;
        return TRUE;
    }

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
    {
        if (magic != FCEUMM_STATE_MAGIC)
            return FALSE;

        FceummStateHeaderT h;
        memcpy(&h, pState, sizeof(h));

        Int32 currentPayload = FceummBridge_GetStateSize();
        size_t head = sizeof(h);

        if (h.uVersion != FCEUMM_STATE_VERSION ||
            h.nPayloadBytes == 0 ||
            currentPayload <= 0 ||
            h.nPayloadBytes > (Uint32)(sizeof(NesStateT) - head))
        {
            return FALSE;
        }

        if (!FceummBridge_LoadState(
                ((const Uint8 *)pState) + head,
                (Int32)h.nPayloadBytes))
        {
            return FALSE;
        }

        m_uFrame = h.uFrame;
        m_uLine = h.uLine;
        m_uFrameTick = h.uFrame;
        return TRUE;
    }

    return FALSE;
}

Int32 NesSystem::GetSRAMBytes()
{
    if (!m_bRomReady)
        return 0;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        return QuicknesBridge_GetSRAMBytes();

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
        return FceummBridge_GetSRAMBytes();

    return 0;
}

Uint8 *NesSystem::GetSRAMData()
{
    if (!m_bRomReady)
        return NULL;

    if (s_eNesBackend == NES_BACKEND_QUICKNES)
        return (Uint8 *)QuicknesBridge_GetSRAMData();

    if (s_eNesBackend == NES_BACKEND_FCEUMM)
        return FceummBridge_GetSRAMData();

    return NULL;
}

const char *NesSystem::GetString(StringE eString)
{
    switch (eString)
    {
        case STRING_SHORTNAME: return "NES";
        case STRING_FULLNAME:  return "Nintendo Entertainment System";
        case STRING_SRAMEXT:   return "srm";
        case STRING_STATEEXT:  return "nst";
    }
    return "";
}

Uint32 NesSystem::GetSampleRate()
{
    if (s_eNesBackend == NES_BACKEND_FCEUMM)
        return FceummBridge_GetSampleRate();

    return QuicknesBridge_GetSampleRate();
}

void NesSystem::DiagnosticPaint(CRenderSurface *pTarget)
{
    if (!pTarget) return;
    pTarget->Clear();
    PixelFormatT *fmt = pTarget->GetFormat();
    if (!fmt || fmt->uBitDepth != 32) return;

    Uint32 w = pTarget->GetWidth();
    Uint32 h = pTarget->GetHeight();
    if (!h) return;
    Uint32 band = (m_uFrameTick / 2) % h;
    for (Uint32 y = 0; y < h; ++y)
    {
        Uint8 *line = pTarget->GetLinePtr((Int32)y);
        if (!line) continue;
        for (Uint32 x = 0; x < w; ++x)
        {
            line[x * 4 + 0] = (y == band) ? 0xff : 0x10;
            line[x * 4 + 1] = (y == band) ? 0xc0 : 0x20;
            line[x * 4 + 2] = (y == band) ? 0x10 : 0x80;
            line[x * 4 + 3] = 0xff;
        }
    }
}
