#ifndef _AUDFRAMESCHEDULE_H
#define _AUDFRAMESCHEDULE_H

#include "types.h"

/*
 * Distribui uma taxa de amostragem por quadros em blocos alinhados.
 *
 * Para 32000 Hz / 60 quadros / quantum 4, a sequencia e' 532, 532, 536.
 * Ao fim de 60 quadros a soma e' exatamente 32000. A fase representa apenas
 * arredondamento fracionario; ela nunca acumula atraso do ring de audio.
 */
/* AURORA_MEGA_V2_AUDIO_RATIONAL
 * Distribute samples against a rational video refresh. For NTSC PS2 output
 * the caller uses 60000/1001 instead of pretending every VBlank is 1/60 s.
 * The phase stores the exact remainder; no audio debt is repaid by doing a
 * double-size mix on a later frame.
 */
_INLINE Int32 AudFrameScheduleNextRational(Uint32 *pPhase, Uint32 uSampleRate,
	Uint32 uFrameRateNum, Uint32 uFrameRateDen, Uint32 uQuantum)
{
	Uint64 uDenominator;
	Uint64 uAccum;
	Uint32 uSamples;

	if (!pPhase || !uSampleRate || !uFrameRateNum || !uFrameRateDen || !uQuantum)
		return 0;

	uDenominator = (Uint64)uFrameRateNum * uQuantum;
	uAccum = (Uint64)(*pPhase) + (Uint64)uSampleRate * uFrameRateDen;
	uSamples = (Uint32)(uAccum / uDenominator) * uQuantum;
	uAccum -= (Uint64)uSamples * uFrameRateNum;
	*pPhase = (Uint32)uAccum;
	return (Int32)uSamples;
}

_INLINE Int32 AudFrameScheduleNext(Uint32 *pPhase, Uint32 uSampleRate,
	Uint32 uFrameRate, Uint32 uQuantum)
{
	return AudFrameScheduleNextRational(pPhase, uSampleRate,
	                                    uFrameRate, 1, uQuantum);
}

#endif // _AUDFRAMESCHEDULE_H
