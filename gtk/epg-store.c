#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "epg-store.h"
#include "dvb.h"
#include "parseconfig.h"

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
    /* alloc / init stuff */
}

static void
epg_store_finalize(GObject *object)
{
    /* free() stuff */
    (*parent_class->finalize)(object);
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
    char buf[64],*name;
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
	snprintf(buf,sizeof(buf),"%d-%d",epg->tsid,epg->pnr);
	name = cfg_get_str("dvb-pr",buf,"name");
	g_value_init(value, G_TYPE_STRING);
	g_value_set_string(value, name ? name : buf);
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

void
epg_store_refresh(EpgStore *st)
{
    struct epgitem   *epg;
    struct list_head *item;
    GtkTreeIter  iter;
    GtkTreePath  *path;
    int inserted;

    list_for_each(item,&epg_list) {
	epg = list_entry(item, struct epgitem, next);
	if (!epg->updated)
	    continue;

	if (-1 == epg->row) {
	    /* new */
	    if (0 == (st->num_rows % 16)) {
		int size = (st->num_rows+16) * sizeof(st->rows[0]);
		st->rows = realloc(st->rows, size);
	    }
	    epg->row = st->num_rows;
	    st->rows[st->num_rows++] = epg;
	    inserted = 1;
	} else {
	    /* updated */
	    inserted = 0;
	}

	epg->updated = 0;
	path = gtk_tree_path_new();
	gtk_tree_path_append_index(path, epg->row);
	epg_store_get_iter(GTK_TREE_MODEL(st), &iter, path);
	if (inserted)
	    gtk_tree_model_row_inserted(GTK_TREE_MODEL(st), path, &iter);
	else
	    gtk_tree_model_row_changed(GTK_TREE_MODEL(st), path, &iter);
	gtk_tree_path_free(path);
    }
}

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

#if 0
static gboolean is_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    gboolean matches = FALSE;
    gint start, stop, tsid, pnr, now;

    switch (cur_filter) {
    case EPG_FILTER_NOFILTER:
	matches = TRUE;
	break;
    case EPG_FILTER_NOW:
	gtk_tree_model_get(model, iter,
			   EPG_COL_START, &start,
			   EPG_COL_STOP,  &stop,
			   -1);
	now = time(NULL);
	if (start <= now && stop > now)
	    matches = TRUE;
	break;
    case EPG_FILTER_STATION:
	gtk_tree_model_get(model, iter,
			   EPG_COL_TSID, &tsid,
			   EPG_COL_PNR,  &pnr,
			   -1);
	if (tsid == epg_filter_tsid && pnr == epg_filter_pnr)
	    matches = TRUE;
	break;
    case EPG_FILTER_NEXT:
    case EPG_FILTER_TEXT:
	g_warning("filter not implemented yet");
	break;
    }
    return matches;
}
#endif
