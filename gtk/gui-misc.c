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
    char title[128];

    snprintf(title, sizeof(title), "About %s version %s",
	     name, version);
    about = gtk_dialog_new_with_buttons(title,
					parent, 0,
					GTK_STOCK_OK, GTK_RESPONSE_OK,
					NULL);
    label = gtk_label_new (text);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(about)->vbox),
		       label, TRUE, TRUE, 0);
    g_signal_connect(about, "response",
		     G_CALLBACK(dialog_destroy), NULL);
    gtk_widget_show_all(about);
}
