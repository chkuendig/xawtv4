/*
 * (c) 1998-2003 Gerd Knorr
 *
 *    capture a image, compress as jpeg and upload to the webserver
 *    using the ftp utility or ssh
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <math.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "grab-ng.h"
#include "jpeglib.h"
#include "ftp.h"
#include "devs.h"
#include "parseconfig.h"
#include "list.h"


/* ---------------------------------------------------------------------- */
/* configuration                                                          */

int   debug     = 0;
int   daemonize = 0;
char  *archive  = NULL;
char  *tmpdir;
struct list_head connections;

char  *grab_text   = "webcam %Y-%m-%d %H:%M:%S"; /* strftime */
char  *grab_infofile = NULL;
int   grab_width  = 320;
int   grab_height = 240;
int   grab_delay  = 3;
int   grab_wait   = 0;
int   grab_rotate = 0;
int   grab_top    = -1;
int   grab_left   = -1;
int   grab_bottom = -1;
int   grab_right  = -1;
int   grab_quality= 75;
int   grab_trigger= 0;
int   grab_times  = -1;
int   grab_fg_r   = 255;
int   grab_fg_g   = 255;
int   grab_fg_b   = 255;
int   grab_bg_r   = -1;
int   grab_bg_g   = -1;
int   grab_bg_b   = -1;

/* ---------------------------------------------------------------------- */
/* jpeg stuff                                                             */

static int
write_file(int fd, char *data, int width, int height)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fp;
    int i;
    unsigned char *line;

    fp = fdopen(fd,"w");
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, fp);
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, grab_quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    for (i = 0, line = data; i < height; i++, line += width*3)
	jpeg_write_scanlines(&cinfo, &line, 1);
    
    jpeg_finish_compress(&(cinfo));
    jpeg_destroy_compress(&(cinfo));
    fclose(fp);

    return 0;
}

/* ---------------------------------------------------------------------- */
/* image transfer                                                         */

struct xfer_ops;

struct xfer_state {
    char              *name;
    struct list_head  list;

    /* xfer options */
    char              *host;
    char              *user;
    char              *pass;
    char              *dir;
    char              *file;
    char              *tmpfile;
    int               debug;

    /* ftp options */
    int               passive,autologin;

    /* function pointers + private date */
    struct xfer_ops   *ops;
    void              *data;
};

struct xfer_ops {
    int  (*open)(struct xfer_state*);
    void (*info)(struct xfer_state*);
    int  (*xfer)(struct xfer_state*, char *image, int width, int height);
    void (*close)(struct xfer_state*);
};

static int ftp_open(struct xfer_state *s)
{
    s->data = ftp_init(s->name,s->autologin,s->passive,s->debug);
    ftp_connect(s->data,s->host,s->user,s->pass,s->dir);
    return 0;
}

static void ftp_info(struct xfer_state *s)
{
    fprintf(stderr,"ftp config [%s]:\n  %s@%s:%s\n  %s => %s\n",
	    s->name,s->user,s->host,s->dir,s->tmpfile,s->file);
}

static int ftp_xfer(struct xfer_state *s, char *image, int width, int height)
{
    char filename[1024];
    int fh;
    
    snprintf(filename,sizeof(filename),"%s/webcamXXXXXX",tmpdir);
    if (-1 == (fh = mkstemp(filename))) {
	perror("mkstemp");
	exit(1);
    }
    write_file(fh, image, width, height);
    if (ftp_connected(s->data))
	ftp_connect(s->data,s->host,s->user,s->pass,s->dir);
    ftp_upload(s->data,filename,s->file,s->tmpfile);
    unlink(filename);
    return 0;
}

static void ftp_close(struct xfer_state *s)
{
    ftp_fini(s->data);
}

static struct xfer_ops ftp_ops = {
    open:  ftp_open,
    info:  ftp_info,
    xfer:  ftp_xfer,
    close: ftp_close,
};

static int ssh_open(struct xfer_state *s)
{
    s->data = malloc(strlen(s->user)+strlen(s->host)+strlen(s->tmpfile)*3+
		     strlen(s->dir)+strlen(s->file)+64);
    sprintf(s->data, "ssh %s@%s \"cat >%s && chmod 644 %s && mv %s %s/%s\"",
	    s->user,s->host,s->tmpfile,s->tmpfile,s->tmpfile,s->dir,s->file);
    return 0;
}

static void ssh_info(struct xfer_state *s)
{
    fprintf(stderr,"ssh config [%s]:\n  %s@%s:%s\n  %s => %s\n",
	    s->name,s->user,s->host,s->dir,s->tmpfile,s->file);
}

static int ssh_xfer(struct xfer_state *s, char *image, int width, int height)
{
    char filename[1024];
    char *cmd = s->data;
    unsigned char buf[4096];
    FILE *sshp, *imgdata;
    int len,fh;
    
    snprintf(filename,sizeof(filename),"%s/webcamXXXXXX",tmpdir);
    if (-1 == (fh = mkstemp(filename))) {
	perror("mkstemp");
	exit(1);
    }
    write_file(fh, image, width, height);

    if ((sshp=popen(cmd, "w")) == NULL) {
	perror("popen");
	exit(1);
    }
    if ((imgdata = fopen(filename,"rb"))==NULL) {
	perror("fopen");
	exit(1);
    }
    for (;;) {
	len = fread(buf,1,sizeof(buf),imgdata);
	if (len <= 0)
	    break;
	fwrite(buf,1,len,sshp);
    }
    fclose(imgdata);
    pclose(sshp);

    unlink(filename);
    return 0;
}

static void ssh_close(struct xfer_state *s)
{
    char *cmd = s->data;
    free(cmd);
}

static struct xfer_ops ssh_ops = {
    open:  ssh_open,
    info:  ssh_info,
    xfer:  ssh_xfer,
    close: ssh_close,
};

static int sshs_open(struct xfer_state *s)
{
    s->data = malloc(strlen(s->tmpfile)*3+
		     strlen(s->dir)+strlen(s->file)+
		     64);
    sprintf(s->data, "uudecode -o %s << EOF; chmod 644 %s; mv %s %s/%s\n",
	    s->tmpfile,s->tmpfile,s->tmpfile,s->dir,s->file);
    return 0;
}

static void sshs_info(struct xfer_state *s)
{
    fprintf(stderr,"sshs config [%s]:\n  %s@%s:%s\n  %s => %s\n",
	    s->name,s->user,s->host,s->dir,s->tmpfile,s->file);
}

static int sshs_xfer(struct xfer_state *s, char *image, int width, int height)
{
    char filename[1024];
    char *cmd = s->data;
    unsigned char buf[4096];
    FILE *sshp, *imgdata;
    int len,fh;
    
    sprintf(filename,"%s/webcamXXXXXX",tmpdir);
    if (-1 == (fh = mkstemp(filename))) {
	perror("mkstemp");
	exit(1);
    }
    write_file(fh, image, width, height);

    printf("%s\n",cmd);
    fflush(stdout);
    if ((sshp=popen("uuencode -m img", "w")) == NULL) {
	perror("popen");
	exit(1);
    }
    if ((imgdata = fopen(filename,"rb"))==NULL) {
	perror("fopen");
	exit(1);
    }
    for (;;) {
	len = fread(buf,1,sizeof(buf),imgdata);
	if (len <= 0)
	    break;
	fwrite(buf,1,len,sshp);
    }
    fclose(imgdata);
    pclose(sshp);
    printf("\nEOF\n");
    fflush(stdout);

    unlink(filename);
    return 0;
}

static void sshs_close(struct xfer_state *s)
{
    char *cmd = s->data;
    free(cmd);
}

static struct xfer_ops sshs_ops = {
    open:  sshs_open,
    info:  sshs_info,
    xfer:  sshs_xfer,
    close: sshs_close,
};

static int local_open(struct xfer_state *s)
{
    char *t;
    
    if (s->dir != NULL && s->dir[0] != '\0' ) {
	t = malloc(strlen(s->tmpfile)+strlen(s->dir)+2);
	sprintf(t, "%s/%s", s->dir, s->tmpfile);
	s->tmpfile = t;
	
	t = malloc(strlen(s->file)+strlen(s->dir)+2);
	sprintf(t, "%s/%s", s->dir, s->file);
	s->file = t;
    }
    return 0;
}

static void local_info(struct xfer_state *s)
{
    fprintf(stderr,"write config [%s]:\n  local transfer %s => %s\n",
	    s->name,s->tmpfile,s->file);
}

static int local_xfer(struct xfer_state *s, char *image, int width, int height)
{
    int fh;
    
    if (-1 == (fh = open(s->tmpfile,O_CREAT|O_WRONLY|O_TRUNC,0666))) {
	fprintf(stderr,"open %s: %s\n",s->tmpfile,strerror(errno));
	exit(1);
    }
    write_file(fh, image, width, height);
    if (rename(s->tmpfile, s->file) ) {
	fprintf(stderr, "can't move %s -> %s\n", s->tmpfile, s->file);
	exit(1);
    }
    return 0;
}

static void local_close(struct xfer_state *s)
{
    /* nothing */
}

static struct xfer_ops local_ops = {
    open:  local_open,
    info:  local_info,
    xfer:  local_xfer,
    close: local_close,
};

/* ---------------------------------------------------------------------- */
/* capture stuff                                                          */

char                        *device;
struct ng_devstate          dev;
struct ng_video_fmt         fmt,gfmt;
struct ng_video_conv        *conv;
struct ng_process_handle    *handle;

static void
grab_init(void)
{
    struct ng_attribute *attr;
    char *str;
    int val;
    int i;

    /* open device */
    if (0 != ng_vid_init(device,&dev)) {
	fprintf(stderr,"no grabber device available\n");
	exit(1);
    }
    if (!(dev.flags & CAN_CAPTURE)) {
	fprintf(stderr,"device does'nt support capture\n");
	exit(1);
    }

    /* configure input + tv norm */
    if (NULL != (str  = cfg_get_str("webcam","grab","input")) &&
	NULL != (attr = find_attr_by_dev_id(&dev,ATTR_ID_INPUT))) {
	val = ng_attr_getint(attr,str);
	if (-1 != val)
	    attr_write(attr,val,0);
    }
    if (NULL != (str  = cfg_get_str("webcam","grab","norm")) &&
	NULL != (attr = find_attr_by_dev_id(&dev,ATTR_ID_NORM))) {
	val = ng_attr_getint(attr,str);
	if (-1 != val)
	    attr_write(attr,val,0);
    }

    /* configure image attributes */
    if (NULL != (str  = cfg_get_str("webcam","grab","bright")) &&
	NULL != (attr = find_attr_by_dev_id(&dev,ATTR_ID_BRIGHT))) {
	val = ng_attr_parse_int(attr,str);
	attr_write(attr,val,0);
    }
    if (NULL != (str  = cfg_get_str("webcam","grab","contrast")) &&
	NULL != (attr = find_attr_by_dev_id(&dev,ATTR_ID_CONTRAST))) {
	val = ng_attr_parse_int(attr,str);
	attr_write(attr,val,0);
    }
    if (NULL != (str  = cfg_get_str("webcam","grab","hue")) &&
	NULL != (attr = find_attr_by_dev_id(&dev,ATTR_ID_HUE))) {
	val = ng_attr_parse_int(attr,str);
	attr_write(attr,val,0);
    }
    if (NULL != (str  = cfg_get_str("webcam","grab","color")) &&
	NULL != (attr = find_attr_by_dev_id(&dev,ATTR_ID_COLOR))) {
	val = ng_attr_parse_int(attr,str);
	attr_write(attr,val,0);
    }

    /* try native */
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = grab_width;
    fmt.height = grab_height;
    if (0 == dev.v->setformat(dev.handle,&fmt))
	return;
    
    /* check all available conversion functions */
    fmt.bytesperline = fmt.width*ng_vfmt_to_depth[fmt.fmtid]/8;
    for (i = 0;;) {
	conv = ng_conv_find_to(fmt.fmtid, &i);
	if (NULL == conv)
	    break;
	gfmt = fmt;
	gfmt.fmtid = conv->fmtid_in;
	gfmt.bytesperline = 0;
	if (0 == dev.v->setformat(dev.handle,&gfmt)) {
	    fmt.width  = gfmt.width;
	    fmt.height = gfmt.height;
	    handle = ng_conv_init(conv,&gfmt,&fmt);
	    return;
	}
    }
    fprintf(stderr,"can't get rgb24 data\n");
    exit(1);
}

static unsigned char*
grab_one(int *width, int *height)
{
    static struct ng_video_buf *cap,*buf;

    if (NULL != buf)
	ng_release_video_buf(buf);
    if (NULL == (cap = dev.v->getimage(dev.handle))) {
	fprintf(stderr,"capturing image failed\n");
	exit(1);
    }

    if (NULL != conv) {
	ng_process_put_frame(handle,cap);
	buf = ng_process_get_frame(handle);
	buf->info = cap->info;
	ng_release_video_buf(cap);
    } else {
	buf = cap;
    }
    
    *width  = buf->fmt.width;
    *height = buf->fmt.height;
    return buf->data;
}

/* ---------------------------------------------------------------------- */

#define MSG_MAXLEN   256

#define CHAR_HEIGHT  11
#define CHAR_WIDTH   6
#define CHAR_START   4
#include "font-6x11.h"

static char*
get_message(void)
{
    static char buffer[MSG_MAXLEN+1];
    FILE *fp;
    char *p;
    
    if (NULL == grab_infofile)
	return grab_text;

    if (NULL == (fp = fopen(grab_infofile, "r"))) {
	fprintf(stderr,"open %s: %s\n",grab_infofile,strerror(errno));
	return grab_text;
    }

    fgets(buffer, MSG_MAXLEN, fp);
    fclose(fp);
    if (NULL != (p = strchr(buffer,'\n')))
	*p = '\0';
    return buffer;
}

static void
add_text(char *image, int width, int height)
{
    time_t      t;
    struct tm  *tm;
    unsigned char line[MSG_MAXLEN+1],*ptr;
    int         i,x,y,f,len;

    time(&t);
    tm = localtime(&t);
    len = strftime(line,MSG_MAXLEN,get_message(),tm);
    // fprintf(stderr,"%s\n",line);

    for (y = 0; y < CHAR_HEIGHT; y++) {
	ptr = image + 3 * width * (height-CHAR_HEIGHT-2+y) + 12;
	for (x = 0; x < len; x++) {
	    f = fontdata[line[x] * CHAR_HEIGHT + y];
	    for (i = CHAR_WIDTH-1; i >= 0; i--) {
		if (f & (CHAR_START << i)) {
		    ptr[0] = grab_fg_r;
		    ptr[1] = grab_fg_g;
		    ptr[2] = grab_fg_b;
		} else if (grab_bg_r != -1) {
		    ptr[0] = grab_bg_r;
		    ptr[1] = grab_bg_g;
		    ptr[2] = grab_bg_b;
		}
		ptr += 3;
	    }
	}
    }
}

/* ---------------------------------------------------------------------- */
/* Frederic Helin <Frederic.Helin@inrialpes.fr> - 15/07/2002              */
/* Correction fonction of stereographic radial distortion                 */

int grab_dist_on = 0; 
int grab_dist_k = 700;
int grab_dist_cx = -1; 
int grab_dist_cy = -1;
int grab_dist_zoom = 50;
int grab_dist_sensorw = 640;
int grab_dist_sensorh = 480;

static unsigned char *
correct_distor(unsigned char * in, int width, int height,
	       int grab_zoom, int grap_k, int cx, int cy,
	       int grab_sensorw, int grab_sensorh)
{
    static unsigned char * corrimg = NULL;
    
    int i, j, di, dj;
    float dr, cr,ca, sensor_w, sensor_h, sx, zoom, k;
    
    sensor_w = grab_dist_sensorw/100.0;
    sensor_h = grab_dist_sensorh/100.0;
    zoom = grab_zoom / 100.0;
    k = grap_k / 100.0;
    
    if (corrimg == NULL && (corrimg = malloc(width*height*3)) == NULL ) {
	fprintf(stderr, "out of memory\n");
	exit(1);
    }
    
    sensor_w = 6.4;
    sensor_h = 4.8;
    
    // calc ratio x/y
    sx = width * sensor_h / (height * sensor_w);
    
    // calc new value of k in the coordonates systeme of computer
    k = k * height / sensor_h;
    
    // Clear image
    for (i = 0; i < height*width*3; i++) corrimg[i] = 255;
    
    for (j = 0; j < height ; j++) {
	for (i = 0; i < width ; i++) {	
	    
	    // compute radial distortion / parameters of center of image 
	    cr  = sqrt((i-cx)/sx*(i-cx)/sx+(j-cy)*(j-cy));
	    ca  = atan(cr/k/zoom);
	    dr = k * tan(ca/2);	
	    
	    if (i == cx && j == cy) {di = cx; dj = cy;}
	    else {
		di = (i-cx) * dr / cr + cx;
		dj = (j-cy) * dr / cr + cy;
	    }
	    
	    if (dj<height && di < width && di >= 0  && dj >= 0 &&
		j<height &&  i < width &&  i >= 0  &&  j >= 0 ) {
		corrimg[3*(j*width + i)  ] = in[3*(dj*width + di)  ];
		corrimg[3*(j*width + i)+1] = in[3*(dj*width + di)+1];
		corrimg[3*(j*width + i)+2] = in[3*(dj*width + di)+2];	
	    }
	}
    }
    return corrimg;	
}

/* ---------------------------------------------------------------------- */

static unsigned int
compare_images(unsigned char *last, unsigned char *current,
	       int width, int height)
{
    unsigned char *p1 = last;
    unsigned char *p2 = current;
    int avg, diff, max, i = width*height*3;

    for (max = 0, avg = 0; --i; p1++,p2++) {
	diff = (*p1 < *p2) ? (*p2 - *p1) : (*p1 - *p2);
	avg += diff;
	if (diff > max)
	    max = diff;
    }
    avg = avg / width / height;
    if (0 /* FIXME */)
	fprintf(stderr,"compare: max=%d,avg=%d\n",max,avg);
    /* return avg */
    return max;
}


static unsigned char *
rotate_image(unsigned char * in, int *wp, int *hp, int rot,
	     int top, int left, int bottom, int right)
{
    static unsigned char * rotimg = NULL;

    int i, j;

    int w = *wp;
    int ow = (right-left);
    int oh = (bottom-top);

    if (rotimg == NULL && (rotimg = malloc(ow*oh*3)) == NULL ) {
	fprintf(stderr, "out of memory\n");
	exit(1);
    }
    switch (rot) {
    default:
    case 0:
	for (j = 0; j < oh; j++) {
	    int ir = (j+top)*w+left;
	    int or = j*ow;
	    for (i = 0; i < ow; i++) {
		rotimg[3*(or + i)]   = in[3*(ir+i)];
		rotimg[3*(or + i)+1] = in[3*(ir+i)+1];
		rotimg[3*(or + i)+2] = in[3*(ir+i)+2];
	    }
	}
	*wp = ow;
	*hp = oh;
	break;
    case 1:
	for (i = 0; i < ow; i++) {
	    int rr = (ow-1-i)*oh;
	    int ic = i+left;
	    for (j = 0; j < oh; j++) {
		rotimg[3*(rr+j)]   = in[3*((j+top)*w+ic)];
		rotimg[3*(rr+j)+1] = in[3*((j+top)*w+ic)+1];
		rotimg[3*(rr+j)+2] = in[3*((j+top)*w+ic)+2];
	    }
	}
	*wp = oh;
	*hp = ow;
	break;
    case 2:
	for (j = 0; j < oh; j++) {
	    int ir = (j+top)*w;
	    for (i = 0; i < ow; i++) {
		rotimg[3*((oh-1-j)*ow + (ow-1-i))] = in[3*(ir+i+left)];
		rotimg[3*((oh-1-j)*ow + (ow-1-i))+1] = in[3*(ir+i+left)+1];
		rotimg[3*((oh-1-j)*ow + (ow-1-i))+2] = in[3*(ir+i+left)+2];
	    }
	}
	*wp = ow;
	*hp = oh;
	break;
    case 3:
	for (i = 0; i < ow; i++) {
	    int rr = i*oh;
	    int ic = i+left;
	    rr += oh-1;
	    for (j = 0; j < oh; j++) {
		rotimg[3*(rr-j)]   = in[3*((j+top)*w+ic)];
		rotimg[3*(rr-j)+1] = in[3*((j+top)*w+ic)+1];
		rotimg[3*(rr-j)+2] = in[3*((j+top)*w+ic)+2];
	    }
	}
	*wp = oh;
	*hp = ow;
	break;
    }
    return rotimg;	
}


/* ---------------------------------------------------------------------- */

static int make_dirs(char *filename)
{
    char *dirname,*h;
    int retval = -1;

    dirname = strdup(filename);
    if (NULL == dirname)
	goto done;
    h = strrchr(dirname,'/');
    if (NULL == h)
	goto done;
    *h = 0;

    if (-1 == (retval = mkdir(dirname,0777)))
	if (ENOENT == errno)
	    if (0 == make_dirs(dirname))
		retval = mkdir(dirname,0777);
    
 done:
    free(dirname);
    return retval;
}

int
main(int argc, char *argv[])
{
    unsigned char *image,*val,*gimg,*lastimg = NULL;
    int width, height, i, fh;
    char filename[1024];
    char *section;
    struct list_head *item;
    struct xfer_state *s;

    /* read config */
    if (argc > 1) {
	snprintf(filename,strlen(filename),"%s",argv[1]);
    } else {
	snprintf(filename,strlen(filename),"%s/%s",getenv("HOME"),".webcamrc");
    }
    fprintf(stderr,"reading config file: %s\n",filename);
    cfg_parse_file("webcam",filename);
    ng_init();

    if (NULL != (val = cfg_get_str("webcam","grab","device")))
	device = val;
    if (NULL != (val = cfg_get_str("webcam","grab","text")))
	grab_text = val;
    if (NULL != (val = cfg_get_str("webcam","grab","infofile")))
	grab_infofile = val;

    grab_width   = cfg_get_int("webcam","grab","width",  320);
    grab_height  = cfg_get_int("webcam","grab","height", 240);
    grab_delay   = cfg_get_int("webcam","grab","delay",  3);
    grab_wait    = cfg_get_int("webcam","grab","wait",   0);
    grab_rotate  = cfg_get_int("webcam","grab","rotate", 0);
    grab_quality = cfg_get_int("webcam","grab","quality", 75);
    grab_trigger = cfg_get_int("webcam","grab","trigger", 0);

    grab_top     = cfg_get_signed_int("webcam","grab","top",    -1);
    grab_left    = cfg_get_signed_int("webcam","grab","left",   -1);
    grab_bottom  = cfg_get_signed_int("webcam","grab","bottom", -1);
    grab_right   = cfg_get_signed_int("webcam","grab","right",  -1);

    if (cfg_get_bool("webcam","grab","once", 0))
	grab_times = 1;
    if (-1 != (i = cfg_get_signed_int("webcam","grab","times", -1)))
	grab_times = i;
    if (NULL != (val = cfg_get_str("webcam","grab","archive")))
	archive = val;

    grab_fg_r = cfg_get_signed_int("webcam","grab","fg_red",   255);
    grab_fg_g = cfg_get_signed_int("webcam","grab","fg_green", 255);
    grab_fg_b = cfg_get_signed_int("webcam","grab","fg_blue",  255);
    grab_bg_r = cfg_get_signed_int("webcam","grab","bg_red",   -1);
    grab_bg_g = cfg_get_signed_int("webcam","grab","bg_green", -1);
    grab_bg_b = cfg_get_signed_int("webcam","grab","bg_blue",  -1);
    
    /* defaults */
    if (grab_top < 0)    grab_top    = 0;
    if (grab_left < 0)   grab_left   = 0;
    if (grab_bottom < 0) grab_bottom = grab_height;
    if (grab_right < 0)  grab_right  = grab_width;

    if (grab_bottom > grab_height) grab_bottom = grab_height;
    if (grab_right > grab_width)   grab_right  = grab_width;

    if (grab_top >= grab_bottom) {
	fprintf(stderr, "config error: top must be smaller than bottom\n");
	exit(1);
    }
    if (grab_left >= grab_right) {
	fprintf(stderr, "config error: left must be smaller than right\n");
	exit(1);
    }

    if (grab_dist_k < 1 || grab_dist_k > 10000)
	grab_dist_k = 700;
    if (grab_dist_cx < 0 || grab_dist_cx > grab_width)
	grab_dist_cx = grab_width / 2;
    if (grab_dist_cy < 0 || grab_dist_cy > grab_height)
	grab_dist_cy = grab_height / 2;
    if (grab_dist_zoom < 1 || grab_dist_zoom > 1000)
	grab_dist_zoom = 100;
    if (grab_dist_sensorw < 1 ||  grab_dist_sensorw >9999)
	grab_dist_sensorw = 640;
    if (grab_dist_sensorh < 1 ||  grab_dist_sensorh >9999)
	grab_dist_sensorh = 480;

    INIT_LIST_HEAD(&connections);
    for (section  = cfg_sections_first("webcam");
	 section != NULL;
	 section  = cfg_sections_next("webcam",section)) {
	if (0 == strcasecmp(section,"grab"))
	    continue;

	/* init + set defaults */
	s = malloc(sizeof(*s));
	memset(s,0,sizeof(*s));
	s->name      = section;
	s->host      = "www";
	s->user      = "webcam";
	s->pass      = "xxxxxx";
	s->dir       = "public_html/images";
	s->file      = "webcam.jpeg";
	s->tmpfile   = "uploading.jpeg";
	s->ops       = &ftp_ops;

	/* from config */
	if (NULL != (val = cfg_get_str("webcam",section,"host")))
	    s->host = val;
	if (NULL != (val = cfg_get_str("webcam",section,"user")))
	    s->user = val;
	if (NULL != (val = cfg_get_str("webcam",section,"pass")))
	    s->pass = val;
	if (NULL != (val = cfg_get_str("webcam",section,"dir")))
	    s->dir = val;
	if (NULL != (val = cfg_get_str("webcam",section,"file")))
	    s->file = val;
	if (NULL != (val = cfg_get_str("webcam",section,"tmp")))
	    s->tmpfile = val;

	s->passive   = cfg_get_bool("webcam",section,"passive", 1);
	s->autologin = cfg_get_bool("webcam",section,"auto",    0);
	s->debug     = cfg_get_bool("webcam",section,"debug",   0);

	if (-1 != (i = cfg_get_int("webcam",section,"local", -1)))
	    if (i)
		s->ops = &local_ops;
	if (-1 != (i = cfg_get_int("webcam",section,"ssh", -1)))
	    if (i)
		s->ops = &ssh_ops;
	if (-1 != (i = cfg_get_int("webcam",section,"sshs", -1)))
	    if (i)
		s->ops = &sshs_ops;
	
	/* all done */
	list_add_tail(&s->list,&connections);
    }

    /* init everything */
    grab_init();
    sleep(grab_wait);
    tmpdir = (NULL != getenv("TMPDIR")) ? getenv("TMPDIR") : "/tmp";
    list_for_each(item,&connections) {
	s = list_entry(item, struct xfer_state, list);
	s->ops->open(s);
    }

    /* print config */
    fprintf(stderr,"video4linux webcam v1.5 - (c) 1998-2002 Gerd Knorr\n");
    fprintf(stderr,"grabber config:\n  size %dx%d [%s]\n",
	    fmt.width,fmt.height,ng_vfmt_to_desc[gfmt.fmtid]);
    fprintf(stderr,"  jpeg quality %d\n", grab_quality);
    fprintf(stderr,"  rotate=%d, top=%d, left=%d, bottom=%d, right=%d\n",
	   grab_rotate, grab_top, grab_left, grab_bottom, grab_right);
    list_for_each(item,&connections) {
	s = list_entry(item, struct xfer_state, list);
	s->ops->info(s);
    }

    /* run as daemon - detach from terminal */
    if (daemonize) {
        switch (fork()) {
        case -1:
	    perror("fork");
	    exit(1);
        case 0:
            close(0); close(1); close(2); setsid();
            break;
        default:
            exit(0);
        }
    }

    /* main loop */
    for (;;) {
	/* grab a new one */
	gimg = grab_one(&width,&height);

	if (grab_dist_on)
	    gimg = correct_distor(gimg, width, height,
				  grab_dist_zoom, grab_dist_k,
				  grab_dist_cx, grab_dist_cy,
				  grab_dist_sensorw, grab_dist_sensorh);
	
	image = rotate_image(gimg, &width, &height, grab_rotate,
			     grab_top, grab_left, grab_bottom, grab_right);

	if (grab_trigger) {
	    /* look if it has changed */
	    if (NULL != lastimg) {
		i = compare_images(lastimg,image,width,height);
		if (i < grab_trigger)
		    continue;
	    } else {
		lastimg = malloc(width*height*3);
	    }
	    memcpy(lastimg,image,width*height*3);
	}

	/* ok, label it and upload */
	add_text(image,width,height);
	list_for_each(item,&connections) {
	    s = list_entry(item, struct xfer_state, list);
	    s->ops->xfer(s,image,width,height);
	}
	if (archive) {
	    time_t      t;
	    struct tm  *tm;

	    time(&t);
	    tm = localtime(&t);
	    strftime(filename,sizeof(filename)-1,archive,tm);
	again:
	    if (-1 == (fh = open(filename,O_CREAT|O_WRONLY|O_TRUNC,0666))) {
		if (ENOENT == errno) {
		    if (0 == make_dirs(filename))
			goto again;
		}
		fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
		exit(1);
	    }
	    write_file(fh, image, width, height);
	}

	if (-1 != grab_times && --grab_times == 0)
	    break;
	if (grab_delay > 0)
	    sleep(grab_delay);
    }
    list_for_each(item,&connections) {
	s = list_entry(item, struct xfer_state, list);
	s->ops->close(s);
    }
    return 0;
}
