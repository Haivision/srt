#! /usr/bin/bash

set -e
set -o pipefail

if [ -d Release.linux ]; then
	rm -rf ./Release.linux
fi

printf "\nBuilding Release\n"
mkdir Release.linux
cd Release.linux
cmake -S ../ -DENABLE_STDCXX_SYNC=ON -DENABLE_UNITTESTS=ON -DENABLE_EXPERIMENTAL_BONDING=ON -DENABLE_SWIG=ON
cmake --build ./