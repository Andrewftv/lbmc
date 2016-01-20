TOP_DIR=../../..
include $(TOP_DIR)/envir.mak

SRC:=pulse_player.c

LIBA=libaudio_player.a
OBJ_PATH:=.
include $(TOP_DIR)/Makefile.include

all: $(OBJS) $(LIBA)

$(LIBA):
	@echo "[AR ] " $(LIBA)
	$(PREFIX)$(AR) $(ARFLAGS) $(TOP_DIR)/$(OBJ_DIR)/$(LIBA) $(OBJS)

clean:
	@echo "Clean pulse directory"
	@rm -f *.o
	@rm -f *.d

include $(TOP_DIR)/rules.mak

-include $(DEPS)

