#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <glib.h>

#include "grab-ng.h"
#include "devs.h"
#include "parseconfig.h"
#include "tv-config.h"
#include "dvb-tuning.h"
#include "dvb-monitor.h"

/* ----------------------------------------------------------------------- */

static int exit_application;
static time_t last_new_eit_record;

static void termsig(int signal)
{
    exit_application++;
}

static void siginit(void)
{
    struct sigaction act,old;

    memset(&act,0,sizeof(act));
    sigemptyset(&act.sa_mask);

    act.sa_handler = termsig;
    sigaction(SIGINT,  &act, &old);
    sigaction(SIGTERM, &act, &old);
}

/* ----------------------------------------------------------------------- */

struct versions {
    struct list_head    next;
    int                 tab;
    int                 pnr;
    int                 tsid;
    int                 part;
    int                 version;
};
static LIST_HEAD(seen_list);

static int eit_seen(int tab, int pnr, int tsid, int part, int version)
{
    struct versions   *ver;
    struct list_head  *item;
    int seen = 0;

    list_for_each(item,&seen_list) {
	ver = list_entry(item, struct versions, next);
	if (ver->tab  != tab)
	    continue;
	if (ver->pnr  != pnr)
	    continue;
	if (ver->tsid != tsid)
	    continue;
	if (ver->part != part)
	    continue;
	if (ver->version == version)
	    seen = 1;
	ver->version = version;
	return seen;
    }
    ver = malloc(sizeof(*ver));
    memset(ver,0,sizeof(*ver));
    ver->tab     = tab;
    ver->pnr     = pnr;
    ver->tsid    = tsid;
    ver->part    = part;
    ver->version = version;
    list_add_tail(&ver->next,&seen_list);
    return seen;
}

/* ----------------------------------------------------------------------- */

#if 0
static char *content_group[16] = {
    [ 0x1 ] = "Movie/Drama",
    [ 0x2 ] = "News/Current affairs",
    [ 0x3 ] = "Show/Game show",
    [ 0x4 ] = "Sports",
    [ 0x5 ] = "Children's/Youth programmes",
    [ 0x6 ] = "Music/Ballet/Dance",
    [ 0x7 ] = "Arts/Culture (without music)",
    [ 0x8 ] = "Social/Political issues/Economics",
    [ 0x9 ] = "Education/ Science/Factual topics",
    [ 0xA ] = "Leisure hobbies",
    [ 0xB ] = "Special Characteristics",
};
#endif

static char *content_desc[256] = {
    [ 0x10 ] = "movie/drama (general)",
    [ 0x11 ] = "detective/thriller",
    [ 0x12 ] = "adventure/western/war",
    [ 0x13 ] = "science fiction/fantasy/horror",
    [ 0x14 ] = "comedy",
    [ 0x15 ] = "soap/melodrama/folkloric",
    [ 0x16 ] = "romance",
    [ 0x17 ] = "serious/classical/religious/historical movie/drama",
    [ 0x18 ] = "adult movie/drama",

    [ 0x20 ] = "news/current affairs (general)",
    [ 0x21 ] = "news/weather report",
    [ 0x22 ] = "news magazine",
    [ 0x23 ] = "documentary",
    [ 0x24 ] = "discussion/interview/debate",

    [ 0x30 ] = "show/game show (general)",
    [ 0x31 ] = "game show/quiz/contest",
    [ 0x32 ] = "variety show",
    [ 0x33 ] = "talk show",

    [ 0x40 ] = "sports (general)",
    [ 0x41 ] = "special events (Olympic Games, World Cup etc.)",
    [ 0x42 ] = "sports magazines",
    [ 0x43 ] = "football/soccer",
    [ 0x44 ] = "tennis/squash",
    [ 0x45 ] = "team sports (excluding football)",
    [ 0x46 ] = "athletics",
    [ 0x47 ] = "motor sport",
    [ 0x48 ] = "water sport",
    [ 0x49 ] = "winter sports",
    [ 0x4A ] = "equestrian",
    [ 0x4B ] = "martial sports",

    [ 0x50 ] = "children's/youth programmes (general)",
    [ 0x51 ] = "pre-school children's programmes",
    [ 0x52 ] = "entertainment programmes for 6 to14",
    [ 0x53 ] = "entertainment programmes for 10 to 16",
    [ 0x54 ] = "informational/educational/school programmes",
    [ 0x55 ] = "cartoons/puppets",

    [ 0x60 ] = "music/ballet/dance (general)",
    [ 0x61 ] = "rock/pop",
    [ 0x62 ] = "serious music/classical music",
    [ 0x63 ] = "folk/traditional music",
    [ 0x64 ] = "jazz",
    [ 0x65 ] = "musical/opera",
    [ 0x66 ] = "ballet",

    [ 0x70 ] = "arts/culture (without music, general)",
    [ 0x71 ] = "performing arts",
    [ 0x72 ] = "fine arts",
    [ 0x73 ] = "religion",
    [ 0x74 ] = "popular culture/traditional arts",
    [ 0x75 ] = "literature",
    [ 0x76 ] = "film/cinema",
    [ 0x77 ] = "experimental film/video",
    [ 0x78 ] = "broadcasting/press",
    [ 0x79 ] = "new media",
    [ 0x7A ] = "arts/culture magazines",
    [ 0x7B ] = "fashion",

    [ 0x80 ] = "social/political issues/economics (general)",
    [ 0x81 ] = "magazines/reports/documentary",
    [ 0x82 ] = "economics/social advisory",
    [ 0x83 ] = "remarkable people",

    [ 0x90 ] = "education/science/factual topics (general)",
    [ 0x91 ] = "nature/animals/environment",
    [ 0x92 ] = "technology/natural sciences",
    [ 0x93 ] = "medicine/physiology/psychology",
    [ 0x94 ] = "foreign countries/expeditions",
    [ 0x95 ] = "social/spiritual sciences",
    [ 0x96 ] = "further education",
    [ 0x97 ] = "languages",

    [ 0xA0 ] = "leisure hobbies (general)",
    [ 0xA1 ] = "tourism/travel",
    [ 0xA2 ] = "handicraft",
    [ 0xA3 ] = "motoring",
    [ 0xA4 ] = "fitness & health",
    [ 0xA5 ] = "cooking",
    [ 0xA6 ] = "advertizement/shopping",
    [ 0xA7 ] = "gardening",

    [ 0xB0 ] = "original language",
    [ 0xB1 ] = "black & white",
    [ 0xB2 ] = "unpublished",
    [ 0xB3 ] = "live broadcast",
};

/* ----------------------------------------------------------------------- */

struct epgitem {
    struct list_head    next;
    int                 id;
    int                 tsid;
    int                 pnr;
    int                 updated;
    int                 day;         /* YYYYDDMM */
    int                 start;       /* HHMMSS   */
    int                 length;      /* HHMMSS   */
    char                lang[4];
    char                name[64];
    char                stext[64];
    char                etext[256];
};
static LIST_HEAD(epg_list);

static struct epgitem* epgitem_get(int tsid, int pnr, int id)
{
    struct epgitem   *epg;
    struct list_head *item;

    list_for_each(item,&epg_list) {
	epg = list_entry(item, struct epgitem, next);
	if (epg->tsid != tsid)
	    continue;
	if (epg->pnr != pnr)
	    continue;
	if (epg->id != id)
	    continue;
	return epg;
    }
    epg = malloc(sizeof(*epg));
    memset(epg,0,sizeof(*epg));
    epg->tsid    = tsid;
    epg->pnr     = pnr;
    epg->id      = id;
    epg->updated = 1;
    list_add_tail(&epg->next,&epg_list);
    return epg;
}

static int decode_mjd(int mjd)
{
    int y,m,d,y2,m2,k;

    /* taken as-is from EN-300-486 */
    y2 = (int)((mjd - 15078.2) / 365.25);
    m2 = (int)((mjd - 14956.1 - (int)(y2 * 365.25)) / 30.6001);
    d = mjd - 14956 - (int)(y2 * 365.25) - (int)(m2 * 30.6001);
    k = (m2 == 14 || m2 == 15) ? 1 : 0;
    y = y2 + k;
    m = m2 - 1 - k * 12;

    return (y+1900) * 10000 + m * 100 + d;
}

static void dump_data(unsigned char *data, int len)
{
    int i;
    
    for (i = 0; i < len; i++) {
	if (isprint(data[i]))
	    fprintf(stderr,"%c", data[i]);
	else
	    fprintf(stderr,"\\x%02x", (int)data[i]);
    }
}

static void parse_eit_desc(unsigned char *desc, int dlen,
			   struct epgitem *epg, int verbose)
{
    int i,j,t,l,l2,l3;

    for (i = 0; i < dlen; i += desc[i+1] +2) {
	t = desc[i];
	l = desc[i+1];

	switch (t) {
	case 0x4d: /*  event (eid) */
	    l2 = desc[i+5];
	    l3 = desc[i+6+l2];
	    memcpy(epg->lang,desc+i+2,3);
	    mpeg_parse_psi_string(desc+i+6,    l2, epg->name,
				  sizeof(epg->name)-1);
	    mpeg_parse_psi_string(desc+i+7+l2, l3, epg->stext,
				  sizeof(epg->stext)-1);
	    break;
	case 0x4e: /*  event (eid) */
	    if (0 == desc[i+2]) {
		l2 = desc[i+6];     /* item list (not implemented) */
		l3 = desc[i+7+l2];  /* description */
		mpeg_parse_psi_string(desc+i+8+l2, l3, epg->etext,
				      sizeof(epg->etext)-1);
	    } else {
		/* FIXME: multiple extended descriptors */
	    }
	    break;

	case 0x4f: /*  event (eid) */
	    if (verbose > 1)
		fprintf(stderr," *time shift event");
	    break;
	case 0x50: /*  event (eid) */
	    if (verbose > 1)
		fprintf(stderr," *component");
	    break;
	case 0x54: /*  event (eid) */
	    if (verbose > 1) {
		for (j = 0; j < l; j+=2) {
		    int d = desc[i+j+2];
		    fprintf(stderr," content=0x%02x:",d);
		    if (content_desc[d])
			fprintf(stderr,"%s",content_desc[d]);
		    else
			fprintf(stderr,"?");
		}
	    }
	    break;
	case 0x55: /*  event (eid) */
	    if (verbose > 1)
		fprintf(stderr," *parental rating");
	    break;

	case 0x80: /* kategorie     ??? */
	case 0x82: /* aufnehmen     ??? */
	case 0x83: /* schlagzeilen  ??? */
	    break;
	    
	default:
	    if (verbose > 1) {
		fprintf(stderr," %02x[",desc[i]);
		dump_data(desc+i+2,l);
		fprintf(stderr,"]");
	    }
	}
    }
}

static int mpeg_parse_psi_eit(unsigned char *data, int verbose)
{
    int tab,pnr,version,current,len;
    int j,dlen,tsid,nid,part,parts,seen;
    struct epgitem *epg;
    int id,mjd;

    tab     = mpeg_getbits(data, 0,8);
    len     = mpeg_getbits(data,12,12) + 3 - 4;
    pnr     = mpeg_getbits(data,24,16);
    version = mpeg_getbits(data,42,5);
    current = mpeg_getbits(data,47,1);
    if (!current)
	return len+4;

    part  = mpeg_getbits(data,48, 8);
    parts = mpeg_getbits(data,56, 8);
    tsid  = mpeg_getbits(data,64,16);
    nid   = mpeg_getbits(data,80,16);
    seen  = eit_seen(tab,pnr,tsid,part,version);
    if (seen)
	return len+4;

    last_new_eit_record = time(NULL);
    if (verbose)
	fprintf(stderr,
		"ts [eit]: tab 0x%x pnr %3d ver %2d tsid %d nid %d [%d/%d]\n",
		tab, pnr, version, tsid, nid, part, parts);
    
    j = 112;
    while (j < len*8) {
	id  = mpeg_getbits(data,j,16);
	mjd = mpeg_getbits(data,j+16,16);
	epg = epgitem_get(tsid,pnr,id);
	epg->day    = decode_mjd(mjd);
	epg->start  = mpeg_getbits(data,j+32,24);
	epg->length = mpeg_getbits(data,j+56,24); 

	if (verbose > 1)
	    fprintf(stderr,"  id %d day %d time %06x du %06x r %d ca %d  #",
		    id, epg->day, epg->start, epg->length,
		    mpeg_getbits(data,j+80,3),
		    mpeg_getbits(data,j+83,1));
	dlen = mpeg_getbits(data,j+84,12);
	j += 96;
	parse_eit_desc(data + j/8, dlen, epg, verbose);
	if (verbose > 1) {
	    fprintf(stderr,"    n: %s\n",epg->name);
	    fprintf(stderr,"    s: %s\n",epg->stext);
	    fprintf(stderr,"    e: %s\n",epg->etext);
	    fprintf(stderr,"\n");
	}
	j += 8*dlen;
    }
    
    if (verbose > 1)
	fprintf(stderr,"\n");
    return len+4;
}

/* ----------------------------------------------------------------------- */

struct eit_state {
    struct dvb_state    *dvb;
    int                 sec;
    int                 mask;
    int                 fd;
    GIOChannel          *ch;
    guint               id;
    int                 verbose;
};

static gboolean eit_data(GIOChannel *source, GIOCondition condition,
			 gpointer data)
{
    struct eit_state *eit = data;
    unsigned char buf[4096];

    if (dvb_demux_get_section(eit->fd, buf, sizeof(buf)) < 0)
	return FALSE;
    mpeg_parse_psi_eit(buf, eit->verbose);
    return TRUE;
}

static struct eit_state* eit_add_watch(struct dvb_state *dvb,
				       int section, int mask, int verbose)
{
    struct eit_state *eit;

    eit = malloc(sizeof(*eit));
    memset(eit,0,sizeof(*eit));

    eit->dvb  = dvb;
    eit->sec  = section;
    eit->mask = mask;
    eit->verbose = verbose;
    eit->fd   = dvb_demux_req_section(dvb, -1, 0x12, eit->sec, eit->mask, 0, 20);
    eit->ch   = g_io_channel_unix_new(eit->fd);
    eit->id   = g_io_add_watch(eit->ch, G_IO_IN, eit_data, eit);
    return eit;
}

/* ----------------------------------------------------------------------- */

static int export_xmltv(char *filename)
{
    xmlDocPtr doc;
    xmlNodePtr tv, prog, chan;
    struct epgitem   *epg;
    struct list_head *item;
    char buf[64],*list,*name,*enc;

    LIBXML_TEST_VERSION;

    // xmlSubstituteEntitiesDefault(1);
    doc = xmlNewDoc(BAD_CAST "1.0");
    tv = xmlNewNode(NULL, BAD_CAST "tv");
    xmlDocSetRootElement(doc, tv);
    xmlNewProp(tv, BAD_CAST "generator-info-name", BAD_CAST "xawtv");

    cfg_sections_for_each("dvb-pr",list) {
	name = cfg_get_str("dvb-pr",list,"name");
	if (NULL == name)
	    continue;
	chan = xmlNewChild(tv, NULL, BAD_CAST "channel", NULL);
	xmlNewProp(chan, BAD_CAST "id", list);

	enc = xmlEncodeSpecialChars(doc, name);
	xmlNewChild(chan, NULL, BAD_CAST "display-name", enc);
	xmlFree(enc);
    }
    
    list_for_each(item,&epg_list) {
	epg  = list_entry(item, struct epgitem, next);
	prog = xmlNewChild(tv, NULL, BAD_CAST "programme", NULL);

	sprintf(buf,"%d-%d",epg->tsid,epg->pnr);
	xmlNewProp(prog, BAD_CAST "channel", buf);
	sprintf(buf,"%08d%06d GMT",epg->day,epg->start);
	xmlNewProp(prog, BAD_CAST "start", buf);

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
	if (epg->etext[0]) {
	    enc = xmlEncodeSpecialChars(doc, epg->etext);
	    xmlNewChild(prog, NULL, BAD_CAST "desc", enc);
	    xmlFree(enc);
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
int verbose = 1;
int current = 0;

static void dvbwatch_tsid(struct psi_info *info, int event,
			  int tsid, int pnr, void *data)
{
    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	current = tsid;
	break;
    }
}

int main(int argc, char *argv[])
{
    GMainContext *context;
    char filename[32];

    siginit();
    ng_init();

    devlist_init(1, 0, 0);
    device_init(NULL);
    if (NULL == devs.dvb) {
	fprintf(stderr,"no dvb device found\n");
	exit(1);
    }
    devs.dvbmon = dvbmon_init(devs.dvb, debug, 1, 2);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_scanner,NULL);
    dvbmon_add_callback(devs.dvbmon,dvbwatch_tsid,NULL);
    // eit_add_watch(devs.dvb,0x4e,0xff,verbose);
    eit_add_watch(devs.dvb,0x50,0xf0,verbose);
    last_new_eit_record = time(NULL);

    context = g_main_context_default();
    while (!exit_application && time(NULL) - last_new_eit_record < 5)
	g_main_context_iteration(context,TRUE);

    dvbmon_fini(devs.dvbmon);
    device_fini();
    
    sprintf(filename,"%d.xml",current);
    export_xmltv(filename);
    return 0;
}
