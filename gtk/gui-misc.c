#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <X11/Xlib.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "list.h"
#include "commands.h"
#include "gui.h"

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

GtkBox *gtk_add_hbox_with_label(GtkBox *vbox, char *text)
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

/* ---------------------------------------------------------------------------- */

static void dialog_destroy(GtkDialog *dialog,
			   gint arg1,
			   gpointer user_data)
{
    gtk_widget_destroy(GTK_WIDGET(dialog));
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

    label = gtk_label_new (text);
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
    gtk_box_pack_start(box, label, TRUE, TRUE, 0);
    g_signal_connect(errbox, "response",
		     G_CALLBACK(dialog_destroy), NULL);
    gtk_widget_show_all(errbox);
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
