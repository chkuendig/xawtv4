/*
 * dvb epg browser
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
#include "commands.h"
#include "tv-config.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "gui.h"

/* ------------------------------------------------------------------------ */

/* misc globals */
int debug = 0;
Display *dpy;
static struct eit_state *eit;
static int tune_secs = 32;

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
	    "usage: dvbrowse [ options ]\n"
	    "options:\n");

    cfg_help_cmdline(cmd_opts_only,2,16,0);
    fprintf(stderr,"\n");

    exit(0);
}

static gboolean tune_timeout(gpointer data)
{
    static time_t last_tune;
    static char *list = NULL;
    static int n;
    char buf[64];

    if (list) {
	if (time(NULL) - last_tune < tune_secs)
	    return TRUE;
	if (time(NULL) - eit_last_new_record < tune_secs)
	    return TRUE;
	list = cfg_sections_next("dvb-ts",list);
	n++;
    }
    if (!list) {
	list = cfg_sections_first("dvb-ts");
	n = 1;
    }
    if (!list) {
	fprintf(stderr,
		"Hmm, no DVB streams found.  Probably you don't have scanned\n"
		"for stations yet (use \"alexplore\" to scan).\n");
	return FALSE;
    }

    last_tune = time(NULL);
    dvb_frontend_tune(devs.dvb, "dvb-ts", list);
    snprintf(buf,sizeof(buf),"Scanning TSID %s (%d/%d).",
	     list, n, cfg_sections_count("dvb-ts"));
    gtk_label_set_label(GTK_LABEL(epg_status),buf);
	
    return TRUE;
}

int
main(int argc, char *argv[])
{
    /* options */
    cfg_parse_cmdline(&argc,argv,cmd_opts_only);
    if (GET_CMD_HELP())
	usage();
    debug = GET_CMD_DEBUG();

    gtk_init(&argc, &argv);
    ng_init();
    devlist_init(1, 0, 0);
    device_init(NULL);
    if (NULL == devs.dvb)
	gtk_panic_box(1, "No DVB device found.\n");
    devs.dvbmon = dvbmon_init(devs.dvb, debug, 1, 2);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_scanner,NULL);
//  eit = eit_add_watch(devs.dvb, 0x4e,0xff, 0, 0);
    eit = eit_add_watch(devs.dvb, 0x50,0xf0, debug, 0);
    read_config_file("dvb-ts");
    read_config_file("dvb-pr");
    dvb_lang_init();

    /* setup gtk gui */
    create_epgwin(NULL);
    gtk_widget_show_all(epg_win);
    if (!debug)
	gtk_redirect_stderr_to_gui(GTK_WINDOW(epg_win));

    g_timeout_add(1000, tune_timeout, NULL);
    gtk_main();
    fprintf(stderr,"bye...\n");

    eit_del_watch(eit);
    dvbmon_fini(devs.dvbmon);
    device_fini();
    exit(0);
}
