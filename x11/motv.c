/*
 * Openmotif user interface
 *
 *   (c) 2000-2002 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <Xm/Xm.h>
#include <Xm/XmStrDefs.h>
#include <Xm/Primitive.h>
#include <Xm/Form.h>
#include <Xm/Label.h>
#include <Xm/RowColumn.h>
#include <Xm/PushB.h>
#include <Xm/ToggleB.h>
#include <Xm/CascadeB.h>
#include <Xm/Separator.h>
#include <Xm/Scale.h>
#include <Xm/Protocols.h>
#include <Xm/Display.h>
#include <Xm/Text.h>
#include <Xm/FileSB.h>
#include <Xm/ComboBox.h>
#include <Xm/ScrolledW.h>
#include <Xm/MessageB.h>
#include <Xm/Frame.h>
#include <Xm/SelectioB.h>
#include <Xm/TransferP.h>
#include <Xm/DragIcon.h>
#include <Xm/Container.h>
#include <Xm/IconG.h>
#if 0
#include <Xm/TabStack.h>
#endif
#include <X11/extensions/XShm.h>

#include "grab-ng.h"
#include "tv-config.h"
#include "commands.h"
#include "parseconfig.h"
#include "devs.h"
#include "tuning.h"
#include "capture.h"
#include "wmhooks.h"
#include "atoms.h"
#include "x11.h"
#include "xt.h"
#include "xv.h"
#include "man.h"
#include "RegEdit.h"
#include "icons.h"
#include "sound.h"
#include "complete.h"
#include "writefile.h"
#include "list.h"
#include "vbi-data.h"
#include "vbi-x11.h"
#include "blit.h"
#include "av-sync.h"
#include "dvb.h"

/*----------------------------------------------------------------------*/

int debug;

/*----------------------------------------------------------------------*/

static void PopupAction(Widget, XEvent*, String*, Cardinal*);
static void DebugAction(Widget, XEvent*, String*, Cardinal*);
static void IpcAction(Widget, XEvent*, String*, Cardinal*);
static void ontop_ac(Widget, XEvent*, String*, Cardinal*);

#ifdef HAVE_ZVBI
static void chscan_cb(Widget widget, XtPointer clientdata,
		      XtPointer call_data);
#endif
static void pref_manage_cb(Widget widget, XtPointer clientdata,
			   XtPointer call_data);
static void add_cmd_callback(Widget widget, String callback,char *command,
			     const char *arg1, const char *arg2);

static XtActionsRec actionTable[] = {
    { "CloseMain",   CloseMainAction   },
    { "Command",     CommandAction     },
    { "Popup",       PopupAction       },
    { "Debug",       DebugAction       },
    { "Remote",      RemoteAction      },
    { "Zap",         ZapAction         },
    { "Scan",        ScanAction        },
    { "man",         man_action        },
    { "Ratio",       RatioAction       },
    { "Launch",      LaunchAction      },
#ifdef HAVE_ZVBI
    { "Vtx",         VtxAction         },
#endif
    { "Complete",    CompleteAction    },
    { "Ipc",         IpcAction         },
    { "Filter",      FilterAction      },
    { "StayOnTop",   ontop_ac          },
    { "Event",       EventAction       },
};

static String fallback_ressources[] = {
#include "MoTV4.h"
    NULL
};

XtIntervalId audio_timer;

static Widget st_menu1,st_menu2;
static Widget control_shell,str_shell,levels_shell,levels_toggle;
static Widget launch_menu,opt_menu,cap_menu,freq_menu;
static Widget chan_viewport,chan_cont;
static Widget st_freq,st_chan,st_name,st_key;
static Widget w_full;
static Widget b_ontop;
#ifdef HAVE_ZVBI
static struct vbi_window *vtx;
#endif

/* attributes */
static Widget attr_shell, attr_rc1;

/* properties */
static Widget prop_dlg,prop_name,prop_key;
static Widget prop_channel,prop_channelL;
static Widget prop_vdr,prop_vdrL;
static Widget prop_group;

/* preferences */
static Widget pref_dlg;
static Widget pref_osd,pref_ntsc,pref_partial,pref_quality;

/* streamer */
static Widget driver_menu, driver_option;
static Widget audio_menu, audio_option;
static Widget video_menu, video_option;
static Widget m_rate,m_fps,m_fvideo,m_status;
static Widget m_faudio,m_faudioL,m_faudioB;

static struct ng_writer *movie_driver;
static unsigned int i_movie_driver;
static unsigned int movie_audio;
static unsigned int movie_video;
static XtWorkProcId rec_work_id;

static struct MY_TOPLEVELS {
    char        *name;
    Widget      *shell;
    int         mapped;
} my_toplevels [] = {
    { "control",   &control_shell },
    { "streamer",  &str_shell     },
    { "attr",      &attr_shell    },
    { "levels",    &levels_shell  },
};
#define TOPLEVELS (sizeof(my_toplevels)/sizeof(struct MY_TOPLEVELS))

struct motif_attr {
    Widget  widget;
    Widget  menu;
    int     value;
};

/*----------------------------------------------------------------------*/
/* debug/test code                                                      */

#if 0
static void
print_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *msg = (char*) clientdata;
    fprintf(stderr,"debug: %s\n",msg);
}

static void
toggle_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmToggleButtonCallbackStruct *tb = call_data;

    if (tb->reason != XmCR_VALUE_CHANGED)
	return;
    fprintf(stderr,"toggle: [%s] set=%s\n",
	    clientdata ? (char*)clientdata : "???",
	    tb->set ? "on" : "off");
}
#endif

void
DebugAction(Widget widget, XEvent *event,
	  String *params, Cardinal *num_params)
{
    fprintf(stderr,"debug: foo\n");
}

/*----------------------------------------------------------------------*/

void toolkit_set_label(Widget widget, char *str)
{
    XmString xmstr;

    if (NULL == str)
	str = "";
    xmstr = XmStringGenerate(str, NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(widget,XmNlabelString,xmstr,NULL);
    XmStringFree(xmstr);
}

static void delete_children(Widget widget)
{
    WidgetList children,list;
    Cardinal nchildren;
    unsigned int i;

    XtVaGetValues(widget,XtNchildren,&children,
		  XtNnumChildren,&nchildren,NULL);
    if (0 == nchildren)
	return;
    list = malloc(sizeof(Widget*)*nchildren);
    memcpy(list,children,sizeof(Widget*)*nchildren);
    for (i = 0; i < nchildren; i++)
	XtDestroyWidget(list[i]);
    free(list);
}

static void
new_channel(void)
{
    set_property();
    toolkit_set_label(st_freq, curr_details);
    toolkit_set_label(st_chan, curr_channel);
    toolkit_set_label(st_name, curr_station);

#if 0
    toolkit_set_label(st_key,(cur_sender != -1 &&
			      NULL != channels[cur_sender]->key) ?
	channels[cur_sender]->key : "");
#endif

    if (zap_timer) {
	XtRemoveTimeOut(zap_timer);
	zap_timer = 0;
    }
    if (scan_timer) {
	XtRemoveTimeOut(scan_timer);
	scan_timer = 0;
    }
    if (audio_timer) {
	XtRemoveTimeOut(audio_timer);
	audio_timer = 0;
    }
}

static void
do_ontop(Boolean state)
{
    unsigned int i;

    if (!wm_stay_on_top)
	return;

    stay_on_top = state;
    wm_stay_on_top(dpy,XtWindow(app_shell),stay_on_top);
    wm_stay_on_top(dpy,XtWindow(on_shell),stay_on_top);
    for (i = 0; i < TOPLEVELS; i++)
	wm_stay_on_top(dpy,XtWindow(*(my_toplevels[i].shell)),
		       (stay_on_top == -1) ? 0 : stay_on_top);
    XmToggleButtonSetState(b_ontop,state,False);
}

static void
ontop_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmToggleButtonCallbackStruct *tb = call_data;

    if (tb->reason != XmCR_VALUE_CHANGED)
	return;
    do_ontop(tb->set);
}

static void
ontop_ac(Widget widget, XEvent *event, String *params, Cardinal *num_params)
{
    do_ontop(stay_on_top ? False : True);
}

/*----------------------------------------------------------------------*/

static void
resize_event(Widget widget, XtPointer client_data, XEvent *event, Boolean *d)
{
#if 0
    static int width = 0, height = 0, first = 1;
    
    switch(event->type) {
    case ConfigureNotify:
	if (first) {
	    video_gd_init(tv,args.xv_image,args.gl);
	    first = 0;
	}
	if (width  != event->xconfigure.width ||
	    height != event->xconfigure.height) {
	    width  = event->xconfigure.width;
	    height = event->xconfigure.height;
	    video_gd_configure(width, height);
	    //XClearWindow(XtDisplay(tv),XtWindow(tv));
	}
	break;
    }
#else
    switch (display_mode) {
    case DISPLAY_OVERLAY:
    case DISPLAY_GRAB:
	/* trigger reconfigure */
	command_pending++;
	break;
    default:
	/* nothing */
	break;
    };
#endif
}

static void
PopupAction(Widget widget, XEvent *event,
	    String *params, Cardinal *num_params)
{
    unsigned int i;

    /* which window we are talking about ? */
    if (*num_params > 0) {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (0 == strcasecmp(my_toplevels[i].name,params[0]))
		break;
	}
	if (i == TOPLEVELS) {
	    fprintf(stderr,"PopupAction: oops: shell not found (name=%s)\n",
		    params[0]);
	    return;
	}
    } else {
	for (i = 0; i < TOPLEVELS; i++) {
	    if (*(my_toplevels[i].shell) == widget)
		break;
	}
	if (i == TOPLEVELS) {
	    fprintf(stderr,"PopupAction: oops: shell not found (%p:%s)\n",
		    widget,XtName(widget));
	    return;
	}
    }

    /* popup/down window */
    if (!my_toplevels[i].mapped) {
	Widget shell = *(my_toplevels[i].shell);
	XtPopup(shell, XtGrabNone);
	if (fs)
	    XRaiseWindow(XtDisplay(shell), XtWindow(shell));
	if (wm_stay_on_top && stay_on_top > 0)
	    wm_stay_on_top(dpy,XtWindow(shell),1);
	my_toplevels[i].mapped = 1;
    } else {
	XtPopdown(*(my_toplevels[i].shell));
	my_toplevels[i].mapped = 0;
    }
}

static void
popup_eh(Widget widget, XtPointer clientdata, XEvent *event, Boolean *cont)
{
    Widget popup = clientdata;

    XmMenuPosition(popup,(XButtonPressedEvent*)event);
    XtManageChild(popup);
}

static void
popupdown_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    int i = 0;
    PopupAction(clientdata, NULL, NULL, &i);
}

static void
destroy_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XtDestroyWidget(clientdata);
}

static void
free_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    free(clientdata);
}

static void
about_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget msgbox;
    
    msgbox = XmCreateInformationDialog(app_shell,"about_box",NULL,0);
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(msgbox,XmNokCallback,destroy_cb,msgbox);
    XtManageChild(msgbox);
}

static void
action_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *calls, *action, *argv[8];
    int argc;

    calls = strdup(clientdata);
    action = strtok(calls,"(),");
    for (argc = 0; NULL != (argv[argc] = strtok(NULL,"(),")); argc++)
	/* nothing */;
    XtCallActionProc(widget,action,NULL,argv,argc);
    free(calls);
}

static void
config_save_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *domain = clientdata;
    write_config_file(domain);
}

/*--- videotext ----------------------------------------------------------*/

static void create_vtx(void)
{
    Widget shell,label;
    
    shell = XtVaCreateWidget("vtx",transientShellWidgetClass,
			     app_shell,
			     XtNoverrideRedirect,True,
			     XtNvisual,vinfo.visual,
			     XtNcolormap,colormap,
			     XtNdepth,vinfo.depth,
			     NULL);
    label = XtVaCreateManagedWidget("label", xmLabelWidgetClass, shell,
				    NULL);
#ifdef HAVE_ZVBI
    vtx = vbi_render_init(shell,label,NULL);
#endif
}

#ifdef HAVE_ZVBI
static void
display_subtitle(struct vbi_page *pg, struct vbi_rect *rect)
{
    static int first = 1;
    static Pixmap pix;
    Dimension x,y,w,h,sw,sh;

    if (NULL == pg) {
	XtPopdown(vtx->shell);
	return;
    }

    if (pix)
	XFreePixmap(dpy,pix);
    pix = vbi_export_pixmap(vtx,pg,rect);
    XtVaSetValues(vtx->tt,XmNlabelPixmap,pix,XmNlabelType,XmPIXMAP,NULL);
    
    XtVaGetValues(app_shell,XtNx,&x,XtNy,&y,XtNwidth,&w,XtNheight,&h,NULL);
    XtVaGetValues(vtx->shell,XtNwidth,&sw,XtNheight,&sh,NULL);
    XtVaSetValues(vtx->shell,XtNx,x+(w-sw)/2,XtNy,y+h-10-sh,NULL);
    XtPopup(vtx->shell, XtGrabNone);
    if (wm_stay_on_top && stay_on_top > 0)
	wm_stay_on_top(dpy,XtWindow(vtx->shell),1);

    if (first) {
	first = 0;
	XDefineCursor(dpy, XtWindow(vtx->shell), left_ptr);
	XDefineCursor(dpy, XtWindow(vtx->tt), left_ptr);
    }
}
#endif

/*----------------------------------------------------------------------*/

static void
new_freqtab(void)
{
    WidgetList children;
    Cardinal nchildren;
    XmStringTable tab;
    char *channel;
    int i;

    /* update menu */
    XtVaGetValues(freq_menu,XtNchildren,&children,
		  XtNnumChildren,&nchildren,NULL);
    for (i = 0; i < nchildren; i++)
	XmToggleButtonSetState(children[i],
			       0 == strcmp(XtName(children[i]),freqtab),
			       False);

    /* update property window */
    tab = malloc(cfg_sections_count(freqtab)*sizeof(*tab));
    for (i = 0, channel = cfg_sections_first(freqtab);
	 NULL != channel;
	 i++, channel = cfg_sections_next(freqtab,channel)) {
	if (debug)
	    fprintf(stderr,"  freqtab: %d - %s\n",i,channel);
	tab[i] = XmStringGenerate(channel, NULL, XmMULTIBYTE_TEXT, NULL);
    }
    XtVaSetValues(prop_channel,
		  XmNitemCount,i,
		  XmNitems,tab,
		  XmNselectedItem,tab[0],
		  XmNsensitive,True,
		  NULL);
    XtVaSetValues(prop_channelL,
		  XmNsensitive,True,
		  NULL);
    while (i > 0)
	XmStringFree(tab[--i]);
    free(tab);
}

/*----------------------------------------------------------------------*/

static void
attr_x11_changed_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct ng_attribute *attr = clientdata;
    struct motif_attr   *x11  = attr->app;
    Widget w;

    switch (attr->type) {
    case ATTR_TYPE_INTEGER:
	XmScaleGetValue(x11->widget,&x11->value);
	break;
    case ATTR_TYPE_BOOL:
	x11->value = XmToggleButtonGetState(x11->widget);
	break;
    case ATTR_TYPE_CHOICE:
	w = NULL;
	XtVaGetValues(x11->widget,XmNmenuHistory,&w,NULL);
	x11->value = ng_attr_getint(attr,XtName(w));
	break;
    }
    attr_write(attr,x11->value,0);
}

static void
addr_x11_add_ctrl(Widget parent, struct ng_attribute *attr)
{
    struct motif_attr *x11;
    Widget push;
    XmString str;
    Arg args[2];
    int i;

    x11 = malloc(sizeof(*x11));
    memset(x11,0,sizeof(*x11));
    attr->app  = x11;
    x11->value = attr_read(attr);
    
    str = XmStringGenerate((char*)attr->name, NULL, XmMULTIBYTE_TEXT, NULL);
    switch (attr->type) {
    case ATTR_TYPE_INTEGER:
	x11->widget = XtVaCreateManagedWidget("scale",
					      xmScaleWidgetClass,parent,
					      XmNtitleString,str,
					      XmNminimum,attr->min,
					      XmNmaximum,attr->max,
					      XmNdecimalPoints,attr->points,
					      NULL);
	XmScaleSetValue(x11->widget,x11->value);
	XtAddCallback(x11->widget,XmNvalueChangedCallback,
		      attr_x11_changed_cb,attr);
	break;
    case ATTR_TYPE_BOOL:
	x11->widget = XtVaCreateManagedWidget("bool",
					      xmToggleButtonWidgetClass,parent,
					      XmNlabelString,str,
					      NULL);
	XmToggleButtonSetState(x11->widget,x11->value,False);
	XtAddCallback(x11->widget,XmNvalueChangedCallback,
		      attr_x11_changed_cb,attr);
	break;
    case ATTR_TYPE_CHOICE:
	x11->widget = XmCreatePulldownMenu(parent,"choiceM",NULL,0);
	XtSetArg(args[0],XmNsubMenuId,x11->widget);
	XtSetArg(args[1],XmNlabelString,str);
	x11->menu = XmCreateOptionMenu(parent,"choiceO",args,2);
	XtManageChild(x11->menu);
	for (i = 0; attr->choices[i].str != NULL; i++) {
	    push = XtVaCreateManagedWidget(attr->choices[i].str,
					   xmPushButtonWidgetClass,x11->widget,
					   NULL);
	    XtAddCallback(push,XmNactivateCallback,
			  attr_x11_changed_cb,attr);
	    if (x11->value == attr->choices[i].nr)
		XtVaSetValues(x11->widget,XmNmenuHistory,push,NULL);
	}
	break;
    }
    XmStringFree(str);
}

static void
attr_x11_update_ctrl(struct ng_attribute *attr, int value)
{
    struct motif_attr *x11;
    WidgetList children;
    Cardinal nchildren;
    unsigned int i;

    x11 = attr->app;
    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	XtVaGetValues(x11->widget,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	for (i = 0; i < nchildren; i++) {
	    if (attr->choices[i].nr == value)
		XtVaSetValues(x11->widget,XmNmenuHistory,children[i],NULL);
	}
	break;
    case ATTR_TYPE_BOOL:
	XmToggleButtonSetState(x11->widget,value,False);
	break;
    case ATTR_TYPE_INTEGER:
	XmScaleSetValue(x11->widget,value);
	break;
    }
    return;
}

static void
addr_x11_del_ctrl(struct ng_attribute *attr)
{
    struct motif_attr *x11 = attr->app;

    if (x11->menu)
	XtDestroyWidget(x11->menu);
    if (x11->widget)
	XtDestroyWidget(x11->widget);
    attr->app = NULL;
    free(x11);
}

static void
create_attrs(void)
{
#if 0
    Widget rc1,frame,rc2;
    XmString str;
    struct list_head *item;
    struct ng_filter *filter;
    int j;
#endif
    Widget attr_scroll;
    
    attr_shell = XtVaAppCreateShell("attr","MoTV4",
				    topLevelShellWidgetClass,
				    dpy,
				    XtNclientLeader,app_shell,
				    XtNvisual,vinfo.visual,
				    XtNcolormap,colormap,
				    XtNdepth,vinfo.depth,
				    XmNdeleteResponse,XmDO_NOTHING,
				    NULL);
    XmdRegisterEditres(attr_shell);
    XmAddWMProtocolCallback(attr_shell,WM_DELETE_WINDOW,
			    popupdown_cb,attr_shell);

#if 0
    attr_tabs = XtVaCreateManagedWidget("tab", xmTabStackWidgetClass, attr_shell,
					NULL);
    attr_rc1 = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, attr_tabs,
				       NULL);
#else
    attr_scroll = XtVaCreateManagedWidget("scroll", xmScrolledWindowWidgetClass,
					  attr_shell, NULL);
    attr_rc1 = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass,
				       attr_scroll, NULL);
#endif

#if 0
    list_for_each(item,&ng_filters) {
	filter = list_entry(item, struct ng_filter, list);
	if (NULL == filter->attrs)
	    continue;
	str = XmStringGenerate(filter->name, NULL,
			       XmMULTIBYTE_TEXT, NULL);
	frame = XtVaCreateManagedWidget("frame",xmFrameWidgetClass,rc1,NULL);
	XtVaCreateManagedWidget("label",xmLabelWidgetClass,frame,
				XmNlabelString,str,
				NULL);
	rc2 = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,frame,NULL);
	XmStringFree(str);
	for (j = 0; NULL != filter->attrs[j].name; j++)
	    filter_add_ctrls(rc2,filter,&filter->attrs[j]);
    }
#endif
}

static void
attr_x11_add_hook(struct ng_attribute *attr)
{
    if (debug)
	fprintf(stderr,"motv: new attribute: \"%s\" \"%s\"\n",
		attr->group, attr->name);
    addr_x11_add_ctrl(attr_rc1, attr);
}

static void
attr_x11_del_hook(struct ng_attribute *attr)
{
    addr_x11_del_ctrl(attr);
}

static void __init attr_x11_hooks_init(void)
{
    attr_add_hook = attr_x11_add_hook;
    attr_del_hook = attr_x11_del_hook;
    attr_update_hook = attr_x11_update_ctrl;
}

/*----------------------------------------------------------------------*/

struct st_group {
    char               *name;
    struct list_head   list;
    struct list_head   stations;

    Widget             menu1, btn1;
    Widget             menu2, btn2;
};
LIST_HEAD(ch_groups);

struct station {
    char               *name;
    struct list_head   global;
    struct list_head   group;

    Widget             menu1;
    Widget             menu2;
    Widget             button;
    XmString           details[3];
};
LIST_HEAD(x11_stations);

static void
add_cmd_callback(Widget widget, String callback,
		 char *command, const char *arg1, const char *arg2)
{
    struct command *cmd;

    cmd = malloc(sizeof(*cmd));
    cmd->argc    = 1;
    cmd->argv[0] = command;
    if (arg1) {
	cmd->argc    = 2;
	cmd->argv[1] = (char*)arg1;
	if (arg2) {
	    cmd->argc    = 3;
	    cmd->argv[2] = (char*)arg2;
	}
    }
    XtAddCallback(widget,callback,command_cb,cmd);
    XtAddCallback(widget,XmNdestroyCallback,free_cb,cmd);
}

static Widget
add_menuitem(char *name, Widget parent, char *k, char *a)
{
    XmString label,accel;
    Widget w;

    label = XmStringGenerate(name, NULL, XmMULTIBYTE_TEXT, NULL);
    if (k && a) {
	accel = XmStringGenerate(k, NULL, XmMULTIBYTE_TEXT, NULL);
	w = XtVaCreateManagedWidget(name,
				    xmPushButtonWidgetClass,parent,
				    XmNlabelString,label,
				    XmNacceleratorText,accel,
				    XmNaccelerator,a,
				    NULL);
	XmStringFree(accel);
    } else {
	w = XtVaCreateManagedWidget(name,
				    xmPushButtonWidgetClass,parent,
				    XmNlabelString,label,
				    NULL);
    }
    XmStringFree(label);
    return w;
}

static struct st_group* group_get(char *name)
{
    struct list_head *item;
    struct st_group *group;

    /* find one */
    list_for_each(item,&ch_groups) {
	group = list_entry(item, struct st_group, list);
	if (0 == strcasecmp(name,group->name))
	    return group;
    }

    /* 404 -> create new */
    group = malloc(sizeof(*group));
    memset(group,0,sizeof(*group));
    group->name = strdup(name);
    INIT_LIST_HEAD(&group->stations);

    group->menu1 = XmCreatePulldownMenu(st_menu1, group->name, NULL,0);
    group->menu2 = XmCreatePulldownMenu(st_menu2, group->name, NULL,0);

    group->btn1 = XtVaCreateManagedWidget(group->name,
					  xmCascadeButtonWidgetClass, st_menu1,
					  XmNsubMenuId, group->menu1,
					  NULL);
    group->btn2 = XtVaCreateManagedWidget(group->name,
					  xmCascadeButtonWidgetClass, st_menu2,
					  XmNsubMenuId, group->menu2,
					  NULL);

    list_add_tail(&group->list,&ch_groups);
    return group;
}

static void x11_station_add(char *name)
{
    struct st_group *group;
    struct station *st;
    char key[32], ctrl[16], accel[64];
    char *keyname, *groupname, *val;
    Arg args[8];
    int n = 0;

    /* hotkey */
    keyname = cfg_get_str("stations", name, "key");
    if (NULL != keyname) {
	if (2 == sscanf(keyname, "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			ctrl,key)) {
	    sprintf(accel,"%s<Key>%s",ctrl,key);
	} else {
	    sprintf(accel,"<Key>%s",keyname);
	}
    } else {
	accel[0] = 0;
    }

    /* submenu */
    groupname = cfg_get_str("stations", name, "group");
    if (NULL != groupname) {
	group = group_get(groupname);
    } else {
	group = NULL;
    }

    /* add channel */
    st = malloc(sizeof(*st));
    memset(st,0,sizeof(*st));
    st->name = strdup(name);

    /* ... menu */
    st->menu1 = add_menuitem(st->name, group ? group->menu1 : st_menu1,
			     keyname, accel);
    add_cmd_callback(st->menu1,XmNactivateCallback,
		     "setstation", st->name, NULL);
    st->menu2 = add_menuitem(st->name, group ? group->menu2 : st_menu2,
			     keyname, accel);
    add_cmd_callback(st->menu2,XmNactivateCallback,
		     "setstation", st->name, NULL);

    /* ... icon */
    val = cfg_get_str("stations", name, "channel");
    if (NULL == val || 0 == strlen(val))
	val = "-";
    st->details[0] = XmStringGenerate(val, NULL, XmMULTIBYTE_TEXT, NULL);

    val = cfg_get_str("stations", name, "key");
    if (NULL == val || 0 == strlen(val))
	val = "-";
    st->details[1] = XmStringGenerate(val, NULL, XmMULTIBYTE_TEXT, NULL);

    val = cfg_get_str("stations", name, "group");
    if (NULL == val || 0 == strlen(val))
	val = "-";
    st->details[2] = XmStringGenerate(val, NULL, XmMULTIBYTE_TEXT, NULL);

    XtSetArg(args[n], XmNdetail,          st->details);   n++;
    XtSetArg(args[n], XmNdetailCount,     3);             n++;
    st->button = XmCreateIconGadget(chan_cont, st->name, args, n);
    XtManageChild(st->button);

    list_add_tail(&st->global,&x11_stations);
    if (group)
	list_add_tail(&st->group,&group->stations);
}

static void x11_station_del(char *name)
{
    struct list_head *item;
    struct station   *st;

    list_for_each(item,&x11_stations) {
	st = list_entry(item, struct station, global);
	if (0 == strcmp(st->name,name))
	    goto found;
    }
    return;

 found:
    list_del(&st->global);
    if (NULL != st->group.next)
	list_del(&st->group);

    XtDestroyWidget(st->menu1);
    XtDestroyWidget(st->menu2);
    XtVaSetValues(chan_cont, XmNselectedObjectCount,0, NULL);
    XtUnmanageChild(st->button);
    XtDestroyWidget(st->button);
    free(st->name);
    free(st);
}

/* use more columns until the menu fits onto the screen */
static void
menu_cols_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Dimension height,num;
    int i = 8;

    for (;i;i--) {
	XtVaGetValues(widget,
		      XtNheight,&height,
		      XmNnumColumns,&num,
		      NULL);
	if (height < XtScreen(widget)->height - 100)
	    break;
	XtVaSetValues(widget,XmNnumColumns,num+1,NULL);
    }
}

static void
x11_channel_init(void)
{
    char *list;

    st_menu2 = XmCreatePopupMenu(tv,"stationsM",NULL,0);
    XtAddEventHandler(tv,ButtonPressMask,False,popup_eh,st_menu2);
    XtAddCallback(st_menu2,XmNmapCallback,menu_cols_cb,NULL);

    for (list  = cfg_sections_first("stations");
	 list != NULL;
	 list  = cfg_sections_next("stations",list)) {
	x11_station_add(list);
    }
}

/*----------------------------------------------------------------------*/

static char *station_edit;

static void
station_add_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmString str;

    /* init stuff */
    station_edit = NULL;
    XmTextSetString(prop_name,"");
    XmTextSetString(prop_key,"");
    XmTextSetString(prop_group,"");
    if (curr_channel) {
	str = XmStringGenerate(curr_channel, NULL, XmMULTIBYTE_TEXT, NULL);
	XtVaSetValues(prop_channel,XmNselectedItem,str,NULL);
	XmStringFree(str);
    }

    XtManageChild(prop_dlg);
}

static void
station_edit_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    WidgetList children;
    Cardinal nchildren;
    XmString str;
    char *station,*channel,*vdr;

    /* get station */
    XtVaGetValues(chan_cont,
		  XmNselectedObjects,&children,
		  XmNselectedObjectCount,&nchildren,
		  NULL);
    if (1 != nchildren)
	return;

    /* init stuff */
    station = XtName(children[0]);
    station_edit = station;
    XmTextSetString(prop_name,station);
    XmTextSetString(prop_key,cfg_get_str("stations",station,"key"));
    XmTextSetString(prop_group,cfg_get_str("stations",station,"group"));

    channel = cfg_get_str("stations",station,"channel");
    vdr     = cfg_get_str("stations",station,"vdr");
    if (channel) {
	str = XmStringGenerate(channel, NULL, XmMULTIBYTE_TEXT, NULL);
	XtVaSetValues(prop_channel,XmNselectedItem,str,NULL);
	XmStringFree(str);
    }
    if (vdr) {
	str = XmStringGenerate(vdr, NULL, XmMULTIBYTE_TEXT, NULL);
	XtVaSetValues(prop_vdr,XmNselectedItem,str,NULL);
	XmStringFree(str);
    }

    XtManageChild(prop_dlg);
}

static void
station_delete_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    WidgetList children;
    Cardinal nchildren;
    char *station;
    int i;

    XtVaGetValues(chan_cont,
		  XmNselectedObjects,&children,
		  XmNselectedObjectCount,&nchildren,
		  NULL);
    for (i = 0; i < nchildren; i++) {
	station = XtName(children[i]);
	x11_station_del(station);
	cfg_del_section("stations",station);
    }
    XmContainerRelayout(chan_cont);
}

static void
station_key_eh(Widget widget, XtPointer client_data, XEvent *event, Boolean *cont)
{
    XKeyEvent *ke = (XKeyEvent*)event;
    KeySym sym;
    char *key;
    char line[64];

    sym = XKeycodeToKeysym(dpy,ke->keycode,0);
    if (NoSymbol == sym) {
	fprintf(stderr,"can't translate keycode %d\n",ke->keycode);
	return;
    }
    key = XKeysymToString(sym);

    line[0] = '\0';
    if (ke->state & ShiftMask)   strcpy(line,"Shift+");
    if (ke->state & ControlMask) strcpy(line,"Ctrl+");
    strcat(line,key);
    XmTextSetString(prop_key,line);
}

static void
station_apply_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char *station, *key, *group, *channel, *vdr;
    XmString str;
    Widget msgbox;
    
    station = XmTextGetString(prop_name);
    key     = XmTextGetString(prop_key);
    group   = XmTextGetString(prop_group);
    
    XtVaGetValues(prop_channel,XmNselectedItem,&str,NULL);
    channel = XmStringUnparse(str,NULL,XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
			      NULL,0,0);
    XtVaGetValues(prop_vdr,XmNselectedItem,&str,NULL);
    vdr = XmStringUnparse(str,NULL,XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
			  NULL,0,0);

    if (0 == strlen(station)) {
	msgbox = XmCreateErrorDialog(prop_dlg,"no_name",NULL,0);
	XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_HELP_BUTTON));
	XtUnmanageChild(XmMessageBoxGetChild(msgbox,XmDIALOG_CANCEL_BUTTON));
	XtAddCallback(msgbox,XmNokCallback,destroy_cb,msgbox);
	XtManageChild(msgbox);
	return;
    }

    if (station_edit) {
	/* del old */
	x11_station_del(station_edit);
	if (0 != strcmp(station_edit,station))
	    cfg_del_section("stations",station_edit);
    }

    /* add new */
    fprintf(stderr,"add: \"%s\" ch=\"%s\" key=\"%s\" group=\"%s\"\n",
	    station,channel,key,group);
    if (strlen(channel) > 0)
	cfg_set_str("stations",station,"channel",channel);
    if (strlen(vdr) > 0)
	cfg_set_str("stations",station,"vdr",vdr);
    if (strlen(key) > 0)
	cfg_set_str("stations",station,"key",key);
    if (strlen(group) > 0)
	cfg_set_str("stations",station,"group",group);
    x11_station_add(station);
    XmContainerRelayout(chan_cont);
}

static void
station_tune_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmComboBoxCallbackStruct *cb = call_data;
    char *line;

    line = XmStringUnparse(cb->item_or_text,NULL,
			   XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
			   NULL,0,0);
    do_va_cmd(2,"setchannel",line);
}

static void
create_station_prop(void)
{
    Widget label,rowcol;

    prop_dlg = XmCreatePromptDialog(control_shell,"prop",NULL,0);
    XmdRegisterEditres(XtParent(prop_dlg));
    XtUnmanageChild(XmSelectionBoxGetChild(prop_dlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(prop_dlg,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(prop_dlg,XmDIALOG_TEXT));

    rowcol = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, prop_dlg,
				     NULL);
    label = XtVaCreateManagedWidget("nameL", xmLabelWidgetClass, rowcol,
				    NULL);
    prop_name = XtVaCreateManagedWidget("name", xmTextWidgetClass, rowcol,
					NULL);

    label = XtVaCreateManagedWidget("keyL", xmLabelWidgetClass, rowcol,
				    NULL);
    prop_key = XtVaCreateManagedWidget("key", xmTextWidgetClass, rowcol,
				       NULL);
    XtAddEventHandler(prop_key, KeyPressMask, False, station_key_eh, NULL);

    label = XtVaCreateManagedWidget("groupL", xmLabelWidgetClass, rowcol,
				    NULL);
    prop_group = XtVaCreateManagedWidget("group", xmTextWidgetClass, rowcol,
					 NULL);

    prop_channelL = XtVaCreateManagedWidget("channelL", xmLabelWidgetClass,
					    rowcol,
					    XmNsensitive, False,
					    NULL);
    prop_channel = XtVaCreateManagedWidget("channel",xmComboBoxWidgetClass,
					   rowcol,
#if 0 /* FIXME, set to "True" later seems not to work ... */
					   XmNsensitive, False,
#endif
					   NULL);
    XtAddCallback(prop_channel,XmNselectionCallback, station_tune_cb, NULL);

    prop_vdrL = XtVaCreateManagedWidget("vdrL", xmLabelWidgetClass,
					rowcol,
					NULL);
    prop_vdr = XtVaCreateManagedWidget("vdr",xmComboBoxWidgetClass,
				       rowcol,
				       NULL);
    XtAddCallback(prop_channel,XmNselectionCallback, station_tune_cb, NULL);

    XtAddCallback(prop_dlg,XmNokCallback, station_apply_cb, NULL);

    if (0 != cfg_sections_count("dvb")) {
	/* fill vdr list */
	XmStringTable tab;
	char *vdr;
	int i;

	i = 0;
	tab = malloc(cfg_sections_count("dvb")*sizeof(*tab));
	cfg_sections_for_each("dvb",vdr)
	    tab[i++] = XmStringGenerate(vdr, NULL, XmMULTIBYTE_TEXT, NULL);

	XtVaSetValues(prop_vdr,
		      XmNitemCount,i,
		      XmNitems,tab,
		      XmNselectedItem,tab[0],
		      NULL);
	while (i > 0)
	    XmStringFree(tab[--i]);
	free(tab);
    } else {
	XtVaSetValues(prop_vdrL, XmNsensitive,False, NULL);
	XtVaSetValues(prop_vdr,  XmNsensitive,False, NULL);
    }
}

/*----------------------------------------------------------------------*/

static void
container_resize_eh(Widget widget, XtPointer clientdata, XEvent *event, Boolean *d)
{
    Widget clip,scroll,container;
    Dimension width,height,ch;

    clip = widget;
    scroll = XtParent(widget);
    XtVaGetValues(scroll,XmNworkWindow,&container,NULL);

    XtVaGetValues(clip,
		  XtNwidth,&width,
		  XtNheight,&height,
		  NULL);
    XtVaGetValues(container,
		  XtNheight, &ch,
		  NULL);
    XtVaSetValues(container, XtNwidth,width, NULL);
#if 1
    /* hmm ... */
    if (ch < height)
	XtVaSetValues(container, XtNheight,height, NULL);
#endif

    XmContainerRelayout(container);
}

static void
container_spatial_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;

    XtVaSetValues(container,
		  XmNlayoutType,    XmSPATIAL,
		  XmNentryViewType, XmLARGE_ICON,
		  NULL);
    XmContainerRelayout(container);
}

static void
container_detail_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget container = clientdata;

    XtVaSetValues(container,
		  XmNlayoutType,    XmDETAIL,
		  XmNentryViewType, XmSMALL_ICON,
		  NULL);
    XmContainerRelayout(container);
}

static void
container_activate_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmContainerSelectCallbackStruct *cd = call_data;
    char *name;

    if (XmCR_DEFAULT_ACTION  == cd->reason &&
	1 == cd->selected_item_count) {
	name = XtName(cd->selected_items[0]);
	do_va_cmd(2, "setstation", name);
    }
}

static void
container_traverse_cb(Widget scroll, XtPointer clientdata, XtPointer call_data)
{
    XmTraverseObscuredCallbackStruct *cd = call_data;

    if (cd->reason == XmCR_OBSCURED_TRAVERSAL)
	XmScrollVisible(scroll, cd->traversal_destination, 25, 25);
}

static void
create_control(void)
{
    Widget form,menubar,menu,smenu,push,clip,tool,rc,fr;
    char *list;
#if 0
    char action[256];
#endif
    
    control_shell = XtVaAppCreateShell("control","MoTV4",
				       topLevelShellWidgetClass,
				       dpy,
				       XtNclientLeader,app_shell,
				       XtNvisual,vinfo.visual,
				       XtNcolormap,colormap,
				       XtNdepth,vinfo.depth,
				       XmNdeleteResponse,XmDO_NOTHING,
				       NULL);
    XmdRegisterEditres(control_shell);
    XmAddWMProtocolCallback(control_shell,WM_DELETE_WINDOW,
			    popupdown_cb,control_shell);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, control_shell,
				   NULL);

    /* menbar */
    menubar = XmCreateMenuBar(form,"menubar",NULL,0);
    XtManageChild(menubar);

    tool = XtVaCreateManagedWidget("tool",xmRowColumnWidgetClass,form,NULL);

    /* status line */
    rc = XtVaCreateManagedWidget("status", xmRowColumnWidgetClass, form,
				 NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_freq = XtVaCreateManagedWidget("freq", xmLabelWidgetClass, fr, NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_chan = XtVaCreateManagedWidget("chan", xmLabelWidgetClass, fr, NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_name = XtVaCreateManagedWidget("name", xmLabelWidgetClass, fr, NULL);
    fr = XtVaCreateManagedWidget("f", xmFrameWidgetClass, rc, NULL);
    st_key = XtVaCreateManagedWidget("key", xmLabelWidgetClass, fr, NULL);

    /* channel buttons */
    chan_viewport = XmCreateScrolledWindow(form,"scroll",NULL,0);
    XtManageChild(chan_viewport);
    chan_cont = XmCreateContainer(chan_viewport, "cont", NULL, 0);
    XtManageChild(chan_cont);

    XtAddCallback(chan_viewport, XmNtraverseObscuredCallback,
		  container_traverse_cb, NULL);
    XtAddCallback(chan_cont, XmNdefaultActionCallback,
		  container_activate_cb, NULL);

    XtVaGetValues(chan_viewport,XmNclipWindow,&clip,NULL);
    XtAddEventHandler(clip,StructureNotifyMask,True,container_resize_eh,NULL);
    
    /* menu - file */
    menu = XmCreatePulldownMenu(menubar,"fileM",NULL,0);
    XtVaCreateManagedWidget("file",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("rec",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,popupdown_cb,str_shell);
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("quit",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,ExitCB,NULL);

    /* menu - edit */
    menu = XmCreatePulldownMenu(menubar,"editM",NULL,0);
    XtVaCreateManagedWidget("edit",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    push = XtVaCreateManagedWidget("st_add",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,station_add_cb,NULL);
    push = XtVaCreateManagedWidget("st_edit",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,station_edit_cb,NULL);
    push = XtVaCreateManagedWidget("st_del",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,station_delete_cb,NULL);
    push = XtVaCreateManagedWidget("st_save",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,config_save_cb,"stations");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("copy",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Ipc(clipboard)");

    /* menu - view */
    menu = XmCreatePulldownMenu(menubar,"viewM",NULL,0);
    XtVaCreateManagedWidget("view",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);

    push = XtVaCreateManagedWidget("spatial",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,
		  container_spatial_cb,chan_cont);
    push = XtVaCreateManagedWidget("details",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,
		  container_detail_cb,chan_cont);

    /* menu - tv stations */
    st_menu1 = XmCreatePulldownMenu(menubar,"stationsM",NULL,0);
    XtVaCreateManagedWidget("stations",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,st_menu1,NULL);
    XtAddCallback(st_menu1,XmNmapCallback,menu_cols_cb,NULL);
    
    /* menu - tools (name?) */
    menu = XmCreatePulldownMenu(menubar,"toolsM",NULL,0);
    XtVaCreateManagedWidget("tools",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
    w_full = XtVaCreateManagedWidget("full",xmToggleButtonWidgetClass,menu,
				     NULL);
    XtAddCallback(w_full,XmNvalueChangedCallback,action_cb,
		  "Command(fullscreen)");
    push = XtVaCreateManagedWidget("ontop",xmToggleButtonWidgetClass,menu,
				   NULL);
    b_ontop = push;
    XtAddCallback(push,XmNvalueChangedCallback,ontop_cb,NULL);
    push = XtVaCreateManagedWidget("levels",xmPushButtonWidgetClass,menu,
				   NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Popup(levels)");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("st_up",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,next)");
    push = XtVaCreateManagedWidget("st_dn",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,prev)");

    /* menu - tools / tuner */
    smenu = XmCreatePulldownMenu(menu,"tuneM",NULL,0);
    XtVaCreateManagedWidget("tune",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("ch_up",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,next)");
    push = XtVaCreateManagedWidget("ch_dn",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,prev)");
    push = XtVaCreateManagedWidget("fi_up",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,fine_up)");
    push = XtVaCreateManagedWidget("fi_dn",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setchannel,fine_down)");

    /* menu - tools / capture */
    smenu = XmCreatePulldownMenu(menu,"grabM",NULL,0);
    XtVaCreateManagedWidget("grab",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("ppm_f",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,ppm,full)");
    push = XtVaCreateManagedWidget("ppm_w",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,ppm,win)");
    push = XtVaCreateManagedWidget("jpg_f",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,jpeg,full)");
    push = XtVaCreateManagedWidget("jpg_w",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,jpeg,win)");
    
    /* menu - tools / aspect ratio */
    smenu = XmCreatePulldownMenu(menu,"ratioM",NULL,0);
    XtVaCreateManagedWidget("ratio",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("r_no",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Ratio(0,0)");
    push = XtVaCreateManagedWidget("r_43",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Ratio(4,3)");

    /* menu - tools / launch */
    launch_menu = XmCreatePulldownMenu(menu,"launchM",NULL,0);
    XtVaCreateManagedWidget("launch",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,launch_menu,NULL);

#ifdef HAVE_ZVBI
    /* menu - tools / subtitles */
    smenu = XmCreatePulldownMenu(menu,"subM",NULL,0);
    XtVaCreateManagedWidget("sub",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    push = XtVaCreateManagedWidget("s_off",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(stop)");
    push = XtVaCreateManagedWidget("s_150",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,150)");
    push = XtVaCreateManagedWidget("s_333",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,333)");
    push = XtVaCreateManagedWidget("s_777",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,777)");
    push = XtVaCreateManagedWidget("s_801",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,801)");
    push = XtVaCreateManagedWidget("s_888",xmPushButtonWidgetClass,smenu,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,"Vtx(start,888)");
#endif

    /* menu - internal options */
    opt_menu = menu = XmCreatePulldownMenu(menubar,"optionsM",NULL,0);
    XtVaCreateManagedWidget("options",xmCascadeButtonWidgetClass,menubar,
			    XmNsubMenuId,menu,NULL);
#ifdef HAVE_ZVBI
    if (0 /* FIXME */) {
	push = XtVaCreateManagedWidget("scan",xmPushButtonWidgetClass,menu,NULL);
	XtAddCallback(push,XmNactivateCallback,chscan_cb,NULL);
    }
#endif
    push = XtVaCreateManagedWidget("pref",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,pref_manage_cb,NULL);
    push = XtVaCreateManagedWidget("opt_save",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,config_save_cb,"options");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);

    cap_menu = XmCreatePulldownMenu(menu,"captureM",NULL,0);
    XtVaCreateManagedWidget("capture",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,cap_menu,NULL);
    push = XtVaCreateManagedWidget("overlay",xmToggleButtonWidgetClass,
				   cap_menu,XmNindicatorType,XmONE_OF_MANY,
				   NULL);
    add_cmd_callback(push,XmNvalueChangedCallback,"capture","overlay",NULL);
    push = XtVaCreateManagedWidget("grabdisplay",xmToggleButtonWidgetClass,
				   cap_menu,XmNindicatorType,XmONE_OF_MANY,
				   NULL);
    add_cmd_callback(push,XmNvalueChangedCallback,"capture","grab",NULL);
    push = XtVaCreateManagedWidget("none",xmToggleButtonWidgetClass,
				   cap_menu,XmNindicatorType,XmONE_OF_MANY,
				   NULL);
    add_cmd_callback(push,XmNvalueChangedCallback,"capture","off",NULL);
    
    freq_menu = XmCreatePulldownMenu(menu,"freqM",NULL,0);
    XtVaCreateManagedWidget("freq",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,freq_menu,NULL);
    for (list = cfg_sections_first("freqtabs");
	 NULL != list;
	 list = cfg_sections_next("freqtabs",list)) {
	push = XtVaCreateManagedWidget(list,
				       xmToggleButtonWidgetClass,freq_menu,
				       XmNindicatorType,XmONE_OF_MANY,
				       NULL);
	add_cmd_callback(push,XmNvalueChangedCallback,
			 "setfreqtab", list, NULL);
    }

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    smenu = XmCreatePulldownMenu(menu,"devicesM",NULL,0);
    XtVaCreateManagedWidget("devices",xmCascadeButtonWidgetClass,menu,
			    XmNsubMenuId,smenu,NULL);
    for (list = cfg_sections_first("devs");
	 NULL != list;
	 list = cfg_sections_next("devs",list)) {
	push = XtVaCreateManagedWidget(list,
				       xmToggleButtonWidgetClass,smenu,
				       XmNindicatorType,XmONE_OF_MANY,
				       NULL);
	XtAddCallback(push,XmNvalueChangedCallback,pick_device_cb,NULL);
    }
    push = XtVaCreateManagedWidget("attrs",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,popupdown_cb,attr_shell);

    /* menu - filter */
#if 0
    if ((f_drv & CAN_CAPTURE)  &&  !list_empty(&ng_filters))  {
	struct list_head *item;
	struct ng_filter *filter;
	
	menu = XmCreatePulldownMenu(menubar,"filterM",NULL,0);
	XtVaCreateManagedWidget("filter",xmCascadeButtonWidgetClass,menubar,
				XmNsubMenuId,menu,NULL);
	push = XtVaCreateManagedWidget("fnone",
				       xmPushButtonWidgetClass,menu,
				       NULL);
	XtAddCallback(push,XmNactivateCallback,action_cb,"Filter()");
	list_for_each(item,&ng_filters) {
	    filter = list_entry(item, struct ng_filter, list);
	    push = XtVaCreateManagedWidget(filter->name,
					   xmPushButtonWidgetClass,menu,
					   NULL);
	    sprintf(action,"Filter(%s)",filter->name);
	    XtAddCallback(push,XmNactivateCallback,action_cb,strdup(action));
	}
	XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
	push = XtVaCreateManagedWidget("fopts",xmPushButtonWidgetClass,menu,
				       NULL);
	XtAddCallback(push,XmNactivateCallback,action_cb,"Popup(filter)");
    }
#endif

    /* menu - help */
    menu = XmCreatePulldownMenu(menubar,"helpM",NULL,0);
    push = XtVaCreateManagedWidget("help",xmCascadeButtonWidgetClass,menubar,
				   XmNsubMenuId,menu,NULL);
    XtVaSetValues(menubar,XmNmenuHelpWidget,push,NULL);
    push = XtVaCreateManagedWidget("man",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,man_cb,"motv");
    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,menu,NULL);
    push = XtVaCreateManagedWidget("about",xmPushButtonWidgetClass,menu,NULL);
    XtAddCallback(push,XmNactivateCallback,about_cb,NULL);

    /* toolbar */
    push = XtVaCreateManagedWidget("prev",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,prev)");
    push = XtVaCreateManagedWidget("next",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(setstation,next)");

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,tool,NULL);
    push = XtVaCreateManagedWidget("snap",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(snap,jpeg,full)");
    push = XtVaCreateManagedWidget("movie",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,popupdown_cb,str_shell);
    push = XtVaCreateManagedWidget("mute",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,action_cb,
		  "Command(volume,mute,toggle)");

    XtVaCreateManagedWidget("sep",xmSeparatorWidgetClass,tool,NULL);
    push = XtVaCreateManagedWidget("exit",xmPushButtonWidgetClass,tool,NULL);
    XtAddCallback(push,XmNactivateCallback,ExitCB,NULL);

#if 0
    container_detail_cb(NULL, chan_cont, NULL);
    container_resize_eh(clip, NULL, NULL, NULL);
#endif
}

/*----------------------------------------------------------------------*/

#if 0
void create_chanwin(void)
{
    Widget form,clip,menu,push;
    
}
#endif

/*----------------------------------------------------------------------*/

#if 0
static void
do_capture(int from, int to, int tmp_switch)
{
    static int niced = 0;
    WidgetList children;

    /* off */
    switch (from) {
    case CAPTURE_GRABDISPLAY:
//	video_gd_stop();
	XClearArea(XtDisplay(tv), XtWindow(tv), 0,0,0,0, True);
	break;
    case CAPTURE_OVERLAY:
	video_overlay(0);
	break;
    }

    /* on */
    switch (to) {
    case CAPTURE_GRABDISPLAY:
	if (!niced)
	    nice(niced = 10);
//	video_gd_start();
	break;
    case CAPTURE_OVERLAY:
	video_overlay(1);
	break;
    }

    /* update menu */
    XtVaGetValues(cap_menu,XtNchildren,&children,NULL);
    XmToggleButtonSetState(children[0],to == CAPTURE_OVERLAY,    False);
    XmToggleButtonSetState(children[1],to == CAPTURE_GRABDISPLAY,False);
    XmToggleButtonSetState(children[2],to == CAPTURE_OFF,        False);
}
#endif

static void
do_motif_fullscreen(void)
{
    XmToggleButtonSetState(w_full,!fs,False);
    do_fullscreen();
}

/*----------------------------------------------------------------------*/

struct FILE_DATA {
    Widget filebox;
    Widget text;
    Widget push;
};

static void
file_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmFileSelectionBoxCallbackStruct *cb = call_data;
    struct FILE_DATA *h = clientdata;
    char *line;

    if (cb->reason == XmCR_OK) {
	line = XmStringUnparse(cb->value,NULL,
			       XmMULTIBYTE_TEXT,XmMULTIBYTE_TEXT,
			       NULL,0,0);
	XmTextSetString(h->text,line);
    }
    XtUnmanageChild(h->filebox);
}

static void
file_browse_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct FILE_DATA *h = clientdata;
    Widget help;
    /* XmString str; */

    if (NULL == h->filebox) {
	h->filebox = XmCreateFileSelectionDialog(h->push,"filebox",NULL,0);
	help = XmFileSelectionBoxGetChild(h->filebox,XmDIALOG_HELP_BUTTON);
	XtUnmanageChild(help);
	XtAddCallback(h->filebox,XmNokCallback,file_done_cb,h);
	XtAddCallback(h->filebox,XmNcancelCallback,file_done_cb,h);
    }
#if 0
    str = XmStringGenerate(XmTextGetString(h->text),
			   NULL, XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(h->filebox,XmNdirMask,str,NULL);
    XmStringFree(str);
#endif
    XtManageChild(h->filebox);
}

static void
exec_player_cb(Widget widget, XtPointer client_data, XtPointer calldata)
{
    char *filename;

    filename = XmTextGetString(m_fvideo);
    exec_player(filename);
}

static void
create_strwin(void)
{
    Widget form,push,rowcol,frame,fbox;
    struct FILE_DATA *h;
    Arg args[2];
    
    str_shell = XtVaAppCreateShell("streamer", "MoTV4",
				   topLevelShellWidgetClass,
				   dpy,
				   XtNclientLeader,app_shell,
				   XtNvisual,vinfo.visual,
				   XtNcolormap,colormap,
				   XtNdepth,vinfo.depth,
				   XmNdeleteResponse,XmDO_NOTHING,
				   NULL);
    XmdRegisterEditres(str_shell);
    XmAddWMProtocolCallback(str_shell,WM_DELETE_WINDOW,
			    popupdown_cb,str_shell);
    form = XtVaCreateManagedWidget("form", xmFormWidgetClass, str_shell,
				   NULL);

    /* driver */
    frame = XtVaCreateManagedWidget("driverF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("driverL",xmLabelWidgetClass,frame,NULL);
    driver_menu = XmCreatePulldownMenu(form,"driverM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,driver_menu);
    driver_option = XmCreateOptionMenu(frame,"driver",args,1);
    XtManageChild(driver_option);

    /* video format + frame rate */
    frame = XtVaCreateManagedWidget("videoF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("videoL",xmLabelWidgetClass,frame,NULL);
    rowcol = XtVaCreateManagedWidget("videoB",xmRowColumnWidgetClass,
				     frame,NULL);
    video_menu = XmCreatePulldownMenu(rowcol,"videoM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,video_menu);
    video_option = XmCreateOptionMenu(rowcol,"video",args,1);
    XtManageChild(video_option);
    XtVaCreateManagedWidget("fpsL",xmLabelWidgetClass,rowcol,NULL);
    m_fps = XtVaCreateManagedWidget("fps",xmComboBoxWidgetClass,rowcol,NULL);
    
    /* audio format + sample rate */
    frame = XtVaCreateManagedWidget("audioF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("audioL",xmLabelWidgetClass,frame,NULL);
    rowcol = XtVaCreateManagedWidget("audioB",xmRowColumnWidgetClass,
				     frame,NULL);
    audio_menu = XmCreatePulldownMenu(rowcol,"audioM",NULL,0);
    XtSetArg(args[0],XmNsubMenuId,audio_menu);
    audio_option = XmCreateOptionMenu(rowcol,"audio",args,1);
    XtManageChild(audio_option);
    XtVaCreateManagedWidget("rateL",xmLabelWidgetClass,rowcol,NULL);
    m_rate = XtVaCreateManagedWidget("rate",xmComboBoxWidgetClass,rowcol,NULL);

    /* filenames */
    frame = XtVaCreateManagedWidget("fileF", xmFrameWidgetClass, form, NULL);
    XtVaCreateManagedWidget("fileL",xmLabelWidgetClass,frame,NULL);
    fbox = XtVaCreateManagedWidget("fbox",xmRowColumnWidgetClass,
				   frame,NULL);

    rowcol = XtVaCreateManagedWidget("fvideoB",xmRowColumnWidgetClass,
				     fbox,NULL);
    XtVaCreateManagedWidget("fvideoL",xmLabelWidgetClass,rowcol,NULL);
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->text = XtVaCreateManagedWidget("fvideo",xmTextWidgetClass,
				      rowcol,NULL);
    m_fvideo = h->text;
    h->push = XtVaCreateManagedWidget("files",xmPushButtonWidgetClass,rowcol,
				      NULL);
    XtAddCallback(h->push,XmNactivateCallback,file_browse_cb,h);

    rowcol = XtVaCreateManagedWidget("faudioB",xmRowColumnWidgetClass,
				     fbox,NULL);
    m_faudioL = XtVaCreateManagedWidget("faudioL",xmLabelWidgetClass,rowcol,
					NULL);
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->text = XtVaCreateManagedWidget("faudio",xmTextWidgetClass,rowcol,
				      NULL);
    m_faudio = h->text;
    h->push = XtVaCreateManagedWidget("files",xmPushButtonWidgetClass,rowcol,
				      NULL);
    m_faudioB = h->push;
    XtAddCallback(h->push,XmNactivateCallback,file_browse_cb,h);

    /* seperator, buttons */
    m_status = XtVaCreateManagedWidget("status",xmLabelWidgetClass,form,NULL);
    rowcol = XtVaCreateManagedWidget("buttons",xmRowColumnWidgetClass,form,
				   NULL);
    push = XtVaCreateManagedWidget("rec", xmPushButtonWidgetClass, rowcol,
				   NULL);
    add_cmd_callback(push,XmNactivateCallback, "movie","start",NULL);
    push = XtVaCreateManagedWidget("stop", xmPushButtonWidgetClass, rowcol,
				   NULL);
    add_cmd_callback(push,XmNactivateCallback, "movie","stop",NULL);
    push = XtVaCreateManagedWidget("play", xmPushButtonWidgetClass, rowcol,
				   NULL);
    XtAddCallback(push,XmNactivateCallback,exec_player_cb,NULL);
    push = XtVaCreateManagedWidget("cancel", xmPushButtonWidgetClass, rowcol,
				   NULL);
    XtAddCallback(push,XmNactivateCallback, popupdown_cb, str_shell);
}

static void
update_movie_menus(void)
{
    struct list_head *item;
    struct ng_writer *writer;
    static int first = 1;
    Widget push;
    XmString str;
    Boolean sensitive;
    int i;
    
    char *driver = cfg_get_str(O_REC_DRIVER);
    char *video  = cfg_get_str(O_REC_VIDEO);
    char *audio  = cfg_get_str(O_REC_AUDIO);

    /* drivers  */
    if (first) {
	first = 0;
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    str = XmStringGenerate((char*)writer->desc,
				   NULL, XmMULTIBYTE_TEXT, NULL);
	    push = XtVaCreateManagedWidget(writer->name,
					   xmPushButtonWidgetClass,driver_menu,
					   XmNlabelString,str,
					   NULL);
	    XmStringFree(str);
	    add_cmd_callback(push,XmNactivateCallback,
			     "movie","driver",writer->name);
	    if (NULL == movie_driver ||
		(NULL != driver && 0 == strcasecmp(driver,writer->name))) {
		movie_driver = writer;
		i_movie_driver = i;
		XtVaSetValues(driver_option,XmNmenuHistory,push,NULL);
	    }
	    i++;
	}
    }

    /* audio formats */
    delete_children(audio_menu);
    for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	str = XmStringGenerate
	    ((char*)(movie_driver->audio[i].desc ?
		     movie_driver->audio[i].desc : 
		     ng_afmt_to_desc[movie_driver->audio[i].fmtid]),
	     NULL, XmMULTIBYTE_TEXT, NULL);
	push = XtVaCreateManagedWidget(movie_driver->audio[i].name,
				       xmPushButtonWidgetClass,audio_menu,
				       XmNlabelString,str,
				       NULL);
	XmStringFree(str);
	add_cmd_callback(push,XmNactivateCallback,
			 "movie","audio",movie_driver->audio[i].name);
	if (NULL != audio)
	    if (0 == strcasecmp(audio,movie_driver->audio[i].name)) {
		XtVaSetValues(audio_option,XmNmenuHistory,push,NULL);
		movie_audio = i;
	    }
    }
    str = XmStringGenerate("no sound", NULL, XmMULTIBYTE_TEXT, NULL);
    push = XtVaCreateManagedWidget("none",xmPushButtonWidgetClass,audio_menu,
				   XmNlabelString,str,NULL);
    XmStringFree(str);
    add_cmd_callback(push,XmNactivateCallback, "movie","audio","none");

    /* video formats */
    delete_children(video_menu);
    for (i = 0; NULL != movie_driver->video[i].name; i++) {
	str = XmStringGenerate
	    ((char*)(movie_driver->video[i].desc ?
		     movie_driver->video[i].desc : 
		     ng_vfmt_to_desc[movie_driver->video[i].fmtid]),
	     NULL, XmMULTIBYTE_TEXT, NULL);
	push = XtVaCreateManagedWidget(movie_driver->video[i].name,
				       xmPushButtonWidgetClass,video_menu,
				       XmNlabelString,str,
				       NULL);
	XmStringFree(str);
	add_cmd_callback(push,XmNactivateCallback,
			 "movie","video",movie_driver->video[i].name);
	if (NULL != video)
	    if (0 == strcasecmp(video,movie_driver->video[i].name)) {
		XtVaSetValues(video_option,XmNmenuHistory,push,NULL);
		movie_video = i;
	    }
    }

    /* need audio filename? */
    sensitive = movie_driver->combined ? False : True;
    XtVaSetValues(m_faudio,  XtNsensitive,sensitive, NULL);
    XtVaSetValues(m_faudioL, XtNsensitive,sensitive, NULL);
    XtVaSetValues(m_faudioB, XtNsensitive,sensitive, NULL);
}

static void
init_movie_menus(void)
{
    update_movie_menus();

#if 0
    if (mov_rate)
	do_va_cmd(3,"movie","rate",mov_rate);
    if (mov_fps)
	do_va_cmd(3,"movie","fps",mov_fps);
#endif
}

static void
do_movie_record(int argc, char **argv)
{
    char *fvideo,*faudio;
    struct ng_video_fmt video;
    struct ng_audio_fmt audio;
    const struct ng_writer *wr;
    WidgetList children;
    Cardinal nchildren;
    Widget text;
    int i,rate,fps;

    /* set parameters */
    if (argc > 1 && 0 == strcasecmp(argv[0],"driver")) {
	struct list_head *item;
	struct ng_writer *writer;

	if (debug)
	    fprintf(stderr,"set driver: %s\n",argv[1]);
	XtVaGetValues(driver_menu,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	i = 0;
	list_for_each(item,&ng_writers) {
	    writer = list_entry(item, struct ng_writer, list);
	    if (0 == strcasecmp(argv[1],writer->name)) {
		movie_driver = writer;
		i_movie_driver = i;
	    }
	    i++;
	}
	update_movie_menus();
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"audio")) {
	if (debug)
	    fprintf(stderr,"set audio: %s\n",argv[1]);
	XtVaGetValues(audio_menu,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	for (i = 0; NULL != movie_driver->audio[i].name; i++) {
	    if (0 == strcasecmp(argv[1],movie_driver->audio[i].name)) {
		XtVaSetValues(audio_option,XmNmenuHistory,children[i],NULL);
		movie_audio = i;
	    }
	}
	if (0 == strcmp(argv[1],"none")) {
	    XtVaSetValues(audio_option,XmNmenuHistory,children[i],NULL);
	    movie_audio = i;
	}
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"video")) {
	if (debug)
	    fprintf(stderr,"set video: %s\n",argv[1]);
	XtVaGetValues(video_menu,XtNchildren,&children,
		      XtNnumChildren,&nchildren,NULL);
	for (i = 0; NULL != movie_driver->video[i].name; i++) {
	    if (0 == strcasecmp(argv[1],movie_driver->video[i].name)) {
		XtVaSetValues(video_option,XmNmenuHistory,children[i],NULL);
		movie_video = i;
	    }
	}
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"rate")) {
	XtVaGetValues(m_rate,XmNtextField,&text,NULL);
	XmTextSetString(text,argv[1]);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fps")) {
	XtVaGetValues(m_fps,XmNtextField,&text,NULL);
	XmTextSetString(text,argv[1]);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"fvideo")) {
	XmTextSetString(m_fvideo,argv[1]);
    }
    if (argc > 1 && 0 == strcasecmp(argv[0],"faudio")) {
	XmTextSetString(m_faudio,argv[1]);
    }

    /* start */
    if (argc > 0 && 0 == strcasecmp(argv[0],"start")) {
	if (0 != cur_movie)
	    return; /* records already */
	cur_movie = 1;
//	movie_blit = (cur_capture == CAPTURE_GRABDISPLAY);
//	video_gd_suspend();
	XmToggleButtonSetState(levels_toggle,0,True);
	
	fvideo = XmTextGetString(m_fvideo);
	faudio = XmTextGetString(m_faudio);
	fvideo = tilde_expand(fvideo);
	faudio = tilde_expand(faudio);

	XtVaGetValues(m_rate,XmNtextField,&text,NULL);
	rate = atoi(XmTextGetString(text));
	XtVaGetValues(m_fps,XmNtextField,&text,NULL);
	fps = (int)(atof(XmTextGetString(text))*1000);
	
	memset(&video,0,sizeof(video));
	memset(&audio,0,sizeof(audio));

	wr = movie_driver;
	video.fmtid  = wr->video[movie_video].fmtid;
	video.width  = cur_tv_width;
	video.height = cur_tv_height;
	audio.fmtid  = wr->audio[movie_audio].fmtid;
	audio.rate   = rate;
	
	movie_state = movie_writer_init
	    (fvideo, faudio, wr,
	     &devs.video, &devs.sndrec,
	     &video, wr->video[movie_video].priv,
	     &audio, wr->audio[movie_audio].priv,
	     fps,
	     GET_REC_BUFCOUNT(),
	     GET_REC_THREADS());
	if (NULL == movie_state) {
	    /* init failed */
//	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    toolkit_set_label(m_status, "error [init]");
	    return;
	}
	if (0 != movie_writer_start(movie_state)) {
	    /* start failed */
	    movie_writer_stop(movie_state);
//	    video_gd_restart();
	    cur_movie = 0;
	    /* hmm, not the most elegant way to flag an error ... */
	    toolkit_set_label(m_status, "error [start]");
	    return;
	}
	rec_work_id  = XtAppAddWorkProc(app_context,rec_work,NULL);
	toolkit_set_label(m_status, "recording");
	return;
    }
    
    /* stop */
    if (argc > 0 && 0 == strcasecmp(argv[0],"stop")) {
	if (0 == cur_movie)
	    return; /* nothing to stop here */

	movie_writer_stop(movie_state);
	XtRemoveWorkProc(rec_work_id);
	rec_work_id = 0;
//	video_gd_restart();
	cur_movie = 0;
	return;
    }
}

static void
do_rec_status(char *message)
{
    toolkit_set_label(m_status, message);
}

/*----------------------------------------------------------------------*/

#ifdef HAVE_ZVBI
#if 0
#define CHSCAN 250
static int chscan;
static int chvbi;
#endif
static XtIntervalId chtimer;
static Widget chdlg,chscale;

#if 0
static void
chscan_timeout(XtPointer client_data, XtIntervalId *id)
{
    struct CHANNEL *c;
    char title[32];
    XmString xmstr;

    if (!x11_vbi_tuned()) {
	if (debug)
	    fprintf(stderr,"scan [%s]: no station\n",chanlist[chscan].name);
	goto next_station;
    }

    if (0 != x11_vbi_station[0]) {
	if (debug)
	    fprintf(stderr,"scan [%s]: %s\n",chanlist[chscan].name,
		    x11_vbi_station);
	c = add_channel(x11_vbi_station);
	c->cname   = strdup(chanlist[chscan].name);
	c->channel = chscan;
	cur_sender = count-1;
	channel_menu();
	goto next_station;
    }

    if (chvbi++ > 3) {
	if (debug)
	    fprintf(stderr,"scan [%s]: no vbi name\n",chanlist[chscan].name);
	sprintf(title,"%s [no name]",chanlist[chscan].name);
	c = add_channel(title);
	c->cname   = strdup(chanlist[chscan].name);
	c->channel = chscan;
	cur_sender = count-1;
	channel_menu();
	goto next_station;
    }

    if (debug)
	fprintf(stderr,"scan [%s] vbi ...\n",chanlist[chscan].name);
    chtimer = XtAppAddTimeOut(app_context, CHSCAN, chscan_timeout, NULL);
    return;

 next_station:
    chscan++;
    if (chscan >= chancount) {
	/* all done */
	x11_vbi_stop();
	chtimer = 0;
	if (count)
	    do_va_cmd(2,"setchannel",channels[0]->name);
	XtDestroyWidget(chdlg);
	chdlg = NULL;
	return;
    }
    chvbi  = 0;
    if (channel_switch_hook)
	channel_switch_hook();
    xmstr = XmStringGenerate(chanlist[chscan].name, NULL,
			     XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(chscale,XmNtitleString,xmstr,NULL);
    XmStringFree(xmstr);
    XmScaleSetValue(chscale,chscan);
    do_va_cmd(2,"setchannel",chanlist[chscan]);
    x11_vbi_station[0] = 0;
    chtimer = XtAppAddTimeOut(app_context, CHSCAN, chscan_timeout,NULL);
    return;
}
#endif

static void
chscan_start_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
#if 0
    XmString xmstr;

    /* check */
    if (!(f_drv & CAN_TUNE))
	return;
    if (channel_switch_hook)
	channel_switch_hook();
    
    /* clear */
    while (count) {
	XtDestroyWidget(channels[count-1]->button);
	del_channel(count-1);
    }
    cur_sender = -1;
    channel_menu();
    
    x11_vbi_start(args.vbidev);
    chscan = 0;
    chvbi  = 0;
    xmstr = XmStringGenerate(chanlist[chscan].name, NULL,
			     XmMULTIBYTE_TEXT, NULL);
    XtVaSetValues(chscale,XmNtitleString,xmstr,XmNmaximum,chancount,NULL);
    XmStringFree(xmstr);
    XmScaleSetValue(chscale,chscan);
    do_va_cmd(2,"setchannel",chanlist[chscan]);
    x11_vbi_station[0] = 0;
    chtimer = XtAppAddTimeOut(app_context, CHSCAN, chscan_timeout,NULL);
#endif
}
    
static void
chscan_cancel_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    x11_vbi_stop();
    if (chtimer)
	XtRemoveTimeOut(chtimer);
    chtimer = 0;
    XtDestroyWidget(chdlg);
    chdlg = NULL;
}

static void
chscan_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    Widget rc;
    
    chdlg = XmCreatePromptDialog(control_shell,"chscan",NULL,0);
    XmdRegisterEditres(XtParent(chdlg));
    XtUnmanageChild(XmSelectionBoxGetChild(chdlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(chdlg,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(chdlg,XmDIALOG_TEXT));
    
    rc = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,chdlg,NULL);
    XtVaCreateManagedWidget("hints",xmLabelWidgetClass,rc,NULL);
    chscale = XtVaCreateManagedWidget("channel",xmScaleWidgetClass,rc,NULL);
    XtRemoveAllCallbacks(XmSelectionBoxGetChild(chdlg,XmDIALOG_OK_BUTTON),
			 XmNactivateCallback);
    XtAddCallback(XmSelectionBoxGetChild(chdlg,XmDIALOG_OK_BUTTON),
		  XmNactivateCallback,chscan_start_cb,NULL);
    XtAddCallback(chdlg,XmNcancelCallback,chscan_cancel_cb,NULL);
    XtManageChild(chdlg);
}
#endif

/*----------------------------------------------------------------------*/

static void
pref_done_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    XmSelectionBoxCallbackStruct *cb = call_data;

    if (cb->reason == XmCR_OK  ||  cb->reason == XmCR_APPLY) {
	cfg_set_bool(O_OSD,            XmToggleButtonGetState(pref_osd));
	cfg_set_bool(O_KEYPAD_NTSC,    XmToggleButtonGetState(pref_ntsc));
	cfg_set_bool(O_KEYPAD_PARTIAL, XmToggleButtonGetState(pref_partial));
	cfg_set_int(O_JPEG_QUALITY,    atoi(XmTextGetString(pref_quality)));
	ng_jpeg_quality = GET_JPEG_QUALITY();
    }
    if (cb->reason == XmCR_OK) {
	write_config_file("options");
    }
    XtUnmanageChild(pref_dlg);
}

static void
pref_manage_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    char tmp[16];
    
    XmToggleButtonSetState(pref_osd, GET_OSD(),
			   False);
    XmToggleButtonSetState(pref_ntsc, GET_KEYPAD_NTSC(),
			   False);
    XmToggleButtonSetState(pref_partial, GET_KEYPAD_PARTIAL(),
			   False);
    sprintf(tmp,"%d",ng_jpeg_quality);
    XmTextSetString(pref_quality,tmp);
    XtManageChild(pref_dlg);
}

static void
create_pref(void)
{
    Widget rc1,frame,rc2,rc3;

    pref_dlg = XmCreatePromptDialog(control_shell,"pref",NULL,0);
    XmdRegisterEditres(XtParent(pref_dlg));
    XtUnmanageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_SELECTION_LABEL));
    XtUnmanageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_HELP_BUTTON));
    XtManageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_APPLY_BUTTON));
    XtUnmanageChild(XmSelectionBoxGetChild(pref_dlg,XmDIALOG_TEXT));

    rc1 = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, pref_dlg,
				  NULL);

    /* third frame */
    frame = XtVaCreateManagedWidget("optF",xmFrameWidgetClass,rc1,NULL);
    XtVaCreateManagedWidget("optL",xmLabelWidgetClass,frame,NULL);
    rc2 = XtVaCreateManagedWidget("rc",xmRowColumnWidgetClass,frame,NULL);
    
    /* options */
    pref_osd = XtVaCreateManagedWidget("osd",xmToggleButtonWidgetClass,
				       rc2,NULL);
    pref_ntsc = XtVaCreateManagedWidget("keypad-ntsc",
					xmToggleButtonWidgetClass,
					rc2,NULL);
    pref_partial = XtVaCreateManagedWidget("keypad-partial",
					   xmToggleButtonWidgetClass,
					   rc2,NULL);
    rc3 = XtVaCreateManagedWidget("jpeg", xmRowColumnWidgetClass,
				  rc2,NULL);
    XtVaCreateManagedWidget("label",xmLabelWidgetClass,rc3,NULL);
    pref_quality = XtVaCreateManagedWidget("quality",
					   xmTextWidgetClass,
					   rc3,NULL);
    
    /* buttons */
    XtAddCallback(pref_dlg,XmNokCallback,pref_done_cb,NULL);
    XtAddCallback(pref_dlg,XmNapplyCallback,pref_done_cb,NULL);
    XtAddCallback(pref_dlg,XmNcancelCallback,pref_done_cb,NULL);
}

/*---------------------------------------------------------------------- */
/* selection & dnd support                                               */

#if 0
static struct ng_video_buf*
convert_buffer(struct ng_video_buf *in, int out_fmt)
{
    struct ng_video_conv *conv;
    struct ng_convert_handle *ch;
    struct ng_video_fmt ofmt;
    int i;

    /* find converter */
    for (i = 0;;) {
	conv = ng_conv_find_to(out_fmt,&i);
	if (NULL == conv)
	    break;
	if (conv->fmtid_in == in->fmt.fmtid)
	    goto found;
    }
    return NULL;

 found:
    memset(&ofmt,0,sizeof(ofmt));
    ofmt.fmtid  = out_fmt;
    ofmt.width  = in->fmt.width;
    ofmt.height = in->fmt.height;
    ch = ng_convert_alloc(conv,&in->fmt,&ofmt);
    return ng_convert_single(ch,in);
}

static struct ng_video_buf*
scale_rgb_buffer(struct ng_video_buf *in, int scale)
{
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;
    char *src,*dst;
    unsigned int x,y;
    
    fmt = in->fmt;
    fmt.width  = in->fmt.width  / scale;
    fmt.height = in->fmt.height / scale;
    while (fmt.width & 0x03)
	fmt.width++;
    fmt.bytesperline = fmt.width * 3;
    buf = ng_malloc_video_buf(&fmt, fmt.width * fmt.height * 3);
    
    /* scale down */
    dst = buf->data;
    for (y = 0; y < fmt.height; y++) {
	src = in->data + y * scale * in->fmt.bytesperline;
	for (x = 0; x < fmt.width; x++) {
	    dst[0] = src[0];
	    dst[1] = src[1];
	    dst[2] = src[2];
	    dst += 3;
	    src += 3*scale;
	}
    }
    return buf;
}

struct ipc_data {
    struct list_head     list;
    Atom                 atom;
    struct ng_video_buf  *buf;
    char                 *filename;
    Pixmap               pix;
    Pixmap               icon_pixmap;
    Widget               icon_widget;
};
struct list_head ipc_selections;

static void
ipc_iconify(Widget widget, struct ipc_data *ipc)
{
    struct ng_video_buf *small;
    int scale,depth;
    Arg args[4];
    Cardinal n=0;

    /* calc size */
    for (scale = 1;; scale++) {
	if (ipc->buf->fmt.width  / scale < 128 &&
	    ipc->buf->fmt.height / scale < 128)
	    break;
    }

    /* scale down & create pixmap */
    small = scale_rgb_buffer(ipc->buf,scale);
    small = convert_buffer(small, x11_dpy_fmtid);
    ipc->icon_pixmap = x11_create_pixmap(dpy,&vinfo,small);
        
    /* build DnD icon */
    n = 0;
    depth = DefaultDepthOfScreen(XtScreen(widget));
    XtSetArg(args[n], XmNpixmap, ipc->icon_pixmap); n++;
    XtSetArg(args[n], XmNwidth,  small->fmt.width); n++;
    XtSetArg(args[n], XmNheight, small->fmt.height); n++;
    XtSetArg(args[n], XmNdepth,  depth); n++;
    ipc->icon_widget = XmCreateDragIcon(widget,"dragicon",args,n);
    
    ng_release_video_buf(small);
}

static struct ipc_data*
ipc_find(Atom selection)
{
    struct list_head   *item;
    struct ipc_data    *ipc;
    
    list_for_each(item,&ipc_selections) {
	ipc = list_entry(item, struct ipc_data, list);
	if (ipc->atom == selection)
	    return ipc;
    }
    return NULL;
}

static struct ipc_data*
ipc_init(Atom selection)
{
    struct ipc_data *ipc;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;
    
    /* capture a frame and save a copy */
    video_gd_suspend();
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = cur_tv_width;
    fmt.height = cur_tv_height;
    buf = ng_grabber_get_image(&fmt);
    buf = ng_filter_single(cur_filter,buf);
    ipc = malloc(sizeof(*ipc));
    memset(ipc,0,sizeof(*ipc));
    ipc->buf = ng_malloc_video_buf(&buf->fmt,buf->size);
    ipc->atom = selection;
    ipc->buf->info = buf->info;
    memcpy(ipc->buf->data,buf->data,buf->size);
    ng_release_video_buf(buf);
    video_gd_restart();

    list_add_tail(&ipc->list,&ipc_selections);
    return ipc;
}

static void
ipc_tmpfile(struct ipc_data *ipc)
{
    static char *base = "motv";
    struct ng_video_buf *buf;
    char *tmpdir;
    int fd;

    if (NULL != ipc->filename)
	return;

    tmpdir = getenv("TMPDIR");
    if (NULL == tmpdir)
	tmpdir="/tmp";
    ipc->filename = malloc(strlen(tmpdir)+strlen(base)+16);
    sprintf(ipc->filename,"%s/%s-XXXXXX",tmpdir,base);
    fd = mkstemp(ipc->filename);

    ipc->buf->refcount++;
    buf = convert_buffer(ipc->buf, VIDEO_JPEG);
    write(fd,buf->data,buf->size);
    ng_release_video_buf(buf);
}

static void
ipc_pixmap(struct ipc_data *ipc)
{
    struct ng_video_buf *buf;
    
    if (0 != ipc->pix)
	return;

    ipc->buf->refcount++;
    buf = convert_buffer(ipc->buf, x11_dpy_fmtid);
    ipc->pix = x11_create_pixmap(dpy,&vinfo,buf);
    ng_release_video_buf(buf);
    return;
}

static void
ipc_fini(Atom selection)
{
    struct ipc_data *ipc;

    ipc = ipc_find(selection);
    if (NULL == ipc)
	return;

    /* free stuff */
    if (ipc->buf)
	ng_release_video_buf(ipc->buf);
    if (ipc->filename) {
	unlink(ipc->filename);
	free(ipc->filename);
    }
    if (ipc->icon_widget)
	XtDestroyWidget(ipc->icon_widget);
    if (ipc->icon_pixmap)
	XFreePixmap(dpy,ipc->icon_pixmap);
    if (ipc->pix)
	XFreePixmap(dpy,ipc->pix);

    list_del(&ipc->list);
    free(ipc);
}

static Atom ipc_unique_atom(Widget widget)
{
    char id_name[32];
    Atom id;
    int i;

    for (i = 0;; i++) {
	sprintf(id_name,"_MOTV_IMAGE_%lX_%d",XtWindow(widget),i);
	id = XInternAtom(XtDisplay(widget),id_name,False);
	if (NULL == ipc_find(id))
	    break;
    }
    return id;
}

static void
ipc_convert(Widget widget, XtPointer ignore, XtPointer call_data)
{
    XmConvertCallbackStruct *ccs = call_data;
    struct ipc_data *ipc;
    Atom *targs;
    Pixmap *pix;
    unsigned long *ldata;
    unsigned char *cdata;
    char *filename;
    int n;

    if (debug) {
	char *y = !ccs->type      ? NULL : XGetAtomName(dpy,ccs->type);
	char *t = !ccs->target    ? NULL : XGetAtomName(dpy,ccs->target);
	char *s = !ccs->selection ? NULL : XGetAtomName(dpy,ccs->selection);
	fprintf(stderr,"conv: target=%s type=%s selection=%s\n",t,y,s);
	if (y) XFree(y);
	if (t) XFree(t);
	if (s) XFree(s);
    }
    
    /* tell which formats we can handle */
    if ((ccs->target == XA_TARGETS) ||
	(ccs->target == _MOTIF_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_DEFERRED_CLIPBOARD_TARGETS) ||
	(ccs->target == _MOTIF_EXPORT_TARGETS)) {
	n = 0;
	targs = (Atom*)XtMalloc(sizeof(Atom)*12);
	if (ccs->target != _MOTIF_CLIPBOARD_TARGETS) {
	    targs[n++] = XA_TARGETS;
	    targs[n++] = MIME_IMAGE_PPM;
	    targs[n++] = XA_PIXMAP;
	    targs[n++] = XA_FOREGROUND;
	    targs[n++] = XA_BACKGROUND;
	    targs[n++] = XA_COLORMAP;
	    targs[n++] = MIME_IMAGE_JPEG;
	    targs[n++] = XA_FILE_NAME;
	    targs[n++] = XA_FILE;
	    targs[n++] = MIME_TEXT_URI_LIST;
	    targs[n++] = _NETSCAPE_URL;
	}
	if (ccs->target == _MOTIF_EXPORT_TARGETS) {
	    /* save away drag'n'drop data */
	    ipc_init(ccs->selection);
	}
	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;

    } else if (ccs->target == _MOTIF_SNAPSHOT) {
	/* save away clipboard data */
	n = 0;
	targs = (Atom*)XtMalloc(sizeof(Atom));
	targs[n++] = ipc_unique_atom(widget);
	ipc_init(targs[0]);
	ccs->value  = targs;
	ccs->length = n;
	ccs->type   = XA_ATOM;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }
    
    /* find data */
    ipc = ipc_find(ccs->selection);
    if (NULL == ipc) {
	/* shouldn't happen */
	fprintf(stderr,"oops: selection data not found\n");
	ccs->status = XmCONVERT_REFUSE;
	return;
    }
    
    if (ccs->target == _MOTIF_LOSE_SELECTION ||
	ccs->target == XA_DONE) {
	/* cleanup */
	ipc_fini(ccs->selection);
	ccs->value  = NULL;
	ccs->length = 0;
	ccs->type   = XA_INTEGER;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;
	return;
    }

    /* convert data */
    if (ccs->target == XA_BACKGROUND ||
	ccs->target == XA_FOREGROUND ||
	ccs->target == XA_COLORMAP) {
	n = 0;
	ldata = (Atom*)XtMalloc(sizeof(Atom)*8);
	if (ccs->target == XA_BACKGROUND) {
	    ldata[n++] = WhitePixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_FOREGROUND) {
	    ldata[n++] = BlackPixelOfScreen(XtScreen(widget));
	    ccs->type  = XA_PIXEL;
	}
	if (ccs->target == XA_COLORMAP) {
	    ldata[n++] = DefaultColormapOfScreen(XtScreen(widget));
	    ccs->type  = XA_COLORMAP;
	}
	ccs->value  = ldata;
	ccs->length = n;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == XA_PIXMAP) {
	/* xfer pixmap */
	ipc_pixmap(ipc);
	pix = (Pixmap*)XtMalloc(sizeof(Pixmap));
	pix[0] = ipc->pix;
	if (debug)
	    fprintf(stderr,"conv: pixmap id is 0x%lx\n",pix[0]);
	ccs->value  = pix;
	ccs->length = 1;
	ccs->type   = XA_DRAWABLE;
	ccs->format = 32;
	ccs->status = XmCONVERT_DONE;

    } else if (ccs->target == MIME_IMAGE_PPM) {
	cdata = XtMalloc(ipc->buf->size + 32);
	n = sprintf(cdata,"P6\n%d %d\n255\n",
		    ipc->buf->fmt.width,ipc->buf->fmt.height);
	memcpy(cdata+n,ipc->buf->data,ipc->buf->size);
	ccs->value  = cdata;
	ccs->length = n+ipc->buf->size;
	ccs->type   = MIME_IMAGE_PPM;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;
	
    } else if (ccs->target == MIME_IMAGE_JPEG) {
	struct ng_video_buf *buf;
	ipc->buf->refcount++;
	buf = convert_buffer(ipc->buf, VIDEO_JPEG);
	cdata = XtMalloc(buf->size);
	memcpy(cdata,buf->data,buf->size);
	ng_release_video_buf(buf);
	ccs->value  = cdata;
	ccs->length = buf->size;
	ccs->type   = MIME_IMAGE_JPEG;
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;
	
    } else if (ccs->target == XA_FILE_NAME       ||
	       ccs->target == XA_FILE            ||
	       ccs->target == XA_STRING          ||
	       ccs->target == MIME_TEXT_URI_LIST ||
	       ccs->target == _NETSCAPE_URL) {
	/* xfer filename (image via tmp file) */
	ipc_tmpfile(ipc);
	if (ccs->target == MIME_TEXT_URI_LIST ||
	    ccs->target == _NETSCAPE_URL) {
	    /* filename => url */
	    filename = XtMalloc(strlen(ipc->filename)+8);
	    sprintf(filename,"file:%s\r\n",ipc->filename);
	    ccs->type = ccs->target;
	    if (debug)
		fprintf(stderr,"conv: tmp url is %s\n",filename);
	} else {
	    filename = XtMalloc(strlen(ipc->filename));
	    strcpy(filename,ipc->filename);
	    ccs->type = XA_STRING;
	    if (debug)
		fprintf(stderr,"conv: tmp file is %s\n",filename);
	}
	ccs->value  = filename;
	ccs->length = strlen(filename);
	ccs->format = 8;
	ccs->status = XmCONVERT_DONE;

    } else {
	/* shouldn't happen */
	fprintf(stderr,"oops: unknown target\n");
	ccs->status = XmCONVERT_REFUSE;
    }
}

static void
ipc_finish(Widget widget, XtPointer ignore, XtPointer call_data)
{
    if (debug)
	fprintf(stderr,"conv: transfer finished\n");
    ipc_fini(_MOTIF_DROP);
}
#endif

void IpcAction(Widget widget, XEvent *event, String *argv, Cardinal *argc)
{
#if 0
    struct    ipc_data *ipc;
    Widget    drag;
    Arg       args[4];
    Cardinal  n=0;

    if (0 == *argc)
	return;
    if (!(f_drv & CAN_CAPTURE)) {
	if (debug)
	    fprintf(stderr,"ipc: can't capture - cancel\n");
	return;
    }

    if (debug)
	fprintf(stderr,"ipc: %s\n",argv[0]);
    if (0 == strcmp(argv[0],"drag")) {
	ipc_fini(_MOTIF_DROP);
	ipc = ipc_init(_MOTIF_DROP);
	ipc_iconify(widget,ipc);
	n = 0;
	XtSetArg(args[n], XmNdragOperations, XmDROP_COPY); n++;
	XtSetArg(args[n], XmNsourcePixmapIcon, ipc->icon_widget); n++;
	drag = XmeDragSource(tv, NULL, event, args, n);
	XtAddCallback(drag, XmNdragDropFinishCallback, ipc_finish, NULL);
    }
    if (0 == strcmp(argv[0],"primary")) {
#if 0
	ipc_fini(XA_PRIMARY);
	ipc_init(XA_PRIMARY);
	XmePrimarySource(tv,XtLastTimestampProcessed(dpy));
#else
	fprintf(stderr,"FIXME [primary called]\n");
#endif
    }
    if (0 == strcmp(argv[0],"clipboard")) {
	XmeClipboardSource(tv,XmCOPY,XtLastTimestampProcessed(dpy));
    }
#endif
}

/*----------------------------------------------------------------------*/

Widget levels_left, levels_right;
XtInputId levels_id;
const struct ng_dsp_driver *levels_dsp;
void *levels_hdsp;

#if 0
static void
levels_input(XtPointer clientdata, int *src, XtInputId *id)
{
    struct ng_audio_buf *buf;
    int left, right;

    buf = levels_dsp->read(levels_hdsp,0);
    oss_levels(buf,&left,&right);
    XmScaleSetValue(levels_left,left);
    XmScaleSetValue(levels_right,right);
    if (debug > 1)
	fprintf(stderr,"levels: left = %3d, right = %3d\r",left,right);
}
#endif

static void
levels_toggle_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
#if 0
    XmToggleButtonCallbackStruct *tb = call_data;
    struct ng_audio_fmt a;

    if (tb->reason != XmCR_VALUE_CHANGED)
	return;

    if (tb->set  &&  NULL == levels_dsp) {
	/* enable */
	a.fmtid = AUDIO_U8_STEREO;
	a.rate  = 44100;
	levels_dsp = ng_dsp_open(args.dspdev,&a,1,&levels_hdsp);
	if (levels_dsp) {
	    levels_dsp->startrec(levels_hdsp);
	    levels_id  = XtAppAddInput(app_context,levels_dsp->fd(levels_hdsp),
				       (XtPointer)XtInputReadMask,
				       levels_input,NULL);
	    if (debug)
		fprintf(stderr,"levels: started sound monitor\n");
	}
    }
    if (!tb->set  &&  NULL != levels_hdsp) {
	/* disable */
	XtRemoveInput(levels_id);
	levels_dsp->close(levels_hdsp);
	levels_dsp = NULL;
	levels_hdsp = NULL;
	XmScaleSetValue(levels_left,0);
	XmScaleSetValue(levels_right,0);
	if (debug)
	    fprintf(stderr,"levels: stopped sound monitor\n");
    }
#endif
}

static void
create_levels(void)
{
    Widget rc;
    
    levels_shell = XtVaAppCreateShell("levels", "MoTV4",
				      topLevelShellWidgetClass,
				      dpy,
				      XtNclientLeader,app_shell,
				      XtNvisual,vinfo.visual,
				      XtNcolormap,colormap,
				      XtNdepth,vinfo.depth,
				      XmNdeleteResponse,XmDO_NOTHING,
				      NULL);
    XmdRegisterEditres(levels_shell);
    XmAddWMProtocolCallback(levels_shell,WM_DELETE_WINDOW,
			    popupdown_cb,levels_shell);
    rc = XtVaCreateManagedWidget("rc", xmRowColumnWidgetClass, levels_shell,
				 NULL);

    levels_toggle = XtVaCreateManagedWidget("enable",
					    xmToggleButtonWidgetClass,rc,
					    NULL);
    XtAddCallback(levels_toggle,XmNvalueChangedCallback,
		  levels_toggle_cb,NULL);

    levels_left = XtVaCreateManagedWidget("left",xmScaleWidgetClass,rc,
					  NULL);
    levels_right = XtVaCreateManagedWidget("right",xmScaleWidgetClass,rc,
					   NULL);
}

/*----------------------------------------------------------------------*/

struct stderr_handler {
    Widget box;
    XmString str;
    int pipe;
    XtInputId id;
};

static void
stderr_input(XtPointer clientdata, int *src, XtInputId *id)
{
    struct stderr_handler *h = clientdata;
    XmString item;
    Widget label;
    char buf[1024];
    int rc;
    
    rc = read(h->pipe,buf,sizeof(buf)-1);
    if (rc <= 0) {
	/* Oops */
	XtRemoveInput(h->id);
	close(h->pipe);
	XtDestroyWidget(h->box);
	free(h);
    }
    buf[rc] = 0;
    item = XmStringGenerate(buf, NULL, XmMULTIBYTE_TEXT,NULL);
    h->str = XmStringConcatAndFree(h->str,item);
    label = XmMessageBoxGetChild(h->box,XmDIALOG_MESSAGE_LABEL);
    XtVaSetValues(label,XmNlabelString,h->str,NULL);
    XtManageChild(h->box);
};

static void
stderr_ok_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct stderr_handler *h = clientdata;

    XmStringFree(h->str);
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
    XtUnmanageChild(h->box);
}

static void
stderr_close_cb(Widget widget, XtPointer clientdata, XtPointer call_data)
{
    struct stderr_handler *h = clientdata;

    XmStringFree(h->str);
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
}

static void
stderr_init(void)
{
    struct stderr_handler *h;
    int p[2];

    if (debug)
	return;
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));
    h->str = XmStringGenerate("", NULL, XmMULTIBYTE_TEXT,NULL);
    h->box = XmCreateErrorDialog(app_shell,"errbox",NULL,0);
    XtUnmanageChild(XmMessageBoxGetChild(h->box,XmDIALOG_HELP_BUTTON));
    XtUnmanageChild(XmMessageBoxGetChild(h->box,XmDIALOG_CANCEL_BUTTON));
    XtAddCallback(h->box,XmNokCallback,stderr_ok_cb,h);
    XtAddCallback(XtParent(h->box),XmNpopdownCallback,stderr_close_cb,h);
    pipe(p);
    dup2(p[1],2);
    close(p[1]);
    h->pipe = p[0];
    h->id = XtAppAddInput(app_context,h->pipe,(XtPointer)XtInputReadMask,
			  stderr_input,h);
}

/*----------------------------------------------------------------------*/

int
main(int argc, char *argv[])
{
#if 0
    int            i;
    unsigned long  freq;
#endif
    
    XtSetLanguageProc(NULL,NULL,NULL);
    app_shell = XtVaAppInitialize(&app_context, "MoTV4",
				  opt_desc, opt_count,
				  &argc, argv,
				  fallback_ressources,
				  NULL);
    XmdRegisterEditres(app_shell);
    dpy = XtDisplay(app_shell);
    x11_icons_init(dpy,0);
    init_atoms(dpy);
    ng_init();
    hello_world("motv");
    
    /* handle command line args, read config file */
    handle_cmdline_args(&argc,argv);

    /* set hooks (command.c) */
    update_title        = new_title;
    display_message     = new_message;
#ifdef HAVE_ZVBI
    vtx_subtitle        = display_subtitle;
#endif
    fullscreen_hook     = do_motif_fullscreen;
    freqtab_notify      = new_freqtab;
    setstation_notify   = new_channel;
    movie_hook          = do_movie_record;
    rec_status          = do_rec_status;
    exit_hook           = do_exit;
#if 0 /* FIXME */
    set_capture_hook    = do_capture;
    capture_get_hook    = video_gd_suspend;
    capture_rel_hook    = video_gd_restart;
#endif

    /* look for a useful visual */
    visual_init("motv","MoTV4");
    
    /* x11 stuff */
    XtAppAddActions(app_context,actionTable,
		    sizeof(actionTable)/sizeof(XtActionsRec));
    x11_misc_init(dpy);
    XmAddWMProtocolCallback(app_shell,WM_DELETE_WINDOW,ExitCB,NULL);
    if (debug)
	fprintf(stderr,"main: xinerama extention...\n");
    xfree_xinerama_init(dpy);
#if 0 /* def HAVE_LIBXV */
    if (debug)
	fprintf(stderr,"main: xvideo extention [video]...\n");
    if (args.xv_video)
	xv_video_init(args.xv_port,0);
#endif

    if (debug)
	fprintf(stderr,"main: init main window...\n");
    tv = XtVaCreateManagedWidget("tv",xmPrimitiveWidgetClass,app_shell,NULL);
    XtAddEventHandler(XtParent(tv),StructureNotifyMask, True,
		      resize_event, NULL);
    if (debug)
	fprintf(stderr,"main: install signal handlers...\n");
    xt_siginit();

    /* create windows */
    XSetIOErrorHandler(x11_ctrl_alt_backspace);
    if (debug)
	fprintf(stderr,"main: checking wm...\n");
    wm_detect(dpy);
    if (debug)
	fprintf(stderr,"main: creating windows ...\n");
    create_onscreen(xmLabelWidgetClass);
    create_vtx();
    create_strwin();
    create_attrs();

    create_control();
    create_station_prop();
    create_pref();
    create_levels();

    init_movie_menus();
#if 0
    INIT_LIST_HEAD(&ipc_selections);
    if (f_drv & CAN_CAPTURE)
	XtAddCallback(tv,XmNconvertCallback,ipc_convert,NULL);
#endif

    /* finalize X11 init + show windows */
    xt_input_init(dpy);
    if (debug)
	fprintf(stderr,"main: mapping main window ...\n");
    stderr_init();
    XtRealizeWidget(app_shell);
    create_pointers(app_shell);
    create_bitmaps(app_shell);
    XDefineCursor(dpy, XtWindow(app_shell), left_ptr);

    XtVaSetValues(app_shell,
		  XtNwidthInc,  WIDTH_INC,
		  XtNheightInc, HEIGHT_INC,
		  XtNminWidth,  WIDTH_INC,
		  XtNminHeight, HEIGHT_INC,
		  NULL);

    /* mouse pointer magic */
    XtAddEventHandler(tv, PointerMotionMask, True, mouse_event, NULL);
    mouse_event(tv,NULL,NULL,NULL);

#if 1
    if (0 == cfg_sections_count("stations") &&
	0 != cfg_sections_count("dvb") &&
	NULL != cfg_get_str("devs",cfg_sections_first("devs"),"dvb")) {
	/* easy start for dvb users, import vdr's list ... */
	vdr_import_stations();
    }
#endif

    /* build channel list */
    if (args.readconfig) {
	if (debug)
	    fprintf(stderr,"main: parse channels from config file ...\n");
	apply_config();
    }
    x11_channel_init();

    if (optind+1 == argc)
	do_va_cmd(2,"setstation",argv[optind]);

    XtAddEventHandler(tv,ExposureMask, True, tv_expose_event, NULL);
    if (args.fullscreen) {
	XSync(dpy,False);
	do_motif_fullscreen();
    }

    xt_main_loop();
    return 0;
}
