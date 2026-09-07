#!/bin/bash

DIR=$1

if [[ -z $DIR ]]; then
	DIR=.
fi

cd $DIR && cmake --build ./
