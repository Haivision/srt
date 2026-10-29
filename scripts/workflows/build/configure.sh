#!/bin/bash

# Options supported:
# -ut on
# -enc on
# -werror no
# -sync <posix|=std>
# -std <98|=11|17>
# -bonding on
# -testing no
# -codecov no
# -examples no
# -logging on
# -debug no


# First argument is the directory.
# If first argument is option, directory is .

OPTMATCH="-*"
if [[ "$1" == $OPTMATCH ]]; then
	DIR=.
else
	DIR=$1
	shift
fi
OPTIONS=$*

HERE=`dirname $0`
source $HERE/../options.sh.src


CMAKE_ARGS=" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
CMAKE_ARGS+=" "-DENABLE_UNITTESTS=$(cmake_ON $(get-opt -ut $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_ENCRYPTION=$(cmake_OFF $(get-opt -enc $OPTIONS))
CMAKE_ARGS+=" "-DCMAKE_COMPILE_WARNING_AS_ERROR=$(cmake_ON $(get-opt -werror $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_STDCXX_SYNC=$(cmake_OFF_if posix $(get-opt -sync $OPTIONS))
CMAKE_ARGS+=" "-DUSE_CXX_STD=$(if_empty 11 $(get-opt -std $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_BONDING=$(cmake_OFF $(get-opt -bonding $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_TESTING=$(cmake_ON $(get-opt -testing $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_CODE_COVERAGE=$(cmake_ON $(get-opt -codecov $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_EXAMPLES=$(cmake_ON $(get-opt -examples $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_LOGGING=$(cmake_OFF $(get-opt -logging $OPTIONS))
CMAKE_ARGS+=" "-DENABLE_DEBUG=$(cmake_ON $(get-opt -debug $OPTIONS))
CMAKE_ARGS+=$(get-opt -- $OPTIONS)

set -o xtrace

mkdir -p $DIR && cd $DIR && cmake ../ $CMAKE_ARGS

