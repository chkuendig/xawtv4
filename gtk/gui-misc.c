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

