
/* current hardware drivers */
struct devs {
    /* video hardware */
    struct ng_devstate video;

    /* sound hardware */
    struct ng_devstate sndrec;
    struct ng_devstate sndplay;

    /* vbi don't use libng */
    char *vbidev;

    /* ... and neither does dvb */
    char *dvbadapter;
    struct dvb_state *dvb;
};

extern struct devs devs;
extern struct list_head global_attrs;

extern void (*attr_add_hook)(struct ng_attribute *attr);
extern void (*attr_del_hook)(struct ng_attribute *attr);
extern void (*attr_update_hook)(struct ng_attribute *attr, int value);

struct ng_attribute* find_attr_by_dev_id(struct ng_devstate *dev, int id);
struct ng_attribute* find_attr_by_name(char *name);
struct ng_attribute* find_attr_by_id(int id);

int attr_read(struct ng_attribute *attr);
int attr_write(struct ng_attribute *attr, int value, int call_hook);
int attr_read_name(char *name);
int attr_write_name(char *name, int value);
int attr_read_id(int id);
int attr_write_id(int id, int value);
int attr_read_dev_id(struct ng_devstate *dev, int id);
int attr_write_dev_id(struct ng_devstate *dev, int id, int value);

/* ---------------------------------------------------------------------------- */

extern int devlist_probe(void);
extern int devlist_init(int readconf, int forceprobe, int writeconf);
extern int device_init(char *name);
extern int device_fini(void);

/* ---------------------------------------------------------------------------- */

struct ng_devinfo* vbi_probe(int verbose);
