#!/bin/bash -x

SRT_TAG_VERSION=$1
SRT_BASE=$2

if [[ -z $SRT_TAG_VERSION ]]; then
	echo "Parameters required: SRT_TAG_VERSION and SRT_BASE"
	exit 1
fi

echo "TAG:$SRT_TAG_VERSION BASE:$SRT_BASE"

#This is currently a paranoid check - the if should do the job
if [[ -z $SRT_BASE ]]; then
	echo "NO BASE DEFINED. NOT BUILDING"
	exit 1
fi

set -o xtrace

cd gitview_base
mkdir -p _build && cd _build

cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_BONDING=1 -DENABLE_PKTINFO=1 -DENABLE_MAXREXMITBW=1 ..

