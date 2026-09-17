#!/usr/bin/tclsh

# This script is required to parse the output of ts_analyuzer, which should
# be run with the MPEG-TS file with predefined bitrate. This script analyzes
# the "default text" output of this file and produces the *.bsrc file.

# Next the *.bsrc file can be used with `bstow-generate` application.

set fd [open [lindex $argv 0]]

set total ""
set current ""

proc foreach-i {r_index r_element slist block} {
	set size [llength $slist]
	upvar $r_index index
	upvar $r_element element

	for {set index 0} {$index < $size} {incr index} {
		set element [lindex $slist $index]

		set es [catch {uplevel $block} res]
		#puts stderr "DEBUG: EVAL result=$es return=$res"
		if {$es == 0 || $es == 4} {
			continue
		}
		break
	}

	switch -- $es {
		1 {
			error $res
		}

		2 {
			return -code return $res
		}

		default {
			return -code $es $res
		}
	}
}

proc flush-ts {} {
	global current
	global total

	if {$current == ""} {
		return
	}

	set part [lindex $current 0]
	lassign [split $part :] type spec

	# Collect the number of TS packets of the same type.
	set follow $type

	set count 0
	set lastx -1
	#puts stderr "DEBUG: checking series starting with $part"
	foreach-i i e $current {
		set part [lindex $current 0]
		set type [lindex [split $part :] 0]
		if {$type != $follow} {
			puts stderr "DEBUG: after $follow caught $type: total of $count"
			break
		}
		incr count
		set lastx $i
	}

	if {$i + 1 == [llength $current]} {
		set current ""
	} else {
		set current [lrange $current [expr {$i+1}] end]
	}

	set out $count
	if {$spec == "I"} {
		lappend out i
	}
	lappend total $out
}

# First find the PAT entry.

puts stderr "Finding PAT..."
while { [gets $fd line] } {
	if { [string range $line 0 2] != "I: " } {
		continue
	}

	if { [string first "pid=0000" $line] == -1 } {
		continue
	}

	break
}

if { [eof $fd] } {
	error "PAT not found"
}

# Read the next line; should specify the PMT PID
set line ""
gets $fd line

lassign $line number pmtpid

# Expected is only one program, so identify and find the PMT

lassign [split $pmtpid =] label pmtpid
if {$label != "PID"} {
	error "Wrong PAT entry: $line"
}

lappend current "T:PAT"

puts stderr "PMT PID = $pmtpid"

set pmtline ""

while { [gets $fd line] } {
	if { [string range $line 0 2] != "I: " } {
		continue
	}

	if { [string first "pid=$pmtpid" $line] == -1 } {
		continue
	}

	break
}

# Found PMT PID entry. Read next line - should be PMT
set pmtline $line

if {$pmtline == ""} {
	error "PMT NOT FOUND"
}

gets $fd pmtline

if {$pmtline != "PMT"} {
	error "Wrong PMT"
}

lappend current "T:PMT"

set pmtmap ""

dict set pmtmap 0000 T:PAT
dict set pmtmap $pmtpid T:PMT

while { [gets $fd line] } {
	set line [string trim $line]
	if {$line == ""} {
		break
	}

	set rest [lassign $line pidspec typespec typedesc]

	lassign [split $pidspec =] lab pidval
	if {$lab != "PID"} {
		error "Wrong PMT entry line: $line"
	}
	
	set pos [string first | $typedesc]
	if {$pos == -1 } {
		error "Wrong PMT entry typedesc: $typedesc"
	}

	set mediatype [string range $typedesc 1 $pos-1]
	dict set pmtmap $pidval $mediatype
}

# XXX Here you can try to read the remaining parts of PMT
# if it is split into multiple TS packets, possibly interleft
# by TS packets of other streams. Here we state that it fits
# in one TS packet.

puts stderr mappings:
foreach {key val} $pmtmap {
	puts stderr " - $val: pid=$key"
}

flush-ts

# Ok, now read the file and collect them

while { [gets $fd line] != -1 } {
	set line [string trim $line]

	if { [string range $line 0 2] != "I: " } {
		continue
	}

	set pos [string first "pid=" $line]
	if {$pos == -1} {
		error "Wrong stream line: $line"
	}

	set uline [string range $line $pos end-1]
	set flags [lassign $uline pidspec kind type media]
	lassign [split $pidspec =] lab pidval

	if {![dict exists $pmtmap $pidval] } {
		error "Unknown PID $pidval"
	}

	set pusi [expr {"PUSI" in $flags}]
	set rai [expr {"RAI" in $flags}]
	if {$pusi} {
		flush-ts
	}
	
	set frame [dict get $pmtmap $pidval]
	if {$pusi && $rai} {
		append frame ":I"
	}

	lappend current $frame
}

flush-ts

puts stderr "----------------------"
foreach l $total {
	puts $l
}

