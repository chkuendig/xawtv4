/*
 * (c) 2000-2002 Gerd Knorr <kraxel@goldbach.in-berlin.de>
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

#include "tv-config.h"
#include "tuning.h"
#include "grab-ng.h"
#include "commands.h"
#include "devs.h"
#include "parseconfig.h"

#include "vbi-data.h"


int debug = 0;
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
    struct vbi_state *vbi;
    int on,tuned;
    unsigned int f,fc,f1,f2;
    char *oldname, *name, *channel, *fchannel, dummy[32];

    if (!(devs.video.flags & CAN_TUNE)) {
	fprintf(stderr,"device has no tuner\n");
	return;
    }

    do_va_cmd(2,"setinput",   cfg_get_str("options", "global", "input"));
    do_va_cmd(2,"setnorm",    cfg_get_str("options", "global", "norm"));
    do_va_cmd(2,"setfreqtab", cfg_get_str("options", "global", "freqtab"));
    
    /* vbi */
    vbi = vbi_open(devs.vbidev,0,0);
    if (NULL == vbi) {
	fprintf(stderr,"open %s: %s\n",ng_dev.vbi,strerror(errno));
	exit(1);
    }
    vbi_event_handler_add(vbi->dec,~0,event,vbi);
    
    if (!fullscan) {
	/* scan channels */
	fprintf(stderr,"\nscanning channel list %s...\n",
		cfg_get_str("options", "global", "freqtab"));
	for (channel = cfg_sections_first(freqtab);
	     NULL != channel;
	     channel = cfg_sections_next(freqtab,channel)) {
	    fprintf(stderr,"%-4s (%6.2f MHz): ",
		    channel,cfg_get_float(freqtab,channel,"freq")/1000);
	    tune_analog_channel(channel);
	    usleep(200000); /* 0.2 sec */
	    if (0 == devs.video.v->is_tuned(devs.video.handle)) {
		fprintf(stderr,"no station\n");
		continue;
	    }
	    name    = get_vbi_name(vbi);
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
	    for (channel = cfg_sections_first(freqtab);
		 NULL != channel;
		 channel = cfg_sections_next(freqtab,channel)) {
		if (cfg_get_int(freqtab,channel,"freq",0) * 16 == f * 1000)
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
	    
	    name = get_vbi_name(vbi);
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
	    "   -h           print this text\n"
	    "   -i input     set input.              [%s]\n"
	    "   -n norm      set tv norm.            [%s]\n"
	    "   -f table     set frequency table.    [%s]\n"
	    "   -g group     set group               [%s]\n"
	    "   -t timeout   set timeout             [%d]\n"
	    "   -c device    set video device file.  [%s]\n"
	    "   -C device    set vbi device file.    [%s]\n"
	    "   -s           skip channel scan\n"
	    "   -a           full scan (all frequencies, not just\n"
	    "                the ones from the frequency table)\n",
	    prog,
	    input  ? input  : "none",
	    tvnorm ? tvnorm : "none",
	    table  ? table  : "none",
	    group  ? group  : "none",
	    timeout,
	    ng_dev.video,
	    ng_dev.vbi);
}

int
main(int argc, char **argv)
{
    struct ng_attribute *attr_input;
    struct ng_attribute *attr_tvnorm;
    char *devname = "default";
    int c, i, t, f, scan=1, fullscan=0;
    struct STRTAB *freqtabs;
    char *tab;

    /* get defaults */
    read_config();
    input  = cfg_get_str("options", "global", "input");
    tvnorm = cfg_get_str("options", "global", "norm");
    table  = cfg_get_str("options", "global", "freqtab");
    
    /* parse options */
    ng_init();
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hsadi:n:f:o:c:C:g:t:")))
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
	case 'i':
	    input = optarg;
	    break;
	case 'n':
	    tvnorm = optarg;
	    break;
	case 'f':
	    table = optarg;
	    break;
	case 'c':
	    cfg_set_str("devs", devname, "video", optarg);
	    break;
	case 'C':
	    cfg_set_str("devs", devname, "vbi", optarg);
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

    /* video */
    devlist_init(1);
    device_init(devname);

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
    i = menu("input",attr_input->choices,input);
    t = menu("TV norm",attr_tvnorm->choices,tvnorm);
    f = menu("frequency table",freqtabs,table);

    cfg_set_str("options", "global", "input", ng_attr_getstr(attr_input,i));
    cfg_set_str("options", "global", "norm",  ng_attr_getstr(attr_tvnorm,t));
    cfg_set_str("options", "global", "freqtab", freqtabs[f].str);

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
