/*
 * Xvideo extension -- handle video ports
 *
 * (c) 2001 Gerd Knorr <kraxel@bytesex.org>
 */

#include "config.h"

#ifdef HAVE_LIBXV

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>

#include "grab-ng.h"
#include "commands.h"    /* FIXME: global *drv vars */
#include "atoms.h"
#include "xv.h"

extern Display *dpy;

/* ------------------------------------------------------------------------ */

struct ENC_MAP {
    int norm;
    int input;
    int encoding;
};

struct xv_handle {
    /* port */
    int                  adaptor;
    XvPortID             port;
    char                 name[32];
    GC                   gc;
    int                  grabbed;
    XvEncodingInfo       *ei;
    Window               window;
    
    /* attributes */
    int                  nattr;
    struct ng_attribute  *attr;
    Atom xv_encoding;
    Atom xv_freq;
    Atom xv_colorkey;

    /* encoding */
    struct ENC_MAP       *enc_map;
    int                  norm, input, enc;
    int                  encodings;
};

static const struct XVATTR {
    int   id;
    int   type;
    char  *atom;
} xvattr[] = {
    { ATTR_ID_COLOR,    ATTR_TYPE_INTEGER, "XV_COLOR"       },
    { ATTR_ID_COLOR,    ATTR_TYPE_INTEGER, "XV_SATURATION"  },
    { ATTR_ID_HUE,      ATTR_TYPE_INTEGER, "XV_HUE",        },
    { ATTR_ID_BRIGHT,   ATTR_TYPE_INTEGER, "XV_BRIGHTNESS", },
    { ATTR_ID_CONTRAST, ATTR_TYPE_INTEGER, "XV_CONTRAST",   },
    { ATTR_ID_MUTE,     ATTR_TYPE_BOOL,    "XV_MUTE",       },
    { ATTR_ID_VOLUME,   ATTR_TYPE_INTEGER, "XV_VOLUME",     },
    { -1,               -1,                "XV_COLORKEY",   },
    { -1,               -1,                "XV_FREQ",       },
    { -1,               -1,                "XV_ENCODING",   },
    {}
};

static int xv_read_attr(struct ng_attribute *attr)
{
    struct xv_handle *h   = attr->handle;
    const XvAttribute *at = attr->priv;
    Atom atom;
    int value = 0;

    if (NULL != at) {
	atom = XInternAtom(dpy, at->name, False);
	XvGetPortAttribute(dpy, h->port,atom,&value);
	if (debug)
	    fprintf(stderr,"xv: get %s: %d\n",at->name,value);
	
    } else if (attr->id == ATTR_ID_NORM) {
	value = h->norm;
	
    } else if (attr->id == ATTR_ID_INPUT) {
	value = h->input;

    }
    return value;
}

static void xv_write_attr(struct ng_attribute *attr, int value)
{
    struct xv_handle *h   = attr->handle;
    const XvAttribute *at = attr->priv;
    Atom atom;
    int i;

    if (NULL != at) {
	atom = XInternAtom(dpy, at->name, False);
	XvSetPortAttribute(dpy, h->port,atom,value);
	if (debug)
	    fprintf(stderr,"xv: set %s: %d\n",at->name,value);

    } else if (attr->id == ATTR_ID_NORM || attr->id == ATTR_ID_INPUT) {
	if (attr->id == ATTR_ID_NORM)
	    h->norm  = value;
	if (attr->id == ATTR_ID_INPUT)
	    h->input = value;
	for (i = 0; i < h->encodings; i++) {
	    if (h->enc_map[i].norm  == h->norm &&
		h->enc_map[i].input == h->input) {
		h->enc = i;
		XvSetPortAttribute(dpy,h->port,h->xv_encoding,h->enc);
		break;
	    }
	}
    }
    /* needed for proper timing on the
       "mute - wait - switch - wait - unmute" channel switches */
    XSync(dpy,False);
}

static void
xv_add_attr(struct xv_handle *h, int id, int type,
	    int defval, struct STRTAB *choices, XvAttribute *at)
{
    int i;
    
    h->attr = realloc(h->attr,(h->nattr+2) * sizeof(struct ng_attribute));
    memset(h->attr+h->nattr,0,sizeof(struct ng_attribute)*2);
    if (at) {
	h->attr[h->nattr].priv    = at;
	for (i = 0; xvattr[i].atom != NULL; i++)
	    if (0 == strcmp(xvattr[i].atom,at->name))
		break;
	if (-1 == xvattr[i].type)
	    /* ignore this one */
	    return;
	if (NULL != xvattr[i].atom) {
	    h->attr[h->nattr].id      = xvattr[i].id;
	    h->attr[h->nattr].type    = xvattr[i].type;
	    h->attr[h->nattr].priv    = at;
	    if (ATTR_TYPE_INTEGER == h->attr[h->nattr].type) {
		h->attr[h->nattr].min = at->min_value;
		h->attr[h->nattr].max = at->max_value;
	    }
	} else {
	    /* unknown */
	    return;
	}
    }

    if (id)
	h->attr[h->nattr].id      = id;
    if (type)
	h->attr[h->nattr].type    = type;
    if (defval)
	h->attr[h->nattr].defval  = defval;
    if (choices)
	h->attr[h->nattr].choices = choices;
    if (h->attr[h->nattr].id < ATTR_ID_COUNT)
	h->attr[h->nattr].name    = ng_attr_to_desc[h->attr[h->nattr].id];

    h->attr[h->nattr].read    = xv_read_attr;
    h->attr[h->nattr].write   = xv_write_attr;
    h->attr[h->nattr].handle  = h;
    h->nattr++;
}

static unsigned long
xv_getfreq(void *handle)
{
    struct xv_handle *h = handle;
    unsigned int freq;

    XvGetPortAttribute(dpy,h->port,h->xv_freq,&freq);
    return freq;
}

static void
xv_setfreq(void *handle, unsigned long freq)
{
    struct xv_handle *h = handle;

    XvSetPortAttribute(dpy,h->port,h->xv_freq,freq);
    XSync(dpy,False);
}

static int
xv_tuned(void *handle)
{
    /* don't know ... */
    return 0;
}

static int
xv_overlay(void *handle, int enable, int aspect,
	   long window, int dw, int dh)
{
    struct xv_handle *h = handle;
    int sx,sy,dx,dy;
    int sw,sh;
    
    BUG_ON(0 == h->grabbed, "device not opened");
    
    if (enable) {
	h->window = window;
	sx = sy = dx = dy = 0;
	sw = dw;
	sh = dh;
	if (-1 != h->enc) {
	    sw = h->ei[h->enc].width;
	    sh = h->ei[h->enc].height;
	}
	if (NULL == h->gc)
	    h->gc = XCreateGC(dpy, h->window, 0, NULL);
	ng_ratio_fixup(&dw,&dh,&dx,&dy);
	XvPutVideo(dpy,h->port,h->window,h->gc,
		   sx,sy,sw,sh, dx,dy,dw,dh);
	if (debug)
		fprintf(stderr,"xv: video: win=0x%lx, "
			"src=%dx%d+%d+%d dst=%dx%d+%d+%d\n",
			h->window, sw,sh,sx,sy, dw,dh,dx,dy);
    } else {
	XClearArea(dpy,h->window,0,0,0,0,False);
	XvStopVideo(dpy,h->port,h->window);
	h->window = 0;
    }
    return 0;
}

static int
xv_strlist_add(struct STRTAB **tab, char *str)
{
    int i;

    if (NULL == *tab) {
	*tab = malloc(sizeof(struct STRTAB)*2);
	i = 0;
    } else {
	for (i = 0; (*tab)[i].str != NULL; i++)
	    if (0 == strcasecmp((*tab)[i].str,str))
		return (*tab)[i].nr;
	*tab = realloc(*tab,sizeof(struct STRTAB)*(i+2));
    }
    (*tab)[i].nr  = i;
    (*tab)[i].str = strdup(str);
    (*tab)[i+1].nr  = -1;
    (*tab)[i+1].str = NULL;
    return i;
}

static int xv_flags(void *handle)
{
    struct xv_handle *h = handle;
    int ret = 0;

    ret |= CAN_OVERLAY;
    if (h->xv_freq != None)
	ret |= CAN_TUNE;
    return ret;
}

static struct ng_attribute* xv_attrs(void *handle)
{
    struct xv_handle *h  = handle;
    return h->attr;
}

/* ------------------------------------------------------------------------ */

static int xv_open(void *handle)
{
    struct xv_handle *h  = handle;

    BUG_ON(0 != h->grabbed, "device already open");
    if (Success != XvGrabPort(dpy,h->port,CurrentTime)) {
	fprintf(stderr,"xv: can't grab port, busy?\n");
	return -1;
    }
    h->grabbed = 1;
    return 0;
}

static int xv_close(void *handle)
{
    struct xv_handle *h  = handle;

    BUG_ON(0 == h->grabbed, "device not opened");
    if (h->window) {
	XClearArea(dpy,h->window,0,0,0,0,False);
	XvStopVideo(dpy,h->port,h->window);
	h->window = 0;
    }
    XvUngrabPort(dpy,h->port,CurrentTime);
    h->grabbed = 0;
    return 0;
}

static int xv_check(void)
{
    int ver, rel, req, ev, err;

    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	if (debug)
	    fprintf(stderr,"xv: Server has no Xvideo extension support\n");
	return -1;
    }
    return 0;
}

static void xv_mkname(XvAdaptorInfo *ai, char *dest, int len)
{
    snprintf(dest,len,"Xvideo: %s",ai->name);
}

static void*
xv_init(char *device)
{
    struct STRTAB *norms  = NULL;
    struct STRTAB *inputs = NULL;
    XvAttribute      *at;
    XvAdaptorInfo    *ai;
    struct xv_handle *h;
    int adaptors, attributes;
    int port, i;
    char *tmp;

    if (1 != sscanf(device,"port:%d",&port))
	return NULL;
    if (0 != xv_check())
	return NULL;
    h = malloc(sizeof(struct xv_handle));
    memset(h,0,sizeof(struct xv_handle));

    /* search for adaptor */
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	fprintf(stderr,"xv: XvQueryAdaptors failed");
	free(h);
	return NULL;
    }
    for (i = 0; i < adaptors; i++) {
	if ((ai[i].type & XvInputMask) &&
	    (ai[i].type & XvVideoMask) &&
	    port >= ai[i].base_id      &&
	    port <  ai[i].base_id+ai[i].num_ports) {
	    xv_mkname(ai+i, h->name, sizeof(h->name));
	    h->adaptor = i;
	    h->port    = port;
	    break;
	}
    }
    XvFreeAdaptorInfo(ai);
    if (0 == h->port) {
	if (debug)
	    fprintf(stderr,"xv: port %d not present\n",port);
	free(h);
	return NULL;
    }

    /* initialize */
    if (debug)
	fprintf(stderr,"xv: using port video %d\n",port);
    h->xv_encoding = None;
    h->xv_freq     = None;
    h->xv_colorkey = None;
    h->enc         = -1;
    h->norm        = -1;
    h->input       = -1;

    /* query encoding list */
    if (Success != XvQueryEncodings(dpy, port,
				    &h->encodings, &h->ei)) {
	fprintf(stderr,"Oops: XvQueryEncodings failed\n");
	exit(1);
    }
    h->enc_map = malloc(sizeof(struct ENC_MAP)*h->encodings);
    for (i = 0; i < h->encodings; i++) {
	if (NULL != (tmp = strrchr(h->ei[i].name,'-'))) {
	    *(tmp++) = 0;
	    h->enc_map[i].input = xv_strlist_add(&inputs,tmp);
	}
	h->enc_map[i].norm = xv_strlist_add(&norms,h->ei[i].name);
	h->enc_map[i].encoding = h->ei[i].encoding_id;
    }
    
    /* build atoms */
    at = XvQueryPortAttributes(dpy,port,&attributes);
    for (i = 0; i < attributes; i++) {
	if (debug)
	    fprintf(stderr,"  %s%s%s, %i -> %i\n",
		    at[i].name,
		    (at[i].flags & XvGettable) ? " get" : "",
		    (at[i].flags & XvSettable) ? " set" : "",
		    at[i].min_value,at[i].max_value);
	if (0 == strcmp("XV_ENCODING",at[i].name))
	    h->xv_encoding = XV_ENCODING;
	if (0 == strcmp("XV_FREQ",at[i].name))
	    h->xv_freq     = XV_FREQ;
#if 0
	if (0 == strcmp("XV_COLORKEY",at[i].name))
	    h->xv_colorkey = XV_COLORKEY;
#endif
	xv_add_attr(h, 0, 0, 0, NULL, at+i);
    }
    
    if (h->xv_encoding != None) {
	if (norms)
	    xv_add_attr(h, ATTR_ID_NORM, ATTR_TYPE_CHOICE,
			0, norms, NULL);
	if (inputs)
	    xv_add_attr(h, ATTR_ID_INPUT, ATTR_TYPE_CHOICE,
			0, inputs, NULL);
    }
#if 0
    if (xv_colorkey != None) {
	XvGetPortAttribute(dpy,vi_port,xv_colorkey,&xv.colorkey);
	fprintf(stderr,"xv: colorkey: %x\n",xv.colorkey);
    }
#endif
    return h;
}

static int
xv_fini(void *handle)
{
    struct xv_handle *h = handle;

    free(h);
    return 0;
}

static char*
xv_devname(void *handle)
{
    struct xv_handle *h = handle;
    return h->name;
}

static struct ng_devinfo* xv_probe(int verbose)
{
    struct ng_devinfo *info = NULL;
    XvAdaptorInfo     *ai;
    int adaptors;
    int i,n;

    if (0 != xv_check())
	return NULL;
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	fprintf(stderr,"xv: XvQueryAdaptors failed");
	return NULL;
    }

    n = 0;
    for (i = 0; i < adaptors; i++) {
	if (!(ai[i].type & XvInputMask) ||
	    !(ai[i].type & XvVideoMask))
	    continue;

	if (debug)
	    fprintf(stderr,"xv: %s:%s%s%s%s%s, ports %ld-%ld\n",
		    ai[i].name,
		    (ai[i].type & XvInputMask)  ? " input"  : "",
		    (ai[i].type & XvOutputMask) ? " output" : "",
		    (ai[i].type & XvVideoMask)  ? " video"  : "",
		    (ai[i].type & XvStillMask)  ? " still"  : "",
		    (ai[i].type & XvImageMask)  ? " image"  : "",
		    ai[i].base_id,
		    ai[i].base_id+ai[i].num_ports-1);
	
	info = realloc(info,sizeof(*info) * (n+2));
	memset(info+n,0,sizeof(*info)*2);
	snprintf(info[n].device, sizeof(info[n].device),
		 "port:%ld", ai[i].base_id);
	xv_mkname(ai+i, info[n].name, sizeof(info[n].name));
	n++;
    }
    XvFreeAdaptorInfo(ai);
    return info;
}

/* ------------------------------------------------------------------------ */

struct ng_vid_driver xv_driver = {
    .name          = "xv",

    .init          = xv_init,
    .open          = xv_open,
    .close         = xv_close,
    .fini          = xv_fini,
    .devname       = xv_devname,
    .probe         = xv_probe,

    .capabilities  = xv_flags,
    .list_attrs    = xv_attrs,

    .overlay       = xv_overlay,

    .getfreq       = xv_getfreq,
    .setfreq       = xv_setfreq,
    .is_tuned      = xv_tuned,
};

static void __init plugin_init(void)
{
    ng_vid_driver_register(NG_PLUGIN_MAGIC, __FILE__, &xv_driver);
}

#endif /* HAVE_LIBXV */
