#!/bin/bash

HERE=`dirname $0`
source $HERE/../options.sh.src


set -o xtrace
if is-enable $(get-opt -ut $@); then WITH_UT=1; fi

brew install make cmake llvm

if (( $WITH_UT )); then
	# googletest is not installed through brew: 1.12.1 (the last version supporting
	# C++11) can no longer be extracted since homebrew/core is no longer tapped locally.
	# CMake fetches release-1.12.1 from git when GTest 1.10...1.12 is not found.
	echo "Unit tests enabled: googletest will be fetched by CMake"
fi
