/*
 * common X11 stuff (mostly libXt level) moved here from main.c
 *
 *   (c) 1997-2003 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>

#if defined(__linux__)
# include <sys/ioctl.h>
# include "videodev.h"
#endif

#include "config.h"

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XShm.h>

#include "grab-ng.h"
#include "commands.h"
#include "devs.h"
#include "sound.h"
#include "toolbox.h"
#include "xv.h"
#include "atoms.h"
#include "xt.h"
#include "x11.h"
#include "wmhooks.h"
#include "tv-config.h"
#include "capture.h"
#include "lirc.h"
#include "joystick.h"
#include "vbi-data.h"
#include "blit.h"
#include "parseconfig.h"
#include "tuning.h"
#include "event.h"
#include "av-sync.h"
#include "dvb.h"

/* jwz */
#include "remote.h"

/*----------------------------------------------------------------------*/

XtAppContext      app_context;
Widget            app_shell, tv;
Widget            on_shell;
Display           *dpy;
int               stay_on_top = 0;

XVisualInfo       vinfo;
Colormap          colormap;

int               have_dga   = 0;
int               have_vm    = 0;
int               have_randr = 0;
int               fs = 0;

void              *movie_state;
int               movie_blit;

XtIntervalId      zap_timer,scan_timer;

#ifdef HAVE_LIBXXF86VM
int               vm_count;
XF86VidModeModeInfo **vm_modelines;
XF86VidModeModeLine vm_line;
unsigned int        vm_dot;
#endif
#ifdef HAVE_LIBXINERAMA
XineramaScreenInfo *xinerama;
int                nxinerama;
#endif
#ifdef HAVE_LIBXRANDR
XRRScreenSize      *randr;
int                nrandr;
int                randr_evbase;
#endif

static Widget on_label;
static XtIntervalId title_timer, on_timer;
static char default_title[256] = "???";
static char *pick_device_new = NULL;

#if 0
static int zap_start,zap_fast;
#endif

/*--- args ----------------------------------------------------------------*/

struct ARGS args;

XtResource args_desc[] = {
    /* name, class, type, size, offset, default_type, default_addr */
    {
	/* Strings */
	"driver",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,driver),
	XtRString, NULL
    },{
	"joydev",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,joydev),
	XtRString, NULL
    },{
	"basename",
	XtCString, XtRString, sizeof(char*),
	XtOffset(struct ARGS*,basename),
	XtRString, "snap"
    },{
	/* Integer */
	"debug",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,debug),
	XtRString, "0"
    },{
	"bpp",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,bpp),
	XtRString, "0"
    },{
	"shift",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,shift),
	XtRString, "0"
    },{
	"xvport",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,xv_port),
	XtRString, "0"
    },{
	"parallel",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,parallel),
	XtRString, "1"
    },{
	"bufcount",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(struct ARGS*,bufcount),
	XtRString, "16"
    },{
	/* Boolean */
	"remote",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,remote),
	XtRString, "0"
    },{
	"readconfig",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,readconfig),
	XtRString, "1"
    },{
	"fullscreen",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,fullscreen),
	XtRString, "0"
    },{
	"fbdev",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,fbdev),
	XtRString, "0"
    },{
	"help",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,help),
	XtRString, "0"
    },{
	"hwscan",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,hwscan),
	XtRString, "0"
    },{
	"printver",
	XtCBoolean, XtRBoolean, sizeof(int),
	XtOffset(struct ARGS*,printver),
	XtRString, "0"
    }
};

const int args_count = XtNumber(args_desc);

XrmOptionDescRec opt_desc[] = {
    { "-c",          "device",      XrmoptionSepArg, NULL },
    { "-device",     "device",      XrmoptionSepArg, NULL },
    { "-driver",     "driver",      XrmoptionSepArg, NULL },
    { "-C",          "dspdev",      XrmoptionSepArg, NULL },
    { "-dspdev",     "dspdev",      XrmoptionSepArg, NULL },
    { "-vbidev",     "vbidev",      XrmoptionSepArg, NULL },
    { "-joydev",     "joydev",      XrmoptionSepArg, NULL },
    { "-o",          "basename",    XrmoptionSepArg, NULL },
    { "-outfile",    "basename",    XrmoptionSepArg, NULL },
    
    { "-v",          "debug",       XrmoptionSepArg, NULL },
    { "-debug",      "debug",       XrmoptionSepArg, NULL },
    { "-b",          "bpp",         XrmoptionSepArg, NULL },
    { "-bpp",        "bpp",         XrmoptionSepArg, NULL },
    { "-shift",      "shift",       XrmoptionSepArg, NULL },
    { "-xvport",     "xvport",      XrmoptionSepArg, NULL },
    { "-parallel",   "parallel",    XrmoptionSepArg, NULL },
    { "-bufcount",   "bufcount",    XrmoptionSepArg, NULL },
    
    { "-remote",     "remote",      XrmoptionNoArg,  "1" },
    { "-n",          "readconfig",  XrmoptionNoArg,  "0" },
    { "-noconf",     "readconfig",  XrmoptionNoArg,  "0" },
    { "-f",          "fullscreen",  XrmoptionNoArg,  "1" },
    { "-fullscreen", "fullscreen",  XrmoptionNoArg,  "1" },
    { "-hwscan",     "hwscan",      XrmoptionNoArg,  "1" },
    { "-fb",         "fbdev",       XrmoptionNoArg,  "1" },
    { "-version",    "printver",    XrmoptionNoArg,  "1" },
    
    { "-xv",         "xv",          XrmoptionNoArg,  "1" },
    { "-noxv",       "xv",          XrmoptionNoArg,  "0" },
    { "-xv-video",   "xvVideo",     XrmoptionNoArg,  "1" },
    { "-noxv-video", "xvVideo",     XrmoptionNoArg,  "0" },
    { "-xv-image",   "xvImage",     XrmoptionNoArg,  "1" },
    { "-noxv-image", "xvImage",     XrmoptionNoArg,  "0" },
    { "-gl",         "gl",          XrmoptionNoArg,  "1" },
    { "-nogl",       "gl",          XrmoptionNoArg,  "0" },

    { "-vm",         "vidmode",     XrmoptionNoArg,  "1" },
    { "-novm",       "vidmode",     XrmoptionNoArg,  "0" },
    { "-dga",        "dga",         XrmoptionNoArg,  "1" },
    { "-nodga",      "dga",         XrmoptionNoArg,  "0" },
    { "-randr",      "randr",       XrmoptionNoArg,  "1" },
    { "-norandr",    "randr",       XrmoptionNoArg,  "0" },
    
    { "-h",          "help",        XrmoptionNoArg,  "1" },
    { "-help",       "help",        XrmoptionNoArg,  "1" },
    { "--help",      "help",        XrmoptionNoArg,  "1" },
};

const int opt_count = (sizeof(opt_desc)/sizeof(XrmOptionDescRec));
/*----------------------------------------------------------------------*/

void
ExitCB(Widget widget, XtPointer client_data, XtPointer calldata)
{
    command_pending++;
    exit_application++;
}

void
do_exit(void)
{
    ExitCB(NULL,NULL,NULL);
}

void
CloseMainAction(Widget widget, XEvent *event,
		String *params, Cardinal *num_params)
{
    if (NULL != event && event->type == ClientMessage) {
	if (debug)
	    fprintf(stderr,"CloseMainAction: received %s message\n",
		    XGetAtomName(dpy,event->xclient.data.l[0]));
	if ((Atom)event->xclient.data.l[0] == WM_DELETE_WINDOW) {
	    /* fall throuth -- popdown window */
	} else {
	    /* whats this ?? */
	    return;
	}
    }
    ExitCB(widget,NULL,NULL);
}

void
RemoteAction(Widget widget, XEvent * event,
	     String * params, Cardinal * num_params)
{
    Atom            type;
    int             format, argc;
    unsigned int    i;
    char            *argv[32];
    unsigned long   nitems, bytesafter;
    unsigned char   *args = NULL;

    if (event->type == PropertyNotify) {
	if (debug > 1)
	    fprintf(stderr,"PropertyNotify %s\n",
		    XGetAtomName(dpy,event->xproperty.atom));
	if (event->xproperty.atom == _XAWTV_REMOTE &&
	    Success == XGetWindowProperty(dpy,
					  event->xproperty.window,
					  event->xproperty.atom,
					  0, (65536 / sizeof(long)),
					  True, XA_STRING,
					  &type, &format, &nitems, &bytesafter,
					  &args) &&
	    nitems != 0) {
	    for (i = 0, argc = 0; i <= nitems; i += strlen(args + i) + 1) {
		if (i == nitems || args[i] == '\0') {
		    argv[argc] = NULL;
		    do_command(argc,argv);
		    argc = 0;
	        } else {
		    argv[argc++] = args+i;
	        }
	    }
	    XFree(args);
	}
    }
}

#if 0
static void
zap_timeout(XtPointer client_data, XtIntervalId *id)
{
    static int muted = 0;

    if (zap_fast && !cur_attrs[ATTR_ID_MUTE]) {
	/* mute for fast channel scan */
	muted = 1;
	do_va_cmd(2,"volume","mute","on");
    }
    /* pixit(); */
    do_va_cmd(2,"setstation","next");
    if (cur_sender != zap_start) {
	zap_timer = XtAppAddTimeOut
	    (app_context, zap_fast ? CAP_TIME : ZAP_TIME, zap_timeout,NULL);
    } else {
	if(muted) {
	    /* unmute */
	    muted = 0;
	    do_va_cmd(2,"volume","mute","off");
	}
    }
}
#endif

void
ZapAction(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
#if 0
    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
	strcpy(title,"channel hopping off");
	set_timer_title();
    } else {
	zap_start = (cur_sender == -1) ? 0 : cur_sender;
	zap_fast = 0;
	if (*num_params > 0) {
	    if (0 == strcasecmp(params[0],"fast"))
		zap_fast = 1;
	}
	if (count)
	    zap_timer = XtAppAddTimeOut
		(app_context, CAP_TIME, zap_timeout,NULL);
    }
#endif
}

static void
scan_timeout(XtPointer client_data, XtIntervalId *id)
{
    scan_timer = 0;
    
    /* check */
    if (!(devs.video.flags & CAN_TUNE))
	return;
    if (devs.video.v->is_tuned(devs.video.handle))
	return;

    do_va_cmd(2,"setchannel","next");
    scan_timer = XtAppAddTimeOut
	(app_context, SCAN_TIME, scan_timeout, NULL);
}

void
ScanAction(Widget widget, XEvent *event,
	   String *params, Cardinal *num_params)
{
    do_va_cmd(2,"setchannel","next");
    scan_timer = XtAppAddTimeOut
	(app_context, SCAN_TIME, scan_timeout,NULL);
}

void
RatioAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    int w,h;
    
    if (2 != *num_params)
	return;
    w = atoi(params[0]);
    h = atoi(params[1]);
    ng_ratio_x = w;
    ng_ratio_y = h;
    do_va_cmd(2,"capture","off");
    do_va_cmd(2,"capture","on");
}

/*--- onscreen display (fullscreen) --------------------------------------*/

void
create_onscreen(WidgetClass class)
{
    on_shell = XtVaCreateWidget("onscreen",transientShellWidgetClass,
				app_shell,
				XtNoverrideRedirect,True,
				XtNvisual,vinfo.visual,
				XtNcolormap,colormap,
				XtNdepth,vinfo.depth,
				NULL);
    on_label = XtVaCreateManagedWidget("label", class, on_shell,
				       NULL);
}

static void
popdown_onscreen(XtPointer client_data, XtIntervalId *id)
{
    if (debug)
	fprintf(stderr,"osd: hide\n");
    XtPopdown(on_shell);
    on_timer = 0;
}

static void
display_onscreen(char *title)
{
    static int first = 1;
    Dimension x,y;

    if (!fs)
	return;
    if (!cfg_get_bool("options","global","osd",1))
	return;

    if (debug)
	fprintf(stderr,"osd: show (%s)\n",title);
    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,NULL);
    XtVaSetValues(on_shell,
		  XtNx, x + cfg_get_int("options","global","osd-x",30),
		  XtNy, y + cfg_get_int("options","global","osd-y",20),
		  NULL);
    toolkit_set_label(on_label,title);
    XtPopup(on_shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(on_shell),1);
    if (on_timer)
	XtRemoveTimeOut(on_timer);
    on_timer = XtAppAddTimeOut
	(app_context, ONSCREEN_TIME, popdown_onscreen,NULL);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(on_shell), no_ptr);
	XDefineCursor(dpy, XtWindow(on_label), no_ptr);
    }
}

/*----------------------------------------------------------------------*/

Boolean
rec_work(XtPointer client_data)
{
    struct ng_video_buf *buf;

    if (movie_blit) {
	buf = NULL;
	movie_grab_put_video(movie_state, &buf);
#if 0 /* FIXME */
	if (buf)
	    video_gd_blitframe(&vh,buf);
#endif
    } else {
	movie_grab_put_video(movie_state, NULL);
    }
    return False;
}

void
exec_done(int signal)
{
    int pid,stat;

    if (debug)
	fprintf(stderr,"got sigchild\n");
    pid = waitpid(-1,&stat,WUNTRACED|WNOHANG);
    if (debug) {
	if (-1 == pid) {
	    perror("waitpid");
	} else if (0 == pid) {
	    fprintf(stderr,"oops: got sigchild and waitpid returns 0 ???\n");
	} else if (WIFEXITED(stat)){
	    fprintf(stderr,"[%d]: normal exit (%d)\n",pid,WEXITSTATUS(stat));
	} else if (WIFSIGNALED(stat)){
	    fprintf(stderr,"[%d]: %s\n",pid,strsignal(WTERMSIG(stat)));
	} else if (WIFSTOPPED(stat)){
	    fprintf(stderr,"[%d]: %s\n",pid,strsignal(WSTOPSIG(stat)));
	}
    }
}

static void
exec_output(XtPointer data, int *fd, XtInputId * iproc)
{
    char buffer[81];
    int len;

    switch (len = read(*fd,buffer,80)) {
    case -1: /* error */
	perror("read pipe");
	/* fall */
    case 0:  /* EOF */
	close(*fd);
	XtRemoveInput(*iproc);
	break;
    default: /* got some bytes */
	buffer[len] = 0;
	fprintf(stderr,"%s",buffer);
	break;
    }
}

int
exec_x11(char **argv)
{
    int p[2],pid,i;

    if (debug) {
	fprintf(stderr,"exec: \"%s\"",argv[0]);
	for (i = 1; argv[i] != NULL; i++)
	    fprintf(stderr,", \"%s\"",argv[i]);
	fprintf(stderr,"\n");
    }
    pipe(p);
    switch (pid = fork()) {
    case -1:
	perror("fork");
	return -1;
    case 0:
	/* child */
	dup2(p[1],1);
	dup2(p[1],2);
	close(p[0]);
	close(p[1]);
	close(ConnectionNumber(dpy));
	execvp(argv[0],argv);
	fprintf(stderr,"exec %s: %s\n",argv[0],strerror(errno));
	exit(1);
    default:
	/* parent */
	close(p[1]);
	XtAppAddInput(app_context, p[0], (XtPointer) XtInputReadMask,
		      exec_output, NULL);
	break;
    }
    return pid;
}

void
exec_player(char *moviefile)
{
    //static char *command = "xanim +f +Sr +Ze -Zr";
    static char *command = "pia";
    char *cmd;
    char **argv;
    int  argc;
    
    /* go! */
    cmd = malloc(strlen(command)+strlen(moviefile)+5);
    sprintf(cmd,"%s %s",command,moviefile);
    argv = split_cmdline(cmd,&argc);
    exec_x11(argv);
}

void
LaunchAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
#if 0
    struct LAUNCH     *l;
    struct list_head  *item;
    char **argv;
    int  i,argc;
    
    if (*num_params != 1)
	return;

    /* fixme: get cmdline */

    argv = split_cmdline(l->cmdline,&argc);

    switch (fork()) {
    case -1:
	perror("fork");
	break;
    case 0:
	if (debug) {
	    fprintf(stderr,"[%d]: exec ",getpid());
	    for (i = 0; i < argc; i++) {
		fprintf(stderr,"\"%s\" ",argv[i]);
	    }
	    fprintf(stderr,"\n");
	}
	execvp(argv[0],argv);
	fprintf(stderr,"execvp %s: %s",argv[0],strerror(errno));
	exit(1);
	break;
    default:
	break;
    }
#endif
}

/*------------------------------------------------------------------------*/

XtSignalId sig_id;

static void termsig_handler(XtPointer data, XtSignalId *id)
{
    ExitCB(NULL,NULL,NULL);
}

static void
termsig(int signal)
{
    if (debug)
	fprintf(stderr,"received signal %d [%s]\n",signal,strsignal(signal));
    XtNoticeSignal(sig_id);
}

static void
segfault(int signal)
{
    fprintf(stderr,"[pid=%d] segfault catched, aborting\n",getpid());
    abort();
}

void
xt_siginit(void)
{
    struct sigaction act,old;

    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler  = exec_done;
    sigaction(SIGCHLD,&act,&old);

    sig_id = XtAppAddSignal(app_context,termsig_handler,NULL);
    act.sa_handler  = termsig;
    sigaction(SIGINT,&act,&old);
    sigaction(SIGTERM,&act,&old);

    act.sa_handler  = SIG_IGN;
    sigaction(SIGPIPE,&act,&old);
    
    if (debug) {
	act.sa_handler  = segfault;
	sigaction(SIGSEGV,&act,&old);
	fprintf(stderr,"main thread [pid=%d]\n",getpid());
    }
}

/*----------------------------------------------------------------------*/

static XtIntervalId xscreensaver_timer;

static void
xscreensaver_timefunc(XtPointer clientData, XtIntervalId *id)
{
    static int first = 1;
    int status;
    char *err;

    if (debug)
	fprintf(stderr,"xscreensaver_timefunc\n");
    xscreensaver_timer = 0;
    if (first) {
	xscreensaver_init(dpy);
	first = 0;
    }
    status = xscreensaver_command(dpy,XA_DEACTIVATE,0,debug,&err);
    if (0 != status) {
	if (debug)
	    fprintf(stderr,"xscreensaver_command: %s\n",err);
	return;
    }
    xscreensaver_timer = XtAppAddTimeOut(app_context,60000,
					 xscreensaver_timefunc,NULL);
}

/*----------------------------------------------------------------------*/

#ifdef HAVE_LIBXXF86VM
#if 0
static void
vidmode_timer(XtPointer clientData, XtIntervalId *id)
{
    do_va_cmd(2,"capture", "on");
}
#endif

static void
set_vidmode(XF86VidModeModeInfo *mode)
{
#if 0 /* FIXME */
    if (CAPTURE_OVERLAY == cur_capture) {
	do_va_cmd(2,"capture", "off");
	XtAppAddTimeOut(app_context,VIDMODE_DELAY,vidmode_timer,NULL);
    }
#endif
    /* usleep(VIDMODE_DELAY*1000); */
    if (debug)
	fprintf(stderr,"switching mode: %d  %d %d %d %d  %d %d %d %d  %d\n",
		mode->dotclock,
		mode->hdisplay,
		mode->hsyncstart,
		mode->hsyncend,
		mode->htotal,
		mode->vdisplay,
		mode->vsyncstart,
		mode->vsyncend,
		mode->vtotal,
		mode->flags);
    XF86VidModeSwitchToMode(dpy,XDefaultScreen(dpy),mode);
    XSync(dpy,False);
}

static void
do_vidmode_modeswitch(int fs_state, int *vp_width, int *vp_height)
{
    static int                  vm_switched;
    static XF86VidModeModeInfo  *vm_current    = NULL;
    static XF86VidModeModeInfo  *vm_fullscreen = NULL;
    int fs_width  = cfg_get_int("options","global","fs-width",0);
    int fs_height = cfg_get_int("options","global","fs-height",0);
    int i;
    
    if (fs_state) {
	/* enter fullscreen mode */
	XF86VidModeGetModeLine(dpy,XDefaultScreen(dpy),&vm_dot,&vm_line);
	XF86VidModeGetAllModeLines(dpy,XDefaultScreen(dpy),
				   &vm_count,&vm_modelines);
	vm_fullscreen = NULL;
	for (i = 0; i < vm_count; i++) {
	    if (fs_width  == vm_modelines[i]->hdisplay &&
		fs_height == vm_modelines[i]->vdisplay &&
		vm_fullscreen == NULL)
		vm_fullscreen = vm_modelines[i];
	    if (vm_line.hdisplay == vm_modelines[i]->hdisplay &&
		vm_line.vdisplay == vm_modelines[i]->vdisplay &&
		vm_dot           == vm_modelines[i]->dotclock &&
		vm_current == NULL)
		vm_current = vm_modelines[i];
	}
	if (debug) {
	    fprintf(stderr,"vm: current=%dx%d",
		    vm_current->hdisplay,vm_current->vdisplay);
	    if (vm_fullscreen)
		fprintf(stderr,"fullscreen=%dx%d",
			vm_fullscreen->hdisplay,vm_fullscreen->vdisplay);
	    fprintf(stderr,"\n");
	}
	if (vm_current && vm_fullscreen &&
	    vm_fullscreen->hdisplay != vm_current->hdisplay &&
	    vm_fullscreen->vdisplay != vm_current->vdisplay) {
	    set_vidmode(vm_fullscreen);
	    vm_switched = 1;
	    *vp_width   = vm_fullscreen->hdisplay;
	    *vp_height  = vm_fullscreen->vdisplay;
	} else {
	    vm_switched = 0;
	    *vp_width   = vm_current->hdisplay;
	    *vp_height  = vm_current->vdisplay;
	}
    } else {
	/* leave fullscreen mode */
	if (vm_switched) {
	    set_vidmode(vm_current);
	    vm_switched = 0;
	}
    }
}
#endif

#ifdef HAVE_LIBXRANDR
static void
do_randr_modeswitch(int fs_state, int *vp_width, int *vp_height)
{
    static SizeID normal;
    int fs_width  = cfg_get_int("options","global","fs-width",0);
    int fs_height = cfg_get_int("options","global","fs-height",0);
    Window root = RootWindow(dpy, DefaultScreen(dpy));
    XRRScreenConfiguration *sc;
    Rotation rotation;
    SizeID current, new, i;
    
    sc = XRRGetScreenInfo(dpy, root);
    current = XRRConfigCurrentConfiguration(sc, &rotation);
    new = current;
    if (fs_state) {
	/* enter fullscreen mode */
	normal = current;
	new = current;
	for (i = 0; i < nrandr; i++) {
	    if (randr[i].width  == fs_width &&
		randr[i].height == fs_height) {
		new = i;
		break;
	    }
	}
    } else {
	/* leave fullscreen mode */
	new = normal;
    }

    if (new != current) {
	/* switch mode */
	if (debug)
	    fprintf(stderr, "randr: switch to %dx%d\n",
		    randr[new].width, randr[new].height);
	XRRSetScreenConfig(dpy, sc, root, new, rotation, CurrentTime);
    }
    XRRFreeScreenConfigInfo(sc);

    /* FIXME: change swidth / sheight instead */
    *vp_width  = randr[new].width;
    *vp_height = randr[new].height;
}
#endif

static void
do_modeswitch(int fs_state, int *vp_width, int *vp_height)
{
    *vp_width  = swidth;
    *vp_height = sheight;

#ifdef HAVE_LIBXXF86VM
    if (have_vm)
	do_vidmode_modeswitch(fs_state,vp_width,vp_height);
#endif
#ifdef HAVE_LIBXRANDR
    if (!have_vm && have_randr)
	do_randr_modeswitch(fs_state,vp_width,vp_height);
#endif
}

/*----------------------------------------------------------------------*/

Boolean
MyResize(XtPointer client_data)
{
    /* needed for program-triggered resizes (fullscreen mode) */
    video_new_size();
    return TRUE;
}

static void
do_screensaver(int fs_state)
{
    static int timeout,interval,prefer_blanking,allow_exposures;
#ifdef HAVE_LIBXDPMS
    static BOOL dpms_on;
    CARD16 dpms_state;
    int dpms_dummy;
#endif

    if (fs_state) {
	/* fullscreen on -- disable screensaver */
	XGetScreenSaver(dpy,&timeout,&interval,
			&prefer_blanking,&allow_exposures);
	XSetScreenSaver(dpy,0,0,DefaultBlanking,DefaultExposures);
#ifdef HAVE_LIBXDPMS
	if ((DPMSQueryExtension(dpy, &dpms_dummy, &dpms_dummy)) && 
	    (DPMSCapable(dpy))) {
	    DPMSInfo(dpy, &dpms_state, &dpms_on);
            DPMSDisable(dpy); 
	}
#endif
	xscreensaver_timer = XtAppAddTimeOut(app_context,60000,
					     xscreensaver_timefunc,NULL);
    } else {
	/* fullscreen off -- enable screensaver */
	XSetScreenSaver(dpy,timeout,interval,prefer_blanking,allow_exposures);
#ifdef HAVE_LIBXDPMS
	if ((DPMSQueryExtension(dpy, &dpms_dummy, &dpms_dummy)) && 
	    (DPMSCapable(dpy)) && (dpms_on)) {
		DPMSEnable(dpy);
	}
#endif
	if (xscreensaver_timer)
	    XtRemoveTimeOut(xscreensaver_timer);
    }
}

void
do_fullscreen(void)
{
    static Dimension x,y,w,h;
    static int rpx,rpy;
    static int warp_pointer;

    Window root,child;
    int    wpx,wpy,mask;
    unsigned int vp_width, vp_height;

    if (wm_fullscreen &&
	cfg_get_bool("options","global","use-wm-fullscreen",1)) {
	/* full service for us, next to nothing to do */
	fs = !fs;
	if (debug)
	    fprintf(stderr,"fullscreen %s via netwm\n", fs ? "on" : "off");

	do_modeswitch(fs,&vp_width,&vp_height);
	XSync(dpy,False);
	wm_fullscreen(dpy,XtWindow(app_shell),fs);

	if (0 == fs  &&  on_timer) {
	    XtPopdown(on_shell);
	    XtRemoveTimeOut(on_timer);
	    on_timer = 0;
	}
	do_screensaver(fs);
	return;
    }

    if (fs) {
	if (debug)
	    fprintf(stderr,"turning fs off (%dx%d+%d+%d)\n",w,h,x,y);
	do_modeswitch(0,&vp_width,&vp_height);
	
	if (on_timer) {
	    XtPopdown(on_shell);
	    XtRemoveTimeOut(on_timer);
	    on_timer = 0;
	}
	
	XtVaSetValues(app_shell,
		      XtNwidthInc, WIDTH_INC,
		      XtNheightInc,HEIGHT_INC,
		      XtNx,        x /* + fs_xoff */,
		      XtNy,        y /* + fs_yoff */,
		      XtNwidth,    w,
		      XtNheight,   h,
		      NULL);

	do_screensaver(0);
    	if (warp_pointer)
	    XWarpPointer(dpy, None, RootWindowOfScreen(XtScreen(tv)),
			 0, 0, 0, 0, rpx, rpy);
	fs = 0;
    } else {
	int vp_x, vp_y;

	if (debug)
	    fprintf(stderr,"turning fs on\n");
	XQueryPointer(dpy, RootWindowOfScreen(XtScreen(tv)),
		      &root, &child, &rpx, &rpy, &wpx, &wpy, &mask);

	vp_x = 0;
	vp_y = 0;
	do_modeswitch(1,&vp_width,&vp_height);
	if (vp_width < sheight || vp_width < swidth) {
	    /* move viewpoint, make sure the pointer is in there */
	    warp_pointer = 1;
	    XWarpPointer(dpy, None, RootWindowOfScreen(XtScreen(tv)),
			 0, 0, 0, 0, vp_width/2, vp_height/2);
#ifdef HAVE_LIBXXF86VM
	    XF86VidModeSetViewPort(dpy,XDefaultScreen(dpy),0,0);
#endif
	}
	XtVaGetValues(app_shell,
		      XtNx,          &x,
		      XtNy,          &y,
		      XtNwidth,      &w,
		      XtNheight,     &h,
		      NULL);

#ifdef HAVE_LIBXINERAMA
	if (nxinerama) {
	    /* check which physical screen we are visible on */
	    int i;
	    for (i = 0; i < nxinerama; i++) {
		if (x >= xinerama[i].x_org &&
		    y >= xinerama[i].y_org && 
		    x <  xinerama[i].x_org + xinerama[i].width &&
		    y <  xinerama[i].y_org + xinerama[i].height) {
		    vp_x      = xinerama[i].x_org;
		    vp_y      = xinerama[i].y_org;
		    vp_width  = xinerama[i].width;
		    vp_height = xinerama[i].height;
		    break;
		}
	    }
	}
#endif
	if (debug)
	    fprintf(stderr,"viewport: %dx%d+%d+%d\n",
		    vp_width,vp_height,vp_x,vp_y);

	XtVaSetValues(app_shell,
		      XtNwidthInc,   1,
		      XtNheightInc,  1,
		      NULL);
	XtVaSetValues(app_shell,
		      XtNx,          (vp_x & 0xfffc) /* + fs_xoff */,
		      XtNy,          vp_y            /* + fs_yoff */,
		      XtNwidth,      vp_width,
		      XtNheight,     vp_height,
		      NULL);

        XRaiseWindow(dpy, XtWindow(app_shell));
	do_screensaver(1);
	if (warp_pointer)
	    XWarpPointer(dpy, None, XtWindow(tv), 0, 0, 0, 0, 30, 15);
	fs = 1;
    }
    XtAppAddWorkProc (app_context, MyResize, NULL);
}

/*----------------------------------------------------------------------*/

static void
title_timeout(XtPointer client_data, XtIntervalId *id)
{
    keypad_timeout();
    XtVaSetValues(app_shell,XtNtitle,default_title,NULL);
    title_timer = 0;
}

void
new_title(char *txt)
{
    strcpy(default_title,txt);
    XtVaSetValues(app_shell,
		  XtNtitle,default_title,
		  XtNiconName,default_title,
		  NULL);
    display_onscreen(default_title);

    if (title_timer) {
	XtRemoveTimeOut(title_timer);
	title_timer = 0;
    }
}

void
new_message(char *txt)
{
    XtVaSetValues(app_shell,XtNtitle,txt,NULL);
    display_onscreen(txt);
    if (title_timer)
	XtRemoveTimeOut(title_timer);
    title_timer = XtAppAddTimeOut
	(app_context, TITLE_TIME, title_timeout,NULL);
}

/*
 * mode = -1: check mode (just update the title)
 * mode =  0: set autodetect (and read back result)
 * mode >  0: set some mode
 */
void
change_audio(int mode)
{
#if 0
    struct ng_attribute *attr;
    const char *mname;
    char label[64];

    attr = ng_attr_byid(attrs,ATTR_ID_AUDIO_MODE);
    if (NULL == attr)
	return;

    if (-1 != mode)
	attr->write(attr,mode);
    if (-1 == mode || 0 == mode)
	mode = attr->read(attr);

    mname = ng_attr_getstr(attr,mode);
    if (NULL == mname)
	mname = "???";

    if (attr_notify)
	attr_notify(attr,mode);

    sprintf(label,"%s (%s)",default_title,mname);
    XtVaSetValues(app_shell,XtNtitle,label,NULL);
#endif
}

/*----------------------------------------------------------------------*/

void
CommandAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    do_command(*num_params,params);
}

void
set_property(void)
{
    int  len;
    char line[80];

    len  = sprintf(line,     "%s", curr_details)+1;
    len += sprintf(line+len, "%s", curr_channel ? curr_channel : "?") +1;
    len += sprintf(line+len, "%s", curr_station ? curr_station : "?") +1;
    XChangeProperty(dpy, XtWindow(app_shell),
                    _XAWTV_STATION, XA_STRING,
                    8, PropModeReplace,
                    line, len);
}

void command_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct command *cmd = clientdata;
    do_command(cmd->argc,cmd->argv);
}

void tv_expose_event(Widget widget, XtPointer client_data,
		     XEvent *event, Boolean *d)
{
#if 0 /* FIXME */
    static GC  gc;
    XGCValues  values;

    switch(event->type) {
    case Expose:
	if (debug)
	    fprintf(stderr,"expose count=%d\n",
		    event->xexpose.count);
	if (0 == event->xexpose.count && CAPTURE_OVERLAY == cur_capture) {
	    if (devs.cap.flags & NEEDS_CHROMAKEY) {
		Dimension win_width, win_height;
		if (debug)
		    fprintf(stderr,"expose: chromakey [%dx%d]\n",
			    cur_tv_width, cur_tv_height);
		/* draw background for chroma keying */
		if (0 == gc) {
		    XColor color;
		    color.red   = (ng_chromakey & 0x00ff0000) >> 8;
		    color.green = (ng_chromakey & 0x0000ff00);
		    color.blue  = (ng_chromakey & 0x000000ff) << 8;
		    XAllocColor(dpy,colormap,&color);
		    values.foreground = color.pixel;
		    values.background = color.pixel;
		    gc = XCreateGC(dpy, XtWindow(widget),
				   GCForeground | GCBackground,
				   &values);
		}
		XtVaGetValues(widget, XtNwidth, &win_width,
			      XtNheight, &win_height, NULL);
		XFillRectangle(dpy,XtWindow(widget),gc,
			       (win_width  - cur_tv_width)  >> 1,
			       (win_height - cur_tv_height) >> 1,
			       cur_tv_width, cur_tv_height);
	    }
	    if (have_xv) {
		if (debug)
		    fprintf(stderr,"expose: xv reblit\n");
		video_new_size();
	    }
	}
	break;
    }
#endif
}

void
FilterAction(Widget widget, XEvent *event,
	     String *params, Cardinal *num_params)
{
#if 0 /* FIXME */
    struct list_head *item;
    struct ng_filter *filter;
#endif

    cur_filter = NULL;
    if (0 == *num_params)
	return;
#if 0 /* FIXME */
    list_for_each(item,&ng_filters) {
	filter = list_entry(item, struct ng_filter, list);
	if (0 == strcasecmp(filter->name,params[0])) {
	    cur_filter = filter;
	    break;
	}
    }
#endif
}

void pick_device_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    if (debug)
	fprintf(stderr,"switch device: %s\n",XtName(widget));
    pick_device_new = XtName(widget);
    command_pending = 1;
}

/*----------------------------------------------------------------------*/

void
xfree_dga_init(Display *dpy)
{
#ifdef HAVE_LIBXXF86DGA
    int  flags,foo,bar,ma,mi;

    if (!do_overlay)
	return;
    
    if (GET_EXT_DGA()) {
	if (XF86DGAQueryExtension(dpy,&foo,&bar)) {
	    XF86DGAQueryDirectVideo(dpy,XDefaultScreen(dpy),&flags);
	    if (flags & XF86DGADirectPresent) {
		XF86DGAQueryVersion(dpy,&ma,&mi);
		if (debug)
		    fprintf(stderr,"DGA version %d.%d\n",ma,mi);
		have_dga = 1;
	    }
	}
    }
#endif
}

void
xfree_vm_init(Display *dpy)
{
#ifdef HAVE_LIBXXF86VM
    int  foo,bar,i,ma,mi;

    if (!do_overlay)
	return;

    if (GET_EXT_XVIDMODE()) {
	if (XF86VidModeQueryExtension(dpy,&foo,&bar)) {
	    XF86VidModeQueryVersion(dpy,&ma,&mi);
	    if (debug)
		fprintf(stderr,"VidMode  version %d.%d\n",ma,mi);
	    have_vm = 1;
	    XF86VidModeGetAllModeLines(dpy,XDefaultScreen(dpy),
				       &vm_count,&vm_modelines);
	    if (debug) {
		fprintf(stderr,"  available video mode(s):");
		for (i = 0; i < vm_count; i++) {
		    fprintf(stderr," %dx%d",
			    vm_modelines[i]->hdisplay,
			    vm_modelines[i]->vdisplay);
		}	    
		fprintf(stderr,"\n");
	    }
	}
    }
#endif
}

void
xfree_randr_init(Display *dpy)
{
#ifdef HAVE_LIBXRANDR
    int bar,i;
    
    if (GET_EXT_XRANDR()) {
	if (XRRQueryExtension(dpy,&randr_evbase,&bar)) {
	    randr = XRRSizes(dpy,DefaultScreen(dpy),&nrandr);
	    if (nrandr > 0) {
		have_randr = 1;
		if (debug) {
		    fprintf(stderr,"xrandr:");
		    for (i = 0; i < nrandr; i++) {
			fprintf(stderr, " %dx%d",
				randr[i].width, randr[i].height);
		    }
		    fprintf(stderr,"\n");
		}
	    }
	}
    }
#endif
}

void
xfree_xinerama_init(Display *dpy)
{
#ifdef HAVE_LIBXINERAMA
    int foo,bar,i;
    
    if (XineramaQueryExtension(dpy,&foo,&bar) &&
	XineramaIsActive(dpy)) {
	xinerama = XineramaQueryScreens(dpy,&nxinerama);
	for (i = 0; i < nxinerama; i++) {
	    fprintf(stderr,"xinerama %d: %dx%d+%d+%d\n",
		    xinerama[i].screen_number,
		    xinerama[i].width,
		    xinerama[i].height,
		    xinerama[i].x_org,
		    xinerama[i].y_org);
	}
    }
#endif
}

static void
v4lconf_init(char *dev)
{
    if (!do_overlay)
	return;

#if 0
    strcpy(ng_v4l_conf,"v4l-conf");
    if (!args.debug)
	strcat(ng_v4l_conf," -q");
    if (args.fbdev)
	strcat(ng_v4l_conf," -f");
    if (args.shift)
	sprintf(ng_v4l_conf+strlen(ng_v4l_conf)," -s %d",args.shift);
    if (args.bpp)
	sprintf(ng_v4l_conf+strlen(ng_v4l_conf)," -b %d",args.bpp);
    if (args.device)
	sprintf(ng_v4l_conf+strlen(ng_v4l_conf)," -c %s",args.device);
#endif
}

void
grabber_init(char *dev)
{
#if 0
    struct ng_video_fmt screen;
    void *base = NULL;

    memset(&screen,0,sizeof(screen));
#ifdef HAVE_LIBXXF86DGA
    if (have_dga) {
	int bar,fred;
    	XF86DGAGetVideoLL(dpy,XDefaultScreen(dpy),(void*)&base,
			  &screen.bytesperline,&bar,&fred);
    }
    if (!do_overlay) {
	if (debug)
	    fprintf(stderr,"x11: remote display (overlay disabled)\n");
    } else {
	screen.width  = XtScreen(app_shell)->width;
	screen.height = XtScreen(app_shell)->height;
	screen.fmtid  = x11_dpy_fmtid;
	screen.bytesperline *= ng_vfmt_to_depth[x11_dpy_fmtid]/8;
	if (debug)
	    fprintf(stderr,"x11: %dx%d, %d bit/pixel, %d byte/scanline%s%s\n",
		    screen.width,screen.height,
		    ng_vfmt_to_depth[screen.fmtid],
		    screen.bytesperline,
		    have_dga ? ", DGA"     : "",
		    have_vm  ? ", VidMode" : "");
	ng_vid_screen(&devs.video, &screen, base);
    }
#endif
#endif

    char *val;

    v4lconf_init(dev);
    device_init(dev);
    audio_on();

    if (devs.video.type != NG_DEV_NONE) {
	/* init v4l device */
	if (NULL != (val = cfg_get_str(O_TVNORM)))
	    do_va_cmd(2,"setnorm",val);
	if (NULL != (val = cfg_get_str(O_INPUT)))
	    do_va_cmd(2,"setinput",val);
	if (devs.video.flags & CAN_CAPTURE)
	    display_mode = DISPLAY_GRAB;

    } else if (NULL != devs.dvb) {
	/* init dvb device */
	display_mode = DISPLAY_DVB;

    } else {
	display_mode = DISPLAY_NONE;

    }

    /* retune */
    if (NULL != curr_station)
	do_va_cmd(2,"setstation",curr_station);
}

void
grabber_fini(void)
{
    audio_off();
    device_fini();
}

#if 0 /* FIXME */
void
grabber_scan(void)
{
    const struct ng_vid_driver  *driver;
    void *handle;
    struct stat st;
    int n,i,fh,flags;

    for (i = 0; ng_dev.video_scan[i] != NULL; i++) {
	if (-1 == lstat(ng_dev.video_scan[i],&st)) {
	    if (ENOENT == errno)
		continue;
	    fprintf(stderr,"%s: %s\n",ng_dev.video_scan[i],strerror(errno));
	    continue;
	}
	fh = open(ng_dev.video_scan[i],O_RDWR);
	if (-1 == fh) {
	    if (ENODEV == errno)
		continue;
	    fprintf(stderr,"%s: %s\n",ng_dev.video_scan[i],strerror(errno));
	    continue;
	}
	close(fh);

	driver = ng_vid_open(ng_dev.video_scan[i], args.driver,
			     NULL, NULL, &handle);
	if (NULL == driver) {
	    fprintf(stderr,"%s: initialization failed\n",ng_dev.video_scan[i]);
	    continue;
	}
	flags = driver->capabilities(handle);
	n = fprintf(stderr,"%s: OK",ng_dev.video_scan[i]);
	fprintf(stderr,"%*s[ -device %s ]\n",40-n,"",ng_dev.video_scan[i]);
	fprintf(stderr,"    type : %s\n",driver->name);
	if (driver->get_devname)
	    fprintf(stderr,"    name : %s\n",driver->get_devname(handle));
	fprintf(stderr,"    flags: %s %s %s %s\n",
		(flags & CAN_OVERLAY)     ? "overlay"   : "",
		(flags & CAN_CAPTURE)     ? "capture"   : "",
		(flags & CAN_TUNE)        ? "tuner"     : "",
		(flags & NEEDS_CHROMAKEY) ? "chromakey" : "");
	driver->close(handle);
	fprintf(stderr,"\n");
    }
    exit(0);
}
#endif

void
x11_check_remote()
{
#if defined(HAVE_GETNAMEINFO) && defined(HAVE_SOCKADDR_STORAGE)
    int fd = ConnectionNumber(dpy);
    struct sockaddr_storage ss;
    char me[INET6_ADDRSTRLEN+1];
    char peer[INET6_ADDRSTRLEN+1];
    char port[17];
    int length;

    if (debug)
	fprintf(stderr, "check if the X-Server is local ... ");
    
    /* me */
    length = sizeof(ss);
    if (-1 == getsockname(fd,(struct sockaddr*)&ss,&length)) {
	perror("getsockname");
	return;
    }
    if (debug)
	fprintf(stderr,"*");

    /* catch unix sockets on FreeBSD */
    if (0 == length || ss.ss_family == AF_UNIX) {
	if (debug)
	    fprintf(stderr, " ok (unix socket)\n");
	return;
    }
    
    getnameinfo((struct sockaddr*)&ss,length,
		me,INET6_ADDRSTRLEN,port,16,
		NI_NUMERICHOST | NI_NUMERICSERV);
    if (debug)
	fprintf(stderr,"*");

    /* peer */
    length = sizeof(ss);
    if (-1 == getpeername(fd,(struct sockaddr*)&ss,&length)) {
	perror("getsockname");
	return;
    }
    if (debug)
	fprintf(stderr,"*");

    getnameinfo((struct sockaddr*)&ss,length,
		peer,INET6_ADDRSTRLEN,port,16,
		NI_NUMERICHOST | NI_NUMERICSERV);
    if (debug)
	fprintf(stderr,"*");

    if (debug)
	fprintf(stderr," ok\nx11 socket: me=%s, server=%s\n",me,peer);
    if (0 != strcmp(me,peer))
	/* different hosts => assume remote display */
	do_overlay = 0;
#endif
    return;
}

void x11_misc_init(Display *dpy)
{
    fcntl(ConnectionNumber(dpy),F_SETFD,FD_CLOEXEC);
}

/*----------------------------------------------------------------------*/

void
visual_init(char *n1, char *n2)
{
    Visual         *visual;
    XVisualInfo    *vinfo_list;
    int            n;

    /* look for a useful visual */
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
	app_shell = XtVaAppCreateShell(n1,n2,
				       applicationShellWidgetClass, dpy,
				       XtNvisual,vinfo.visual,
				       XtNcolormap,colormap,
				       XtNdepth, vinfo.depth,
				       NULL);
    } else {
	colormap = DefaultColormapOfScreen(XtScreen(app_shell));
    }
    x11_init_visual(XtDisplay(app_shell),&vinfo);
}

static void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: xawtv [ options ] [ station ]\n"
	    "options:\n"
	    "  -h  -help           print this text\n"
	    "      -version        print version\n"
	    "  -v  -debug n        debug level n, n = [0..2]\n"
	    "      -remote         assume remote display\n"
	    "  -n  -noconf         don't read the config file\n"
	    "  -m  -nomouse        startup with mouse pointer disabled\n"
	    "  -f  -fullscreen     startup in fullscreen mode\n"
#ifdef HAVE_LIBXXF86DGA
	    "      -(no)dga        enable/disable DGA extension\n"
#endif
#ifdef HAVE_LIBXXF86VM
	    "      -(no)vm         enable/disable VidMode extension\n"
#endif
#ifdef HAVE_LIBXRANDR
	    "      -(no)randr      enable/disable Xrandr extension\n"
#endif
#ifdef HAVE_LIBXV
	    "      -(no)xv         enable/disable Xvideo extension altogether\n"
	    "      -(no)xv-video   enable/disable Xvideo extension (for video only,\n"
	    "                      i.e. XvPutVideo() calls)\n"
	    "      -(no)xv-image   enable/disable Xvideo extension (for image scaling\n"
	    "                      only, i.e. XvPutImage() calls)\n"
#endif
#ifdef HAVE_GL
	    "      -(no)gl         enable/disable OpenGL\n"
#endif
	    "  -b  -bpp n          color depth of the display is n (n=24,32)\n"
	    "  -o  -outfile file   filename base for snapshots\n"
	    "  -c  -device file    use <file> as video4linux device\n"
	    "  -C  -dspdev file    use <file> as audio (oss) device\n"
	    "      -vbidev file    use <file> as vbi device\n"
	    "      -joydev file    use <file> as joystick device\n"
	    "      -shift x        shift display by x bytes\n"
	    "      -fb             let fb (not X) set up v4l device\n"
	    "      -parallel n     use n compression threads\n"
	    "      -bufcount n     use n video buffers\n"
	    "      -hwscan         print a list of available devices.\n"
	    "station:\n"
	    "  this is one of the stations listed in $HOME/.xawtv\n"
	    "\n"
	    "Check the manual page for a more detailed description.\n"
	    "\n"
	    "--\n"
	    "Gerd Knorr <kraxel@bytesex.org>\n");
}

void
hello_world(char *name)
{
    struct utsname uts;

    if (0 == geteuid() && 0 != getuid()) {
	fprintf(stderr,"%s *must not* be installed suid root\n",name);
	exit(1);
    }
    
    uname(&uts);
    fprintf(stderr,"This is %s-%s, running on %s/%s (%s)\n",
	    name,VERSION,uts.sysname,uts.machine,uts.release);
    if (args.printver)
	exit(0);
}

void
handle_cmdline_args(void)
{
    XtGetApplicationResources(app_shell,&args,
			      args_desc,args_count,
			      NULL,0);
    if (args.help) {
	usage();
	exit(0);
    }
    snapbase = args.basename;
    debug    = args.debug;
    ng_debug = args.debug;
}

int
x11_ctrl_alt_backspace(Display *dpy)
{
    fprintf(stderr,"game over\n");
    if (debug)
	abort();
    do_va_cmd(2,"capture", "off");
    video_overlay(0);
    video_close();
    grabber_fini();
    exit(0);
}

/*----------------------------------------------------------------------*/

static int mouse_visible;
static XtIntervalId mouse_timer;

static void
mouse_timeout(XtPointer clientData, XtIntervalId *id)
{
    Widget widget = clientData;
    if (debug > 1)
	fprintf(stderr,"xt: pointer hide\n");
    if (XtWindow(widget))
	XDefineCursor(dpy, XtWindow(widget), no_ptr);
    mouse_visible = 0;
    mouse_timer = 0;
}

void
mouse_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
    if (!mouse_visible) {
	if (debug > 1)
	    fprintf(stderr,"xt: pointer show\n");
	if (XtWindow(widget))
	    XDefineCursor(dpy, XtWindow(widget), left_ptr);
	mouse_visible = 1;
    }
    if (mouse_timer)
	XtRemoveTimeOut(mouse_timer);
    mouse_timer = XtAppAddTimeOut(app_context, 1000, mouse_timeout,widget);
}

/*----------------------------------------------------------------------*/

#ifdef HAVE_ZVBI
static XtInputId x11_vbi_input;
static struct vbi_state *x11_vbi;
static int x11_vbi_page;
char x11_vbi_station[64];

static void
x11_vbi_event(struct vbi_event *ev, void *user)
{
    struct vbi_page pg;
    struct vbi_rect rect;
    
    switch (ev->type) {
    case VBI_EVENT_NETWORK:
	strcpy(x11_vbi_station,ev->ev.network.name);
	break;
    case VBI_EVENT_TTX_PAGE:
	if (ev->ev.ttx_page.pgno == x11_vbi_page) {
	    if (debug)
		fprintf(stderr,"got vtx page %03x\n",ev->ev.ttx_page.pgno);
	    if (vtx_subtitle) {
		vbi_fetch_vt_page(x11_vbi->dec,&pg,
				  ev->ev.ttx_page.pgno,
				  ev->ev.ttx_page.subno,
				  VBI_WST_LEVEL_1p5,25,1);
		vbi_find_subtitle(&pg,&rect);
		if (25 == rect.y1)
		    vtx_subtitle(NULL,NULL);
		else
		    vtx_subtitle(&pg,&rect);
	    }
	}
	break;
    }
}

static void x11_vbi_data(XtPointer data, int *fd, XtInputId *iproc)
{
    struct vbi_state *vbi = data;
    vbi_hasdata(vbi);
}

int
x11_vbi_start(char *device, int pid)
{
    if (NULL != x11_vbi)
	return 0;

    if (NULL == device)
	device = ng_dev.vbi;

    x11_vbi = vbi_open(device, debug, 0);
    if (NULL == x11_vbi) {
	fprintf(stderr,"open %s: %s\n",device,strerror(errno));
	return -1;
    }
    if (pid)
	vbi_set_pid(x11_vbi, pid);
    vbi_event_handler_add(x11_vbi->dec,~0,x11_vbi_event,x11_vbi);
    x11_vbi_input = XtAppAddInput(app_context,x11_vbi->fd,
				  (XtPointer) XtInputReadMask,
				  x11_vbi_data,x11_vbi);
    if (debug)
	fprintf(stderr,"x11_vbi_start\n");
    return 0;
}

/* try to use the vbi device to see whenever we have a station or not.
 * failing that, fallback to the libng grabber driver. */
int
x11_vbi_tuned(void)
{
#if defined(__linux__)
    struct video_tuner tuner;

    if (NULL == x11_vbi)
	return devs.video.v->is_tuned(devs.video.handle);
    memset(&tuner,0,sizeof(tuner));
    if (-1 != ioctl(x11_vbi->fd,VIDIOCGTUNER,&tuner))
	return tuner.signal ? 1 : 0;
#endif
    return devs.video.v->is_tuned(devs.video.handle);
}

void
x11_vbi_stop(void)
{
    if (NULL == x11_vbi)
	return;
    XtRemoveInput(x11_vbi_input);
    vbi_event_handler_remove(x11_vbi->dec,x11_vbi_event);
    vbi_close(x11_vbi);
    x11_vbi = NULL;
    if (debug)
	fprintf(stderr,"x11_vbi_stop\n");
}

void
VtxAction(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    if (0 == *num_params)
	return;
    if (0 == strcasecmp(params[0],"start")) {
	if (2 >= *num_params)
	    sscanf(params[1],"%x",&x11_vbi_page);
	if (debug)
	    fprintf(stderr,"subtitles page: %x\n",x11_vbi_page);
	x11_vbi_start(devs.vbidev, 0 /* FIXME */);
    }
    if (0 == strcasecmp(params[0],"stop")) {
	x11_vbi_page = 0;
	x11_vbi_stop();
#ifdef HAVE_ZVBI
	if (vtx_subtitle)
	    vtx_subtitle(NULL,NULL);
#endif
    }
}
#endif

/* ---------------------------------------------------------------------- */
/* control via lirc / midi / joystick                                     */

static int xt_lirc;

static void
xt_lirc_data(XtPointer data, int *fd, XtInputId *iproc)
{
    if (debug)
	fprintf(stderr,"lirc_input triggered\n");
    if (-1 == lirc_tv_havedata()) {
	fprintf(stderr,"lirc: connection lost\n");
	XtRemoveInput(*iproc);
	close(*fd);
    }
}

int xt_lirc_init(void)
{
    if (-1 != (xt_lirc = lirc_tv_init()))
	XtAppAddInput(app_context,xt_lirc,(XtPointer)XtInputReadMask,
		      xt_lirc_data,NULL);
    return 0;
}

static int xt_joystick;

static void
xt_joystick_data(XtPointer data, int *fd, XtInputId *iproc)
{
    joystick_tv_havedata(xt_joystick);
}

int xt_joystick_init(void)
{
    if (-1 != (xt_joystick = joystick_tv_init(args.joydev)))
	XtAppAddInput(app_context,xt_joystick,(XtPointer)XtInputReadMask,
		      xt_joystick_data,NULL);
    return 0;
}

/* ---------------------------------------------------------------------- */

void xt_kbd_init(Widget tv)
{
    char *list,key[32],str[128];

    for (list  = cfg_entries_first("options","eventmap");
	 list != NULL;
	 list  = cfg_entries_next("options","eventmap", list)) {
	if (1 != sscanf(list,"kbd-key-%31s",key))
	    continue;
	sprintf(str,"<Key>%s: Event(%s)",key,list);
	XtOverrideTranslations(tv,XtParseTranslationTable(str));
    }
}

void
EventAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    if (0 != *num_params)
	event_dispatch(params[0]);
}

/* ---------------------------------------------------------------------- */

Cursor left_ptr;
Cursor menu_ptr;
Cursor qu_ptr;
Cursor no_ptr;

Pixmap bm_yes;
Pixmap bm_no;

static unsigned char bm_yes_data[] = {
    /* -------- -------- */  0x00,
    /* -------- -------- */  0x00,
    /* ------xx xx------ */  0x18,			   
    /* ----xxxx xxxx---- */  0x3c,
    /* ----xxxx xxxx---- */  0x3c,
    /* ------xx xx------ */  0x18,
    /* -------- -------- */  0x00,
    /* -------- -------- */  0x00
};

static unsigned char bm_no_data[] = { 0,0,0,0, 0,0,0,0 };

void
create_pointers(Widget app_shell)
{
    XColor white,red,dummy;
    Screen *scr;
    
    left_ptr = XCreateFontCursor(dpy,XC_left_ptr);
    menu_ptr = XCreateFontCursor(dpy,XC_right_ptr);
    qu_ptr   = XCreateFontCursor(dpy,XC_question_arrow);
    scr = DefaultScreenOfDisplay(dpy);
    if (vinfo.depth > 1) {
	if (XAllocNamedColor(dpy,colormap,"white",&white,&dummy) &&
	    XAllocNamedColor(dpy,colormap,"red",&red,&dummy)) {
	    XRecolorCursor(dpy,left_ptr,&red,&white);
	    XRecolorCursor(dpy,menu_ptr,&red,&white);
	    XRecolorCursor(dpy,qu_ptr,&red,&white);
	} 
    }
}

void
create_bitmaps(Widget app_shell)
{
    XColor black, dummy;

    bm_yes = XCreateBitmapFromData(dpy, XtWindow(app_shell),
				   bm_yes_data, 8,8);
    bm_no = XCreateBitmapFromData(dpy, XtWindow(app_shell),
				  bm_no_data, 8,8);

    XAllocNamedColor(dpy,colormap,"black",&black,&dummy);
    no_ptr = XCreatePixmapCursor(dpy, bm_no, bm_no,
				 &black, &black,
				 0, 0);
}

/* ---------------------------------------------------------------------- */

int xt_vm_randr_input_init(Display *dpy)
{
    /* vidmode / randr */
    if (debug)
	fprintf(stderr,"xt: checking for vidmode extension ...\n");
    xfree_vm_init(dpy);
    if (debug)
	fprintf(stderr,"xt: checking for randr extension ...\n");
    xfree_randr_init(dpy);
    
    /* input */
    if (debug)
	fprintf(stderr,"xt: checking for lirc ...\n");
    xt_lirc_init();
    if (debug)
	fprintf(stderr,"xt: checking for joystick ...\n");
    xt_joystick_init();
    if (debug)
	fprintf(stderr,"xt: adding kbd hooks ...\n");
    xt_kbd_init(tv);

    return 0;
}

/* ---------------------------------------------------------------------- */

static void
grabdisplay_loop(Widget widget, struct blit_handle *blit)
{
    struct media_stream mm;
    
    memset(&mm,0,sizeof(mm));
    mm.blit = blit;
    mm.speed = 0;
    
    /* video setup */
    ng_dev_open(&devs.video);
    av_media_grab_video(&mm,widget);
    
    /* go playback stuff */
    if (mm.vs) {
	devs.video.v->startvideo(devs.video.handle,-1,2);
	av_media_mainloop(&mm);
	devs.video.v->stopvideo(devs.video.handle);
    }

    /* cleanup */
    BUG_ON(NULL != mm.as,"mm.as isn't NULL");
    BUG_ON(NULL != mm.vs,"mm.vs isn't NULL");
    ng_dev_close(&devs.video);
    return;
}

#ifdef HAVE_DVB

static void
dvb_loop(Widget widget, struct blit_handle *blit)
{
    char path[64];
    struct media_stream mm;
    struct ng_audio_fmt *afmt;
    struct ng_video_fmt *vfmt;
    
    memset(&mm,0,sizeof(mm));
    mm.blit = blit;
    mm.speed = 1;

    if (!dvb_frontend_is_locked(devs.dvb))
	return;
    
    mm.reader = ng_find_reader_name("mpeg-ts");
    if (NULL == mm.reader) {
	fprintf(stderr,"Oops: transport stream parser not found\n");
	return;
    }

    snprintf(path,sizeof(path),"%s/dvr0",devs.dvbadapter);
    mm.rhandle = mm.reader->rd_open(path);
    if (NULL == mm.rhandle) {
	fprintf(stderr,"can't open: %s\n",path);
	return;
    }

    /* audio + video setup */
    vfmt = mm.reader->rd_vfmt(mm.rhandle,NULL,0);
    if (vfmt && (0 == vfmt->width || 0 == vfmt->height))
	vfmt = NULL;
    afmt = mm.reader->rd_afmt(mm.rhandle);

    if (vfmt)
	av_media_reader_video(&mm);
    if (afmt)
	av_media_reader_audio(&mm,afmt);

    /* go playback stuff */
    av_media_mainloop(&mm);

    /* cleanup */
    BUG_ON(NULL != mm.as,"mm.as isn't NULL");
    BUG_ON(NULL != mm.vs,"mm.vs isn't NULL");
    mm.reader->rd_close(mm.rhandle);
    return;
}

#endif

int xt_main_loop()
{
    struct blit_handle *blit;
    XtInputMask x11mask;
    XEvent event;

    /* init */
    if (debug)
	fprintf(stderr,"xt: open grabber device...\n");
    grabber_init(NULL);
    blit = blit_init(tv, &vinfo, GET_EXT_XV_SCALE(), GET_EXT_OPENGL());
    signal(SIGHUP,SIG_IGN); /* don't really need a tty ... */

    /* mainloop */
    if (debug)
	fprintf(stderr,"xt: enter main event loop... \n");
    for (;;) {
	if (XtAppGetExitFlag(app_context))
	    break;
	if (exit_application)
	    break;

	/* switch to another device */
	if (pick_device_new) {
	    grabber_fini();
	    grabber_init(pick_device_new);
	    pick_device_new = NULL;
	}
	
	/* handle all outstanding events */
	queue_run();
	while (0 != (x11mask = XtAppPending(app_context)))
	    XtAppProcessEvent(app_context,x11mask);

	/* media display */
	switch (display_mode) {
	case DISPLAY_NONE:
	    if (debug)
		fprintf(stderr,"display mode \"none\"\n");
	    break;
	case DISPLAY_GRAB:
	    if (devs.video.type != NG_DEV_NONE)
		grabdisplay_loop(tv,blit);
	    break;
#ifdef HAVE_DVB
	case DISPLAY_DVB:
	    if (devs.dvb)
		dvb_loop(tv,blit);
	    break;
#endif
	default:
	    fprintf(stderr,"unknown/unimplemented display mode %d\n",display_mode);
	    break;
	}

	/* default/error catch */
	while (!command_pending) {
	    XtAppNextEvent(app_context, &event);
	    if (True == XtDispatchEvent(&event))
		continue;
	}

    }

    /* cleanup */
    blit_fini(blit);
    grabber_fini();
    return 0;
}

/* ---------------------------------------------------------------------- */
/* from channel.c                                                         */

#if 0

static void hotkey_channel(struct CHANNEL *channel)
{
    char str[100],key[32],ctrl[16];

    if (NULL == channel->key)
	return;
    if (2 == sscanf(channel->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
		    ctrl,key))
	sprintf(str,"%s<Key>%s: Command(setstation,\"%s\")",
		ctrl,key,channel->name);
    else
	sprintf(str,"<Key>%s: Command(setstation,\"%s\")",
		channel->key,channel->name);
    XtOverrideTranslations(tv,XtParseTranslationTable(str));
    XtOverrideTranslations(opt_paned,XtParseTranslationTable(str));
    XtOverrideTranslations(chan_viewport,XtParseTranslationTable(str));
}

static void
launch_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *argv[2];

    argv[0] = (char*)clientdata;
    argv[1] = NULL;
    XtCallActionProc(widget,"Launch",NULL,argv,1);
}

static void
hotkey_launch(struct LAUNCH *launch)
{
    Widget c;
    char str[100],key[32],ctrl[16],label[64];

    if (NULL == launch->key)
	return;
    if (2 == sscanf(launch->key,"%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
		    ctrl,key))
	sprintf(str,"%s<Key>%s: Launch(\"%s\")",ctrl,key,launch->name);
    else
	sprintf(str,"<Key>%s: Launch(\"%s\")",launch->key,launch->name);
    XtOverrideTranslations(tv,XtParseTranslationTable(str));
    XtOverrideTranslations(opt_paned,XtParseTranslationTable(str));
    XtOverrideTranslations(chan_viewport,XtParseTranslationTable(str));

    sprintf(label,"%-20s %s",launch->name,launch->key);
    c = XtVaCreateManagedWidget(launch->name, commandWidgetClass,
				launch_paned,
				PANED_FIX,
				XtNlabel,label,
				NULL);
    XtAddCallback(c,XtNcallback,launch_cb,(XtPointer)(launch->name));
}

static void
button_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct CHANNEL *channel = clientdata;
    do_va_cmd(2,"setstation",channel->name);
}

#endif
