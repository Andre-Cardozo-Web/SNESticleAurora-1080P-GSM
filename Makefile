# =========================================================================
# SNESticle Aurora - PlayStation 2 Makefile
# =========================================================================

# Nome do executável final e diretórios
EE_BIN = build/SNESticle.elf
OBJ_DIR = build/obj

# Coleta automática de arquivos de código na pasta src
SRCS = $(wildcard src/*.c) $(wildcard src/*.cpp) $(wildcard src/*.s) $(wildcard src/*.S)

# Alvo padrão (Primeira regra que o make executará)
all: $(OBJ_DIR) $(EE_BIN)

# Lista completa de objetos (Adicionado o prefixo EE_ exigido pela SDK do PS2)
EE_OBJS := \
	$(patsubst src/%.c,$(OBJ_DIR)/%.o,$(filter %.c,$(SRCS))) \
	$(patsubst src/%.cpp,$(OBJ_DIR)/%.o,$(filter %.cpp,$(SRCS))) \
	$(patsubst src/%.s,$(OBJ_DIR)/%.o,$(filter %.s,$(SRCS))) \
	$(patsubst src/%.S,$(OBJ_DIR)/%.o,$(filter %.S,$(SRCS))) \
	$(OBJ_DIR)/platform/ps2/system/mainloop_state.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_iop.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_net.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_smb.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_ui.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_install.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_menu.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_browser.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_load.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_input.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_exec.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_globals.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_init.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_render.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_process.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_menu_runtime.o \
	$(OBJ_DIR)/platform/ps2/system/mainloop_bgm.o \
	$(OBJ_DIR)/platform/ps2/system/global_alloc.o \
	$(OBJ_DIR)/platform/ps2/system/embedded_irx.o \
	$(OBJ_DIR)/platform/ps2/ui/uiBrowser.o \
	$(OBJ_DIR)/platform/ps2/ui/uiCover.o \
	$(OBJ_DIR)/platform/ps2/ui/uiLog.o \
	$(OBJ_DIR)/platform/ps2/ui/uiMenu.o

# Criação das pastas de build caso não existam
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
	mkdir -p $(OBJ_DIR)/platform/ps2/system
	mkdir -p $(OBJ_DIR)/platform/ps2/ui
	mkdir -p build

# Regras explicitas para forcar a compilacao dos modulos internos do sistema PS2
$(OBJ_DIR)/platform/ps2/system/%.o: src/platform/ps2/system/%.cpp | $(OBJ_DIR)
	$(EE_CXX) $(EE_CXXFLAGS) $(EE_INCS) -c $< -o $@

$(OBJ_DIR)/platform/ps2/ui/%.o: src/platform/ps2/ui/%.cpp | $(OBJ_DIR)
	$(EE_CXX) $(EE_CXXFLAGS) $(EE_INCS) -c $< -o $@

# Regra genérica para compilar arquivos .cpp da raiz de src
$(OBJ_DIR)/%.o: src/%.cpp | $(OBJ_DIR)
	$(EE_CXX) $(EE_CXXFLAGS) $(EE_INCS) -c $< -o $@

# Regra genérica para compilar arquivos .c da raiz de src
$(OBJ_DIR)/%.o: src/%.c | $(OBJ_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

# Inclusão obrigatória das regras globais da SDK do PS2
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal

# Limpeza dos arquivos de build
clean:
	rm -rf build
