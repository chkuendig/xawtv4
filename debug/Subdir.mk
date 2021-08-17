
# variables
TARGETS-debug := \
	debug/sysfs

ifeq ($(FOUND_X11),yes)
TARGETS-debug += \
	debug/xvideo
endif
ifeq ($(FOUND_DVB),yes)
TARGETS-debug += \
	debug/dvb-signal \
	debug/vbi-rec \
	debug/epg
endif
ifeq ($(FOUND_ALSA),yes)
TARGETS-debug += \
	debug/alsamixer
endif

debug/dvb-signal: \
	debug/dvb-signal.o \
	common/parseconfig.o \
	$(OBJS-common-dvb) \
	libng/libng.a

debug/vbi-rec: \
	debug/vbi-rec.o \
	common/parseconfig.o \
	$(OBJS-common-dvb) \
	libng/libng.a

debug/epg: \
	debug/epg.o \
	common/devs.o \
	common/parseconfig.o \
	$(OBJS-common-dvb) \
	$(OBJS-glib-dvb) \
	libng/libng.a

debug/xvideo:     debug/xvideo.o
debug/sysfs:      debug/sysfs.o
debug/alsamixer:  debug/alsamixer.o

debug/epg.o      : CFLAGS  += $(shell pkg-config --cflags glib-2.0 libxml-2.0)

debug/xvideo     : LDLIBS  += $(ATHENA_LIBS) $(DLFLAGS)
debug/alsamixer  : LDLIBS  += $(ALSA_LIBS) $(DLFLAGS)

debug/dvb-signal : LDLIBS += $(DLFLAGS)
debug/vbi-rec    : LDLIBS += $(DLFLAGS)
debug/epg        : LDLIBS += $(DLFLAGS) $(shell pkg-config --libs glib-2.0 libxml-2.0)

# poor mans malloc debugging
CFLAGS           += -fno-omit-frame-pointer
ifeq (1,0)
  malloc_targets := x11/pia
  malloc_wraps   := -Wl,--wrap=malloc
  malloc_wraps   += -Wl,--wrap=free
  $(malloc_targets) : debug/malloc.o
  $(malloc_targets) : LDFLAGS += -rdynamic $(malloc_wraps)
endif

# global targets
all:: $(TARGETS-debug)

distclean::
	rm -f $(TARGETS-debug)

