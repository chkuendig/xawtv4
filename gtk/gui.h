
/* ------------------------------------------------------------------- */
/* i18n                                                                */

#define TITLE_TIME          6000
#define ONSCREEN_TIME       5000
#define EPG_TIME            5000
#define SPACING               12

#define _(string) string

/* ------------------------------------------------------------------- */
/* gui-misc.c                                                          */

#define THIS_IS_GPLv2							\
  "This program is free software; you can redistribute it and/or modify\n" \
  "it under the terms of the GNU General Public License version 2 as\n" \
  "published by the Free Software Foundation.\n"

struct toolbarbutton {
    char           *text;
    char           *tooltip;
    char           *priv;
    char           *stock;
    GtkSignalFunc  callback;
    int            toggle:1;
};

extern void gtk_quit_cb(void);

extern gboolean gtk_wm_delete_quit(GtkWidget *widget, GdkEvent  *event,
				   gpointer   data);

extern void gtk_about_box(GtkWindow *parent, char *name, char *version,
			  char *text);
extern void gtk_error_box(GtkWindow *parent, char *title, char *text);
extern void gtk_panic_box(int have_x11, char *text);

extern GtkBox* gtk_add_hbox_with_label(GtkBox *vbox, char *text);
extern GtkWidget* gtk_toolbar_build(struct toolbarbutton *btns, int count,
				    void *user_data);
void gtk_toolbar_add_widget(GtkWidget *toolbar, GtkWidget *widget,
			    gint pos);

void gtk_screensaver_disable(Display *dpy);
void gtk_screensaver_enable(Display *dpy);
void gtk_unclutter(GtkWidget *widget);

void gtk_redirect_stderr_to_gui(GtkWindow *parent);

gint gtk_sort_iter_compare_int(GtkTreeModel *model,
			       GtkTreeIter  *a,
			       GtkTreeIter  *b,
			       gpointer      userdata);
gint gtk_sort_iter_compare_str(GtkTreeModel *model,
			       GtkTreeIter  *a,
			       GtkTreeIter  *b,
			       gpointer      userdata);
gint gtk_sort_iter_compare_eq(GtkTreeModel *model,
			       GtkTreeIter  *a,
			       GtkTreeIter  *b,
			       gpointer      userdata);

/* ------------------------------------------------------------------- */
/* gui-control.c                                                       */

struct epgitem;

extern int           fs;

extern char          *curr_station;
extern char          *pick_device_new;

extern GtkWidget     *main_win;
extern GtkWidget     *epg_popup;
extern GtkWidget     *control_win;
extern GtkWidget     *control_st_menu;
extern GtkWidget     *control_status;
extern GtkAccelGroup *control_accel_group;

extern void create_control(void);
extern void create_onscreen(void);
extern void display_onscreen(char *title);
extern void create_epg(void);
extern void display_epg(GtkWindow *win, struct epgitem *epg);
/** whether the epg popup is currently shown */
extern gboolean epg_shown(void);

extern void menu_cb_fullscreen(void);
extern void x11_station_activate(char *current);

extern void control_switchdevice(void);

/* ------------------------------------------------------------------- */
/* gui-dvbscan.c                                                       */

extern GtkWidget *dvbscan_win;
void dvbscan_create_window(int s);
void dvbscan_show_window(void);

/* ------------------------------------------------------------------- */
/* gui-dvbtune.c                                                       */

extern GtkWidget *dvbtune_dialog;
extern GtkWidget *satellite_dialog;
extern char *dvbtune_lnb;
extern char *dvbtune_sat;

void create_dvbtune(GtkWindow *parent);
void create_satellite(GtkWindow *parent);

/* ------------------------------------------------------------------- */
/* gui-teletext.c                                                      */

struct vbi_state;
struct dvbmon;

void vbi_create_window(struct vbi_state *vbi, struct dvbmon *dvbmon);

/* ------------------------------------------------------------------- */
/* gui-analog.c                                                        */

extern GtkWidget *analog_win;

void analog_create_window(void);
void analog_set_freqtab(void);
void analog_set_channel(char *channel);

/* ------------------------------------------------------------------- */
/* gui-epg.c                                                           */

extern GtkWidget     *epg_win;

extern void create_epgwin(GtkWindow* parent);
extern void epgwin_hide();
extern void epgwin_show(struct epgitem* epg);

/* ------------------------------------------------------------------- */
/* xawtv.c                                                             */

extern int           recording;

int mm_rec_start(void);
int mm_rec_stop(void);
