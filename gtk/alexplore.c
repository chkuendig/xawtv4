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
#define O_CMD_DEVICE	       	O_CMDLINE, "device"

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
	.cmdline  = "device",
	.option   = { O_CMD_DEVICE },
	.needsarg = 1,
	.desc     = "pick device config (use -hwconfig to list them)",
    },{
	/* end of list */
    }
};

/* ----------------------------------------------------------------------- */

static int exit_application;

static void termsig(int signal)
{
    exit_application++;
}

static void ignore_sig(int signal)
{
    /* nothing */
}

static void siginit(void)
{
    struct sigaction act,old;

    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);

    act.sa_handler = termsig;
    sigaction(SIGINT,  &act, &old);
    sigaction(SIGTERM, &act, &old);
    sigaction(SIGTERM, &act, &old);

    act.sa_handler = ignore_sig;
    sigaction(SIGALRM, &act, &old);
}

/* ------------------------------------------------------------------------ */

static int timeout = 20;
static int current;

static void dvbwatch_tty(struct psi_info *info, int event,
			 int tsid, int pnr, void *data)
{
    struct psi_program *pr;

    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	fprintf(stderr,"  tsid  %5d\n",tsid);
	current = tsid;
	break;
    case DVBMON_EVENT_UPDATE_PR:
	pr = psi_program_get(info, tsid, pnr, 0);
	if (!pr)
	    return;
	if (tsid != current)
	    return;
	if (pr->type != 1)
	    return;
	if (0 == pr->v_pid)
	    return;
	if (pr->name[0] == '\0')
	    return;
	/* Hmm, get duplicates :-/ */
	fprintf(stderr,"    pnr %5d  %s\n",
		pr->pnr, pr->name);
    }
}

static void tty_scan()
{
    GMainContext *context = g_main_context_default();
    time_t tuned;
    char *sec;
    char *name;

    sec = cfg_sections_first("dvb-ts");
    if (NULL == sec)
	fprintf(stderr,"Oops, have no starting point yet.\n");
    for (;sec;) {
	/* tune */
	name = cfg_get_str("dvb-ts",sec,"name");
	fprintf(stderr,"Tuning tsid %s ...\n", sec);
	if (0 != dvb_frontend_tune(devs.dvb, "dvb-ts", sec))
	    break;
	current = 0;

 	/* fish data */
	tuned = time(NULL);
	while (!exit_application && time(NULL) - tuned < timeout) {
	    alarm(timeout/3);
	    g_main_context_iteration(context,TRUE);
	}
	if (!current) {
	    fprintf(stderr,"Hmm, no data received. Frontend is%s locked.\n",
		    dvb_frontend_is_locked(devs.dvb) ? "" : " not");
	}
	if (exit_application) {
	    fprintf(stderr,"Ctrl-C seen, stopping here.\n");
	    break;
	}

	/* more not-yet seen transport streams? */
	cfg_set_sflags("dvb-ts",sec,1,1);
	cfg_sections_for_each("dvb-ts",sec) {
	    if (!cfg_get_sflags("dvb-ts",sec))
		break;
	}
    }
}

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

    setlocale(LC_ALL,"");
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);

    /* options */
    cfg_parse_cmdline(&argc,argv,cmd_opts_only);
    if (GET_CMD_HELP())
	usage();
    debug = GET_CMD_DEBUG();

    have_x11 = gtk_init_check(&argc, &argv);
    
    ng_init();
    devlist_init(1, 0, 0);
    device_init(cfg_get_str(O_CMD_DEVICE));
    if (NULL == devs.dvb)
	gtk_panic_box(have_x11, "No DVB device found.\n");
    devs.dvbmon = dvbmon_init(devs.dvb, debug, 1, have_x11, 2);
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
	gtk_main();
    } else {
	/* enter tty mode */
	fprintf(stderr,"scanning ...\n");
	dvbmon_add_callback(devs.dvbmon,dvbwatch_tty,NULL);
	siginit();
	tty_scan();
    }
    fprintf(stderr,"bye...\n");

    write_config_file("dvb-ts");
    write_config_file("dvb-pr");
    write_config_file("vdr-channels");  // DEBUG
    write_config_file("vdr-diseqc");    // DEBUG
    
    dvbmon_fini(devs.dvbmon);
    device_fini();
    exit(0);
}
