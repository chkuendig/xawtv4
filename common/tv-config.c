/* 
    (c) 2004 Gerd Knorr <kraxel@bytesex.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <pthread.h>

#include "grab-ng.h"
#include "tv-config.h"
#include "commands.h"
#include "sound.h"
#include "parseconfig.h"
#include "tuning.h"
#include "event.h"

/* ----------------------------------------------------------------------- */

enum display_mode display_mode;
enum tuning_mode  tuning_mode;

struct ng_video_filter *cur_filter;

/* ----------------------------------------------------------------------- */

/* x11 apps */
struct cfg_cmdline cmd_opts_x11[] = {
    {
	.cmdline = "randr",
	.option  = { O_EXT_XRANDR },
	.yesno   = 1,
	.desc    = "enable/disable Xrandr extention",
    },{
	.cmdline = "vm",
	.option  = { O_EXT_XVIDMODE },
	.yesno   = 1,
	.desc    = "enable/disable VidMode extention",
    },{
	.cmdline = "xv",
	.option  = { O_EXT_XVIDEO },
	.yesno   = 1,
	.desc    = "enable/disable Xvideo extention",
    },{
	.cmdline = "gl",
	.option  = { O_EXT_OPENGL },
	.yesno   = 1,
	.desc    = "enable/disable OpenGL (GLX extention)",
    },{
	/* end of list */
    }
};

/* recording */
struct cfg_cmdline cmd_opts_record[] = {
    {
	.cmdline  = "bufcount",
	.option   = { O_REC_BUFCOUNT },
	.needsarg = 1,
	.desc     = "number of capture buffers to request",
    },{
	.cmdline  = "parallel",
	.option   = { O_REC_THREADS },
	.needsarg = 1,
	.desc     = "number of compression threads (for capture)",
    },{
	.cmdline  = "destdir",
	.option   = { O_REC_DESTDIR },
	.needsarg = 1,
	.desc     = "destination directory for movies",
    },{
	/* end of list */
    }
};

/* devices */
struct cfg_cmdline cmd_opts_devices[] = {
    {
	.cmdline  = "input",
	.option   = { O_INPUT },
	.needsarg = 1,
	.desc     = "video input",
    },{
	.cmdline  = "tvnorm",
	.option   = { O_TVNORM },
	.needsarg = 1,
	.desc     = "TV norm",
    },{
	.cmdline  = "freqtab",
	.option   = { O_FREQTAB },
	.needsarg = 1,
	.desc     = "frequency table (for tuning analog tv)",

    },{
	.cmdline  = "video",
	.option   = { "devs", "cmdline", "video" },
	.needsarg = 1,
	.desc     = "video4linux device",
    },{
	.cmdline  = "vbi",
	.option   = { "devs", "cmdline", "vbi" },
	.needsarg = 1,
	.desc     = "vbi device",
    },{
	.cmdline  = "dvb",
	.option   = { "devs", "cmdline", "dvb" },
	.needsarg = 1,
	.desc     = "dvb adapter",
    },{
	.cmdline  = "sndrec",
	.option   = { "devs", "cmdline", "sndrec" },
	.needsarg = 1,
	.desc     = "sound device (for recording)",
    },{
	.cmdline  = "sndplay",
	.option   = { "devs", "cmdline", "sndplay" },
	.needsarg = 1,
	.desc     = "sound device (for playback)",
    },{
	/* end of list */
    }
};

/* ----------------------------------------------------------------------- */

void apply_config(void)
{
    char *val;

    /* for libng */
    ng_ratio_x      = GET_RATIO_X();
    ng_ratio_y      = GET_RATIO_Y();
    ng_jpeg_quality = GET_JPEG_QUALITY();

    // snapbase = FIXME ;

    /* update GUI */
    if (NULL != (val = cfg_get_str(O_FREQTAB)))
	freqtab_set(val);

    /* init events */
    event_readconfig();
}

/* ----------------------------------------------------------------------- */

int read_config_file(char *name)
{
    char filename[1024];

    snprintf(filename,sizeof(filename),"%s/.tv/%s",
	     getenv("HOME"), name);
    return cfg_parse_file(name,filename);
}

int write_config_file(char *name)
{
    char filename[1024];

    snprintf(filename,sizeof(filename),"%s/.tv/%s",
	     getenv("HOME"), name);
    return cfg_write_file(name,filename);
}

void
read_config(void)
{
    read_config_file("options");
    read_config_file("stations");
    read_config_file("dvb-ts");
    read_config_file("dvb-pr");
}

/* ----------------------------------------------------------------------- */

struct STRTAB booltab[] = {
    {  0, "no" },
    {  0, "false" },
    {  0, "off" },
    {  1, "yes" },
    {  1, "true" },
    {  1, "on" },
    { -1, NULL }
};

int
str_to_int(char *str, struct STRTAB *tab)
{
    int i;
    
    if (str[0] >= '0' && str[0] <= '9')
	return atoi(str);
    for (i = 0; tab[i].str != NULL; i++)
	if (0 == strcasecmp(str,tab[i].str))
	    return(tab[i].nr);
    return -1;
}

const char*
int_to_str(int n, struct STRTAB *tab)
{
    int i;
    
    for (i = 0; tab[i].str != NULL; i++)
	if (tab[i].nr == n)
	    return tab[i].str;
    return NULL;
}
