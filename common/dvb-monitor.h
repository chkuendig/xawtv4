struct dvbmon;

typedef void (*dvbmon_notify)(struct psi_info *info, int event,
			      int tsid, int pnr, void *data);
#define DVBMON_EVENT_SWITCH_TS    1
#define DVBMON_EVENT_UPDATE_TS    2
#define DVBMON_EVENT_UPDATE_PR    3
#define DVBMON_EVENT_DESTROY     99

struct dvbmon* dvbmon_init(struct dvb_state *dvb, int verbose,
			   int others, int pmts);
void dvbmon_fini(struct dvbmon* dm);
void dvbmon_refresh(struct dvbmon* dm);
void dvbmon_add_callback(struct dvbmon* dm, dvbmon_notify func, void *data);
void dvbmon_del_callback(struct dvbmon* dm, dvbmon_notify func, void *data);

void dvbwatch_logger(struct psi_info *info, int event,
		     int tsid, int pnr, void *data);
void dvbwatch_scanner(struct psi_info *info, int event,
		      int tsid, int pnr, void *data);
