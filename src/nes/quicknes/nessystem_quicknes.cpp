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
}

NesSystem::~NesSystem()
{
    QuicknesBridge_Shutdown();
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

    if (!pRom)
        return;
    if (pRom != _pNesRom)
    {
        printf("[NesSystem/QuickNES] FDS/non-cartridge unsupported\n");
        return;
    }

    NesRom *rom = (NesRom *)pRom;
    Uint8 *data = rom->GetData();
    Uint32 bytes = rom->GetBytes();
    if (!data || bytes < 16 || data[0] != 'N' || data[1] != 'E' ||
        data[2] != 'S' || data[3] != 0x1a)
    {
        printf("[NesSystem/QuickNES] invalid iNES ROM (%u bytes)\n", (unsigned)bytes);
        return;
    }

    if (!QuicknesBridge_LoadGame(data, (size_t)bytes, NULL))
    {
        printf("[NesSystem/QuickNES] LOAD FAILED\n");
        return;
    }

    m_pNesRom = rom;
    m_bInitialized = TRUE;
    m_bRomReady = TRUE;
    printf("[NesSystem/QuickNES] LOAD OK; SRAM=%d\n", QuicknesBridge_GetSRAMBytes());
}

void NesSystem::Reset()
{
    m_uFrameTick = m_uFrame = m_uLine = 0;
    if (m_bRomReady) QuicknesBridge_Reset();
}

void NesSystem::SoftReset()
{
    m_uFrameTick = m_uFrame = m_uLine = 0;
    if (m_bRomReady) QuicknesBridge_SoftReset();
}

void NesSystem::ExecuteFrame(Emu::SysInputT *pInput,
                             CRenderSurface *pTarget,
                             CMixBuffer *pMixBuf,
                             ModeE eMode)
{
    (void)eMode;
    if (!m_bRomReady || !pTarget)
    {
        if (pTarget) DiagnosticPaint(pTarget);
        ++m_uFrameTick;
        ++m_uFrame;
        return;
    }
    QuicknesBridge_RunFrame(pInput, pTarget, pMixBuf);
    ++m_uFrameTick;
    ++m_uFrame;
    m_uLine = 0;
}

Int32 NesSystem::GetStateSize()
{
    return (Int32)sizeof(NesStateT);
}

void NesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    if (pState && nStateBytes >= (Int32)sizeof(NesStateT))
        SaveState((NesStateT *)pState);
}

void NesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    if (pState && nStateBytes == (Int32)sizeof(NesStateT))
        RestoreState((NesStateT *)pState);
}

void NesSystem::SaveState(NesStateT *pState)
{
    if (!pState) return;
    memset(pState, 0, sizeof(*pState));
    if (!m_bRomReady) return;

    int payload = QuicknesBridge_GetStateSize();
    const size_t head = sizeof(QuicknesStateHeaderT);
    if (payload <= 0 || head + (size_t)payload > sizeof(*pState))
    {
        printf("[QuickNES/state] payload does not fit NesStateT: %d / %u\n",
               payload, (unsigned)(sizeof(*pState) - head));
        return;
    }

    QuicknesStateHeaderT h;
    memset(&h, 0, sizeof(h));
    h.uMagic = QUICKNES_STATE_MAGIC;
    h.uVersion = QUICKNES_STATE_VERSION;
    h.nPayloadBytes = (Uint32)payload;
    h.uFrame = m_uFrame;
    h.uLine = m_uLine;
    memcpy(pState, &h, sizeof(h));

    if (!QuicknesBridge_SaveState(((Uint8 *)pState) + head, payload))
    {
        memset(pState, 0, sizeof(*pState));
        printf("[QuickNES/state] serialize failed\n");
    }
}

Bool NesSystem::RestoreState(NesStateT *pState)
{
    if (!pState || !m_bRomReady) return FALSE;
    QuicknesStateHeaderT h;
    memcpy(&h, pState, sizeof(h));
    const size_t head = sizeof(h);
    if (h.uMagic != QUICKNES_STATE_MAGIC ||
        h.uVersion != QUICKNES_STATE_VERSION ||
        h.nPayloadBytes == 0 ||
        head + (size_t)h.nPayloadBytes > sizeof(*pState))
        return FALSE;

    if (!QuicknesBridge_LoadState(((const Uint8 *)pState) + head,
                                  (int)h.nPayloadBytes))
        return FALSE;

    m_uFrame = h.uFrame;
    m_uLine = h.uLine;
    m_uFrameTick = h.uFrame;
    return TRUE;
}

Int32 NesSystem::GetSRAMBytes()
{
    return m_bRomReady ? QuicknesBridge_GetSRAMBytes() : 0;
}

Uint8 *NesSystem::GetSRAMData()
{
    return GetSRAMBytes() > 0 ? (Uint8 *)QuicknesBridge_GetSRAMData() : NULL;
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
