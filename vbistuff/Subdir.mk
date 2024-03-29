
# targets to build
TARGETS-vbistuff := \
	vbistuff/ntsc-cc
ifeq ($(FOUND_ZVBI),yes)
TARGETS-vbistuff += \
	vbistuff/alevtd
endif

HTML-alevtd  := \
	vbistuff/alevt.css.h \
	vbistuff/top.html.h \
	vbistuff/bottom.html.h \
	vbistuff/about.html.h

# objects for targets
vbistuff/alevtd: \
	vbistuff/alevtd.o \
	vbistuff/request.o \
	vbistuff/response.o \
	vbistuff/page.o \
	$(OBJS-common-vbi) \
	libng/devices.o

vbistuff/ntsc-cc: vbistuff/ntsc-cc.o

# libraries to link
vbistuff/alevtd  : LDLIBS  += $(VBI_LIBS) $(THREAD_LIBS)
vbistuff/ntsc-cc : LDLIBS  += $(VBI_LIBS) $(ATHENA_LIBS)

# global targets
all:: $(TARGETS-vbistuff)

install::
	$(INSTALL_PROGRAM) -s $(TARGETS-vbistuff) $(bindir)

clean::
	rm -f $(HTML-alevtd)

distclean::
	rm -f $(TARGETS-vbistuff)

# special dependences
vbistuff/alevtd.o: $(HTML-alevtd)
