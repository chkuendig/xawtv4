
/* ------------------------------------------------------------------- */
/* i18n                                                                */

#define _(string) string
  
/* ------------------------------------------------------------------- */
/* gui-misc.c                                                          */

extern void gtk_quit_cb(void);

extern gboolean gtk_wm_delete_quit(GtkWidget *widget, GdkEvent  *event,
				   gpointer   data);

/* ------------------------------------------------------------------- */
/* gui-control.c                                                       */

extern char          *curr_station;
extern char          *pick_device_new;

extern GtkWidget     *control_win;
extern GtkWidget     *main_win;

extern GtkAccelGroup *control_accel_group;
extern GtkWidget     *control_st_menu;

extern void create_control(void);
