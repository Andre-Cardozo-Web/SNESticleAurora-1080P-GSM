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
    m_eModel = MODEL_SWC; /* AURORA_V6_MAGICOM_FRONT_FAREAST_20260831 */
    m_pDRAM = NULL;
    m_nDRAMBytes = 0;
    m_pFirmware = NULL;
    m_nFirmwareBytes = 0;
    m_pCartRom = NULL; /* AURORA_SWC_FLOPPY_V5_20260831 */
    m_nCartBytes = 0;
    m_iCartMapping = 0;

    /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901 */
    m_pCartSRAM = NULL;
    m_nCartSRAMBytes = 0;
    m_bCartSRAMBattery = FALSE;
    m_bCartSRAMDirty = FALSE;
    m_pDisk = NULL;
    /* AURORA_SWC_D88_ONLY_V5_20260901 */
    memset(m_D88TrackOffset, 0, sizeof(m_D88TrackOffset));
    m_uD88DiskBytes = 0;
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



/* AURORA_CLASSIC_MEMORY_MODE_V10_15B_20260831
 * Cumulative: accurate Mode-2 register mirror + classic firmware revisions.
 */

/* AURORA_CLASSIC_COPIER_FIRMWARE_V10_15B_20260831
 * Normalize only the classic Front-family firmware layouts. DX/DX2 are
 * different hardware and are intentionally not folded into this model. */
static Bool _AuroraClassicSwcVectorOK(const Uint8 *p, Uint32 nBytes)
{
    Uint16 rv;
    if (!p || nBytes < 0x2000u)
        return FALSE;
    rv = (Uint16)p[0x1FFCu] | ((Uint16)p[0x1FFDu] << 8);
    return (rv >= 0xE000u && rv != 0xFFFFu) ? TRUE : FALSE;
}

Bool SNSuperWildCard::LoadFirmware(const Char *pPath)
{
    FILE *pFile;
    long nBytes, sourceOffset = 0;
    Uint32 want;

    if (!pPath || !*pPath) { SetError("empty copier firmware path"); return FALSE; }
    pFile=fopen(pPath,"rb");
    if (!pFile) { SetError("cannot open copier firmware"); return FALSE; }
    if (fseek(pFile,0,SEEK_END)!=0)
    { fclose(pFile); SetError("cannot size copier firmware"); return FALSE; }
    nBytes=ftell(pFile);

    if (m_eModel == MODEL_MAGICOM)
    {
        /* AURORA_V6_MAGICOM_FRONT_FAREAST_20260831: V1H/V31 = one 8-KiB bank; common 32-KiB dump has
         * 24 KiB padding before that bank. 32.5 KiB V3H is not accepted. */
        want=MAGICOM_FIRMWARE_BYTES;
        if (nBytes==(long)MAGICOM_FIRMWARE_BYTES) sourceOffset=0;
        else if (nBytes==(long)MAGICOM_FIRMWARE_BYTES+512L) sourceOffset=512L;
        else if (nBytes==32768L) sourceOffset=32768L-MAGICOM_FIRMWARE_BYTES;
        else
        {
            fclose(pFile);
            SetError("Requires classic 8 KiB Super Magicom V1H/V31 BIOS (32 KiB overdump accepted)");
            return FALSE;
        }
    }
    else
    {
        want=SWC_FIRMWARE_BYTES;
        if (nBytes==(long)SWC_FIRMWARE_BYTES+512L)
            sourceOffset=512L;
        else if (nBytes==(long)SWC_FIRMWARE_BYTES)
            sourceOffset=0;
        else if (nBytes==65536L)
        {
            /* AURORA_CLASSIC_COPIER_FIRMWARE_V10_15B_20260831
             * Some V2.8CC dumps are 64-KiB EPROM images although the classic
             * machine exposes a 16-KiB BIOS. Accept only an unambiguous
             * classic 16-KiB payload: exactly one valid quarter, or multiple
             * valid quarters that are byte-identical. */
            Uint8 firstBlock[SWC_FIRMWARE_BYTES];
            Uint8 block[SWC_FIRMWARE_BYTES];
            Int32 first = -1;
            Int32 nValid = 0;
            Bool identical = TRUE;

            for (Int32 q = 0; q < 4; ++q)
            {
                long off = (long)q * (long)SWC_FIRMWARE_BYTES;
                if (fseek(pFile, off, SEEK_SET) != 0 ||
                    fread(block, 1, SWC_FIRMWARE_BYTES, pFile) != SWC_FIRMWARE_BYTES)
                {
                    fclose(pFile);
                    SetError("cannot inspect classic SWC 64 KiB overdump");
                    return FALSE;
                }

                if (_AuroraClassicSwcVectorOK(block, SWC_FIRMWARE_BYTES))
                {
                    if (first < 0)
                    {
                        first = q;
                        memcpy(firstBlock, block, SWC_FIRMWARE_BYTES);
                    }
                    else if (memcmp(firstBlock, block, SWC_FIRMWARE_BYTES) != 0)
                    {
                        identical = FALSE;
                    }
                    ++nValid;
                }
            }

            if (nValid == 0 || (nValid > 1 && !identical))
            {
                fclose(pFile);
                SetError("64 KiB image is not an unambiguous classic SWC BIOS");
                return FALSE;
            }
            sourceOffset = (long)first * (long)SWC_FIRMWARE_BYTES;
        }
        else
        {
            fclose(pFile);
            SetError("Requires classic 16 KiB SWC BIOS (safe 64 KiB V2.8CC overdump accepted)");
            return FALSE;
        }
    }

    m_pFirmware=(Uint8 *)malloc(want);
    if (!m_pFirmware)
    { fclose(pFile); SetError("not enough EE memory for copier firmware"); return FALSE; }
    if (fseek(pFile,sourceOffset,SEEK_SET)!=0 ||
        fread(m_pFirmware,1,want,pFile)!=want)
    {
        fclose(pFile); free(m_pFirmware); m_pFirmware=NULL;
        SetError("short read while loading copier firmware"); return FALSE;
    }
    fclose(pFile);

    {
        Uint16 rv=(Uint16)m_pFirmware[0x1ffc] | ((Uint16)m_pFirmware[0x1ffd]<<8);
        Bool bad=(m_eModel==MODEL_MAGICOM)
            ? (rv<0xE000 || rv==0xFFFF)
            : (rv==0x0000 || rv==0xFFFF);
        if (bad)
        {
            free(m_pFirmware); m_pFirmware=NULL;
            SetError("invalid classic copier firmware reset vector"); return FALSE;
        }
    }
    m_nFirmwareBytes=want;
    return TRUE;
}

/* AURORA_SWC_D88_ONLY_V5_20260901 */
static Uint16 _AuroraD88Get16(const Uint8 *p)
{
    return (Uint16)p[0] | ((Uint16)p[1] << 8);
}

static Uint32 _AuroraD88Get32(const Uint8 *p)
{
    return (Uint32)p[0] |
           ((Uint32)p[1] << 8) |
           ((Uint32)p[2] << 16) |
           ((Uint32)p[3] << 24);
}

static void _AuroraD88Put16(Uint8 *p, Uint16 v)
{
    p[0] = (Uint8)(v & 0xFF);
    p[1] = (Uint8)((v >> 8) & 0xFF);
}

/* AURORA_SWC_D88_ONLY_V5_1_20260901:
 * D88 mount validates sector-count geometry against the active copier. */
Bool SNSuperWildCard::D88Probe(
    FILE *pFile, long nBytes,
    Int32 *pTracks, Int32 *pHeads, Int32 *pMaxSpt,
    Uint32 *pOffsets, Uint32 *pDiskBytes,
    Bool *pProtected)
{
    Uint8 header[D88_HEADER_MAX_BYTES];
    Uint8 sh[D88_SECTOR_HEADER_BYTES];
    Uint32 diskBytes;
    Uint32 firstTrack = 0;
    Uint32 prev = 0;
    Uint32 tableCount;
    Int32 tracks = 0, heads = 0, maxSpt = 0;

    if (!pFile || nBytes < D88_HEADER_MIN_BYTES ||
        !pTracks || !pHeads || !pMaxSpt ||
        !pOffsets || !pDiskBytes || !pProtected)
        return FALSE;

    memset(header, 0, sizeof(header));

    if (fseek(pFile, 0, SEEK_SET) != 0 ||
        fread(
            header,
            1,
            D88_HEADER_MIN_BYTES,
            pFile) != D88_HEADER_MIN_BYTES)
        return FALSE;

    diskBytes = _AuroraD88Get32(header + 0x1C);

    /* AURORA_SWC_D88_ONLY_V5_2_20260901:
     * one D88 medium may become 2DD or 2HD after FORMAT. */
    if (diskBytes != (Uint32)nBytes ||
        (header[0x1B] != 0x10 && header[0x1B] != 0x20))
        return FALSE;

    for (Uint32 i = 0; i < D88_TRACK_ACTIVE; ++i)
    {
        Uint32 off = _AuroraD88Get32(header + 0x20 + i * 4);
        if (off)
        {
            firstTrack = off;
            break;
        }
    }

    if (firstTrack == D88_HEADER_MIN_BYTES)
    {
        tableCount = 160;
    }
    else if (firstTrack == D88_HEADER_MAX_BYTES)
    {
        if (nBytes < D88_HEADER_MAX_BYTES ||
            fseek(pFile, D88_HEADER_MIN_BYTES, SEEK_SET) != 0 ||
            fread(
                header + D88_HEADER_MIN_BYTES,
                1,
                D88_HEADER_MAX_BYTES - D88_HEADER_MIN_BYTES,
                pFile) !=
                D88_HEADER_MAX_BYTES - D88_HEADER_MIN_BYTES)
            return FALSE;

        tableCount = D88_TRACK_TABLE;
    }
    else
    {
        return FALSE;
    }

    memset(pOffsets, 0, sizeof(Uint32) * D88_TRACK_TABLE);

    for (Uint32 i = 0; i < tableCount; ++i)
    {
        Uint32 off = _AuroraD88Get32(header + 0x20 + i * 4);

        /* 80 cylinders x two heads is an intentional hard ceiling.
         * Some D88 tools put diskBytes in unused tail entries, so accept
         * that standard sentinel without treating it as a real track. */
        if (i >= D88_TRACK_ACTIVE)
        {
            if (off != 0 && off != diskBytes)
                return FALSE;
            continue;
        }

        /* V5 models a complete inserted 80x2 physical medium. Missing
         * track slots are rejected rather than guessed or synthesized. */
        if (!off)
            return FALSE;

        if (off < firstTrack || off >= diskBytes ||
            (prev && off <= prev))
            return FALSE;

        pOffsets[i] = off;
        prev = off;
    }

    for (Uint32 i = 0; i < D88_TRACK_ACTIVE; ++i)
    {
        Uint32 start = pOffsets[i];
        Uint32 end = diskBytes;
        Uint32 pos;
        Uint16 count;
        Uint8 seen[256];

        if (!start)
            continue;

        for (Uint32 j = i + 1; j < D88_TRACK_ACTIVE; ++j)
        {
            if (pOffsets[j])
            {
                end = pOffsets[j];
                break;
            }
        }

        if (end <= start ||
            start + D88_SECTOR_HEADER_BYTES > end ||
            fseek(pFile, (long)start, SEEK_SET) != 0 ||
            fread(sh, 1, sizeof(sh), pFile) != sizeof(sh))
            return FALSE;

        count = _AuroraD88Get16(sh + 4);
        if (!count || count > D88_MAX_SECTORS ||
            !D88FormatSectorCountAllowed((Uint8)count))
            return FALSE;

        memset(seen, 0, sizeof(seen));
        pos = start;

        for (Uint16 k = 0; k < count; ++k)
        {
            Uint16 declared;
            Uint16 bytes;

            if (pos + D88_SECTOR_HEADER_BYTES > end ||
                fseek(pFile, (long)pos, SEEK_SET) != 0 ||
                fread(sh, 1, sizeof(sh), pFile) != sizeof(sh))
                return FALSE;

            declared = _AuroraD88Get16(sh + 4);
            bytes = _AuroraD88Get16(sh + 14);

            if (declared != count ||
                sh[0] != (Uint8)(i >> 1) ||
                sh[1] != (Uint8)(i & 1) ||
                sh[2] == 0 || sh[2] > count ||
                seen[sh[2]] ||
                sh[3] != 2 ||
                sh[6] != 0x00 || sh[7] != 0x00 || sh[8] != 0x00 ||
                bytes != SWC_SECTOR_BYTES)
                return FALSE;

            seen[sh[2]] = 1;

            pos += D88_SECTOR_HEADER_BYTES + bytes;
            if (pos > end)
                return FALSE;
        }

        for (Uint16 r = 1; r <= count; ++r)
            if (!seen[r])
                return FALSE;

        {
            Int32 c = (Int32)(i >> 1) + 1;
            Int32 h = (Int32)(i & 1) + 1;
            if (c > tracks) tracks = c;
            if (h > heads) heads = h;
            if ((Int32)count > maxSpt) maxSpt = count;
        }
    }

    if (tracks != 80 || heads != 2 ||
        !maxSpt || maxSpt > D88_MAX_SECTORS)
        return FALSE;

    *pTracks = tracks;
    *pHeads = heads;
    *pMaxSpt = maxSpt;
    *pDiskBytes = diskBytes;
    *pProtected = header[0x1A] ? TRUE : FALSE;

    rewind(pFile);
    return TRUE;
}

Bool SNSuperWildCard::D88FindSector(
    Uint8 c, Uint8 h, Uint8 r, Uint8 n, long *pDataOffset)
{
    Uint32 index = (Uint32)c * 2u + h;
    Uint32 start;
    Uint32 end;
    Uint32 pos;
    Uint8 sh[D88_SECTOR_HEADER_BYTES];
    Uint16 count;

    if (!m_pDisk ||
        index >= D88_TRACK_ACTIVE ||
        !r || r > D88_MAX_SECTORS ||
        !m_D88TrackOffset[index])
        return FALSE;

    start = m_D88TrackOffset[index];
    end = m_uD88DiskBytes;

    for (Uint32 j = index + 1; j < D88_TRACK_ACTIVE; ++j)
    {
        if (m_D88TrackOffset[j])
        {
            end = m_D88TrackOffset[j];
            break;
        }
    }

    if (fseek(m_pDisk, (long)start, SEEK_SET) != 0 ||
        fread(sh, 1, sizeof(sh), m_pDisk) != sizeof(sh))
        return FALSE;

    count = _AuroraD88Get16(sh + 4);
    if (!count || count > D88_MAX_SECTORS)
        return FALSE;

    pos = start;
    for (Uint16 k = 0; k < count; ++k)
    {
        Uint16 bytes;

        if (pos + D88_SECTOR_HEADER_BYTES > end ||
            fseek(m_pDisk, (long)pos, SEEK_SET) != 0 ||
            fread(sh, 1, sizeof(sh), m_pDisk) != sizeof(sh))
            return FALSE;

        bytes = _AuroraD88Get16(sh + 14);
        if (bytes != SWC_SECTOR_BYTES)
            return FALSE;

        if (sh[0] == c && sh[1] == h &&
            sh[2] == r && sh[3] == n)
        {
            if (pDataOffset)
                *pDataOffset =
                    (long)(pos + D88_SECTOR_HEADER_BYTES);
            return TRUE;
        }

        pos += D88_SECTOR_HEADER_BYTES + bytes;
        if (pos > end)
            return FALSE;
    }

    return FALSE;
}

Uint8 SNSuperWildCard::D88TrackSectorCount(Uint8 c, Uint8 h)
{
    Uint32 index = (Uint32)c * 2u + h;
    Uint8 sh[D88_SECTOR_HEADER_BYTES];
    Uint16 count;

    if (!m_pDisk ||
        index >= D88_TRACK_ACTIVE ||
        !m_D88TrackOffset[index])
        return 0;

    if (fseek(
            m_pDisk,
            (long)m_D88TrackOffset[index],
            SEEK_SET) != 0 ||
        fread(sh, 1, sizeof(sh), m_pDisk) != sizeof(sh))
        return 0;

    count = _AuroraD88Get16(sh + 4);
    return (!count || count > D88_MAX_SECTORS)
        ? 0 : (Uint8)count;
}

Uint8 SNSuperWildCard::D88TrackSlotCapacity(Uint8 c, Uint8 h)
{
    Uint32 index = (Uint32)c * 2u + h;
    Uint32 start;
    Uint32 end;
    Uint32 entryBytes =
        D88_SECTOR_HEADER_BYTES + SWC_SECTOR_BYTES;
    Uint32 capacity;

    if (!m_pDisk ||
        index >= D88_TRACK_ACTIVE ||
        !m_D88TrackOffset[index])
        return 0;

    start = m_D88TrackOffset[index];
    end = m_uD88DiskBytes;

    for (Uint32 j = index + 1; j < D88_TRACK_ACTIVE; ++j)
    {
        if (m_D88TrackOffset[j])
        {
            end = m_D88TrackOffset[j];
            break;
        }
    }

    if (end <= start ||
        ((end - start) % entryBytes) != 0)
        return 0;

    capacity = (end - start) / entryBytes;
    if (capacity > D88_SLOT_SECTORS)
        capacity = D88_SLOT_SECTORS;

    return (Uint8)capacity;
}

Bool SNSuperWildCard::D88FirstSectorID(
    Uint8 c, Uint8 h,
    Uint8 *pC, Uint8 *pH, Uint8 *pR, Uint8 *pN)
{
    Uint32 index = (Uint32)c * 2u + h;
    Uint8 sh[D88_SECTOR_HEADER_BYTES];

    if (!pC || !pH || !pR || !pN ||
        !m_pDisk ||
        index >= D88_TRACK_ACTIVE ||
        !m_D88TrackOffset[index])
        return FALSE;

    if (fseek(
            m_pDisk,
            (long)m_D88TrackOffset[index],
            SEEK_SET) != 0 ||
        fread(sh, 1, sizeof(sh), m_pDisk) != sizeof(sh))
        return FALSE;

    *pC = sh[0];
    *pH = sh[1];
    *pR = sh[2];
    *pN = sh[3];
    return TRUE;
}

Bool SNSuperWildCard::D88RefreshGeometry()
{
    Int32 maxSpt = 0;

    if (!m_pDisk)
        return FALSE;

    for (Uint32 i = 0; i < D88_TRACK_ACTIVE; ++i)
    {
        Uint8 count;

        if (!m_D88TrackOffset[i])
            return FALSE;

        count = D88TrackSectorCount(
            (Uint8)(i >> 1),
            (Uint8)(i & 1));
        if (!count)
            return FALSE;

        if ((Int32)count > maxSpt)
            maxSpt = count;
    }

    if (!maxSpt || maxSpt > D88_MAX_SECTORS)
        return FALSE;

    m_nTracks = 80;
    m_nHeads = 2;
    m_nSectorsPerTrack = maxSpt;
    return TRUE;
}

Bool SNSuperWildCard::D88FormatSectorCountAllowed(Uint8 count) const
{
    /* AURORA_SWC_D88_ONLY_V5_1_20260901
     * Preserve the media sets from before RAW was retired.
     * Magicom: 720K/800K/1.44M/1.6M.
     * SWC: same plus 1.2M. */
    if (m_eModel == MODEL_MAGICOM)
    {
        return count == 9  ||
               count == 10 ||
               count == 18 ||
               count == 20;
    }

    return count == 9  ||
           count == 10 ||
           count == 15 ||
           count == 18 ||
           count == 20;
}

Bool SNSuperWildCard::D88FormatCurrentTrack()
{
    Uint32 index = (Uint32)m_uCylinder * 2u + m_uHead;
    Uint32 start;
    Uint32 end;
    Uint32 slotBytes;
    Uint8 capacity;
    Uint8 seen[256];
    Uint8 oldMediaFlag = 0;
    Uint8 desiredMediaFlag = 0;
    Uint8 uniformSpt = 0;
    Uint8 *oldSlot = NULL;
    Uint8 *newSlot = NULL;
    Bool bUniform = TRUE;
    Bool ok = FALSE;

    if (!m_pDisk || !m_bDiskWritable ||
        index >= D88_TRACK_ACTIVE ||
        !m_D88TrackOffset[index] ||
        !D88FormatSectorCountAllowed(m_uFormatSC) ||
        m_uFormatSC > D88_SLOT_SECTORS ||
        m_nFormatIDBytes != (Uint16)m_uFormatSC * 4u)
        return FALSE;

    capacity = D88TrackSlotCapacity(m_uCylinder, m_uHead);
    if (!capacity || m_uFormatSC > capacity)
    {
        SetError("D88 track has no safe room for requested format");
        return FALSE;
    }

    memset(seen, 0, sizeof(seen));
    for (Uint16 i = 0; i < m_nFormatIDBytes; i += 4)
    {
        Uint8 c = m_FormatIDs[i + 0];
        Uint8 h = m_FormatIDs[i + 1];
        Uint8 r = m_FormatIDs[i + 2];
        Uint8 n = m_FormatIDs[i + 3];

        if (c != m_uCylinder || h != m_uHead ||
            r == 0 || r > m_uFormatSC ||
            seen[r] || n != 2)
            return FALSE;
        seen[r] = 1;
    }

    for (Uint8 r = 1; r <= m_uFormatSC; ++r)
        if (!seen[r])
            return FALSE;

    start = m_D88TrackOffset[index];
    end = m_uD88DiskBytes;
    for (Uint32 j = index + 1; j < D88_TRACK_ACTIVE; ++j)
    {
        if (m_D88TrackOffset[j])
        {
            end = m_D88TrackOffset[j];
            break;
        }
    }
    if (end <= start)
        return FALSE;

    slotBytes = end - start;
    oldSlot = (Uint8 *)malloc(slotBytes);
    newSlot = (Uint8 *)calloc(1, slotBytes);
    if (!oldSlot || !newSlot)
        goto done;

    /* AURORA_SWC_D88_ONLY_V5_2_20260901:
     * header media type participates in the same rollback as this track. */
    if (fseek(m_pDisk, 0x1B, SEEK_SET) != 0 ||
        fread(&oldMediaFlag, 1, 1, m_pDisk) != 1 ||
        (oldMediaFlag != 0x10 && oldMediaFlag != 0x20))
        goto done;

    if (fseek(m_pDisk, (long)start, SEEK_SET) != 0 ||
        fread(oldSlot, 1, slotBytes, m_pDisk) != slotBytes)
        goto done;

    {
        Uint32 pos = 0;
        for (Uint8 s = 0; s < m_uFormatSC; ++s)
        {
            Uint16 id = (Uint16)s * 4u;
            Uint8 *sh;
            Uint8 *data;

            if (pos + D88_SECTOR_HEADER_BYTES + SWC_SECTOR_BYTES > slotBytes)
                goto rollback;

            sh = newSlot + pos;
            data = sh + D88_SECTOR_HEADER_BYTES;
            sh[0] = m_FormatIDs[id + 0];
            sh[1] = m_FormatIDs[id + 1];
            sh[2] = m_FormatIDs[id + 2];
            sh[3] = m_FormatIDs[id + 3];
            _AuroraD88Put16(sh + 4, m_uFormatSC);
            sh[6] = 0x00;
            sh[7] = 0x00;
            sh[8] = 0x00;
            /* AURORA_SWC_D88_ONLY_V5_1_20260901 */
            sh[13] = (m_uFormatSC == 15) ? 0 : 1;
            _AuroraD88Put16(sh + 14, SWC_SECTOR_BYTES);
            memset(data, m_uFormatFill, SWC_SECTOR_BYTES);
            pos += D88_SECTOR_HEADER_BYTES + SWC_SECTOR_BYTES;
        }
    }

    clearerr(m_pDisk);
    if (fseek(m_pDisk, (long)start, SEEK_SET) != 0 ||
        fwrite(newSlot, 1, slotBytes, m_pDisk) != slotBytes ||
        fflush(m_pDisk) != 0)
        goto rollback;

    m_bDiskDirty = FALSE;
    if (!D88RefreshGeometry())
        goto rollback;

    /* Mixed geometry is normal while a whole-disk FORMAT is in progress.
     * Commit 2DD/2HD only when all 160 track-sides are uniform. */
    for (Uint32 i = 0; i < D88_TRACK_ACTIVE; ++i)
    {
        Uint8 count = D88TrackSectorCount(
            (Uint8)(i >> 1), (Uint8)(i & 1));
        if (!count || !D88FormatSectorCountAllowed(count))
            goto rollback;

        if (!uniformSpt)
            uniformSpt = count;
        else if (count != uniformSpt)
        {
            bUniform = FALSE;
            break;
        }
    }

    if (bUniform && uniformSpt)
    {
        desiredMediaFlag =
            (uniformSpt == 9 || uniformSpt == 10) ? 0x10 : 0x20;

        if (desiredMediaFlag != oldMediaFlag)
        {
            if (fseek(m_pDisk, 0x1B, SEEK_SET) != 0 ||
                fwrite(&desiredMediaFlag, 1, 1, m_pDisk) != 1 ||
                fflush(m_pDisk) != 0)
                goto rollback;
        }
    }

    ok = TRUE;
    goto done;

rollback:
    clearerr(m_pDisk);

    if (oldMediaFlag == 0x10 || oldMediaFlag == 0x20)
    {
        if (fseek(m_pDisk, 0x1B, SEEK_SET) == 0)
            (void)fwrite(&oldMediaFlag, 1, 1, m_pDisk);
    }

    if (oldSlot && fseek(m_pDisk, (long)start, SEEK_SET) == 0)
        (void)fwrite(oldSlot, 1, slotBytes, m_pDisk);

    (void)fflush(m_pDisk);
    m_bDiskDirty = FALSE;
    (void)D88RefreshGeometry();
    ok = FALSE;

done:
    if (newSlot) free(newSlot);
    if (oldSlot) free(oldSlot);
    return ok;
}

Bool SNSuperWildCard::MountDisk(const Char *pDiskPath)
{
    FILE *pFile;
    const Char *pExt;
    long nBytes;
    Bool bWritable = TRUE;
    Int32 tracks = 0;
    Int32 heads = 0;
    Int32 maxSpt = 0;
    Uint32 offsets[D88_TRACK_TABLE];
    Uint32 diskBytes = 0;
    Bool d88Protected = FALSE;

    if (!pDiskPath || !*pDiskPath)
    {
        SetError("empty SWC floppy path");
        return FALSE;
    }

    pExt = strrchr(pDiskPath, '.');
    if (!pExt || strcasecmp(pExt, ".d88") != 0)
    {
        SetError("SWC V5 accepts D88 floppy images only");
        return FALSE;
    }

    pFile = fopen(pDiskPath, "r+b");
    if (!pFile)
    {
        bWritable = FALSE;
        pFile = fopen(pDiskPath, "rb");
    }
    if (!pFile)
    {
        SetError("cannot open SWC D88 floppy image");
        return FALSE;
    }

    if (fseek(pFile, 0, SEEK_END) != 0 ||
        (nBytes = ftell(pFile)) < 0)
    {
        fclose(pFile);
        SetError("cannot size SWC D88 floppy image");
        return FALSE;
    }

    if (!D88Probe(
            pFile,
            nBytes,
            &tracks,
            &heads,
            &maxSpt,
            offsets,
            &diskBytes,
            &d88Protected))
    {
        fclose(pFile);
        SetError("invalid or unsupported SWC D88 floppy image");
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

    memcpy(
        m_D88TrackOffset,
        offsets,
        sizeof(m_D88TrackOffset));
    m_uD88DiskBytes = diskBytes;
    m_nTracks = tracks;
    m_nHeads = heads;
    m_nSectorsPerTrack = maxSpt;
    m_bDiskWritable = bWritable && !d88Protected;
    m_bDiskDirty = FALSE;
    m_bDiskChanged = TRUE;

    ResetDebugTrace();
    m_uIndexPollCounter = 0;
    snprintf(
        m_DiskPath,
        sizeof(m_DiskPath),
        "%s",
        pDiskPath);

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

    /* AURORA_FRONT_FDC_BYTEFLOW_V10_6_20260831
     * Hot media change is not instantaneous on a real drive. The backing
     * FILE* is already the new image, but expose 32 C000 polls with INDEX
     * inactive first so the Front BIOS observes disk removal before the new
     * medium starts producing index pulses. No frame timer or state-format
     * change is required. */
    m_uIndexPollCounter = 0x100u + 32u;
    return TRUE;
}

Bool SNSuperWildCard::Load(const Char *pFirmwarePath,
                             const Char *pDiskPath,
                             ModelE eModel)
{
    Shutdown();
    m_LastError[0]=0;
    if (eModel!=MODEL_SWC && eModel!=MODEL_MAGICOM)
    { SetError("unsupported Front Fareast copier model"); return FALSE; }

    m_eModel=eModel;
    m_nDRAMBytes=(m_eModel==MODEL_MAGICOM)?MAGICOM_DRAM_BYTES:SWC_DRAM_BYTES;
    /* AURORA_V6_MAGICOM_FRONT_FAREAST_20260831: allocate only the physical model capacity. */
    m_pDRAM=(Uint8 *)malloc(m_nDRAMBytes);
    if (!m_pDRAM)
    { SetError("not enough EE memory for copier DRAM"); m_nDRAMBytes=0; return FALSE; }
    memset(m_pDRAM,0,m_nDRAMBytes);

    if (!LoadFirmware(pFirmwarePath))
    {
        Char e[sizeof(m_LastError)]; snprintf(e,sizeof(e),"%s",m_LastError);
        Shutdown(); SetError(e); return FALSE;
    }
    if (pDiskPath && *pDiskPath)
    {
        if (!MountDisk(pDiskPath))
        {
            Char e[sizeof(m_LastError)]; snprintf(e,sizeof(e),"%s",m_LastError);
            Shutdown(); SetError(e); return FALSE;
        }

        /* AURORA_SWC_MEDIA_PROBE_V10_2_20260831
         * Media supplied together with the copier BIOS was already in the
         * drive at cold power-on. Do not expose it as a post-boot disk
         * change. Hot SwapDisk() intentionally keeps m_bDiskChanged=TRUE. */
        m_bDiskChanged = FALSE;
    }
    m_bActive=TRUE;
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
    m_nDRAMBytes = 0;
    m_nFirmwareBytes = 0;
    m_eModel = MODEL_SWC;
    m_bDiskWritable = FALSE;
    m_bDiskDirty = FALSE;
    m_bDiskChanged = FALSE;
    m_uIndexPollCounter = 0; /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831 */
    /* AURORA_SWC_D88_ONLY_V5_20260901 */
    memset(m_D88TrackOffset, 0, sizeof(m_D88TrackOffset));
    m_uD88DiskBytes = 0;
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
    /* AURORA_FRONT_TRACE_V10_8_20260831 */
    ResetDebugTrace();
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
    /* AURORA_FRONT_FDC_SILICON_V10_5_20260831
     * Classic Front/MCS3201 behavior: /RES clears the controller state.
     * READY polling is not wired on this part, so do not manufacture the
     * PC-style four-drive post-reset Sense-Interrupt queue. */
    (void)bRaiseIRQ;
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
    m_uLastST0 = 0;
    m_uResetSensePending = 0;
    m_bIRQ = FALSE;
}

/* AURORA_FRONT_FDC_DRIVE_MODEL_V10_3_20260831
 * MCS3201/GM82C765-compatible Front copier drive wiring.
 * DSEL is a 2-bit unit number; the motor bits are one-per-unit.
 * Aurora has one physical backing IMG, attached to whichever unit the
 * firmware actually selects and spins. No artificial spin-up delay. */
Uint8 SNSuperWildCard::FdcSelectedDrive() const
{
    return (Uint8)(m_uDOR & 0x03);
}

Bool SNSuperWildCard::FdcDriveReady(Uint8 uDrive) const
{
    /* AURORA_FRONT_FDC_SILICON_V10_5_20260831
     * NEC765 command HU selects the unit. DOR controls its motor.
     * Requiring DOR DSEL == HU is over-strict and can reject valid Front
     * firmware sequences. This helper means "media can be accessed". */
    Uint8 unit = (Uint8)(uDrive & 0x03);
    Uint8 motor = (Uint8)(0x10u << unit);

    return m_pDisk &&
           (m_uDOR & 0x04) &&
           (m_uDOR & motor)
        ? TRUE : FALSE;
}

Uint8 SNSuperWildCard::FdcMainStatus() const
{
    /* AURORA_FRONT_FDC_SILICON_V10_5_20260831
     * NEC765 MSR:
     *   /RES low -> controller not requesting the bus (00h)
     *   seek/recalibrate -> drive busy bit remains set until SIS result
     *   normal PIO phases retain the existing instantaneous RQM model.
     */
    Uint8 value;

    if (!(m_uDOR & 0x04))
        return 0x00;

    switch (m_FdcPhase)
    {
        case FDC_PHASE_READ:
            value = 0xF0;
            break;
        case FDC_PHASE_RESULT:
            value = 0xD0;
            break;
        case FDC_PHASE_WRITE:
        case FDC_PHASE_FORMAT:
            value = 0xB0;
            break;
        case FDC_PHASE_COMMAND:
        default:
            value = m_nCommand ? 0x90 : 0x80;
            break;
    }

    if (m_bIRQ &&
        (m_uLastST0 & 0x20) &&
        !(m_uLastST0 & 0xC0))
        value |= (Uint8)(1u << (m_uLastST0 & 3));

    return value;
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

Bool SNSuperWildCard::FdcValidCHS(Uint8 c, Uint8 h, Uint8 r)
{
    long off;

    return FdcDriveReady(m_uDrive) &&
           D88FindSector(c, h, r, 2, &off)
        ? TRUE : FALSE;
}



Bool SNSuperWildCard::FdcLoadCurrentSector()
{
    long off;

    if (m_uDataN != 2 ||
        !FdcDriveReady(m_uDrive) ||
        !D88FindSector(
            m_uDataC,
            m_uDataH,
            m_uDataR,
            m_uDataN,
            &off))
        return FALSE;

    if (fseek(m_pDisk, off, SEEK_SET) != 0 ||
        fread(
            m_Sector,
            1,
            SWC_SECTOR_BYTES,
            m_pDisk) != SWC_SECTOR_BYTES)
        return FALSE;

    m_iSectorByte = 0;
    return TRUE;
}

Bool SNSuperWildCard::FdcStoreCurrentSector()
{
    long off;

    if (!m_bDiskWritable ||
        m_uDataN != 2 ||
        !FdcDriveReady(m_uDrive) ||
        !D88FindSector(
            m_uDataC,
            m_uDataH,
            m_uDataR,
            m_uDataN,
            &off))
        return FALSE;

    if (fseek(m_pDisk, off, SEEK_SET) != 0 ||
        fwrite(
            m_Sector,
            1,
            SWC_SECTOR_BYTES,
            m_pDisk) != SWC_SECTOR_BYTES)
        return FALSE;

    m_bDiskDirty = TRUE;
    return TRUE;
}

Bool SNSuperWildCard::FdcAdvanceSector()
{
    if (m_uDataR < m_uDataEOT &&
        m_uDataR < D88_MAX_SECTORS)
    {
        long off;
        Uint8 next = (Uint8)(m_uDataR + 1);

        if (D88FindSector(
                m_uDataC,
                m_uDataH,
                next,
                m_uDataN,
                &off))
        {
            m_uDataR = next;
            return TRUE;
        }
    }

    if (m_bDataMT && m_uDataH == 0 && m_nHeads > 1)
    {
        long off;

        if (D88FindSector(
                m_uDataC,
                1,
                1,
                m_uDataN,
                &off))
        {
            m_uDataH = 1;
            m_uDataR = 1;
            return TRUE;
        }
    }

    return FALSE;
}

void SNSuperWildCard::FdcFinishRW(Bool bOK, Uint8 uST1, Uint8 uST2)
{
    Uint8 r[7];

    /* AURORA_SWC_MEGA_V9_20260831: physical floppy persistence is command-granular, not sector-granular. */
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
            /* AURORA_FRONT_FDC_SILICON_V10_5_20260831
             * ST3.TS is inverted: 0 means two-sided, 1 means single-sided.
             * Front uses INDEX for disk presence; MCS READY is not wired. */
            if (m_nHeads <= 1) r[0] |= 0x08;
            if (m_uCylinder == 0) r[0] |= 0x10;
            r[0] |= 0x20;
            if (m_pDisk && !m_bDiskWritable) r[0] |= 0x40;
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

            /* AURORA_FRONT_FDC_BYTEFLOW_V10_6_20260831
             * READ TRACK is index-synchronous and EOT is the number of
             * sector records to transfer. Treating R as the first sector
             * shortens the byte stream whenever R != 1. */
            if (cmd == 0x02)
            {
                Uint8 trackSpt =
                    D88TrackSectorCount(m_uDataC, m_uDataH);

                m_uDataR = 1;
                if (m_uDataEOT == 0 || m_uDataEOT > trackSpt)
                    m_uDataEOT = trackSpt;
                m_bDataMT = FALSE;
            }

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
            /* AURORA_FRONT_FDC_SILICON_V10_5_20260831
             * Sense Interrupt Status terminates Seek/Recalibrate.
             * No interrupt pending -> IC=10 (80h), one byte.
             * IRQ/busy clear when the first result byte is actually read. */
            if (!m_bIRQ)
            {
                r[0] = 0x80;
                FdcSetResult(r, 1, FALSE);
                return;
            }

            r[0] = m_uLastST0;
            r[1] = m_uCylinder;
            FdcSetResult(r, 2, FALSE);
            return;

        case 0x0A:
            m_uDrive = m_Command[1] & 3;
            m_uHead = (m_Command[1] >> 2) & 1;

            if (!FdcDriveReady(m_uDrive) ||
                !D88FirstSectorID(
                    m_uCylinder,
                    m_uHead,
                    &r[3],
                    &r[4],
                    &r[5],
                    &r[6]))
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
                m_uDataN != 2 ||
                !D88FormatSectorCountAllowed(m_uFormatSC) ||
                !FdcDriveReady(m_uDrive) ||
                !m_bDiskWritable)
            {
                m_uDataC = m_uCylinder;
                m_uDataH = m_uHead;
                m_uDataR = 1;
                FdcFinishRW(
                    FALSE,
                    !FdcDriveReady(m_uDrive)
                        ? 0x04
                        : (m_bDiskWritable ? 0x04 : 0x02),
                    0);
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
    /* AURORA_FRONT_FDC_SILICON_V10_5_20260831 */
    if (!(m_uDOR & 0x04))
        return 0xFF;

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
        ++m_uDebugFdcReadBytes; /* AURORA_FRONT_TRACE_V10_8_20260831 */
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
    /* AURORA_FRONT_FDC_SILICON_V10_5_20260831 */
    if (!(m_uDOR & 0x04))
        return;

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
            Bool ok;
            Uint8 lastC = m_uCylinder;
            Uint8 lastH = m_uHead;
            Uint8 lastR = 1;
            Uint8 lastN = 2;

            if (m_nFormatIDBytes >= 4)
            {
                Uint16 i = (Uint16)(m_nFormatIDBytes - 4);
                lastC = m_FormatIDs[i + 0];
                lastH = m_FormatIDs[i + 1];
                lastR = m_FormatIDs[i + 2];
                lastN = m_FormatIDs[i + 3];
            }

            ok = D88FormatCurrentTrack();

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


/* AURORA_FRONT_TRACE_V10_8_20260831 */
void SNSuperWildCard::ResetDebugTrace()
{
    m_uDebugFdcReadBytes = 0;
    m_uDebugDramWriteBytes = 0;
    m_uDebugDramMaxOffset = 0;
    m_bDebugTransitionPending = FALSE;
    memset(&m_DebugTransition, 0, sizeof(m_DebugTransition));
}

void SNSuperWildCard::CaptureDebugTransition(Uint8 uNewMode)
{
    Uint32 mask;
    Uint32 loD5, loRV, hiD5, hiRV;
    DebugTransitionT d;

    if (!m_pDRAM || !m_nDRAMBytes)
        return;

    mask = m_nDRAMBytes - 1u;
    loD5 = 0x007FD5u & mask;
    loRV = 0x007FFCu & mask;
    hiD5 = 0x00FFD5u & mask;
    hiRV = 0x00FFFCu & mask;

    memset(&d, 0, sizeof(d));
    d.oldMode = m_uSystemMode;
    d.newMode = uNewMode;
    d.parallel = m_uParallel;
    d.fdcReadBytes = m_uDebugFdcReadBytes;
    d.dramWriteBytes = m_uDebugDramWriteBytes;
    d.dramMaxOffset = m_uDebugDramMaxOffset;

    d.loD5 = m_pDRAM[loD5];
    d.loReset = (Uint16)m_pDRAM[loRV] |
                ((Uint16)m_pDRAM[(loRV + 1u) & mask] << 8);
    d.hiD5 = m_pDRAM[hiD5];
    d.hiReset = (Uint16)m_pDRAM[hiRV] |
                ((Uint16)m_pDRAM[(hiRV + 1u) & mask] << 8);

    if (m_uParallel & 0x01)
    {
        d.mappedD5 = d.hiD5;
        d.mappedReset = d.hiReset;
    }
    else
    {
        d.mappedD5 = d.loD5;
        d.mappedReset = d.loReset;
    }

    m_DebugTransition = d;
    m_bDebugTransitionPending = TRUE;
}

Bool SNSuperWildCard::ConsumeDebugTransition(DebugTransitionT *pOut)
{
    if (!pOut || !m_bDebugTransitionPending)
        return FALSE;

    *pOut = m_DebugTransition;
    m_bDebugTransitionPending = FALSE;
    return TRUE;
}

Uint32 SNSuperWildCard::DramOffsetMode2(Uint8 bank, Uint16 addr) const
{
    Uint32 b = (Uint32)(bank & 0x7F);
    Uint32 off;

    if (m_uParallel & 0x01)
        off = b * 0x10000u + (Uint32)addr;
    else
        off = b * 0x8000u + (Uint32)(addr - 0x8000);

    return off & (m_nDRAMBytes - 1);
}


/* AURORA_SWC_FLOPPY_V5_20260831 */
Bool SNSuperWildCard::SetExternalCartridge(
    const Uint8 *pRom, Uint32 nRomBytes, Int32 iMapping,
    Uint32 nSramBytes, Bool bBatterySRAM)
{
    Uint8 *pNewSRAM = NULL;

    if (!m_bActive || !pRom || !nRomBytes ||
        (iMapping != 0 && iMapping != 1))
    {
        SetError("invalid external cartridge");
        return FALSE;
    }

    /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901
     * SnesRom accepts ordinary Game Pak RAM up to the fixed 256 KiB
     * SNESticle backing. A power-of-two size is required by the real
     * mirroring/address-mask semantics used by the normal Aurora mapper. */
    if (nSramBytes > 0x40000u ||
        (nSramBytes && (nSramBytes & (nSramBytes - 1u))))
    {
        SetError("unsupported external cartridge SRAM size");
        return FALSE;
    }

    if (nSramBytes)
    {
        pNewSRAM = (Uint8 *)malloc(nSramBytes);
        if (!pNewSRAM)
        {
            SetError("not enough EE memory for cartridge SRAM");
            return FALSE;
        }
        memset(pNewSRAM, 0xFF, nSramBytes);
    }

    if (m_pCartSRAM)
        free(m_pCartSRAM);

    m_pCartRom = pRom;
    m_nCartBytes = nRomBytes;
    m_iCartMapping = iMapping;
    m_pCartSRAM = pNewSRAM;
    m_nCartSRAMBytes = nSramBytes;
    m_bCartSRAMBattery =
        (pNewSRAM && bBatterySRAM) ? TRUE : FALSE;
    m_bCartSRAMDirty = FALSE;
    return TRUE;
}

void SNSuperWildCard::ClearExternalCartridge()
{
    /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901 */
    if (m_pCartSRAM)
    {
        free(m_pCartSRAM);
        m_pCartSRAM = NULL;
    }

    m_pCartRom = NULL;
    m_nCartBytes = 0;
    m_iCartMapping = 0;
    m_nCartSRAMBytes = 0;
    m_bCartSRAMBattery = FALSE;
    m_bCartSRAMDirty = FALSE;
    m_bCartridgeMap = FALSE;
}

/* AURORA_FRONT_CART_PAGE_MAPPER_V10_11_20260831
 * The Front aperture selects one of four 8-KiB pages inside a cartridge
 * bank.  Crucially, "1 bank = 4 pages" here means 32 KiB of linear page
 * space; the cartridge mapper then decides how that bus address reaches ROM.
 *
 * A000-BFFF represents the upper cartridge half ($8000-$FFFF), while the
 * documented 40-7D/C0-FF:2000-3FFF aperture represents the lower half.
 * Reconstruct that cartridge bus address, then reuse ReadExternalCartridge()
 * so LoROM gets 32-KiB banks and HiROM gets 64-KiB banks naturally.
 */
/* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901 */
Bool SNSuperWildCard::ExternalCartridgeSRAMOffset(
    Uint8 bank, Uint16 addr, Uint32 *pOffset) const
{
    Uint32 off;

    if (!pOffset || !m_pCartSRAM || !m_nCartSRAMBytes)
        return FALSE;

    if (m_iCartMapping == 0) /* LoROM */
    {
        Bool bankOK =
            (bank >= 0x70 && bank <= 0x7D) || bank >= 0xF0;
        Bool lowHalf = addr < 0x8000;
        Bool smallFullBank =
            m_nCartBytes <= 0x200000u &&
            m_nCartSRAMBytes <= 0x8000u;

        if (!bankOK || (!lowHalf && !smallFullBank))
            return FALSE;

        off = ((Uint32)(bank & 0x0F) * 0x8000u) +
              ((Uint32)addr & 0x7FFFu);
    }
    else if (m_iCartMapping == 1) /* HiROM */
    {
        Bool bankOK =
            (bank >= 0x20 && bank <= 0x3F) ||
            (bank >= 0xA0 && bank <= 0xBF);

        if (!bankOK || addr < 0x6000 || addr > 0x7FFF)
            return FALSE;

        off = ((Uint32)(bank & 0x1F) * 0x2000u) +
              (Uint32)(addr - 0x6000);
    }
    else
    {
        return FALSE;
    }

    *pOffset = off & (m_nCartSRAMBytes - 1u);
    return TRUE;
}

Bool SNSuperWildCard::ReadExternalCartridgeSRAM(
    Uint8 bank, Uint16 addr, Uint8 *pData) const
{
    Uint32 off;

    if (!pData || !ExternalCartridgeSRAMOffset(bank, addr, &off))
        return FALSE;

    *pData = m_pCartSRAM[off];
    return TRUE;
}

Bool SNSuperWildCard::WriteExternalCartridgeSRAM(
    Uint8 bank, Uint16 addr, Uint8 uData)
{
    Uint32 off;

    if (!ExternalCartridgeSRAMOffset(bank, addr, &off))
        return FALSE;

    if (m_pCartSRAM[off] != uData)
    {
        m_pCartSRAM[off] = uData;
        if (m_bCartSRAMBattery)
            m_bCartSRAMDirty = TRUE;
    }
    return TRUE;
}

Bool SNSuperWildCard::ReadExternalPage(Uint8 bank, Uint16 addr,
                                       Uint8 *pData) const
{
    Bool upperPage;
    Bool lowerHighBankPage;
    Uint16 cartAddr;

    if (!m_pCartRom || !m_nCartBytes || !pData)
        return FALSE;

    upperPage = (addr >= 0xA000 && addr <= 0xBFFF);
    lowerHighBankPage =
        ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0) &&
        addr >= 0x2000 && addr <= 0x3FFF;

    if (!upperPage && !lowerHighBankPage)
        return FALSE;

    cartAddr = (Uint16)(((m_uSelectedDRAMPage & 3u) << 13) |
                        ((Uint32)addr & 0x1FFFu));
    if (upperPage)
        cartAddr |= 0x8000;

    /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901
     * Cartridge-page mapping exposes the physical Game Pak bus. Battery/
     * work RAM therefore has precedence over ROM wherever the cart decoder
     * selects it; this is also what lets BIOS battery-RAM transfer operate
     * without borrowing the copier's own B-RAM. */
    if (ReadExternalCartridgeSRAM(bank, cartAddr, pData))
        return TRUE;

    return ReadExternalCartridge(bank, cartAddr, pData);
}

Bool SNSuperWildCard::WriteExternalPage(
    Uint8 bank, Uint16 addr, Uint8 uData)
{
    Bool upperPage;
    Bool lowerHighBankPage;
    Uint16 cartAddr;
    Uint8 ignored;

    if (!m_pCartRom || !m_nCartBytes)
        return FALSE;

    upperPage = (addr >= 0xA000 && addr <= 0xBFFF);
    lowerHighBankPage =
        ((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0) &&
        addr >= 0x2000 && addr <= 0x3FFF;

    if (!upperPage && !lowerHighBankPage)
        return FALSE;

    cartAddr = (Uint16)(((m_uSelectedDRAMPage & 3u) << 13) |
                        ((Uint32)addr & 0x1FFFu));
    if (upperPage)
        cartAddr |= 0x8000;

    if (WriteExternalCartridgeSRAM(bank, cartAddr, uData))
        return TRUE;

    /* ROM writes are electrically ignored but the cartridge handled them. */
    return ReadExternalCartridge(bank, cartAddr, &ignored);
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
        /* AURORA_V7_FRONT_COPIER_MEDIA_CART_RESET_20260831
         * Front Fareast System Mode 1 exposes cartridge $0000-$7FFF in
         * banks 40-7D/C0-FF (Mode 21). LoROM A15 is not decoded, so those
         * lower halves mirror the same 32-KiB chunk as $8000-$FFFF. */
        if (addr < 0x8000 &&
            !((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0))
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
    /* AURORA_FRONT_FDC_MIRROR_DECODE_V10_4_20260831
     * Front Fareast decodes only the low I/O address bits:
     *   C010-DFFF mirror C000-C00F.
     * C00A-C00F in turn mirror the parallel C008/C009 pair.
     * Normalize before any individual FDC/parallel register test so real
     * Magicom and Wild Card BIOS code sees the same partially-decoded bus.
     */
    if (addr >= 0xC010 && addr <= 0xDFFF)
        addr = (Uint16)(0xC000 | (addr & 0x000F));
    if (addr >= 0xC00A && addr <= 0xC00F)
        addr = (Uint16)(0xC008 | (addr & 1));

    if (addr == 0xC000)
    {
        Uint8 value = m_bIRQ ? 0x80 : 0;

        /* AURORA_FRONT_FDC_BYTEFLOW_V10_6_20260831
         * 0x100+N is the transient hot-eject sentinel installed by SwapDisk.
         * During this interval INDEX is electrically inactive/high. */
        if (m_uIndexPollCounter >= 0x100u)
        {
            if (m_uIndexPollCounter > 0x100u)
                --m_uIndexPollCounter;
            else
                m_uIndexPollCounter = 0;

            value |= 0x40;
            *pData = value;
            return TRUE;
        }

        /* AURORA_FRONT_FDC_DRIVE_MODEL_V10_3_20260831
         * C000 bit6 is the raw active-low INDEX line used by Front BIOSes
         * as the disk-insert check. INDEX exists only while the selected
         * drive's motor is running and the FDC is out of reset.
         *
         * Keep a deliberately wide polling-domain pulse: first half LOW,
         * second half HIGH. This guarantees both edges to a tight 65816
         * polling loop without host timers or per-frame floppy work. */
        if (FdcDriveReady(FdcSelectedDrive()))
        {
            m_uIndexPollCounter =
                (m_uIndexPollCounter + 1u) & 0x0Fu;
            if (m_uIndexPollCounter >= 8u)
                value |= 0x40;
        }
        else
        {
            m_uIndexPollCounter = 0;
            value |= 0x40; /* inactive INDEX */
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

    if (addr >= 0xE000 && m_pFirmware)
    {
        Uint32 off=0; Bool mapped=FALSE;
        /* AURORA_V6_MAGICOM_FRONT_FAREAST_20260831: 8-KiB Magicom BIOS mirrors through 00-01:E000;
         * SWC retains its two 8-KiB banks. */
        if (m_eModel==MODEL_MAGICOM && bank<=1)
        { off=(Uint32)(addr-0xE000); mapped=TRUE; }
        else if (m_eModel==MODEL_SWC && bank<=1)
        { off=((Uint32)bank<<13)|(Uint32)(addr-0xE000); mapped=TRUE; }
        if (mapped && off<m_nFirmwareBytes)
        { *pData=m_pFirmware[off]; return TRUE; }
    }

    if (addr >= 0x8000 && addr <= 0x9FFF && m_pDRAM)
    {
        /* AURORA_FRONT_CART_PAGE_MAPPER_V10_11_20260831
         * Front page bus: 4 * 8 KiB = 32 KiB per bank. */
        Uint32 page = ((Uint32)bank << 2) |
                      (m_uSelectedDRAMPage & 3u);
        Uint32 off = (page * 0x2000u +
                      ((Uint32)addr & 0x1FFFu)) &
                     (m_nDRAMBytes - 1);
        *pData = m_pDRAM[off];
        return TRUE;
    }

    if (!m_bPageSRAM && ReadExternalPage(bank, addr, pData))
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
    /* AURORA_FRONT_FDC_MIRROR_DECODE_V10_4_20260831
     * Same partial address decode on writes: all Front FDC/DOR/DCR and
     * parallel mirrors must hit the canonical C000-C009 handlers. */
    if (addr >= 0xC010 && addr <= 0xDFFF)
        addr = (Uint16)(0xC000 | (addr & 0x000F));
    if (addr >= 0xC00A && addr <= 0xC00F)
        addr = (Uint16)(0xC008 | (addr & 1));

    if (addr == 0xC002)
    {
        Uint8 old = m_uDOR;
        m_uDOR = uData;

        /* AURORA_FRONT_FDC_DRIVE_MODEL_V10_3_20260831
         * Front DOR: DSEL=bits0-1, /RES=bit2, DMAEN=bit3,
         * MOTOR1..4=bits4-7. A new selected/motor state starts a fresh
         * INDEX revolution for the BIOS disk-insert polling loop. */
        if ((old ^ uData) & 0xF3)
            m_uIndexPollCounter = 0;

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

    /* AURORA_FRONT_MODE0_PAGEBUS_V10_9_20260831
     * E000-E003 select page 0..3 globally. The CPU bank contributes the
     * upper physical address bits when the page window itself is accessed. */
    if (addr >= 0xE000 && addr <= 0xE003)
    {
        m_uSelectedDRAMPage = (Uint32)(addr & 3);
        m_uSelectedSRAMPage = addr & 3;
        return TRUE;
    }

    if (addr >= 0xE004 && addr <= 0xE007)
    {
        Uint8 uNewMode = (Uint8)(addr - 0xE004);
        if (uNewMode == 2 || uNewMode == 3)
            CaptureDebugTransition(uNewMode); /* AURORA_FRONT_TRACE_V10_8_20260831 */
        m_uSystemMode = uNewMode;
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
        /* AURORA_FRONT_CART_PAGE_MAPPER_V10_11_20260831
         * Same 32-KiB-per-bank linear page bus as the read path. */
        Uint32 page = ((Uint32)bank << 2) |
                      (m_uSelectedDRAMPage & 3u);
        Uint32 off = (page * 0x2000u +
                      ((Uint32)addr & 0x1FFFu)) &
                     (m_nDRAMBytes - 1);
        m_pDRAM[off] = uData;
        /* AURORA_FRONT_TRACE_V10_8_20260831
         * V10_8 traps the Mode-0 write fastpath, so this is the exact number
         * of bytes delivered by the BIOS into copier DRAM. */
        ++m_uDebugDramWriteBytes;
        if (off > m_uDebugDramMaxOffset)
            m_uDebugDramMaxOffset = off;
        return TRUE;
    }

    /* AURORA_FRONT_MODE0_PAGEBUS_V10_9_20260831 */
    if (!m_bPageSRAM && m_pCartRom &&
        ((addr >= 0xA000 && addr <= 0xBFFF) ||
         ((((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0)) &&
          addr >= 0x2000 && addr <= 0x3FFF)))
        return WriteExternalPage(bank, addr, uData);

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
        /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901
         * E00D overlays the physical cartridge bus. HiROM battery RAM at
         * 20-3F/A0-BF:6000-7FFF therefore wins over the external ROM. */
        if (ReadExternalCartridgeSRAM(bank, addr, pData))
            return TRUE;
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
    /* AURORA_FRONT_MEMORY_MODE_REG_MIRROR_V10_13_20260831
     * In System Mode 2 the Front mode-select registers E004-E007 remain
     * decoded in copier banks 00-7D/80-FF. 7E/7F are native SNES WRAM. */
    if (m_uSystemMode == 2 &&
        (bank <= 0x7D || bank >= 0x80) &&
        addr >= 0xE004 && addr <= 0xE007)
    {
        Uint8 uNewMode = (Uint8)(addr - 0xE004);
        if (uNewMode == 2 || uNewMode == 3)
            CaptureDebugTransition(uNewMode); /* AURORA_FRONT_TRACE_V10_8_20260831 */
        m_uSystemMode = uNewMode;
        return TRUE;
    }

    /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901
     * E004-E007 above retain absolute write priority. After that, E00D's
     * external-cartridge overlay must precede the copier SRAM mapping, just
     * as ReadEmulation already does. This is essential for HiROM cart SRAM
     * in the overlapping 30-33:6000-7FFF region. */
    if (m_bCartridgeMap &&
        ((bank >= 0x20 && bank <= 0x5F) ||
         (bank >= 0xA0 && bank <= 0xDF)))
    {
        Uint8 ignored;
        if (WriteExternalCartridgeSRAM(bank, addr, uData))
            return TRUE;
        return ReadExternalCartridge(bank, addr, &ignored);
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

    if (!m_pDRAM || !inBankRange)
        return FALSE;

    if (m_uParallel & 0x01)
    {
        if (addr < 0x8000 &&
            !((bank >= 0x40 && bank <= maxLoBank) ||
              (bank >= 0xC0 && bank <= maxHiBank)))
            return FALSE;

        /* AURORA_FRONT_GAMEBUS_V10_7_20260831
         * Cartridge-emulation DRAM is ROM on the SNES bus.
         * Ignore writes instead of corrupting the loaded game image. */
        (void)uData;
        return TRUE;
    }

    if (addr >= 0x8000)
    {
        /* AURORA_FRONT_GAMEBUS_V10_7_20260831
         * Cartridge-emulation DRAM is ROM on the SNES bus.
         * Ignore writes instead of corrupting the loaded game image. */
        (void)uData;
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
    if (!ppMem || !m_bActive || m_uSystemMode!=0 || !m_pFirmware || addr!=0xE000)
        return FALSE;
    if (m_eModel==MODEL_MAGICOM)
    {
        /* AURORA_V6_MAGICOM_FRONT_FAREAST_20260831: one physical 8-KiB ROM, mirrored in Front firmware banks 00-01. */
        if (bank>1 || m_nFirmwareBytes!=MAGICOM_FIRMWARE_BYTES) return FALSE;
        off=0;
    }
    else
    {
        if (bank>1 || m_nFirmwareBytes!=SWC_FIRMWARE_BYTES) return FALSE;
        off=(Uint32)bank<<13;
    }
    if (off+0x2000u>m_nFirmwareBytes) return FALSE;
    *ppMem=m_pFirmware+off;
    return TRUE;
}

Bool SNSuperWildCard::ResolveDirectDram(Uint8 bank, Uint16 addr,
                                        Uint8 **ppMem)
{
    Uint8 maxLoBank;
    Uint8 maxHiBank;
    Bool inBankRange;

    if (!ppMem || !m_bActive || !m_pDRAM || (addr & 0x1FFF))
        return FALSE;

    if (m_uSystemMode == 0)
    {
        /* Hardware Mode 0 DRAM page window:
         * bb:8000-9FFF, bb=00-7D,80-FF. 7E/7F remain SNES WRAM. */
        if (addr != 0x8000 ||
            !((bank <= 0x7D) || (bank >= 0x80)))
            return FALSE;

        /* AURORA_FRONT_CART_PAGE_MAPPER_V10_11_20260831
         * 1 Front bank = 4 x 8-KiB pages = 32 KiB. */
        Uint32 page = ((Uint32)bank << 2) |
                      (m_uSelectedDRAMPage & 3u);
        *ppMem = m_pDRAM +
            ((page * 0x2000u) & (m_nDRAMBytes - 1));
        return TRUE;
    }

    if (m_uSystemMode != 2 && m_uSystemMode != 3)
        return FALSE;

    /* AURORA_CLASSIC_MEMORY_MODE_DIRECT_READ_V4_20260901
     * E004-E007 are write-only mode-select registers. SNCPUSetBank(...,
     * FALSE) keeps the direct read pointer while bRAM=0 sends every write
     * through the existing WriteSWC trap. */
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

    /* AURORA_SWC_CART_SRAM_MEMORY_FINAL_V5_3_20260901
     * Keep complete 8-KiB physical SRAM pages trapped/RW. This applies to
     * Mode 1 and to the E00D external-cart overlay in Modes 2/3. */
    {
        Uint32 sramOff;
        if (ExternalCartridgeSRAMOffset(bank, addr, &sramOff))
            return FALSE;
    }

    /* AURORA_CLASSIC_MEMORY_MODE_DIRECT_READ_V4_20260901
     * External CART follows the same split read/write descriptor rule:
     * direct reads stay enabled; E004-E007 writes remain trapped. */
    if (m_iCartMapping == 0)
    {
        /* AURORA_V7_FRONT_COPIER_MEDIA_CART_RESET_20260831: same Mode-21 LoROM mirror in the direct 8-KiB fastpath. */
        if (addr < 0x8000 &&
            !((bank >= 0x40 && bank <= 0x7D) || bank >= 0xC0))
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
    return (Uint32)AURORA_SWC_STATE_META_BYTES +
           (m_nDRAMBytes ? m_nDRAMBytes : (Uint32)SWC_DRAM_BYTES);
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
    _AuroraSwcStatePut32(p, m_nDRAMBytes);
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
           m_pDRAM, m_nDRAMBytes);
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
        !m_nDRAMBytes || dramBytes != m_nDRAMBytes ||
        !m_pFirmware ||
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
        selectedDRAMPage >= (m_nDRAMBytes / 0x2000u) ||
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
           m_nDRAMBytes);

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
        case 1:
            if (ReadExternalCartridgeSRAM(bank, addr, pData))
                return TRUE;
            return ReadExternalCartridge(bank, addr, pData);
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
            if (WriteExternalCartridgeSRAM(bank, addr, uData))
                return TRUE;
            return ReadExternalCartridge(bank, addr, &ignored);
        }
        case 2:
        case 3: return WriteEmulation(bank, addr, uData, pSRAM, nSRAMBytes);
        default: return FALSE;
    }
}
