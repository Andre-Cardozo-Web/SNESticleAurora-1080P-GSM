IOP_BIN = aurora_ps2mouse.irx
IOP_OBJS = ps2mouse.o imports.o
IOP_INCS += -I.

all: $(IOP_BIN)

clean:
	rm -f -r $(IOP_OBJS) $(IOP_BIN) imports.s

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
