#!/bin/sh
station="$1"
audio=$(grep -ie "^$station:" /etc/vdr/channels.conf | cut -d: -f6)
video=$(grep -ie "^$station:" /etc/vdr/channels.conf | cut -d: -f7)
tzap "$station"&
tzap="$!"
sleep 5
kill $tzap
dvbstream -o -ps $audio $video | mplayer -
