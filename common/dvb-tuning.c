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
#include "struct-dump.h"
#include "struct-dvb.h"
#include "dvb-tuning.h"

int dvb_debug = 0;

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

struct demux_filter {
    int                              fd;
    struct dmx_pes_filter_params     filter;
};

struct dvb_state {
    /* device file names */
    char                             frontend[32];
    char                             demux[32];

    /* frontend */
    int                              fdro;
    int                              fdwr;
    struct dvb_frontend_info         info;
    struct dvb_frontend_parameters   p;
    struct dvb_frontend_parameters   plast;
    
    /* demux */
    struct demux_filter              audio;
    struct demux_filter              video;
};

/* ----------------------------------------------------------------------- */
/* handle diseqc                                                           */

static int
xioctl(int fd, int cmd, void *arg, int mayfail)
{
    int rc;

    rc = ioctl(fd,cmd,arg);
    if (0 == rc && !dvb_debug)
	return rc;
    if (mayfail && errno == mayfail && !dvb_debug)
	return rc;
    print_ioctl(stderr,ioctls_dvb,"dvb ioctl: ",cmd,arg);
    fprintf(stderr,": %s\n",(rc == 0) ? "ok" : strerror(errno));
    return rc;
}

static int exec_diseqc(int fd, char *action)
{
    struct dvb_diseqc_master_cmd cmd;
    int wait,len,done;
    int c0,c1,c2,c3;

    for (done = 0; !done;) {
	switch (*action) {
	case '\0':
	    done = 1;
	    break;
	case ' ':
	case '\t':
	    /* ignore */
	    break;
	case 't':
	    xioctl(fd, FE_SET_TONE, (void*)SEC_TONE_OFF, 0);
	    break;
	case 'T':
	    xioctl(fd, FE_SET_TONE, (void*)SEC_TONE_ON, 0);
	    break;
	case 'v':
	    xioctl(fd, FE_SET_VOLTAGE, (void*)SEC_VOLTAGE_13, 0);
	    break;
	case 'V':
	    xioctl(fd, FE_SET_VOLTAGE, (void*)SEC_VOLTAGE_18, 0);
	    break;
	case 'a':
	case 'A':
	    xioctl(fd, FE_DISEQC_SEND_BURST, (void*)SEC_MINI_A, 0);
	    break;
	case 'b':
	case 'B':
	    xioctl(fd, FE_DISEQC_SEND_BURST, (void*)SEC_MINI_B, 0);
	    break;
	case '[':
	    if (4 == sscanf(action+1,"%x %x %x %x%n",
			    &c0, &c1, &c2, &c3, &len)) {
		cmd.msg[0] = c0;
		cmd.msg[1] = c1;
		cmd.msg[2] = c2;
		cmd.msg[3] = c3;
		cmd.msg_len = 4;
		if (dvb_debug)
		    fprintf(stderr, "dvb: cmd %x %x %x %x\n", c0, c1, c2, c3);
		xioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &cmd, 0);
		action += len+1;
	    }
	    break;
	case 'w':
	case 'W':
	    if (1 == sscanf(action+1,"%d%n",&wait,&len)) {
		if (dvb_debug)
		    fprintf(stderr, "dvb: wait %d msec\n",wait);
		usleep(wait*1000);
		action += len;
	    }
	    break;
	default:
	    fprintf(stderr,"  *** Huh? '%c'?",*action);
	    done=1;
	    break;
	}
	action++;
    }
    return 0;
}

static char *find_diseqc(char *name)
{
    char *source = cfg_get_str("dvb", name, "source");
    char *pol    = cfg_get_str("dvb", name, "polarization");
    int  freq    = cfg_get_int("dvb", name, "frequency",0);
    char *list,*check;

    if (dvb_debug)
	fprintf(stderr,"diseqc lookup: source=\"%s\" pol=\"%s\" freq=%d\n",
		source,pol,freq);
    cfg_sections_for_each("diseqc",list) {
	check = cfg_get_str("diseqc", list, "source");
	if (!check || !source || 0 != (strcasecmp(check,source)))
	    continue;
	check = cfg_get_str("diseqc", list, "polarization");
	if (!check || !pol || 0 != (strcasecmp(check,pol)))
	    continue;
	if (freq < cfg_get_int("diseqc", list, "lsof", 0))
	    return list;
    }
    return NULL;
}

/* ----------------------------------------------------------------------- */
/* handle dvb frontend                                                     */

static int dvb_frontend_open(struct dvb_state *h, int write)
{
    int *fd = write ? &h->fdwr : &h->fdro;

    if (-1 != *fd)
	return 0;

    *fd = open(h->frontend, (write ? O_RDWR : O_RDONLY) | O_NONBLOCK);
    if (-1 == *fd) {
	fprintf(stderr,"dvb fe: open %s: %s\n",
		h->frontend,strerror(errno));
	return -1;
    }
    return 0;
}

void dvb_frontend_release(struct dvb_state *h, int write)
{
    int *fd = write ? &h->fdwr : &h->fdro;

    if (-1 != *fd) {
	close(*fd);
	*fd = -1;
    }
}

int dvb_frontend_tune(struct dvb_state *h, char *name)
{
    char *diseqc;
    char *action;
    int lof;
    int val;

    if (-1 == dvb_frontend_open(h,1))
	return -1;

    switch (h->info.type) {
    case FE_QPSK:
	/*
	 * DVB-S
	 *   - kernel API uses kHz here.
	 *   - /etc/vdr/channel.conf + diseqc.conf use MHz
	 */
	diseqc = find_diseqc(name);
	if (!diseqc) {
	    fprintf(stderr,"no diseqc info for \"%s\"\n",name);
	    return -1;
	}
	action = cfg_get_str("diseqc", diseqc, "action");
	if (dvb_debug)
	    fprintf(stderr,"diseqc action: \"%s\"\n",action);
	exec_diseqc(h->fdwr,action);

	lof = cfg_get_int("diseqc", diseqc, "lof", 0);
	val = cfg_get_int("dvb", name, "frequency", 0);
	h->p.frequency = val - lof;
	val = cfg_get_int("dvb", name, "inversion", INVERSION_AUTO);
	h->p.inversion = val;

	val = cfg_get_int("dvb", name, "symbol_rate", 0);
	h->p.u.qpsk.symbol_rate = val * 1000;
	h->p.u.qpsk.fec_inner = FEC_AUTO; // FIXME 

	if (dvb_debug) {
	    fprintf(stderr,"dvb fe: tuning freq=%d+%d MHz, inv=%s "
		    "symbol_rate=%d fec_inner=%s\n",
		    lof, h->p.frequency,
		    dvb_fe_inversion [ h->p.inversion ],
		    h->p.u.qpsk.symbol_rate,
		    dvb_fe_rates [ h->p.u.qpsk.fec_inner ]);
	}
	h->p.frequency *= 1000; // MHz => kHz
	break;

    case FE_QAM:
	/*
	 * DVB-C
	 *   - kernel API uses Hz here.
	 *   - /etc/vdr/channel.conf allows Hz, kHz and MHz
	 */
	val = cfg_get_int("dvb", name, "frequency", 0);
	h->p.frequency = val;
	while (h->p.frequency && h->p.frequency < 1000000)
	    h->p.frequency *= 1000;
	val = cfg_get_int("dvb", name, "inversion", INVERSION_AUTO);
	h->p.inversion = val;
	
	val = cfg_get_int("dvb", name, "symbol_rate", 0);
	h->p.u.qam.symbol_rate = val;
	h->p.u.qam.fec_inner = FEC_AUTO; // FIXME
	val = cfg_get_int("dvb", name, "modulation", 0);
	h->p.u.qam.modulation = fe_vdr_modulation [ val ];

	if (dvb_debug) {
	    fprintf(stderr,"dvb fe: tuning freq=%d Hz, inv=%d "
		    "symbol_rate=%d fec_inner=%s modulation=%s\n",
		    h->p.frequency, h->p.inversion, h->p.u.qam.symbol_rate,
		    dvb_fe_rates      [ h->p.u.qam.fec_inner  ],
		    dvb_fe_modulation [ h->p.u.qam.modulation ]);
	}
	break;

    case FE_OFDM:
	/* DVB-T  --  same as DVB-C */
	val = cfg_get_int("dvb", name, "frequency", 0);
	h->p.frequency = val * 1000;
	while (h->p.frequency && h->p.frequency < 1000000)
	    h->p.frequency *= 1000;
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
	    fprintf(stderr,"dvb fe: tuning freq=%d Hz, inv=%d "
		    "bandwidth=%s code_rate=[%s-%s] constellation=%s "
		    "transmission=%s guard=%s hierarchy=%s\n",
		    h->p.frequency, h->p.inversion,
		    dvb_fe_bandwidth    [ h->p.u.ofdm.bandwidth             ],
		    dvb_fe_rates        [ h->p.u.ofdm.code_rate_HP          ],
		    dvb_fe_rates        [ h->p.u.ofdm.code_rate_LP          ],
		    dvb_fe_modulation   [ h->p.u.ofdm.constellation         ],
		    dvb_fe_transmission [ h->p.u.ofdm.transmission_mode     ],
		    dvb_fe_guard        [ h->p.u.ofdm.guard_interval        ],
		    dvb_fe_hierarchy    [ h->p.u.ofdm.hierarchy_information ]);
	}
	break;
    }

    if (0 == memcmp(&h->p, &h->plast, sizeof(h->plast))) {
	if (dvb_frontend_is_locked(h)) {
	    /* same frequency and frontend still locked */
	    if (dvb_debug)
		fprintf(stderr,"dvb fe: skipped tuning\n");
	    return 0;
	}
    }

    if (-1 == xioctl(h->fdwr,FE_SET_FRONTEND,&h->p, 0))
	return -1;
    memcpy(&h->plast, &h->p, sizeof(h->plast));
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

    ioctl(h->fdro, FE_READ_STATUS,             &status);
    ioctl(h->fdro, FE_READ_BER,                &ber);
    ioctl(h->fdro, FE_READ_SNR,                &snr);
    ioctl(h->fdro, FE_READ_SIGNAL_STRENGTH,    &signal);
    ioctl(h->fdro, FE_READ_UNCORRECTED_BLOCKS, &ublocks);
    
    fprintf(stderr,"dvb fe: %7d %7d %7d %7d | ",
	    ber, snr, signal, ublocks);
    for (i = 0; i < 32; i++)
	if (status & (1 << i))
	    fprintf(stderr," %s",dvb_fe_status[i]);
    fprintf(stderr,"\n");
}

int dvb_frontend_is_locked(struct dvb_state *h)
{
    fe_status_t  status  = 0;

    if (-1 == ioctl(h->fdro, FE_READ_STATUS, &status)) {
	perror("dvb fe: ioctl FE_READ_STATUS");
	return 0;
    }
    return (status & FE_HAS_LOCK);
}

int dvb_frontend_wait_lock(struct dvb_state *h, time_t timeout)
{
    time_t start, now;
    struct timeval tv;

    if (0 == timeout)
	timeout = 10;

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
    if (-1 == xioctl(h->video.fd,DMX_SET_PES_FILTER,&h->video.filter,0)) {
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
    if (-1 == xioctl(h->audio.fd,DMX_SET_PES_FILTER,&h->audio.filter,0)) {
	fprintf(stderr,"dvb mux: [audio %d] ioctl DMX_SET_PES_FILTER: %s\n",
		ng_mpeg_apid, strerror(errno));
	goto oops;
    }

    if (-1 == xioctl(h->video.fd,DMX_START,NULL,0)) {
	perror("dvb mux: [video] ioctl DMX_START");
	goto oops;
    }
    if (-1 == xioctl(h->audio.fd,DMX_START,NULL,0)) {
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
	xioctl(h->audio.fd,DMX_STOP,NULL,0);
	close(h->audio.fd);
	h->audio.fd = -1;
    }
    if (-1 != h->video.fd) {
	xioctl(h->video.fd,DMX_STOP,NULL,0);
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
    filter.timeout          = 60 * 1000;
    filter.flags            = DMX_IMMEDIATE_START;
    if (oneshot)
	filter.flags       |= DMX_ONESHOT;

    fd = open(h->demux, O_RDWR);
    if (-1 == fd) {
	fprintf(stderr,"dvb mux: [pid %d] open %s: %s\n",
		pid, h->demux, strerror(errno));
	goto oops;
    }
    if (-1 == xioctl(fd, DMX_SET_FILTER, &filter, 0)) {
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
    dvb_frontend_release(h,1);
    dvb_frontend_release(h,0);
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
    h->fdro     = -1;
    h->fdwr     = -1;
    h->audio.fd = -1;
    h->video.fd = -1;

    snprintf(h->frontend, sizeof(h->frontend),"%s/frontend0", adapter);
    snprintf(h->demux,    sizeof(h->demux),   "%s/demux0",    adapter);

    if (0 != dvb_frontend_open(h,0))
	goto oops;

    if (-1 == xioctl(h->fdro, FE_GET_INFO, &h->info, 0)) {
	perror("dvb fe: ioctl FE_GET_INFO");
	goto oops;
    }
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
    if (0 != dvb_frontend_tune(h,name)) {
	fprintf(stderr,"dvb: frontend tuning failed\n");
	return -1;
    }
    if (0 != dvb_frontend_wait_lock(h,0)) {
	fprintf(stderr,"dvb: frontend doesn't lock\n");
	return -1;
    }
    if (0 != dvb_demux_station_filter(h,name)) {
	fprintf(stderr,"dvb: pid filter setup failed\n");
	return -1;
    }
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
    struct list_head   *item;
    struct psi_program *pr;
    int sdt, pat;

    if (names)
	sdt = dvb_demux_req_section(dvb, 0x11, 0x42, 1);
    pat = dvb_demux_req_section(dvb, 0x00, 0x00, 1);

    /* program association table */
    if (dvb_demux_get_section(&pat, buf, sizeof(buf), 1) < 0)
	goto oops;
    mpeg_parse_psi_pat(info, buf, verbose);
    
    /* program maps */
    list_for_each(item,&info->programs) {
        pr = list_entry(item, struct psi_program, next);
	pr->fd = dvb_demux_req_section(dvb, pr->p_pid, 2, 1);
    }
    list_for_each(item,&info->programs) {
        pr = list_entry(item, struct psi_program, next);
	if (dvb_demux_get_section(&pr->fd, buf, sizeof(buf), 1) < 0)
	    goto oops;
	mpeg_parse_psi_pmt(pr, buf, verbose);
    }

    /* service descriptor */
    if (names) {
	if (dvb_demux_get_section(&sdt, buf, sizeof(buf), 1) < 0)
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
    struct list_head   *item;
    struct psi_program *pr;
    
    list_for_each(item,&info->programs) {
        pr = list_entry(item, struct psi_program, next);
	dvb_print_program_info(NULL, pr);
    }
}

/* ----------------------------------------------------------------------- */
/* parse /etc/vdr/channels.conf                                            */
  
void vdr_import_stations(void)
{
    char line[256],station[64],group[64];
    int number;
    FILE *fp;

    fp = fopen("/etc/vdr/channels.conf","r");
    if (NULL == fp)
	return;

    group[0] = 0;
    while (NULL != (fgets(line,sizeof(line),fp))) {
	switch (line[0]) {
	case ':':
	    /* group */
	    if (2 != sscanf(line,":@%d %64[^:\n]",&number,group))
		sscanf(line,":%64[^:\n]",group);
	    break;
	default:
	    /* station */
	    if (1 == sscanf(line,"%64[^:\n]:",station)) {
		cfg_set_str("stations",station,"vdr",station);
		if (0 != group[0])
		    cfg_set_str("stations",station,"group",group);
	    }
	    break;
	}
    }
}

static int parse_vdr_channels(char *domain, FILE *fp)
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

static int parse_vdr_diseqc(char *domain, FILE *fp)
{
    char line[256];
    char source[16];
    char lsof[16];
    char pol[2];
    char lof[16];
    char action[128];
    char section[16];
    int i = 0;
    
    while (NULL != fgets(line,sizeof(line),fp)) {
	if (line[0] == '#')
	    continue;
	if (5 != sscanf(line,"%15s %15[0-9] %1s %15[0-9] %[][ a-fA-F0-9tTvVtTwW]\n",
			source, lsof, pol, lof, action))
	    continue;
	sprintf(section,"diseqc-%d",i++);
	cfg_set_str(domain, section, "source",       source);
	cfg_set_str(domain, section, "polarization", pol);
	cfg_set_str(domain, section, "lsof",         lsof);
	cfg_set_str(domain, section, "lof",          lof);
	cfg_set_str(domain, section, "action",       action);
    }
    return 0;
}

static void __init vdr_init(void)
{
    FILE *fp;

    fp = fopen("/etc/vdr/channels.conf","r");
    if (NULL == fp)
	return;
    parse_vdr_channels("dvb",fp);
    fclose(fp);

    fp = fopen("/etc/vdr/diseqc.conf","r");
    if (NULL == fp)
	return;
    parse_vdr_diseqc("diseqc",fp);
    fclose(fp);
}
