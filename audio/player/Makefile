TOP_DIR=../..
include $(TOP_DIR)/envir.mak

SRC:=

ifdef CONFIG_PULSE_AUDIO
SUBDIRS+=pulse
endif

ifdef CONFIG_RASPBERRY_PI
SUBDIRS+=raspi
endif

OBJ_PATH:=.
include $(TOP_DIR)/Makefile.include

all: $(SUBDIRS) $(OBJS)

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(PREFIX)make -C $@

clean:
	@echo "Clean gui direcrory"

include $(TOP_DIR)/rules.mak

