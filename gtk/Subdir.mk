
# targets to build
TARGETS-gtk := \
	gtk/pia \
	gtk/xawtv \
	gtk/mtt

ifeq ($(FOUND_DVB),yes)
TARGETS-gtk += \
	gtk/alexplore
OBJS-gtk-dvb := \
	gtk/gui-dvbscan.o \
	gtk/gui-dvbtune.o
endif

gtk/pia: \
	gtk/pia.o \
	gtk/av-sync.o \
	gtk/blit.o \
	gtk/gui-misc.o \
	gtk/xscreensaver.o \
	common/parseconfig.o \
	$(OBJS-common-capture)

gtk/xawtv: \
	gtk/xawtv.o \
	gtk/av-sync.o \
	gtk/blit.o \
	gtk/gui-control.o \
	$(OBJS-gtk-dvb) \
	gtk/gui-misc.o \
	gtk/xscreensaver.o \
	x11/xv.o \
	x11/atoms.o \
	common/parseconfig.o \
	$(OBJS-common-capture) \
	$(OBJS-glib-dvb)

gtk/mtt: \
	gtk/mtt.o \
	gtk/gui-teletext.o \
	gtk/gui-misc.o \
	gtk/xscreensaver.o \
	console/vbi-tty.o \
	common/vbi-data.o \
	common/vbi-dvb.o \
	common/parseconfig.o \
	$(OBJS-common-capture) \
	$(OBJS-glib-dvb)

gtk/alexplore: \
	gtk/alexplore.o \
	$(OBJS-gtk-dvb) \
	gtk/gui-misc.o \
	gtk/xscreensaver.o \
	common/parseconfig.o \
	$(OBJS-common-capture) \
	$(OBJS-glib-dvb)

$(TARGETS-gtk) : CFLAGS  += $(GTK_CFLAGS)
$(TARGETS-gtk) : LDLIBS  += $(GTK_LIBS) $(GL_LIBS) $(THREAD_LIBS) -ljpeg -lm
$(TARGETS-gtk) : LDFLAGS += $(DLFLAGS)

gtk/mtt        : LDLIBS  += $(VBI_LIBS)

ifeq ($(FOUND_GTK),yes)

all:: $(TARGETS-gtk)

install::
	$(INSTALL_PROGRAM) -s $(TARGETS-gtk) $(bindir)

distclean::
	rm -f $(TARGETS-gtk)

endif
