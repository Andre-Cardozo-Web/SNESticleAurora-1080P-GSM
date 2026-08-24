/* AURORA_PCE_EXPERIMENTAL_V1 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "pcesystem.h"
#include "pce/beetle/pce_bridge.h"
extern PceRom *_pPceRom;
PceSystem::PceSystem(){m_pPceRom=NULL;m_bRomReady=FALSE;m_nCachedStateBytes=0;m_uFrame=m_uLine=0;}
PceSystem::~PceSystem(){PceBridge_Shutdown();}
void PceSystem::SetRom(Emu::Rom *r){m_pPceRom=NULL;m_bRomReady=FALSE;m_nCachedStateBytes=0;m_uFrame=m_uLine=0;if(!r){PceBridge_Shutdown();return;}PceBridge_UnloadGame();if(r!=_pPceRom){printf("[PceSystem] rejected non-PceRom object\n");return;}PceRom *rom=(PceRom*)r;if(!rom->GetData()||!rom->GetBytes())return;const char *name=rom->GetSourceName();if(!name||!*name)name="game.pce";if(!PceBridge_LoadGame(rom->GetData(),(size_t)rom->GetBytes(),(size_t)rom->GetCapacity(),name)){printf("[PceSystem/Beetle] LOAD FAILED\n");return;}m_pPceRom=rom;m_bRomReady=TRUE;printf("[PceSystem/Beetle] LOAD OK; SRAM=%d\n",PceBridge_GetSRAMBytes());}

/* AURORA_SNES9X2010_V6_CD_SRAM_NOTICES_20260824 */
Bool PceSystem::LoadDisc(const Char *path, const Char *systemPath)
{
    m_pPceRom = NULL;
    m_bRomReady = FALSE;
    m_nCachedStateBytes = 0;
    m_uFrame = m_uLine = 0;
    PceBridge_UnloadGame();
    if (!PceBridge_LoadDisc(path, systemPath))
    {
        printf("[PceSystem/Beetle] PCE CD LOAD FAILED\n");
        return FALSE;
    }
    m_bRomReady = TRUE;
    printf("[PceSystem/Beetle] PCE CD LOAD OK; SRAM=%d\n",
           PceBridge_GetSRAMBytes());
    return TRUE;
}

void PceSystem::Reset(){m_uFrame=m_uLine=0;if(m_bRomReady)PceBridge_Reset();}
void PceSystem::SoftReset(){m_uFrame=m_uLine=0;if(m_bRomReady)PceBridge_SoftReset();}
void PceSystem::ExecuteFrame(Emu::SysInputT *in,CRenderSurface *dst,CMixBuffer *mix,ModeE mode){(void)mode;if(!m_bRomReady)return;PceBridge_RunFrame(in,dst,mix);++m_uFrame;m_uLine=0;}
Int32 PceSystem::GetStateSize(){if(!m_bRomReady)return 0;if(m_nCachedStateBytes>0)return m_nCachedStateBytes;Int32 p=PceBridge_GetStateSize();if(p<=0||p>INT_MAX-(Int32)sizeof(PceStateHeaderT))return 0;m_nCachedStateBytes=(Int32)sizeof(PceStateHeaderT)+p;return m_nCachedStateBytes;}
void PceSystem::SaveState(void *p,Int32 n){(void)SaveStateChecked(p,n);}void PceSystem::RestoreState(void *p,Int32 n){(void)RestoreStateChecked(p,n);}
Bool PceSystem::SaveStateChecked(void *p,Int32 n){if(!p||!m_bRomReady)return FALSE;Int32 total=GetStateSize();if(total<=(Int32)sizeof(PceStateHeaderT)||n<total)return FALSE;PceStateHeaderT h;memset(&h,0,sizeof(h));Int32 bytes=total-(Int32)sizeof(h);Uint8 *payload=((Uint8*)p)+sizeof(h);memset(payload,0,(size_t)bytes);Int32 wrote=PceBridge_SaveState(payload,bytes);if(wrote!=bytes)return FALSE;h.uMagic=PCE_STATE_MAGIC;h.uVersion=PCE_STATE_VERSION;h.nPayloadBytes=(Uint32)wrote;h.uFrame=m_uFrame;memcpy(p,&h,sizeof(h));return TRUE;}
Bool PceSystem::RestoreStateChecked(const void *p,Int32 n){if(!p||!m_bRomReady||n<(Int32)sizeof(PceStateHeaderT))return FALSE;PceStateHeaderT h;memcpy(&h,p,sizeof(h));if(h.uMagic!=PCE_STATE_MAGIC||h.uVersion!=PCE_STATE_VERSION||!h.nPayloadBytes||h.nPayloadBytes>(Uint32)(n-(Int32)sizeof(h)))return FALSE;Int32 expected=GetStateSize();if(expected<=0||(Uint32)expected!=(Uint32)sizeof(h)+h.nPayloadBytes)return FALSE;if(!PceBridge_LoadState(((const Uint8*)p)+sizeof(h),(Int32)h.nPayloadBytes))return FALSE;m_uFrame=h.uFrame;m_uLine=0;return TRUE;}
Int32 PceSystem::GetSRAMBytes(){return m_bRomReady?PceBridge_GetSRAMBytes():0;}Uint8 *PceSystem::GetSRAMData(){return m_bRomReady?PceBridge_GetSRAMData():NULL;}
const char *PceSystem::GetString(StringE s){switch(s){case STRING_SHORTNAME:return "PCE";case STRING_FULLNAME:return "PC Engine / Beetle PCE Fast";case STRING_SRAMEXT:return "srm";case STRING_STATEEXT:return "pst";}return "";}
Uint32 PceSystem::GetSampleRate(){return PceBridge_GetSampleRate();}
