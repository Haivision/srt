#!/bin/bash

if [[ -z `type -p codespell` ]]; then
	echo >&2 "You need 'codespell' app to run the spell check."
	echo >&2 "Follow instructions in CONTRIBUTING.md document"
	exit 1
fi

function showhelp()
{
	echo "Usage: `basename $0` <workmode> <targets>"
	echo ""
	echo "WORK MODES:"
	echo "  * show [default] - only show spelling errors"
	echo "  * fix - write spelling error fixes directly to files"
	echo "  * check - like fix, but ask user to confirm ambiguous fix"
	echo "  * review - like fix, but ask user for every modification"
	echo "TARGETS:"
    echo "  * all [default] - check all files in the repository (including index)"
    echo "  * changed - check only files currently modified in the repository"
}


# Check if the script is run inside the repository view
GIT_TOP=`git rev-parse --show-toplevel` || (echo >&2 "Please run this script in the SRT repository directory"; exit)

# Forcefully enter the top repo dir; this is the only directory where it should be run
cd $GIT_TOP

WORKMODE=${1:-show}
EXTRACTION=${2:-all}

CS_OPTIONS=

case $WORKMODE in
	help | --help)
		showhelp
		exit
		;;
	show) 
		;;

	fix)
		CS_OPTIONS="-w "
		;;

	check)
		CS_OPTIONS="-w -i 2"
		;;

	review)
		CS_OPTIONS="-w -i 1"
		;;

	*)
		echo >&2 "Unknown mode. Use 'help' to list available options."
		exit 1
		;;
esac

CS_FILES=

if [[ $EXTRACTION == "all" ]]; then
	CS_FILES="git ls-files"
elif [[ $EXTRACTION == "changed" ]]; then
	CS_FILES="git ls-files -m"
else
	echo >&2 "Unknown extractopn spec '$EXTRACTION'. Use 'all' or 'changed'"
	exit 1
fi

# Unfortunately this isn't Tcl, so we need to do it "space safe" way.
declare -a FILELIST
eval FILELIST=( $($CS_FILES | awk "{print \"'\" \$1 \"'\"}") )

if [[ -z ${FILELIST[@]} ]]; then
	echo "SPELLCHECK: no files listed, not checking."
	exit 0
fi

#echo Running in files from $CS_FILE: $FILELIST

codespell --config scripts/codespell/codespell.cfg $CS_OPTIONS ${FILELIST[@]}
