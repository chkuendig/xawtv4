#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include "grab-ng.h"
#include "dvb-tuning.h"

static int verbose = 0;

/* ----------------------------------------------------------------------- */

static int set_demux_filter(int fd, int pid)
{
    struct dmx_pes_filter_params filter;

    memset(&filter, 0, sizeof(filter));
    filter.pid      = pid;
    filter.input    = DMX_IN_FRONTEND;
    filter.output   = DMX_OUT_TAP;
    filter.pes_type = DMX_PES_TELETEXT;
    filter.flags    = DMX_IMMEDIATE_START;
    if (0 != ioctl(fd, DMX_SET_PES_FILTER, &filter)) {
	perror("ioctl DMX_SET_PES_FILTER");
	return -1;
    }
    return 0;
}

static int add_dvr_filter(char *demux, int pid, int nr)
{
    struct dmx_pes_filter_params filter;
    int fd;

    fd = open(demux,O_RDONLY);
    if (-1 == fd) {
	fprintf(stderr,"open %s: %s",demux,strerror(errno));
	return -1;
    }
    memset(&filter, 0, sizeof(filter));
    filter.pid      = pid;
    filter.input    = DMX_IN_FRONTEND;
    filter.output   = DMX_OUT_TS_TAP;
#if 1
    filter.pes_type = nr;
#else
    filter.pes_type = DMX_PES_TELETEXT;
#endif
    filter.flags    = DMX_IMMEDIATE_START;
    if (0 != ioctl(fd, DMX_SET_PES_FILTER, &filter)) {
	perror("ioctl DMX_SET_PES_FILTER");
	return -1;
    }
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
	    "record vbi streams from dvb cards\n"
	    "\n"
	    "usage: %s [ options ] station\n"
	    "options:\n"
	    "  -h          print this help text\n"
	    "  -v          be verbose\n"
	    "  -t          write transport stream\n"
	    "  -p <pid>    write pes with specified pid\n"
	    "  -s <sec>    how long to record\n",
	    name);
};

static int write_stream(int stream, char *filename, int sec)
{
    time_t start, now;
    char buffer[4096];
    int file;
    int count;
    int rc;
    
    file = open(filename, O_WRONLY | O_TRUNC | O_CREAT, 0666);
    if (-1 == file) {
	fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	exit(1);
    }

    count = 0;
    start = time(NULL);
    if (-1 != stream) {
	for (;;) {
	    rc = read(stream, buffer, sizeof(buffer));
	    switch (rc) {
	    case -1:
		perror("read");
		exit(1);
	    case 0:
		fprintf(stderr,"EOF\n");
		exit(1);
	    default:
		write(file, buffer, rc);
		count += rc;
		break;
	    }
	    now = time(NULL);
	    fprintf(stderr,"  %02ld:%02ld - %8d\r",
		    (now - start) / 60,
		    (now - start) % 60,
		    count);
	    if (-1 != sec && now - start >= sec)
		break;
	}
	fprintf(stderr,"\n");
    }
    close(file);
    return 0;
}

int main(int argc, char *argv[])
{
    struct dvb_state *dvb;
    struct psi_info *info;
    struct list_head *item;
    struct psi_program *pr;
    char demux[32];
    char dvr[32];
    char filename[32];
    int adapter = 0;
    int ts = 0;
    int pes = 0;
    int pes_pid = 0;
    int stream = -1;
    int sec = -1;
    int c,nr;
    
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hvtp?s:")))
	    break;
	switch (c) {
	case 't':
	    ts=1;
	    break;
	case 'p':
	    pes = 1;
	    if (optarg)
		pes_pid = atoi(optarg);
	    break;
	case 's':
	    sec = atoi(optarg);
	    break;
	case 'v':
	    verbose++;
	    break;
	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }

    dvb = dvb_init_nr(adapter);
    if (NULL == dvb) {
	fprintf(stderr,"can't init dvb\n");
	exit(1);
    }
    if (!dvb_frontend_is_locked(dvb)) {
	fprintf(stderr,"dvb frontend not locked\n");
	exit(1);
    }

    fprintf(stderr,"scanning ...\n");
    info = psi_info_alloc();
    dvb_get_transponder_info(dvb,info,1,verbose);
    fprintf(stderr,"tsid %d, teletext streams:\n",info->tsid);
    list_for_each(item,&info->programs) {
        pr = list_entry(item, struct psi_program, next);
	if (0 == pr->t_pid)
	    continue;
	fprintf(stderr,"  %-20s  pid=%d  pnr=%d\n",
		pr->name, pr->t_pid, pr->pnr);
    }
    fprintf(stderr,"\n");

    snprintf(demux, sizeof(demux), "/dev/dvb/adapter%d/demux0", adapter);
    snprintf(dvr,   sizeof(dvr),   "/dev/dvb/adapter%d/dvr0",   adapter);
    
    if (pes) {
	/* single stream as PES */
	stream = open(demux,O_RDONLY);
	if (-1 == stream) {
	    fprintf(stderr,"open %s: %s\n",demux,strerror(errno));
	    exit(1);
	}

	list_for_each(item,&info->programs) {
	    pr = list_entry(item, struct psi_program, next);
	    if (0 == pr->t_pid)
		continue;
	    if (pes_pid  &&  pes_pid != pr->t_pid)
		continue;
	    if (-1 == set_demux_filter(stream,pr->t_pid))
		exit(1);
	    snprintf(filename, sizeof(filename), "%d-%d.pes",
		     info->tsid, pr->t_pid);
	    fprintf(stderr,"recording pes (pid %d) to %s\n",
		    pr->t_pid,filename);
	    write_stream(stream,filename,sec);
	}
	close(stream);
	stream = -1;
    }
	
    if (ts) {
	/* multiple streams as TS */
	stream = open(dvr,O_RDONLY);
	if (-1 == stream) {
	    fprintf(stderr,"open %s: %s\n",dvr,strerror(errno));
	    exit(1);
	}

	snprintf(filename, sizeof(filename), "%d.ts", info->tsid);
	fprintf(stderr,"recording ts to %s\n",filename);

	nr = 0;
	if (0 == add_dvr_filter(demux, 0x00, nr++))
	    fprintf(stderr, "  added pid %d [pat]\n", 0x00);
	if (0 == add_dvr_filter(demux, 0x11, nr++))
	    fprintf(stderr, "  added pid %d [sdt]\n", 0x11);
	list_for_each(item,&info->programs) {
	    pr = list_entry(item, struct psi_program, next);
	    if (0 == add_dvr_filter(demux, 0x11, nr++))
		fprintf(stderr, "  added pid %d [pmt]\n", pr->p_pid);
	    if (0 == pr->t_pid)
		continue;
	    if (0 == add_dvr_filter(demux, pr->t_pid, nr++))
		fprintf(stderr, "  added pid %d tt %s\n", pr->t_pid, pr->name);
	}
	write_stream(stream,filename,sec);
    }

    psi_info_free(info);
    dvb_fini(dvb);
    return 0;
}
