#!/usr/bin/tclsh

# A utility script to check the CMakeLists.txt for every
# expression that "potentially could be" a variable and its
# very first occurrence. This is to track if the variable
# wasn't used without being first properly set.

lassign $argv filename re

if {$re == ""} {
	puts stderr "Usage: [file tail $argv0] <cmake-filename> <expression>"
	exit 1
}

set f [open $filename r]

set firstoccur {}

set lno 0

while { [gets $f line] != -1 } {
	incr lno

	set line [string trim $line]
	if { [string index $line 0] == "#" } {
		# Skip comments
		continue
	}

	set parts ""
	set partid ""

	# Cut off parts that are whole strings. Identify which
	# parts are strings. Within strings, only ${...} are variables.

	set hasq [string first "\"" $line]
	if {$hasq == -1} {
		set parts "{$line}"
		set partid 0
	} else {

		set prevq 0
		while 1 {
			set part [string range $line 0 [expr $hasq-1]]
			lappend parts $part
			lappend partid 0

			set prevq [expr $hasq+1]

			set searchfrom $prevq
			set cont 1
			while 1 {
				set hasq [string first "\"" $line $searchfrom]
				if {$hasq != -1} {
					# Check if this quote was quoted
					if {[string index $line [expr $hasq-1]] == "\\"} {
						set searchfrom [expr {$hasq+1}]
						continue
					}
					break
				} else {
					# Unended quote - treat as ended.
					lappend parts [string range $line $prevq end]
					lappend partid 1
					set cont 0
					break
				}
			}
			if (!$cont) {
				break
			}

			# If found, search for the next one.

			lappend parts [string range $line $prevq [expr $hasq-1]]
			lappend partid 1
			set prevq [expr $hasq+1]
			set hasq [string first "\"" $line $prevq]

			if {$hasq == -1} {
				# No next quote - get until the end
				lappend parts [string range $line $prevq end]
				lappend partid 0
				break
			}
		}
	}

	#puts stderr "LINE: $line"
	#puts stderr "PARTS:"
	foreach p $parts i $partid {
		#set idname [expr {$i ? {string} : {plain}}]
		#puts stderr "  \[$p\] : $idname"

		if {$i} {
			set vars [regexp -inline -all {\$\{\m([A-Z0-9_]*)\M\}} $p]
		} else {
			set vars [regexp -inline -all {\m([A-Z0-9_]*)\M} $p]
		}
		#puts stderr "     -> $vars"

		foreach {unu v} $vars {
			set v [string trim $v]

			# Treat words with less than 3 letters as not a variable.
			if {[string length $v] < 3} {
				continue
			}

			if {[dict exists $firstoccur $v]} {
				# Already exists. Skip
				continue
			}

			dict set firstoccur $v $lno

			puts "$filename:$lno: $v"
		}
	}
}
