#include "parse-mpeg.h"

/* open/close */
struct dvb_state;
struct dvb_state* dvb_init(int adapter, int frontend, int demux);
void dvb_fini(struct dvb_state *h);

/* high level interface */
int  dvb_tune(struct dvb_state *h, char *name);

/* low level frontend interface */
int dvb_frontend_tune(struct dvb_state *h, char *name);
void dvb_frontend_status_title(void);
void dvb_frontend_status_print(struct dvb_state *h);
int dvb_frontend_is_locked(struct dvb_state *h);
int dvb_frontend_wait_lock(struct dvb_state *h, time_t timeout);
void dvb_frontend_release(struct dvb_state *h);

/* low level demux interface */
int dvb_demux_station_filter(struct dvb_state *h, char *name);
void dvb_demux_station_release(struct dvb_state *h);

int dvb_demux_req_section(struct dvb_state *h, int pid, int sec, int oneshot);
int dvb_demux_get_section(int *fd, unsigned char *buf, int len, int oneshot);

int dvb_get_transponder_info(struct dvb_state *dvb,
			     struct psi_info *info,
			     int names, int verbose);
void dvb_print_program_info(char *prefix, struct psi_program *pr);
void dvb_print_transponder_info(struct psi_info *info);
