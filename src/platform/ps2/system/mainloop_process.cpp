/* mainloop_process.cpp
 *
 * Hosts MainLoopProcess() -- the per-frame driver that polls input,
 * runs the SNES core, drives netplay/movie clip, kicks SRAM bookkeeping
 * and finally calls into MainLoopRender().
 *
 * Owns the file-static _iframetex flip flag used to alternate between
 * the two CRenderSurface entries in _fbTexture[].
 *
 * Extracted from mainloop.cpp during the Batch 3 split. Behaviour is
 * unchanged.
 */

#include <stdio.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_input.h"
#include "mainloop_state.h"
#include "mainloop_exec.h"
#include "mainloop_iop.h"
#include "sega/picodrive/picodrive_bridge.h"
#include "gskit_backend.h"

#include "types.h"
#include "console.h"
#include "input.h"
#include "snes.h"
#include "rendersurface.h"
#include "mixbuffer.h"
#include "prof.h"
#include "emusys.h"
#include "emumovie.h"

extern "C" {
#include "gpprim.h"
};

extern "C" {
#include "netplay_ee.h"
};

extern "C" {
#include "audio.h"
};

static Uint32 _iframetex=0;

/* AURORA_GAMEPLAY_HEADROOM_TRANSITION
 * Host-audio warmup state only; no emulated/save-state state. */
static Bool _AudioGameplayWasActive = FALSE;
static Emu::System *_AudioGameplaySystem = NULL;
static Bool _SnesMouseWasActive = FALSE;
static Bool _UsbMouseGameplayWasActive = FALSE;


Bool MainLoopProcess()
{
    NetPlayRPCInputT NetInput;
    /* AURORA_PD_CADENCE_RESUME_FIRST_FRAME_V7_20260821 */
    Bool bGameplayJustStarted = FALSE;

    PROF_ENTER("Frame");

    PROF_ENTER("NetPlayRPCProcess");
    NetPlayRPCProcess();
    PROF_LEAVE("NetPlayRPCProcess");

    PROF_ENTER("InputProcess");
    InputPoll();

    PROF_LEAVE("InputProcess");


	{
	    /* OR the digital pad bits with d-pad bits synthesised from each
	       pad's left analog stick. The synthesised bits only travel
	       through _MainLoopInputProcess (menu / screen-cycle / debug
	       triggers), so SNES gameplay still uses the strictly digital
	       _Input_PadData via _MainLoopInput. Result: the analog stick
	       drives menu navigation just like InfinityStation, without
	       leaking into the running game. */
	    Uint32 buttons =
	          InputGetPadData(0) | InputGetPadData(1)
	        | InputGetPadData(2) | InputGetPadData(3)
	        | InputGetPadDpadFromAnalog(0) | InputGetPadDpadFromAnalog(1)
	        | InputGetPadDpadFromAnalog(2) | InputGetPadDpadFromAnalog(3);

	    _MainLoopInputProcess(buttons);
	}

//	_MainLoopInputProcess(InputGetPadData(0));
//	_MainLoopInputProcess(InputGetPadData(1));

    {
        const Bool bGameplayNow =
            (!_bMenu && _pSystem && !_MainLoop_BlackScreen) ? TRUE : FALSE;

        bGameplayJustStarted =
            (bGameplayNow &&
             (!_AudioGameplayWasActive ||
              _AudioGameplaySystem != _pSystem)) ? TRUE : FALSE;

        if (bGameplayJustStarted)
        {
            /* Fill host-audio safety headroom BEFORE the first cold frame. */
            Aud_PrepareGameplayHeadroom();
            _AudioGameplaySystem = _pSystem;
        }

        _AudioGameplayWasActive = bGameplayNow;
    }

    if (!_bMenu && _pSystem && !_MainLoop_BlackScreen)
    {
        CRenderSurface *pSurface;
        CMixBuffer *pMixBuffer = NULL;
        pSurface = _fbTexture[_iframetex];
	
		Emu::SysInputT Input;
		Bool bSnesMouse = FALSE;
		Int32 nSnesMouseX = 0;
		Int32 nSnesMouseY = 0;
		Uint32 uSnesMouseButtons = 0;
	
		Int32 iPad;

		/* AURORA_AUTO_SNES_MOUSE_V1_5
		 * Mouse replaces gameplay port 1 on SNES and non-8-bit Sega.
		 * Pads remain polled for menus and frontend hotkeys. */
		if ((_pSystem == _pSnes ||
		     (_pSystem == _pSega && !PicoDriveBridge_Is8Bit())) &&
		    InputSnesMouseShouldUse())
		{
			bSnesMouse = TRUE;
			InputGetMouseData(&nSnesMouseX, &nSnesMouseY, &uSnesMouseButtons);
		}

        /*
        if (_WavFile.IsOpen())
        {
            pMixBuffer = &_WavFile;
        } else
        {
            pMixBuffer = &_AudMix;
        } 
        */                        
        pMixBuffer = _AudMix;
//                pMixBuffer = NULL;

		// read inputs
		for (iPad=0; iPad < 5; iPad++)
		{
			if (bSnesMouse && iPad == 0)
			{
				Input.uPad[iPad] = EMUSYS_DEVICE_DISCONNECTED;
			}
			else if (InputIsPadConnected(iPad))
			{
				/* OR the digital pad bits with d-pad bits synthesised from
				   the left analog stick so the analog stick drives the SNES
				   d-pad in-game (LEFT/RIGHT/UP/DOWN). Digital and analog
				   inputs are merged: if both press the same direction the
				   result is identical to a single press, so users can use
				   whichever they prefer (or both). */
				Input.uPad[iPad] = _MainLoopInput(InputGetPadData(iPad)
				                               | InputGetPadDpadFromAnalog(iPad));
			} else
			{
				Input.uPad[iPad] = EMUSYS_DEVICE_DISCONNECTED;
			}
		}

		// send controller 1 + 2 inputs combined to 32-bits
		NetInput.InputSend = ((Uint32)Input.uPad[0]) | (((Uint32)Input.uPad[1])<<16);

        PROF_ENTER("NetPlayClientInput");
        NetPlayClientInput(&NetInput);
        PROF_LEAVE("NetPlayClientInput");

        if (NetInput.eGameState == NETPLAY_GAMESTATE_PLAY)
        {
            if ((_pSystem->GetFrame()+1) != NetInput.uFrame)
            {
				#if CODE_DEBUG
                printf("Not executing frame %d %d\n", NetInput.uFrame, _pSystem->GetFrame());
				#endif
                NetInput.eGameState = NETPLAY_GAMESTATE_PAUSE;
            }

			// we are connected, retrieve input data
	        Input.uPad[0] = (Uint16)NetInput.InputRecv[0];
	        Input.uPad[1] = (Uint16)NetInput.InputRecv[1];
	        Input.uPad[2] = (Uint16)NetInput.InputRecv[2];
	        Input.uPad[3] = (Uint16)NetInput.InputRecv[3];
			Input.uPad[4] = EMUSYS_DEVICE_DISCONNECTED;

			if (Input.uPad[2] == EMUSYS_DEVICE_DISCONNECTED)
			{
				// if controller 3 is disconnected, use controller 2 of first peer
				Input.uPad[2] = (Uint16)(NetInput.InputRecv[0]>>16);
			}

			if (Input.uPad[3] == EMUSYS_DEVICE_DISCONNECTED)
			{
				// if controller 4 is disconnected, use controller 2 of second peer
				Input.uPad[3] = (Uint16)(NetInput.InputRecv[1]>>16);
			}
		
        }  
		else
		{
		
            if (s_pMovieClip->IsPlaying())
            {
                if (!s_pMovieClip->PlayFrame(Input))
                {
                    s_pMovieClip->PlayEnd();
                    ConPrint("Movie: Play End\n");
                }
            }
	
		}

        if (NetInput.eGameState != NETPLAY_GAMESTATE_PAUSE)
        {
			Emu::System::ModeE eMode;

            #if MAINLOOP_HISTORY
            if (_nHistory < 16384 * 2)
            {
                _History[_nHistory++] = Input.uPad[0];
                _History[_nHistory++] = Input.uPad[1];
                _History[_nHistory++] = Input.uPad[2];
                _History[_nHistory++] = Input.uPad[3];
            }
            #endif

			_uInputFrame    = NetInput.uFrame;
			_uInputChecksum[0] += Input.uPad[0];
			_uInputChecksum[1] += Input.uPad[1];
			_uInputChecksum[2] += Input.uPad[2];
			_uInputChecksum[3] += Input.uPad[3];
			_uInputChecksum[4] += Input.uPad[4];

			eMode = (NetInput.eGameState == NETPLAY_GAMESTATE_IDLE) ? Emu::System::MODE_ACCURATENONDETERMINISTIC : Emu::System::MODE_INACCURATEDETERMINISTIC;

            if (s_pMovieClip->IsRecording())
            {
                if (!s_pMovieClip->RecordFrame(Input))
                {
                    s_pMovieClip->RecordEnd();
                    ConPrint("Movie: Reached end of record buffer!\n");
                }
            }

            GPPrimDisableZBuf();

            /* Phase 2 of the NES integration: dispatch ExecuteFrame
               through the polymorphic Emu::System* when the loaded
               system is the NES wrapper.  The SNES path stays on its
               bespoke _ExecuteSnes helper that does the PPU upload
               and CLUT bookkeeping.  NesSystem renders directly into
               the surface (Phase 2 = diagnostic test pattern) and we
               upload to the EE texture from here. */
            if (_pSystem == _pNes)
            {
                PROF_ENTER("NesExecuteFrame");
                _pNes->ExecuteFrame(&Input, pSurface, pMixBuffer, eMode);
                PROF_LEAVE("NesExecuteFrame");
                PROF_ENTER("NesTexUpload");
                TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
                PROF_LEAVE("NesTexUpload");
            }
            else if (_pSystem == _pSega)
            {
                /* AURORA_PD_HOST_CADENCE_EXEC_V1_20260821
                 *
                 * O core produz 50 fps em PAL e 60 fps em NTSC, enquanto o
                 * Aurora apresenta uma vez por VBlank do GS. Quando as famílias
                 * diferem, converta a cadência no frontend:
                 *
                 *   PAL 50 -> NTSC ~59.94: repete periodicamente um frame;
                 *   NTSC 60 -> PAL 50: executa periodicamente dois frames e
                 *                       apresenta apenas o mais novo.
                 *
                 * AURORA_PD_HOST_CADENCE_ALL_SEGA_V6_20260821
                 * O one-shot skip do V2 faz o primeiro frame de um burst 60->50
                 * avançar CPU+áudio sem reescrever vídeo. Assim o caminho RGBA
                 * também pode usar 0/2 ExecuteFrame(): num tick sem core frame,
                 * _OutTex simplesmente reapresenta a última imagem residente.
                 * Netplay e movies permanecem 1:1 para preservar determinismo.
                 */
                static Uint32 sPdHostNum = 0;
                static Uint32 sPdHostDen = 0;
                static Int32 sPdGameHz = 0;
                /* AURORA_PD_NTSC_5994_CLOCK_V1_20260822
                 * Phase is rational; all products stay below ~61 million. */
                static Uint32 sPdPhase = 0;

                Bool bDirectSega;
                Int32 executeFrames = 1;

                PicoDriveBridge_SetRegion((int)g_SnesForceRegion);
                PicoDriveBridge_SetMouseInput(
                    bSnesMouse ? true : false,
                    (int)nSnesMouseX,
                    (int)nSnesMouseY,
                    (unsigned)uSnesMouseButtons);

                bDirectSega =
                    PicoDriveBridge_CanDirectGsVideo() ? TRUE : FALSE;

                if (NetInput.eGameState == NETPLAY_GAMESTATE_IDLE &&
                    /* AURORA_PD_HOST_CADENCE_ALL_SEGA_V6_GATE_20260821 */
                    !s_pMovieClip->IsPlaying() &&
                    !s_pMovieClip->IsRecording())
                {
                    Uint32 hostNum = 60000, hostDen = 1001;
                    Uint32 gameNum, gameDen;
                    Uint32 add, threshold;
                    Int32 gameHz;

                    GSK_GetRefreshRate(&hostNum, &hostDen);
                    if (hostNum == 0 || hostDen == 0)
                    {
                        hostNum = 60000;
                        hostDen = 1001;
                    }

                    gameHz = PicoDriveBridge_GetNominalFrameRate();

                    /* Match the PS2/core sound timebase above: PAL is 50/1;
                     * Aurora's NTSC PicoDrive presentation is 60000/1001.
                     * Do not collapse NTSC to integer 60 here. */
                    if (gameHz == 50)
                    {
                        gameNum = 50;
                        gameDen = 1;
                    }
                    else
                    {
                        gameNum = 60000;
                        gameDen = 1001;
                    }

                    add = gameNum * hostDen;
                    threshold = hostNum * gameDen;

                    if (add != threshold)
                    {
                        /* AURORA_PD_CADENCE_RESUME_FIRST_FRAME_V7_RESET_20260821 */
                        if (bGameplayJustStarted ||
                            _pSega->GetFrame() == 0 ||
                            hostNum != sPdHostNum ||
                            hostDen != sPdHostDen ||
                            gameHz != sPdGameHz)
                        {
                            sPdHostNum = hostNum;
                            sPdHostDen = hostDen;
                            sPdGameHz = gameHz;
                            sPdPhase =
                                (add < threshold && threshold > 0)
                                ? threshold - 1 : 0;
                        }

                        executeFrames = 0;
                        sPdPhase += add;

                        while (sPdPhase >= threshold &&
                               executeFrames < 2)
                        {
                            sPdPhase -= threshold;
                            ++executeFrames;
                        }
                    }
                    else
                    {
                        sPdHostNum = hostNum;
                        sPdHostDen = hostDen;
                        sPdGameHz = gameHz;
                        sPdPhase = 0;
                    }
                }
                else
                {
                    sPdHostNum = 0;
                    sPdHostDen = 0;
                    sPdGameHz = 0;
                    sPdPhase = 0;
                }

                PROF_ENTER("SegaExecuteFrame");
                for (Int32 iPdFrame = 0;
                     iPdFrame < executeFrames;
                     ++iPdFrame)
                {
                    /* AURORA_PD_SKIP_DISCARDED_VIDEO_V2_EXEC_20260821
                     * In 60->50 conversion, the first of a two-frame burst is
                     * never displayed. CPU + audio still advance; only video
                     * rendering is skipped for that discarded frame. */
                    PicoDriveBridge_SetSkipVideo(
                        (executeFrames > 1 &&
                         iPdFrame + 1 < executeFrames) ? true : false);

                    /* Sempre passe pSurface como fallback. No caminho direto
                     * ela não é tocada; se a geometria mudar durante retro_run,
                     * o bridge ainda consegue produzir o RGBA de segurança. */
                    _pSega->ExecuteFrame(
                        &Input, pSurface, pMixBuffer, eMode);
                }
                PROF_LEAVE("SegaExecuteFrame");

                /* Releia depois de retro_run(): a geometria/PSM do core pode
                 * ter mudado durante o frame. */
                bDirectSega =
                    PicoDriveBridge_CanDirectGsVideo() ? TRUE : FALSE;

                if (!bDirectSega && executeFrames > 0)
                {
                    PROF_ENTER("SegaTexUpload");
                    TextureUpload(&_OutTex, pSurface->GetLinePtr(0));
                    PROF_LEAVE("SegaTexUpload");
                }
            }
            else
            {
                /* AURORA_MEGA_V2_SNES_AUDIO_CLOCK
                   Match produced PCM to the GS VBlank clock. Keep this
                   SNES-only so the QuickNES path is unchanged. */
                if (_AudMix)
                {
                    Uint32 uRateNum = 60, uRateDen = 1;
                    GSK_GetRefreshRate(&uRateNum, &uRateDen);
                    _AudMix->SetFrameRateRational(uRateNum, uRateDen);
                }
				if (bSnesMouse)
					Input.uPad[0] = EMUSYS_DEVICE_DISCONNECTED;
				if (bSnesMouse || _SnesMouseWasActive)
					_pSnes->SetMouseInput(
						bSnesMouse,
						nSnesMouseX,
						nSnesMouseY,
						uSnesMouseButtons);
				_SnesMouseWasActive = bSnesMouse;
				_ExecuteSnes(pSurface, pMixBuffer, &Input, eMode);
            }
		    _iframetex^=1;
        }

        /* AURORA_AUDIO_FAILSOFT_POSTVBLANK_V1
         * Host-audio RPC drain moved to MainLoopRender(), AFTER GSK_SyncFlip.
         * Do not spend synchronous SIF time before this frame is presented. */
    }

    _MainLoopCheckSRAM();
	/* Deferred menu work runs only after _MenuEnable has returned. In the
	   L2+R2 path this leaves two complete frames for the menu/status to become
	   visible before a synchronous SRAM write begins. */
	_MenuRuntimeUpdate();

	MainLoopRender();

	/* AURORA_MOUSE_EXPLICIT_V4: no SIF mouse RPC in Off/Controller,
	   menus, NES, SMS or GG. First resumed USB frame only drains stale motion. */
	{
		Bool bUsbGameplay =
			(!_bMenu &&
			 (_pSystem == _pSnes ||
			  (_pSystem == _pSega && !PicoDriveBridge_Is8Bit())) &&
			 !_MainLoop_BlackScreen &&
			 InputSnesMouseGetMode() == INPUT_SNES_MOUSE_USB) ? TRUE : FALSE;
		if (bUsbGameplay)
			InputMousePollPostFrame(_UsbMouseGameplayWasActive ? TRUE : FALSE);
		else if (_UsbMouseGameplayWasActive)
			InputMouseClearSnapshot();
		_UsbMouseGameplayWasActive = bUsbGameplay;
	}

    PROF_LEAVE("Frame");

    #if PROF_ENABLED
    ProfProcess();
    #endif

    return TRUE;
}
