#include <libpad.h>
#include <stdio.h>

#include "types.h"
#include "input.h"
#include "font.h"
#include "gskit_backend.h"
#include "mainloop_shared.h"
#include "mainloop_menu.h"
#include "mainloop_ui.h"

// Caminhos corrigidos subindo um nível para localizar a pasta pai system
#include "../system/mainloop_exec.h"
#include "../system/picodrive_bridge.h"
#include "../system/quicknes_bridge.h"
#include "../system/snes_bridge.h"
#include "../system/storage.h"
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
