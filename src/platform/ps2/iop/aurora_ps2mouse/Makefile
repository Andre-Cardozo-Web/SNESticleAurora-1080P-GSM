# AURORA_FCEUMM_FDS_V5_CI_SMB_UPSTREAM_PERF_20260827
# Standalone IOP/IRX build against the *installed* PS2SDK.
# Do not include $(PS2SDK)/samples/Makefile.*: current ps2dev packages do not
# install those source-tree sample makefiles.

IOP_BIN := aurora_ps2mouse.irx
IOP_OBJS := ps2mouse.o imports.o
IOP_BIN_ELF := $(IOP_BIN:.irx=.notiopmod.elf)
IOP_BIN_STRIPPED := $(IOP_BIN:.irx=.notiopmod.stripped.elf)

include $(PS2SDK)/Defs.make

IOP_CC_VERSION := $(shell $(IOP_CC) -dumpversion 2>/dev/null)
IOP_CFLAGS_LOCAL := -D_IOP -fno-builtin -G0 -Os -Wall -Werror \
	-I. -I$(PS2SDK)/iop/include -I$(PS2SDK)/common/include
IOP_IETABLE_CFLAGS_LOCAL :=

# Match current PS2SDK's ABI workarounds, while retaining compatibility with
# the historical 3.2.2/3.2.3 IOP compiler if a local toolchain still uses it.
ifneq ($(IOP_CC_VERSION),3.2.2)
ifneq ($(IOP_CC_VERSION),3.2.3)
IOP_CFLAGS_LOCAL += -msoft-float -mno-explicit-relocs
IOP_IETABLE_CFLAGS_LOCAL += -fno-toplevel-reorder
endif
endif

IOP_LINKFILE_LOCAL := $(PS2SDK)/iop/startup/linkfile
SRXFIXUP_LOCAL := $(PS2SDK)/bin/srxfixup

.PHONY: all clean
all: $(IOP_BIN)

ps2mouse.o: ps2mouse.c ps2mouse.h
	$(IOP_CC) $(IOP_CFLAGS_LOCAL) -c $< -o $@

build-imports.c: irx_imports.h imports.lst
	@printf '%s\n' '#include "irx_imports.h"' > $@
	@cat imports.lst >> $@

imports.o: build-imports.c irx_imports.h imports.lst
	$(IOP_CC) $(IOP_CFLAGS_LOCAL) $(IOP_IETABLE_CFLAGS_LOCAL) -c $< -o $@

$(IOP_BIN_ELF): $(IOP_OBJS)
	$(IOP_CC) $(IOP_CFLAGS_LOCAL) -T$(IOP_LINKFILE_LOCAL) -Os \
		-o $@ $(IOP_OBJS) -nostdlib -dc -r

$(IOP_BIN_STRIPPED): $(IOP_BIN_ELF)
	$(IOP_STRIP) --strip-unneeded --remove-section=.pdr \
		--remove-section=.comment --remove-section=.mdebug.abi32 \
		--remove-section=.gnu.attributes -o $@ $<

$(IOP_BIN): $(IOP_BIN_STRIPPED)
	$(SRXFIXUP_LOCAL) --rb --irx1 --allow-zero-text -o $@ $<

clean:
	rm -f ps2mouse.o imports.o build-imports.c \
		$(IOP_BIN_ELF) $(IOP_BIN_STRIPPED) $(IOP_BIN)
