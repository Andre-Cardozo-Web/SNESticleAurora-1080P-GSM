/* AURORA_SNES9X2010_V1 */
#ifndef _SNES9X2010SYSTEM_H
#define _SNES9X2010SYSTEM_H

#include "types.h"
#include "emusys.h"
#include "snes9x2010rom.h"

#define SNES9X2010_STATE_MAGIC   0x30395853u /* 'SX90' little-endian tag */
#define SNES9X2010_STATE_VERSION 1u

struct Snes9x2010StateHeaderT
{
    Uint32 uMagic;
    Uint32 uVersion;
    Uint32 nPayloadBytes;
    Uint32 uFrame;
    Uint32 Reserved[4];
};

class Snes9x2010System : public Emu::System
{
public:
    Snes9x2010System();
    virtual ~Snes9x2010System();
    virtual void SetRom(Emu::Rom *pRom);
    virtual void Reset();
    virtual void SoftReset();
    virtual void ExecuteFrame(Emu::SysInputT *pInput,
                              class CRenderSurface *pSurface,
                              class CMixBuffer *pMixBuffer,
                              ModeE eMode);
    virtual Int32 GetStateSize();
    virtual void SaveState(void *pState, Int32 nBytes);
    virtual void RestoreState(void *pState, Int32 nBytes);
    virtual Int32 GetSRAMBytes();
    virtual Uint8 *GetSRAMData();
    virtual const char *GetString(StringE eString);
    virtual Uint32 GetSampleRate();

    Bool SaveStateChecked(void *pState, Int32 nBytes);
    Bool RestoreStateChecked(const void *pState, Int32 nBytes);
    Bool IsRomReady() const { return m_bRomReady; }

private:
    Snes9x2010Rom *m_pRom;
    Bool m_bRomReady;
    Int32 m_nCachedStateBytes;
};

#endif
