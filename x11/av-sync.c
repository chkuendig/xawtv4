#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>
#include <X11/extensions/XShm.h>

#include "grab-ng.h"
#include "devs.h"
#include "capture.h"
#include "blit.h"
#include "commands.h"
#include "av-sync.h"

extern int          debug;
extern XtAppContext app_context;
extern Display      *dpy;

/* ------------------------------------------------------------------------ */

void av_audio_fini(struct audio_stream *a)
{
    if (a->chandle)
	a->conv->fini(a->chandle);
    ng_dev_close(&devs.sndplay);
    free(a);
}

struct audio_stream* av_audio_init(struct ng_audio_fmt *ifmt)
{
    struct audio_stream *a;
    struct list_head *item;
    int rc;

    if (NG_DEV_DSP != devs.sndplay.type)
	return NULL;
    if (0 != ng_dev_open(&devs.sndplay))
	return NULL;

    a = malloc(sizeof(*a));
    if (NULL == a) {
	ng_dev_close(&devs.sndplay);
	return NULL;
    }
    memset(a,0,sizeof(*a));
    a->ifmt = ifmt;
    a->ofmt = *ifmt;
    a->wrfd = devs.sndplay.a->fd(devs.sndplay.handle);

    /* try direct ... */
    if (0 == devs.sndplay.a->setformat(devs.sndplay.handle,&a->ofmt))
	return a;
    
    /* try find a converter */
    rc = -1;
    list_for_each(item,&ng_aconv) {
	a->conv = list_entry(item, struct ng_audio_conv, list);
	if (a->conv->fmtid_in == a->ifmt->fmtid) {
	    a->ofmt = *a->ifmt;
	    a->ofmt.fmtid = a->conv->fmtid_out;
	    rc = devs.sndplay.a->setformat(devs.sndplay.handle,&a->ofmt);
	    if (0 == rc)
		break;
	}
    }
    if (0 != rc) {
	av_audio_fini(a);
	return NULL;
    }

    /* init converter */
    a->chandle = a->conv->init(a->conv->priv);
    if (debug)
	fprintf(stderr,"audio: conv [%s] => [%s]\n",
		ng_afmt_to_desc[a->ifmt->fmtid],
		ng_afmt_to_desc[a->ofmt.fmtid]);
    return a;
}

int av_audio_writeable(struct audio_stream *a)
{
    struct timeval wait;
    fd_set wr;
    int rc;

    FD_ZERO(&wr);
    FD_SET(a->wrfd,&wr);
    wait.tv_sec = 0;
    wait.tv_usec = 0;
    rc = select(a->wrfd+1,NULL,&wr,NULL,&wait);
    return (1 == rc);
}

struct ng_audio_buf* av_audio_conv_buf(struct audio_stream *a,
				       struct ng_audio_buf *buf)
{
    if (buf && a->conv)
	buf = a->conv->data(a->chandle,buf);
    return buf;
}

/* ------------------------------------------------------------------------ */

struct video_process {
    struct ng_video_fmt         ifmt;
    struct ng_video_fmt         ofmt;
    struct ng_video_conv        *conv;
    struct ng_process_handle    *handle;

    struct list_head            next;
};

void av_video_fini(struct video_stream *v)
{
    struct video_process *p;

    while (!list_empty(&v->processes)) {
	p = list_entry(v->processes.next, struct video_process, next);
	ng_process_fini(p->handle);
	list_del(&p->next);
	free(p);
    }
}

struct video_stream* av_video_init(struct ng_video_fmt *ifmt)
{
    struct video_stream *v;

    v = malloc(sizeof(*v));
    if (NULL == v)
	goto oops;
    memset(v,0,sizeof(*v));

    INIT_LIST_HEAD(&v->processes);
    v->ifmt = ifmt;
    return v;
    
 oops:
    if (v)
	av_video_fini(v);
    return NULL;
}

void av_video_add_convert(struct video_stream  *v,
			  struct ng_video_conv *conv,
			  ng_get_video_buf get_obuf,
			  void *ohandle)
{
    struct video_process *p;

    p = malloc(sizeof(*p));
    memset(p,0,sizeof(*p));

    p->ifmt = *v->ifmt;
    p->ofmt = *v->ifmt;
    p->ifmt.fmtid = conv->fmtid_in;
    p->ofmt.fmtid = conv->fmtid_out;
    p->ifmt.bytesperline = 0; // hmm, fixme?
    p->ofmt.bytesperline = 0; // hmm, fixme?

    p->handle = ng_conv_init(conv,&p->ifmt,&p->ofmt);
    ng_process_setup(p->handle,get_obuf,ohandle);
    //list_add_tail(&p->next,&v->processes);
    list_add(&p->next,&v->processes);
    if (debug)
	fprintf(stderr,"video conv [%s] => [%s]\n",
		ng_vfmt_to_desc[p->ifmt.fmtid],
		ng_vfmt_to_desc[p->ofmt.fmtid]);
}

void av_video_put_frame(struct video_stream  *v,
			struct ng_video_buf *buf)
{
    BUG_ON(NULL != v->ibuf, "have v->ibuf");
    v->ibuf = buf;
}

static struct ng_video_buf *av_video_convert_frame(struct video_stream *v,
						   struct video_process *p)
{
    struct video_process *pnext = NULL;
    struct ng_video_buf *buf;
    
    buf = ng_process_get_frame(p->handle);
    if (NULL != buf) {
	return buf;
    }

    if (p->next.next == &v->processes) {
	buf = v->ibuf;
	v->ibuf = NULL;
    } else {
	pnext = list_entry(p->next.next, struct video_process, next);
	buf = av_video_convert_frame(v,pnext);
    }

    if (NULL == buf) {
	return NULL;
    }
    ng_process_put_frame(p->handle,buf);
    buf = ng_process_get_frame(p->handle);
    return buf;
}

struct ng_video_buf *av_video_get_frame(struct video_stream *v)
{
    struct video_process *p = NULL;
    struct ng_video_buf *buf;

    if (list_empty(&v->processes)) {
	/* just copy */
	if (NULL == v->ibuf)
	    return NULL;
	buf = blit_get_buf(v->blit,v->ifmt);
	if (NULL == buf) {
	    fprintf(stderr,"Huh? blit_get_buf() failed\n");
	    ng_release_video_buf(v->ibuf);
	    v->ibuf = NULL;
	    return NULL;
	}
	ng_copy_video_buf(buf,v->ibuf);
	ng_release_video_buf(v->ibuf);
	v->ibuf = NULL;
	return buf;
    }

    p = list_entry(v->processes.next, struct video_process, next);
    buf = av_video_convert_frame(v,p);
    return buf;
}

/* ------------------------------------------------------------------------ */

void av_sync_audio(struct media_stream *mm)
{
    if (NULL != mm->abuf)
	mm->drift = mm->abuf->info.ts - (ng_get_timestamp() - mm->start);
}

void av_sync_video(struct media_stream *mm)
{
    int64_t now = ng_get_timestamp() - mm->start;

    if (NULL == mm->vbuf) {
	if (mm->abuf) {
	    /* audio only, the sound card will take care ... */
	    mm->dtv.tv_sec  = 3;
	    mm->dtv.tv_usec = 0;
	} else {
	    /* nothing left to do, instant return */
	    mm->dtv.tv_sec  = 0;
	    mm->dtv.tv_usec = 0;
	}
	return;
    }

    switch (mm->speed) {
    case 0:
	/* as fast as possible */
	mm->delay = 0;
	break;
    case 1:
	/* normal speed */
	if (mm->drift) {
	    /* sync with audio */
	    now += mm->drift;
	    now -= mm->latency;
	}
	mm->delay = mm->vbuf->info.ts - now;
	break;
    default:
	/* slow down */
	mm->delay = mm->vbuf->info.ts * mm->speed - now;
	break;
    }

    if (mm->delay <= 0) {
	mm->drop = -mm->delay / mm->frame;
	mm->dtv.tv_sec  = 0;
	mm->dtv.tv_usec = 0;
    } else {
	mm->drop = 0;
	mm->dtv.tv_sec  = mm->delay / 1000000000;
	mm->dtv.tv_usec = (mm->delay / 1000) % 1000000;
    }
    if (debug)
	fprintf(stderr,"drop %2d | sleep: %d.%03d sec\r",
		mm->drop,(int)mm->dtv.tv_sec,(int)mm->dtv.tv_usec/1000);
}

/* ------------------------------------------------------------------------ */
/* work in progress ...                                                     */

void av_media_reader_audio(struct media_stream *mm,
			   struct ng_audio_fmt *afmt)
{
    mm->as = av_audio_init(afmt);
    if (NULL == mm->as) {
	fprintf(stderr,"can't play audio stream\n");
 	return;
    }

    mm->latency = devs.sndplay.a->latency(devs.sndplay.handle);
}

void av_media_reader_video(struct media_stream *mm)
{
    unsigned int fmtids[2*VIDEO_FMT_COUNT],i;
    struct ng_video_fmt *vfmt;
    struct ng_video_conv *vconv1,*vconv2;

    blit_get_formats(mm->blit,fmtids,DIMOF(fmtids));
    vfmt = mm->reader->rd_vfmt(mm->rhandle,fmtids,sizeof(fmtids)/sizeof(int));
    if (0 == vfmt->width || 0 == vfmt->height || VIDEO_NONE == vfmt->fmtid)
	return;
    
    mm->vs = av_video_init(vfmt);

    /* try direct */
    for (i = 0; i < DIMOF(fmtids); i++)
	if (fmtids[i] == vfmt->fmtid)
	    goto done;

    /* try to convert once */
    for (i = 0; i < sizeof(fmtids)/sizeof(int); i++)
	if (NULL != (vconv1 = ng_conv_find_match(vfmt->fmtid,fmtids[i])))
	    break;
    if (vconv1) {
	av_video_add_convert(mm->vs,vconv1,blit_get_buf,mm->blit);
	goto done;
    }

    /* try to convert twice via VIDEO_YUV420P */
    if (NULL != (vconv1 = ng_conv_find_match(vfmt->fmtid,VIDEO_YUV420P)))
	for (i = 0; i < sizeof(fmtids)/sizeof(int); i++)
	    if (NULL != (vconv2 = ng_conv_find_match(VIDEO_YUV420P,fmtids[i])))
		break;
    if (vconv1 && vconv2) {
	av_video_add_convert(mm->vs,vconv1,ng_malloc_video_buf,NULL);
	av_video_add_convert(mm->vs,vconv2,blit_get_buf,mm->blit);
	goto done;
    }

    /* failed */
    fprintf(stderr,"can't play video stream\n");
    av_video_fini(mm->vs);
    mm->vs = NULL;
    return;

 done:
    mm->frame = mm->reader->frame_time(mm->rhandle);
    mm->vs->blit = mm->blit;
}

void av_media_grab_video(struct media_stream *mm,
			 Widget widget)
{
    unsigned int fmtids[2*VIDEO_FMT_COUNT],i;
    Dimension width,height;
    
    XtVaGetValues(widget,XtNwidth,&width,XtNheight,&height,NULL);
    blit_get_formats(mm->blit,fmtids,DIMOF(fmtids));

    for (i = 0; i < DIMOF(fmtids); i++) {
	memset(&mm->capfmt,0,sizeof(mm->capfmt));
	mm->capfmt.fmtid  = fmtids[i];
	mm->capfmt.width  = width;
	mm->capfmt.height = height;
	if (0 == ng_grabber_setformat(&devs.video,&mm->capfmt,1))
	    break;
    }

    if (i == DIMOF(fmtids)) {
	/* FIXME: convert stuff */
	fprintf(stderr,"can't grabdisplay\n");
	return;
    }
    
    mm->vs = av_video_init(&mm->capfmt);
    mm->frame = (int64_t)1000000000 / 25;  // FIXME
    mm->vs->blit = mm->blit;
}

static int audio_fill(struct media_stream *mm)
{
    devs.sndplay.a->startplay(devs.sndplay.handle);
    for (;;) {
	if (!av_audio_writeable(mm->as))
	    break;
	mm->abuf = av_audio_conv_buf(mm->as,mm->reader->rd_adata(mm->rhandle));
	if (NULL == mm->abuf)
	    break;
	av_sync_audio(mm);
	mm->abuf = devs.sndplay.a->write(devs.sndplay.handle,mm->abuf);
	if (NULL != mm->abuf)
	    break;
    }
    if (debug)
	fprintf(stderr,"media: audio buffer filled\n");
    return 0;
}

void av_media_mainloop(struct media_stream *mm)
{
    int rc,max;
    fd_set rd,wr;
    
    /*
     * enter main loop
     * 
     * can't use XtAppMainLoop + Input + Timeout handlers here because
     * that doesn't give us usable event scheduling, thus we have our
     * own main loop here ...
     */
    mm->start = ng_get_timestamp();
    if (mm->as)
	audio_fill(mm);
    for (; mm->as || mm->vs;) {
	XtInputMask x11mask;

	/* handle X11 events */
	while (0 != (x11mask = XtAppPending(app_context)))
	    XtAppProcessEvent(app_context,x11mask);
	
	if (command_pending) {
	    /* don't fetch new buffers */
	    if (mm->as && NULL == mm->abuf) {
		av_audio_fini(mm->as);
		mm->as = NULL;
	    }
	    if (mm->vs && NULL == mm->vbuf) {
		av_video_fini(mm->vs);
		mm->vs = NULL;
	    }
	}

	/* read media data */
	if (mm->as && NULL == mm->abuf) {
	    mm->abuf = av_audio_conv_buf(mm->as,mm->reader->rd_adata(mm->rhandle));
	    if (NULL == mm->abuf) {
		if (debug)
		    fprintf(stderr,"media: audio: end-of-stream\n");
		av_audio_fini(mm->as);
		mm->as = NULL;
	    } else {
		av_sync_audio(mm);
	    }
	}
	if (mm->vs && NULL == mm->vbuf) {
	    mm->blitframe = 1;
	    for (;;) {
		mm->vbuf = av_video_get_frame(mm->vs);
		if (NULL != mm->vbuf)
		    break;
		if (mm->reader) {
		    mm->vbuf = mm->reader->rd_vdata(mm->rhandle,&mm->drop);
		    if (mm->drop)
			mm->blitframe = 0;
		} else {
		    mm->vbuf = ng_grabber_grab_image(&devs.video,0);
		}
		if (NULL == mm->vbuf)
		    break;
		av_video_put_frame(mm->vs,mm->vbuf);
	    }
	    if (NULL == mm->vbuf) {
		if (debug)
		    fprintf(stderr,"media: video: end-of-stream\n");
		av_video_fini(mm->vs);
		mm->vs = NULL;
	    }
	}
	
	/* wait for events */
	max = 0;
	FD_ZERO(&rd);
	FD_ZERO(&wr);
	FD_SET(ConnectionNumber(dpy),&rd);
	max = ConnectionNumber(dpy);
	if (mm->as) {
	    FD_SET(mm->as->wrfd,&wr);
	    if (max < mm->as->wrfd)
		max = mm->as->wrfd;
	}
	av_sync_video(mm);
	rc = select(max+1,&rd,&wr,NULL,&mm->dtv);
	
	if (mm->as && FD_ISSET(mm->as->wrfd,&wr)) {
	    /* write audio data */
	    mm->abuf = devs.sndplay.a->write(devs.sndplay.handle,mm->abuf);
	}
	if (mm->vs && 0 == rc) {
	    /* blit video frame */
	    if (mm->blitframe)
		blit_draw_buf(mm->vbuf);
	    ng_release_video_buf(mm->vbuf);
	    mm->vbuf = NULL;
	    mm->frames++;
	}
    }
}
