ifeq ($(FOUND_DVB),yes)
OBJS-dvb := \
	common/dvb-tuning.o \
	structs/struct-dvb.o \
	structs/struct-dump.o
endif

ifeq ($(FOUND_ZVBI),yes)
OBJS-common-vbi := \
	common/vbi-dvb.o \
	common/vbi-data.o
endif

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
	common/tv-config.o \
	$(OBJS-dvb) \
	libng/libng.a

OBJS-common-input := \
	common/lirc.o \
	common/joystick.o

# RegEdit.c is good old K&R ...
common/RegEdit.o: CFLAGS += -Wno-missing-prototypes -Wno-strict-prototypes
