
# targets to build
TARGETS-console := \
	console/dump-mixers \
	console/record \
	console/showriff \
	console/showqt \
	console/streamer \
	console/webcam
TARGETS-v4l-conf := 

ifeq ($(FOUND_ZVBI),yes)
TARGETS-console += \
	console/scantv
endif
ifeq ($(FOUND_AALIB),yes)
TARGETS-console += \
	console/ttv
endif
ifeq ($(FOUND_OS),linux)
TARGETS-console += \
	console/radio \
	console/v4l-info
#	console/fbtv
TARGETS-v4l-conf += \
	console/v4l-conf
endif

# objects for targets
console/fbtv: \
	console/fbtv.o \
	console/fbtools.o \
	console/fs.o \
	console/matrox.o \
	$(OBJS-common-input) \
	$(OBJS-common-capture)

console/ttv: \
	console/ttv.o \
	$(OBJS-common-capture)

console/scantv: \
	console/scantv.o \
	$(OBJS-common-vbi) \
	$(OBJS-common-capture)

console/streamer: \
	console/streamer.o \
	$(OBJS-common-capture)

console/webcam: \
	console/webcam.o \
	console/ftp.o \
	common/parseconfig.o \
	common/devs.o \
	$(OBJS-common-dvb) \
	libng/libng.a

console/v4l-info: \
	console/v4l-info.o \
	structs/struct-dump.o \
	structs/struct-v4l.o \
	structs/struct-v4l2.o
ifeq ($(FOUND_DVB),yes)
console/v4l-info: \
	structs/struct-dvb.o
endif

console/radio: \
	console/radio.o \
	libng/devices.o

console/dump-mixers: console/dump-mixers.o
console/showriff: console/showriff.o
console/showqt: console/showqt.o
console/showmpeg: console/showmpeg.o
console/record: console/record.o
console/v4l-conf: console/v4l-conf.o

# libraries to link
console/fbtv     : LDLIBS  += \
	$(THREAD_LIBS) $(CURSES_LIBS) $(LIRC_LIBS) $(ALSA_LIBS) \
	$(FS_LIBS) -ljpeg -lm $(DLFLAGS)
console/ttv          : LDLIBS  += $(THREAD_LIBS) $(AA_LIBS) -ljpeg -lm $(DLFLAGS)
console/scantv       : LDLIBS  += $(THREAD_LIBS) $(VBI_LIBS) -ljpeg $(DLFLAGS)
console/streamer     : LDLIBS  += $(THREAD_LIBS) $(ICONV_LIBS) -ljpeg -lm $(DLFLAGS)
console/webcam       : LDLIBS  += $(THREAD_LIBS) $(ICONV_LIBS) -ljpeg -lm $(DLFLAGS)
console/radio        : LDLIBS  += $(CURSES_LIBS) $(DLFLAGS)
console/record       : LDLIBS  += $(CURSES_LIBS) $(OSS_LIBS) $(DLFLAGS)
console/dump-mixers  : LDLIBS  += $(OSS_LIBS) $(DLFLAGS)
console/v4l-conf     : LDLIBS  += $(X11_LIBS) $(DLFLAGS)

# global targets
all:: $(TARGETS-console) $(TARGETS-v4l-conf)

install::
	$(INSTALL_PROGRAM) $(TARGETS-console) $(bindir)
ifneq ($(TARGETS-v4l-conf),)
	$(INSTALL_PROGRAM) $(SUID_ROOT) $(TARGETS-v4l-conf) $(bindir)
endif

distclean::
	rm -f $(TARGETS-console) $(TARGETS-v4l-conf)
