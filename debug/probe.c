#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "grab-ng.h"
#include "devs.h"
#include "dvb.h"

int verbose;
int debug;

/* ------------------------------------------------------------------ */

static void print_driver(const char *name)
{
    // fprintf(stderr,"  [%s]\n",name);
}

static void print_devinfo(const char *driver, struct ng_devinfo *info)
{
    int spaces1 = 24 - strlen(driver) - strlen(info->device);
    int spaces2 = 50 - strlen(info->name) - strlen(info->bus);

    if (spaces1 < 0) spaces1 = 0;
    if (spaces2 < 0) spaces2 = 0;
    fprintf(stderr,"  %s:%s%*s %s%*s %s\n",
	    driver, info->device,
	    spaces1, "", info->name,
	    spaces2, "", info->bus);
}

/* ------------------------------------------------------------------ */

static int
print_dvb(void)
{
#ifdef HAVE_DVB
    struct ng_devinfo *info;
    int i;

    print_driver("dvb");
    info = dvb_probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo("dvb",info+i);
#endif
    return 0;
}

static int
print_vbi(void)
{
    struct ng_devinfo *info;
    int i;

    print_driver("vbi");
    info = vbi_probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo("vbi",info+i);
    return 0;
}

static int
print_video(struct ng_vid_driver *vid)
{
    struct ng_devinfo    *info;
    int i;

    print_driver(vid->name);
    info = vid->probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo(vid->name,info+i);
    return 0;
}

static int
print_dsp(struct ng_dsp_driver *dsp, int record)
{
    struct ng_devinfo    *info;
    int i;

    print_driver(dsp->name);
    info = dsp->probe(record,debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo(dsp->name,info+i);
    return 0;
}

static int
print_mix(struct ng_mix_driver *mix)
{
    struct ng_devinfo    *info;
    struct ng_devinfo    *elem;
    int i,j;

    print_driver(mix->name);
    info = mix->probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++) {
	print_devinfo(mix->name,info+i);
	if (verbose) {
	    elem = mix->channels(info[i].device);
	    for (j = 0; elem && 0 != strlen(elem[j].name); j++)
		fprintf(stderr,"      %-14s - %s\n",
			elem[j].device, elem[j].name);
	}
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static void
usage(FILE *out)
{
    fprintf(out,
	    "debug tool -- probe devices\n"
	    "\n"
	    "options:\n"
	    "  -h          print this help text\n"
	    "  -v          be verbose\n"
	    "  -d          debug messages\n");
};

int main(int argc, char *argv[])
{
    struct list_head *item;
    struct ng_mix_driver *mix;
    struct ng_dsp_driver *dsp;
    struct ng_vid_driver *vid;
    int c;

    ng_init();
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hvd")))
	    break;
	switch (c) {
	case 'v':
	    verbose = 1;
	    break;
	case 'd':
	    debug = 1;
	    break;
	case 'h':
	    usage(stdout);
	    exit(0);
	default:
	    usage(stderr);
	    exit(1);
	}
    }
    
    /* dvb devices */
    fprintf(stderr,"probing dvb devices ...\n");
    print_dvb();
    fprintf(stderr,"\n");

    /* vbi devices */
    fprintf(stderr,"probing vbi devices ...\n");
    print_vbi();
    fprintf(stderr,"\n");

    /* video devices */
    fprintf(stderr,"probing video devices ...\n");
    list_for_each(item,&ng_vid_drivers) {
        vid = list_entry(item, struct ng_vid_driver, list);
	print_video(vid);
    }
    fprintf(stderr,"\n");

    /* dsp devices */
    fprintf(stderr,"probing dsp devices (playback) ...\n");
    list_for_each(item,&ng_dsp_drivers) {
        dsp = list_entry(item, struct ng_dsp_driver, list);
	print_dsp(dsp,0);
    }
    fprintf(stderr,"\n");

    fprintf(stderr,"probing dsp devices (record) ...\n");
    list_for_each(item,&ng_dsp_drivers) {
        dsp = list_entry(item, struct ng_dsp_driver, list);
	print_dsp(dsp,1);
    }
    fprintf(stderr,"\n");

    /* mixer devices */
    fprintf(stderr,"probing mixers ...\n");
    list_for_each(item,&ng_mix_drivers) {
        mix = list_entry(item, struct ng_mix_driver, list);
	print_mix(mix);
    }
    fprintf(stderr,"\n");

    return 0;
}
