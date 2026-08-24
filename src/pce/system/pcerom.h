#ifndef _PCEROM_H
#define _PCEROM_H
/* AURORA_PCE_EXPERIMENTAL_V1 */
#include "types.h"
#include "emurom.h"
class PceRom : public Emu::Rom
{
public:
    PceRom(); virtual ~PceRom();
    virtual LoadErrorE LoadRom(CDataIO *pFileIO, Uint8 *pBuffer=NULL, Uint32 nBufferBytes=0);
    virtual void Unload(); virtual Uint32 GetNumExts(); virtual Char *GetExtName(Uint32 uExt);
    virtual Uint32 GetNumRomRegions(); virtual Char *GetRomRegionName(Uint32 uRegion); virtual Uint32 GetRomRegionSize(Uint32 uRegion);
    virtual Char *GetMapperName(); virtual Char *GetRomTitle();
    LoadErrorE AttachBuffer(Uint8 *pData, Uint32 nBytes, Uint32 nCapacity);
    Uint8 *GetData() const{return m_pRomData;} Uint32 GetBytes() const{return m_uRomBytes;} Uint32 GetCapacity() const{return m_uRomCapacity;}
    void SetSourceName(const Char *pName); const Char *GetSourceName() const{return m_szSourceName;}
private:
    Uint8 *m_pRomMem,*m_pRomData; Uint32 m_uRomBytes,m_uRomCapacity; Char m_szSourceName[1024];
};
#endif
