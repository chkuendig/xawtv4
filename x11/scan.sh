#!/bin/sh
set -x
scan -i 0 -o vdr ~/.tzap/de-Berlin | tee /etc/vdr/channels.conf.new
