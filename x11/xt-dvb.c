/*
 * maintain dvb transponder informations in Xt-based x11 apps,
 * using a pseudo widget.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

#include <X11/StringDefs.h>
#include <X11/IntrinsicP.h>
#include <X11/CoreP.h>

#include "grab-ng.h"
#include "dvb-tuning.h"
#include "xt-dvb.h"

/* ----------------------------------------------------------------------------- */
/* structs & prototypes                                                          */

struct table {
    struct list_head    next;
    int                 pid;
    int                 fd;
    XtInputId           id;
};

typedef struct _dvbClassPart {
    /* nothing */
} DvbClassPart;

typedef struct _dvbClassRec {
    CoreClassPart       core_class;
    DvbClassPart        dvb_class;
} DvbClassRec;

typedef struct _dvbPart
{
    /* callbacks */
    XtCallbackList      update;
    XtCallbackList      delete;
    int                 verbose;
    char                *adapter;

    /* private stuff */
    struct dvb_state    *dvb;
    struct psi_info     *info;
    struct list_head    tables;
    int                 sdt_fd;
    int                 pat_fd;
} DvbPart;

typedef struct _dvbRec
{
    CorePart            core;
    DvbPart             dvb;
} DvbRec;

static void dvbInitialize(Widget, Widget, ArgList, Cardinal*);

/* ----------------------------------------------------------------------------- */
/* static data                                                                   */

static XtResource resources[] = {
    {
	"update_channel",
	XtCCallback, XtRCallback, sizeof(XtCallbackList),
	XtOffset(DvbWidget,dvb.update),
	XtRCallback, NULL
    },{
	"delete_channel",
	XtCCallback, XtRCallback, sizeof(XtCallbackList),
	XtOffset(DvbWidget,dvb.delete),
	XtRCallback, NULL
    },{
	"verbose",
	XtCValue, XtRInt, sizeof(int),
	XtOffset(DvbWidget,dvb.verbose),
	XtRString, "0"
    },{
	"adapter",
	XtCString, XtRString, sizeof(char*),
	XtOffset(DvbWidget,dvb.adapter),
	XtRString, NULL
    },
};

#define Superclass (&widgetClassRec)
static DvbClassRec dvbClassRec = {
    .core_class = {
	.superclass          = (WidgetClass)Superclass,
	.class_name          = "dvb",
	.widget_size         = sizeof(DvbRec),
	.resources           = resources,
	.num_resources       = XtNumber(resources),
	.xrm_class           = NULLQUARK,
	.compress_motion     = True,
	.compress_exposure   = True,
	.compress_enterleave = True,
	.visible_interest    = False,
	.version             = XtVersion,

	.initialize          = dvbInitialize,
	.destroy             = NULL,
	.realize             = XtInheritRealize,
	.set_values_almost   = XtInheritSetValuesAlmost,
	.query_geometry      = XtInheritQueryGeometry,
	.display_accelerator = XtInheritDisplayAccelerator,
    },
};
WidgetClass dvbWidgetClass = (WidgetClass)&dvbClassRec;

/* ----------------------------------------------------------------------------- */
/* internal functions                                                            */

static int  table_open(DvbWidget dw, int pid);
static void table_close(DvbWidget dw, int pid);

static void dvb_data(XtPointer data, int *fd, XtInputId *iproc)
{
    DvbWidget dw = data;
    struct psi_info *info = dw->dvb.info;
    struct psi_program *pr;
    struct list_head *item,*safe;
    char buf[4096];
    int handled = 0;
    //int i;

    /* get data */
    if (*fd == dw->dvb.pat_fd) {
	if (dvb_demux_get_section(&dw->dvb.pat_fd, buf, sizeof(buf), 0) < 0) {
	    fprintf(stderr,"dvbmon: read pat oops (fd %d)\n",*fd);
	} else {
	    mpeg_parse_psi_pat(info, buf, dw->dvb.verbose);
	}
	handled = 1;
    }

    if (*fd == dw->dvb.sdt_fd) {
	if (dvb_demux_get_section(&dw->dvb.sdt_fd, buf, sizeof(buf), 0) < 0) {
	    fprintf(stderr,"dvbmon: read sdt oops (fd %d)\n",*fd);
	} else {
	    mpeg_parse_psi_sdt(info, buf, dw->dvb.verbose);
	}
	handled = 1;
    }

    list_for_each(item,&info->programs) {
        pr = list_entry(item, struct psi_program, next);
	if (*fd == pr->fd) {
	    if (dvb_demux_get_section(&pr->fd, buf, sizeof(buf), 0) < 0) {
		fprintf(stderr,"dvbmon: read pmd oops (fd %d)\n",*fd);
	    } else {
		mpeg_parse_psi_pmt(pr, buf, dw->dvb.verbose);
	    }
	    handled = 1;
	}
    }

    if (!handled) {
	fprintf(stderr,"dvbmon: unknown fd %d\n",*fd);
	goto oops;
    }

    /* check for changes */
    if (info->pat_updated) {
	info->pat_updated = 0;
	if (dw->dvb.verbose)
	    fprintf(stderr,"dvbmon: updated: pat\n");
	list_for_each_safe(item,safe,&info->programs) {
	    pr = list_entry(item, struct psi_program, next);
	    if (!pr->seen) {
		XtCallCallbacks((Widget)dw,"delete_channel",pr);
		table_close(dw, pr->p_pid);
		list_del(&pr->next);
		free(pr);
	    }
	}
	list_for_each(item,&info->programs) {
	    pr = list_entry(item, struct psi_program, next);
	    pr->seen = 0;
	    if (PSI_NEW == pr->version)
		pr->fd = table_open(dw, pr->p_pid);
	}
    }
    list_for_each(item,&info->programs) {
	pr = list_entry(item, struct psi_program, next);
	if (!pr->updated)
	    continue;
	pr->updated = 0;
	XtCallCallbacks((Widget)dw,"update_channel",pr);
    }
    return;
    
 oops:
    XtRemoveInput(*iproc);
    close(*fd);
    return;
}

static int table_open(DvbWidget dw, int pid)
{
    struct table *tab; 

    tab = malloc(sizeof(*tab));
    memset(tab,0,sizeof(*tab));
    tab->pid  = pid;
    tab->fd   = dvb_demux_req_section(dw->dvb.dvb, pid, 0x02, 0);
    if (-1 == tab->fd) {
	free(tab);
	return -1;
    }
    tab->id = XtAppAddInput(XtWidgetToApplicationContext((Widget)dw),
			    tab->fd, (XtPointer) XtInputReadMask, dvb_data, dw);
    list_add_tail(&tab->next,&dw->dvb.tables);
    if (dw->dvb.verbose)
	fprintf(stderr,"dvbmon: new:  fd=%d pid=%d\n",
		tab->fd,  tab->pid);
    return tab->fd;
}

static void table_close(DvbWidget dw, int pid)
{
    struct table      *tab = NULL;
    struct list_head  *item;

    list_for_each(item,&dw->dvb.tables) {
	tab = list_entry(item, struct table, next);
	if (tab->pid == pid)
	    break;
	tab = NULL;
    }
    if (NULL == tab) {
	fprintf(stderr,"dvbmon: oops: closing unknown pid %d\n",pid);
	return;
    }
    if (dw->dvb.verbose)
	fprintf(stderr,"dvbmon: gone: fd=%d pid=%d\n",
		tab->fd, tab->pid);
    XtRemoveInput(tab->id);
    close(tab->fd);
    list_del(&tab->next);
    free(tab);
}

/* ----------------------------------------------------------------------------- */
/* widget class calls                                                            */

static void
dvbInitialize(Widget request, Widget w, ArgList args, Cardinal *num_args)
{
    DvbWidget dw = (DvbWidget)w;

    if (dw->dvb.verbose)
	fprintf(stderr,"dvbmon: init\n");
    dw->core.width  = 1;
    dw->core.height = 1;

    INIT_LIST_HEAD(&dw->dvb.tables);
    dw->dvb.dvb  = dvb_init(dw->dvb.adapter);
    dw->dvb.info = psi_info_alloc();
    if (dw->dvb.dvb) {
	if (dw->dvb.verbose)
	    fprintf(stderr,"dvbmon: hwinit ok\n");
	dw->dvb.sdt_fd = dvb_demux_req_section(dw->dvb.dvb, 0x11, 0x42, 0);
	dw->dvb.pat_fd = dvb_demux_req_section(dw->dvb.dvb, 0x00, 0x00, 0);
	XtAppAddInput(XtWidgetToApplicationContext((Widget)dw), dw->dvb.sdt_fd,
		      (XtPointer) XtInputReadMask, dvb_data, dw);
	XtAppAddInput(XtWidgetToApplicationContext((Widget)dw), dw->dvb.pat_fd,
		      (XtPointer) XtInputReadMask, dvb_data, dw);
    } else {
	fprintf(stderr,"dvbmon: hwinit FAILED\n");
    }
}

void dvb_refresh(Widget w)
{
    DvbWidget dw = (DvbWidget)w;

    // force re-read
    dw->dvb.info->pat_version = PSI_NEW;
    dw->dvb.info->sdt_version = PSI_NEW;
}
