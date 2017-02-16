-include $(TOP_DIR)/config.mak
SILENT=1
PREFIX=
ifeq ($(SILENT),1)
	PREFIX=@
endif

ARFLAGS=rc
ifdef CONFIG_RASPBERRY_PI
TOOLCHAIN:=/opt/arm-bcm2708/arm-bcm2708hardfp-linux-gnueabi
SYSROOT:=$(TOOLCHAIN)/arm-bcm2708hardfp-linux-gnueabi/sysroot
HOST:=arm-bcm2708hardfp-linux-gnueabi
LD:=$(TOOLCHAIN)/bin/$(HOST)-ld
CC:=$(TOOLCHAIN)/bin/$(HOST)-gcc
CXX:=$(TOOLCHAIN)/bin/$(HOST)-g++
OBJDUMP:=$(TOOLCHAIN)/bin/$(HOST)-objdump
RANLIB:=$(TOOLCHAIN)/bin/$(HOST)-ranlib
STRIP:=$(TOOLCHAIN)/bin/$(HOST)-strip
AR:=$(TOOLCHAIN)/bin/$(HOST)-ar
CXXCP:=$(CXX) -E
FLOAT=hard
CFLAGS+=-pipe -mfloat-abi=$(FLOAT) -mcpu=arm1176jzf-s -fomit-frame-pointer \
	-mabi=aapcs-linux -mtune=arm1176jzf-s -mfpu=vfp -Wno-psabi \
	-mno-apcs-stack-check -mstructure-size-boundary=32 -mno-sched-prolog
endif

ifdef CONFIG_PC
LD:=ld
CC:=gcc
CXX:=g++
OBJDUMP:=objdump
RANLIB:=ranlib
STRIP:=strip
AR:=ar
CXXCP:=$(CXX) -E
CFLAGS+=-pipe
CXXFLAGS=-std=c++0x
endif

ifdef LBMC_DEBUG
CFLAGS+=-g -O0 -DLBMC_DEBUG
else
CFLAGS+=-O2
endif

CFLAGS+=-Wall -Werror -Wno-deprecated-declarations
INCLUDES:=-I$(TOP_DIR)/inc -I/usr/include/freetype2/
ifdef CONFIG_RASPBERRY_PI
INCLUDES+=-I$(SYSROOT)/usr/include/interface/vcos/pthreads -I$(SYSROOT)/usr/include/interface/vmcs_host/linux
CFLAGS+=-DOMX_SKIP64BIT
endif

FFMPEG_LIBS = -lavdevice -lavformat -lavfilter -lavcodec -lswresample -lswscale -lavutil

LDFLAGS := $(FFMPEG_LIBS) -lpthread -lrt -lfreetype
ifdef CONFIG_RASPBERRY_PI
LDFLAGS += -lbcm_host -lWFC -lGLESv2 -lEGL -lopenmaxil -lvchiq_arm -lvcos
endif

ifdef CONFIG_PULSE_AUDIO
LDFLAGS += -lpulse -lpulse-simple
endif

ifdef CONFIG_OPENGL_VIDEO
LDFLAGS += -lGL -lGLU -lglut -lGLEW
endif

ifdef CONFIG_SDL2_VIDEO
LDFLAGS += -lSDL2
endif

ifdef CONFIG_LIBPNG
LDFLAGS += -lpng
endif

OBJ_DIR=objs
DEPFLAGS=-MM -MP -MT $(DEPFILE).o -MT $(DEPFILE).d

