#!/usr/bin/tclsh

lassign $argv infile outfile

if {$infile == ""} {
	set here [file dirname [info script]]
	set infile [file join $here .. srtcore logging_defs.inc.cpp]
}

set id [open $infile]

if {$outfile == ""} {
	set od stdout
} else {
	set od [open $outfile]
}

set prevval 0

while 1 {
	set n [gets $id line]
	if {$n == -1} {
		break
	}

	# Cut off comments
	set nc [string first // $line]
	if {$nc != -1} {
		set line [string range $line 0 $nc-1]
	}
	set line [string trim $line]

	# Skip empty lines
	if {$line == ""} {
		continue
	}

	set line [string map { ( " \{" ) "\} " } $line]

	set arglist [lindex $line 1]
	set arglist [regsub -all ",\s*" $arglist " "]

	lassign $arglist logsym logmark logval

	set form [format "%-20s %d" SRT_LOGFA_$logsym $logval]

	if {$logval - $prevval != 1} {
		puts $od ""
	}

	puts $od "#define $form // $logmark"
	set prevval $logval
}
