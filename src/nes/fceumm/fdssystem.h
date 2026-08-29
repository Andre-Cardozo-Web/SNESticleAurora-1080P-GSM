/* AURORA_FCEUMM_FDS_V0_6_RUNTIME */
#ifndef _AURORA_FDSSYSTEM_H
#define _AURORA_FDSSYSTEM_H

#include "types.h"
#include "emusys.h"

#define FDS_STATE_MAGIC   0x54534446u /* FDST */
#define FDS_STATE_VERSION 1u

struct FdsStateHeaderT
{
    Uint32 uMagic;
    Uint32 uVersion;
    Uint32 nPayloadBytes;
    Uint32 uFrame;
    Uint32 uSelectedSide;
    Uint32 bDiskInserted;
    Uint32 nSwapFramesRemaining;
    Uint32 uSwapTargetSide;
};

class FdsSystem : public Emu::System
{
public:
    FdsSystem();
    virtual ~FdsSystem();

    virtual void SetRom(Emu::Rom *pRom);
    Bool LoadDisk(const Char *path, const Char *systemPath);
    /* AURORA_FDS_V4_ZIP_SAFE_20260828 */
    Bool LoadDiskMemory(const void *data, Uint32 bytes,
                        const Char *contentName, const Char *systemPath);
    virtual void Reset();
    virtual void SoftReset();
    virtual void ExecuteFrame(Emu::SysInputT *input,
                              class CRenderSurface *target,
                              class CMixBuffer *mix,
                              ModeE mode);
    virtual Int32 GetStateSize();
    virtual void SaveState(void *state, Int32 bytes);
    virtual void RestoreState(void *state, Int32 bytes);
    virtual Int32 GetSRAMBytes();
    virtual Uint8 *GetSRAMData();
    virtual const char *GetString(StringE value);
    virtual Uint32 GetSampleRate();

    Bool SaveStateChecked(void *state, Int32 bytes);
    Bool RestoreStateChecked(const void *state, Int32 bytes);
    Bool IsRomReady() const { return m_bRomReady; }
    Uint32 GetContentBytes() const { return m_nContentBytes; }
    Uint32 GetContentCRC() const { return m_uContentCRC; }

private:
    Bool m_bRomReady;
    Int32 m_nCachedStateBytes;
    Uint32 m_nContentBytes;
    Uint32 m_uContentCRC;
};

#endif

