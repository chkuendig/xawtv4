#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <time.h>
#include <stdio.h>

#include "dvb.h"
#include "parseconfig.h"
#include "commands.h"
#include "gui.h"

GtkWidget* epg_win;

/* ------------------------------------------------------------------------ */

enum {
    EPG_COL_EPGITEM,
    EPG_COL_TIME,
    EPG_COL_NAME,
    EPG_CUR_NUM_COLS,

    EPG_COL_STATION = EPG_CUR_NUM_COLS,
    EPG_NOW_NUM_COLS
};

static void epgwin_textview_show_entry(GtkTextView* textview, struct epgitem* epg);
static void epgwin_add_now_items(GtkTreeView* treeview);
static void epgwin_add_cur_items(GtkTreeView* treeview, int tsid, int pnr);
static void selection_changed(GtkTreeSelection *sel, GtkTextView* textview);

static void epg_row_activated(
	GtkTreeView* treeview,
	GtkTreePath *path,
	GtkTreeViewColumn* col,
	GtkWidget* epg_win)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    struct epgitem* epg;
    char buf[64];

    model = gtk_tree_view_get_model(treeview);
    if (gtk_tree_model_get_iter(model, &iter, path)) {
	gtk_tree_model_get(model, &iter, EPG_COL_EPGITEM, &epg, -1);
	snprintf(buf, sizeof(buf), "%d-%d", epg->tsid, epg->pnr);
	do_va_cmd(2,"setdvb",buf);
    }
}

static void selection_changed(GtkTreeSelection *sel, GtkTextView* textview)
{
//    GtkTreeView* treeview;
    GtkTreeModel* model;
    GtkTreeIter iter;
    struct epgitem* epg;

    if(!gtk_tree_selection_get_selected (sel, &model, &iter))
	return;

    gtk_tree_model_get(model, &iter, EPG_COL_EPGITEM, &epg, -1);
    epgwin_textview_show_entry(textview, epg);
}

static void create_tags(GtkWidget* textview)
{
    GtkTextBuffer* buffer;
    buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    gtk_text_buffer_create_tag (buffer, "bold", "weight", PANGO_WEIGHT_BOLD, NULL);
    gtk_text_buffer_create_tag (buffer, "word_wrap", "wrap_mode", GTK_WRAP_WORD, NULL);
}

static GtkTreeViewColumn* append_text_col(GtkTreeView* tree, const char* title, unsigned id)
{
    GtkTreeViewColumn* col;
    GtkCellRenderer* renderer;

    renderer = gtk_cell_renderer_text_new ();
    col = gtk_tree_view_column_new_with_attributes(title, renderer,
	    "text", id,
	    NULL);
    gtk_tree_view_column_set_resizable(col, TRUE);
    gtk_tree_view_insert_column(tree, col, -1);

    return col;
}

void create_epgwin(GtkWindow* parent)
{
    GtkWidget *vbox,*scroll;
    GtkWidget* paned;
    GtkListStore *cur_model;
    GtkWidget* cur_view;
    GtkListStore *now_model;
    GtkWidget* now_view;
    GtkWidget* textview;
    GtkWidget* notebook;
    GtkWidget* frame;
    GtkTreeSelection* sel;

    epg_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if(parent)
	gtk_window_set_transient_for(GTK_WINDOW(epg_win), parent);
    gtk_window_set_title(GTK_WINDOW(epg_win),_("EPG"));
    gtk_widget_set_size_request(GTK_WIDGET(epg_win), -1, 400);
    g_signal_connect(epg_win, "delete-event",
		     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    paned = gtk_vpaned_new();

    notebook = gtk_notebook_new();

    /* current station */
    {
	cur_view  = gtk_tree_view_new();
	cur_model = gtk_list_store_new(EPG_CUR_NUM_COLS,
				      G_TYPE_POINTER,  // epgitem
				      G_TYPE_STRING,   // time
				      G_TYPE_STRING,   // name
				      NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(cur_view),
				GTK_TREE_MODEL(cur_model));
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	g_signal_connect(cur_view, "row-activated",
			 G_CALLBACK(epg_row_activated), epg_win);

	append_text_col(GTK_TREE_VIEW(cur_view), _("Time"), EPG_COL_TIME);
	append_text_col(GTK_TREE_VIEW(cur_view), _("Name"), EPG_COL_NAME);

	gtk_container_add(GTK_CONTAINER(scroll), cur_view);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll, gtk_label_new_with_mnemonic(_("Current")));
    }

    /* what's running now */
    {
	now_view  = gtk_tree_view_new();
	now_model = gtk_list_store_new(EPG_NOW_NUM_COLS,
				      G_TYPE_POINTER,  // epgitem
				      G_TYPE_STRING,   // station
				      G_TYPE_STRING,   // time
				      G_TYPE_STRING,   // name
				      NULL);
	gtk_tree_view_set_model(GTK_TREE_VIEW(now_view),
				GTK_TREE_MODEL(now_model));
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	g_signal_connect(now_view, "row-activated",
			 G_CALLBACK(epg_row_activated), epg_win);

	append_text_col(GTK_TREE_VIEW(now_view), _("Station"), EPG_COL_STATION);
	append_text_col(GTK_TREE_VIEW(now_view), _("Time"), EPG_COL_TIME);
	append_text_col(GTK_TREE_VIEW(now_view), _("Name"), EPG_COL_NAME);
	
	gtk_container_add(GTK_CONTAINER(scroll), now_view);

	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll, gtk_label_new_with_mnemonic(_("Now")));
    }


    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(frame), notebook);
    gtk_widget_set_size_request (frame, -1, 40);
    gtk_paned_pack1(GTK_PANED(paned), frame, TRUE, FALSE);

    /* epg description */
    {
	textview = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW(textview), FALSE);

	create_tags(textview);
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), textview);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame), scroll);
	gtk_widget_set_size_request (frame, -1, 60);
	gtk_paned_pack2(GTK_PANED(paned), frame, TRUE, FALSE);
    }

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 2);
    gtk_container_add(GTK_CONTAINER(epg_win), vbox);

    gtk_box_pack_start(GTK_BOX(vbox), paned, TRUE, TRUE, 0);

    {
	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cur_view));
	g_signal_connect(sel, "changed",
			 G_CALLBACK(selection_changed), textview);

	sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(now_view));
	g_signal_connect(sel, "changed",
			 G_CALLBACK(selection_changed), textview);
    }
    
    gtk_object_set_data_full (GTK_OBJECT (epg_win), "cur_treeview", cur_view,
                            (GtkDestroyNotify) gtk_widget_unref);
    gtk_object_set_data_full (GTK_OBJECT (epg_win), "now_treeview", now_view,
                            (GtkDestroyNotify) gtk_widget_unref);
    gtk_object_set_data_full (GTK_OBJECT (epg_win), "textview", textview,
                            (GtkDestroyNotify) gtk_widget_unref);

    gtk_widget_set_size_request (epg_win, 500, 400);
}

static void epgwin_cur_add_listitem(GtkListStore* store, struct epgitem* epg)
{
    GtkTreeIter iter;
    char buf[64];
    struct tm tm;
    
    if(!epg->name[0])
	return;

    localtime_r(&epg->start,&tm);
    strftime(buf, sizeof(buf), "%a %02d.%02m. %R", &tm);

    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter,
	    EPG_COL_EPGITEM, epg,
	    EPG_COL_TIME, buf,
	    EPG_COL_NAME, epg->name,
	    -1);
}

static void epgwin_add_cur_items(GtkTreeView* treeview, int tsid, int pnr)
{
    struct list_head *item;
    struct epgitem* e;
    GtkListStore* store = GTK_LIST_STORE(gtk_tree_view_get_model(treeview));

    gtk_list_store_clear(store);

    if(tsid == -1 || pnr == -1)
	return;

    list_for_each(item, &epg_list)
    {
	e = list_entry(item, struct epgitem, next);
	if(e->tsid == tsid && e->pnr == pnr)
	    epgwin_cur_add_listitem(store, e);
    }
}

static void epgwin_now_add_listitem(GtkListStore* store, struct epgitem* epg)
{
    GtkTreeIter iter;
    char buf[64], buf2[64], *name;
    struct tm tm;
    
    if(!epg->name[0])
	return;

    localtime_r(&epg->start,&tm);
    strftime(buf, sizeof(buf), "%a %02d.%02m. %R", &tm);

    snprintf(buf2, sizeof(buf2), "%d-%d", epg->tsid, epg->pnr);
    name = cfg_get_str("dvb-pr",buf2,"name");
    if (NULL == name)
	name = buf2;

    gtk_list_store_append (store, &iter);
    gtk_list_store_set (store, &iter,
	    EPG_COL_EPGITEM, epg,
	    EPG_COL_STATION, name,
	    EPG_COL_TIME, buf,
	    EPG_COL_NAME, epg->name,
	    -1);
}

static void epgwin_add_now_items(GtkTreeView* treeview)
{
    struct list_head *item;
    struct epgitem* e;
    time_t now = time(NULL);
    GtkListStore* store = GTK_LIST_STORE(gtk_tree_view_get_model(treeview));

    gtk_list_store_clear(store);

    //FIXME: only stations in control window
    list_for_each(item, &epg_list)
    {
	e = list_entry(item, struct epgitem, next);
	if (e->start <= now && e->stop > now)
	    epgwin_now_add_listitem(store, e);
    }
}

#define TBBOLD(x) \
    gtk_text_buffer_insert_with_tags_by_name (buffer, &iter, \
	    x, -1, "bold", NULL);
#define TBNORM(x) \
    gtk_text_buffer_insert (buffer, &iter, x, -1);

static void epgwin_textview_show_entry(GtkTextView* textview, struct epgitem* epg)
{
    GtkTextIter iter, start, end;
    GtkTextBuffer* buffer;
    char buf[64];
    struct tm tm1, tm2;

    buffer = gtk_text_view_get_buffer(textview);

    gtk_text_buffer_get_bounds (buffer, &start, &end);
    gtk_text_buffer_delete(buffer, &start, &end);
    
    if(!epg)
	return;

    gtk_text_buffer_get_iter_at_offset (buffer, &iter, 0);

    localtime_r(&epg->start,&tm1);
    localtime_r(&epg->stop, &tm2);

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

    if (epg->name[0]) {
	TBBOLD(_("Name: "));
	TBNORM(epg->name);
	TBNORM("\n");
    }

    if (epg->stext[0]) {
	TBBOLD(_("Summary: "));
	TBNORM(epg->stext);
	TBNORM("\n");
    }

    if (epg->lang[0]) {
	TBBOLD(_("Language: "));
	TBNORM(epg->lang);
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
	    TBNORM(_("4:3"));
	if (epg->flags&EPG_FLAG_VIDEO_16_9)
	    TBNORM(_("16:9"));
	if (epg->flags&EPG_FLAG_VIDEO_HDTV)
	    TBNORM(_("HDTV"));
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
    GtkWidget* textview;
    GtkWidget* treeview;

    textview = gtk_object_get_data (GTK_OBJECT(epg_win), "textview");
    epgwin_textview_show_entry(GTK_TEXT_VIEW(textview), epg);

    treeview = gtk_object_get_data (GTK_OBJECT(epg_win), "cur_treeview");
    epgwin_add_cur_items(GTK_TREE_VIEW(treeview), epg?epg->tsid:-1, epg?epg->pnr:-1);

    treeview = gtk_object_get_data (GTK_OBJECT(epg_win), "now_treeview");
    epgwin_add_now_items(GTK_TREE_VIEW(treeview));

    if (!GTK_WIDGET_VISIBLE(epg_win))
	gtk_widget_show_all(epg_win);
}

// vim: sw=4
