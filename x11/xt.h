#include <X11/Xlib.h>
#include <X11/Xmd.h>
#ifdef HAVE_LIBXXF86DGA
# include <X11/extensions/xf86dga.h>
# include <X11/extensions/xf86dgastr.h>
#endif
#ifdef HAVE_LIBXXF86VM
# include <X11/extensions/xf86vmode.h>
# include <X11/extensions/xf86vmstr.h>
#endif
#ifdef HAVE_LIBXINERAMA
# include <X11/extensions/Xinerama.h>
#endif
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif
#ifdef HAVE_LIBXRANDR
# include <X11/extensions/Xrandr.h>
#endif
#ifdef HAVE_LIBXDPMS
# include <X11/extensions/dpms.h>
/* XFree 3.3.x has'nt prototypes for this ... */
Bool   DPMSQueryExtension(Display*, int*, int*);
Bool   DPMSCapable(Display*);
Status DPMSInfo(Display*, CARD16*, BOOL*);
Status DPMSEnable(Display*);
Status DPMSDisable(Display*);
#endif

struct ARGS {
    /* int */
    int  debug;

    /* boolean */
    int  help;
    int  printver;
    int  readconfig;
    int  writeconfig;

    /* hardware management */
    int  hwls;
    int  hwconfig;
    int  hwstore;

    int  fullscreen;
};

extern struct ARGS args;
extern XtResource args_desc[];
extern XrmOptionDescRec opt_desc[];

extern const int args_count;
extern const int opt_count;

/*----------------------------------------------------------------------*/

extern XtAppContext      app_context;
extern Widget            app_shell, tv;
extern Widget            on_shell;
extern Display           *dpy;
extern int               stay_on_top;

extern XVisualInfo       vinfo;
extern Colormap          colormap;

extern int               have_dga;
extern int               have_vm;
extern int               have_randr;
extern int               fs;

extern void              *movie_state;
extern int               movie_blit;

extern XtIntervalId      zap_timer,scan_timer;

#ifdef HAVE_LIBXXF86VM
extern int               vm_count;
extern XF86VidModeModeInfo **vm_modelines;
#endif
#ifdef HAVE_LIBXINERAMA
extern XineramaScreenInfo *xinerama;
extern int                nxinerama;
#endif
#ifdef HAVE_LIBXRANDR
extern XRRScreenSize      *randr;
extern int                nrandr;
#endif

extern char v4l_conf[128];

#define ZAP_TIME            8000
#define CAP_TIME             100
#define SCAN_TIME            100

#define ONSCREEN_TIME       5000
#define TITLE_TIME          6000
#define WIDTH_INC             32
#define HEIGHT_INC            24
#define VIDMODE_DELAY        100   /* 0.1 sec */

/*----------------------------------------------------------------------*/

/* defined in main.c / motif.c */
void toolkit_set_label(Widget widget, char *str);

/*----------------------------------------------------------------------*/

Boolean ExitWP(XtPointer client_data);
void ExitCB(Widget widget, XtPointer client_data, XtPointer calldata);
void do_exit(void);
void CloseMainAction(Widget widget, XEvent *event,
		     String *params, Cardinal *num_params);
void ZapAction(Widget, XEvent*, String*, Cardinal*);
void ScanAction(Widget, XEvent*, String*, Cardinal*);
void RatioAction(Widget, XEvent*, String*, Cardinal*);
void LaunchAction(Widget, XEvent*, String*, Cardinal*);
void VtxAction(Widget, XEvent*, String*, Cardinal*);
void FilterAction(Widget, XEvent*, String*, Cardinal*);
void EventAction(Widget, XEvent*, String*, Cardinal*);

void do_fullscreen(void);

void create_onscreen(WidgetClass class);
Boolean rec_work(XtPointer client_data);

void exec_done(int signal);
/* static void exec_output(XtPointer data, int *fd, XtInputId * iproc); */
int exec_x11(char **argv);
void exec_player(char *moviefile);
void xt_siginit(void);

void new_title(char *txt);
void new_message(char *txt);
void change_audio(int mode);
void tv_expose_event(Widget widget, XtPointer client_data,
		     XEvent *event, Boolean *d);

/*----------------------------------------------------------------------*/

void command_cb(Widget widget, XtPointer clientdata, XtPointer call_data);
void pick_device_cb(Widget widget, XtPointer clientdata, XtPointer call_data);

/*----------------------------------------------------------------------*/

void CommandAction(Widget, XEvent*, String*, Cardinal*);
void RemoteAction(Widget, XEvent*, String*, Cardinal*);
void set_property(void);

/*----------------------------------------------------------------------*/

void x11_misc_init(Display *dpy);
void xfree_xinerama_init(Display *dpy);

void grabber_init(char *dev);
void grabber_fini(void);
void visual_init(char *n1, char *n2);
void hello_world(char *name);
void handle_cmdline_args(int *argc, char **argv);
int x11_ctrl_alt_backspace(Display *dpy);

void mouse_event(Widget widget, XtPointer client_data,
		 XEvent *event, Boolean *d);

/*----------------------------------------------------------------------*/

extern char x11_vbi_station[];

int x11_vbi_start(char *device, int pid);
int x11_vbi_tuned(void);
void x11_vbi_stop(void);

/*----------------------------------------------------------------------*/

int xt_lirc_init(void);
int xt_joystick_init(void);
void init_icon_window(Widget shell,WidgetClass class);
void xt_kbd_init(Widget tv);

/*----------------------------------------------------------------------*/

void create_pointers(Widget);
void create_bitmaps(Widget);

extern Cursor  left_ptr;
extern Cursor  menu_ptr;
extern Cursor  qu_ptr;
extern Cursor  no_ptr;

extern Pixmap bm_yes;
extern Pixmap bm_no;

/*----------------------------------------------------------------------*/

int xt_handle_pending(Display *dpy);
int xt_input_init(Display *dpy);
int xt_main_loop(void);
