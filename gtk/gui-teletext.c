#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <iconv.h>
#include <langinfo.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#define _XFT_NO_COMPAT_ 1
#include <X11/Xft/Xft.h>

#include "list.h"
#include "grab-ng.h"
#include "parseconfig.h"
#include "vbi-data.h"
#include "vbi-dvb.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "gui.h"

#if 1
/* from vbi/src/lang.h */
typedef enum {
        LATIN_G0 = 1,
        LATIN_G2,
        CYRILLIC_1_G0,
        CYRILLIC_2_G0,
        CYRILLIC_3_G0,
        CYRILLIC_G2,
        GREEK_G0,
        GREEK_G2,
        ARABIC_G0,
        ARABIC_G2,
        HEBREW_G0,
        BLOCK_MOSAIC_G1,
        SMOOTH_MOSAIC_G3
} vbi_character_set;

typedef enum {
        NO_SUBSET,
        CZECH_SLOVAK,
        ENGLISH,
        ESTONIAN,
        FRENCH,
        GERMAN,
        ITALIAN,
        LETT_LITH,
        POLISH,
        PORTUG_SPANISH,
        RUMANIAN,
        SERB_CRO_SLO,
        SWE_FIN_HUN,
        TURKISH
} vbi_national_subset;

struct vbi_font_descr {
        vbi_character_set       G0;
        vbi_character_set       G2;     
        vbi_national_subset     subset;         /* applies only to LATIN_G0 */
        char *                  label;          /* Latin-1 */
};

extern struct vbi_font_descr    vbi_font_descriptors[88];
#endif

extern int debug;

/* --------------------------------------------------------------------- */

struct vbi_selection {
    struct list_head  list;
    GdkAtom           atom;
    struct vbi_page   pg;
    struct vbi_rect   rect;
    Pixmap            pix;
};

struct vbi_font {
    char *label;
    char *xlfd1;
    char *xlfd2;
    char *xft;
};

struct vbi_window {
    GtkWidget         *top,*tt;
    GtkWidget         *savebox,*cmenu;
    GtkWidget         *subpages,*stations,*fonts;
    GtkWidget         *sub[VBI_MAX_SUBPAGES];
    Display           *dpy;
    Screen            *scr;
    Colormap          cmap;
    GC                gc;
    XFontStruct       *font1,*font2;
    int               w,a,d,h;
    unsigned long     colors[8];

    XftFont           *xft_font;
    XftColor          xft_color[8];

    struct vbi_state  *vbi;
    struct vbi_page   pg;
    int               pgno,subno;

    struct dvbmon     *dvbmon;
    int               tsid;
    
    int               newpage;
    Time              down;
    struct vbi_rect   s;

    struct list_head  selections;
};

static void vbi_create_subpage_menu(struct vbi_window *vw);
static void vbi_update_subpage_menu(struct vbi_window *vw);
static void vbi_setpage(struct vbi_window *vw, int pgno, int subno);
static void save_page_dialog(struct vbi_window *vw);

static void selection_init(struct vbi_window *vw, GdkAtom selection);

#if 0
static void vbi_new_cb(Widget, XtPointer, XtPointer);
static void vbi_goto_cb(Widget, XtPointer, XtPointer);
static void selection_pri(struct vbi_window *vw);
static void selection_dnd_start(struct vbi_window *vw, XEvent *event);

struct vbi_window* vbi_render_init(Widget shell, Widget tt,
				   struct vbi_state *vbi);
void vbi_render_free_font(Widget shell, struct vbi_window *vw);
void vbi_render_set_font(Widget shell, struct vbi_window *vw, char *label);
void vbi_render_line(struct vbi_window *vw, Drawable d, struct vbi_char *ch,
		     int y, int top, int left, int right);
Pixmap vbi_export_pixmap(struct vbi_window *vw,
			 struct vbi_page *pg, struct vbi_rect *rect);
#endif

/* --------------------------------------------------------------------- */

static int tt_windows = 0;
static GQuark mtt_font;
static GQuark mtt_subpage;
static GQuark mtt_pid;
static GQuark mtt_pnr;

static struct vbi_font vbi_fonts[] = {
    {
	.label = "Teletext 20px",
	.xlfd1 = "-*-teletext-medium-r-normal--20-*-*-*-*-*-iso10646-1",
	.xlfd2 = "-*-teletext-medium-r-normal--40-*-*-*-*-*-iso10646-1",
    },{
	.label = "Teletext 10px",
	.xlfd1 = "-*-teletext-medium-r-normal--10-*-*-*-*-*-iso10646-1",
	.xlfd2 = "-*-teletext-medium-r-normal--20-*-*-*-*-*-iso10646-1",
    },{
	.label = "sep",
    },{
	.label = "Fixed 20px",
	.xlfd1 = "-*-fixed-medium-r-normal--20-*-*-*-*-*-iso10646-1",
    },{
	.label = "Fixed 18px",
	.xlfd1 = "-*-fixed-medium-r-normal--18-*-*-*-*-*-iso10646-1",
    },{
	.label = "Fixed 13px",
	.xlfd1 = "-misc-fixed-medium-r-semicondensed--13-*-*-*-*-*-iso10646-1",
    },{
	.label = "sep",
    },{
	.label = "Mono 20pt",
	.xft   = "mono:size=20",
    },{
	.label = "Mono 18pt",
	.xft   = "mono:size=18",
    },{
	.label = "Mono 16pt",
	.xft   = "mono:size=16",
    },{
	.label = "Mono 14pt",
	.xft   = "mono:size=14",
    },{
	.label = "Mono 12pt",
	.xft   = "mono:size=12",
    },{
	.label = "Mono 10pt",
	.xft   = "mono:size=10",
    },{
	/* end of list */
    }
};

static void __init quarks(void)
{
    mtt_pid     = g_quark_from_string("mtt-pid");
    mtt_pnr     = g_quark_from_string("mtt-pnr");
    mtt_font    = g_quark_from_string("mtt-font");
    mtt_subpage = g_quark_from_string("mtt-subpage");
}

/* --------------------------------------------------------------------- */

static int vbi_render_try_font(struct vbi_window *vw,
			       struct vbi_font *fnt)
{
    FcPattern  *pattern;
    FcResult   rc;

    if (NULL != vw->xft_font)
	return 0;

    if (NULL != fnt->xlfd1) {
	/* core font */
	vw->font1 = XLoadQueryFont(vw->dpy, fnt->xlfd1);
	if (NULL != fnt->xlfd2)
	    vw->font2 = XLoadQueryFont(vw->dpy, fnt->xlfd2);
	if (NULL != vw->font1) {
	    vw->a      = vw->font1->max_bounds.ascent;
	    vw->d      = vw->font1->max_bounds.descent;
	    vw->w      = vw->font1->max_bounds.width;
	    vw->h      = vw->a + vw->d;
	    return 0;
	}
    }

    if (NULL != fnt->xft) {
	/* xft */
	pattern = FcNameParse(fnt->xft);
	pattern = XftFontMatch(vw->dpy, XScreenNumberOfScreen(vw->scr),
			       pattern,&rc);
	vw->xft_font = XftFontOpenPattern(vw->dpy, pattern);
	if (vw->xft_font) {
	    vw->a = vw->xft_font->ascent;
	    vw->d = vw->xft_font->descent;
	    vw->w = vw->xft_font->max_advance_width;
	    vw->h = vw->xft_font->height;
	    return 0;
	}
    }
    return 1;
}

static void vbi_render_free_font(struct vbi_window *vw)
{
    if (NULL != vw->xft_font) {
	XftFontClose(vw->dpy,vw->xft_font);
	vw->xft_font = NULL;
    }
    if (NULL != vw->font1) {
	XFreeFont(vw->dpy,vw->font1);
	vw->font1 = NULL;
    }
    if (NULL != vw->font2) {
	XFreeFont(vw->dpy,vw->font2);
	vw->font2 = NULL;
    }
}

static void vbi_render_set_font(struct vbi_window *vw, char *label)
{
    int        i;

    /* free old stuff */
    vbi_render_free_font(vw);

    if (NULL != label) {
	/* look for specified font */
	for (i = 0; vbi_fonts[i].label != NULL; i++) {
	    if (0 != strcasecmp(label,vbi_fonts[i].label))
		continue;
	    if (0 == vbi_render_try_font(vw, &vbi_fonts[i]))
		return;
	}
    }

    /* walk through the whole list as fallback */
    for (i = 0; vbi_fonts[i].label != NULL; i++) {
	if (0 == vbi_render_try_font(vw, &vbi_fonts[i]))
	    return;
    }
    fprintf(stderr,"Oops: can't load any font\n");
    exit(1);
}

static struct vbi_window*
vbi_render_init(GtkWidget *toplevel, struct vbi_state *vbi)
{
    struct vbi_window *vw;
    XColor color,dummy;
    int i;

    vw = malloc(sizeof(*vw));
    memset(vw,0,sizeof(*vw));

    vw->top  = toplevel;
    vw->dpy  = gdk_x11_display_get_xdisplay(gtk_widget_get_display(toplevel));
    vw->scr  = gdk_x11_screen_get_xscreen(gtk_widget_get_screen(toplevel));
    vw->cmap = gdk_x11_colormap_get_xcolormap(gtk_widget_get_colormap(toplevel));
    vw->vbi  = vbi;
    vw->gc   = XCreateGC(vw->dpy, RootWindowOfScreen(vw->scr),
			 0, NULL);

    for (i = 0; i < 8; i++) {
	XftColorAllocName(vw->dpy, DefaultVisualOfScreen(vw->scr),
			  vw->cmap, vbi_colors[i], &vw->xft_color[i]);
	XAllocNamedColor(vw->dpy, vw->cmap, vbi_colors[i],
			 &color, &dummy);
	vw->colors[i] = color.pixel;
    }
    vbi_render_set_font(vw,NULL);

    INIT_LIST_HEAD(&vw->selections);
    return vw;
}

static void
vbi_render_line(struct vbi_window *vw, Drawable d, struct vbi_char *ch,
		int y, int top, int left, int right)
{
    XGCValues values;
    XChar2b line[42];
    int x1,x2,i,code,sy;
    FcChar32 wline[42];
    XftDraw *xft_draw = NULL;

    for (x1 = left; x1 < right; x1 = x2) {
	for (x2 = x1; x2 < right; x2++) {
	    if (ch[x1].foreground != ch[x2].foreground)
		break;
	    if (ch[x1].background != ch[x2].background)
		break;
	    if (ch[x1].size != ch[x2].size)
		break;
	}
	sy = 1;
	if (vw->font2) {
	    if (ch[x1].size == VBI_DOUBLE_HEIGHT ||
		ch[x1].size == VBI_DOUBLE_SIZE)
		sy = 2;
	    if (ch[x1].size == VBI_DOUBLE_HEIGHT2 ||
		ch[x1].size == VBI_DOUBLE_SIZE2)
		continue;
	}

	for (i = x1; i < x2; i++) {
	    code = ch[i].unicode;
	    if (ch[i].conceal)
		code = ' ';
	    if (ch[i].size == VBI_OVER_TOP       ||
		ch[i].size == VBI_OVER_BOTTOM    ||
		ch[i].size == VBI_DOUBLE_HEIGHT2 ||
		ch[i].size == VBI_DOUBLE_SIZE2)
		code = ' ';
	    line[i-x1].byte1 = (code >> 8) & 0xff;
	    line[i-x1].byte2 =  code       & 0xff;
	    wline[i-x1] = code;
	}

	values.function   = GXcopy;
	values.foreground = vw->colors[ch[x1].background & 7];
	XChangeGC(vw->dpy, vw->gc, GCForeground|GCFunction, &values);
	XFillRectangle(vw->dpy, d, 
		       vw->gc, (x1-left)*vw->w, (y-top)*vw->h,
		       vw->w * (x2-x1), vw->h * sy);

	if (vw->xft_font) {
	    if (NULL == xft_draw)
		xft_draw = XftDrawCreate(vw->dpy, d,
					 DefaultVisualOfScreen(vw->scr),
					 vw->cmap);
	    XftDrawString32(xft_draw, &vw->xft_color[ch[x1].foreground & 7],
			    vw->xft_font,
			    (x1-left)*vw->w, vw->a + (y-top+sy-1)*vw->h,
			    wline, x2-x1);
	} else {
	    values.foreground = vw->colors[ch[x1].foreground & 7];
	    values.font = (1 == sy) ? vw->font1->fid : vw->font2->fid;
	    XChangeGC(vw->dpy, vw->gc, GCForeground|GCFont, &values);
	    XDrawString16(vw->dpy, d, vw->gc,
			  (x1-left)*vw->w, vw->a + (y-top+sy-1)*vw->h,
			  line, x2-x1);
	}
    }
    if (NULL != xft_draw)
	XftDrawDestroy(xft_draw);
}

static Pixmap
vbi_export_pixmap(struct vbi_window *vw,
		  struct vbi_page *pg, struct vbi_rect *rect)
{
    Drawable win = gdk_x11_drawable_get_xid(vw->top->window);
    Pixmap pix;
    vbi_char *ch;
    int y;

    pix = XCreatePixmap(vw->dpy, win,
			vw->w * (rect->x2 - rect->x1),
			vw->h * (rect->y2 - rect->y1),
			DefaultDepthOfScreen(vw->scr));
    for (y = rect->y1; y < rect->y2; y++) {
	ch = vw->pg.text + 41*y;
	vbi_render_line(vw,pix,ch,y,rect->y1,rect->x1,rect->x2);
    }
    return pix;
}

/* --------------------------------------------------------------------- */

static void
vbi_fix_head(struct vbi_window *vw, struct vbi_char *ch)
{
    int showno,showsub,red,i;

    showno  = vw->pg.pgno;
    showsub = vw->pg.subno;
    red     = 0;
    if (0 == showno) {
	showno  = vw->pgno;
	showsub = 0;
	red     = 1;
    }
    if (vw->newpage) {
	showno  = vw->newpage;
	showsub = 0;
	red     = 1;
    }

    for (i = 1; i <= 6; i++)
	ch[i].unicode = ' ';
    if (showno >= 0x100)
	ch[1].unicode = '0' + ((showno >> 8) & 0xf);
    if (showno >= 0x10)
	ch[2].unicode = '0' + ((showno >> 4) & 0xf);
    if (showno >= 0x1)
	ch[3].unicode = '0' + ((showno >> 0) & 0xf);
    if (showsub) {
	ch[4].unicode = '/';
	ch[5].unicode = '0' + ((showsub >> 4) & 0xf);
	ch[6].unicode = '0' + ((showsub >> 0) & 0xf);
    }
    if (red) {
	ch[1].foreground = VBI_RED;
	ch[2].foreground = VBI_RED;
	ch[3].foreground = VBI_RED;
    }
}

static void
vbi_check_rectangle(struct vbi_rect *rect)
{
    int h;

    if (rect->x1 > rect->x2)
	h = rect->x1, rect->x1 = rect->x2, rect->x2 = h;
    if (rect->y1 > rect->y2)
	h = rect->y1, rect->y1 = rect->y2, rect->y2 = h;
    
    if (rect->x1 < 0) rect->x1 = 0;
    if (rect->x2 < 0) rect->x2 = 0;
    if (rect->y1 < 0) rect->y1 = 0;
    if (rect->y2 < 0) rect->y2 = 0;

    if (rect->x1 > 41) rect->x1 = 41;
    if (rect->x2 > 41) rect->x2 = 41;
    if (rect->y1 > 25) rect->y1 = 25;
    if (rect->y2 > 25) rect->y2 = 25;
}

static void
vbi_mark_rectangle(struct vbi_window *vw)
{
    Drawable draw = gdk_x11_drawable_get_xid(vw->tt->window);
    struct vbi_rect rect;
    XGCValues values;
    int x,y,w,h;

    rect = vw->s;
    vbi_check_rectangle(&rect);
    x = vw->w * (rect.x1);
    w = vw->w * (rect.x2 - rect.x1);
    y = vw->h * (rect.y1);
    h = vw->h * (rect.y2 - rect.y1);
    values.function   = GXxor;
    values.foreground = ~0;
    XChangeGC(vw->dpy,vw->gc,GCFunction|GCForeground,&values);
    XFillRectangle(vw->dpy, draw, vw->gc, x,y,w,h);
}

static void vbi_print_page_lang(struct vbi_page *pg)
{
    static struct vbi_font_descr *f0, *f1;

    if (pg->font[0] == f0  &&  pg->font[1] == f1)
	return;
    f0 = pg->font[0];
    f1 = pg->font[1];
    fprintf(stderr,"language/region:");
    if (f0)
	fprintf(stderr," font0=\"%s\",(%d)",
		f0->label, (int)(f0-vbi_font_descriptors));
    if (f1)
	fprintf(stderr," font1=\"%s\",(%d)",
		f1->label, (int)(f1-vbi_font_descriptors));
    if (!f0 && !f1)
	fprintf(stderr," unspecified");
    fprintf(stderr,"\n");
}

static void
vbi_render_page(struct vbi_window *vw)
{
    Drawable draw = gdk_x11_drawable_get_xid(vw->tt->window);
    struct vbi_char *ch;
    int y;

    if (debug)
	vbi_print_page_lang(&vw->pg);
    
    vbi_fix_head(vw,vw->pg.text);
    for (y = 0; y < 25; y++) {
	ch = vw->pg.text + 41*y;
	vbi_render_line(vw,draw,ch,y,0,0,41);
    }
    if ((vw->s.x1 || vw->s.x2) &&
	(vw->s.y1 || vw->s.y2))
	vbi_mark_rectangle(vw);
}

static void
vbi_render_head(struct vbi_window *vw, int pgno, int subno)
{
    Drawable draw = gdk_x11_drawable_get_xid(vw->tt->window);
    vbi_page pg;
    
    memset(&pg,0,sizeof(pg));
    vbi_fetch_vt_page(vw->vbi->dec,&pg,pgno,subno,
		      VBI_WST_LEVEL_1p5,1,0);
    vbi_fix_head(vw,pg.text);
    vbi_render_line(vw,draw,pg.text,0,0,0,41);
}

static void
vbi_newdata(struct vbi_event *ev, void *user)
{
    char title[128];
    char *name;
    struct vbi_window *vw = user;
    
    switch (ev->type) {
    case VBI_EVENT_TTX_PAGE:
	if (vw->pgno  == ev->ev.ttx_page.pgno) {
	    if (vw->subno == ev->ev.ttx_page.subno ||
		vw->subno == VBI_ANY_SUBNO) {
		vbi_fetch_vt_page(vw->vbi->dec,&vw->pg,vw->pgno,vw->subno,
				  VBI_WST_LEVEL_1p5,25,1);
		vbi_render_page(vw);
	    }
	    vbi_update_subpage_menu(vw);
	} else {
	    vbi_render_head(vw,
			    ev->ev.ttx_page.pgno,
			    ev->ev.ttx_page.subno);
	}
	break;
    case VBI_EVENT_NETWORK:
	name = "<no station name>";
	if (strlen(ev->ev.network.name) > 0)
	    name = ev->ev.network.name;
	snprintf(title,sizeof(title),"teletext: %s",name);
	gtk_window_set_title(GTK_WINDOW(vw->top), title);
	break;
    }
}

/* --------------------------------------------------------------------- */
/* GUI handling                                                          */

static void
vbi_goto_subpage(GtkWidget *widget, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    int subpage = atoi(g_object_get_qdata(G_OBJECT(widget),mtt_subpage));
    vbi_setpage(vw,vw->pg.pgno,subpage);
}

static void
vbi_create_subpage_menu(struct vbi_window *vw)
{
    GtkWidget *item;
    char str[32];
    int i;

    item = gtk_menu_item_new_with_label("cycle");
    snprintf(str,sizeof(str),"%d", VBI_ANY_SUBNO);
    g_object_set_qdata_full(G_OBJECT(item),mtt_subpage,
			    strdup(str),free);
    g_signal_connect(G_OBJECT(item), "activate",
		     G_CALLBACK(vbi_goto_subpage), vw);
    gtk_menu_shell_append(GTK_MENU_SHELL(vw->subpages), item);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(vw->subpages), item);

    for (i = 1; i < VBI_MAX_SUBPAGES; i++) {
	snprintf(str,sizeof(str),"#%02x",i);
	vw->sub[i] = gtk_menu_item_new_with_label(str);
	snprintf(str,sizeof(str),"%d",i);
	g_object_set_qdata_full(G_OBJECT(vw->sub[i]),mtt_subpage,
				strdup(str),free);
	g_signal_connect(G_OBJECT(vw->sub[i]), "activate",
			 G_CALLBACK(vbi_goto_subpage), vw);
	gtk_menu_shell_append(GTK_MENU_SHELL(vw->subpages),vw->sub[i]);
    }
}

static void
vbi_update_subpage_menu(struct vbi_window *vw)
{
    int i,subs = 0;
    vbi_page pg;

    for (i = 1; i < VBI_MAX_SUBPAGES; i++) {
	if (vbi_fetch_vt_page(vw->vbi->dec,&pg,vw->pg.pgno,i,
			      VBI_WST_LEVEL_1,0,0)) {
	    gtk_widget_show(vw->sub[i]);
	    subs++;
	} else {
	    gtk_widget_hide(vw->sub[i]);
	}
    }
}

static void
vbi_setpage(struct vbi_window *vw, int pgno, int subno)
{
    vw->pgno = pgno;
    vw->subno = subno;
    vw->newpage = 0;
    memset(&vw->pg,0,sizeof(struct vbi_page));
    vbi_fetch_vt_page(vw->vbi->dec,&vw->pg,vw->pgno,vw->subno,
		      VBI_WST_LEVEL_1p5,25,1);
    if (vw->tt->window)
	vbi_render_page(vw);
    vbi_update_subpage_menu(vw);
}

static gboolean
vbi_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
    struct vbi_window *vw = user_data;

    if (0 == event->count)
	vbi_render_page(vw);
    return TRUE;
}

static void vbi_destroy(GtkWidget *widget,
			gpointer data)
{
    struct vbi_window *vw = data;
    
    vbi_event_handler_unregister(vw->vbi->dec,vbi_newdata,vw);
    vbi_render_free_font(vw);
    XFreeGC(vw->dpy,vw->gc);
    free(vw);
    tt_windows--;
    if (0 == tt_windows)
	gtk_main_quit();
}

static void
vbi_button_home(GtkWidget *widget, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    vbi_setpage(vw,0x100,VBI_ANY_SUBNO);
}

static void
vbi_button_prev(GtkWidget *widget, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    vbi_setpage(vw,vbi_calc_page(vw->pgno,-1),VBI_ANY_SUBNO);
}

static void
vbi_button_next(GtkWidget *widget, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    vbi_setpage(vw,vbi_calc_page(vw->pgno,+1),VBI_ANY_SUBNO);
}

static void
vbi_button_close(GtkWidget *widget, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    gtk_widget_destroy(vw->top);
}

static void
vbi_menu_cb(gpointer    user_data,
	    guint       action,
	    GtkWidget  *widget)
{
    static char *about_text =
	"This is mtt, a teletext browser for X11.\n"
	"\n"
	THIS_IS_GPLv2
	"\n"
	"(c) 2004 Gerd Knorr <kraxel@bytesex.org> [SUSE Labs]";

    struct vbi_window *vw = user_data;
    GdkAtom sel;

    switch (action) {
    case 11:
	vbi_create_window(vw->vbi,vw->dvbmon);
	break;
    case 12:
	save_page_dialog(vw);
	break;
    case 13:
	vbi_button_close(widget,vw);
	break;
    case 21:
	sel = gdk_atom_intern("CLIPBOARD", FALSE);
	selection_init(vw, sel);
	gtk_selection_owner_set(vw->tt, sel, GDK_CURRENT_TIME);
	break;
    case 31:
	vbi_button_home(widget,vw);
	break;
    case 32:
	vbi_button_prev(widget,vw);
	break;
    case 33:
	vbi_button_next(widget,vw);
	break;
    case 40 ... 49:
	vbi_teletext_set_default_region(vw->vbi->dec, (action-40)*8);
	break;
    case 91:
	gtk_about_box(GTK_WINDOW(vw->top), "mtt", VERSION, about_text);
	break;
    default:
	fprintf(stderr,"%s [%p]: oops: unknown action %d\n",
		__FUNCTION__, vw, action);
	break;
    }
}

static int
vbi_findpage(struct vbi_page *pg, int px, int py)
{
    int newpage = 0;
    int x;

    if (py == 24) {
	/* navigation line */
	int i = (pg->text[py*41+px].foreground & 7) -1;
	if (i >= 6)
	    i = 0;
	newpage = pg->nav_link[i].pgno;
    } else if (px <= 40 && py <= 23) {
	if (pg->text[py*41+px].unicode >= '0' &&
	    pg->text[py*41+px].unicode <= '9') {
	    /* look for a 3-digit string ... */
	    x = px; newpage = 0;
	    while (pg->text[py*41+x].unicode >= '0' &&
		   pg->text[py*41+x].unicode <= '9' &&
		   x > 0) {
		x--;
	    }
	    x++;
	    while (pg->text[py*41+x].unicode >= '0' &&
		   pg->text[py*41+x].unicode <= '9' &&
		   x < 40) {
		newpage = newpage*16 + pg->text[py*41+x].unicode - '0';
		x++;
	    }

	} else if (pg->text[py*41+px].unicode == '>') {
	    /* next page */
	    newpage = vbi_calc_page(pg->pgno,+1);

	} else if (pg->text[py*41+px].unicode == '<') {
	    /* prev page */
	    newpage = vbi_calc_page(pg->pgno,-1);
	}
    }
    
    if (newpage < 0x100 || newpage >= 0x999)
	return 0;
    return newpage;
}

static gboolean
vbi_keypress(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    int digit = -1;
    int subno;

    if (event->type != GDK_KEY_PRESS)
	return FALSE;
    switch(event->keyval) {
    case GDK_0:
    case GDK_KP_0:
    case GDK_KP_Insert:
	digit = 0;
	break;
    case GDK_1:
    case GDK_KP_1:
    case GDK_KP_End:
	digit = 1;
	break;
    case GDK_2:
    case GDK_KP_2:
    case GDK_KP_Down:
	digit = 2;
	break;
    case GDK_3:
    case GDK_KP_3:
    case GDK_KP_Next:
	digit = 3;
	break;
    case GDK_4:
    case GDK_KP_4:
    case GDK_KP_Left:
	digit = 4;
	break;
    case GDK_5:
    case GDK_KP_5:
    case GDK_KP_Begin:
	digit = 5;
	break;
    case GDK_6:
    case GDK_KP_6:
    case GDK_KP_Right:
	digit = 6;
	break;
    case GDK_7:
    case GDK_KP_7:
    case GDK_KP_Home:
	digit = 7;
	break;
    case GDK_8:
    case GDK_KP_8:
    case GDK_KP_Up:
	digit = 8;
	break;
    case GDK_9:
    case GDK_KP_9:
    case GDK_KP_Prior:
	digit = 9;
	break;
    case GDK_space:
    case GDK_l:
    case GDK_L:
    case GDK_Right:
	vbi_setpage(vw,vbi_calc_page(vw->pgno,+1),VBI_ANY_SUBNO);
	break;
    case GDK_BackSpace:
    case GDK_h:
    case GDK_H:
    case GDK_Left:
	vbi_setpage(vw,vbi_calc_page(vw->pgno,-1),VBI_ANY_SUBNO);
	break;
    case GDK_k:
    case GDK_K:
    case GDK_Down:
	subno = (vw->subno != VBI_ANY_SUBNO) ? vw->subno : vw->pg.subno;
	subno = vbi_calc_subpage(vw->vbi->dec,vw->pgno,subno,+1);
	vbi_setpage(vw,vw->pgno,subno);
	break;
    case GDK_j:
    case GDK_J:
    case GDK_Up:
	subno = (vw->subno != VBI_ANY_SUBNO) ? vw->subno : vw->pg.subno;
	subno = vbi_calc_subpage(vw->vbi->dec,vw->pgno,subno,-1);
	vbi_setpage(vw,vw->pgno,subno);
	break;
#if 0
    default:
	fprintf(stderr,"key: 0x%x\n",event->keyval);
#endif
    }
    if (-1 != digit) {
	vw->newpage *= 16;
	vw->newpage += digit;
	if (vw->newpage >= 0x100)
	    vbi_setpage(vw,vw->newpage,VBI_ANY_SUBNO);
    }
    return TRUE;
}

static gboolean
vbi_mouse_button(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    int px,py,newpage;
    GdkAtom sel;

    switch (event->type) {
    case GDK_BUTTON_PRESS:
	switch (event->button) {
	case 1: /* left mouse button */
	    px = event->x / vw->w;
	    py = event->y / vw->h;
	    vw->s.x1 = vw->s.x2 = px;
	    vw->s.y1 = vw->s.y2 = py;
	    vw->down = event->time;
	    break;
#if 0
	case 2: /* middle button */
	    selection_dnd_start(vw,event);
	    break;
#endif
	}
	return TRUE;
    case GDK_BUTTON_RELEASE:
	switch (event->button) {
	case 1: /* left mouse button */
	    px = event->x / vw->w;
	    py = event->y / vw->h;
	    if (abs(vw->s.x1 - px) < 2  &&  abs(vw->s.y1 - py) < 2  &&
		event->time - vw->down < 500) {
		/* mouse click */
		vw->s.x1 = vw->s.x2 = 0;
		vw->s.y1 = vw->s.y2 = 0;
		newpage = vbi_findpage(&vw->pg,px,py);
		if (0 != newpage)
		    vbi_setpage(vw,newpage,VBI_ANY_SUBNO);
		else
		    vbi_render_page(vw);
	    } else {
		/* marked region */
		vw->s.x2 = px +1;
		vw->s.y2 = py +1;
		vbi_render_page(vw);
		sel = gdk_atom_intern("PRIMARY", FALSE);
		selection_init(vw, sel);
		gtk_selection_owner_set(vw->tt, sel, GDK_CURRENT_TIME);
	    }
	    break;
	case 4: /* wheel up */
	    newpage = vbi_calc_page(vw->pgno,-1);
	    vbi_setpage(vw,newpage,VBI_ANY_SUBNO);
	    break;
	case 5: /* wheel down */
	    newpage = vbi_calc_page(vw->pgno,+1);
	    vbi_setpage(vw,newpage,VBI_ANY_SUBNO);
	    break;
	}
	return TRUE;
    default:
	return FALSE;
    }
}

static gboolean
vbi_mouse_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
    struct vbi_window *vw = user_data;

    switch (event->type) {
    case GDK_MOTION_NOTIFY:
	if (event->state & GDK_BUTTON1_MASK) {
	    vw->s.x2 = event->x / vw->w +1;
	    vw->s.y2 = event->y / vw->h +1;
	    vbi_render_page(vw);
	}
	return TRUE;
    default:
	return FALSE;
    }
}

/* --------------------------------------------------------------------- */
/* export data                                                           */

static char *csets[] = {
    "UTF-8",
    "ISO-8859-1",
    "ISO-8859-2",
    "ISO-8859-15",
    "US-ASCII"
};

static void save_response(GtkDialog *dialog,
			  gint arg1,
			  gpointer user_data)
{
    static struct vbi_rect rect = {
	.x1 =  0,
	.x2 = 41,
	.y1 =  0,
	.y2 = 25,
    };
    struct vbi_window *vw = user_data;
    const char *filename;
    int charset,fh,len;
    char *data;

    if (arg1 == GTK_RESPONSE_OK) {
	charset = gtk_option_menu_get_history(GTK_OPTION_MENU(vw->cmenu));
	filename = gtk_file_selection_get_filename(GTK_FILE_SELECTION(vw->savebox));
	fprintf(stderr,"%s: ok %s %s\n",__FUNCTION__,
		filename,csets[charset]);

	data = malloc(25*41*8);
	len = vbi_export_txt(data,csets[charset],25*41*8,&vw->pg,&rect,
			     VBI_NOCOLOR);
	fh = open(filename,O_WRONLY | O_CREAT, 0666);
	if (-1 == fh) {
	    fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	} else {
	    ftruncate(fh,0);
	    write(fh,data,len);
	    close(fh);
	}
	free(data);
    }
    gtk_widget_hide(GTK_WIDGET(dialog));
}

static void
save_page_dialog(struct vbi_window *vw)
{
    GtkWidget *item, *menu;
    GtkBox *vbox,*hbox;
    char *charset;
    int i;

    if (NULL == vw->savebox) {
	vw->savebox = gtk_file_selection_new("Save page to");
	g_signal_connect(vw->savebox, "response",
			 G_CALLBACK(save_response), vw);
	gtk_dialog_set_default_response(GTK_DIALOG(vw->savebox),
					GTK_RESPONSE_OK);

	vbox = GTK_BOX(GTK_FILE_SELECTION(vw->savebox)->main_vbox);
	hbox = gtk_add_hbox_with_label(vbox, "Character Set");

	menu = gtk_menu_new();
	charset = nl_langinfo(CODESET);
	for (i = 0; i < DIMOF(csets); i++) {
	    item = gtk_menu_item_new_with_label(csets[i]);
	    gtk_menu_append(menu,item);
	    if (0 == strcasecmp(charset,csets[i]))
		gtk_menu_set_active(GTK_MENU(menu),i);
	}
	vw->cmenu = gtk_option_menu_new();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(vw->cmenu),menu);
	gtk_box_pack_start(hbox, vw->cmenu, TRUE, TRUE, 0);
    }
    gtk_widget_show_all(vw->savebox);
}

/* --------------------------------------------------------------------- */
/* selection handling (cut+paste, drag'n'drop)                           */

static struct vbi_selection*
selection_find(struct vbi_window *vw, GdkAtom selection)
{
    struct list_head      *item;
    struct vbi_selection  *sel;
    
    list_for_each(item,&vw->selections) {
	sel = list_entry(item, struct vbi_selection, list);
	if (sel->atom == selection)
	    return sel;
    }
    return NULL;
}

static void
selection_fini(struct vbi_window *vw, GdkAtom selection)
{
    struct vbi_selection  *sel;

    sel = selection_find(vw,selection);
    if (NULL == sel)
	return;
    if (sel->pix)
	XFreePixmap(vw->dpy,sel->pix);

    list_del(&sel->list);
    free(sel);
}

static void
selection_init(struct vbi_window *vw, GdkAtom selection)
{
    struct vbi_selection  *sel;

    selection_fini(vw,selection);
    sel = malloc(sizeof(*sel));
    memset(sel,0,sizeof(*sel));
    list_add_tail(&sel->list,&vw->selections);
    sel->atom = selection;
    sel->pg   = vw->pg;
    sel->rect = vw->s;
    vbi_check_rectangle(&sel->rect);
    if (0 == sel->rect.x2  &&  0 == sel->rect.y2) {
	sel->rect.x2 = 41;
	sel->rect.y2 = 25;
    }
}

GtkTargetEntry vbi_targets[] = {
    { "PIXMAP",       0,  1 },
    { "UTF8_STRING",  0,  2 },
    { "STRING",       0,  3 },
};

static void do_convert(struct vbi_window *vw, GtkSelectionData *sd,
		       gint info)
{
    struct vbi_selection *sel;
    char *cdata;
    int len;
    
    sel = selection_find(vw, sd->selection);
    if (NULL == sel) {
	/* shouldn't happen */
	fprintf(stderr,"tt: oops: selection data not found\n");
	return;
    }

    switch (info) {
    case 1: /* image */
	if (!sel->pix)
	    sel->pix = vbi_export_pixmap(vw,&sel->pg,&sel->rect);
	gtk_selection_data_set(sd, gdk_atom_intern("DRAWABLE", FALSE),
			       32, (gpointer)(&sel->pix), sizeof(sel->pix));
	break;
    case 2: /* utf8 text */
	cdata = malloc(25*41*8);
	len = vbi_export_txt(cdata,"UTF-8",25*41*8,&sel->pg,&sel->rect,
			     VBI_NOCOLOR);
	gtk_selection_data_set(sd, gdk_atom_intern("STRING", FALSE),
			       8, cdata, len);
	free(cdata);
	break;
    case 3: /* us ascii text */
	cdata = malloc(25*41*8);
	len = vbi_export_txt(cdata,"US-ASCII",25*41*8,&sel->pg,&sel->rect,
			     VBI_NOCOLOR);
	gtk_selection_data_set(sd, gdk_atom_intern("STRING", FALSE),
			       8, cdata, len);
	free(cdata);
	break;
    }
}

static void
selection_get(GtkWidget *widget,
	      GtkSelectionData *sd,
	      guint info,
	      guint time,
	      gpointer user_data)
{
    struct vbi_window *vw = user_data;
    do_convert(vw, sd, info);
}

/* --------------------------------------------------------------------- */

#if 0

#ifdef linux
# include "videodev.h"
#endif

static void vbi_station_cb(Widget widget, XtPointer client, XtPointer call)
{
    struct vbi_state *vbi = client;
    char *name = XtName(widget);
    int i;

    for (i = 0; i < count; i++)
	if (0 == strcmp(channels[i]->name,name))
	    break;
    if (i == count)
	return;
    fprintf(stderr,"tune: %.3f MHz [channel %s, station %s]\n",
	    channels[i]->freq / 16.0,
	    channels[i]->cname,
	    channels[i]->name);

#ifdef linux
    if (-1 == ioctl(vbi->fd,VIDIOCSFREQ,&channels[i]->freq))
	perror("ioctl VIDIOCSFREQ");
#endif
    /* FIXME: should add some BSD code once libzvbi is ported ... */
}

static void vbi_analog_station_menu(Widget menu, struct vbi_state *vbi)
{
    struct {
	char *name;
	Widget menu;
    } *sub = NULL;
    int subs = 0;

    Widget m,push;
    XmString label;
#if 0
    char ctrl[15],key[32],accel[64];
#endif
    char *name,*hotkey,*group;
    int j;

    for (name = cfg_sections_first("stations");
	 name != NULL;
         name = cfg_sections_next("stations",name)) {
	hotkey = cfg_get_str("stations", name, "key");
	group  = cfg_get_str("stations", name, "group");

#if 0
	/* hotkey */
	if (NULL != hot) {
	    if (2 == sscanf(hotkey, "%15[A-Za-z0-9_]+%31[A-Za-z0-9_]",
			    ctrl,key))
		sprintf(accel,"%s<Key>%s",ctrl,key);
	    else
		sprintf(accel,"<Key>%s",channels[i]->key);
	} else {
	    accel[0] = 0;
	}
#endif

	m = menu;
	if (NULL != group) {
	    /* submenu */
	    for (j = 0; j < subs; j++)
		if (0 == strcmp(group,sub[j].name))
		    break;
	    if (j == subs) {
		subs++;
		sub = realloc(sub, subs * sizeof(*sub));
		sub[j].name = group;
		sub[j].menu = XmCreatePulldownMenu(menu, group,
						   NULL, 0);
		XtVaCreateManagedWidget(group,
					xmCascadeButtonWidgetClass, menu,
					XmNsubMenuId, sub[j].menu,
					NULL);
	    }
	    m = sub[j].menu;
	}

	label = XmStringGenerate(name, NULL, XmMULTIBYTE_TEXT, NULL);
	push = XtVaCreateManagedWidget(name,
				       xmPushButtonWidgetClass,m,
				       XmNlabelString,label,
				       NULL);
      	// XtAddCallback(push,XmNactivateCallback,vbi_station_cb,vbi);
	XmStringFree(label);
    }
    if (sub)
	free(sub);
}
#endif

/* --------------------------------------------------------------------- */

#ifdef HAVE_DVB

static void
vbi_dvb_pid(GtkWidget *widget, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    int pid = atoi(g_object_get_qdata(G_OBJECT(widget),mtt_pid));
    vbi_set_pid(vw->vbi,pid);
}

static GtkWidget* vbi_dvb_del_widgets(struct vbi_window *vw)
{
    GList *children,*item;
    GtkWidget *child;

    children = gtk_container_get_children(GTK_CONTAINER(vw->stations));
    for (item = g_list_first(children);
	 NULL != item;
	 item = g_list_next(item)) {
	child = item->data;
	gtk_widget_destroy(child);
    }
    return NULL;
}

static GtkWidget* vbi_dvb_get_widget(struct vbi_window *vw,
				    struct psi_program *pr)
{
    GList *children,*item;
    GtkWidget *child;
    int pid,pnr;

    children = gtk_container_get_children(GTK_CONTAINER(vw->stations));
    for (item = g_list_first(children);
	 NULL != item;
	 item = g_list_next(item)) {
	child = item->data;
	pid = atoi(g_object_get_qdata(G_OBJECT(child),mtt_pid));
	pnr = atoi(g_object_get_qdata(G_OBJECT(child),mtt_pnr));
	if (pid == pr->t_pid &&
	    pnr == pr->pnr)
	    return child;
    }
    return NULL;
}

static void dvbwatch_teletext(struct psi_info *info, int event,
			      int tsid, int pnr, void *data)
{
    struct vbi_window *vw = data;
    struct psi_program *pr;
    GtkWidget *item;
    char label[64],pid[8],spnr[8];
    
    switch (event) {
    case DVBMON_EVENT_SWITCH_TS:
	if (vw->tsid == tsid)
	    break;
	vbi_dvb_del_widgets(vw);
	vw->tsid = tsid;
	vw->vbi->pid = 0;
	break;
    case DVBMON_EVENT_UPDATE_PR:
	pr = psi_program_get(info, tsid, pnr, 0);
	if (0 == pr->t_pid)
	    break;
	if (tsid != vw->tsid)
	    break;
	if (0 == vw->vbi->pid)
	    vbi_set_pid(vw->vbi, pr->t_pid);
	item = vbi_dvb_get_widget(vw, pr);
	if (NULL != item)
	    gtk_widget_destroy(item);
	snprintf(label,sizeof(label),"[pid %d] %s / %s",
		 pr->t_pid,pr->net,pr->name);
	snprintf(pid,sizeof(pid),"%d",pr->t_pid);
	snprintf(spnr,sizeof(spnr),"%d",pnr);
	item = gtk_menu_item_new_with_label(label);
	g_object_set_qdata_full(G_OBJECT(item),mtt_pid,
				strdup(pid),free);
	g_object_set_qdata_full(G_OBJECT(item),mtt_pnr,
				strdup(spnr),free);
	g_signal_connect(G_OBJECT(item), "activate",
			 G_CALLBACK(vbi_dvb_pid), vw);
	gtk_menu_shell_append(GTK_MENU_SHELL(vw->stations),item);
	gtk_widget_show(item);
	break;
    }
}

#endif

/* --------------------------------------------------------------------- */

static void
vbi_font_cb(GtkWidget *widget, gpointer user_data)
{
    struct vbi_window *vw = user_data;
    char *name = g_object_get_qdata(G_OBJECT(widget),mtt_font);
    Drawable draw = gdk_x11_drawable_get_xid(vw->tt->window);

    vbi_render_set_font(vw, name);
    gtk_widget_set_size_request(vw->tt, vw->w*41, vw->h*25);
    gtk_widget_queue_resize(vw->tt);
    XClearArea(vw->dpy, draw, 0,0,0,0, True);
}

static void vbi_font_menu(struct vbi_window *vw)
{
    GtkWidget *item;
    int i;

    /* core fonts */
    for (i = 0; vbi_fonts[i].label != NULL; i++) {
	if (0 == strcmp("sep",vbi_fonts[i].label)) {
	    item = gtk_separator_menu_item_new();
	} else {
	    item = gtk_menu_item_new_with_label(vbi_fonts[i].label);
	    g_object_set_qdata(G_OBJECT(item),mtt_font,vbi_fonts[i].label);
	    g_signal_connect(G_OBJECT(item), "activate",
			     G_CALLBACK(vbi_font_cb), vw);
	}
	gtk_menu_shell_append(GTK_MENU_SHELL(vw->fonts),item);
    }
}

/* --------------------------------------------------------------------- */

static GtkItemFactoryEntry menu_items[] = {
    {
	/* --- File menu ----------------------------- */
	.path            = noop("/_File"),
	.item_type       = "<Branch>",
    },{
	.path            = noop("/File/_New window"),
	.accelerator     = "N",
	.callback        = vbi_menu_cb,
	.callback_action = 11,
	.item_type       = "<StockItem>",
	.extra_data      = GTK_STOCK_NEW,
    },{
	.path            = noop("/File/_Save as ..."),
	.accelerator     = "S",
	.callback        = vbi_menu_cb,
	.callback_action = 12,
	.item_type       = "<StockItem>",
	.extra_data      = GTK_STOCK_SAVE,
    },{
	.path            = "/File/sep1",
	.item_type       = "<Separator>",
    },{
	.path            = noop("/File/_Close window"),
	.accelerator     = "Q",
	.callback        = vbi_menu_cb,
	.callback_action = 13,
	.item_type       = "<StockItem>",
	.extra_data      = GTK_STOCK_QUIT,
    },{

	/* --- Edit menu ----------------------------- */
	.path            = noop("/_Edit"),
	.item_type       = "<Branch>",
    },{
	.path            = noop("/Edit/_Copy ..."),
	.callback        = vbi_menu_cb,
	.callback_action = 21,
	.item_type       = "<StockItem>",
	.extra_data      = GTK_STOCK_COPY,
    },{

	/* --- Goto menu ----------------------------- */
	.path            = noop("/_Go to"),
	.item_type       = "<Branch>",
    },{
	.path            = noop("/Go to/_Index"),
	.accelerator     = "I",
	.callback        = vbi_menu_cb,
	.callback_action = 31,
	.item_type       = "<StockItem>",
	.extra_data      = GTK_STOCK_HOME,
    },{
	.path            = noop("/Go to/_Previous Page"),
	.accelerator     = "BackSpace",
	.callback        = vbi_menu_cb,
	.callback_action = 32,
	.item_type       = "<StockItem>",
	.extra_data      = GTK_STOCK_GO_BACK,
    },{
	.path            = noop("/Go to/_Next Page"),
	.accelerator     = "space",
	.callback        = vbi_menu_cb,
	.callback_action = 33,
	.item_type       = "<StockItem>",
	.extra_data      = GTK_STOCK_GO_FORWARD,
    },{

	/* --- Region menu --------------------------- */
	.path            = noop("/_Region"),
	.item_type       = "<Branch>",
    },{
	.path            = noop("/_Region/Western and _Central Europe"),
	.callback        = vbi_menu_cb,
	.callback_action = 40,
	.item_type       = "<RadioItem>",
    },{
	.path            = noop("/_Region/_Eastern Europe"),
	.callback        = vbi_menu_cb,
	.callback_action = 41,
	.item_type       = "/Region/Western and Central Europe",
    },{
	.path            = noop("/_Region/_Western Europe and Turkey"),
	.callback        = vbi_menu_cb,
	.callback_action = 42,
	.item_type       = "/Region/Western and Central Europe",
    },{
	.path            = noop("/_Region/Central and _Southeast Europe"),
	.callback        = vbi_menu_cb,
	.callback_action = 43,
	.item_type       = "/Region/Western and Central Europe",
    },{
	.path            = noop("/_Region/C_yrillic"),
	.callback        = vbi_menu_cb,
	.callback_action = 44,
	.item_type       = "/Region/Western and Central Europe",
#if 0
    },{
	.path            = noop("/_Region/unused5"),
	.callback        = vbi_menu_cb,
	.callback_action = 45,
	.item_type       = "/Region/Western and Central Europe",
#endif
    },{
	.path            = noop("/_Region/_Arabic"),
	.callback        = vbi_menu_cb,
	.callback_action = 46,
	.item_type       = "/Region/Western and Central Europe",
#if 0
    },{
	.path            = noop("/_Region/unused7"),
	.callback        = vbi_menu_cb,
	.callback_action = 47,
	.item_type       = "/Region/Western and Central Europe",
    },{
	.path            = noop("/_Region/unused8"),
	.callback        = vbi_menu_cb,
	.callback_action = 48,
	.item_type       = "/Region/Western and Central Europe",
#endif
    },{
	
	/* --- dynamic stuff ------------------------- */
	.path            = noop("/_Subpage"),
	.item_type       = "<Branch>",
    },{
	.path            = noop("/_Station"),
	.item_type       = "<Branch>",
    },{
	.path            = noop("/_Font"),
	.item_type       = "<Branch>",
    },{

	/* --- Help menu ----------------------------- */
	.path            = noop("/_Help"),
	.item_type       = "<LastBranch>",
    },{
	.path            = noop("/Help/_About ..."),
	.callback        = vbi_menu_cb,
	.callback_action = 91,
	.item_type       = "<Item>",
    }
};

static struct toolbarbutton toolbaritems[] = {
    {
	.text     = noop("index"),
	.tooltip  = noop("index page"),
	.stock    = GTK_STOCK_HOME,
	.callback = G_CALLBACK(vbi_button_home),
    },{
	.text     = noop("prev"),
	.tooltip  = noop("previous page"),
	.stock    = GTK_STOCK_GO_BACK,
	.callback = G_CALLBACK(vbi_button_prev),
    },{
	.text     = noop("next"),
	.tooltip  = noop("next page"),
	.stock    = GTK_STOCK_GO_FORWARD,
	.callback = G_CALLBACK(vbi_button_next),
    },{
	/* nothing */
    },{
	.text     = noop("close"),
	.tooltip  = noop("close window"),
	.stock    = GTK_STOCK_QUIT,
	.callback = G_CALLBACK(vbi_button_close),
    }
};

void vbi_create_window(struct vbi_state *vbi, struct dvbmon *dvbmon)
{
    GtkWidget *toplevel,*menubar,*toolbar;
    GtkWidget *vbox,*handlebox;
    GtkAccelGroup *accel_group;
    static GtkItemFactory *item_factory;
    struct vbi_window *vw;

    toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(toplevel),_("teletext"));
    vw = vbi_render_init(toplevel,vbi);
    vbi_event_handler_register(vw->vbi->dec,~0,vbi_newdata,vw);

    g_signal_connect(G_OBJECT(toplevel), "destroy",
		     G_CALLBACK(vbi_destroy), vw);
#if 0
    g_signal_connect(control_win, "delete-event",
		     G_CALLBACK(gtk_widget_hide_on_delete), NULL);
#endif

    /* build menu + toolbar */
    accel_group = gtk_accel_group_new ();
    item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<tt>",
					accel_group);
    gtk_item_factory_set_translate_func(item_factory,
					(GtkTranslateFunc)gettext,
					NULL,NULL);
    gtk_item_factory_create_items(item_factory, DIMOF(menu_items),
				  menu_items, vw);
    gtk_window_add_accel_group(GTK_WINDOW(toplevel), accel_group);
    menubar = gtk_item_factory_get_widget(item_factory, "<tt>");
    vw->subpages = gtk_item_factory_get_widget(item_factory,"<tt>/Subpage");
    vw->stations = gtk_item_factory_get_widget(item_factory,"<tt>/Station");
    vw->fonts    = gtk_item_factory_get_widget(item_factory,"<tt>/Font");
    toolbar = gtk_toolbar_build(toolbaritems, DIMOF(toolbaritems), vw);

    /* drawing area */
    vw->tt = gtk_drawing_area_new();
    gtk_widget_set_size_request(vw->tt, vw->w*41, vw->h*25);
    g_signal_connect(vw->tt, "expose-event",
		     G_CALLBACK(vbi_expose), vw);
    g_signal_connect(vw->tt, "button-press-event",
		     G_CALLBACK(vbi_mouse_button), vw);
    g_signal_connect(vw->tt, "button-release-event",
		     G_CALLBACK(vbi_mouse_button), vw);
    g_signal_connect(vw->tt, "motion-notify-event",
		     G_CALLBACK(vbi_mouse_motion), vw);
    g_signal_connect(vw->tt, "key-press-event",
		     G_CALLBACK(vbi_keypress), vw);
    gtk_widget_add_events(vw->tt,
			  GDK_EXPOSURE_MASK       |
			  GDK_BUTTON_PRESS_MASK   |
			  GDK_BUTTON_RELEASE_MASK | 
			  GDK_BUTTON_MOTION_MASK  |
			  GDK_KEY_PRESS_MASK);
    GTK_WIDGET_SET_FLAGS(vw->tt,   GTK_CAN_FOCUS);
    GTK_WIDGET_UNSET_FLAGS(vw->tt, GTK_DOUBLE_BUFFERED);

#if 0
    /* dnd */
    gtk_drag_dest_set(st_view,
		      // GTK_DEST_DEFAULT_ALL,
		      GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT,
		      dnd_targets, DIMOF(dnd_targets),
                      GDK_ACTION_COPY);
    g_signal_connect(st_view, "drag-data-received",
		     G_CALLBACK(drag_data_received), NULL);
    g_signal_connect(st_view, "drag-drop",
		     G_CALLBACK(drag_drop), NULL);
#endif
    /* cut+paste */
    gtk_selection_add_targets(vw->tt, gdk_atom_intern("PRIMARY", FALSE),
			      vbi_targets, DIMOF(vbi_targets));
    gtk_selection_add_targets(vw->tt, gdk_atom_intern("CLIPBOARD", FALSE),
			      vbi_targets, DIMOF(vbi_targets));
    g_signal_connect(vw->tt, "selection-get",
		     G_CALLBACK(selection_get), vw);
    

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    handlebox = gtk_handle_box_new();
    gtk_container_add(GTK_CONTAINER(handlebox), toolbar);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
    gtk_container_add(GTK_CONTAINER(toplevel), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), handlebox, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), vw->tt, TRUE, TRUE, 0);

    /* dynamic menus */
    vbi_font_menu(vw);
    vbi_create_subpage_menu(vw);
    vbi_update_subpage_menu(vw);

    /* station tuning */
#ifdef HAVE_DVB
    if (dvbmon) {
	vw->dvbmon = dvbmon;
	dvbmon_add_callback(dvbmon,dvbwatch_teletext,vw);
    }
#endif

    /* finalize */
    gtk_widget_grab_focus(vw->tt);
    gtk_widget_show_all(toplevel);
    vbi_setpage(vw,0x100,VBI_ANY_SUBNO);
    tt_windows++;
}
