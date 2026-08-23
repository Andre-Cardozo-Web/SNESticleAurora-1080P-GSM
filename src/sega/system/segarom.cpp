/* AURORA_PICODRIVE_STAGE2 - generic Sega cartridge/image wrapper */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "segarom.h"
#include "dataio.h"

static Char *s_SegaExts[] =
{
    (Char *)"md",
    (Char *)"bin",
    (Char *)"sms",
    (Char *)"gg",
    (Char *)"32x"
};

SegaRom::SegaRom()
{
    m_bLoaded = FALSE;
    m_pRomMem = NULL;
    m_pRomData = NULL;
    m_uRomBytes = 0;
    m_uRomCapacity = 0;
    m_szSourceName[0] = 0;
}

SegaRom::~SegaRom()
{
    Unload();
}

void SegaRom::Unload()
{
    if (m_pRomMem)
        free(m_pRomMem);
    m_pRomMem = NULL;
    m_pRomData = NULL;
    m_uRomBytes = 0;
    m_uRomCapacity = 0;
    m_szSourceName[0] = 0;
    m_bLoaded = FALSE;
}

void SegaRom::SetSourceName(const Char *pName)
{
    if (!pName)
    {
        m_szSourceName[0] = 0;
        return;
    }
    strncpy(m_szSourceName, pName, sizeof(m_szSourceName) - 1);
    m_szSourceName[sizeof(m_szSourceName) - 1] = 0;
}

Emu::Rom::LoadErrorE SegaRom::LoadRom(
    CDataIO *pFileIO, Uint8 *pBuffer, Uint32 nBufferBytes)
{
    Unload();
    if (!pFileIO)
        return LOADERROR_OPENFILE;

    pFileIO->Seek(0, SEEK_END);
    Uint32 uTotal = (Uint32)pFileIO->GetPos();
    pFileIO->Seek(0, SEEK_SET);

    if (uTotal == 0)
        return LOADERROR_BADROMSIZE;

    Uint8 *pBuf = NULL;
    if (pBuffer && nBufferBytes >= uTotal)
    {
        pBuf = pBuffer;
    }
    else
    {
        m_pRomMem = (Uint8 *)malloc(uTotal);
        if (!m_pRomMem)
            return LOADERROR_OUTOFSPACE;
        pBuf = m_pRomMem;
    }

    size_t nRead = pFileIO->Read(pBuf, (Int32)uTotal);
    if (nRead != uTotal)
    {
        Unload();
        return LOADERROR_READFILE;
    }

    m_pRomData = pBuf;
    m_uRomBytes = uTotal;
    m_uRomCapacity =
        (pBuffer && pBuf == pBuffer) ? nBufferBytes : uTotal;
    m_bLoaded = TRUE;

    printf("[SegaRom] loaded %u bytes\n", (unsigned)m_uRomBytes);
    return LOADERROR_NONE;
}

Emu::Rom::LoadErrorE SegaRom::AttachBuffer(
    Uint8 *pData, Uint32 nBytes, Uint32 nCapacity)
{
    Unload();
    if (!pData || !nBytes || nCapacity < nBytes)
        return LOADERROR_BADROMSIZE;
    m_pRomMem = NULL;
    m_pRomData = pData;
    m_uRomBytes = nBytes;
    m_uRomCapacity = nCapacity;
    m_bLoaded = TRUE;
    printf("[SegaRom] attached Aurora ROM buffer: %u/%u bytes\n",
           (unsigned)m_uRomBytes, (unsigned)m_uRomCapacity);
    return LOADERROR_NONE;
}

Uint32 SegaRom::GetNumExts()
{
    return sizeof(s_SegaExts) / sizeof(s_SegaExts[0]);
}

Char *SegaRom::GetExtName(Uint32 uExt)
{
    return uExt < GetNumExts() ? s_SegaExts[uExt] : NULL;
}

Uint32 SegaRom::GetNumRomRegions()
{
    return m_bLoaded ? 1 : 0;
}

Char *SegaRom::GetRomRegionName(Uint32 uRegion)
{
    return (uRegion == 0 && m_bLoaded) ? (Char *)"ROM" : NULL;
}

Uint32 SegaRom::GetRomRegionSize(Uint32 uRegion)
{
    return (uRegion == 0 && m_bLoaded) ? m_uRomBytes : 0;
}

Char *SegaRom::GetMapperName()
{
    return (Char *)"PicoDrive";
}

Char *SegaRom::GetRomTitle()
{
    return m_szSourceName[0] ? m_szSourceName : (Char *)"SEGA";
}
