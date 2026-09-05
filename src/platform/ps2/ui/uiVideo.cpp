#include <libpad.h>
#include <stdio.h>

#include "types.h"
#include "input.h"
#include "font.h"
#include "gskit_backend.h"
#include "mainloop_shared.h"
#include "mainloop_menu.h"
#include "mainloop_ui.h"

// Caminhos locais puros sincronizados com o barramento do Makefile principal
#include "mainloop_exec.h"
#include "picodrive_bridge.h"
#include "quicknes_bridge.h"
#include "snes_bridge.h"
#include "storage.h"
#include "audmixbuffer.h"

// Declarações internas da tela de vídeo do emulador
extern int _VideoModeIndex(int mode);
extern struct { int mode; } _VideoModes[];
extern int _VideoCycleSystemAudioRate(int rate, int dir);
extern void _VideoSetCdMusicEnabled(int enabled);
extern void _VideoApplyCompatFlags(int flags);

void CVideoScreen::Input(Uint32 buttons, Uint32 trigger)
{
    int dir = 0;

    if (trigger & PAD_CIRCLE)
    {
        if (m_iSelect < 10)       m_iSelect = 50; 
        else if (m_iSelect < 20)  m_iSelect = 0; 
        else if (m_iSelect <= 29) m_iSelect = 10;
        else if (m_iSelect < 40)  m_iSelect = 40; 
        else if (m_iSelect < 50)  m_iSelect = 20; 
        else                      m_iSelect = 31;
    }

    {
        int lo, hi;
        if (m_iSelect <= 9)       { lo = 0; hi = 9; }
        else if (m_iSelect < 20)  { lo = 10; hi = 19; } 
        else if (m_iSelect <= 29) { lo = 20; hi = 29; }
        else if (m_iSelect < 40)  { lo = 31; hi = 37; } 
        else if (m_iSelect < 50)  { lo = 40; hi = 44; }
        else                      { lo = 50; hi = 58; }

        if (trigger & PAD_UP) 
        { 
            m_iSelect--; 
            if (m_iSelect < lo)  m_iSelect = hi; 
            if (m_iSelect == 36) m_iSelect = 35; 
            if (m_iSelect == 15) m_iSelect = 14; 
        }
        if (trigger & PAD_DOWN) 
        { 
            m_iSelect++; 
            if (m_iSelect > hi)  m_iSelect = lo; 
            if (m_iSelect == 36) m_iSelect = 37; 
            if (m_iSelect == 15) m_iSelect = 16; 
        }
    }

    if (trigger & PAD_LEFT)  dir = -1; 
    if (trigger & PAD_RIGHT) dir = +1;

    if (dir != 0)
    {
        switch (m_iSelect)
        {
        case 0: 
            { 
                Int32 count = 3; 
                Int32 modeIndex = _VideoModeIndex(g_GskVideoMode) + dir; 
                if (modeIndex < 0) modeIndex = count - 1; 
                if (modeIndex >= count) modeIndex = 0; 
                MainLoopReinitVideoMode(_VideoModes[modeIndex].mode); 
            } 
            break;
        case 1: 
            g_GskWidescreen = !g_GskWidescreen; 
            GSK_SetWidescreen(g_GskWidescreen); 
            break;
        case 2: 
            g_GskOverscan += dir * 5; 
            if (g_GskOverscan < 0) g_GskOverscan = 0; 
            if (g_GskOverscan > 100) g_GskOverscan = 100; 
            GSK_SetOverscan(g_GskOverscan); 
            break;
        case 3: 
            g_GskDispOffX += dir; 
            if (g_GskDispOffX < -64) g_GskDispOffX = -64; 
            if (g_GskDispOffX > 64) g_GskDispOffX = 64; 
            GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY); 
            break;
        case 4: 
            g_GskDispOffY += dir; 
            if (g_GskDispOffY < -64) g_GskDispOffY = -64; 
            if (g_GskDispOffY > 64) g_GskDispOffY = 64; 
            GSK_SetDisplayOffset(g_GskDispOffX, g_GskDispOffY); 
            break;
        case 5: 
            CoverToggle(); 
            break;
        case 6: 
            PicoDriveBridge_SetSmsColorBorder(!PicoDriveBridge_GetSmsColorBorder()); 
            break;
        case 7: 
            PicoDriveBridge_SetGgZoom(!PicoDriveBridge_GetGgZoom()); 
            break;
        case 8: 
            { 
                Int32 level = MainLoopSafeFrameskipGetLevel() + dir; 
                if (level > 9) level = 0; 
                if (level < 0) level = 9; 
                MainLoopSafeFrameskipSetLevel(level); 
            } 
            break;
        case 9: 
            g_VideoBrightnessGain += dir * 10; 
            if (g_VideoBrightnessGain < 50) g_VideoBrightnessGain = 50; 
            if (g_VideoBrightnessGain > 200) g_VideoBrightnessGain = 200; 
            break;
        case 50: 
            BgmSetEnabled(!BgmIsEnabled()); 
            break;
        case 51: 
            { 
                int v = BgmGetVolume() + dir * 2; 
                if (v < 0) v = 0; 
                if (v > 400) v = 400; 
                BgmSetVolume(v); 
            } 
            break;
        case 52: 
            { 
                int v = AudMixGameGetVolume() + dir * 2; 
                if (v < 0) v = 0; 
                if (v > 400) v = 400; 
                AudMixGameSetVolume(v); 
            } 
            break;
        case 53: 
            { 
                int v = AudMixSegaGetVolume() + dir * 2; 
                if (v < 0) v = 0; 
                if (v > 400) v = 400; 
                AudMixSegaSetVolume(v); 
            } 
            break;
        case 54: 
            { 
                int v = AudMixPceGetVolume() + dir * 2; 
                if (v < 0) v = 0; 
                if (v > 400) v = 400; 
                AudMixPceSetVolume(v); 
            } 
            break;
        case 55: 
            SnesAudioSetRate(SNSPCDSP_SAMPLERATE); 
            if (_pSystem == _pSnes && _AudMix) _AudMix->SetSampleRate(SNSPCDSP_SAMPLERATE); 
            break;
        case 56: 
            PicoDriveBridge_SetAudioRate(_VideoCycleSystemAudioRate(PicoDriveBridge_GetAudioRate(), dir)); 
            break;
        case 57: 
            PicoDriveBridge_SetSmsFm(!PicoDriveBridge_GetSmsFm()); 
            break;
        case 58: 
            _VideoSetCdMusicEnabled(!g_CdMusicEnabled); 
            break;
        case 10: 
            MassStorageSetEnabled(!MassStorageIsEnabled()); 
            break;
        case 11: 
            HddSupportSetEnabled(!HddSupportIsEnabled()); 
            break;
        case 12: 
            MmceSupportSetEnabled(!MmceSupportIsEnabled()); 
            if (MmceSupportIsEnabled()) { BgmIOBegin(); MmceProbeAvailableSlots(); BgmIOEnd(); } 
            break;
        case 13: 
            if (SmbSupportIsEnabled()) { BgmIOBegin(); SmbDisconnect(); BgmIOEnd(); SmbSupportSetEnabled(0); } 
            else { SmbSupportSetEnabled(1); } 
            break;
        case 14: 
            Mx4sioSetEnabled(!Mx4sioIsEnabled()); 
            if (Mx4sioIsEnabled()) { BgmIOBegin(); Mx4sioLoadIfEnabled(); BgmIOEnd(); } 
            break;
        case 15: 
            g_FakeSRAMSize = 0; 
            break;
        case 16: 
            g_SnesForceRegion += (dir > 0) ? 1 : -1; 
            if (g_SnesForceRegion > 3) g_SnesForceRegion = (SnesForceRegionE)0; 
            if (g_SnesForceRegion < 0) g_SnesForceRegion = (SnesForceRegionE)3; 
            PicoDriveBridge_SetRegion(g_SnesForceRegion); 
            break;
        case 17: 
            g_FamicloneAudio = !g_FamicloneAudio; 
            QuicknesBridge_SetDutySwap(g_FamicloneAudio ? true : false); 
            break;
        case 20: 
        case 21: 
        case 22: 
        case 23: 
        case 24: 
            { 
                static const Uint8 kLayers[] = { 0x01, 0x02, 0x04, 0x08, 0x10 }; 
                Uint8 uMask = SNPPURenderGetSoftwareLayerMask() ^ kLayers[m_iSelect - 20]; 
                SNPPURenderSetSoftwareLayerMask(uMask); 
            } 
            break;
        case 25: 
            SNPPURenderSetSoftwareHackFlags(SNPPURenderGetSoftwareHackFlags() ^ SNPPU_HACK_COLOR_MATH_OFF); 
            break;
        case 26: 
            SNPPURenderSetSoftwareHackFlags(SNPPURenderGetSoftwareHackFlags() ^ SNPPU_HACK_WINDOWS_OFF); 
            break;
        case 27: 
            SNPPURenderSetSoftwareHackFlags(SNPPURenderGetSoftwareHackFlags() ^ SNPPU_HACK_MODE7_HALF); 
            break;
        case 28: 
            { 
                Int32 level = (Int32)SNPPURenderGetObjLimitLevel() + dir; 
                if (level < 0) level = SNPPU_OBJ_LIMIT_NUM - 1; 
                if (level >= SNPPU_OBJ_LIMIT_NUM) level = 0; 
                SNPPURenderSetObjLimitLevel((Uint8)level); 
            } 
            break;
        case 29: 
            { 
                Int32 mode = (Int32)SNPPURenderGetObjLimitMode() + dir; 
                if (mode < 0) mode = SNPPU_LIMIT_MODE_NUM - 1; 
                if (mode >= SNPPU_OBJ_LIMIT_MODE_NUM) mode = 0; 
                SNPPURenderSetObjLimitMode((Uint8)mode); 
            } 
            break;
        case 31: 
            _VideoApplyCompatFlags(g_VideoCompatFlags == VIDEO_COMPAT_ALL ? 0 : VIDEO_COMPAT_ALL); 
            break;
        case 32: 
            _VideoApplyCompatFlags(g_VideoCompatFlags ^ VIDEO_COMPAT_GS_FULL_CACHE); 
            break;
        case 33: 
            _VideoApplyCompatFlags(g_VideoCompatFlags ^ VIDEO_COMPAT_GIF_LONG_WAIT); 
            break;
        case 34: 
            _VideoApplyCompatFlags(g_VideoCompatFlags ^ VIDEO_COMPAT_AUDIO_SMALL_RPC); 
            break;
        case 35: 
            _VideoApplyCompatFlags(g_VideoCompatFlags ^ VIDEO_COMPAT_AUDIO_DEEP_Q); 
            break;
        case 37: 
            { 
                int v = PicoDriveBridge_GetRenderingMode() + dir; 
                if (v < 0) v = 2; 
                if (v > 2) v = 0; 
                PicoDriveBridge_SetRenderingMode(v); 
            } 
            break;
        case 40: 
            InputSnesMouseCycleModeDir(dir); 
            break;
        case 41: 
            PicoDriveBridge_Set6Button(!PicoDriveBridge_Get6Button()); 
            break;
        case 42: 
            MainLoopMdPadCycleLayoutDir(dir); 
            break;
        case 43: 
            MainLoopTurboCycleSpeedDir(dir); 
            break;
        case 44: 
            QuicknesBridge_SetLightGunEnabled(!QuicknesBridge_GetLightGunEnabled()); 
            break;
        }
    }

    if (trigger & PAD_SQUARE)
    {
        if (m_iSelect >= 50)      m_iSelect = 0;
        else if (m_iSelect >= 40) m_iSelect = 31;
        else if (m_iSelect >= 31) m_iSelect = 50;
        else if (m_iSelect >= 20) m_iSelect = 40;
        else if (m_iSelect >= 10) m_iSelect = 20;
        else                      m_iSelect = 10;
    }

    if (trigger & (PAD_CROSS | PAD_START))
    {
        if (m_iSelect == 18) 
        { 
            if (Aud_IsInitialized()) Aud_Setvol(0); 
            MainResetEmulator(); 
        }
        else if (m_iSelect == 19) 
        { 
            if (Aud_IsInitialized()) Aud_Setvol(0); 
            ExecOSD(0, NULL); 
        }
        else 
        {
            VideoSettingsSave();
        }
    }
}
