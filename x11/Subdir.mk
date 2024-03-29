
# targets to build
TARGETS-x11 := \
	x11/v4lctl

ifeq ($(FOUND_X11),yes)
TARGETS-x11 += \
	x11/propwatch \
	x11/xawtv-remote \
	x11/rootv
endif

#ifeq ($(FOUND_MOTIF),yes)
#TARGETS-x11 += \
#	x11/motv
#endif
#ifeq ($(FOUND_MOTIF)$(FOUND_ZVBI),yesyes)
#TARGETS-x11 += \
#	x11/mtt
#endif

# objects for targets
x11/xawtv: \
	x11/xawtv.o \
	x11/wmhooks.o \
	x11/atoms.o \
	x11/x11.o \
	x11/blit.o \
	x11/xt.o \
	x11/xv.o \
	x11/toolbox.o \
	x11/conf.o \
	x11/complete-xaw.o \
	x11/vbi-x11.o \
	x11/av-sync.o \
	jwz/remote.o \
	$(OBJS-common-vbi) \
	$(OBJS-common-input) \
	$(OBJS-common-capture)

x11/motv: \
	x11/motv.o \
	x11/motif-gettext.o \
	x11/man.o \
	x11/icons.o \
	x11/wmhooks.o \
	x11/atoms.o \
	x11/x11.o \
	x11/blit.o \
	x11/xt.o \
	x11/xv.o \
	x11/complete-motif.o \
	x11/vbi-x11.o \
	x11/av-sync.o \
	jwz/remote.o \
	common/RegEdit.o \
	$(OBJS-common-vbi) \
	$(OBJS-common-input) \
	$(OBJS-common-capture)

x11/mtt: \
	x11/mtt.o \
	x11/motif-gettext.o \
	x11/icons.o \
	x11/atoms.o \
	x11/vbi-x11.o \
	x11/vbi-gui.o \
	console/vbi-tty.o \
	common/RegEdit.o \
	$(OBJS-common-vbi) \
	$(OBJS-common-capture)

ifeq ($(FOUND_DVB),yes)
x11/mtt: x11/xt-dvb.o
endif

x11/v4lctl: \
	x11/v4lctl.o \
	x11/atoms.o \
	x11/xv.o \
	$(OBJS-common-capture)

x11/rootv: \
	x11/rootv.o \
	x11/atoms.o \
	common/parseconfig.o

x11/pia: \
	x11/pia.o \
	x11/atoms.o \
	x11/av-sync.o \
	x11/blit.o \
	$(OBJS-common-capture)

x11/xawtv-remote: x11/xawtv-remote.o
x11/propwatch:    x11/propwatch.o

# libraries to link
x11/xawtv        : LDLIBS  += \
	$(THREAD_LIBS) $(LIRC_LIBS) \
	$(ATHENA_LIBS) $(VBI_LIBS) $(GL_LIBS) -ljpeg -lm $(DLFLAGS)
x11/motv         : LDLIBS  += \
	$(THREAD_LIBS) $(LIRC_LIBS) \
	$(MOTIF_LIBS) $(VBI_LIBS) $(GL_LIBS) -ljpeg -lm $(DLFLAGS)
x11/pia          : LDLIBS  += \
	$(THREAD_LIBS) $(ATHENA_LIBS) $(GL_LIBS) -ljpeg -lm $(DLFLAGS)
x11/mtt          : LDLIBS  += $(THREAD_LIBS) $(MOTIF_LIBS) $(VBI_LIBS) -ljpeg $(DLFLAGS)
x11/v4lctl       : LDLIBS  += $(THREAD_LIBS) $(ICONV_LIBS) $(ATHENA_LIBS) -ljpeg -lm $(DLFLAGS)
x11/rootv        : LDLIBS  += $(ATHENA_LIBS) $(DLFLAGS)
x11/xawtv-remote : LDLIBS  += $(ATHENA_LIBS) $(DLFLAGS)
x11/propwatch    : LDLIBS  += $(ATHENA_LIBS) $(DLFLAGS)

# compile flags
x11/complete-xaw.o   : CFLAGS += -DATHENA=1
x11/complete-motif.o : CFLAGS += -DMOTIF=1


# local targets
x11/complete-xaw.o: x11/complete.c
	@$(echo_compile_c)
	@$(compile_c)
	@$(fixup_deps)

x11/complete-motif.o: x11/complete.c
	@$(echo_compile_c)
	@$(compile_c)
	@$(fixup_deps)


# global targets
all:: $(TARGETS-x11)
ifeq ($(FOUND_MOTIF),yes)
all:: $(MOTV-app)
endif

ifeq ($(FOUND_X11),yes)
install::
	$(INSTALL_PROGRAM) -s $(TARGETS-x11) $(bindir)
#	$(INSTALL_DIR) $(resdir)/app-defaults
#	$(INSTALL_DATA) $(srcdir)/x11/Xawtv4.ad $(resdir)/app-defaults/Xawtv4
endif
#ifeq ($(FOUND_MOTIF),yes)
#install::
#	$(INSTALL_DATA) $(srcdir)/x11/mtt4.ad $(resdir)/app-defaults/mtt4
#	$(INSTALL_DATA) $(srcdir)/x11/MoTV4.ad $(resdir)/app-defaults/MoTV4
#endif

distclean::
	rm -f $(TARGETS-x11)
	rm -f x11/MoTV4.h x11/Xawtv4.h x11/mtt4.h

# special dependences / rules
x11/xawtv.o: x11/Xawtv4.h
x11/motv.o: x11/MoTV4.h
x11/mtt.o: x11/mtt4.h
