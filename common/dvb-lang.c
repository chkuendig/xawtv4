/*
 * $Id: dvb-lang.c,v 1.3 2005/09/30 12:55:31 kraxel Exp $
 *
 * keep track of the language codes seen so far
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <inttypes.h>

#include "grab-ng.h"
#include "parseconfig.h"
#include "dvb-tuning.h"
#include "dvb.h"
#include "list.h"

/* ----------------------------------------------------------------------- */

LIST_HEAD(dvb_langs);

static void dvb_lang_add(char *lang)
{
    struct list_head *item;
    struct dvb_lang *l;

    list_for_each(item,&dvb_langs) {
	l = list_entry(item, struct dvb_lang, next);
	if (0 == strncmp(lang,l->lang,3))
	    return;
    }
    // fprintf(stderr,"new language: \"%.3s\"\n",lang);
    l = malloc(sizeof(*l));
    memset(l,0,sizeof(*l));
    strncpy(l->lang, lang, 3);
    list_add_tail(&l->next,&dvb_langs);
}

void dvb_lang_parse_audio(char *audio)
{
    char lang[4];
    int pid, pos = 0, n;

    while (2 == sscanf(audio+pos," %3[^:]:%d%n",lang,&pid,&n)) {
	dvb_lang_add(lang);
	pos += n;
    }
}

void dvb_lang_init(void)
{
    char *list,*audio;

    cfg_sections_for_each("dvb-pr",list) {
	audio = cfg_get_str("dvb-pr",list,"audio_details");
	if (!audio)
	    continue;
	dvb_lang_parse_audio(audio);
    }
}
