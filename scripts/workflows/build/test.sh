#!/bin/bash

DIR=$1

if [[ -z $DIR ]]; then
	DIR=.
fi

function is_linux()
{
	if [[ "`uname`" != "Linux" ]]; then
		return 1
	fi
	return 0
}

if is_linux; then
	echo "core.%e" | sudo tee /proc/sys/kernel/core_pattern
	ulimit -c unlimited
fi

cd $DIR || exit 1

ctest --extra-verbose
SUCCESS=$?

if is_linux; then
    if [[ -f core.test-srt ]]; then
		gdb -batch ./test-srt -c core.test-srt -ex bt -ex "info thread" -ex quit
	else
		echo "NO CORE - NO CRY!"
	fi
fi

# Propagate the status
test $SUCCESS == 0
