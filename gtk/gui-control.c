#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <inttypes.h>
#include <X11/Xlib.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "list.h"
#include "grab-ng.h"
#include "parse-mpeg.h"

#include "parseconfig.h"
#include "tv-config.h"
#include "commands.h"
#include "tuning.h"
#include "devs.h"
#include "dvb.h"

#include "gui.h"

/* ------------------------------------------------------------------------ */

int           fs;

char          *curr_station;
char          *pick_device_new;

GtkWidget     *main_win;
GtkWidget     *control_win;
GtkWidget     *control_st_menu;
GtkWidget     *control_status;
GtkAccelGroup *control_accel_group;

static GtkWidget     *station_dialog;
static GtkWidget     *station_name;
static GtkWidget     *station_group_c;
static GtkWidget     *station_group_e;
static GtkWidget     *station_hotkey;
static GtkWidget     *station_ch_omenu;
static GtkWidget     *station_dvb_omenu;
static GtkTreeRowReference *station_edit;

static GtkWidget     *control_dev_menu;
static GtkWidget     *control_freq_menu;
static GtkWidget     *control_attr_bool_menu;
static GtkWidget     *control_attr_choice_menu;

extern Display       *dpy;

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

static gboolean accel_yes(GtkWidget *widget, guint signal_id,
			  gpointer user_data)
{
    return TRUE;
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
	g_signal_connect(G_OBJECT(x11->widget), "activate",
			 G_CALLBACK(attr_x11_bool_cb), attr);
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

    if (debug)
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

GdkColor black = { .red = 0x0000, .green = 0x0000, .blue = 0x0000 };
GdkColor white = { .red = 0xffff, .green = 0xffff, .blue = 0xffff };
GdkColor green = { .red = 0x0000, .green = 0xffff, .blue = 0x0000 };

static void widget_colors(GtkWidget *widget, GdkColor *fg, GdkColor *bg)
{
    GdkColormap *cmap;
    GtkRcStyle *style;

    cmap = gtk_widget_get_colormap(widget);
    style = gtk_widget_get_modifier_style(widget);
    if (fg) {
	gdk_colormap_alloc_color(cmap, fg, FALSE, TRUE);
	style->fg[GTK_STATE_INSENSITIVE] = *fg;
	style->fg[GTK_STATE_NORMAL]      = *fg;
	style->color_flags[GTK_STATE_INSENSITIVE] |= GTK_RC_FG;
	style->color_flags[GTK_STATE_NORMAL]      |= GTK_RC_FG;
    }
    if (bg) {
	gdk_colormap_alloc_color(cmap, bg, FALSE, TRUE);
	style->bg[GTK_STATE_INSENSITIVE] = *bg;
	style->bg[GTK_STATE_NORMAL]      = *bg;
	style->color_flags[GTK_STATE_INSENSITIVE] |= GTK_RC_BG;
	style->color_flags[GTK_STATE_NORMAL]      |= GTK_RC_BG;
    }
    gtk_widget_modify_style(widget,style);
}

static GtkWidget *on_win, *on_label;
static guint on_timer;

void create_onscreen(void)
{
    PangoFontDescription *font;

    on_win   = gtk_window_new(GTK_WINDOW_POPUP);
    on_label = gtk_widget_new(GTK_TYPE_LABEL,
			      "xalign", 0.0,
			      "xpad",   20,
			      "ypad",   5,
			      NULL);

    font = pango_font_description_from_string("led fixed 36");
    gtk_widget_modify_font(on_label, font);
    widget_colors(on_win,   &green, &black);
    widget_colors(on_label, &green, &black);

    gtk_container_add(GTK_CONTAINER(on_win), on_label);
    gtk_window_set_resizable(GTK_WINDOW(on_win), TRUE);
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
    if (!GET_OSD())
	return;

    if (debug)
	fprintf(stderr,"osd: show (%s)\n",title);

    gtk_label_set_text(GTK_LABEL(on_label), title);
    gtk_window_move(GTK_WINDOW(on_win),
		    cfg_get_int("options","global","osd-x",30),
		    cfg_get_int("options","global","osd-y",20));
    gtk_window_resize(GTK_WINDOW(on_win), 1, 1);
    gtk_widget_show_all(on_win);

    if (on_timer)
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(), on_timer));
    on_timer = g_timeout_add(TITLE_TIME, popdown_onscreen, NULL);
}

/* ------------------------------------------------------------------------ */
/* epg display                                                              */

GtkWidget *epg_popup;
static GtkWidget *epg_bar, *epg_time, *epg_name;
static guint epg_timer;

void create_epg(void)
{
    GtkWidget *box;
    
    epg_popup  = gtk_window_new(GTK_WINDOW_POPUP);
    box = gtk_vbox_new(TRUE, 0);
    epg_bar  = gtk_progress_bar_new();
    epg_time = gtk_widget_new(GTK_TYPE_LABEL,
			      "xalign", 0.5,
			      "xpad", SPACING,
			      NULL);
    epg_name = gtk_widget_new(GTK_TYPE_LABEL,
			      "xalign", 0.5,
			      "xpad", SPACING,
			      NULL);

    gtk_container_add(GTK_CONTAINER(epg_popup), box);
    gtk_box_pack_start(GTK_BOX(box), epg_bar,  FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), epg_time, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), epg_name, FALSE, TRUE, 0);

    gtk_window_set_resizable(GTK_WINDOW(epg_popup), TRUE);
}

static gboolean popdown_epg(gpointer data)
{
    gtk_widget_hide(epg_popup);
    epg_timer = 0;
    return FALSE;
}

gboolean epg_shown(void)
{
    if(epg_popup && GTK_WIDGET_VISIBLE(epg_popup))
	return TRUE;
    else
	return FALSE;
}

void display_epg(GtkWindow *win, struct epgitem *epg)
{
    char buf[64];
    struct tm tm;
    int len,i;
    gint vx,vy,vw,vh;
    gint ex,ey,ew,eh;
    time_t total, passed;

    if (!GET_EPG())
	return;
    gtk_widget_hide(epg_popup);

    if(!epg)
	return;

    total  = epg->stop  - epg->start;
    passed = time(NULL) - epg->start;

    localtime_r(&epg->start,&tm);
    len = strftime(buf,sizeof(buf),"%H:%M - ",&tm);
    localtime_r(&epg->stop,&tm);
    len += strftime(buf+len,sizeof(buf)-len,"%H:%M",&tm);
    gtk_label_set_text(GTK_LABEL(epg_time), buf);

    len = snprintf(buf,sizeof(buf),"%s",epg->name);
    for (i = 0; i < len; i++)
	if (buf[i] == '\n')
	    buf[i] = ' ';
    gtk_label_set_text(GTK_LABEL(epg_name), buf);

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(epg_bar),
				  (gdouble)passed/total);
    gtk_window_resize(GTK_WINDOW(epg_popup), 100, 30);
    gtk_window_get_size(GTK_WINDOW(epg_popup), &ew, &eh);
    gtk_window_get_position(win, &vx, &vy);
    gtk_window_get_size(win, &vw, &vh);
    ex = vx + (vw-ew)/2;
    ey = vy + vh - eh - 10;
    gtk_window_move(GTK_WINDOW(epg_popup), ex, ey);

    gtk_widget_show_all(epg_popup);
    if (epg_timer)
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(), epg_timer));
    epg_timer = g_timeout_add(TITLE_TIME, popdown_epg, NULL);
}

/* ------------------------------------------------------------------------ */

static void menu_cb_setdevice(GtkMenuItem *menuitem, gchar *string)
{
    if (debug)
	fprintf(stderr,"%s: %s\n", __FUNCTION__, string);
    pick_device_new = string;
    command_pending++;
}

static void menu_cb_start_stop_record()
{
    if (debug)
	fprintf(stderr,"%s\n", __FUNCTION__);
    if (recording) {
	if (0 == mm_rec_stop())
	    display_message("recording stopped");
    } else {
	if (0 == mm_rec_start())
	    display_message("recording started");
    }
}

void menu_cb_fullscreen(void)
{
    fs = !fs;
    if (fs) {
	gtk_window_fullscreen(GTK_WINDOW(main_win));
	gtk_screensaver_disable(dpy);
    } else {
	gtk_window_unfullscreen(GTK_WINDOW(main_win));
	gtk_screensaver_enable(dpy);

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
    do_va_cmd(2,"volume","inc");
}

static void menu_cb_vol_dec(void)
{
    do_va_cmd(2,"volume","dec");
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
	"This is xawtv, a TV application for X11, using the gtk2 toolkit\n"
	"\n"
	THIS_IS_GPLv2
	"\n"
	"(c) 1997-2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]";

    gtk_about_box(GTK_WINDOW(control_win), "xawtv", VERSION, text);
}

/* ------------------------------------------------------------------------ */

GtkListStore *st_model;
GtkWidget    *st_view;

enum {
    ST_COL_NAME    = 0,
    ST_COL_GROUP,
    ST_COL_HOTKEY,
    ST_COL_CHANNEL,
    ST_COL_TSID,
    ST_COL_PNR,

    ST_DATA_MENU,

    ST_STATE_ACTIVE,
    ST_NUM_COLS
};

struct st_group {
    char               *name;
    GtkWidget          *push;
    GtkWidget          *menu;

    struct list_head   list;
};
LIST_HEAD(ch_groups);

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

    group->push = gtk_menu_item_new_with_label(name);
    gtk_menu_shell_append(GTK_MENU_SHELL(control_st_menu),
			  group->push);
    group->menu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(group->push), group->menu);

    gtk_widget_show(group->push);
    gtk_widget_show(group->menu);
    list_add_tail(&group->list,&ch_groups);
    return group;
}

static void x11_station_add(GtkTreeIter *iter)
{
    gtk_list_store_append(st_model, iter);
    gtk_list_store_set(st_model, iter,
		       ST_STATE_ACTIVE, FALSE,
		       -1);
}

static gboolean x11_station_find(GtkTreeIter *iter, char *search)
{
    gboolean valid;
    char *name;
    
    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(st_model),iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(st_model),iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(st_model), iter,
			   ST_COL_NAME,  &name, -1);
	if (0 == strcasecmp(name,search))
	    return TRUE;
    }
    return FALSE;
}

void x11_station_activate(char *current)
{
    GtkTreeIter iter;
    gboolean valid,active;
    char *name;

    for (valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(st_model),&iter);
	 valid;
	 valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(st_model),&iter)) {
	gtk_tree_model_get(GTK_TREE_MODEL(st_model), &iter,
			   ST_COL_NAME,  &name, -1);
	active = FALSE;
	if (current && 0 == strcasecmp(name,current))
	    active = TRUE;
	gtk_list_store_set(st_model, &iter, ST_STATE_ACTIVE, active, -1);
    }
}

static void x11_station_apply(GtkTreeIter *iter, char *name)
{
    char key[32], ctrl[16], accel[64];
    char *keyname, *groupname;
    struct st_group *group = NULL;
    GdkModifierType gmods;
    GtkWidget *menu;
    guint gkey;

    /* submenu */
    groupname = cfg_get_str("stations", name, "group");
    if (NULL != groupname)
	group = group_get(groupname);

    /* hotkey */
    keyname = cfg_get_str("stations", name, "key");
    if (NULL != keyname) {
	if (2 == sscanf(keyname, "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			ctrl,key)) {
	    sprintf(accel,"<%s>%s",ctrl,key);
	} else {
	    sprintf(accel,"%s",keyname);
	}
	gtk_accelerator_parse(accel,&gkey,&gmods);
    } else {
	gkey  = 0;
	gmods = 0;
    }

    /* menu item */
    gtk_tree_model_get(GTK_TREE_MODEL(st_model), iter,
		       ST_DATA_MENU,  &menu, -1);
    if (menu)
	gtk_widget_destroy(menu);

    menu = gtk_menu_item_new_with_label(name);
    gtk_menu_shell_append(GTK_MENU_SHELL(group ? group->menu : control_st_menu),
			  menu);
    gtk_widget_show(menu);
    command_cb_add(GTK_MENU_ITEM(menu), 2, "setstation", name);
    if (gkey) {
	gtk_widget_add_accelerator(menu, "activate",
				   control_accel_group,
				   gkey, gmods,
				   GTK_ACCEL_VISIBLE);
	g_signal_connect(menu, "can_activate_accel", G_CALLBACK(accel_yes), NULL);
    }

    /* update storage */
    gtk_list_store_set(st_model, iter,
		       ST_COL_NAME,     name,
		       ST_COL_GROUP,    group ? group->name : NULL,
		       ST_COL_HOTKEY,   keyname,
		       ST_COL_CHANNEL,  cfg_get_str("stations", name, "channel"),
		       ST_COL_TSID,     cfg_get_int("stations", name, "tsid", 0),
		       ST_COL_PNR,      cfg_get_int("stations", name, "pnr",  0),
		       ST_DATA_MENU,    menu,
		       -1);
}

static void x11_station_del(GtkTreeIter *iter)
{
    GtkWidget *menu;

    gtk_tree_model_get(GTK_TREE_MODEL(st_model), iter,
		       ST_DATA_MENU,  &menu, -1);
    if (menu)
	gtk_widget_destroy(menu);
    gtk_list_store_remove(st_model,iter);
}

/* ------------------------------------------------------------------------ */

static void station_response(GtkDialog *dialog,
			    gint arg1,
			    gpointer user_data)
{
    GtkWidget *menu,*item;
    char *oname,*name,*group,*hotkey,*channel,*dvb;
    GtkTreePath *path;
    GtkTreeIter iter;
    int tsid,pnr;

    if (arg1 == GTK_RESPONSE_OK || arg1 == GTK_RESPONSE_APPLY) {

	/* get user input */
        name   = (char*)gtk_entry_get_text(GTK_ENTRY(station_name));
	group  = (char*)gtk_entry_get_text(GTK_ENTRY(station_group_e));
	hotkey = (char*)gtk_entry_get_text(GTK_ENTRY(station_hotkey));

	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(station_ch_omenu));
	item = gtk_menu_get_active(GTK_MENU(menu));
        channel = g_object_get_data(G_OBJECT(item), "channel");
	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(station_dvb_omenu));
	item = gtk_menu_get_active(GTK_MENU(menu));
	dvb = g_object_get_data(G_OBJECT(item), "dvb-pr");

	if (0 == strlen(group))
	    group = NULL;
	if (0 == strlen(hotkey))
	    hotkey = NULL;
	if (dvb)
	    sscanf(dvb,"%d-%d",&tsid,&pnr);
	else
	    tsid = 0, pnr = 0;

	if (NULL != station_edit &&
	    NULL != (path = gtk_tree_row_reference_get_path(station_edit)) &&
	    gtk_tree_model_get_iter(GTK_TREE_MODEL(st_model),&iter,path)) {
	    gtk_tree_model_get(GTK_TREE_MODEL(st_model), &iter,
			       ST_COL_NAME,  &oname, -1);
	} else {
	    oname = NULL;
	}

	if (debug)
	    fprintf(stderr,"apply: oldname=\"%s\" name=\"%s\" group=\"%s\""
		    " hotkey=\"%s\" channel=%s tsid=%d pnr=%d\n",
		    oname, name, group, hotkey, channel, tsid, pnr);

	/* sanity checks */
	if (0 == strlen(name)) {
	    gtk_error_box(GTK_WINDOW(station_dialog),
			  "Error","no name specified for station");
	    return;
	}
	if ((NULL == oname || 0 != strcasecmp(oname,name)) && 
	    NULL != cfg_search("stations", name, NULL, NULL)) {
	    gtk_error_box(GTK_WINDOW(station_dialog),
			  "Error","station with specified name exists already");
	    return;
	}

	/* apply changes */
	if (oname && 0 != strcasecmp(oname,name))
	    cfg_del_section("stations", oname);
	cfg_set_str("stations", name, "group",   group);
	cfg_set_str("stations", name, "key",     hotkey);
	cfg_set_str("stations", name, "channel", channel);
	cfg_set_int("stations", name, "tsid",    tsid);
	cfg_set_int("stations", name, "pnr",     pnr);

	if (NULL == station_edit ||
	    NULL == (path = gtk_tree_row_reference_get_path(station_edit)) ||
	    !gtk_tree_model_get_iter(GTK_TREE_MODEL(st_model),&iter,path)) {
	    x11_station_add(&iter);
	}
	x11_station_apply(&iter, name);
    }

    if (arg1 != GTK_RESPONSE_APPLY)
	gtk_widget_hide(GTK_WIDGET(dialog));
}

static void update_station_prop(GtkTreeIter *iter)
{
    GtkWidget *menu, *item;
    struct st_group *gr;
    struct list_head *head;
    GList *groups;
    char *list,*name,*net,*video;
    char label[20];
    int active,type;
    char *sname   = NULL;
    char *sgroup  = NULL;
    char *shotkey = NULL;
    char *channel = curr_channel;
    int  tsid     = 0;
    int  pnr      = 0;
    char tsid_pnr[32];

    /* text fields */
    if (iter)
	gtk_tree_model_get(GTK_TREE_MODEL(st_model), iter,
			   ST_COL_NAME,    &sname,
			   ST_COL_GROUP,   &sgroup,
			   ST_COL_HOTKEY,  &shotkey,
			   ST_COL_CHANNEL, &channel,
			   ST_COL_TSID,    &tsid,
			   ST_COL_PNR,     &pnr,
			   -1);
    if (debug)
	fprintf(stderr,"edit/add: name=\"%s\" group=\"%s\" hotkey=\"%s\""
		" channel=%s tsid=%d pnr=%d\n",
		sname, sgroup, shotkey, channel, tsid, pnr);

    snprintf(tsid_pnr,sizeof(tsid_pnr),"%d-%d",tsid,pnr);
    gtk_entry_set_text(GTK_ENTRY(station_name),    sname   ? sname   : "");
    gtk_widget_grab_focus(station_name);
    gtk_entry_set_text(GTK_ENTRY(station_hotkey),  shotkey ? shotkey : "");
    
    /* groups dropdown */
    groups = NULL;
    list_for_each(head, &ch_groups) {
	gr = list_entry(head, struct st_group, list);
	groups = g_list_append (groups, gr->name);
    }
    if (groups)
	gtk_combo_set_popdown_strings(GTK_COMBO(station_group_c), groups);
    g_list_free(groups);
    gtk_entry_set_text(GTK_ENTRY(station_group_e), sgroup ? sgroup : "");
    
    /* analog channel menu */
    menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(station_ch_omenu));
    gtk_container_foreach(GTK_CONTAINER(menu),
			  (GtkCallback)gtk_widget_destroy,NULL);
    active = 0;
    item = gtk_menu_item_new_with_label("none");
    g_object_set_data_full(G_OBJECT(item),"channel",NULL,NULL);
    gtk_menu_append(menu, item);
    gtk_option_menu_set_history(GTK_OPTION_MENU(station_ch_omenu),active);
    active++;
    if (freqtab_get()) {
	cfg_sections_for_each(freqtab_get(), list) {
	    item = gtk_menu_item_new_with_label(list);
	    g_object_set_data_full(G_OBJECT(item),"channel",strdup(list),free);
	    gtk_menu_append(menu, item);
	    if (channel && 0 == strcmp(channel, list))
		gtk_option_menu_set_history(GTK_OPTION_MENU(station_ch_omenu),active);
	    active++;
	}
    }

    /* digital dvb menu */
    menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(station_dvb_omenu));
    gtk_container_foreach(GTK_CONTAINER(menu),
			  (GtkCallback)gtk_widget_destroy,NULL);
    active = 0;
    item = gtk_menu_item_new_with_label("none");
    g_object_set_data_full(G_OBJECT(item),"dvb-pr",NULL,NULL);
    gtk_menu_append(menu, item);
    gtk_option_menu_set_history(GTK_OPTION_MENU(station_dvb_omenu),active);
    active++;
    cfg_sections_for_each("dvb-pr", list) {
	net   = cfg_get_str("dvb-pr", list, "net");
	name  = cfg_get_str("dvb-pr", list, "name");
	video = cfg_get_str("dvb-pr", list, "video");
	type  = cfg_get_int("dvb-pr", list, "type",0);
	if (1 != type || NULL == video)
	    continue;
	snprintf(label,sizeof(label),"%s (%s)",list,name);
	item = gtk_menu_item_new_with_label(label);
	g_object_set_data_full(G_OBJECT(item),"dvb-pr",strdup(list),free);
	gtk_menu_append(menu, item);
	if (0 == strcmp(tsid_pnr, list))
	    gtk_option_menu_set_history(GTK_OPTION_MENU(station_dvb_omenu),active);
	active++;
    }
}

static void create_station_prop(GtkWindow *parent)
{
    GtkWidget *frame,*bmenu;
    GtkBox *vbox, *box, *hbox;
	
    station_dialog =
	gtk_dialog_new_with_buttons("TV Station Properties", parent, 0,
				    GTK_STOCK_OK,     GTK_RESPONSE_OK,
				    GTK_STOCK_APPLY,  GTK_RESPONSE_APPLY,
				    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(station_dialog),
				    GTK_RESPONSE_OK);
    g_signal_connect(station_dialog, "delete-event",
		     G_CALLBACK(gtk_widget_hide_on_delete), NULL);
    vbox = GTK_BOX(GTK_DIALOG(station_dialog)->vbox);

    /* general */
    frame = gtk_frame_new("General");
    gtk_container_set_border_width(GTK_CONTAINER(frame), SPACING);
    gtk_box_pack_start(vbox, frame, TRUE, TRUE, 0);
    box = GTK_BOX(gtk_vbox_new(TRUE, SPACING));
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);
    gtk_container_add(GTK_CONTAINER(frame),GTK_WIDGET(box));

    hbox = gtk_add_hbox_with_label(box, "Name");
    station_name = gtk_entry_new();
    gtk_box_pack_start(hbox, station_name, TRUE, TRUE, 0);

    hbox = gtk_add_hbox_with_label(box, "Group");
    station_group_c = gtk_combo_new();
    station_group_e = GTK_COMBO(station_group_c)->entry;
    gtk_box_pack_start(hbox, station_group_c, TRUE, TRUE, 0);

    hbox = gtk_add_hbox_with_label(box, "Hotkey");
    station_hotkey = gtk_entry_new();
    gtk_box_pack_start(hbox, station_hotkey, TRUE, TRUE, 0);

    /* analog TV */
    frame = gtk_frame_new("Analog TV");
    gtk_container_set_border_width(GTK_CONTAINER(frame), SPACING);
    gtk_box_pack_start(vbox, frame, TRUE, TRUE, 0);
    box = GTK_BOX(gtk_vbox_new(TRUE, SPACING));
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);
    gtk_container_add(GTK_CONTAINER(frame),GTK_WIDGET(box));

    hbox = gtk_add_hbox_with_label(box, "Channel");
    bmenu = gtk_menu_new();
    station_ch_omenu = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(station_ch_omenu),bmenu);
    gtk_box_pack_start(hbox, station_ch_omenu, TRUE, TRUE, 0);

    /* DVB */
    frame = gtk_frame_new("Digital TV (DVB)");
    gtk_container_set_border_width(GTK_CONTAINER(frame), SPACING);
    gtk_box_pack_start(vbox, frame, TRUE, TRUE, 0);
    box = GTK_BOX(gtk_vbox_new(TRUE, SPACING));
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);
    gtk_container_add(GTK_CONTAINER(frame),GTK_WIDGET(box));

    hbox = gtk_add_hbox_with_label(box, "TSID/PNR");
    bmenu = gtk_menu_new();
    station_dvb_omenu = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(station_dvb_omenu),bmenu);
    gtk_box_pack_start(hbox, station_dvb_omenu, TRUE, TRUE, 0);

    g_signal_connect(station_dialog, "response",
		     G_CALLBACK(station_response), NULL);
}

static void menu_cb_add_station(void)
{
    station_edit = NULL;
    if (!station_dialog)
	create_station_prop(GTK_WINDOW(control_win));
    update_station_prop(NULL);
    gtk_widget_show_all(station_dialog);
}

static void menu_cb_edit_station(void)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(st_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;
    if (!station_dialog)
	create_station_prop(GTK_WINDOW(control_win));
    update_station_prop(&iter);
    path = gtk_tree_model_get_path(GTK_TREE_MODEL(st_model),&iter);
    station_edit = gtk_tree_row_reference_new(GTK_TREE_MODEL(st_model),path);
    gtk_widget_show_all(station_dialog);
}

static void menu_cb_del_station(void)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;
    char *name;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(st_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &iter))
	return;
    gtk_tree_model_get(GTK_TREE_MODEL(st_model), &iter,
		       ST_COL_NAME,  &name, -1);
    cfg_del_section("stations",name);
    x11_station_del(&iter);
}

static void menu_cb_save_stations(void)
{
    write_config_file("stations");
}

static void menu_cb_scan_analog(void)
{
    if (NULL == analog_win) {
	analog_create_window();
	analog_set_freqtab();
	analog_set_channel(curr_channel);
    }
    gtk_widget_show_all(analog_win);
}

#ifdef HAVE_DVB
extern struct dvbmon *dvbmon;
static void menu_cb_scan_dvb(void)
{
    if (NULL == dvbscan_win)
	dvbscan_create_window(0);
    gtk_widget_show_all(dvbscan_win);
}
#endif

static void station_activate(GtkTreeView        *treeview,
			     GtkTreePath        *path,
			     GtkTreeViewColumn  *col,
			     gpointer            userdata)
{
    GtkTreeModel *model;
    GtkTreeIter   iter;
    char *name;

    model = gtk_tree_view_get_model(treeview);
    if (gtk_tree_model_get_iter(model, &iter, path)) {
	gtk_tree_model_get(model, &iter, ST_COL_NAME, &name, -1);
	do_va_cmd(2,"setstation",name);
    }
}

/* ------------------------------------------------------------------------ */

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path        = "/_File",
	.item_type   = "<Branch>",
#if 0
    },{
	.path        = "/File/_Record ...",
	.accelerator = "<control>R",
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_SAVE,
#endif
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

	/* --- Edit menu ----------------------------- */
	.path        = "/_Edit",
	.item_type   = "<Branch>",
    },{
	.path        = "/Edit/_Add Station ...",
	.callback    = menu_cb_add_station,
	.item_type   = "<Item>",
    },{
	.path        = "/Edit/_Edit Station ...",
	.accelerator = "E",
	.callback    = menu_cb_edit_station,
	.item_type   = "<Item>",
    },{
	.path        = "/Edit/_Delete Station",
	.callback    = menu_cb_del_station,
	.item_type   = "<Item>",
    },{
	.path        = "/Edit/sep1",
	.item_type   = "<Separator>",
    },{
	.path        = "/Edit/_Save Stations",
	.callback    = menu_cb_save_stations,
	.item_type   = "<Item>",
    },{
	.path        = "/Edit/sep2",
	.item_type   = "<Separator>",
    },{
	.path        = "/Edit/Scan analog ...",
	.callback    = menu_cb_scan_analog,
	.item_type   = "<Item>",
#ifdef HAVE_DVB
    },{
	.path        = "/Edit/Scan DVB ...",
	.callback    = menu_cb_scan_dvb,
	.item_type   = "<Item>",
#endif
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
	.path        = "/Commands/Start or stop _recording",
	.accelerator = "R",
	.callback    = menu_cb_start_stop_record,
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
	.accelerator = "BackSpace", // Page_Down
	.callback    = menu_cb_station_prev,
	.item_type   = "<StockItem>",
	.extra_data  = GTK_STOCK_GO_BACK,
    },{
	.path        = "/Commands/Tuning",
	.item_type   = "<Branch>",
    },{
	.path        = "/Commands/Tuning/tearoff",
	.item_type   = "<Tearoff>",
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
static GtkItemFactory *item_factory;

/* ------------------------------------------------------------------------ */
/* dnd                                                                      */

static GtkTargetEntry dnd_targets[] = {
    { "TARGETS",      0, 42 },
    { "UTF8_STRING",  0,  0 },
    { "TSID",         0,  1 },
    { "PNR",          0,  2 },
    { "VPID",         0,  3 },
    { "APID",         0,  4 },
    { "CHANNEL",      0,  5 },
};
static int  dnd_pending;
static char *dnd_data[6];
static int  dnd_len[6];

static void drag_data_received(GtkWidget *widget,
			       GdkDragContext *dc,
			       gint x, gint y,
			       GtkSelectionData *sd,
			       guint info, guint time, gpointer data)
{
    GtkTreeIter iter;
    GdkAtom *targets;
    char *atom,*name,*channel;
    int pos[6];
    int i,tsid,pnr,audio,video;

    if (debug)
	fprintf(stderr,"%s: %s %s %s fmt=%d ptr=%p len=%d id=%d\n",__FUNCTION__,
		gdk_atom_name(sd->selection),
		gdk_atom_name(sd->target),
		gdk_atom_name(sd->type),
		sd->format, sd->data, sd->length, info);

    switch (info) {
    case 42: // TARGETS
	targets = (void*)sd->data;
	for (i = 0; i < sd->length/sizeof(targets[0]); i++) {
	    atom = gdk_atom_name(targets[i]);
	    if (debug)
		fprintf(stderr,"  %s\n",atom);
	    if (0 == strcmp(atom,"UTF8_STRING")  ||
		0 == strcmp(atom,"TSID")         ||
		0 == strcmp(atom,"PNR")          ||
		0 == strcmp(atom,"VPID")         ||
		0 == strcmp(atom,"APID")         ||
		0 == strcmp(atom,"CHANNEL")) {
		dnd_pending++;
		gtk_drag_get_data(widget, dc, targets[i], time);
	    }
	}
	break;
    case 0 ... 5:
 	if (dnd_data[info])
	    free(dnd_data[info]);
	dnd_data[info] = malloc(sd->length);
	dnd_len[info] = sd->length;
	memcpy(dnd_data[info], sd->data, sd->length);
	break;
    }
    dnd_pending--;
    if (0 != dnd_pending)
	return;

    if (debug)
	fprintf(stderr,"%s: all done\n",__FUNCTION__);
    gtk_drag_finish(dc, TRUE, FALSE, time);

    for (i = 0; i < 6; i++)
	pos[i] = 0;
    for (;;) {
	for (i = 0; i < 6; i++) {
	    if (dnd_data[i] && pos[i] >= dnd_len[i]) {
		fprintf(stderr,"pos %d %d %d\n",i,pos[i],dnd_len[i]);
		return;
	    }
	}
	name    = dnd_data[0] ? dnd_data[0]+pos[0]       : NULL;
	channel = dnd_data[5] ? dnd_data[5]+pos[5]       : NULL;
	tsid    = dnd_data[1] ? atoi(dnd_data[1]+pos[1]) : 0;
	pnr     = dnd_data[2] ? atoi(dnd_data[2]+pos[2]) : 0;
	video   = dnd_data[3] ? atoi(dnd_data[3]+pos[3]) : 0;
	audio   = dnd_data[4] ? atoi(dnd_data[4]+pos[4]) : 0;

	if (NULL != name && 0 != tsid && 0 != pnr && 0 != audio) {
	    /* DVB channel (TV or radio) */
	    if (1 || debug)
		fprintf(stderr,"add dvb: %s tsid=%d pnr=%d video=%d audio=%d\n",
			name, tsid, pnr, video, audio);
	    cfg_set_int("stations",name,"tsid",tsid);
	    cfg_set_int("stations",name,"pnr", pnr);
	    if (!x11_station_find(&iter,name))
		x11_station_add(&iter);
	    x11_station_apply(&iter,name);
	} else if (NULL != name && NULL != channel) {
	    /* analog channel */
	    if (1 || debug)
		fprintf(stderr,"add analog: %s ch=%s\n", name, channel);
	    cfg_set_str("stations",name,"channel",channel);
	    if (!x11_station_find(&iter,name))
		x11_station_add(&iter);
	    x11_station_apply(&iter,name);
	} else {
	    /* no channel -- ignore */
	    if (debug)
		fprintf(stderr,"ignore: %s tsid=%d pnr=%d video=%d audio=%d ch=%s\n",
			name, tsid, pnr, video, audio, channel);
	}

	for (i = 0; i < 6; i++)
	    if (dnd_data[i])
		pos[i] += strlen(dnd_data[i]+pos[i])+1;
    }
}

static void drag_drop(GtkWidget *widget,
		      GdkDragContext *dc,
		      gint x, gint y,
		      guint time, gpointer data)
{
    GdkAtom targets = gdk_atom_intern("TARGETS", FALSE);
    int i;

    if (debug)
	fprintf(stderr,"%s\n",__FUNCTION__);

    for (i = 0; i < 3; i++) {
    	if (dnd_data[i])
	    free(dnd_data[i]);
	dnd_data[i] = NULL;
	dnd_len[i]  = 0;
    }
    dnd_pending = 1;
    gtk_drag_get_data(widget, dc, targets, time);
}

/* ------------------------------------------------------------------------ */

static void init_channel_list(void)
{
    GtkTreeIter iter;
    char *list;

    cfg_sections_for_each("stations",list) {
	x11_station_add(&iter);
	x11_station_apply(&iter, list);
    }
}

static void init_devices_list(void)
{
    // GSList *group = NULL;
    GtkWidget *item;
    char *list;

    cfg_sections_for_each("devs",list) {
	// item = gtk_radio_menu_item_new_with_label(group,list);
	// group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
	item = gtk_menu_item_new_with_label(list);
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

static struct toolbarbutton toolbaritems[] = {
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
#if 0
    },{
	.text     = "stop",
	.stock    = GTK_STOCK_STOP,
#endif
    },{
	/* nothing */
    },{
	.text     = "quit",
	.tooltip  = "quit application",
	.stock    = GTK_STOCK_QUIT,
	.callback = gtk_quit_cb,
    }
};

void control_switchdevice(void)
{
    GtkWidget *item;
    gboolean sensitive;

    item = gtk_item_factory_get_widget(item_factory,"<control>/Edit/Scan analog ...");
    sensitive = (devs.video.type  != NG_DEV_NONE &&
		 devs.video.flags &  CAN_TUNE);
    gtk_widget_set_sensitive(item,sensitive);

#ifdef HAVE_DVB
    item = gtk_item_factory_get_widget(item_factory,"<control>/Edit/Scan DVB ...");
    sensitive = (NULL != devs.dvb);
    gtk_widget_set_sensitive(item,sensitive);
#endif
}

static void enable_accels(GtkWidget *widget, gpointer dummy)
{
    if (GTK_IS_CONTAINER(widget))
	gtk_container_foreach(GTK_CONTAINER(widget),enable_accels,NULL);
    g_signal_connect(widget, "can-activate-accel", G_CALLBACK(accel_yes), NULL);
}

void create_control(void)
{
    GtkWidget *vbox,*menubar,*scroll;
    GtkWidget *handlebox,*toolbar;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    control_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(control_win),_("control window"));
    gtk_widget_set_size_request(GTK_WIDGET(control_win), -1, 400);
    g_signal_connect(control_win, "delete-event",
		     G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    /* build menus */
    control_accel_group = gtk_accel_group_new();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<control>",
					control_accel_group);
    gtk_item_factory_create_items(item_factory, DIMOF(menu_items),
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
    toolbar = gtk_toolbar_build(toolbaritems, DIMOF(toolbaritems), NULL);

    /* station list */
    st_view  = gtk_tree_view_new();
    st_model = gtk_list_store_new(ST_NUM_COLS,
				  G_TYPE_STRING,   // nane
				  G_TYPE_STRING,   // group
				  G_TYPE_STRING,   // hotkey
				  G_TYPE_STRING,   // channel
				  G_TYPE_INT,      // tsid
				  G_TYPE_INT,      // pnr
				  G_TYPE_POINTER,  // widget
				  G_TYPE_BOOLEAN); // active
    gtk_tree_view_set_model(GTK_TREE_VIEW(st_view),
			    GTK_TREE_MODEL(st_model));
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_NEVER,
				   GTK_POLICY_AUTOMATIC);
    g_signal_connect(st_view, "row-activated",
		     G_CALLBACK(station_activate), NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "Name", renderer,
	 "text",        ST_COL_NAME,
	 "weight-set",  ST_STATE_ACTIVE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "Group", renderer,
	 "text",        ST_COL_GROUP,
	 "weight-set",  ST_STATE_ACTIVE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "Key", renderer,
	 "text",        ST_COL_HOTKEY,
	 "weight-set",  ST_STATE_ACTIVE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "Ch", renderer,
	 "text",        ST_COL_CHANNEL,
	 "weight-set",  ST_STATE_ACTIVE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "xalign",      1.0,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "TSID", renderer,
	 "text",        ST_COL_TSID,
	 "visible",     ST_COL_TSID,
	 "weight-set",  ST_STATE_ACTIVE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer,
		 "weight",      PANGO_WEIGHT_BOLD,
		 "xalign",      1.0,
		 NULL);
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "PNR", renderer,
	 "text",        ST_COL_PNR,
	 "visible",     ST_COL_PNR,
	 "weight-set",  ST_STATE_ACTIVE,
	 NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes
	(GTK_TREE_VIEW(st_view), -1, "", renderer,
	 NULL);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(st_view), ST_COL_NAME);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(st_model), ST_COL_NAME,
				    gtk_sort_iter_compare_str,
                                    GINT_TO_POINTER(ST_COL_NAME), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_NAME);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(st_view), ST_COL_GROUP);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(st_model), ST_COL_GROUP,
				    gtk_sort_iter_compare_str,
                                    GINT_TO_POINTER(ST_COL_GROUP), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_GROUP);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(st_view), ST_COL_CHANNEL);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(st_model), ST_COL_CHANNEL,
				    gtk_sort_iter_compare_str,
                                    GINT_TO_POINTER(ST_COL_CHANNEL), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_CHANNEL);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(st_view), ST_COL_TSID);
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(st_model), ST_COL_TSID,
				    gtk_sort_iter_compare_int,
                                    GINT_TO_POINTER(ST_COL_TSID), NULL);
    gtk_tree_view_column_set_sort_column_id(col, ST_COL_TSID);

    /* configure */
    col = gtk_tree_view_get_column(GTK_TREE_VIEW(st_view), ST_COL_NAME);
    gtk_tree_view_column_set_resizable(col,True);

    col = gtk_tree_view_get_column(GTK_TREE_VIEW(st_view), ST_COL_GROUP);
    gtk_tree_view_column_set_resizable(col,True);

    /* dnd */
    gtk_drag_dest_set(st_view,
		      // GTK_DEST_DEFAULT_ALL,
		      GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		      dnd_targets, DIMOF(dnd_targets),
                      GDK_ACTION_COPY);
    g_signal_connect(st_view, "drag-data-received",
		     G_CALLBACK(drag_data_received), NULL);
    g_signal_connect(st_view, "drag-drop",
		     G_CALLBACK(drag_drop), NULL);

    /* other widgets */
    control_status = gtk_widget_new(GTK_TYPE_LABEL,
				    "label",  "status line",
				    "xalign", 0.0,
				    NULL);

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    handlebox = gtk_handle_box_new();
    gtk_container_add(GTK_CONTAINER(handlebox), toolbar);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(control_win), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), handlebox, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(scroll), st_view);
    gtk_box_pack_end(GTK_BOX(vbox), control_status, FALSE, TRUE, 0);

    /* dynamic stuff */
    init_channel_list();
    init_devices_list();
    init_freqtab_list();
    control_switchdevice();
    enable_accels(menubar,NULL);

    return;
}
