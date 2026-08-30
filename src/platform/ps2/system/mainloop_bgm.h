/* mainloop_bgm.h
 *
 * Trilha sonora de fundo do menu (browser de ROMs e menu de pausa).
 *
 * Toca modulos de tracker .mod (Amiga ProTracker) e .xm (FastTracker II)
 * via libxmp-lite. O PCM e' gerado na CPU (EE) a cada frame de menu e
 * empurrado para o audsrv pela API Aud_* (48000 Hz / 16-bit / stereo).
 *
 * Uso:
 *   - BgmUpdate() e' chamado a cada frame enquanto o menu esta visivel
 *     (MainLoopRender, bloco `if (_bMenu)`).  Ele faz lazy-load da
 *     primeira faixa achada e alimenta o audsrv com o que couber.
 *   - BgmStop() para a reproducao ao iniciar uma ROM.
 *
 * Os arquivos sao procurados em BGM_PATH (define do Makefile, espelha
 * COVERS_PATH) e em algumas pastas padrao (ver mainloop_bgm.cpp).
 */

#ifndef _MAINLOOP_BGM_H
#define _MAINLOOP_BGM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Alimenta o audsrv com PCM da faixa atual.  Faz lazy-load na 1a
   chamada.  Seguro chamar todo frame; nao faz nada se desabilitado,
   se nao houver faixa, ou se o audio ainda nao esta pronto. */
void BgmUpdate(void);

/* Mark menu entry without doing discovery or file I/O. This lets L2+R2 show
   its first frame immediately; normal BgmUpdate performs lazy loading. */
void BgmMenuEnter(void);

/* Para a reproducao (chamado ao lancar uma ROM).  NAO libera o decoder:
   mantem a faixa carregada para reabrir o menu sem reler do disco. */
void BgmStop(void);

/* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830
 * Stronger than BgmStop only for a real ROM/disc transition. */
void BgmReleaseDecoderForGameSwitch(void);

/* Avanca explicitamente para a proxima faixa. So' troca se houver 2+
   faixas; a retomada normal do menu preserva o decoder e nao chama isto. */
void BgmNext(void);
void BgmSetTrackIndex(int track);
int  BgmGetTrackIndex(void);
/* AURORA_PD_MEGA_FIX_20260820 */

/* Scope synchronous UI/file operations. While at least one scope is active,
   a small EE helper keeps an already-loaded tracker feeding audsrv without
   touching the filesystem. Calls may be nested. */
void BgmIOBegin(void);
void BgmIOEnd(void);

/* Menu Volume: ganho PCM interno 0..400, exibido no Settings Menu /2
   como 0..200; 200 interno = UI 100 = unity. O liga/desliga e independente
   e controlado por BgmSetEnabled/BgmIsEnabled. */
void BgmSetVolume(int vol);
int  BgmGetVolume(void);
void BgmSetEnabled(int enabled);
int  BgmIsEnabled(void);

/* Numero de faixas .mod/.xm achadas. Dispositivos locais sao escaneados
   imediatamente; o CD/DVD e' acrescentado depois de uma sondagem segura. */
int  BgmTrackCount(void);
int  BgmIsSearching(void);

/* Frequencia de sintese (Hz).  A saida e' sempre 48 kHz (reamostrada).
   BgmCycleRate(+1/-1) percorre a lista de frequencias oferecidas. */
int  BgmGetRate(void);
void BgmSetRate(int hz);
void BgmCycleRate(int dir);

#ifdef __cplusplus
}
#endif

#endif /* _MAINLOOP_BGM_H */

/* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830 */
