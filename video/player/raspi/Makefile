TOP_DIR=../../..
include $(TOP_DIR)/envir.mak

SRC:=raspi_video_player.c

LIBA=libvideo_player.a
OBJ_PATH:=.
include $(TOP_DIR)/Makefile.include

all: $(OBJS) $(LIBA)

$(LIBA):
	@echo "[AR ] " $(LIBA)
	$(PREFIX)$(AR) $(ARFLAGS) $(TOP_DIR)/$(OBJ_DIR)/$(LIBA) $(OBJS)

clean:
	@echo "Clean raspi directory"
	@rm -f *.o
	@rm -f *.d

include $(TOP_DIR)/rules.mak

-include $(DEPS)

