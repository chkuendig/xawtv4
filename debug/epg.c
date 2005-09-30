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
#include "dvb.h"

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
    char *list, *name;
    unsigned char *enc;
    int type;
    char tz[8];

    LIBXML_TEST_VERSION;
    
    doc = xmlNewDoc(BAD_CAST "1.0");
    tv = xmlNewNode(NULL, BAD_CAST "tv");
    xmlDocSetRootElement(doc, tv);
    snprintf(tz,sizeof(tz),"GMT%+ld",-timezone/3600);

    cfg_sections_for_each("dvb-pr",list) {
	name = cfg_get_str("dvb-pr",list,"name");
	type = cfg_get_int("dvb-pr",list,"type",0);
	if (NULL == name)
	    continue;
	if (1 != type)
	    continue;

	chan = xmlNewChild(tv, NULL, BAD_CAST "channel", NULL);
	xmlNewProp(chan, BAD_CAST "id", BAD_CAST list);

	enc = xmlEncodeSpecialChars(doc, BAD_CAST name);
	xmlNewChild(chan, NULL, BAD_CAST "name", enc);
	xmlFree(enc);

	xmlNewChild(chan, NULL, BAD_CAST "group",     BAD_CAST "dvbepg");
	xmlNewChild(chan, NULL, BAD_CAST "time-zone", BAD_CAST tz);
	xmlNewChild(chan, NULL, BAD_CAST "country",   BAD_CAST "xx");
	xmlNewChild(chan, NULL, BAD_CAST "copyright", BAD_CAST "(no copyright info)");
	xmlNewChild(chan, NULL, BAD_CAST "url",       BAD_CAST "about:blank");
    }
    
    xmlSaveFormatFileEnc(filename, doc, "UTF-8", 1);
    fprintf(stderr,"Wrote tvbrowser channel file \"%s\".\n",filename);
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
    struct tm *tm;
    char buf[64],*list,*name;
    unsigned char *enc;
    int ts,pr,c,type;

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
	type = cfg_get_int("dvb-pr",list,"type",0);
	if (NULL == name)
	    continue;
	if (1 != type)
	    continue;
	if (2 == sscanf(list,"%d-%d",&ts,&pr)) {
	    if (0 != tsid && ts != tsid)
		continue;
	}
	chan = xmlNewChild(tv, NULL, BAD_CAST "channel", NULL);
	xmlNewProp(chan, BAD_CAST "id", BAD_CAST list);

	enc = xmlEncodeSpecialChars(doc, BAD_CAST name);
	xmlNewChild(chan, NULL, BAD_CAST "display-name", enc);
	xmlFree(enc);
    }
    
    list_for_each(item,&epg_list) {
	epg  = list_entry(item, struct epgitem, next);
	if (0 != tsid && epg->tsid != tsid)
	    continue;
	prog = xmlNewChild(tv, NULL, BAD_CAST "programme", NULL);

	sprintf(buf,"%d-%d",epg->tsid,epg->pnr);
	xmlNewProp(prog, BAD_CAST "channel", BAD_CAST buf);

	/* time */
	tm = localtime(&epg->start);
	strftime(buf,sizeof(buf),"%Y%m%d%H%M%S %Z",tm);
	xmlNewProp(prog, BAD_CAST "start", BAD_CAST buf);
	tm = localtime(&epg->stop);
	strftime(buf,sizeof(buf),"%Y%m%d%H%M%S %Z",tm);
	xmlNewProp(prog, BAD_CAST "stop", BAD_CAST buf);

	/* description */
	if (epg->name[0]) {
	    enc = xmlEncodeSpecialChars(doc, BAD_CAST epg->name);
	    xmlNewChild(prog, NULL, BAD_CAST "title", enc);
	    xmlFree(enc);
	}
	if (epg->stext[0]) {
	    enc = xmlEncodeSpecialChars(doc, BAD_CAST epg->stext);
	    xmlNewChild(prog, NULL, BAD_CAST "sub-title", enc);
	    xmlFree(enc);
	}
	if (epg->etext) {
	    enc = xmlEncodeSpecialChars(doc, BAD_CAST epg->etext);
	    xmlNewChild(prog, NULL, BAD_CAST "desc", enc);
	    xmlFree(enc);
	}

	/* category */
	for (c = 0; c < DIMOF(epg->cat) && NULL != epg->cat[c]; c++) {
	    enc = xmlEncodeSpecialChars(doc, BAD_CAST epg->cat[c]);
	    xmlNewChild(prog, NULL, BAD_CAST "category", enc);
	    xmlFree(enc);
	}

	/* video attributes */
	if (epg->flags & EPG_FLAGS_VIDEO) {
	    attr = xmlNewChild(prog, NULL, BAD_CAST "video", NULL);
	    if (epg->flags & EPG_FLAG_VIDEO_4_3)
		xmlNewChild(attr, NULL, BAD_CAST "aspect", BAD_CAST "4:3");
	    else if (epg->flags & EPG_FLAG_VIDEO_16_9)
		xmlNewChild(attr, NULL, BAD_CAST "aspect", BAD_CAST "16:9");
	}

	/* audio attributes */
	if (epg->flags & EPG_FLAGS_AUDIO) {
	    attr = xmlNewChild(prog, NULL, BAD_CAST "audio", NULL);
	    if (epg->flags & EPG_FLAG_AUDIO_SURROUND)
		xmlNewChild(attr, NULL, BAD_CAST "stereo", BAD_CAST "surround");
	    else if (epg->flags & EPG_FLAG_AUDIO_STEREO)
		xmlNewChild(attr, NULL, BAD_CAST "stereo", BAD_CAST "stereo");
	    else if (epg->flags & EPG_FLAG_AUDIO_MONO)
		xmlNewChild(attr, NULL, BAD_CAST "stereo", BAD_CAST "mono");
	}

	/* other attributes */
	if (epg->flags & EPG_FLAG_SUBTITLES) {
	    attr = xmlNewChild(prog, NULL, BAD_CAST "subtitles", NULL);
	    xmlNewProp(attr, BAD_CAST "type", BAD_CAST "teletext");
	}
    }

    xmlSaveFormatFileEnc(filename, doc, "UTF-8", 1);
    fprintf(stderr,"Wrote xmltv file \"%s\".\n",filename);
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}

/* ----------------------------------------------------------------------- */

int debug   = 0;
int verbose = 0;
int current = 0;
int notune  = 0;
int timeout = 32;

static void dvbwatch_tsid(struct psi_info *info, int event,
			  int tsid, int pnr, void *data)
{
    // struct psi_program *pr;

    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	fprintf(stderr,"Current stream: tsid %d.\n",tsid);
	current = tsid;
	break;
#if 0
    case DVBMON_EVENT_UPDATE_PR:
	pr = psi_program_get(info, tsid, pnr, 0);
	if (!pr)
	    return;
	if (tsid != current)
	    return;
	if (pr->type != 1)
	    return;
	if (pr->name[0] == '\0')
	    return;
	/* Hmm, get duplicates :-/ */
	fprintf(stderr,"  %s\n", pr->name);
#endif
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
	    "  -c          no tuning, scan current transponder only.\n"
	    "  -t          dump tvbrowser files.\n",
	    name);
};

int main(int argc, char *argv[])
{
    GMainContext *context;
    struct eit_state *eit;
    char filename[1024];
    char ts[8],*sec = NULL,*name;
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
    devs.dvbmon = dvbmon_init(devs.dvb, debug, 1, 1, 2);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_scanner,NULL);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_tsid,NULL);
    eit = eit_add_watch(devs.dvb, 0x50,0xf0, verbose, 1);
    context = g_main_context_default();

    if (!dvb_frontend_is_locked(devs.dvb)) {
	snprintf(filename,sizeof(filename),"%s/.tv/dvb-ts",getenv("HOME"));
	fprintf(stderr,"Hmm, frontend not locked, must tune first.\n");
	fprintf(stderr,"Getting tuning config [%s].\n",filename);
	cfg_parse_file("dvb-ts",filename);
	sec = cfg_sections_first("dvb-ts");
	if (NULL == sec)
	    fprintf(stderr,"Oops, no config available.\n");
    } else {
	fprintf(stderr,"Frontend is locked, fine.\n");
    }
    
    for (;;) {
	/* something to tune? */
	if (sec) {
	    if (notune) {
		fprintf(stderr,"Tuning is disabled, stopping here.\n");
		break;
	    }
	    name = cfg_get_str("dvb-ts",sec,"name");
	    fprintf(stderr,"Tuning now: tsid %s [%s].\n", sec, name ? name : "???");
	    if (0 != dvb_frontend_tune(devs.dvb, "dvb-ts", sec))
		break;
	    tuned   = 1;
	    current = 0;
	}

 	/* fish data */
	fprintf(stderr,"Going fish data ...\n");
	eit_last_new_record = time(NULL);
	while (!exit_application && time(NULL) - eit_last_new_record < timeout) {
	    alarm(timeout/3);
	    g_main_context_iteration(context,TRUE);
	}
	if (current) {
	    sprintf(filename,"%d.xml",current);
	    export_xmltv(current,filename);
	    sprintf(ts,"%d",current);
	    cfg_set_sflags("dvb-ts",ts,1,1);
	} else {
	    fprintf(stderr,"Hmm, no data received. Frontend is%s locked.\n",
		    dvb_frontend_is_locked(devs.dvb) ? "" : " not");
	}
	if (exit_application) {
	    fprintf(stderr,"Ctrl-C seen, stopping here.\n");
	    break;
	}

	/* more not-yet seen transport streams? */
	if (sec)
	    cfg_set_sflags("dvb-ts",sec,1,1);
	cfg_sections_for_each("dvb-ts",sec) {
	    if (!cfg_get_sflags("dvb-ts",sec))
		break;
	}
	if (NULL == sec) {
	    fprintf(stderr,"No more transport streams, stopping here.\n");
	    break;
	}
    }

    if (tvb) {
	export_xmltv(0,"TvData.xml");
	export_tvbrowser_channels("TvChannels.xml");
    }

    eit_del_watch(eit);
    dvbmon_fini(devs.dvbmon);
    device_fini();
    return 0;
}
