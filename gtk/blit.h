/* plain X11 stuff */
extern unsigned int x11_dpy_fmtid;

Visual* x11_find_visual(Display *dpy);
void x11_init_visual(Display *dpy, XVisualInfo *vinfo);

XImage *x11_create_ximage(Display *dpy,  XVisualInfo *vinfo,
			  int width, int height, XShmSegmentInfo **shm);
void x11_destroy_ximage(Display *dpy, XImage * ximage, XShmSegmentInfo *shm);
Pixmap x11_create_pixmap(Display *dpy, XVisualInfo *vinfo,
			 struct ng_video_buf *buf);
void x11_draw_ximage(Display *dpy, Drawable dr, GC gc, XImage *xi,
		     int a, int b, int c, int d, int w, int h);

/* video frame blitter */
struct blit_handle;
struct blit_handle* blit_init(GtkWidget *widget, XVisualInfo *vinfo,
			      int use_xv, int use_gl);
void blit_fini(struct blit_handle *h);
void blit_get_formats(struct blit_handle *h, int *fmtids, int max);
struct ng_video_buf* blit_get_buf(void *handle, struct ng_video_fmt *fmt);
void blit_draw_buf(struct ng_video_buf *buf);
