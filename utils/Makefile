TOP_DIR=..
include $(TOP_DIR)/envir.mak

SRC:=logs.c timeutils.c queue.c list.c msleep.c
ifdef CONFIG_RASPBERRY_PI
SRC += ilcore.c omxclock.c
endif

OBJ_PATH:=$(TOP_DIR)/$(OBJ_DIR)
include $(TOP_DIR)/Makefile.include

all: $(OBJS)

clean:
	@echo "Clean gtk directory"

include $(TOP_DIR)/rules.mak

-include $(DEPS)

