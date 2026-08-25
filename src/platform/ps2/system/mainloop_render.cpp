/* mainloop_render.cpp
 *
 * Hosts MainLoopRender() and the file-static state it needs
 * (_uVblankCycle and the screen-size #defines used to clear / blit the
 * SNES output texture).
 *
 * Extracted from mainloop.cpp during the Batch 3 split. No logic,
 * literal, or attribute change.
 */

#include <stdio.h>

#include "mainloop_debug.h"
#include "mainloop_shared.h"
#include "mainloop_ui.h"
#include "mainloop_bgm.h"
#include "sega/picodrive/picodrive_bridge.h"
/* AURORA_SNES9X2010_V5_ALLCORES_PERF_20260824 */
#include "nes/quicknes/quicknes_bridge.h"
#include "pce/beetle/pce_bridge.h"

/* AURORA_SNES9X2010_V4_PS2_PERF_20260824 */
#include "snes/snes9x2010/snes9x2010_bridge.h"
#include "types.h"
#include "console.h"
#include "snes.h"
#include "rendersurface.h"
#include "texture.h"
#include "font.h"
#include "poly.h"
#include "prof.h"
#include "snstate.h"
#include "snppublend_gs.h"
#include "common/debug/dbgterm.h"

#include "mainloop_iop.h"

extern "C" {
#include "hw.h"
#include "gs.h"
#include "gpfifo.h"
#include "gpprim.h"
#include "gskit_backend.h"
#include "audio.h"
};

extern "C" {
#include "mcsave_ee.h"
};


/* Same MAINLOOP_SCREENWIDTH / HEIGHT pair as mainloop_init.cpp. The
   render path uses these to size the output blit; the init path uses
   them to size the GS framebuffer. Three other historical layouts are
   kept commented out in mainloop_init.cpp for reference. */
#define MAINLOOP_SCREENWIDTH 256
#define MAINLOOP_SCREENHEIGHT 240


static Uint32 _uVblankCycle;

void MainLoopRender()
{
	static Uint32 _iFrame=0;
        static int whichdrawbuf = 0;

        /* AURORA_SEGA_UI_PRESENTATION_V3
         *
         * Keep two different host presentations:
         *
         *   UI / browser / prompts -> Aurora's normal 256-wide raster
         *   plain MD gameplay      -> native 320-wide raster in 240p
         *   SMS and other systems  -> normal 256-wide raster
         *
         * The important distinction from the old late-init path is that
         * the FIRST MD 320 raster is still selected in mainloop_load.cpp
         * before PicoDrive is initialised. These later switches happen
         * only when entering/leaving Aurora's internal UI.
         *
         * In 480i/1080i MainLoopEnsureGameplayRasterWidth() does not
         * rebuild the GS; those modes keep their normal 640 framebuffer.
         */
        {
            Int32 wantedRaster = 256;
            const Bool bPlainMd =
                (_pSystem == _pSega &&
                 PicoDriveBridge_IsMegaDrive()) ? TRUE : FALSE;

            /* AURORA_MD_UI256_320FB_V1_20260823
             * Keep 320 for the whole active MD cartridge lifetime to avoid
             * rebuilding gsKit under large-ROM memory pressure. */
            if (bPlainMd)
                wantedRaster = 320;

            /* Menu/prompts use exact 256-source integer presentation on the
             * still-alive 320 framebuffer, not 256->320 resampling. */
            GSK_SetUi256On320Framebuffer(
                (_bMenu && bPlainMd) ? 1 : 0);

            if (_bMenu)
                GSK_SetNative240pPar(0);

            if (!MainLoopEnsureGameplayRasterWidth(wantedRaster))
            {
                printf("[video] warning: could not switch to %d-wide presentation\n",
                       (int)wantedRaster);
            }
        }



        /*
         * NES/SNES 240p: keep the framebuffer strictly 256x240 (1 source
         * pixel = 1 framebuffer pixel) and correct horizontal size at
         * the PCRTC level instead. This avoids uneven pixel widths
         * caused by scaling 256 pixels into a smaller PolyRect.
         */
        static int s_native240pPar = -1;
        int native240pPar =
            (g_GskVideoMode == GSK_VIDMODE_240P &&
             (_pSystem == _pNes ||
              _pSystem == _pSnes ||
              _pSystem == _pSnes9x2010 ||
              (_pSystem == _pSega &&
               (PicoDriveBridge_IsMegaDrive() ||
                PicoDriveBridge_IsMasterSystem()))) &&
             !_bMenu) ? 1 : 0;

        if (native240pPar != s_native240pPar)
        {
            GSK_SetNative240pPar(native240pPar);
            s_native240pPar = native240pPar;
        }

        /* AURORA_MD_YOFFSET_MINUS9_V1
         * Plain MD only: effective Y = configured menu Y - 9. */
        {
            static int s_lastMdYBias = 9999;
            int mdYBias = 0;

            if (mdYBias != s_lastMdYBias)
            {
                GSK_SetGameplayYOffsetBias(mdYBias);
                s_lastMdYBias = mdYBias;
            }
        }



    /* Re-anchor FRAME_1 to gsKit's current draw buffer before any
       primitive runs this frame. The legacy GS_SetDrawFB used to do
       this implicitly per frame; gsKit_sync_flip only swaps the
       display buffer, not the draw buffer. Without this, prims drew
       to a stale (or, after the SNES blender ran, completely wrong)
       buffer and the visible framebuffer flickered black on every
       other frame. See gskit_backend.h for the longer rationale. */
    /* AURORA_GS_PARTIAL_GAMEPLAY_CLEAR_V1
     * Only enable the reduced clear when this frame is guaranteed to draw the
     * normal game output texture. Menu, boot and black-screen frames retain
     * the complete physical clear. */
    /* AURORA_PD_DIRECT_MD_SKIP_CLEAR_V3_RENDER_20260821
     * DrawDirectGs() for plain MD always emits an opaque backdrop over
     * the entire transformed 256x240 canvas, which is the entire active
     * framebuffer in 240p/480i/1080i. Avoid clearing pixels that are
     * guaranteed to be replaced later in this same frame. */
    /* AURORA_PD_DIRECT_8BIT_SKIP_CLEAR_V4_20260821 */
    /* AURORA_PCE_EXPERIMENTAL_V13_NATIVE_PAR_REBUILD
     *
     * PCE base geometry is 256x240 with a narrower-than-4:3 presentation.
     * Use the same native-240p PCRTC technique already used by Aurora's
     * NES/SNES path: change scanout magnification, NOT framebuffer sampling.
     * Every one of the 256 source columns is still scanned exactly once.
     *
     * Keep menu/black-screen presentation untouched. The loaded system remains
     * _pPce while Aurora's menu is open, so explicitly turn the PCE-only PAR
     * correction back off there. GSK_SetNative240pPar internally affects only
     * the 240p display mode.
     */
    if (_pSystem == _pPce)
    {
        GSK_SetNative240pPar(
            (!_bMenu && !_MainLoop_BlackScreen) ? 1 : 0);
    }

    GSK_SetGameplaySkipClear(
        (!_bMenu && !_MainLoop_BlackScreen &&
         (((_pSystem == _pSega) &&
           PicoDriveBridge_CanDirectGsVideo() &&
           (PicoDriveBridge_IsMegaDrive() ||
            g_GskVideoMode == GSK_VIDMODE_240P)) ||
          ((_pSystem == _pPce) &&
           PceBridge_CanDirectGsVideo()))) ? TRUE : FALSE);
    GSK_SetGameplayFastClear(
        (!_bMenu && _pSystem && !_MainLoop_BlackScreen) ? TRUE : FALSE);
    GSK_ResetFrame();

    // render frame
    GPPrimDisableZBuf();

    /* Per-frame full-screen clear to black.
     *
     * MainLoopRender historically NEVER cleared the framebuffer: it
     * relied on the full-screen _OutTex blit below to repaint every
     * pixel.  But that blit is (a) skipped entirely when
     * _MainLoop_BlackScreen is set (boot log + menus) and (b) even when
     * drawn it starts at dy=8, so the top rows are never touched.  With
     * DoubleBuffering=ON each draw goes to the alternate buffer, so any
     * row we don't repaint shows stale content from two frames ago --
     * which appears as a fixed-position horizontal "faixa"/stripe
     * through the text (worst in the log and menus, where nothing
     * covers the background).  Clearing to black first costs a single
     * sprite and removes the band entirely.  GSK_ResetFrame now clears
     * the complete PHYSICAL framebuffer, including any overscan borders.
     * Keep the Poly state reset here, but do not queue a duplicate
     * logical-canvas clear. */
    PolyTexture(NULL);
    PolyBlend(FALSE);
    PolyColor4f(0.0f, 0.0f, 0.0f, 1.0f);

	if (!_MainLoop_BlackScreen)
	{
//		Float32 fDestColor = (_bMenu || _MainLoop_ModalCount) ? 0.10f : 0.80f;
		Float32 fDestColor = 0.10f;
		
		if  (!_bMenu && !_MainLoop_ModalCount)
		{
			fDestColor = _MainLoop_fOutputIntensity;
		}

		static Float32 fColor=0.0f;
		Float32 dx = 0.0f;
		Float32 dy = 8.0f;

		if (fColor < fDestColor) 
		{
			fColor+=0.06f;
			if (fColor > fDestColor) 
			{
				fColor = fDestColor;
			}
		} 

		if (fColor > fDestColor) 
		{
			fColor-=0.06f;
			if (fColor < fDestColor) 
			{
				fColor = fDestColor;
			}
		}


        /* AURORA_PD_DIRECT_8BIT_GS_V1_20260821 */
        if (_pSystem == _pNes &&
            QuicknesBridge_CanDirectGsVideo())
        {
            QuicknesBridge_DrawDirectGs(
                _MainLoop_uOutTexTBP,
                g_GskVideoMode == GSK_VIDMODE_240P ? 2 : 4,
                fColor);
        }
        else if (_pSystem == _pSnes9x2010 &&
                 Snes9x2010Bridge_CanDirectGsVideo())
        {
            Snes9x2010Bridge_DrawDirectGs(
                _MainLoop_uOutTexTBP, fColor);
        }
        else if (_pSystem == _pPce &&
                 PceBridge_CanDirectGsVideo())
        {
            /* AURORA_PCE_EXPERIMENTAL_V10_DIRECT_GS */
            PceBridge_DrawDirectGs(
                _MainLoop_uOutTexTBP, fColor);
        }
        else if (_pSystem == _pSega &&
                 PicoDriveBridge_CanDirectGsVideo())
        {
            /* AURORA_PD_DIRECT_T8_RENDER */
            PicoDriveBridge_DrawDirectGs(
                _MainLoop_uOutTexTBP, fColor);
        }
        else
        {
            PolyBlend(FALSE);
            PolyTexture(&_OutTex);
            PolyUV(0,0,256,240);
    		PolyColor4f(fColor, fColor, fColor, 1.0f);
    
    
                    if (g_GskVideoMode == GSK_VIDMODE_240P && _pSystem == _pNes)
            {
    /*
     * InfoNES 240p overscan compensation.
     *
     * Keep the NES framebuffer at its native 256x240 size and
     * preserve a 1:1 pixel mapping. Only reposition the image
     * to compensate for CRT overscan.
     */
    PolyRect(0.0f, 2.0f, 256.0f, 240.0f);
            }
            else if (g_GskVideoMode == GSK_VIDMODE_240P && _pSystem == _pSega)
            {
    /* AURORA_SEGA_NATIVE_240P_V1
     * PicoDriveBridge already composes MD/SMS/GG into an exact 256x240 logical
     * raster. Present that raster 1:1; PCRTC handles the CRT pixel aspect. */
    PolyRect(0.0f, 0.0f, 256.0f, 240.0f);
            }
            else if (_pSystem == _pSnes || _pSystem == _pSnes9x2010)
            {
    PolyRect(0.0f, 8.0f, 256.0f, 240.0f);
            }
            else
            {
    PolyRect(0.0f, 4.0f, 256.0f, 240.0f);
            }
    
            PolyBlend(TRUE);
        }
        //PolyTexture(NULL);
        //PolyRect(dx,dy,128,120);
    }


    if (!_bMenu)
    {	
	
		if (s_pMovieClip->IsPlaying())
		{
	        FontSelect(2);
	        FontColor4f(0.5, 0.5f, 0.5f, 1.0f);
	        FontPrintf(240,220, ">");
		}

		if (s_pMovieClip->IsRecording())
		{
	        FontSelect(2);
	        FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
	        FontPrintf(240,220, "O");
		}


		switch (_MainLoop_uDebugDisplay)
        {
		case 0:
/*	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,170, "%08X", InputGetPadData(0));
  */

//		        FontSelect(2);
//		        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
//		        FontPrintf(40,190, "%3d", xpadGetFrameCount(0,0));
			break;
		case 1:
		/*
	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,190, "%3d %3d", NetInput.InputSize[0], NetInput.OutputSize[0]);
	        FontPrintf(40,200, "%3d %3d", NetInput.InputSize[1], NetInput.OutputSize[1]);
	        FontPrintf(40,210, "%3d %3d", NetInput.InputSize[2], NetInput.OutputSize[2]);
	        FontPrintf(40,220, "%3d %3d", NetInput.InputSize[3], NetInput.OutputSize[3]);
			*/
			break;
		case 2:
	        FontSelect(2);
	        FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
	        FontPrintf(40,170, "%08X", _uInputFrame);
	        FontPrintf(40,180, "%08X", _uInputChecksum[0]);
	        FontPrintf(40,190, "%08X", _uInputChecksum[1]);
	        FontPrintf(40,200, "%08X", _uInputChecksum[2]);
	        FontPrintf(40,210, "%08X", _uInputChecksum[3]);
	        FontPrintf(40,220, "%08X", _uInputChecksum[4]);
			break;
		case 3:
			FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
			FontPrintf(195, 210, "%8d", _uVblankCycle / 1024);
			break;
        }

        FontSelect(2);
		FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
		{

/*
		FontPrintf(15, 180, "%08X %08X Y", (Int32)(_ColorCalib.y_mul * 0x10000), (Int32)(_ColorCalib.y_add * 0x10000));
		FontPrintf(15, 190, "%08X %08X I", (Int32)(_ColorCalib.i_mul * 0x10000), (Int32)(_ColorCalib.i_add * 0x10000));
		FontPrintf(15, 200, "%08X %08X Q", (Int32)(_ColorCalib.q_mul * 0x10000), (Int32)(_ColorCalib.q_add * 0x10000));
  */

		/*
		FontPrintf(195, 180, "%6.3f %6.3f", _ColorCalib.y_mul, _ColorCalib.y_add);
		FontPrintf(195, 190, "%6.3f %6.3f", _ColorCalib.i_mul, _ColorCalib.i_add);
		FontPrintf(195, 200, "%6.3f %6.3f", _ColorCalib.q_mul, _ColorCalib.q_add);
		*/
		}

//			FontPrintf(195, 210, "%8d", _AudMix.GetLastOutput());
    }


	/* Keep menu audio alive even while a modal overlays the UI. Previously
	   BgmUpdate lived only in the non-modal branch below, so every fixed-time
	   message starved audsrv regardless of whether any I/O was happening. */
	if (_bMenu)
	{
		static Bool s_bgmArmed = FALSE;
		if ((void *)_MainLoop_pScreen == (void *)_MainLoop_pBrowserScreen)
			s_bgmArmed = TRUE;
		if (s_bgmArmed)
			BgmUpdate();
		/* Draw the live menu first, then place modal/status text on top. The
		   previous order painted _MenuDraw after the status and hid it. */
		_MenuDraw();
	}

	if (_MainLoop_ModalCount > 0)
	{
		FontSelect(0);
		FontColor4f(1.0, 1.0f, 1.0f, 1.0f);
		FontPrintf(128 - FontGetStrWidth(_MainLoop_ModalStr) / 2,100, _MainLoop_ModalStr);

		_MainLoop_ModalCount--;
	}
	else
	{
		if (_MainLoop_StatusCount > 0)
		{
			FontSelect(0);
			/* AURORA_V15_STATUS_OUTLINE_20260824
			 * Faux 1px outline only for transient cyan status prompts.
			 * Two black underprints are deliberately local to this path: the
			 * menu/font renderer and every persistent UI label stay untouched. */
			FontColor4f(0.0f, 0.0f, 0.0f, 1.0f);
			FontPrintf(19, 201, _MainLoop_StatusStr);
			FontPrintf(21, 201, _MainLoop_StatusStr);
			FontColor4f(0.0f, 0.8f, 0.8f, 1.0f);
			FontPrintf(20, 200, _MainLoop_StatusStr);

			_MainLoop_StatusCount--;
		}
	}


	#if CODE_DEBUG
	if (_MainLoop_bMCSaveReady && MCSave_WriteSync(FALSE, NULL))
	{
		FontSelect(1);
		FontColor4f(1.0, 0.0f, 0.0f, 1.0f);
		if (_iFrame & 4)
			FontPrintf(235,216, "#");
	}
	#endif



    PROF_ENTER("GPFlush");
    GPFifoFlush();
    PROF_LEAVE("GPFlush");

    /* gsKit_sync_flip waits for vsync, swaps the display buffer
       and resets gsKit's draw queue for the next frame. The
       legacy WaitForNextVRstart / GS_SetCrtFB / GS_SetDrawFB
       block is now subsumed by this single call. */
    PROF_ENTER("WaitVBlank");
    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle();
    GSK_SyncFlip();
    if ( (_iFrame&15)==0)   _uVblankCycle = ProfCtrGetCycle() - _uVblankCycle;
    PROF_LEAVE("WaitVBlank");

    /* AURORA_AUDIO_FAILSOFT_POSTVBLANK_V1
     * The frame is already presented. Keep the normal gameplay audio drain
     * here so synchronous audsrv RPC latency is moved out of the pre-render
     * deadline. Aud_BufferedAsyncStart uses only wait=0 drains. */
    if (!_bMenu && _pSystem && !_MainLoop_BlackScreen)
        Aud_BufferedAsyncStart();

    /* whichdrawbuf is now decorative - gsKit owns the active
       framebuffer index via gsGlobal->ActiveBuffer. Keep it
       alive so the diff against the original is small. */
    whichdrawbuf ^= 1;
    (void)whichdrawbuf;

    _iFrame++;
}
