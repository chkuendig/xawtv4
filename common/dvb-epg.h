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
struct epgitem* eit_lookup(int tsid, int pnr, time_t when);
