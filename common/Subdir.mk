
OBJS-common-capture := \
	common/sound.o \
	common/webcam.o \
	common/dvb.o \
	common/tuning.o \
	common/commands.o \
	common/devs.o \
	common/parseconfig.o \
	common/fifo.o \
	common/capture.o \
	common/event.o \
	common/tv-config.o \
	libng/libng.a

OBJS-common-input := \
	common/lirc.o \
	common/joystick.o

OBJS-common-vbi := \
	common/vbi-dvb.o \
	common/vbi-data.o

# RegEdit.c is good old K&R ...
common/RegEdit.o: CFLAGS += -Wno-missing-prototypes -Wno-strict-prototypes

