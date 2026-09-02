#ifndef _SNSA1_H
#define _SNSA1_H

#include "types.h"
extern "C" {
#include "sncpu.h"
}

class SnesSystem;

/* AURORA_SA1_V1_SNES9X_LOGIC_20260902
 *
 * SA-1 is modelled as an Aurora peripheral around a second native SNCpuT.
 * The register/DMA/MMC/arithmetic/bitstream behaviour follows the Snes9x
 * implementation, translated to Aurora's memory/trap and scheduling model.
 */
class SNSA1
{
public:
    SNSA1();
    ~SNSA1();

    Bool Attach(SnesSystem *pOwner,
                const Uint8 *pRom, Uint32 nRomBytes,
                Uint8 *pBWRAM, Uint32 nBWRAMBytes,
                Bool bMapMainRom, Bool bDonorBWRAM);
    void Detach();
    void Reset();
    void Run(Int32 nMainMasterCycles);

    Bool IsActive() const { return m_bActive; }
    SNCpuT *GetCPU() { return &m_Cpu; }

    /* S-CPU side of the SA-1 cartridge. */
    Uint8 ReadMainRegister(Uint16 uAddr, Uint8 uOpenBus);
    void  WriteMainRegister(Uint16 uAddr, Uint8 uData);
    Uint8 ReadMainIRAM(Uint16 uAddr, Uint8 uOpenBus) const;
    void  WriteMainIRAM(Uint16 uAddr, Uint8 uData);
    Uint8 ReadMainBWRAM(Uint32 uAddr, Uint8 uOpenBus);
    void  WriteMainBWRAM(Uint32 uAddr, Uint8 uData);
    Uint8 ReadMainROM(Uint32 uAddr, Uint8 uOpenBus) const;

    void MapMainCPU(SNCpuT *pMainCpu);

    /* Current Snes9x S-CPU vector override semantics. */
    Bool EnterMainIRQOverride(SNCpuT *pMainCpu);
    Bool EnterMainNMIOverride(SNCpuT *pMainCpu);

private:
    SnesSystem *m_pOwner;
    SNCpuT m_Cpu;
    Bool m_bActive;
    Bool m_bMapMainRom;
    Bool m_bDonorBWRAM;

    const Uint8 *m_pRom;
    Uint32 m_nRomBytes;
    Uint8 *m_pBWRAM;
    Uint32 m_nBWRAMBytes;

    Uint8 m_Reg[0x200];       /* $2200-$23FF backing/status */
    Uint8 m_IRAM[0x800];      /* real 2 KiB SA-1 I-RAM */
    Uint8 m_CharData[0x80];   /* CC2 staging, not hidden in ROM storage */

    Uint16 m_uOp1;
    Uint16 m_uOp2;
    Uint8 m_uArithmeticOp;
    unsigned long long m_uSum;
    Bool m_bArithmeticOverflow;

    Uint8 m_uVariableBitPos;
    Uint8 m_uCharIndex;
    Bool m_bCharDMA;
    Uint8 m_uBitmapFormat;

    Uint32 m_uHCounter;
    Uint32 m_uVCounter;
    Uint32 m_uPrevHCounter;
    Uint16 m_uLatchedHCounter; /* AURORA_SA1_ACCURACY_REVIEW_V2_20260902 */
    Uint16 m_uLatchedVCounter;
    Bool m_bTimerLastState;

    static Uint8 SNCPU_TRAPFUNC ReadCPU(SNCpuT *pCpu, Uint32 uAddr);
    static void  SNCPU_TRAPFUNC WriteCPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData);

    Uint8 ReadBus(Uint32 uAddr, Uint8 uOpenBus);
    void  WriteBus(Uint32 uAddr, Uint8 uData);

    Uint8 ReadRegister(Uint16 uAddr, Uint8 uOpenBus);
    void  WriteRegister(Uint16 uAddr, Uint8 uData);
    static Bool MainCanWriteRegister(Uint16 uAddr);
    static Bool SA1CanWriteRegister(Uint16 uAddr);

    Uint32 MirrorRomOffset(Uint32 uPos) const;
    Uint32 RomOffset(Uint8 uBank, Uint16 uAddr) const;
    void MapRomWindows(SNCpuT *pCpu, Bool bMainCpu);
    void MapRomPage(SNCpuT *pCpu, Uint32 uBus, Bool bMainCpu);
    void MapSA1CPU();

    Uint32 MainBWRAMOffset(Uint32 uAddr, Bool *pOK) const;
    Uint32 SA1BWRAMOffset(Uint32 uAddr, Bool *pOK, Bool *pBitmap) const;
    Uint8 ReadBWRAMLinear(Uint32 uOffset, Uint8 uOpenBus) const;
    void WriteBWRAMLinear(Uint32 uOffset, Uint8 uData);
    Uint8 ReadBitmap(Uint32 uPixelAddr, Uint8 uOpenBus) const;
    void WriteBitmap(Uint32 uPixelAddr, Uint8 uData);
    Bool BWRAMWriteProtected(Uint32 uOffset) const;
    Bool IRAMWriteAllowed(Bool bSA1, Uint32 uOffset) const;

    void DoDMA();
    Uint8 ReadCC1(Uint32 uBWRAMOffset);
    void DoCC2();
    void ReadVariableLength(Bool bInc, Bool bNoShift);
    void DoArithmetic();

    void UpdateMainIRQ();
    void UpdateTimer(Uint32 nSA1Cycles);
    void ServiceInterrupts();
    void ResetCPUToVector(Uint16 uVector);
    static Bool EnterInterrupt(SNCpuT *pCpu, Uint16 uVector, Bool bNMI);

    void MarkBWRAMDirty();
};

#endif
