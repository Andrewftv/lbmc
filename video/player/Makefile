TOP_DIR=../..
include $(TOP_DIR)/envir.mak

SRC:=

ifdef CONFIG_OPENGL_VIDEO
SUBDIRS+=gl
endif

ifdef CONFIG_SDL2_VIDEO
SUBDIRS+=sdl2
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
	@for dir in $(SUBDIRS); do \
		make clean -C $$dir; \
	done
	@echo "Clean player direcrory"

include $(TOP_DIR)/rules.mak

