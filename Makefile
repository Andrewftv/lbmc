TOP_DIR=.
include $(TOP_DIR)/envir.mak

DISTCFGDIR=configs
DISTCFG=.distconfig
CONF_FILES := $(wildcard $(DISTCFGDIR)/*.cfg)

TARGET=lbmc

SUBDIRS := utils audio demux ui
ifdef CONFIG_VIDEO
SUBDIRS+=video
endif

SRC=main.c

OBJ_PATH = $(TOP_DIR)/$(OBJ_DIR)
include $(TOP_DIR)/Makefile.include

all: init $(TARGET)

init:
	@if test ! -d $(OBJ_DIR); then mkdir $(OBJ_DIR); fi
	@if test ! -f $(DISTCFG); then \
		echo ""; \
		echo "Project is not configured. Run \"make config DIST=<dist-name>\""; \
		echo ""; \
		echo "Existing configurations:"; \
		echo ""; \
		for file in $(CONF_FILES); do \
			conf=$${file#*\/}; \
			conf=$${conf%.*}; \
			echo $$conf; \
		done; \
		echo ""; \
		exit 1; \
	fi

.PHONY: $(SUBDIRS)
$(SUBDIRS):
	$(PREFIX)make -C $@

$(TARGET): $(SUBDIRS) $(OBJS)
	@echo "[LINK] " $(TARGET)
	$(PREFIX)$(CC) $(LDFLAGS) -o $(TARGET) -Wl,--start-group $(shell find $(OBJ_DIR) -name '*.a') \
		$(shell find $(OBJ_DIR) -name '*.o') -Wl,--end-group

config:
	@if test ! -f $(DISTCFG); then \
		./config.sh $(DISTCFGDIR)/$(DIST).cfg; \
		echo "DIST=$(DIST)" > $(DISTCFG); \
	else \
		echo "Dist already exist. Run make distclean first"; \
	fi

clean:
	@for dir in $(SUBDIRS); do \
		make clean -C $$dir; \
	done
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
