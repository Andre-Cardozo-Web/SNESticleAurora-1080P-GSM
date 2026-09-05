OBJS := \
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

# Regras explicitas para forcar a compilacao dos modulos internos do sistema PS2
$(OBJ_DIR)/platform/ps2/system/%.o: src/platform/ps2/system/%.cpp | $(OBJ_DIR)
	$(call RUN_COMPILE,CXX,$<,$(EE_CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCS) -c "$<" -o "$@")

$(OBJ_DIR)/platform/ps2/ui/%.o: src/platform/ps2/ui/%.cpp | $(OBJ_DIR)
	$(call RUN_COMPILE,CXX,$<,$(EE_CXX) $(CXXFLAGS) $(DEPFLAGS) $(INCS) -c "$<" -o "$@")
