
/* ------------------------------------------------------------------- */
/* i18n                                                                */

#define TITLE_TIME          6000
#define ONSCREEN_TIME       5000

#define _(string) string

/* ------------------------------------------------------------------- */
/* gui-misc.c                                                          */

extern void gtk_quit_cb(void);

extern gboolean gtk_wm_delete_quit(GtkWidget *widget, GdkEvent  *event,
				   gpointer   data);

extern void gtk_about_box(GtkWindow *parent, char *name, char *version,
			  char *text);

#define THIS_IS_GPLv2 \
  "This program is free software; you can redistribute it and/or modify\n" \
  "it under the terms of the GNU General Public License version 2 as\n" \
  "published by the Free Software Foundation.\n"


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


/* ------------------------------------------------------------------- */
/* gui-dvbtune.c                                                       */

extern GtkWidget *dvbtune_dialog;

void create_dvbtune(GtkWindow *parent);
