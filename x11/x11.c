/*
 * misc x11 functions:  pixmap handling (incl. MIT SHMEM), event
 *                      tracking for the TV widget.
 *
 *  (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/extensions/XShm.h>

#include "grab-ng.h"
#include "capture.h"
#include "x11.h"
#include "xv.h"
#include "commands.h"
#include "devs.h"
#include "blit.h"

#define DEL_TIMER(proc)     XtRemoveTimeOut(proc)
#define ADD_TIMER(proc)     XtAppAddTimeOut(app_context,200,proc,NULL)

extern XtAppContext    app_context;
extern XVisualInfo     vinfo;

/* ------------------------------------------------------------------------ */

Pixmap
x11_capture_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
		   unsigned int width, unsigned int height)
{
#if 0 /* FIXME */
    struct ng_video_buf *buf;
    struct ng_video_fmt fmt;
    Pixmap pix = 0;
#endif

    if (!(devs.video.flags & CAN_CAPTURE))
	return 0;

#if 0 /* FIXME */
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = x11_dpy_fmtid;
    fmt.width  = width  ? width  : cur_tv_width;
    fmt.height = height ? height : cur_tv_height;
    if (NULL == (buf = ng_grabber_get_image(&devs.video,&fmt)))
	return 0;
    buf = ng_filter_single(cur_filter,buf);
    pix = x11_create_pixmap(dpy,vinfo,buf);
    ng_release_video_buf(buf);
    return pix;
#else
    return 0;
#endif
}

void
x11_label_pixmap(Display *dpy, Colormap colormap, Pixmap pixmap,
		 int height, char *label)
{
    static XFontStruct    *font;
    static XColor          color,dummy;
    XGCValues              values;
    GC                     gc;
    
    if (!font) {
	font = XLoadQueryFont(dpy,"fixed");
	XAllocNamedColor(dpy,colormap,"yellow",&color,&dummy);
    }
    values.font       = font->fid;
    values.foreground = color.pixel;
    gc = XCreateGC(dpy, pixmap, GCFont | GCForeground, &values);
    XDrawString(dpy,pixmap,gc,5,height-5,label,strlen(label));
    XFreeGC(dpy, gc);
}
