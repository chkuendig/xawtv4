/*
 * maintain dvb transponder informations in glib-based apps.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <linux/dvb/frontend.h>

#include <glib.h>

#include "grab-ng.h"
#include "dvb-tuning.h"
#include "dvb.h"

#include "parseconfig.h"
#include "tv-config.h"

/* ----------------------------------------------------------------------------- */
/* structs & prototypes                                                          */

struct table {
    struct list_head    next;
    char                *name;
    int                 pid;
    int                 sec;
    int                 fd;
    GIOChannel          *ch;
    guint               id;
    int                 once;
    int                 done;
};

struct version {
    struct list_head    next;
    char                *name;
    int                 id;
    int                 version;
};

struct callback {
    struct list_head    next;
    dvbmon_notify       func;
    void                *data;
};

struct dvbmon {
    int                 verbose;
    int                 tabdebug;
    int                 timeout;
    int                 tabfds;
    int                 tablimit;
    struct dvb_state    *dvb;
    struct psi_info     *info;

    struct list_head    tables;
    struct list_head    versions;
    struct list_head    callbacks;
};

static void table_add(struct dvbmon *dm, char *name, int pid, int sec,
		      int oneshot);
static void table_open(struct dvbmon *dm, struct table *tab);
static void table_refresh(struct dvbmon *dm, struct table *tab);
static void table_close(struct dvbmon *dm, struct table *tab);
static void table_del(struct dvbmon *dm, int pid, int sec);
static void table_next(struct dvbmon *dm);

/* ----------------------------------------------------------------------------- */
/* internal functions                                                            */

static void call_callbacks(struct dvbmon* dm, int event, int tsid, int pnr)
{
    struct callback   *cb;
    struct list_head  *item;

    list_for_each(item,&dm->callbacks) {
	cb = list_entry(item, struct callback, next);
	cb->func(dm->info,event,tsid,pnr,cb->data);
    }
}

static int table_data_seen(struct dvbmon *dm, char *name, int id, int version)
{
    struct version    *ver;
    struct list_head  *item;
    int seen = 0;

    list_for_each(item,&dm->versions) {
	ver = list_entry(item, struct version, next);
	if (ver->name == name && ver->id == id) {
	    if (ver->version == version)
		seen = 1;
	    ver->version = version;
	    return seen;
	}
    }
    ver = malloc(sizeof(*ver));
    memset(ver,0,sizeof(*ver));
    ver->name    = name;
    ver->id      = id;
    ver->version = version;
    list_add_tail(&ver->next,&dm->versions);
    return seen;
}

static gboolean table_data(GIOChannel *source, GIOCondition condition,
			   gpointer data)
{
    struct dvbmon *dm = data;
    struct table *tab = NULL;
    struct list_head *item;
    struct psi_program *pr;
    struct psi_stream *stream;
    int id, version, current, old_tsid;
    char buf[4096];

    list_for_each(item,&dm->tables) {
	tab = list_entry(item, struct table, next);
	if (tab->ch == source)
	    break;
	tab = NULL;
    }
    if (NULL == tab) {
	fprintf(stderr,"dvbmon: unknown giochannel %p\n",source);
	return FALSE;
    }
    
    /* get data */
    if (dvb_demux_get_section(tab->fd, buf, sizeof(buf)) < 0) {
	if (dvb_debug)
	    fprintf(stderr,"dvbmon: reading %s failed (frontend not locked?), "
		    "fd %d, trying to re-init.\n", tab->name, tab->fd);
	table_refresh(dm,tab);
	return TRUE;
    }
    if (tab->once) {
	table_close(dm,tab);
	table_next(dm);
    }
    id      = mpeg_getbits(buf,24,16);
    version = mpeg_getbits(buf,42,5);
    current = mpeg_getbits(buf,47,1);
    if (!current)
	return TRUE;
    if (table_data_seen(dm, tab->name, id, version) &&
	0x00 != tab->sec /* pat */)
	return TRUE;

    switch (tab->sec) {
    case 0x00: /* pat */
	old_tsid = dm->info->tsid;
	mpeg_parse_psi_pat(dm->info, buf, dm->verbose);
	if (old_tsid != dm->info->tsid)
	    call_callbacks(dm, DVBMON_EVENT_SWITCH_TS, dm->info->tsid, 0);
	break;
    case 0x02: /* pmt */
	pr = psi_program_get(dm->info, dm->info->tsid, id, 0);
	if (!pr) {
	    fprintf(stderr,"dvbmon: 404: tsid %d pid %d\n", dm->info->tsid, id);
	    break;
	}
	mpeg_parse_psi_pmt(pr, buf, dm->verbose);
	break;
    case 0x40: /* nit this  */
    case 0x41: /* nit other */
	mpeg_parse_psi_nit(dm->info, buf, dm->verbose);
	break;
    case 0x42: /* sdt this  */
    case 0x46: /* sdt other */
	mpeg_parse_psi_sdt(dm->info, buf, dm->verbose);
	break;
    default:
	fprintf(stderr,"dvbmon: oops: sec=0x%02x\n",tab->sec);
	break;
    }

    /* check for changes */
    if (dm->info->pat_updated) {
	dm->info->pat_updated = 0;
	if (dm->verbose)
	    fprintf(stderr,"dvbmon: updated: pat\n");
	list_for_each(item,&dm->info->programs) {
	    pr = list_entry(item, struct psi_program, next);
	    if (!pr->seen)
		table_del(dm, pr->p_pid, 2);
	}
	list_for_each(item,&dm->info->programs) {
	    pr = list_entry(item, struct psi_program, next);
	    if (pr->seen && pr->p_pid)
		table_add(dm, "pmt", pr->p_pid, 2, 1);
	    pr->seen = 0;
	}
    }

    /* inform callbacks */
    list_for_each(item,&dm->info->streams) {
        stream = list_entry(item, struct psi_stream, next);
	if (!stream->updated)
	    continue;
	stream->updated = 0;
	call_callbacks(dm, DVBMON_EVENT_UPDATE_TS, stream->tsid, 0);
    }
    list_for_each(item,&dm->info->programs) {
	pr = list_entry(item, struct psi_program, next);
	if (!pr->updated)
	    continue;
	pr->updated = 0;
	call_callbacks(dm, DVBMON_EVENT_UPDATE_PR, pr->tsid, pr->pnr);
    }
    
    return TRUE;
}

static struct table* table_find(struct dvbmon *dm, int pid, int sec)
{
    struct table      *tab;
    struct list_head  *item;

    list_for_each(item,&dm->tables) {
	tab = list_entry(item, struct table, next);
	if (tab->pid == pid && tab->sec == sec)
	    return tab;
    }
    return NULL;
}

static void table_open(struct dvbmon *dm, struct table *tab)
{
    if (tab->once && dm->tabfds >= dm->tablimit)
	return;

    tab->fd = dvb_demux_req_section(dm->dvb, -1, tab->pid, tab->sec, 0xff,
				    tab->once, dm->timeout);
    if (-1 == tab->fd)
	return;

    dm->tabfds++;
    tab->ch  = g_io_channel_unix_new(tab->fd);
    tab->id  = g_io_add_watch(tab->ch, G_IO_IN, table_data, dm);
    if (dm->tabdebug)
	fprintf(stderr,"dvbmon: open:  %s %4d | fd=%d n=%d\n",
		tab->name, tab->pid, tab->fd, dm->tabfds);
}

static void table_close(struct dvbmon *dm, struct table *tab)
{
    if (-1 == tab->fd)
	return;

    g_source_remove(tab->id);
    g_io_channel_unref(tab->ch);
    close(tab->fd);
    tab->id = 0;
    tab->ch = 0;
    tab->fd = -1;
    if (tab->once)
	tab->done = 1;

    dm->tabfds--;
    if (dm->tabdebug)
	fprintf(stderr,"dvbmon: close: %s %4d | n=%d\n",
		tab->name, tab->pid, dm->tabfds);
}

static void table_next(struct dvbmon *dm)
{
    struct table      *tab;
    struct list_head  *item;

    list_for_each(item,&dm->tables) {
	tab = list_entry(item, struct table, next);
	if (tab->fd != -1)
	    continue;
	if (tab->done)
	    continue;
	table_open(dm,tab);
	if (dm->tabfds >= dm->tablimit)
	    return;
    }
}

static void table_add(struct dvbmon *dm, char *name, int pid, int sec,
		      int oneshot)
{
    struct table *tab; 

    tab = table_find(dm, pid, sec);
    if (tab)
	return;
    tab = malloc(sizeof(*tab));
    memset(tab,0,sizeof(*tab));
    tab->name = name;
    tab->pid  = pid;
    tab->sec  = sec;
    tab->fd   = -1;
    tab->once = oneshot;
    tab->done = 0;
    list_add_tail(&tab->next,&dm->tables);
    if (dm->tabdebug)
	fprintf(stderr,"dvbmon: add:   %s %4d | sec=0x%02x once=%d\n",
		tab->name, tab->pid, tab->sec, tab->once);

    table_open(dm,tab);
}

static void table_del(struct dvbmon *dm, int pid, int sec)
{
    struct table      *tab;

    tab = table_find(dm, pid, sec);
    if (NULL == tab)
	return;
    table_close(dm,tab);

    if (dm->tabdebug)
	fprintf(stderr,"dvbmon: del:   %s %4d\n", tab->name, tab->pid);
    list_del(&tab->next);
    free(tab);
}

static void table_refresh(struct dvbmon *dm, struct table *tab)
{
    tab->fd = dvb_demux_req_section(dm->dvb, tab->fd, tab->pid,
				    tab->sec, 0xff, 0, dm->timeout);
    if (-1 == tab->fd) {
	fprintf(stderr,"%s: failed\n",__FUNCTION__);
	g_source_remove(tab->id);
	g_io_channel_unref(tab->ch);
	list_del(&tab->next);
	free(tab);
	return;
    }
}

/* ----------------------------------------------------------------------------- */
/* external interface                                                            */

struct dvbmon*
dvbmon_init(struct dvb_state *dvb, int verbose, int others, int pmts)
{
    struct dvbmon *dm;

    dm = malloc(sizeof(*dm));
    memset(dm,0,sizeof(*dm));
    INIT_LIST_HEAD(&dm->tables);
    INIT_LIST_HEAD(&dm->versions);
    INIT_LIST_HEAD(&dm->callbacks);

    dm->verbose  = verbose;
    dm->tabdebug = 0;
    dm->tablimit = (others ? 5 : 3) + pmts;
    dm->timeout  = 60;
    dm->dvb      = dvb;
    dm->info     = psi_info_alloc();
    if (dm->dvb) {
	if (dm->verbose)
	    fprintf(stderr,"dvbmon: hwinit ok\n");
	table_add(dm, "pat",   0x00, 0x00, 0);
	table_add(dm, "nit",   0x10, 0x40, 0);
	table_add(dm, "sdt",   0x11, 0x42, 0);
	if (others) {
	    table_add(dm, "nit",   0x10, 0x41, 0);
	    table_add(dm, "sdt",   0x11, 0x46, 0);
	}
    } else {
	fprintf(stderr,"dvbmon: hwinit FAILED\n");
    }
    return dm;
}

void dvbmon_fini(struct dvbmon* dm)
{
    struct list_head  *item, *safe;
    struct version    *ver;
    struct table      *tab;
    struct callback   *cb;

    call_callbacks(dm, DVBMON_EVENT_DESTROY, 0, 0);
    list_for_each_safe(item,safe,&dm->tables) {
	tab = list_entry(item, struct table, next);
	table_del(dm, tab->pid, tab->sec);
    };
    list_for_each_safe(item,safe,&dm->versions) {
	ver = list_entry(item, struct version, next);
	list_del(&ver->next);
	free(ver);
    };
    list_for_each_safe(item,safe,&dm->callbacks) {
        cb = list_entry(item, struct callback, next);
	list_del(&cb->next);
	free(cb);
    };
    psi_info_free(dm->info);
    free(dm);
}

void dvbmon_refresh(struct dvbmon* dm)
{
    struct list_head  *item;
    struct version    *ver;

    list_for_each(item,&dm->versions) {
	ver = list_entry(item, struct version, next);
	ver->version = PSI_NEW;
    }
}

void dvbmon_add_callback(struct dvbmon* dm, dvbmon_notify func, void *data)
{
    struct callback *cb;

    cb = malloc(sizeof(*cb));
    memset(cb,0,sizeof(*cb));
    cb->func = func;
    cb->data = data;
    list_add_tail(&cb->next,&dm->callbacks);
}

void dvbmon_del_callback(struct dvbmon* dm, dvbmon_notify func, void *data)
{
    struct callback   *cb = NULL;
    struct list_head  *item;

    list_for_each(item,&dm->callbacks) {
	cb = list_entry(item, struct callback, next);
	if (cb->func == func && cb->data == data)
	    break;
	cb = NULL;
    }
    if (NULL == cb) {
	fprintf(stderr,"dvbmon: oops: rm unknown cb %p %p\n",func,data);
	return;
    }
    list_del(&cb->next);
    free(cb);
}

/* ----------------------------------------------------------------------------- */
/* callbacks                                                                     */

void dvbwatch_logger(struct psi_info *info, int event,
		     int tsid, int pnr, void *data)
{
    struct psi_stream  *stream;
    struct psi_program *pr;
    
    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	fprintf(stderr,"%s: switch ts 0x%04x\n",__FUNCTION__,tsid);
	break;
    case DVBMON_EVENT_UPDATE_TS:
	stream = psi_stream_get(info, tsid, 0);
	if (stream)
	    fprintf(stderr,"%s: update ts 0x%04x \"%s\" @ freq %d\n",
		    __FUNCTION__, tsid, stream->net, stream->frequency);
	break;
    case DVBMON_EVENT_UPDATE_PR:
	pr = psi_program_get(info, tsid, pnr, 0);
	if (pr)
	    fprintf(stderr,"%s: update pr 0x%04x/0x%04x  "
		    "v 0x%04x  a 0x%04x  t 0x%04x  \"%s\", \"%s\"\n",
		    __FUNCTION__, tsid, pnr,
		    pr->v_pid, pr->a_pid, pr->t_pid, pr->net, pr->name);
	break;
    case DVBMON_EVENT_DESTROY:
	fprintf(stderr,"%s: destroy\n",__FUNCTION__);
	break;
    default:
	fprintf(stderr,"%s: unknown event %d\n",__FUNCTION__,event);
	break;
    }
}

void dvbwatch_scanner(struct psi_info *info, int event,
		      int tsid, int pnr, void *data)
{
    struct psi_stream  *stream;
    struct psi_program *pr;
    char section[32];
    
    switch (event) {
    case DVBMON_EVENT_UPDATE_TS:
	stream = psi_stream_get(info, tsid, 0);
	if (!stream)
	    return;
	snprintf(section, sizeof(section), "%d", tsid);
	if (stream->net[0] != '\0')
	    cfg_set_str("dvb-ts", section, "name", stream->net);
	if (stream->frequency)
	    cfg_set_int("dvb-ts", section, "frequency", stream->frequency);
	if (stream->symbol_rate)
	    cfg_set_int("dvb-ts", section, "symbol_rate", stream->symbol_rate);
	if (stream->bandwidth)
	    cfg_set_str("dvb-ts", section, "bandwidth",
			stream->bandwidth);
	if (stream->constellation)
	    cfg_set_str("dvb-ts", section, "modulation",
			stream->constellation);
	if (stream->hierarchy)
	    cfg_set_str("dvb-ts", section, "hierarchy",
			stream->hierarchy);
	if (stream->code_rate_hp)
	    cfg_set_str("dvb-ts", section, "code_rate_high",
			stream->code_rate_hp);
	if (stream->code_rate_lp)
	    cfg_set_str("dvb-ts", section, "code_rate_low",
			stream->code_rate_lp);
	if (stream->guard)
	    cfg_set_str("dvb-ts", section, "guard_interval",
			stream->guard);
	if (stream->transmission)
	    cfg_set_str("dvb-ts", section, "transmission",
			stream->transmission);
	if (stream->polarization)
	    cfg_set_str("dvb-ts", section, "polarization",
			stream->polarization);

	if (INVERSION_AUTO != dvb_inv)
	    cfg_set_int("dvb-ts", section, "inversion", dvb_inv);
	if (dvb_src)
	    cfg_set_str("dvb-ts", section, "source", dvb_src);
	if (dvb_lnb)
	    cfg_set_str("dvb-ts", section, "lnb", dvb_lnb);
	if (dvb_sat)
	    cfg_set_str("dvb-ts", section, "sat", dvb_sat);
	    
	break;
    case DVBMON_EVENT_UPDATE_PR:
	pr = psi_program_get(info, tsid, pnr, 0);
	if (!pr)
	    return;
	snprintf(section, sizeof(section), "%d-%d", tsid, pnr);
	if (pr->type)
	    cfg_set_int("dvb-pr", section, "type", pr->type);
	if (pr->net[0] != '\0')
	    cfg_set_str("dvb-pr", section, "net", pr->net);
	if (pr->name[0] != '\0')
	    cfg_set_str("dvb-pr", section, "name", pr->name);
	if (pr->v_pid)
	    cfg_set_int("dvb-pr", section, "video", pr->v_pid);
	if (pr->a_pid)
	    cfg_set_int("dvb-pr", section, "audio", pr->a_pid);
	if (0 != strlen(pr->audio))
	    cfg_set_str("dvb-pr", section, "audio_details", pr->audio);
	if (pr->t_pid)
	    cfg_set_int("dvb-pr", section, "teletext", pr->t_pid);
	if (pr->ca)
	    cfg_set_int("dvb-pr", section, "ca", pr->ca);
	break;
    case DVBMON_EVENT_DESTROY:
	break;
    }
}

