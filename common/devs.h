
/* current hardware drivers */
struct devs {
    /* video hardware */
    struct ng_devstate video;     /* v4l device  */
    struct ng_devstate x11;       /* xvideo port */

    /* use pointer for overlay */
    struct ng_devstate *overlay;

    /* sound hardware */
    struct ng_devstate sndrec;
    struct ng_devstate sndplay;

    /* FIXME */
    char *vbidev;
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
extern int devlist_init(int probe);
extern int device_init(char *name);
extern int device_fini(void);
