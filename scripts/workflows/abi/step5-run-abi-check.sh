#!/bin/bash

echo "FILESYSTEM state before running abi-check at $PWD"
ls -l
sha256sum libsrt-base.dump
sha256sum libsrt-pr.dump

RES=0
abi-compliance-checker -l libsrt -old libsrt-base.dump -new libsrt-pr.dump || RES=$?
# Flatten the report for download-preview
cd compat_reports
REPORT=$(find . -name *.html)
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

