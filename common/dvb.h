#include <inttypes.h>
#include <list.h>

/* -------------------------------------------------------- */
/* dvb-monitor.c                                            */

struct dvbmon;
struct psi_info;
struct dvb_state;

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

/* -------------------------------------------------------- */
/* dvb-epg.c                                                */

#define EPG_FLAG_AUDIO_MONO      (1<<0)
#define EPG_FLAG_AUDIO_STEREO    (1<<1)
#define EPG_FLAG_AUDIO_DUAL      (1<<2)
#define EPG_FLAG_AUDIO_MULTI     (1<<3)
#define EPG_FLAG_AUDIO_SURROUND  (1<<4)
#define EPG_FLAGS_AUDIO          (0xff)

#define EPG_FLAG_VIDEO_4_3       (1<< 8)
#define EPG_FLAG_VIDEO_16_9      (1<< 9)
#define EPG_FLAG_VIDEO_HDTV      (1<<10)
#define EPG_FLAGS_VIDEO          (0xff << 8)

#define EPG_FLAG_SUBTITLES       (1<<16)

struct epgitem {
    struct list_head    next;
    int                 id;
    int                 tsid;
    int                 pnr;
    int                 updated;
    time_t              start;       /* unix epoch */
    time_t              stop;
    char                lang[4];
    char                name[64];
    char                stext[128];
    char                *etext;
    char                *cat[4];
    uint64_t            flags;
};

extern struct list_head epg_list;
extern time_t eit_last_new_record;

struct eit_state;
struct eit_state* eit_add_watch(struct dvb_state *dvb,
				int section, int mask, int verbose, int alive);
void eit_del_watch(struct eit_state *eit);

#ifdef HAVE_DVB
extern struct epgitem* eit_lookup(int tsid, int pnr, time_t when, int debug);
#else
static inline struct epgitem* eit_lookup(int tsid, int pnr, time_t when,
					 int debug) { return NULL; }
#endif

/* -------------------------------------------------------- */
/* dvb-lang.c                                               */

