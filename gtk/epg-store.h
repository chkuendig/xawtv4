#ifndef _epg_store_h_included_
#define _epg_store_h_included_

#include <gtk/gtk.h>

/* Some boilerplate GObject defines */

#define EPG_STORE_TYPE            (epg_store_get_type ())
#define EPG_STORE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EPG_STORE_TYPE, EpgStore))
#define EPG_STORE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass),  EPG_STORE_TYPE, EpgStoreClass))
#define IS_EPG_STORE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EPG_STORE_TYPE))
#define IS_EPG_STORE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass),  EPG_STORE_TYPE))
#define EPG_STORE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),  EPG_STORE_TYPE, EpgStoreClass))

typedef struct _EpgStore       EpgStore;
typedef struct _EpgStoreClass  EpgStoreClass;

struct _EpgStoreClass {
    GObjectClass parent_class;
};
GType     epg_store_get_type (void);

/* here is our stuff ... */

enum epg_filter {
    EPG_FILTER_NOFILTER,
    EPG_FILTER_NOW,
    EPG_FILTER_NEXT,
    EPG_FILTER_STATION,
    EPG_FILTER_TEXT,
};

enum epg_cols {
    /* strings */
    EPG_COL_DATE,
    EPG_COL_START,
    EPG_COL_STOP,
    EPG_COL_STATION,
    EPG_COL_NAME,
    EPG_COL_LANG,

    /* ints */
    EPG_COL_TSID,
    EPG_COL_PNR,

    /* bool */
    EPG_COL_PLAYING,

    /* ptr */
    EPG_COL_EPGITEM,
    EPG_N_COLUMNS,
};

struct _EpgStore {
    GObject         parent;      /* this MUST be the first member */
    guint           num_rows;    /* number of rows that we have   */
    struct epgitem  **rows;

    enum epg_filter filter_type;
    int filter_tsid;
    int filter_pnr;
};

EpgStore  *epg_store_new(void);
void epg_store_refresh(EpgStore *st);
void epg_store_sort(EpgStore *st);
void epg_store_set_filter(EpgStore *st, enum epg_filter type);
void epg_store_set_station(EpgStore *st, int tsid, int pnr);

#endif /* _epg_store_h_included_ */
