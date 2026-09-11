#!/bin/bash
HERE=`dirname $0`
cd $HERE
curl -L -o codecov https://cli.codecov.io/latest/linux/codecov
chmod +x codecov
