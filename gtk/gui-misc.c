#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>
#ifdef HAVE_LIBXDPMS
# include <X11/extensions/dpms.h>
#endif

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "list.h"
#include "commands.h"
#include "xscreensaver.h"
#include "gui.h"

static int panic = 0;
static int stderr_redirect = 0;
extern int gtk_have_x11;

/* ---------------------------------------------------------------------------- */

void gtk_quit_cb(void)
{
    command_pending++;
    exit_application++;
}

gboolean gtk_wm_delete_quit(GtkWidget *widget,
			    GdkEvent  *event,
			    gpointer   data)
{
    gtk_quit_cb();
    return TRUE;
}

GtkBox* gtk_add_hbox_with_label(GtkBox *vbox, char *text)
{
    GtkWidget *label;
    GtkBox *hbox;

    hbox = GTK_BOX(gtk_hbox_new(TRUE, SPACING));
    gtk_box_pack_start(vbox, GTK_WIDGET(hbox), TRUE, TRUE, 0);

    label = gtk_widget_new(GTK_TYPE_LABEL,
			   "label",  text,
			   "xalign", 0.0,
			   NULL);
    gtk_box_pack_start(hbox, label, TRUE, TRUE, 0);
    return hbox;
}

GtkWidget* gtk_toolbar_build(struct toolbarbutton *btns, int count,
			     void *user_data)
{
    GtkWidget   *toolbar;
    GtkTooltips *tooltips = NULL;
    GtkToolItem *item;
    int i;
    
    toolbar = gtk_toolbar_new();
    for (i = 0; i < count; i++) {
	if (!btns[i].text) {
	    item = gtk_separator_tool_item_new();
	} else {
	    if (btns[i].toggle) {
		item = gtk_toggle_tool_button_new();
		gtk_tool_button_set_label(GTK_TOOL_BUTTON(item),
					  gettext(btns[i].text));
	    } else {
		item = gtk_tool_button_new(NULL, gettext(btns[i].text));
	    }
	    if (btns[i].stock)
		gtk_tool_button_set_stock_id(GTK_TOOL_BUTTON(item),
					     btns[i].stock);
	    if (btns[i].tooltip) {
		if (NULL == tooltips)
		    tooltips = gtk_tooltips_new();
		gtk_tool_item_set_tooltip(GTK_TOOL_ITEM(item),
					  tooltips,
					  gettext(btns[i].tooltip),
					  btns[i].priv);
	    }
	    if (btns[i].callback)
		g_signal_connect(item, btns[i].toggle ? "toggled" : "clicked",
				 btns[i].callback, user_data);
	}
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar),item,-1);
    }
    return toolbar;
}

void gtk_toolbar_add_widget(GtkWidget *toolbar, GtkWidget *widget, gint pos)
{
    GtkToolItem *item;

    item = gtk_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar),item,pos);
    gtk_container_add(GTK_CONTAINER(item),widget);
}

/* ---------------------------------------------------------------------------- */

static int screensaver_off = 0;
static int timeout, interval, prefer_blanking, allow_exposures;
#ifdef HAVE_LIBXDPMS
static BOOL dpms_on;
CARD16 dpms_state;
int dpms_dummy;
#endif
static Window xscreensaver_window;
static guint xscreensaver_timer;

static gboolean xscreensaver_timefunc(gpointer data)
{
    Display *dpy = data;

    if (debug)
	fprintf(stderr,"xscreensaver_timefunc\n");
    xscreensaver_send_deactivate(dpy, xscreensaver_window);
    return TRUE;
}

void gtk_screensaver_disable(Display *dpy)
{
    if (screensaver_off)
	return;
    screensaver_off = 1;

    XGetScreenSaver(dpy,&timeout,&interval,
		    &prefer_blanking,&allow_exposures);
    XSetScreenSaver(dpy,0,0,DefaultBlanking,DefaultExposures);
#ifdef HAVE_LIBXDPMS
    if ((DPMSQueryExtension(dpy, &dpms_dummy, &dpms_dummy)) && 
	(DPMSCapable(dpy))) {
	DPMSInfo(dpy, &dpms_state, &dpms_on);
	DPMSDisable(dpy); 
    }
#endif

    xscreensaver_init(dpy);
    xscreensaver_window = xscreensaver_find_window(dpy, NULL);
    if (xscreensaver_window) {
	if (debug)
	    fprintf(stderr,"xscreensaver window is 0x%lx\n",xscreensaver_window);
	xscreensaver_timer = g_timeout_add(50 * 1000, xscreensaver_timefunc,
					   dpy);
    } else {
	if (debug)
	    fprintf(stderr,"xscreensaver not running\n");
    }
}

void gtk_screensaver_enable(Display *dpy)
{
    if (!screensaver_off)
	return;
    screensaver_off = 0;

    XSetScreenSaver(dpy,timeout,interval,prefer_blanking,allow_exposures);
#ifdef HAVE_LIBXDPMS
    if ((DPMSQueryExtension(dpy, &dpms_dummy, &dpms_dummy)) && 
	(DPMSCapable(dpy)) && (dpms_on)) {
	DPMSEnable(dpy);
    }
#endif

    if (xscreensaver_timer)
	g_source_destroy(g_main_context_find_source_by_id
			 (g_main_context_default(), xscreensaver_timer));
    xscreensaver_window = 0;
    xscreensaver_timer  = 0;
}

/* ---------------------------------------------------------------------------- */

struct unclutter {
    GtkWidget  *widget;
    GdkCursor  *on,*off;
    guint      timer;
    int        idle;
    int        hidden;
};

static gboolean unclutter_timefunc(gpointer data)
{
    struct unclutter *u = data;

    u->idle++;
    if (!u->hidden && u->idle > 5) {
	u->hidden = 1;
	if (debug)
	    fprintf(stderr,"%s hide\n",__FUNCTION__);
	gdk_window_set_cursor(u->widget->window, u->off);
    }
    return TRUE;
}

static gboolean
unclutter_mouse(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct unclutter *u = data;

    if (u->hidden) {
	u->hidden = 0;
	if (debug)
	    fprintf(stderr,"%s: show\n",__FUNCTION__);
	gdk_window_set_cursor(u->widget->window, u->on);
    }
    u->idle = 0;
    return FALSE;
}

static GdkCursor* empty_cursor(void)
{
    static char bits[32];
    GdkCursor *cursor;
    GdkPixmap *pixmap;
    GdkColor fg = { 0, 0, 0, 0 };
    GdkColor bg = { 0, 0, 0, 0 };
 
    pixmap = gdk_bitmap_create_from_data(NULL, bits, 16, 16);
    cursor = gdk_cursor_new_from_pixmap(pixmap, pixmap, &fg, &bg, 0, 0);
    gdk_pixmap_unref(pixmap);
    return cursor;
}

void gtk_unclutter(GtkWidget *widget)
{
    struct unclutter *u;

    u = malloc(sizeof(*u));
    memset(u,0,sizeof(*u));

    u->widget = widget;
    u->on     = gdk_cursor_new(GDK_LEFT_PTR);
    u->off    = empty_cursor();
    u->timer  = g_timeout_add(1 * 1000, unclutter_timefunc, u);
    gtk_widget_add_events(widget, GDK_POINTER_MOTION_MASK);
    g_signal_connect(widget, "motion-notify-event",
		     G_CALLBACK(unclutter_mouse), u);
}

/* ---------------------------------------------------------------------------- */

struct stderr_handler {
    GtkWidget  *win,*label;
    char       *messages;
    size_t     msgsize;
    int        pipe_stderr;
    int        real_stderr;
    GIOChannel *ch;
    guint      id;
};

static gboolean
stderr_data(GIOChannel *source, GIOCondition condition,
	    gpointer data)
{
    struct stderr_handler *h = data;
    char buf[1024];
    int rc;
    
    rc = read(h->pipe_stderr,buf,sizeof(buf)-1);
    if (rc <= 0) {
	/* shouldn't happen */
	dup2(h->real_stderr,2);
	close(h->pipe_stderr);
	close(h->real_stderr);
	gtk_widget_destroy(h->win);
	free(h);
	fprintf(stderr,"Oops: stderr redirection broke\n");
	return FALSE;
    }
    buf[rc] = 0;

    h->messages = realloc(h->messages,h->msgsize+rc+1);
    if (!h->messages) {
	/* oom */
	h->msgsize = 0;
	return TRUE;
    }
    strcpy(h->messages+h->msgsize,buf);
    h->msgsize += rc;

    gtk_window_resize(GTK_WINDOW(h->win), 100, 20);
    gtk_label_set_text(GTK_LABEL(h->label),h->messages);
    gtk_widget_show_all(h->win);
    return TRUE;
}

static void stderr_hide(GtkDialog *dialog, gint arg1, gpointer user_data)
{
    struct stderr_handler *h = user_data;

    if (h->messages) {
	free(h->messages);
	h->messages = NULL;
	h->msgsize  = 0;
    }
    if (panic) {
	command_pending++;
	exit_application++;
	gtk_main_quit();
    }
    gtk_widget_hide(h->win);
}

void gtk_redirect_stderr_to_gui(GtkWindow *parent)
{
    struct stderr_handler *h;
    GtkBox *box;
    int p[2];

    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    h->win = gtk_dialog_new_with_buttons("stderr messages", parent, 0,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 NULL);
    box = GTK_BOX(GTK_DIALOG(h->win)->vbox);
    gtk_box_set_spacing(box, SPACING);
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);
    h->label = gtk_label_new("");
    gtk_widget_set(h->label,
		   "xpad", SPACING,
		   "ypad", SPACING,
		   NULL);
    gtk_box_pack_start(box, h->label, TRUE, TRUE, 0);
    g_signal_connect(h->win, "response", G_CALLBACK(stderr_hide), h);

    pipe(p);
    h->pipe_stderr = p[0];
    h->real_stderr = dup(2);
    dup2(p[1],2);
    close(p[1]);

    h->ch = g_io_channel_unix_new(h->pipe_stderr);
    h->id = g_io_add_watch(h->ch, G_IO_IN, stderr_data, h);
    stderr_redirect = 1;
}

/* ---------------------------------------------------------------------------- */

static void dialog_destroy(GtkDialog *dialog,
			   gint arg1,
			   gpointer user_data)
{
    gtk_widget_destroy(GTK_WIDGET(dialog));
    if (panic) {
	command_pending++;
	exit_application++;
	gtk_main_quit();
    }
}

void gtk_about_box(GtkWindow *parent, char *name, char *version, char *text)
{
    GtkWidget *about,*label;
    GtkBox *box;
    char title[128];

    snprintf(title, sizeof(title), "About %s version %s",
	     name, version);
    about = gtk_dialog_new_with_buttons(title, parent, 0,
					GTK_STOCK_OK, GTK_RESPONSE_OK,
					NULL);
    box = GTK_BOX(GTK_DIALOG(about)->vbox);
    gtk_box_set_spacing(box, SPACING);
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);

    label = gtk_label_new(text);
    gtk_widget_set(label,
		   "xpad", SPACING,
		   "ypad", SPACING,
		   NULL);
    gtk_box_pack_start(box, label, TRUE, TRUE, 0);
    g_signal_connect(about, "response",
		     G_CALLBACK(dialog_destroy), NULL);
    gtk_widget_show_all(about);
}

void gtk_error_box(GtkWindow *parent, char *title, char *text)
{
    GtkWidget *errbox,*label;
    GtkBox *box;

    errbox = gtk_dialog_new_with_buttons(title, parent, 0,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 NULL);
    box = GTK_BOX(GTK_DIALOG(errbox)->vbox);
    gtk_box_set_spacing(box, SPACING);
    gtk_container_set_border_width(GTK_CONTAINER(box), SPACING);

    label = gtk_label_new (text);
    gtk_widget_set(label,
		   "xpad", SPACING,
		   "ypad", SPACING,
		   NULL);
    gtk_box_pack_start(box, label, TRUE, TRUE, 0);
    g_signal_connect(errbox, "response",
		     G_CALLBACK(dialog_destroy), NULL);
    gtk_widget_show_all(errbox);
}

void gtk_panic_box(int have_x11, char *text)
{
    panic = 1;
    if (!have_x11) {
	/* no X11 */
	fprintf(stderr,"%s",text);
	exit(1);
    }

    if (stderr_redirect) {
	fprintf(stderr,"\nFatal Error:\n%s",text);
    } else {
	gtk_error_box(NULL,"Fatal Error",text);
    }
    gtk_main();
    exit(1);
}

/* ---------------------------------------------------------------------------- */

gint gtk_sort_iter_compare_int(GtkTreeModel *model,
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

gint gtk_sort_iter_compare_str(GtkTreeModel *model,
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

gint gtk_sort_iter_compare_eq(GtkTreeModel *model,
			      GtkTreeIter  *a,
			      GtkTreeIter  *b,
			      gpointer      userdata)
{
    /* dummy function to _not_ sort */
    return 0;
}
