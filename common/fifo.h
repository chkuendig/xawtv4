struct FIFO;
void  fifo_fini(struct FIFO* fifo);
struct FIFO* fifo_init(char *name, int slots);
int   fifo_put(struct FIFO *fifo, void *data);
void  *fifo_get(struct FIFO *fifo);
int   fifo_max(struct FIFO *fifo);
