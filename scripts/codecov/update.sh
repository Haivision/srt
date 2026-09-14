#!/bin/bash
HERE=`dirname $0`
cd $HERE

VERSION=hardcoded
if [[ -n $1 ]]; then
	VERSION=$1
fi

if [[ $VERSION == latest ]]; then
	BASEURL=https://cli.codecov.io/latest/linux/codecov
	SHA=
else
	BASEURL=https://cli.codecov.io/v11.3.1/linux/codecov
	SHA="ca1d64196d2d34771084afe76ea657d581bf628e31d993ff8e52ea09cc88a56d  codecov"
	echo 'USING HARDCODED VERSION: 11.3.1. Use "update.sh latest" to get the latest version'
	echo 'NOTE: Using the latest version is unsafe as SHA is also downloaded over the network'
fi

echo "DOWNLOADING: $BASEURL"
rm -f codecov || { echo "Can't delete 'codecov'; please delete manually" ; exit 1; }

curl -L -o codecov $BASEURL
FILESHA=$(sha256sum codecov)

if [[ -z $SHA ]]; then
	echo "DOWNLOADING HASH: ${BASEURL}.SHA256SUM"
	curl -L -o codecov.SHA256SUM ${BASEURL}.SHA256SUM
	SHA=$(cat codecov.SHA256SUM)
	echo "Downloaded HASH: $SHA"
else
	echo "Hardcoded HASH: $SHA"
fi

if [[ "$FILESHA" == "$SHA" ]]; then
	echo "Checksum SHA256 matches."
else
	echo "ERROR: WRONG SHA256 CHECKSUM:"
	echo "EXPECTED: $SHA"
	echo "RECEIVED: $FILESHA"
	rm codecov
	echo "Corrupt file deleted"
	exit 1
fi

chmod +x codecov
