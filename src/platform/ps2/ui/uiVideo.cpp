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
