/*
 * dvb channel browser
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

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "devs.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "dvb-tuning.h"
#include "dvb-monitor.h"
#include "gui.h"

/* ------------------------------------------------------------------------ */

/* misc globals */
int debug = 0;
Display *dpy;

/* local globales */
static struct dvbmon *dvbmon;

/* gui widgets */
static GtkWidget     *win,*status;
static GtkTreeStore  *store;
static GtkWidget     *view;

static int current_ts;

/* ------------------------------------------------------------------------ */

enum {
    ST_COL_NAME = 0,
    ST_COL_TSID,
    ST_COL_FREQ,
    ST_COL_PNR,
    ST_COL_VIDEO,
    ST_COL_AUDIO,
    ST_COL_TELETEXT,
    ST_COL_FILL,
    ST_ATTR_RIGHT,
    ST_ATTR_WEIGHT,
    ST_NUM_COLS
};

static void menu_cb_about(void)
{
    static char *text =
	"\n"
	"This is alexplore, a DVB Channel browser & scanner\n"
	"\n"
	THIS_IS_GPLv2
	"\n"
	"(c) 1997-2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]\n"
	"\n"
	"Hints for getting started:\n"
	"  - If you don't see anything tune some random station, right\n"
	"    now this tool needs a running transport stream to get\n"
	"    started.\n"
	"  - The tool will notice any transport stream changes auto-\n"
	"    magically, it is possible to let it run in the background\n"
	"    fishing for data while your are zapping through the TV\n"
	"    stations or doing a channelscan with another tool.\n"
	"  - Activating a row (double-click or enter key) to tune that\n"
	"    transport stream.  Implemented only for DVB-T right now.\n"
	"  - Automatic scanning isn't implemented yet.\n"
	"\n";

    gtk_about_box(GTK_WINDOW(win), "alexplore", VERSION, text);
}

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path        = "/_File",
	.item_type   = "<Branch>",
    },{
	.path        = "/File/_Quit",
	.accelerator = "Q",
	.callback    = gtk_main_quit,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_QUIT,
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
static gint nmenu_items = sizeof(menu_items)/sizeof (menu_items[0]);
static GtkItemFactory *item_factory;

static void destroy(GtkWidget *widget,
		    gpointer   data)
{
    gtk_main_quit ();
}

static void activate(GtkTreeView        *treeview,
		     GtkTreePath        *path,
		     GtkTreeViewColumn  *col,
		     gpointer            userdata)
{
    GtkTreeModel *model;
    GtkTreeIter   iter;
    int   tsid;
    char  section[16];

    model = gtk_tree_view_get_model(treeview);
    if (gtk_tree_model_get_iter(model, &iter, path)) {
	gtk_tree_model_get(model, &iter, ST_COL_TSID, &tsid, -1);
	snprintf(section, sizeof(section), "%d", tsid);
	dvb_frontend_tune(devs.dvb, "dvb-ts", section);
    }
}

static gint sort_iter_compare_int(GtkTreeModel *model,
				  GtkTreeIter  *a,
				  GtkTreeIter  *b,
				  gpointer      userdata)
{
    gint sortcol = GPOINTER_TO_INT(userdata);
    gint ret = 0;
    int aa,bb;

    gtk_tree_model_get(model, a, sortcol, &aa, -1);
    gtk_tree_model_get(model, b, sortcol, &bb, -1);
    if (aa < bb)
	ret = -1;
    if (aa > bb)
	ret = 1;
    return ret;
}

static gint sort_iter_compare_str(GtkTreeModel *model,
				  GtkTreeIter  *a,
				  GtkTreeIter  *b,
				  gpointer      userdata)
{
    gint sortcol = GPOINTER_TO_INT(userdata);
    char *aa,*bb;

    gtk_tree_model_get(model, a, sortcol, &aa, -1);
    gtk_tree_model_get(model, b, sortcol, &bb, -1);
    if (NULL == aa && NULL == bb)
	return 0;
    if (NULL == aa)
	return 1;
    if (NULL == bb)
	return -1;
    return strcmp(aa,bb);
}


static void create_window(void)
{
    GtkWidget *vbox,*hbox,*menubar,*scroll;
    GtkCellRenderer *renderer;
    GtkAccelGroup *accel_group;
    GtkTreeViewColumn *col;

    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win),_("alexplore: DVB channel browser & scanner"));
    gtk_widget_set_size_request(GTK_WIDGET(win), -1, 400);
    g_signal_connect(G_OBJECT(win), "destroy",
		     G_CALLBACK(destroy), NULL);
    
    /* build menus */
    accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<browser>",
					accel_group);
    gtk_item_factory_create_items(item_factory, nmenu_items,
				  menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(win), accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<browser>");

    /* station list */
    view  = gtk_tree_view_new();
    store = gtk_tree_store_new(ST_NUM_COLS,
			       G_TYPE_STRING,   // name
			       G_TYPE_INT,      // tsid
			       G_TYPE_INT,      // freq
			       G_TYPE_INT,      // pnr
			       G_TYPE_INT,      // video
			       G_TYPE_INT,      // audio
			       G_TYPE_INT,      // teletext
			       G_TYPE_STRING,   // fill
			       G_TYPE_FLOAT,    // right aligned
			       G_TYPE_INT);     // weight
    gtk_tree_view_set_model(GTK_TREE_VIEW(view),
			    GTK_TREE_MODEL(store));
    scroll = gtk_vscrollbar_new(gtk_tree_view_get_hadjustment(GTK_TREE_VIEW(view)));
    g_signal_connect(view, "row-activated", G_CALLBACK(activate), NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Name", renderer,
	 "text",   ST_COL_NAME,
	 "weight", ST_ATTR_WEIGHT,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "TSID", renderer,
	 "text",   ST_COL_TSID,
	 "weight", ST_ATTR_WEIGHT,
	 "xalign", ST_ATTR_RIGHT,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Freq", renderer,
	 "text",    ST_COL_FREQ,
	 "visible", ST_COL_FREQ,
	 "weight",  ST_ATTR_WEIGHT,
	 "xalign",  ST_ATTR_RIGHT,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "PNR", renderer,
	 "text",    ST_COL_PNR,
	 "visible", ST_COL_PNR,
	 "xalign",  ST_ATTR_RIGHT,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Video", renderer,
	 "text",    ST_COL_VIDEO,
	 "visible", ST_COL_VIDEO,
	 "xalign",  ST_ATTR_RIGHT,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Audio", renderer,
	 "text",    ST_COL_AUDIO,
	 "visible", ST_COL_AUDIO,
	 "xalign",  ST_ATTR_RIGHT,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Teletext", renderer,
	 "text",    ST_COL_TELETEXT,
	 "visible", ST_COL_TELETEXT,
	 "xalign",  ST_ATTR_RIGHT,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "", renderer,
	 "text",    ST_COL_FILL,
	 NULL);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(view), 0);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), ST_COL_NAME,
				    sort_iter_compare_str,
                                    GINT_TO_POINTER(ST_COL_NAME), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_NAME);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(view), 1);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), ST_COL_TSID,
				    sort_iter_compare_int,
                                    GINT_TO_POINTER(ST_COL_TSID), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_TSID);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(view), 2);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), ST_COL_FREQ,
				    sort_iter_compare_int,
                                    GINT_TO_POINTER(ST_COL_FREQ), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_FREQ);

    /* other widgets */
    status = gtk_widget_new(GTK_TYPE_LABEL,
			    "label",  "status line",
			    "xalign", 0.0,
			    NULL);

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    hbox = gtk_hbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), view, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), scroll, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), status, FALSE, TRUE, 0);

    return;
}

/* ------------------------------------------------------------------------ */

static gboolean frontend_poll(gpointer data)
{
    char line[80];

    snprintf(line,sizeof(line),"Frontend %s | bit errors %d",
	     dvb_frontend_is_locked(devs.dvb) ? "locked" : "NOT LOCKED",
	     dvb_frontend_get_biterr(devs.dvb));
    gtk_label_set_label(GTK_LABEL(status),line);
    return TRUE;
}

static void ts_update()
{
    gboolean valid;
    GtkTreeIter iter;
    int id, weight;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, ST_COL_TSID, &id, -1);
	weight = (id == current_ts) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
	gtk_tree_store_set(store, &iter, ST_ATTR_WEIGHT, weight, -1);
    }
}

static void find_ts(GtkTreeIter *iter, int tsid)
{
    gboolean valid;
    int id, weight;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), iter, ST_COL_TSID, &id, -1);
	if (tsid == id)
	    return;
    }
    gtk_tree_store_append(store, iter, NULL);
    weight = (tsid == current_ts) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
    gtk_tree_store_set(store, iter,
		       ST_COL_TSID,    tsid,
		       ST_ATTR_RIGHT,  1.0,
		       ST_ATTR_WEIGHT, weight,
		       -1);
}

static void find_pr(GtkTreeIter *iter, int tsid, int pnr)
{
    gboolean valid;
    GtkTreeIter parent;
    int id;

    find_ts(&parent, tsid);
    for (valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store),iter,&parent);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), iter, ST_COL_PNR, &id, -1);
	if (pnr == id)
	    return;
    }
    gtk_tree_store_append(store, iter, &parent);
    gtk_tree_store_set(store, iter,
		       ST_COL_TSID,    tsid,
		       ST_COL_PNR,     pnr,
		       ST_ATTR_RIGHT,  1.0,
		       ST_ATTR_WEIGHT, PANGO_WEIGHT_NORMAL,
		       -1);
}

static void dvbwatch_gui(struct psi_info *info, int event,
			 int tsid, int pnr, void *data)
{
    struct psi_stream  *stream;
    struct psi_program *pr;
    GtkTreeIter iter;
    int weight;
    
    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	current_ts = tsid;
	ts_update();
	break;
    case DVBMON_EVENT_UPDATE_TS:
	stream = psi_stream_get(info, tsid, 0);
	if (!stream)
	    return;
	find_ts(&iter,stream->tsid);
	gtk_tree_store_set(store, &iter, ST_COL_NAME,  stream->net, -1);
	weight = (stream->tsid == current_ts)
	    ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
	gtk_tree_store_set(store, &iter, ST_ATTR_WEIGHT, weight, -1);
	if (stream->frequency)
	    gtk_tree_store_set(store, &iter, ST_COL_FREQ,
			       stream->frequency, -1);
	break;
    case DVBMON_EVENT_UPDATE_PR:
	pr = psi_program_get(info, tsid, pnr, 0);
	if (!pr)
	    return;
	find_pr(&iter,pr->tsid,pr->pnr);
	if (pr->name[0] != 0)
	    gtk_tree_store_set(store, &iter, ST_COL_NAME,
			       pr->name, -1);
	if (pr->v_pid)
	    gtk_tree_store_set(store, &iter, ST_COL_VIDEO,
			       pr->v_pid, -1);
	if (pr->a_pid)
	    gtk_tree_store_set(store, &iter, ST_COL_AUDIO,
			       pr->a_pid, -1);
	if (pr->t_pid)
	    gtk_tree_store_set(store, &iter, ST_COL_TELETEXT,
			       pr->t_pid, -1);
	break;
    }
}

int
main(int argc, char *argv[])
{
    ng_init();
    devlist_init(1, 0, 0);
    device_init(NULL);
    if (NULL == devs.dvb) {
	fprintf(stderr,"no dvb device found\n");
	exit(1);
    }
    dvbmon = dvbmon_init(devs.dvb, 0);

    if (gtk_init_check(&argc, &argv)) {
	/* setup gtk gui */
	create_window();
	gtk_widget_show_all(win);
	dvbmon_add_callback(dvbmon,dvbwatch_scanner,NULL);
	dvbmon_add_callback(dvbmon,dvbwatch_gui,NULL);
	g_timeout_add(1000, frontend_poll, NULL);
    } else {
	/* enter tty mode */
	fprintf(stderr,"can't open display\n");
	fprintf(stderr,"non-x11 support not there yet, sorry\n");
	exit(1);
    }

    gtk_main();
    fprintf(stderr,"bye...\n");

    dvbmon_fini(dvbmon);
    device_fini();
    exit(0);
}
