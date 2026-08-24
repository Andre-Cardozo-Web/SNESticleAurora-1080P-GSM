#ifndef _PCESYSTEM_H
#define _PCESYSTEM_H
/* AURORA_PCE_EXPERIMENTAL_V1 */
#include "types.h"
#include "emusys.h"
#include "pcerom.h"
#define PCE_STATE_MAGIC 0x54534350u
#define PCE_STATE_VERSION 1u
struct PceStateHeaderT{Uint32 uMagic,uVersion,nPayloadBytes,uFrame,Reserved[4];};
class PceSystem:public Emu::System
{
public:
    PceSystem();virtual ~PceSystem();virtual void SetRom(Emu::Rom*);Bool LoadDisc(const Char*,const Char*);/* AURORA_SNES9X2010_V6_CD_SRAM_NOTICES_20260824 */virtual void Reset();virtual void SoftReset();
    virtual void ExecuteFrame(Emu::SysInputT*,class CRenderSurface*,class CMixBuffer*,ModeE);
    virtual Int32 GetStateSize();virtual void SaveState(void*,Int32);virtual void RestoreState(void*,Int32);
    virtual Int32 GetSRAMBytes();virtual Uint8 *GetSRAMData();virtual const char *GetString(StringE);virtual Uint32 GetSampleRate();
    Bool SaveStateChecked(void*,Int32);Bool RestoreStateChecked(const void*,Int32);Bool IsRomReady()const{return m_bRomReady;}
private:PceRom *m_pPceRom;Bool m_bRomReady;Int32 m_nCachedStateBytes;
};
#endif
