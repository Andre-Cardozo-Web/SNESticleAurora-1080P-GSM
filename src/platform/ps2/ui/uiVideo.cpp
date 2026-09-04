#include <libpad.h>
#include <stdio.h>

#include "types.h"
#include "input.h"
#include "font.h"
#include "gskit_backend.h"
#include "mainloop_shared.h"
#include "mainloop_menu.h"
#include "mainloop_ui.h"

// Cabeçalhos adicionados para as pontes de áudio, emuladores e sistemas
#include "mainloop_exec.h"
#include "picodrive_bridge.h"
#include "quicknes_bridge.h"
#include "audmixbuffer.h"
#include "snes_bridge.h"
#include "storage.h"

// Declarações internas da tela de vídeo do emulador
extern int _VideoModeIndex(int mode);
extern struct { int mode; } _VideoModes[];
extern int _VideoCycleSystemAudioRate(int rate, int dir);
extern void _VideoSetCdMusicEnabled(int enabled);
extern void _VideoApplyCompatFlags(int flags);
