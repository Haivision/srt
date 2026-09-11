#!/bin/bash

SRT_BASE=$1
if [[ -z $SRT_BASE ]]; then
	echo "Parameter required: SRT_BASE"
	exit 1
fi

WD=$PWD
HERE=`dirname $0`
$HERE/prepare-environment.sh

set -o xtrace

cd gitview_base      
cd _build && cmake --build ./
make install DESTDIR=./installdir
echo "TAGGING BASE BUILD: $SRT_BASE"
abi-dumper libsrt.so -o libsrt-base.dump -public-headers installdir/usr/local/include/srt/ -lver $SRT_BASE
ls -ld libsrt-base.dump
sha256sum libsrt-base.dump

cd $WD
cp gitview_base/_build/libsrt-base.dump .

