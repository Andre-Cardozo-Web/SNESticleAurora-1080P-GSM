#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "snswc.h"

/* AURORA_SWC_FLOPPY_V1_20260831 */
/* AURORA_SWC_FLOPPY_V2_20260831: read-only media + FDC hardening. */

SNSuperWildCard::SNSuperWildCard()
{
    m_bActive = FALSE;
    m_pDRAM = NULL;
    m_pFirmware = NULL;
    m_nFirmwareBytes = 0;
    m_pCartRom = NULL; /* AURORA_SWC_FLOPPY_V5_20260831 */
    m_nCartBytes = 0;
    m_iCartMapping = 0;
    m_pDisk = NULL;
    m_DiskPath[0] = 0;
    m_LastError[0] = 0;
    m_bDiskWritable = FALSE;
    m_bDiskDirty = FALSE; /* AURORA_SWC_MEGA_V9_20260831 */
    m_bDiskChanged = FALSE;
    m_uIndexPollCounter = 0; /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831 */
    m_nTracks = m_nHeads = m_nSectorsPerTrack = 0;
    Reset();
}

SNSuperWildCard::~SNSuperWildCard()
{
    Shutdown();
}

void SNSuperWildCard::SetError(const Char *pText)
{
    snprintf(m_LastError, sizeof(m_LastError), "%s",
             pText ? pText : "unknown SWC error");
}

Bool SNSuperWildCard::DetectGeometry(long nBytes)
{
    struct GeometryT { long bytes; Int32 tracks, heads, spt; };
    static const GeometryT kGeometry[] =
    {
        {  368640L, 40, 2,  9 }, /* 360 KiB */
        {  737280L, 80, 2,  9 }, /* 720 KiB */
        {  819200L, 80, 2, 10 }, /* 800 KiB */
        { 1228800L, 80, 2, 15 }, /* 1.2 MiB */
        { 1474560L, 80, 2, 18 }, /* 1.44 MiB */
        { 1638400L, 80, 2, 20 }, /* 1.60 MiB */
        { 1699840L, 83, 2, 20 }, /* 1.66 MiB copier format */
        { 1720320L, 80, 2, 21 }  /* 1.68 MiB / 21-SPT */
    };

    for (Uint32 i = 0; i < sizeof(kGeometry) / sizeof(kGeometry[0]); ++i)
    {
        if (nBytes == kGeometry[i].bytes)
        {
            m_nTracks = kGeometry[i].tracks;
            m_nHeads = kGeometry[i].heads;
            m_nSectorsPerTrack = kGeometry[i].spt;
            return TRUE;
        }
    }

    SetError("unsupported floppy geometry (raw 360K/720K/800K/1.2M/1.44M/1.6M/1.66M/1.68M only)");
    return FALSE;
}

Bool SNSuperWildCard::LoadFirmware(const Char *pPath)
{
    FILE *pFile;
    long nBytes;
    long nSkip = 0;

    pFile = fopen(pPath, "rb");
    if (!pFile)
    {
        SetError("cannot open SWC firmware");
        return FALSE;
    }

    if (fseek(pFile, 0, SEEK_END) != 0)
    {
        fclose(pFile);
        SetError("cannot size SWC firmware");
        return FALSE;
    }
    nBytes = ftell(pFile);

    if (nBytes == SWC_FIRMWARE_BYTES + 512)
    {
        nSkip = 512;
        nBytes -= 512;
    }

    if (nBytes != SWC_FIRMWARE_BYTES)
    {
        fclose(pFile);
        SetError("Requires classic 16 KiB Super Wild Card BIOS");
        return FALSE;
    }

    m_pFirmware = (Uint8 *)malloc(SWC_FIRMWARE_BYTES);
    if (!m_pFirmware)
    {
        fclose(pFile);
        SetError("not enough EE memory for SWC firmware");
        return FALSE;
    }

    if (fseek(pFile, nSkip, SEEK_SET) != 0 ||
        fread(m_pFirmware, 1, SWC_FIRMWARE_BYTES, pFile) != SWC_FIRMWARE_BYTES)
    {
        fclose(pFile);
        free(m_pFirmware);
        m_pFirmware = NULL;
        SetError("short read while loading SWC firmware");
        return FALSE;
    }

    fclose(pFile);

    /* AURORA_SWC_FLOPPY_V3_20260831
     * Fail closed on an obviously wrong 16 KiB file accidentally named as
     * firmware. The cold reset vector is at $00:FFFC => firmware + $1FFC. */
    {
        Uint16 resetVector =
            (Uint16)m_pFirmware[0x1ffc] |
            ((Uint16)m_pFirmware[0x1ffd] << 8);
        if (resetVector == 0x0000 || resetVector == 0xffff)
        {
            free(m_pFirmware);
            m_pFirmware = NULL;
            SetError("invalid SWC firmware reset vector");
            return FALSE;
        }
    }

    m_nFirmwareBytes = SWC_FIRMWARE_BYTES;
    return TRUE;
}

Bool SNSuperWildCard::MountDisk(const Char *pDiskPath)
{
    FILE *pFile;
    long nBytes;
    Bool bWritable = TRUE;

    if (!pDiskPath || !*pDiskPath)
    {
        SetError("empty SWC floppy path");
        return FALSE;
    }

    /* AURORA_SWC_FLOPPY_V4_20260831
     * Writable raw floppy when the backing file permits it; otherwise the
     * controller naturally reports write-protect. */
    bWritable = TRUE;
    pFile = fopen(pDiskPath, "r+b");
    if (!pFile)
    {
        bWritable = FALSE;
        pFile = fopen(pDiskPath, "rb");
    }
    if (!pFile)
    {
        SetError("cannot open SWC floppy image");
        return FALSE;
    }

    if (fseek(pFile, 0, SEEK_END) != 0)
    {
        fclose(pFile);
        SetError("cannot size SWC floppy image");
        return FALSE;
    }
    nBytes = ftell(pFile);
    if (!DetectGeometry(nBytes))
    {
        fclose(pFile);
        return FALSE;
    }
    rewind(pFile);

    if (m_pDisk)
    {
        if (!FdcFlushDisk())
        {
            fclose(pFile);
            return FALSE;
        }
        fclose(m_pDisk);
    }

    m_pDisk = pFile;
    m_bDiskWritable = bWritable;
    m_bDiskDirty = FALSE; /* AURORA_SWC_MEGA_V9_20260831 */
    m_bDiskChanged = TRUE;
    m_uIndexPollCounter = 0; /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831: new medium starts at an INDEX pulse */
    snprintf(m_DiskPath, sizeof(m_DiskPath), "%s", pDiskPath);

    FdcReset(FALSE);
    return TRUE;
}

Bool SNSuperWildCard::SwapDisk(const Char *pDiskPath)
{
    Uint8 oldCylinder = m_uCylinder;
    Uint8 oldHead = m_uHead;
    Uint8 oldDrive = m_uDrive;
    Uint8 oldDOR = m_uDOR;
    Uint8 oldDCR = m_uDCR;

    if (!MountDisk(pDiskPath))
        return FALSE;

    /* AURORA_SWC_FLOPPY_V3_20260831
     * Changing media aborts an in-flight command, but is not an FDC reset. */
    m_uCylinder = oldCylinder;
    m_uHead = oldHead;
    m_uDrive = oldDrive;
    m_uDOR = oldDOR;
    m_uDCR = oldDCR;
    m_uLastST0 = (Uint8)(0x20 | (m_uHead << 2) | (m_uDrive & 3));
    m_uResetSensePending = 0;
    m_bIRQ = FALSE;
    m_bDiskChanged = TRUE;
    return TRUE;
}

Bool SNSuperWildCard::Load(const Char *pFirmwarePath, const Char *pDiskPath)
{
    Shutdown();
    m_LastError[0] = 0;

    m_pDRAM = (Uint8 *)malloc(SWC_DRAM_BYTES);
    if (!m_pDRAM)
    {
        SetError("not enough EE memory for 4 MiB SWC DRAM");
        return FALSE;
    }
    memset(m_pDRAM, 0, SWC_DRAM_BYTES);

    if (!LoadFirmware(pFirmwarePath))
    {
        Char ErrorCopy[sizeof(m_LastError)];
        snprintf(ErrorCopy, sizeof(ErrorCopy), "%s", m_LastError);
        Shutdown();
        SetError(ErrorCopy);
        return FALSE;
    }

    /* AURORA_SWC_FLOPPY_V5_20260831: allow empty drive at boot. */
    if (pDiskPath && *pDiskPath)
    {
        if (!MountDisk(pDiskPath))
        {
            Char ErrorCopy[sizeof(m_LastError)];
            snprintf(ErrorCopy, sizeof(ErrorCopy), "%s", m_LastError);
            Shutdown();
            SetError(ErrorCopy);
            return FALSE;
        }
    }

    m_bActive = TRUE;
    Reset();
    return TRUE;
}

void SNSuperWildCard::Shutdown()
{
    ClearExternalCartridge(); /* AURORA_SWC_FLOPPY_V5_20260831 */
    if (m_pDisk)
    {
        (void)FdcFlushDisk();
        fclose(m_pDisk);
        m_pDisk = NULL;
    }
    if (m_pFirmware)
    {
        free(m_pFirmware);
        m_pFirmware = NULL;
    }
    if (m_pDRAM)
    {
        free(m_pDRAM);
        m_pDRAM = NULL;
    }

    m_bActive = FALSE;
    m_nFirmwareBytes = 0;
    m_bDiskWritable = FALSE;
    m_bDiskDirty = FALSE;
    m_bDiskChanged = FALSE;
    m_uIndexPollCounter = 0; /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831 */
    m_DiskPath[0] = 0;
    m_nTracks = m_nHeads = m_nSectorsPerTrack = 0;
}

void SNSuperWildCard::Reset()
{
    m_uSelectedDRAMPage = 0;
    m_uSelectedSRAMPage = 0;
    m_uSystemMode = 0;
    m_uParallel = 0;
    m_bPageSRAM = FALSE;
    m_bCartridgeMap = FALSE;
    m_uDOR = 0;
    m_uDCR = 0;
    m_uIndexPollCounter = 0; /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831 */
    FdcReset(FALSE);
}

Bool SNSuperWildCard::FdcFlushDisk()
{
    if (!m_pDisk || !m_bDiskDirty)
        return TRUE;

    if (fflush(m_pDisk) != 0)
    {
        SetError("SWC floppy flush failed");
        return FALSE;
    }

    m_bDiskDirty = FALSE;
    return TRUE;
}

/* AURORA_SWC_MEGA_V9_20260831: command-level persistence avoids one USB flush per 512-byte sector. */

void SNSuperWildCard::FdcReset(Bool bRaiseIRQ)
{
    m_FdcPhase = FDC_PHASE_COMMAND;
    m_nCommand = 0;
    m_nCommandExpected = 0;
    m_nResult = 0;
    m_iResult = 0;
    m_iSectorByte = 0;
    m_nFormatIDBytes = 0;
    m_iFormatIDByte = 0;
    m_uCylinder = 0;
    m_uHead = 0;
    m_uDrive = 0;
    m_uLastST0 = 0x20;
    m_uResetSensePending = bRaiseIRQ ? 4 : 0;
    m_bIRQ = bRaiseIRQ;
}

Uint8 SNSuperWildCard::FdcMainStatus() const
{
    /* AURORA_SWC_MEGA_V9_20260831: uPD765 non-DMA phase bits:
     *   READ exec  = RQM|DIO|EXM|CB = F0
     *   WRITE exec = RQM|EXM|CB     = B0
     *   RESULT     = RQM|DIO|CB     = D0 (EXM must be clear)
     */
    switch (m_FdcPhase)
    {
        case FDC_PHASE_READ:
            return 0xF0;
        case FDC_PHASE_RESULT:
            return 0xD0;
        case FDC_PHASE_WRITE:
        case FDC_PHASE_FORMAT:
            return 0xB0;
        case FDC_PHASE_COMMAND:
        default:
            return m_nCommand ? 0x90 : 0x80;
    }
}

Uint8 SNSuperWildCard::FdcCommandLength(Uint8 uCommand) const
{
    switch (uCommand & 0x1F)
    {
        case 0x03: return 3;
        case 0x04: return 2;
        case 0x02: return 9; /* Read Track */
        case 0x05: return 9;
        case 0x06: return 9;
        case 0x09: return 9; /* Write Deleted Data: raw-sector alias */
        case 0x0C: return 9; /* Read Deleted Data: raw-sector alias */
        case 0x07: return 2;
        case 0x08: return 1;
        case 0x0A: return 2;
        case 0x0D: return 6;
        case 0x0F: return 3;
        case 0x10: return 1;
        default:   return 1;
    }
}

void SNSuperWildCard::FdcSetResult(const Uint8 *pData, Uint8 nBytes, Bool bIRQ)
{
    if (nBytes > sizeof(m_Result))
        nBytes = sizeof(m_Result);
    if (nBytes)
        memcpy(m_Result, pData, nBytes);
    m_nResult = nBytes;
    m_iResult = 0;
    m_FdcPhase = nBytes ? FDC_PHASE_RESULT : FDC_PHASE_COMMAND;
    m_nCommand = 0;
    m_nCommandExpected = 0;
    if (bIRQ)
        m_bIRQ = TRUE;
}

Bool SNSuperWildCard::FdcValidCHS(Uint8 c, Uint8 h, Uint8 r) const
{
    return m_pDisk &&
           c < (Uint8)m_nTracks &&
           h < (Uint8)m_nHeads &&
           r >= 1 && r <= (Uint8)m_nSectorsPerTrack;
}

long SNSuperWildCard::FdcSectorOffset(Uint8 c, Uint8 h, Uint8 r) const
{
    return (((long)c * m_nHeads + h) * m_nSectorsPerTrack + (r - 1)) *
           SWC_SECTOR_BYTES;
}

Bool SNSuperWildCard::FdcLoadCurrentSector()
{
    if (!FdcValidCHS(m_uDataC, m_uDataH, m_uDataR) || m_uDataN != 2)
        return FALSE;

    if (fseek(m_pDisk,
              FdcSectorOffset(m_uDataC, m_uDataH, m_uDataR),
              SEEK_SET) != 0)
        return FALSE;

    if (fread(m_Sector, 1, SWC_SECTOR_BYTES, m_pDisk) != SWC_SECTOR_BYTES)
        return FALSE;

    m_iSectorByte = 0;
    return TRUE;
}

Bool SNSuperWildCard::FdcStoreCurrentSector()
{
    if (!m_bDiskWritable ||
        !FdcValidCHS(m_uDataC, m_uDataH, m_uDataR) ||
        m_uDataN != 2)
        return FALSE;

    if (fseek(m_pDisk,
              FdcSectorOffset(m_uDataC, m_uDataH, m_uDataR),
              SEEK_SET) != 0)
        return FALSE;

    if (fwrite(m_Sector, 1, SWC_SECTOR_BYTES, m_pDisk) != SWC_SECTOR_BYTES)
        return FALSE;

    m_bDiskDirty = TRUE; /* AURORA_SWC_MEGA_V9_20260831 */
    return TRUE;
}

Bool SNSuperWildCard::FdcAdvanceSector()
{
    if (m_uDataR < m_uDataEOT &&
        m_uDataR < (Uint8)m_nSectorsPerTrack)
    {
        ++m_uDataR;
        return TRUE;
    }

    if (m_bDataMT && m_uDataH == 0 && m_nHeads > 1)
    {
        m_uDataH = 1;
        m_uDataR = 1;
        return TRUE;
    }
    return FALSE;
}

void SNSuperWildCard::FdcFinishRW(Bool bOK, Uint8 uST1, Uint8 uST2)
{
    Uint8 r[7];

    /* AURORA_SWC_MEGA_V9_20260831: physical IMG persistence is command-granular, not sector-granular. */
    if (m_bDiskDirty && !FdcFlushDisk())
    {
        bOK = FALSE;
        if (!uST1) uST1 = 0x20;
    }

    r[0] = bOK ? (Uint8)((m_uDataH << 2) | m_uDrive)
               : (Uint8)(0x40 | (m_uDataH << 2) | m_uDrive);
    r[1] = uST1;
    r[2] = uST2;
    r[3] = m_uDataC;
    r[4] = m_uDataH;
    r[5] = m_uDataR;
    r[6] = m_uDataN;

    m_uLastST0 = r[0];
    FdcSetResult(r, 7, TRUE);
}

void SNSuperWildCard::FdcExecuteCommand()
{
    Uint8 cmd = m_Command[0] & 0x1F;
    Uint8 r[7];

    switch (cmd)
    {
        case 0x03:
            FdcSetResult(NULL, 0, FALSE);
            return;

        case 0x04:
            m_uDrive = m_Command[1] & 3;
            m_uHead = (m_Command[1] >> 2) & 1;
            r[0] = (Uint8)(m_uDrive | (m_uHead << 2));
            if (m_nHeads > 1) r[0] |= 0x08;
            if (m_uCylinder == 0) r[0] |= 0x10;
            if (m_pDisk) r[0] |= 0x20;
            if (!m_bDiskWritable) r[0] |= 0x40;
            FdcSetResult(r, 1, FALSE);
            return;

        case 0x02: /* Read Track: sector-model approximation */
        case 0x05:
        case 0x06:
        case 0x09: /* Write Deleted Data -> raw-sector write */
        case 0x0C: /* Read Deleted Data -> raw-sector read */
            m_uDrive = m_Command[1] & 3;
            m_uDataH = m_Command[3];
            m_uHead = m_uDataH;
            m_uDataC = m_Command[2];
            m_uDataR = m_Command[4];
            m_uDataN = m_Command[5];
            m_uDataEOT = m_Command[6];
            m_bDataMT = (m_Command[0] & 0x80) ? TRUE : FALSE;
            m_uCylinder = m_uDataC;
            m_iSectorByte = 0;

            if (!FdcValidCHS(m_uDataC, m_uDataH, m_uDataR) ||
                m_uDataN != 2)
            {
                FdcFinishRW(FALSE, 0x04, 0x00);
                return;
            }

            if (cmd == 0x06 || cmd == 0x0C || cmd == 0x02)
            {
                if (!FdcLoadCurrentSector())
                {
                    FdcFinishRW(FALSE, 0x20, 0x00);
                    return;
                }
                m_FdcPhase = FDC_PHASE_READ;
            }
            else
            {
                if (!m_bDiskWritable)
                {
                    FdcFinishRW(FALSE, 0x02, 0x00);
                    return;
                }
                memset(m_Sector, 0, sizeof(m_Sector));
                m_FdcPhase = FDC_PHASE_WRITE;
            }
            m_nCommand = 0;
            m_nCommandExpected = 0;
            return;

        case 0x07:
            m_uDrive = m_Command[1] & 3;
            m_uCylinder = 0;
            m_uLastST0 = (Uint8)(0x20 | m_uDrive);
            m_bDiskChanged = FALSE;
            m_bIRQ = TRUE;
            FdcSetResult(NULL, 0, FALSE);
            return;

        case 0x08:
            /* NEC765-compatible reset completion: one pending Sense
               Interrupt Status result for each drive. */
            if (m_uResetSensePending)
            {
                Uint8 drive = (Uint8)(4 - m_uResetSensePending);
                r[0] = (Uint8)(0xC0 | (drive & 3));
                r[1] = 0;
                --m_uResetSensePending;
                m_bIRQ = m_uResetSensePending ? TRUE : FALSE;
                FdcSetResult(r, 2, FALSE);
                return;
            }

            r[0] = m_uLastST0;
            r[1] = m_uCylinder;
            m_bIRQ = FALSE;
            FdcSetResult(r, 2, FALSE);
            return;

        case 0x0A:
            m_uDrive = m_Command[1] & 3;
            m_uHead = (m_Command[1] >> 2) & 1;
            if (!m_pDisk)
            {
                m_uDataC = m_uCylinder;
                m_uDataH = m_uHead;
                m_uDataR = 1;
                m_uDataN = 2;
                FdcFinishRW(FALSE, 0x04, 0x00);
                return;
            }
            r[0] = (Uint8)((m_uHead << 2) | m_uDrive);
            r[1] = 0;
            r[2] = 0;
            r[3] = m_uCylinder;
            r[4] = m_uHead;
            r[5] = 1;
            r[6] = 2;
            FdcSetResult(r, 7, TRUE);
            return;

        case 0x0D:
            m_uDrive = m_Command[1] & 3;
            m_uHead = (m_Command[1] >> 2) & 1;
            m_uDataN = m_Command[2];
            m_uFormatSC = m_Command[3];
            m_uFormatFill = m_Command[5];
            m_nFormatIDBytes = (Uint16)m_uFormatSC * 4;
            if (m_nFormatIDBytes > SWC_MAX_FORMAT_IDS ||
                m_uDataN != 2 || !m_bDiskWritable)
            {
                m_uDataC = m_uCylinder;
                m_uDataH = m_uHead;
                m_uDataR = 1;
                FdcFinishRW(FALSE, m_bDiskWritable ? 0x04 : 0x02, 0);
                return;
            }
            m_iFormatIDByte = 0;
            m_FdcPhase = FDC_PHASE_FORMAT;
            m_nCommand = 0;
            m_nCommandExpected = 0;
            return;

        case 0x0F:
            m_uDrive = m_Command[1] & 3;
            m_uHead = (m_Command[1] >> 2) & 1;
            m_uCylinder = m_Command[2];
            if (m_uCylinder >= (Uint8)m_nTracks)
                m_uCylinder = (Uint8)(m_nTracks ? m_nTracks - 1 : 0);
            m_uLastST0 = (Uint8)(0x20 | (m_uHead << 2) | m_uDrive);
            m_bDiskChanged = FALSE;
            m_bIRQ = TRUE;
            FdcSetResult(NULL, 0, FALSE);
            return;

        case 0x10:
            r[0] = 0x90;
            FdcSetResult(r, 1, FALSE);
            return;

        default:
            r[0] = 0x80;
            FdcSetResult(r, 1, FALSE);
            return;
    }
}

Uint8 SNSuperWildCard::FdcReadData()
{
    if (m_FdcPhase == FDC_PHASE_RESULT)
    {
        /* AURORA_SWC_MEGA_V9_20260831: the 765 clears transfer-complete INT when the first
         * result byte is read. */
        if (m_iResult == 0)
            m_bIRQ = FALSE;
        Uint8 v = (m_iResult < m_nResult) ? m_Result[m_iResult++] : 0xFF;
        if (m_iResult >= m_nResult)
        {
            m_iResult = m_nResult = 0;
            m_FdcPhase = FDC_PHASE_COMMAND;
        }
        return v;
    }

    if (m_FdcPhase == FDC_PHASE_READ)
    {
        Uint8 v = m_Sector[m_iSectorByte++];
        if (m_iSectorByte >= SWC_SECTOR_BYTES)
        {
            if (FdcAdvanceSector())
            {
                if (!FdcLoadCurrentSector())
                    FdcFinishRW(FALSE, 0x20, 0);
            }
            else
            {
                FdcFinishRW(TRUE, 0, 0);
            }
        }
        return v;
    }

    return 0xFF;
}

void SNSuperWildCard::FdcWriteData(Uint8 uData)
{
    if (m_FdcPhase == FDC_PHASE_WRITE)
    {
        m_Sector[m_iSectorByte++] = uData;
        if (m_iSectorByte >= SWC_SECTOR_BYTES)
        {
            if (!FdcStoreCurrentSector())
            {
                FdcFinishRW(FALSE, m_bDiskWritable ? 0x20 : 0x02, 0);
                return;
            }

            if (FdcAdvanceSector())
            {
                m_iSectorByte = 0;
                memset(m_Sector, 0, sizeof(m_Sector));
            }
            else
            {
                FdcFinishRW(TRUE, 0, 0);
            }
        }
        return;
    }

    if (m_FdcPhase == FDC_PHASE_FORMAT)
    {
        if (m_iFormatIDByte < m_nFormatIDBytes)
            m_FormatIDs[m_iFormatIDByte++] = uData;

        if (m_iFormatIDByte >= m_nFormatIDBytes)
        {
            Bool ok = TRUE;
            Uint8 lastC = m_uCylinder, lastH = m_uHead, lastR = 1, lastN = 2;

            memset(m_Sector, m_uFormatFill, sizeof(m_Sector));
            for (Uint16 i = 0; i + 3 < m_nFormatIDBytes; i += 4)
            {
                Uint8 c = m_FormatIDs[i + 0];
                Uint8 h = m_FormatIDs[i + 1];
                Uint8 r = m_FormatIDs[i + 2];
                Uint8 n = m_FormatIDs[i + 3];

                lastC = c; lastH = h; lastR = r; lastN = n;
                if (n != 2 || !FdcValidCHS(c, h, r) ||
                    fseek(m_pDisk, FdcSectorOffset(c, h, r), SEEK_SET) != 0 ||
                    fwrite(m_Sector, 1, SWC_SECTOR_BYTES, m_pDisk) != SWC_SECTOR_BYTES)
                {
                    ok = FALSE;
                    break;
                }
                m_bDiskDirty = TRUE; /* AURORA_SWC_MEGA_V9_20260831 */
            }

            m_uDataC = lastC;
            m_uDataH = lastH;
            m_uDataR = lastR;
            m_uDataN = lastN;
            FdcFinishRW(ok, ok ? 0 : 0x20, 0);
        }
        return;
    }

    if (m_FdcPhase != FDC_PHASE_COMMAND)
        return;

    if (m_nCommand == 0)
    {
        m_nCommandExpected = FdcCommandLength(uData);
        if (!m_nCommandExpected)
            m_nCommandExpected = 1;
    }

    if (m_nCommand < sizeof(m_Command))
        m_Command[m_nCommand++] = uData;

    if (m_nCommand >= m_nCommandExpected)
        FdcExecuteCommand();
}

Uint32 SNSuperWildCard::DramOffsetMode2(Uint8 bank, Uint16 addr) const
{
    Uint32 b = (Uint32)(bank & 0x7F);
    Uint32 off;

    if (m_uParallel & 0x01)
        off = b * 0x10000u + (Uint32)addr;
    else
        off = b * 0x8000u + (Uint32)(addr - 0x8000);

    return off & (SWC_DRAM_BYTES - 1);
}


/* AURORA_SWC_FLOPPY_V5_20260831 */
Bool SNSuperWildCard::SetExternalCartridge(const Uint8 *pRom,
                                           Uint32 nRomBytes,
                                           Int32 iMapping)
{
    if (!m_bActive || !pRom || !nRomBytes ||
        (iMapping != 0 && iMapping != 1))
        return FALSE;

    m_pCartRom = pRom;
    m_nCartBytes = nRomBytes;
    m_iCartMapping = iMapping;
    return TRUE;
}

void SNSuperWildCard::ClearExternalCartridge()
{
    m_pCartRom = NULL;
    m_nCartBytes = 0;
    m_iCartMapping = 0;
}

Bool SNSuperWildCard::ReadExternalPage(Uint16 addr, Uint8 *pData) const
{
    Uint32 off;

    if (!m_pCartRom || !m_nCartBytes || !pData ||
        addr < 0xA000 || addr > 0xBFFF)
        return FALSE;

    off = m_uSelectedDRAMPage * 0x2000u +
          (Uint32)(addr - 0xA000);
    off %= m_nCartBytes;
    *pData = m_pCartRom[off];
    return TRUE;
}

Bool SNSuperWildCard::ReadExternalCartridge(Uint8 bank,
                                            Uint16 addr,
                                            Uint8 *pData) const
{
    Uint32 off;

    if (!m_pCartRom || !m_nCartBytes || !pData)
        return FALSE;

    if (m_iCartMapping == 0) /* LoROM */
    {
        if (addr < 0x8000)
            return FALSE;

        off = ((Uint32)(bank & 0x7F) << 15) |
              (Uint32)(addr & 0x7FFF);
    }
    else /* HiROM */
    {
        Bool fullBank =
            ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0);

        if (addr < 0x8000 && !fullBank)
            return FALSE;

        off = ((Uint32)(bank & 0x3F) << 16) | (Uint32)addr;
    }

    off %= m_nCartBytes;
    *pData = m_pCartRom[off];
    return TRUE;
}

Bool SNSuperWildCard::ReadMode0(Uint8 bank, Uint16 addr, Uint8 *pData,
                                Uint8 *pSRAM, Uint32 nSRAMBytes)
{
    if (addr == 0xC000)
    {
        Uint8 value = m_bIRQ ? 0x80 : 0;

        /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831
         * C000 bit 6 is the floppy INDEX signal used by the SWC BIOS as a
         * disk-insert check. A permanently-high "disk present" bit never
         * produces an edge, so the real BIOS can keep asking Insert disk...
         * even with a mounted IMG.
         *
         * Expose a short repeating pulse in the BIOS polling domain. No
         * wall-clock wait or host I/O is introduced in this hot register.
         */
        if (m_pDisk)
        {
            m_uIndexPollCounter = (m_uIndexPollCounter + 1u) & 0xFFu;
            if (m_uIndexPollCounter < 8u)
                value |= 0x40;
        }
        else
        {
            m_uIndexPollCounter = 0;
        }

        *pData = value;
        return TRUE;
    }
    if (addr == 0xC004)
    {
        *pData = FdcMainStatus();
        return TRUE;
    }
    if (addr == 0xC005)
    {
        *pData = FdcReadData();
        return TRUE;
    }
    if (addr == 0xC007)
    {
        *pData = m_bDiskChanged ? 0x80 : 0x00;
        return TRUE;
    }
    if (addr == 0xC008)
    {
        *pData = m_uParallel;
        return TRUE;
    }
    if (addr == 0xC009)
    {
        *pData = 0x80;
        return TRUE;
    }

    if (addr >= 0xE000 && bank <= 1 && m_pFirmware)
    {
        Uint32 off = ((Uint32)bank << 13) | (Uint32)(addr - 0xE000);
        if (off < m_nFirmwareBytes)
        {
            *pData = m_pFirmware[off];
            return TRUE;
        }
    }

    if (addr >= 0x8000 && addr <= 0x9FFF && m_pDRAM)
    {
        Uint32 off = (m_uSelectedDRAMPage * 0x2000u +
                      (Uint32)(addr - 0x8000)) &
                     (SWC_DRAM_BYTES - 1);
        *pData = m_pDRAM[off];
        return TRUE;
    }

    if (!m_bPageSRAM && ReadExternalPage(addr, pData))
        return TRUE;

    if (m_bPageSRAM && pSRAM && nSRAMBytes >= 0x8000)
    {
        Bool lowerHighBank =
            ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0) &&
            addr >= 0x2000 && addr <= 0x3FFF;
        Bool upperAnyBank = addr >= 0xA000 && addr <= 0xBFFF;

        if (lowerHighBank || upperAnyBank)
        {
            Uint32 off = (m_uSelectedSRAMPage & 3u) * 0x2000u +
                         (Uint32)(addr & 0x1FFF);
            *pData = pSRAM[off & 0x7FFF];
            return TRUE;
        }
    }

    return FALSE;
}

Bool SNSuperWildCard::WriteMode0(Uint8 bank, Uint16 addr, Uint8 uData,
                                 Uint8 *pSRAM, Uint32 nSRAMBytes)
{
    if (addr == 0xC002)
    {
        Uint8 old = m_uDOR;
        m_uDOR = uData;
        if (!(uData & 0x04))
            FdcReset(FALSE);
        else if (!(old & 0x04))
            FdcReset(TRUE);
        return TRUE;
    }
    if (addr == 0xC005)
    {
        FdcWriteData(uData);
        return TRUE;
    }
    if (addr == 0xC007)
    {
        m_uDCR = uData;
        return TRUE;
    }
    if (addr == 0xC008)
    {
        m_uParallel = uData & 0x03;
        return TRUE;
    }

    /* Page register identity is encoded by CPU bank + E000..E003. */
    if (addr >= 0xE000 && addr <= 0xE003)
    {
        m_uSelectedDRAMPage = (((Uint32)bank << 2) |
                               (Uint32)(addr & 3)) &
                              ((SWC_DRAM_BYTES / 0x2000u) - 1);
        m_uSelectedSRAMPage = addr & 3;
        return TRUE;
    }

    if (addr >= 0xE004 && addr <= 0xE007)
    {
        m_uSystemMode = (Uint8)(addr - 0xE004);
        return TRUE;
    }
    if (addr == 0xE008 || addr == 0xE009)
        return TRUE;

    if (addr == 0xE00C)
    {
        /* Mode 0: cartridge page at A000-BFFF.
           Modes 2/3: external cart window 20-5F/A0-DF disabled. */
        m_bPageSRAM = FALSE;
        m_bCartridgeMap = FALSE;
        return TRUE;
    }
    if (addr == 0xE00D)
    {
        /* Mode 0: SRAM page at A000-BFFF.
           Modes 2/3: external cart window 20-5F/A0-DF enabled.
           V1-V3 intentionally emulate no inserted cartridge. */
        m_bPageSRAM = TRUE;
        m_bCartridgeMap = TRUE;
        return TRUE;
    }

    if (addr >= 0x8000 && addr <= 0x9FFF && m_pDRAM)
    {
        Uint32 off = (m_uSelectedDRAMPage * 0x2000u +
                      (Uint32)(addr - 0x8000)) &
                     (SWC_DRAM_BYTES - 1);
        m_pDRAM[off] = uData;
        return TRUE;
    }

    if (!m_bPageSRAM && m_pCartRom &&
        addr >= 0xA000 && addr <= 0xBFFF)
        return TRUE; /* ROM ignores writes */

    if (m_bPageSRAM && pSRAM && nSRAMBytes >= 0x8000)
    {
        Bool lowerHighBank =
            ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0) &&
            addr >= 0x2000 && addr <= 0x3FFF;
        Bool upperAnyBank = addr >= 0xA000 && addr <= 0xBFFF;

        if (lowerHighBank || upperAnyBank)
        {
            Uint32 off = (m_uSelectedSRAMPage & 3u) * 0x2000u +
                         (Uint32)(addr & 0x1FFF);
            pSRAM[off & 0x7FFF] = uData;
            return TRUE;
        }
    }

    return FALSE;
}

Bool SNSuperWildCard::ReadEmulation(Uint8 bank, Uint16 addr, Uint8 *pData,
                                    Uint8 *pSRAM, Uint32 nSRAMBytes)
{
    Uint8 maxLoBank = (m_uSystemMode == 2) ? 0x70 : 0x6F;
    Uint8 maxHiBank = (m_uSystemMode == 2) ? 0xE0 : 0xDF;
    Bool inBankRange =
        (bank <= maxLoBank) || (bank >= 0x80 && bank <= maxHiBank);

    /* AURORA_SWC_FLOPPY_V3_20260831
     * E00D enables the real cartridge in banks 20-5F/A0-DF. Aurora V3 has
     * no inserted cartridge, so those windows are physical open bus. */
    if (m_bCartridgeMap &&
        ((bank >= 0x20 && bank <= 0x5F) ||
         (bank >= 0xA0 && bank <= 0xDF)))
    {
        if (ReadExternalCartridge(bank, addr, pData))
            return TRUE;
        return FALSE;
    }

    if (pSRAM && nSRAMBytes >= 0x8000)
    {
        if (!(m_uParallel & 0x02) &&
            bank == 0x70 && addr >= 0x8000)
        {
            *pData = pSRAM[(addr - 0x8000) & 0x7FFF];
            return TRUE;
        }

        if ((m_uParallel & 0x02) &&
            bank >= 0x30 && bank <= 0x33 &&
            addr >= 0x6000 && addr <= 0x7FFF)
        {
            Uint32 off = (Uint32)(bank - 0x30) * 0x2000u +
                         (Uint32)(addr - 0x6000);
            *pData = pSRAM[off & 0x7FFF];
            return TRUE;
        }
    }

    if (!m_pDRAM || !inBankRange)
        return FALSE;

    if (m_uParallel & 0x01)
    {
        if (addr < 0x8000)
        {
            if (!((bank >= 0x40 && bank <= maxLoBank) ||
                  (bank >= 0xC0 && bank <= maxHiBank)))
                return FALSE;
        }
        *pData = m_pDRAM[DramOffsetMode2(bank, addr)];
        return TRUE;
    }

    if (addr >= 0x8000)
    {
        *pData = m_pDRAM[DramOffsetMode2(bank, addr)];
        return TRUE;
    }

    return FALSE;
}

Bool SNSuperWildCard::WriteEmulation(Uint8 bank, Uint16 addr, Uint8 uData,
                                     Uint8 *pSRAM, Uint32 nSRAMBytes)
{
    if (m_uSystemMode == 2 && addr >= 0xE004 && addr <= 0xE007)
    {
        m_uSystemMode = (Uint8)(addr - 0xE004);
        return TRUE;
    }

    if (pSRAM && nSRAMBytes >= 0x8000)
    {
        if (!(m_uParallel & 0x02) &&
            bank == 0x70 && addr >= 0x8000)
        {
            pSRAM[(addr - 0x8000) & 0x7FFF] = uData;
            return TRUE;
        }

        if ((m_uParallel & 0x02) &&
            bank >= 0x30 && bank <= 0x33 &&
            addr >= 0x6000 && addr <= 0x7FFF)
        {
            Uint32 off = (Uint32)(bank - 0x30) * 0x2000u +
                         (Uint32)(addr - 0x6000);
            pSRAM[off & 0x7FFF] = uData;
            return TRUE;
        }
    }

    Uint8 maxLoBank = (m_uSystemMode == 2) ? 0x70 : 0x6F;
    Uint8 maxHiBank = (m_uSystemMode == 2) ? 0xE0 : 0xDF;
    Bool inBankRange =
        (bank <= maxLoBank) || (bank >= 0x80 && bank <= maxHiBank);

    if (m_bCartridgeMap &&
        ((bank >= 0x20 && bank <= 0x5F) ||
         (bank >= 0xA0 && bank <= 0xDF)))
    {
        Uint8 ignored;
        return ReadExternalCartridge(bank, addr, &ignored);
    }

    if (!m_pDRAM || !inBankRange)
        return FALSE;

    if (m_uParallel & 0x01)
    {
        if (addr < 0x8000 &&
            !((bank >= 0x40 && bank <= maxLoBank) ||
              (bank >= 0xC0 && bank <= maxHiBank)))
            return FALSE;

        m_pDRAM[DramOffsetMode2(bank, addr)] = uData;
        return TRUE;
    }

    if (addr >= 0x8000)
    {
        m_pDRAM[DramOffsetMode2(bank, addr)] = uData;
        return TRUE;
    }

    return FALSE;
}

/* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831
 * Mode-0 BIOS ROM is a fixed 16 KiB read window at 00-01:E000-FFFF.
 * Direct reads remove the per-opcode C++ trap from the SWC menu.
 * Writes remain trapped because MapSuperWildCard installs this as ROM.
 */
Bool SNSuperWildCard::ResolveDirectFirmware(
    Uint8 bank, Uint16 addr, const Uint8 **ppMem) const
{
    Uint32 off;

    if (!ppMem || !m_bActive || m_uSystemMode != 0 ||
        !m_pFirmware || bank > 1 || addr != 0xE000)
        return FALSE;

    off = (Uint32)bank << 13;
    if (off + 0x2000u > m_nFirmwareBytes)
        return FALSE;

    *ppMem = m_pFirmware + off;
    return TRUE;
}

/* AURORA_SWC_MEGA_V9_20260831
 * Resolve stable 8 KiB windows so the 65816 hot path can use direct memory
 * rather than calling ReadSWC/WriteSWC for every opcode/data byte.
 */
Bool SNSuperWildCard::ResolveDirectDram(Uint8 bank, Uint16 addr,
                                        Uint8 **ppMem)
{
    Uint8 maxLoBank;
    Uint8 maxHiBank;
    Bool inBankRange;

    if (!ppMem || !m_bActive || !m_pDRAM ||
        (m_uSystemMode != 2 && m_uSystemMode != 3) ||
        (addr & 0x1FFF))
        return FALSE;

    /* Keep the actual mode/control register block trapped. */
    if ((bank == 0x00 || bank == 0x80) && addr == 0xE000)
        return FALSE;

    if (m_bCartridgeMap &&
        ((bank >= 0x20 && bank <= 0x5F) ||
         (bank >= 0xA0 && bank <= 0xDF)))
        return FALSE;

    if (!(m_uParallel & 0x02) &&
        bank == 0x70 && addr >= 0x8000)
        return FALSE;
    if ((m_uParallel & 0x02) &&
        bank >= 0x30 && bank <= 0x33 && addr == 0x6000)
        return FALSE;

    maxLoBank = (m_uSystemMode == 2) ? 0x70 : 0x6F;
    maxHiBank = (m_uSystemMode == 2) ? 0xE0 : 0xDF;
    inBankRange =
        (bank <= maxLoBank) || (bank >= 0x80 && bank <= maxHiBank);
    if (!inBankRange)
        return FALSE;

    if (m_uParallel & 0x01)
    {
        if (addr < 0x8000 &&
            !((bank >= 0x40 && bank <= maxLoBank) ||
              (bank >= 0xC0 && bank <= maxHiBank)))
            return FALSE;
    }
    else if (addr < 0x8000)
    {
        return FALSE;
    }

    *ppMem = m_pDRAM + DramOffsetMode2(bank, addr);
    return TRUE;
}

Bool SNSuperWildCard::ResolveDirectCartridge(
    Uint8 bank, Uint16 addr, const Uint8 **ppMem) const
{
    Uint32 off;
    Bool allowed = FALSE;

    if (!ppMem || !m_bActive || !m_pCartRom || !m_nCartBytes ||
        (addr & 0x1FFF))
        return FALSE;

    if (m_uSystemMode == 1)
        allowed = TRUE;
    else if ((m_uSystemMode == 2 || m_uSystemMode == 3) &&
             m_bCartridgeMap &&
             ((bank >= 0x20 && bank <= 0x5F) ||
              (bank >= 0xA0 && bank <= 0xDF)))
        allowed = TRUE;

    if (!allowed)
        return FALSE;

    if (m_iCartMapping == 0)
    {
        if (addr < 0x8000)
            return FALSE;
        off = ((Uint32)(bank & 0x7F) << 15) |
              (Uint32)(addr & 0x7FFF);
    }
    else
    {
        Bool fullBank =
            ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0);
        if (addr < 0x8000 && !fullBank)
            return FALSE;
        off = ((Uint32)(bank & 0x3F) << 16) | (Uint32)addr;
    }

    off %= m_nCartBytes;
    if (off + 0x2000u > m_nCartBytes)
        return FALSE;

    *ppMem = m_pCartRom + off;
    return TRUE;
}


/* AURORA_SWC_FLOPPY_V4_20260831
 *
 * The outer Aurora state container already provides CRC32, generation and
 * optional DEFLATE. This private SWC envelope stays byte-oriented and does
 * not alter SnesStateT or any ordinary SNES state.
 */
enum
{
    AURORA_SWC_STATE_META_BYTES = 2048,
    AURORA_SWC_STATE_VERSION = 1
};

static void _AuroraSwcStatePut8(Uint8 *&p, Uint8 v)
{
    *p++ = v;
}

static void _AuroraSwcStatePut16(Uint8 *&p, Uint16 v)
{
    *p++ = (Uint8)(v & 0xff);
    *p++ = (Uint8)((v >> 8) & 0xff);
}

static void _AuroraSwcStatePut32(Uint8 *&p, Uint32 v)
{
    *p++ = (Uint8)(v & 0xff);
    *p++ = (Uint8)((v >> 8) & 0xff);
    *p++ = (Uint8)((v >> 16) & 0xff);
    *p++ = (Uint8)((v >> 24) & 0xff);
}

static Uint8 _AuroraSwcStateGet8(const Uint8 *&p)
{
    return *p++;
}

static Uint16 _AuroraSwcStateGet16(const Uint8 *&p)
{
    Uint16 v = (Uint16)p[0] | ((Uint16)p[1] << 8);
    p += 2;
    return v;
}

static Uint32 _AuroraSwcStateGet32(const Uint8 *&p)
{
    Uint32 v = (Uint32)p[0] |
               ((Uint32)p[1] << 8) |
               ((Uint32)p[2] << 16) |
               ((Uint32)p[3] << 24);
    p += 4;
    return v;
}

static Uint32 _AuroraSwcStateHash32(const Uint8 *pData, Uint32 nBytes)
{
    Uint32 h = 2166136261u;
    for (Uint32 i = 0; i < nBytes; ++i)
    {
        h ^= pData[i];
        h *= 16777619u;
    }
    return h;
}

Uint32 SNSuperWildCard::GetStateBytes() const
{
    return (Uint32)AURORA_SWC_STATE_META_BYTES + (Uint32)SWC_DRAM_BYTES;
}

Bool SNSuperWildCard::SaveState(void *pState, Uint32 nStateBytes) const
{
    static const Uint8 Magic[8] =
        { 'S', 'W', 'C', 'S', 'T', '4', 0, 0 };
    Uint8 *pBase = (Uint8 *)pState;
    Uint8 *p;
    Char SavedPath[sizeof(m_DiskPath)];

    if (!m_bActive || !m_pDRAM || !pBase ||
        nStateBytes != GetStateBytes())
        return FALSE;

    memset(pBase, 0, nStateBytes);
    p = pBase;

    memcpy(p, Magic, sizeof(Magic)); p += sizeof(Magic);
    _AuroraSwcStatePut32(p, AURORA_SWC_STATE_VERSION);
    _AuroraSwcStatePut32(p, AURORA_SWC_STATE_META_BYTES);
    _AuroraSwcStatePut32(p, GetStateBytes());
    _AuroraSwcStatePut32(p, SWC_DRAM_BYTES);
    _AuroraSwcStatePut32(
        p, _AuroraSwcStateHash32(m_pFirmware, m_nFirmwareBytes));

    _AuroraSwcStatePut32(p, m_uSelectedDRAMPage);
    _AuroraSwcStatePut32(p, m_uSelectedSRAMPage);
    _AuroraSwcStatePut8(p, m_uSystemMode);
    _AuroraSwcStatePut8(p, m_uParallel);
    _AuroraSwcStatePut8(p, m_bPageSRAM ? 1 : 0);
    _AuroraSwcStatePut8(p, m_bCartridgeMap ? 1 : 0);

    _AuroraSwcStatePut32(p, (Uint32)m_nTracks);
    _AuroraSwcStatePut32(p, (Uint32)m_nHeads);
    _AuroraSwcStatePut32(p, (Uint32)m_nSectorsPerTrack);
    _AuroraSwcStatePut8(p, m_bDiskChanged ? 1 : 0);

    _AuroraSwcStatePut8(p, m_uDOR);
    _AuroraSwcStatePut8(p, m_uDCR);
    _AuroraSwcStatePut8(p, m_uCylinder);
    _AuroraSwcStatePut8(p, m_uHead);
    _AuroraSwcStatePut8(p, m_uDrive);
    _AuroraSwcStatePut8(p, m_bIRQ ? 1 : 0);
    _AuroraSwcStatePut8(p, m_uLastST0);
    _AuroraSwcStatePut8(p, m_uResetSensePending);
    _AuroraSwcStatePut8(p, m_FdcPhase);

    memcpy(p, m_Command, sizeof(m_Command)); p += sizeof(m_Command);
    _AuroraSwcStatePut8(p, m_nCommand);
    _AuroraSwcStatePut8(p, m_nCommandExpected);

    memcpy(p, m_Result, sizeof(m_Result)); p += sizeof(m_Result);
    _AuroraSwcStatePut8(p, m_nResult);
    _AuroraSwcStatePut8(p, m_iResult);

    memcpy(p, m_Sector, sizeof(m_Sector)); p += sizeof(m_Sector);
    _AuroraSwcStatePut16(p, m_iSectorByte);
    _AuroraSwcStatePut8(p, m_uDataC);
    _AuroraSwcStatePut8(p, m_uDataH);
    _AuroraSwcStatePut8(p, m_uDataR);
    _AuroraSwcStatePut8(p, m_uDataN);
    _AuroraSwcStatePut8(p, m_uDataEOT);
    _AuroraSwcStatePut8(p, m_bDataMT ? 1 : 0);

    memcpy(p, m_FormatIDs, sizeof(m_FormatIDs)); p += sizeof(m_FormatIDs);
    _AuroraSwcStatePut16(p, m_nFormatIDBytes);
    _AuroraSwcStatePut16(p, m_iFormatIDByte);
    _AuroraSwcStatePut8(p, m_uFormatSC);
    _AuroraSwcStatePut8(p, m_uFormatFill);

    memset(SavedPath, 0, sizeof(SavedPath));
    snprintf(SavedPath, sizeof(SavedPath), "%s", m_DiskPath);
    memcpy(p, SavedPath, sizeof(SavedPath)); p += sizeof(SavedPath);

    if (p > pBase + AURORA_SWC_STATE_META_BYTES)
        return FALSE;

    memcpy(pBase + AURORA_SWC_STATE_META_BYTES,
           m_pDRAM, SWC_DRAM_BYTES);
    return TRUE;
}

Bool SNSuperWildCard::RestoreState(const void *pState, Uint32 nStateBytes)
{
    static const Uint8 Magic[8] =
        { 'S', 'W', 'C', 'S', 'T', '4', 0, 0 };
    const Uint8 *pBase = (const Uint8 *)pState;
    const Uint8 *p;
    Uint32 version, metaBytes, totalBytes, dramBytes, firmwareHash;
    Uint32 selectedDRAMPage, selectedSRAMPage;
    Uint8 systemMode, parallel, pageSRAM, cartridgeMap;
    Uint32 tracks, heads, spt;
    Uint8 diskChanged;
    Uint8 dor, dcr, cylinder, head, drive, irq, lastST0, resetSensePending;
    Uint8 phase;
    Uint8 Command[sizeof(m_Command)];
    Uint8 nCommand, nCommandExpected;
    Uint8 Result[sizeof(m_Result)];
    Uint8 nResult, iResult;
    Uint8 Sector[sizeof(m_Sector)];
    Uint16 iSectorByte;
    Uint8 dataC, dataH, dataR, dataN, dataEOT, dataMT;
    Uint8 FormatIDs[sizeof(m_FormatIDs)];
    Uint16 nFormatIDBytes, iFormatIDByte;
    Uint8 formatSC, formatFill;
    Char SavedPath[sizeof(m_DiskPath)];

    if (!m_bActive || !m_pDRAM || !pBase ||
        nStateBytes != GetStateBytes())
        return FALSE;

    p = pBase;
    if (memcmp(p, Magic, sizeof(Magic)) != 0)
        return FALSE;
    p += sizeof(Magic);

    version = _AuroraSwcStateGet32(p);
    metaBytes = _AuroraSwcStateGet32(p);
    totalBytes = _AuroraSwcStateGet32(p);
    dramBytes = _AuroraSwcStateGet32(p);
    firmwareHash = _AuroraSwcStateGet32(p);

    if (version != AURORA_SWC_STATE_VERSION ||
        metaBytes != AURORA_SWC_STATE_META_BYTES ||
        totalBytes != GetStateBytes() ||
        dramBytes != SWC_DRAM_BYTES ||
        !m_pFirmware || m_nFirmwareBytes != SWC_FIRMWARE_BYTES ||
        firmwareHash != _AuroraSwcStateHash32(
            m_pFirmware, m_nFirmwareBytes))
        return FALSE;

    selectedDRAMPage = _AuroraSwcStateGet32(p);
    selectedSRAMPage = _AuroraSwcStateGet32(p);
    systemMode = _AuroraSwcStateGet8(p);
    parallel = _AuroraSwcStateGet8(p);
    pageSRAM = _AuroraSwcStateGet8(p);
    cartridgeMap = _AuroraSwcStateGet8(p);

    tracks = _AuroraSwcStateGet32(p);
    heads = _AuroraSwcStateGet32(p);
    spt = _AuroraSwcStateGet32(p);
    diskChanged = _AuroraSwcStateGet8(p);

    dor = _AuroraSwcStateGet8(p);
    dcr = _AuroraSwcStateGet8(p);
    cylinder = _AuroraSwcStateGet8(p);
    head = _AuroraSwcStateGet8(p);
    drive = _AuroraSwcStateGet8(p);
    irq = _AuroraSwcStateGet8(p);
    lastST0 = _AuroraSwcStateGet8(p);
    resetSensePending = _AuroraSwcStateGet8(p);
    phase = _AuroraSwcStateGet8(p);

    memcpy(Command, p, sizeof(Command)); p += sizeof(Command);
    nCommand = _AuroraSwcStateGet8(p);
    nCommandExpected = _AuroraSwcStateGet8(p);

    memcpy(Result, p, sizeof(Result)); p += sizeof(Result);
    nResult = _AuroraSwcStateGet8(p);
    iResult = _AuroraSwcStateGet8(p);

    memcpy(Sector, p, sizeof(Sector)); p += sizeof(Sector);
    iSectorByte = _AuroraSwcStateGet16(p);
    dataC = _AuroraSwcStateGet8(p);
    dataH = _AuroraSwcStateGet8(p);
    dataR = _AuroraSwcStateGet8(p);
    dataN = _AuroraSwcStateGet8(p);
    dataEOT = _AuroraSwcStateGet8(p);
    dataMT = _AuroraSwcStateGet8(p);

    memcpy(FormatIDs, p, sizeof(FormatIDs)); p += sizeof(FormatIDs);
    nFormatIDBytes = _AuroraSwcStateGet16(p);
    iFormatIDByte = _AuroraSwcStateGet16(p);
    formatSC = _AuroraSwcStateGet8(p);
    formatFill = _AuroraSwcStateGet8(p);

    memcpy(SavedPath, p, sizeof(SavedPath)); p += sizeof(SavedPath);

    if (p > pBase + AURORA_SWC_STATE_META_BYTES ||
        !memchr(SavedPath, 0, sizeof(SavedPath)) ||
        SavedPath[0] == 0 ||
        selectedDRAMPage >= (SWC_DRAM_BYTES / 0x2000u) ||
        selectedSRAMPage >= 4 ||
        systemMode > 3 ||
        parallel > 3 ||
        pageSRAM > 1 ||
        cartridgeMap > 1 ||
        tracks == 0 || tracks > 255 ||
        heads == 0 || heads > 2 ||
        spt == 0 || spt > 36 ||
        diskChanged > 1 ||
        drive > 3 || head > 1 || irq > 1 ||
        resetSensePending > 4 ||
        phase > FDC_PHASE_FORMAT ||
        nCommand > sizeof(Command) ||
        nCommandExpected > sizeof(Command) ||
        nResult > sizeof(Result) ||
        iResult > nResult ||
        iSectorByte > SWC_SECTOR_BYTES ||
        dataMT > 1 ||
        nFormatIDBytes > sizeof(FormatIDs) ||
        iFormatIDByte > nFormatIDBytes ||
        formatSC > 36)
        return FALSE;

    if (strcmp(m_DiskPath, SavedPath) != 0)
    {
        if (!MountDisk(SavedPath))
            return FALSE;
    }

    if ((Uint32)m_nTracks != tracks ||
        (Uint32)m_nHeads != heads ||
        (Uint32)m_nSectorsPerTrack != spt)
        return FALSE;

    memcpy(m_pDRAM,
           pBase + AURORA_SWC_STATE_META_BYTES,
           SWC_DRAM_BYTES);

    m_uSelectedDRAMPage = selectedDRAMPage;
    m_uSelectedSRAMPage = selectedSRAMPage;
    m_uSystemMode = systemMode;
    m_uParallel = parallel;
    m_bPageSRAM = pageSRAM ? TRUE : FALSE;
    m_bCartridgeMap = cartridgeMap ? TRUE : FALSE;
    m_bDiskChanged = diskChanged ? TRUE : FALSE;

    m_uDOR = dor;
    m_uDCR = dcr;
    m_uCylinder = cylinder;
    m_uHead = head;
    m_uDrive = drive;
    m_bIRQ = irq ? TRUE : FALSE;
    m_uLastST0 = lastST0;
    m_uResetSensePending = resetSensePending;
    m_FdcPhase = phase;

    memcpy(m_Command, Command, sizeof(m_Command));
    m_nCommand = nCommand;
    m_nCommandExpected = nCommandExpected;
    memcpy(m_Result, Result, sizeof(m_Result));
    m_nResult = nResult;
    m_iResult = iResult;

    memcpy(m_Sector, Sector, sizeof(m_Sector));
    m_iSectorByte = iSectorByte;
    m_uDataC = dataC;
    m_uDataH = dataH;
    m_uDataR = dataR;
    m_uDataN = dataN;
    m_uDataEOT = dataEOT;
    m_bDataMT = dataMT ? TRUE : FALSE;

    memcpy(m_FormatIDs, FormatIDs, sizeof(m_FormatIDs));
    m_nFormatIDBytes = nFormatIDBytes;
    m_iFormatIDByte = iFormatIDByte;
    m_uFormatSC = formatSC;
    m_uFormatFill = formatFill;

    return TRUE;
}

Bool SNSuperWildCard::Read(Uint32 uAddr, Uint8 *pData,
                           Uint8 *pSRAM, Uint32 nSRAMBytes)
{
    Uint8 bank = (Uint8)((uAddr >> 16) & 0xFF);
    Uint16 addr = (Uint16)(uAddr & 0xFFFF);

    if (!m_bActive || !pData)
        return FALSE;

    switch (m_uSystemMode)
    {
        case 0: return ReadMode0(bank, addr, pData, pSRAM, nSRAMBytes);
        case 1: return ReadExternalCartridge(bank, addr, pData);
        case 2:
        case 3: return ReadEmulation(bank, addr, pData, pSRAM, nSRAMBytes);
        default: return FALSE;
    }
}

Bool SNSuperWildCard::Write(Uint32 uAddr, Uint8 uData,
                            Uint8 *pSRAM, Uint32 nSRAMBytes)
{
    Uint8 bank = (Uint8)((uAddr >> 16) & 0xFF);
    Uint16 addr = (Uint16)(uAddr & 0xFFFF);

    if (!m_bActive)
        return FALSE;

    switch (m_uSystemMode)
    {
        case 0: return WriteMode0(bank, addr, uData, pSRAM, nSRAMBytes);
        case 1:
        {
            Uint8 ignored;
            return ReadExternalCartridge(bank, addr, &ignored);
        }
        case 2:
        case 3: return WriteEmulation(bank, addr, uData, pSRAM, nSRAMBytes);
        default: return FALSE;
    }
}
