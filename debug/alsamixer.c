#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>

#include <alsa/asoundlib.h>

static void dump_channels(char *device)
{
    snd_mixer_t          *mixer = NULL;
    snd_mixer_elem_t     *elem;
    snd_mixer_selem_id_t *sid;
    int                  n;
    
    snd_mixer_selem_id_alloca(&sid);
    
    if (0 != snd_mixer_open(&mixer,0))
	goto err;
    if (0 != snd_mixer_attach(mixer, device))
	goto err;
    if (0 != snd_mixer_selem_register(mixer, NULL, NULL))
	goto err;
    if (0 != snd_mixer_load(mixer))
	goto err;

    n = 0;
    for (elem = snd_mixer_first_elem(mixer); NULL != elem;
	 elem = snd_mixer_elem_next(elem)) {
	if (!snd_mixer_selem_is_active(elem))
	    continue;
	snd_mixer_selem_get_id(elem,sid);

	fprintf(stdout,
		"%d %s [%s,%s,%s]\n"
		"\tplayback   %s%s%s%s%s%s%s%s\n"
		"\tcapture    %s%s%s%s%s%s%s%s\n"
		"\n",
		snd_mixer_selem_id_get_index(sid),
		snd_mixer_selem_id_get_name(sid),

		snd_mixer_selem_is_enumerated(elem)     ? "enumerated" : "",
		snd_mixer_selem_has_common_volume(elem) ? "cvolume"    : "",
		snd_mixer_selem_has_common_switch(elem) ? "cswitch"    : "",

		snd_mixer_selem_is_playback_mono(elem)           ? " mono" : "",
		snd_mixer_selem_has_playback_channel(elem,0)     ? " ch0"  : "",
		snd_mixer_selem_has_playback_channel(elem,1)     ? " cd1"  : "",
		snd_mixer_selem_has_playback_channel(elem,2)     ? " cd2"  : "",
		snd_mixer_selem_has_playback_channel(elem,3)     ? " cd3"  : "",
		snd_mixer_selem_has_playback_channel(elem,4)     ? " cd4"  : "",
		snd_mixer_selem_has_playback_volume(elem)        ? " vol"  : "",
		snd_mixer_selem_has_playback_volume_joined(elem) ? " volj" : "",

		snd_mixer_selem_is_capture_mono(elem)            ? " mono" : "",
		snd_mixer_selem_has_capture_channel(elem,0)      ? " ch0"  : "",
		snd_mixer_selem_has_capture_channel(elem,1)      ? " ch1"  : "",
		snd_mixer_selem_has_capture_channel(elem,2)      ? " ch2"  : "",
		snd_mixer_selem_has_capture_channel(elem,3)      ? " ch3"  : "",
		snd_mixer_selem_has_capture_channel(elem,4)      ? " ch4"  : "",
		snd_mixer_selem_has_capture_volume(elem)         ? " vol"  : "",
		snd_mixer_selem_has_capture_volume_joined(elem)  ? " volj" : "");
    }

err:
    if (mixer)
	snd_mixer_close(mixer);
}

int main(int argc, char *argv[])
{
    dump_channels("hw:0");
    return 0;
}
