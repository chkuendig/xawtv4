#define _GNU_SOURCE
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "grab-ng.h"

#include "capture.h"
#include "commands.h"
#include "devs.h"
#include "writefile.h"
#include "tv-config.h"
#include "webcam.h"
#include "tuning.h"
#include "sound.h"
#include "parseconfig.h"

/* ----------------------------------------------------------------------- */

/* feedback for the user */
void (*update_title)(char *message);
void (*display_message)(char *message);
void (*rec_status)(char *message);
#ifdef HAVE_ZVBI
void (*vtx_subtitle)(struct vbi_page *pg, struct vbi_rect *rect);
#endif

/* for updating GUI elements / whatever */
void (*freqtab_notify)(void);
void (*setstation_notify)(void);

/* toggle fullscreen */
void (*fullscreen_hook)(void);
void (*exit_hook)(void);
void (*movie_hook)(int argc, char **argv);

int debug;
char *snapbase = "snap";
int have_shmem;

unsigned int cur_tv_width,cur_tv_height;
int cur_movie;

/* ----------------------------------------------------------------------- */

static int setfreqtab_handler(char *name, int argc, char **argv);
static int tuning_handler(char *name, int argc, char **argv);

static int volume_handler(char *name, int argc, char **argv);
static int attr_handler(char *name, int argc, char **argv);
static int show_handler(char *name, int argc, char **argv);
static int list_handler(char *name, int argc, char **argv);
static int dattr_handler(char *name, int argc, char **argv);

static int snap_handler(char *name, int argc, char **argv);
static int webcam_handler(char *name, int argc, char **argv);
static int movie_handler(char *name, int argc, char **argv);
static int fullscreen_handler(char *name, int argc, char **argv);
static int msg_handler(char *name, int argc, char **argv);
static int showtime_handler(char *name, int argc, char **argv);
static int vdr_handler(char *name, int argc, char **argv);
static int exit_handler(char *name, int argc, char **argv);

static int keypad_handler(char *name, int argc, char **argv);

static struct COMMANDS {
    char   *name;
    int    min_args;
    int    (*handler)(char *name, int argc, char **argv);
    int    queue;
} commands[] = {
    { "setstation", 0, tuning_handler,     1 },
    { "setchannel", 0, tuning_handler,     1 },
    { "setfreq",    1, tuning_handler,     1 },
    { "setfreqtab", 1, setfreqtab_handler, 0 },

    { "setnorm",    1, attr_handler,       1 },
    { "setinput",   1, attr_handler,       1 },
    { "setattr",    1, attr_handler,       1 },
    { "color",      0, attr_handler,       1 },
    { "hue",        0, attr_handler,       1 },
    { "bright",     0, attr_handler,       1 },
    { "contrast",   0, attr_handler,       1 },
    { "show",       0, show_handler,       0 },
    { "list",       0, list_handler,       0 },

    { "volume",     0, volume_handler,     0 },
    { "attr",       0, dattr_handler,      0 },

    { "snap",       0, snap_handler,       1 },
    { "webcam",     1, webcam_handler,     1 },
    { "movie",      1, movie_handler,      0 },
    { "fullscreen", 0, fullscreen_handler, 0 },
    { "msg",        1, msg_handler,        0 },
#if 0
    { "vtx",        0, vtx_handler,        0 },
#endif
    { "message",    0, msg_handler,        0 },
    { "exit",       0, exit_handler,       0 },
    { "quit",       0, exit_handler,       0 },
    { "bye",        0, exit_handler,       0 },

    { "keypad",     1, keypad_handler,     0 },
    { "showtime",   0, showtime_handler,   0 },
    { "vdr",        1, vdr_handler,        0 },

    { NULL, 0, NULL }
};

#if 0
static int cur_dattr = 0;
static int dattr[] = {
    ATTR_ID_VOLUME,
    ATTR_ID_BRIGHT,
    ATTR_ID_CONTRAST,
    ATTR_ID_COLOR,
    ATTR_ID_HUE
};
#define NUM_DATTR (sizeof(dattr)/sizeof(char*))
#endif

static int keypad_state = -1;

/* ----------------------------------------------------------------------- */

int command_pending;
int exit_application;
static LIST_HEAD(cmd_queue);

static void print_command(char *prefix, int argc, char **argv)
{
    int i;
    
    fprintf(stderr,"%s:",prefix);
    for (i = 0; i < argc; i++)
	fprintf(stderr," \"%s\"",argv[i]);
    fprintf(stderr,"\n");
}

static void
run_command(int argc, char **argv)
{
    int i;
    
    for (i = 0; commands[i].name != NULL; i++)
	if (0 == strcasecmp(commands[i].name,argv[0]))
	    break;
    if (debug)
	print_command("qexec",argc,argv);
    commands[i].handler(argv[0],argc-1,argv+1);
}

static void queue_add(int argc, char **argv)
{
    struct command *cmd;
    int i;

    cmd = malloc(sizeof(*cmd));
    memset(cmd,0,sizeof(*cmd));
    cmd->argc = argc;
    for (i = 0; i < argc; i++)
	cmd->argv[i] = strdup(argv[i]);
    list_add_tail(&cmd->next,&cmd_queue);
    command_pending++;
}

void queue_run(void)
{
    struct command *cmd;
    int i;

    if (debug) fprintf(stderr,"%s: enter\n",__FUNCTION__);
    for (;;) {
	if (list_empty(&cmd_queue)) {
	    command_pending = 0;
	    if (debug) fprintf(stderr,"%s: exit\n",__FUNCTION__);
	    return;
	}
	cmd = list_entry(cmd_queue.next, struct command, next);
	run_command(cmd->argc,cmd->argv);
	list_del(&cmd->next);
	for (i = 0; i < cmd->argc; i++)
	    free(cmd->argv[i]);
	free(cmd);
    }
}

/* ----------------------------------------------------------------------- */

void
do_va_cmd(int argc, ...)
{
    va_list ap;
    int  i;
    char *argv[CMD_ARGC_MAX];
    
    va_start(ap,argc);
    for (i = 0; i < argc; i++)
	argv[i] = va_arg(ap,char*);
    argv[i] = NULL;
    va_end (ap);
    do_command(argc,argv);
}

void
do_command(int argc, char **argv)
{
    int i;
    
    if (argc == 0) {
	fprintf(stderr,"do_command: no argument\n");
	return;
    }

    for (i = 0; commands[i].name != NULL; i++)
	if (0 == strcasecmp(commands[i].name,argv[0]))
	    break;
    if (commands[i].name == NULL) {
	fprintf(stderr,"no handler for %s\n",argv[0]);
	return;
    }
    if (argc-1 < commands[i].min_args) {
	fprintf(stderr,"no enough args for %s\n",argv[0]);
	return;
    }

    if (commands[i].queue) {
	if (debug)
	    print_command("queue",argc,argv);
	queue_add(argc,argv);
    } else {
	if (debug)
	    print_command("exec",argc,argv);
	commands[i].handler(argv[0],argc-1,argv+1);
    }
}

char**
split_cmdline(char *line, int *count)
{
    static char cmdline[1024];
    static char *argv[32];
    int  argc,i;

    strcpy(cmdline,line);
    for (argc=0, i=0; argc<31;) {
	argv[argc++] = cmdline+i;
	while (cmdline[i] != ' ' &&
	       cmdline[i] != '\t' &&
	       cmdline[i] != '\0')
	    i++;
	if (cmdline[i] == '\0')
	    break;
	cmdline[i++] = '\0';
	while (cmdline[i] == ' ' ||
	       cmdline[i] == '\t')
	    i++;
	if (cmdline[i] == '\0')
	    break;
    }
    argv[argc] = NULL;

    *count = argc;
    return argv;
}

/* ----------------------------------------------------------------------- */

static void
set_freqtab(char *name)
{
    freqtab_set(name);
    
    if (freqtab_notify)
	freqtab_notify();
}

static void
set_title(void)
{
    static char title[256];
    int len;
    
    keypad_state = -1;
    if (update_title) {
	if (NULL != curr_station) {
	    sprintf(title,"%s",curr_station);
	} else {
	    if (NULL != curr_channel) {
		len = sprintf(title,"channel %s",curr_channel);
	    } else {
		len = sprintf(title,"unknown");
	    }
	    if (strlen(curr_details)) {
		sprintf(title+len, " (%s)", curr_details);
	    }
	}
		
	update_title(title);
    }
}

static void
set_msg_int(struct ng_attribute *attr, int val)
{
    static char  title[256];
    
    if (display_message) {
	sprintf(title,"%s: %d%%",attr->name,
		ng_attr_int2percent(attr,val));
	display_message(title);
    }
}

static void
set_msg_bool(const char *name, int val)
{
    static char  title[256];
    
    if (display_message) {
	sprintf(title,"%s: %s",name, val ? "on" : "off");
	display_message(title);
    }
}

static void
set_msg_str(char *name, char *val)
{
    static char  title[256];
    
    if (display_message) {
	sprintf(title,"%s: %s",name,val);
	display_message(title);
    }
}

/* ----------------------------------------------------------------------- */

static int update_int(struct ng_attribute *attr, int old, char *new)
{
    int value = old;
    int step;

    step = (attr->max - attr->min) * 3 / 100;
    if (step == 0)
	step = 1;
    
    if (0 == strcasecmp(new,"inc"))
        value += step;
    else if (0 == strcasecmp(new,"dec"))
	value -= step;
    else if (0 == strncasecmp(new,"+=",2))
	value += ng_attr_parse_int(attr,new+2);
    else if (0 == strncasecmp(new,"-=",2))
	value -= ng_attr_parse_int(attr,new+2);
    else if (isdigit(new[0]) || '+' == new[0] || '-' == new[0])
	value = ng_attr_parse_int(attr,new);
    else
	fprintf(stderr,"update_int: can't parse %s\n",new);

    if (value < attr->min)
	value = attr->min;
    if (value > attr->max)
	value = attr->max;
    return value;
}

/* ----------------------------------------------------------------------- */

void
audio_on(void)
{
    attr_write_dev_id(&devs.video, ATTR_ID_MUTE, 0);
}

void
audio_off(void)
{
    attr_write_dev_id(&devs.video, ATTR_ID_MUTE, 1);
}

/* ----------------------------------------------------------------------- */

static int tuning_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *mute;
    char *newstation = NULL;
    char *newchannel = NULL;
    int newfreq = 0;
    char *list;
    int i,c,isnum;

    if (cur_movie) {
	if (display_message)
	    display_message("grabber busy");
	return -1;
    }
    if (debug) fprintf(stderr,"%s: enter\n",__FUNCTION__);

    if (0 == strcasecmp("setfreq",name))
	newfreq = (unsigned long)(atof(argv[0])*16);

    if (0 == strcasecmp("setchannel",name)) {
	if (NULL == freqtab) {
	    fprintf(stderr,"can't tune channels: freqtab not set (yet)\n");
	} else if (0 == strcasecmp(argv[0],"next")) {
	    if (curr_channel)
		newchannel = cfg_sections_next(freqtab,curr_channel);
	    if (NULL == newchannel)
		/* wrap around and no current channel */
		newchannel = cfg_sections_first(freqtab);
	} else if (0 == strcasecmp(argv[0],"prev") && curr_channel) {
	    newchannel = cfg_sections_prev(freqtab,curr_channel);
	} else {
	    newchannel = argv[0];
	}
	if (newchannel)
	    newstation = cfg_search("stations",NULL,"channel",newchannel);
    }

    if (0 == strcasecmp("setstation",name)) {
	if (0 == strcasecmp(argv[0],"next")) {
	    if (curr_station)
		newstation = cfg_sections_next("stations", curr_station);
	    if (NULL == newstation)
		/* wrap around and no current station */
		newstation = cfg_sections_first("stations");
	} else if (0 == strcasecmp(argv[0],"prev") && curr_station) {
	    newstation = cfg_sections_prev("stations", curr_station);
#if 0
	} else if (count && 0 == strcasecmp(argv[0],"back")) {
	    if (-1 == last_sender)
		return -1;
	    i = last_sender;
#endif
	}
	
	if (NULL == newstation)
	    /* search the configured channels first... */
	    newstation = cfg_search("stations",argv[0],NULL,NULL);
	
	if (NULL == newstation) {
	    /* ... next try using the argument as index ... */
	    for (c = 0, isnum = 1; argv[0][c] != 0; c++)
		if (!isdigit(argv[0][c]))
		    isnum = 0;
	    if (isnum) {
		i = atoi(argv[0]);
		newstation = cfg_sections_index("stations",i);
	    }
	}
	
	if (NULL == newstation) {
	    /* ... next try substring matches ... */
	    for (list  = cfg_sections_first("stations");
		 list != NULL;
		 list  = cfg_sections_next("stations",list)) {
		if (0 == strcasestr(list,argv[0]))
		    newstation = list;
	    }
	}
	
	if (NULL == newstation) {
	    /* ... sorry folks */
	    fprintf(stderr,"station \"%s\" not found\n",argv[0]);
	    return -1;
	}
    }
    
    /* preprocessing ... */
    mute = find_attr_by_id(ATTR_ID_MUTE);
    if (mute && attr_read(mute))
	attr_write(mute,1,0);
    else
	mute = NULL;

    /* ... switch ... */
    if (debug)
	fprintf(stderr,"%s: station=%s channel=%s freq=%d\n",
		__FUNCTION__,newstation,newchannel,newfreq);
    if (newstation) {
	tune_station(newstation);
    } else if (newchannel) {
	tune_analog_channel(newchannel);
    } else {
	tune_analog_freq(newfreq);
    }
    
    /* ... postprocessing */
    set_title();
    if (setstation_notify)
	setstation_notify();

    if (mute) {
	usleep(20000);
	attr_write(mute,0,0);
    }
    if (debug) fprintf(stderr,"%s: exit\n",__FUNCTION__);
    return 0;
}

/* ----------------------------------------------------------------------- */

#if 0
static void
print_choices(char *name, char *value, struct STRTAB *tab)
{
    int i;
    
    fprintf(stderr,"unknown %s: '%s' (available: ",name,value);
    for (i = 0; tab[i].str != NULL; i++)
	fprintf(stderr,"%s'%s'", (0 == i) ? "" : ", ", tab[i].str);
    fprintf(stderr,")\n");
}
#endif

static int setfreqtab_handler(char *name, int argc, char **argv)
{
    set_freqtab(argv[0]);
    return 0;
}

/* ----------------------------------------------------------------------- */

static int volume_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *avol,*amute;
    int mute = 0, volume = 0;

    avol   = find_attr_by_id(ATTR_ID_VOLUME);
    amute  = find_attr_by_id(ATTR_ID_MUTE);
    if (NULL == amute && NULL == avol)
	return 0;

    if (avol)
	volume = attr_read(avol);
    if (amute)
	mute   = attr_read(amute);
    
    if (0 == argc)
	goto display;
    
    if (0 == strcasecmp(argv[0],"mute")) {
	/* mute on/off/toggle */
	if (argc > 1) {
	    switch (str_to_int(argv[1],booltab)) {
	    case 0:  mute = 0;     break;
	    case 1:  mute = 1;     break;
	    default: mute = !mute; break;
	    }
	} else {
	    mute = !mute;
	}
    } else {
	/* volume */
	if (avol)
	    volume = update_int(avol, volume, argv[0]);
    }

    if (avol)
	attr_write(avol, volume, 1);
    if (amute)
	attr_write(amute, mute, 1);

 display:
    if (mute)
	set_msg_str("volume","muted");
    else {
	if (avol)
	    set_msg_int(avol,volume);
	else
	    set_msg_str("volume","unmuted");
    }
    return 0;
}

static int attr_handler(char *name, int argc, char **argv)
{
    struct ng_attribute *attr;
    int val,newval,arg=0;

    if (0 == strcasecmp(name,"setnorm")) {
	attr = find_attr_by_id(ATTR_ID_NORM);

    } else if (0 == strcasecmp(name,"setinput")) {
	attr = find_attr_by_id(ATTR_ID_INPUT);

    } else if (0 == strcasecmp(name,"setattr") &&
	       argc > 0) {
	attr = find_attr_by_name(argv[arg++]);

    } else {
	attr = find_attr_by_name(name);
    }
    
    if (NULL == attr) {
#if 0
	fprintf(stderr,"cmd: %s: attribute not found\nvalid choices are:",
		(arg > 0) ? argv[0] : name);
	for (attr = attrs; attr->name != NULL; attr++)
	    fprintf(stderr,"%s \"%s\"",
		    (attr != attrs) ? "," : "", attr->name);
	fprintf(stderr,"\n");
#endif
	return -1;
    }

    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	if (argc > arg) {
	    if (0 == strcasecmp("next", argv[arg])) {
		val = attr_read(attr);
		val++;
		if (NULL == attr->choices[val].str)
		    val = 0;
	    } else {
		val = ng_attr_getint(attr, argv[arg]);
	    }
	    if (-1 == val) {
		fprintf(stderr,"invalid value for %s: %s\n",attr->name,argv[arg]);
		ng_attr_listchoices(attr);
	    } else {
		attr_write(attr,val,1);
	    }
	}
	break;
    case ATTR_TYPE_INTEGER:
	val = attr_read(attr);
	if (argc > arg) {
	    val = update_int(attr,val,argv[arg]);
	    attr_write(attr,val,1);
	}
	set_msg_int(attr,val);
	break;
    case ATTR_TYPE_BOOL:
	val = attr_read(attr);
	if (argc > arg) {
	    newval = str_to_int(argv[arg],booltab);
	    if (-1 == newval) {
		if (0 == strcasecmp(argv[arg],"toggle"))
		    newval = !val;
	    }
	    val = newval;
	    attr_write(attr,val,1);
	}
	set_msg_bool(attr->name,val);
	break;
    }
    return 0;
}

static int show_handler(char *name, int argc, char **argv)
{
#if 0 /* FIXME */
    struct ng_attribute *attr;
    char *n[2] = { NULL, NULL };
    int val;

    if (0 == argc) {
	for (attr = attrs; attr->name != NULL; attr++) {
	    n[0] = (char*)attr->name;
	    show_handler("show", 1, n);
	}
	return 0;
    }

    attr = ng_attr_byname(attrs,argv[0]);
    if (NULL == attr) {
	fprintf(stderr,"fixme: 404 %s\n",argv[0]);
	return 0;
    }
    val = cur_attrs[attr->id];
    switch (attr->type) {
    case ATTR_TYPE_CHOICE:
	printf("%s: %s\n", attr->name, ng_attr_getstr(attr,val));
	break;
    case ATTR_TYPE_INTEGER:
	printf("%s: %d\n", attr->name, val);
	break;
    case ATTR_TYPE_BOOL:
	printf("%s: %s\n", attr->name, val ? "on" : "off");
	break;
    }
#endif
    return 0;
}

static int list_handler(char *name, int argc, char **argv)
{
#if 0 /* FIXME */
    struct ng_attribute *attr;
    int val,i;

    printf("%-10.10s | type   | %-7.7s | %-7.7s | %s\n",
	   "attribute","current","default","comment");
    printf("-----------+--------+---------+--------"
	   "-+-------------------------------------\n");
    for (attr = attrs; attr->name != NULL; attr++) {
	val = cur_attrs[attr->id];
	switch (attr->type) {
	case ATTR_TYPE_CHOICE:
	    printf("%-10.10s | choice | %-7.7s | %-7.7s |",
		   attr->name,
		   ng_attr_getstr(attr,val),
		   ng_attr_getstr(attr,attr->defval));
	    for (i = 0; attr->choices[i].str != NULL; i++)
		printf(" %s",attr->choices[i].str);
	    printf("\n");
	    break;
	case ATTR_TYPE_INTEGER:
	    printf("%-10.10s | int    | %7d | %7d | range is %d => %d\n",
		   attr->name, val, attr->defval,
		   attr->min, attr->max);
	    break;
	case ATTR_TYPE_BOOL:
	    printf("%-10.10s | bool   | %-7.7s | %-7.7s |\n",
		   attr->name,
		   val ? "on" : "off",
		   attr->defval ? "on" : "off");
	    break;
	}
    }
#endif
    return 0;
}

static int dattr_handler(char *name, int argc, char **argv)
{
#if 0 /* FIXME */
    struct ng_attribute *attr = NULL;
    unsigned int i;
    
    if (argc > 0 && 0 == strcasecmp(argv[0],"next")) {
	for (i = 0; i < NUM_DATTR; i++) {
	    cur_dattr++;
	    cur_dattr %= NUM_DATTR;
	    attr = ng_attr_byid(attrs,dattr[cur_dattr]);
	    if (NULL != attr)
		break;
	}
	if (NULL == attr)
	    return 0;
	argc = 0;
    }
    if (NULL == attr)
	attr = ng_attr_byid(attrs,dattr[cur_dattr]);
    if (NULL == attr)
	return 0;
    return attr_handler((char*)attr->name,argc,argv);
#else
    return 0;
#endif
}

/* ----------------------------------------------------------------------- */

static int snap_handler(char *hname, int argc, char **argv)
{
    char message[512];
    char *tmpfilename = NULL;
    char *filename = NULL;
    char *name;
    int   jpeg = 0;
    int   ret = 0;
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf = NULL;

    if (!(devs.video.flags & CAN_CAPTURE)) {
	fprintf(stderr,"grabbing: not supported\n");
	return -1;
    }

    if (cur_movie) {
	if (display_message)
	    display_message("grabber busy");
	return -1;
    }

    /* format */
    if (argc > 0) {
	if (0 == strcasecmp(argv[0],"jpeg"))
	    jpeg = 1;
	if (0 == strcasecmp(argv[0],"ppm"))
	    jpeg = 0;
    }

    /* size */
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = 2048;
    fmt.height = 1572;
    if (argc > 1) {
	if (0 == strcasecmp(argv[1],"full")) {
	    /* nothing */
	} else if (0 == strcasecmp(argv[1],"win")) {
	    fmt.width  = cur_tv_width;
	    fmt.height = cur_tv_height;
	} else if (2 == sscanf(argv[1],"%dx%d",&fmt.width,&fmt.height)) {
	    /* nothing */
	} else {
	    return -1;
	}
    }

    /* filename */
    if (argc > 2)
	filename = argv[2];
    
    if (NULL == (buf = ng_grabber_get_image(&devs.video, &fmt))) {
	if (display_message)
	    display_message("grabbing failed");
	ret = -1;
	goto done;
    }
    buf = ng_filter_single(cur_filter,buf);

    if (NULL == filename) {
	if (NULL != curr_station) {
	    name = curr_station;
	} else if (NULL != curr_channel) {
	    name = curr_channel;
	} else {
	    name = "unknown";
	}
	filename = snap_filename(snapbase, name, jpeg ? "jpeg" : "ppm");
    }
    tmpfilename = malloc(strlen(filename)+8);
    sprintf(tmpfilename,"%s.$$$",filename);

    if (jpeg) {
	if (-1 == write_jpeg(tmpfilename, buf, ng_jpeg_quality, 0)) {
	    snprintf(message,sizeof(message),"open %s: %s\n",
		     tmpfilename,strerror(errno));
	} else {
	    snprintf(message,sizeof(message),"saved jpeg: %s",
		     filename);
	}
    } else {
	if (-1 == write_ppm(tmpfilename, buf)) {
	    snprintf(message,sizeof(message),"open %s: %s\n",
		     tmpfilename,strerror(errno));
	} else {
	    snprintf(message,sizeof(message),"saved ppm: %s",
		     filename);
	}
    }
    unlink(filename);
    if (-1 == link(tmpfilename,filename)) {
	fprintf(stderr,"link(%s,%s): %s\n",
		tmpfilename,filename,strerror(errno));
	goto done;
    }
    unlink(tmpfilename);
    if (display_message)
	display_message(message);

done:
    if (tmpfilename)
	free(tmpfilename);
    if (NULL != buf)
	ng_release_video_buf(buf);
    return ret;
}

static int webcam_handler(char *hname, int argc, char **argv)
{
#if 0 /* FIXME */
    struct ng_video_fmt fmt;
    struct ng_video_buf *buf;

    if (webcam)
	free(webcam);
    webcam = strdup(argv[0]);

    /* if either avi recording or grabdisplay is active, we do
       /not/ stop capture and switch the video format.  The next
       capture will send a copy of the frame to the webcam thread
       and it has to deal with it as-is */
    if (cur_movie)
	return 0;
    if (cur_capture == CAPTURE_GRABDISPLAY)
	return 0;

    /* if no capture is running we can switch to RGB first to make
       the webcam happy */
    memset(&fmt,0,sizeof(fmt));
    fmt.fmtid  = VIDEO_RGB24;
    fmt.width  = cur_tv_width;
    fmt.height = cur_tv_height;
    buf = ng_grabber_get_image(&devs.video, &fmt);
    if (buf)
	ng_release_video_buf(buf);
#endif
    return 0;
}

static int movie_handler(char *name, int argc, char **argv)
{
    if (!movie_hook)
	return 0;
    movie_hook(argc,argv);
    return 0;
}

static int
fullscreen_handler(char *name, int argc, char **argv)
{
    if (fullscreen_hook)
	fullscreen_hook();
    return 0;
}

static int
msg_handler(char *name, int argc, char **argv)
{
    if (display_message)
	display_message(argv[0]);
    return 0;
}

static int
showtime_handler(char *name, int argc, char **argv)
{
    char timestr[6];
    struct tm *times;
    time_t timet;

    timet = time(NULL);
    times = localtime(&timet);
    strftime(timestr, 6, "%k:%M", times);
    if (display_message)
	display_message(timestr);
    return 0;
}

static int
exit_handler(char *name, int argc, char **argv)
{
    if (exit_hook)
	exit_hook();
    return 0;
}

/* ----------------------------------------------------------------------- */

static char *strfamily(int family)
{
    switch (family) {
    case PF_INET6: return "ipv6";
    case PF_INET:  return "ipv4";
    case PF_UNIX:  return "unix";
    }
    return "????";
}

static int
tcp_connect(struct addrinfo *ai, char *host, char *serv)
{
    struct addrinfo *res,*e;
    char uhost[INET6_ADDRSTRLEN+1];
    char userv[33];
    int sock,rc,opt=1;

    ai->ai_flags = AI_CANONNAME;
    if (debug)
	fprintf(stderr,"tcp: lookup %s:%s ... ",host,serv);
    if (0 != (rc = getaddrinfo(host, serv, ai, &res))) {
	fprintf(stderr,"tcp: getaddrinfo (%s:%s): %s\n",
		host,serv,gai_strerror(rc));
	return -1;
    }
    if (debug)
	fprintf(stderr,"ok\n");
    for (e = res; e != NULL; e = e->ai_next) {
	if (0 != getnameinfo((struct sockaddr*)e->ai_addr,e->ai_addrlen,
			     uhost,INET6_ADDRSTRLEN,userv,32,
			     NI_NUMERICHOST | NI_NUMERICSERV)) {
	    fprintf(stderr,"tcp: getnameinfo (peer): oops\n");
	    continue;
	}
	if (debug)
	    fprintf(stderr,"tcp: trying %s (%s:%s) ... ",
		    strfamily(e->ai_family),uhost,userv);
	if (-1 == (sock = socket(e->ai_family, e->ai_socktype,
				 e->ai_protocol))) {
	    fprintf(stderr,"tcp: socket: %s\n",strerror(errno));
	    continue;
	}
        setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
	if (-1 == connect(sock,e->ai_addr,e->ai_addrlen)) {
	    fprintf(stderr,"tcp: connect: %s\n",strerror(errno));
	    close(sock);
	    continue;
	}
	if (debug)
	    fprintf(stderr,"ok\n");
	fcntl(sock,F_SETFL,O_NONBLOCK);
	fcntl(sock,F_SETFD,FD_CLOEXEC);
	return sock;
    }
    return -1;
}

static int tcp_readbuf(int sock, int timeout, char *dest, char dlen)
{
    struct timeval tv;
    fd_set set;
    int rc;

 again:
    FD_ZERO(&set);
    FD_SET(sock,&set);
    tv.tv_sec  = timeout;
    tv.tv_usec = 0;
    rc = select(sock+1,&set,NULL,NULL,&tv);
    if (-1 == rc && EINTR == errno)
	goto again;
    if (-1 == rc) {
	if (debug)
	    perror("tcp: select");
	return -1;
    }
    if (0 == rc) {
	if (debug)
	    fprintf(stderr,"tcp: select timeout\n");
	return -1;
    }
    rc = read(sock,dest,dlen-1);
    if (-1 == rc) {
	if (debug)
	    perror("tcp: read");
	return -1;
    }
    dest[rc] = 0;
    return rc;
}

static int vdr_sock = -1;

static int
vdr_handler(char *name, int argc, char **argv)
{
    char line[80];
    struct addrinfo ask;
    int i,rc;
    unsigned int l,len;

 reconnect:
    if (-1 == vdr_sock) {
	memset(&ask,0,sizeof(ask));
	ask.ai_family = PF_UNSPEC;
	ask.ai_socktype = SOCK_STREAM;
	vdr_sock = tcp_connect(&ask,"localhost","2001");
	if (-1 == vdr_sock)
	    return -1;
	if (debug)
	    fprintf(stderr,"vdr: connected\n");

	/* skip greeting line */
	if (-1 == tcp_readbuf(vdr_sock,3,line,sizeof(line)))
	    goto oops;
	if (debug)
	    fprintf(stderr,"vdr: << %s",line);
    }

    /* send command */
    line[0] = 0;
    for (i = 0, len = 0; i < argc; i++) {
	l = strlen(argv[i]);
	if (len+l+4 > sizeof(line))
	    break;
	if (len) {
	    strcpy(line+len," ");
	    len++;
	}
	strcpy(line+len,argv[i]);
	len += l;
    }
    strcpy(line+len,"\r\n");
    len += 2;
    if (len != (rc = write(vdr_sock,line,len))) {
 	if (-1 == rc  &&  EPIPE == errno) {
	    if (debug)
		fprintf(stderr,"tcp: write: broken pipe, trying reconnect\n");
	    close(vdr_sock);
	    vdr_sock = -1;
	    goto reconnect;
	}
	if (debug)
	    perror("tcp: write");
	goto oops;
    }
    if (debug)
	fprintf(stderr,"vdr: >> %s",line);
    
    /* skip answer */
    if (-1 == tcp_readbuf(vdr_sock,3,line,sizeof(line)))
	goto oops;
    if (debug)
	fprintf(stderr,"vdr: << %s",line);

#if 0
    /* play nicely and close the handle -- vdr can handle only one
     * connection at the same time.  Drawback is that it increases
     * latencies ... */
    close(vdr_sock);
    vdr_sock = -1;
#endif
    return 0;

oops:
    close(vdr_sock);
    vdr_sock = -1;
    return -1;
}

/* ----------------------------------------------------------------------- */

#define CH_MAX (keypad_ntsc ? 99 : count)

static int
keypad_handler(char *name, int argc, char **argv)
{
    int keypad_partial = cfg_get_bool("options","global","keypad-partial", 1);
    int keypad_ntsc    = cfg_get_bool("options","global","keypad-ntsc",    0);
    int count = cfg_sections_count("stations");
    int n = atoi(argv[0])%10;
    char msg[8],ch[8];

    if (debug)
	fprintf(stderr,"keypad: key %d\n",n);
    if (-1 == keypad_state) {
	if ((keypad_partial   &&  n > 0 && n <= CH_MAX) ||
	    (!keypad_partial  &&  n > 0 && n <= CH_MAX && 10*n > CH_MAX)) {
	    sprintf(ch,"%d",n);
	    if (keypad_ntsc) {
		do_va_cmd(2,"setchannel",ch,NULL);
	    } else {
		do_va_cmd(2,"setstation",ch,NULL);
	    }
	}
	if (n >= 0 && 10*n <= CH_MAX) {
	    if (debug)
		fprintf(stderr,"keypad: hang: %d\n",n);
	    keypad_state = n;
	    if (display_message) {
		sprintf(msg,"%d_",n);
		display_message(msg);
	    }
	}
    } else {
	if ((n+keypad_state*10) <= CH_MAX)
	    n += keypad_state*10;
	keypad_state = -1;
	if (debug)
	    fprintf(stderr,"keypad: ok: %d\n",n);
	if (n > 0 && n <= CH_MAX) {
	    sprintf(ch,"%d",n);
	    if (keypad_ntsc) {
		do_va_cmd(2,"setchannel",ch,NULL);
	    } else {
		do_va_cmd(2,"setstation",ch,NULL);
	    }
	}
    }
    return 0;
}

void
keypad_timeout(void)
{
    if (debug)
	fprintf(stderr,"keypad: timeout\n");
    keypad_state = -1;
    set_title();
}
