#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/xpm.h>
#include <Xm/Xm.h>

#include "icons.h"
#include "xpm/home.xpm"
#include "xpm/prev.xpm"
#include "xpm/next.xpm"
#include "xpm/movie.xpm"
#include "xpm/snap.xpm"
#include "xpm/mute.xpm"
#include "xpm/exit.xpm"
#include "xpm/tv.xpm"

static void patch_bg(XImage *image, XImage *shape,
		     int width, int height, Pixel bg)
{
    unsigned int x,y;

    for (y = 0; y < height; y++)
	for (x = 0; x < width; x++)
	    if (!XGetPixel(shape, x, y))
		XPutPixel(image, x, y, bg);
}

static void
add_pixmap(Display *dpy, Pixel bg, char *name, char **data)
{
    XImage *image,*shape;
    XpmAttributes attr;
    char sname[32];

    memset(&attr,0,sizeof(attr));
    XpmCreateImageFromData(dpy,data,&image,&shape,&attr);

    if (shape) {
	patch_bg(image,shape,attr.width,attr.height,bg);
	snprintf(sname,sizeof(sname),"%s_shape",name);
	XmInstallImage(shape,sname);
    }
    XmInstallImage(image,name);
}

void
x11_icons_init(Display *dpy, unsigned long bg)
{
    add_pixmap(dpy, bg,  "home",   home_xpm);
    add_pixmap(dpy, bg,  "prev",   prev_xpm);
    add_pixmap(dpy, bg,  "next",   next_xpm);
    add_pixmap(dpy, bg,  "movie",  movie_xpm);
    add_pixmap(dpy, bg,  "snap",   snap_xpm);
    add_pixmap(dpy, bg,  "mute",   mute_xpm);
    add_pixmap(dpy, bg,  "exit",   exit_xpm);
    add_pixmap(dpy, bg,  "TVimg",  tv_xpm);
}
