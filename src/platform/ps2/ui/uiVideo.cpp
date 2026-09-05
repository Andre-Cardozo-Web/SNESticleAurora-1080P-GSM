#include <libpad.h>
#include <stdio.h>

#include "types.h"
#include "input.h"
#include "font.h"
#include "gskit_backend.h"
#include "mainloop_shared.h"
#include "mainloop_menu.h"
#include "mainloop_ui.h"

// Mantido apenas o barramento essencial para o funcionamento do NES e do sistema
#include "../system/mainloop_exec.h"
#include "../system/quicknes_bridge.h"
#include "../system/storage.h"
#include "audmixbuffer.h"

void CVideoScreen::Input(Uint32 buttons, Uint32 trigger)
{
    int dir = 0;

    if (trigger & PAD_CIRCLE)
    {
        if (m_iSelect < 10)       m_iSelect = 50; 
        else if (m_iSelect < 20)  m_iSelect = 0; 
        else                      m_iSelect = 10;
    }

    {
        int lo, hi;
        if (m_iSelect <= 9)       { lo = 0; hi = 9; }
        else if (m_iSelect < 20)  { lo = 10; hi = 19; } 
        else                      { lo = 50; hi = 54; }

        if (trigger & PAD_UP) 
        { 
            m_iSelect--; 
            if (m_iSelect < lo)  m_iSelect = hi; 
        }
        if (trigger & PAD_DOWN) 
        { 
            m_iSelect++; 
            if (m_iSelect > hi)  m_iSelect = lo; 
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
                Int32 modeIndex = MainLoopGetVideoMode() + dir; 
                if (modeIndex < 0) modeIndex = 2; 
                if (modeIndex > 2) modeIndex = 0; 
                MainLoopReinitVideoMode(modeIndex); 
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
        case 10: 
            MassStorageSetEnabled(!MassStorageIsEnabled()); 
            break;
        case 11: 
            HddSupportSetEnabled(!HddSupportIsEnabled()); 
            break;
        case 12: 
            MmceSupportSetEnabled(!MmceSupportIsEnabled()); 
            break;
        case 13: 
            SmbSupportSetEnabled(!SmbSupportIsEnabled()); 
            break;
        }
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
