#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <fcntl.h>
#include <iconv.h>
#include <assert.h>
#include <pthread.h>

#include "vbi-data.h"
#include "vbi-sim.c"
#include "vbi-dvb.h"

char *vbi_colors[8] = { "black", "red", "green", "yellow",
			"blue", "magenta", "cyan", "white" };
struct vbi_rect vbi_fullrect = {
	x1:  0,  y1:  0,  x2: 41,  y2: 25,
};

/*---------------------------------------------------------------------*/

struct vbi_state*
vbi_open(char *dev, char *appname, int debug, int sim, int timeout)
{
    struct vbi_state *vbi;
    int services = VBI_SLICED_VBI_525 | VBI_SLICED_VBI_625
	| VBI_SLICED_TELETEXT_B | VBI_SLICED_CAPTION_525
	| VBI_SLICED_CAPTION_625 | VBI_SLICED_VPS
	| VBI_SLICED_WSS_625 | VBI_SLICED_WSS_CPR1204;
    int p[2];

    /* init vbi */
    vbi = malloc(sizeof(*vbi));
    if (NULL == vbi)
	goto oops;
    memset(vbi,0,sizeof(*vbi));
    vbi->debug = debug;
    vbi->sim   = sim;
    vbi->dec   = vbi_decoder_new();
    if (NULL == vbi->dec)
	goto oops;

    if (vbi->sim) {
	vbi->par = init_sim(625,services);
        /* simulation for select */
	pipe(p);
	switch (fork()) {
	case -1:
	    perror("fork");
	    exit(1);
	case 0:
	    close(p[0]);
	    for (;;) {
		if (1 != write(p[1],"x",1))
		    exit(0);
		usleep(100*1000);
	    }
	default:
	    vbi->fd = p[0];
	    close(p[1]);
	};
    } else {
	if (NULL == vbi->cap && 0 == strncmp(dev,"/dev/dvb/",9)) {
	    vbi->cap = vbi_capture_dvb_new(dev,16,&services,-1,&vbi->err,debug);
	    if (NULL != vbi->cap)
		vbi->dvb = 1;
	}
#ifdef HAVE_VBI_PROXY
	if (NULL == vbi->cap) {
	    vbi->proxy = vbi_proxy_client_create(dev, appname, 0, &vbi->err, debug);
	    if (vbi->proxy)
		vbi->cap = vbi_capture_proxy_new(vbi->proxy,16,0,&services,-1,
						 &vbi->err);
	}
#endif
	if (NULL == vbi->cap)
	    vbi->cap = vbi_capture_v4l2_new(dev,16,&services,-1,&vbi->err,debug);
	if (NULL == vbi->cap)
	    vbi->cap = vbi_capture_v4l_new(dev,16,&services,-1,&vbi->err,debug);
	if (NULL == vbi->cap)
	    vbi->cap = vbi_capture_bktr_new(dev,16,&services,-1,&vbi->err,debug);
	if (NULL == vbi->cap)
	    goto oops;
	vbi->par = vbi_capture_parameters(vbi->cap);
	vbi->fd  = vbi_capture_fd(vbi->cap);
    }
    vbi->lines = (vbi->par->count[0] + vbi->par->count[1]);
    vbi->raw = malloc(vbi->lines * vbi->par->bytes_per_line);
    if (NULL == vbi->raw)
	goto oops;
    vbi->sliced = malloc(vbi->lines * sizeof(vbi_sliced));
    if (NULL == vbi->sliced)
	goto oops;
    memset(vbi->sliced,0,vbi->lines * sizeof(vbi_sliced));
    vbi->tv.tv_sec  = timeout;
    vbi->tv.tv_usec = 0;
    return vbi;

 oops:
    if (vbi)
	vbi_close(vbi);
    fprintf(stderr,"vbi: open failed [%s]\n",dev);
    return NULL;
}

int
vbi_hasdata(struct vbi_state *vbi)
{
    char buf[1];
    int rc;

    if (vbi->sim) {
	read(vbi->fd,buf,1);
	read_sim(vbi->raw, vbi->sliced, &vbi->lines, &vbi->ts);
	rc = 1;
    } else {
	rc = vbi_capture_read(vbi->cap, vbi->raw, vbi->sliced,
			      &vbi->lines, &vbi->ts, &vbi->tv);
    }
    if (rc)
	vbi_decode(vbi->dec, vbi->sliced, vbi->lines, vbi->ts);
    return rc;
}

void
vbi_close(struct vbi_state *vbi)
{
    if (!vbi)
	return;

    if (vbi->sliced)
	free(vbi->sliced);
    if (vbi->raw)
	free(vbi->raw);
    if (vbi->cap)
	vbi_capture_delete(vbi->cap);
    if (vbi->dec)
	vbi_decoder_delete(vbi->dec);
#ifdef HAVE_VBI_PROXY
    if (vbi->proxy)
	vbi_proxy_client_destroy(vbi->proxy);
#endif
    free(vbi);
}

void
vbi_set_pid(struct vbi_state *vbi, int pid)
{
    if (!vbi->dvb)
	return;
    vbi->pid = pid;
    vbi_capture_dvb_filter(vbi->cap,pid);
}

void
vbi_dump_event(struct vbi_event *ev, void *user)
{
    switch (ev->type) {
    case VBI_EVENT_TTX_PAGE:
	fprintf(stderr,"vbi ev: ttx page %03x.%02x \r",
		ev->ev.ttx_page.pgno,
		ev->ev.ttx_page.subno);
	break;
    case VBI_EVENT_CLOSE:
	fprintf(stderr,"vbi ev: close \n");
	break;
    case VBI_EVENT_CAPTION:
	fprintf(stderr,"vbi ev: caption \n");
	break;
    case VBI_EVENT_NETWORK:
	fprintf(stderr,"vbi ev: network id=%d name=\"%s\" call=\"%s\"\n",
		ev->ev.network.nuid,
		ev->ev.network.name,
		ev->ev.network.call);
	break;
    case VBI_EVENT_TRIGGER:
	switch (ev->ev.trigger->type) {
	case VBI_LINK_NONE:
	    fprintf(stderr,"vbi ev: trigger none \n");
	    break;
	case VBI_LINK_MESSAGE:
	    fprintf(stderr,"vbi ev: trigger message \n");
	    break;
	case VBI_LINK_PAGE:
	    fprintf(stderr,"vbi ev: trigger page [%03x.%02x]\n",
		    ev->ev.trigger->pgno,
		    ev->ev.trigger->subno);
	    break;
	case VBI_LINK_SUBPAGE:
	    fprintf(stderr,"vbi ev: trigger subpage \n");
	    break;
	case VBI_LINK_HTTP:
	    fprintf(stderr,"vbi ev: trigger http [%s]\n",
		    ev->ev.trigger->url);
	    break;
	case VBI_LINK_FTP:
	    fprintf(stderr,"vbi ev: trigger ftp \n");
	    break;
	case VBI_LINK_EMAIL:
	    fprintf(stderr,"vbi ev: trigger email \n");
	    break;
	case VBI_LINK_LID:
	    fprintf(stderr,"vbi ev: trigger lid \n");
	    break;
	case VBI_LINK_TELEWEB:
	    fprintf(stderr,"vbi ev: trigger teleweb \n");
	    break;
	}
	break;
    case VBI_EVENT_ASPECT:
	fprintf(stderr,"vbi ev: aspect \n");
	break;
    case VBI_EVENT_PROG_INFO:
	fprintf(stderr,"vbi ev: prog info \n");
	break;
    default:
	fprintf(stderr,"vbi ev: UNKNOWN[0x%x] \n",ev->type);
	break;
    }
}

int vbi_calc_page(int pagenr, int offset)
{
    int result;
    
    result = pagenr + offset;
    if (offset < 0) {
	while ((result & 0x0f) > 0x09)
	    result -= 0x01;
	while ((result & 0xf0) > 0x90)
	    result -= 0x10;
	if (result < 0x100)
	    result = 0x100;
    }
    if (offset > 0) {
	while ((result & 0x0f) > 0x09)
	    result += 0x01;
	while ((result & 0xf0) > 0x90)
	    result += 0x10;
	if (result > 0x899)
	    result = 0x899;
    }
    return result;
}

int vbi_calc_subpage(vbi_decoder *dec, int pgno, int subno, int offset)
{
    vbi_page pg;
    int newno;
    
    for (newno = subno+offset; newno != subno;) {
	if (vbi_fetch_vt_page(dec,&pg,pgno,newno,
			      VBI_WST_LEVEL_1,0,0))
	    break;
	if (offset < 0) {
	    newno--;
	    if (newno < 0)
		newno += VBI_MAX_SUBPAGES;
	    while ((newno & 0x0f) > 0x09)
		newno -= 0x01;
	}
	if (offset > 0) {
	    newno++;
	    while ((newno & 0x0f) > 0x09)
		newno += 0x01;
	    if (newno >= VBI_MAX_SUBPAGES)
		newno = 0;
	}
    }
    return newno;
}

int vbi_export_txt(char *dest, char *charset, int size,
		   vbi_page *pg, struct vbi_rect *rect,
		   enum vbi_txt_colors color)
{
    int x,y,rc;
    size_t olen,ilen;
    int fg,bg,len=0;
    char *ibuf, *obuf;
    vbi_char *ch;
    wchar_t wch;
    iconv_t ic;

    ic = iconv_open(charset,"WCHAR_T");
    if (NULL == ic)
	return -1;

    obuf = dest;
    olen = size;
    for (y = rect->y1; y < rect->y2; y++) {
	ch = pg->text + 41*y;
	fg = -1;
	bg = -1;
	for (x = rect->x1; x <= rect->x2; x++) {
	    if (x < rect->x2) {
		wch = ch[x].unicode;
		if (ch[x].size > VBI_DOUBLE_SIZE)
		    wch = ' ';
		if (ch[x].conceal)
		    wch = ' ';
	    } else {
		wch = '\n';
	    }
	    if (fg != ch[x].foreground || bg != ch[x].background) {
		fg  = ch[x].foreground;
		bg  = ch[x].background;
		switch (color) {
		case VBI_ANSICOLOR:
		    len = sprintf(obuf,"\033[%d;%dm",
				  30 + (fg & 7), 40 + (bg & 7));
		    break;
		case VBI_NOCOLOR:
		    len = 0;
		    break;
		}
		olen -= len;
		obuf += len;
	    }
	    ibuf = (char*)(&wch);
	    ilen = sizeof(wch);
	retry:
	    rc = iconv(ic,&ibuf,&ilen,&obuf,&olen);
	    if (-1 == rc && EILSEQ == errno && wch != '?') {
		if (vbi_is_gfx(wch))
		    wch = '#';
		else
		    wch = '?';
		goto retry;
	    }
	    if (-1 == rc)
		goto done;
	}
	switch (color) {
	case VBI_ANSICOLOR:
	    len  = sprintf(obuf,"\033[0m");
	    break;
	case VBI_NOCOLOR:
	    len = 0;
	    break;
	}
	olen -= len;
	obuf += len;
    }

 done:
    return obuf - dest;
}

void vbi_find_subtitle(vbi_page *pg, struct vbi_rect *rect)
{
    int x,y,showline;
    vbi_char *ch;
    
    *rect = vbi_fullrect;

    for (y = 1; y < 25; y++) {
	showline = 0;
	ch = pg->text + 41*y;
	for (x = 0; x <= 40; x++)
	    if (ch[x].unicode != ' ')
		showline = 1;
	if (showline)
	    break;
    }
    rect->y1 = y;
    
    for (y = 25; y >= rect->y1; y--) {
	showline = 0;
	ch = pg->text + 41*y;
	for (x = 0; x <= 40; x++)
	    if (ch[x].unicode != ' ')
		showline = 1;
	if (showline)
	    break;
    }
    rect->y2 = y+1;
}
