/* AURORA_PICODRIVE_STAGE2 - Emu::System wrapper */
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "segasystem.h"
#include "sega/picodrive/picodrive_bridge.h"

extern SegaRom *_pSegaRom;

SegaSystem::SegaSystem()
{
    m_pSegaRom = NULL;
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_uFrame = 0;
    m_uLine = 0;
}

SegaSystem::~SegaSystem()
{
    PicoDriveBridge_Shutdown();
}

void SegaSystem::SetRom(Emu::Rom *pRom)
{
    m_pSegaRom = NULL;
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_uFrame = m_uLine = 0;

    /* A null SetRom is Aurora's real system-unload path. Fully deinit the
     * embedded core so its ROM/vout allocations are returned before a SNES
     * game starts. This is intentionally stronger than retro_unload_game(),
     * which PicoDrive currently leaves empty. */
    if (!pRom)
    {
        PicoDriveBridge_Shutdown();
        return;
    }

    PicoDriveBridge_UnloadGame();

    if (pRom != _pSegaRom)
    {
        printf("[SegaSystem] rejected non-SegaRom object\n");
        return;
    }

    SegaRom *rom = (SegaRom *)pRom;
    if (!rom->GetData() || !rom->GetBytes())
        return;

    const char *name = rom->GetSourceName();
    if (!name || !*name)
        name = "game.md";

    if (!PicoDriveBridge_LoadGame(
            rom->GetData(), (size_t)rom->GetBytes(),
            (size_t)rom->GetCapacity(), name))
    {
        printf("[SegaSystem/PicoDrive] LOAD FAILED\n");
        return;
    }

    m_pSegaRom = rom;
    m_bRomReady = TRUE;
    printf("[SegaSystem/PicoDrive] LOAD OK; SRAM=%d\n",
           PicoDriveBridge_GetSRAMBytes());
}

/* AURORA_SNES9X2010_V6_CD_SRAM_NOTICES_20260824 */
Bool SegaSystem::LoadDisc(const Char *pPath, const Char *pSystemPath)
{
    m_pSegaRom = NULL;
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_uFrame = m_uLine = 0;

    PicoDriveBridge_UnloadGame();
    if (!PicoDriveBridge_LoadDisc(pPath, pSystemPath))
    {
        printf("[SegaSystem/PicoDrive] SEGA CD LOAD FAILED\n");
        return FALSE;
    }

    m_bRomReady = TRUE;
    printf("[SegaSystem/PicoDrive] SEGA CD LOAD OK; SRAM=%d\n",
           PicoDriveBridge_GetSRAMBytes());
    return TRUE;
}


void SegaSystem::Reset()
{
    m_uFrame = m_uLine = 0;
    if (m_bRomReady)
        PicoDriveBridge_Reset();
}

void SegaSystem::SoftReset()
{
    m_uFrame = m_uLine = 0;
    if (m_bRomReady)
        PicoDriveBridge_SoftReset();
}

void SegaSystem::ExecuteFrame(Emu::SysInputT *pInput,
                              CRenderSurface *pTarget,
                              CMixBuffer *pMixBuf,
                              ModeE eMode)
{
    (void)eMode;
    if (!m_bRomReady)
        return;

    PicoDriveBridge_RunFrame(pInput, pTarget, pMixBuf);
    ++m_uFrame;
    m_uLine = 0;
}

Int32 SegaSystem::GetStateSize()
{
    if (!m_bRomReady)
        return 0;

    if (m_nCachedStateBytes > 0)
        return m_nCachedStateBytes;

    Int32 payload = PicoDriveBridge_GetStateSize();
    if (payload <= 0 || payload > INT_MAX - (Int32)sizeof(SegaStateHeaderT))
        return 0;

    m_nCachedStateBytes = (Int32)sizeof(SegaStateHeaderT) + payload;
    return m_nCachedStateBytes;
}

void SegaSystem::SaveState(void *pState, Int32 nStateBytes)
{
    (void)SaveStateChecked(pState, nStateBytes);
}

void SegaSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    (void)RestoreStateChecked(pState, nStateBytes);
}

Bool SegaSystem::SaveStateChecked(void *pState, Int32 nStateBytes)
{
    if (!pState || !m_bRomReady)
        return FALSE;

    Int32 total = GetStateSize();
    if (total <= (Int32)sizeof(SegaStateHeaderT) || nStateBytes < total)
        return FALSE;

    SegaStateHeaderT h;
    memset(&h, 0, sizeof(h));
    memset(pState, 0, sizeof(h));

    Int32 capacity = total - (Int32)sizeof(h);
    /* retro_serialize_size() is a maximum envelope (MD is sized as if 32X
     * state may exist). Zero the unused tail before serialization so states
     * are deterministic, compress well and never persist stale heap bytes. */
    memset(((Uint8 *)pState) + sizeof(h), 0, (size_t)capacity);
    Int32 written = PicoDriveBridge_SaveState(
        ((Uint8 *)pState) + sizeof(h), capacity);
    if (written <= 0 || written != capacity)
    {
        printf("[PicoDrive/state] serialize failed: %d/%d\n",
               (int)written, (int)capacity);
        return FALSE;
    }

    h.uVersion = SEGA_STATE_VERSION;
    h.nPayloadBytes = (Uint32)written;
    h.uFrame = m_uFrame;
    h.uMagic = SEGA_STATE_MAGIC;

    /* Commit header last so a partial snapshot cannot look valid. */
    memcpy(pState, &h, sizeof(h));
    return TRUE;
}

Bool SegaSystem::RestoreStateChecked(const void *pState, Int32 nStateBytes)
{
    if (!pState || !m_bRomReady || nStateBytes < (Int32)sizeof(SegaStateHeaderT))
        return FALSE;

    SegaStateHeaderT h;
    memcpy(&h, pState, sizeof(h));

    if (h.uMagic != SEGA_STATE_MAGIC ||
        h.uVersion != SEGA_STATE_VERSION ||
        h.nPayloadBytes == 0 ||
        h.nPayloadBytes > (Uint32)(nStateBytes - (Int32)sizeof(h)))
        return FALSE;

    Int32 expected = GetStateSize();
    if (expected <= 0 ||
        (Uint32)expected != (Uint32)sizeof(h) + h.nPayloadBytes)
        return FALSE;

    if (!PicoDriveBridge_LoadState(
            ((const Uint8 *)pState) + sizeof(h),
            (Int32)h.nPayloadBytes))
        return FALSE;

    m_uFrame = h.uFrame;
    m_uLine = 0;
    return TRUE;
}

Int32 SegaSystem::GetSRAMBytes()
{
    return m_bRomReady ? PicoDriveBridge_GetSRAMBytes() : 0;
}

Uint8 *SegaSystem::GetSRAMData()
{
    return m_bRomReady ? PicoDriveBridge_GetSRAMData() : NULL;
}

const char *SegaSystem::GetString(StringE eString)
{
    switch (eString)
    {
        case STRING_SHORTNAME: return "SEGA";
        case STRING_FULLNAME:  return "SEGA / PicoDrive";
        case STRING_SRAMEXT:   return "srm";
        case STRING_STATEEXT:  return "sst";
    }
    return "";
}

Uint32 SegaSystem::GetSampleRate()
{
    return PicoDriveBridge_GetSampleRate();
}
