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
#include "dvb-monitor.h"
#include "gui.h"

/* ------------------------------------------------------------------------ */

/* misc globals */
int debug = 0;
Display *dpy;

/* local globales */
static struct dvbmon *dvbmon;

/* ------------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
    ng_init();
    devlist_init(1, 0, 0);
    device_init(NULL);
    if (NULL == devs.dvb) {
	fprintf(stderr,"no dvb device found\n");
	exit(1);
    }
    dvbmon = dvbmon_init(devs.dvb, 0);
    dvbmon_add_callback(dvbmon,dvbwatch_scanner,NULL);
    read_config_file("dvb-ts");
    read_config_file("dvb-pr");

    if (gtk_init_check(&argc, &argv)) {
	/* setup gtk gui */
	dvbscan_create_window(1,dvbmon);
	gtk_widget_show_all(dvbscan_win);
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
    
    dvbmon_fini(dvbmon);
    device_fini();
    exit(0);
}
