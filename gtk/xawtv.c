/*
 * main.c for xawtv -- a TV application
 *
 *   (c) 1997-2004 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "blit.h"
#include "devs.h"
#include "dvb.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "commands.h"
#include "av-sync.h"
#include "gui.h"

/* ------------------------------------------------------------------------ */

/* misc globals */
int debug = 0;

/* video window */
static GtkWidget *video;

/* x11 stuff */
static Display *dpy;
static Visual *visual;
static XVisualInfo vinfo;

/* ------------------------------------------------------------------------ */

#define O_CMDLINE               "cmdline", "cmdline"

#define O_CMD_HELP             	O_CMDLINE, "help"
#define O_CMD_VERSION           O_CMDLINE, "version"
#define O_CMD_VERBOSE          	O_CMDLINE, "verbose"
#define O_CMD_DEBUG	       	O_CMDLINE, "debug"

#define O_CMD_READCONF	       	O_CMDLINE, "readconf"
#define O_CMD_WRITECONF	       	O_CMDLINE, "writeconf"

#define O_CMD_HW_LS	       	O_CMDLINE, "hw-ls"
#define O_CMD_HW_CONFIG	       	O_CMDLINE, "hw-config"
#define O_CMD_HW_STORE	       	O_CMDLINE, "hw-store"

#define GET_CMD_HELP()		cfg_get_bool(O_CMD_HELP,   	0)
#define GET_CMD_VERSION()       cfg_get_bool(O_CMD_VERSION,   	0)
#define GET_CMD_VERBOSE()	cfg_get_bool(O_CMD_VERBOSE,   	0)
#define GET_CMD_DEBUG()		cfg_get_bool(O_CMD_DEBUG,   	0)

#define GET_CMD_READCONF()     	cfg_get_bool(O_CMD_READCONF,   	1)
#define GET_CMD_WRITECONF()    	cfg_get_bool(O_CMD_WRITECONF,   0)

#define GET_CMD_HW_LS()    	cfg_get_bool(O_CMD_HW_LS,       0)
#define GET_CMD_HW_CONFIG()    	cfg_get_bool(O_CMD_HW_CONFIG,   0)
#define GET_CMD_HW_STORE()    	cfg_get_bool(O_CMD_HW_STORE,    0)

struct cfg_cmdline cmd_opts_only[] = {
    {
	.letter   = 'h',
	.cmdline  = "help",
	.option   = { O_CMD_HELP },
	.value    = "1",
	.desc     = "print this text",
    },{
	.cmdline  = "version",
	.option   = { O_CMD_VERSION },
	.value    = "1",
	.desc     = "print version and exit",
    },{
	.cmdline  = "verbose",
	.option   = { O_CMD_VERBOSE },
	.value    = "1",
	.desc     = "be verbose",
    },{
	.cmdline  = "debug",
	.option   = { O_CMD_DEBUG },
	.needsarg = 1,
	.desc     = "enable debug output",

    },{
	.cmdline  = "noconf",
	.option   = { O_CMD_READCONF },
	.value    = "0",
	.desc     = "don't read the config file",
    },{
	.cmdline  = "store",
	.option   = { O_CMD_WRITECONF },
	.value    = "1",
	.desc     = "write cmd line args to config file",

    },{
	.cmdline  = "hwls",
	.option   = { O_CMD_HW_LS },
	.value    = "1",
	.desc     = "list all devices found",
    },{
	.cmdline  = "hwconfig",
	.option   = { O_CMD_HW_CONFIG },
	.value    = "1",
	.desc     = "show hardware configurations",
    },{
	.cmdline  = "hwstore",
	.option   = { O_CMD_HW_STORE },
	.value    = "1",
	.desc     = "write hardware configuration to config file",

    },{
	/* end of list */
    }
};

/* ------------------------------------------------------------------------ */

static void
grabber_init(char *dev)
{
    char *val;

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
	if (devs.video.flags & CAN_OVERLAY)
	    display_mode = DISPLAY_OVERLAY;

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

static void
grabber_fini(void)
{
    audio_off();
    device_fini();
}

/* ------------------------------------------------------------------------ */

static void
grabdisplay_loop(GMainContext *context, GtkWidget *widget, struct blit_handle *blit)
{
    struct media_stream mm;

    if (debug) fprintf(stderr,"%s: enter\n",__FUNCTION__);
    
    memset(&mm,0,sizeof(mm));
    mm.blit = blit;
    mm.speed = 0;
    
    /* video setup */
    ng_dev_open(&devs.video);
    av_media_grab_video(&mm,widget);
    
    /* go playback stuff */
    if (mm.vs) {
	devs.video.v->startvideo(devs.video.handle,-1,2);
	av_media_mainloop(context, &mm);
	devs.video.v->stopvideo(devs.video.handle);
    }

    /* cleanup */
    BUG_ON(NULL != mm.as,"mm.as isn't NULL");
    BUG_ON(NULL != mm.vs,"mm.vs isn't NULL");
    ng_dev_close(&devs.video);

    if (debug) fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

static void
overlay_loop(GMainContext *context, GtkWidget *widget)
{
    Window win = gdk_x11_drawable_get_xid(widget->window);
    int x,y, width, height, depth;

    if (debug) fprintf(stderr,"%s: enter\n",__FUNCTION__);

    /* video setup */
    ng_dev_open(&devs.video);
    
    /* go start overlay */
    gdk_window_get_geometry(widget->window, &x, &y, &width, &height, &depth);
    devs.video.v->overlay(devs.video.handle,1,1, win, width, height);
    while (!command_pending)
	g_main_context_iteration(context,TRUE);
    devs.video.v->overlay(devs.video.handle,0,1, win, width, height);

    /* cleanup */
    ng_dev_close(&devs.video);

    if (debug) fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

#ifdef HAVE_DVB

static void
dvb_loop(GMainContext *context, GtkWidget *widget, struct blit_handle *blit)
{
    char path[64];
    struct media_stream mm;
    struct ng_audio_fmt *afmt;
    struct ng_video_fmt *vfmt;
    
    if (debug) fprintf(stderr,"%s: enter\n",__FUNCTION__);

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
    av_media_mainloop(context, &mm);

    /* cleanup */
    BUG_ON(NULL != mm.as,"mm.as isn't NULL");
    BUG_ON(NULL != mm.vs,"mm.vs isn't NULL");
    mm.reader->rd_close(mm.rhandle);

    if (debug) fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

#endif

static int main_loop(GMainContext *context)
{
    struct blit_handle *blit;

    /* init */
    if (debug)
	fprintf(stderr,"main: open grabber device...\n");
    grabber_init(NULL);
    blit = blit_init(video, &vinfo, GET_EXT_XVIDEO(), GET_EXT_OPENGL());

    /* mainloop */
    if (debug)
	fprintf(stderr,"main: enter main event loop... \n");
    for (;;) {
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
	while (g_main_context_iteration(context,FALSE))
	    /* nothing */;

	/* media display */
	switch (display_mode) {
	case DISPLAY_NONE:
	    if (debug)
		fprintf(stderr,"display mode \"none\"\n");
	    break;
	case DISPLAY_OVERLAY:
	    if (devs.video.type != NG_DEV_NONE)
		overlay_loop(context,video);
	    break;
	case DISPLAY_GRAB:
	    if (devs.video.type != NG_DEV_NONE)
		grabdisplay_loop(context,video,blit);
	    break;
#ifdef HAVE_DVB
	case DISPLAY_DVB:
	    if (devs.dvb)
		dvb_loop(context,video,blit);
	    break;
#endif
	default:
	    fprintf(stderr,"unknown/unimplemented display mode %d\n",display_mode);
	    break;
	}

	/* default/error catch */
	while (!command_pending)
	    g_main_context_iteration(context,TRUE);
    }

    /* cleanup */
    blit_fini(blit);
    grabber_fini();
    return 0;
}

/* ------------------------------------------------------------------------ */

static gboolean mouse_button(GtkWidget *widget,
			     GdkEventButton *event,
			     gpointer user_data)
{
    if (NULL != control_win  &&  3 == event->button) {
	if (GTK_WIDGET_VISIBLE(control_win))
	    gtk_widget_hide(control_win);
	else
	    gtk_widget_show_all(control_win);
	return TRUE;
    }
    if (NULL != control_st_menu  &&  1 == event->button) {
	gtk_widget_show_all(control_st_menu);
	gtk_menu_popup(GTK_MENU(control_st_menu),
		       NULL, NULL, NULL, NULL,
		       event->button, event->time);
	return TRUE;
    }
    return FALSE;
}

static gboolean resize(GtkWidget *widget,
		       GdkEventConfigure *event,
		       gpointer user_data)
{
    static int ww,wh;
    
    if (event->width == ww && event->height == wh)
	return 0;
    ww = event->width;
    wh = event->height;

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
    return 0;
}

/* ------------------------------------------------------------------------ */
/* main function and related helpers                                        */

static void
hello_world(void)
{
    struct utsname uts;

    uname(&uts);
    fprintf(stderr,"This is xawtv %s, running on %s/%s (%s)\n",
	    VERSION,uts.sysname,uts.machine,uts.release);

    if (0 == geteuid() && 0 != getuid()) {
	fprintf(stderr,"xawtv *must not* be installed suid root\n");
	exit(1);
    }
}

static void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: xawtv [ options ] [ station ]\n"
	    "options:\n");

    cfg_help_cmdline(cmd_opts_only,2,16,0);
    fprintf(stderr,"\n");

    cfg_help_cmdline(cmd_opts_devices,2,16,40);
    fprintf(stderr,"\n");

    cfg_help_cmdline(cmd_opts_x11,2,16,40);
    fprintf(stderr,"\n");

    cfg_help_cmdline(cmd_opts_record,2,16,40);
    fprintf(stderr,"\n");
    
    fprintf(stderr,
#if 0
	    "station:\n"
	    "  this is one of the stations listed in $HOME/.xawtv\n"
	    "\n"
#endif
	    "Check the manual page for a more detailed description.\n"
	    "\n"
	    "--\n"
	    "Gerd Knorr <kraxel@bytesex.org>\n");
    exit(0);
}

static void
parse_args(int *argc, char **argv)
{
    /* get config */
    cfg_parse_cmdline(argc,argv,cmd_opts_only);
    if (GET_CMD_READCONF())
	read_config();
    cfg_parse_cmdline(argc,argv,cmd_opts_x11);
    cfg_parse_cmdline(argc,argv,cmd_opts_record);
    cfg_parse_cmdline(argc,argv,cmd_opts_devices);

    if (GET_CMD_HELP())
	usage();
    if (GET_CMD_VERSION())
	exit(0);
    if (GET_CMD_WRITECONF())
	write_config_file("options");

    /* set debug variables */
    debug    = GET_CMD_DEBUG();
    ng_debug = debug;
    ng_log_bad_stream = debug;
    ng_log_resync = debug;
#ifdef HAVE_DVB
    dvb_debug = debug;
#endif

    if (GET_CMD_HW_LS()) {
	fprintf(stderr,"\n");
	device_ls_devs(0);
	exit(0);
    }

    /* handle devices */
    if (debug)
	fprintf(stderr,"looking for available devices\n");
    devlist_init(GET_CMD_READCONF(), 0, GET_CMD_HW_STORE());

    if (GET_CMD_HW_CONFIG()) {
	fprintf(stderr,"\n");
	devlist_print();
	exit(0);
    }
}

int
main(int argc, char *argv[])
{
    XVisualInfo *vinfo_list;
    int n;

    /* basic init */
    hello_world();
    gtk_init(&argc, &argv);
    ng_init();
    parse_args(&argc,argv);

#ifdef HAVE_DVB
    /* easy start for dvb users, import vdr's list ... */
    if (0 == cfg_sections_count("stations") &&
	0 != cfg_sections_count("dvb") &&
	NULL != cfg_get_str("devs",cfg_sections_first("devs"),"dvb")) {
	vdr_import_stations();
    }
#endif

    /* gtk main window */
    if (debug)
	fprintf(stderr,"main: init main window...\n");
    main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    video = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(main_win), video);
    gtk_widget_add_events(video,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(main_win, "delete-event",
		     G_CALLBACK(gtk_wm_delete_quit), NULL);
    g_signal_connect(video, "expose-event", G_CALLBACK(resize), NULL);
    g_signal_connect(video, "button-release-event",
		     G_CALLBACK(mouse_button), NULL);

    /* lowlevel X11 initializations */
    dpy = gdk_x11_display_get_xdisplay(gtk_widget_get_display(video));
    fcntl(ConnectionNumber(dpy),F_SETFD,FD_CLOEXEC);
    visual = gdk_x11_visual_get_xvisual(gtk_widget_get_visual(video));
    vinfo.visualid = XVisualIDFromVisual(visual);
    vinfo_list = XGetVisualInfo(dpy, VisualIDMask, &vinfo, &n);
    vinfo = vinfo_list[0];
    XFree(vinfo_list);
    x11_init_visual(dpy,&vinfo);
    // init_atoms(dpy);

    /* set hooks (command.c) */
#if 0 /* FIXME */
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
    set_capture_hook    = do_capture;
    capture_get_hook    = video_gd_suspend;
    capture_rel_hook    = video_gd_restart;
#endif

#if 0
    /* x11 stuff */
    if (debug)
	fprintf(stderr,"main: xinerama extention...\n");
    xfree_xinerama_init(dpy);
    if (debug)
	fprintf(stderr,"main: install signal handlers...\n");
    xt_siginit();
    if (debug)
	fprintf(stderr,"main: checking wm...\n");
    wm_detect(dpy);
    xt_input_init(dpy);
    XSetIOErrorHandler(x11_ctrl_alt_backspace);
#endif

    /* create windows */
    if (debug)
	fprintf(stderr,"main: creating windows ...\n");
#if 0
    create_onscreen(labelWidgetClass);
    create_vtx();
    create_strwin();
    init_movie_menus();
#endif
    create_control();
    gtk_window_add_accel_group(GTK_WINDOW(main_win), control_accel_group);
    
    /* finalize X11 init + show windows */
    if (debug)
	fprintf(stderr,"main: mapping main window ...\n");
    gtk_widget_set_size_request(GTK_WIDGET(main_win), 320, 240);
    gtk_widget_show_all(main_win);
#if 0
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);

    /* mouse pointer magic */
    XtAddEventHandler(tv, PointerMotionMask, True, mouse_event, NULL);
    mouse_event(tv,NULL,NULL,NULL);
#endif

    /* build channel list */
    if (GET_CMD_READCONF()) {
	if (debug)
	    fprintf(stderr,"main: parse channels from config file ...\n");
	// apply_config();
    }

#if 0
    channel_menu();
    if (optind+1 == argc)
	do_va_cmd(2,"setstation",argv[optind]);
#endif

    return main_loop(g_main_context_default());
}
