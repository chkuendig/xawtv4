#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <inttypes.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <glib.h>

#include "grab-ng.h"
#include "devs.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "dvb-tuning.h"
#include "dvb-monitor.h"
#include "dvb-epg.h"

/* ----------------------------------------------------------------------- */

static int exit_application;

static void termsig(int signal)
{
    exit_application++;
}

static void ignore_sig(int signal)
{
    /* nothing */
}

static void siginit(void)
{
    struct sigaction act,old;

    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);

    act.sa_handler = termsig;
    sigaction(SIGINT,  &act, &old);
    sigaction(SIGTERM, &act, &old);
    sigaction(SIGTERM, &act, &old);

    act.sa_handler = ignore_sig;
    sigaction(SIGALRM, &act, &old);
}

/* ----------------------------------------------------------------------- */

static int export_tvbrowser_channels(char *filename)
{
    xmlDocPtr doc;
    xmlNodePtr tv, chan;
    char *list,*name,*enc;
    char tz[8];

    LIBXML_TEST_VERSION;
    
    doc = xmlNewDoc(BAD_CAST "1.0");
    tv = xmlNewNode(NULL, BAD_CAST "tv");
    xmlDocSetRootElement(doc, tv);
    snprintf(tz,sizeof(tz),"GMT%+ld",-timezone/3600);

    cfg_sections_for_each("dvb-pr",list) {
	name = cfg_get_str("dvb-pr",list,"name");
	if (NULL == name)
	    continue;
	chan = xmlNewChild(tv, NULL, BAD_CAST "channel", NULL);
	xmlNewProp(chan, BAD_CAST "id", list);

	enc = xmlEncodeSpecialChars(doc, name);
	xmlNewChild(chan, NULL, BAD_CAST "name", enc);
	xmlFree(enc);

	xmlNewChild(chan, NULL, BAD_CAST "group",     "dvbepg");
	xmlNewChild(chan, NULL, BAD_CAST "time-zone", tz);
	xmlNewChild(chan, NULL, BAD_CAST "country",   "xx");
	xmlNewChild(chan, NULL, BAD_CAST "copyright", "none");
	xmlNewChild(chan, NULL, BAD_CAST "url",       "about:blank");
    }
    
    xmlSaveFormatFileEnc(filename, doc, "UTF-8", 1);
    fprintf(stderr,"wrote tvbrowser file \"%s\"\n",filename);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}

static int export_xmltv(int tsid, char *filename)
{
    xmlDocPtr doc;
    xmlNodePtr tv, prog, chan, attr;
    struct epgitem   *epg;
    struct list_head *item;
    time_t stop;
    struct tm *tm;
    char buf[64],*list,*name,*enc;
    int ts,pr,c;

    LIBXML_TEST_VERSION;

    doc = xmlNewDoc(BAD_CAST "1.0");
    tv = xmlNewNode(NULL, BAD_CAST "tv");
    xmlDocSetRootElement(doc, tv);
    xmlNewProp(tv, BAD_CAST "generator-info-name",
	       BAD_CAST "xawtv/" VERSION);
    xmlNewProp(tv, BAD_CAST "generator-info-url",
	       BAD_CAST "http://linux.bytesex.org/xawtv/");
    xmlNewProp(tv, BAD_CAST "source-info-name",
	       BAD_CAST "DVB EPG (event information tables)");

    cfg_sections_for_each("dvb-pr",list) {
	name = cfg_get_str("dvb-pr",list,"name");
	if (NULL == name)
	    continue;
	if (2 == sscanf(list,"%d-%d",&ts,&pr)) {
	    if (0 != tsid && ts != tsid)
		continue;
	}
	chan = xmlNewChild(tv, NULL, BAD_CAST "channel", NULL);
	xmlNewProp(chan, BAD_CAST "id", list);

	enc = xmlEncodeSpecialChars(doc, name);
	xmlNewChild(chan, NULL, BAD_CAST "display-name", enc);
	xmlFree(enc);
    }
    
    list_for_each(item,&epg_list) {
	epg  = list_entry(item, struct epgitem, next);
	if (0 != tsid && epg->tsid != tsid)
	    continue;
	prog = xmlNewChild(tv, NULL, BAD_CAST "programme", NULL);

	sprintf(buf,"%d-%d",epg->tsid,epg->pnr);
	xmlNewProp(prog, BAD_CAST "channel", buf);

	/* time */
	tm = localtime(&epg->start);
	strftime(buf,sizeof(buf),"%Y%m%d%H%M%S %Z",tm);
	xmlNewProp(prog, BAD_CAST "start", buf);

	stop = epg->start + epg->length;
	tm = localtime(&stop);
	strftime(buf,sizeof(buf),"%Y%m%d%H%M%S %Z",tm);
	xmlNewProp(prog, BAD_CAST "stop", buf);

	/* description */
	if (epg->name[0]) {
	    enc = xmlEncodeSpecialChars(doc, epg->name);
	    xmlNewChild(prog, NULL, BAD_CAST "title", enc);
	    xmlFree(enc);
	}
	if (epg->stext[0]) {
	    enc = xmlEncodeSpecialChars(doc, epg->stext);
	    xmlNewChild(prog, NULL, BAD_CAST "sub-title", enc);
	    xmlFree(enc);
	}
	if (epg->etext) {
	    enc = xmlEncodeSpecialChars(doc, epg->etext);
	    xmlNewChild(prog, NULL, BAD_CAST "desc", enc);
	    xmlFree(enc);
	}

	/* category */
	for (c = 0; c < DIMOF(epg->cat) && NULL != epg->cat[c]; c++) {
	    enc = xmlEncodeSpecialChars(doc, epg->cat[c]);
	    xmlNewChild(prog, NULL, BAD_CAST "category", enc);
	    xmlFree(enc);
	}

	/* video attributes */
	if (epg->flags & EPG_FLAGS_VIDEO) {
	    attr = xmlNewChild(prog, NULL, BAD_CAST "video", NULL);
	    if (epg->flags & EPG_FLAG_VIDEO_4_3)
		xmlNewChild(attr, NULL, BAD_CAST "aspect", "4:3");
	    else if (epg->flags & EPG_FLAG_VIDEO_16_9)
		xmlNewChild(attr, NULL, BAD_CAST "aspect", "16:9");
	}

	/* audio attributes */
	if (epg->flags & EPG_FLAGS_AUDIO) {
	    attr = xmlNewChild(prog, NULL, BAD_CAST "audio", NULL);
	    if (epg->flags & EPG_FLAG_AUDIO_SURROUND)
		xmlNewChild(attr, NULL, BAD_CAST "stereo", "surround");
	    else if (epg->flags & EPG_FLAG_AUDIO_STEREO)
		xmlNewChild(attr, NULL, BAD_CAST "stereo", "stereo");
	    else if (epg->flags & EPG_FLAG_AUDIO_MONO)
		xmlNewChild(attr, NULL, BAD_CAST "stereo", "mono");
	}

	/* other attributes */
	if (epg->flags & EPG_FLAG_SUBTITLES) {
	    attr = xmlNewChild(prog, NULL, BAD_CAST "subtitles", NULL);
	    xmlNewProp(attr, BAD_CAST "type", BAD_CAST "teletext");
	}
    }

    xmlSaveFormatFileEnc(filename, doc, "UTF-8", 1);
    fprintf(stderr,"wrote xmltv file \"%s\"\n",filename);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}

/* ----------------------------------------------------------------------- */

int debug   = 0;
int verbose = 0;
int current = 0;
int notune  = 0;

static void dvbwatch_tsid(struct psi_info *info, int event,
			  int tsid, int pnr, void *data)
{
    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	fprintf(stderr,"read: tsid %d\n",tsid);
	current = tsid;
	break;
    }
}

static void
usage(FILE *out, char *argv0)
{
    char *name;

    if (NULL != (name = strrchr(argv0,'/')))
	name++;
    else
	name = argv0;

    fprintf(out,
	    "fish epg data from dvb card, write xmltv files to cwd\n"
	    "\n"
	    "usage: %s [ options ]\n"
	    "options:\n"
	    "  -h          print this help text.\n"
	    "  -v          be verbose.\n"
	    "  -c          currently tuned transponder only.\n"
	    "  -t          dump tvbrowser files.\n",
	    name);
};

int main(int argc, char *argv[])
{
    GMainContext *context;
    char filename[32],ts[8],*sec = NULL,*name;
    int tuned = 0;
    int tvb = 0;
    int c;
    
    for (;;) {
	if (-1 == (c = getopt(argc, argv, "hvct")))
	    break;
	switch (c) {
	case 'v':
	    verbose++;
	    break;
	case 'c':
	    notune = 1;
	    break;
	case 't':
	    tvb = 1;
	    break;
	case 'h':
	    usage(stdout,argv[0]);
	    exit(0);
	default:
	    usage(stderr,argv[0]);
	    exit(1);
	}
    }

    siginit();
    devlist_init(1, 0, 0);
    device_init(NULL);
    if (NULL == devs.dvb) {
	fprintf(stderr,"no dvb device found\n");
	exit(1);
    }
    devs.dvbmon = dvbmon_init(devs.dvb, debug, 1, 2);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_scanner,NULL);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_tsid,NULL);
    eit_add_watch(devs.dvb, 0x50,0xf0, verbose, 1);
    context = g_main_context_default();

    for (;;) {
	/* fish data */
	eit_last_new_record = time(NULL);
	while (!exit_application && time(NULL) - eit_last_new_record < 10) {
	    alarm(3);
	    g_main_context_iteration(context,TRUE);
	}
	if (current) {
	    sprintf(filename,"%d.xml",current);
	    export_xmltv(current,filename);
	    sprintf(ts,"%d",current);
	    cfg_set_sflags("dvb-ts",ts,1,1);
	} else {
	    if (tuned)
		fprintf(stderr,"Hmm, no data received, tuning failed?\n");
	    else
		fprintf(stderr,"Hmm, no data received, dvb card not tuned?\n");
	}
	if (exit_application)
	    break;
	if (notune)
	    break;

	/* more not-yet seen transport streams? */
	if (sec)
	    cfg_set_sflags("dvb-ts",sec,1,1);
	cfg_sections_for_each("dvb-ts",sec) {
	    if (!cfg_get_sflags("dvb-ts",sec))
		break;
	}
	if (NULL == sec)
	    break;

	/* yes! => tune */
	name = cfg_get_str("dvb-ts",sec,"name");
	fprintf(stderr,"tune: tsid %s [%s]\n", sec, name ? name : "???");
	if (0 != dvb_frontend_tune(devs.dvb, "dvb-ts", sec))
	    break;
	tuned   = 1;
	current = 0;
    }

    if (tvb) {
	export_xmltv(0,"TvData.xml");
	export_tvbrowser_channels("TvChannels.xml");
    }

    dvbmon_fini(devs.dvbmon);
    device_fini();
    return 0;
}
