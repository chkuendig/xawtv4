#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "parseconfig.h"
#include "devs.h"
#include "commands.h"
#include "tuning.h"
#include "gui.h"

extern int debug;

/* ------------------------------------------------------------------------ */

GtkWidget *analog_win;

static GtkWidget     *status;
static GtkListStore  *store;
static GtkWidget     *view;

static gint          scan_id;

/* ------------------------------------------------------------------------ */

enum {
    ST_COL_NAME = 0,
    ST_COL_CHANNEL,

    ST_STATE_ACTIVE,
    ST_STATE_NOSIGNAL,
    ST_STATE_SCANNED,
    
    ST_NUM_COLS
};

/* ------------------------------------------------------------------------ */

static void mark_unscanned(void)
{
    gboolean valid;
    GtkTreeIter iter;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&iter)) {
	gtk_list_store_set(store, &iter, ST_STATE_SCANNED, FALSE, -1);
    }
}

static char* next_unscanned(void)
{
    gboolean valid, scanned;
    GtkTreeIter iter;
    char *channel,line[32];

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
			   ST_STATE_SCANNED, &scanned,
			   ST_COL_CHANNEL,   &channel,
			   -1);
	if (scanned)
	    continue;
	gtk_list_store_set(store, &iter, ST_STATE_SCANNED, TRUE, -1);
	snprintf(line,sizeof(line),_("scanning %s ..."),channel);
	gtk_label_set_label(GTK_LABEL(status),line);
	return channel;
    }
    return NULL;
}

static gboolean find_active(GtkTreeIter *iter)
{
    gboolean valid,active;

    for (valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store),iter, NULL);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), iter,
			   ST_STATE_ACTIVE, &active, -1);
	if (active)
	    return True;
    }
    return False;
}

static gboolean scan_timer(gpointer data)
{
    GtkTreeIter iter;
    char *name,*channel,unknown[32];

    if (find_active(&iter)) {
	if (devs.video.type == NG_DEV_VIDEO &&
	    devs.video.v->is_tuned(devs.video.handle)) {
	    gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
			       ST_COL_NAME,    &name,
			       ST_COL_CHANNEL, &channel,
			       -1);
	    if (NULL == name || 0 == strlen(name)) {
		snprintf(unknown,sizeof(unknown),"unknown (%s)",channel);
		gtk_list_store_set(store, &iter, ST_COL_NAME, unknown, -1);
	    }
	    gtk_list_store_set(store, &iter, ST_STATE_NOSIGNAL, False, -1);
	} else {
	    gtk_list_store_set(store, &iter,
			       ST_COL_NAME,       "<no signal>",
			       ST_STATE_NOSIGNAL, True,
			       -1);
	}
    }

    channel = next_unscanned();
    if (NULL == channel) {
	gtk_label_set_label(GTK_LABEL(status),_("channel scan finished"));
	scan_id = 0;
	return FALSE;
    }
    do_va_cmd(2,"setchannel",channel);
    return TRUE;
}

static void menu_cb_scan(void)
{
    char *channel;

    mark_unscanned();
    channel = next_unscanned();
    if (channel) {
	do_va_cmd(2,"setchannel",channel);
	scan_id = g_timeout_add(1000, scan_timer, NULL);
    }
}

/* ------------------------------------------------------------------------ */

#ifdef HAVE_ZVBI

static GIOChannel        *vbi_ch;
static guint             vbi_id;

static gboolean vbi_raw_data(GIOChannel *source, GIOCondition condition,
			     gpointer data)
{
    vbi_hasdata(devs.vbi);
    return TRUE;
}

static void vbi_set_name(char *name)
{
    GtkTreeIter iter;

    if (find_active(&iter))
	gtk_list_store_set(store, &iter,
			   ST_COL_NAME,       name,
			   ST_STATE_NOSIGNAL, False,
			   -1);
}

static void vbi_dec_data(struct vbi_event *ev, void *user)
{
    char title[128];
    char *name;
    
    switch (ev->type) {
    case VBI_EVENT_NETWORK:
	name = "<no station name>";
	if (strlen(ev->ev.network.name) > 0) {
	    name = ev->ev.network.name;
	    vbi_set_name(name);
	}
	snprintf(title,sizeof(title),"teletext: %s",name);
	fprintf(stderr,"%s\n",title);
	break;
    }
}

#endif

/* ------------------------------------------------------------------------ */

static void clear_entries(void)
{
    GtkTreeIter iter;
    
    while (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&iter))
	gtk_list_store_remove(store,&iter);
}

static void fill_entries(void)
{
    GtkTreeIter iter;
    char *list;
    char *ft = freqtab_get();

    if (NULL == ft)
	return;

    cfg_sections_for_each(ft,list) {
	gtk_list_store_append(store, &iter);
	gtk_list_store_set(store, &iter,
			   ST_COL_CHANNEL,    list,
			   ST_STATE_NOSIGNAL, True,
			   -1);
    }
}

/* ------------------------------------------------------------------------ */

static GtkTargetEntry dnd_targets[] = {
    { "UTF8_STRING",  0, ST_COL_NAME    },
    { "STRING",       0, ST_COL_NAME    },
    { "CHANNEL",      0, ST_COL_CHANNEL },
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
    int i,len;

    if (debug)
	fprintf(stderr,"dnd: %d: %s\n",info,gdk_atom_name(sd->target));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    rows = gtk_tree_selection_get_selected_rows(selection, &model);

    switch (info) {
    case ST_COL_NAME:
    case ST_COL_CHANNEL:
	name = malloc(g_list_length(rows)*sizeof(char*));
	for (item = g_list_first(rows), i = 0, len = 0;
	     NULL != item;
	     item = g_list_next(item), i++) {
	    gtk_tree_model_get_iter(model, &iter, item->data);
	    gtk_tree_model_get(model, &iter, info, &name[i], -1);
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
    }
}

/* ------------------------------------------------------------------------ */

static void menu_cb_close(void)
{
    gtk_widget_destroy(analog_win);
}

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path        = "/_File",
	.item_type   = "<Branch>",
    },{
	.path        = "/File/_Close",
	.accelerator = "Q",
	.callback    = menu_cb_close,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_QUIT,

    },{
	/* --- Commands menu ------------------------- */
	.path        = "/_Commands",
	.item_type   = "<Branch>",
    },{
	.path        = "/Commands/_Scan",
	.accelerator = "S",
	.callback    = menu_cb_scan,
	.item_type   = "<Item>",
    }
};
static gint nmenu_items = sizeof(menu_items)/sizeof (menu_items[0]);
static GtkItemFactory *item_factory;

static void destroy(GtkWidget *widget,
		    gpointer   data)
{
#ifdef HAVE_ZVBI
    if (devs.vbi) {
	g_source_remove(vbi_id);
	g_io_channel_unref(vbi_ch);
	vbi_event_handler_unregister(devs.vbi->dec,vbi_dec_data,NULL);
	vbi_close(devs.vbi);
	devs.vbi = NULL;
    }
#endif
    if (scan_id) {
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(), scan_id));
	scan_id = 0;
    }
    analog_win = 0;
}

static void activate(GtkTreeView        *treeview,
		     GtkTreePath        *path,
		     GtkTreeViewColumn  *col,
		     gpointer            userdata)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *channel;

    model = gtk_tree_view_get_model(treeview);
    if (!gtk_tree_model_get_iter(model, &iter, path))
	return;
    gtk_tree_model_get(model, &iter, ST_COL_CHANNEL, &channel, -1);
    do_va_cmd(2, "setchannel", channel);
}

void analog_create_window(void)
{
    GtkWidget *vbox,*hbox,*menubar,*scroll;
    GtkCellRenderer *renderer;
    GtkAccelGroup *accel_group;
    GtkTreeViewColumn *col;

    analog_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(analog_win), _("analog channels"));
    gtk_widget_set_size_request(GTK_WIDGET(analog_win), -1, 400);
    g_signal_connect(G_OBJECT(analog_win), "destroy",
		     G_CALLBACK(destroy), NULL);
    
    /* build menus */
    accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<analog>",
					accel_group);
    gtk_item_factory_create_items(item_factory, nmenu_items,
				  menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(analog_win), accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<analog>");

    /* station list */
    view  = gtk_tree_view_new();
    store = gtk_list_store_new(ST_NUM_COLS,
			       G_TYPE_STRING,   // name
			       G_TYPE_STRING,   // channel

			       G_TYPE_BOOLEAN,  // active
			       G_TYPE_BOOLEAN,  // signal
			       G_TYPE_BOOLEAN); // scanned
    gtk_tree_view_set_model(GTK_TREE_VIEW(view),
			    GTK_TREE_MODEL(store));
    scroll = gtk_vscrollbar_new(gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(view)));
    g_signal_connect(view, "row-activated", G_CALLBACK(activate), NULL);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),
				GTK_SELECTION_MULTIPLE);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  "gray",
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Name", renderer,
	 "text",           ST_COL_NAME,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_NOSIGNAL,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "foreground",  "gray",
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(view), -1, "Channel", renderer,
	 "text",           ST_COL_CHANNEL,
	 "weight-set",     ST_STATE_ACTIVE,
	 "foreground-set", ST_STATE_NOSIGNAL,
	 NULL);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(view), ST_COL_NAME);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), ST_COL_NAME,
				    gtk_sort_iter_compare_str,
                                    GINT_TO_POINTER(ST_COL_NAME), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_NAME);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(view), ST_COL_CHANNEL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), ST_COL_CHANNEL,
				    gtk_sort_iter_compare_str,
                                    GINT_TO_POINTER(ST_COL_CHANNEL), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_CHANNEL);

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
    hbox = gtk_hbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(analog_win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), view, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), scroll, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), status, FALSE, TRUE, 0);

#ifdef HAVE_ZVBI
    /* setup vbi */
    if (devs.vbidev) {
	devs.vbi = vbi_open(devs.vbidev,debug,0);
	vbi_ch = g_io_channel_unix_new(devs.vbi->fd);
	vbi_id = g_io_add_watch(vbi_ch, G_IO_IN, vbi_raw_data, NULL);
	vbi_hasdata(devs.vbi);
	vbi_event_handler_register(devs.vbi->dec,~0,vbi_dec_data,NULL);
    }
#endif
    
    return;
}

void analog_set_freqtab(void)
{
    if (!analog_win)
	return;
    clear_entries();
    fill_entries();
}

void analog_set_channel(char *channel)
{
    GtkTreeIter iter;
    gboolean valid,active;
    char *ch;

    if (!analog_win)
	return;

    for (valid = gtk_tree_model_iter_children(GTK_TREE_MODEL(store),&iter, NULL);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(store),&iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
			   ST_COL_CHANNEL, &ch, -1);
	if (channel && ch)
	    active = (0 == strcasecmp(ch,channel));
	else
	    active = False;
	gtk_list_store_set(store, &iter, ST_STATE_ACTIVE, active, -1);
    }
}
