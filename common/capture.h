#ifndef CAPTURE_H
#define CAPTURE_H

void* ng_convert_thread(void *arg);

int ng_grabber_setformat(struct ng_devstate *dev, struct ng_video_fmt *fmt,
			 int fix_ratio);
struct ng_video_conv* ng_grabber_findconv(struct ng_devstate *dev,
					  struct ng_video_fmt *fmt,
					  int fix_ratio);
struct ng_video_buf* ng_grabber_grab_image(struct ng_devstate *dev,
					   int single);
struct ng_video_buf* ng_grabber_get_image(struct ng_devstate *dev,
					  struct ng_video_fmt *fmt);


struct movie_handle*
movie_writer_init(char *moviename, char *audioname,
		  const struct ng_writer *writer, 
		  struct ng_devstate *vdev, struct ng_devstate *adev,
		  struct ng_video_fmt *video, const void *priv_video,
		  struct ng_audio_fmt *audio, const void *priv_audio,
		  int fps, int slots, int threads);
int movie_writer_start(struct movie_handle*);
int movie_writer_stop(struct movie_handle*);

int movie_grab_put_video(struct movie_handle*, struct ng_video_buf **ret);
int movie_grab_put_audio(struct movie_handle*);

#endif /* CAPTURE_H */
