/*
 * MPEG1/2 transport and program stream parser and demuxer code.
 *
 * (c) 2003 Gerd Knorr <kraxel@bytesex.org>
 *
 */

#include <inttypes.h>

#define TS_SIZE                   188

extern int mpeg_rate_n[16];
extern int mpeg_rate_d[16];
extern const char *mpeg_frame_s[];

/* ----------------------------------------------------------------------- */

#define PSI_PROGS  16
#define PSI_NEW    42  // initial version, valid range is 0 ... 32

struct psi_program {
    int                  id;
    int                  version;

    /* program data */
    int                  p_pid;             // program
    int                  v_pid;             // video
    int                  a_pid;             // audio
    int                  t_pid;             // teletext
    int                  type;
    char                 net[64];
    char                 name[64];

    /* status info */
    int                  modified;
    int                  seen;
    int                  pmt_fd;
};

struct psi_info {
    int                  id;
    int                  pat_version;
    int                  sdt_version;

    /* program map */
    int                  n_pid;             // network
    struct psi_program   progs[PSI_PROGS];  // programs

    /* status info */
    int                  modified;
    int                  pat_fd;
    int                  sdt_fd;
};

/* ----------------------------------------------------------------------- */

struct ts_packet {
    unsigned int   pid;
    unsigned int   cont;

    unsigned int   tei       :1;
    unsigned int   payload   :1;
    unsigned int   scramble  :2;
    unsigned int   adapt     :2;
    
    unsigned char  *data;
    unsigned int   size;
};

struct psc_info {
    int                   temp_ref;
    enum ng_video_frame   frame;
    uint64_t              pts;
    int                   gop_seen;
    int                   dec_seq;
    int                   play_seq;
};

struct mpeg_handle {
    int                   fd;
    
    /* file buffer */
    int                   pgsize;
    unsigned char         *buffer;
    off_t                 boff;
    size_t                bsize;
    size_t                balloc;
    int                   beof;
    int                   slowdown;

    /* error stats */
    int                   errors;
    int                   error_out;

    /* libng format info */
    struct ng_video_fmt   vfmt;
    struct ng_audio_fmt   afmt;
    int                   rate, ratio;

    /* video frame fifo */
    struct list_head      vfifo;
    struct ng_video_buf   *vbuf;

    /* TS packet / PIDs */
    struct ts_packet      ts;
    int                   p_pid;
    int                   v_pid;
    int                   a_pid;

    /* parser state */
    int                   init;
    uint64_t              start_pts;
    uint64_t              video_pts;
    uint64_t              video_pts_last;
    uint64_t              audio_pts;
    uint64_t              audio_pts_last;
    off_t                 video_offset;
    off_t                 audio_offset;
    off_t                 init_offset;

    int                   frames;
    int                   gop_seen;
    int                   psc_seen;
    struct psc_info       psc;      /* current picture */
    struct psc_info       pts_ref;
    struct psc_info       gop_ref;
};

/* ----------------------------------------------------------------------- */

/* misc */
void hexdump(char *prefix, unsigned char *data, size_t size);

/* common */
unsigned int mpeg_getbits(unsigned char *buf, int start, int count);

struct mpeg_handle* mpeg_init(void);
void mpeg_fini(struct mpeg_handle *h);

unsigned char* mpeg_get_data(struct mpeg_handle *h, off_t pos, size_t size);
size_t mpeg_parse_pes_packet(struct mpeg_handle *h, unsigned char *packet,
			     uint64_t *ts, int *al);
int mpeg_get_audio_rate(unsigned char *header);
int mpeg_get_video_fmt(struct mpeg_handle *h, unsigned char *header);

/* program stream */
size_t mpeg_find_ps_packet(struct mpeg_handle *h, int packet, off_t *pos);

/* transport stream */
int mpeg_parse_psi_pat(struct psi_info *info, unsigned char *data, int verbose);
int mpeg_parse_psi_pmt(struct psi_program *program, unsigned char *data, int verbose);
int mpeg_parse_psi_sdt(struct psi_info *info, unsigned char *data, int verbose);
int mpeg_parse_psi(struct psi_info *info, struct mpeg_handle *h, int verbose);
int mpeg_find_ts_packet(struct mpeg_handle *h, int wanted, off_t *pos);
