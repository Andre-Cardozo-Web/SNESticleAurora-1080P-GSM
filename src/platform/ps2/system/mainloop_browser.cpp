#include <string.h>

#include "types.h"
#include "path.h"
#include "pathext.h"
#include "mainloop_browser.h"
#include "mainloop_load.h"
#include "mainloop_net.h"
#include "mainloop_shared.h"
#include "mainloop_ui.h"
#include "mainloop_bgm.h"
#include "mainloop_menu.h" /* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830 */
#include "sega/picodrive/picodrive_bridge.h" /* AURORA_V4_17_SAFE_CD_GAME_SWITCH_QUIESCE_20260830 */

extern "C" {
#include "audio.h"
}

int _MainLoopBrowserEvent(Uint32 Type, Uint32 Parm1, void *Parm2)
{
        switch (Type)
        {
                case 1:
                {
                        Char *str = (Char *)Parm2;
                        NetPlayRPCStatusT status;
                        NetPlayGetStatus(&status);

                        if (status.eClientStatus == NETPLAY_STATUS_CONNECTED)
                        {
                                NetPlayClientSendLoadReq(str);
                        }
                        else
                        {
                                /* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830
                                 * Ignore silently before BGM/audio/core teardown. */
                                if (MainLoopSramSaveBusy())
                                        return 1;

                                /* AURORA_V4_17_SAFE_CD_GAME_SWITCH_QUIESCE_20260830
                                 * Do not mute BGM/audio or destroy any core until
                                 * Sega CD's private CDDA RPC transport is idle.
                                 * The helper is bounded; a pathological read
                                 * becomes a harmless retry instead of a freeze. */
                                if (!PicoDriveBridge_PrepareGameSwitch())
                                {
                                        MainLoopStatusPrintf(
                                                120,
                                                "Sega CD I/O busy; try again.");
                                        return 1;
                                }

                                /* Antes do load (que bloqueia a EE por mais
                                   tempo que o ring de ~107ms do audsrv): para
                                   e MUTA a trilha de menu.  Senao a cauda da
                                   musica fica "picotando" (underrun) durante o
                                   carregamento da ROM.  O _MenuEnable(FALSE)
                                   abaixo restaura o volume com a ROM ja'
                                   carregada; no erro, restauramos na mao. */
                                BgmStop();
                                if (Aud_IsInitialized()) Aud_Setvol(0);

                                // load rom with sram load
                                if (_MainLoopExecuteFile(str, TRUE))
                                {
                                        _MenuEnable(FALSE);
                                }
                                else
                                {
                                        if (Aud_IsInitialized()) Aud_Setvol(0x3FFF);
                                        MainLoopModalPrintf(60*1, "ERROR: %s\n", str);
                                }
                        }
                        return 1;
                }

                case 2:
                {
                        char str[256];
                        char *pName = (char *)Parm2;
                        PathExtTypeE eType;

                        strcpy(str, pName);

                        // figure out what type of file this is
                        if (PathExtResolve(str, &eType, TRUE))
                        {
                                return BROWSER_ENTRYTYPE_EXECUTABLE;
                        }

                        return BROWSER_ENTRYTYPE_OTHER;
                }
        }
        return 0;
}

/* AURORA_V4_16_SAFE_GAME_SWITCH_FLUSH_20260830 */
