TOP_DIR=../..
include $(TOP_DIR)/envir.mak

SRC:=ft_text.c video_player.c
ifdef CONFIG_RASPBERRY_PI
SRC+=hdmi.c
endif

OBJ_PATH:=$(TOP_DIR)/$(OBJ_DIR)
include $(TOP_DIR)/Makefile.include

all: $(SUBDIRS) $(OBJS)

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(PREFIX)make -C $@

clean:
	@echo "Clean gui direcrory"

include $(TOP_DIR)/rules.mak

