/*
 *  (c) 1999 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>

#include "config.h"

#ifdef HAVE_LIBXV
# include <X11/Xlib.h>
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include "grab-ng.h"
#include "tv-config.h"
#include "commands.h"
#include "devs.h"
#include "atoms.h"
#include "xv.h"
#include "parseconfig.h"

int debug = 0;
int have_dga = 0;
#ifdef HAVE_LIBXV
Display *dpy;
#endif

/*--- main ---------------------------------------------------------------*/

static void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: v4lctl [ options ] command\n"
	    "options:\n"
	    "  -v, --debug=n      debug level n, n = [0..2]\n"
	    "  -c, --device=file  use <file> as video4linux device\n"
	    "  -h, --help         print this text\n"
	    "\n");
}

int main(int argc, char *argv[])
{
    int c;
    int xvideo = 1;
    char *devname = "default";

    ng_init();
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hv:c:")))
	    break;
	switch (c) {
	case 'v':
	    ng_debug = debug = atoi(optarg);
	    break;
	case 'c':
	    cfg_set_str("devs", devname, "video", optarg);
	    xvideo = 0;
	    break;
	case 'h':
	default:
	    usage();
	    exit(1);
	}
    }
    if (optind == argc) {
	usage();
	exit(1);
    }

#if 0 /* FIXME */
    if (NULL != getenv("DISPLAY"))
	dpy = XOpenDisplay(NULL);
    if (dpy) {
	init_atoms(dpy);
	if (xvideo)
	    xv_video_init(-1,0);
    }
#endif

    read_config();
    devlist_init(1,0,0);
    device_init(devname);
    apply_config();

    do_command(argc-optind,argv+optind);
    device_fini();
#ifdef HAVE_LIBXV
    if (dpy)
	XCloseDisplay(dpy);
#endif
    return 0;
}
