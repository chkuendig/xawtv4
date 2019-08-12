/*
 * gather some device info from sysfs
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

/* ------------------------------------------------------------------------ */

/* we care about chardevs only ... */
const char *sysfs_base = "/sys/class";

/* ------------------------------------------------------------------------ */

static int sysfs_read_file(char *class, char *dev, char *attr,
			   char *buf, int len)
{
    char path[64];
    int fd,rc;

    snprintf(path,sizeof(path),"%s/%s/%s/%s",
	     sysfs_base, class, dev, attr);
    if (-1 == (fd = open(path,O_RDONLY)))
	return -1;
    rc = read(fd,buf,len);
    close(fd);
    if (rc >= 0)
	buf[rc] = 0;
    if (rc >= 1 && buf[rc-1] == '\n')
	buf[rc-1] = 0;
    return rc;
}

static int sysfs_find_path(int fd)
{
    char path[64],buf[64];
    struct stat st;
    DIR *d_class, *d_device;
    struct dirent *e_class, *e_device;
    int major,minor;

    if (-1 == fstat(fd,&st))
	return -1;
    if (!S_ISCHR(st.st_mode))
	return -1;

    /* walk through /sys/class to find the chardev */
    d_class = opendir(sysfs_base);
    if (NULL == d_class)
	return -1;
    while (NULL != (e_class = readdir(d_class))) {
	if ('.' == e_class->d_name[0])
	    continue;
	snprintf(path,sizeof(path),"%s/%s",sysfs_base,e_class->d_name);
	d_device = opendir(path);
	if (NULL == d_device)
	    continue;
	while (NULL != (e_device = readdir(d_device))) {
	    if ('.' == e_device->d_name[0])
		continue;

	    /* get and compare major/minor */
	    if (-1 == sysfs_read_file(e_class->d_name, e_device->d_name,
				      "dev", buf, sizeof(buf)))
		continue;
	    if (2 != sscanf(buf,"%d:%d",&major,&minor))
		continue;
	    if (major(st.st_rdev) != major)
		continue;
	    if (minor(st.st_rdev) != minor)
		continue;

	    /* hit ;) */
	    fprintf(stderr,"  %s\n",e_class->d_name);
	    fprintf(stderr,"    %s\n",e_device->d_name);
	    fprintf(stderr,"      dev  = %s\n",buf);
	    if (-1 != sysfs_read_file(e_class->d_name, e_device->d_name,
				      "name", buf, sizeof(buf)))
		fprintf(stderr,"      name = %s\n",buf);
	}
	closedir(d_device);
    }
    closedir(d_class);
    return -1;
}

/* ------------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int fd;
    
    if (2 != argc) {
	fprintf(stderr,"usage: %s device\n",argv[0]);
	exit(1);
    }
    if (-1 == (fd = open(argv[1], O_RDONLY | O_NONBLOCK))) {
	fprintf(stderr,"open %s: %s\n",argv[1],strerror(errno));
	exit(1);
    }

    sysfs_find_path(fd);
    close(fd);
    exit(0);
}
