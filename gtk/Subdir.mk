
# targets to build
TARGETS-gtk := \
	gtk/pia \
	gtk/xawtv

ifeq ($(FOUND_DVB),yes)
TARGETS-gtk += \
	gtk/alexplore
endif

gtk/pia: \
	gtk/pia.o \
	gtk/av-sync.o \
	gtk/blit.o \
	gtk/gui-misc.o \
	common/parseconfig.o \
	$(OBJS-common-capture)

gtk/xawtv: \
	gtk/xawtv.o \
	gtk/av-sync.o \
	gtk/blit.o \
	gtk/gui-misc.o \
	gtk/gui-control.o \
	x11/xv.o \
	x11/atoms.o \
	common/dvb-monitor.o \
	common/parseconfig.o \
	$(OBJS-common-capture)

gtk/alexplore: \
	gtk/alexplore.o \
	gtk/gui-misc.o \
	gtk/gui-dvbtune.o \
	common/dvb-monitor.o \
	common/parseconfig.o \
	$(OBJS-common-capture)

$(TARGETS-gtk) : CFLAGS  += $(GTK_CFLAGS)
$(TARGETS-gtk) : LDLIBS  += $(GTK_LIBS) $(GL_LIBS) $(THREAD_LIBS) -ljpeg -lm
$(TARGETS-gtk) : LDFLAGS += $(DLFLAGS)


ifeq ($(FOUND_GTK),yes)

all:: $(TARGETS-gtk)

install::
	$(INSTALL_PROGRAM) -s $(TARGETS-gtk) $(bindir)

endif
