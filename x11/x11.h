extern unsigned int  swidth,sheight;

void x11_label_pixmap(Display *dpy, Colormap colormap, Pixmap pixmap,
		      int height, char *label);
Pixmap x11_capture_pixmap(Display *dpy, XVisualInfo *vinfo, Colormap colormap,
			  unsigned int width, unsigned int height);

