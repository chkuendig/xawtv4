#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <X11/Xlib.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "list.h"
#include "parseconfig.h"
#include "commands.h"
#include "gui.h"

/* ------------------------------------------------------------------------ */

char          *curr_station;
char          *pick_device_new;

GtkWidget     *main_win;
GtkWidget     *control_win;
GtkAccelGroup *control_accel_group;
GtkWidget     *control_st_menu;

static GtkWidget     *control_dev_menu;
static GtkWidget     *control_freq_menu;

/* ------------------------------------------------------------------------ */

static void menu_cb_setstation(gchar *string)
{
    if (debug)
	fprintf(stderr,"%s: %s\n", __FUNCTION__, string);
    do_va_cmd(2,"setstation", string);
}

static void menu_cb_setdevice(gchar *string)
{
    if (debug)
	fprintf(stderr,"%s: %s\n", __FUNCTION__, string);
    pick_device_new = string;
    command_pending++;
}

static void menu_cb_setfreqtab(gchar *string)
{
    if (debug)
	fprintf(stderr,"%s: %s\n", __FUNCTION__, string);
    do_va_cmd(2,"setfreqtab", string);
}

static void menu_cb_fullscreen(void)
{
    static int on = 0;

    on = !on;
    if (on)
	gtk_window_fullscreen(GTK_WINDOW(main_win));
    else
	gtk_window_unfullscreen(GTK_WINDOW(main_win));
}

/* ------------------------------------------------------------------------ */

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path        = "/_File",
	.item_type   = "<Branch>",
    },{
	.path        = "/File/_Record ...",
	.accelerator = "<control>R",
	.item_type   = "<Item>"
    },{
	.path        = "/File/sep1",
	.item_type   = "<Separator>"
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
	.path        = "/Stations/tearoff",
	.item_type   = "<Tearoff>",
    },{
	.path        = "/_Devices",
	.item_type   = "<Branch>",
    },{
	.path        = "/Devices/tearoff",
	.item_type   = "<Tearoff>",
    },{
	
	/* --- Control menu -------------------------- */
	.path        = "/_Settings",
	.item_type   = "<Branch>",
    },{
	.path        = "/Settings/_Fullscreen",
	.accelerator = "F",
	.callback    = menu_cb_fullscreen,
	.item_type   = "<Item>",
    },{
	.path        = "/Settings/sep1",
	.item_type   = "<Separator>",
    },{
	.path        = "/Settings/Frequency _Table",
	.item_type   = "<Branch>",
    },{

	/* --- Help menu ----------------------------- */
	.path        = "/_Help",
	.item_type   = "<LastBranch>",
    },{
	.path        = "/Help/_About",
	.item_type   = "<Item>",
    }
};
static gint nmenu_items = sizeof(menu_items)/sizeof (menu_items[0]);
static GtkItemFactory *item_factory;

/* ------------------------------------------------------------------------ */

GtkListStore *st_model;
GtkWidget    *st_view;

enum {
    ST_COL_NAME    = 0,
    ST_COL_GROUP,
    ST_COL_DATA,
    ST_NUM_COLS
};

struct st_group {
    char               *name;
    struct list_head   list;
    struct list_head   stations;
};
LIST_HEAD(ch_groups);

struct station {
    char               *name;
    struct list_head   global;
    struct list_head   group;
    guint              key;
    GdkModifierType    mods;
    GtkWidget          *menu;
};
LIST_HEAD(x11_stations);

static struct st_group* group_get(char *name)
{
    struct list_head *item;
    struct st_group *group;

    /* find one */
    list_for_each(item,&ch_groups) {
	group = list_entry(item, struct st_group, list);
	if (0 == strcasecmp(name,group->name))
	    return group;
    }

    /* 404 -> create new */
    group = malloc(sizeof(*group));
    memset(group,0,sizeof(*group));
    group->name = strdup(name);
    INIT_LIST_HEAD(&group->stations);

#if 0
    group->menu1 = XmCreatePulldownMenu(st_menu1, group->name, NULL, 0);
    group->menu2 = XmCreatePulldownMenu(st_menu2, group->name, NULL, 0);

    group->btn1 = XtVaCreateManagedWidget(group->name,
					  xmCascadeButtonWidgetClass, st_menu1,
					  XmNsubMenuId, group->menu1,
					  NULL);
    group->btn2 = XtVaCreateManagedWidget(group->name,
					  xmCascadeButtonWidgetClass, st_menu2,
					  XmNsubMenuId, group->menu2,
					  NULL);
#endif

    list_add_tail(&group->list,&ch_groups);
    return group;
}

static void x11_station_add(char *name)
{
    struct st_group *group;
    struct station *st;
    char key[32], ctrl[16], accel[64];
    char *keyname, *groupname;
    GtkTreeIter iter;
    
    /* add channel */
    st = malloc(sizeof(*st));
    memset(st,0,sizeof(*st));
    st->name = strdup(name);

    /* submenu */
    groupname = cfg_get_str("stations", name, "group");
    if (NULL != groupname) {
	group = group_get(groupname);
    } else {
	group = NULL;
    }

    /* hotkey */
    keyname = cfg_get_str("stations", name, "key");
    if (NULL != keyname) {
	if (2 == sscanf(keyname, "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			ctrl,key)) {
	    sprintf(accel,"<%s>%s",ctrl,key);
	} else {
	    sprintf(accel,"%s",keyname);
	}
	gtk_accelerator_parse(accel,&st->key,&st->mods);
    }

    /* FIXME: submenus, details */
    gtk_list_store_append(st_model, &iter);
    gtk_list_store_set(st_model, &iter,
		       ST_COL_NAME,  st->name,
		       ST_COL_GROUP, group ? group->name : "-",
		       ST_COL_DATA,  st,
		       -1);

    st->menu = gtk_menu_item_new_with_label(st->name);
    gtk_menu_shell_append(GTK_MENU_SHELL(control_st_menu), st->menu);
    g_signal_connect_swapped (G_OBJECT(st->menu), "activate",
			      G_CALLBACK(menu_cb_setstation),
			      st->name);
    if (st->key)
	gtk_widget_add_accelerator(st->menu, "activate",
				   control_accel_group,
				   st->key, st->mods,
				   GTK_ACCEL_VISIBLE);

    list_add_tail(&st->global,&x11_stations);
    if (group)
	list_add_tail(&st->group,&group->stations);
}

#if 0
static void x11_station_del(char *name)
{
    struct list_head *item;
    struct station   *st;

    list_for_each(item,&x11_stations) {
	st = list_entry(item, struct station, global);
	if (0 == strcmp(st->name,name))
	    goto found;
    }
    return;

 found:
    list_del(&st->global);
    if (NULL != st->group.next)
	list_del(&st->group);

    XtDestroyWidget(st->menu1);
    XtDestroyWidget(st->menu2);
    XtVaSetValues(chan_cont, XmNselectedObjectCount,0, NULL);
    XtUnmanageChild(st->button);
    XtDestroyWidget(st->button);
    free(st->name);
    free(st);
}
#endif

/* ------------------------------------------------------------------------ */

static void init_channel_list(void)
{
    char *list;

    cfg_sections_for_each("stations",list)
	x11_station_add(list);
}

static void init_devices_list(void)
{
    GtkWidget *item;
    char *list;

    cfg_sections_for_each("devs",list) {
	item = gtk_menu_item_new_with_label(list);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_dev_menu),item);
	g_signal_connect_swapped(G_OBJECT(item), "activate",
				 G_CALLBACK(menu_cb_setdevice),
				 list);
    }
}

static void init_freqtab_list(void)
{
    GtkWidget *item;
    char *list;

    cfg_sections_for_each("freqtabs",list) {
	item = gtk_menu_item_new_with_label(list);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_freq_menu),item);
	g_signal_connect_swapped(G_OBJECT(item), "activate",
				 G_CALLBACK(menu_cb_setfreqtab),
				 list);
    }
}

void create_control(void)
{
    GtkWidget *vbox,*hbox,*menubar,*status,*scroll;
    GtkCellRenderer *renderer;

    control_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(control_win),_("control window"));
    gtk_widget_set_size_request(GTK_WIDGET(control_win), 400, 300);
    g_signal_connect(control_win, "delete-event",
		     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    /* build menus */
    control_accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<control>",
					control_accel_group);
    gtk_item_factory_create_items(item_factory, nmenu_items,
				  menu_items, NULL);
    gtk_window_add_accel_group(GTK_WINDOW(control_win), control_accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<control>");
    control_st_menu = gtk_item_factory_get_widget(item_factory,"<control>/Stations");
    control_dev_menu = gtk_item_factory_get_widget(item_factory,"<control>/Devices");
    control_freq_menu = gtk_item_factory_get_widget
	(item_factory,"<control>/Settings/Frequency Table");


    /* station list */
    st_view  = gtk_tree_view_new();
    st_model = gtk_list_store_new(ST_NUM_COLS,
				  G_TYPE_STRING,
				  G_TYPE_STRING,
				  G_TYPE_POINTER);
    gtk_tree_view_set_model(GTK_TREE_VIEW(st_view),
			    GTK_TREE_MODEL(st_model));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "Name", renderer,
	 "text", ST_COL_NAME, NULL);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "Group", renderer,
	 "text", ST_COL_GROUP, NULL);
    scroll = gtk_vscrollbar_new(gtk_tree_view_get_hadjustment(GTK_TREE_VIEW(st_view)));

    /* other widgets */
    status = gtk_widget_new(GTK_TYPE_LABEL,
			    "label",  "status line",
			    "xalign", 0,
			    NULL);

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    hbox = gtk_hbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(control_win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), st_view, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), scroll, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), status, FALSE, TRUE, 0);

    /* dynamic stuff */
    init_channel_list();
    init_devices_list();
    init_freqtab_list();

    return;
}
