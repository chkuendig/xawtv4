/*
 * maintain dvb transponder informations in glib-based apps,
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <glib.h>

#include "grab-ng.h"
#include "dvb-tuning.h"
#include "dvb-monitor.h"

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
    char                adapter[32];
    int                 verbose;
    struct dvb_state    *dvb;
    struct psi_info     *info;

    struct list_head    tables;
    struct list_head    versions;
    struct list_head    callbacks;
};

static void table_open(struct dvbmon *dm, char *name, int pid, int sec);
static void table_close(struct dvbmon *dm, int pid);

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
    if (dvb_demux_get_section(&tab->fd, buf, sizeof(buf), 0) < 0) {
	fprintf(stderr,"dvbmon: read %s oops (fd %d)\n",
		tab->name, tab->fd);
	return TRUE;
    }
    id      = mpeg_getbits(buf,24,16);
    version = mpeg_getbits(buf,42,5);
    current = mpeg_getbits(buf,47,1);
    if (!current)
	return TRUE;
    if (table_data_seen(dm, tab->name, id, version))
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
		table_close(dm, pr->p_pid);
	}
	list_for_each(item,&dm->info->programs) {
	    pr = list_entry(item, struct psi_program, next);
	    pr->seen = 0;
	    if (PSI_NEW == pr->version)
		table_open(dm, "pmt", pr->p_pid, 2);
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

static void table_open(struct dvbmon *dm, char *name, int pid, int sec)
{
    struct table *tab; 

    tab = malloc(sizeof(*tab));
    memset(tab,0,sizeof(*tab));
    tab->name = name;
    tab->pid  = pid;
    tab->sec  = sec;
    tab->fd   = dvb_demux_req_section(dm->dvb, pid, sec, 0);
    if (-1 == tab->fd) {
	free(tab);
	return;
    }
    tab->ch  = g_io_channel_unix_new(tab->fd);
    tab->id  = g_io_add_watch(tab->ch, G_IO_IN, table_data, dm);
    list_add_tail(&tab->next,&dm->tables);
    if (dm->verbose)
	fprintf(stderr,"dvbmon: new:   %s  fd=%2d  sec=0x%02x  pid=%d\n",
		tab->name, tab->fd, tab->sec, tab->pid);
}

static void table_close(struct dvbmon *dm, int pid)
{
    struct table      *tab;
    struct list_head  *item;

    list_for_each(item,&dm->tables) {
	tab = list_entry(item, struct table, next);
	if (tab->pid == pid)
	    break;
	tab = NULL;
    }
    if (NULL == tab) {
	fprintf(stderr,"dvbmon: oops: closing unknown pid %d\n",pid);
	return;
    }
    if (dm->verbose)
	fprintf(stderr,"dvbmon: done:  %s  fd=%2d   sec=0x%02x  pid=%d\n",
		tab->name, tab->fd, tab->sec, tab->pid);
    g_source_remove(tab->id);
    g_io_channel_unref(tab->ch);
    close(tab->fd);
    list_del(&tab->next);
    free(tab);
}

/* ----------------------------------------------------------------------------- */
/* external interface                                                            */

struct dvbmon*
dvbmon_init(char *adapter, int verbose)
{
    struct dvbmon *dm;

    dm = malloc(sizeof(*dm));
    memset(dm,0,sizeof(*dm));
    INIT_LIST_HEAD(&dm->tables);
    INIT_LIST_HEAD(&dm->versions);
    INIT_LIST_HEAD(&dm->callbacks);

    strncpy(dm->adapter, adapter, sizeof(dm->adapter));
    dm->verbose = verbose;
    dm->dvb  = dvb_init(dm->adapter);
    dm->info = psi_info_alloc();
    if (dm->dvb) {
	if (dm->verbose)
	    fprintf(stderr,"dvbmon: hwinit ok\n");
	table_open(dm, "pat",   0x00, 0x00);
	table_open(dm, "nit",   0x10, 0x40);
	table_open(dm, "nit",   0x10, 0x41);
	table_open(dm, "sdt",   0x11, 0x42);
	table_open(dm, "sdt",   0x11, 0x46);
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

    list_for_each_safe(item,safe,&dm->tables) {
	tab = list_entry(item, struct table, next);
	table_close(dm, tab->pid);
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
    dvb_fini(dm->dvb);
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
    struct callback   *cb;
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
