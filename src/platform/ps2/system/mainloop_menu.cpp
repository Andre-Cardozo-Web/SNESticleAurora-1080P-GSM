#include <libpad.h>
#include <stdio.h>

#include "types.h"
#include "mainloop_install.h"
#include "mainloop_input.h"
#include "input.h"
#include "mainloop_menu.h"
#include "mainloop_iop.h"
#include "mainloop_shared.h"
#include "mainloop_state.h"
#include "mainloop_ui.h"
#include "embedded_irx.h"
#include "memcard.h"

// Cabeçalhos de integração adicionados para resolver escopo
#include "mainloop_exec.h"
#include "picodrive_bridge.h"
#include "quicknes_bridge.h"

extern "C" {
#include "audio.h"
}

extern "C" int list_title_db(char *pPath);
