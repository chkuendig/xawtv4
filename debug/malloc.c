#include <stdio.h>
#include <string.h>
#include <execinfo.h>

#include "list.h"

/* ------------------------------------------------------------------------ */

void *__wrap_malloc(size_t size);
void *__real_malloc(size_t size);
void __wrap_free(void *ptr);
void __real_free(void *ptr);

static int malloc_count = 0;
static int malloc_max   = 64;
static int frame_shift  = 0;

struct stats {
    void              *ptr;
    size_t            size;
    void              *bt[16];
    int               btc;
    struct list_head  next;
};
LIST_HEAD(stats);

void *__wrap_malloc(size_t size)
{
    struct stats *e;
    void *ptr;

    ptr = __real_malloc(size);
    e = __real_malloc(sizeof(*e));
    memset(e,0,sizeof(*e));
    e->ptr  = ptr;
    e->size = size;
    e->btc  = backtrace(e->bt,16);
    list_add_tail(&e->next,&stats);
    malloc_count++;
    if (malloc_count > malloc_max) {
	e = list_entry(stats.next, struct stats, next);
	list_del(&e->next);
	fprintf(stderr,"=========================================\n");
	fprintf(stderr,"malloc leak candidate: %p %ld |\n",e->ptr,e->size);
	if (e->btc > frame_shift)
	    backtrace_symbols_fd(e->bt  + frame_shift,
				 e->btc - frame_shift, 2);
	fprintf(stderr,"\n");
	__real_free(e);
	malloc_count--;
    }
    return ptr;
}

void __wrap_free(void *ptr)
{
    struct list_head *item;
    struct stats *e;

    list_for_each(item,&stats) {
	e = list_entry(item, struct stats, next);
	if (ptr != e->ptr)
	    continue;
	list_del(&e->next);
	__real_free(e);
	malloc_count--;
	break;
    }
    __real_free(ptr);
}
