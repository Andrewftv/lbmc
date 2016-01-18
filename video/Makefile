TOP_DIR=..
include $(TOP_DIR)/envir.mak

SRC:=

SUBDIRS+=player common

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
	@echo "Clean video direcrory"

include $(TOP_DIR)/rules.mak

