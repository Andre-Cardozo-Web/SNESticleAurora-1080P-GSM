/* AURORA_SNES9X2010_V1 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "snes9x2010system.h"
#include "snes/snes9x2010/snes9x2010_bridge.h"

extern Snes9x2010Rom *_pSnes9x2010Rom;

Snes9x2010System::Snes9x2010System()
{
    m_pRom = NULL;
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_uFrame = m_uLine = 0;
}

Snes9x2010System::~Snes9x2010System()
{
    Snes9x2010Bridge_Shutdown();
}

void Snes9x2010System::SetRom(Emu::Rom *pRom)
{
    m_pRom = NULL;
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_uFrame = m_uLine = 0;

    if (!pRom)
    {
        Snes9x2010Bridge_Shutdown();
        return;
    }

    Snes9x2010Bridge_UnloadGame();
    if (pRom != _pSnes9x2010Rom)
    {
        printf("[Snes9x2010System] rejected non-Snes9x2010Rom object\n");
        return;
    }

    Snes9x2010Rom *rom = (Snes9x2010Rom *)pRom;
    if (!rom->GetData() || !rom->GetBytes())
        return;

    const char *name = rom->GetSourceName();
    if (!name || !*name)
        name = "game.sfc";

    if (!Snes9x2010Bridge_LoadGame(
            rom->GetData(), (size_t)rom->GetBytes(),
            (size_t)rom->GetCapacity(), name))
    {
        printf("[Snes9x2010System] LOAD FAILED\n");
        return;
    }

    m_pRom = rom;
    m_bRomReady = TRUE;
    printf("[Snes9x2010System] LOAD OK; SRAM=%d\n",
           Snes9x2010Bridge_GetSRAMBytes());
}

void Snes9x2010System::Reset()
{
    m_uFrame = m_uLine = 0;
    if (m_bRomReady)
        Snes9x2010Bridge_Reset();
}

void Snes9x2010System::SoftReset()
{
    m_uFrame = m_uLine = 0;
    if (m_bRomReady)
        Snes9x2010Bridge_SoftReset();
}

void Snes9x2010System::ExecuteFrame(Emu::SysInputT *pInput,
                                    CRenderSurface *pSurface,
                                    CMixBuffer *pMixBuffer,
                                    ModeE eMode)
{
    (void)eMode;
    if (!m_bRomReady)
        return;
    Snes9x2010Bridge_RunFrame(pInput, pSurface, pMixBuffer);
    ++m_uFrame;
    m_uLine = 0;
}

Int32 Snes9x2010System::GetStateSize()
{
    if (!m_bRomReady)
        return 0;
    if (m_nCachedStateBytes > 0)
        return m_nCachedStateBytes;

    Int32 payload = Snes9x2010Bridge_GetStateSize();
    if (payload <= 0 || payload > INT_MAX - (Int32)sizeof(Snes9x2010StateHeaderT))
        return 0;
    m_nCachedStateBytes = (Int32)sizeof(Snes9x2010StateHeaderT) + payload;
    return m_nCachedStateBytes;
}

void Snes9x2010System::SaveState(void *pState, Int32 nBytes)
{
    (void)SaveStateChecked(pState, nBytes);
}

void Snes9x2010System::RestoreState(void *pState, Int32 nBytes)
{
    (void)RestoreStateChecked(pState, nBytes);
}

Bool Snes9x2010System::SaveStateChecked(void *pState, Int32 nBytes)
{
    if (!pState || !m_bRomReady)
        return FALSE;

    Int32 total = GetStateSize();
    if (total <= (Int32)sizeof(Snes9x2010StateHeaderT) || nBytes < total)
        return FALSE;

    Snes9x2010StateHeaderT h;
    memset(&h, 0, sizeof(h));
    Int32 payloadBytes = total - (Int32)sizeof(h);
    Uint8 *payload = ((Uint8 *)pState) + sizeof(h);
    memset(payload, 0, (size_t)payloadBytes);

    Int32 wrote = Snes9x2010Bridge_SaveState(payload, payloadBytes);
    if (wrote != payloadBytes)
        return FALSE;

    h.uMagic = SNES9X2010_STATE_MAGIC;
    h.uVersion = SNES9X2010_STATE_VERSION;
    h.nPayloadBytes = (Uint32)wrote;
    h.uFrame = m_uFrame;
    memcpy(pState, &h, sizeof(h));
    return TRUE;
}

Bool Snes9x2010System::RestoreStateChecked(const void *pState, Int32 nBytes)
{
    if (!pState || !m_bRomReady || nBytes < (Int32)sizeof(Snes9x2010StateHeaderT))
        return FALSE;

    Snes9x2010StateHeaderT h;
    memcpy(&h, pState, sizeof(h));
    if (h.uMagic != SNES9X2010_STATE_MAGIC ||
        h.uVersion != SNES9X2010_STATE_VERSION ||
        !h.nPayloadBytes ||
        h.nPayloadBytes > (Uint32)(nBytes - (Int32)sizeof(h)))
        return FALSE;

    Int32 expected = GetStateSize();
    if (expected <= 0 ||
        (Uint32)expected != (Uint32)sizeof(h) + h.nPayloadBytes)
        return FALSE;

    if (!Snes9x2010Bridge_LoadState(
            ((const Uint8 *)pState) + sizeof(h), (Int32)h.nPayloadBytes))
        return FALSE;

    m_uFrame = h.uFrame;
    m_uLine = 0;
    return TRUE;
}

Int32 Snes9x2010System::GetSRAMBytes()
{
    return m_bRomReady ? Snes9x2010Bridge_GetSRAMBytes() : 0;
}

Uint8 *Snes9x2010System::GetSRAMData()
{
    return m_bRomReady ? Snes9x2010Bridge_GetSRAMData() : NULL;
}

const char *Snes9x2010System::GetString(StringE eString)
{
    switch (eString)
    {
        case STRING_SHORTNAME: return "SNES9x";
        case STRING_FULLNAME:  return "SNES / Snes9x 2010";
        case STRING_SRAMEXT:   return "srm";
        case STRING_STATEEXT:  return "s9s";
    }
    return "";
}

Uint32 Snes9x2010System::GetSampleRate()
{
    return Snes9x2010Bridge_GetSampleRate();
}
