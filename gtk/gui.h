
/* ------------------------------------------------------------------- */
/* i18n                                                                */

#define TITLE_TIME          6000
#define ONSCREEN_TIME       5000
#define SPACING               12

#define _(string) string

/* ------------------------------------------------------------------- */
/* gui-misc.c                                                          */

#define THIS_IS_GPLv2							\
  "This program is free software; you can redistribute it and/or modify\n" \
  "it under the terms of the GNU General Public License version 2 as\n" \
  "published by the Free Software Foundation.\n"

extern void gtk_quit_cb(void);

extern gboolean gtk_wm_delete_quit(GtkWidget *widget, GdkEvent  *event,
				   gpointer   data);

extern void gtk_about_box(GtkWindow *parent, char *name, char *version,
			  char *text);
extern void gtk_error_box(GtkWindow *parent, char *title, char *text);
extern GtkBox *gtk_add_hbox_with_label(GtkBox *vbox, char *text);

gint gtk_sort_iter_compare_int(GtkTreeModel *model,
			       GtkTreeIter  *a,
			       GtkTreeIter  *b,
			       gpointer      userdata);
gint gtk_sort_iter_compare_str(GtkTreeModel *model,
			       GtkTreeIter  *a,
			       GtkTreeIter  *b,
			       gpointer      userdata);

/* ------------------------------------------------------------------- */
/* gui-control.c                                                       */

extern int           fs;

extern char          *curr_station;
extern char          *pick_device_new;

extern GtkWidget     *control_win;
extern GtkWidget     *main_win;

extern GtkAccelGroup *control_accel_group;
extern GtkWidget     *control_st_menu;
extern GtkWidget     *control_status;

extern void create_control(void);
extern void create_onscreen(void);
extern void display_onscreen(char *title);

extern void menu_cb_fullscreen(void);
void x11_station_activate(char *current);


/* ------------------------------------------------------------------- */
/* gui-dvbtune.c                                                       */

extern GtkWidget *dvbtune_dialog;

void create_dvbtune(GtkWindow *parent);
