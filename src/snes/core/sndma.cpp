
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "console.h"
#include "prof.h"
extern "C" {
#include "sncpu.h"
};
#include "sndma.h"
#include "snppu.h"
#include "snsdd1.h"
#include "sndbglog.h"

#define SNESDMA_DEBUG 0

static Uint8	_SNDma_MDMATransfer[8][4]=
{
	// 000 1-address
	{0,0,0,0},
	// 001 2-address (l,h)
	{0,1,0,1},
	// 010 1-address
	{0,0,0,0},
	// 011 2-address (l,l,h,h)
	{0,0,1,1},
	// 100 4-address (l,h,l,h)
	{0,1,2,3},
	// 101 4-address (l,h,l,h)
	{0,1,0,1},
	// 110 4-address (l,h,l,h)
	{0,0,0,0},
	// 111 4-address (l,h,l,h)
	{0,0,1,1},
};


static Int8 _SNDma_MDMAInc[4] = 
{
	1, 0, -1, 0
};



Uint8 SnesDMAC::Read8(Uint32 uChan, Uint32 uAddr)
{
	SnesDMAChT *pChan;
	pChan = &m_Channels[uChan];

	switch(uAddr & 0xF)
	{
	case 0x0: 
		return pChan->dmapx;

	case 0x1: 
		return pChan->bbadx;

	case 0x2: 
		return pChan->a1tx & 0xFF;

	case 0x3: 
		return pChan->a1tx >> 8;

	case 0x4: 
		return pChan->a1bx;

	case 0x5: 
		return	pChan->dasx & 0xFF;

	case 0x6: 
		return	pChan->dasx >> 8;

	case 0x7: 
		return pChan->dasbx;

	case 0x8: 
		return pChan->a2ax & 0xFF;

	case 0x9: 
		return pChan->a2ax >> 8;

	case 0xA: 
		return pChan->ntlrx;

	case 0xB:
	case 0xF:
		return pChan->unknown;

	default:
		//ConDebug("read_dma8[%06X]\n", uAddr);
		return 0x00;
	}
}






void SnesDMAC::Write8(Uint32 uChan, Uint32 uAddr, Uint8 uData)
{
	SnesDMAChT *pChan;

	pChan = &m_Channels[uChan];
//	if (uChan==7)
//	ConDebug("dma%d[%02X]=%02X\n", uChan, uAddr & 0xF, uData);

	switch(uAddr & 0xF)
	{
	case 0x0: 
		pChan->dmapx = uData;
		break;

	case 0x1: 
		pChan->bbadx = uData;
		break;

	case 0x2: 
		pChan->a1tx &= 0xFF00;
		pChan->a1tx |= uData << 0;
		break;

	case 0x3: 
		pChan->a1tx &= 0x00FF;
		pChan->a1tx |= uData << 8;
		break;

	case 0x4: 
		pChan->a1bx = uData;
		break;

	case 0x5: 
		pChan->dasx &= 0xFF00;
		pChan->dasx |= uData << 0;
		break;

	case 0x6: 
		pChan->dasx &= 0x00FF;
		pChan->dasx |= uData << 8;
		break;

	case 0x7: 
		pChan->dasbx = uData;
		break;

	case 0x8: 
		pChan->a2ax &= 0xFF00;
		pChan->a2ax |= uData << 0;
		break;

	case 0x9: 
		pChan->a2ax &= 0x00FF;
		pChan->a2ax |= uData << 8;
		break;

	case 0xA: 
		pChan->ntlrx = uData;
		break;

	case 0xB:
	case 0xF:
		pChan->unknown = uData;
		break;

	default:
//		ConDebug("write_dma8[%06X]=%02X\n", uAddr, uData);
		break;
	}
}

void SnesDMAC::SetMDMAEnable(Uint8 uData)
{
#if SNDBG_LOG
	Uint32 uChan;

	for (uChan = 0; uChan < SNESDMAC_CHANNEL_NUM; uChan++)
	{
		SnesDMAChT *pChan;
		Uint32 uBytes;
		Uint32 uMode;
		Int32 iDelta;

		if (!(uData & (1 << uChan)))
			continue;

		pChan = &m_Channels[uChan];
		uBytes = pChan->dasx ? (Uint32)pChan->dasx : 0x10000u;
		uMode = pChan->dmapx & 7;
		iDelta = _SNDma_MDMAInc[(pChan->dmapx >> 3) & 3];

		g_DbgDMAStarts++;
		g_DbgDMAModes[uMode]++;
		if (uBytes > g_DbgDMAMaxBytes)
			g_DbgDMAMaxBytes = uBytes;

		if (g_DbgCaptureActive)
		{
			const SnesPPURegsT *pRegs = m_pPPU->GetRegs();
			DLog("[snes-dma-start] f=%u ch=%u dmap=%02X mode=%u dir=%s src=%02X:%04X len=%u bbad=%02X oam/vm/cg=%04X/%04X/%04X vmain=%02X",
				(unsigned)g_DbgCaptureFrameNo, (unsigned)uChan,
				(unsigned)pChan->dmapx, (unsigned)uMode,
				(pChan->dmapx & 0x80) ? "B>A" : "A>B",
				(unsigned)pChan->a1bx, (unsigned)pChan->a1tx,
				(unsigned)uBytes, (unsigned)pChan->bbadx,
				(unsigned)pRegs->oamaddr.w, (unsigned)pRegs->vmaddr.w,
				(unsigned)pRegs->cgadd.w, (unsigned)(Uint8)pRegs->vmain);
		}

		if ((iDelta > 0 && uBytes > 0x10000u - pChan->a1tx) ||
		    (iDelta < 0 && uBytes > (Uint32)pChan->a1tx + 1u))
			g_DbgDMAWraps++;

		if (pChan->dmapx & 0x80)
		{
			g_DbgDMAReadBytes += uBytes;
		}
		else
		{
			/* Count the exact B-bus ports selected by the four-byte mode
			   pattern, rather than assuming that BBAD alone names the port. */
			Uint32 uPhase;
			for (uPhase = 0; uPhase < 4 && uPhase < uBytes; uPhase++)
			{
				Uint32 uCount = 1u + (uBytes - 1u - uPhase) / 4u;
				Uint32 uPort = (pChan->bbadx +
					_SNDma_MDMATransfer[uMode][uPhase]) & 0xFF;
				if (uPort == 0x04)
					g_DbgDMAOAMBytes += uCount;
				else if (uPort == 0x18 || uPort == 0x19)
					g_DbgDMAVRAMBytes += uCount;
				else if (uPort == 0x22)
					g_DbgDMACGRAMBytes += uCount;
				else
					g_DbgDMAOtherBytes += uCount;
			}
		}
	}
#endif
	m_MDMAEnable = uData;
}

void SnesDMAC::SetHDMAEnable(Uint8 uData)
{
	// confirm:
	// ghouls and ghosts enabled hdma mid-frame
	m_HDMAEnable = uData;
}


void SnesDMAC::ProcessMDMAChRead(Uint32 uChan)
{
    SnesDMAChT *pChan;

    assert(uChan < SNESDMAC_CHANNEL_NUM);

    pChan = &m_Channels[uChan];

    Int32 uSrcDelta;
	Uint8 *pTransfer;
	Int32 iTransfer=0;

    // any cycles available?
    if (m_pCPU->Cycles <= 0) {
        return;
    }
	// determine a-bus increment
	uSrcDelta = _SNDma_MDMAInc[(pChan->dmapx>>3) & 3];

	// get transfer order
	pTransfer = _SNDma_MDMATransfer[pChan->dmapx & 7];
	iTransfer = 0;

	do
	{
		Uint8 uData;
		Uint32 uAddr;


		// get address to read from
		uAddr = 0x2100 + pChan->bbadx + pTransfer[iTransfer & 3];
		iTransfer++;

		// read byte
		uData = SNCPURead8(m_pCPU, uAddr);

		// write byte
		SNCPUWrite8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16), uData);

		// increment src address (does overflow go into next bank?)
		pChan->a1tx += uSrcDelta;

		// decrement byte count
		pChan->dasx--;

        // decrement cpu clock cycles
        SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 1);
	}
	while (pChan->dasx!=0 && (m_pCPU->Cycles > 0));

    // are we done?
    if (pChan->dasx == 0)
    {
        // clear channel enable bit
        m_MDMAEnable &= ~(1 << uChan);
    }
}

#if 0
Uint32 SnesDMAC::ProcessMDMACh(Uint32 uChan)
{
	SnesDMAChT *pChan;
	Int32 uSrcDelta;
	Uint8 *pTransfer;
	Int32 iTransfer=0;

	assert(uChan < SNESDMAC_CHANNEL_NUM);

	pChan = &m_Channels[uChan];
	
	#if SNESDMA_DEBUG
	ConDebug("dma%d: %02X %02X%04X -> %02X %04X (%04X)\n", uChan, 
		pChan->dmapx,
		pChan->a1bx, 
		pChan->a1tx, 
		pChan->bbadx, 
		pChan->dasx,
		m_pPPU->m_Regs.vmaddr.w
		);
	#endif


	if (pChan->dmapx & 0x80)
	{
		// ppu -> mem
		return ProcessMDMAChRead(uChan);
	}

	// determine a-bus increment
	uSrcDelta = _SNDma_MDMAInc[(pChan->dmapx>>3) & 3];

	// get transfer order
	pTransfer = _SNDma_MDMATransfer[pChan->dmapx & 7];
	iTransfer = 0;
	
	do
	{
		Uint8 uData;
		Uint32 uAddr;

		// read byte
		uData = SNCPURead8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16));

		// increment src address (does overflow go into next bank?)
		pChan->a1tx += uSrcDelta;

		// get address to write to (b-bus)
		uAddr = 0x2100 + pChan->bbadx + pTransfer[iTransfer & 3];
		iTransfer++;

		// write byte
		SNCPUWrite8(m_pCPU, uAddr, uData);

		// decrement cpu clock cycles
		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 1);

		// decrement byte count
		pChan->dasx--;
	}
	while (pChan->dasx!=0);

	return 1;
}
#endif

void SnesDMAC::TransferData(SnesDMAChT *pChan, Uint8 *pData, Int32 nBytes)
{
    SNCPUConsumeCycles(m_pCPU,  SNCPU_CYCLE_SLOW * nBytes);

    // special case simple transfer mode llll
	if ((pChan->dmapx & 7)==0)
	{
		switch (pChan->bbadx)
		{
		case 0x04: // oamdata (oam data)
			m_pPPU->WriteOAMBlock(pData, nBytes);
			break;
		case 0x18: // vmaddl (video port address low)
			while (nBytes > 0)
			{
				m_pPPU->WriteVMDATAL(*pData++);
				nBytes--;
			}
			break;
		case 0x19: // vmdatah (video port data hi)
			while (nBytes > 0)
			{
				m_pPPU->WriteVMDATAH(*pData++);
				nBytes--;
			}
			break;
		case 0x22: // cgdata (color data)
			while (nBytes > 0)
			{
				m_pPPU->WriteCGDATA(*pData++);
				nBytes--;
			}
			break;

		default:
			// generic write byte
			while (nBytes > 0)
			{
				SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, *pData++);
				nBytes--;
			}
		}

	} else
	if ((pChan->dmapx & 7)==1 && pChan->bbadx==0x18)
	{
		m_pPPU->WriteVMDATABlock(pData, nBytes);
	} else
	{
		Uint8 *pTransfer;
		Int32 iTransfer=0;

		// get transfer order
		pTransfer = _SNDma_MDMATransfer[pChan->dmapx & 7];

		while (nBytes > 0)
		{
			Uint8 uData;
			Uint32 uAddr;

			// fetch data byte
			uData = pData[iTransfer];

			// get address to write to (b-bus)
			uAddr = pChan->bbadx + pTransfer[iTransfer & 3];
			iTransfer++;

			switch (uAddr)
			{
			case 0x04: // oamdata (oam data)
				m_pPPU->WriteOAMDATA(uData);
				break;
			case 0x18: // vmaddl (video port address low)
				m_pPPU->WriteVMDATAL(uData);
				break;
			case 0x19: // vmdatah (video port data hi)
				m_pPPU->WriteVMDATAH(uData);
				break;
			case 0x22: // cgdata (color data)
				m_pPPU->WriteCGDATA(uData);
				break;

			default:
				// write byte
				SNCPUWrite8(m_pCPU, 0x2100 + uAddr, uData);
			}
			nBytes--;
		}
	}
}


void SnesDMAC::ProcessMDMAChFast(Uint32 uChan)
{
	SnesDMAChT *pChan;

	assert(uChan < SNESDMAC_CHANNEL_NUM);

    pChan = &m_Channels[uChan];

#if SNESDMA_DEBUG
	ConDebug("dma%d: %02X %02X%04X -> %02X %04X vram=%04X nCycles=%d\n", uChan, 
		pChan->dmapx,
		pChan->a1bx, 
		pChan->a1tx, 
		pChan->bbadx, 
		pChan->dasx,
		m_pPPU->m_Regs.vmaddr.w,
        m_pCPU->Cycles
		);
#endif

    // any cycles available?
    if (m_pCPU->Cycles <= 0) {
        return;
    }
	if (pChan->dmapx & 0x80)
	{
		// ppu -> mem
		return ProcessMDMAChRead(uChan);
	}

	// S-DD1: descomprime quando o DMA tem endereco-A fixo (dmapx bit 0x08) e
	// $4801 != 0 (mesma condicao do snes9x). Antes eu so' checava o bit do
	// canal em $4801, o que podia disparar num DMA normal por engano.
	if (m_pSDD1 && (pChan->dmapx & 0x08) && m_pSDD1->DmaActive())
	{
		static Uint8 s_DecodeBuf[0x10000];
		Int32  count   = pChan->dasx ? pChan->dasx : 0x10000;
		Uint32 srcAddr = ((Uint32)pChan->a1bx << 16) | pChan->a1tx;
		Uint8 *pIn     = m_pCPU->Bank[srcAddr >> SNCPU_BANK_SHIFT].pMem;

		if (pIn)
		{
			// pMem ja' inclui (-base do banco), entao soma-se o endereco
			// completo (mesma convencao de SNCPUPeek8: pMem[Addr]).
			pIn += srcAddr;
			m_pSDD1->Decompress(s_DecodeBuf, pIn, (Int32)pChan->dasx);
			TransferData(pChan, s_DecodeBuf, count);
#if SNDBG_LOG
			{
				static int n = 0;
				if (n < 120) {
					DLog("[sdd1] dma ch=%d src=%06X cnt=%d bbad=%02X mode=%d hdr=%02X out=%02X%02X%02X%02X",
						(int)uChan, (unsigned)srcAddr, (int)count,
						(int)pChan->bbadx, (int)(pChan->dmapx & 7),
						(int)pIn[0], (int)s_DecodeBuf[0], (int)s_DecodeBuf[1],
						(int)s_DecodeBuf[2], (int)s_DecodeBuf[3]);
					n++;
				}
			}
#endif
		}
#if SNDBG_LOG
		else
		{
			static int nn = 0;
			if (nn < 40) {
				DLog("[sdd1] dma ch=%d src=%06X SEM PONTEIRO (banco nao mapeado!)",
					(int)uChan, (unsigned)srcAddr);
				nn++;
			}
		}
#endif

		m_pSDD1->ClearDmaEnable();
		pChan->dasx = 0;
		m_MDMAEnable &= ~(1 << uChan);
		return;
	}

    do
	{
		Uint8 DmaBuffer[256];
		Int32 nBytes;

		// calculate number of bytes remaining to transfer
		nBytes = pChan->dasx ? pChan->dasx : 0x10000;

        // clamp number of bytes to the size of our temporary buffer
		if (nBytes > (Int32)sizeof(DmaBuffer)) 
            nBytes = sizeof(DmaBuffer);

        // clamp number of bytes to cycle time remaining
        Int32 nMaxBytes = ((m_pCPU->Cycles+7) >> 3);
        // we must transfer a multiple of 4-bytes at a time though....
        nMaxBytes = (nMaxBytes + 3) & ~3;
        if (nBytes > nMaxBytes)
            nBytes = nMaxBytes;

        // any bytes to transfer?
        if (nBytes > 0)
        {
		    PROF_ENTER("DMAREADMEM");
		    switch ((pChan->dmapx>>3) & 3)
		    {
		    case 0: //+1 increment
                {
					Int32 nFirst = nBytes;
					Int32 nToBankEnd = 0x10000 - (Int32)pChan->a1tx;

					if (nFirst > nToBankEnd)
						nFirst = nToBankEnd;

			        // A1T wraps inside A1B. Keep both pieces in one buffer so
			        // TransferData also keeps its B-bus mode phase continuous.
			        SNCPUReadMem(m_pCPU,
			                     pChan->a1tx | (pChan->a1bx << 16),
			                     DmaBuffer, nFirst);
					if (nFirst < nBytes)
					{
						SNCPUReadMem(m_pCPU, pChan->a1bx << 16,
						             DmaBuffer + nFirst, nBytes - nFirst);
					}

			        // increment src address 
			        pChan->a1tx = (Uint16)(pChan->a1tx + nBytes);
                }
			    break;
		    case 2: //-1 decrement
			    {
                    // read data into dma buffer (decrement)
				    Int32 iByte;
				    for (iByte=0; iByte < nBytes; iByte++)
				    {
					    DmaBuffer[iByte] = SNCPURead8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16));
					    pChan->a1tx--;
				    }
			    }
			    break;
		    case 1:
		    case 3: // 0
			    // read data into dma buffer (no increment)
			    memset(DmaBuffer, SNCPURead8(m_pCPU, pChan->a1tx | (pChan->a1bx << 16)), nBytes);
			    break;
		    }
		    PROF_LEAVE("DMAREADMEM");

            // transfer cached data to B-bus
		    TransferData(pChan, DmaBuffer, nBytes);

            // decrement byte count
            pChan->dasx -= nBytes;
        }

	}	while ( (pChan->dasx!=0) && (m_pCPU->Cycles > 0) );

    // are we done?
    if (pChan->dasx == 0)
    {
        // clear channel enable bit
        m_MDMAEnable &= ~(1 << uChan);
    }
}








void SnesDMAC::BeginHDMA()
{
	Uint8 uEnabled = m_HDMAEnable;

	for (Uint32 uChan=0; uChan < SNESDMAC_CHANNEL_NUM; uChan++)
	{
		// did a channel go from off to on?
		if (uEnabled & (1<<uChan))
		{
			SnesDMAChT *pChan = &m_Channels[uChan];

			// set table address
			pChan->a2ax = pChan->a1tx;
			pChan->ntlrx = 0;
		}
	}
}

Uint32 SnesDMAC::ProcessHDMACh(Uint32 uChan)
{
	SnesDMAChT *pChan;
	Uint8 uData;

	assert(uChan < SNESDMAC_CHANNEL_NUM);
	pChan = &m_Channels[uChan];

	// are we done with current run?
	if ((pChan->ntlrx&0x7F) == 0)
	{
		// fetch next count byte
		pChan->ntlrx = SNCPURead8(m_pCPU, pChan->a2ax | (pChan->a1bx << 16));
		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 1);


		if (pChan->ntlrx == 0)
		{
			// done with dma
			return 1; 
		}

		// decrement line count (0x80 -> 127)
		pChan->ntlrx--;

		// increment table address
		pChan->a2ax++;

		// indirect addressing?
		if (pChan->dmapx & 0x40)
		{
			// fetch 16-bit address
			pChan->dasx = SNCPURead16(m_pCPU, pChan->a2ax | (pChan->a1bx << 16));
			SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 2);

			// increment table address
			pChan->a2ax+=2;

		} else
		{
			// data address is same as table address
			pChan->dasx = pChan->a2ax;
			pChan->dasbx= pChan->a1bx;
		}

	} else
	{
		pChan->ntlrx--;
		if (pChan->ntlrx < 0x80)
		{
			// skip line
			return 0;
		}
	}



	switch (pChan->dmapx & 7)
	{
	case 0:	// 1 address				(1)
		uData = SNCPURead8(m_pCPU, (pChan->dasx+0) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, uData);
		pChan->dasx+=1;

		// confirm: dead on
		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 1  + SNCPU_CYCLE_FAST * 3 - 1 - 8 -4 -1);
		break;

	case 1: // 2 address				(l,h) (2)
	case 5:
		uData = SNCPURead8(m_pCPU, (pChan->dasx+0) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, uData);

		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW + SNCPU_CYCLE_FAST);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+1) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2101 + pChan->bbadx, uData);
		pChan->dasx+=2;

		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW + SNCPU_CYCLE_FAST);

		// confirm: dead on
		//SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 2 + SNCPU_CYCLE_FAST * 3 - 2);
		break;

	case 2: // write twice				(l,l) (1)
	case 6:
		uData = SNCPURead8(m_pCPU, (pChan->dasx+0) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, uData);

		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW + SNCPU_CYCLE_FAST);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+1) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, uData);

		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW + SNCPU_CYCLE_FAST);

		pChan->dasx+=2;

		// confirm: dead on
		//SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 2 + SNCPU_CYCLE_FAST * 3 - 2);
		break;

	case 3: // 2 address write twice	(llhh)(2)
	case 7:
		uData = SNCPURead8(m_pCPU, (pChan->dasx+0) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+1) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+2) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2101 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+3) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2101 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);
		pChan->dasx+=4;

		// confirm: dead on
		//SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 4  + SNCPU_CYCLE_FAST * 3 - 1 );
		break;

	case 4: // 4 address				(lhlh)(4)
		uData = SNCPURead8(m_pCPU, (pChan->dasx+0) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2100 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+1) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2101 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+2) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2102 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);

		uData = SNCPURead8(m_pCPU, (pChan->dasx+3) | (pChan->dasbx << 16));
		SNCPUWrite8(m_pCPU, 0x2103 + pChan->bbadx, uData);
		SNCPUConsumeCycles(m_pCPU, 12);
		pChan->dasx+=4;

		// confirm: dead on
//		SNCPUConsumeCycles(m_pCPU, SNCPU_CYCLE_SLOW * 4  + SNCPU_CYCLE_FAST * 3 - 1);
		break;
	}


	if (!(pChan->dmapx & 0x40))
	{
		// increment table address as well
		pChan->a2ax=pChan->dasx;
	} 

	// continue dma
	return 0;
}






void SnesDMAC::ProcessMDMA()
{
    Uint32 uChan = 0;

    while (m_MDMAEnable && (m_pCPU->Cycles > 0))
    {
        if (m_MDMAEnable & (1 << uChan))
        {
            // process channel
            ProcessMDMAChFast(uChan);
        } 
        else
        {
            // next channel
            uChan++;
        }
    }
}

void SnesDMAC::ProcessHDMA()
{
	Uint8 uEnable;
	Uint32 uChan;

	uEnable = m_HDMAEnable;
	uChan = 0;

	while (uEnable)
	{
		if (uEnable&1)
		{
			// process channel
			ProcessHDMACh(uChan);
		}
		uEnable>>=1;
		uChan++;
	}
}

void SnesDMAC::Reset()
{
	memset(m_Channels, 0xFF, sizeof(m_Channels));
	m_MDMAEnable = 0;
	m_HDMAEnable = 0;
}
