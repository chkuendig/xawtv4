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
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XShm.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#ifdef HAVE_DVB
# include <linux/dvb/frontend.h>
#endif

#include "grab-ng.h"
#include "blit.h"
#include "devs.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "tuning.h"
#include "commands.h"
#include "av-sync.h"
#include "atoms.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "gui.h"

#include "logo.xpm"

/* ------------------------------------------------------------------------ */

/* misc globals */
Display *dpy;

/* video window */
static GtkWidget *video;
static GdkPixmap *logo;
static GdkGC     *gc;
static int       idle;

/* x11 stuff */
static Visual *visual;
static XVisualInfo vinfo;

/* ------------------------------------------------------------------------ */

#define O_CMD_READCONF	       	O_CMDLINE, "readconf"
#define O_CMD_HW_LS	       	O_CMDLINE, "hw-ls"
#define O_CMD_HW_CONFIG	       	O_CMDLINE, "hw-config"
#define O_CMD_HW_STORE	       	O_CMDLINE, "hw-store"
#define O_CMD_DVB_S	       	O_CMDLINE, "dvbs"
#define O_CMD_DVB_C	       	O_CMDLINE, "dvbc"
#define O_CMD_DVB_T	       	O_CMDLINE, "dvbt"
#define O_CMD_ATSC	       	O_CMDLINE, "atsc"

#define GET_CMD_READCONF()     	cfg_get_bool(O_CMD_READCONF,   	1)
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
	.letter   = 'c',
	.cmdline  = "debug",
	.option   = { O_CMD_DEBUG },
	.needsarg = 1,
	.desc     = "set debug level",
    },{
	.cmdline  = "device",
	.option   = { O_CMD_DEVICE },
	.needsarg = 1,
	.desc     = "pick device config (use -hwconfig to list them)",

    },{
	.cmdline  = "noconf",
	.option   = { O_CMD_READCONF },
	.value    = "0",
	.desc     = "don't read the config file",
    },{
	.cmdline  = "dvbs",
	.option   = { O_CMD_DVB_S },
	.value    = "1",
	.desc     = "force DVB-S mode (for hackers only)",
    },{
	.cmdline  = "dvbc",
	.option   = { O_CMD_DVB_C },
	.value    = "1",
	.desc     = "force DVB-C mode (for hackers only)",
    },{
	.cmdline  = "dvbt",
	.option   = { O_CMD_DVB_T },
	.value    = "1",
	.desc     = "force DVB-T mode (for hackers only)",
    },{
	.cmdline  = "atsc",
	.option   = { O_CMD_ATSC },
	.value    = "1",
	.desc     = "force ATSC mode (for hackers only)",

    },{
	.cmdline  = "geometry",
	.option   = { O_CMD_GEOMETRY },
	.needsarg = 1,
	.desc     = "specify window geoemtry",

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

static void childsig(int signal)
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

static void termsig(int signal)
{
    if (debug)
	fprintf(stderr,"received signal %d [%s]\n",signal,strsignal(signal));
    command_pending++;
    exit_application++;
}

static void segfault(int signal)
{
    fprintf(stderr,"[pid=%d] segfault catched, aborting\n",getpid());
    abort();
}

static void siginit(void)
{
    struct sigaction act,old;

    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);

    act.sa_handler = SIG_IGN;
    sigaction(SIGHUP,  &act, &old);
    sigaction(SIGPIPE, &act, &old);
    
    act.sa_handler = childsig;
    sigaction(SIGCHLD, &act, &old);

    act.sa_handler = termsig;
    sigaction(SIGINT,  &act, &old);
    sigaction(SIGTERM, &act, &old);

    if (debug) {
	act.sa_handler = segfault;
	sigaction(SIGSEGV, &act, &old);
	fprintf(stderr,"main thread [pid=%d]\n",getpid());
    }
}

/* ------------------------------------------------------------------------ */

#ifdef HAVE_DVB
static struct eit_state *eit;
#endif

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
	if (devs.video.flags & CAN_OVERLAY)
	    display_mode = DISPLAY_OVERLAY;
	else if (devs.video.flags & (CAN_MPEG_PS|CAN_MPEG_TS))
	    display_mode = DISPLAY_MPEG;
	else if (devs.video.flags & CAN_CAPTURE)
	    display_mode = DISPLAY_GRAB;

#ifdef HAVE_DVB
    } else if (NULL != devs.dvb) {
	/* init dvb device */
	display_mode = DISPLAY_DVB;
	devs.dvbmon = dvbmon_init(devs.dvb, debug, 1, 1, 2);
	dvbmon_add_callback(devs.dvbmon,dvbwatch_scanner,NULL);
	eit = eit_add_watch(devs.dvb, 0x4e,0xff, 0, 0);
//	eit = eit_add_watch(devs.dvb, 0x50,0xf0, 0, 0);
	if (debug)
	    dvbmon_add_callback(devs.dvbmon,dvbwatch_logger,NULL);
#endif

    } else {
	display_mode = DISPLAY_NONE;

    }

    /* GUI update */
    control_switchdevice();

    /* retune */
    if (NULL != curr_station)
	do_va_cmd(2,"setstation",curr_station);
}

static void
grabber_fini(void)
{
#ifdef HAVE_DVB
    if (NULL != devs.dvbmon) {
	if (dvbscan_win)
	    gtk_widget_destroy(dvbscan_win);
	if (dvbtune_dialog)
	    gtk_widget_destroy(dvbtune_dialog);
	if (satellite_dialog)
	    gtk_widget_destroy(satellite_dialog);
	dvbmon_fini(devs.dvbmon);
	devs.dvbmon = NULL;
	if (GET_CMD_READCONF()) {
	    write_config_file("dvb-ts");
	    write_config_file("dvb-pr");
	}
	if (eit)
	    eit_del_watch(eit);
    }
#endif
    if (NULL != analog_win)
	gtk_widget_destroy(analog_win);
    
    audio_off();
    device_fini();
}

static void
set_property(void)
{
    Window win;
    int    len;
    char   line[80];

    len  = sprintf(line,     "%s", curr_details)+1;
    len += sprintf(line+len, "%s", curr_channel ? curr_channel : "?") +1;
    len += sprintf(line+len, "%s", curr_station ? curr_station : "?") +1;
    win  = gdk_x11_drawable_get_xid(main_win->window);
    if (win)
	XChangeProperty(dpy, win, _XAWTV_STATION, XA_STRING,
			8, PropModeReplace,
			line, len);
}

/* ------------------------------------------------------------------------ */
/* hooks                                                                    */

static char default_title[256];
static guint title_timer_id;
static guint epg_timer_id;
static struct epgitem *epg_last;
static int epg_debug = 0;

static gboolean title_timeout(gpointer data)
{
    keypad_timeout();
    gtk_window_set_title(GTK_WINDOW(main_win), default_title);
    title_timer_id = 0;
    return FALSE;
}

static void new_message(char *txt)
{
    gtk_window_set_title(GTK_WINDOW(main_win), txt);
    display_onscreen(txt);

    if (title_timer_id)
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(),title_timer_id));
    title_timer_id = g_timeout_add(TITLE_TIME, title_timeout, NULL);
}

static void new_title(char *txt)
{
    snprintf(default_title,sizeof(default_title),"%s",txt);
    gtk_window_set_title(GTK_WINDOW(main_win), default_title);
    display_onscreen(default_title);

    if (title_timer_id) {
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(),title_timer_id));
	title_timer_id = 0;
    }
}

static void print_epg(struct epgitem *epg)
{
    char from[10], to[10];
    struct tm tm;
    time_t total, passed;
    int c;
    unsigned char percent = 0;

    localtime_r(&epg->start,&tm);
    strftime(from,sizeof(from),"%H:%M",&tm);
    localtime_r(&epg->stop,&tm);
    strftime(to,sizeof(to),"%H:%M",&tm);

    total  = epg->stop  - epg->start;
    passed = time(NULL) - epg->start;

    if (total)
	percent = passed * 100 / total;

    fprintf(stderr,"<epg>\n");
    fprintf(stderr,"time    : %s - %s  (%2d%%)\n",
	    from, to, percent);
    if (epg->name[0])
	fprintf(stderr,"title   : %s\n",epg->name);
    if (epg->stext[0])
	fprintf(stderr,"subtitle: %s\n",epg->stext);
    for (c = 0; c < DIMOF(epg->cat); c++)
	if (NULL != epg->cat[c])
	    fprintf(stderr,"category: %s\n",epg->cat[c]);
    if (epg->etext)
	fprintf(stderr,"<desc>\n%s\n</desc>\n",epg->etext);
    fprintf(stderr,"</epg>\n");
}

static gboolean epg_timer_func(gpointer data)
{
    struct epgitem *epg;

    if (curr_tsid && curr_pnr) {
	epg = eit_lookup(curr_tsid, curr_pnr, time(NULL), epg_debug);
	if (NULL == epg)
	    return TRUE;
	if (epg_last == epg)
	    return TRUE;
	epg_last = epg;
	if (debug)
	    print_epg(epg);
	display_epg(GTK_WINDOW(main_win), epg);
	if (GTK_WIDGET_VISIBLE(epg_win))
	    epgwin_show(epg);
	epg_timer_id = g_timeout_add(3000, epg_timer_func, NULL);
	return FALSE;
    }
    epg_timer_id = 0;
    return FALSE;
}

static void do_exit(void)
{
    command_pending++;
    exit_application++;
}

static void new_station(void)
{
    struct epgitem *epg;
    char label[256];

    snprintf(label, sizeof(label), "%s | %s | %s",
	     curr_details,
	     curr_channel,
	     curr_station);
    gtk_label_set_text(GTK_LABEL(control_status), label);
    set_property();
    x11_station_activate(curr_station);
    analog_set_channel(curr_channel);

    epg_last = NULL;
    if (epg_timer_id) {
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(),epg_timer_id));
	epg_timer_id = 0;
    }
    if (curr_tsid && curr_pnr) {
	epg = eit_lookup(curr_tsid, curr_pnr, time(NULL), epg_debug);
	if (NULL != epg) {
	    if (debug)
		print_epg(epg);
	    display_epg(GTK_WINDOW(main_win), epg);
	    if (GTK_WIDGET_VISIBLE(epg_win))
		epgwin_show(epg);
	} else {
	    epg_timer_id = g_timeout_add(300, epg_timer_func, NULL);
	}
    }
}

static void new_freqtab(void)
{
    cfg_set_str(O_FREQTAB, freqtab_get());
    analog_set_freqtab();
}

/* ------------------------------------------------------------------------ */
/* main loop                                                                */

static struct media_stream *mm;
int recording = 0;

static int mm_init(struct blit_handle *blit, int speed)
{
    mm = malloc(sizeof(*mm));
    if (NULL == mm)
	return -1;
    memset(mm,0,sizeof(*mm));
    mm->blit  = blit;
    mm->speed = speed;
    return 0;
}

static void mm_fini(void)
{
    BUG_ON(NULL != mm->as,"mm->as isn't NULL");
    BUG_ON(NULL != mm->vs,"mm->vs isn't NULL");
    mm_rec_stop();
    if (mm->reader && mm->rhandle)
	mm->reader->rd_close(mm->rhandle);
    free(mm);
    mm = NULL;
}

int mm_rec_start(void)
{
    struct epgitem *epg = NULL;
    struct ng_writer *wr;
    char *recfile;

    if (NULL == mm)
	return -1;
    if (mm->writer)
	return -1;
    
    switch (display_mode) {
    case DISPLAY_DVB:
	if (curr_tsid && curr_pnr)
	    epg = eit_lookup(curr_tsid, curr_pnr, time(NULL), epg_debug);
	recfile = record_filename("tv", curr_station,
				  epg ? epg->name : NULL, "mpeg");
	wr = ng_find_writer_name("mpeg-ps");
	break;
    default:
	recfile = NULL;
	wr = NULL;
    }
    if (!wr)
	return -1;
    
    if (0 != av_media_start_recording(mm, wr, recfile))
	return -1;
    recording = 1;
    return 0;
}

int mm_rec_stop(void)
{
    if (NULL == mm)
	return -1;
    if (NULL == mm->writer)
	return -1;

    av_media_stop_recording(mm);
    mm->writer = NULL;
    recording = 0;
    return 0;
}

static void
grabdisplay_loop(GMainContext *context, GtkWidget *widget, struct blit_handle *blit)
{
    if (debug)
	fprintf(stderr,"%s: enter\n",__FUNCTION__);

    if (0 != mm_init(blit,0))
	return;
    
    /* video setup */
    ng_dev_open(&devs.video);
    av_media_setup_video_grab(mm,widget);
    
    /* go playback stuff */
    if (mm->vs) {
	devs.video.v->startvideo(devs.video.handle,-1,2);
	av_media_mainloop(context, mm);
	devs.video.v->stopvideo(devs.video.handle);
    }

    /* cleanup */
    ng_dev_close(&devs.video);
    mm_fini();

    if (debug)
	fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

static void
overlay_loop(GMainContext *context, GtkWidget *widget)
{
    Window win = gdk_x11_drawable_get_xid(widget->window);
    int x,y, width, height, depth;

    if (debug)
	fprintf(stderr,"%s: enter\n",__FUNCTION__);

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

    if (debug)
	fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

#ifdef HAVE_DVB

static void
dvb_loop(GMainContext *context, GtkWidget *widget, struct blit_handle *blit)
{
    char path[64];
    struct ng_audio_fmt *afmt;
    struct ng_video_fmt *vfmt;

    if (0 == ng_mpeg_vpid && 0 == ng_mpeg_apid) {
	fprintf(stderr,"no DVB station tuned\n");
	return;
    }
    
    if (debug)
	fprintf(stderr,"%s: enter (%d/%d)\n",__FUNCTION__,
		ng_mpeg_vpid, ng_mpeg_apid);

    if (!dvb_frontend_is_locked(devs.dvb))
	return;

    if (0 != mm_init(blit,1))
	return;
    mm->reader = ng_find_reader_name("mpeg-ts");
    if (NULL == mm->reader) {
	fprintf(stderr,"Oops: transport stream parser not found\n");
	mm_fini();
	return;
    }

    snprintf(path,sizeof(path),"%s/dvr0",devs.dvbadapter);
    mm->rhandle = mm->reader->rd_open(path);
    if (NULL == mm->rhandle) {
	fprintf(stderr,"can't open: %s\n",path);
	mm_fini();
	return;
    }

    /* audio + video setup */
    vfmt = mm->reader->rd_vfmt(mm->rhandle,NULL,0);
    if (vfmt && (0 == vfmt->width || 0 == vfmt->height))
	vfmt = NULL;
    afmt = mm->reader->rd_afmt(mm->rhandle);

    if (vfmt)
	av_media_setup_video_reader(mm);
    if (afmt)
	av_media_setup_audio_reader(mm,afmt);

    /* go playback stuff */
    av_media_mainloop(context, mm);

    /* cleanup */
    mm_fini();

    if (debug)
	fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

#endif

static void
mpeg_loop(GMainContext *context, GtkWidget *widget, struct blit_handle *blit)
{
    struct ng_audio_fmt *afmt;
    struct ng_video_fmt *vfmt;
    int flags = 0;
    char *path;
    
    if (debug)
	fprintf(stderr,"%s: enter (%d/%d)\n",__FUNCTION__,
		ng_mpeg_vpid, ng_mpeg_apid);

    if (0 != mm_init(blit,1))
	return;

    /* find mpeg parser */
    if (devs.video.flags & CAN_MPEG_PS) {
	mm->reader = ng_find_reader_name("mpeg-ps");
	flags     = MPEG_FLAGS_PS;
    }
    else if (devs.video.flags & CAN_MPEG_TS) {
	mm->reader = ng_find_reader_name("mpeg-ts");
	flags     = MPEG_FLAGS_TS;
    }
    if (NULL == mm->reader) {
	fprintf(stderr,"Oops: mpeg stream parser not found\n");
	mm_fini();
	return;
    }

    /* init driver */
    ng_dev_open(&devs.video);
    path = devs.video.v->setup_mpeg(devs.video.handle, flags);
    if (NULL == path) {
	fprintf(stderr,"Oops: driver mpeg setup failed\n");
	ng_dev_close(&devs.video);
	mm_fini();
	return;
    }
    ng_dev_close(&devs.video);

    mm->rhandle = mm->reader->rd_open(path);
    if (NULL == mm->rhandle) {
	fprintf(stderr,"can't open: %s\n",path);
	mm_fini();
	return;
    }

    /* audio + video setup */
    vfmt = mm->reader->rd_vfmt(mm->rhandle,NULL,0);
    if (vfmt && (0 == vfmt->width || 0 == vfmt->height))
	vfmt = NULL;
    afmt = mm->reader->rd_afmt(mm->rhandle);

    if (vfmt)
	av_media_setup_video_reader(mm);
    if (afmt)
	av_media_setup_audio_reader(mm,afmt);

    /* go playback stuff */
    av_media_mainloop(context, mm);

    /* cleanup */
    mm_fini();

    if (debug)
	fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

static int main_loop(GMainContext *context)
{
    struct blit_handle *blit;

    /* init */
    if (debug)
	fprintf(stderr,"main: open grabber device...\n");
    grabber_init(cfg_get_str(O_CMD_DEVICE));
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
	case DISPLAY_MPEG:
	    if (devs.video.type != NG_DEV_NONE)
		mpeg_loop(context,video,blit);
	    break;
#ifdef HAVE_DVB
	case DISPLAY_DVB:
	    if (devs.dvb) {
		int rc;
		/* wait for DVB tuning finish (frontend lock) */
		// fprintf(stderr,"#");
		do {
		    rc = dvb_finish_tune(devs.dvb,100);
		    // fprintf(stderr,"*");
		    while (g_main_context_iteration(context,FALSE))
			/* nothing */;
		} while (-1 == rc && !command_pending);
		// fprintf(stderr,"#%d#\n",rc);
		if (0 == rc && !command_pending)
		    dvb_loop(context,video,blit);
	    }
	    break;
#endif
	default:
	    fprintf(stderr,"unknown/unimplemented display mode %d\n",display_mode);
	    break;
	}

	/* default/error catch */
	if (!command_pending) {
	    idle = 1;
	    gdk_window_clear_area_e(video->window, 0,0,0,0);
	    while (!command_pending)
		g_main_context_iteration(context,TRUE);
	    idle = 0;
	}
	gdk_window_clear_area_e(video->window, 0,0,0,0);
    }

    /* cleanup */
    blit_fini(blit);
    grabber_fini();
    return 0;
}

/* ------------------------------------------------------------------------ */

static gboolean mouse_button_eh(GtkWidget *widget,
				GdkEventButton *event,
				gpointer user_data)
{
    /* left button */
    if (NULL != control_st_menu  &&  1 == event->button) {
	gtk_widget_show_all(control_st_menu);
	gtk_menu_popup(GTK_MENU(control_st_menu),
		       NULL, NULL, NULL, NULL,
		       event->button, event->time);
	return TRUE;
    }

    /* right button */
    if (NULL != control_win  &&  3 == event->button) {
	if (GTK_WIDGET_VISIBLE(control_win))
	    gtk_widget_hide(control_win);
	else
	    gtk_widget_show_all(control_win);
	return TRUE;
    }

#ifdef HAVE_DVB
    /* middle button */
    if (NULL != epg_win && 2 == event->button) {
	struct epgitem *epg = NULL;

	if (!GTK_WIDGET_VISIBLE(epg_win)) {
	    epg = eit_lookup(curr_tsid, curr_pnr, time(NULL), epg_debug);
	    epgwin_show(epg);
	} else {
	    gtk_widget_hide(epg_win);
	}
    }
#endif

    return FALSE;
}

static gboolean configure_eh(GtkWidget *widget,
			     GdkEventConfigure *event,
			     gpointer user_data)
{
    static int ww,wh;
    
    if (event->width == ww && event->height == wh)
	return FALSE;
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
    return FALSE;
}

static gboolean expose_eh(GtkWidget *widget,
			  GdkEventExpose *event,
			  gpointer user_data)
{
    gint width, height;

    if (debug)
	fprintf(stderr,"%s\n",__FUNCTION__);

    if (event->count)
	return FALSE;
    switch (display_mode) {
    case DISPLAY_OVERLAY:
	/* trigger reblit */
	command_pending++;
	break;
    default:
	/* nothing */
	break;
    };

    if (idle) {
	gdk_drawable_get_size(video->window, &width, &height);
	gdk_window_clear(video->window);
	gdk_draw_pixmap(video->window, gc, logo, 0,0,
			(width-320)/2,
			(height-240)/2,
			320,240);
	return TRUE;
    }
    return FALSE;
}

static gboolean property_eh(GtkWidget *widget,
			    GdkEventProperty *event,
			    gpointer user_data)
{
    Atom atom = gdk_x11_atom_to_xatom_for_display
	(gtk_widget_get_display(widget), event->atom);
    Window win = gdk_x11_drawable_get_xid(widget->window);

    Atom            type;
    int             format, argc;
    unsigned int    i;
    char            *argv[32];
    unsigned long   nitems, bytesafter;
    unsigned char   *args = NULL;

    if (debug)
	fprintf(stderr,"%s %s\n", __FUNCTION__,
		XGetAtomName(dpy,atom));

    if (atom != _XAWTV_REMOTE)
	return FALSE;
    if (Success != XGetWindowProperty(dpy, win, atom, 
				      0, (65536 / sizeof(long)),
				      True, XA_STRING, &type, &format,
				      &nitems, &bytesafter, &args))
	return TRUE;
    if (nitems == 0)
	return TRUE;

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
    return TRUE;
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
	fprintf(stderr,"xawtv /must not/ be installed suid root\n");
	exit(1);
    }
}

static void
usage(FILE *out)
{
    fprintf(out,
	    "\n"
	    "usage: xawtv [ options ] [ station ]\n"
	    "options:\n");

    cfg_help_cmdline(out,cmd_opts_only,2,16,0);
    fprintf(out,"\n");

    cfg_help_cmdline(out,cmd_opts_devices,2,16,40);
    fprintf(out,"\n");

    cfg_help_cmdline(out,cmd_opts_x11,2,16,40);
    fprintf(out,"\n");

    cfg_help_cmdline(out,cmd_opts_record,2,16,40);
    fprintf(out,"\n");
    
    fprintf(out,
#if 0
	    "station:\n"
	    "  this is one of the stations listed in $HOME/.tv/stations\n"
	    "\n"
#endif
#if 0
	    /* outdated right now, so don't confuse users */
	    "Check the manual page for a more detailed description.\n"
#endif
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
	usage(stdout);
    if (GET_CMD_VERSION())
	exit(0);

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
	device_ls_devs(GET_CMD_VERBOSE());
	exit(0);
    }

#ifdef HAVE_DVB
    /* dvb type override */
    if (cfg_get_bool(O_CMD_DVB_S,0))
	dvb_type_override = FE_QPSK;
    if (cfg_get_bool(O_CMD_DVB_C,0))
	dvb_type_override = FE_QAM;
    if (cfg_get_bool(O_CMD_DVB_T,0))
	dvb_type_override = FE_OFDM;
    if (cfg_get_bool(O_CMD_ATSC,0)) {
#ifdef FE_ATSC
	dvb_type_override = FE_ATSC;
#else
	fprintf(stderr,"Compiled without ATSC support, sorry.\n");
	exit(1);
#endif
    }
#endif

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
    GdkColor black = { .red = 0x0000, .green = 0x0000, .blue = 0x0000 };
    XVisualInfo *vinfo_list;
    char *freqtab, *geometry;
    int n;

    hello_world();
    setlocale(LC_ALL,"");
    bindtextdomain(PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);

    /* lowlevel X11 init */
    gtk_init(&argc, &argv);
    dpy = gdk_x11_display_get_xdisplay(gdk_display_get_default());
    fcntl(ConnectionNumber(dpy),F_SETFD,FD_CLOEXEC);
    init_atoms(dpy);

    /* basic init */
    ng_init();
    parse_args(&argc,argv);
#ifdef HAVE_DVB
    dvb_lang_init();
#endif

    /* gtk main window */
    if (debug)
	fprintf(stderr,"main: init main window...\n");
    main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    video = gtk_drawing_area_new();
    gtk_container_add(GTK_CONTAINER(main_win), video);
    gtk_widget_modify_bg(video,GTK_STATE_NORMAL, &black);

    gtk_widget_add_events(video,
			  GDK_BUTTON_PRESS_MASK   |
			  GDK_BUTTON_RELEASE_MASK |
			  GDK_EXPOSURE_MASK       |
			  GDK_STRUCTURE_MASK      );
    g_signal_connect(video, "configure-event",
		     G_CALLBACK(configure_eh), NULL);
    g_signal_connect(video, "expose-event",
		     G_CALLBACK(expose_eh), NULL);
    g_signal_connect(video, "button-release-event",
		     G_CALLBACK(mouse_button_eh), NULL);

    gtk_widget_add_events(main_win,
			  GDK_STRUCTURE_MASK      |
			  GDK_PROPERTY_CHANGE_MASK);
    g_signal_connect(main_win, "delete-event",
		     G_CALLBACK(gtk_wm_delete_quit), NULL);
    g_signal_connect(main_win, "property-notify-event",
		     G_CALLBACK(property_eh), NULL);

    /* init X11 visual info */
    visual = gdk_x11_visual_get_xvisual(gtk_widget_get_visual(video));
    vinfo.visualid = XVisualIDFromVisual(visual);
    vinfo_list = XGetVisualInfo(dpy, VisualIDMask, &vinfo, &n);
    vinfo = vinfo_list[0];
    XFree(vinfo_list);
    x11_init_visual(dpy,&vinfo);

    /* set hooks (command.c) */
    update_title        = new_title;
    display_message     = new_message;
    exit_hook           = do_exit;
    setstation_notify   = new_station;
    fullscreen_hook     = menu_cb_fullscreen;
    freqtab_notify      = new_freqtab;
#if 0 /* TODO */
#ifdef HAVE_ZVBI
    vtx_subtitle        = display_subtitle;
#endif
    movie_hook          = do_movie_record;
    rec_status          = do_rec_status;
#endif

#if 0
    /* x11 stuff */
    if (debug)
	fprintf(stderr,"main: xinerama extention...\n");
    xfree_xinerama_init(dpy);
    if (debug)
	fprintf(stderr,"main: checking wm...\n");
    wm_detect(dpy);
    xt_input_init(dpy);
    XSetIOErrorHandler(x11_ctrl_alt_backspace);
#endif
    if (debug)
	fprintf(stderr,"main: install signal handlers...\n");
    siginit();

    /* create windows */
    if (debug)
	fprintf(stderr,"main: creating windows ...\n");
#if 0
    create_vtx();
    create_strwin();
    init_movie_menus();
#endif
    create_control();
    create_onscreen();
    create_epg();
#ifdef HAVE_DVB
    create_epgwin(GTK_WINDOW(main_win));
#endif
    gtk_window_add_accel_group(GTK_WINDOW(main_win), control_accel_group);
    
    /* finalize X11 init + show windows */
    if (debug)
	fprintf(stderr,"main: mapping main window ...\n");
    gtk_widget_show(video);
    if (NULL == (geometry = cfg_get_str(O_CMD_GEOMETRY)) ||
	!gtk_window_parse_geometry(GTK_WINDOW(main_win), geometry))
	gtk_window_set_default_size(GTK_WINDOW(main_win), 384, 288);
    gtk_widget_show(main_win);
    gtk_unclutter(video);
    if (!debug && GET_X11_STDERR_REDIR())
	gtk_redirect_stderr_to_gui(GTK_WINDOW(main_win));

    gc = gdk_gc_new(video->window);
    logo = gdk_pixmap_create_from_xpm_d(video->window,
					NULL, NULL,
					logo_xpm);

    /* set some values */
    if (NULL != (freqtab = cfg_get_str(O_FREQTAB)))
	do_va_cmd(2,"setfreqtab",freqtab);
    if (argc > 1)
	do_va_cmd(2,"setstation",argv[1]);
    else {
	/* first in list (FIXME: don't modify when known station tuned?) */
	do_va_cmd(2,"setstation","0");
    }

    /* enter main loop */
    main_loop(g_main_context_default());

    if (GET_CMD_READCONF())
	write_config_file("options");
    fprintf(stderr,"bye...\n");
    exit(0);
}
