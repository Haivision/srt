#!/bin/bash

if [[ "`id -u`" != "0" ]]; then
	echo "REQUIRED ROOT; attempting to resolve into sudo:"
	exec sudo $0 "$@"
fi

HERE=`dirname $0`
source $HERE/../options.sh.src

if [[ $(cmake_OFF $(get-opt -gdb $@)) != OFF ]]; then
	MAYBE_GDB=gdb
fi
	

if [[ $(cmake_OFF $(get-opt -ssl $@)) != OFF ]]; then
	MAYBE_SSL=libssl-dev
fi

apt update -y
apt install -y tcl cmake $MAYBE_SSL $MAYBE_GDB

