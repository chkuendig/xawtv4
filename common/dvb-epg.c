#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <inttypes.h>

#include <glib.h>

#include "grab-ng.h"
#include "dvb-tuning.h"
#include "dvb.h"

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
    [ 0x52 ] = "entertainment programmes for 6 to 14",
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

struct eit_state {
    struct dvb_state    *dvb;
    int                 sec;
    int                 mask;
    int                 fd;
    GIOChannel          *ch;
    guint               id;
    int                 verbose;
    int                 alive;
};

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
    epg->row     = -1;
    epg->updated++;
    list_add_tail(&epg->next,&epg_list);
    eit_count_records++;
    return epg;
}

static time_t decode_mjd_time(int mjd, int start)
{
    struct tm tm;
    time_t t;
    int y2,m2,k;

    memset(&tm,0,sizeof(tm));

    /* taken as-is from EN-300-486 */
    y2 = (int)((mjd - 15078.2) / 365.25);
    m2 = (int)((mjd - 14956.1 - (int)(y2 * 365.25)) / 30.6001);
    k  = (m2 == 14 || m2 == 15) ? 1 : 0;
    tm.tm_mday = mjd - 14956 - (int)(y2 * 365.25) - (int)(m2 * 30.6001);
    tm.tm_year = y2 + k + 1900;
    tm.tm_mon  = m2 - 1 - k * 12;

    /* time is bcd ... */
    tm.tm_hour  = ((start >> 20) & 0xf) * 10;
    tm.tm_hour += ((start >> 16) & 0xf);
    tm.tm_min   = ((start >> 12) & 0xf) * 10;
    tm.tm_min  += ((start >>  8) & 0xf);
    tm.tm_sec   = ((start >>  4) & 0xf) * 10;
    tm.tm_sec  += ((start)       & 0xf);

#if 0
    fprintf(stderr,"mjd %d, time 0x%06x  =>  %04d-%02d-%02d %02d:%02d:%02d",
	    mjd, start,
	    tm.tm_year, tm.tm_mon, tm.tm_mday,
	    tm.tm_hour, tm.tm_min, tm.tm_sec);
#endif

    /* convert to unix epoch */
    tm.tm_mon--;
    tm.tm_year -= 1900;
    t = mktime(&tm);
    t -= timezone;

#if 0
    {
	char buf[16];

	strftime(buf,sizeof(buf),"%H:%M:%S",&tm);
	fprintf(stderr,"  =>  %s",buf);

	gmtime_r(&t,&tm);
	strftime(buf,sizeof(buf),"%H:%M:%S GMT",&tm);
	fprintf(stderr,"  =>  %s",buf);

	localtime_r(&t,&tm);
	strftime(buf,sizeof(buf),"%H:%M:%S %z",&tm);
	fprintf(stderr,"  =>  %s\n",buf);
    }
#endif
    
    return t;
}

static int decode_length(int length)
{
    int hour, min, sec;

    /* time is bcd ... */
    hour  = ((length >> 20) & 0xf) * 10;
    hour += ((length >> 16) & 0xf);
    min   = ((length >> 12) & 0xf) * 10;
    min  += ((length >>  8) & 0xf);
    sec   = ((length >>  4) & 0xf) * 10;
    sec  += ((length)       & 0xf);

    return hour * 3600 + min * 60 + sec;
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
    int dump,len,part,pcount;

    for (i = 0; i < dlen; i += desc[i+1] +2) {
	t = desc[i];
	l = desc[i+1];

	dump = 0;
	switch (t) {
	case 0x4d: /*  event (eid) */
	    l2 = desc[i+5];
	    l3 = desc[i+6+l2];
	    memcpy(epg->lang,desc+i+2,3);
	    mpeg_parse_psi_string(desc+i+6,    l2, epg->name,
				  sizeof(epg->name)-1);
	    mpeg_parse_psi_string(desc+i+7+l2, l3, epg->stext,
				  sizeof(epg->stext)-1);
	    if (0 == strcmp(epg->name, epg->stext))
		memset(epg->stext, 0, sizeof(epg->stext));
	    break;
	case 0x4e: /*  event (eid) */
	    len    = (epg->etext ? strlen(epg->etext) : 0);
	    part   = (desc[i+2] >> 4) & 0x0f;
	    pcount = (desc[i+2] >> 0) & 0x0f;
	    if (verbose > 1)
		fprintf(stderr,"eit: ext event: %d/%d\n",part,pcount);
	    if (0 == part)
		len = 0;
	    epg->etext = realloc(epg->etext, len+512);
	    l2 = desc[i+6];     /* item list (not implemented) */
	    l3 = desc[i+7+l2];  /* description */
	    mpeg_parse_psi_string(desc+i+8+l2, l3, epg->etext+len, 511);
	    if (l2) {
		if (verbose) {
		    fprintf(stderr," [not implemented: item list (ext descr)]");
		    dump = 1;
		}
	    }
	    break;
		
	case 0x4f: /*  event (eid) */
	    if (verbose > 1)
		fprintf(stderr," *time shift event");
	    break;
	case 0x50: /*  event (eid) */
	    if (verbose > 1)
		fprintf(stderr," component=%d,%d",
			desc[i+2] & 0x0f, desc[i+3]);
	    if (1 == (desc[i+2] & 0x0f)) {
		/* video */
		switch (desc[i+3]) {
		case 0x01:
		case 0x05:
		    epg->flags |= EPG_FLAG_VIDEO_4_3;
		    break;
		case 0x02:
		case 0x03:
		case 0x06:
		case 0x07:
		    epg->flags |= EPG_FLAG_VIDEO_16_9;
		    break;
		case 0x09:
		case 0x0d:
		    epg->flags |= EPG_FLAG_VIDEO_4_3;
		    epg->flags |= EPG_FLAG_VIDEO_HDTV;
		    break;
		case 0x0a:
		case 0x0b:
		case 0x0e:
		case 0x0f:
		    epg->flags |= EPG_FLAG_VIDEO_16_9;
		    epg->flags |= EPG_FLAG_VIDEO_HDTV;
		    break;
		}
	    }
	    if (2 == (desc[i+2] & 0x0f)) {
		/* audio */
		switch (desc[i+3]) {
		case 0x01:
		    epg->flags |= EPG_FLAG_AUDIO_MONO;
		    break;
		case 0x02:
		    epg->flags |= EPG_FLAG_AUDIO_DUAL;
		    break;
		case 0x03:
		    epg->flags |= EPG_FLAG_AUDIO_STEREO;
		    break;
		case 0x04:
		    epg->flags |= EPG_FLAG_AUDIO_MULTI;
		    break;
		case 0x05:
		    epg->flags |= EPG_FLAG_AUDIO_SURROUND;
		    break;
		}
	    }
	    if (3 == (desc[i+2] & 0x0f)) {
		/* subtitles / vbi */
		epg->flags |= EPG_FLAG_SUBTITLES;
	    }
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
	    for (j = 0; j < l; j+=2) {
		int d = desc[i+j+2];
		int c;
		if (!content_desc[d])
		    continue;
		for (c = 0; c < DIMOF(epg->cat); c++)
		    if (NULL == epg->cat[c])
			break;
		if (c == DIMOF(epg->cat))
		    continue;
		epg->cat[c] = content_desc[d];
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
	    if (verbose > 1)
		dump = 1;
	    break;
	}

	if (dump) {
	    fprintf(stderr," %02x[",desc[i]);
	    dump_data(desc+i+2,l);
	    fprintf(stderr,"]");
	}
    }
}

static int mpeg_parse_psi_eit(unsigned char *data, int verbose)
{
    int tab,pnr,version,current,len;
    int j,dlen,tsid,nid,part,parts,seen;
    struct epgitem *epg;
    int id,mjd,start,length;

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

    eit_last_new_record = time(NULL);
    if (verbose)
	fprintf(stderr,
		"ts [eit]: tab 0x%x pnr %3d ver %2d tsid %d nid %d [%d/%d]\n",
		tab, pnr, version, tsid, nid, part, parts);
    
    j = 112;
    while (j < len*8) {
	id     = mpeg_getbits(data,j,16);
	mjd    = mpeg_getbits(data,j+16,16);
	start  = mpeg_getbits(data,j+32,24);
	length = mpeg_getbits(data,j+56,24);
	epg = epgitem_get(tsid,pnr,id);
	epg->start  = decode_mjd_time(mjd,start);
	epg->stop   = epg->start + decode_length(length);
	epg->updated++;

	if (verbose > 1)
	    fprintf(stderr,"  id %d mjd %d time %06x du %06x r %d ca %d  #",
		    id, mjd, start, length,
		    mpeg_getbits(data,j+80,3),
		    mpeg_getbits(data,j+83,1));
	dlen = mpeg_getbits(data,j+84,12);
	j += 96;
	parse_eit_desc(data + j/8, dlen, epg, verbose);
	if (verbose > 1) {
	    fprintf(stderr,"\n");
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

static gboolean eit_data(GIOChannel *source, GIOCondition condition,
			 gpointer data)
{
    static const char alive[] = "-\\|/";
    static int count = 0;
    struct eit_state *eit = data;
    unsigned char buf[4096];

    if (dvb_demux_get_section(eit->fd, buf, sizeof(buf)) < 0) {
	eit->fd = dvb_demux_req_section(eit->dvb, eit->fd , 0x12,
					eit->sec, eit->mask, 0, 20);
	return TRUE;
    }
    mpeg_parse_psi_eit(buf, eit->verbose);
    if (eit->alive)
	fprintf(stderr,"%c  #%d %2ds\r",
		alive[(count++)%4], eit_count_records,
		(int)(time(NULL)-eit_last_new_record));
    return TRUE;
}

/* ----------------------------------------------------------------------- */
/* public interface                                                        */

LIST_HEAD(epg_list);
time_t eit_last_new_record;
int    eit_count_records;

struct eit_state* eit_add_watch(struct dvb_state *dvb,
				int section, int mask, int verbose, int alive)
{
    struct eit_state *eit;

    eit = malloc(sizeof(*eit));
    memset(eit,0,sizeof(*eit));

    eit->dvb  = dvb;
    eit->sec  = section;
    eit->mask = mask;
    eit->verbose = verbose;
    eit->alive = alive;
    eit->fd   = dvb_demux_req_section(eit->dvb, -1, 0x12, eit->sec, eit->mask, 0, 20);
    eit->ch   = g_io_channel_unix_new(eit->fd);
    eit->id   = g_io_add_watch(eit->ch, G_IO_IN, eit_data, eit);
    return eit;
}

void eit_del_watch(struct eit_state *eit)
{
    g_source_remove(eit->id);
    g_io_channel_unref(eit->ch);
    close(eit->fd);
    free(eit);
}

struct epgitem* eit_lookup(int tsid, int pnr, time_t when, int debug)
{
    struct epgitem   *epg;
    struct list_head *item;
    char tw[16],t1[16],t2[16];
    struct tm tm;

    if (debug) {
	localtime_r(&when,&tm);
	strftime(tw,sizeof(tw),"%d.%m. %H:%M:%S",&tm);
	fprintf(stderr,"\n!  %s\n",tw);
    }
    list_for_each(item,&epg_list) {
	epg = list_entry(item, struct epgitem, next);
	if (epg->tsid  != tsid)
	    continue;
	if (epg->pnr   != pnr)
	    continue;
	if (debug) {
	    localtime_r(&epg->start,&tm);
	    strftime(t1,sizeof(tw),"%d.%m. %H:%M:%S",&tm);
	    localtime_r(&epg->stop,&tm);
	    strftime(t2,sizeof(tw),"%d.%m. %H:%M:%S",&tm);
	    fprintf(stderr,"?  %s - %s  |  %s\n", t1, t2, epg->name);
	}
	if (epg->start > when)
	    continue;
	if (epg->stop  < when)
	    continue;
	return epg;
    }
    return NULL;
}
