#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "grab-ng.h"
#include "xscreensaver.h"

static XErrorHandler old_handler = 0;
static Bool got_badwindow = False;

Atom XA_SCREENSAVER_VERSION;
Atom XA_SCREENSAVER;
Atom XA_DEACTIVATE;

static int BadWindow_ehandler(Display *dpy, XErrorEvent *error)
{
    if (error->error_code == BadWindow) {
	got_badwindow = True;
	return 0;
    } else {
	if (!old_handler)
	    BUG();
	return(*old_handler)(dpy, error);
    }
}

Window xscreensaver_find_window(Display *dpy, char **version)
{
    unsigned int i;
    Window root = RootWindowOfScreen (DefaultScreenOfDisplay (dpy));
    Window root2, parent, *kids;
    unsigned int nkids;
    
    if (version) *version = 0;
    
    if (! XQueryTree (dpy, root, &root2, &parent, &kids, &nkids))
	BUG();
    if (root != root2)
	BUG();
    if (parent)
	BUG();
    if (! (kids && nkids))
	return 0;
    for (i = 0; i < nkids; i++) {
	Atom type;
	int format;
	unsigned long nitems, bytesafter;
	unsigned char *v;
	int status;
	
	/* We're walking the list of root-level windows and trying to find
	   the one that has a particular property on it.  We need to trap
	   BadWindows errors while doing this, because it's possible that
	   some random window might get deleted in the meantime.  (That
	   window won't have been the one we're looking for.)
	*/
	XSync (dpy, False);
	if (old_handler)
	    BUG()
	got_badwindow = False;
	old_handler = XSetErrorHandler (BadWindow_ehandler);
	status = XGetWindowProperty(dpy, kids[i],
				    XA_SCREENSAVER_VERSION,
				    0, 200, False, XA_STRING,
				    &type, &format, &nitems, &bytesafter,
				    &v);
	XSync (dpy, False);
	XSetErrorHandler (old_handler);
	old_handler = 0;
	
	if (got_badwindow) {
	    status = BadWindow;
	    got_badwindow = False;
        }
	
	if (status == Success && type != None) {
	    if (version)
		*version = v;
	    return kids[i];
	}
    }
    return 0;
}

int xscreensaver_send_deactivate(Display *dpy, Window window)
{
    XEvent event;

#if 0
    /* Select for property change events, so that we can read the response. */
    XWindowAttributes xgwa;
    XGetWindowAttributes (dpy, window, &xgwa);
    XSelectInput (dpy, window, xgwa.your_event_mask | PropertyChangeMask);
#endif

    event.xany.type = ClientMessage;
    event.xclient.display = dpy;
    event.xclient.window = window;
    event.xclient.message_type = XA_SCREENSAVER;
    event.xclient.format = 32;
    memset(&event.xclient.data, 0, sizeof(event.xclient.data));
    event.xclient.data.l[0] = XA_DEACTIVATE;
    if (!XSendEvent (dpy, window, False, 0L, &event))
	return -1;
    XSync (dpy, 0);
    return 0;
}

void xscreensaver_init(Display *dpy)
{
    XA_SCREENSAVER_VERSION = XInternAtom(dpy, "_SCREENSAVER_VERSION", False);
    XA_SCREENSAVER         = XInternAtom(dpy, "SCREENSAVER",          False);
    XA_DEACTIVATE          = XInternAtom(dpy, "DEACTIVATE",           False);
}
