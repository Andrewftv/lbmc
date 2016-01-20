TOP_DIR=..
include $(TOP_DIR)/envir.mak

SRC:=logs.c timeutils.c queue.c list.c msleep.c
ifdef CONFIG_RASPBERRY_PI
SRC += ilcore.c omxclock.c
endif

LIBA=libutils.a
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

