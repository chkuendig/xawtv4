/*
 * handle dvb devices
 * import vdr channels.conf files
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>

#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include "grab-ng.h"
#include "parseconfig.h"
#include "dvb.h"

int dvb_debug = 1;

/* ----------------------------------------------------------------------- */
/* map vdr config file numbers to enums                                    */

static fe_bandwidth_t fe_vdr_bandwidth[] = {
    [ 0 ... 256 ] = BANDWIDTH_AUTO,
    [ 8 ]         = BANDWIDTH_8_MHZ,
    [ 7 ]         = BANDWIDTH_7_MHZ,
    [ 6 ]         = BANDWIDTH_6_MHZ,
};

static fe_code_rate_t fe_vdr_rates[] = {
    [ 0 ... 256 ] = FEC_AUTO,
    [ 12 ]        = FEC_1_2,
    [ 23 ]        = FEC_2_3,
    [ 34 ]        = FEC_3_4,
    [ 45 ]        = FEC_4_5,
    [ 56 ]        = FEC_5_6,
    [ 67 ]        = FEC_6_7,
    [ 78 ]        = FEC_7_8,
    [ 89 ]        = FEC_8_9,
};

static fe_modulation_t fe_vdr_modulation[] = {
    [ 0 ... 256 ] = QAM_AUTO,
    [  16 ]       = QAM_16,
    [  32 ]       = QAM_32,
    [  64 ]       = QAM_64,
    [ 128 ]       = QAM_128,
    [ 256 ]       = QAM_256,
};

static fe_transmit_mode_t fe_vdr_transmission[] = {
    [ 0 ... 256 ] = TRANSMISSION_MODE_AUTO,
    [ 2 ]         = TRANSMISSION_MODE_2K,
    [ 8 ]         = TRANSMISSION_MODE_8K,
};

static fe_guard_interval_t fe_vdr_guard[] = {
    [ 0 ... 256 ] = GUARD_INTERVAL_AUTO,
    [  4 ]        = GUARD_INTERVAL_1_4,
    [  8 ]        = GUARD_INTERVAL_1_8,
    [ 16 ]        = GUARD_INTERVAL_1_16,
    [ 32 ]        = GUARD_INTERVAL_1_32,
};

static fe_hierarchy_t fe_vdr_hierarchy[] = {
    [ 0 ... 256 ] = HIERARCHY_AUTO,
    [ 0 ]         = HIERARCHY_NONE,
    [ 1 ]         = HIERARCHY_1,
    [ 2 ]         = HIERARCHY_2,
    [ 4 ]         = HIERARCHY_4,
};

/* ----------------------------------------------------------------------- */
/* map enums to human-readable names                                       */

static char *fe_type[] = {
    [ FE_QPSK ] = "QPSK (dvb-s)",
    [ FE_QAM  ] = "QAM  (dvb-c)",
    [ FE_OFDM ] = "OFDM (dvb-t)",
};

static char *fe_status[] = {
    "signal",
    "carrier",
    "viterbi",
    "sync",
    "lock",
    "timeout",
    "reinit",
};

static char *fe_name_bandwidth[] = {
    [ BANDWIDTH_AUTO  ] = "auto",
    [ BANDWIDTH_8_MHZ ] = "8 MHz",
    [ BANDWIDTH_7_MHZ ] = "7 MHz",
    [ BANDWIDTH_6_MHZ ] = "6 MHz",
};

static char *fe_name_rates[] = {
    [ FEC_AUTO ] = "auto",
    [ FEC_1_2  ] = "1/2",
    [ FEC_2_3  ] = "2/3",
    [ FEC_3_4  ] = "3/4",
    [ FEC_4_5  ] = "4/5",
    [ FEC_5_6  ] = "5/6",
    [ FEC_6_7  ] = "6/7",
    [ FEC_7_8  ] = "7/8",
    [ FEC_8_9  ] = "8/9",
};

static char *fe_name_modulation[] = {
    [ QAM_AUTO ] = "auto",
    [ QAM_16   ] = "16",
    [ QAM_32   ] = "32",
    [ QAM_64   ] = "64",
    [ QAM_128  ] = "128",
    [ QAM_256  ] = "256",
};

static char *fe_name_transmission[] = {
    [ TRANSMISSION_MODE_AUTO ] = "auto",
    [ TRANSMISSION_MODE_2K   ] = "2k",
    [ TRANSMISSION_MODE_8K   ] = "8k",
};

static char *fe_name_guard[] = {
    [ GUARD_INTERVAL_AUTO ] = "auto",
    [ GUARD_INTERVAL_1_4  ] = "1/4",
    [ GUARD_INTERVAL_1_8  ] = "1/8",
    [ GUARD_INTERVAL_1_16 ] = "1/16",
    [ GUARD_INTERVAL_1_32 ] = "1/32",
};

static char *fe_name_hierarchy[] = {
    [ HIERARCHY_AUTO ] = "auto",
    [ HIERARCHY_NONE ] = "none",
    [ HIERARCHY_1 ]    = "1",
    [ HIERARCHY_2 ]    = "2",
    [ HIERARCHY_4 ]    = "3",
};

/* ----------------------------------------------------------------------- */

struct demux_filter {
    int                              fd;
    struct dmx_pes_filter_params     filter;
};

struct dvb_state {
    /* device file names */
    char                             frontend[32];
    char                             demux[32];

    /* frontend */
    int                              fd;
    struct dvb_frontend_info         info;
    struct dvb_frontend_parameters   p;
    struct dvb_frontend_parameters   last;
    
    /* demux */
    struct demux_filter              audio;
    struct demux_filter              video;
};

/* ----------------------------------------------------------------------- */
/* handle dvb frontend                                                     */

static int dvb_frontend_open(struct dvb_state *h)
{
    if (-1 != h->fd)
	return 0;

    h->fd = open(h->frontend,O_RDWR);
    if (-1 == h->fd) {
	fprintf(stderr,"dvb fe: open %s: %s\n",
		h->frontend,strerror(errno));
	return -1;
    }
    return 0;
}

int dvb_frontend_tune(struct dvb_state *h, char *name)
{
    int val;

    switch (h->info.type) {
    case FE_QPSK:
	val = cfg_get_int("dvb", name, "frequency", 0);
	h->p.frequency = val;
	val = cfg_get_int("dvb", name, "inversion", INVERSION_AUTO);
	h->p.inversion = val;

	val = cfg_get_int("dvb", name, "symbol_rate", 0);
	h->p.u.qpsk.symbol_rate = val;
	h->p.u.qpsk.fec_inner = FEC_AUTO; // FIXME 

	if (dvb_debug) {
	    fprintf(stderr,"dvb fe: tuning freq=%d, inv=%d "
		    "symbol_rate=%d fec_inner=%s\n",
		    h->p.frequency, h->p.inversion, h->p.u.qpsk.symbol_rate,
		    fe_name_rates [ h->p.u.qpsk.fec_inner ]);
	}

	break;
    case FE_QAM:
	val = cfg_get_int("dvb", name, "frequency", 0);
	h->p.frequency = val;
	val = cfg_get_int("dvb", name, "inversion", INVERSION_AUTO);
	h->p.inversion = val;

	val = cfg_get_int("dvb", name, "symbol_rate", 0);
	h->p.u.qam.symbol_rate = val;
	h->p.u.qam.fec_inner = FEC_AUTO; // FIXME
	val = cfg_get_int("dvb", name, "modulation", 0);
	h->p.u.qam.modulation = fe_vdr_modulation [ val ];

	if (dvb_debug) {
	    fprintf(stderr,"dvb fe: tuning freq=%d, inv=%d "
		    "symbol_rate=%d fec_inner=%s modulation=%s\n",
		    h->p.frequency, h->p.inversion, h->p.u.qam.symbol_rate,
		    fe_name_rates      [ h->p.u.qam.fec_inner  ],
		    fe_name_modulation [ h->p.u.qam.modulation ]);
	}

	break;
    case FE_OFDM:
	val = cfg_get_int("dvb", name, "frequency", 0);
	h->p.frequency = val * 1000;
	val = cfg_get_int("dvb", name, "inversion", INVERSION_AUTO);
	h->p.inversion = val;

	val = cfg_get_int("dvb", name, "bandwidth", BANDWIDTH_AUTO);
	h->p.u.ofdm.bandwidth = fe_vdr_bandwidth [ val ];
	val = cfg_get_int("dvb", name, "code_rate_high", 0);
	h->p.u.ofdm.code_rate_HP = fe_vdr_rates [ val ];
	val = cfg_get_int("dvb", name, "code_rate_low", 0);
	h->p.u.ofdm.code_rate_LP = fe_vdr_rates [ val ];
	val = cfg_get_int("dvb", name, "modulation", 0);
	h->p.u.ofdm.constellation = fe_vdr_modulation [ val ];
	val = cfg_get_int("dvb", name, "transmission", 0);
	h->p.u.ofdm.transmission_mode = fe_vdr_transmission [ val ];
	val= cfg_get_int("dvb", name, "guard_intervall", GUARD_INTERVAL_AUTO);
	h->p.u.ofdm.guard_interval = fe_vdr_guard [ val ];
	val = cfg_get_int("dvb", name, "hierarchy", HIERARCHY_AUTO);
	h->p.u.ofdm.hierarchy_information = fe_vdr_hierarchy [ val ];

	if (dvb_debug) {
	    fprintf(stderr,"dvb fe: tuning freq=%d, inv=%d "
		    "bandwidth=%s code_rate=[%s-%s] constellation=%s "
		    "transmission=%s guard=%s hierarchy=%s\n",
		    h->p.frequency, h->p.inversion,
		    fe_name_bandwidth    [ h->p.u.ofdm.bandwidth             ],
		    fe_name_rates        [ h->p.u.ofdm.code_rate_HP          ],
		    fe_name_rates        [ h->p.u.ofdm.code_rate_LP          ],
		    fe_name_modulation   [ h->p.u.ofdm.constellation         ],
		    fe_name_transmission [ h->p.u.ofdm.transmission_mode     ],
		    fe_name_guard        [ h->p.u.ofdm.guard_interval        ],
		    fe_name_hierarchy    [ h->p.u.ofdm.hierarchy_information ]);
	}
	break;
    }

    if (-1 == dvb_frontend_open(h))
	return -1;
    if (0 == memcmp(&h->p, &h->last, sizeof(h->last))) {
	if (dvb_frontend_is_locked(h)) {
	    /* same frequency and frontend still locked */
	    if (dvb_debug)
		fprintf(stderr,"dvb fe: skipped tuning\n");
	    return 0;
	}
    }

    if (-1 == ioctl(h->fd,FE_SET_FRONTEND,&h->p)) {
	perror("dvb fe: ioctl FE_SET_FRONTEND");
	return -1;
    }
    memcpy(&h->last, &h->p, sizeof(h->last));
    return 0;
}

void dvb_frontend_status_title(void)
{
    fprintf(stderr,"dvb fe:  biterr      snr signal  blkerr | status\n");
}

void dvb_frontend_status_print(struct dvb_state *h)
{
    fe_status_t  status  = 0;
    uint32_t     ber     = 0;
    int16_t      snr     = 0;
    int16_t      signal  = 0;
    uint32_t     ublocks = 0;
    int i;

    if (-1 == dvb_frontend_open(h))
	return;
    ioctl(h->fd, FE_READ_STATUS,             &status);
    ioctl(h->fd, FE_READ_BER,                &ber);
    ioctl(h->fd, FE_READ_SNR,                &snr);
    ioctl(h->fd, FE_READ_SIGNAL_STRENGTH,    &signal);
    ioctl(h->fd, FE_READ_UNCORRECTED_BLOCKS, &ublocks);
    
    fprintf(stderr,"dvb fe: %7d %7d %7d %7d | ",
	    ber, snr, signal, ublocks);
    for (i = 0; i < sizeof(fe_status)/sizeof(fe_status[0]); i++)
	if (status & (1 << i))
	    fprintf(stderr," %s",fe_status[i]);
    fprintf(stderr,"\n");
}

int dvb_frontend_is_locked(struct dvb_state *h)
{
    fe_status_t  status  = 0;

    if (-1 == dvb_frontend_open(h))
	return 0;
    if (-1 == ioctl(h->fd, FE_READ_STATUS, &status)) {
	perror("dvb fe: FE_READ_STATUS");
	return 0;
    }
    return (status & FE_HAS_LOCK);
}

int dvb_frontend_wait_lock(struct dvb_state *h, time_t timeout)
{
    time_t start, now;
    struct timeval tv;

    if (0 == timeout)
	timeout = 3;

    time(&start);
    time(&now);
    if (dvb_debug)
	dvb_frontend_status_title();
    while (start + timeout > now) {
	if (dvb_debug)
	    dvb_frontend_status_print(h);
	if (dvb_frontend_is_locked(h))
	    return 0;
	tv.tv_sec  = 0;
	tv.tv_usec = 250 * 1000;
	select(0,NULL,NULL,NULL,&tv);
	time(&now);
    }
    return -1;
}

void dvb_frontend_release(struct dvb_state *h)
{
    if (-1 != h->fd) {
	close(h->fd);
	h->fd = -1;
    }
}

/* ----------------------------------------------------------------------- */
/* handle dvb demux                                                        */

int dvb_demux_station_filter(struct dvb_state *h, char *name)
{
    ng_mpeg_vpid = cfg_get_int("dvb",name,"video_pid",0);
    ng_mpeg_apid = cfg_get_int("dvb",name,"audio_pid",0);
    if (dvb_debug)
	fprintf(stderr,"dvb mux: dvb ts pids for \"%s\": video=%d audio=%d\n",
		name,ng_mpeg_vpid,ng_mpeg_apid);
    
    h->video.filter.pid      = ng_mpeg_vpid;
    h->video.filter.input    = DMX_IN_FRONTEND;
    h->video.filter.output   = DMX_OUT_TS_TAP;
    h->video.filter.pes_type = DMX_PES_VIDEO;
    h->video.filter.flags    = 0;

    h->audio.filter.pid      = ng_mpeg_apid;
    h->audio.filter.input    = DMX_IN_FRONTEND;
    h->audio.filter.output   = DMX_OUT_TS_TAP;
    h->audio.filter.pes_type = DMX_PES_AUDIO;
    h->audio.filter.flags    = 0;

    if (-1 == h->video.fd) {
	h->video.fd = open(h->demux,O_RDWR);
	if (-1 == h->video.fd) {
	    fprintf(stderr,"dvb mux: [video] open %s: %s\n",
		    h->demux,strerror(errno));
	    goto oops;
	}
    }
    if (-1 == ioctl(h->video.fd,DMX_SET_PES_FILTER,&h->video.filter)) {
	fprintf(stderr,"dvb mux: [video %d] ioctl DMX_SET_PES_FILTER: %s\n",
		ng_mpeg_vpid, strerror(errno));
	goto oops;
    }

    if (-1 == h->audio.fd) {
	h->audio.fd = open(h->demux,O_RDWR);
	if (-1 == h->audio.fd) {
	    fprintf(stderr,"dvb mux: [audio] open %s: %s\n",
		    h->demux,strerror(errno));
	    goto oops;
	}
    }
    if (-1 == ioctl(h->audio.fd,DMX_SET_PES_FILTER,&h->audio.filter)) {
	fprintf(stderr,"dvb mux: [audio %d] ioctl DMX_SET_PES_FILTER: %s\n",
		ng_mpeg_apid, strerror(errno));
	goto oops;
    }

    if (-1 == ioctl(h->video.fd,DMX_START,NULL)) {
	perror("dvb mux: [video] ioctl DMX_START");
	goto oops;
    }
    if (-1 == ioctl(h->audio.fd,DMX_START,NULL)) {
	perror("dvb mux: [audio] ioctl DMX_START");
	goto oops;
    }
    return 0;

 oops:
    return -1;
}

void dvb_demux_station_release(struct dvb_state *h)
{
    if (-1 != h->audio.fd) {
	ioctl(h->audio.fd,DMX_STOP,NULL);
	close(h->audio.fd);
	h->audio.fd = -1;
    }
    if (-1 != h->video.fd) {
	ioctl(h->video.fd,DMX_STOP,NULL);
	close(h->video.fd);
	h->video.fd = -1;
    }
}

int dvb_demux_req_section(struct dvb_state *h, int pid, int sec, int oneshot)
{
    struct dmx_sct_filter_params filter;
    int fd = -1;
    
    memset(&filter,0,sizeof(filter));
    filter.pid              = pid;
    filter.filter.filter[0] = sec;
    filter.filter.mask[0]   = 0xff;
    filter.timeout          = 10 * 1000;
    filter.flags            = DMX_IMMEDIATE_START;
    if (oneshot)
	filter.flags       |= DMX_ONESHOT;

    fd = open(h->demux, O_RDWR);
    if (-1 == fd) {
	fprintf(stderr,"dvb mux: [pid %d] open %s: %s\n",
		pid, h->demux, strerror(errno));
	goto oops;
    }
    if (-1 == ioctl(fd, DMX_SET_FILTER, &filter)) {
	fprintf(stderr,"dvb mux: [pid %d] ioctl DMX_SET_PES_FILTER: %s\n",
		pid, strerror(errno));
	goto oops;
    }
    return fd;

 oops:
    if (-1 != fd)
	close(fd);
    return -1;
}

int dvb_demux_get_section(int *fd, unsigned char *buf, int len, int oneshot)
{
    int rc;
    
    memset(buf,0,len);
    if ((rc = read(*fd, buf, len)) < 0)
	fprintf(stderr,"dvb mux: read: %s\n", strerror(errno));
    if (oneshot) {
	close(*fd);
	*fd = -1;
    }
    return rc;
}

/* ----------------------------------------------------------------------- */
/* open/close/tune dvb devices                                             */

void dvb_fini(struct dvb_state *h)
{
    dvb_frontend_release(h);
    dvb_demux_station_release(h);
    free(h);
}

struct dvb_state* dvb_init(char *adapter)
{
    struct dvb_state *h;

    h = malloc(sizeof(*h));
    if (NULL == h)
	goto oops;
    memset(h,0,sizeof(*h));
    h->fd       = -1;
    h->audio.fd = -1;
    h->video.fd = -1;

    snprintf(h->frontend, sizeof(h->frontend),"%s/frontend0", adapter);
    snprintf(h->demux,    sizeof(h->demux),   "%s/demux0",    adapter);

    h->fd = open(h->frontend,O_RDWR);
    if (-1 == h->fd) {
	fprintf(stderr,"dvb fe: open %s: %s\n",
		h->frontend,strerror(errno));
	goto oops;
    }

    if (-1 == ioctl(h->fd, FE_GET_INFO, &h->info)) {
	perror("dvb fe: ioctl FE_GET_INFO");
	goto oops;
    }
    if (dvb_debug)
	fprintf(stderr,"dvb fe: %s: %s\n",
		fe_type[h->info.type], h->info.name);

    dvb_frontend_release(h);
    return h;

 oops:
    if (h)
	dvb_fini(h);
    return NULL;
}

struct dvb_state* dvb_init_nr(int adapter)
{
    char path[32];

    snprintf(path,sizeof(path),"/dev/dvb/adapter%d",adapter);
    return dvb_init(path);
}

int dvb_tune(struct dvb_state *h, char *name)
{
    if (0 != dvb_frontend_tune(h,name))
	return -1;
    if (0 != dvb_frontend_wait_lock(h,0))
	return -1;
    if (0 != dvb_demux_station_filter(h,name))
	return -1;
    dvb_frontend_release(h);
    return 0;
}

struct ng_devinfo* dvb_probe(int debug)
{
    struct dvb_frontend_info feinfo;
    char device[32];
    struct ng_devinfo *info = NULL;
    int i,n,fd;

    n = 0;
    for (i = 0; i < 4; i++) {
	snprintf(device,sizeof(device),"/dev/dvb/adapter%d/frontend0",i);
	fd = ng_chardev_open(device, O_RDONLY | O_NONBLOCK, 250, debug);
	if (-1 == fd)
	    continue;
	if (-1 == ioctl(fd, FE_GET_INFO, &feinfo)) {
	    if (debug)
		perror("ioctl FE_GET_INFO");
	    close(fd);
	    continue;
	}
	info = realloc(info,sizeof(*info) * (n+2));
	memset(info+n,0,sizeof(*info)*2);
	snprintf(info[n].device,sizeof(info[n].device),
		 "/dev/dvb/adapter%d",i);
	strcpy(info[n].name, feinfo.name);
	close(fd);
	n++;
    }
    return info;
}

char* dvb_devname(struct dvb_state *h)
{
    return h->info.name;
}

/* ----------------------------------------------------------------------- */
/* get infos about the transport stream                                    */

int dvb_get_transponder_info(struct dvb_state *dvb,
			     struct psi_info *info,
			     int names, int verbose)
{
    unsigned char buf[4096];
    struct psi_program *pr;
    int i;

    memset(info, 0, sizeof(*info));
    if (names)
	info->sdt_fd = dvb_demux_req_section(dvb, 0x11, 0x42, 1);
    info->pat_fd = dvb_demux_req_section(dvb, 0x00, 0x00, 1);

    /* program association table */
    if (dvb_demux_get_section(&info->pat_fd, buf, sizeof(buf), 1) < 0)
	goto oops;
    mpeg_parse_psi_pat(info, buf, verbose);
    
    /* program maps */
    for (i = 0; i < PSI_PROGS; i++) {
	pr = info->progs+i;
	if (0 == pr->p_pid)
	    continue;
	pr->pmt_fd = dvb_demux_req_section(dvb, pr->p_pid, 2, 1);
    }
    for (i = 0; i < PSI_PROGS; i++) {
	pr = info->progs+i;
	if (0 == pr->p_pid)
	    continue;
	if (dvb_demux_get_section(&pr->pmt_fd, buf, sizeof(buf), 1) < 0)
	    goto oops;
	mpeg_parse_psi_pmt(pr, buf, verbose);
    }

    /* service descriptor */
    if (names) {
	if (dvb_demux_get_section(&info->sdt_fd, buf, sizeof(buf), 1) < 0)
	    goto oops;
	mpeg_parse_psi_sdt(info, buf, verbose);
    }

 oops:
    return -1;
}

void dvb_print_program_info(char *prefix, struct psi_program *pr)
{
    if (prefix)
	fprintf(stderr,"%s:  ",prefix);
    fprintf(stderr,"pmt %d",pr->p_pid);
    if (pr->v_pid)
	fprintf(stderr,"  video: %d",pr->v_pid);
    if (pr->a_pid)
	fprintf(stderr,"  audio: %d",pr->a_pid);
    if (pr->t_pid)
	fprintf(stderr,"  teletext: %d",pr->t_pid);
    if (pr->net[0] != 0)
	fprintf(stderr,"  net: %s", pr->net);
    if (pr->name[0] != 0)
	fprintf(stderr,"  [%s]", pr->name);
    fprintf(stderr,"\n");
}

void dvb_print_transponder_info(struct psi_info *info)
{
    struct psi_program *pr;
    int i;
    
    for (i = 0; i < PSI_PROGS; i++) {
	pr = info->progs+i;
	if (0 == pr->p_pid)
	    continue;
	dvb_print_program_info(NULL, pr);
    }
}

/* ----------------------------------------------------------------------- */
/* parse /etc/vdr/channels.conf                                            */
  
static int vdr_parse(char *domain, FILE *fp)
{
    static char *names[13] = {
	NULL, "frequency", NULL, "source", "symbol_rate",
	"video_pid", "audio_pid", "teletext_pid", "conditional_access",
	"service_id", "network_id", "ts_id", "radio_id"
    };
    static char *params[32] = {
	[ 'B' & 31 ] = "bandwidth",
	[ 'C' & 31 ] = "code_rate_high",
	[ 'D' & 31 ] = "code_rate_low",
	[ 'G' & 31 ] = "guard_interval",
	[ 'I' & 31 ] = "inversion",
	[ 'M' & 31 ] = "modulation",
	[ 'T' & 31 ] = "transmission",
	[ 'Y' & 31 ] = "hierarchy",
    };
    char line[256];
    char *fields[13];
    char *name,value[32];
    int i,j;
    
    while (NULL != fgets(line,sizeof(line),fp)) {
	for (i = 0, fields[i] = strtok(line,":\r\n");
	     i < 13 && NULL != fields[i];
	     i++, fields[i] = strtok(NULL,":\r\n"))
	    /* nothing */;
	if (13 != i)
	    continue;

	/* save all but params */
	for (i = 0; i < 13; i++) {
	    if (NULL == names[i])
		continue;
	    cfg_set_str(domain, fields[0], names[i], fields[i]);
	}

	/* parse params */
	i = 0;
	while (isalpha(fields[2][i])) {
	    switch (fields[2][i] & 31) {
	    case 'h' & 31:
		cfg_set_str(domain, fields[0], "polarization", "h");
		i++;
		break;
	    case 'v' & 31:
		cfg_set_str(domain, fields[0], "polarization", "v");
		i++;
		break;
	    default:
		name = params[fields[2][i] & 31];
		for (j = 0; i < 32 && isdigit(fields[2][i+j+1]); j++)
		    value[j] = fields[2][i+j+1];
		value[j] = 0;
		cfg_set_str(domain, fields[0], name, value);
		i += j+1;
		break;
	    }
	}
    }
    return 0;
}

static void __init vdr_init(void)
{
    FILE *fp;

    fp = fopen("/etc/vdr/channels.conf","r");
    if (NULL == fp)
	return;
    vdr_parse("dvb",fp);
    fclose(fp);
}
