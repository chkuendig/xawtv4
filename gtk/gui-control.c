#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <X11/Xlib.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "list.h"
#include "grab-ng.h"

#include "parseconfig.h"
#include "commands.h"
#include "devs.h"

#include "gui.h"

/* ------------------------------------------------------------------------ */

int           fs;

char          *curr_station;
char          *pick_device_new;

GtkWidget     *main_win;
GtkWidget     *control_win;
GtkAccelGroup *control_accel_group;
GtkWidget     *control_st_menu;
GtkWidget     *control_status;

static GtkWidget     *control_dev_menu;
static GtkWidget     *control_freq_menu;
static GtkWidget     *control_attr_bool_menu;
static GtkWidget     *control_attr_choice_menu;

/* ------------------------------------------------------------------------ */

static void command_cb_activate(GtkMenuItem *menuitem, void *userdata)
{
    struct command *cmd = userdata;
    do_command(cmd->argc, cmd->argv);
}

static void command_cb_destroy(GtkMenuItem *menuitem, void *userdata)
{
    struct command *cmd = userdata;
    int i;

    for (i = 0; i < cmd->argc; i++)
	free(cmd->argv[i]);
    free(cmd);
}

static void command_cb_add(GtkMenuItem *menuitem, int argc, ...)
{
    struct command *cmd;
    va_list ap;
    int  i;

    cmd = malloc(sizeof(*cmd));
    memset(cmd,0,sizeof(*cmd));
    cmd->argc = argc;

    va_start(ap,argc);
    for (i = 0; i < argc; i++)
	cmd->argv[i] = strdup(va_arg(ap,char*));
    va_end (ap);

    g_signal_connect(G_OBJECT(menuitem), "activate",
		     G_CALLBACK(command_cb_activate), cmd);
    g_signal_connect(G_OBJECT(menuitem), "destroy",
		     G_CALLBACK(command_cb_destroy), cmd);
}

/* ------------------------------------------------------------------------ */

struct gtk_attr {
    GtkWidget  *widget;
    GtkWidget  *menu;
    int        value;
};

static void attr_x11_bool_cb(GtkCheckMenuItem *menuitem, void *userdata)
{
    struct ng_attribute *attr = userdata;
    struct gtk_attr *x11 = attr->app;
    int newval;

    newval = gtk_check_menu_item_get_active(menuitem);
    if (newval == x11->value)
	return;

    if (debug)
	fprintf(stderr,"%s: %s %d => %d\n",__FUNCTION__,
		attr->name, x11->value, newval);
    x11->value = newval;
    attr_write(attr,newval,1);
}

static void
attr_x11_add_ctrl(struct ng_attribute *attr)
{
    struct gtk_attr *x11;
    GtkWidget *push,*tearoff;
    GSList *group = NULL;
    int i;

    x11 = malloc(sizeof(*x11));
    memset(x11,0,sizeof(*x11));
    attr->app  = x11;
    x11->value = attr_read(attr);
    
    switch (attr->type) {
    case ATTR_TYPE_INTEGER:
	if (debug)
	    fprintf(stderr,"add attr int FIXME: %s\n",attr->name);
#if 0
    	x11->widget = XtVaCreateManagedWidget("scale",
					      xmScaleWidgetClass,parent,
					      XmNtitleString,str,
					      XmNminimum,attr->min,
					      XmNmaximum,attr->max,
					      XmNdecimalPoints,attr->points,
					      NULL);
	XmScaleSetValue(x11->widget,x11->value);
	XtAddCallback(x11->widget,XmNvalueChangedCallback,
		      attr_x11_changed_cb,attr);
#endif
	break;
    case ATTR_TYPE_BOOL:
	if (debug)
	    fprintf(stderr,"add attr bool: %s\n",attr->name);
	x11->widget = gtk_check_menu_item_new_with_label(attr->name);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_attr_bool_menu),
			      x11->widget);
	gtk_widget_show(x11->widget);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(x11->widget),
				       x11->value);
#if 1
	g_signal_connect(G_OBJECT(x11->widget), "activate",
			 G_CALLBACK(attr_x11_bool_cb), attr);
#else
	command_cb_add(GTK_MENU_ITEM(x11->widget),
		       3,"setattr",attr->name,"toggle");
#endif
	break;
    case ATTR_TYPE_CHOICE:
	if (debug)
	    fprintf(stderr,"add attr choice: %s\n",attr->name);
	x11->widget = gtk_menu_item_new_with_label(attr->name);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_attr_choice_menu),
			      x11->widget);
	group = NULL;
	x11->menu = gtk_menu_new();
	tearoff = gtk_tearoff_menu_item_new();
	gtk_menu_shell_append(GTK_MENU_SHELL(x11->menu), tearoff);
	for (i = 0; attr->choices[i].str != NULL; i++) {
	    push  = gtk_radio_menu_item_new_with_label(group,attr->choices[i].str);
	    group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(push));
	    gtk_menu_shell_append(GTK_MENU_SHELL(x11->menu), push);
	    command_cb_add(GTK_MENU_ITEM(push),
			   3,"setattr",attr->name,attr->choices[i].str);
	    if (x11->value == attr->choices[i].nr)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(push),True);
	}
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(x11->widget), x11->menu);
	gtk_widget_show_all(x11->widget);
	break;
    }
}

static void
attr_x11_del_ctrl(struct ng_attribute *attr)
{
    struct gtk_attr *x11 = attr->app;

    if (x11->menu)
	gtk_widget_destroy(x11->menu);
    if (x11->widget)
	gtk_widget_destroy(x11->widget);
    attr->app = NULL;
    free(x11);
}

static void
attr_x11_update_ctrl(struct ng_attribute *attr, int value)
{
    struct gtk_attr *x11 = attr->app;
    GtkWidget *item;
    GList *list;

    if (x11->value == value)
	return;
    
    fprintf(stderr,"%s: %s %d => %d\n",__FUNCTION__,
	    attr->name,x11->value,value);
    x11->value = value;
    switch (attr->type) {
    case ATTR_TYPE_INTEGER:
#if 0
	XmScaleSetValue(x11->widget,value);
#endif
	break;
    case ATTR_TYPE_BOOL:
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(x11->widget),
				       x11->value);
	break;
    case ATTR_TYPE_CHOICE:
	for (list = gtk_container_get_children(GTK_CONTAINER(x11->menu));
	     NULL != list;
	     list = g_list_next(list)) {
	    item = list->data;
	    // fprintf(stderr,"  %p %s\n",item,gtk_widget_get_name(item));
	}
#if 0
	XtVaGetValues(x11->widget,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	for (i = 0; i < nchildren; i++) {
	    if (attr->choices[i].nr == value)
		XtVaSetValues(x11->widget,XmNmenuHistory,children[i],NULL);
	}
#endif
	break;
    }
    return;
}

static void __init attr_x11_hooks_init(void)
{
    attr_add_hook    = attr_x11_add_ctrl;
    attr_del_hook    = attr_x11_del_ctrl;
    attr_update_hook = attr_x11_update_ctrl;
}

/* ------------------------------------------------------------------------ */
/* onscreen display                                                         */

static GtkWidget *on_win, *on_label;
static guint on_timer;

void create_onscreen(void)
{
    GdkColormap *cmap;
    GdkColor black = { .red = 0x0000, .green = 0x0001, .blue = 0x0000 };
    GdkColor green = { .red = 0x0000, .green = 0xffff, .blue = 0x0000 };
    PangoFontDescription *font;
    
    on_win   = gtk_window_new(GTK_WINDOW_POPUP);
    on_label = gtk_widget_new(GTK_TYPE_LABEL,
			      "xalign", 0,
			      NULL);
    gtk_container_add(GTK_CONTAINER(on_win), on_label);
    gtk_window_set_resizable(GTK_WINDOW(on_win), TRUE);
    gtk_widget_set_sensitive(on_win,   FALSE);
    gtk_widget_set_sensitive(on_label, FALSE);

    cmap = gtk_widget_get_colormap(on_label);
    gdk_colormap_alloc_color(cmap, &black, FALSE, TRUE);
    gdk_colormap_alloc_color(cmap, &green, FALSE, TRUE);
    font = pango_font_description_from_string("led fixed 36");

#if 0
    /* Hmm, bg doesn't work ... */
    gtk_widget_modify_bg(on_label, GTK_STATE_INSENSITIVE, &black);
    gtk_widget_modify_fg(on_label, GTK_STATE_INSENSITIVE, &green);
#else
    gtk_widget_modify_fg(on_label, GTK_STATE_INSENSITIVE, &black);
#endif
    gtk_widget_modify_font(on_label, font);
}

static gboolean popdown_onscreen(gpointer data)
{
    if (debug)
	fprintf(stderr,"osd: hide\n");
    gtk_widget_hide(on_win);
    on_timer = 0;
    return FALSE;
}

void display_onscreen(char *title)
{
    if (!fs)
	return;
    if (!cfg_get_bool("options","global","osd",1))
	return;

    if (debug)
	fprintf(stderr,"osd: show (%s)\n",title);

    gtk_label_set_text(GTK_LABEL(on_label), title);
    gtk_window_move(GTK_WINDOW(on_win),
		    cfg_get_int("options","global","osd-x",30),
		    cfg_get_int("options","global","osd-y",20));
    gtk_widget_show_all(on_win);

    if (on_timer)
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(), on_timer));
    on_timer = g_timeout_add(TITLE_TIME, popdown_onscreen, NULL);
}

/* ------------------------------------------------------------------------ */

static void menu_cb_setdevice(GtkMenuItem *menuitem, gchar *string)
{
    if (debug)
	fprintf(stderr,"%s: %s\n", __FUNCTION__, string);
    pick_device_new = string;
    command_pending++;
}

void menu_cb_fullscreen(void)
{
    fs = !fs;
    if (fs)
	gtk_window_fullscreen(GTK_WINDOW(main_win));
    else {
	gtk_window_unfullscreen(GTK_WINDOW(main_win));

	/* hide osd */
	gtk_widget_hide(on_win);
	if (on_timer) {
	    g_source_destroy(g_main_context_find_source_by_id
			     (g_main_context_default(), on_timer));
	    on_timer = 0;
	}
    }
}

static void menu_cb_mute(void)
{
    do_va_cmd(3,"volume","mute","toggle");
}

static void menu_cb_vol_inc(void)
{
    do_va_cmd(3,"volume","inc");
}

static void menu_cb_vol_dec(void)
{
    do_va_cmd(3,"volume","dec");
}

static void menu_cb_station_next(void)
{
    do_va_cmd(2,"setstation","next");
}

static void menu_cb_station_prev(void)
{
    do_va_cmd(2,"setstation","prev");
}

static void menu_cb_channel_next(void)
{
    do_va_cmd(2,"setchannel","next");
}

static void menu_cb_channel_prev(void)
{
    do_va_cmd(2,"setchannel","prev");
}

static void menu_cb_fine_next(void)
{
    // do_va_cmd(2,"setchannel","next");
}

static void menu_cb_fine_prev(void)
{
    // do_va_cmd(2,"setchannel","prev");
}

static void menu_cb_about(void)
{
    static char *text =
	"\n"
	"This is xawtv, a TV application for X11, using the gtk2 toolkit\n"
	"\n"
	THIS_IS_GPLv2
	"\n"
	"(c) 1997-2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]\n"
	"\n";

    gtk_about_box(GTK_WINDOW(control_win), "xawtv", VERSION, text);
}

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
    GtkWidget          *push;
    GtkWidget          *menu;
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
#else
    group->push = gtk_menu_item_new_with_label(name);
    gtk_menu_shell_append(GTK_MENU_SHELL(control_st_menu),
			  group->push);
    group->menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(group->push), group->menu);
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

    /* FIXME: more details */
    gtk_list_store_append(st_model, &iter);
    gtk_list_store_set(st_model, &iter,
		       ST_COL_NAME,  st->name,
		       ST_COL_GROUP, group ? group->name : "-",
		       ST_COL_DATA,  st,
		       -1);

    st->menu = gtk_menu_item_new_with_label(st->name);
    gtk_menu_shell_append(GTK_MENU_SHELL(group ? group->menu : control_st_menu),
			  st->menu);
    command_cb_add(GTK_MENU_ITEM(st->menu), 2, "setstation", st->name);
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

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path        = "/_File",
	.item_type   = "<Branch>",
    },{
	.path        = "/File/_Record ...",
	.accelerator = "<control>R",
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_SAVE,
    },{
	.path        = "/File/sep1",
	.item_type   = "<Separator>",
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
	
	/* --- Settings menu ------------------------- */
	.path        = "/_Settings",
	.item_type   = "<Branch>",
    },{
	.path        = "/Settings/Frequency _Table",
	.item_type   = "<Branch>",
    },{
	.path        = "/Settings/sep1",
	.item_type   = "<Separator>",
    },{
	.path        = "/Settings/Device _Options",
	.item_type   = "<Branch>",
    },{
	.path        = "/Settings/Device Options/tearoff",
	.item_type   = "<Tearoff>",
    },{

	/* --- Commands menu ------------------------- */
	.path        = "/_Commands",
	.item_type   = "<Branch>",
    },{
	.path        = "/Commands/_Fullscreen",
	.accelerator = "F",
	.callback    = menu_cb_fullscreen,
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
	.path        = "/Commands/sep2",
	.item_type   = "<Separator>",
    },{
	.path        = "/Commands/Next Station",
	.accelerator = "space",  // Page_Up
	.callback    = menu_cb_station_next,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_GO_FORWARD,
    },{
	.path        = "/Commands/Previous Station",
	// .accelerator = "Page_Down",
	.callback    = menu_cb_station_prev,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_GO_BACK,
    },{
	.path        = "/Commands/Tuning",
	.item_type   = "<Branch>",
    },{
	.path        = "/Commands/Tuning/Channel up",
	// .accelerator = "Up",
	.callback    = menu_cb_channel_next,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/Tuning/Channel down",
	// .accelerator = "Down",
	.callback    = menu_cb_channel_prev,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/Tuning/Finetuning up",
	// .accelerator = "Right",
	.callback    = menu_cb_fine_next,
	.item_type   = "<Item>",
    },{
	.path        = "/Commands/Tuning/Finetuning down",
	// .accelerator = "Left",
	.callback    = menu_cb_fine_prev,
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
static gint nmenu_items = sizeof(menu_items)/sizeof (menu_items[0]);
static GtkItemFactory *item_factory;

/* ------------------------------------------------------------------------ */

static void init_channel_list(void)
{
    char *list;

    cfg_sections_for_each("stations",list)
	x11_station_add(list);
}

static void init_devices_list(void)
{
    GSList *group = NULL;
    GtkWidget *item;
    char *list;

    cfg_sections_for_each("devs",list) {
	item = gtk_radio_menu_item_new_with_label(group,list);
	group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
	gtk_menu_shell_append(GTK_MENU_SHELL(control_dev_menu),item);
	g_signal_connect(G_OBJECT(item), "activate",
			 G_CALLBACK(menu_cb_setdevice),
			 list);
    }
}

static void init_freqtab_list(void)
{
    // GSList *group = NULL;
    GtkWidget *item;
    char *list;

    cfg_sections_for_each("freqtabs",list) {
	// item = gtk_radio_menu_item_new_with_label(group,list);
	// group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
	item = gtk_menu_item_new_with_label(list);
	gtk_menu_shell_append(GTK_MENU_SHELL(control_freq_menu),item);
	command_cb_add(GTK_MENU_ITEM(item), 2, "setfreqtab", list);
    }
}

static struct {
    char           *text;
    char           *tooltip;
    char           *priv;
    char           *stock;
    GtkSignalFunc  callback;
} toolbaritems[] = {
    {
	.text     = "prev",
	.tooltip  = "previous station",
	.stock    = GTK_STOCK_GO_BACK,
	.callback = menu_cb_station_prev,
    },{
	.text     = "next",
	.tooltip  = "next station",
	.stock    = GTK_STOCK_GO_FORWARD,
	.callback = menu_cb_station_next,
    },{
	.text     = "mute",
	.tooltip  = "mute sound",
	.callback = menu_cb_mute,
    },{
	/* nothing */
    },{
	.text     = "quit",
	.tooltip  = "quit application",
	.stock    = GTK_STOCK_QUIT,
	.callback = gtk_quit_cb,
    }
};

void create_control(void)
{
    GtkWidget *vbox,*hbox,*menubar,*scroll;
    GtkWidget *handlebox,*toolbar,*icon;
    GtkCellRenderer *renderer;
    int i;

    control_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(control_win),_("control window"));
    gtk_widget_set_size_request(GTK_WIDGET(control_win), -1, 400);
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

    control_attr_bool_menu = gtk_item_factory_get_widget
	(item_factory,"<control>/Settings/Device Options");
    control_attr_choice_menu = gtk_item_factory_get_widget
	(item_factory,"<control>/Settings");

    /* toolbar */
    toolbar = gtk_toolbar_new();
    for (i = 0; i < DIMOF(toolbaritems); i++) {
	icon = NULL;
	if (toolbaritems[i].stock)
	    icon = gtk_image_new_from_stock(toolbaritems[i].stock,
					    GTK_ICON_SIZE_SMALL_TOOLBAR);
	if (!toolbaritems[i].text)
	    gtk_toolbar_append_space(GTK_TOOLBAR(toolbar));
	else
	    gtk_toolbar_append_item(GTK_TOOLBAR(toolbar),
				    toolbaritems[i].text,
				    toolbaritems[i].tooltip,
				    toolbaritems[i].priv,
				    icon,
				    toolbaritems[i].callback,
				    NULL /* user_data */);
    }

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
    control_status = gtk_widget_new(GTK_TYPE_LABEL,
				    "label",  "status line",
				    "xalign", 0,
				    NULL);

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    hbox = gtk_hbox_new(FALSE, 1);
    handlebox = gtk_handle_box_new();
    gtk_container_add(GTK_CONTAINER(handlebox), toolbar);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(control_win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), handlebox, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), st_view, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), scroll, FALSE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), control_status, FALSE, TRUE, 0);

    /* dynamic stuff */
    init_channel_list();
    init_devices_list();
    init_freqtab_list();

    return;
}
