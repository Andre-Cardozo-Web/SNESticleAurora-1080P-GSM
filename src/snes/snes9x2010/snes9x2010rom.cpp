/* AURORA_SNES9X2010_V1 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes9x2010rom.h"
#include "dataio.h"

static Char *s_Snes9x2010Exts[] = {
    (Char *)"sfc", (Char *)"smc", (Char *)"fig", (Char *)"swc",
    (Char *)"gd3", (Char *)"gd7", (Char *)"dx2", (Char *)"bsx"
};

Snes9x2010Rom::Snes9x2010Rom()
{
    m_bLoaded = FALSE;
    m_pRomMem = NULL;
    m_pRomData = NULL;
    m_uRomBytes = 0;
    m_uRomCapacity = 0;
    m_szSourceName[0] = 0;
}

Snes9x2010Rom::~Snes9x2010Rom()
{
    Unload();
}

void Snes9x2010Rom::Unload()
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

void Snes9x2010Rom::SetSourceName(const Char *pName)
{
    if (!pName)
    {
        m_szSourceName[0] = 0;
        return;
    }
    strncpy(m_szSourceName, pName, sizeof(m_szSourceName) - 1);
    m_szSourceName[sizeof(m_szSourceName) - 1] = 0;
}

Emu::Rom::LoadErrorE Snes9x2010Rom::LoadRom(
    CDataIO *pFileIO, Uint8 *pBuffer, Uint32 nBufferBytes)
{
    Uint32 nBytes;
    Uint8 *pData;
    size_t nRead;

    Unload();
    if (!pFileIO)
        return LOADERROR_OPENFILE;

    pFileIO->Seek(0, SEEK_END);
    nBytes = (Uint32)pFileIO->GetPos();
    pFileIO->Seek(0, SEEK_SET);
    if (!nBytes || nBytes > 0x800200U)
        return LOADERROR_BADROMSIZE;

    if (pBuffer && nBufferBytes >= nBytes)
        pData = pBuffer;
    else
    {
        m_pRomMem = (Uint8 *)malloc(nBytes);
        if (!m_pRomMem)
            return LOADERROR_OUTOFSPACE;
        pData = m_pRomMem;
    }

    nRead = pFileIO->Read(pData, (Int32)nBytes);
    if (nRead != nBytes)
    {
        Unload();
        return LOADERROR_READFILE;
    }

    m_pRomData = pData;
    m_uRomBytes = nBytes;
    m_uRomCapacity = (pBuffer && pData == pBuffer) ? nBufferBytes : nBytes;
    m_bLoaded = TRUE;
    return LOADERROR_NONE;
}

Emu::Rom::LoadErrorE Snes9x2010Rom::AttachBuffer(
    Uint8 *pData, Uint32 nBytes, Uint32 nCapacity)
{
    Unload();
    if (!pData || !nBytes || nBytes > 0x800200U || nCapacity < nBytes)
        return LOADERROR_BADROMSIZE;

    m_pRomData = pData;
    m_uRomBytes = nBytes;
    m_uRomCapacity = nCapacity;
    m_bLoaded = TRUE;
    printf("[Snes9x2010Rom] attached Aurora ROM buffer: %u/%u bytes\n",
           (unsigned)nBytes, (unsigned)nCapacity);
    return LOADERROR_NONE;
}

void Snes9x2010Rom::DetachFrontendBacking()
{
    /* AURORA_SNES9X2010_V2_PS2LEAN_20260824
     * retro_load_game/LoadROM has already copied/transformed the image into
     * SNES9x Memory.ROM. Keep IsLoaded/GetBytes/title valid for Aurora state
     * identity and UI, but stop retaining a second full cartridge image. */
    if (m_pRomMem)
        free(m_pRomMem);
    m_pRomMem = NULL;
    m_pRomData = NULL;
    m_uRomCapacity = 0;
}

Uint32 Snes9x2010Rom::GetNumExts()
{
    return sizeof(s_Snes9x2010Exts) / sizeof(s_Snes9x2010Exts[0]);
}

Char *Snes9x2010Rom::GetExtName(Uint32 uExt)
{
    return uExt < GetNumExts() ? s_Snes9x2010Exts[uExt] : NULL;
}

Uint32 Snes9x2010Rom::GetNumRomRegions()
{
    return m_bLoaded ? 1 : 0;
}

Char *Snes9x2010Rom::GetRomRegionName(Uint32 uRegion)
{
    return uRegion == 0 && m_bLoaded ? (Char *)"SNES ROM" : NULL;
}

Uint32 Snes9x2010Rom::GetRomRegionSize(Uint32 uRegion)
{
    return uRegion == 0 && m_bLoaded ? m_uRomBytes : 0;
}

Char *Snes9x2010Rom::GetMapperName()
{
    return (Char *)"Snes9x 2010";
}

Char *Snes9x2010Rom::GetRomTitle()
{
    return m_szSourceName[0] ? m_szSourceName : (Char *)"SNES / Snes9x 2010";
}
