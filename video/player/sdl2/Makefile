TOP_DIR=../../..
include $(TOP_DIR)/envir.mak

SRC:=sdl_player.c

OBJ_PATH:=$(TOP_DIR)/$(OBJ_DIR)
include $(TOP_DIR)/Makefile.include

all: $(OBJS)

clean:
	@echo "Clean sdl2 directory"

include $(TOP_DIR)/rules.mak

-include $(DEPS)

