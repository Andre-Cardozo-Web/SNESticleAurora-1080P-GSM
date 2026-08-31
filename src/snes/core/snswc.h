#ifndef _SNSWC_H
#define _SNSWC_H

#include <stdio.h>
#include "types.h"

/*
 * AURORA_SWC_FLOPPY_V1_20260831
 *
 * Classic Super Wild Card hardware only:
 *   - 4 MiB allocated DRAM (covers 32-Mbit upgraded classic units)
 *   - 32 KiB battery SRAM supplied by SnesSystem
 *   - 16 KiB firmware
 *   - MCS3201 / NEC765-compatible polling floppy controller
 *
 * No external cartridge passthrough in V1.
 */
class SNSuperWildCard
{
public:
    SNSuperWildCard();
    ~SNSuperWildCard();

    Bool Load(const Char *pFirmwarePath, const Char *pDiskPath);
    void Shutdown();
    void Reset();

    Bool MountDisk(const Char *pDiskPath);
    Bool SwapDisk(const Char *pDiskPath);

    /* AURORA_SWC_FLOPPY_V5_20260831 */
    Bool SetExternalCartridge(const Uint8 *pRom, Uint32 nRomBytes,
                              Int32 iMapping);
    void ClearExternalCartridge();
    Bool HasExternalCartridge() const { return m_pCartRom != NULL; }
    Bool HasDisk() const { return m_pDisk != NULL; }

    Bool IsActive() const { return m_bActive; }
    const Char *GetDiskPath() const { return m_DiskPath; }
    const Char *GetLastError() const { return m_LastError; }

    /* AURORA_SWC_FLOPPY_V4_20260831
     * Private copier-state extension. Normal SnesStateT is untouched. */
    Uint32 GetStateBytes() const;
    Bool SaveState(void *pState, Uint32 nStateBytes) const;
    Bool RestoreState(const void *pState, Uint32 nStateBytes);

    Bool Read(Uint32 uAddr, Uint8 *pData, Uint8 *pSRAM, Uint32 nSRAMBytes);
    Bool Write(Uint32 uAddr, Uint8 uData, Uint8 *pSRAM, Uint32 nSRAMBytes);
    Bool ResolveDirectDram(Uint8 bank, Uint16 addr, Uint8 **ppMem); /* AURORA_SWC_MEGA_V9_20260831 */
    Bool ResolveDirectCartridge(Uint8 bank, Uint16 addr,
                                const Uint8 **ppMem) const;
    Bool ResolveDirectFirmware(Uint8 bank, Uint16 addr,
                               const Uint8 **ppMem) const; /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831 */

private:
    enum
    {
        SWC_DRAM_BYTES = 4 * 1024 * 1024,
        SWC_FIRMWARE_BYTES = 16 * 1024,
        SWC_SECTOR_BYTES = 512,
        SWC_MAX_FORMAT_IDS = 36 * 4,

        FDC_PHASE_COMMAND = 0,
        FDC_PHASE_READ,
        FDC_PHASE_WRITE,
        FDC_PHASE_RESULT,
        FDC_PHASE_FORMAT
    };

    Bool m_bActive;
    Uint8 *m_pDRAM;
    Uint8 *m_pFirmware;
    Uint32 m_nFirmwareBytes;

    const Uint8 *m_pCartRom; /* AURORA_SWC_FLOPPY_V5_20260831 */
    Uint32 m_nCartBytes;
    Int32 m_iCartMapping;

    Uint32 m_uSelectedDRAMPage;
    Uint32 m_uSelectedSRAMPage;
    Uint8 m_uSystemMode;
    Uint8 m_uParallel;
    Bool m_bPageSRAM;
    Bool m_bCartridgeMap; /* AURORA_SWC_FLOPPY_V3_20260831: external cart window in Modes 2/3 */

    FILE *m_pDisk;
    Bool m_bDiskWritable;
    Bool m_bDiskDirty; /* AURORA_SWC_MEGA_V9_20260831: flush once per FDC command */
    Bool m_bDiskChanged;
    Uint32 m_uIndexPollCounter; /* AURORA_SWC_V10_MENU_INDEX_CARTRESET_20260831: synthetic rotating INDEX pulse */
    Char m_DiskPath[1024];
    Char m_LastError[192];

    Int32 m_nTracks;
    Int32 m_nHeads;
    Int32 m_nSectorsPerTrack;

    Uint8 m_uDOR;
    Uint8 m_uDCR;
    Uint8 m_uCylinder;
    Uint8 m_uHead;
    Uint8 m_uDrive;
    Bool m_bIRQ;
    Uint8 m_uLastST0;
    Uint8 m_uResetSensePending; /* AURORA_SWC_FLOPPY_V2_20260831 */

    Uint8 m_FdcPhase;
    Uint8 m_Command[16];
    Uint8 m_nCommand;
    Uint8 m_nCommandExpected;

    Uint8 m_Result[16];
    Uint8 m_nResult;
    Uint8 m_iResult;

    Uint8 m_Sector[SWC_SECTOR_BYTES];
    Uint16 m_iSectorByte;
    Uint8 m_uDataC;
    Uint8 m_uDataH;
    Uint8 m_uDataR;
    Uint8 m_uDataN;
    Uint8 m_uDataEOT;
    Bool m_bDataMT;

    Uint8 m_FormatIDs[SWC_MAX_FORMAT_IDS];
    Uint16 m_nFormatIDBytes;
    Uint16 m_iFormatIDByte;
    Uint8 m_uFormatSC;
    Uint8 m_uFormatFill;

    void SetError(const Char *pText);
    Bool LoadFirmware(const Char *pPath);
    Bool DetectGeometry(long nBytes);

    void FdcReset(Bool bRaiseIRQ);
    Bool FdcFlushDisk();
    Uint8 FdcMainStatus() const;
    Uint8 FdcReadData();
    void FdcWriteData(Uint8 uData);
    Uint8 FdcCommandLength(Uint8 uCommand) const;
    void FdcExecuteCommand();
    void FdcSetResult(const Uint8 *pData, Uint8 nBytes, Bool bIRQ);
    void FdcFinishRW(Bool bOK, Uint8 uST1, Uint8 uST2);
    Bool FdcLoadCurrentSector();
    Bool FdcStoreCurrentSector();
    Bool FdcAdvanceSector();
    long FdcSectorOffset(Uint8 c, Uint8 h, Uint8 r) const;
    Bool FdcValidCHS(Uint8 c, Uint8 h, Uint8 r) const;

    Bool ReadExternalCartridge(Uint8 bank, Uint16 addr, Uint8 *pData) const;
    Bool ReadExternalPage(Uint16 addr, Uint8 *pData) const;

    Uint32 DramOffsetMode2(Uint8 bank, Uint16 addr) const;
    Bool ReadMode0(Uint8 bank, Uint16 addr, Uint8 *pData,
                   Uint8 *pSRAM, Uint32 nSRAMBytes);
    Bool WriteMode0(Uint8 bank, Uint16 addr, Uint8 uData,
                    Uint8 *pSRAM, Uint32 nSRAMBytes);
    Bool ReadEmulation(Uint8 bank, Uint16 addr, Uint8 *pData,
                       Uint8 *pSRAM, Uint32 nSRAMBytes);
    Bool WriteEmulation(Uint8 bank, Uint16 addr, Uint8 uData,
                        Uint8 *pSRAM, Uint32 nSRAMBytes);
};

#endif
