#!/bin/sh
inst=$(ls	/usr/share/automake*/install-sh \
		/usr/local/share/automake*/install-sh \
		2>/dev/null | head -1)
set -ex
autoconf
autoheader
rm -rf autom4te.cache
cp "$inst" .
