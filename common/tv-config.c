/* 
    (c) 1998-2003 Gerd Knorr <kraxel@bytesex.org>

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

#ifndef NO_X11
# include <X11/Xlib.h>
# include <X11/Intrinsic.h>
# include <X11/StringDefs.h>
# include <X11/Xaw/XawInit.h>
# include <X11/Xaw/Command.h>
# include <X11/Xaw/Paned.h>
#endif

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

void apply_config(void)
{
    char *val;

    /* for libng */
    ng_ratio_x      = GET_RATIO_X();
    ng_ratio_y      = GET_RATIO_Y();
    ng_jpeg_quality = GET_JPEG_QUALITY();

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
