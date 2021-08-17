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
#include "parseconfig.h"
#include "tv-config.h"
#include "commands.h"
#include "devs.h"
#include "atoms.h"
#include "xv.h"

int have_dga = 0;
Display *dpy;

/*--- main ---------------------------------------------------------------*/

struct cfg_cmdline cmd_opts_only[] = {
    {
	.letter   = 'h',
	.cmdline  = "help",
	.option   = { O_CMD_HELP },
	.value    = "1",
	.desc     = "print this text",
    },{
	.cmdline  = "debug",
	.option   = { O_CMD_DEBUG },
	.needsarg = 1,
	.desc     = "set debug level",
    },{
	.cmdline  = "device",
	.option   = { O_CMD_DEVICE },
	.needsarg = 1,
	.desc     = "pick device config",
    },{
	/* end of list */
    }
};

static void
usage(FILE *out)
{
    fprintf(out,
	    "\n"
	    "usage: v4lctl [ options ] command\n"
	    "options:\n");

    cfg_help_cmdline(out,cmd_opts_only,2,16,0);
    fprintf(out,"\n");

    cfg_help_cmdline(out,cmd_opts_devices,2,16,40);
    fprintf(out,"\n");

    exit(0);
}

static void
parse_args(int *argc, char **argv)
{
    read_config();
    cfg_parse_cmdline(argc,argv,cmd_opts_only);
    cfg_parse_cmdline(argc,argv,cmd_opts_devices);

    if (GET_CMD_HELP())
	usage(stdout);

    debug    = GET_CMD_DEBUG();
    ng_debug = debug;
}

int main(int argc, char *argv[])
{
    ng_init();
    parse_args(&argc,argv);
    if (1 == argc) {
	usage(stderr);
	exit(1);
    }

    if (NULL != getenv("DISPLAY"))
	dpy = XOpenDisplay(NULL);
    if (dpy)
	init_atoms(dpy);

    devlist_init(1,0,0);
    device_init(cfg_get_str(O_CMD_DEVICE));
    apply_config();

    do_command(argc-1,argv+1);
    device_fini();
    if (dpy)
	XCloseDisplay(dpy);
    return 0;
}
