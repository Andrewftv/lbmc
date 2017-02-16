TOP_DIR=..
include $(TOP_DIR)/envir.mak

SRC := control.c
ifdef CONFIG_RASPBERRY_PI
SRC += draw_prim.c init_gui.c image.c
else
ifdef CONFIG_LIBPNG
SRC += image.c
endif
endif

LIBA=libui.a
OBJ_PATH:=.
include $(TOP_DIR)/Makefile.include

all: $(OBJS) $(LIBA)

$(LIBA):
	@echo "[AR ] " $(LIBA)
	$(PREFIX)$(AR) $(ARFLAGS) $(TOP_DIR)/$(OBJ_DIR)/$(LIBA) $(OBJS)

clean:
	@echo "Clean utils directory"
	@rm -f *.o
	@rm -f *.d

include $(TOP_DIR)/rules.mak

-include $(DEPS)

