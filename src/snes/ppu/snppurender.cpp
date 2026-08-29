

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "snppu.h"
#include "snppurender.h"
#include "snppuchrcache.h"
#include "rendersurface.h"
#include "snmask.h"
#include "snmaskop.h"
#include "prof.h"
#include "sndbglog.h"
#if CODE_PLATFORM == CODE_PS2
#include "ps2mem.h"
#include "ps2dma.h"
#endif

#define SNPPURENDER_INFOSCRATCHPAD ((CODE_PLATFORM == CODE_PS2) && TRUE)

/*

render order

mode 01, bgmode8=0

bg4lo
bg3lo
obj0
bg4hi
bg3hi
obj1
bg2lo
bg1lo
obj2
bg2hi
bg1hi
obj3

mode 01, bgmode8=1

bg4lo
bg3lo
obj0
bg4hi
obj1
bg2lo
bg1lo
obj2
bg2hi
bg1hi
obj3
bg3hi

*/

Uint8 _tm = 0x3F;
Uint8 _tmw = 0x3F;
Uint8 _ts = 0x3F;
Uint8 _tsw = 0x3F;

/* AURORA_V85_SOFTWARE_HACKS_STATE
 * 0x1f = BG1..BG4+OBJ enabled. flags==0 = unchanged V8.3 output. */
Uint8 g_SnesSoftwareLayerMask = (Uint8)(
    SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2 | SNESPPU_MASK_BG3 |
    SNESPPU_MASK_BG4 | SNESPPU_MASK_OBJ);
Uint8 g_SnesSoftwareHackFlags = 0;
Uint8 g_SnesObjLimitLevel = SNPPU_OBJ_LIMIT_OFF;
Uint8 g_SnesObjLimitMode = SNPPU_OBJ_LIMIT_MODE_SCANLINE;
Int32 g_SnesObjLimitTileBudget = SNPPU_MAXOBJCHR;
Int32 g_SnesObjLimitScreenBudget = SNESPPU_OBJ_NUM;
Uint32 g_SnesObjLimitFramePhase = 0;
static Bool g_SnesObjLimitVisibilityDirty = TRUE;

static void _SNPPURenderRefreshObjLimitCache()
{
	Int32 nBudget;
	switch (g_SnesObjLimitLevel)
	{
		case SNPPU_OBJ_LIMIT_LIGHT: nBudget=28; break;
		case SNPPU_OBJ_LIMIT_MEDIUM: nBudget=24; break;
		case SNPPU_OBJ_LIMIT_STRONG: nBudget=20; break;
		case SNPPU_OBJ_LIMIT_EXTREME: nBudget=16; break;
		case SNPPU_OBJ_LIMIT_HEAVY: nBudget=12; break;
		case SNPPU_OBJ_LIMIT_INSANE: nBudget=8; break;
		default: nBudget=SNPPU_MAXOBJCHR; break;
	}
	g_SnesObjLimitTileBudget =
		(g_SnesObjLimitMode==SNPPU_OBJ_LIMIT_MODE_SCANLINE &&
		 g_SnesObjLimitLevel!=SNPPU_OBJ_LIMIT_OFF) ? nBudget : SNPPU_MAXOBJCHR;
	g_SnesObjLimitScreenBudget =
		(g_SnesObjLimitMode==SNPPU_OBJ_LIMIT_MODE_SCREEN &&
		 g_SnesObjLimitLevel!=SNPPU_OBJ_LIMIT_OFF) ? nBudget : SNESPPU_OBJ_NUM;
}

/* AURORA_SONIC_BLAST_MAN_COLOR_V7
 * Set by the ROM loader only for exact Sonic Blast Man CRCs. */
extern Bool g_SnesCompatSonicBlastManColorMath;
static Uint8 g_SoftwareFramePhase = 0;

void SNPPURenderSetSoftwareLayerMask(Uint8 uMask)
{
    g_SnesSoftwareLayerMask = (Uint8)(uMask &
        (SNESPPU_MASK_BG1 | SNESPPU_MASK_BG2 | SNESPPU_MASK_BG3 |
         SNESPPU_MASK_BG4 | SNESPPU_MASK_OBJ));
}

void SNPPURenderSetSoftwareHackFlags(Uint8 uFlags)
{
    Uint8 uNew = (Uint8)(uFlags & SNPPU_HACK_ALL);
    if ((uNew ^ g_SnesSoftwareHackFlags) & SNPPU_HACK_FRAME_SKIP)
        g_SoftwareFramePhase = 0;
    g_SnesSoftwareHackFlags = uNew;
}

Bool SNPPURenderShouldRenderFrame(void)
{
    if (!(g_SnesSoftwareHackFlags & SNPPU_HACK_FRAME_SKIP))
    {
        g_SoftwareFramePhase = 0;
        return TRUE;
    }

    Bool bRender = (g_SoftwareFramePhase == 0);
    g_SoftwareFramePhase ^= 1;
    return bRender;
}


/* AURORA_OBJ_LIMIT_HOTPATH_V3 */
void SNPPURenderSetObjLimitLevel(Uint8 uLevel)
{
    if (uLevel >= SNPPU_OBJ_LIMIT_NUM) uLevel=SNPPU_OBJ_LIMIT_OFF;
    if (g_SnesObjLimitLevel != uLevel)
    {
        g_SnesObjLimitLevel=uLevel;
        g_SnesObjLimitFramePhase=0;
        _SNPPURenderRefreshObjLimitCache();
        g_SnesObjLimitVisibilityDirty=TRUE;
    }
}
void SNPPURenderSetObjLimitMode(Uint8 uMode)
{
    if (uMode >= SNPPU_OBJ_LIMIT_MODE_NUM) uMode=SNPPU_OBJ_LIMIT_MODE_SCANLINE;
    if (g_SnesObjLimitMode != uMode)
    {
        g_SnesObjLimitMode=uMode;
        g_SnesObjLimitFramePhase=0;
        _SNPPURenderRefreshObjLimitCache();
        g_SnesObjLimitVisibilityDirty=TRUE;
    }
}


#if CODE_PLATFORM == CODE_PS2
#define PS2_RENDERINFOADDR  (PS2MEM_SCRATCHPAD +  0*1024)
#endif

//
//
//


SnesChrLookupT _SnesPPU_PlaneLookup[2] _ALIGN(32);
Uint8 _SnesPPU_HFlipLookup[2][256] _ALIGN(32);

static Bool _SnesPPU_bInitialized=FALSE;

//
//
//

static Uint8 _HFlipBits(Uint8 Bits)
{
	Uint8 FlipBits=0;

	for (int n=0; n<8; n++)
		if (Bits&(1<<n)) FlipBits|=(0x80 >> n);

	return FlipBits;
}

static void _BuildPlaneLookup()
{
	Uint32 i, iBit;

	for (i=0; i < 256; i++)
	{
		Uint8 *pBits;
		
		pBits = (Uint8 *)&_SnesPPU_PlaneLookup[0][i];
		for (iBit=0; iBit < 8; iBit++)
		{
			pBits[iBit] = ((i<<iBit) & 0x80) ? 1 : 0;
		}

		pBits = (Uint8 *)&_SnesPPU_PlaneLookup[1][i];
		for (iBit=0; iBit < 8; iBit++)
		{
			pBits[iBit] = ((i>>iBit) & 0x01) ? 1 : 0;
		}
	}

	for (i=0; i < 256; i++)
	{
		_SnesPPU_HFlipLookup[0][i] = i;
		_SnesPPU_HFlipLookup[1][i] = _HFlipBits(i);
	}
}

void _DrawMask(Uint32 *pDest, SNMaskT *pMask, Int32 nPixels)
{
	Int32 iPixel;

	for (iPixel=0; iPixel < nPixels; iPixel++)
	{
		Uint8 uMask;

		uMask =	pMask->uMask8[iPixel >> 3] & (1<<(iPixel&7));

		pDest[iPixel] = uMask ? 0 : 0xFFFFFFF;
	}
}

void _DrawMask2(Uint32 *pDest, SNMaskT *pMask1, SNMaskT *pMask2, Int32 nPixels)
{
	Int32 iPixel;
	static Uint32 Lookup[4]= { 0x0, 0xFF, 0xFF00, 0xFFFFFF};

	for (iPixel=0; iPixel < nPixels; iPixel++)
	{
		Uint8 uMask1, uMask2;
		Uint32 uColor;

		uMask1 =	pMask1->uMask8[iPixel >> 3] & (1<<(iPixel&7));
		uMask2 =	pMask2->uMask8[iPixel >> 3] & (1<<(iPixel&7));

		uColor=0;
		if (uMask1) uColor+=1;
		if (uMask2) uColor+=2;

		pDest[iPixel] = Lookup[uColor];
	}
}

void SnesPPURender::RenderLine(Int32 iLine)
{
	if (m_pTarget)
	{
		switch (m_pTarget->GetFormat()->uBitDepth)
		{
		case 16:
			RenderLine16(iLine);
			break;
		case 32:
	   		RenderLine32(iLine, 0);
			break;

		}
	}
}

void SnesPPURender::RenderLine16(Int32 iLine)
{
}

void SnesPPURender::UpdateCGRAM(Uint32 uAddr, Uint16 uData)
{
	/* AURORA_SNES_SAFE_FRAMESKIP_HOST_ELIDE_V1_20260828
	 * Safe Frameskip still updates emulated CGRAM in SnesPPU::WriteCGDATA().
	 * With no render target, updating the host/GS palette is wasted work.
	 * BeginRender() marks SNESPPURENDER_UPDATE_ALL on the next frame, so the
	 * next visible frame rebuilds the complete host palette from emulated CGRAM.
	 */
	if (m_pTarget && m_pRenderInfo && m_pBlend)
	{
		m_pBlend->UpdatePaletteEntry(&m_pRenderInfo->BlendInfo, uAddr, uData, m_pPPU->GetIntensity());
	}
}

void SnesPPURender::RenderLine32(Int32 iLine, Bool bPlanar)
{
	SnesRender8pInfoT *pRenderInfo;
    SNPPUBlendInfoT *pBlendInfo;
	const SnesPPURegsT *pRegs  = m_pPPU->GetRegs();
	/* AURORA_V85_EFFECTIVE_COLOR_PATH
	 * Derive renderer-only values. Never mutate emulated PPU registers. */
	const Uint8 uHackFlags = SNPPURenderGetSoftwareHackFlags();
	const Uint8 uEffectiveCGWSEL =
		(uHackFlags & SNPPU_HACK_WINDOWS_OFF)
		? (Uint8)(pRegs->cgwsel & 0x03) : pRegs->cgwsel;
	Uint8 uEffectiveCGADSUB =
		(uHackFlags & SNPPU_HACK_COLOR_MATH_OFF) ? 0 : pRegs->cgadsub;
#if SNES_SONIC_COLOR_WORKAROUND
	/* Sonic Blast Man workaround: preserve windows/layers, suppress only
	 * CGADSUB color math. This is deliberately narrower than the menu hack. */
	if (g_SnesCompatSonicBlastManColorMath)
		uEffectiveCGADSUB = 0;
#endif

	pRenderInfo = m_pRenderInfo;
    pBlendInfo = &pRenderInfo->BlendInfo;


#if CODE_DEBUG && CODE_PLATFORM==CODE_PS2
static Bool bPrint = TRUE;
	if (bPrint)
	{
		printf("BlendInfo: %X\n", (Uint32)&pRenderInfo->BlendInfo);
		printf("Main: %X\n", (Uint32)pRenderInfo->Main);
		printf("Sub: %X\n", (Uint32)pRenderInfo->Sub);
		printf("BGPlanes: %X\n", (Uint32)pRenderInfo->BGPlanes);
		printf("Tiles: %X\n", (Uint32)pRenderInfo->Tiles);
		printf("Size= %X\n", sizeof(pRenderInfo));
		bPrint=FALSE;
	}
#endif

	if (pRegs->inidisp & 0x80)
	{
        m_pBlend->Clear(pBlendInfo, iLine);
	} else
	{
		SNMaskT ColorMask[3];
		Bool bDirectMain = FALSE;

		if (m_UpdateFlags & SNESPPURENDER_UPDATE_PAL)
		{
            m_pBlend->UpdatePalette(pBlendInfo, m_pPPU->GetCGData(), m_pPPU->GetIntensity());

			m_UpdateFlags &= ~SNESPPURENDER_UPDATE_PAL;
		}

		if ((m_UpdateFlags & SNESPPURENDER_UPDATE_OBJ) ||
		    g_SnesObjLimitVisibilityDirty)
		{
#if SNDBG_LOG
			Uint32 _tObjUpdate = ProfCtrGetCycle();
#endif
			UpdateOBJ(pRenderInfo->uObjY, pRenderInfo->uObjSize);

            PROF_ENTER("UpdateOBJVisibility");
            UpdateOBJVisibility(pRenderInfo->uObjY, pRenderInfo->uObjSize, pRegs->oampri.w, SNESPPU_OBJ_NUM);
            PROF_LEAVE("UpdateOBJVisibility");
#if SNDBG_LOG
			{
				Uint32 _dObjUpdate = ProfCtrGetCycle() - _tObjUpdate;
				g_TmgCycObj += _dObjUpdate;
				g_TmgCycObjUpdate += _dObjUpdate;
			}
#endif

			m_UpdateFlags &= ~SNESPPURENDER_UPDATE_OBJ;
			g_SnesObjLimitVisibilityDirty = FALSE;
		}

		/* Tiles and decoded character rows are cached across scanlines. A VRAM
		   upload can replace either the tilemap or the character data without
		   changing scroll/base registers, so both update classes must invalidate
		   the cached VRAM addresses. This is especially visible after pause/map
		   screens and SuperFX text overlays. */
		if (m_UpdateFlags &
		    (SNESPPURENDER_UPDATE_BGSCR | SNESPPURENDER_UPDATE_BGCHR))
		{
            pRenderInfo->uBGVramAddr[0] = 0xFFFFFFFF;
            pRenderInfo->uBGVramAddr[1] = 0xFFFFFFFF; 
            pRenderInfo->uBGVramAddr[2] = 0xFFFFFFFF; 
            pRenderInfo->uBGVramAddr[3] = 0xFFFFFFFF; 

			m_UpdateFlags &= ~(SNESPPURENDER_UPDATE_BGSCR |
			                   SNESPPURENDER_UPDATE_BGCHR);
		}

	    if (m_UpdateFlags & SNESPPURENDER_UPDATE_WINDOW)
        {
			if (!(uHackFlags & SNPPU_HACK_WINDOWS_OFF))
			DecodeWindows(pRenderInfo->WindowMask, pRenderInfo->BGWindow);
    		m_UpdateFlags &= ~SNESPPURENDER_UPDATE_WINDOW;
        }

		// render line
		RenderLine8(iLine, pRenderInfo);

#if SNDBG_LOG
		Uint32 _tColorMath = ProfCtrGetCycle();
#endif

#if CODE_PLATFORM == CODE_PS2
		/* If no main-screen source is selected by CGADSUB, the sub screen and
		   all add/sub masks are mathematically unable to change the result.
		   With main clipping disabled and brightness at 15, the GS can expand
		   the indexed main line directly into the output texture. */
		bDirectMain = (uEffectiveCGADSUB & 0x3F) == 0 &&
		              (uEffectiveCGWSEL & 0xC0) == 0 &&
		              m_pPPU->GetIntensity() == 15;
#endif

		// determine color window mask for main screen
		// 0 = disabled (masked)
        // 1 = enabled
		if (!bDirectMain)
		{
			switch ((uEffectiveCGWSEL >> 6) & 3)
			{
			case 0:	// all the time
				SNMaskSet(&ColorMask[0]);
				break;
			case 1: // inside color window
				SNMaskCopy(&ColorMask[0], &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 2:	// outside color window
				SNMaskNOT(&ColorMask[0], &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 3: // confirmed: never.
			default:
				SNMaskClear(&ColorMask[0]);
				break;
			}

			// determine color window mask for sub screen
			// 0 = disabled (masked)
			// 1 = enabled
			switch ((uEffectiveCGWSEL >> 4) & 3)
			{
			case 0:	// enabled all the time (only when add/sub layers of main screen are opaque)
				SNMaskCopy(&ColorMask[1], &pRenderInfo->MainAddSubMask);
				break;
			case 1: // inside color window
				SNMaskAND(&ColorMask[1], &pRenderInfo->MainAddSubMask, &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 2:	// outside color window
				SNMaskANDN(&ColorMask[1], &pRenderInfo->MainAddSubMask, &pRenderInfo->BGWindow[SNPPU_BGWINDOW_COLOR]);
				break;
			case 3: // confirmed: never
			default:
				SNMaskClear(&ColorMask[1]);
				break;
			}

			// determine pixels that are subject to 1/2 color add/sub
			// these are the:
			//      layers of the mainscreen that are set in the cgadsub register that are not obscured by color window
			//      ANDed with the enabled pixels of the subscreen (opaque, fixed color, and windowed)
			// Quoth: "in the back color constant area on the sub screen, it does not	become 1/2"
			// there will never be a case where 1/2 is applied to a main or subscreen color that has been masked by color window
			if (uEffectiveCGADSUB & 0x40)
			{
				// 0 = disabled
				// 1 = 1/2 add sub enabled
				SNMaskAND(&ColorMask[2], &pRenderInfo->SubAddSubMask, &ColorMask[1]);
				SNMaskAND(&ColorMask[2], &ColorMask[2], &ColorMask[0]);
			} else
			{
				// 1/2 disabled
				SNMaskClear(&ColorMask[2]);
			}
		}

		// perform color blending of main+sub
#if SNDBG_LOG
		g_TmgCycColorMath += ProfCtrGetCycle() - _tColorMath;
		Uint32 _tBlend = ProfCtrGetCycle();
#endif
        m_pBlend->Exec(
            pBlendInfo,
            iLine,
            pRegs->coldata,
			bDirectMain ? NULL : ColorMask,
            (uEffectiveCGADSUB & 0x80),
            m_pPPU->GetIntensity()
            );
#if SNDBG_LOG
		g_TmgCycBlend += ProfCtrGetCycle() - _tBlend;
#endif
	}
}


#if CODE_PLATFORM == CODE_PS2
#include "snppublend_gs.h"
/* TBPs of the blender scratchpad slab and the SNES output texture, both
   allocated via gsKit's VRAM allocator in MainLoopInit() after the
   mode-specific framebuffers. A zero address is a fatal boot-time VRAM
   allocation failure, so MainLoopInit() never reaches this renderer then. */
extern Uint32 _MainLoop_uBlenderTBP;
extern Uint32 _MainLoop_uOutTexTBP;
static SNPPUBlendGS *_Blend;
#else

#include "snppublend_c.h"
static SNPPUBlendC _Blend;
#endif

/* AURORA_GS_VRAM_EPOCH_V4_2
 * SNPPUBlendGS caches uPalAddr/uInputAddr/uTempAddr/uOutAddr at construction.
 * After GSK_ReinitVideo() those TBPs no longer belong to the current gsKit
 * allocation epoch. Delete only while no frame is active; BeginRender lazily
 * creates a fresh blender with the current globals on the next SNES frame. */
void SNPPURenderInvalidateGsResources(void)
{
#if CODE_PLATFORM == CODE_PS2
    if (_Blend)
    {
        delete _Blend;
        _Blend = NULL;
    }
#endif
}

#if !SNPPURENDER_INFOSCRATCHPAD
static SnesRender8pInfoT _RenderInfo;
#endif

//#include "snppublend_mm.h"
//static SNPPUBlendMM _Blend;


void SnesPPURender::BeginRender(CRenderSurface *pTarget)
{
	/* AURORA_OBJ_LIMIT_FLICKER_V3
	 * Rotate artificial limiter victims across rendered frames. The
	 * physical SNES 32-OBJ / 34-tile rules are not changed. */
	if (pTarget && g_SnesObjLimitLevel != SNPPU_OBJ_LIMIT_OFF)
	{
		g_SnesObjLimitFramePhase++;
		if (g_SnesObjLimitMode == SNPPU_OBJ_LIMIT_MODE_SCREEN)
			g_SnesObjLimitVisibilityDirty = TRUE;
	}
#if CODE_PLATFORM == CODE_PS2
	if (!_Blend)
	{
		_Blend = new SNPPUBlendGS(_MainLoop_uBlenderTBP,
		                          _MainLoop_uOutTexTBP);
	}
	m_pBlend = _Blend;
#else
	m_pBlend = &_Blend;
#endif

    #if SNPPURENDER_INFOSCRATCHPAD
    m_pRenderInfo = (SnesRender8pInfoT *)PS2_RENDERINFOADDR;
	#else
    m_pRenderInfo = &_RenderInfo;
	#endif

	m_pTarget = pTarget;
	if (pTarget)
	{
		pTarget->Lock();
		pTarget->SetLineOffset(1);
        m_pBlend->Begin(pTarget);
	}

    SetUpdateFlags(SNESPPURENDER_UPDATE_ALL);

    if (!_SnesPPU_bInitialized)
    {
	    _BuildPlaneLookup();
        _SnesPPU_bInitialized = TRUE;
    }
}


void SnesPPURender::EndRender()
{
    #if CODE_PLATFORM == CODE_PS2
    /* AURORA_SNES_SAFE_FRAMESKIP_HOST_ELIDE_V1_20260828
     * A NULL render target produces no visible-frame SPR->RAM work here.
     * Avoid polling DMA8 on frames intentionally hidden by Safe Frameskip. */
    if (m_pTarget)
        DmaSyncSprToRam();
    #endif

	if (m_pTarget)
	{
        m_pBlend->End();
        m_pBlend = NULL;
        m_pTarget->Unlock();
	}

    m_pRenderInfo=NULL;
	m_pTarget=NULL;
}



void SnesPPURender::UpdateVRAM(Uint32 uVramAddr)
{
	UpdateVRAMRange(uVramAddr, 1);
}

void SnesPPURender::UpdateVRAMRange(Uint32 uVramAddr, Uint32 nWords)
{
	SnesPPUInvalidateChrCache(uVramAddr, nWords);

	/* O cache CHR ja foi invalidado acima. Estes bits cuidam dos caches
	   derivados de tilemap/scroll na proxima scanline. */
	SetUpdateFlags(SNESPPURENDER_UPDATE_BGSCR |
	               SNESPPURENDER_UPDATE_BGCHR);
}
