#include "parse-mpeg.h"

/* print debug info? */
extern int dvb_debug;

extern char *dvb_src;
extern char *dvb_lnb;
extern char *dvb_sat;
extern int  dvb_inv;

/* open/close/probe/... */
struct dvb_state;
struct dvb_state* dvb_init(char *adapter);
struct dvb_state* dvb_init_nr(int adapter);
void dvb_fini(struct dvb_state *h);
char* dvb_devname(struct dvb_state *h);
struct ng_devinfo* dvb_probe(int debug);

/* high level interface */
int dvb_start_tune(struct dvb_state *h, int tsid, int pnr);
int dvb_start_tune_vdr(struct dvb_state *h, char *section);
int dvb_finish_tune(struct dvb_state *h, int timeout);

/* low level frontend interface */
int dvb_frontend_tune(struct dvb_state *h, char *domain, char *section);
void dvb_frontend_status_title(void);
void dvb_frontend_status_print(struct dvb_state *h);
int dvb_frontend_is_locked(struct dvb_state *h);
int dvb_frontend_wait_lock(struct dvb_state *h, int timeout);
int dvb_frontend_get_biterr(struct dvb_state *h);
int dvb_frontend_get_type(struct dvb_state *h);
int dvb_frontend_get_caps(struct dvb_state *h);
void dvb_frontend_release(struct dvb_state *h, int write);

/* low level demux interface */
void dvb_demux_filter_setup(struct dvb_state *h, int video, int audio);
int  dvb_demux_filter_apply(struct dvb_state *h);
void dvb_demux_filter_release(struct dvb_state *h);

int dvb_demux_req_section(struct dvb_state *h, int fd, int pid,
			  int sec, int mask, int oneshot, int timeout);
int dvb_demux_get_section(int fd, unsigned char *buf, int len);

int dvb_get_transponder_info(struct dvb_state *dvb,
			     struct psi_info *info,
			     int names, int verbose);
void dvb_print_program_info(char *prefix, struct psi_program *pr);
void dvb_print_transponder_info(struct psi_info *info);

/* vdr config data */
void vdr_import_stations(void);
