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
#include "dvb.h"
#include "xt-dvb.h"

/* ----------------------------------------------------------------------------- */
/* structs & prototypes                                                          */

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
    int                 adapter;

    /* private stuff */
    struct dvb_state    *dvb;
    struct psi_info     info;
    XtInputId           ids[PSI_PROGS];
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
	XtCValue, XtRInt, sizeof(int),
	XtOffset(DvbWidget,dvb.adapter),
	XtRString, "0"
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

static void dvb_data(XtPointer data, int *fd, XtInputId *iproc)
{
    DvbWidget dw = data;
    struct psi_info *info = &dw->dvb.info;
    struct psi_program *pr;
    char buf[4096];
    int handled = 0;
    int i;

    /* get data */
    if (*fd == info->pat_fd) {
	if (dvb_demux_get_section(&info->pat_fd, buf, sizeof(buf), 0) < 0) {
	    fprintf(stderr,"dvbmon: read pat oops (fd %d)\n",*fd);
	} else {
	    mpeg_parse_psi_pat(info, buf, dw->dvb.verbose);
	}
	handled = 1;
    }

    if (*fd == info->sdt_fd) {
	if (dvb_demux_get_section(&info->sdt_fd, buf, sizeof(buf), 0) < 0) {
	    fprintf(stderr,"dvbmon: read sdt oops (fd %d)\n",*fd);
	} else {
	    mpeg_parse_psi_sdt(info, buf, dw->dvb.verbose);
	}
	handled = 1;
    }

    for (i = 0; i < PSI_PROGS; i++) {
	pr = info->progs+i;
	if (0 == pr->p_pid)
	    continue;
	if (*fd == pr->pmt_fd) {
	    if (dvb_demux_get_section(&pr->pmt_fd, buf, sizeof(buf), 0) < 0) {
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
    if (info->modified) {
	info->modified = 0;
	if (dw->dvb.verbose)
	    fprintf(stderr,"dvbmon: updated: pat\n");
	dw->dvb.info.sdt_version = PSI_NEW;
	for (i = 0; i < PSI_PROGS; i++) {
	    pr = info->progs+i;
	    if (0 == pr->p_pid)
		continue;
	    if (!pr->seen) {
		if (dw->dvb.verbose)
		    fprintf(stderr,"dvbmon: gone: pid %d fd %d\n",
			    pr->p_pid, pr->pmt_fd);
		XtCallCallbacks((Widget)dw,"delete_channel",pr);
		close(pr->pmt_fd);
		XtRemoveInput(dw->dvb.ids[i]);
		memset(pr,0,sizeof(*pr));
	    }
	}
	for (i = 0; i < PSI_PROGS; i++) {
	    pr = info->progs+i;
	    if (0 == pr->p_pid)
		continue;
	    pr->seen = 0;
	    if (42 == pr->version) {
		pr->pmt_fd = dvb_demux_req_section(dw->dvb.dvb, pr->p_pid,
						   2, 0);
		dw->dvb.ids[i] = XtAppAddInput
		    (XtWidgetToApplicationContext((Widget)dw), pr->pmt_fd,
		     (XtPointer) XtInputReadMask, dvb_data, dw);
		if (dw->dvb.verbose)
		    fprintf(stderr,"dvbmon: new: pid %d fd %d\n",
			    pr->p_pid, pr->pmt_fd);
	    }
	}
    }
    for (i = 0; i < PSI_PROGS; i++) {
	pr = info->progs+i;
	if (0 == pr->p_pid)
	    continue;
	if (!pr->modified)
	    continue;
	pr->modified = 0;
	XtCallCallbacks((Widget)dw,"update_channel",pr);
    }
    return;
    
 oops:
    XtRemoveInput(*iproc);
    close(*fd);
    return;
}

/* ----------------------------------------------------------------------------- */
/* widget class calls                                                            */

static void
dvbInitialize(Widget request, Widget w, ArgList args, Cardinal *num_args)
{
    DvbWidget dw = (DvbWidget)w;
    struct psi_info *info = &dw->dvb.info;

    if (dw->dvb.verbose)
	fprintf(stderr,"dvbmon: init\n");
    dw->core.width  = 1;
    dw->core.height = 1;

    //dw->dvb.verbose = 1;
    dw->dvb.dvb = dvb_init(dw->dvb.adapter,0,0);
    if (dw->dvb.dvb) {
	if (dw->dvb.verbose)
	    fprintf(stderr,"dvbmon: hwinit ok\n");
	info->sdt_fd = dvb_demux_req_section(dw->dvb.dvb, 0x11, 0x42, 0);
	info->pat_fd = dvb_demux_req_section(dw->dvb.dvb, 0x00, 0x00, 0);
	XtAppAddInput(XtWidgetToApplicationContext((Widget)dw), info->sdt_fd,
		      (XtPointer) XtInputReadMask, dvb_data, dw);
	XtAppAddInput(XtWidgetToApplicationContext((Widget)dw), info->pat_fd,
		      (XtPointer) XtInputReadMask, dvb_data, dw);
    } else {
	fprintf(stderr,"dvbmon: hwinit FAILED\n");
    }
}

void dvb_refresh(Widget w)
{
    DvbWidget dw = (DvbWidget)w;

    // force re-read
    dw->dvb.info.pat_version = PSI_NEW;
    dw->dvb.info.sdt_version = PSI_NEW;
}
