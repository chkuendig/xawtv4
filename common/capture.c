#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "grab-ng.h"
#include "commands.h"       /* FIXME: *drv globals */
#include "sound.h"
#include "capture.h"
#include "webcam.h"
#include "fifo.h"

#define REORDER_SIZE  32

/*-------------------------------------------------------------------------*/
/* data fifos (audio/video)                                                */

static void*
flushit(void *arg)
{
    int old;

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,&old);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&old);
    for (;;) {
	sleep(1);
	sync();
    }
    return NULL;
}

/*-------------------------------------------------------------------------*/
/* color space conversion / compression thread                             */

struct ng_convthread_handle {
    /* converter data / state */
    struct ng_process_handle *p;

    /* thread data */
    struct FIFO              *in;
    struct FIFO              *out;
};

void*
ng_convert_thread(void *arg)
{
    struct ng_convthread_handle *h = arg;
    struct ng_video_buf *in, *out;
    
    if (debug)
	fprintf(stderr,"convert_thread start [pid=%d]\n",getpid());
    for (;;) {
	out = ng_process_get_frame(h->p);
	if (NULL == out) {
	    in  = fifo_get(h->in);
	    if (NULL == in)
		break;
	    ng_process_put_frame(h->p,in);
	    continue;
	}
#if 0 /* FIXME */
	if (NULL != webcam && 0 == webcam_put(webcam,out)) {
	    free(webcam);
	    webcam = NULL;
	}
#endif
	fifo_put(h->out,out);
    }
    fifo_put(h->out,NULL);
    if (debug)
	fprintf(stderr,"convert_thread done [pid=%d]\n",getpid());
    return NULL;
}

/*-------------------------------------------------------------------------*/
/* parameter negotiation -- look what the driver can do and what           */
/* convert functions are available                                         */

int
ng_grabber_setformat(struct ng_devstate *dev, struct ng_video_fmt *fmt, int fix_ratio)
{
    struct ng_video_fmt gfmt;
    int rc;
    
    /* no capture support */
    if (!(dev->flags & CAN_CAPTURE))
	return -1;

    /* try setting the format */
    gfmt = *fmt;
    rc = dev->v->setformat(dev->handle,&gfmt);
    if (debug)
	fprintf(stderr,"setformat: %s (%dx%d): %s\n",
		ng_vfmt_to_desc[gfmt.fmtid],
		gfmt.width,gfmt.height,
		(0 == rc) ? "ok" : "failed");
    if (0 != rc)
	return -1;

    if (fix_ratio) {
	/* fixup aspect ratio if needed */
	ng_ratio_fixup(&gfmt.width, &gfmt.height, NULL, NULL);
	gfmt.bytesperline = 0;
	if (0 != dev->v->setformat(dev->handle, &gfmt)) {
	    fprintf(stderr,"Oops: ratio size renegotiation failed\n");
	    exit(1);
	}
    }

    /* return the real format the grabber uses now */
    *fmt = gfmt;
    return 0;
}

struct ng_video_conv*
ng_grabber_findconv(struct ng_devstate *dev, struct ng_video_fmt *fmt,
		    int fix_ratio)
{
    struct ng_video_fmt  gfmt;
    struct ng_video_conv *conv;
    int i;
    
    /* check all available conversion functions */
    for (i = 0;;) {
	conv = ng_conv_find_to(fmt->fmtid, &i);
	if (NULL == conv)
	    break;
	gfmt = *fmt;
	gfmt.fmtid = conv->fmtid_in;
	if (0 == ng_grabber_setformat(dev, &gfmt,fix_ratio))
	    goto found;
    }
    fprintf(stderr,"no way to get: %dx%d %s\n",
	    fmt->width,fmt->height,ng_vfmt_to_desc[fmt->fmtid]);
    return NULL;

 found:
    *fmt = gfmt;
    return conv;
}

struct ng_video_buf*
ng_grabber_grab_image(struct ng_devstate *dev, int single)
{
    return single
	? dev->v->getimage(dev->handle)
	: dev->v->nextframe(dev->handle);
}

struct ng_video_buf*
ng_grabber_get_image(struct ng_devstate *dev, struct ng_video_fmt *fmt)
{
    struct ng_video_fmt gfmt;
    struct ng_video_conv *conv;
#if 0
    struct ng_convert_handle *ch;
#endif
    struct ng_video_buf *buf;
    
    if (0 == ng_grabber_setformat(dev,fmt,1))
	return ng_grabber_grab_image(dev,1);
    gfmt = *fmt;
    if (NULL == (conv = ng_grabber_findconv(dev,&gfmt,1)))
	return NULL;
#if 0
    ch = ng_convert_alloc(conv,&gfmt,fmt);
    if (NULL == (buf = ng_grabber_grab_image(dev,1)))
	return NULL;
    buf = ng_convert_single(ch,buf);
#else
    BUG_ON(1,"not implemented yet");
#endif
    return buf;
}

/*-------------------------------------------------------------------------*/

struct movie_handle {
    /* general */
    pthread_mutex_t           lock;
    const struct ng_writer    *writer;
    void                      *handle;
    pthread_t                 tflush;
    uint64_t                  start;
    uint64_t                  rts;
    uint64_t                  stopby;
    int                       slots;

    /* video */
    struct ng_devstate        *vdev;
    struct ng_video_fmt       vfmt;
    int                       fps;
    int                       frames;
    int                       seq;
    struct FIFO               *vfifo;
    pthread_t                 tvideo;
    uint64_t                  vts;

    /* video converter thread */
    struct FIFO               *cfifo;
    struct ng_convthread_handle *hconv;
    pthread_t                 tconv;

    /* audio */
    struct ng_devstate        *adev;
    struct ng_audio_fmt       afmt;
    unsigned long             bytes_per_sec;
    unsigned long             bytes;
    struct FIFO               *afifo;
    pthread_t                 taudio;
    pthread_t                 raudio;
    uint64_t                  ats;

    uint64_t                  rdrift;
    uint64_t                  vdrift;
};

static void*
writer_audio_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_audio_buf *buf;

    if (debug)
	fprintf(stderr,"writer_audio_thread start [pid=%d]\n",getpid());
    for (;;) {
	buf = fifo_get(h->afifo);
	if (NULL == buf)
	    break;
	pthread_mutex_lock(&h->lock);
	h->writer->wr_audio(h->handle,buf);
	pthread_mutex_unlock(&h->lock);
	free(buf);
    }
    if (debug)
	fprintf(stderr,"writer_audio_thread done\n");
    return NULL;
}

/*
 * with multiple compression threads we might receive
 * the frames out-of-order
 */
static void*
writer_video_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_video_buf *buf;

    if (debug)
	fprintf(stderr,"writer_video_thread start [pid=%d]\n",getpid());
    for (;;) {
        buf = fifo_get(h->vfifo);
	if (NULL == buf)
	    break;
	pthread_mutex_lock(&h->lock);
	h->writer->wr_video(h->handle,buf);
	if (buf->info.twice)
	    h->writer->wr_video(h->handle,buf);
	pthread_mutex_unlock(&h->lock);
	ng_release_video_buf(buf);
    }
    if (debug)
	fprintf(stderr,"writer_video_thread done\n");
    return NULL;
}

static void*
record_audio_thread(void *arg)
{
    struct movie_handle *h = arg;
    struct ng_audio_buf *buf;

    if (debug)
	fprintf(stderr,"record_audio_thread start [pid=%d]\n",getpid());
    for (;;) {
	buf = h->adev->a->read(h->adev->handle,h->stopby);
	if (NULL == buf)
	    break;
	if (0 == buf->size)
	    continue;
	h->ats    = buf->info.ts;
	h->rts    = ng_get_timestamp() - h->start;
	h->rdrift = h->rts - h->ats;
	h->vdrift = h->vts - h->ats;
	if (0 != fifo_put(h->afifo,buf))
	    free(buf);
    }
    fifo_put(h->afifo,NULL);
    if (debug)
	fprintf(stderr,"record_audio_thread done\n");
    return NULL;
}

struct movie_handle*
movie_writer_init(char *moviename, char *audioname,
		  const struct ng_writer *writer,
		  struct ng_devstate *vdev,   struct ng_devstate *adev,
		  struct ng_video_fmt *video, const void *priv_video,
		  struct ng_audio_fmt *audio, const void *priv_audio,
		  int fps, int slots, int threads)
{
    struct movie_handle *h;
    struct ng_video_conv *conv;
    void *dummy;
    
    if (debug)
	fprintf(stderr,"movie_init_writer start\n");
    h = malloc(sizeof(*h));
    if (NULL == h)
	return NULL;
    memset(h,0,sizeof(*h));
    pthread_mutex_init(&h->lock, NULL);
    h->writer = writer;
    h->slots = slots;

    /* audio */
    if (audio->fmtid != AUDIO_NONE) {
	if (0 != ng_dev_open(adev)) {
	    free(h);
	    return NULL;
	}
	if (0 != adev->a->setformat(adev->handle, audio)) {
	    ng_dev_close(adev);
	    free(h);
	    return NULL;
	}
	h->afifo = fifo_init("audio",slots);
	pthread_create(&h->taudio,NULL,writer_audio_thread,h);
	h->bytes_per_sec = ng_afmt_to_bits[audio->fmtid] *
	    ng_afmt_to_channels[audio->fmtid] * audio->rate / 8;
	h->adev = adev;
	h->afmt = *audio;
    }

    /* video */
    if (video->fmtid != VIDEO_NONE) {
	if (0 == ng_grabber_setformat(vdev, video,1)) {
	    /* native format works -- no conversion needed */
	    h->vfifo = fifo_init("video",slots);
	    pthread_create(&h->tvideo,NULL,writer_video_thread,h);
	} else {
	    /* have to convert video frames */
	    struct ng_video_fmt gfmt = *video;
	    if (NULL == (conv = ng_grabber_findconv(vdev,&gfmt,1))) {
		if (h->afmt.fmtid != AUDIO_NONE)
		    ng_dev_close(adev);
		free(h);
		return NULL;
	    }
	    h->vfifo = fifo_init("video",slots);
	    h->cfifo = fifo_init("conv",slots);
	    pthread_create(&h->tvideo,NULL,writer_video_thread,h);
	    h->hconv = malloc(sizeof(struct ng_convthread_handle));
	    memset(h->hconv,0,sizeof(struct ng_convthread_handle));
	    h->hconv->p   = ng_conv_init(conv,&gfmt,video);
	    h->hconv->in  = h->cfifo;
	    h->hconv->out = h->vfifo;
	    pthread_create(&h->tconv,NULL,ng_convert_thread,
			   h->hconv);
	}
	h->vdev = vdev;
	h->vfmt = *video;
	h->fps  = fps;
    }	
    
    /* open file */
    h->handle = writer->wr_open(moviename,audioname,
				video,priv_video,fps,
				audio,priv_audio);
    if (debug)
	fprintf(stderr,"movie_init_writer end (h=%p)\n",h->handle);
    if (NULL != h->handle)
	return h;

    /* Oops -- wr_open() didn't work.  cleanup.  */
    if (h->afmt.fmtid != AUDIO_NONE) {
	pthread_cancel(h->taudio);
	pthread_join(h->taudio,&dummy);
	ng_dev_close(adev);
    }
    if (h->vfmt.fmtid != VIDEO_NONE) {
	pthread_cancel(h->tvideo);
	pthread_join(h->tvideo,&dummy);
    }
    if (h->hconv) {
	pthread_cancel(h->tconv);
	pthread_join(h->tconv,&dummy);
    }
    free(h);
    return NULL;
}

int
movie_writer_start(struct movie_handle *h)
{
    int rc = 0;

    if (debug)
	fprintf(stderr,"movie_writer_start\n");
    h->start = ng_get_timestamp();
    if (h->afmt.fmtid != AUDIO_NONE)
	if (0 != h->adev->a->startrec(h->adev->handle))
	    rc = -1;
    if (h->vfmt.fmtid != VIDEO_NONE)
	if (0 != h->vdev->v->startvideo(h->vdev->handle,h->fps,h->slots))
	    rc = -1;
    if (h->afmt.fmtid != AUDIO_NONE)
	pthread_create(&h->raudio,NULL,record_audio_thread,h);
    pthread_create(&h->tflush,NULL,flushit,NULL);
    return rc;
}

int
movie_writer_stop(struct movie_handle *h)
{
    uint64_t  stopby;
    int frames;
    void *dummy;

    if (debug)
	fprintf(stderr,"movie_writer_stop\n");

    if (h->vfmt.fmtid != VIDEO_NONE && h->afmt.fmtid != AUDIO_NONE) {
	for (frames = 0; frames < 16; frames++) {
	    stopby = (uint64_t)(h->frames + frames) * (uint64_t)1000000000000 / h->fps;
	    if (stopby > h->ats)
		break;
	}
	frames++;
	h->stopby = (uint64_t)(h->frames + frames) * (uint64_t)1000000000000 / h->fps;
	while (frames) {
	    movie_grab_put_video(h,NULL);
	    frames--;
	}
    } else if (h->afmt.fmtid != AUDIO_NONE) {
	h->stopby = h->ats;
    }

    /* send EOF */
    if (h->hconv)
	fifo_put(h->cfifo,NULL);
    else
	fifo_put(h->vfifo,NULL);

    /* join threads */
    if (h->afmt.fmtid != AUDIO_NONE) {
	pthread_join(h->raudio,&dummy);
	pthread_join(h->taudio,&dummy);
    }
    if (h->vfmt.fmtid != VIDEO_NONE)
	pthread_join(h->tvideo,&dummy);
    if (h->hconv)
	pthread_join(h->tconv,&dummy);
    pthread_cancel(h->tflush);
    pthread_join(h->tflush,&dummy);

    /* close file */
    h->writer->wr_close(h->handle);
    if (h->afmt.fmtid != AUDIO_NONE)
	ng_dev_close(h->adev);
    if (h->vfmt.fmtid != VIDEO_NONE)
	h->vdev->v->stopvideo(h->vdev->handle);

    /* fifo stats */
#if 0 /* FIXME */
    sprintf(line, "fifo max fill: audio %d/%d, video %d/%d, convert %d/%d",
	    h->afifo.max,h->afifo.slots,
	    h->vfifo.max,h->vfifo.slots,
	    h->cfifo.max,h->cfifo.slots);
    rec_status(line);
#endif

    free(h);
    return 0;
}

/*-------------------------------------------------------------------------*/

static void
movie_print_timestamps(struct movie_handle *h)
{
    char line[128];

    if (NULL == rec_status)
	return;

    sprintf(line,"rec %d:%02d.%02d  -  a/r: %c%d.%02ds [%d], a/v: %c%d.%02ds [%d]",
	    (int)(h->rts / 1000000000 / 60),
	    (int)(h->rts / 1000000000 % 60),
	    (int)((h->rts % 1000000000) / 10000000),
	    (h->rdrift > 0) ? '+' : '-',
	    (int)((abs(h->rdrift) / 1000000000)),
	    (int)((abs(h->rdrift) % 1000000000) / 10000000),
	    (int)(h->rdrift * h->fps / (uint64_t)1000000000000),
	    (h->vdrift > 0) ? '+' : '-',
	    (int)((abs(h->vdrift) / 1000000000)),
	    (int)((abs(h->vdrift) % 1000000000) / 10000000),
	    (int)(h->vdrift * h->fps / (uint64_t)1000000000000));
    rec_status(line);
}

int
movie_grab_put_video(struct movie_handle *h, struct ng_video_buf **ret)
{
    struct ng_video_buf *buf;
    int expected;

    if (debug > 1)
	fprintf(stderr,"grab_put_video\n");

    /* fetch next frame */
    buf = ng_grabber_grab_image(h->vdev,0);
    if (NULL == buf) {
	if (debug)
	    fprintf(stderr,"grab_put_video: grab image failed\n");
	return -1;
    }
#if 0 /* FIXME */
    buf = ng_filter_single(cur_filter,buf);
#endif

    /* rate control */
    expected = (buf->info.ts - h->vdrift) * h->fps / (uint64_t)1000000000000;
    if (expected < h->frames-1) {
	if (debug > 1)
	    fprintf(stderr,"rate: ignoring frame [%d %d]\n",
		    expected, h->frames);
	ng_release_video_buf(buf);
	return 0;
    }
    if (expected > h->frames+1) {
	fprintf(stderr,"rate: queueing frame twice (%d)\n",
		expected-h->frames);
	buf->info.twice++;
	h->frames++;
    }
    h->frames++;
    h->vts = buf->info.ts;
    buf->info.file_seq = h->seq;
    buf->info.play_seq = h->seq;
    h->seq++;

    /* return a pointer to the frame if requested */
    if (NULL != ret) {
	buf->refcount++;
	*ret = buf;
    }
    
    /* put into fifo */
    if (h->hconv) {
	if (0 != fifo_put(h->cfifo,buf))
	    ng_release_video_buf(buf);    
    } else {
	if (0 != fifo_put(h->vfifo,buf))
	    ng_release_video_buf(buf);    
    }

    /* feedback */
    movie_print_timestamps(h);
    return h->frames;
}
