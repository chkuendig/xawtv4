#define _GNU_SOURCE 1 /* strcasestr */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "misc.h"
#include "parseconfig.h"
#include "devs.h"
#include "commands.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "gui.h"

extern int debug;

#define FOREGROUND "#606060"

/* ------------------------------------------------------------------------ */

GtkWidget *dvbscan_win;

static int           standalone;

static GtkWidget     *status;
static GtkTreeStore  *store;
static GtkTreeModel  *filter;
static GtkWidget     *view;

static char  *filter_substr;
static int   filter_free_to_air;

static int   current_tsid;
static int   last_ts_switch;
static int   scan_rescan = 6;   // enougth to refrest pmt's
static int   scan_full   = 20;  // should also catch NIT tables
static int   scan_slow   = 60;  // if full is still to fast

static int   timeout_lock = 15;
static int   timeout_pat  = 10;
static int   timeout_data;

static char  *scan_tsid;
static int   scan_tuned;
static int   scan_locked;
static gint  fe_poll_id;

static int   scan_ts_start(void);
static char* scan_ts_next(void);
static void  mark_stale(void);
static void  do_search(void);
static void  find_ts(GtkTreeIter *iter, int tsid);
static void  find_pr(GtkTreeIter *iter, int tsid, int pnr);
static void  dvbwatch_gui(struct psi_info *info, int event,
			  int tsid, int pnr, void *data);
static void scan_database(GtkItemFactory *factory, int type, char *path);

/* ------------------------------------------------------------------------ */

enum {
    ST_COL_NAME = 0,
    ST_COL_NET,
    ST_COL_TSID,
    ST_COL_COUNT,
    ST_COL_FREQ,
    ST_COL_PNR,
    ST_COL_CA,
    ST_COL_TYPE,
    ST_COL_VIDEO,
    ST_COL_AUDIO,
    ST_COL_TELETEXT,

    ST_STATE_ACTIVE,
    ST_STATE_STALE,
    ST_STATE_SCANNED,
    ST_STATE_MATCHES,
    
    ST_NUM_COLS
};

static void menu_cb_about(void)
{
    static char *text =
	"This is alexplore, a DVB Channel browser & scanner\n"
	"\n"
	THIS_IS_GPLv2
	"\n"
	"(c) 2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]\n"
	"\n"
	"Hints for getting started:\n"
	"  - The channel scan needs a starting point, thus you have to\n"
	"    tune one channel manually before starting a scan.\n"
	"  - Activate a row (double-click or enter key) to tune that\n"
	"    transport stream.";

    gtk_about_box(GTK_WINDOW(dvbscan_win), "alexplore", VERSION, text);
}

static void menu_cb_tune(void)
{
    if (dvbtune_dialog)
	gtk_widget_show(dvbtune_dialog);
    else
	create_dvbtune(GTK_WINDOW(dvbscan_win));
}

static void menu_cb_sat(void)
{
    if (satellite_dialog)
	gtk_widget_show(satellite_dialog);
    else
	create_satellite(GTK_WINDOW(dvbscan_win));
}

static void scan_drop_stale()
{
    GtkTreeModel *model = GTK_TREE_MODEL(store);
    GtkTreeIter ts,pr;
    gboolean vts,vpr;
    int tsid, pnr, stale, children;
    char *name;

    for (vts = gtk_tree_model_get_iter_first(model,&ts);
	 vts;
	 vts = gtk_tree_model_iter_next(model,&ts)) {
	children = 0;
	for (vpr = gtk_tree_model_iter_children(model,&pr,&ts);
	     vpr;
	     vpr = gtk_tree_model_iter_next(model,&pr)) {
	    gtk_tree_model_get(model, &pr,
			       ST_COL_NAME,    &name,
			       ST_COL_TSID,    &tsid,
			       ST_COL_PNR,     &pnr,
			       ST_STATE_STALE, &stale,
			       -1);
	    if (stale) {
		fprintf(stderr,"stale:   %s\n", name);
		// TODO: remove it
	    } else
		children++;
	}
	gtk_tree_model_get(model, &ts,
			   ST_COL_NAME,    &name,
			   ST_COL_TSID,    &tsid,
			   ST_STATE_STALE, &stale,
			   -1);
	if (stale && 0 == children) {
	    fprintf(stderr, "stale: %s\n", name);
	    // TODO: remove it
	}
    }
}

static void scan_do_next(int now)
{
    for (;;) {
	scan_tsid = scan_ts_next();
	if (NULL == scan_tsid)
	    break;
	scan_tuned = now;
	scan_locked = 0;
	if (0 == dvb_frontend_tune(devs.dvb, "dvb-ts", scan_tsid))
	    break;
    }
    if (NULL == scan_tsid)
	scan_drop_stale();
}

static gboolean frontend_poll(gpointer data)
{
    char line[128];
    int pos = 0, next = 0, locked;

    locked = dvb_frontend_is_locked(devs.dvb);
    if (scan_tsid) {
	int now = time(NULL);

	pos += snprintf(line+pos,sizeof(line)-pos,
			"Scan TSID %s | ", scan_tsid);

	if (locked && 0 == scan_locked)
	    scan_locked = now;
	if (!locked)
	    scan_locked = 0;

	if (!scan_locked) {
	    if (now - scan_tuned > timeout_lock) {
		pos += snprintf(line+pos,sizeof(line)-pos,
				"lock timeout");
		next = 1;
	    } else {
		pos += snprintf(line+pos,sizeof(line)-pos,
				"wait for lock (%d/%d)",
				now - scan_tuned, timeout_lock);
	    }
	} else if (last_ts_switch < scan_tuned) {
	    if (now - scan_locked > timeout_pat) {
		pos += snprintf(line+pos,sizeof(line)-pos,
				"read PAT timeout");
		next = 1;
	    } else {
		pos += snprintf(line+pos,sizeof(line)-pos,
				"wait for PAT (%d/%d)",
				now - scan_locked, timeout_pat);
	    }
	} else {
	    if (now - last_ts_switch > timeout_data) {
		pos += snprintf(line+pos,sizeof(line)-pos,
				"reading tables done");
		next = 1;
	    } else {
		pos += snprintf(line+pos,sizeof(line)-pos,
				"reading tables (%d/%d)",
				now - last_ts_switch, timeout_data);
	    }
	}
	if (next)
	    scan_do_next(now);
	pos += snprintf(line+pos,sizeof(line)-pos,
			" | ");
    }

    pos += snprintf(line+pos,sizeof(line)-pos,"Frontend %s",
		    locked ? "locked" : "NOT LOCKED");
    if (locked)
	pos += snprintf(line+pos,sizeof(line)-pos,", bit errors %d",
			dvb_frontend_get_biterr(devs.dvb));
    gtk_label_set_label(GTK_LABEL(status),line);
    return TRUE;
}

static void scan_do_start(int seconds)
{
    if (scan_tsid)
	return;

    timeout_data = seconds;
    mark_stale();
    dvbmon_refresh(devs.dvbmon);
    if (0 == scan_ts_start())
	gtk_error_box(GTK_WINDOW(dvbscan_win), "Sorry",
		      "I need at least one DVB stream as starting point\n"
		      "for a scan, please tune one from the database\n"
		      "or by specifying the parameters manually.\n");
    scan_do_next(time(NULL));
}

static void menu_cb_close(void)
{
    if (standalone) {
	gtk_main_quit();
    } else {
	gtk_widget_destroy(dvbscan_win);
    }
}

static void menu_cb_rescan(void)
{
    scan_do_start(scan_rescan);
}

static void menu_cb_full_scan(void)
{
    scan_do_start(scan_full);
}

static void menu_cb_slow_scan(void)
{
    scan_do_start(scan_slow);
}

/* ------------------------------------------------------------------------ */

static GtkTargetEntry dnd_targets[] = {
    { "UTF8_STRING",  0, ST_COL_NAME  },
    { "STRING",       0, ST_COL_NAME  },
    { "TSID",         0, ST_COL_TSID  },
    { "PNR",          0, ST_COL_PNR   },
//    { "CA",           0, ST_COL_CA    },
    { "VPID",         0, ST_COL_VIDEO },
    { "APID",         0, ST_COL_AUDIO },
};

static void drag_data_get(GtkWidget *widget,
			  GdkDragContext *dc,
			  GtkSelectionData *sd,
			  guint info, guint time, gpointer data)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    GList *rows,*item;
    char **name;
    char *buf;
    int i,len,val;

    if (debug)
	fprintf(stderr,"dnd: %d: %s\n",info,gdk_atom_name(sd->target));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    rows = gtk_tree_selection_get_selected_rows(selection, &model);

    switch (info) {
    case ST_COL_NAME:
	name = malloc(g_list_length(rows)*sizeof(char*));
	for (item = g_list_first(rows), i = 0, len = 0;
	     NULL != item;
	     item = g_list_next(item), i++) {
	    gtk_tree_model_get_iter(model, &iter, item->data);
	    gtk_tree_model_get(model, &iter, ST_COL_NAME, &name[i], -1);
	    if (debug)
		fprintf(stderr,"  %s\n",name[i]);
	    len += strlen(name[i])+1;
	}
	buf = malloc(len);
	for (item = g_list_first(rows), i = 0, len = 0;
	     NULL != item;
	     item = g_list_next(item), i++) {
	    len += sprintf(buf+len,"%s",name[i])+1;
	}
	gtk_selection_data_set(sd, gdk_atom_intern("STRING", FALSE),
			       8, buf, len);
	free(buf);
	free(name);
	break;
    case ST_COL_TSID:
    case ST_COL_PNR:
    case ST_COL_AUDIO:
    case ST_COL_VIDEO:
	buf = malloc(g_list_length(rows) * 16);
	for (item = g_list_first(rows), i = 0, len = 0;
	     NULL != item;
	     item = g_list_next(item), i++) {
	    gtk_tree_model_get_iter(model, &iter, item->data);
	    gtk_tree_model_get(model, &iter, info, &val, -1);
	    if (debug)
		fprintf(stderr,"  %d\n",val);
	    len += sprintf(buf+len,"%d",val)+1;
	}
	gtk_selection_data_set(sd, gdk_atom_intern("STRING", FALSE),
			       8, buf, len);
	free(buf);
	break;
    }
}

/* ------------------------------------------------------------------------ */

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path        = "/_File",
	.item_type   = "<Branch>",
    },{
	.path        = "/File/_Close",
	.accelerator = "<control>Q",
	.callback    = menu_cb_close,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_QUIT,
    },{

	/* --- Commands menu ------------------------- */
	.path        = "/_Commands",
	.item_type   = "<Branch>",
    },{
	.path        = "/Commands/Satellite _config ...",
	.callback    = menu_cb_sat,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/_Tune manually ...",
	.accelerator = "<control>T",
	.callback    = menu_cb_tune,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/sep1",
	.item_type   = "<Separator>",
    },{
	.path        = "/Commands/Fast _rescan",
	.accelerator = "<control>R",
	.callback    = menu_cb_rescan,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/Full _scan",
	.accelerator = "<control>S",
	.callback    = menu_cb_full_scan,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/Insane slow scan",
	.callback    = menu_cb_slow_scan,
	.item_type   = "<Item>",
    },{

	/* --- Database menu ------------------------- */
	.path        = "/_Database",
	.item_type   = "<Branch>",

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
    if (fe_poll_id) {
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(), fe_poll_id));
	fe_poll_id = 0;
    }
    dvbmon_del_callback(devs.dvbmon,dvbwatch_gui,NULL);
    dvbscan_win = 0;

    if (standalone)
	gtk_main_quit();
}

static void activate(GtkTreeView        *treeview,
		     GtkTreePath        *path,
		     GtkTreeViewColumn  *col,
		     gpointer            userdata)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    int   tsid, pnr;
    char  section[32];

    model = gtk_tree_view_get_model(treeview);
    if (!gtk_tree_model_get_iter(model, &iter, path))
	return;
    gtk_tree_model_get(model, &iter,
		       ST_COL_TSID, &tsid,
		       ST_COL_PNR,  &pnr,
		       -1);

    if (standalone) {
	snprintf(section, sizeof(section), "%d", tsid);
	dvb_frontend_tune(devs.dvb, "dvb-ts", section);
	return;
    } else {
	snprintf(section, sizeof(section), "%d-%d", tsid, pnr);
	do_va_cmd(2, "setdvb", section);
    }
}

static void search_text_changed(GtkWidget *search)
{
    const char *text = gtk_entry_get_text(GTK_ENTRY(search));

    if (filter_substr) {
	free(filter_substr);
	filter_substr = NULL;
    }
    if (text && strlen(text) > 0)
	filter_substr = strdup(text);
	
    if (debug)
	fprintf(stderr,"search text: \"%s\"\n", filter_substr);
    do_search();
}

static void fta_button_changed(GtkWidget *btn)
{
    filter_free_to_air = gtk_toggle_tool_button_get_active
	(GTK_TOGGLE_TOOL_BUTTON(btn));
    if (debug)
	fprintf(stderr,"filter fta: %s\n",
		filter_free_to_air ? "yes" : "no");
    do_search();
}

static void dvbscan_init_gui(void)
{
    GtkTreeIter iter;
    int tsid,pnr;
    char *list,*val;

    cfg_sections_for_each("dvb-ts",list) {
	if (1 != sscanf(list,"%d",&tsid))
	    continue;
	find_ts(&iter,tsid);
	if (NULL != (val = cfg_get_str("dvb-ts",list,"name")))
	    gtk_tree_store_set(store, &iter, ST_COL_NAME, val, -1);
	gtk_tree_store_set(store, &iter,
			   ST_COL_FREQ, cfg_get_int("dvb-ts",list,"frequency",0),
			   -1);
    }
    cfg_sections_for_each("dvb-pr",list) {
	if (2 != sscanf(list,"%d-%d",&tsid,&pnr))
	    continue;
	find_pr(&iter,tsid,pnr);
	if (NULL != (val = cfg_get_str("dvb-pr",list,"name")))
	    gtk_tree_store_set(store, &iter, ST_COL_NAME, val, -1);
	if (NULL != (val = cfg_get_str("dvb-pr",list,"net")))
	    gtk_tree_store_set(store, &iter, ST_COL_NET, val, -1);
	gtk_tree_store_set(store, &iter,
			   ST_COL_CA,       cfg_get_int("dvb-pr",list,"ca",0),
			   ST_COL_TYPE,     cfg_get_int("dvb-pr",list,"type",0),
			   ST_COL_VIDEO,    cfg_get_int("dvb-pr",list,"video",0),
			   ST_COL_AUDIO,    cfg_get_int("dvb-pr",list,"audio",0),
			   ST_COL_TELETEXT, cfg_get_int("dvb-pr",list,"teletext",0),
			   -1);
    }
    mark_stale();

    dvbmon_add_callback(devs.dvbmon,dvbwatch_gui,NULL);
    dvbmon_refresh(devs.dvbmon);
    fe_poll_id = g_timeout_add(1000, frontend_poll, NULL);
}

static struct toolbarbutton toolbaritems[] = {
    {
	.text     = "tune",
	.tooltip  = "tune manually",
//	.stock    = GTK_STOCK_,
	.callback = menu_cb_tune,
    },{
	.text     = "scan",
	.tooltip  = "full channel scan",
	.stock    = GTK_STOCK_FIND,
	.callback = menu_cb_full_scan,
    },{
	.text     = "close",
	.tooltip  = "close window",
	.stock    = GTK_STOCK_QUIT,
	.callback = menu_cb_close,
    },{
	/* nothing */
    },{
	.text     = "fta",
	.tooltip  = "show only free to air channels",
	.toggle   = 1,
	.callback = G_CALLBACK(fta_button_changed),
    },{
	/* nothing */
    }
};

void dvbscan_create_window(int s)
{
    GtkWidget *vbox,*menubar,*scroll;
    GtkWidget *handlebox,*toolbar,*label,*search;
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkAccelGroup *accel_group;

    standalone = s;
    dvbscan_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(dvbscan_win), standalone
			 ? _("alexplore: DVB channel browser & scanner")
			 : _("DVB channels"));
    gtk_widget_set_size_request(GTK_WIDGET(dvbscan_win), -1, 400);
    g_signal_connect(G_OBJECT(dvbscan_win), "destroy",
		     G_CALLBACK(destroy), NULL);
    
    /* build menus */
    accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<browser>",
					accel_group);
    gtk_item_factory_create_items(item_factory, nmenu_items,
				  menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(dvbscan_win), accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<browser>");
    if (!standalone) {
	GtkWidget *noabout;
	noabout = gtk_item_factory_get_widget(item_factory,"<browser>/Help/About ...");
	gtk_widget_destroy(noabout);
    }

    /* db menu */
    scan_database(item_factory, dvb_frontend_get_type(devs.dvb),
		  "/usr/share/dvb");

    /* toolbar */
    toolbar = gtk_toolbar_build(toolbaritems, DIMOF(toolbaritems), NULL);
    label = gtk_label_new(" Search: ");
    gtk_toolbar_add_widget(toolbar,label,-1);
    search = gtk_entry_new_with_max_length(32);
    gtk_toolbar_add_widget(toolbar,search,-1);
    g_signal_connect(search, "activate",
		     G_CALLBACK(search_text_changed), NULL);

    /* station list */
    view  = gtk_tree_view_new();
    store = gtk_tree_store_new(ST_NUM_COLS,
			       G_TYPE_STRING,   // name
			       G_TYPE_STRING,   // net
			       G_TYPE_INT,      // tsid
			       G_TYPE_INT,      // count
			       G_TYPE_INT,      // freq
			       G_TYPE_INT,      // pnr
			       G_TYPE_INT,      // ca
			       G_TYPE_INT,      // type
			       G_TYPE_INT,      // video
			       G_TYPE_INT,      // audio
			       G_TYPE_INT,      // teletext

			       G_TYPE_BOOLEAN,  // active
			       G_TYPE_BOOLEAN,  // stale
			       G_TYPE_BOOLEAN,  // scanned
			       G_TYPE_BOOLEAN); // matches

    filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(store),NULL);
    gtk_tree_model_filter_set_visible_column(GTK_TREE_MODEL_FILTER(filter),
					     ST_STATE_MATCHES);
    gtk_tree_view_set_model(GTK_TREE_VIEW(view), filter);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    g_signal_connect(view, "row-activated", G_CALLBACK(activate), NULL);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),
				GTK_SELECTION_MULTIPLE);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Name", renderer,
	 "text",           ST_COL_NAME,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Net", renderer,
	 "text",           ST_COL_NET,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);


    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "TSID", renderer,
	 "text",           ST_COL_TSID,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "#", renderer,
	 "text",           ST_COL_COUNT,
	 "visible",        ST_COL_COUNT,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Freq", renderer,
	 "text",           ST_COL_FREQ,
	 "visible",        ST_COL_FREQ,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "PNR", renderer,
	 "text",           ST_COL_PNR,
	 "visible",        ST_COL_PNR,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "CA", renderer,
	 "text",           ST_COL_CA,
	 "visible",        ST_COL_CA,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Type", renderer,
	 "text",           ST_COL_TYPE,
	 "visible",        ST_COL_TYPE,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Video", renderer,
	 "text",           ST_COL_VIDEO,
	 "visible",        ST_COL_VIDEO,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Audio", renderer,
	 "text",           ST_COL_AUDIO,
	 "visible",        ST_COL_AUDIO,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "xalign",      1.0,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  FOREGROUND,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Teletext", renderer,
	 "text",           ST_COL_TELETEXT,
	 "visible",        ST_COL_TELETEXT,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_STALE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "", renderer,
	 NULL);

    /* configure */
    col = gtk_tree_view_get_column(GTK_TREE_VIEW(view), ST_COL_NAME);
    gtk_tree_view_column_set_resizable(col,True);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(view), ST_COL_NET);
    gtk_tree_view_column_set_resizable(col,True);

    /* dnd */
    gtk_drag_source_set(view,
			GDK_BUTTON1_MASK,
			dnd_targets, DIMOF(dnd_targets),
			GDK_ACTION_COPY);
    g_signal_connect(view, "drag-data-get",
		     G_CALLBACK(drag_data_get), NULL);
    
    /* other widgets */
    status = gtk_widget_new(GTK_TYPE_LABEL,
			    "label",  "status line",
			    "xalign", 0.0,
			    NULL);

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    handlebox = gtk_handle_box_new();
    gtk_container_add(GTK_CONTAINER(handlebox), toolbar);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(dvbscan_win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), handlebox, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_end(GTK_BOX(vbox), status, FALSE, TRUE, 0);

    /* fill data */
    dvbscan_init_gui();
    return;
}

/* ------------------------------------------------------------------------ */

static void switch_ts(void)
{
    gboolean valid,active;
    GtkTreeIter iter;
    int tsid;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, ST_COL_TSID, &tsid, -1);
	active = (tsid == current_tsid) ? TRUE : FALSE;
	gtk_tree_store_set(store, &iter, ST_STATE_ACTIVE, active, -1);
	if (active)
	    gtk_tree_store_set(store, &iter, ST_STATE_STALE, FALSE, -1);
    }
}

static int scan_ts_start(void)
{
    gboolean valid;
    GtkTreeIter iter;
    int count = 0;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&iter)) {
	gtk_tree_store_set(store, &iter, ST_STATE_SCANNED, FALSE, -1);
	count++;
    }
    return count;
}

static char* scan_ts_next(void)
{
    static char str[16];
    gboolean valid, scanned;
    GtkTreeIter iter;
    int tsid;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
			   ST_STATE_SCANNED, &scanned,
			   ST_COL_TSID,      &tsid,
			   -1);
	if (scanned)
	    continue;
	gtk_tree_store_set(store, &iter, ST_STATE_SCANNED, TRUE, -1);
	snprintf(str,sizeof(str),"%d",tsid);
	return str;
    }
    return NULL;
}

static void find_ts(GtkTreeIter *iter, int tsid)
{
    gboolean valid, active;
    int id;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), iter, ST_COL_TSID, &id, -1);
	if (tsid == id)
	    return;
    }
    gtk_tree_store_append(store, iter, NULL);
    active = (tsid == current_tsid) ? TRUE : FALSE;
    gtk_tree_store_set(store, iter,
		       ST_COL_TSID,      tsid,
		       ST_STATE_ACTIVE,  active,
		       ST_STATE_MATCHES, TRUE,
		       -1);
}

static void find_pr(GtkTreeIter *iter, int tsid, int pnr)
{
    gboolean valid;
    GtkTreeIter parent;
    int id,count;

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
		       ST_COL_TSID,      tsid,
		       ST_COL_PNR,       pnr,
		       ST_STATE_MATCHES, TRUE,
		       -1);

    gtk_tree_model_get(GTK_TREE_MODEL(store), &parent, ST_COL_COUNT, &count, -1);
    count++;
    gtk_tree_store_set(store, &parent, ST_COL_COUNT, count, -1);
}

static void mark_stale(void)
{
    GtkTreeIter ts,pr;
    gboolean vts,vpr;

    for (vts = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&ts);
	 vts;
	 vts = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&ts)) {
	gtk_tree_store_set(store, &ts, ST_STATE_STALE, TRUE, -1);
	for (vpr = gtk_tree_model_iter_children(GTK_TREE_MODEL(store),&pr,&ts);
	     vpr;
	     vpr = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&pr)) {
	    gtk_tree_store_set(store, &pr, ST_STATE_STALE, TRUE, -1);
	}
    }
}

static gboolean filter_line(GtkTreeIter *pr)
{
    gboolean matches = FALSE;
    char *name, *net;
    gint ca;

    gtk_tree_model_get(GTK_TREE_MODEL(store), pr,
		       ST_COL_NAME,    &name,
		       ST_COL_NET,     &net,
		       ST_COL_CA,      &ca,
		       -1);

    if (NULL != filter_substr) {
	if (NULL != name  &&  NULL != strcasestr(name,filter_substr))
	    matches = TRUE;
	if (NULL != net   &&  NULL != strcasestr(net,filter_substr))
	    matches = TRUE;
    } else
	matches = TRUE;

    if (filter_free_to_air  &&  0 != ca)
	matches = FALSE;
    gtk_tree_store_set(store, pr, ST_STATE_MATCHES, matches, -1);
    return matches;
}

static void do_search(void)
{
    GtkTreeIter ts,pr;
    gboolean vts,vpr,matches,submatch;

    for (vts = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&ts);
	 vts;
	 vts = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&ts)) {
	submatch = FALSE;
	gtk_tree_store_set(store, &ts, ST_STATE_MATCHES, TRUE, -1);
	for (vpr = gtk_tree_model_iter_children(GTK_TREE_MODEL(store),&pr,&ts);
	     vpr;
	     vpr = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&pr)) {
	    matches = filter_line(&pr);
	    if (matches)
		submatch = TRUE;
	}
	if (NULL == filter_substr && !filter_free_to_air) {
	    /* no filters active */
	    submatch = TRUE;
	}
	gtk_tree_store_set(store, &ts, ST_STATE_MATCHES, submatch, -1);
	if (submatch)
	    gtk_tree_view_expand_row(GTK_TREE_VIEW(view),
				     gtk_tree_model_get_path(GTK_TREE_MODEL(store),&ts),
				     TRUE);
    }
    gtk_tree_view_expand_all(GTK_TREE_VIEW(view));
}

static void dvbwatch_gui(struct psi_info *info, int event,
			 int tsid, int pnr, void *data)
{
    struct psi_stream  *stream;
    struct psi_program *pr;
    GtkTreeIter iter;
    gboolean active;
    
    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	current_tsid = tsid;
	last_ts_switch = time(NULL);
	switch_ts();
	break;
    case DVBMON_EVENT_UPDATE_TS:
	stream = psi_stream_get(info, tsid, 0);
	if (!stream)
	    return;
	find_ts(&iter,stream->tsid);
	active = (tsid == current_tsid) ? TRUE : FALSE;
	gtk_tree_store_set(store, &iter,
			   ST_COL_NAME,     stream->net,
			   ST_STATE_ACTIVE, active,
			   ST_STATE_STALE,  FALSE,
			   -1);
	if (stream->frequency)
	    gtk_tree_store_set(store, &iter, ST_COL_FREQ,
			       stream->frequency, -1);
	break;
    case DVBMON_EVENT_UPDATE_PR:
	pr = psi_program_get(info, tsid, pnr, 0);
	if (!pr)
	    return;
	find_pr(&iter,pr->tsid,pr->pnr);
	gtk_tree_store_set(store, &iter,
			   ST_STATE_STALE,  FALSE,
			   -1);
	if (pr->name[0] != 0)
	    gtk_tree_store_set(store, &iter, ST_COL_NAME,
			       pr->name, -1);
	if (pr->net[0] != 0)
	    gtk_tree_store_set(store, &iter, ST_COL_NET,
			       pr->net, -1);
	if (pr->ca)
	    gtk_tree_store_set(store, &iter, ST_COL_CA,
			       pr->ca, -1);
	if (pr->type)
	    gtk_tree_store_set(store, &iter, ST_COL_TYPE,
			       pr->type, -1);
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

/* ------------------------------------------------------------------------ */

static void dbentry_cb(gpointer data)
{
    char *d = "tmp";
    char *s = "dvbtune";
    char freq[16] = "";
    char sym[16]  = "";
    char r_hi[8]  = "";
    char r_lo[8]  = "";
    char pol[4]   = "";
    char bw[4]    = "";
    char mo[4]    = "";
    char tr[4]    = "";
    char gu[4]    = "";
    char hi[4]    = "";
    char *line = data;

    switch (line[0]) {
    case 'T':
	sscanf(line, "T %15[0-9] %3[0-9]MHz %7s %7s "
	       "QAM%3[0-9] %3[0-9]k 1/%3[0-9] %8s",
	       freq, bw, r_hi, r_lo, mo, tr, gu, hi);
	break;
    case 'C':
	sscanf(line, "C %15[0-9] %15[0-9] %7s QAM%3[0-9]",
	       freq, sym, r_hi, mo);
	break;
    case 'S':
	sscanf(line, "S %15[0-9] %3s %15[0-9] %7s",
	       freq, pol, sym, r_hi);
	if (dvbtune_lnb)
	    cfg_set_str(d, s, "lnb", dvbtune_lnb);
	if (dvbtune_sat)
	    cfg_set_str(d, s, "sat", dvbtune_sat);
	break;
    }

    if (strlen(freq) > 0)
	cfg_set_str(d, s, "frequency", freq);
    if (strlen(pol) > 0)
	cfg_set_str(d, s, "polarization", pol);
    if (strlen(sym) > 0)
	cfg_set_str(d, s, "symbol_rate", sym);
    if (strlen(bw) > 0)
	cfg_set_str(d, s, "bandwidth", bw);
    if (strlen(mo) > 0)
	cfg_set_str(d, s, "modulation", mo);
    if (strlen(tr) > 0)
	cfg_set_str(d, s, "transmission", tr);
    if (strlen(gu) > 0)
	cfg_set_str(d, s, "guard_interval", gu);

#if 0
    /* FIXME (or depend on "auto" working ok ... ) */
    cfg_set_str(d, s, "code_rate_high", r_hi);
    cfg_set_str(d, s, "code_rate_low", r_lo);
    cfg_set_str(d, s, "hierarchy", hi);
#endif

    dvb_frontend_tune(devs.dvb, d, s);
    cfg_del_section(d, s);
    dvbmon_refresh(devs.dvbmon);
}
    
static void add_dbentry(GtkItemFactory *factory, char *filename, int nr, char *line)
{
    char *basename;
    char guipath[128];
    GtkItemFactoryEntry entry;
    int i,freq;

    basename = strrchr(filename,'/');
    if (NULL == basename)
	return;
    sscanf(line+2,"%d",&freq);
    snprintf(guipath, sizeof(guipath),
	     "/Database/%s/#%d (%d)", basename+1, nr, freq);
    basename = guipath+10;
    for (i = 0; basename[i] != 0; i++) {
	switch (basename[i]) {
	case '-':
	    basename[i] = (2 == i) ? '/' : ' ';
	    break;
	case '_':
	    basename[i] = ' ';
	    break;
	}
    }
    memset(&entry,0,sizeof(entry));
    entry.path      = guipath;
    entry.callback  = dbentry_cb;
    entry.item_type = "<Item>";
    gtk_item_factory_create_items(factory, 1, &entry, strdup(line));
}

static void scan_dbfile(GtkItemFactory *factory, int type, char *filename)
{
    FILE *fp;
    char line[128];
    int count = 1;

    if (NULL == (fp = fopen(filename,"r")))
	return;
    while (NULL != fgets(line,sizeof(line),fp)) {
	if (line[0] == '#')  // comment
	    continue;
	if (line[0] == '\n') // empty line
	    continue;
	if (line[1] != ' ') {
//	    fprintf(stderr,"ERROR: %s",line);
	    goto out;
	}
	switch (line[0]) {
	case 'T':
	    if (FE_OFDM == type)
		add_dbentry(factory, filename, count++, line);
	    break;
	case 'C':
	    if (FE_QAM == type)
		add_dbentry(factory, filename, count++, line);
	    break;
	case 'S':
	    if (FE_QPSK == type)
		add_dbentry(factory, filename, count++, line);
	    break;
	default:
//	    fprintf(stderr,"ERROR: %s",line);
	    goto out;
	}
    }

out:
    fclose(fp);
}

static void scan_database(GtkItemFactory *factory, int type, char *dirpath)
{
    struct dirent *ent;
    struct stat st;
    char filename[128];
    DIR *dir;

    dir = opendir(dirpath);
    if (NULL == dir)
	return;
    while (NULL != (ent = readdir(dir))) {
	if ('.' == ent->d_name[0])
	    continue;
	snprintf(filename,sizeof(filename),
		 "%s/%s",dirpath,ent->d_name);

        if (ent->d_type != DT_UNKNOWN) {
	    st.st_mode = DTTOIF(ent->d_type);
        } else {
            if (-1 == lstat(filename, &st))
		continue;
	}

        if (S_ISDIR(st.st_mode))
	    scan_database(factory, type, filename);
        if (S_ISREG(st.st_mode))
	    scan_dbfile(factory, type, filename);
    }
    closedir(dir);
}
