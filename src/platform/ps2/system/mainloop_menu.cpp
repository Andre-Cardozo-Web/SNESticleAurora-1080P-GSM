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

// Caminhos globais corrigidos com o prefixo canônico exigido pelo Makefile
#include "mainloop_exec.h"                         // Mantido o canônico local
#include "../../../md/picodrive/picodrive_bridge.h"   // Já alterado por você! Perfeito.
#include "../../../nes/quicknes/quicknes_bridge.h"   // Alterar linha 19 para usar o mesmo recuo funcional

extern "C" {
#include "audio.h"
}

extern "C" int list_title_db(char *pPath);
