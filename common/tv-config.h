
enum display_mode {
    DISPLAY_NONE    = 0,  // nothing
    DISPLAY_OVERLAY = 1,  // overlay (pcipci dma grabber => vga)
    DISPLAY_GRAB    = 2,  // grabdisplay (capture from grabber => vga)
    DISPLAY_DVB     = 3,  // DVB playback (software mpeg2 decoding)
    DISPLAY_FILE    = 4,  // movie file playback
};

enum tuning_mode {
    TUNING_NONE     = 0,  // nothing
    TUNING_ANALOG   = 1,
    TUNING_DVB      = 2,
};

extern enum display_mode display_mode;
extern enum tuning_mode  tuning_mode;

extern struct cfg_cmdline cmd_opts_x11[];

/* ----------------------------------------------------------------------- */

extern struct ng_video_filter *cur_filter;

int read_config_file(char *name);
int write_config_file(char *name);

void read_config(void);
void apply_config(void);

/* ----------------------------------------------------------------------- */

extern struct STRTAB booltab[];

int str_to_int(char *str, struct STRTAB *tab);
const char* int_to_str(int n, struct STRTAB *tab);

/* ----------------------------------------------------------------------- */

/* sections */
#define O_OPTIONS		"options", "global"
#define O_RECORD		"options", "record"
#define O_EVENTS		"options", "eventmap"

/* option bools */
#define O_KEYPAD_PARTIAL       	O_OPTIONS, "keypad-partial"
#define O_KEYPAD_NTSC		O_OPTIONS, "keypad-ntsc"
#define O_OSD			O_OPTIONS, "osd"
#define O_WM_FULLSCREEN		O_OPTIONS, "use-wm-fullscreen"
#define O_EXT_XRANDR		O_OPTIONS, "use-xext-xrandr"
#define O_EXT_XVIDMODE		O_OPTIONS, "use-xext-vidmode"
#define O_EXT_XVIDEO		O_OPTIONS, "use-xext-xvideo"
#define O_EXT_OPENGL		O_OPTIONS, "use-xext-opengl"

#define GET_KEYPAD_PARTIAL()	cfg_get_bool(O_KEYPAD_PARTIAL,	1)
#define GET_KEYPAD_NTSC()	cfg_get_bool(O_KEYPAD_NTSC,	0)
#define GET_OSD()		cfg_get_bool(O_OSD,		1)
#define GET_WM_FILLSCREEN()	cfg_get_bool(O_WM_FULLSCREEN,	1)
#define GET_EXT_DGA()		cfg_get_bool(O_EXT_DGA,		1)
#define GET_EXT_XRANDR()	cfg_get_bool(O_EXT_XRANDR,	1)
#define GET_EXT_XVIDMODE()	cfg_get_bool(O_EXT_XVIDMODE,	1)
#define GET_EXT_XVIDEO()	cfg_get_bool(O_EXT_XVIDEO,   	1)
#define GET_EXT_OPENGL()       	cfg_get_bool(O_EXT_OPENGL,     	1)

/* option ints */
#define O_RATIO_X		O_OPTIONS, "ratio-x"
#define O_RATIO_Y		O_OPTIONS, "ratio-y"
#define O_JPEG_QUALITY		O_OPTIONS, "jpeg-quality"
#define O_OSD_X			O_OPTIONS, "osd-x"
#define O_OSD_Y			O_OPTIONS, "osd-y"
#define O_PIX_COLS		O_OPTIONS, "pix-cols"
#define O_PIX_WIDTH		O_OPTIONS, "pix-width"

#define GET_RATIO_X()		cfg_get_int(O_RATIO_X,		4)
#define GET_RATIO_Y()		cfg_get_int(O_RATIO_Y,		3)
#define GET_JPEG_QUALITY()	cfg_get_int(O_JPEG_QUALITY,	75)
#define GET_OSD_X()		cfg_get_int(O_OSD_X,		30)
#define GET_OSD_Y()		cfg_get_int(O_OSD_Y,		20)
#define GET_PIX_COLS()		cfg_get_int(O_PIX_COLS,		1)
#define GET_PIX_WIDTH()		cfg_get_int(O_PIX_WIDTH,       	128)

/* option strings */
#define O_FREQTAB		O_OPTIONS, "freqtab"
#define O_FILTER		O_OPTIONS, "filter"

/* FIXME: global? per device? */
#define O_INPUT			O_OPTIONS, "input"
#define O_TVNORM		O_OPTIONS, "norm"

/* recording parameters */
#define O_REC_DRIVER		O_RECORD, "driver"
#define O_REC_VIDEO		O_RECORD, "video"
#define O_REC_AUDIO		O_RECORD, "audio"
#define O_REC_FPS		O_RECORD, "fps"
#define O_REC_RATE		O_RECORD, "rate"
#define O_REC_BUFCOUNT		O_RECORD, "bufcount"
#define O_REC_THREADS		O_RECORD, "threads"

#define GET_REC_FPS()		cfg_get_int(O_REC_FPS,		10)
#define GET_REC_RATE()		cfg_get_int(O_REC_RATE,		44100)
#define GET_REC_BUFCOUNT()     	cfg_get_int(O_REC_BUFCOUNT,    	16)
#define GET_REC_THREADS()      	cfg_get_int(O_REC_THREADS,     	1)
