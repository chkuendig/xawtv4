#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "list.h"
#include "grab-ng.h"
#include "fifo.h"

/*-------------------------------------------------------------------------*/

struct fifo_elem {
    void              *data;
    struct list_head  list;
};

struct FIFO {
    char              *name;
    pthread_mutex_t   lock;
    pthread_cond_t    hasdata;
    struct list_head  data;
    struct list_head  free;
    int               count,max;
};

/*-------------------------------------------------------------------------*/

extern int debug;
static int fifos;

void fifo_fini(struct FIFO* fifo)
{
    struct fifo_elem *elem;
    
    BUG_ON(!list_empty(&fifo->data),"fifo_fini: list not empty\n");
    while (!list_empty(&fifo->free)) {
	elem = list_entry(fifo->free.next, struct fifo_elem, list);
	list_del(&elem->list);
	free(elem);
    }
    free(fifo);
    fifos--;
}

struct FIFO*
fifo_init(char *name, int slots)
{
    struct FIFO      *fifo;
    struct fifo_elem *elem;
    int i;

    fifo = malloc(sizeof(*fifo));
    if (NULL == fifo)
	return NULL;
    memset(fifo,0,sizeof(*fifo));
    fifos++;

    pthread_mutex_init(&fifo->lock, NULL);
    pthread_cond_init(&fifo->hasdata, NULL);
    INIT_LIST_HEAD(&fifo->data);
    INIT_LIST_HEAD(&fifo->free);
    fifo->name    = name;
    fifo->count   = 0;
    fifo->max     = 0;

    for (i = 0; i < slots; i++) {
	elem = malloc(sizeof(*elem));
	if (NULL == elem) {
	    fifo_fini(fifo);
	    return NULL;
	}
	memset(elem,0,sizeof(*elem));
	list_add_tail(&elem->list,&fifo->free);
    }
    return fifo;
}

int
fifo_put(struct FIFO *fifo, void *data)
{
    struct fifo_elem *elem;
    
    pthread_mutex_lock(&fifo->lock);
    if (list_empty(&fifo->free)) {
	pthread_mutex_unlock(&fifo->lock);
	fprintf(stderr,"fifo %s is full\n",fifo->name);
	return -1;
    }
    if (debug > 1)
	fprintf(stderr,"put %s %p [pid=%d]\n",
		fifo->name,data,getpid());

    elem = list_entry(fifo->free.next, struct fifo_elem, list);
    elem->data = data;
    list_del(&elem->list);
    list_add_tail(&elem->list,&fifo->data);
    fifo->count++;
    if (fifo->max < fifo->count)
	fifo->max = fifo->count;
    
    pthread_cond_signal(&fifo->hasdata);
    pthread_mutex_unlock(&fifo->lock);
    return 0;
}

void*
fifo_get(struct FIFO *fifo)
{
    struct fifo_elem *elem;
    void *data;

    pthread_mutex_lock(&fifo->lock);
    while (list_empty(&fifo->data)) {
	pthread_cond_wait(&fifo->hasdata, &fifo->lock);
    }

    elem = list_entry(fifo->data.next, struct fifo_elem, list);
    data = elem->data;
    list_del(&elem->list);
    elem->data = NULL;
    list_add_tail(&elem->list,&fifo->free);
    fifo->count--;

    if (debug > 1)
	fprintf(stderr,"get %s %p [pid=%d]\n",
		fifo->name,data,getpid());
    pthread_mutex_unlock(&fifo->lock);
    return data;
}

int fifo_max(struct FIFO *fifo)
{
    return fifo->max;
}

static void __fini fifo_check(void)
{
    OOPS_ON(fifos > 0, "fifos is %d (expected 0)",fifos);
}
