TOP_DIR=.
include $(TOP_DIR)/envir.mak

DISTCFGDIR=configs
DISTCFG=.distconfig

TARGET=lbmc
SUBDIRS := utils audio demux
ifdef CONFIG_VIDEO
SUBDIRS+=video
endif

SRC=main.c

OBJ_PATH = $(TOP_DIR)/$(OBJ_DIR)
include $(TOP_DIR)/Makefile.include

all: init $(TARGET)

init:
	@if test ! -d $(OBJ_DIR); then mkdir $(OBJ_DIR); fi

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(PREFIX)make -C $@

$(TARGET): $(SUBDIRS) $(OBJS)
	@echo "[LD ] " $(TARGET)
	$(PREFIX)$(CXX) $(LDFLAGS) -o $(TARGET) $(shell find $(OBJ_DIR) -name '*.o')

config:
	@if test ! -f $(DISTCFG); then \
		./config.sh $(DISTCFGDIR)/$(DIST).cfg; \
		echo "DIST=$(DIST)" > $(DISTCFG); \
	else \
		echo "Dist already exist. Run make distclean first"; \
	fi

clean:
	@echo "Remove objects"
	@rm -rf $(OBJ_DIR)
	@echo "Remove target"
	@rm -f $(TARGET)

distclean: clean
	@rm -f $(DISTCFG)
	@rm -f config.mak

include $(TOP_DIR)/rules.mak

ifneq ($(MAKECMDGOALS),clean)
-include $(DEPS)
endif
