/*
 * dvb radio application
 *
 *   (c) 2004 Gerd Knorr <kraxel@bytesex.org>
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
#include "dvb-monitor.h"
#include "gui.h"

#define APPNAME "dvbradio"

/* ------------------------------------------------------------------------ */

/* misc globals */
int debug = 0;
Display *dpy;

static GtkWidget  *label;
static GtkWidget  *main_win;
static char       *current;

static struct media_stream *mm;

/* ------------------------------------------------------------------------ */

#define O_CMDLINE               "cmdline", "cmdline"

#define O_CMD_HELP             	O_CMDLINE, "help"
#define O_CMD_VERBOSE          	O_CMDLINE, "verbose"
#define O_CMD_DEBUG	       	O_CMDLINE, "debug"
#define O_CMD_DEVICE	       	O_CMDLINE, "device"
#define O_CMD_GEOMETRY	       	O_CMDLINE, "geometry"

#define GET_CMD_HELP()		cfg_get_bool(O_CMD_HELP,   	0)
#define GET_CMD_VERBOSE()	cfg_get_bool(O_CMD_VERBOSE,   	0)
#define GET_CMD_DEBUG()		cfg_get_int(O_CMD_DEBUG,   	0)

struct cfg_cmdline cmd_opts_only[] = {
    {
	.letter   = 'h',
	.cmdline  = "help",
	.option   = { O_CMD_HELP },
	.value    = "1",
	.desc     = "print this text",
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
	.desc     = "pick device config",

    },{
	.cmdline  = "geometry",
	.option   = { O_CMD_GEOMETRY },
	.needsarg = 1,
	.desc     = "specify window geoemtry",

    },{
	/* end of list */
    }
};

/* ------------------------------------------------------------------------ */
/* main loop                                                                */

static void tune_byname(char *station)
{
    char *list;
    char *name;

    cfg_sections_for_each("dvb-pr",list) {
	name = cfg_get_str("dvb-pr",list,"name");
	if (0 != strcasecmp(station,name))
	    continue;
	current = name;
	tune_dvb_channel(list);
	if (label)
	    gtk_label_set_text(GTK_LABEL(label),name);
	return;
    }
}

static void
dvb_loop(GMainContext *context)
{
    char path[64];
    struct ng_audio_fmt *afmt;

    if (0 == ng_mpeg_apid) {
	fprintf(stderr,"no DVB station tuned\n");
	return;
    }
    
    if (debug)
	fprintf(stderr,"%s: enter (%d)\n",__FUNCTION__,ng_mpeg_apid);

    mm = malloc(sizeof(*mm));
    memset(mm,0,sizeof(*mm));
    mm->speed = 1;

    if (!dvb_frontend_is_locked(devs.dvb))
	return;
    
    mm->reader = ng_find_reader_name("mpeg-ts");
    if (NULL == mm->reader) {
	fprintf(stderr,"Oops: transport stream parser not found\n");
	return;
    }

    snprintf(path,sizeof(path),"%s/dvr0",devs.dvbadapter);
    mm->rhandle = mm->reader->rd_open(path);
    if (NULL == mm->rhandle) {
	fprintf(stderr,"can't open: %s\n",path);
	return;
    }

    /* audio setup */
    afmt = mm->reader->rd_afmt(mm->rhandle);
    if (afmt)
	av_media_setup_audio_reader(mm,afmt);

    /* go playback stuff */
    av_media_mainloop(context, mm);

    /* cleanup */
    BUG_ON(NULL != mm->as,"mm->as isn't NULL");
    mm->reader->rd_close(mm->rhandle);
    if (mm->writer)
	av_media_stop_recording(mm);
    free(mm);
    mm = NULL;

    if (debug)
	fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return;
}

static int main_loop(GMainContext *context, char *station)
{
    /* init */
    device_init(cfg_get_str(O_CMD_DEVICE));
    if (NULL == devs.dvb) {
	fprintf(stderr,"not a dvb device\n");
    } else {
	if (station)
	    tune_byname(station);
    }

    /* mainloop */
    if (debug)
	fprintf(stderr,"main: enter main event loop... \n");
    for (;;) {
	if (exit_application)
	    break;

	/* handle all outstanding events */
	queue_run();
	while (g_main_context_iteration(context,FALSE))
	    /* nothing */;

	/* radio playback */
	if (devs.dvb) {
	    int rc;
	    /* wait for DVB tuning finish (frontend lock) */
	    do {
		rc = dvb_finish_tune(devs.dvb,100);
		while (g_main_context_iteration(context,FALSE))
		    /* nothing */;
	    } while (-1 == rc && !command_pending);
	    if (0 == rc && !command_pending)
		dvb_loop(context);
	}

	/* default/error catch */
	while (!command_pending)
	    g_main_context_iteration(context,TRUE);
    }

    /* cleanup */
    device_fini();
    return 0;
}

/* ------------------------------------------------------------------------ */
/* GUI                                                                      */

static void menu_cb_mute(void)
{
    do_va_cmd(3,"volume","mute","toggle");
}

static void menu_cb_vol_inc(void)
{
    do_va_cmd(2,"volume","inc");
}

static void menu_cb_vol_dec(void)
{
    do_va_cmd(2,"volume","dec");
}

static void menu_cb_station_next(void)
{
    char *station;

    if (current)
	station = cfg_sections_next("radio",current);
    else
	station = cfg_sections_first("radio");

    if (NULL == station)
	return;
    tune_byname(station);
    command_pending++;
}

static void menu_cb_station_prev(void)
{
    char *station = NULL;

    if (current)
	station = cfg_sections_prev("radio",current);

    if (NULL == station)
	return;
    tune_byname(station);
    command_pending++;
}

static void menu_cb_record_start(void)
{
    struct ng_writer *wr = ng_find_writer_name("mp3");
    char *recfile = record_filename("radio",current,NULL,"mp3");

    if (mm && wr) {
	if (debug)
	    fprintf(stderr,"start recording to %s\n", recfile);
	av_media_start_recording(mm, wr, recfile);
    }
}

static void menu_cb_record_stop(void)
{
    if (mm && mm->writer) {
	if (debug)
	    fprintf(stderr,"stop recording\n");
	av_media_stop_recording(mm);
    }
}

static void menu_cb_record_toggle(void)
{
    if (mm && mm->writer)
	menu_cb_record_stop();
    else
	menu_cb_record_start();
}
    
static void menu_cb_about(void)
{
    static char *text =
	"This is " APPNAME ", a dvb radio application for X11\n"
	"\n"
	THIS_IS_GPLv2
	"\n"
	"(c) 2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]";

    gtk_about_box(GTK_WINDOW(main_win), APPNAME, VERSION, text);
}

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path        = "/_File",
	.item_type   = "<Branch>",
    },{
	.path        = "/File/_Quit",
	.accelerator = "Q",
	.callback    = gtk_quit_cb,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_QUIT,
    },{

	/* --- dynamic devices/stations menus -------- */
	.path        = "/_Stations",
	.item_type   = "<Branch>",
    },{

	/* --- Commands menu ------------------------- */
	.path        = "/_Commands",
	.item_type   = "<Branch>",
    },{
	.path        = "/Commands/Next station",
	.accelerator = "space",  // Page_Up
	.callback    = menu_cb_station_next,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_GO_FORWARD,
    },{
	.path        = "/Commands/Previous station",
	.accelerator = "BackSpace",  // Page_Down
	.callback    = menu_cb_station_prev,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_GO_BACK,
    },{
	.path        = "/Commands/Start or stop _recording",
	.accelerator = "r",
	.callback    = menu_cb_record_toggle,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/sep1",
	.item_type   = "<Separator>",
    },{
	.path        = "/Commands/Mute",
	.accelerator = "KP_Enter",
	.callback    = menu_cb_mute,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/Increase Volume",
	.accelerator = "KP_Add",
	.callback    = menu_cb_vol_inc,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/Decrease Volume",
	.accelerator = "KP_Subtract",
	.callback    = menu_cb_vol_dec,
	.item_type   = "<Item>",
    },{

	/* --- Help menu ----------------------------- */
	.path        = "/_Help",
	.item_type   = "<LastBranch>",
    },{
	.path        = "/Help/_About ...",
	.callback    = menu_cb_about,
	.item_type   = "<Item>",
    }
};

static void menu_cb_tune(GtkMenuItem *menuitem, void *userdata)
{
    char *station = userdata;
    tune_byname(station);
    command_pending++;
}

static void init_channel_list(GtkWidget *parent)
{
    GtkWidget *item;
    char *list;
    char *name;

    cfg_sections_for_each("dvb-pr",list) {
	name = cfg_get_str("dvb-pr",list,"name");
	if (0 == cfg_get_str("dvb-pr",list,"audio"))
	    continue;
	if (0 != cfg_get_str("dvb-pr",list,"video"))
	    continue;
	cfg_set_str("radio",name,"pr",list);
	item = gtk_menu_item_new_with_label(name);
	gtk_menu_shell_append(GTK_MENU_SHELL(parent), item);
	g_signal_connect(G_OBJECT(item), "activate",
			 G_CALLBACK(menu_cb_tune), name);
    }
}

static void gtk_widget_font(GtkWidget *widget, char *fname)
{
    PangoFontDescription *font;
    GtkRcStyle *style;

    font = pango_font_description_from_string(fname);
    style = gtk_widget_get_modifier_style(label);
    style->font_desc = font;
    gtk_widget_modify_style(label,style);
}

static void gtk_widget_colors(GtkWidget *widget, GdkColor *fg, GdkColor *bg)
{
    int states[] = {
	GTK_STATE_INSENSITIVE,
	GTK_STATE_NORMAL,
    };
    GdkColormap *cmap;
    GtkRcStyle *style;
    int i;

    cmap = gtk_widget_get_colormap(widget);
    gdk_colormap_alloc_color(cmap, fg, FALSE, TRUE);
    gdk_colormap_alloc_color(cmap, bg, FALSE, TRUE);

    style = gtk_widget_get_modifier_style(widget);
    for (i = 0; i < DIMOF(states); i++) {
	style->bg_pixmap_name[states[i]]  = strdup("<none>");
	style->fg[states[i]]              = *fg;
	style->bg[states[i]]              = *bg;
	style->color_flags[states[i]]    |= GTK_RC_FG | GTK_RC_BG;
    }
    gtk_widget_modify_style(widget,style);
}

static void create_window(void)
{
    GdkColor black = { .red = 0x0000, .green = 0x0000, .blue = 0x0000 };
    GdkColor green = { .red = 0x0000, .green = 0xffff, .blue = 0x0000 };
    GtkWidget *vbox,*menubar,*box;
    GtkWidget *st_menu;
    GtkAccelGroup *accel_group;
    GtkItemFactory *item_factory;

    main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_win),_(APPNAME));
    g_signal_connect(main_win, "delete-event",
		     G_CALLBACK(gtk_wm_delete_quit), NULL);

    /* build menus */
    accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<radio>",
					accel_group);
    gtk_item_factory_create_items(item_factory, DIMOF(menu_items),
				  menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(main_win), accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<radio>");
    st_menu = gtk_item_factory_get_widget(item_factory,"<radio>/Stations");

    /* label widget */
    box = gtk_event_box_new();
    label = gtk_widget_new(GTK_TYPE_LABEL,
			   "label",  "no station tuned",
			   "xpad",   20,
			   "ypad",   5,
			   NULL);
    gtk_container_add(GTK_CONTAINER(box), label);
    gtk_widget_font(label, "led fixed 36");
    gtk_widget_colors(label, &green, &black);
    gtk_widget_colors(box, &green, &black);
    
    /* put stuff into boxes */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(main_win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), box, TRUE, TRUE, 0);

    init_channel_list(st_menu);
    return;
}

/* ------------------------------------------------------------------------ */
/* main function and related helpers                                        */

static void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: " APPNAME " [ options ] [ station ]\n"
	    "options:\n");

    cfg_help_cmdline(cmd_opts_only,2,16,0);
    fprintf(stderr,"\n");

    exit(0);
}

static void
parse_args(int *argc, char **argv)
{
    /* get config */
    cfg_parse_cmdline(argc,argv,cmd_opts_only);
    read_config();

    if (GET_CMD_HELP())
	usage();

    /* set debug variables */
    debug    = GET_CMD_DEBUG();
    ng_debug = debug;
    dvb_debug = debug;
}

int
main(int argc, char *argv[])
{
    /* basic init */
    ng_init();
    devlist_init(1, 0, 0);
    parse_args(&argc,argv);

    if (gtk_init_check(&argc, &argv)) {
	/* gtk window */
	create_window();
	gtk_widget_show_all(main_win);
	gtk_redirect_stderr_to_gui(GTK_WINDOW(main_win));
    }

    /* enter main loop */
    main_loop(g_main_context_default(), (argc > 1) ? argv[1] : NULL);
    fprintf(stderr,"bye...\n");
    exit(0);
}
