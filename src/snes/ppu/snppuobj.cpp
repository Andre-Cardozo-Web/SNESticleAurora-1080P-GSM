

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "snppu.h"
#include "snppurender.h"
#include "rendersurface.h"
#include "snmask.h"
#include "prof.h"



// OBSEL.5-7 escolhe dois tamanhos. Os modos 6/7 sao retangulares e nao
// podem ser representados por um unico shift, como fazia o renderer antigo.
static const Uint8 _SnesPPU_OAMWidth[8][2]=
{
	{ 8, 16}, { 8, 32}, { 8, 64}, {16, 32},
	{16, 64}, {32, 64}, {16, 32}, {16, 32}
};

static const Uint8 _SnesPPU_OAMHeight[8][2]=
{
	{ 8, 16}, { 8, 32}, { 8, 64}, {16, 32},
	{16, 64}, {32, 64}, {32, 64}, {32, 32}
};

Bool _SnesPPUOBJVisibleX(Uint16 uPosX, Uint8 uWidth)
{
	uPosX &= 0x1FF;

	/* X=256 is the hardware's special counted-but-hidden position. Other
	   negative positions count only while at least one pixel reaches x=0. */
	if (uPosX == 0x100)
		return TRUE;

	if (uPosX & 0x100)
		return ((Int32)uPosX - 512) > -(Int32)uWidth;

	return TRUE;
}

Bool _SnesPPUOBJTileCountedX(Uint16 uObjectX, Int32 iTileX)
{
	/* OBJ X=256 is a hardware quirk: its tiles consume the 34-tile budget
	   even though no pixel is visible. A tile ending exactly at x=-1 is the
	   first ordinary off-left tile that counts. */
	return ((uObjectX & 0x1FF) == 0x100) ||
	       (iTileX > -8 && iTileX < 256);
}


void _DecodeOBJEX(Uint8 *pObjEx, SnesRenderObjT *pObjs, Int32 nObjs, Uint32 uBaseSize)
{
	uBaseSize &= 7;
	while (nObjs > 0)
	{
		Uint8	uObjEx;
		Uint8  uLarge;

		// fetch obj byte
		uObjEx = *pObjEx++;

		//uObjEx|=0xAA;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		pObjs->uPosX	   = (uObjEx & 1) << 8;
		uObjEx>>=1;
		uLarge = uObjEx & 1;
		pObjs->uWidth  = _SnesPPU_OAMWidth [uBaseSize][uLarge];
		pObjs->uHeight = _SnesPPU_OAMHeight[uBaseSize][uLarge];
		uObjEx>>=1;
		pObjs++;

		nObjs-=4;
	}

}


void _DecodeOBJ(SnesPPUOBJT *pPPUObj, SnesRenderObjT *pObjs, Int32 nObjs, Uint8 *pObjY, Uint8 *pObjSize)
{
	// xxxxxxxx
	// yyyyyyyy
	// CCCCCCCC
	// vhppcccC


	while (nObjs > 0)
	{
		Uint32 uTile;
		Uint8 uAttrib;

		uAttrib = pPPUObj->uAttrib;

		uTile =  pPPUObj->uTile;
		uTile|= ((uAttrib&1)<<8);

		pObjs->uPosX   |= pPPUObj->uX;
		pObjs->uPosY    = pPPUObj->uY + 1;
		pObjs->uPal   = (uAttrib >> 1) & 7;
		pObjs->uPri   = (uAttrib >> 4) & 3;
		pObjs->bHFlip = (uAttrib >> 6) & 1;
		if (uAttrib & 0x80)
		{
			// Nos modos H=2*W, o PPU vira duas metades W x W em vez de
			// espelhar o retangulo inteiro. Isso equivale a XOR com W-1.
			pObjs->uVXOR = pObjs->uWidth - 1;
		} else
		{
			pObjs->uVXOR = 0;
		}

		pObjs->uTile = uTile;

        *pObjY++    = pObjs->uPosY;
        *pObjSize++ = pObjs->uHeight;

		// next obj
		pPPUObj++;
		pObjs++;
		nObjs--;
	}
}



Int32 SnesPPURender::CheckOBJ(SnesRenderObjT *pObjs, Int32 iObj, Int32 nObjs, Uint8 *pObjList, Int32 MaxObjLine, Int32 iLine)
{
	Int32 nObjLine = 0;

	while (nObjs > 0)
	{
		SnesRenderObjT *pObj;
		Uint32 uObjY;

		// get pointer to object
		pObj   = &pObjs[iObj & 0x7F];

		uObjY = iLine - pObj->uPosY;
		uObjY&= 0xFF;

		if (uObjY < pObj->uHeight &&
		    _SnesPPUOBJVisibleX(pObj->uPosX, pObj->uWidth))
		{
			// we got an obj
			*pObjList =  iObj;
			pObjList++;

			nObjLine++;
			if (nObjLine >= MaxObjLine) break;
		}

		
		iObj++;
		nObjs--;
	}

	return nObjLine;
}







Int32 SnesPPURender::CheckOBJ(Uint8 *pObjY, Uint8 *pObjSize, Int32 iObj, Int32 nObjs, Uint8 *pObjList, Int32 MaxObjLine, Int32 iLine)
{
	Int32 nObjLine = 0;

	while (nObjs > 0)
	{
		Uint32 uObjY, uObjSize;

		iObj &= 0x7F;

		// get pointer to object
        uObjSize = pObjSize[iObj];
		uObjY    = pObjY[iObj];

		uObjY = iLine - uObjY;
		uObjY&= 0xFF;

		if (uObjY < uObjSize) 
		{
			// we got an obj
			*pObjList =  iObj;
			pObjList++;

			nObjLine++;
			if (nObjLine >= MaxObjLine) break;
		}

		iObj++;
		nObjs--;
	}

	return nObjLine;
}



Int32 SnesPPURender::CheckOBJ(Uint8 *pObjList, Int32 iLine)
{
    if (iLine >= 0 && iLine < SNPPU_MAXLINE)
    {
        memcpy(pObjList, m_ObjLine[iLine], SNPPU_MAXOBJ);
        return m_nObjLine[iLine];
    } else
    {
        return 0;
    }
}



void SnesPPURender::UpdateOBJVisibility(Uint8 *pObjY, Uint8 *pObjSize, Int32 iObj, Int32 nObjs)
{
    memset(m_nObjLine, 0, sizeof(m_nObjLine));

	while (nObjs > 0)
	{
		Uint32 uObjY, uObjSize;

		iObj &= 0x7F;

		// get pointer to object
        uObjSize = pObjSize[iObj];
		uObjY    = pObjY[iObj];

		if (_SnesPPUOBJVisibleX(m_Objs[iObj].uPosX,
		                           m_Objs[iObj].uWidth))
		while (uObjSize > 0)
        {
            if (uObjY < SNPPU_MAXLINE)
            {
                if (m_nObjLine[uObjY] < SNPPU_MAXOBJ)
                {
                    m_ObjLine[uObjY][m_nObjLine[uObjY]] = (Uint8)iObj;
                    m_nObjLine[uObjY]++;
                }
            }

            uObjY++;
            uObjSize--;
    		uObjY&= 0xFF;
        }

		iObj++;
		nObjs--;
	}
}






void SnesPPURender::UpdateOBJ(Uint8 *pObjY, Uint8 *pObjSize)
{
	SnesOAMT *pOAM = m_pPPU->GetOAM();
	const SnesPPURegsT *pRegs  = m_pPPU->GetRegs();

	// decode objs
	_DecodeOBJEX(pOAM->ObjEx, m_Objs, SNESPPU_OBJ_NUM,
	             (pRegs->obsel >> 5) & 7);
	_DecodeOBJ(pOAM->Objs, m_Objs, SNESPPU_OBJ_NUM, pObjY, pObjSize);
}
