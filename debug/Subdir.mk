
# variables
TARGETS-debug := \
	debug/probe \
	debug/sysfs

ifeq ($(FOUND_X11),yes)
TARGETS-debug += \
	debug/xvideo
endif
ifeq ($(FOUND_DVB),yes)
TARGETS-debug += \
	debug/dvb-signal
endif

debug/probe: \
	debug/probe.o \
	$(OBJS-dvb) \
	common/devs.o \
	common/parseconfig.o \
	libng/libng.a

debug/dvb-signal: \
	debug/dvb-signal.o \
	common/parseconfig.o \
	$(OBJS-dvb) \
	libng/libng.a

debug/xvideo: debug/xvideo.o

debug/probe      : LDLIBS  += -ljpeg -lm
debug/xvideo     : LDLIBS  += $(ATHENA_LIBS)

debug/probe      : LDFLAGS += $(DLFLAGS)
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

