#!/bin/bash

[[ -f WORKS_DONE.properties ]] && source WORKS_DONE.properties

if [[ -n $DID_REPO ]]; then
	echo "SKIPPING REPO: already done"
	exit 0
fi

# NOTE: This step is to be run manually only. Github workflow
# will do the same through actions. Some duplicated settings are here, too.
# XXX Consider some common settings storage

set -o xtrace

export SRT_BASE=v1.5.0

WD=$PWD

PR_LABEL=`git rev-parse HEAD`

git clone --depth 1 --revision=$PR_LABEL git@github.com:Haivision/srt.git gitview_pr
git clone --depth 1 --branch $SRT_BASE git@github.com:Haivision/srt.git gitview_base



