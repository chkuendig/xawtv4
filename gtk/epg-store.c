#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "grab-ng.h"
#include "epg-store.h"
#include "list.h"
#include "parseconfig.h"
#include "dvb.h"

/* boring declarations of local functions */

static void         epg_store_init            (EpgStore      *pkg_tree);
static void         epg_store_class_init      (EpgStoreClass *klass);
static void         epg_store_tree_model_init (GtkTreeModelIface *iface);
static void         epg_store_finalize        (GObject           *object);
static GtkTreeModelFlags epg_store_get_flags  (GtkTreeModel      *tree_model);
static gint         epg_store_get_n_columns   (GtkTreeModel      *tree_model);
static GType        epg_store_get_column_type (GtkTreeModel      *tree_model,
					       gint               index);
static gboolean     epg_store_get_iter        (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter,
					       GtkTreePath       *path);
static GtkTreePath *epg_store_get_path        (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter);
static void         epg_store_get_value       (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter,
					       gint               column,
					       GValue            *value);
static gboolean     epg_store_iter_next       (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter);
static gboolean     epg_store_iter_children   (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter,
					       GtkTreeIter       *parent);
static gboolean     epg_store_iter_has_child  (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter);
static gint         epg_store_iter_n_children (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter);
static gboolean     epg_store_iter_nth_child  (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter,
					       GtkTreeIter       *parent,
					       gint               n);
static gboolean     epg_store_iter_parent     (GtkTreeModel      *tree_model,
					       GtkTreeIter       *iter,
					       GtkTreeIter       *child);

static GObjectClass *parent_class = NULL;

/* ------------------------------------------------------------------------- */

struct station {
    struct list_head  next;
    int               tsid;
    int               pnr;
    char              name[64];
    int               visible;
};

struct _EpgStore {
    GObject           parent;      /* this MUST be the first member */
    guint             num_rows;    /* number of rows that we have   */
    struct epgitem    **rows;

    enum epg_filter   filter_type;
    int               filter_tsid;
    int               filter_pnr;
    char              *filter_text;

    struct list_head  stations;
};

/* ------------------------------------------------------------------------- */

GType
epg_store_get_type (void)
{
    static GType epg_store_type = 0;
    static const GTypeInfo epg_store_info = {
	sizeof (EpgStoreClass),
	NULL,                                         /* base_init */
	NULL,                                         /* base_finalize */
	(GClassInitFunc) epg_store_class_init,
	NULL,                                         /* class finalize */
	NULL,                                         /* class_data */
	sizeof (EpgStore),
	0,                                           /* n_preallocs */
	(GInstanceInitFunc) epg_store_init
    };
    static const GInterfaceInfo tree_model_info = {
	(GInterfaceInitFunc) epg_store_tree_model_init,
	NULL,
	NULL
    };
    
    if (epg_store_type)
	return epg_store_type;

    epg_store_type = g_type_register_static (G_TYPE_OBJECT, "EpgStore",
					     &epg_store_info, (GTypeFlags)0);
    g_type_add_interface_static (epg_store_type, GTK_TYPE_TREE_MODEL, &tree_model_info);
    return epg_store_type;
}

static void
epg_store_class_init (EpgStoreClass *klass)
{
    GObjectClass *object_class;
    
    parent_class = (GObjectClass*) g_type_class_peek_parent (klass);
    object_class = (GObjectClass*) klass;
    
    object_class->finalize = epg_store_finalize;
}

static void
epg_store_tree_model_init (GtkTreeModelIface *iface)
{
    iface->get_flags       = epg_store_get_flags;
    iface->get_n_columns   = epg_store_get_n_columns;
    iface->get_column_type = epg_store_get_column_type;
    iface->get_iter        = epg_store_get_iter;
    iface->get_path        = epg_store_get_path;
    iface->get_value       = epg_store_get_value;
    iface->iter_next       = epg_store_iter_next;
    iface->iter_children   = epg_store_iter_children;
    iface->iter_has_child  = epg_store_iter_has_child;
    iface->iter_n_children = epg_store_iter_n_children;
    iface->iter_nth_child  = epg_store_iter_nth_child;
    iface->iter_parent     = epg_store_iter_parent;
}

/* ------------------------------------------------------------------------- */

static void
epg_store_init(EpgStore *st)
{
    st->filter_type = EPG_FILTER_NOFILTER;
    st->filter_tsid = -1;
    st->filter_pnr  = -1;
    INIT_LIST_HEAD(&st->stations);
}

static void
epg_store_finalize(GObject *object)
{
    EpgStore *st = EPG_STORE(object);

    if (st->rows)
	free(st->rows);
    (*parent_class->finalize)(object);
}

/* ------------------------------------------------------------------------- */

static struct station* station_new(int tsid, int pnr)
{
    char buf[32],*name;
    struct station *tv;

    tv = malloc(sizeof(*tv));
    memset(tv,0,sizeof(*tv));
    tv->tsid    = tsid;
    tv->pnr     = pnr;
    tv->visible = TRUE;
    snprintf(buf,sizeof(buf),"%d-%d",tsid,pnr);
    name = cfg_get_str("dvb-pr",buf,"name");
    snprintf(tv->name,sizeof(tv->name),"%s",name ? name : buf);
    return tv;
}

static struct station* station_lookup(EpgStore *st, int tsid, int pnr)
{
    struct list_head *item;
    struct station *tv;

    list_for_each(item,&st->stations) {
	tv = list_entry(item, struct station, next);
	if (tv->tsid != tsid)
	    continue;
	if (tv->pnr != pnr)
	    continue;
	return tv;
    }
    tv = station_new(tsid,pnr);
    list_add_tail(&tv->next,&st->stations);
    return tv;
}

/* ------------------------------------------------------------------------- */

static GtkTreeModelFlags
epg_store_get_flags(GtkTreeModel *tree_model)
{
    EpgStore *st = EPG_STORE(tree_model);

    g_return_val_if_fail(IS_EPG_STORE(st), (GtkTreeModelFlags)0);
    return (GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
}

static gint
epg_store_get_n_columns(GtkTreeModel *tree_model)
{
    return EPG_N_COLUMNS;
}

static GType
epg_store_get_column_type (GtkTreeModel *tree_model,
			   gint          index)
{
    enum epg_cols column = index;

    switch(column) {
    case EPG_COL_PLAYING:
	return G_TYPE_BOOLEAN;
    case EPG_COL_EPGITEM:
	return G_TYPE_POINTER;
    case EPG_COL_TSID:
    case EPG_COL_PNR:
	return G_TYPE_INT;
    case EPG_COL_DATE:
    case EPG_COL_START:
    case EPG_COL_STOP:
    case EPG_COL_STATION:
    case EPG_COL_NAME:
    case EPG_COL_LANG:
	return G_TYPE_STRING;
    case EPG_N_COLUMNS:
	break;
    }
    return G_TYPE_INVALID;
}

static gboolean
epg_store_get_iter (GtkTreeModel *tree_model,
		    GtkTreeIter  *iter,
		    GtkTreePath  *path)
{
    EpgStore *st = EPG_STORE(tree_model);
    gint *indices, n;

    g_assert(IS_EPG_STORE(st));
    g_assert(path!=NULL);

    g_assert(1 == gtk_tree_path_get_depth(path));
    indices = gtk_tree_path_get_indices(path);
    n = indices[0];
    if (n >= st->num_rows || n < 0)
	return FALSE;
    
    memset(iter,0,sizeof(*iter));
    iter->user_data = st->rows[n];
    return TRUE;
}

static GtkTreePath*
epg_store_get_path(GtkTreeModel *tree_model,
		   GtkTreeIter  *iter)
{
    struct epgitem *epg = iter->user_data;
    GtkTreePath *path;

    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, epg->row);
    return path;
}

static void
epg_store_get_value (GtkTreeModel *tree_model,
		     GtkTreeIter  *iter,
		     gint         index,
		     GValue       *value)
{
    enum epg_cols column = index;
    struct epgitem *epg = iter->user_data;
    char buf[64];
    struct tm *tm;

    switch(column) {
    case EPG_COL_DATE:
	tm = localtime(&epg->start);
	strftime(buf, sizeof(buf), "%a, %d.%m.", tm);
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, buf);
	break;
    case EPG_COL_START:
	tm = localtime(&epg->start);
	strftime(buf, sizeof(buf), "%H:%M", tm);
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, buf);
	break;
    case EPG_COL_STOP:
	tm = localtime(&epg->stop);
	strftime(buf, sizeof(buf), "%H:%M", tm);
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, buf);
	break;
    case EPG_COL_STATION:
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, epg->station->name);
	break;
    case EPG_COL_NAME:
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, epg->name);
	break;
    case EPG_COL_LANG:
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, epg->lang);
	break;

    case EPG_COL_TSID:
	g_value_init(value, G_TYPE_UINT);
	g_value_set_uint(value, epg->tsid);
	break;
    case EPG_COL_PNR:
	g_value_init(value, G_TYPE_UINT);
	g_value_set_uint(value, epg->pnr);
	break;

    case EPG_COL_EPGITEM:
	g_value_init(value, G_TYPE_POINTER);
	g_value_set_pointer(value, epg);
	break;
		
    case EPG_COL_PLAYING:
	g_value_init(value, G_TYPE_BOOLEAN);
	g_value_set_boolean(value, epg->playing);
	break;

    case EPG_N_COLUMNS:
	break;
    }
}

static gboolean
epg_store_iter_next(GtkTreeModel  *tree_model,
		    GtkTreeIter   *iter)
{
    EpgStore *st = EPG_STORE(tree_model);
    struct epgitem *epg = iter->user_data;
    int i = epg->row;

    if (++i >= st->num_rows)
	return FALSE;

    memset(iter,0,sizeof(*iter));
    iter->user_data = st->rows[i];
    return TRUE;
}


static gboolean
epg_store_iter_has_child(GtkTreeModel *tree_model,
			 GtkTreeIter  *iter)
{
    return FALSE;
}

static gboolean
epg_store_iter_parent(GtkTreeModel *tree_model,
		      GtkTreeIter  *iter,
		      GtkTreeIter  *child)
{
    return FALSE;
}

static gint
epg_store_iter_n_children(GtkTreeModel *tree_model,
			  GtkTreeIter  *iter)
{
    EpgStore *st = EPG_STORE(tree_model);

    /* special case: if iter == NULL, return number of top-level rows */
    if (!iter)
	return st->num_rows;

    /* otherwise, this is easy again for a list */
    return 0;
}

static gboolean
epg_store_iter_nth_child(GtkTreeModel *tree_model,
		         GtkTreeIter  *iter,
		         GtkTreeIter  *parent,
		         gint          n)
{
    EpgStore *st = EPG_STORE(tree_model);

    /* a list has only top-level rows */
    if (parent)
	return FALSE;

    /* special case: if parent == NULL, set iter to n-th top-level row */
    if (n >= st->num_rows)
	return FALSE;

    memset(iter,0,sizeof(*iter));
    iter->user_data = st->rows[n];
    return TRUE;
}

static gboolean
epg_store_iter_children(GtkTreeModel *tree_model,
			GtkTreeIter  *iter,
			GtkTreeIter  *parent)
{
    return epg_store_iter_nth_child(tree_model, iter, parent, 0);
}

/* ------------------------------------------------------------------------- */

EpgStore *
epg_store_new(void)
{
    return g_object_new(EPG_STORE_TYPE, NULL);
}

static void epg_row_append(EpgStore *st, struct epgitem *epg)
{
    GtkTreeIter  iter;
    GtkTreePath  *path;

    if (0 == (st->num_rows % 16)) {
	int size = (st->num_rows+16) * sizeof(st->rows[0]);
	st->rows = realloc(st->rows, size);
    }
    epg->row = st->num_rows;
    st->rows[st->num_rows++] = epg;

    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, epg->row);
    epg_store_get_iter(GTK_TREE_MODEL(st), &iter, path);
    gtk_tree_model_row_inserted(GTK_TREE_MODEL(st), path, &iter);
    gtk_tree_path_free(path);
    epg->updated = 0;
}

static void epg_row_changed(EpgStore *st, struct epgitem *epg)
{
    GtkTreeIter  iter;
    GtkTreePath  *path;

    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, epg->row);
    epg_store_get_iter(GTK_TREE_MODEL(st), &iter, path);
    gtk_tree_model_row_changed(GTK_TREE_MODEL(st), path, &iter);
    gtk_tree_path_free(path);
    epg->updated = 0;
}

static void epg_row_delete(EpgStore *st, struct epgitem *epg)
{
    GtkTreePath  *path;
    int row;

    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, epg->row);

    row = epg->row;
    memmove(st->rows + epg->row, st->rows + epg->row + 1,
	    (st->num_rows - epg->row - 1) * sizeof(st->rows[0]));
    st->num_rows--;
    epg->row = -1;
    for (; row < st->num_rows; row++)
	st->rows[row]->row = row;

    gtk_tree_model_row_deleted(GTK_TREE_MODEL(st), path);
    gtk_tree_path_free(path);
}

static gboolean is_visible(EpgStore *st, struct epgitem *epg, time_t now)
{
    gboolean matches = FALSE;
    gboolean playing = FALSE;
    gboolean stale = FALSE;
    int i;

    /* check time */
    if (epg->stop < now)
	stale = TRUE;
    if (epg->start <= now && epg->stop > now)
	playing = TRUE;
    if (epg->playing != playing) {
	epg->playing = playing;
	epg->updated++;
    }

    /* filter options */
    switch (st->filter_type) {
    case EPG_FILTER_NOFILTER:
	matches = !stale;
	break;
#if 0
    case EPG_FILTER_NOW:
	matches = playing;
	break;
#endif
    case EPG_FILTER_STATION:
	if (epg->tsid == st->filter_tsid && epg->pnr == st->filter_pnr)
	    matches = !stale;
	break;
    case EPG_FILTER_TEXT:
	if (st->filter_text && !stale) {
	    if (NULL != strcasestr(epg->station->name, st->filter_text) ||
		NULL != strcasestr(epg->name, st->filter_text) ||
		NULL != strcasestr(epg->stext, st->filter_text))
		matches = TRUE;
	    for (i = 0; !matches && i < DIMOF(epg->cat); i++) {
		if (NULL == epg->cat[i])
		    continue;
		if (NULL != strcasestr(epg->cat[i], st->filter_text))
		    matches = TRUE;
	    }
	} else {
	    /* no search text */
	    matches = !stale;
	}
	break;
    }

    /* station visble? */
    if (!epg->station->visible)
	matches = FALSE;
    
    return matches;
}

void
epg_store_refresh(EpgStore *st)
{
    struct epgitem   *epg;
    struct list_head *item;
    time_t now = time(NULL);

    list_for_each(item,&epg_list) {
	epg = list_entry(item, struct epgitem, next);
	if (NULL == epg->station)
	    epg->station = station_lookup(st, epg->tsid, epg->pnr);
	if (is_visible(st,epg,now)) {
	    if (-1 == epg->row)
		epg_row_append(st,epg);
	    else if (epg->updated)
		epg_row_changed(st,epg);
	} else {
	    if (-1 != epg->row)
		epg_row_delete(st,epg);
	}
    }
    epg_store_sort(st);
}

void
epg_store_filter_type(EpgStore *st, enum epg_filter type)
{
    if (st->filter_type != type) {
	st->filter_type = type;
	epg_store_refresh(st);
    }
}

void
epg_store_filter_station(EpgStore *st, int tsid, int pnr)
{
    st->filter_tsid = tsid;
    st->filter_pnr  = pnr;
    if (st->filter_type == EPG_FILTER_STATION)
	epg_store_refresh(st);
}

void
epg_store_filter_text(EpgStore *st, const char *text)
{
    if (st->filter_text) {
	free(st->filter_text);
	st->filter_text = NULL;
    }
    if (text && strlen(text))
	st->filter_text = strdup(text);
    if (st->filter_type == EPG_FILTER_TEXT)
	epg_store_refresh(st);
}

void
epg_store_station_visible(EpgStore *st, int tsid, int pnr, int visible)
{
    struct station *tv;

    tv = station_lookup(st, tsid, pnr);
    tv->visible = visible;
    epg_store_refresh(st);
}

/* ------------------------------------------------------------------------- */

static int cmp_start(const void *aa, const void *bb)
{
    struct epgitem *a = *(void**)aa;
    struct epgitem *b = *(void**)bb;

    if (a->start < b->start)
	return -1;
    if (b->start < a->start)
	return 1;
    return 0;
}

void
epg_store_sort(EpgStore *st)
{
    GtkTreePath  *path;
    gint *new_order;
    int i;

    qsort(st->rows, st->num_rows, sizeof(st->rows[0]), cmp_start);
    new_order = malloc(sizeof(*new_order) * st->num_rows);
    for (i = 0; i < st->num_rows; i++) {
	new_order[i] = st->rows[i]->row;
	st->rows[i]->row = i;
    }
    path = gtk_tree_path_new();
    gtk_tree_model_rows_reordered(GTK_TREE_MODEL(st),
				  path, NULL, new_order);
    gtk_tree_path_free(path);
    free(new_order);
}
