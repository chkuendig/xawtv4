#include <X11/Xproto.h>		/* for CARD32 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>

Window xscreensaver_find_window(Display *dpy, char **version);
int    xscreensaver_send_deactivate(Display *dpy, Window window);
void   xscreensaver_init(Display *dpy);
