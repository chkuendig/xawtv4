
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
