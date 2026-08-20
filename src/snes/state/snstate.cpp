
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "console.h"
#include "snes.h"
#include "snstate.h"

/* AURORA_SPEEDY_MDR_STATE_LAYOUT_V1
   SNStateCPUT was 44 bytes before MDR support. New fields only consume
   old padding, so existing state payload size must remain identical. */
typedef char SNStateCPUSizeMustRemain44[(sizeof(SNStateCPUT) == 44) ? 1 : -1];

/* AURORA_CX4_STATE_V7
 *
 * SnesStateT already carries a fixed 256 KiB SRAM image for compatibility.
 * Put a tagged CX4 snapshot at the END of its unused SRAM tail rather than
 * growing SnesStateT and invalidating every pre-existing normal SNES state.
 */
struct SNCX4StateEnvelopeT
{
    Uint8 Tag[8];
    Uint32 Version;
    SNCX4StateT CX4;
};

static const Uint8 _SNCX4StateTag[8] =
    { 'C', 'X', '4', 'S', 'T', '7', 0, 0 };

static SNCX4StateEnvelopeT *_SNCX4StateEnvelope(SnesStateT *pState)
{
    return (SNCX4StateEnvelopeT *)
        (pState->SRam + sizeof(pState->SRam) - sizeof(SNCX4StateEnvelopeT));
}

static const SNCX4StateEnvelopeT *_SNCX4StateEnvelope(
    const SnesStateT *pState)
{
    return (const SNCX4StateEnvelopeT *)
        (pState->SRam + sizeof(pState->SRam) - sizeof(SNCX4StateEnvelopeT));
}

Bool SnesSystem::CanSerializeCX4State()
{
    Uint32 nSramBytes;

    if (!m_pRom || !(m_pRom->m_Flags & SNROM_FLAG_CX4))
        return FALSE;

    nSramBytes = m_pRom->GetSRAMBytes();

    return nSramBytes <=
        (Uint32)(SNES_SRAMSIZE - sizeof(SNCX4StateEnvelopeT))
        ? TRUE : FALSE;
}

void SnesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    if (nStateBytes == sizeof(SnesStateT))
    {
        SaveState((SnesStateT *)pState);
    }
}

void SnesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    if (nStateBytes == sizeof(SnesStateT))
    {
        RestoreState((SnesStateT *)pState);
    }
}

Int32 SnesSystem::GetStateSize()
{
    return sizeof(SnesStateT);
}



void SnesSystem::SaveState(SnesStateT *pState)
{
	/* The state is persisted as an opaque payload by the PS2 front-end.
	   Clear padding and currently-unused fields first so two equivalent
	   states have deterministic bytes (and therefore a deterministic
	   CRC), instead of leaking whatever happened to be in the buffer. */
	memset(pState, 0, sizeof(*pState));

	// set tag
	pState->Tag[0] = 'S';
	pState->Tag[1] = 'N';
	pState->Tag[2] = 'S';
	pState->Tag[3] = '\0';

	pState->uFrame = m_uFrame;
	pState->uLine  = m_uLine;

	// copy cpu state
	pState->CPU.Regs = m_Cpu.Regs;
	pState->CPU.Cycles = m_Cpu.Cycles;
	pState->CPU.Counter[0] = m_Cpu.Counter[0];
	pState->CPU.Counter[1] = m_Cpu.Counter[1];
	pState->CPU.Counter[2] = m_Cpu.Counter[2];
	pState->CPU.Counter[3] = m_Cpu.Counter[3];
	pState->CPU.uSignal    = m_Cpu.uSignal;
	/* AURORA_SPEEDY_MDR_STATE_RW_V1 */
	pState->CPU.uMDR       = m_Cpu.uMDR;
	pState->CPU.uMDRTag[0] = 'M';
	pState->CPU.uMDRTag[1] = 'D';

	pState->SPC.Regs = m_Spc.Regs;
	pState->SPC.Cycles = m_Spc.Cycles;
	pState->SPC.Counter[0] = m_Spc.Counter[0];
	pState->SPC.Counter[1] = m_Spc.Counter[1];
	pState->SPC.uCycleShift = 0;

	m_PPU.SaveState(&pState->PPU);
	m_DMAC.SaveState(&pState->DMAC);
	m_IO.SaveState(&pState->IO);
	m_SpcDsp.SaveState(&pState->SPCDSP);
	m_SpcDspMixer.SaveState(&pState->SPCDSP);
	m_SpcIO.SaveState(&pState->SPCIO);

	// save memory state
	memcpy(pState->Ram, m_Ram, sizeof(pState->Ram));
	memcpy(pState->SRam, m_SRam, sizeof(pState->SRam));

    /* AURORA_CX4_STATE_V7 */
    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_CX4) &&
        CanSerializeCX4State())
    {
        SNCX4StateEnvelopeT *pCX4 = _SNCX4StateEnvelope(pState);
        memset(pCX4, 0, sizeof(*pCX4));
        memcpy(pCX4->Tag, _SNCX4StateTag, sizeof(pCX4->Tag));
        pCX4->Version = 1;
        m_CX4.SaveState(&pCX4->CX4);
    }

    // copy spc ram
    pState->SPC.bRomEnable = m_Spc.bRomEnable;

    SNSPCSetRomEnable(&m_Spc, FALSE);
	memcpy(pState->SpcRam, m_Spc.Mem, SNSPC_MEM_SIZE);
    SNSPCSetRomEnable(&m_Spc,  pState->SPC.bRomEnable);
}

Bool SnesSystem::RestoreState(SnesStateT *pState)
{
	if (memcmp(pState->Tag, "SNS", 4))
	{
		return FALSE;
	}

    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_CX4))
    {
        const SNCX4StateEnvelopeT *pCX4 = _SNCX4StateEnvelope(pState);
        if (!CanSerializeCX4State() ||
            memcmp(pCX4->Tag, _SNCX4StateTag, sizeof(pCX4->Tag)) != 0 ||
            pCX4->Version != 1)
        {
            return FALSE;
        }
    }

	m_uFrame = pState->uFrame;
	m_uLine  = pState->uLine;

	// restore state
	m_Cpu.Regs = pState->CPU.Regs;
	m_Cpu.Cycles = pState->CPU.Cycles;
	m_Cpu.Counter[0] = pState->CPU.Counter[0];
	m_Cpu.Counter[1] = pState->CPU.Counter[1];
	m_Cpu.Counter[2] = pState->CPU.Counter[2];
	m_Cpu.Counter[3] = pState->CPU.Counter[3];
	m_Cpu.nAbortCycles = 0;
	m_Cpu.uSignal    = pState->CPU.uSignal;
	/* AURORA_SPEEDY_MDR_STATE_RW_V1
	   Old states used these bytes as padding; trust MDR only with tag MD. */
	if (pState->CPU.uMDRTag[0] == 'M' && pState->CPU.uMDRTag[1] == 'D')
		m_Cpu.uMDR = pState->CPU.uMDR;
	else
		m_Cpu.uMDR = (Uint8)(m_Cpu.Regs.rPC >> 8);
	m_Cpu.uNmiDmaDelay = 0;

	m_Spc.Regs = pState->SPC.Regs;
	m_Spc.Cycles = pState->SPC.Cycles;
	m_Spc.Counter[0] = pState->SPC.Counter[0];
	m_Spc.Counter[1] = pState->SPC.Counter[1];
	m_Spc.uPad = 0;


	m_PPU.RestoreState(&pState->PPU);
	m_DMAC.RestoreState(&pState->DMAC);
	m_IO.RestoreState(&pState->IO);
	/* The legacy state payload does not contain the DSP write queue,
	   echo history or noise generator.  Reset those transient pieces
	   before applying the serialized registers/channels so a load does
	   not inherit audio work from the future state it is replacing. */
	m_SpcDsp.Reset();
	m_SpcDspMixer.Reset();
	m_SpcDspSilentMixer.Reset();
	m_SpcDsp.RestoreState(&pState->SPCDSP);
	m_SpcDspMixer.RestoreState(&pState->SPCDSP);
	m_SpcIO.RestoreState(&pState->SPCIO);

	// restore memory state
	memcpy(m_Ram, pState->Ram, sizeof(m_Ram));
	memcpy(m_SRam, pState->SRam, sizeof(m_SRam));

    /* AURORA_CX4_STATE_V7 */
    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_CX4))
    {
        const SNCX4StateEnvelopeT *pCX4 = _SNCX4StateEnvelope(pState);
        if (!m_CX4.RestoreState(&pCX4->CX4))
            return FALSE;
    }

    // copy spc ram
    SNSPCSetRomEnable(&m_Spc, FALSE);
	memcpy(m_Spc.Mem, pState->SpcRam, SNSPC_MEM_SIZE);
    SNSPCSetRomEnable(&m_Spc, pState->SPC.bRomEnable);


	// set fast or slow rom
	if (m_IO.m_Regs.memsel & 1)
	{
		SetFastRom();
	} else
	{
		SetSlowRom();
	}

	return TRUE;
}


void SnesIO::SaveState(struct SNStateIOT *pState)
{
	pState->Input = m_Input;
	pState->Regs = m_Regs;
}
void SnesIO::RestoreState(struct SNStateIOT *pState)
{
	m_Input = pState->Input;
	m_Regs = pState->Regs;
}

void SNSpcIO::SaveState(struct SNStateSPCIOT *pState)
{
	pState->Regs = m_Regs;
}
void SNSpcIO::RestoreState(struct SNStateSPCIOT *pState)
{
	m_Regs = pState->Regs;
}

void SnesDMAC::SaveState(struct SNStateDMACT *pState)
{
	pState->m_HDMAEnable = m_HDMAEnable;
	pState->m_HDMAEnded = m_HDMAEnded;
	pState->m_HDMADoTransfer = m_HDMADoTransfer;
	pState->m_MDMAEnable = m_MDMAEnable;
	memcpy(pState->m_Channels, m_Channels, sizeof(pState->m_Channels));
}

void SnesDMAC::RestoreState(struct SNStateDMACT *pState)
{
	m_HDMAEnable = pState->m_HDMAEnable;
	m_HDMAEnded = pState->m_HDMAEnded;
	m_HDMADoTransfer = pState->m_HDMADoTransfer;
	m_MDMAEnable = pState->m_MDMAEnable;
	memcpy(m_Channels, pState->m_Channels, sizeof(m_Channels));
	/* AURORA_V82_MDMA_PHASE_STATE_RESTORE
	 * Phase/startup are transient scheduler state and are intentionally not
	 * added to the legacy opaque save-state payload. */
	memset(m_MDMAPhase, 0, sizeof(m_MDMAPhase));
	m_MDMAChannelStartup = 0;
	m_MDMAStartupPending = 0;
}

void SnesPPU::SaveState(struct SNStatePPUT *pState)
{
	pState->Regs  = m_Regs;
	memcpy(pState->m_CGRAM,   m_CGRAM, sizeof(pState->m_CGRAM));
	memcpy(pState->m_VRAM,    m_VRAM, sizeof(pState->m_VRAM));
	pState->m_OAM = m_OAM;
}

void SnesPPU::RestoreState(struct SNStatePPUT *pState)
{
	m_Regs = pState->Regs;
	memcpy(m_CGRAM,   pState->m_CGRAM, sizeof(m_CGRAM));
	memcpy(m_VRAM,    pState->m_VRAM,  sizeof(m_VRAM));
	m_OAM = pState->m_OAM;
	m_OAMLatch = 0;
	m_pRender->UpdateVRAMRange(0, SNESPPU_VRAM_NUMWORDS);
	UpdateOAMPriority();
}


void SNSpcDsp::SaveState(struct SNStateSPCDSPT *pState)
{
	memcpy(pState->m_Regs, m_Regs, sizeof(m_Regs));
}

void SNSpcDsp::RestoreState(struct SNStateSPCDSPT *pState)
{
	memcpy(m_Regs, pState->m_Regs, sizeof(m_Regs));
}

void SNSpcDspMix::SaveState(struct SNStateSPCDSPT *pState)
{
	memcpy(pState->m_Channels, m_Channels, sizeof(m_Channels));
}

void SNSpcDspMix::RestoreState(struct SNStateSPCDSPT *pState)
{
	memcpy(m_Channels, pState->m_Channels, sizeof(m_Channels));
}


void _SNStateMemDiff(const char *pTag, Uint8 *pA, Uint8 *pB, Int32 nBytes)
{
    Int32 iOffset;

    for (iOffset=0; iOffset < nBytes; iOffset++)
    {
        if (pA[iOffset]!=pB[iOffset])
        {
            ConDebug("%s: %04X %02X %02X\n", pTag, iOffset, pA[iOffset], pB[iOffset]);
        }
    }

}

void SNStateCompare(SnesStateT *pStateA, SnesStateT *pStateB)
{
    _SNStateMemDiff("Mem", pStateA->Ram, pStateB->Ram, SNES_RAMSIZE);
    _SNStateMemDiff("SpcMem", pStateA->SpcRam, pStateB->SpcRam, SNSPC_RAM_SIZE);
    _SNStateMemDiff("SRM", pStateA->SRam, pStateB->SRam, SNES_SRAMSIZE);
    _SNStateMemDiff("Cpu", (Uint8 *)&pStateA->CPU, (Uint8 *)&pStateB->CPU, sizeof(pStateA->CPU));
    _SNStateMemDiff("SPC", (Uint8 *)&pStateA->SPC, (Uint8 *)&pStateB->SPC, sizeof(pStateA->SPC));
	_SNStateMemDiff("SPCIO", (Uint8 *)&pStateA->SPCIO, (Uint8 *)&pStateB->SPCIO, sizeof(pStateA->SPCIO));
    _SNStateMemDiff("DSP", (Uint8 *)&pStateA->SPCDSP, (Uint8 *)&pStateB->SPCDSP, sizeof(pStateA->SPCDSP));
    _SNStateMemDiff("IO", (Uint8 *)&pStateA->IO, (Uint8 *)&pStateB->IO, sizeof(pStateA->IO));
    _SNStateMemDiff("PPU", (Uint8 *)&pStateA->PPU, (Uint8 *)&pStateB->PPU, sizeof(pStateA->PPU));
    _SNStateMemDiff("DMAC", (Uint8 *)&pStateA->DMAC, (Uint8 *)&pStateB->DMAC, sizeof(pStateA->DMAC));

}
