/*
 * x11 helper functions -- blit frames to the screen
 *
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#ifdef HAVE_LIBXV
# include <X11/extensions/Xv.h>
# include <X11/extensions/Xvlib.h>
#endif

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#if HAVE_GL
# include <GL/gl.h>
# include <GL/glx.h>
#endif

#include "grab-ng.h"
#include "blit.h"

/* ------------------------------------------------------------------------ */

extern int             debug;

unsigned int           x11_dpy_fmtid;

static int             display_bits = 0;
static unsigned int    display_bytes = 0;
static unsigned int    pixmap_bytes = 0;
static bool            x11_byteswap = 0;
static int             no_mitshm = 0;
static int             gl_error = 0;

#if HAVE_LIBXV
static int             ver, rel, req, ev, err;
static int             formats;
static int             adaptors;
static XvImageFormatValues  *fo;
static XvAdaptorInfo        *ai;

static unsigned int    im_adaptor,im_port = UNSET;
static unsigned int    im_formats[VIDEO_FMT_COUNT];
#endif


static struct SEARCHFORMAT {
    unsigned int   depth;
    int            order;
    unsigned long  red;
    unsigned long  green;
    unsigned long  blue;
    unsigned int   format;
} fmt[] = {
    { 2, MSBFirst, 0x7c00,     0x03e0,     0x001f,     VIDEO_RGB15_BE },
    { 2, MSBFirst, 0xf800,     0x07e0,     0x001f,     VIDEO_RGB16_BE },
    { 2, LSBFirst, 0x7c00,     0x03e0,     0x001f,     VIDEO_RGB15_LE },
    { 2, LSBFirst, 0xf800,     0x07e0,     0x001f,     VIDEO_RGB16_LE },

    { 3, LSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_BGR24    },
    { 3, LSBFirst, 0x000000ff, 0x0000ff00, 0x00ff0000, VIDEO_RGB24    },
    { 3, MSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_RGB24    },
    { 3, MSBFirst, 0x000000ff, 0x0000ff00, 0x00ff0000, VIDEO_BGR24    },

    { 4, LSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_BGR32    },
    { 4, LSBFirst, 0x0000ff00, 0x00ff0000, 0xff000000, VIDEO_RGB32    },
    { 4, MSBFirst, 0x00ff0000, 0x0000ff00, 0x000000ff, VIDEO_RGB32    },
    { 4, MSBFirst, 0x0000ff00, 0x00ff0000, 0xff000000, VIDEO_BGR32    },

    { 2, -1,       0,          0,          0,          VIDEO_LUT2     },
    { 4, -1,       0,          0,          0,          VIDEO_LUT4     },
    { 0 /* END OF LIST */ },
};

static int
catch_no_mitshm(Display * dpy, XErrorEvent * event)
{
    fprintf(stderr,"WARNING: MIT shared memory extention not available\n");
    no_mitshm++;
    return 0;
}

static int
catch_gl_error(Display * dpy, XErrorEvent * event)
{
    fprintf(stderr,"WARNING: Your OpenGL setup is broken.\n");
    gl_error++;
    return 0;
}

/* ------------------------------------------------------------------------ */

enum blit_video_mode {
    MODE_UNSET  = 0,
#ifdef HAVE_LIBXV
    MODE_XVIDEO = 1,
#endif
    MODE_OPENGL = 2,
    MODE_X11    = 3,
};

struct blit_handle {
    GtkWidget                 *widget;
    Display                   *dpy;
    Screen                    *scr;
    Window                    win;
    GC                        gc;
    XVisualInfo               *vinfo;
    int                       width, height;
    int                       buffers;
    enum blit_video_mode      mode;

    struct list_head          freelist;
};

struct blit_video_buf_priv {
    enum blit_video_mode   mode;
    struct ng_video_buf    *buf;
    struct blit_handle     *blit;
    int                    seq;
    struct list_head       list;

    XShmSegmentInfo        *shm;
    XImage                 *ximage;
#ifdef HAVE_LIBXV
    XvImage                *xvimage;
#endif
#if HAVE_GL
    GLint                  tex;
    int                    tw,th;
#endif
};

/* ------------------------------------------------------------------------ */

static void blit_ratio(struct ng_video_buf *buf,
		       int *wx, int *wy, int *ww, int *wh)
{
    struct blit_video_buf_priv *p = buf->priv;
    struct blit_handle *h = p->blit;
    *wx = 0;
    *wy = 0;
    *ww = h->width;
    *wh = h->height;

    switch (buf->info.ratio) {
    case NG_RATIO_UNSPEC:
	ng_ratio_fixup(ww, wh, wx, wy);
	break;
    case NG_RATIO_SQUARE:
	ng_ratio_fixup2(ww, wh, wx, wy, buf->fmt.width, buf->fmt.height, 0);
	break;
    case NG_RATIO_3_4:
	ng_ratio_fixup2(ww, wh, wx, wy, 4, 3, 0);
	break;
    case NG_RATIO_9_16:
	ng_ratio_fixup2(ww, wh, wx, wy, 16, 9, 0);
	break;
    case NG_RATIO_2dot21:
	ng_ratio_fixup2(ww, wh, wx, wy, 221, 100, 0);
	break;
    }
}

/* ------------------------------------------------------------------------ */
/* plain X11 stuff                                                          */

Visual*
x11_find_visual(Display *dpy)
{
    XVisualInfo  *info, template;
    Visual*      vi = CopyFromParent;
    int          found,i;
    char         *class;

    template.screen = XDefaultScreen(dpy);
    info = XGetVisualInfo(dpy, VisualScreenMask,&template,&found);
    for (i = 0; i < found; i++) {
	switch (info[i].class) {
	case StaticGray:   class = "StaticGray";  break;
	case GrayScale:    class = "GrayScale";   break;
	case StaticColor:  class = "StaticColor"; break;
	case PseudoColor:  class = "PseudoColor"; break;
	case TrueColor:    class = "TrueColor";   break;
	case DirectColor:  class = "DirectColor"; break;
	default:           class = "UNKNOWN";     break;
	}
	if (debug)
	    fprintf(stderr,"visual: id=0x%lx class=%d (%s), depth=%d\n",
		    info[i].visualid,info[i].class,class,info[i].depth);
    }
    for (i = 0; vi == CopyFromParent && i < found; i++)
	if (info[i].class == TrueColor && info[i].depth >= 15)
	    vi = info[i].visual;
    for (i = 0; vi == CopyFromParent && i < found; i++)
	if (info[i].class == StaticGray && info[i].depth == 8)
	    vi = info[i].visual;
    return vi;
}

void
x11_init_visual(Display *dpy, XVisualInfo *vinfo)
{
    XPixmapFormatValues *pf;
    int                  i,n;
    int                  format = 0;

    if (!XShmQueryExtension(dpy)) {
	fprintf(stderr,"WARNING: MIT shared memory extention not available\n");
	no_mitshm = 1;
    }

    display_bits = vinfo->depth;
    display_bytes = (display_bits+7)/8;

    pf = XListPixmapFormats(dpy,&n);
    for (i = 0; i < n; i++)
	if (pf[i].depth == display_bits)
	    pixmap_bytes = pf[i].bits_per_pixel/8;

    if (debug) {
	fprintf(stderr,"x11: color depth: "
		"%d bits, %d bytes - pixmap: %d bytes\n",
		display_bits,display_bytes,pixmap_bytes);
	if (vinfo->class == TrueColor || vinfo->class == DirectColor)
	    fprintf(stderr, "x11: color masks: "
		    "red=0x%08lx green=0x%08lx blue=0x%08lx\n",
		    vinfo->red_mask, vinfo->green_mask, vinfo->blue_mask);
	fprintf(stderr,"x11: server byte order: %s\n",
		ImageByteOrder(dpy)==LSBFirst ? "little endian":"big endian");
	fprintf(stderr,"x11: client byte order: %s\n",
		BYTE_ORDER==LITTLE_ENDIAN ? "little endian":"big endian");
    }
    if (ImageByteOrder(dpy)==LSBFirst && BYTE_ORDER!=LITTLE_ENDIAN)
	x11_byteswap=1;
    if (ImageByteOrder(dpy)==MSBFirst && BYTE_ORDER!=BIG_ENDIAN)
	x11_byteswap=1;
    if (vinfo->class == TrueColor /* || vinfo->class == DirectColor */) {
	/* pixmap format */
	for (i = 0; fmt[i].depth > 0; i++) {
	    if (fmt[i].depth  == pixmap_bytes                               &&
		(fmt[i].order == ImageByteOrder(dpy) || fmt[i].order == -1) &&
		(fmt[i].red   == vinfo->red_mask     || fmt[i].red   == 0)  &&
		(fmt[i].green == vinfo->green_mask   || fmt[i].green == 0)  &&
		(fmt[i].blue  == vinfo->blue_mask    || fmt[i].blue  == 0)) {
		x11_dpy_fmtid = fmt[i].format;
		break;
	    }
	}
	if (fmt[i].depth == 0) {
	    fprintf(stderr, "Huh?\n");
	    exit(1);
	}
	ng_lut_init(vinfo->red_mask, vinfo->green_mask, vinfo->blue_mask,
		    x11_dpy_fmtid,x11_byteswap);
	/* guess physical screen format */
	if (ImageByteOrder(dpy) == MSBFirst) {
	    switch (pixmap_bytes) {
	    case 2: format = (display_bits==15) ?
			VIDEO_RGB15_BE : VIDEO_RGB16_BE; break;
	    case 3: format = VIDEO_RGB24; break;
	    case 4: format = VIDEO_RGB32; break;
	    }
	} else {
	    switch (pixmap_bytes) {
	    case 2: format = (display_bits==15) ?
			VIDEO_RGB15_LE : VIDEO_RGB16_LE; break;
	    case 3: format = VIDEO_BGR24; break;
	    case 4: format = VIDEO_BGR32; break;
	    }
	}
    }
    if (vinfo->class == StaticGray && vinfo->depth == 8) {
	format = VIDEO_GRAY;
    }
    if (0 == format) {
	if (vinfo->class == PseudoColor && vinfo->depth == 8) {
	    fprintf(stderr,
		    "\n"
		    "8-bit Pseudocolor Visual (256 colors) is *not* supported.\n"
		    "You can startup X11 either with 15 bpp (or more)...\n"
		    "	xinit -- -bpp 16\n"
		    "... or with StaticGray visual:\n"
		    "	xinit -- -cc StaticGray\n"
	    );
	} else {
	    fprintf(stderr, "Sorry, I can't handle your strange display\n");
	}
	exit(1);
    }
    x11_dpy_fmtid = format;
}

XImage*
x11_create_ximage(Display *dpy, XVisualInfo *vinfo,
		  int width, int height, XShmSegmentInfo **shm)
{
    XImage          *ximage = NULL;
    unsigned char   *ximage_data;
    XShmSegmentInfo *shminfo = NULL;
    void            *old_handler;
    
    if (no_mitshm)
	goto no_mitshm;

    assert(width > 0 && height > 0);
    
    old_handler = XSetErrorHandler(catch_no_mitshm);
    shminfo = malloc(sizeof(XShmSegmentInfo));
    memset(shminfo, 0, sizeof(XShmSegmentInfo));
    ximage = XShmCreateImage(dpy,vinfo->visual,vinfo->depth,
			     ZPixmap, NULL,
			     shminfo, width, height);
    if (NULL == ximage)
	goto shm_error;
    shminfo->shmid = shmget(IPC_PRIVATE,
			    ximage->bytes_per_line * ximage->height,
			    IPC_CREAT | 0777);
    if (-1 == shminfo->shmid) {
	perror("shmget [x11]");
	goto shm_error;
    }
    shminfo->shmaddr = (char *) shmat(shminfo->shmid, 0, 0);
    if ((void *)-1 == shminfo->shmaddr) {
	perror("shmat");
	goto shm_error;
    }
    ximage->data = shminfo->shmaddr;
    shminfo->readOnly = False;
    
    XShmAttach(dpy, shminfo);
    XSync(dpy, False);
    if (no_mitshm)
	goto shm_error;
    shmctl(shminfo->shmid, IPC_RMID, 0);
    XSetErrorHandler(old_handler);
    *shm = shminfo;
    return ximage;

 shm_error:
    if (ximage) {
	XDestroyImage(ximage);
	ximage = NULL;
    }
    if ((void *)-1 != shminfo->shmaddr  &&  NULL != shminfo->shmaddr)
	shmdt(shminfo->shmaddr);
    free(shminfo);
    XSetErrorHandler(old_handler);
    fprintf(stderr,"WARNING: MIT shared memory extention not available\n");
    no_mitshm = 1;

 no_mitshm:
    *shm = NULL;
    if (NULL == (ximage_data = malloc(width * height * pixmap_bytes))) {
	fprintf(stderr,"out of memory\n");
	exit(1);
    }
    ximage = XCreateImage(dpy, vinfo->visual, vinfo->depth,
			  ZPixmap, 0, ximage_data,
			  width, height,
			  8, 0);
    memset(ximage->data, 0, ximage->bytes_per_line * ximage->height);
    return ximage;
}

void
x11_destroy_ximage(Display *dpy, XImage *ximage, XShmSegmentInfo *shm)
{
    if (shm && !no_mitshm) {
	XShmDetach(dpy, shm);
	XDestroyImage(ximage);
	shmdt(shm->shmaddr);
	free(shm);
    } else
	XDestroyImage(ximage);
}

void x11_draw_ximage(Display *dpy, Drawable dr, GC gc, XImage *xi,
		    int a, int b, int c, int d, int w, int h)
{
    if (no_mitshm)
	XPutImage(dpy,dr,gc,xi,a,b,c,d,w,h);
    else
	XShmPutImage(dpy,dr,gc,xi,a,b,c,d,w,h,True);
}

Pixmap
x11_create_pixmap(Display *dpy, XVisualInfo *vinfo, struct ng_video_buf *buf)
{
    Pixmap          pixmap;
    XImage          *ximage;
    GC              gc;
    XShmSegmentInfo *shm;
    Screen          *scr = DefaultScreenOfDisplay(dpy);

    pixmap = XCreatePixmap(dpy,RootWindowOfScreen(scr),
                           buf->fmt.width, buf->fmt.height, vinfo->depth);

    gc = XCreateGC(dpy, pixmap, 0, NULL);

    if (NULL == (ximage = x11_create_ximage(dpy, vinfo, buf->fmt.width,
					    buf->fmt.height, &shm))) {
	XFreePixmap(dpy, pixmap);
        XFreeGC(dpy, gc);
        return 0;
    }
    memcpy(ximage->data,buf->data,buf->size);
    x11_draw_ximage(dpy, pixmap, gc, ximage, 0, 0, 0, 0,
		    buf->fmt.width, buf->fmt.height);
    x11_destroy_ximage(dpy, ximage, shm);
    XFreeGC(dpy, gc);
    return pixmap;
}

static void x11_blit(struct ng_video_buf *buf)
{
    struct blit_video_buf_priv *p = buf->priv;
    struct blit_handle *h = p->blit;
    int x = (h->width  - buf->fmt.width)  / 2;
    int y = (h->height - buf->fmt.height) / 2;

    if (p->shm)
	XShmPutImage(h->dpy, h->win, h->gc, p->ximage,
		     0,0,x,y,buf->fmt.width,buf->fmt.height,
		     True);
    else
	XPutImage(h->dpy, h->win,
		  h->gc, p->ximage,
		  0,0,x,y,buf->fmt.width,buf->fmt.height);
}

/* ------------------------------------------------------------------------ */
/* XVideo extension code                                                    */

#ifdef HAVE_LIBXV
static void xv_image_init(Display *dpy)
{
    int i;

    if (Success != XvQueryExtension(dpy,&ver,&rel,&req,&ev,&err)) {
	if (debug)
	    fprintf(stderr,"Xvideo: Server has no Xvideo extension support\n");
	return;
    }
    if (Success != XvQueryAdaptors(dpy,DefaultRootWindow(dpy),&adaptors,&ai)) {
	fprintf(stderr,"Xvideo: XvQueryAdaptors failed");
	return;
    }
    for (i = 0; i < adaptors; i++) {
	if (!(ai[i].type & XvInputMask))
	    continue;
	if (!(ai[i].type & XvImageMask))
	    continue;
	if (Success != XvGrabPort(dpy,ai[i].base_id,CurrentTime)) {
	    if (debug)
		fprintf(stderr, "blit: xv: can't grab port %ld\n",
			ai[i].base_id);
	    continue;
	}
	if (debug)
	    fprintf(stderr, "blit: xv: grabbed port %ld\n", ai[i].base_id);
	im_port = ai[i].base_id;
	im_adaptor = i;
	break;
    }
    if (UNSET == im_port)
	return;

    fo = XvListImageFormats(dpy, im_port, &formats);
    for(i = 0; i < formats; i++) {
	if (debug)
	    fprintf(stderr, "blit: xv: 0x%x (%c%c%c%c) %s",
		    fo[i].id,
		    (fo[i].id)       & 0xff,
		    (fo[i].id >>  8) & 0xff,
		    (fo[i].id >> 16) & 0xff,
		    (fo[i].id >> 24) & 0xff,
		    (fo[i].format == XvPacked) ? "packed" : "planar");
	if (0x32595559 == fo[i].id) {
	    im_formats[VIDEO_YUYV] = fo[i].id;
	    if (debug)
		fprintf(stderr," [ok: %s]",ng_vfmt_to_desc[VIDEO_YUYV]);
	}
	if (0x59565955 == fo[i].id) {
	    im_formats[VIDEO_UYVY] = fo[i].id;
	    if (debug)
		fprintf(stderr," [ok: %s]",ng_vfmt_to_desc[VIDEO_UYVY]);
	}
	if (0x30323449 == fo[i].id) {
	    im_formats[VIDEO_YUV420P] = fo[i].id;
	    if (debug)
		fprintf(stderr," [ok: %s]",ng_vfmt_to_desc[VIDEO_YUV420P]);
	}
	if (debug)
	    fprintf(stderr,"\n");
    }
}

static XvImage*
xv_create_ximage(Display *dpy, int width, int height, int format,
		 XShmSegmentInfo **shm)
{
    XvImage         *xvimage = NULL;
    unsigned char   *ximage_data;
    XShmSegmentInfo *shminfo = NULL;
    void            *old_handler;
    
    if (no_mitshm)
	goto no_mitshm;
    
    old_handler = XSetErrorHandler(catch_no_mitshm);
    shminfo = malloc(sizeof(XShmSegmentInfo));
    memset(shminfo, 0, sizeof(XShmSegmentInfo));
    xvimage = XvShmCreateImage(dpy, im_port, format, 0,
			       width, height, shminfo);
    if (NULL == xvimage)
	goto shm_error;
    shminfo->shmid = shmget(IPC_PRIVATE, xvimage->data_size,
			    IPC_CREAT | 0777);
    if (-1 == shminfo->shmid) {
	perror("shmget [xv]");
	goto shm_error;
    }
    shminfo->shmaddr = (char *) shmat(shminfo->shmid, 0, 0);
    if ((void *)-1 == shminfo->shmaddr) {
	perror("shmat");
	goto shm_error;
    }
    xvimage->data = shminfo->shmaddr;
    shminfo->readOnly = False;
    
    XShmAttach(dpy, shminfo);
    XSync(dpy, False);
    if (no_mitshm)
	goto shm_error;
    shmctl(shminfo->shmid, IPC_RMID, 0);
    XSetErrorHandler(old_handler);
    *shm = shminfo;
    return xvimage;

shm_error:
    if (xvimage) {
	XFree(xvimage);
	xvimage = NULL;
    }
    if ((void *)-1 != shminfo->shmaddr  &&  NULL != shminfo->shmaddr)
	shmdt(shminfo->shmaddr);
    free(shminfo);
    XSetErrorHandler(old_handler);
    fprintf(stderr,"WARNING: MIT shared memory extention not available\n");
    no_mitshm = 1;

 no_mitshm:
    *shm = NULL;
    if (NULL == (ximage_data = malloc(width * height * 2))) {
	fprintf(stderr,"out of memory\n");
	exit(1);
    }
    xvimage = XvCreateImage(dpy, im_port, format, ximage_data,
			    width, height);
    return xvimage;
}

static void
xv_destroy_ximage(Display *dpy, XvImage * xvimage, XShmSegmentInfo *shm)
{
    if (shm && !no_mitshm) {
	XShmDetach(dpy, shm);
	XFree(xvimage);
	shmdt(shm->shmaddr);
	free(shm);
    } else
	XFree(xvimage);
}

static void xv_blit(struct ng_video_buf *buf)
{
    struct blit_video_buf_priv *p = buf->priv;
    struct blit_handle *h = p->blit;
    int wx,wy,ww,wh;

    blit_ratio(buf,&wx,&wy,&ww,&wh);
    if (p->shm)
	XvShmPutImage(h->dpy, im_port,
		      h->win, h->gc, p->xvimage,
		      0,0, buf->fmt.width, buf->fmt.height,
		      wx,wy,ww,wh,
		      True);
    else
	XvPutImage(h->dpy, im_port,
		   h->win, h->gc, p->xvimage,
		   0,0, buf->fmt.width, buf->fmt.height,
		   wx,wy,ww,wh);
}
#endif

/* ------------------------------------------------------------------------ */
/* OpenGL code                                                              */

/* TODO:  look at:

      MESA_ycbcr_texture, SGI_ycrcb, APPLE_ycbcr, NVX_ycbcr,
      NV_texture_rectangle, EXT_texture_rectangle,
      APPLE_client_storage, APPLE_fence

      http://mesa3d.sourceforge.net/MESA_ycbcr_texture.spec
      http://oss.sgi.com/projects/ogl-sample/registry/APPLE/client_storage.txt
      http://oss.sgi.com/projects/ogl-sample/registry/APPLE/fence.txt

*/

#if HAVE_GL
static int have_gl,max_gl;
static int gl_attrib[] = { GLX_RGBA,
			   GLX_RED_SIZE, 1,
			   GLX_GREEN_SIZE, 1,
			   GLX_BLUE_SIZE, 1,
			   GLX_DOUBLEBUFFER,
			   None };

struct {
    int  ifmt;
    int  fmt;
    int  type;
    char *ext;
    int  works;
} gl_formats[VIDEO_FMT_COUNT] = {
    [ VIDEO_RGB24 ] = {
	ifmt: GL_RGB,
	fmt:  GL_RGB,
	type: GL_UNSIGNED_BYTE,
    },
#ifdef GL_EXT_bgra
    [ VIDEO_BGR24 ] = {
	ifmt: GL_RGB,
	fmt:  GL_BGR_EXT,
	type: GL_UNSIGNED_BYTE,
	ext:  "GL_EXT_bgra",
    },
    [ VIDEO_BGR32 ] = {
	ifmt: GL_RGB,
	fmt:  GL_BGRA_EXT,
	type: GL_UNSIGNED_BYTE,
	ext:  "GL_EXT_bgra",
    },
#endif
#ifdef GL_MESA_ycbcr_texture
    [ VIDEO_UYVY ] = {
	ifmt: GL_YCBCR_MESA,
	fmt:  GL_YCBCR_MESA,
	type: GL_UNSIGNED_SHORT_8_8_MESA,
	ext:  "GL_MESA_ycbcr_texture",
    },
    [ VIDEO_YUYV ] = {
	ifmt: GL_YCBCR_MESA,
	fmt:  GL_YCBCR_MESA,
	type: GL_UNSIGNED_SHORT_8_8_REV_MESA,
	ext:  "GL_MESA_ycbcr_texture",
    },
#endif
};

static int gl_init(Display *dpy, Screen *scr, Window win)
{
    void *old_handler;
    XVisualInfo *visinfo;
    GLXContext ctx;

    if (debug)
	fprintf(stderr,"blit: gl: init [window=0x%lx]\n",win);
    visinfo = glXChooseVisual(dpy, XScreenNumberOfScreen(scr),
			      gl_attrib);
    if (!visinfo) {
	if (debug)
	    fprintf(stderr,"blit: gl: can't get visual (rgb,db)\n");
	return -1;
    }
    ctx = glXCreateContext(dpy, visinfo, NULL, True);
    if (!ctx) {
	if (debug)
	    fprintf(stderr,"blit: gl: can't create context\n");
	return -1;
    }

    /* there is no point in using OpenGL for image scaling if it
     * isn't hardware accelerated ... */
    if (debug)
	fprintf(stderr, "blit: gl: DRI=%s\n",
		glXIsDirect(dpy, ctx) ? "Yes" : "No");
    if (!glXIsDirect(dpy, ctx))
	return -1;

    old_handler = XSetErrorHandler(catch_gl_error);
    glXMakeCurrent(dpy,win,ctx);
    XSync(dpy, False);
    XSetErrorHandler(old_handler);
    if (gl_error)
	return -1;
    
    have_gl = 1;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_gl);
    if (debug)
	fprintf(stderr,"blit: gl: texture max size: %d\n",max_gl);
    return 0;
}

static int gl_ext(GLubyte *find)
{
    int len = strlen(find);
    const GLubyte *ext;
    GLubyte *pos;
    
    ext = glGetString(GL_EXTENSIONS);
    if (NULL == ext)
	return 0;
    if (NULL == (pos = strstr(ext,find)))
	return 0;
    if (pos != ext && pos[-1] != ' ')
	return 0;
    if (pos[len] != ' ' && pos[len] != '\0')
	return 0;
    if (debug)
	fprintf(stderr,"blit: gl: extension %s is available\n",find);
    return 1;
}

static int gl_resize(int ww, int wh)
{
    glClearColor (0.0, 0.0, 0.0, 0.0);
    glShadeModel(GL_FLAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    glViewport(0, 0, ww, wh);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, ww, 0.0, wh, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    return 0;
}

static void gl_blit(struct ng_video_buf *buf)
{
    struct blit_video_buf_priv *p = buf->priv;
    struct blit_handle *h = p->blit;
    float x,y;
    int wx,wy,ww,wh;

    blit_ratio(buf,&wx,&wy,&ww,&wh);

    /* TODO: clear window bg */
    
    glBindTexture(GL_TEXTURE_2D, p->tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
		    0,0, buf->fmt.width,buf->fmt.height,
		    gl_formats[buf->fmt.fmtid].fmt,
		    gl_formats[buf->fmt.fmtid].type,
		    buf->data);
    x = (float)buf->fmt.width  / p->tw;
    y = (float)buf->fmt.height / p->th;

    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glBegin(GL_QUADS);
    glTexCoord2f(0,y);  glVertex3f(wx,    wy,    0);
    glTexCoord2f(0,0);  glVertex3f(wx,    wy+wh, 0);
    glTexCoord2f(x,0);  glVertex3f(wx+ww, wy+wh, 0);
    glTexCoord2f(x,y);  glVertex3f(wx+ww, wy,    0);
    glEnd();
    glXSwapBuffers(h->dpy, h->win);
    glDisable(GL_TEXTURE_2D);
}
#endif

/* ------------------------------------------------------------------------ */
/* video frame blitter                                                      */

static gboolean blit_configure(GtkWidget *widget,
			       GdkEventConfigure *event,
			       gpointer user_data)
{
    struct blit_handle *h = user_data;

    h->width  = event->width;
    h->height = event->height;
    if (debug)
	fprintf(stderr,"blit: window resize %dx%d\n",h->width,h->height);
#ifdef HAVE_GL
    if (have_gl)
	gl_resize(h->width,h->height);
#endif
    gtk_widget_queue_draw(h->widget);
    return 0;
}

static gboolean blit_exposure(GtkWidget *widget,
			      GdkEventExpose *event,
			      gpointer user_data)
{
    struct blit_handle *h = user_data;

    if (0 != event->count)
	return 0;
    if (debug > 1)
	fprintf(stderr,"blit: expose\n");
    switch (h->mode) {
#ifdef HAVE_LIBXV
    case MODE_XVIDEO:
	XvStopVideo(h->dpy, im_port, h->win);
	XClearWindow(h->dpy, h->win);
	break;
#endif
    default:
	/* nothing */
	break;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */

static void blit_delete_buf(struct ng_video_buf *buf)
{
    struct blit_video_buf_priv *p = buf->priv;
    struct blit_handle *h = p->blit;

    switch (p->mode) {
#ifdef HAVE_LIBXV
    case MODE_XVIDEO:
	if (debug)
	    fprintf(stderr,"blit: delete xvideo buffer %dx%d/[%s] seq %d\n",
		    buf->fmt.width, buf->fmt.height,
		    ng_vfmt_to_desc[buf->fmt.fmtid], p->seq);
	xv_destroy_ximage(h->dpy,p->xvimage,p->shm);
	break;
#endif
#if HAVE_GL
    case MODE_OPENGL:
	if (debug)
	    fprintf(stderr,"blit: FIXME delete openbl buffer %dx%d/[%s] seq %d\n",
		    buf->fmt.width, buf->fmt.height,
		    ng_vfmt_to_desc[buf->fmt.fmtid], p->seq);
	/* FIXME */
	break;
#endif
    case MODE_X11:
	if (debug)
	    fprintf(stderr,"blit: delete x11 buffer %dx%d/[%s] seq %d\n",
		    buf->fmt.width, buf->fmt.height,
		    ng_vfmt_to_desc[buf->fmt.fmtid], p->seq);
	x11_destroy_ximage(h->dpy,p->ximage,p->shm);
	break;
    default:
	BUG_ON(1, "invalid mode");
    }
    free(p);
    free(buf);
    h->buffers--;
}

static void blit_release_buf(struct ng_video_buf *buf)
{
#if 1
    struct blit_video_buf_priv *p = buf->priv;
    struct blit_handle *h = p->blit;

    list_add_tail(&p->list, &h->freelist);
#else
    blit_delete_buf(buf);
#endif
}

static struct ng_video_buf*
blit_create_buf(struct blit_handle *h, struct ng_video_fmt *fmt)
{
    static int seq = 0;
    struct ng_video_buf         *buf;
    struct blit_video_buf_priv  *p;
    void *dummy;
    int i;

    buf = malloc(sizeof(*buf));
    ng_init_video_buf(buf);
    p = malloc(sizeof(*p));
    memset(p,0,sizeof(*p));

    buf->priv     = p;
    buf->fmt      = *fmt;
    buf->release  = blit_release_buf;
    buf->size     = fmt->width * fmt->height *
	ng_vfmt_to_depth[fmt->fmtid] / 8;
    buf->refcount = 1;
    p->buf  = buf;
    p->blit = h;
    p->seq  = seq++;
    
#ifdef HAVE_LIBXV
    /* Xvideo extention */
    if (0 != im_formats[fmt->fmtid]) {
	p->mode    = MODE_XVIDEO;
	p->xvimage = xv_create_ximage(h->dpy,
				      fmt->width, fmt->height,
				      im_formats[fmt->fmtid],
				      &p->shm);
	buf->data = p->xvimage->data;
	if (debug)
	    fprintf(stderr,"blit: create xvideo buffer %dx%d/[%s] seq %d\n",
		    buf->fmt.width, buf->fmt.height,
		    ng_vfmt_to_desc[buf->fmt.fmtid], p->seq);
	h->buffers++;
	return buf;
    }
#endif

#if HAVE_GL
    /* OpenGL */
    if (have_gl) {
	if (gl_formats[fmt->fmtid].fmt   != VIDEO_NONE &&
	    gl_formats[fmt->fmtid].works == 0) {
	    if (NULL != gl_formats[fmt->fmtid].ext)
		gl_formats[fmt->fmtid].works = 
		    gl_ext(gl_formats[fmt->fmtid].ext) ? 1 : -1;
	    else
		gl_formats[fmt->fmtid].works = 1;
	}
	if (gl_formats[fmt->fmtid].works == 1 &&
	    fmt->width <= max_gl && fmt->height <= max_gl) {

	    /* figure texture size */
	    for (i = 0; fmt->width >= (1 << i); i++)
		;
	    p->tw = (1 << i);
	    for (i = 0; fmt->height >= (1 << i); i++)
		;
	    p->th = (1 << i);

	    /* create texture */
	    p->mode  = MODE_OPENGL;
	    glGenTextures(1, &p->tex);
	    glBindTexture(GL_TEXTURE_2D, p->tex);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	    dummy = malloc(p->tw * p->th * 4);
	    memset(dummy, 128, p->tw * p->th * 4);
	    glTexImage2D(GL_TEXTURE_2D, 0,
			 gl_formats[fmt->fmtid].ifmt,
			 p->tw, p->th,0,
			 gl_formats[fmt->fmtid].fmt,
			 gl_formats[fmt->fmtid].type,
			 dummy);
	    free(dummy);

	    /* image buffer */
	    buf->data = malloc(buf->size);
	    
	    if (debug)
		fprintf(stderr,"blit: create opengl buffer %dx%d/[%s] seq %d\n",
			buf->fmt.width, buf->fmt.height,
			ng_vfmt_to_desc[buf->fmt.fmtid], p->seq);
	    h->buffers++;
	    return buf;
	}
    }
#endif

    /* plain X11 */
    if (x11_dpy_fmtid == fmt->fmtid) {
	p->mode   = MODE_X11;
	p->ximage = x11_create_ximage(h->dpy, h->vinfo,
				      fmt->width, fmt->height,
				      &p->shm);
	buf->data = p->ximage->data;
	if (debug)
	    fprintf(stderr,"blit: create x11 buffer %dx%d/[%s] seq %d\n",
		    buf->fmt.width, buf->fmt.height,
		    ng_vfmt_to_desc[buf->fmt.fmtid], p->seq);
	h->buffers++;
	return buf;
    }
    return NULL;
}

/* ------------------------------------------------------------------------ */
/* video frame blitter public interface                                     */

static int blit_count;

struct blit_handle*
blit_init(GtkWidget *widget, XVisualInfo *vinfo, int use_xv, int use_gl)
{
    struct blit_handle *h;
    gint x,y,width,height,depth;

    if (debug)
	fprintf(stderr,"blit: init\n");
    
    h = malloc(sizeof(*h));
    memset(h,0,sizeof(*h));

    h->widget = widget;
    h->dpy    = gdk_x11_display_get_xdisplay(gtk_widget_get_display(widget));
    h->scr    = gdk_x11_screen_get_xscreen(gtk_widget_get_screen(widget));
    h->win    = gdk_x11_drawable_get_xid(widget->window);
    h->vinfo  = vinfo;

    BUG_ON(0 == h->win, "no blit window");
    h->gc     = XCreateGC(h->dpy,h->win,0,NULL);
    INIT_LIST_HEAD(&h->freelist);

    g_signal_connect(widget, "configure-event",
		     G_CALLBACK(blit_configure), h);
    g_signal_connect(widget, "expose-event",
		     G_CALLBACK(blit_exposure), h);
    gtk_widget_set_double_buffered(widget, FALSE);
    gdk_window_get_geometry(widget->window, &x, &y, &width, &height, &depth);
    h->width  = width;
    h->height = height;

#ifdef HAVE_LIBXV
    if (use_xv)
	xv_image_init(h->dpy);
#endif

#ifdef HAVE_GL
    if (use_gl) {
	gl_init(h->dpy, h->scr, h->win);
	if (have_gl)
	    gl_resize(h->width,h->height);
    }
#endif
    blit_count++;
    return h;
}

void blit_fini(struct blit_handle *h)
{
    struct ng_video_buf *buf;
    struct blit_video_buf_priv *p;
    
    for (;;) {
	if (list_empty(&h->freelist))
	    break;
	p   = list_entry(h->freelist.next, struct blit_video_buf_priv, list);
	buf = p->buf;
	list_del(&p->list);
	blit_delete_buf(buf);
    }
    g_signal_handlers_disconnect_by_func(h->widget, G_CALLBACK(blit_configure), h);
    g_signal_handlers_disconnect_by_func(h->widget, G_CALLBACK(blit_exposure), h);
    OOPS_ON(h->buffers > 0, "blit video buffer leak (%d)", h->buffers);
    blit_count--;
    free(h);
}

static void __fini blit_check(void)
{
    OOPS_ON(blit_count > 0, "blit count is %d (expected 0)", blit_count);
}

void blit_get_formats(struct blit_handle *h, int *fmtids, int max)
{
    int i, n=0;

    BUG_ON(NULL == h, "blit handle is NULL");
    memset(fmtids,0,sizeof(*fmtids)*max);

    /* Xvideo extension */
#ifdef HAVE_LIBXV
    for (i = 0; i < VIDEO_FMT_COUNT; i++) {
	if (0 != im_formats[i])
	    fmtids[n++] = i;
	if (n == max)
	    return;
    }
#endif

#if HAVE_GL
    /* OpenGL */
    if (have_gl) {
	for (i = 0; i < VIDEO_FMT_COUNT; i++) {
	    if (0 != gl_formats[i].fmt  &&
		(NULL == gl_formats[i].ext || gl_ext(gl_formats[i].ext)))
		fmtids[n++] = i;
	    if (n == max)
		return;
	}
    }
#endif

    /* plain X11 */
    fmtids[n++] = x11_dpy_fmtid;
    if (n == max)
	return;
}

struct ng_video_buf*
blit_get_buf(void *handle, struct ng_video_fmt *fmt)
{
    struct blit_handle *h = handle;
    struct ng_video_buf *buf;
    struct blit_video_buf_priv *p;

    for (;;) {
	if (list_empty(&h->freelist))
	    break;
	p   = list_entry(h->freelist.next, struct blit_video_buf_priv, list);
	buf = p->buf;
	list_del(&p->list);
	if (0 == memcmp(&buf->fmt,fmt,sizeof(*fmt))) {
	    buf->refcount++;
	    return buf;
	}
	blit_delete_buf(buf);
    }
    return blit_create_buf(h, fmt);
}

void blit_draw_buf(struct ng_video_buf *buf)
{
    static const char *type[] = {
	[ NG_FRAME_UNKNOWN ] = "",
	[ NG_FRAME_I_FRAME ] = "[I-Frame]",
	[ NG_FRAME_P_FRAME ] = "[P-Frame]",
	[ NG_FRAME_B_FRAME ] = "[B-Frame]",
    };

    struct blit_video_buf_priv *p = buf->priv;
    struct blit_handle *h = p->blit;

    if (debug > 1)
	fprintf(stderr,"blit: draw buffer %dx%d/[%s] size %ld seq %d "
		"| ts %.3f file %d play %d %s\n",
		buf->fmt.width, buf->fmt.height,
		ng_vfmt_to_desc[buf->fmt.fmtid],
		buf->size, p->seq,
		buf->info.ts / 1000000000.0,
		buf->info.file_seq, buf->info.play_seq,
		type[buf->info.frame]);
    switch (p->mode) {
#ifdef HAVE_LIBXV
    case MODE_XVIDEO:
	xv_blit(buf);
	break;
#endif
#if HAVE_GL
    case MODE_OPENGL:
	gl_blit(buf);
	break;
#endif
    case MODE_X11:
	x11_blit(buf);
	break;
    default:
	BUG_ON(1, "invalid mode");
    }
    XSync(h->dpy, False);
    h->mode = p->mode;
}
