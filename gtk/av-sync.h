struct audio_stream {
    struct ng_audio_fmt         *ifmt;
    struct ng_audio_fmt         ofmt;
    int                         datarate;
    int                         wrfd;

    struct ng_audio_conv        *conv;
    void                        *chandle;
};

struct video_stream {
    struct ng_video_fmt         *ifmt;
    struct ng_video_buf         *ibuf;

    struct list_head            processes;

    /* dest -- hmm, duplicated */
    struct blit_handle          *blit;
};

struct media_stream {
    /* real time */
    int64_t                     start;
    
    /* audio */
    struct audio_stream         *as;
    struct ng_audio_buf         *abuf;
    struct ng_audio_buf         *abuf_next;
    int64_t                     latency;
    int64_t                     drift;
    int64_t                     drifts[16];
    int                         idrift;

    /* video */
    struct video_stream         *vs;
    struct ng_video_buf         *vbuf;
    struct ng_video_buf         *vbuf_next;
    int64_t                     frame;
    int64_t                     delay;
    int                         timeout;
    int                         drop;
    int                         blitframe;

    /* source -- media stream parser */
    struct ng_reader            *reader;
    void                        *rhandle;

    /* source -- capture device */
    struct ng_video_fmt         capfmt;

    /* dest */
    struct blit_handle          *blit;
    
    /* config */
    int                         speed;
    
    /* stats */
    int                         drops;
    int                         frames;
};

/* ---------------------------------------------------------------------------- */

struct audio_stream* av_audio_init(struct ng_audio_fmt *ifmt);
void av_audio_fini(struct audio_stream *a);
int  av_audio_writeable(struct audio_stream *a);
struct ng_audio_buf* av_audio_conv_buf(struct audio_stream *a,
				       struct ng_audio_buf *buf);

/* ---------------------------------------------------------------------------- */

struct video_stream* av_video_init(struct ng_video_fmt *ifmt);
void av_video_fini(struct video_stream *v);

void av_video_add_convert(struct video_stream  *v,
			  struct ng_video_conv *conv,
			  ng_get_video_buf get_obuf,
			  void *ohandle);
void av_video_put_frame(struct video_stream  *v,
			struct ng_video_buf *buf);
struct ng_video_buf *av_video_get_frame(struct video_stream  *v);

/* ---------------------------------------------------------------------------- */

void av_sync_audio(struct media_stream *mm);
void av_sync_video(struct media_stream *mm);

/* ---------------------------------------------------------------------------- */
/* work in progress ...                                                         */

void av_media_reader_audio(struct media_stream *mm,
			struct ng_audio_fmt *afmt);
void av_media_reader_video(struct media_stream *mm);
void av_media_grab_video(struct media_stream *mm, GtkWidget *widget);
void av_media_mainloop(GMainContext *context, struct media_stream *mm);
