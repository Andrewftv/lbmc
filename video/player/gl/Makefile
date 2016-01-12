TOP_DIR=../../..
include $(TOP_DIR)/envir.mak

SRC:=gl_player.c

OBJ_PATH:=$(TOP_DIR)/$(OBJ_DIR)
include $(TOP_DIR)/Makefile.include

all: $(OBJS)

clean:
	@echo "Clean gtk directory"

include $(TOP_DIR)/rules.mak

-include $(DEPS)

