ifeq ($(FOUND_ZVBI),yes)
OBJS-common-vbi := \
	common/vbi-dvb.o \
	common/vbi-data.o
endif

ifeq ($(FOUND_DVB),yes)
OBJS-common-dvb := \
	common/dvb-tuning.o \
	structs/struct-dvb.o \
	structs/struct-dump.o
OBJS-glib-dvb := \
	common/dvb-monitor.o \
	common/dvb-lang.o \
	common/dvb-epg.o
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
	$(OBJS-common-dvb) \
	libng/libng.a

OBJS-common-input := \
	common/lirc.o \
	common/joystick.o

# RegEdit.c is good old K&R ...
common/RegEdit.o     : CFLAGS += -Wno-missing-prototypes -Wno-strict-prototypes
common/dvb-monitor.o : CFLAGS += $(GTK_CFLAGS)  # uses glib only through
common/dvb-epg.o     : CFLAGS += $(GTK_CFLAGS)  # uses glib only through
