/*
 * dvb channel browser
 */

#define _GNU_SOURCE
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "devs.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "gui.h"

/* ------------------------------------------------------------------------ */

/* misc globals */
int debug = 0;
Display *dpy;

/* ------------------------------------------------------------------------ */

#define O_CMDLINE               "cmdline", "cmdline"

#define O_CMD_HELP             	O_CMDLINE, "help"
#define O_CMD_DEBUG	       	O_CMDLINE, "debug"

#define GET_CMD_HELP()		cfg_get_bool(O_CMD_HELP,   	0)
#define GET_CMD_DEBUG()		cfg_get_int(O_CMD_DEBUG,   	0)

struct cfg_cmdline cmd_opts_only[] = {
    {
	.letter   = 'h',
	.cmdline  = "help",
	.option   = { O_CMD_HELP },
	.value    = "1",
	.desc     = "print this text",
    },{
	.letter   = 'd',
	.cmdline  = "debug",
	.option   = { O_CMD_DEBUG },
	.needsarg = 1,
	.desc     = "set debug level",
    },{
	/* end of list */
    }
};

/* ------------------------------------------------------------------------ */

static void
usage(void)
{
    fprintf(stderr,
	    "\n"
	    "usage: alexplore [ options ]\n"
	    "options:\n");

    cfg_help_cmdline(cmd_opts_only,2,16,0);
    fprintf(stderr,"\n");

    exit(0);
}

int
main(int argc, char *argv[])
{
    gboolean have_x11;

    /* options */
    cfg_parse_cmdline(&argc,argv,cmd_opts_only);
    if (GET_CMD_HELP())
	usage();
    debug = GET_CMD_DEBUG();

    have_x11 = gtk_init_check(&argc, &argv);
    
    ng_init();
    devlist_init(1, 0, 0);
    device_init(NULL);
    if (NULL == devs.dvb)
	gtk_panic_box(have_x11, "No DVB device found.\n");
    devs.dvbmon = dvbmon_init(devs.dvb, debug, 1, 2);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_scanner,NULL);
    read_config_file("dvb-ts");
    read_config_file("dvb-pr");
    dvb_lang_init();

    if (have_x11) {
	/* setup gtk gui */
	dvbscan_create_window(1);
	dvbscan_show_window();
	if (!debug)
	    gtk_redirect_stderr_to_gui(GTK_WINDOW(dvbscan_win));
    } else {
	/* enter tty mode */
	fprintf(stderr,"can't open display\n");
	fprintf(stderr,"non-x11 support not there yet, sorry\n");
	exit(1);
    }

    gtk_main();
    fprintf(stderr,"bye...\n");

    write_config_file("dvb-ts");
    write_config_file("dvb-pr");
    write_config_file("vdr-channels");  // DEBUG
    write_config_file("vdr-diseqc");    // DEBUG
    
    dvbmon_fini(devs.dvbmon);
    device_fini();
    exit(0);
}
