#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "grab-ng.h"
#include "devs.h"
#include "capture.h"
#include "blit.h"
#include "commands.h"
#include "av-sync.h"

#define DRIFT_BIG ((int64_t)1<<60)
extern int          debug;

struct ng_video_filter *av_filter = NULL;

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
    if (0 == devs.sndplay.a->setformat(devs.sndplay.handle,&a->ofmt)) {
	a->datarate = a->ofmt.rate
	    * ng_afmt_to_channels[a->ofmt.fmtid]
	    * ng_afmt_to_bits[a->ofmt.fmtid] / 8;
	return a;
    }
    
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
    a->datarate = a->ofmt.rate
	* ng_afmt_to_channels[a->ofmt.fmtid]
	* ng_afmt_to_bits[a->ofmt.fmtid] / 8;
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
			  struct ng_video_conv *conv)
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
    list_add(&p->next,&v->processes);
    if (debug)
	fprintf(stderr,"video conv [%s] => [%s]\n",
		ng_vfmt_to_desc[p->ifmt.fmtid],
		ng_vfmt_to_desc[p->ofmt.fmtid]);
}

void av_video_add_filter(struct video_stream    *v,
			 struct ng_video_filter *filter,
			 int fmtid)
{
    struct video_process *p;

    p = malloc(sizeof(*p));
    memset(p,0,sizeof(*p));

    p->ifmt = *v->ifmt;
    p->ifmt.fmtid = fmtid;
    p->ifmt.bytesperline = 0; // hmm, fixme?
    p->ofmt = p->ifmt;

    p->handle = ng_filter_init(filter, &p->ifmt);
    if (p->handle) {
	list_add(&p->next,&v->processes);
    } else {
	av_filter = NULL;
	free(p);
    }
}

static void av_video_process_setup(struct video_stream *v,
				   ng_get_video_buf get,
				   void *ghandle)
{
    struct video_process *p = NULL;

    if (list_empty(&v->processes))
	return;
    p = list_entry(v->processes.next, struct video_process, next);
    ng_process_setup(p->handle, get, ghandle);
    while (p->next.next != &v->processes) {
	p = list_entry(p->next.next, struct video_process, next);
	ng_process_setup(p->handle, ng_malloc_video_buf, NULL);
    }
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
    int64_t drift,max,min;
    int i;

    if (NULL == mm->abuf)
	return;

    drift  = mm->abuf->info.ts - (ng_get_timestamp() - mm->start);
    drift += (int64_t)1000000000 * mm->abuf->written / mm->as->datarate;
    
    mm->drifts[mm->idrift] = drift;
    mm->idrift++;
    if (mm->idrift == sizeof(mm->drifts)/sizeof(mm->drifts[0]))
	mm->idrift = 0;

    min = DRIFT_BIG;
    max = -DRIFT_BIG;
    for (i = 0; i < sizeof(mm->drifts)/sizeof(mm->drifts[0]); i++) {
	if (min > mm->drifts[i])
	    min = mm->drifts[i];
	if (max < mm->drifts[i])
	    max = mm->drifts[i];
    }
    mm->drift = max;

    if (debug > 1)
	fprintf(stderr,"audio: %3d/%3d  |  %5d/%5d  |\n",
		(int)(min/1000000),
		(int)(max/1000000),
		mm->abuf->written, mm->abuf->size);
}

void av_sync_video(struct media_stream *mm)
{
    int64_t now = ng_get_timestamp() - mm->start;

    if (NULL == mm->vbuf) {
	if (mm->abuf) {
	    /* audio only, the sound card will take care ... */
	    mm->timeout = 3 * 1000;
	} else {
	    /* nothing left to do, instant return */
	    mm->timeout = 0;
	}
	return;
    }

    if (NULL == mm->abuf) {
	if (1 == mm->vbuf->info.play_seq) {
	    /* initial sync with real if not synced with audio */
	    mm->drift = mm->vbuf->info.ts;
	}
	if (mm->vbuf->info.slowdown) {
	    /* fine adjustments for buffering / latency */
	    mm->drift -= 50 * 1000000;
	    if (debug)
		fprintf(stderr,"noaudio slowdown\n");
	}
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
	mm->timeout = 0;
    } else {
	mm->drop = 0;
	mm->timeout = mm->delay / 1000000;
    }
    if (debug)
	fprintf(stderr,"drop %2d | sleep: %d.%03d sec | drift %d |\r",
		mm->drop, mm->timeout / 1000, mm->timeout % 1000,
		(int)(mm->drift/1000000));
}

/* ------------------------------------------------------------------------ */
/* work in progress ...                                                     */

static void av_media_process_audio(struct media_stream *mm)
{
    struct ng_audio_buf *buf;

    if (NULL != mm->abuf_next)
	return;
    buf = mm->reader->rd_adata(mm->rhandle);
    if (mm->writer && AUDIO_NONE != mm->wafmt.fmtid && buf)
	mm->writer->wr_audio(mm->whandle, buf);
    mm->abuf_next = av_audio_conv_buf(mm->as,buf);
}

static void av_media_process_video(struct media_stream *mm, int idle)
{
    struct ng_video_buf *vbuf;

    if (NULL != mm->vbuf_next)
	return;
    for (;;) {
	mm->vbuf_next = av_video_get_frame(mm->vs);
	if (NULL != mm->vbuf_next)
	    break;
	if (idle)
	    break;
	if (mm->reader) {
	    if (mm->writer && VIDEO_NONE != mm->wvfmt.fmtid) {
		/* don't drop frames when recording ... */
		int nodrop = 0;
		vbuf = mm->reader->rd_vdata(mm->rhandle,&nodrop);
		if (vbuf)
		    mm->writer->wr_video(mm->whandle, vbuf);
	    } else {
		vbuf = mm->reader->rd_vdata(mm->rhandle,&mm->drop);
	    }
	    if (mm->drop)
		mm->blitframe = 0;
	} else {
	    vbuf = ng_grabber_grab_image(&devs.video,0);
	    if (mm->writer && VIDEO_NONE != mm->wvfmt.fmtid && vbuf)
		mm->writer->wr_video(mm->whandle, vbuf);
	}
	if (NULL == vbuf)
	    break;
	av_video_put_frame(mm->vs,vbuf);
    }
}

void av_media_setup_audio_reader(struct media_stream *mm,
				 struct ng_audio_fmt *afmt)
{
    mm->as = av_audio_init(afmt);
    if (NULL == mm->as) {
	fprintf(stderr,"can't play audio stream\n");
 	return;
    }

    mm->latency = devs.sndplay.a->latency(devs.sndplay.handle);
}

void av_media_setup_video_reader(struct media_stream *mm)
{
    unsigned int fmtids[2*VIDEO_FMT_COUNT],i;
    struct ng_video_fmt *vfmt;
    struct ng_video_conv *vconv1,*vconv2;
    int ofmtid = 0;

    blit_get_formats(mm->blit,fmtids,DIMOF(fmtids));
    vfmt = mm->reader->rd_vfmt(mm->rhandle,fmtids,sizeof(fmtids)/sizeof(int));
    if (0 == vfmt->width || 0 == vfmt->height || VIDEO_NONE == vfmt->fmtid)
	return;
    
    mm->vs = av_video_init(vfmt);

    /* try direct */
    for (i = 0; i < DIMOF(fmtids); i++)
	if (fmtids[i] == vfmt->fmtid) {
	    ofmtid = fmtids[i];
	    goto done;
	}

    /* try to convert once */
    for (i = 0; i < sizeof(fmtids)/sizeof(int); i++)
	if (NULL != (vconv1 = ng_conv_find_match(vfmt->fmtid,fmtids[i]))) {
	    ofmtid = fmtids[i];
	    break;
	}
    if (vconv1) {
	av_video_add_convert(mm->vs,vconv1);
	goto done;
    }

    /* try to convert twice via VIDEO_YUV420P */
    if (NULL != (vconv1 = ng_conv_find_match(vfmt->fmtid,VIDEO_YUV420P)))
	for (i = 0; i < sizeof(fmtids)/sizeof(int); i++)
	    if (NULL != (vconv2 = ng_conv_find_match(VIDEO_YUV420P,fmtids[i]))) {
		ofmtid = fmtids[i];
		break;
	    }
    if (vconv1 && vconv2) {
	av_video_add_convert(mm->vs,vconv1);
	av_video_add_convert(mm->vs,vconv2);
	goto done;
    }

    /* failed */
    fprintf(stderr,"can't play video stream\n");
    av_video_fini(mm->vs);
    mm->vs = NULL;
    return;

 done:
    if (av_filter)
	av_video_add_filter(mm->vs, av_filter, ofmtid);
    av_video_process_setup(mm->vs, blit_get_buf, mm->blit);
    mm->frame = mm->reader->frame_time(mm->rhandle);
    mm->vs->blit = mm->blit;
}

void av_media_setup_video_grab(struct media_stream *mm,
			       GtkWidget *widget)
{
    unsigned int fmtids[2*VIDEO_FMT_COUNT],i;
    gint x,y,width,height,depth;

    gdk_window_get_geometry(widget->window, &x, &y, &width, &height, &depth);
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
    av_video_process_setup(mm->vs, blit_get_buf, mm->blit);
    mm->frame = (int64_t)1000000000 / 25;  // FIXME
    mm->vs->blit = mm->blit;
}

int av_media_start_recording(struct media_stream *mm,
			     struct ng_writer *wr,
			     char *filename)
{
    int video = -1, audio = -1, fps = 0;

    /* audio */
    if (mm->as && mm->as->ifmt) {
	for (audio = 0; wr->audio && wr->audio[audio].name != NULL; audio++)
	    if (wr->audio[audio].fmtid == mm->as->ifmt->fmtid)
		break;
	if (NULL == wr->audio[audio].name) {
	    /* todo: try to convert */
	    fprintf(stderr,"can't record audio (%s in %s)\n",
		    ng_afmt_to_desc[mm->as->ifmt->fmtid], wr->desc);
	} else {
	    mm->wafmt = *(mm->as->ifmt);
	}
    }

    /* video */
    if (mm->vs && mm->vs->ifmt) {
	for (video = 0; wr->video && wr->video[video].name != NULL; video++)
	    if (wr->video[video].fmtid == mm->vs->ifmt->fmtid)
		break;
	if (NULL == wr->video[video].name) {
	    /* todo: try to convert */
	    fprintf(stderr,"can't record video (%s in %s)\n",
		    ng_vfmt_to_desc[mm->vs->ifmt->fmtid], wr->desc);
	} else {
	    mm->wvfmt = *(mm->vs->ifmt);
	}
	fps = 1000 / mm->frame;
    }

    if (mm->wafmt.fmtid == AUDIO_NONE  &&
	mm->wvfmt.fmtid == VIDEO_NONE)
	return -1;

    mm->whandle = wr->wr_open
	(filename, NULL,
	 &mm->wvfmt, (-1 == video) ? NULL : wr->video[video].priv, fps,
	 &mm->wafmt, (-1 == audio) ? NULL : wr->audio[audio].priv);
    if (NULL == mm->whandle) {
	fprintf(stderr,"init movie writer failed\n");
	return -1;
    }

    mm->writer = wr;
    return 0;
}

void av_media_stop_recording(struct media_stream *mm)
{
    mm->writer->wr_close(mm->whandle);
    mm->writer  = NULL;
    mm->whandle = NULL;
}

static int audio_fill(struct media_stream *mm)
{
    struct ng_audio_buf *buf;

    devs.sndplay.a->startplay(devs.sndplay.handle);
    for (;;) {
	if (!av_audio_writeable(mm->as))
	    break;
	buf = mm->reader->rd_adata(mm->rhandle);
	if (mm->writer && AUDIO_NONE != mm->wafmt.fmtid && buf)
	    mm->writer->wr_audio(mm->whandle, buf);
	mm->abuf = av_audio_conv_buf(mm->as, buf);
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

void av_media_mainloop(GMainContext *context, struct media_stream *mm)
{
    GPollFunc poll = g_main_context_get_poll_func(context);
    gint priority,timeout,ngtk,npoll,rc;
    GPollFD fds[32];

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

	/* check for & handle pending events */
	while (g_main_context_iteration(context,FALSE))
	    /* nothing */;

	if (command_pending) {
	    /* don't fetch new buffers */
	    if (mm->as && NULL == mm->abuf) {
#if 0
		if (mm->abuf_next) {
		    ng_free_audio_buf(mm->abuf_next);
		    mm->abuf_next = NULL;
		}
#endif
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
	    av_media_process_audio(mm);
	    mm->abuf = mm->abuf_next;
	    mm->abuf_next = NULL;
	    av_sync_audio(mm);
	    if (NULL == mm->abuf) {
		if (debug)
		    fprintf(stderr,"media: audio: end-of-stream [%p,%p]\n",
			    mm->abuf,mm->abuf_next);
		av_audio_fini(mm->as);
		mm->as = NULL;
	    }
	}
	if (mm->vs && NULL == mm->vbuf) {
	    mm->blitframe = 1;
	    av_media_process_video(mm,0);
	    mm->vbuf = mm->vbuf_next;
	    mm->vbuf_next = NULL;
	    if (NULL == mm->vbuf) {
		if (debug)
		    fprintf(stderr,"media: video: end-of-stream [%p,%p]\n",
			    mm->abuf,mm->abuf_next);
		av_video_fini(mm->vs);
		mm->vs = NULL;
	    }
	}

	/* gtk stuff ... */
	g_main_context_prepare(context, &priority);
	ngtk = g_main_context_query(context, priority, &timeout, fds,
				   sizeof(fds)/sizeof(fds[0]));
	npoll = ngtk;

	/* add audio out */
	if (mm->as) {
	    npoll++;
	    fds[ngtk].fd      = mm->as->wrfd;
	    fds[ngtk].events  = G_IO_OUT | G_IO_ERR;
	    fds[ngtk].revents = 0;
	}

	/* video timeout */
	av_sync_video(mm);
	if (mm->timeout > 10 &&
	    NULL != mm->as && NULL == mm->abuf_next &&
	    NULL != mm->vs && NULL == mm->vbuf_next) {
	    if (NULL != mm->as && NULL == mm->abuf_next)
		av_media_process_audio(mm);
	    else
		av_media_process_video(mm,1);
	    av_sync_video(mm);
	}

	/* poll */
	if (timeout < 0 || timeout > mm->timeout)
	    timeout = mm->timeout;
	rc = poll(fds,npoll,timeout);

	if (mm->as && 0 != fds[ngtk].revents) {
	    /* write audio data */
	    mm->abuf = devs.sndplay.a->write(devs.sndplay.handle,mm->abuf);
	    av_sync_audio(mm);
	}

	if (mm->vs && 0 == rc) {
	    /* blit video frame */
	    if (mm->blitframe)
		blit_draw_buf(mm->vbuf);
	    ng_release_video_buf(mm->vbuf);
	    mm->vbuf = NULL;
	    mm->frames++;
	}

	/* dispatch glib events */
	g_main_context_check(context, priority, fds, ngtk);
	g_main_context_dispatch(context);
    }
}
