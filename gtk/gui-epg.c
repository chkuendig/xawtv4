#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/times.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "dvb.h"
#include "parseconfig.h"
#include "commands.h"
#include "gui.h"
#include "epg-store.h"

GtkWidget *epg_win;
GtkWidget *epg_status;

static int standalone;

static GtkWidget *epg_textview;
static GtkWidget *epg_treeview;

static EpgStore *epg_store;

/* ------------------------------------------------------------------------ */

/* callbacks */
static void do_filter_nofilter(void);
static void do_filter_station(void);
static void do_filter_text(GtkWidget *search);

#if 0
static gboolean is_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static void do_filter_one(GtkTreeModel* model, GtkTreeIter *iter);
static void do_filter_all(void);
#endif

static void epgwin_textview_show_entry(GtkTextView* textview,
				       GtkTreeModel *model,
				       GtkTreeIter *tree);
static void selection_changed(GtkTreeSelection *sel, GtkTextView* textview);

/* ------------------------------------------------------------------------ */

static void init_stations(void)
{
    char *list;
    int tsid, pnr;

    if (0 == cfg_sections_count("stations"))
	/* no stations configured => show all */
	return;

    /* disable all stations ... */
    cfg_sections_for_each("dvb-pr",list) {
	if (2 != sscanf(list,"%d-%d",&tsid,&pnr))
	    continue;
	epg_store_station_visible(epg_store, tsid, pnr, FALSE);
    }

    /* ... and enable the ones in the station list */
    cfg_sections_for_each("stations",list) {
	tsid = cfg_get_int("stations",list,"tsid",0);
	pnr  = cfg_get_int("stations",list,"pnr",0);
	if (0 == tsid || 0 == pnr)
	    continue;
	epg_store_station_visible(epg_store, tsid, pnr, TRUE);
    }
}

/* ------------------------------------------------------------------------ */

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

static guint timer_id;
static gboolean keep_up_to_date(gpointer data)
{
    if (GTK_WIDGET_VISIBLE(epg_win))
	epg_store_refresh(epg_store);
    return TRUE;
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
	.path        = noop("/_File"),
	.item_type   = "<Branch>",
    },{
	.path        = noop("/File/_Close"),
	.accelerator = "<control>Q",
	.callback    = menu_cb_close,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_QUIT,

#if 0
    },{
	/* --- Help menu ----------------------------- */
	.path        = noop("/_Help"),
	.item_type   = "<LastBranch>",
    },{
	.path        = noop("/Help/_About ..."),
	.callback    = menu_cb_about,
	.item_type   = "<Item>",
#endif
    }
};
static gint nmenu_items = sizeof(menu_items)/sizeof (menu_items[0]);
static GtkItemFactory *item_factory;

static struct toolbarbutton toolbaritems[] = {
    {
	.text     = noop("station"),
	.tooltip  = noop("current station"),
	.stock    = GTK_STOCK_INDEX,
	.callback = do_filter_station,
    },{
	.text     = noop("no filter"),
	.tooltip  = noop("show all entries"),
	.stock    = GTK_STOCK_JUSTIFY_FILL,
	.callback = do_filter_nofilter,
    },{
	/* nothing */
    }
};

static GtkTreeViewColumn* add_text_col(char *title, enum epg_cols c,
				       gboolean resizable)
{
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;

    renderer = gtk_cell_renderer_text_new ();
    g_object_set(renderer,
		 "weight", PANGO_WEIGHT_BOLD,
		 NULL);
    col = gtk_tree_view_column_new_with_attributes
	(title, renderer,
	 "text",       c,
	 "weight-set", EPG_COL_PLAYING,
	 NULL);
    gtk_tree_view_column_set_resizable(col, resizable);
    gtk_tree_view_insert_column(GTK_TREE_VIEW(epg_treeview), col, -1);
    return col;
}

void create_epgwin(GtkWindow* parent)
{
    GtkTreeSelection *sel;
    GtkWidget *paned,*vbox,*scroll,*frame,*toolbar,*menubar,*handlebox;
    GtkWidget *label,*search;
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
    gtk_item_factory_set_translate_func(item_factory,
					(GtkTranslateFunc)gettext,
					NULL,NULL);
    gtk_item_factory_create_items(item_factory, nmenu_items,
				  menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(epg_win), accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<epg>");

    /* toolbar */
    toolbar = gtk_toolbar_build(toolbaritems, DIMOF(toolbaritems), NULL);
    label = gtk_label_new(_(" Search: "));
    gtk_toolbar_add_widget(toolbar,label,-1);
    search = gtk_entry_new_with_max_length(32);
    gtk_toolbar_add_widget(toolbar,search,-1);
    g_signal_connect(search, "activate",
		     G_CALLBACK(do_filter_text), NULL);
    handlebox = gtk_handle_box_new();
    gtk_container_add(GTK_CONTAINER(handlebox), toolbar);

    /* status line */
    epg_status = gtk_widget_new(GTK_TYPE_LABEL,
				"label",  "Ready.",
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
    gtk_widget_set_size_request (frame, -1, 120);
    gtk_paned_pack2(GTK_PANED(paned), frame, FALSE, FALSE);
    
    /* setup store */
    epg_store = epg_store_new();
    gtk_tree_view_set_model(GTK_TREE_VIEW(epg_treeview),
			    GTK_TREE_MODEL(epg_store));
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(epg_treeview), True);

    g_signal_connect(epg_treeview, "row-activated",
		     G_CALLBACK(epg_row_activated), epg_win);

    /* columns */
    add_text_col(_("Date"),    EPG_COL_DATE,    False);
    add_text_col(_("Begin") ,  EPG_COL_START,   False);
    add_text_col(_("End"),     EPG_COL_STOP,    False);
    add_text_col(_("Station"), EPG_COL_STATION, True);
    add_text_col(_("Name"),    EPG_COL_NAME,    True);
    add_text_col(_("Lang"),    EPG_COL_LANG,    False);

#if 0
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(epg_store), EPG_COL_START,
				    gtk_sort_iter_compare_int,
                                    GINT_TO_POINTER(EPG_COL_START), NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(epg_store),
					 EPG_COL_START, GTK_SORT_ASCENDING);
#endif

    /* epg description */
    create_tags(epg_textview);
    init_stations();

    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(epg_treeview));
    g_signal_connect(sel, "changed",
		     G_CALLBACK(selection_changed), epg_textview);

    /* update view */
    timer_id = g_timeout_add(5000, keep_up_to_date, NULL);
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
    struct epgitem *epg;
    int i,len;

    buffer = gtk_text_view_get_buffer(textview);

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_get_iter_at_offset (buffer, &iter, 0);

    gtk_tree_model_get(model, tree,
		       EPG_COL_EPGITEM,  &epg,
		       -1);

    localtime_r(&epg->start,&tm1);
    localtime_r(&epg->stop,&tm2);

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

    if (strlen(epg->name)) {
	TBBOLD(_("Name: "));
	TBNORM(epg->name);
	TBNORM("\n");
    }

    if (0 != (len = strlen(epg->stext))) {
	TBBOLD(_("Summary: "));
	TBNORM(epg->stext);
	if (epg->stext[len-1] != '\n')
	    TBNORM("\n");
    }

    if (strlen(epg->lang)) {
	TBBOLD(_("Language: "));
	TBNORM(epg->lang);
	TBNORM("\n");
    }

    for (i = 0; i < DIMOF(epg->cat); i++) {
	if (NULL == epg->cat[i])
	    continue;
	TBBOLD(_("Category: "));
	TBNORM(epg->cat[i]);
	TBNORM("\n");
    }

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
	    TBNORM("4:3");
	if (epg->flags&EPG_FLAG_VIDEO_16_9)
	    TBNORM("16:9");
	if (epg->flags&EPG_FLAG_VIDEO_HDTV)
	    TBNORM("HDTV");
	TBNORM("\n");
    }

    if (epg->etext) {
	TBBOLD(_("Description:\n"));
	TBNORM(epg->etext);
	TBNORM("\n");
    }

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    gtk_text_buffer_apply_tag_by_name (buffer, "word_wrap", &start, &end);
}

void epgwin_show(struct epgitem* epg)
{
    if (epg) {
	epg_store_filter_station(epg_store, epg->tsid, epg->pnr);
#if 0
	if (0 == epgwin_find_item(&iter, epg->tsid, epg->pnr, epg->id))
	    epgwin_textview_show_entry(GTK_TEXT_VIEW(epg_textview),
				       GTK_TREE_MODEL(epg_store),
				       &iter);
#endif
    }

    if (!GTK_WIDGET_VISIBLE(epg_win))
	gtk_widget_show_all(epg_win);
}

/* callbacks */

static void do_filter(enum epg_filter type)
{
    /* temporary disconnect store from view for fast large-scale update */
    g_object_ref(epg_store);
    gtk_tree_view_set_model(GTK_TREE_VIEW(epg_treeview), NULL);
    epg_store_filter_type(epg_store, type);
    gtk_tree_view_set_model(GTK_TREE_VIEW(epg_treeview), GTK_TREE_MODEL(epg_store));
    g_object_unref(epg_store);
}

static void do_filter_nofilter(void)
{
    do_filter(EPG_FILTER_NOFILTER);
}

static void do_filter_station(void)
{
    do_filter(EPG_FILTER_STATION);
}

static void do_filter_text(GtkWidget *search)
{
    const char *text = gtk_entry_get_text(GTK_ENTRY(search));

    epg_store_filter_text(epg_store, text);
    do_filter(EPG_FILTER_TEXT);
}

// vim: sw=4
