
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

#define O_OPTIONS		"options", "global"
#define O_EVENTS		"options", "eventmap"

/* bools */
#define O_KEYPAD_PARTIAL       	O_OPTIONS, "keypad-partial"
#define O_KEYPAD_NTSC		O_OPTIONS, "keypad-ntsc"
#define O_OSD			O_OPTIONS, "osd"
#define O_WM_FULLSCREEN		O_OPTIONS, "use-wm-fullscreen"
#define O_EXT_DGA		O_OPTIONS, "use-xext-dga"
#define O_EXT_XRANDR		O_OPTIONS, "use-xext-xrandr"
#define O_EXT_XVIDMODE		O_OPTIONS, "use-xext-vidmode"
#define O_EXT_XV_VIDEO		O_OPTIONS, "use-xext-xv-video"
#define O_EXT_XV_SCALE		O_OPTIONS, "use-xext-xv-scale"
#define O_EXT_OPENGL		O_OPTIONS, "use-xext-opengl"

#define GET_KEYPAD_PARTIAL()	cfg_get_bool(O_KEYPAD_PARTIAL,	1)
#define GET_KEYPAD_NTSC()	cfg_get_bool(O_KEYPAD_NTSC,	0)
#define GET_OSD()		cfg_get_bool(O_OSD,		1)
#define GET_WM_FILLSCREEN()	cfg_get_bool(O_WM_FULLSCREEN,	1)
#define GET_EXT_DGA()		cfg_get_bool(O_EXT_DGA,		1)
#define GET_EXT_XRANDR()	cfg_get_bool(O_EXT_XRANDR,	1)
#define GET_EXT_XVIDMODE()	cfg_get_bool(O_EXT_XVIDMODE,	1)
#define GET_EXT_XV_VIDEO()	cfg_get_bool(O_EXT_XV_VIDEO,   	1)
#define GET_EXT_XV_SCALE()	cfg_get_bool(O_EXT_XV_SCALE, 	1)
#define GET_EXT_OPENGL()       	cfg_get_bool(O_EXT_OPENGL,     	1)

/* ints */
#define O_RATIO_X		O_OPTIONS, "ratio-x"
#define O_RATIO_Y		O_OPTIONS, "ratio-y"
#define O_FS_WIDTH		O_OPTIONS, "fs-height"
#define O_FS_HEIGHT		O_OPTIONS, "fs-width"
#define O_JPEG_QUALITY		O_OPTIONS, "jpeg-quality"
#define O_OSD_X			O_OPTIONS, "osd-x"
#define O_OSD_Y			O_OPTIONS, "osd-y"
#define O_PIX_COLS		O_OPTIONS, "pix-cols"
#define O_PIX_WIDTH		O_OPTIONS, "pix-width"

#define GET_RATIO_X()		cfg_get_int(O_RATIO_X,		4)
#define GET_RATIO_Y()		cfg_get_int(O_RATIO_Y,		3)
#define GET_FS_WIDTH()		cfg_get_int(O_FS_WIDTH,		0)
#define GET_FS_HEIGHT()		cfg_get_int(O_FS_HEIGHT,       	0)
#define GET_JPEG_QUALITY()	cfg_get_int(O_JPEG_QUALITY,	75)
#define GET_OSD_X()		cfg_get_int(O_OSD_X,		30)
#define GET_OSD_Y()		cfg_get_int(O_OSD_Y,		20)
#define GET_PIX_COLS()		cfg_get_int(O_PIX_COLS,		1)
#define GET_PIX_WIDTH()		cfg_get_int(O_PIX_WIDTH,       	128)

/* strings */
#define O_INPUT			O_OPTIONS, "input"
#define O_TVNORM		O_OPTIONS, "norm"
#define O_FREQTAB		O_OPTIONS, "freqtab"
#define O_MOV_DRIVER		O_OPTIONS, "mov-driver"
#define O_MOV_VIDEO		O_OPTIONS, "mov-video"
#define O_MOV_FPS		O_OPTIONS, "mov-fps"
#define O_MOV_AUDIO		O_OPTIONS, "mov-audio"
#define O_MOV_RATE		O_OPTIONS, "mov-rate"
#define O_FILTER		O_OPTIONS, "filter"

