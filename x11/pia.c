/*
 * simple movie player
 *
 *  (c) 2002,03 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mcheck.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xaw/Simple.h>
#include <X11/Xaw/Label.h>
#include <X11/extensions/XShm.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab-ng.h"
#include "blit.h"
#include "devs.h"
#include "dvb.h"
#include "parseconfig.h"
#include "commands.h"
#include "av-sync.h"

/* ------------------------------------------------------------------------ */

int debug = 0;
XtAppContext app_context;
Display *dpy;

/* X11 */
static Widget       app_shell;
static Colormap     colormap;
static Visual       *visual;
static XVisualInfo  vinfo,*vinfo_list;

static Widget simple;

/* ------------------------------------------------------------------------ */

struct ARGS {
    char  *dsp;
    int   slow;
    int   help;
    int   verbose;
    int   debug;
    int   xv;
    int   gl;
    int   xn;
    int   max;
    int   audio;
    int   video;

    int   dvb;
    int   adapter;
} args;

XtResource args_desc[] = {
    /* name, class, type, size, offset, default_type, default_addr */
    {
	/* Strings */
	"dsp",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,dsp),
	XtRString, NULL,
    },{
	/* Integer */
	"verbose",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,verbose),
	XtRString, "0"
    },{
	"debug",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,debug),
	XtRString, "0"
    },{
	"help",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,help),
	XtRString, "0"
    },{
	"slow",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,slow),
	XtRString, "1"
    },{
	"xv",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,xv),
	XtRString, "1"
    },{
	"gl",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,gl),
	XtRString, "1"
    },{
	"xn",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,xn),
	XtRString, "1"
    },{
	"max",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,max),
	XtRString, "0"
    },{
	"audio",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,audio),
	XtRString, "1"
    },{
	"video",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,video),
	XtRString, "1"
    },{
	"dvb",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,dvb),
	XtRString, "0"
    },{
	"adapter",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,adapter),
	XtRString, "0"
    }
};
const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-dsp",        "dsp",         XrmoptionSepArg, NULL },
    { "-slow",       "slow",        XrmoptionSepArg, NULL },
    { "-fast",       "slow",        XrmoptionNoArg,  "0"  },

    { "-d",          "debug",       XrmoptionSepArg, NULL },
    { "-debug",      "debug",       XrmoptionNoArg,  "1"  },
    { "-v",          "verbose",     XrmoptionNoArg,  "1"  },
    { "-verbose",    "verbose",     XrmoptionNoArg,  "1"  },

    { "-h",          "help",        XrmoptionNoArg,  "1"  },
    { "-help",       "help",        XrmoptionNoArg,  "1"  },
    { "--help",      "help",        XrmoptionNoArg,  "1"  },

    { "-max",        "max",         XrmoptionNoArg,  "1"  },
    { "-1",          "xn",          XrmoptionNoArg,  "1"  },
    { "-2",          "xn",          XrmoptionNoArg,  "2"  },
    { "-3",          "xn",          XrmoptionNoArg,  "3"  },
    { "-4",          "xn",          XrmoptionNoArg,  "4"  },

    { "-xv",         "xv",          XrmoptionNoArg,  "1"  },
    { "-noxv",       "xv",          XrmoptionNoArg,  "0"  },
    { "-gl",         "gl",          XrmoptionNoArg,  "1"  },
    { "-nogl",       "gl",          XrmoptionNoArg,  "0"  },
    { "-audio",      "audio",       XrmoptionNoArg,  "1"  },
    { "-noaudio",    "audio",       XrmoptionNoArg,  "0"  },
    { "-video",      "video",       XrmoptionNoArg,  "1"  },
    { "-novideo",    "video",       XrmoptionNoArg,  "0"  },

    { "-dvb",        "dvb",         XrmoptionNoArg,  "1"  },
    { "-a",          "adapter",     XrmoptionSepArg, NULL },
    { "-adapter",    "adapter",     XrmoptionSepArg, NULL },
};
const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));

/* ------------------------------------------------------------------------ */

static void quit_ac(Widget widget, XEvent *event,
		    String *params, Cardinal *num_params)
{
    command_pending++;
    exit_application++;
}

static void next_ac(Widget widget, XEvent *event,
		    String *params, Cardinal *num_params)
{
    command_pending++;
}

static XtActionsRec action_table[] = {
    { "Quit",  quit_ac },
    { "Next",  next_ac },
};

static String res[] = {
    "pia.winGravity:		 Static",
    "pia.allowShellResize:	 true",
    "pia.playback.translations:  #override \\n"
    "       <Key>space:          Next()    \\n"
    "       Ctrl<Key>C:          Quit()    \\n"
    "       <Key>Q:              Quit()    \\n"
    "       <Key>Escape:         Quit()",
    "pia.playback.background:    black",
    "pia.playback.foreground:    gray80",
    "pia.playback.font:          -*-*-medium-r-normal-*-16-*-*-*-*-*-*-*",
    "pia.playback.fontSet:       -*-*-medium-r-normal-*-16-*-*-*-*-*-*-*,*",
    "pia.playback.label:         no video available (yet?)",
    NULL
};

static void usage(FILE *out, char *prog)
{
    char *h;

    if (NULL != (h = strrchr(prog,'/')))
	prog = h+1;
    fprintf(out,
	    "%s is simple movie player\n"
	    "\n"
	    "usage: %s [ options ] files\n"
	    "options:\n"
	    "  -h, -help       this text\n"
	    "  -v, -verbose    be verbose\n"
	    "      -debug      enable debug messages\n"
	    "      -dsp <dev>  use sound device <dev>\n"
#ifdef HAVE_LIBXV
	    "      -(no)xv     enable/disable Xvideo extention\n"
#endif
#ifdef HAVE_GL
	    "      -(no)gl     enable/disable OpenGL\n"
#endif
	    "      -(no)audio  enable/disable audio playback\n"
	    "      -(no)video  enable/disable video playback\n"
	    "      -2/-3/-4    scale up video size\n"
	    "      -max        maximize video size\n"
	    "\n",
	    prog,prog);
}

/* ------------------------------------------------------------------------ */

static int ww, wh;
static int manually_resized;

static void
resize_eh(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    switch(event->type) {
    case ConfigureNotify:
	if (event->xconfigure.width  != ww ||
	    event->xconfigure.height != wh) {
	    manually_resized = 1;
	    if (args.debug)
		fprintf(stderr,"window manually resized (or moved) to %dx%d\n",
			event->xconfigure.width, event->xconfigure.height);
	}
	break;
    }
}

static void window_setup(struct media_stream *mm,
			 struct ng_video_fmt *fmt)
{
    static int first = 1;
    XEvent event;

    /* calulate window size */
    ww = 320;
    wh = 32;
    if (fmt) {
	ww = fmt->width  * args.xn;
	wh = fmt->height * args.xn;
	if (args.max) {
	    int sw = XtScreen(app_shell)->width;
	    int sh = XtScreen(app_shell)->height;
	    if (sw * wh > sh * ww) {
		ww = ww * sh / wh;
		wh = sh;
	    } else {
		wh = wh * sw / ww;
		ww = sw;
	    }
	}
    }

    if (first) {
	/* init window */
	simple = XtVaCreateManagedWidget("playback",labelWidgetClass,app_shell,
					 XtNwidth,ww, XtNheight,wh, NULL);
	XtAddEventHandler(app_shell, StructureNotifyMask,
			  True, resize_eh, NULL);
	XtRealizeWidget(app_shell);
	while (0 == XtWindow(simple)) {
	    XFlush(dpy);
	    while (True == XCheckMaskEvent(dpy, ~0, &event))
		XtDispatchEvent(&event);
	}
	first = 0;
    } else {
	/* window present -- resize */
	if (!manually_resized)
	    XtVaSetValues(simple, XtNwidth,ww, XtNheight,wh, NULL);
    }

    if (NULL == mm->blit)
	mm->blit = blit_init(simple,&vinfo,args.xv,args.gl);
}

static void play_file(char *filename)
{
    struct media_stream mm;
    struct ng_audio_fmt *afmt;
    struct ng_video_fmt *vfmt;
    struct dvb_state *dvb = NULL;

    memset(&mm,0,sizeof(mm));
    
    /* set title */
    XtVaSetValues(app_shell, XtNtitle, filename, NULL);

    /* open file */
    if (args.dvb) {
	/* dvb hack */
	mm.reader = ng_find_reader_name("mpeg-ts");
	if (NULL == mm.reader) {
	    fprintf(stderr,"can't handle dvb\n");
	    exit(1);
	}
	dvb = dvb_init(0,0,0);
	if (dvb) {
	    if (-1 == dvb_tune(dvb,filename)) {
		fprintf(stderr,"tuning failed\n");
		exit(1);
	    }
	}
	filename = "/dev/dvb/adapter0/dvr0";
    } else {
	/* file playback */
	mm.reader = ng_find_reader_magic(filename);
	if (NULL == mm.reader) {
	    fprintf(stderr,"can't handle %s\n",filename);
	    goto done;
	}
    }

    /* init file/stream parser */
    mm.rhandle = mm.reader->rd_open(filename);
    if (NULL == mm.rhandle) {
	fprintf(stderr,"opening %s failed\n",filename);
	goto done;
    }
    vfmt = mm.reader->rd_vfmt(mm.rhandle,NULL,0);
    if (vfmt && (0 == vfmt->width || 0 == vfmt->height))
	vfmt = NULL;
    afmt = mm.reader->rd_afmt(mm.rhandle);

    if (args.verbose) {
	/* print some info about the movie file */
	fprintf(stderr,"playing \"%s\"\n",filename);
	fprintf(stderr,"movie file is \"%s\"\n",mm.reader->desc);
	if (afmt)
	    fprintf(stderr,"audio stream: %s @ %d Hz\n",
		    ng_afmt_to_desc[afmt->fmtid],afmt->rate);
	else
	    fprintf(stderr,"audio stream: unknown format or not present\n");
	if (vfmt)
	    fprintf(stderr,"video stream: %s @ %dx%d\n",
		    ng_vfmt_to_desc[vfmt->fmtid], vfmt->width,vfmt->height);
	else
	    fprintf(stderr,"video stream: unknown format or not present\n");
    }

    if (0 == args.video)
	vfmt = NULL;
    if (0 == args.audio)
	afmt = NULL;
    mm.speed = args.slow;
    if (1 != mm.speed)
	/* no audio, will not sync anyway ... */
	afmt = NULL;

    /* video setup */
    window_setup(&mm,vfmt);
    if (vfmt)
	av_media_reader_video(&mm);

    /* audio setup */
    if (afmt)
	av_media_reader_audio(&mm,afmt);

    /* go playback stuff */
    av_media_mainloop(&mm);

    /* cleanup */
    BUG_ON(NULL != mm.as,"mm.as isn't NULL");
    BUG_ON(NULL != mm.vs,"mm.vs isn't NULL");
    mm.reader->rd_close(mm.rhandle);
    if (mm.blit)
	blit_fini(mm.blit);
    if (command_pending)
	command_pending--;

 done:
    if (args.verbose)
	fprintf(stderr,"\n");
    return;
}

int main(int argc, char *argv[])
{
    int n,i;

    app_shell = XtVaAppInitialize(&app_context, "pia",
				  opt_desc, opt_count,
				  &argc, argv,
				  res, NULL);
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    if (args.help) {
	usage(stdout,argv[0]);
	exit(1);
    }
    if (args.debug) {
	debug    = args.debug;
	ng_debug = args.debug;
    }

    if (argc < 2) {
	usage(stderr,argv[0]);
	exit(1);
    }
    ng_init();
    ng_dsp_init(args.dsp, &devs.sndplay, 0);

    /* init x11 stuff */
    dpy = XtDisplay(app_shell);
    visual = x11_find_visual(XtDisplay(app_shell));
    vinfo.visualid = XVisualIDFromVisual(visual);
    vinfo_list = XGetVisualInfo(dpy, VisualIDMask, &vinfo, &n);
    vinfo = vinfo_list[0];
    XFree(vinfo_list);
    if (visual != DefaultVisualOfScreen(XtScreen(app_shell))) {
	fprintf(stderr,"switching visual (0x%lx)\n",vinfo.visualid);
	colormap = XCreateColormap(dpy,RootWindowOfScreen(XtScreen(app_shell)),
				   vinfo.visual,AllocNone);
	XtDestroyWidget(app_shell);
	app_shell = XtVaAppCreateShell("pia","pia",
				       applicationShellWidgetClass, dpy,
				       XtNvisual,vinfo.visual,
				       XtNcolormap,colormap,
				       XtNdepth, vinfo.depth,
				       NULL);
    } else {
	colormap = DefaultColormapOfScreen(XtScreen(app_shell));
    }
    x11_init_visual(XtDisplay(app_shell),&vinfo);
    XtAppAddActions(app_context,action_table,
		    sizeof(action_table)/sizeof(XtActionsRec));

    /* play files */
    if (args.dvb) {
	play_file(argv[1]);
    } else {
	for (i = 1; i < argc; i++) {
	    play_file(argv[i]);
	    if (exit_application)
		break;
	}
    }

    /* cleanup */
    ng_dev_fini(&devs.sndplay);
    return 0;
}
