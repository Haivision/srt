#!/bin/bash

OUTPUT=$1

if [[ -z $OUTPUT ]]; then
	echo >&2 "ERROR: Please specify github output file, use - as stdout"
	exit 1
fi

WD=$PWD
HERE=`dirname $0`
$HERE/prepare-environment.sh

if [[ "$OUTPUT" == "-" ]]; then
	exec 3>&1
else
	exec 3>$OUTPUT
fi

cd gitview_pr
cd _build && cmake --build ./
make install DESTDIR=./installdir
SRT_TAG_VERSION=v$(../scripts/get-build-version.tcl full)
echo "SRT_TAG_VERSION=$SRT_TAG_VERSION" >&3
SRT_TAG=${SRT_TAG_VERSION}dev-$(git rev-parse --short HEAD)
echo "TAGGING PR BUILD: $SRT_TAG"
abi-dumper libsrt.so -o libsrt-pr.dump -public-headers installdir/usr/local/include/srt/ -lver $SRT_TAG
ls -ld libsrt-pr.dump
sha256sum libsrt-pr.dump
SRT_BASE=v$(../scripts/get-build-version.tcl base)

if [[ $SRT_TAG_VERSION == $SRT_BASE ]]; then
	echo "NOT CHECKING ABI: base version is being built: $SRT_TAG  (not emitting SRT_BASE)"
	#echo "SRT_BASE=''" >> "$GITHUB_OUTPUT"
else
	echo "WILL CHECK ABI changes $SRT_BASE - $SRT_TAG_VERSION"
	echo "SRT_BASE=$SRT_BASE" >&3
fi

exec 3>&-

cd $WD
cp gitview_pr/_build/libsrt-pr.dump .

exit 0
