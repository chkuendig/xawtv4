#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>

#include "grab-ng.h"
#include "devs.h"

#include "parseconfig.h"
#include "tuning.h"
#include "dvb.h"

extern int debug;

/* ---------------------------------------------------------------------------- */
/* maintain current state                                                       */

char *curr_station;
char *curr_channel;
char curr_details[32];

static void new_string(char **curr, char *update)
{
    if (update == *curr)
	return;
    if (*curr)
	free(*curr);
    *curr = NULL;
    if (update)
	*curr = strdup(update);
}

static void new_station(char *station)
{
    new_string(&curr_station, station);
}

static void new_channel(char *channel)
{
    new_string(&curr_channel, channel);
}

/* ---------------------------------------------------------------------------- */
/* analog tv tuning                                                             */

char *freqtab;

static void __init freqtab_init(void)
{
    cfg_parse_file("freqtabs", DATADIR "/Index.map");
}

void freqtab_set(char *name)
{
    char filename[1024];
    char *file;

    freqtab = name;
    if (debug)
	fprintf(stderr,"freqtab: %s\n",name);
    if (0 != cfg_sections_count(freqtab))
	/* already have that one ... */
	return;

    file = cfg_get_str("freqtabs",name,"file");
    if (!file)
	fprintf(stderr,"freqtab: unknown table %s\n",name);
    snprintf(filename,sizeof(filename),"%s/%s",DATADIR,file);
    cfg_parse_file(name,filename);
}

int freqtab_lookup(char *channel)
{
    int freq;

    freq = cfg_get_int(freqtab, channel, "freq", 0);
    if (0 == freq)
	return -1;
    return freq*16/1000;
}

/* --------------------------------------------------------------------- */

static int cf2freq(char *name, int fine)
{
    int freq;
    
    if (-1 == (freq = freqtab_lookup(name))) {
	fprintf(stderr,"tune analog: channel \"%s\" not found in table \"%s\"\n",
		name, freqtab ? freqtab : "unset");
	return -1;
    }
    return freq+fine;
}

static int tune_analog(int32_t freq, char *channel, char *station)
{
    /* device can't tune ... */
    if (!(devs.video.flags & CAN_TUNE))
	return -1;

    /* go! */
    ng_dev_open(&devs.video);
    devs.video.v->setfreq(devs.video.handle,freq);
    ng_dev_close(&devs.video);

    /* update state */
    new_station(station);
    new_channel(channel);
    snprintf(curr_details, sizeof(curr_details),
	     "%.2f MHz", (float)freq/16);

    if (debug)
	fprintf(stderr,"tune analog: %s %s %s\n",
		curr_details, curr_channel, curr_station);
    return 0;
}

int tune_analog_freq(int freq)
{
    return tune_analog(freq, NULL, NULL);
}

int tune_analog_channel(char *channel)
{
    int32_t  freq;

    /* lookup frequency */
    freq = cf2freq(channel,0);
    if (freq <= 0)
	return -1;

    return tune_analog(freq,channel,NULL);
}

int tune_analog_station(char *station)
{
    char     *channel;
    int32_t  freq;
    int32_t  fine = 0;

    /* lookup channel + frequency */
    channel = cfg_get_str("stations", station, "channel");
    if (channel) {
	fine = cfg_get_signed_int("stations", station, "fine", 0);
	freq = cf2freq(channel,fine);
    } else {
	freq = cfg_get_signed_int("stations", station, "freq", 0);
    }
    if (freq <= 0)
	return -1;

    return tune_analog(freq,channel,station);
}
		       
/* ---------------------------------------------------------------------------- */
/* dvb tuning                                                                   */

static int tune_dvb_station(char *station)
{
#ifndef HAVE_DVB
    return -1;
#else
    char pids[32];
    char *vdrname;
    int32_t freq;

    if (!devs.dvb)
	return -1;

    /* DVB tuning */
    vdrname = cfg_get_str("stations", station, "vdr");
    if (NULL == vdrname)
	vdrname= station;
    if (-1 == dvb_tune(devs.dvb, vdrname)) {
	fprintf(stderr,"tuning failed\n");
	return -1;
    }

    /* update info */
    freq = cfg_get_int("dvb", vdrname, "frequency", 0);
    snprintf(pids,sizeof(pids),"%d / %d",ng_mpeg_vpid,ng_mpeg_apid);
    new_station(station);
    new_channel(pids);
    snprintf(curr_details, sizeof(curr_details),
	     "%.2f MHz", (float)freq/1000);
    return 0;
#endif
}

/* ---------------------------------------------------------------------------- */

int tune_station(char *station)
{
    int rc;

    /* reset state */
    curr_details[0] = 0;

    /* try dvb ... */
    rc = tune_dvb_station(station);
    if (0 == rc)
	return 0;

    /* analog */
    rc = tune_analog_station(station);
    if (0 == rc)
	return 0;

    /* all failed */
    return -1;
}
