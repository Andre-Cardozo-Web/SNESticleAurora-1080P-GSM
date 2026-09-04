#include <libpad.h>
#include <stdio.h>

#include "types.h"
#include "mainloop_install.h"
#include "mainloop_input.h"
#include "input.h"
#include "mainloop_menu.h"
#include "mainloop_iop.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "embedded_irx.h"
#include "memcard.h"

extern "C" {
#include "audio.h"
}

extern "C" int list_title_db(char *pPath);

int _MainLoopMenuEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
    switch (Type)
    {
        case 1:
            {
                char mc0[1024];
                char mc1[1024];
                char exploit_dir[256];
                char **ppInstallFiles = _MainLoop_pInstallFiles;

                _GetExploitDir(exploit_dir);

                snprintf(mc0, sizeof(mc0), "mc0:/%s", exploit_dir);
                snprintf(mc1, sizeof(mc1), "mc1:/%s", exploit_dir);

                ppInstallFiles[0] = (char *)"BOOT.ELF";
                switch (Parm1)
                {
                    case 0:
                        break;
                }
            }
            break;
    }
    return 0;
}
void MainLoopMenuInput(Uint32 buttons, Uint32 trigger, Int32 dir)
{
    if (dir != 0)
    {
        switch (m_iSelect)
        {
        case 0:
            g_GskVideoMode += dir;
            if (g_GskVideoMode < 0) g_GskVideoMode = 2;
            if (g_GskVideoMode > 2) g_GskVideoMode = 0;
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
                AudMixGameGetVolume(v);
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
                AudMixPceGetVolume(v);
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
            if (MmceSupportIsEnabled())
            {
                BgmIOBegin();
                MmceProbeAvailableSlots();
                BgmIOEnd();
            }
            break;
        case 13:
            if (SmbSupportIsEnabled())
            {
                BgmIOBegin();
                SmbDisconnect();
                BgmIOEnd();
                SmbSupportSetEnabled(0);
            }
            else
            {
                SmbSupportSetEnabled(1);
            }
            break;
        case 14:
            Mx4sioSetEnabled(!Mx4sioIsEnabled());
            if (Mx4sioIsEnabled())
            {
                BgmIOBegin();
                Mx4sioLoadIfEnabled();
                BgmIOEnd();
            }
            break;
        case 15:
            g_FakeSRAMSize = 0;
            break;
        case 16:
            g_SnesForceRegion += (dir > 0) ? 1 : -1;
            if (g_SnesForceRegion > 3) g_SnesForceRegion = 0;
            if (g_SnesForceRegion < 0) g_SnesForceRegion = 3;
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
                static const Uint8 kLayers[5] = { 0x01, 0x02, 0x04, 0x08, 0x10 };
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
                if (mode < 0) mode = SNPPU_OBJ_LIMIT_MODE_NUM - 1;
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
