/*
 * write out mpeg program streams [incomplete]
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/param.h>
#include <sys/uio.h>

#include "grab-ng.h"

/* ----------------------------------------------------------------------- */

struct mpeg_handle {
    /* file name+handle */
    char   file[MAXPATHLEN];
    int    fd;

    /* format */
    struct ng_video_fmt video;
    struct ng_audio_fmt audio;
};

/* ----------------------------------------------------------------------- */

static void*
mpeg_open(char *filename, char *dummy,
	  struct ng_video_fmt *video, const void *priv_video, int fps,
	  struct ng_audio_fmt *audio, const void *priv_audio)
{
    struct mpeg_handle      *h;

    if (NULL == filename)
	return NULL;
    if (NULL == (h = malloc(sizeof(*h))))
	return NULL;

    /* init */
    memset(h, 0, sizeof(*h));
    h->video = *video;
    h->audio = *audio;

    strcpy(h->file,filename);
    if (-1 == (h->fd = open(h->file,O_CREAT | O_RDWR | O_TRUNC, 0666))) {
	fprintf(stderr,"open %s: %s\n",h->file,strerror(errno));
	free(h);
	return NULL;
    }

    /* video */
    if (h->video.fmtid != VIDEO_NONE) {
    }

    /* audio */
    if (h->audio.fmtid != AUDIO_NONE) {
    }

    return h;
}

static int
mpeg_video(void *handle, struct ng_video_buf *buf)
{
    // struct mpeg_handle *h = handle;
    return 0;
}

static int
mpeg_audio(void *handle, struct ng_audio_buf *buf)
{
    // struct mpeg_handle *h = handle;
    return 0;
}

static int
mpeg_close(void *handle)
{
    struct mpeg_handle *h = handle;
    close(h->fd);
    free(h);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* data structures describing our capabilities                             */

static const struct ng_format_list mpeg_vformats[] = {
    {
	.name  = "mpeg",
	.ext   = "mpeg",
	.fmtid = VIDEO_MPEG,
    },{
	/* EOF */
    }
};

static const struct ng_format_list mpeg_aformats[] = {
    {
	.name  = "mpeg",
	.ext   = "mpeg",
	.fmtid = AUDIO_MP3,
    },{
	/* EOF */
    }
};

struct ng_writer mpeg_writer = {
    .name      = "mpeg",
    .desc      = "MPEG Programm Stream",
    .combined  = 1,
    .video     = mpeg_vformats,
    .audio     = mpeg_aformats,
    .wr_open   = mpeg_open,
    .wr_video  = mpeg_video,
    .wr_audio  = mpeg_audio,
    .wr_close  = mpeg_close,
};

static void __init plugin_init(void)
{
    ng_writer_register(NG_PLUGIN_MAGIC,__FILE__,&mpeg_writer);
}
