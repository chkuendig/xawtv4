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

#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include "grab-ng.h"
#include "parseconfig.h"
#include "struct-dump.h"
#include "struct-dvb.h"
#include "dvb-tuning.h"

int dvb_debug = 0;

/* maintain current state for these ... */
char *dvb_src   = NULL;
char *dvb_lnb   = NULL;
char *dvb_sat   = NULL;
int  dvb_inv    = INVERSION_AUTO;

/* ----------------------------------------------------------------------- */
/* map vdr config file numbers to enums                                    */

#define VDR_MAX 999

static fe_bandwidth_t fe_vdr_bandwidth[] = {
    [ 0 ... VDR_MAX ] = BANDWIDTH_AUTO,
    [ 8 ]             = BANDWIDTH_8_MHZ,
    [ 7 ]             = BANDWIDTH_7_MHZ,
    [ 6 ]             = BANDWIDTH_6_MHZ,
};

static fe_code_rate_t fe_vdr_rates[] = {
    [ 0 ... VDR_MAX ] = FEC_AUTO,
    [ 12 ]            = FEC_1_2,
    [ 23 ]            = FEC_2_3,
    [ 34 ]            = FEC_3_4,
    [ 45 ]            = FEC_4_5,
    [ 56 ]            = FEC_5_6,
    [ 67 ]            = FEC_6_7,
    [ 78 ]            = FEC_7_8,
    [ 89 ]            = FEC_8_9,
};

static fe_modulation_t fe_vdr_modulation[] = {
    [ 0 ... VDR_MAX ] = QAM_AUTO,
    [  16 ]           = QAM_16,
    [  32 ]           = QAM_32,
    [  64 ]           = QAM_64,
    [ 128 ]           = QAM_128,
    [ 256 ]           = QAM_256,
};

static fe_transmit_mode_t fe_vdr_transmission[] = {
    [ 0 ... VDR_MAX ] = TRANSMISSION_MODE_AUTO,
    [ 2 ]             = TRANSMISSION_MODE_2K,
    [ 8 ]             = TRANSMISSION_MODE_8K,
};

static fe_guard_interval_t fe_vdr_guard[] = {
    [ 0 ... VDR_MAX ] = GUARD_INTERVAL_AUTO,
    [  4 ]            = GUARD_INTERVAL_1_4,
    [  8 ]            = GUARD_INTERVAL_1_8,
    [ 16 ]            = GUARD_INTERVAL_1_16,
    [ 32 ]            = GUARD_INTERVAL_1_32,
};

static fe_hierarchy_t fe_vdr_hierarchy[] = {
    [ 0 ... VDR_MAX ] = HIERARCHY_AUTO,
    [ 0 ]             = HIERARCHY_NONE,
    [ 1 ]             = HIERARCHY_1,
    [ 2 ]             = HIERARCHY_2,
    [ 4 ]             = HIERARCHY_4,
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

static int exec_vdr_diseqc(int fd, char *action)
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

static char *find_vdr_diseqc(char *domain, char *section)
{
    char *source = cfg_get_str(domain, section, "source");
    char *pol    = cfg_get_str(domain, section, "polarization");
    int  freq    = cfg_get_int(domain, section, "frequency",0);
    char *list,*check;

    if (dvb_debug)
	fprintf(stderr,"diseqc lookup: source=\"%s\" pol=\"%s\" freq=%d\n",
		source,pol,freq);
    cfg_sections_for_each("vdr-diseqc",list) {
	check = cfg_get_str("vdr-diseqc", list, "source");
	if (!check || !source || 0 != (strcasecmp(check,source)))
	    continue;
	check = cfg_get_str("vdr-diseqc", list, "polarization");
	if (!check || !pol || 0 != (strcasecmp(check,pol)))
	    continue;
	if (freq < cfg_get_int("vdr-diseqc", list, "lsof", 0))
	    return list;
    }
    return NULL;
}

static int sat_switch(int fd, char *domain, char *section)
{
    char *pol = cfg_get_str(domain, section, "polarization");
    int freq  = cfg_get_int(domain, section, "frequency", 0);
    int lo,hi,sw,lof,nr;
    fe_sec_tone_mode_t tone;
    fe_sec_voltage_t volt;
    
    dvb_lnb = cfg_get_str(domain, section, "lnb");
    if (dvb_lnb)
	dvb_lnb = strdup(dvb_lnb);

    dvb_sat = cfg_get_str(domain, section, "sat");
    if (dvb_sat)
	dvb_sat = strdup(dvb_sat);

    if (NULL == dvb_lnb) {
	fprintf(stderr,"dvb-s: lnb config missing\n");
	return -1;
    }
    if (NULL == pol) {
	fprintf(stderr,"dvb-s: polarisation config missing\n");
	return -1;
    }

    if (3 != sscanf(dvb_lnb,"%d,%d,%d",&lo,&hi,&sw)) {
	if (1 != sscanf(dvb_lnb,"%d",&lo))
	    return -1;
	hi = 0;
	sw = 0;
    }
    if (sw && freq > sw) {
	tone = SEC_TONE_ON;
	lof  = hi;
    } else {
	tone = SEC_TONE_OFF;
	lof  = lo;
    }
    
    if (0 == strcasecmp(pol,"h"))
	volt = SEC_VOLTAGE_18;
    else
	volt = SEC_VOLTAGE_13;

    xioctl(fd, FE_SET_TONE, (void*)SEC_TONE_OFF, 0);
    usleep(15*1000);

    xioctl(fd, FE_SET_VOLTAGE, (void*)volt, 0);
    usleep(15*1000);

    if (NULL != dvb_sat) {
	if (0 == strcasecmp(dvb_sat,"MA")) {
	    xioctl(fd, FE_DISEQC_SEND_BURST, (void*)SEC_MINI_A, 0);
	} else if (0 == strcasecmp(dvb_sat,"MB")) {
	    xioctl(fd, FE_DISEQC_SEND_BURST, (void*)SEC_MINI_B, 0);
	} else if (1 == sscanf(dvb_sat,"D%d",&nr)) {
	    char diseqc[] = { 0xE0, 0x10, 0x38, 0xF0 };
	    if (tone == SEC_TONE_ON)
		diseqc[3] |= 0x01;
	    if (volt == SEC_VOLTAGE_18)
		diseqc[3] |= 0x02;
	    diseqc[3] |= (nr & 0x03) << 2;
	    xioctl(fd, FE_DISEQC_SEND_MASTER_CMD, &diseqc, 0);
	}
    }

    usleep(15*1000);
    xioctl(fd, FE_SET_TONE, (void*)tone, 0);

    return lof;
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

static void fixup_numbers(struct dvb_state *h, int lof)
{
    switch (h->info.type) {
    case FE_QPSK:
	/*
	 * DVB-S
	 *   - kernel API uses kHz here.
	 *   - /etc/vdr/channel.conf + diseqc.conf use MHz
	 *   - scan (from linuxtv.org dvb utils) uses KHz
	 */
        if (lof < 1000000)
	    lof *= 1000;
        if (h->p.frequency < 1000000)
	    h->p.frequency *= 1000;
	h->p.frequency -= lof;
	if (h->p.u.qpsk.symbol_rate < 1000000)
	    h->p.u.qpsk.symbol_rate *= 1000;
	break;
    case FE_QAM:
    case FE_OFDM:
	/*
	 * DVB-C,T
	 *   - kernel API uses Hz here.
	 *   - /etc/vdr/channel.conf allows Hz, kHz and MHz
	 */
	if (h->p.frequency < 1000000)
	    h->p.frequency *= 1000;
	if (h->p.frequency < 1000000)
	    h->p.frequency *= 1000;
	break;
    }
}

static void dump_fe_info(struct dvb_state *h)
{
    switch (h->info.type) {
    case FE_QPSK:
	fprintf(stderr,"dvb fe: tuning freq=lof+%d kHz, inv=%s "
		"symbol_rate=%d fec_inner=%s\n",
		h->p.frequency,
		dvb_fe_inversion [ h->p.inversion ],
		h->p.u.qpsk.symbol_rate,
		dvb_fe_rates [ h->p.u.qpsk.fec_inner ]);
	break;
    case FE_QAM:
	fprintf(stderr,"dvb fe: tuning freq=%d Hz, inv=%s "
		"symbol_rate=%d fec_inner=%s modulation=%s\n",
		h->p.frequency,
		dvb_fe_inversion  [ h->p.inversion       ],
		h->p.u.qam.symbol_rate,
		dvb_fe_rates      [ h->p.u.qam.fec_inner  ],
		dvb_fe_modulation [ h->p.u.qam.modulation ]);
	break;
    case FE_OFDM:
	fprintf(stderr,"dvb fe: tuning freq=%d Hz, inv=%s "
		"bandwidth=%s code_rate=[%s-%s] constellation=%s "
		"transmission=%s guard=%s hierarchy=%s\n",
		h->p.frequency,
		dvb_fe_inversion    [ h->p.inversion                    ],
		dvb_fe_bandwidth    [ h->p.u.ofdm.bandwidth             ],
		dvb_fe_rates        [ h->p.u.ofdm.code_rate_HP          ],
		dvb_fe_rates        [ h->p.u.ofdm.code_rate_LP          ],
		dvb_fe_modulation   [ h->p.u.ofdm.constellation         ],
		dvb_fe_transmission [ h->p.u.ofdm.transmission_mode     ],
		dvb_fe_guard        [ h->p.u.ofdm.guard_interval        ],
		dvb_fe_hierarchy    [ h->p.u.ofdm.hierarchy_information ]);
	break;
    }
}

int dvb_frontend_tune(struct dvb_state *h, char *domain, char *section)
{
    char *diseqc;
    char *action;
    int lof = 0;
    int val;
    int rc;

    if (-1 == dvb_frontend_open(h,1))
	return -1;
    if (0 != cfg_get_int(domain, section, "ca", 0)) {
	fprintf(stderr,"encrypted channel, can't handle that, sorry\n");
	return -1;
    }

    if (dvb_src)
	free(dvb_src);
    if (dvb_lnb)
	free(dvb_lnb);
    if (dvb_sat)
	free(dvb_sat);
    dvb_src = NULL;
    dvb_lnb = NULL;
    dvb_sat = NULL;
    
    switch (h->info.type) {
    case FE_QPSK:
	dvb_src = cfg_get_str(domain, section, "source");
	if (dvb_src)
	    dvb_src = strdup(dvb_src);
	if (NULL != dvb_src) {
	    /* use vdr's diseqc.conf */
	    diseqc = find_vdr_diseqc(domain,section);
	    if (!diseqc) {
		fprintf(stderr,"no entry in /etc/vdr/diseqc.conf for \"%s\"\n",
			section);
		return -1;
	    }
	    action = cfg_get_str("vdr-diseqc", diseqc, "action");
	    if (dvb_debug)
		fprintf(stderr,"diseqc action: \"%s\"\n", action);
	    exec_vdr_diseqc(h->fdwr,action);
	    lof = cfg_get_int("vdr-diseqc", diseqc, "lof", 0);
	} else {
	    /* use my stuff */
	    lof = sat_switch(h->fdwr,domain,section);
	    if (-1 == lof)
		return -1;
	}

	val = cfg_get_int(domain, section, "frequency", 0);
	if (0 == val)
	    return -1;
	h->p.frequency = val;
	dvb_inv = cfg_get_int(domain, section, "inversion", INVERSION_AUTO);
	h->p.inversion = dvb_inv;

	val = cfg_get_int(domain, section, "symbol_rate", 0);
	h->p.u.qpsk.symbol_rate = val;
	h->p.u.qpsk.fec_inner = FEC_AUTO; // FIXME 

	break;

    case FE_QAM:
	val = cfg_get_int(domain, section, "frequency", 0);
	if (0 == val)
	    return -1;
	h->p.frequency = val;
	dvb_inv = cfg_get_int(domain, section, "inversion", INVERSION_AUTO);
	h->p.inversion = dvb_inv;
	
	val = cfg_get_int(domain, section, "symbol_rate", 0);
	h->p.u.qam.symbol_rate = val;
	h->p.u.qam.fec_inner = FEC_AUTO; // FIXME
	val = cfg_get_int(domain, section, "modulation", 0);
	h->p.u.qam.modulation = fe_vdr_modulation [ val ];
	break;

    case FE_OFDM:
	val = cfg_get_int(domain, section, "frequency", 0);
	if (0 == val)
	    return -1;
	h->p.frequency = val;
	dvb_inv = cfg_get_int(domain, section, "inversion", INVERSION_AUTO);
	h->p.inversion = dvb_inv;
	
	val = cfg_get_int(domain, section, "bandwidth", BANDWIDTH_AUTO);
	h->p.u.ofdm.bandwidth = fe_vdr_bandwidth [ val ];
	val = cfg_get_int(domain, section, "code_rate_high", 0);
	h->p.u.ofdm.code_rate_HP = fe_vdr_rates [ val ];
	val = cfg_get_int(domain, section, "code_rate_low", 0);
	h->p.u.ofdm.code_rate_LP = fe_vdr_rates [ val ];
	val = cfg_get_int(domain, section, "modulation", 0);
	h->p.u.ofdm.constellation = fe_vdr_modulation [ val ];
	val = cfg_get_int(domain, section, "transmission", 0);
	h->p.u.ofdm.transmission_mode = fe_vdr_transmission [ val ];
	val= cfg_get_int(domain, section, "guard_interval", GUARD_INTERVAL_AUTO);
	h->p.u.ofdm.guard_interval = fe_vdr_guard [ val ];
	val = cfg_get_int(domain, section, "hierarchy", HIERARCHY_AUTO);
	h->p.u.ofdm.hierarchy_information = fe_vdr_hierarchy [ val ];
	break;
    }
    fixup_numbers(h,lof);

    if (0 == memcmp(&h->p, &h->plast, sizeof(h->plast))) {
	if (dvb_frontend_is_locked(h)) {
	    /* same frequency and frontend still locked */
	    if (dvb_debug)
		fprintf(stderr,"dvb fe: skipped tuning\n");
	    rc = 0;
	    goto done;
	}
    }

    rc = -1;
    if (-1 == xioctl(h->fdwr,FE_SET_FRONTEND,&h->p, 0)) {
	dump_fe_info(h);
	goto done;
    }
    if (dvb_debug)
	dump_fe_info(h);
    memcpy(&h->plast, &h->p, sizeof(h->plast));
    rc = 0;

done:
    // Hmm, the driver seems not to like that :-/
    // dvb_frontend_release(h,1);
    return rc;
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

int dvb_frontend_wait_lock(struct dvb_state *h, int timeout)
{
    struct timeval start,now,tv;
    int msec,locked = 0, runs = 0;

    gettimeofday(&start,NULL);
    if (dvb_debug)
	dvb_frontend_status_title();
    for (;;) {
	tv.tv_sec  = 0;
	tv.tv_usec = 33 * 1000;
	select(0,NULL,NULL,NULL,&tv);
	if (dvb_debug)
	    dvb_frontend_status_print(h);
	if (dvb_frontend_is_locked(h))
	    locked++;
	else
	    locked = 0;
	if (locked > 3)
	    return 0;
	runs++;

	gettimeofday(&now,NULL);
	msec  = (now.tv_sec - start.tv_sec) * 1000;
	msec += now.tv_usec/1000;
	msec -= start.tv_usec/1000;
	if (msec > timeout && runs > 3)
	    break;
    }
    return -1;
}

int dvb_frontend_get_type(struct dvb_state *h)
{
    return h->info.type;
}

int dvb_frontend_get_caps(struct dvb_state *h)
{
    return h->info.caps;
}

int dvb_frontend_get_biterr(struct dvb_state *h)
{
    uint32_t ber = 0;

    ioctl(h->fdro, FE_READ_BER, &ber);
    return ber;
}

/* ----------------------------------------------------------------------- */
/* handle dvb demux                                                        */

void dvb_demux_filter_setup(struct dvb_state *h, int video, int audio)
{
    h->video.filter.pid      = video;
    h->video.filter.input    = DMX_IN_FRONTEND;
    h->video.filter.output   = DMX_OUT_TS_TAP;
    h->video.filter.pes_type = DMX_PES_VIDEO;
    h->video.filter.flags    = 0;

    h->audio.filter.pid      = audio;
    h->audio.filter.input    = DMX_IN_FRONTEND;
    h->audio.filter.output   = DMX_OUT_TS_TAP;
    h->audio.filter.pes_type = DMX_PES_AUDIO;
    h->audio.filter.flags    = 0;
}

int dvb_demux_filter_apply(struct dvb_state *h)
{
    if (0 != h->video.filter.pid) {
	/* setup video filter */
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
    } else {
	/* no video */
	if (-1 != h->video.fd) {
	    close(h->video.fd);
	    h->video.fd = -1;
	}
    }

    if (0 != h->audio.filter.pid) {
	/* setup audio filter */
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
    } else {
	/* no audio */
	if (-1 != h->audio.fd) {
	    close(h->audio.fd);
	    h->audio.fd = -1;
	}
    }

    if (-1 != h->video.fd) {
	if (-1 == xioctl(h->video.fd,DMX_START,NULL,0)) {
	    perror("dvb mux: [video] ioctl DMX_START");
	    goto oops;
	}
    }
    if (-1 != h->audio.fd) {
	if (-1 == xioctl(h->audio.fd,DMX_START,NULL,0)) {
	    perror("dvb mux: [audio] ioctl DMX_START");
	    goto oops;
	}
    }

    ng_mpeg_vpid = h->video.filter.pid;
    ng_mpeg_apid = h->audio.filter.pid;
    if (dvb_debug)
	fprintf(stderr,"dvb mux: dvb ts pids: video=%d audio=%d\n",
		ng_mpeg_vpid,ng_mpeg_apid);
    return 0;

 oops:
    return -1;
}

void dvb_demux_filter_release(struct dvb_state *h)
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
    ng_mpeg_vpid = 0;
    ng_mpeg_apid = 0;
}

int dvb_demux_req_section(struct dvb_state *h, int fd, int pid, int sec,
			  int oneshot, int timeout)
{
    struct dmx_sct_filter_params filter;
    
    memset(&filter,0,sizeof(filter));
    filter.pid              = pid;
    filter.filter.filter[0] = sec;
    filter.filter.mask[0]   = 0xff;
    filter.timeout          = timeout * 1000;
    filter.flags            = DMX_IMMEDIATE_START | DMX_CHECK_CRC;
    if (oneshot)
	filter.flags       |= DMX_ONESHOT;

    if (-1 == fd) {
	fd = open(h->demux, O_RDWR);
	if (-1 == fd) {
	    fprintf(stderr,"dvb mux: [pid %d] open %s: %s\n",
		    pid, h->demux, strerror(errno));
	    goto oops;
	}
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

int dvb_demux_get_section(int fd, unsigned char *buf, int len)
{
    int rc;
    
    memset(buf,0,len);
    if ((rc = read(fd, buf, len)) < 0)
	if ((ETIMEDOUT != errno) || dvb_debug)
	    fprintf(stderr,"dvb mux: read: %s\n", strerror(errno));
    return rc;
}

/* ----------------------------------------------------------------------- */
/* open/close/tune dvb devices                                             */

void dvb_fini(struct dvb_state *h)
{
    dvb_frontend_release(h,1);
    dvb_frontend_release(h,0);
    dvb_demux_filter_release(h);
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

#if 0
    /* hacking DVB-S without hardware ;) */
    h->info.type = FE_QPSK;
#endif
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

int dvb_start_tune(struct dvb_state *h, int tsid, int pnr)
{
    char ts[32];
    char pr[32];

    snprintf(ts,sizeof(ts),"%d",tsid);
    snprintf(pr,sizeof(pr),"%d-%d",tsid,pnr);
    
    if (0 != dvb_frontend_tune(h,"dvb-ts",ts)) {
	fprintf(stderr,"dvb: frontend tuning failed\n");
	return -1;
    }
    dvb_demux_filter_setup(h, cfg_get_int("dvb-pr",pr, "video",0),
			   cfg_get_int("dvb-pr",pr, "audio",0));
    return 0;
}

int dvb_start_tune_vdr(struct dvb_state *h, char *section)
{
    char *domain = "vdr-channels";
    
    if (0 != dvb_frontend_tune(h,domain,section)) {
	fprintf(stderr,"dvb: frontend tuning failed\n");
	return -1;
    }
    dvb_demux_filter_setup(h, cfg_get_int(domain, section, "video",0),
			   cfg_get_int(domain, section, "audio",0));
    return 0;
}

int dvb_finish_tune(struct dvb_state *h, int timeout)
{
    if (0 == h->video.filter.pid && 0 == h->audio.filter.pid)
	return -2;

    if (0 == timeout) {
	if (!dvb_frontend_is_locked(h))
	    return -1;
    } else {
	if (0 != dvb_frontend_wait_lock(h, timeout))
	    return -1;
    }
    if (0 != dvb_demux_filter_apply(h)) {
	return -2;
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
	fd = ng_chardev_open(device, O_RDONLY | O_NONBLOCK, 212, debug);
	if (-1 == fd)
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
    int sdt = 0;
    int pat = 0;

    if (names)
	sdt = dvb_demux_req_section(dvb, -1, 0x11, 0x42, 1, 60);
    pat = dvb_demux_req_section(dvb, -1, 0x00, 0x00, 1, 20);

    /* program association table */
    if (dvb_demux_get_section(pat, buf, sizeof(buf)) < 0)
	goto oops;
    close(pat);
    mpeg_parse_psi_pat(info, buf, verbose);
    
    /* program maps */
    list_for_each(item,&info->programs) {
        pr = list_entry(item, struct psi_program, next);
	pr->fd = dvb_demux_req_section(dvb, -1, pr->p_pid, 2, 1, 20);
    }
    list_for_each(item,&info->programs) {
        pr = list_entry(item, struct psi_program, next);
	if (dvb_demux_get_section(pr->fd, buf, sizeof(buf)) < 0)
	    goto oops;
	close(pr->fd);
	mpeg_parse_psi_pmt(pr, buf, verbose);
    }

    /* service descriptor */
    if (names) {
	if (dvb_demux_get_section(sdt, buf, sizeof(buf)) < 0)
	    goto oops;
	close(sdt);
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
	"video", "audio", "teletext", "conditional_access",
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
    parse_vdr_channels("vdr-channels",fp);
    fclose(fp);

    fp = fopen("/etc/vdr/diseqc.conf","r");
    if (NULL == fp)
	return;
    parse_vdr_diseqc("vdr-diseqc",fp);
    fclose(fp);
}
