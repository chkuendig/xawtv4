dnl ---------------------------------------------------------------------
dnl Process this file with autoconf to produce a configure script.

AC_INIT(xawtv.spec.in)
AC_CONFIG_HEADER(config.h)


dnl ---------------------------------------------------------------------
dnl compile time options

AC_ARG_ENABLE(xfree-ext,
  [  --enable-xfree-ext      use XFree extensions (VidMode,DPMS)])
AC_ARG_ENABLE(xvideo,
  [  --enable-xvideo         use the Xvideo extension])
AC_ARG_ENABLE(lirc,
  [  --enable-lirc           enable lirc support (infrared remote control)])
AC_ARG_ENABLE(quicktime,
  [  --enable-quicktime      enable quicktime support (needs libquicktime)])
AC_ARG_ENABLE(motif,
  [  --enable-motif          enable motif support (needs openmotif)])
AC_ARG_ENABLE(aa,
  [  --enable-aa             enable aalib support])
AC_ARG_ENABLE(alsa,
  [  --enable-alsa           enable alsa support])
AC_ARG_ENABLE(arts,
  [  --enable-arts           enable aRts support (KDE+GNOME sound daemon)])
AC_ARG_ENABLE(mad,
  [  --enable-mad            enable mad support (mpeg audio)])
AC_ARG_ENABLE(mad,
  [  --enable-mpeg2          enable libmpeg2 support (mpeg video)])
AC_ARG_ENABLE(zvbi,
  [  --enable-zvbi           enable vbi support (needs libzvbi)])
AC_ARG_ENABLE(dvb,
  [  --enable-dvb            enable dvb support])
AC_ARG_ENABLE(gl,
  [  --enable-gl             enable OpenGL support])
AC_ARG_ENABLE(dv,
  [  --enable-dv             enable dvlib support])
AC_ARG_ENABLE(mmx,
  [  --enable-mmx            enable mmx support])
AC_ARG_ENABLE(xft,
  [  --enable-xft            enable xft support])

# some options are off by default
test "$enable_arts" = ""   && enable_arts="no"	# incomplete -- no sane select(2) support
test "$enable_aa" = ""     && enable_aa="no"	# doesn't build

dnl ---------------------------------------------------------------------
dnl Checks for programs.

AC_PROG_CC
AC_PROG_CXX
AC_PROG_CPP

AC_PROG_INSTALL
AC_CHECK_PROGS(DEPEND,gccmakedep makedepend,true)

dnl ---------------------------------------------------------------------
dnl do some OS specific stuff here

AC_SUBST(FOUND_AALIB)
AC_SUBST(FOUND_ALSA)
AC_SUBST(FOUND_ARTS)
AC_SUBST(FOUND_DV)
AC_SUBST(FOUND_GTK)
AC_SUBST(FOUND_LQT)
AC_SUBST(FOUND_MAD)
AC_SUBST(FOUND_MPEG2)
AC_SUBST(FOUND_MOTIF)
AC_SUBST(FOUND_OS)
AC_SUBST(FOUND_X11)
AC_SUBST(FOUND_GL)
AC_SUBST(FOUND_ZVBI)
AC_SUBST(FOUND_DVB)
FOUND_AALIB="no"
FOUND_ALSA="no"
FOUND_ARTS="no"
FOUND_DV="no"
FOUND_GTK="no"
FOUND_LQT="no"
FOUND_MAD="no"
FOUND_MPEG2="no"
FOUND_MOTIF="no"
FOUND_OS="unknown"
FOUND_X11="no"
FOUND_GL="no"
FOUND_ZVBI="no"
FOUND_DVB="no"

case "`uname -s`" in
	Linux)
		FOUND_OS="linux"
		;;
	OpenBSD | FreeBSD | NetBSD)
		# *BSD has important stuff (from ports)
		# in /usr/local ...
		CFLAGS="$CFLAGS -I/usr/local/include -L/usr/local/lib"
		LDFLAGS="$LDFLAGS -L/usr/local/lib"
		FOUND_OS="bsd"
		;;
	*)
		AC_MSG_CHECKING(if xawtv will build on `uname -s`)
		AC_MSG_RESULT(maybe)
		FOUND_OS="unknown"
		;;
esac


dnl ---------------------------------------------------------------------
dnl Checks for functions

AC_EGREP_HEADER(sockaddr_storage,sys/socket.h,AC_DEFINE(HAVE_SOCKADDR_STORAGE,1,"have ipv6"))
AC_CHECK_HEADERS(getopt.h soundcard.h unistd.h sys/select.h sys/soundcard.h alsa/asoundlib.h linux/joystick.h dev/bktr/ioctl_bt848.h dev/ic/bt8xx.h)

if test "$enable_dvb" != "no"; then
    AC_CHECK_HEADER(linux/dvb/dmx.h,
        AC_DEFINE(HAVE_DVB,1,"have dvb") FOUND_DVB="yes",,)
else
    echo "*** dvb disabled"
fi

AC_CHECK_FUNCS(ftello fseeko getpt getnameinfo getopt_long strcasestr memmem dlopen iconv_open)
AC_SUBST(DLFLAGS)
DLFLAGS=""
if test "$ac_cv_func_dlopen" = "no"; then
  AC_CHECK_LIB(dl,dlopen, [ DLFLAGS="-ldl" ] )
fi
AC_MSG_CHECKING(for ELF)
if test -z "`echo __ELF__ | $CC -E - | grep __ELF__`"; then
  AC_MSG_RESULT(yes)
  DLFLAGS="$DLFLAGS -Wl,-E"
else
  AC_MSG_RESULT(no)
fi

AC_SUBST(ICONV_LIBS)
ICONV_LIBS=""
if test "$ac_cv_func_iconv_open" = "no"; then
  AC_CHECK_LIB(iconv,iconv_open, [ ICONV_LIBS="-liconv" ] )
fi

AC_CHECK_LIB(pthread,pthread_create,LIBPTHREAD="-lpthread")
if test "$LIBPTHREAD" = ""; then
  AC_CHECK_LIB(c_r,pthread_create,LIBPTHREAD="-lc_r")
fi
AC_CHECK_LIB(ossaudio,main,LIBOSS="-lossaudio")
AC_CHECK_LIB(ncurses,initscr,LIBCURSES="-lncurses")
if test "$LIBCURSES" = ""; then
  AC_CHECK_LIB(curses,initscr,LIBCURSES="-lcurses")
fi
if test "$LIBCURSES" = ""; then
  echo "Oops: (n)curses library not found.  You need this one, please install."
  echo "Note: to compile stuff just the library packages are not enougth,"
  echo "      you need also the *-devel packages."
  exit 1
fi
AC_SUBST(LIBPTHREAD)
AC_SUBST(LIBOSS)
AC_SUBST(LIBCURSES)


dnl ---------------------------------------------------------------------
dnl X11 checks

AC_PATH_XTRA
if test "$no_x" != "yes"; then
  FOUND_X11="yes"
  ATHENA="-lXaw -lXmu -lXt $X_PRE_LIBS -lXpm -lXext -lX11 $X_EXTRA_LIBS"
  FSLIB="-lFS"
  XFT_FLAGS=""
  XFT_LIBS=""
  if test "$enable_xft" != "no"; then
    AC_MSG_CHECKING(for Xft)
    if test -x "`which pkg-config 2>/dev/null`" && pkg-config xft; then
      XFT_FLAGS="`pkg-config --cflags xft`"
      XFT_LIBS="`pkg-config --libs xft`"
      AC_DEFINE(HAVE_XFT,1,"have xft")
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no)
    fi
  fi
else
  ATHENA=""
  FSLIB=""
fi
AC_SUBST(X_CFLAGS)
AC_SUBST(X_PRE_LIBS)
AC_SUBST(X_EXTRA_LIBS)
AC_SUBST(X_LIBS)
AC_SUBST(x_includes)
AC_SUBST(x_libraries)
AC_SUBST(ATHENA)
AC_SUBST(FSLIB)
AC_SUBST(SUBS)
AC_SUBST(TOOLS)
AC_SUBST(LDLIBS)
AC_SUBST(LDFLAGS)
AC_SUBST(XFT_FLAGS)
AC_SUBST(XFT_LIBS)

AC_CHECK_LIB(jpeg, jpeg_start_compress, JPEG="found",,)
if test "$JPEG" != "found"; then
    echo "Oops:	jpeg library not found.  You need this one, please install."
    echo "Note:	to compile stuff just the library packages are not enougth,"
    echo "	you need also the *-devel packages."
    exit 1
fi

AC_SUBST(LIBALSA)
LIBALSA=""
if test "$enable_alsa" != "no" ; then
    AC_CHECK_LIB(asound, snd_seq_open,
	AC_DEFINE(HAVE_ALSA,1,"have alsa") FOUND_ALSA="yes"; LIBALSA="-lasound",,)
else
    echo "*** alsa disabled"
fi

AC_SUBST(ARTS_FLAGS)
AC_SUBST(ARTS_LIBS)
ARTS_FLAGS=""
ARTS_LIBS=""
if test "$enable_arts" != "no"; then
    AC_MSG_CHECKING(for aRts)
    if test -x "`which artsc-config 2>/dev/null`"; then
        FOUND_ARTS="yes"
        ARTS_FLAGS=`artsc-config --cflags`
        ARTS_LIBS=`artsc-config --libs`
      AC_MSG_RESULT(yes)
    else
      AC_MSG_RESULT(no)
    fi
else
    echo "*** arts disabled"
fi

AC_SUBST(LIBMAD)
LIBMAD=""
if test "$enable_mad" != "no"; then
    AC_CHECK_LIB(mad, mad_decoder_init,
	AC_DEFINE(HAVE_LIBMAD,1,"have mad") FOUND_MAD="yes"; LIBMAD="-lmad",,)
else
    echo "*** mad disabled"
fi

AC_SUBST(LIBMPEG2)
LIBMPEG2=""
if test "$enable_mpeg2" != "no"; then
    AC_CHECK_LIB(mpeg2, mpeg2_init,
	AC_DEFINE(HAVE_LIBMPEG2,1,"have libmpeg2") FOUND_MPEG2="yes"; LIBMPEG2="-lmpeg2",,)
else
    echo "*** libmpeg2 disabled"
fi

AC_SUBST(LIRC)
LIRC=""
if test "$enable_lirc" != "no"; then
    AC_CHECK_LIB(lirc_client, lirc_init,
	AC_DEFINE(HAVE_LIBLIRC_CLIENT,1,"have lirc") LIRC="-llirc_client",,)
else
    echo "*** lirc disabled"
fi

AC_SUBST(LIBZVBI)
LIBZVBI=""
if test "$enable_zvbi" != "no"; then
    AC_CHECK_LIB(zvbi, vbi_capture_fd,
        AC_DEFINE(HAVE_ZVBI,1,"have zvbi") FOUND_ZVBI="yes";
	LIBZVBI="-lzvbi -lm -lpng -lz",,$LIBPTHREAD)
else
    echo "*** zvbi disabled"
fi
if test "$FOUND_ZVBI" = "yes"; then
    AC_CHECK_LIB(zvbi, vbi_capture_proxy_new,
        AC_DEFINE(HAVE_VBI_PROXY,1,"proxy support in libzvbi"),,$LIBPTHREAD)
    AC_CHECK_LIB(zvbi, vbi_capture_dvb_new2,
        AC_DEFINE(HAVE_VBI_DVB2,1,"dvb2 supportin libzvbi"),,$LIBPTHREAD)
fi

AC_SUBST(AALIBS)
AALIBS=""
if test "$enable_aa" != "no"; then
    AC_CHECK_LIB(aa,aa_autoinit, [ FOUND_AALIB="yes" ],,)
    if test "$FOUND_AALIB" = "yes"; then
        AALIBS=-laa
        if test -x "`which aalib-config 2>/dev/null`"; then
            AALIBS=`aalib-config --libs`
        fi
    fi
else
    echo "*** aalib support disabled"
fi

AC_SUBST(LIBDV)
LIBDV=""
if test "$enable_dv" != "no"; then
    AC_CHECK_LIB(dv, dv_decoder_new,
        AC_DEFINE(HAVE_DV,1,"have dv") FOUND_DV="yes";
	LIBDV="-ldv -lm",,-lm $LIBPTHREAD)
else
    echo "*** DV disabled"
fi

GLIB_FLAGS="`pkg-config --cflags glib-2.0`"
AC_SUBST(GLIB_FLAGS)

quicktime_libs="$DLFLAGS $(pkg-config --libs glib) $LIBPTHREAD"
if test "$enable_quicktime" != "no"; then
    AC_CHECK_LIB(quicktime, lqt_query_registry,
	FOUND_LQT="yes"; AC_DEFINE(HAVE_LIBQUICKTIME,1,"have libquicktime"),,
	$quicktime_libs)
else
    echo "*** quicktime disabled"
fi
QTLIBS=""
if test "$FOUND_LQT" = "yes"; then
    QTLIBS="-lquicktime $quicktime_libs"
    QT_FLAGS="`pkg-config libquicktime --cflags-only-I`"
    if test "$QT_FLAGS" = ""; then
	QT_FLAGS= "-I/usr/include/quicktime"
    fi
fi
AC_SUBST(QTLIBS)
AC_SUBST(QT_FLAGS)

if test "$enable_xfree_ext" != "no"; then
    AC_CHECK_LIB(Xdpms, DPMSQueryExtension,,, 
	$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS)
    if test "$ac_cv_lib_Xdpms_DPMSQueryExtension" = "no"; then
	AC_CHECK_LIB(Xext, DPMSQueryExtension,AC_DEFINE(HAVE_LIBXDPMS),,
	    $X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS)
    fi
    AC_CHECK_LIB(Xinerama, XineramaQueryExtension,,,
	$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS)
    AC_CHECK_LIB(Xrender, XRenderQueryExtension,,,
	$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS)
else
    echo "*** XFree extensions disabled"
fi

if test "$enable_xvideo" != "no"; then
    AC_CHECK_LIB(Xv, XvQueryExtension,,,
	$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS)
else
    echo "*** Xvideo extension disabled"
fi

if test "$enable_motif" != "no"; then
    AC_CHECK_LIB(Xm,XmStringGenerate, [ FOUND_MOTIF="yes" ],,
	$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS)
else
    echo "*** motif support disabled"
fi

AC_SUBST(LIBGL)
LIBGL=""
if test "$enable_gl" != "no"; then
    AC_CHECK_LIB(GL, glXChooseVisual,
        AC_DEFINE(HAVE_GL,1,"have opengl") FOUND_GL="yes"; LIBGL="-lGL -lm",,
	$X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS -lm)
else
    echo "*** OpenGL disabled"
fi

pkgs="gtk+-x11-2.0 >= 2.4"
AC_MSG_CHECKING(for gtk+ 2.4)
pkg-config "$pkgs" >/dev/null 2>&1
if test "$?" = "0"; then
    AC_MSG_RESULT(yes)
    FOUND_GTK="yes"
    GTK_CFLAGS=$(pkg-config --cflags "$pkgs")
    GTK_LIBS=$(pkg-config --libs "$pkgs")
    AC_SUBST(GTK_CFLAGS)
    AC_SUBST(GTK_LIBS)
else
    AC_MSG_RESULT([$pkgs not found])
fi

dnl ---------------------------------------------------------------------
AC_MSG_CHECKING(for X11 config directory)
x11conf=/usr/X11R6/lib/X11
if test -d /etc/X11; then
	x11conf=/etc/X11
fi
AC_MSG_RESULT($x11conf)
AC_SUBST(x11conf)

AC_MSG_CHECKING(for X11 app-defaults directory)
resdir=/usr/X11R6/lib/X11
if test -d /etc/X11/app-defaults; then
	resdir=/etc/X11
fi
AC_MSG_RESULT($resdir/app-defaults)
AC_SUBST(resdir)

dnl ---------------------------------------------------------------------
dnl for screenlock
AC_DEFINE(HAVE_XMU,1,"have xmu")

dnl ---------------------------------------------------------------------
dnl deinterlace plugins
AC_MSG_CHECKING(if mmx should be used)
AC_SUBST(USE_MMX)
if test "$enable_mmx" = "no" -o "$enable_mmx" = "yes"; then
	USE_MMX="$enable_mmx"
else
	case "`uname -m`" in
		i586 | i686)
			USE_MMX="yes"
			;;
		*)
			USE_MMX="no"
			;;
	esac
fi
AC_MSG_RESULT($USE_MMX)

dnl ---------------------------------------------------------------------
dnl write out stuff
AC_OUTPUT(Makefile xawtv.spec)
for dir in \
	common console debug jwz vbistuff x11 gtk structs \
	libng libng/plugins libng/contrib-plugins mk
do
	test -d $dir && continue
	mkdir -p $dir
done

dnl ---------------------------------------------------------------------
dnl compile time options summary

cat <<EOF

compile time options summary
============================

    aalib        : $FOUND_AALIB
    alsa         : $FOUND_ALSA
    aRts         : $FOUND_ARTS
    dv           : $FOUND_DV
    QuickTime    : $FOUND_LQT
    mad          : $FOUND_MAD
    mpeg2        : $FOUND_MPEG2
    X11R6        : $FOUND_X11
    gtk+         : $FOUND_GTK
    OpenMotif    : $FOUND_MOTIF
    OpenGL       : $FOUND_GL
    zvbi         : $FOUND_ZVBI
    DVB          : $FOUND_DVB

EOF

test "$FOUND_ZVBI" = "no" && cat <<EOF
WARNING: building without zvbi support -- will not build scantv + alevtd
         zvbi is available from http://zapping.sf.net/
EOF

exit 0
