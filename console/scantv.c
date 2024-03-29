/*
 * (c) 2000-2004 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>

#include "grab-ng.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "tuning.h"
#include "commands.h"
#include "devs.h"

#include "vbi-data.h"


int have_dga = 0;

static const char *unknown = "-?-";
static int timeout = 3;
static char *group = NULL;
static char *tvname;

/*---------------------------------------------------------------------*/

static void
event(struct vbi_event *ev, void *user)
{
    switch (ev->type) {
    case VBI_EVENT_NETWORK:
	if (NULL != tvname)
	    free(tvname);
	if (strlen(ev->ev.network.name) > 0)
	    tvname = strdup(ev->ev.network.name);
	break;
    }
}

static char*
get_vbi_name(struct vbi_state *vbi)
{
    static char *progress = "-\\|/";
    int start, i = 0;

    fprintf(stderr,"%c",progress[(i++)%4]);
    vbi_hasdata(vbi);
    if (NULL != tvname)
	free(tvname);
    tvname = NULL;
    start = time(NULL);
    for (;;) {
	fprintf(stderr,"\x08%c",progress[(i++)%4]);
	vbi_hasdata(vbi);
        if (time(NULL) > start+timeout)
            break;
        if (tvname)
            break;
    }
    fprintf(stderr,"\x08");
    return tvname;
}

static int
menu(char *name, struct STRTAB *tab, char *opt)
{
    int i,ret;
    char line[80];

    if (NULL != opt) {
	/* cmd line option -- non-interactive mode */
	for (i = 0; tab[i].str != NULL; i++)
	    if (0 == strcasecmp(tab[i].str,opt)) {
		fprintf(stderr,"using %s \"%s\"\n",name,opt);
		return tab[i].nr;
	    }
	fprintf(stderr,"%s: not found\n",opt);
	exit(1);
    }

    fprintf(stderr,"\nplease pick the %s\n",name);
    for (i = 0; tab[i].str != NULL; i++)
	fprintf(stderr,"  %2ld: %s\n",tab[i].nr,tab[i].str);

    for (;;) {
	fprintf(stderr,"nr ? ");
	fgets(line,79,stdin);
	ret = atoi(line);
	for (i = 0; tab[i].str != NULL; i++)
	    if (ret == tab[i].nr)
		return ret;
	fprintf(stderr,"invalid choice\n");
    }
}

static void do_scan(int fullscan)
{
    struct vbi_state *vbi = NULL;
    int on,tuned;
    unsigned int f,fc,f1,f2;
    char *oldname, *name, *channel, *fchannel, dummy[32];

    if (!(devs.video.flags & CAN_TUNE)) {
	fprintf(stderr,"device has no tuner\n");
	return;
    }

    do_va_cmd(2,"setinput",   cfg_get_str(O_INPUT));
    do_va_cmd(2,"setnorm",    cfg_get_str(O_TVNORM));
    do_va_cmd(2,"setfreqtab", cfg_get_str(O_FREQTAB));
    
    /* vbi */
    if (devs.vbidev) {
	vbi = vbi_open(devs.vbidev,"scantv",0,0,1);
	if (NULL == vbi)
	    fprintf(stderr,"open %s: %s\n",ng_dev.vbi,strerror(errno));
	else
	    vbi_event_handler_add(vbi->dec,~0,event,vbi);
    }
    
    if (!fullscan) {
	/* scan channels */
	fprintf(stderr,"\nscanning channel list %s...\n",
		cfg_get_str("options", "global", "freqtab"));
	for (channel = cfg_sections_first(freqtab_get());
	     NULL != channel;
	     channel = cfg_sections_next(freqtab_get(),channel)) {
	    fprintf(stderr,"%-4s (%6.2f MHz): ",
		    channel,cfg_get_float(freqtab_get(),channel,"freq",0)/1000);
	    tune_analog_channel(channel);
	    usleep(200000); /* 0.2 sec */
	    if (0 == devs.video.v->is_tuned(devs.video.handle)) {
		fprintf(stderr,"no station\n");
		continue;
	    }
	    name    = vbi ? get_vbi_name(vbi) : NULL;
	    oldname = cfg_search("stations", NULL, "channel", channel);

	    if (name) {
		if (oldname) {
		    if (0 == strcmp(name,oldname)) {
			fprintf(stderr, "%-32s [exists]\n", name);
		    } else {
			fprintf(stderr, "%-32s [replacing: %s]\n", name, oldname);
			cfg_del_section("stations", oldname);
			cfg_set_str("stations", name, "channel", channel);
		    }
		} else {
		    fprintf(stderr, "%-32s [new]\n", name);
		    cfg_set_str("stations", name, "channel", channel);
		    if (group)
			cfg_set_str("stations", name, "group", group);
		}
	    } else {
		if (oldname) {
		    fprintf(stderr, "%-32s [keeping: %s]\n", unknown, oldname);
		} else {
		    sprintf(dummy,"unknown (%s)",channel);
		    name = dummy;
		    fprintf(stderr, "%-32s [new, using: %s]\n", unknown, name);
		    cfg_set_str("stations", name, "channel", channel);
		    if (group)
			cfg_set_str("stations", name, "group", group);
		}
	    }
	}
    } else {
	/* scan freqnencies */
	fprintf(stderr,"\nscanning freqencies...\n");
	fchannel = NULL;
	on = 0;
	fc = 0;
	f1 = 0;
	f2 = 0;
	for (f = 44*16; f <= 958*16; f += 4) {
	    for (channel = cfg_sections_first(freqtab_get());
		 NULL != channel;
		 channel = cfg_sections_next(freqtab_get(),channel)) {
		if (cfg_get_int(freqtab_get(),channel,"freq",0) * 16 == f * 1000)
		    break;
	    }
	    fprintf(stderr,"?? %6.2f MHz (%-4s): ",f/16.0,
		    channel ? channel : "-");
	    tune_analog_freq(f);
	    usleep(200000); /* 0.2 sec */
	    tuned = devs.video.v->is_tuned(devs.video.handle);

	    /* state machine */
	    if (0 == on && 0 == tuned) {
		fprintf(stderr,"|   no\n");
		continue;
	    }
	    if (0 == on && 0 != tuned) {
		fprintf(stderr," \\  raise\n");
		f1 = f;
		if (channel) {
		    fchannel = channel;
		    fc       = f;
		}
		on = 1;
		continue;
	    }
	    if (0 != on && 0 != tuned) {
		fprintf(stderr,"  | yes\n");
		if (channel) {
		    fchannel = channel;
		    fc       = f;
		}
		continue;
	    }
	    /* if (on != 0 && 0 == tuned)  --  found one, read name from vbi */
	    fprintf(stderr," /  fall\n");
	    f2 = f;
	    if (NULL == fchannel)
		fc = (f1+f2)/2;

	    fprintf(stderr,"=> %6.2f MHz (%-4s): ", fc/16.0,
		    fchannel ? fchannel : "-");
	    tune_analog_freq(fc);
	    
	    name = vbi ? get_vbi_name(vbi) : NULL;
	    fprintf(stderr,"%s\n",name ? name : unknown);
	    if (NULL == fchannel) {
		if (NULL == name) {
		    sprintf(dummy,"unknown (%s)",fchannel);
		    name = dummy;
		}
		cfg_set_str("stations", name, "channel", fchannel);
	    } else {
		if (NULL == name) {
		    sprintf(dummy,"unknown (%.3f)", fc/16.0);
		    name = dummy;
		}
		cfg_set_int("stations", name, "freq", fc);
	    }
	    fchannel = NULL;
	    fc = 0;
	    on = 0;
	    f1 = 0;
	    f2 = 0;
	}
    }
}

/* ------------------------------------------------------------------------- */

static char *input;
static char *tvnorm;
static char *table;

static void
usage(FILE *out, char *prog)
{
    fprintf(out,
	    "This tool (re-)scans for analog tv stations and creates/updates\n"
	    "the config files for xawtv/motv/related applications.\n"
	    "usage: %s [ options ]\n"
	    "options:\n"
	    "      -h              print this text\n"
	    "      -g group        set group                               [%s]\n"
	    "      -t timeout      set timeout                             [%d]\n"
	    "      -s              skip channel scan\n"
	    "      -a              full scan (all frequencies, not just\n"
	    "                      the ones from the frequency table)\n",
	    prog,
	    group  ? group  : "none",
	    timeout);

    fprintf(out,"\n");
    cfg_help_cmdline(out,cmd_opts_devices,6,16,40);
}

int
main(int argc, char **argv)
{
    struct ng_attribute *attr_input;
    struct ng_attribute *attr_tvnorm;
    int devcount, d, c, i, t, f, scan=1, fullscan=0;
    struct STRTAB *freqtabs, *devices;
    char *tab;
    char *list;
    char *devname = NULL;

    /* parse options */
    ng_init();
    read_config();
    cfg_parse_cmdline(&argc,argv,cmd_opts_devices);

    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hsadg:t:")))
	    break;
	switch (c) {
	case 'd':
	    debug++;
	    break;
	case 's':
	    scan=0;
	    break;
	case 'a':
	    fullscan=1;
	    break;
	case 't':
	    timeout = atoi(optarg);
	    break;
	case 'g':
	    group = optarg;
	    break;
	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }
    ng_debug = debug;

    /* get defaults */
    input  = cfg_get_str(O_INPUT);
    tvnorm = cfg_get_str(O_TVNORM);
    table  = cfg_get_str(O_FREQTAB);

    /* probe devices */
    devlist_init(1,0,0);
    devcount = 0;
    cfg_sections_for_each("devs",list) {
	if (NULL == cfg_get_str("devs",list,"video"))
	    continue;
	devcount++;
    }

    if (0 == devcount) {
	/* huh? */
	fprintf(stderr,"No analog tv hardware found, sorry.\n");
	exit(1);
    }
    if (1 == devcount) {
	/* exactly one device */
	cfg_sections_for_each("devs",list) {
	    if (NULL == cfg_get_str("devs",list,"video"))
		continue;
	    devname = list;
	}
    }
    devices = malloc(sizeof(*freqtabs) * (devcount+1));
    memset(devices,0,sizeof(*freqtabs) * (devcount+1));
    d = 0;
    cfg_sections_for_each("devs",list) {
	if (NULL == cfg_get_str("devs",list,"video"))
	    continue;
	devices[d].nr  = d;
	devices[d].str = list;
	d++;
    }
    d = menu("device",devices,devname);
    devname = devices[d].str;
    device_init(devname);

    /* init video */
    if (NG_DEV_VIDEO != devs.video.type) {
	fprintf(stderr,"can't open video device\n");
	exit(1);
    }

    /* build STRTAB for freqtabs */
    freqtabs = malloc(sizeof(*freqtabs) * (cfg_sections_count("freqtabs")+1));
    for (i = 0, tab = cfg_sections_first("freqtabs");
	 NULL != tab;
	 i++, tab = cfg_sections_next("freqtabs",tab)) {
	freqtabs[i].nr  = i;
	freqtabs[i].str = tab;
    }
    freqtabs[i].nr  = -1;
    freqtabs[i].str = NULL;

    /* ask the user some questions ... */
    attr_input  = ng_attr_byid(&devs.video,ATTR_ID_INPUT);
    attr_tvnorm = ng_attr_byid(&devs.video,ATTR_ID_NORM);
    i = menu("input",           attr_input->choices,  input);
    t = menu("TV norm",         attr_tvnorm->choices, tvnorm);
    f = menu("frequency table", freqtabs,             table);

    cfg_set_str(O_INPUT,   ng_attr_getstr(attr_input,i));
    cfg_set_str(O_TVNORM,  ng_attr_getstr(attr_tvnorm,t));
    cfg_set_str(O_FREQTAB, freqtabs[f].str);

    if (scan) {
	ng_dev_open(&devs.video);
	do_scan(fullscan);
	ng_dev_close(&devs.video);
    }
    write_config_file("options");
    write_config_file("stations");

    /* cleanup */
    device_fini();
    exit(0);
}
