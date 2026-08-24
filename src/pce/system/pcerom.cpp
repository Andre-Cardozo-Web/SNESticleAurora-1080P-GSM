/* AURORA_PCE_EXPERIMENTAL_V1 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcerom.h"
#include "dataio.h"
static Char *s_PceExts[]={(Char*)"pce"};
PceRom::PceRom(){m_bLoaded=FALSE;m_pRomMem=NULL;m_pRomData=NULL;m_uRomBytes=0;m_uRomCapacity=0;m_szSourceName[0]=0;}
PceRom::~PceRom(){Unload();}
void PceRom::Unload(){if(m_pRomMem)free(m_pRomMem);m_pRomMem=NULL;m_pRomData=NULL;m_uRomBytes=0;m_uRomCapacity=0;m_szSourceName[0]=0;m_bLoaded=FALSE;}
void PceRom::SetSourceName(const Char *p){if(!p){m_szSourceName[0]=0;return;}strncpy(m_szSourceName,p,sizeof(m_szSourceName)-1);m_szSourceName[sizeof(m_szSourceName)-1]=0;}
Emu::Rom::LoadErrorE PceRom::LoadRom(CDataIO *io,Uint8 *buf,Uint32 cap){Unload();if(!io)return LOADERROR_OPENFILE;io->Seek(0,SEEK_END);Uint32 n=(Uint32)io->GetPos();io->Seek(0,SEEK_SET);if(!n)return LOADERROR_BADROMSIZE;Uint8 *p=NULL;if(buf&&cap>=n)p=buf;else{m_pRomMem=(Uint8*)malloc(n);if(!m_pRomMem)return LOADERROR_OUTOFSPACE;p=m_pRomMem;}size_t got=io->Read(p,(Int32)n);if(got!=n){Unload();return LOADERROR_READFILE;}m_pRomData=p;m_uRomBytes=n;m_uRomCapacity=(buf&&p==buf)?cap:n;m_bLoaded=TRUE;return LOADERROR_NONE;}
Emu::Rom::LoadErrorE PceRom::AttachBuffer(Uint8 *p,Uint32 n,Uint32 cap){Unload();if(!p||!n||cap<n)return LOADERROR_BADROMSIZE;m_pRomData=p;m_uRomBytes=n;m_uRomCapacity=cap;m_bLoaded=TRUE;printf("[PceRom] attached Aurora ROM buffer: %u/%u bytes\n",(unsigned)n,(unsigned)cap);return LOADERROR_NONE;}
Uint32 PceRom::GetNumExts(){return sizeof(s_PceExts)/sizeof(s_PceExts[0]);}
Char *PceRom::GetExtName(Uint32 i){return i<GetNumExts()?s_PceExts[i]:NULL;}
Uint32 PceRom::GetNumRomRegions(){return m_bLoaded?1:0;}
Char *PceRom::GetRomRegionName(Uint32 i){return i==0&&m_bLoaded?(Char*)"HuCard":NULL;}
Uint32 PceRom::GetRomRegionSize(Uint32 i){return i==0&&m_bLoaded?m_uRomBytes:0;}
Char *PceRom::GetMapperName(){return (Char*)"Beetle PCE Fast";}
Char *PceRom::GetRomTitle(){return m_szSourceName[0]?m_szSourceName:(Char*)"PC Engine";}
