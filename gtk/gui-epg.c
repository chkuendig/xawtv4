#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "dvb.h"
#include "parseconfig.h"
#include "commands.h"
#include "gui.h"

GtkWidget* epg_win;

static int standalone;

static GtkWidget* epg_textview;
static GtkWidget* epg_treeview;
static GtkWidget* epg_status;

static GtkListStore *epg_store;
static GtkTreeModel *epg_store_filter;

#define USE_MATCH_COL 1

/* ------------------------------------------------------------------------ */

enum {
    /* identify entry */
    EPG_COL_TSID,
    EPG_COL_PNR,
    EPG_COL_ID,

    /* time */
    EPG_COL_START,
    EPG_COL_STOP,

    /* properties & descriptions */
    EPG_COL_LANG,
    EPG_COL_NAME,
    EPG_COL_STEXT,
    EPG_COL_ETEXT,

    /* attributes */
    EPG_COL_MATCHES,

    /* thats it ;) */
    EPG_NUM_COLS,
};

enum epg_filter {
    EPG_FILTER_NOFILTER,
    EPG_FILTER_NOW,
    EPG_FILTER_NEXT,
    EPG_FILTER_STATION,
    EPG_FILTER_TEXT,
};

static enum epg_filter cur_filter = EPG_FILTER_NOFILTER;

/* EPG_FILTER_STATION */
static int epg_filter_tsid = -1;
static int epg_filter_pnr = -1;

/* callbacks */
static void do_refresh(void);
static void do_filter_nofilter(void);
static void do_filter_now(void);
// static void do_filter_next(void);
static void do_filter_station(void);

static gboolean is_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static void do_filter_one(GtkTreeModel* model, GtkTreeIter *iter);
static void do_filter_all(void);

static void epgwin_textview_show_entry(GtkTextView* textview,
				       GtkTreeModel *model,
				       GtkTreeIter *tree);
static int  epgwin_reload_items();
static void selection_changed(GtkTreeSelection *sel, GtkTextView* textview);

static void epg_row_activated(GtkTreeView* treeview,
			      GtkTreePath *path,
			      GtkTreeViewColumn* col,
			      GtkWidget* epg_win)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    gint tsid,pnr;
    char buf[64];
    
    model = gtk_tree_view_get_model(treeview);
    if (gtk_tree_model_get_iter(model, &iter, path)) {
	gtk_tree_model_get(model, &iter,
			   EPG_COL_TSID, &tsid,
			   EPG_COL_PNR,  &pnr,
			   -1);
	snprintf(buf, sizeof(buf), "%d-%d", tsid, pnr);
	do_va_cmd(2,"setdvb",buf);
    }
}

static void selection_changed(GtkTreeSelection *sel, GtkTextView* textview)
{
    GtkTreeModel* model;
    GtkTreeIter iter;

    if(!gtk_tree_selection_get_selected (sel, &model, &iter))
	return;

    epgwin_textview_show_entry(textview, model, &iter);
}

static void create_tags(GtkWidget* textview)
{
    GtkTextBuffer* buffer;
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    gtk_text_buffer_create_tag(buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag(buffer, "word_wrap", "wrap_mode", GTK_WRAP_WORD, NULL);
}

static void data_func_start(GtkTreeViewColumn *col,
			    GtkCellRenderer   *renderer,
			    GtkTreeModel      *model,
			    GtkTreeIter       *iter,
			    gpointer           user_data)
{
    gint val;
    time_t start;
    struct tm *tm;
    char buf[20];
    char *fmt = user_data;
    
    gtk_tree_model_get(model, iter, EPG_COL_START, &val, -1);
    start = val;
    tm = localtime(&start);
    strftime(buf, sizeof(buf), fmt, tm);
    g_object_set(renderer, "text", buf, NULL);
}

static void data_func_station(GtkTreeViewColumn *col,
			      GtkCellRenderer   *renderer,
			      GtkTreeModel      *model,
			      GtkTreeIter       *iter,
			      gpointer           user_data)
{
    gint tsid,pnr;
    char buf[20], *name;

    gtk_tree_model_get(model, iter,
		       EPG_COL_TSID, &tsid,
		       EPG_COL_PNR,  &pnr,
		       -1);
    snprintf(buf,sizeof(buf),"%d-%d",tsid,pnr);
    name = cfg_get_str("dvb-pr",buf,"name");
    g_object_set(renderer, "text", name ? name : buf, NULL);
}

static void menu_cb_close(void)
{
    if (standalone) {
	gtk_main_quit();
    } else {
	gtk_widget_hide(epg_win);
    }
}

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

#if 0
    },{
	/* --- Help menu ----------------------------- */
	.path        = "/_Help",
	.item_type   = "<LastBranch>",
    },{
	.path        = "/Help/_About ...",
	.callback    = menu_cb_about,
	.item_type   = "<Item>",
#endif
    }
};
static gint nmenu_items = sizeof(menu_items)/sizeof (menu_items[0]);
static GtkItemFactory *item_factory;

static struct toolbarbutton toolbaritems[] = {
    {
	.text     = "station",
	.tooltip  = "current station",
	.stock    = GTK_STOCK_INDEX,
	.callback = do_filter_station,
    },{
	.text     = "now",
	.tooltip  = "sending now",
	.stock    = GTK_STOCK_GO_DOWN,
	.callback = do_filter_now,
#if 0
    },{
	.text     = "next",
	.tooltip  = "sending soon",
	.stock    = GTK_STOCK_GO_FORWARD,
	.callback = do_filter_next,
#endif
    },{
	.text     = "no filter",
	.tooltip  = "show all entries",
	.stock    = GTK_STOCK_JUSTIFY_FILL,
	.callback = do_filter_nofilter,
    },{
	/* nothing */
    },{
	.text     = "refresh",
	.tooltip  = "reload epg list",
	.stock    = GTK_STOCK_REFRESH,
	.callback = do_refresh,
    },{
	/* nothing */
    }
};

void create_epgwin(GtkWindow* parent)
{
    GtkTreeSelection *sel;
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkWidget *paned,*vbox,*scroll,*frame,*toolbar,*menubar,*handlebox;
    GtkAccelGroup *accel_group;

    epg_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(epg_win),_("EPG"));
    if (parent) {
	g_signal_connect(epg_win, "delete-event",
			 G_CALLBACK(gtk_widget_hide_on_delete), NULL);
#if 0
	gtk_window_set_transient_for(GTK_WINDOW(epg_win), parent);
#endif
    } else {
	/* standalone */
	g_signal_connect(G_OBJECT(epg_win), "destroy",
			 G_CALLBACK(gtk_main_quit), NULL);
	standalone = 1;
    }

    /* build menus */
    accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<epg>",
					accel_group);
    gtk_item_factory_create_items(item_factory, nmenu_items,
				  menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(epg_win), accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<epg>");

    /* toolbar */
    toolbar = gtk_toolbar_build(toolbaritems, DIMOF(toolbaritems), NULL);
#if 0
    label = gtk_label_new(" Search: ");
    gtk_toolbar_add_widget(toolbar,label,-1);
    search = gtk_entry_new_with_max_length(32);
    gtk_toolbar_add_widget(toolbar,search,-1);
    g_signal_connect(search, "activate",
		     G_CALLBACK(search_text_changed), NULL);
#endif
    handlebox = gtk_handle_box_new();
    gtk_container_add(GTK_CONTAINER(handlebox), toolbar);

    /* status line */
    epg_status = gtk_widget_new(GTK_TYPE_LABEL,
				"label",  "not used (yet) status line",
				"xalign", 0.0,
				NULL);

    /* window layout */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(epg_win), vbox);
    gtk_widget_set_size_request (epg_win, 400, 400);

    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), handlebox, FALSE, TRUE, 0);
    paned = gtk_vpaned_new();
    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), epg_status, FALSE, TRUE, 0);

    /* setup tree view */
    epg_treeview = gtk_tree_view_new();
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), epg_treeview);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(frame), scroll);
    gtk_widget_set_size_request (frame, -1, 40);
    gtk_paned_pack1(GTK_PANED(paned), frame, TRUE, FALSE);
    
    /* setup textview */
    epg_textview = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(epg_textview), FALSE);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), epg_textview);

    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(frame), scroll);
    gtk_widget_set_size_request (frame, -1, 60);
    gtk_paned_pack2(GTK_PANED(paned), frame, TRUE, FALSE);
    
    /* setup store */
    epg_store = gtk_list_store_new(EPG_NUM_COLS,
				   G_TYPE_INT,     // EPG_COL_TSID,
				   G_TYPE_INT,     // EPG_COL_PNR,
				   G_TYPE_INT,     // EPG_COL_ID,

				   G_TYPE_INT,     // EPG_COL_START,
				   G_TYPE_INT,     // EPG_COL_END,

				   G_TYPE_STRING,  // EPG_COL_NAME,
				   G_TYPE_STRING,  // EPG_COL_STEXT,
				   G_TYPE_STRING,  // EPG_COL_ETEXT,
				   G_TYPE_STRING,  // EPG_COL_LANG,

				   G_TYPE_BOOLEAN, // EPG_COL_MATCHES,
				   NULL);

    epg_store_filter = gtk_tree_model_filter_new(GTK_TREE_MODEL(epg_store), NULL);
#if USE_MATCH_COL
    gtk_tree_model_filter_set_visible_column(GTK_TREE_MODEL_FILTER(epg_store_filter),
					     EPG_COL_MATCHES);
#else
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(epg_store_filter),
					   is_visible, NULL, NULL);
#endif
    gtk_tree_view_set_model(GTK_TREE_VIEW(epg_treeview), epg_store_filter);

    g_signal_connect(epg_treeview, "row-activated",
		     G_CALLBACK(epg_row_activated), epg_win);

    /* date */
    renderer = gtk_cell_renderer_text_new ();
    col = gtk_tree_view_column_new_with_attributes(_("Date"), renderer, NULL);
    gtk_tree_view_column_set_cell_data_func
	(col, renderer, data_func_start, "%a, %d.%m.", NULL);
    gtk_tree_view_insert_column(GTK_TREE_VIEW(epg_treeview), col, -1);

    /* time */
    renderer = gtk_cell_renderer_text_new ();
    col = gtk_tree_view_column_new_with_attributes(_("Time"), renderer, NULL);
    gtk_tree_view_column_set_cell_data_func
	(col, renderer, data_func_start, "%H:%M", NULL);
    gtk_tree_view_insert_column(GTK_TREE_VIEW(epg_treeview), col, -1);

    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(epg_store), EPG_COL_START,
				    gtk_sort_iter_compare_int,
                                    GINT_TO_POINTER(EPG_COL_START), NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(epg_store),
					 EPG_COL_START, GTK_SORT_ASCENDING);

#if 0
    /* FIXME: doesn't work -- will try to sort the epg_store_filter */
    gtk_tree_view_column_set_sort_column_id(col, EPG_COL_START);
#endif

    /* station */
    renderer = gtk_cell_renderer_text_new ();
    col = gtk_tree_view_column_new_with_attributes(_("Station"), renderer, NULL);
    gtk_tree_view_column_set_cell_data_func
	(col, renderer, data_func_station, NULL, NULL);
    gtk_tree_view_column_set_resizable(col, True);
    gtk_tree_view_insert_column(GTK_TREE_VIEW(epg_treeview), col, -1);

    /* name */
    renderer = gtk_cell_renderer_text_new ();
    col = gtk_tree_view_column_new_with_attributes
	(_("Name"), renderer,
	 "text", EPG_COL_NAME,
	 NULL);
    gtk_tree_view_column_set_resizable(col, True);
    gtk_tree_view_insert_column(GTK_TREE_VIEW(epg_treeview), col, -1);

    /* default sort order is unsorted, needed for mass-insertions of
     * rows without mass-resorting ;) */
#if 0
    /* doesn't work as documented :-/ */
    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(epg_store),
					    NULL, NULL, NULL);
#else
    /* dirty workaround */
    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(epg_store),
					    gtk_sort_iter_compare_eq, NULL, NULL);
#endif

    /* epg description */
    create_tags(epg_textview);

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(epg_treeview));
    g_signal_connect(sel, "changed",
		     G_CALLBACK(selection_changed), epg_textview);
}

static int epgwin_find_item(GtkTreeIter *iter,
			    int tsid, int pnr, int id)
{
    gint this_tsid, this_pnr, this_id;
    GtkTreeModel* model = GTK_TREE_MODEL(epg_store);
    gboolean ok;

    for (ok = gtk_tree_model_get_iter_first(model,iter);
	 ok;
	 ok = gtk_tree_model_iter_next(model,iter)) {
	gtk_tree_model_get(model, iter,
			   EPG_COL_TSID, &this_tsid,
			   EPG_COL_PNR,  &this_pnr,
			   EPG_COL_ID,   &this_id,
			   -1);
	if (this_id != id)
	    continue;
	if (this_pnr != pnr)
	    continue;
	if (this_tsid != tsid)
	    continue;
	return 0;
    }
    return -1;
}

static void epgwin_add_item(GtkTreeIter *iter, struct epgitem* epg)
{
    gtk_list_store_append(epg_store, iter);
    gtk_list_store_set(epg_store, iter,
		       EPG_COL_TSID,     epg->tsid,
		       EPG_COL_PNR,      epg->pnr,
		       EPG_COL_ID,       epg->id,
		       
		       EPG_COL_START,    epg->start,
		       EPG_COL_STOP,     epg->stop,
		       EPG_COL_NAME,     epg->name,
		       EPG_COL_STEXT,    epg->stext,
		       EPG_COL_ETEXT,    epg->etext,

		       EPG_COL_MATCHES,  False,
		       -1);
    do_filter_one(GTK_TREE_MODEL(epg_store), iter);
}

#if 0
static void epgwin_update_item(GtkTreeIter *iter, struct epgitem* epg)
{
    gtk_list_store_set(epg_store, iter,
		       EPG_COL_START,    epg->start,
		       EPG_COL_STOP,     epg->stop,
		       EPG_COL_NAME,     epg->name,
		       EPG_COL_STEXT,    epg->stext,
		       EPG_COL_ETEXT,    epg->etext,
		       -1);
    do_filter_one(GTK_TREE_MODEL(epg_store), iter, time(NULL));
}
#endif

static void mass_row_update(int begin)
{
    static GtkSortType order;
    static gboolean sorted;
    static gint id;

    if (begin) {
	g_object_ref(epg_store_filter);
	gtk_tree_view_set_model(GTK_TREE_VIEW(epg_treeview), NULL);
	sorted = gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(epg_store),
						      &id, &order);
	if (sorted)
	    gtk_tree_sortable_set_sort_column_id
		(GTK_TREE_SORTABLE(epg_store),
		 GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, 0);
    } else {
	if (sorted)
	    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(epg_store),
						 id, order);
	gtk_tree_view_set_model(GTK_TREE_VIEW(epg_treeview),
				GTK_TREE_MODEL(epg_store_filter));
	g_object_unref(epg_store_filter);
    }
}

static int epgwin_reload_items()
{
    GtkTreeIter iter;
    struct list_head *item;
    struct epgitem* epg;
    int count = 0;

    mass_row_update(1);

    gtk_list_store_clear(epg_store);
    list_for_each(item, &epg_list) {
	epg = list_entry(item, struct epgitem, next);
	if (strlen(epg->name) < 3)
	    continue;
	// FIXME: show only stations in control window
	epgwin_add_item(&iter,epg);
	// epgwin_update_item(&iter,epg);
	count++;
    }

    mass_row_update(0);
    return count;
}

#define TBBOLD(x) \
    gtk_text_buffer_insert_with_tags_by_name (buffer, &iter, \
	    x, -1, "bold", NULL);
#define TBNORM(x) \
    gtk_text_buffer_insert (buffer, &iter, x, -1);

static void epgwin_textview_show_entry(GtkTextView* textview,
				       GtkTreeModel *model,
				       GtkTreeIter *tree)
{
    GtkTextIter iter, start, end;
    GtkTextBuffer* buffer;
    char buf[64];
    struct tm tm1, tm2;
    gint t_start, t_stop;
    char *name, *stext, *etext, *lang;
    time_t t;

    buffer = gtk_text_view_get_buffer(textview);

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_get_iter_at_offset (buffer, &iter, 0);

    gtk_tree_model_get(model, tree,
		       EPG_COL_START,  &t_start,
		       EPG_COL_STOP,   &t_stop,
		       EPG_COL_LANG,   &lang,
		       EPG_COL_NAME,   &name,
		       EPG_COL_STEXT,  &stext,
		       EPG_COL_ETEXT,  &etext,
		       -1);

    t = t_start; localtime_r(&t,&tm1);
    t = t_stop;  localtime_r(&t,&tm2);

    TBBOLD(_("Time: "));
    if (tm1.tm_year == tm2.tm_year &&
	tm1.tm_mon  == tm2.tm_mon  &&
	tm1.tm_mday == tm2.tm_mday) {
	strftime(buf, sizeof(buf), "%a %02d.%02m.%Y %R-", &tm1);
	TBNORM(buf);
	strftime(buf, sizeof(buf), "%R\n", &tm2);
	TBNORM(buf);
    } else {
	strftime(buf, sizeof(buf), "%a %02d.%02m.%Y %R ", &tm1);
	TBNORM(buf);
	strftime(buf, sizeof(buf), "%a %02d.%02m.%Y %R\n", &tm2);
	TBNORM(buf);
    }

    if (name && strlen(name)) {
	TBBOLD(_("Name: "));
	TBNORM(name);
	TBNORM("\n");
    }

    if (stext && strlen(stext)) {
	TBBOLD(_("Summary: "));
	TBNORM(stext);
	TBNORM("\n");
    }

    if (lang) {
	TBBOLD(_("Language: "));
	TBNORM(lang);
	TBNORM("\n");
    }

#if 0
    if (epg->flags&EPG_FLAGS_AUDIO) {
	TBBOLD(_("Audio: "));
	if (epg->flags&EPG_FLAG_AUDIO_MONO)
	    TBNORM(_("Mono"));
	if (epg->flags&EPG_FLAG_AUDIO_STEREO)
	    TBNORM(_("Stereo"));
	if (epg->flags&EPG_FLAG_AUDIO_DUAL)
	    TBNORM(_("Dual"));
	if (epg->flags&EPG_FLAG_AUDIO_MULTI)
	    TBNORM(_("Multi"));
	if (epg->flags&EPG_FLAG_AUDIO_SURROUND)
	    TBNORM(_("Surround"));
	TBNORM("\n");
    }

    if (epg->flags&EPG_FLAGS_VIDEO) {
	TBBOLD(_("Video: "));
	if (epg->flags&EPG_FLAG_VIDEO_4_3)
	    TBNORM(_("4:3"));
	if (epg->flags&EPG_FLAG_VIDEO_16_9)
	    TBNORM(_("16:9"));
	if (epg->flags&EPG_FLAG_VIDEO_HDTV)
	    TBNORM(_("HDTV"));
	TBNORM("\n");
    }
#endif

    if (etext && strlen(etext)) {
	TBBOLD(_("Description:\n"));
	TBNORM(etext);
	TBNORM("\n");
    }

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    gtk_text_buffer_apply_tag_by_name (buffer, "word_wrap", &start, &end);
}

void epgwin_show(struct epgitem* epg)
{
    GtkTreeIter iter;

    if (epg) {
	if (0 == epgwin_find_item(&iter, epg->tsid, epg->pnr, epg->id))
	    epgwin_textview_show_entry(GTK_TEXT_VIEW(epg_textview),
				       GTK_TREE_MODEL(epg_store),
				       &iter);
	
	epg_filter_tsid = epg->tsid;
	epg_filter_pnr  = epg->pnr;
	do_filter_all();
    }

    if (!GTK_WIDGET_VISIBLE(epg_win))
	gtk_widget_show_all(epg_win);
}

/* callbacks */

static void do_refresh(void)
{
    char buf[32];
    int n;

    n = epgwin_reload_items();
    snprintf(buf,sizeof(buf),"%d entries",n);
    gtk_label_set_label(GTK_LABEL(epg_status),buf);
}

static void do_filter_nofilter(void)
{
    cur_filter = EPG_FILTER_NOFILTER;
#if USE_MATCH_COL
    do_filter_all();
#else
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(epg_store_filter));
#endif
}

static void do_filter_station(void)
{
    cur_filter = EPG_FILTER_STATION;
#if USE_MATCH_COL
    do_filter_all();
#else
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(epg_store_filter));
#endif
}

static void do_filter_now(void)
{
    cur_filter = EPG_FILTER_NOW;
#if USE_MATCH_COL
    do_filter_all();
#else
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(epg_store_filter));
#endif
}

#if 0
static void do_filter_next(void)
{
    cur_filter = EPG_FILTER_NEXT;
    do_filter_all();
}
#endif

static gboolean is_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gboolean matches = FALSE;
    gint start, stop, tsid, pnr, now;

    switch (cur_filter) {
    case EPG_FILTER_NOFILTER:
	matches = TRUE;
	break;
    case EPG_FILTER_NOW:
	gtk_tree_model_get(model, iter,
			   EPG_COL_START, &start,
			   EPG_COL_STOP,  &stop,
			   -1);
	now = time(NULL);
	if (start <= now && stop > now)
	    matches = TRUE;
	break;
    case EPG_FILTER_STATION:
	gtk_tree_model_get(model, iter,
			   EPG_COL_TSID, &tsid,
			   EPG_COL_PNR,  &pnr,
			   -1);
	if (tsid == epg_filter_tsid && pnr == epg_filter_pnr)
	    matches = TRUE;
	break;
    case EPG_FILTER_NEXT:
    case EPG_FILTER_TEXT:
	g_warning("filter not implemented yet");
	break;
    }
    return matches;
}

static void do_filter_one(GtkTreeModel* model, GtkTreeIter *iter)
{
#if USE_MATCH_COL
    gboolean matches = is_visible(model, iter, NULL);
    gtk_list_store_set(epg_store, iter, EPG_COL_MATCHES, matches, -1);
#endif
}

static void do_filter_all(void)
{
    GtkTreeIter it;
    gboolean ok;
    GtkTreeModel* model = GTK_TREE_MODEL(epg_store);

    for (ok = gtk_tree_model_get_iter_first(model,&it);
	 ok;
	 ok = gtk_tree_model_iter_next(model,&it)) {
	do_filter_one(model,&it);
    }
}

// vim: sw=4
