#include "vbi-data.h"

#define TT 0
#if TT
#define VTX_COUNT 256
#define VTX_LEN   64

struct TEXTELEM {
    char  str[VTX_LEN];
    char  *fg;
    char  *bg;
    int   len;
    int   line;
    int   x,y;
};
#endif

#define CMD_ARGC_MAX 8
struct command {
    int               argc;
    char              *argv[CMD_ARGC_MAX];
    struct list_head  next;
};
extern int command_pending;
extern int exit_application;
void queue_run(void);

/*------------------------------------------------------------------------*/

/* feedback for the user */
extern void (*update_title)(char *message);
extern void (*display_message)(char *message);
extern void (*rec_status)(char *message);
#if TT
extern void (*vtx_message)(struct TEXTELEM *tt);
#endif
#ifdef HAVE_ZVBI
extern void (*vtx_subtitle)(struct vbi_page *pg, struct vbi_rect *rect);
#endif

/* for updating GUI elements / whatever */
extern void (*volume_notify)(void);
extern void (*freqtab_notify)(void);
extern void (*setstation_notify)(void);

/* capture overlay/grab/off */
extern void (*set_capture_hook)(int old, int new, int tmp_switch);

/* toggle fullscreen */
extern void (*fullscreen_hook)(void);
extern void (*exit_hook)(void);
extern void (*movie_hook)(int argc, char **argv);

extern int debug;
extern char *snapbase;
extern int have_shmem;
extern unsigned int cur_tv_width,cur_tv_height;
extern int cur_movie;
extern struct movie_parm m_parm;

/*------------------------------------------------------------------------*/

void audio_on(void);
void audio_off(void);

void do_va_cmd(int argc, ...);
void do_command(int argc, char **argv);
char** split_cmdline(char *line, int *count);
void keypad_timeout(void);
