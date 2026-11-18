#!/bin/bash

HERE=`dirname $0`
source $HERE/../options.sh.src


set -o xtrace
if is-enable $(get-opt -ut $@); then WITH_UT=1; fi

brew install make cmake llvm

if (( $WITH_UT )); then
	# Install googletest systemwide.
	# The tests require C++17, so the latest googletest is fine.
	# The installation may fail, if so, CMakeLists.txt contains a fallback
	# for in-build installation.
	if ! brew install googletest; then
		echo "WARNING: googletest not installed from brew; CMake will fetch it"
	fi
fi
