#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "grab-ng.h"
#include "dvb.h"

/* ----------------------------------------------------------------------- */

static int watch_signal(struct dvb_state *h, int loop)
{
    int i;
    
    dvb_frontend_status_title();
    for (i = 0; i < loop; i++) {
	dvb_frontend_status_print(h);
	sleep(1);
    }
    return 0;
}

static int dump_info(struct dvb_state *dvb, int verbose)
{
    struct psi_info info;

    dvb_get_transponder_info(dvb,&info,1,verbose);
    dvb_print_transponder_info(&info);
    return 0;
}

/* ----------------------------------------------------------------------- */

static void
usage(FILE *out, char *argv0)
{
    char *name;

    if (NULL != (name = strrchr(argv0,'/')))
	name++;
    else
	name = argv0;

    fprintf(out,
	    "dvb debug tool\n"
	    "\n"
	    "usage: %s [ options ] station\n"
	    "options:\n"
	    "  -h          print this help text\n"
	    "  -v          be verbose\n"
	    "  -t          dump transponder info\n"
	    "  -s          watch signal quality   [default]\n"
	    "    -1        run once\n",
	    name);
};


#define JOB_SIGNAL 1
#define JOB_DUMP   2

int main(int argc, char *argv[])
{
    struct dvb_state *h;
    int job = JOB_SIGNAL;
    int adapter = 0;
    int verbose = 0;
    int count = 9999;
    int c;
    
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hvst123a:")))
	    break;
	switch (c) {
	case 'v':
	    verbose++;
	    break;
	case 's':
	    job = JOB_SIGNAL;
	    break;
	case 't':
	    job = JOB_DUMP;
	    break;
	case '1':
	case '2':
	case '3':
	    count = c - '0';
	    break;
	case 'a':
	    adapter = atoi(optarg);
	    break;
	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }

    if (optind == argc) {
	usage(stderr,argv[0]);
	exit(1);
    }

    h = dvb_init_nr(adapter);
    if (NULL == h) {
	fprintf(stderr,"can't init dvb\n");
	exit(1);
    }
    if (-1 == dvb_frontend_tune(h,argv[optind])) {
	fprintf(stderr,"tuning failed\n");
	exit(1);
    }

    switch (job) {
    case JOB_SIGNAL:
	for (;count > 0; count--) {
	    watch_signal(h,5);
	    fprintf(stderr,"\n");
	    dvb_frontend_tune(h,argv[optind]);
	}
	break;
    case JOB_DUMP:
	if (0 == dvb_frontend_wait_lock(h,0))
	    dump_info(h,verbose);
	break;
    }
    
    dvb_fini(h);
    return 0;
}
