/*
 * device and device attribute handling
 */

#define _GNU_SOURCE

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "grab-ng.h"
#include "parseconfig.h"
#include "devs.h"
#include "dvb.h"

/* ------------------------------------------------------------------------------ */

extern int debug;

struct devs devs;
LIST_HEAD(global_attrs);

#define DEVS_FLAG_SEEN  1

/* ------------------------------------------------------------------------------ */

#ifdef __linux__
#include <asm/types.h>
#include "videodev2.h"

struct ng_devinfo* vbi_probe(int verbose)
{
    struct ng_devinfo *info = NULL;
    struct v4l2_capability cap;
    int i,n,fd;

    n = 0;
    for (i = 0; NULL != ng_dev.vbi_scan[i]; i++) {
	fd = ng_chardev_open(ng_dev.vbi_scan[i], O_RDONLY | O_NONBLOCK,
			     81, verbose);
	if (-1 == fd)
	    continue;
	if (-1 == ioctl(fd,VIDIOC_QUERYCAP,&cap)) {
	    if (verbose)
		perror("ioctl VIDIOC_QUERYCAP");
	    close(fd);
	    continue;
	}
	info = realloc(info,sizeof(*info) * (n+2));
	memset(info+n,0,sizeof(*info)*2);
	strcpy(info[n].device, ng_dev.vbi_scan[i]);
	snprintf(info[n].name, sizeof(info[n].name), "%s", cap.card);
	snprintf(info[n].bus,  sizeof(info[n].bus),  "%s", cap.bus_info);
	close(fd);
	n++;
    }
    return info;
}

#else

struct ng_devinfo* vbi_probe(int verbose)
{
    return NULL;
}

#endif

static char* device_find_vbi(char *bus)
{
    static struct ng_devinfo* vbi = NULL;
    int i;

    if (NULL == vbi)
	vbi = vbi_probe(0);
    if (0 == strlen(bus))
	return NULL;
    for (i = 0; vbi && 0 != strlen(vbi[i].name); i++)
	if (0 == strcmp(bus,vbi[i].bus))
	    return vbi[i].device;
    return NULL;
}

/* ------------------------------------------------------------------------------ */

static void device_print_line(char *name, char *entry, int def)
{
    char *value;

    value = cfg_get_str("devs", name, entry);
    if (value) {
	fprintf(stderr,"      %-8s = %s\n", entry, value);
    } else {
	fprintf(stderr,"      %-8s = [unset%s]\n", entry,
		def ? ", using default" : "");
    }
}

static void device_print(char *name)
{
    fprintf(stderr,"device configuration \"%s\":\n",name);
    device_print_line(name, "bus",     0);
    device_print_line(name, "video",   0);
#ifdef HAVE_ZVBI
    device_print_line(name, "vbi",     0);
#endif
#ifdef HAVE_DVB
    device_print_line(name, "dvb",     0);
#endif
    device_print_line(name, "sndrec",  1);
    device_print_line(name, "sndplay", 1);
    // FIXME: mixer
}

static int device_probe_video(char *device)
{
    char *name,*h;
    unsigned int flags;
    struct ng_devstate dev;
    int err,i,add=0;

    name = cfg_search("devs", NULL, "video", device);
    if (name) {
	flags = cfg_get_sflags("devs", name);
	if (flags & DEVS_FLAG_SEEN) {
	    if (debug)
		fprintf(stderr,"%s: %s: seen, skipping\n",__FUNCTION__,device);
	    return 0;
	}
    }
    if (0 != (err = ng_vid_init(device, &dev))) {
	if (EBUSY == err) {
	    /* device busy -- ignore failure and don't change anything */
	    if (debug)
		fprintf(stderr,"%s: %s: busy\n",__FUNCTION__,device);
	} else {
	    /* device seems to be gone */
	    if (debug)
		fprintf(stderr,"%s: %s: delete: probably gone\n",__FUNCTION__,device);
	    cfg_del_section("devs", name);
	    return 0;
	}
    }
    if (NULL != name && dev.v->busname) {
	/* check bus address */
	char *bus1 = cfg_get_str("devs", name, "bus");
	char *bus2 = dev.v->busname(dev.handle);
	if (NULL != bus1 && NULL != bus2 && 0 != strcmp(bus1,bus2)) {
	    if (debug)
		fprintf(stderr,"%s: %s: delete: bus mismatch\n",__FUNCTION__,device);
	    cfg_del_section("devs", name);
	    name = NULL;
	}
    }
    if (NULL == name) {
	/* create new entry */
	add = 1;
	h = dev.v->devname(dev.handle);
	name = strdup(h);
	i = 2;
	while (NULL != cfg_search("devs",name,NULL,NULL)) {
	    free(name);
	    name = malloc(strlen(h) + 10);
	    sprintf(name,"%s - #%d",h,i++);
	}
	cfg_set_str("devs", name, "video", device);
	if (dev.v->busname) {
	    char *vbi = device_find_vbi(dev.v->busname(dev.handle));
	    if (vbi)
		cfg_set_str("devs", name, "vbi", vbi);
	    cfg_set_str("devs", name, "bus", dev.v->busname(dev.handle));
	}
	if (debug)
	    fprintf(stderr,"%s: %s: add\n",__FUNCTION__,device);
    }
    cfg_set_sflags("devs", name, DEVS_FLAG_SEEN, DEVS_FLAG_SEEN);
    return 0;
}

#ifdef HAVE_DVB
static int device_probe_dvb(char *device)
{
    char *name,*h;
    unsigned int flags;
    struct dvb_state *dvb;
    int i,add = 0;

    name = cfg_search("devs", NULL, "dvb", device);
    if (name) {
	flags = cfg_get_sflags("devs", name);
	if (flags & DEVS_FLAG_SEEN) {
	    if (debug)
		fprintf(stderr,"%s: %s: seen, skipping\n",__FUNCTION__,device);
	    return 0;
	}
    }
    if (NULL == (dvb = dvb_init(device))) {
	if (0 /* FIXME */) {
	    /* device busy -- ignore failure and don't change anything */
	    if (debug)
		fprintf(stderr,"%s: %s: busy\n",__FUNCTION__,device);
	} else {
	    /* device seems to be gone */
	    if (debug)
		fprintf(stderr,"%s: %s: delete\n",__FUNCTION__,device);
	    cfg_del_section("devs", name);
	    return 0;
	}
    }
    if (NULL == name) {
	/* create new entry */
	add = 1;
	h = dvb_devname(dvb);
	name = strdup(h);
	i = 2;
	while (NULL != cfg_search("devs",name,NULL,NULL)) {
	    free(name);
	    name = malloc(strlen(h) + 10);
	    sprintf(name,"%s - #%d",h,i++);
	}
	cfg_set_str("devs", name, "dvb", device);
	if (debug)
	    fprintf(stderr,"%s: %s: add\n",__FUNCTION__,device);
    }
    dvb_fini(dvb);
    cfg_set_sflags("devs", name, DEVS_FLAG_SEEN, DEVS_FLAG_SEEN);
    return 0;
}
#endif

int devlist_probe(void)
{
    struct list_head      *item;
    struct ng_vid_driver  *vid;
    struct ng_devinfo     *info;
    char *list,*dev;
    int i;

    if (debug)
	fprintf(stderr,"%s: checking existing entries ...\n",__FUNCTION__);
    cfg_sections_for_each("devs",list) {
#ifdef HAVE_DVB
	dev = cfg_get_str("devs", list, "dvb");
	if (dev)
	    device_probe_dvb(dev);
#endif
	dev = cfg_get_str("devs", list, "video");
	if (dev)
	    device_probe_video(dev);
    }
    
#ifdef HAVE_DVB
    if (debug)
	fprintf(stderr,"%s: probing dvb devices ...\n",__FUNCTION__);
    info = dvb_probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	device_probe_dvb(info[i].device);
#endif

    list_for_each(item,&ng_vid_drivers) {
        vid = list_entry(item, struct ng_vid_driver, list);
	if (debug)
	    fprintf(stderr,"%s: probing %s devices ...\n",__FUNCTION__,vid->name);
	info = vid->probe(debug);
	for (i = 0; info && 0 != strlen(info[i].name); i++)
	    device_probe_video(info[i].device);
    }

    if (debug)
	fprintf(stderr,"%s: probing done\n",__FUNCTION__);
    return 0;
}

int devlist_print(void)
{
    char *list;

    cfg_sections_for_each("devs",list) {
	device_print(list);
	fprintf(stderr,"\n");
    }
    return 0;
}

static void devlist_filename(char *filename, int len)
{
    char hostname[64];
    char *h;

    if (-1 == gethostname(hostname,sizeof(hostname)))
	strcpy(hostname,"please-fix-my-hostname");
    h = strchr(hostname,'.');
    if (h)
	*h = 0;
    snprintf(filename,len,"%s/.tv/devices.%s",
	     getenv("HOME"),hostname);
}

int devlist_init(int readconf, int forceprobe, int writeconf)
{
    char filename[128];
    
    /* read device config file */
    devlist_filename(filename, sizeof(filename));
    if (readconf)
	cfg_parse_file("devs", filename);

    /* probe devices */
    if (forceprobe || 0 == cfg_sections_count("devs"))
	devlist_probe();

    /* write device config file */
    if (writeconf)
	cfg_write_file("devs",filename);
    return 0;
}

/* ------------------------------------------------------------------------------ */

void (*attr_add_hook)(struct ng_attribute *attr);
void (*attr_update_hook)(struct ng_attribute *attr, int value);
void (*attr_del_hook)(struct ng_attribute *attr);

/* sort global list by priority */
static void list_add_sorted(struct ng_attribute *add)
{
    struct ng_attribute *attr;
    struct list_head *item;

    list_for_each(item,&global_attrs) {
	attr = list_entry(item, struct ng_attribute, global_list);
	if (add->priority > attr->priority) {
	    list_add_tail(&add->global_list,&attr->global_list);
	    return;
	}
    }
    list_add_tail(&add->global_list,&global_attrs);
}

static void device_attrs_add(struct ng_devstate *dev)
{
    struct list_head *item;
    struct ng_attribute *attr;
    
    if (NG_DEV_NONE == dev->type)
	return;
    list_for_each(item,&dev->attrs) {
	attr = list_entry(item, struct ng_attribute, device_list);
	list_add_sorted(attr);
	if (attr_add_hook)
	    attr_add_hook(attr);
    }
}

static void device_attrs_del(struct ng_devstate *dev)
{
    struct list_head *item;
    struct ng_attribute *attr;
    
    if (NG_DEV_NONE == dev->type)
	return;
    list_for_each(item,&dev->attrs) {
	attr = list_entry(item, struct ng_attribute, device_list);
	if (attr_del_hook)
	    attr_del_hook(attr);
	list_del(&attr->global_list);
    }
}

/* ------------------------------------------------------------------------------ */

struct ng_attribute*
find_attr_by_dev_id(struct ng_devstate *dev, int id)
{
    struct ng_attribute *attr;
    struct list_head    *item;

    if (dev->type == NG_DEV_NONE)
	return NULL;
    list_for_each(item,&dev->attrs) {
	attr = list_entry(item, struct ng_attribute, device_list);
	if (attr->id == id)
	    return attr;
    }
    return NULL;
}

struct ng_attribute* find_attr_by_name(char *name)
{
    struct ng_attribute *attr;
    struct list_head    *item;

    list_for_each(item,&global_attrs) {
	attr = list_entry(item, struct ng_attribute, global_list);
	if (0 == strcasecmp(attr->name,name))
	    return attr;
    }
    return NULL;
}

struct ng_attribute* find_attr_by_id(int id)
{
    struct ng_attribute *attr;
    struct list_head    *item;

    list_for_each(item,&global_attrs) {
	attr = list_entry(item, struct ng_attribute, global_list);
	if (attr->id == id)
	    return attr;
    }
    return NULL;
}

int attr_read(struct ng_attribute *attr)
{
    int retval;

    if (NULL == attr)
	return -1;
    if (attr->dev)
	ng_dev_open(attr->dev);
    retval = attr->read(attr);
    if (attr->dev)
	ng_dev_close(attr->dev);
    return retval;
}

int attr_write(struct ng_attribute *attr, int value, int call_hook)
{
    if (NULL == attr)
	return -1;
    if (attr->dev)
	ng_dev_open(attr->dev);
    attr->write(attr,value);
    if (attr->dev)
	ng_dev_close(attr->dev);
    if (call_hook && attr_update_hook)
	attr_update_hook(attr,value);
    return 0;
}

int attr_read_name(char *name)
{
    struct ng_attribute *attr = find_attr_by_name(name);
    return attr_read(attr);
}

int attr_write_name(char *name, int value)
{
    struct ng_attribute *attr = find_attr_by_name(name);
    return attr_write(attr,value,1);
}

int attr_read_id(int id)
{
    struct ng_attribute *attr = find_attr_by_id(id);
    return attr_read(attr);
}

int attr_write_id(int id, int value)
{
    struct ng_attribute *attr = find_attr_by_id(id);
    return attr_write(attr,value,1);
}

int attr_read_dev_id(struct ng_devstate *dev, int id)
{
    struct ng_attribute *attr = find_attr_by_dev_id(dev,id);
    return attr_read(attr);
}

int attr_write_dev_id(struct ng_devstate *dev, int id, int value)
{
    struct ng_attribute *attr = find_attr_by_dev_id(dev,id);
    return attr_write(attr,value,1);
}

/* ------------------------------------------------------------------------------ */

int device_fini()
{
    /* cleanup attributes */
    device_attrs_del(&devs.video);
    device_attrs_del(&devs.sndplay);
    device_attrs_del(&devs.sndrec);

    /* close devices */
    ng_dev_fini(&devs.video);
    ng_dev_fini(&devs.sndrec);
    ng_dev_fini(&devs.sndplay);

#ifdef HAVE_DVB
    if (devs.dvb) {
	dvb_fini(devs.dvb);
	devs.dvb = NULL;
    }
#endif
    return 0;
}

int device_init(char *name)
{
    char *device, *list;
    int err;

    memset(&devs,0,sizeof(devs));
    if (NULL == name)
	name = cfg_sections_first("devs");

    if (0 == cfg_entries_count("devs",name))
	cfg_sections_for_each("devs",list)
	    if (NULL != strcasestr(list,name))
		name = list;

    if (debug) {
	fprintf(stderr,"using: ");
	device_print(name);
    }

    /* video4linux */
    device = cfg_get_str("devs", name, "video");
    if (NULL != device && 0 != strcasecmp(device,"none")) {
	err = ng_vid_init(device, &devs.video);
	if (err)
	    goto oops;
    }
    device = cfg_get_str("devs", name, "vbi");
    if (NULL != device && 0 != strcasecmp(device,"none"))
	devs.vbidev = device;

#ifdef HAVE_DVB
    /* dvb */
    device = cfg_get_str("devs", name, "dvb");
    if (NULL != device && 0 != strcasecmp(device,"none")) {
	devs.dvbadapter = device;
	devs.dvb = dvb_init(devs.dvbadapter);
    }
#endif
    
    /* sound */
    device = cfg_get_str("devs", name, "sndrec");
    ng_dsp_init(device, &devs.sndrec, 1);
    device = cfg_get_str("devs", name, "sndplay");
    ng_dsp_init(device, &devs.sndplay, 0);

    /* init attributes */
    device_attrs_add(&devs.video);
    device_attrs_add(&devs.sndplay);
    device_attrs_add(&devs.sndrec);
    return 0;

 oops:
    device_fini();
    return err;
}

/* ------------------------------------------------------------------------------ */

static void print_driver(const char *name)
{
    // fprintf(stderr,"  [%s]\n",name);
}

static void print_devinfo(const char *driver, struct ng_devinfo *info)
{
    int spaces1 = 24 - strlen(driver) - strlen(info->device);
    int spaces2 = 48 - strlen(info->name) - strlen(info->bus);

    if (spaces1 < 0) spaces1 = 0;
    if (spaces2 < 0) spaces2 = 0;
    fprintf(stderr," [%s] %s%*s %s%*s %s\n",
	    driver, info->device,
	    spaces1, "", info->name,
	    spaces2, "", info->bus);
}

static int
print_dvb(void)
{
#ifdef HAVE_DVB
    struct ng_devinfo *info;
    int i;

    print_driver("dvb");
    info = dvb_probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo("dvb",info+i);
#endif
    return 0;
}

static int
print_vbi(void)
{
    struct ng_devinfo *info;
    int i;

    print_driver("vbi");
    info = vbi_probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo("vbi",info+i);
    return 0;
}

static int
print_video(struct ng_vid_driver *vid)
{
    struct ng_devinfo    *info;
    int i;

    print_driver(vid->name);
    info = vid->probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo(vid->name,info+i);
    return 0;
}

static int
print_dsp(struct ng_dsp_driver *dsp, int record)
{
    struct ng_devinfo    *info;
    int i;

    print_driver(dsp->name);
    info = dsp->probe(record,debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++)
	print_devinfo(dsp->name,info+i);
    return 0;
}

static int
print_mix(struct ng_mix_driver *mix, int verbose)
{
    struct ng_devinfo    *info;
    struct ng_devinfo    *elem;
    int i,j;

    print_driver(mix->name);
    info = mix->probe(debug);
    for (i = 0; info && 0 != strlen(info[i].name); i++) {
	print_devinfo(mix->name,info+i);
	if (verbose) {
	    elem = mix->channels(info[i].device);
	    for (j = 0; elem && 0 != strlen(elem[j].name); j++)
		fprintf(stderr,"      %-14s - %s\n",
			elem[j].device, elem[j].name);
	}
    }
    return 0;
}

void device_ls_devs(int verbose)
{
    struct list_head *item;
    struct ng_mix_driver *mix;
    struct ng_dsp_driver *dsp;
    struct ng_vid_driver *vid;

    /* dvb devices */
    fprintf(stderr,"probing dvb devices ...\n");
    print_dvb();
    fprintf(stderr,"\n");

    /* vbi devices */
    fprintf(stderr,"probing vbi devices ...\n");
    print_vbi();
    fprintf(stderr,"\n");

    /* video devices */
    fprintf(stderr,"probing video devices ...\n");
    list_for_each(item,&ng_vid_drivers) {
        vid = list_entry(item, struct ng_vid_driver, list);
	print_video(vid);
    }
    fprintf(stderr,"\n");

    /* dsp devices */
    fprintf(stderr,"probing dsp devices (playback) ...\n");
    list_for_each(item,&ng_dsp_drivers) {
        dsp = list_entry(item, struct ng_dsp_driver, list);
	print_dsp(dsp,0);
    }
    fprintf(stderr,"\n");

    fprintf(stderr,"probing dsp devices (record) ...\n");
    list_for_each(item,&ng_dsp_drivers) {
        dsp = list_entry(item, struct ng_dsp_driver, list);
	print_dsp(dsp,1);
    }
    fprintf(stderr,"\n");

    /* mixer devices */
    fprintf(stderr,"probing mixers ...\n");
    list_for_each(item,&ng_mix_drivers) {
        mix = list_entry(item, struct ng_mix_driver, list);
	print_mix(mix, verbose);
    }
    fprintf(stderr,"\n");
}
