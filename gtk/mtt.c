/*
 * mtt - teletext test app.
 * 
 * This is just main() for the standalone version, the actual
 * code is in vbi-gui.c (motif) and vbi-tty.c (terminal).
 *
 *   (c) 2002,2003 Gerd Knorr <kraxel@bytesex.org>
 * 
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iconv.h>
#include <locale.h>
#include <langinfo.h>
#include <termios.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "devs.h"
#include "vbi-data.h"
#include "vbi-tty.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "gui.h"

int  debug;

/* --------------------------------------------------------------------- */

static int  dvbmode;
static char *device;
static char *vbidev;

#ifdef HAVE_DVB
static int mtt_dvb_findpid(char *device)
{
    struct psi_info    *info;
    struct psi_program *program;
    struct list_head   *item;
    struct dvb_state   *h;
    int pid = 0;

    fprintf(stderr,"no pid given, checking tables, please wait...\n");
    h = dvb_init(device);
    if (NULL == h) {
	fprintf(stderr,"can't init dvb\n");
	exit(1);
    }
    info = psi_info_alloc();
    dvb_get_transponder_info(h, info, 0, debug);
    if (debug)
	dvb_print_transponder_info(info);
    list_for_each(item,&info->programs) {
	program = list_entry(item, struct psi_program, next);
	if (0 == program->t_pid)
	    continue;
	pid = program->t_pid;
    }
    if (0 == pid) {
	fprintf(stderr,"no teletext data stream found, sorry\n");
	exit(1);
    }
    dvb_fini(h);
    psi_info_free(info);
    return pid;
}
#endif

static int mtt_device_init(int pid, int findpid)
{
    char *dev;

    ng_init();
    devlist_init(1,0,0);

    if (NULL == device) {
	dev = cfg_sections_first("devs");
	device = cfg_get_str("devs",dev,"dvb");
	if (NULL == device)
	    device = cfg_get_str("devs",dev,"vbi");
	if (NULL == device)
	    device = cfg_get_str("devs",dev,"video");
	if (NULL == device)
	    device = ng_dev.vbi;
	fprintf(stderr,"using: %s [autodetect]\n",device);
    } else {
	fprintf(stderr,"using: %s [cmd line arg]\n",device);
    }

    if (0 == strncmp(device,"/dev/dvb/",9)) {
	dvbmode = 1;
	vbidev  = malloc(42);
	snprintf(vbidev,42,"%s/demux0",device);
    } else {
	dvbmode = 0;
	vbidev  = device;
    }

    if (dvbmode) {
#ifdef HAVE_DVB
	if (0 == pid && findpid)
	    pid = mtt_dvb_findpid(device);
#else
	fprintf(stderr,"compiled without dvb support, sorry\n");
	exit(1);
#endif
    }
    return pid;
}

/* --------------------------------------------------------------------- */

static void usage(FILE *stream, char *prog)
{
    fprintf(stream,
	    "\n"
	    "mtt -- teletext application\n"
	    "\n"
	    "usage: mtt [ options ]\n"
	    "options:\n"
	    "  -h         print this text\n"
	    "  -d         enable debug messages\n"
	    "  -t         use terminal mode\n"
	    "  -s         simulation\n"
	    "  -c <dev>   use vbi device <dev>, for DVB please use\n"
	    "             /dev/dvb/adapter<n>\n"
	    "  -p <pid>   read vbi data from transport stream pid <pid>\n"
	    "             (for DVB)\n"
	    "\n"
	    "--\n"
	    "Gerd Knorr <kraxel@bytesex.org>\n");
}

static gboolean vbi_data(GIOChannel *source, GIOCondition condition,
			 gpointer data)
{
    struct vbi_state *vbi = data;

    vbi_hasdata(vbi);
    return TRUE;
}

int
main(int argc, char **argv)
{
    struct vbi_state  *vbi;
    struct dvb_state  *dvb;
    struct dvbmon     *dvbmon;
    GIOChannel        *ch;
    guint             id;
    int  pid = 0;
    int  tty = 0;
    int  sim = 0;
    int  c;
    
    setlocale(LC_ALL,"");
    bindtextdomain(PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);

    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hsdtp:c:")))
	    break;
	switch (c) {
	case 'd':
	    debug++;
	    break;
	case 'p':
	    pid = atoi(optarg);
	    break;
	case 't':
	    tty++;
	    break;
	case 's':
	    sim++;
	    break;
	case 'c':
	    device = optarg;
	    break;
	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }

    if (!tty && gtk_init_check(&argc, &argv)) {
	/* x11 */
	mtt_device_init(0,0);
	if (!debug)
	    gtk_redirect_stderr_to_gui(NULL);
	vbi = vbi_open(vbidev, "mtt", debug, sim, 0);
	if (NULL == vbi)
	    gtk_panic_box(True, "Failed to initialize the vbi device.\n"
			  "no TV card installed?\n");
	dvb    = NULL;
	dvbmon = NULL;
#ifdef HAVE_DVB
	if (dvbmode) {
	    vbi_set_pid(vbi, pid);
	    dvb    = dvb_init(device);
	    dvbmon = dvbmon_init(dvb, debug, 0, 0, 2);
	}
#endif
	ch = g_io_channel_unix_new(vbi->fd);
	id = g_io_add_watch(ch, G_IO_IN, vbi_data, vbi);
	if (debug)
	    vbi_event_handler_add(vbi->dec,~0,vbi_dump_event,vbi);
	
	vbi_create_window(vbi,dvbmon);
	gtk_main();
	fprintf(stderr,"bye...\n");
    } else {
	/* tty */
	pid = mtt_device_init(pid,1);
	vbi_tty(vbidev,debug,sim,pid);
    }
    return 0;
}
