/*
 * main.c for xawtv -- a TV application
 *
 *   (c) 1997-2004 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#define _GNU_SOURCE

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/Xmd.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Xatom.h>
#include <X11/Shell.h>
#include <X11/Xaw/XawInit.h>
#include <X11/Xaw/Paned.h>
#include <X11/Xaw/Command.h>
#include <X11/Xaw/Scrollbar.h>
#include <X11/Xaw/Viewport.h>
#include <X11/Xaw/Box.h>
#include <X11/Xaw/Dialog.h>
#include <X11/Xaw/AsciiText.h>
#include <X11/extensions/XShm.h>

#include "grab-ng.h"
#include "writefile.h"

#include "sound.h"
#include "tv-config.h"
#include "commands.h"
#include "devs.h"
#include "parseconfig.h"
#include "tuning.h"
#include "xv.h"
#include "capture.h"
#include "atoms.h"
#include "xt.h"
#include "x11.h"
#include "toolbox.h"
#include "complete.h"
#include "wmhooks.h"
#include "conf.h"
#include "blit.h"
#include "vbi-data.h"
#include "vbi-x11.h"
#include "dvb.h"

#define LABEL_WIDTH         "16"
#define BOOL_WIDTH          "24"

/*--- public variables ----------------------------------------------------*/

static String fallback_ressources[] = {
#include "Xawtv4.h"
    NULL
};

Widget            opt_shell, opt_paned, chan_shell, conf_shell, str_shell;
Widget            launch_shell, launch_paned;
Widget            c_freq,c_cap;
Widget            s_bright,s_color,s_hue,s_contrast,s_volume;
Widget            chan_viewport, chan_box;
Pixmap            tv_pix;
struct vbi_window *vtx;

int               have_config = 0;
XtIntervalId      audio_timer;
int               debug = 0;

char              modename[64];

XtWorkProcId      rec_work_id;

/* movie params / setup */
Widget            w_movie_status;
Widget            w_movie_driver;

Widget            w_movie_fvideo;
Widget            w_movie_video;
Widget            w_movie_fps;
Widget            w_movie_size;

Widget            w_movie_flabel;
Widget            w_movie_faudio;
Widget            w_movie_audio;
Widget            w_movie_rate;

struct STRTAB     *m_movie_driver;
struct STRTAB     *m_movie_audio;
struct STRTAB     *m_movie_video;

struct ng_writer  *movie_driver  = NULL;
unsigned int      i_movie_driver = 0;
unsigned int      movie_audio    = 0;
unsigned int      movie_video    = 0;
unsigned int      movie_fps      = 12000;
unsigned int      movie_rate     = 44100;

static struct STRTAB m_movie_fps[] = {
    {  2000, " 2.0   fps" },
    {  3000, " 3.0   fps" },
    {  5000, " 5.0   fps" },
    {  8000, " 8.0   fps" },
    { 10000, "10.0   fps" },
    { 12000, "12.0   fps" },
    { 15000, "15.0   fps" },
    { 18000, "18.0   fps" },
    { 20000, "20.0   fps" },
    { 23976, "23.976 fps" },
    { 24000, "24.0   fps" },
    { 25000, "25.0   fps" },
    { 29970, "29.970 fps" },
    { 30000, "30.0   fps" },
    { -1, NULL },
};
static struct STRTAB m_movie_rate[] = {
    {   8000, " 8000" },
    {  11025, "11025" },
    {  22050, "22050" },
    {  44100, "44100" },
    {  48000, "48000" },
    { -1, NULL },
};

#if 0
struct xaw_attribute {
    struct ng_attribute   *attr;
    Widget                cmd,scroll;
    struct xaw_attribute  *next;
};
static struct xaw_attribute *xaw_attrs;
#endif

#define MOVIE_DRIVER  "movie driver"
#define MOVIE_AUDIO   "audio format"
#define MOVIE_VIDEO   "video format"
#define MOVIE_FPS     "frames/sec"
#define MOVIE_RATE    "sample rate"
#define MOVIE_SIZE    "video size"

/* fwd decl */
void change_audio(int mode);
void watch_audio(XtPointer data, XtIntervalId *id);

/*-------------------------------------------------------------------------*/

static struct MY_TOPLEVELS {
    char        *name;
    Widget      *shell;
    int         *check;
    int          first;
    int          mapped;
} my_toplevels [] = {
    { "options",  &opt_shell     },
    { "channels", &chan_shell    },
    { "config",   &conf_shell    },
    { "streamer", &str_shell     },
    { "launcher", &launch_shell  }
};
#define TOPLEVELS (sizeof(my_toplevels)/sizeof(struct MY_TOPLEVELS))

struct STRTAB *cmenu = NULL;

struct DO_AC {
    int  argc;
    char *name;
    char *argv[8];
};

/*--- actions -------------------------------------------------------------*/

/* conf.c */
extern void create_confwin(void);
extern void conf_station_switched(void);
extern void conf_list_update(void);

void CloseMainAction(Widget, XEvent*, String*, Cardinal*);
void ScanAction(Widget, XEvent*, String*, Cardinal*);
void ChannelAction(Widget, XEvent*, String*, Cardinal*);
void StayOnTop(Widget, XEvent*, String*, Cardinal*);
void PopupAction(Widget, XEvent*, String*, Cardinal*);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction  },
    { "Scan",        ScanAction },
    { "Channel",     ChannelAction },
    { "Remote",      RemoteAction },
    { "Zap",         ZapAction },
    { "Complete",    CompleteAction },
    { "Help",        help_AC },
    { "StayOnTop",   StayOnTop },
    { "Launch",      LaunchAction },
    { "Popup",       PopupAction },
    { "Command",     CommandAction },
    { "Autoscroll",  offscreen_scroll_AC },
    { "Ratio",       RatioAction },
#ifdef HAVE_ZVBI
    { "Vtx",         VtxAction },
#endif
    { "Event",       EventAction },
};

/*--- exit ----------------------------------------------------------------*/

void
PopupAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    Dimension h;
    unsigned int i;
    int mh;

    /* which window we are talking about ? */
    if (*num_params > 0) {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (0 == strcasecmp(my_toplevels[i].name,params[0]))
		break;
	}
    } else {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (*(my_toplevels[i].shell) == widget)
		break;
	}
    }
    if (i == TOPLEVELS) {
	fprintf(stderr,"PopupAction: oops: shell widget not found (%s)\n",
		(*num_params > 0) ? params[0] : "-");
	return;
    }

    /* Message from WM ??? */
    if (NULL != event && event->type == ClientMessage) {
	if (debug)
	    fprintf(stderr,"%s: received %s message\n",
		    my_toplevels[i].name,
		    XGetAtomName(dpy,event->xclient.data.l[0]));
	if ((Atom)event->xclient.data.l[0] == WM_DELETE_WINDOW) {
	    /* fall throuth -- popdown window */
	} else {
	    /* whats this ?? */
	    return;
	}
    }

    /* check if window should be displayed */
    if (NULL != my_toplevels[i].check)
	if (0 == *(my_toplevels[i].check))
	    return;

    /* popup/down window */
    if (my_toplevels[i].mapped) {
	XtPopdown(*(my_toplevels[i].shell));
	my_toplevels[i].mapped = 0;
    } else {
	Widget shell = *(my_toplevels[i].shell);
	XtPopup(shell, XtGrabNone);
	if (fs)
	    XRaiseWindow(XtDisplay(shell), XtWindow(shell));
	if (wm_stay_on_top && stay_on_top > 0)
	    wm_stay_on_top(dpy,XtWindow(shell),1);
	my_toplevels[i].mapped = 1;
	if (!my_toplevels[i].first) {
	    XSetWMProtocols(XtDisplay(shell),
			    XtWindow(shell),
			    &WM_DELETE_WINDOW, 1);
	    mh = h = 0;
	    XtVaGetValues(shell, XtNmaxHeight,&mh, XtNheight,&h, NULL);
	    if (mh > 0 && h > mh) {
		if (debug)
		    fprintf(stderr,"height fixup: %d => %d\n",h,mh);
		XtVaSetValues(shell, XtNheight,mh, NULL);
	    }
	    my_toplevels[i].first = 1;
	}
    }
}

static void
action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct DO_AC *ca = clientdata;
    XtCallActionProc(widget,ca->name,NULL,ca->argv,ca->argc);
}

void toolkit_set_label(Widget widget, char *str)
{
    if (NULL == str)
	str = "";
    XtVaSetValues(widget,XtNlabel,str,NULL);
}

/*--- videotext ----------------------------------------------------------*/

static void create_vtx(void)
{
    Widget shell,label;

    shell = XtVaCreateWidget("vtx",transientShellWidgetClass,
			     app_shell,
			     XtNoverrideRedirect,True,
			     XtNvisual,vinfo.visual,
			     XtNcolormap,colormap,
			     XtNdepth,vinfo.depth,
			     NULL);
    label = XtVaCreateManagedWidget("label", labelWidgetClass, shell,
				    NULL);
#ifdef HAVE_ZVBI
    vtx = vbi_render_init(shell,label,NULL);
#endif
}

#ifdef HAVE_ZVBI
static void
display_subtitle(struct vbi_page *pg, struct vbi_rect *rect)
{
    static int first = 1;
    static Pixmap pix;
    Dimension x,y,w,h,sw,sh;

    if (NULL == pg) {
	XtPopdown(vtx->shell);
	return;
    }

    if (pix)
	XFreePixmap(dpy,pix);
    pix = vbi_export_pixmap(vtx,pg,rect);
    XtVaSetValues(vtx->tt,XtNbitmap,pix,XtNlabel,NULL,NULL);

    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,NULL);
    XtVaGetValues(vtx->shell,XtNwidth,&sw,XtNheight,&sh,NULL);
    XtVaSetValues(vtx->shell,XtNx,x+(w-sw)/2,XtNy,y+h-10-sh,NULL);
    XtPopup(vtx->shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(vtx->shell),1);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(vtx->shell), left_ptr);
	XDefineCursor(dpy, XtWindow(vtx->tt), left_ptr);
    }
}
#endif

/*--- tv -----------------------------------------------------------------*/

static void
resize_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
#if 0 /* FIXME */
    static int width = 0, height = 0, first = 1;
    char label[64];
    
    switch(event->type) {
    case ConfigureNotify:
	if (first) {
	    video_gd_init(tv,args.xv_image,args.gl);
	    first = 0;
	}
	if (width  != event->xconfigure.width ||
	    height != event->xconfigure.height) {
	    width  = event->xconfigure.width;
	    height = event->xconfigure.height;
	    video_gd_configure(width, height);
	    XClearWindow(XtDisplay(tv),XtWindow(tv));
	    sprintf(label,"%-" LABEL_WIDTH "s: %dx%d",MOVIE_SIZE,width,height);
	    if (w_movie_size)
		XtVaSetValues(w_movie_size,XtNlabel,label,NULL);
	}
	break;
    }
#else
    switch (display_mode) {
    case DISPLAY_OVERLAY:
    case DISPLAY_GRAB:
	/* trigger reconfigure */
	command_pending++;
	break;
    default:
	/* nothing */
	break;
    };
#endif
}

/*------------------------------------------------------------------------*/

#if 0
/* the RightWay[tm] to set float resources (copyed from Xaw specs) */
static void
set_float(Widget widget, char *name, float value)
{
    Arg   args[1];

    if (sizeof(float) > sizeof(XtArgVal)) {
	/*
	 * If a float is larger than an XtArgVal then pass this 
	 * resource value by reference.
	 */
	XtSetArg(args[0], name, &value);
    } else {
        /*
	 * Convince C not to perform an automatic conversion, which
	 * would truncate 0.5 to 0.
	 *
	 * switched from pointer tricks to the union to fix alignment
	 * problems on ia64 (Stephane Eranian <eranian@cello.hpl.hp.com>)
	 */
	union {
	    XtArgVal xt;
	    float   fp;
	} foo;
	foo.fp = value;
	XtSetArg(args[0], name, foo.xt);
    }
    XtSetValues(widget,args,1);
}
#endif

static void
new_freqtab(void)
{
    char label[64];

    if (c_freq) {
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Frequency table",
		freqtab);
	XtVaSetValues(c_freq,XtNlabel,label,NULL);
    }
}

#if 0
static void
new_attr(struct ng_attribute *attr, int val)
{
    struct xaw_attribute *a;
    char label[64],*olabel;
    const char *valstr;

    for (a = xaw_attrs; NULL != a; a = a->next) {
	if (a->attr->id == attr->id)
	    break;
    }
    if (NULL != a) {
	switch (attr->type) {
	case ATTR_TYPE_CHOICE:
	    XtVaGetValues(a->cmd,XtNlabel,&olabel,NULL);
	    valstr = ng_attr_getstr(attr,val);
	    sprintf(label,"%-" LABEL_WIDTH "." LABEL_WIDTH "s: %s",
		    olabel,valstr ? valstr : "unknown");
	    XtVaSetValues(a->cmd,XtNlabel,label,NULL);
	    break;
	case ATTR_TYPE_BOOL:
	    XtVaGetValues(a->cmd,XtNlabel,&olabel,NULL);
	    sprintf(label,"%-" BOOL_WIDTH "." BOOL_WIDTH "s  %s",
		    olabel,val ? "on" : "off");
	    XtVaSetValues(a->cmd,XtNlabel,label,NULL);
	    break;
	case ATTR_TYPE_INTEGER:
	    set_float(a->scroll,XtNtopOfThumb,
		      (float)(val-attr->min) / (attr->max - attr->min));
	    break;
	}
	return;
    }
}
#endif

static void
new_channel(void)
{
    set_property();
    conf_station_switched();
    
    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
    }
    if (scan_timer) {
	XtRemoveTimeOut(scan_timer);
	scan_timer = 0;
    }
    if (audio_timer) {
	XtRemoveTimeOut(audio_timer);
	audio_timer = 0;
    }
}

/*------------------------------------------------------------------------*/

#if 0
static void
do_capture(int from, int to, int tmp_switch)
{
    static int niced = 0;
    char label[64];

    /* off */
    switch (from) {
    case CAPTURE_OFF:
	XtVaSetValues(tv,XtNbackgroundPixmap,XtUnspecifiedPixmap,NULL);
	if (tv_pix)
	    XFreePixmap(dpy,tv_pix);
	tv_pix = 0;
	break;
    case CAPTURE_GRABDISPLAY:
//	video_gd_stop();
	XClearArea(XtDisplay(tv), XtWindow(tv), 0,0,0,0, True);
	break;
    case CAPTURE_OVERLAY:
	video_overlay(0);
	break;
    }

    /* on */
    switch (to) {
    case CAPTURE_OFF:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","off");
	if (!tmp_switch) {
	    tv_pix = x11_capture_pixmap(dpy, &vinfo, colormap, 0, 0);
	    if (tv_pix)
		XtVaSetValues(tv,XtNbackgroundPixmap,tv_pix,NULL);
	}
	break;
    case CAPTURE_GRABDISPLAY:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","grabdisplay");
	if (!niced)
	    nice(niced = 10);
//	video_gd_start();
	break;
    case CAPTURE_OVERLAY:
	sprintf(label,"%-" LABEL_WIDTH "s: %s","Capture","overlay");
	video_overlay(1);
	break;
    }
    if (c_cap)
	XtVaSetValues(c_cap,XtNlabel,label,NULL);
}
#endif

static void
set_menu_val(Widget widget, char *name, struct STRTAB *tab, int val)
{
    char label[64];
    int i;

    for (i = 0; tab[i].str != NULL; i++) {
	if (tab[i].nr == val)
	    break;
    }
    sprintf(label,"%-15s : %s",name,
	    (tab[i].str != NULL) ? tab[i].str : "invalid");
    XtVaSetValues(widget,XtNlabel,label,NULL);
}

void
ChannelAction(Widget widget, XEvent *event,
	      String *params, Cardinal *num_params)
{
    int i;

    if (0 == cfg_sections_count("stations"))
	return;
    i = popup_menu(widget,"Stations",cmenu);

    if (i != -1)
	do_va_cmd(2,"setstation",cfg_sections_index("stations",i));
}

static void create_chanwin(void)
{
    chan_shell = XtVaAppCreateShell("Channels", "Xawtv4",
				    topLevelShellWidgetClass,
				    dpy,
				    XtNclientLeader,app_shell,
				    XtNvisual,vinfo.visual,
				    XtNcolormap,colormap,
				    XtNdepth,vinfo.depth,
		      XtNheight,XtScreen(app_shell)->height/2,
		      XtNmaxHeight,XtScreen(app_shell)->height-50,
				    NULL);
    XtOverrideTranslations(chan_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    chan_viewport = XtVaCreateManagedWidget("viewport",
					    viewportWidgetClass, chan_shell,
					    XtNallowHoriz, False,
					    XtNallowVert, True,
					    NULL);
    chan_box = XtVaCreateManagedWidget("channelbox",
				       boxWidgetClass, chan_viewport,
				       XtNsensitive, True,
				       NULL);
}

static void channel_menu(void)
{
    int  i,max,len,count;
    char *name,*hotkey,str[100],accel[100],ctrl[16],key[32];

    if (cmenu) {
	for (i = 0; cmenu[i].str != NULL; i++)
	    free(cmenu[i].str);
	free(cmenu);
    }

    count = cfg_sections_count("stations");
    cmenu = malloc((count+1)*sizeof(struct STRTAB));
    memset(cmenu,0,(count+1)*sizeof(struct STRTAB));

    max = 0;
    for (name  = cfg_sections_first("stations");
	 name != NULL;
	 name  = cfg_sections_next("stations",name)) {
	len = strlen(name);
	if (max < len)
	    max = len;
    }

    for (name  = cfg_sections_first("stations"), i = 0;
	 name != NULL;
	 name  = cfg_sections_next("stations",name), i++) {
	hotkey = cfg_get_str("stations",name,"key");
	if (hotkey) {
	    sprintf(str,"%2d  %-*s  %s",i+1,max+2,name,key);
	    if (2 == sscanf(hotkey, "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key)) {
		sprintf(accel,"%s<Key>%s: Command(setstation,\"%s\")",
			ctrl,key,name);
	    } else {
		sprintf(accel,"<Key>%s: Command(setstation,\"%s\")",
			hotkey,name);
	    }
	    XtOverrideTranslations(tv,XtParseTranslationTable(accel));
	    XtOverrideTranslations(opt_paned,XtParseTranslationTable(accel));
	} else {
	    sprintf(str,"%2d  %-*s",i+1,max+2,name);
	}
	cmenu[i].nr  = i;
	cmenu[i].str = strdup(str);
    }
}

void
StayOnTop(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    unsigned int i;

    if (!wm_stay_on_top)
	return;

    stay_on_top = stay_on_top ? 0 : 1;
    if (debug)
	fprintf(stderr,"stay_on_top: %d\n",stay_on_top);
	
    wm_stay_on_top(dpy,XtWindow(app_shell),stay_on_top);
    wm_stay_on_top(dpy,XtWindow(on_shell),stay_on_top);
    for (i = 0; i < TOPLEVELS; i++)
	wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),
		       (stay_on_top == -1) ? 0 : stay_on_top);
}

/*--- option window ------------------------------------------------------*/

static void
update_movie_menus(void)
{
#if 0
    struct list_head *item;
    struct ng_writer *writer;
    Boolean sensitive;
    unsigned int i;

    /* drivers  */
    if (NULL == m_movie_driver) {
	i = 0;
	list_for_each(item,&ng_writers)
	    i++;
	m_movie_driver = malloc(sizeof(struct STRTAB)*(i+1));
	memset(m_movie_driver,0,sizeof(struct STRTAB)*(i+1));
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    m_movie_driver[i].nr  = i;
	    m_movie_driver[i].str = writer->desc;
	    if (NULL == movie_driver ||
		(NULL != mov_driver && 0 == strcasecmp(mov_driver,writer->name))) {
		movie_driver = writer;
		i_movie_driver = i;
	    }
	    i++;
	}
	m_movie_driver[i].nr  = i;
	m_movie_driver[i].str = NULL;
    }

    /* audio formats */
    for (i = 0; NULL != movie_driver->audio[i].name; i++)
	;
    if (m_movie_audio)
	free(m_movie_audio);
    movie_audio = 0;
    m_movie_audio = malloc(sizeof(struct STRTAB)*(i+2));
    memset(m_movie_audio,0,sizeof(struct STRTAB)*(i+2));
    for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	m_movie_audio[i].nr  = i;
	m_movie_audio[i].str = movie_driver->audio[i].desc ?
	    movie_driver->audio[i].desc : 
	    ng_afmt_to_desc[movie_driver->audio[i].fmtid];
	if (NULL != mov_audio)
	    if (0 == strcasecmp(mov_audio,movie_driver->audio[i].name))
		movie_audio = i;
    }
    m_movie_audio[i].nr  = i;
    m_movie_audio[i].str = "no sound";

    /* video formats */
    for (i = 0; NULL != movie_driver->video[i].name; i++)
	;
    if (m_movie_video)
	free(m_movie_video);
    movie_video = 0;
    m_movie_video = malloc(sizeof(struct STRTAB)*(i+2));
    memset(m_movie_video,0,sizeof(struct STRTAB)*(i+2));
    for (i = 0; NULL != movie_driver->video[i].name; i++) {
	m_movie_video[i].nr  = i;
	m_movie_video[i].str = movie_driver->video[i].desc ?
	    movie_driver->video[i].desc : 
	    ng_vfmt_to_desc[movie_driver->video[i].fmtid];
	if (NULL != mov_video)
	    if (0 == strcasecmp(mov_video,movie_driver->video[i].name))
		movie_video = i;
    }

    /* need audio filename? */
    sensitive = movie_driver->combined ? False : True;
    XtVaSetValues(w_movie_flabel,
		  XtNsensitive,sensitive,
		  NULL);
    XtVaSetValues(w_movie_faudio,
		  XtNsensitive,sensitive,
		  NULL);
#endif
}

static void
init_movie_menus(void)
{
#if 0
    update_movie_menus();

    if (mov_rate)
	do_va_cmd(3,"movie","rate",mov_rate);
    if (mov_fps)
	do_va_cmd(3,"movie","fps",mov_fps);
    set_menu_val(w_movie_driver,MOVIE_DRIVER,m_movie_driver,i_movie_driver);
    set_menu_val(w_movie_audio,MOVIE_AUDIO,m_movie_audio,movie_audio);
    set_menu_val(w_movie_rate,MOVIE_RATE,m_movie_rate,movie_rate);
    set_menu_val(w_movie_video,MOVIE_VIDEO,m_movie_video,movie_video);
    set_menu_val(w_movie_fps,MOVIE_FPS,m_movie_fps,movie_fps);
#endif
}

#define PANED_FIX               \
        XtNallowResize, False,  \
        XtNshowGrip,    False,  \
        XtNskipAdjust,  True

struct command cmd_fs   = { 1, { "fullscreen",        NULL }};
struct command cmd_mute = { 2, { "volume",  "mute",   NULL }};
struct command cmd_cap  = { 2, { "capture", "toggle", NULL }};
struct command cmd_jpeg = { 2, { "snap",    "jpeg",   NULL }};
struct command cmd_ppm  = { 2, { "snap",    "ppm",    NULL }};

struct DO_AC  ac_fs    = { 0, "FullScreen", { NULL }};
struct DO_AC  ac_top   = { 0, "StayOnTop",  { NULL }};

struct DO_AC  ac_avi   = { 1, "Popup",      { "streamer", NULL }};
struct DO_AC  ac_chan  = { 1, "Popup",      { "channels", NULL }};
struct DO_AC  ac_conf  = { 1, "Popup",      { "config",   NULL }};
struct DO_AC  ac_launch = { 1, "Popup",      { "launcher",  NULL }};
struct DO_AC  ac_zap   = { 0, "Zap",        { NULL }};

static void
menu_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
#if 0 /* FIXME */
    struct ng_attribute *attr;
#endif
    long  cd = (long)clientdata;
    int   j;

    switch (cd) {
#if 0
    case 10:
	attr = ng_attr_byid(a_drv,ATTR_ID_NORM);
	if (-1 != (j=popup_menu(widget,"TV Norm",attr->choices)))
	    do_va_cmd(2,"setnorm",ng_attr_getstr(attr,j));
	break;
    case 11:
	attr = ng_attr_byid(a_drv,ATTR_ID_INPUT);
	if (-1 != (j=popup_menu(widget,"Video Source",attr->choices)))
	    do_va_cmd(2,"setinput",ng_attr_getstr(attr,j));
	break;
    case 12:
	if (-1 != (j=popup_menu(widget,"Freq table",chanlist_names)))
	    do_va_cmd(2,"setfreqtab",chanlist_names[j].str);
	break;
    case 13:
	attr = ng_attr_byid(attrs,ATTR_ID_AUDIO_MODE);
	if (NULL != attr) {
	    int i,mode = attr->read(attr);
	    for (i = 1; attr->choices[i].str != NULL; i++) {
		attr->choices[i].nr =
		    (1 << (i-1)) & mode ? (1 << (i-1)) : -1;
	    }
	    if (-1 != (j=popup_menu(widget,"Audio",attr->choices)))
		change_audio(attr->choices[j].nr);
	}
	break;
    case 14:
	if (-1 != (j=popup_menu(widget,"Capture",cap_list)))
	    do_va_cmd(2,"capture",cap_list[j].str);
	break;
#endif

    case 20:
	if (-1 != (j=popup_menu(widget,MOVIE_DRIVER,m_movie_driver))) {
	    int i = 0;
	    struct list_head *item;
	    struct ng_writer *writer = NULL;
	    
	    list_for_each(item,&ng_writers) {
		if (i++ == j)
		    writer = list_entry(item,struct ng_writer, list);
	    }
	    do_va_cmd(3,"movie","driver",writer->name);
	}
	break;
    case 21:
	if (-1 != (j=popup_menu(widget,MOVIE_AUDIO,m_movie_audio)))
	    do_va_cmd(3,"movie","audio",
		      movie_driver->audio[j].name ?
		      movie_driver->audio[j].name :
		      "none");
	break;
    case 22:
	if (-1 != (j=popup_menu(widget,MOVIE_RATE,m_movie_rate)))
	    do_va_cmd(3,"movie","rate",m_movie_rate[j].str);
	break;
    case 23:
	if (-1 != (j=popup_menu(widget,MOVIE_VIDEO,m_movie_video)))
	    do_va_cmd(3,"movie","video",movie_driver->video[j].name);
	break;
    case 24:
	if (-1 != (j=popup_menu(widget,MOVIE_FPS,m_movie_fps)))
	    do_va_cmd(3,"movie","fps",m_movie_fps[j].str);
	break;
    default:
	/* nothing */
	break;
    }
}

#if 0
static void
jump_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct xaw_attribute *a = clientdata;
    struct ng_attribute *attr = a->attr;
    const char *name;
    char val[16];
    int  value,range;

    range = attr->max - attr->min;
    value = (int)(*(float*)call_data * range) + attr->min;
    if (debug)
	fprintf(stderr,"scroll: value is %d\n",value);
    if (value < attr->min)
	value = attr->min;
    if (value > attr->max)
	value = attr->max;
    sprintf(val,"%d",value);

    if (a) {
	name = a->attr->name;
	do_va_cmd(3,"setattr",name,val);
    } else {
	name = XtName(XtParent(widget));
	do_va_cmd(2,name,val);
    }
}

static void
scroll_scb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    long      move = (long)call_data;
    Dimension length;
    float     shown,top1,top2;

    XtVaGetValues(widget,
		  XtNlength,     &length,
		  XtNshown,      &shown,
		  XtNtopOfThumb, &top1,
		  NULL);

    top2 = top1 + (float)move/length/5;
    if (top2 < 0) top2 = 0;
    if (top2 > 1) top2 = 1;
#if 1
    fprintf(stderr,"scroll by %ld\tlength %d\tshown %f\ttop %f => %f\n",
	    move,length,shown,top1,top2);
#endif
    jump_scb(widget,clientdata,&top2);
}

static void
attr_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct xaw_attribute *a = clientdata;
    int j;

    switch (a->attr->type) {
    case ATTR_TYPE_CHOICE:
	j=popup_menu(widget,a->attr->name,a->attr->choices);
	if (-1 != j)
	    do_va_cmd(3,"setattr",a->attr->name,a->attr->choices[j].str);
	break;
    case ATTR_TYPE_BOOL:
	do_va_cmd(3,"setattr",a->attr->name,"toggle");
	break;
    }
}
#endif

#if 0
static void
add_attr_option(Widget paned, struct ng_attribute *attr)
{
    struct xaw_attribute *a;
    Widget p,l;

    a = malloc(sizeof(*a));
    memset(a,0,sizeof(*a));
    a->attr = attr;
    
    switch (attr->type) {
    case ATTR_TYPE_BOOL:
    case ATTR_TYPE_CHOICE:
	a->cmd = XtVaCreateManagedWidget(attr->name,
					 commandWidgetClass, paned,
					 PANED_FIX,
					 NULL);
	XtAddCallback(a->cmd,XtNcallback,attr_cb,a);
	break;
    case ATTR_TYPE_INTEGER:
	p = XtVaCreateManagedWidget(attr->name,
				    panedWidgetClass, paned,
				    XtNorientation, XtEvertical,
				    PANED_FIX,
				    NULL);
	l = XtVaCreateManagedWidget("l",labelWidgetClass, p,
				    XtNshowGrip, False,
				    NULL);
	a->scroll = XtVaCreateManagedWidget("s",scrollbarWidgetClass,p,
					    PANED_FIX,
					    NULL);
	XtAddCallback(a->scroll, XtNjumpProc,   jump_scb,   a);
	XtAddCallback(a->scroll, XtNscrollProc, scroll_scb, a);
	if (attr->id >= ATTR_ID_COUNT)
	    XtVaSetValues(l,XtNlabel,attr->name,NULL);
	break;
    }
    a->next = xaw_attrs;
    xaw_attrs = a;
}
#endif

static void
create_optwin(void)
{
    Widget c;

    opt_shell = XtVaAppCreateShell("Options", "Xawtv4",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   NULL);
    XtOverrideTranslations(opt_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    opt_paned = XtVaCreateManagedWidget("paned", panedWidgetClass, opt_shell,
					NULL);
    
    c = XtVaCreateManagedWidget("mute", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_mute);
    
    c = XtVaCreateManagedWidget("fs", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_fs);

    c = XtVaCreateManagedWidget("grabppm", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_ppm);
    c = XtVaCreateManagedWidget("grabjpeg", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,command_cb,(XtPointer)&cmd_jpeg);
    c = XtVaCreateManagedWidget("recavi", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_avi);
    c = XtVaCreateManagedWidget("chanwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_chan);
    c = XtVaCreateManagedWidget("confwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_conf);
    c = XtVaCreateManagedWidget("launchwin", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_launch);
    c = XtVaCreateManagedWidget("zap", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_zap);
    if (wm_stay_on_top) {
	c = XtVaCreateManagedWidget("top", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
	XtAddCallback(c,XtNcallback,action_cb,(XtPointer)&ac_top);
    }
}

#if 0
static void
create_attr_widgets(void)
{
    Widget c;
#if 0 /* FIXME */
    struct ng_attribute *attr;

    /* menus / multiple choice options */
    attr = ng_attr_byid(attrs,ATTR_ID_NORM);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_INPUT);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_AUDIO_MODE);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);

    if (f_drv & CAN_TUNE) {
	c_freq = XtVaCreateManagedWidget("freq", commandWidgetClass, opt_paned,
					 PANED_FIX, NULL);
	XtAddCallback(c_freq,XtNcallback,menu_cb,(XtPointer)12);
    }
    c_cap = XtVaCreateManagedWidget("cap", commandWidgetClass, opt_paned,
				    PANED_FIX, NULL);
    XtAddCallback(c_cap,XtNcallback,menu_cb,(XtPointer)14);

    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_CHOICE)
	    continue;
	add_attr_option(opt_paned,attr);
    }
    
    /* integer options */
    attr = ng_attr_byid(attrs,ATTR_ID_BRIGHT);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_HUE);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_CONTRAST);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_COLOR);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);
    attr = ng_attr_byid(attrs,ATTR_ID_VOLUME);
    if (NULL != attr)
	add_attr_option(opt_paned,attr);

    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_INTEGER)
	    continue;
	add_attr_option(opt_paned,attr);
    }

    /* boolean options */
    for (attr = attrs; attr->name != NULL; attr++) {
	if (attr->id < ATTR_ID_COUNT)
	    continue;
	if (attr->type != ATTR_TYPE_BOOL)
	    continue;
	add_attr_option(opt_paned,attr);
    }
#endif

    /* quit */
    c = XtVaCreateManagedWidget("quit", commandWidgetClass, opt_paned,
				PANED_FIX, NULL);
    XtAddCallback(c,XtNcallback,ExitCB,NULL);
}
#endif

/*--- avi recording ------------------------------------------------------*/

static void
exec_record(Widget widget, XtPointer client_data, XtPointer calldata)
{
    if (!(devs.video.flags & CAN_CAPTURE)) {
	fprintf(stderr,"grabbing: not supported\n");
	return;
    }

    if (rec_work_id) {
	do_va_cmd(2,"movie","stop");
    } else {
	do_va_cmd(2,"movie","start");
    }
    return;
}

static void
exec_player_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    char *filename;

    XtVaGetValues(w_movie_fvideo,XtNstring,&filename,NULL);
    filename = tilde_expand(filename);
    exec_player(filename);
}

static void
do_movie_record(int argc, char **argv)
{
    char *fvideo,*faudio;
    struct ng_video_fmt video;
    struct ng_audio_fmt audio;
    const struct ng_writer *wr;
    int i;

    /* set parameters */
    if (argc > 1 && 0 == strcasecmp(argv[0],"driver")) {
	struct list_head *item;
	struct ng_writer *writer;
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    if (0 == strcasecmp(argv[1],writer->name)) {
		movie_driver = writer;
		i_movie_driver = i;
	    }
	    i++;
	}

	set_menu_val(w_movie_driver,MOVIE_DRIVER,
		     m_movie_driver,i_movie_driver);
	update_movie_menus();
	set_menu_val(w_movie_audio,MOVIE_AUDIO,
		     m_movie_audio,movie_audio);
	set_menu_val(w_movie_video,MOVIE_VIDEO,
		     m_movie_video,movie_video);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fvideo")) {
	XtVaSetValues(w_movie_fvideo,XtNstring,argv[1],NULL);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"faudio")) {
	XtVaSetValues(w_movie_faudio,XtNstring,argv[1],NULL);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"audio")) {
	for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	    if (0 == strcasecmp(argv[1],movie_driver->audio[i].name))
		movie_audio = m_movie_audio[i].nr;
	}
	if (0 == strcmp(argv[1],"none"))
	    movie_audio = m_movie_audio[i].nr;
	set_menu_val(w_movie_audio,MOVIE_AUDIO,
		     m_movie_audio,movie_audio);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"rate")) {
	for (i = 0; m_movie_rate[i].str != NULL; i++)
	    if (atoi(argv[1]) == m_movie_rate[i].nr)
		movie_rate = m_movie_rate[i].nr;
	set_menu_val(w_movie_rate,MOVIE_RATE,
		     m_movie_rate,movie_rate);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"video")) {
	for (i = 0; NULL != movie_driver->video[i].name; i++)
	    if (0 == strcasecmp(argv[1],movie_driver->video[i].name))
		movie_video = m_movie_video[i].nr;
	set_menu_val(w_movie_video,MOVIE_VIDEO,
		     m_movie_video,movie_video);
	return;
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fps")) {
	for (i = 0; m_movie_fps[i].str != NULL; i++) {
	    int fps = (int)(atof(argv[1]) * 1000 + .5);
	    if (fps == m_movie_fps[i].nr)
		movie_fps = m_movie_fps[i].nr;
	}
	set_menu_val(w_movie_fps,MOVIE_FPS,
		     m_movie_fps,movie_fps);
    }

    /* start */
    if (argc > 0 && 0 == strcasecmp(argv[0],"start")) {
	if (0 != cur_movie)
	    return; /* records already */
	cur_movie = 1;
//	movie_blit = (cur_capture == CAPTURE_GRABDISPLAY);
//	video_gd_suspend();

	XtVaGetValues(w_movie_fvideo,XtNstring,&fvideo,NULL);
	XtVaGetValues(w_movie_faudio,XtNstring,&faudio,NULL);
	fvideo = tilde_expand(fvideo);
	faudio = tilde_expand(faudio);

	memset(&video,0,sizeof(video));
	memset(&audio,0,sizeof(audio));

	wr = movie_driver;
	video.fmtid  = wr->video[movie_video].fmtid;
	video.width  = cur_tv_width;
	video.height = cur_tv_height;
	if (NULL != wr->audio[movie_audio].name) {
	    audio.fmtid  = wr->audio[movie_audio].fmtid;
	    audio.rate   = movie_rate;
	} else {
	    audio.fmtid  = AUDIO_NONE;
	}

	movie_state = movie_writer_init
	    (fvideo, faudio, wr,
	     &devs.video, &devs.sndrec,
	     &video, wr->video[movie_video].priv,
	     &audio, wr->audio[movie_audio].priv,
	     movie_fps,
	     GET_REC_BUFCOUNT(),
	     GET_REC_THREADS());
	if (NULL == movie_state) {
	    /* init failed */
//	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    XtVaSetValues(w_movie_status,XtNlabel,"error [init]",NULL);
	    return;
	}
	if (0 != movie_writer_start(movie_state)) {
	    /* start failed */
	    movie_writer_stop(movie_state);
//	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    XtVaSetValues(w_movie_status,XtNlabel,"error [start]",NULL);
	    return;
	}
	rec_work_id  = XtAppAddWorkProc(app_context,rec_work,NULL);
	XtVaSetValues(w_movie_status,XtNlabel,"recording",NULL);
	return;
    }
    
    /* stop */
    if (argc > 0 && 0 == strcasecmp(argv[0],"stop")) {
	if (0 == cur_movie)
	    return; /* nothing to stop here */

	movie_writer_stop(movie_state);
	XtRemoveWorkProc(rec_work_id);
	rec_work_id = 0;
//	video_gd_restart();
	cur_movie = 0;
	return;
    }
}

static void
do_rec_status(char *message)
{
    XtVaSetValues(w_movie_status,XtNlabel,message,NULL);
}

#define FIX_LEFT_TOP        \
    XtNleft,XawChainLeft,   \
    XtNright,XawChainRight, \
    XtNtop,XawChainTop,     \
    XtNbottom,XawChainTop

static void
create_strwin(void)
{
    Widget form,label,button,text;

    str_shell = XtVaAppCreateShell("Streamer", "Xawtv4",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   NULL);
    XtOverrideTranslations(str_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));

    form = XtVaCreateManagedWidget("form", formWidgetClass, str_shell,
                                   NULL);

    /* driver */
    button = XtVaCreateManagedWidget("driver", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     NULL);
    w_movie_driver = button;

    /* movie filename */
    label = XtVaCreateManagedWidget("vlabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, button,
				    NULL);
    text = XtVaCreateManagedWidget("vname", asciiTextWidgetClass, form,
				   FIX_LEFT_TOP,
				   XtNfromVert, label,
				   NULL);
    w_movie_fvideo = text;
    
    /* audio filename */
    label = XtVaCreateManagedWidget("alabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, text,
				    NULL);
    w_movie_flabel = label;
    text= XtVaCreateManagedWidget("aname", asciiTextWidgetClass, form,
				  FIX_LEFT_TOP,
				  XtNfromVert, label,
				  NULL);
    w_movie_faudio = text;

    /* audio format */
    button = XtVaCreateManagedWidget("audio", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, text,
				     NULL);
    w_movie_audio = button;
    button = XtVaCreateManagedWidget("rate", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_rate = button;

    /* video format */
    button = XtVaCreateManagedWidget("video", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_video = button;
    button = XtVaCreateManagedWidget("fps", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    w_movie_fps = button;
    label = XtVaCreateManagedWidget("size", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, button,
				    NULL);
    w_movie_size = label;

    /* status line */
    label = XtVaCreateManagedWidget("status", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert, label,
				    XtNlabel,    "",
				    NULL);
    w_movie_status = label;

    /* cmd buttons */
    button = XtVaCreateManagedWidget("streamer", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, label,
				     NULL);
    XtAddCallback(button,XtNcallback,exec_record,NULL);
    
    button = XtVaCreateManagedWidget("xanim", commandWidgetClass, form,
				     FIX_LEFT_TOP,
				     XtNfromVert, button,
				     NULL);
    XtAddCallback(button,XtNcallback,exec_player_cb,NULL);

#if 0
    label = XtVaCreateManagedWidget("olabel", labelWidgetClass, form,
				    FIX_LEFT_TOP,
				    XtNfromVert,button,
				    NULL);
    str_text = XtVaCreateManagedWidget("output", asciiTextWidgetClass, form,
				       XtNleft,XawChainLeft,
				       XtNright,XawChainRight,
				       XtNtop,XawChainTop,
				       XtNbottom,XawChainBottom,
				       XtNfromVert,label,
				       NULL);
#endif

    XtAddCallback(w_movie_driver,XtNcallback,menu_cb,(XtPointer)20);
    XtAddCallback(w_movie_audio,XtNcallback,menu_cb,(XtPointer)21);
    XtAddCallback(w_movie_rate,XtNcallback,menu_cb,(XtPointer)22);
    XtAddCallback(w_movie_video,XtNcallback,menu_cb,(XtPointer)23);
    XtAddCallback(w_movie_fps,XtNcallback,menu_cb,(XtPointer)24);
}

/*--- launcher window -----------------------------------------------------*/

static void
create_launchwin(void)
{
    launch_shell = XtVaAppCreateShell("Launcher", "Xawtv4",
				     topLevelShellWidgetClass,
				     dpy,
				     XtNclientLeader,app_shell,
				     XtNvisual,vinfo.visual,
				     XtNcolormap,colormap,
				     XtNdepth,vinfo.depth,
				     NULL);
    XtOverrideTranslations(launch_shell, XtParseTranslationTable
			   ("<Message>WM_PROTOCOLS: Popup()"));
    launch_paned = XtVaCreateManagedWidget("paned", panedWidgetClass,
					  launch_shell, NULL);
}

/*--- main ---------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
#if 0
    int            i;
    unsigned long  freq;
#endif

    /* toplevel */
    XtSetLanguageProc(NULL,NULL,NULL);
    app_shell = XtVaAppInitialize(&app_context, "Xawtv4",
				  opt_desc, opt_count,
				  &argc, argv,
				  fallback_ressources,
				  NULL);
    dpy = XtDisplay(app_shell);
    init_atoms(dpy);
    ng_init();
    hello_world("xawtv");

    /* handle command line args, read config file */
    handle_cmdline_args(&argc,argv);

    /* set hooks (command.c) */
    update_title        = new_title;
    display_message     = new_message;
#ifdef HAVE_ZVBI
    vtx_subtitle        = display_subtitle;
#endif
    freqtab_notify      = new_freqtab;
    setstation_notify   = new_channel;
    fullscreen_hook     = do_fullscreen;
    movie_hook          = do_movie_record;
    rec_status          = do_rec_status;
    exit_hook           = do_exit;
#if 0 /* FIXME */
    set_capture_hook    = do_capture;
    capture_get_hook    = video_gd_suspend;
    capture_rel_hook    = video_gd_restart;
#endif

    /* look for a useful visual */
    visual_init("xawtv","Xawtv4");

    /* x11 stuff */
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    x11_misc_init(dpy);
    if (debug)
	fprintf(stderr,"main: xinerama extention...\n");
    xfree_xinerama_init(dpy);
#if 0 /* def HAVE_LIBXV */
    if (debug)
	fprintf(stderr,"main: xvideo extention [video]...\n");
    if (args.xv_video)
	xv_video_init(args.xv_port,0);
#endif
        
    if (debug)
	fprintf(stderr,"main: init main window...\n");
    tv = XtVaCreateManagedWidget("tv",simpleWidgetClass,app_shell,NULL);
    XtAddEventHandler(XtParent(tv),StructureNotifyMask, True,
		      resize_event, NULL);
    if (debug)
	fprintf(stderr,"main: install signal handlers...\n");
    xt_siginit();

    /* create windows */
    XSetIOErrorHandler(x11_ctrl_alt_backspace);
    if (debug)
	fprintf(stderr,"main: checking wm...\n");
    wm_detect(dpy);
    if (debug)
	fprintf(stderr,"main: creating windows ...\n");
    create_optwin();
    create_onscreen(labelWidgetClass);
    create_vtx();
    create_chanwin();
    create_confwin();
    create_strwin();
    create_launchwin();

    init_movie_menus();
    
    /* finalize X11 init + show windows */
    xt_input_init(dpy);
    if (debug)
	fprintf(stderr,"main: mapping main window ...\n");
    XtRealizeWidget(app_shell);
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);
    XSetWMProtocols(XtDisplay(app_shell), XtWindow(app_shell),
		    &WM_DELETE_WINDOW, 1);

    XtVaSetValues(app_shell,
		  XtNwidthInc,  WIDTH_INC,
		  XtNheightInc, HEIGHT_INC,
		  XtNminWidth,  WIDTH_INC,
		  XtNminHeight, HEIGHT_INC,
		  NULL);
    XtVaSetValues(chan_shell, XtNwidth,
		  GET_PIX_WIDTH() * GET_PIX_COLS() + 30, NULL);

    /* mouse pointer magic */
    XtAddEventHandler(tv, PointerMotionMask, True, mouse_event, NULL);
    mouse_event(tv,NULL,NULL,NULL);

#ifdef HAVE_DVB
    if (0 == cfg_sections_count("stations") &&
	0 != cfg_sections_count("dvb") &&
	NULL != cfg_get_str("devs",cfg_sections_first("devs"),"dvb")) {
	/* easy start for dvb users, import vdr's list ... */
	vdr_import_stations();
    }
#endif

    /* build channel list */
    if (args.readconfig) {
	if (debug)
	    fprintf(stderr,"main: parse channels from config file ...\n");
	apply_config();
    }
    channel_menu();

    if (optind+1 == argc)
	do_va_cmd(2,"setstation",argv[optind]);

    XtAddEventHandler(tv,ExposureMask, True, tv_expose_event, NULL);
    if (args.fullscreen) {
	XSync(dpy,False);
	do_fullscreen();
    }

    xt_main_loop();
    return 0;
}
