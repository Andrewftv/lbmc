TOP_DIR=../..
include $(TOP_DIR)/envir.mak

SRC:=ft_text.c video_player.c
ifdef CONFIG_RASPBERRY_PI
SRC+=hdmi.c
endif

LIBA=libcommon_video.a
OBJ_PATH:=.
include $(TOP_DIR)/Makefile.include

all: $(SUBDIRS) $(OBJS) $(LIBA)

$(LIBA):
	@echo "[AR ] " $(LIBA)
	$(PREFIX)$(AR) $(ARFLAGS) $(TOP_DIR)/$(OBJ_DIR)/$(LIBA) $(OBJS)

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(PREFIX)make -C $@

clean:
	@echo "Clean gui direcrory"
	@rm -f *.o
	@rm -f *.d

include $(TOP_DIR)/rules.mak

