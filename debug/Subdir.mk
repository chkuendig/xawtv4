
# variables
TARGETS-debug := \
	debug/sysfs

ifeq ($(FOUND_X11),yes)
TARGETS-debug += \
	debug/xvideo
endif
ifeq ($(FOUND_DVB),yes)
TARGETS-debug += \
	debug/dvb-signal
endif
ifeq ($(FOUND_ALSA),yes)
TARGETS-debug += \
	debug/alsamixer
endif

debug/dvb-signal: \
	debug/dvb-signal.o \
	common/parseconfig.o \
	$(OBJS-dvb) \
	libng/libng.a

debug/xvideo: debug/xvideo.o

debug/xvideo     : LDLIBS  += $(ATHENA_LIBS)
debug/alsamixer  : LDLIBS  += $(ALSA_LIBS)

debug/dvb-signal : LDFLAGS += $(DLFLAGS)

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

