
#ifndef _SNTIMING_H
#define _SNTIMING_H

#define SNES_CYCLESPERLINE (1364)

/* AURORA_V7_HORIZONTAL_TIMING
 * Low-cost scanline scheduler positions, in S-CPU master clocks.
 * ares models HBlank at H=1096 and visible-line HDMA at H=1104.  For
 * revision-2 S-CPU timing, HDMA setup and DRAM refresh are phase-aligned
 * to the DMA divider: H=(12+phase) and H=(538-phase), respectively. */
#define SNES_HBLANK_WRAP_CYCLES      (3)
#define SNES_HDMA_SETUP_BASE_CYCLE   (12)
#define SNES_DRAM_REFRESH_BASE_CYCLE (538)
#define SNES_DRAM_REFRESH_CYCLES     (40)
#define SNES_HBLANK_START_CYCLE  (1096)
#define SNES_HDMA_START_CYCLE    (1104)

#define SNES_LINECYCLEDELAY SNES_DRAM_REFRESH_CYCLES
#define SNES_HBLANKCYCLES  (SNES_CYCLESPERLINE - SNES_HBLANK_START_CYCLE)

/* The S-CPU's H/V timer compare is not visible at H=HTIME*4 immediately.
   The counter reset/compare circuit and IRQ pipeline add 14 master clocks
   (Snes9x/bsnes timing); H=0 has the documented one-dot special case. */
#define SNES_IRQ_TRIGGER_CYCLES (14)
#define SNES_HIRQ_CYCLES(_htime) \
	((Int32)(_htime) * 4 + SNES_IRQ_TRIGGER_CYCLES - ((_htime) ? 0 : 4))
#define SNES_VIRQ_CYCLES (SNES_IRQ_TRIGGER_CYCLES - 4)
#define SNES_SPCMINCYCLES 0
#define SNES_CYCLESPERFRAME (SNES_CYCLESPERLINE * 262)


#endif
