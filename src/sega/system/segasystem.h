#ifndef _SEGASYSTEM_H
#define _SEGASYSTEM_H

#include "types.h"
#include "emusys.h"
#include "segarom.h"

#define SEGA_STATE_MAGIC   0x54534753u /* SGST */
#define SEGA_STATE_VERSION 2u

struct SegaStateHeaderT
{
    Uint32 uMagic;
    Uint32 uVersion;
    Uint32 nPayloadBytes;
    Uint32 uFrame;
    Uint32 Reserved[4];
};

class SegaSystem : public Emu::System
{
public:
    SegaSystem();
    virtual ~SegaSystem();

    virtual void SetRom(Emu::Rom *pRom);
    /* AURORA_CD_SRAM_NOTICES_20260824 */
    Bool LoadDisc(const Char *pPath, const Char *pSystemPath);
    /* AURORA_SUPER_MAGIC_DRIVE_V1_20260902 */
    Bool LoadSuperMagicDrive(const Char *pBiosPath, const Char *pDiskPath);
    Bool InsertSuperMagicDriveCartridge(SegaRom *pRom);
    void EjectSuperMagicDriveCartridge();
    Bool SwapSuperMagicDriveDisk(const Char *pPath);
    void PowerCycleSuperMagicDrive();
    Bool IsSuperMagicDrive() const;
    Bool IsSuperMagicDriveFirmwareMode() const;
    Bool HasSuperMagicDriveCartridge() const;
    Bool HasSuperMagicDriveDisk() const;
    const Char *GetSuperMagicDriveDiskPath() const;
    const Char *GetSuperMagicDriveError() const;
    virtual void Reset();
    virtual void SoftReset();

    virtual void ExecuteFrame(Emu::SysInputT *pInput,
                              class CRenderSurface *pTarget,
                              class CMixBuffer *pMixBuf,
                              ModeE eMode);

    virtual Int32 GetStateSize();
    virtual void SaveState(void *pState, Int32 nStateBytes);
    virtual void RestoreState(void *pState, Int32 nStateBytes);

    virtual Int32 GetSRAMBytes();
    virtual Uint8 *GetSRAMData();

    virtual const char *GetString(StringE eString);
    virtual Uint32 GetSampleRate();

    Bool SaveStateChecked(void *pState, Int32 nStateBytes);
    Bool RestoreStateChecked(const void *pState, Int32 nStateBytes);
    Bool IsRomReady() const { return m_bRomReady; }

private:
    SegaRom *m_pSegaRom;
    Bool     m_bRomReady;
    Int32    m_nCachedStateBytes;
};

#endif
