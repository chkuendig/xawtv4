/*
 * simple gtk movie player
 *
 *  (c) 2004 Gerd Knorr <kraxel@bytesex.org>
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
#include "commands.h"
#include "av-sync.h"
#include "gui.h"

/* ------------------------------------------------------------------------ */

int debug = 0;

static GtkWidget *toplevel,*video,*menu;
static Display *dpy;
static Visual *visual;
static XVisualInfo vinfo;
    
/* ------------------------------------------------------------------------ */

#define O_CMDLINE               "cmdline", "cmdline"

#define O_CMD_HELP             	O_CMDLINE, "help"
#define O_CMD_VERBOSE          	O_CMDLINE, "verbose"
#define O_CMD_DEBUG	       	O_CMDLINE, "debug"
#define O_CMD_DVB	       	O_CMDLINE, "dvb"

#define O_CMD_XVIDEO	       	O_CMDLINE, "xvideo"
#define O_CMD_OPENGL	       	O_CMDLINE, "opengl"
#define O_CMD_AUDIO	       	O_CMDLINE, "audio"
#define O_CMD_VIDEO	       	O_CMDLINE, "video"

#define O_CMD_SCALE	       	O_CMDLINE, "scale"
#define O_CMD_SPEED	       	O_CMDLINE, "speed"
#define O_CMD_DSP	       	O_CMDLINE, "dsp"

#define GET_CMD_HELP()		cfg_get_bool(O_CMD_HELP,   	0)
#define GET_CMD_VERBOSE()	cfg_get_bool(O_CMD_VERBOSE,   	0)
#define GET_CMD_DEBUG()		cfg_get_bool(O_CMD_DEBUG,   	0)
#define GET_CMD_DVB()		cfg_get_bool(O_CMD_DVB,   	0)

#define GET_CMD_XVIDEO()	cfg_get_bool(O_CMD_XVIDEO,   	1)
#define GET_CMD_OPENGL()       	cfg_get_bool(O_CMD_OPENGL,     	1)
#define GET_CMD_AUDIO()		cfg_get_bool(O_CMD_AUDIO,   	1)
#define GET_CMD_VIDEO()		cfg_get_bool(O_CMD_VIDEO,   	1)

#define GET_CMD_SCALE()		cfg_get_int(O_CMD_SCALE,   	1)
#define GET_CMD_SPEED()		cfg_get_int(O_CMD_SPEED,   	1)

struct cfg_cmdline cmd_opts[] = {
    {
	.letter   = 'h',
	.cmdline  = "help",
	.option   = { O_CMD_HELP },
	.value    = "1",
	.desc     = "print this text",
    },{
	.letter   = 'v',
	.cmdline  = "verbose",
	.option   = { O_CMD_VERBOSE },
	.value    = "1",
	.desc     = "be verbose",
    },{
	.letter   = 'd',
	.cmdline  = "debug",
	.option   = { O_CMD_DEBUG },
	.value    = "1",
	.desc     = "enable debug output",
    },{
	.cmdline  = "dvb",
	.option   = { O_CMD_DVB },
	.value    = "1",
	.desc     = "enable dvb mode",

    },{
	.cmdline  = "dsp",
	.option   = { O_CMD_DSP },
	.needsarg = 1,
	.desc     = "audio device to use",

    },{
	.cmdline  = "xv",
	.option   = { O_CMD_XVIDEO },
	.yesno    = 1,
	.desc     = "enable/disable Xvideo",
    },{
	.cmdline  = "gl",
	.option   = { O_CMD_OPENGL },
	.yesno    = 1,
	.desc     = "enable/disable OpenGL (GLX extention)",
    },{
	.cmdline  = "audio",
	.option   = { O_CMD_AUDIO },
	.yesno    = 1,
	.desc     = "enable/disable audio playback",
    },{
	.cmdline  = "video",
	.option   = { O_CMD_VIDEO },
	.yesno    = 1,
	.desc     = "enable/disable video playback",

    },{
	.cmdline  = "2",
	.option   = { O_CMD_SCALE },
	.value    = "2",
	.desc     = "scale up video by factor two",
    },{
	.cmdline  = "3",
	.option   = { O_CMD_SCALE },
	.value    = "3",
	.desc     = "scale up video by factor three",
    },{
	.cmdline  = "4",
	.option   = { O_CMD_SCALE },
	.value    = "4",
	.desc     = "scale up video by factor four",

    },{
	.cmdline  = "fast",
	.option   = { O_CMD_SPEED },
	.value    = "0",
	.desc     = "fast playback",
    },{
	.cmdline  = "slow",
	.option   = { O_CMD_SPEED },
	.value    = "2",
	.desc     = "slow playback",

    },{
	/* end of list */
    }
};

/* ------------------------------------------------------------------------ */

static int ww, wh, manually_resized;

static gboolean resize(GtkWidget *widget,
		       GdkEventConfigure *event,
		       gpointer user_data)
{
    if (event->width == ww && event->height == wh)
	return 0;

    manually_resized = 1;
    if (debug)
	fprintf(stderr,"window manually resized (or moved) to %dx%d\n",
		event->width, event->height);
    return 0;
}

static void window_setup(struct media_stream *mm,
			 struct ng_video_fmt *fmt)
{
    static int first = 1;
    int scale = GET_CMD_SCALE();

    /* calulate window size */
    ww = 320;
    wh = 42;
    if (fmt) {
	ww = fmt->width  * scale;
	wh = fmt->height * scale;
    }

    if (first) {
	gtk_window_resize(GTK_WINDOW(toplevel),ww,wh);
	gtk_widget_show_all(toplevel);
	first = 0;
    } else {
	/* window present -- resize */
	if (!manually_resized)
	    gtk_window_resize(GTK_WINDOW(toplevel),ww,wh);
    }

    if (NULL == mm->blit)
	mm->blit = blit_init(video,&vinfo,
			     GET_CMD_XVIDEO(), GET_CMD_OPENGL());
}

static void play_file(char *filename)
{
    struct media_stream mm;
    struct ng_audio_fmt *afmt;
    struct ng_video_fmt *vfmt;

    memset(&mm,0,sizeof(mm));

    /* set title */
    if (toplevel)
	gtk_window_set_title(GTK_WINDOW(toplevel),filename);

    /* open file */
    if (GET_CMD_DVB()) {
#ifdef HAVE_DVB
	/* dvb hack */
	struct dvb_state *dvb = NULL;

	mm.reader = ng_find_reader_name("mpeg-ts");
	if (NULL == mm.reader) {
	    fprintf(stderr,"can't handle dvb\n");
	    exit(1);
	}
	dvb_debug = 1;
	dvb = dvb_init_nr(0);
	if (dvb) {
	    if (-1 == dvb_tune(dvb,filename)) {
		fprintf(stderr,"tuning failed\n");
		exit(1);
	    }
	}
	filename = "/dev/dvb/adapter0/dvr0";
#else
	fprintf(stderr,"built without dvb support, sorry\n");
#endif
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

    if (GET_CMD_VERBOSE()) {
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

    if (0 == GET_CMD_VIDEO())
	vfmt = NULL;
    if (0 == GET_CMD_AUDIO())
	afmt = NULL;
    mm.speed = GET_CMD_SPEED();
    if (vfmt && NULL == video) {
	fprintf(stderr,"video stream: can't play without X11\n");
	vfmt = NULL;
    }
    if (1 != mm.speed)
	/* no audio, will not sync anyway ... */
	afmt = NULL;

    /* video setup */
    if (video)
	window_setup(&mm,vfmt);
    if (vfmt)
	av_media_reader_video(&mm);

    /* audio setup */
    if (afmt)
	av_media_reader_audio(&mm,afmt);

    /* go playback stuff */
    av_media_mainloop(g_main_context_default(), &mm);

    /* cleanup */
    BUG_ON(NULL != mm.as,"mm.as isn't NULL");
    BUG_ON(NULL != mm.vs,"mm.vs isn't NULL");
    mm.reader->rd_close(mm.rhandle);
    if (mm.blit)
	blit_fini(mm.blit);
    if (command_pending)
	command_pending--;

 done:
    if (GET_CMD_VERBOSE())
	fprintf(stderr,"\n");
    return;
}

/* ------------------------------------------------------------------------ */

static gboolean mouse_button(GtkWidget *widget,
			     GdkEventButton *event,
			     gpointer user_data)
{
    if (3 == event->button) {
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
		       event->button, event->time);
	return TRUE;
    }
    return FALSE;
}

static void fullscreen_cb(void)
{
    static int on = 0;

    on = !on;
    if (on)
	gtk_window_fullscreen(GTK_WINDOW(toplevel));
    else
	gtk_window_unfullscreen(GTK_WINDOW(toplevel));
}

static void next_cb(void)
{
    command_pending++;
}

static GtkItemFactoryEntry menu_items[] = {
    { "/_Next",       "space", next_cb,       0, "<Item>"       },
    { "/_Fullscreen", "F",     fullscreen_cb, 0, "<Item>"       },
    { "/qsep",        NULL,    NULL,          0, "<Separator>"  },
    { "/_Quit",       "Q",     gtk_quit_cb,   0, "<StockItem>", GTK_STOCK_QUIT },
};
static gint nmenu_items = sizeof (menu_items)/sizeof (menu_items[0]);

static GtkWidget *create_menu(GtkWidget *window)
{
    GtkItemFactory *item_factory;
    GtkAccelGroup *accel_group;

    accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU, "<menu>",
					accel_group);
    gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
    return gtk_item_factory_get_widget(item_factory, "<menu>");
}

static void usage(char *prog)
{
    char *h;

    if (NULL != (h = strrchr(prog,'/')))
	prog = h+1;
    fprintf(stderr,
	    "\n"
	    "%s is a simple audio + video player.  Works also without X11,\n"
	    "will not play video then through for obvious reasons ;)\n"
	    "\n"
	    "usage: %s [ options ] file ...\n"
	    "options:\n",
	    prog,prog);
    cfg_help_cmdline(cmd_opts,4,16,0);
    fprintf(stderr,
	    "\n"
	    "-- \n"
	    "(c) 2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]\n");
}

int main(int argc, char *argv[])
{
    XVisualInfo *vinfo_list;
    int i,n;

    if (gtk_init_check(&argc, &argv)) {
	/* gtk */
	toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	video = gtk_drawing_area_new();
	gtk_widget_add_events(video,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK);
	menu = create_menu(toplevel);
	gtk_container_add(GTK_CONTAINER(toplevel), video);
	g_signal_connect(toplevel, "delete-event",
			 G_CALLBACK(gtk_wm_delete_quit), NULL);
	g_signal_connect(video, "expose-event", G_CALLBACK(resize), NULL);
	g_signal_connect(video, "button-release-event",
			 G_CALLBACK(mouse_button), NULL);

	/* X11 */
	dpy = gdk_x11_display_get_xdisplay(gtk_widget_get_display(video));
	visual = gdk_x11_visual_get_xvisual(gtk_widget_get_visual(video));
	vinfo.visualid = XVisualIDFromVisual(visual);
	vinfo_list = XGetVisualInfo(dpy, VisualIDMask, &vinfo, &n);
	vinfo = vinfo_list[0];
	XFree(vinfo_list);
	x11_init_visual(dpy,&vinfo);
    }

    /* cmd line args */
    cfg_parse_cmdline(&argc,argv,cmd_opts);
    debug = GET_CMD_DEBUG();
    ng_debug = GET_CMD_DEBUG();
    if (GET_CMD_HELP()) {
	usage(argv[0]);
	return 0;
    }
    ng_init();
    ng_dsp_init(cfg_get_str(O_CMD_DSP), &devs.sndplay, 0);

    /* play files */
    if (GET_CMD_DVB()) {
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
