
OBJS-common-capture := \
	common/sound.o \
	common/webcam.o \
	common/tuning.o \
	common/commands.o \
	common/devs.o \
	common/parseconfig.o \
	common/fifo.o \
	common/capture.o \
	common/event.o \
	common/tv-config.o

OBJS-common-input := \
	common/lirc.o \
	common/joystick.o

OBJS-common-vbi := \
	common/vbi-dvb.o \
	common/vbi-data.o

ifeq ($(FOUND_DVB),yes)
OBJS-common-capture  += common/dvb.o
endif
OBJS-common-capture  += libng/libng.a

# RegEdit.c is good old K&R ...
common/RegEdit.o: CFLAGS += -Wno-missing-prototypes -Wno-strict-prototypes
