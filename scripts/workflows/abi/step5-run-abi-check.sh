#!/bin/bash

echo "FILESYSTEM state before running abi-check at $PWD"
ls -l
sha256sum libsrt-base.dump
sha256sum libsrt-pr.dump

RES=0
EXTRA_ARGS=
for n in types symbols; do
	if [[ -f suppressed-$n.txt ]]; then
		EXTRA_ARGS+=" -skip-$n suppressed-$n.txt";
		echo "SUPPRESSED $n: -----------"
		cat suppressed-$n.txt
		echo "---------------------"
	else
		echo "NO $n suppression file"
	fi
done
set -o xtrace
abi-compliance-checker -l libsrt -old libsrt-base.dump -new libsrt-pr.dump -skip-added-constants $EXTRA_ARGS || RES=$?
set +o xtrace
# Flatten the report for download-preview
cd compat_reports
REPORT=$(find libsrt -name *.html)
if [[ -n $REPORT ]]; then
	cp $REPORT compat_report.html

	echo "SEE REPORT HERE:"
	echo
	echo compat_reports/compat_report.html
	echo
else
	echo "*** NO REPORT FOUND"
fi

exit $RES

