#!/bin/bash

function addr_lookup() {
	perl -ne \
"/^(\w+)-(\w+).*\s(\S+)/ \
	and $1 >= hex(\$1) \
	and $1 <= hex(\$2) \
	and printf \" - %12x %6x %s\",hex(\$1),$1-hex(\$1),\$3;" \
	< pia.mmap
}

function addr_print () {
	sort | uniq -c | sort -n | tail |\
	while read line; do
		set -- $line
		count="$1"
		addr="$2"
		printf "%5d - %12s" "$count" "$addr"
		case "$addr" in
		    0x*)
			addr_lookup "$addr"
			;;
		esac
		echo
	done
}

######################################################################################

echo "free not alloc"
mtrace pia.mtrace \
	| awk '/was never alloc/ { print $8 }' \
	| addr_print
echo

echo "alloc not free"
mtrace pia.mtrace \
	| awk '/ at / { print $4 }' \
	| addr_print
echo