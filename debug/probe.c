#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "grab-ng.h"

/* ------------------------------------------------------------------ */

static int verbose;

static void
usage(FILE *out)
{
    fprintf(out,
	    "debug tool -- probe devices\n"
	    "\n"
	    "options:\n"
	    "  -h          print this help text\n"
	    "  -v          be verbose\n");
};

static int
print_video(struct ng_vid_driver *vid)
{
    struct ng_devinfo    *info;
    int i;

    fprintf(stderr,"  %s ...\n",vid->name);
    info = vid->probe();
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	fprintf(stderr,"    %-16s - %s\n",
		info[i].device, info[i].name);
    return 0;
}

static int
print_dsp(struct ng_dsp_driver *dsp, int record)
{
    struct ng_devinfo    *info;
    int i;

    fprintf(stderr,"  %s ...\n",dsp->name);
    info = dsp->probe(record);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	fprintf(stderr,"    %-16s - %s\n",
		info[i].device, info[i].name);
    return 0;
}

static int
print_mix(struct ng_mix_driver *mix)
{
    struct ng_devinfo    *info;
    struct ng_devinfo    *elem;
    int i,j;

    fprintf(stderr,"  %s ...\n",mix->name);
    info = mix->probe();
    for (i = 0; info && 0 != strlen(info[i].name); i++) {
	fprintf(stderr,"    %-16s - %s\n",
		info[i].device, info[i].name);
	if (verbose) {
	    elem = mix->channels(info[i].device);
	    for (j = 0; elem && 0 != strlen(elem[j].name); j++)
		fprintf(stderr,"      %-14s - %s\n",
			elem[j].device, elem[j].name);
	}
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct list_head *item;
    struct ng_mix_driver *mix;
    struct ng_dsp_driver *dsp;
    struct ng_vid_driver *vid;
    int c;

    ng_init();
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hv")))
	    break;
	switch (c) {
	case 'v':
	    verbose = 1;
	    break;
	case 'h':
	    usage(stdout);
	    exit(0);
	default:
	    usage(stderr);
	    exit(1);
	}
    }
    
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
