
# targets to build
TARGETS-gtk := \
	gtk/pia

gtk/pia: \
	gtk/pia.o \
	gtk/av-sync.o \
	gtk/blit.o \
	common/parseconfig.o \
	$(OBJS-common-capture)

gtk/pia : CFLAGS  += $(GTK_CFLAGS)
gtk/pia : LDLIBS  += $(GTK_LIBS) $(GL_LIBS) -ljpeg -lm
gtk/pia : LDFLAGS += $(DLFLAGS)


ifeq ($(FOUND_GTK),yes)

all:: $(TARGETS-gtk)

install::
	$(INSTALL_PROGRAM) -s $(TARGETS-gtk) $(bindir)

endif
