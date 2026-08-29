/* AURORA_FCEUMM_FDS_V0_6_RUNTIME */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "nes/fceumm/fdssystem.h"
#include "nes/fceumm/fceumm_fds_bridge.h"
#include "miniz.h"

/* AURORA_FDS_V4_ZIP_INSPECT_20260828 */
static Bool fdsInspectMemory(const Uint8 *data, Uint32 bytes,
                             Uint32 *contentCRC,
                             unsigned *totalSides)
{
    const Uint32 sideBytes = 65500U;
    const Uint32 maxSides = 8U;
    unsigned sides;

    if (!data || !contentCRC || !totalSides ||
        bytes < sideBytes || bytes > 16U + maxSides * sideBytes)
        return FALSE;

    if (bytes >= 16U && !memcmp(data, "FDS\x1a", 4))
    {
        sides = data[4];
        if (sides < 1U) sides = 1U;
        if (sides > maxSides) sides = maxSides;
        if (bytes != 16U + sides * sideBytes)
            return FALSE;
    }
    else if (bytes >= 16U &&
             !memcmp(data + 1, "*NINTENDO-HVC*", 14))
    {
        if ((bytes % sideBytes) != 0U)
            return FALSE;
        sides = bytes / sideBytes;
        if (sides < 1U || sides > maxSides)
            return FALSE;
    }
    else
    {
        return FALSE;
    }

    *contentCRC = (Uint32)mz_crc32(
        MZ_CRC32_INIT, data, (size_t)bytes);
    *totalSides = sides;
    return TRUE;
}


static Bool fdsInspectContent(const Char *path,
                              Uint32 *contentBytes,
                              Uint32 *contentCRC,
                              unsigned *totalSides)
{
    FILE *fp;
    Uint8 header[16];
    Uint8 buffer[4096];
    size_t got;
    unsigned long long bytes = 0;
    mz_ulong crc = MZ_CRC32_INIT;
    unsigned sides = 0;

    if (!path || !*path || !contentBytes || !contentCRC || !totalSides)
        return FALSE;

    fp = fopen(path, "rb");
    if (!fp)
        return FALSE;

    got = fread(header, 1, sizeof(header), fp);
    if (got != sizeof(header))
    {
        fclose(fp);
        return FALSE;
    }

    if (!memcmp(header, "FDS\x1a", 4))
    {
        sides = header[4];
    }
    else if (!memcmp(header + 1, "*NINTENDO-HVC*", 14))
    {
        if (fseek(fp, 0, SEEK_END) != 0)
        {
            fclose(fp);
            return FALSE;
        }
        long rawBytes = ftell(fp);
        if (rawBytes < 0)
        {
            fclose(fp);
            return FALSE;
        }
        if (rawBytes < 65500)
            rawBytes = 65500;
        sides = (unsigned)(rawBytes / 65500);
    }
    else
    {
        fclose(fp);
        return FALSE;
    }

    if (sides > 8) sides = 8;
    if (sides < 1) sides = 1;

    if (fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return FALSE;
    }
    while ((got = fread(buffer, 1, sizeof(buffer), fp)) != 0)
    {
        crc = mz_crc32(crc, buffer, got);
        bytes += got;
        if (bytes > 0xffffffffULL)
        {
            fclose(fp);
            return FALSE;
        }
    }
    if (ferror(fp) || bytes == 0)
    {
        fclose(fp);
        return FALSE;
    }
    fclose(fp);

    *contentBytes = (Uint32)bytes;
    *contentCRC = (Uint32)crc;
    *totalSides = sides;
    return TRUE;
}

FdsSystem::FdsSystem()
{
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_nContentBytes = 0;
    m_uContentCRC = 0;
    m_uFrame = 0;
    m_uLine = 0;
}

FdsSystem::~FdsSystem()
{
    FceummFdsBridge_Shutdown();
}

void FdsSystem::SetRom(Emu::Rom *pRom)
{
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_nContentBytes = 0;
    m_uContentCRC = 0;
    m_uFrame = m_uLine = 0;
    FceummFdsBridge_UnloadGame();
    if (pRom)
        printf("[FdsSystem] path-only FDS core rejected Emu::Rom object\n");
}

Bool FdsSystem::LoadDisk(const Char *path, const Char *systemPath)
{
    Uint32 contentBytes = 0;
    Uint32 contentCRC = 0;
    unsigned totalSides = 0;

    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_nContentBytes = 0;
    m_uContentCRC = 0;
    m_uFrame = m_uLine = 0;
    FceummFdsBridge_UnloadGame();

    /* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827: loader proven; persistent frontend breadcrumbs removed. */
    if (!fdsInspectContent(path, &contentBytes, &contentCRC, &totalSides))
    {
        printf("[FdsSystem] invalid/unreadable FDS image: %s\n",
               path ? path : "(null)");
        return FALSE;
    }

    if (!FceummFdsBridge_LoadDisk(path, systemPath, totalSides))
        return FALSE;

    m_nContentBytes = contentBytes;
    m_uContentCRC = contentCRC;
    m_bRomReady = TRUE;
    printf("[FdsSystem] identity bytes=%u crc=%08X sides=%u\n",
           (unsigned)m_nContentBytes, (unsigned)m_uContentCRC, totalSides);
    return TRUE;
}

/* AURORA_FDS_V4_ZIP_SYSTEM_MEMORY_20260828 */
Bool FdsSystem::LoadDiskMemory(const void *data, Uint32 bytes,
                               const Char *contentName,
                               const Char *systemPath)
{
    Uint32 contentCRC = 0;
    unsigned totalSides = 0;

    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_nContentBytes = 0;
    m_uContentCRC = 0;
    m_uFrame = m_uLine = 0;
    FceummFdsBridge_UnloadGame();

    if (!contentName || !*contentName || !systemPath || !*systemPath ||
        !fdsInspectMemory((const Uint8 *)data, bytes,
                          &contentCRC, &totalSides))
        return FALSE;

    if (!FceummFdsBridge_LoadDiskMemory(
            data, bytes, contentName, systemPath, totalSides))
        return FALSE;

    m_nContentBytes = bytes;
    m_uContentCRC = contentCRC;
    m_bRomReady = TRUE;
    printf("[FdsSystem] memory identity bytes=%u crc=%08X sides=%u\n",
           (unsigned)m_nContentBytes, (unsigned)m_uContentCRC, totalSides);
    return TRUE;
}

void FdsSystem::Reset()
{
    m_uFrame = m_uLine = 0;
    if (m_bRomReady) FceummFdsBridge_Reset();
}

void FdsSystem::SoftReset()
{
    m_uFrame = m_uLine = 0;
    if (m_bRomReady) FceummFdsBridge_SoftReset();
}

void FdsSystem::ExecuteFrame(Emu::SysInputT *input,
                             CRenderSurface *target,
                             CMixBuffer *mix,
                             ModeE mode)
{
    (void)mode;
    if (!m_bRomReady)
        return;
    FceummFdsBridge_RunFrame(input, target, mix);
    ++m_uFrame;
    m_uLine = 0;
}

Int32 FdsSystem::GetStateSize()
{
    Int32 coreBytes;
    if (!m_bRomReady)
        return 0;
    if (m_nCachedStateBytes > 0)
        return m_nCachedStateBytes;
    coreBytes = (Int32)FceummFdsBridge_GetStateSize();
    if (coreBytes <= 0 || coreBytes > INT_MAX - (Int32)sizeof(FdsStateHeaderT))
        return 0;
    m_nCachedStateBytes = (Int32)sizeof(FdsStateHeaderT) + coreBytes;
    return m_nCachedStateBytes;
}

void FdsSystem::SaveState(void *state, Int32 bytes)
{
    (void)SaveStateChecked(state, bytes);
}

void FdsSystem::RestoreState(void *state, Int32 bytes)
{
    (void)RestoreStateChecked(state, bytes);
}

Bool FdsSystem::SaveStateChecked(void *state, Int32 bytes)
{
    Int32 total = GetStateSize();
    Int32 coreBytes;
    Uint8 *payload;
    FdsStateHeaderT header;
    unsigned selectedSide = 0;
    unsigned swapFrames = 0;
    unsigned swapTarget = 0;
    bool inserted = false;

    if (!state || !m_bRomReady || total <= (Int32)sizeof(header) || bytes < total)
        return FALSE;

    coreBytes = total - (Int32)sizeof(header);
    /* AURORA_V3_SAFE_FDS_STATE_NO_FULL_CLEAR_20260828 */
    memset(&header, 0, sizeof(header));
    memcpy(state, &header, sizeof(header));
    payload = ((Uint8 *)state) + sizeof(header);

    FceummFdsBridge_GetDriveState(
        &selectedSide, &inserted, &swapFrames, &swapTarget);
    if (FceummFdsBridge_SaveState(payload, coreBytes) != coreBytes)
        return FALSE;

    header.uMagic = FDS_STATE_MAGIC;
    header.uVersion = FDS_STATE_VERSION;
    header.nPayloadBytes = (Uint32)coreBytes;
    header.uFrame = m_uFrame;
    header.uSelectedSide = selectedSide;
    header.bDiskInserted = inserted ? 1u : 0u;
    header.nSwapFramesRemaining = swapFrames;
    header.uSwapTargetSide = swapTarget;

    /* Commit wrapper header last, after the core payload is complete. */
    memcpy(state, &header, sizeof(header));
    return TRUE;
}

Bool FdsSystem::RestoreStateChecked(const void *state, Int32 bytes)
{
    FdsStateHeaderT header;
    Int32 total = GetStateSize();
    Int32 coreBytes;
    const Uint8 *payload;

    if (!state || !m_bRomReady || bytes != total ||
        total <= (Int32)sizeof(header))
        return FALSE;

    memcpy(&header, state, sizeof(header));
    coreBytes = total - (Int32)sizeof(header);
    if (header.uMagic != FDS_STATE_MAGIC ||
        header.uVersion != FDS_STATE_VERSION ||
        header.nPayloadBytes != (Uint32)coreBytes ||
        header.bDiskInserted > 1u ||
        header.nSwapFramesRemaining > 60u)
        return FALSE;

    payload = ((const Uint8 *)state) + sizeof(header);
    if (!FceummFdsBridge_LoadState(payload, coreBytes))
        return FALSE;
    if (!FceummFdsBridge_SetDriveState(
            header.uSelectedSide,
            header.bDiskInserted != 0,
            header.nSwapFramesRemaining,
            header.uSwapTargetSide))
        return FALSE;

    m_uFrame = header.uFrame;
    m_uLine = 0;
    return TRUE;
}

Int32 FdsSystem::GetSRAMBytes() { return 0; }
Uint8 *FdsSystem::GetSRAMData() { return NULL; }

const char *FdsSystem::GetString(StringE value)
{
    switch (value)
    {
        case STRING_SHORTNAME: return "FDS";
        case STRING_FULLNAME:  return "Famicom Disk System / FCEUmm";
        case STRING_SRAMEXT:   return "srm";
        case STRING_STATEEXT:  return "fst";
    }
    return "";
}

Uint32 FdsSystem::GetSampleRate()
{
    return FceummFdsBridge_GetSampleRate();
}

