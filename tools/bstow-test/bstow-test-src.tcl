#!/usr/bin/tclsh

set confobj ""

set frametime 0.0
set frame_timestride 0

set frames {}

proc print_line {size} {
	# State you already have config data, but
	# play times are set initially to 0.

	puts "FRAME \[[format {% 5d} $size]\] PLAY=$::frametime"
	set ::frametime [expr {$::frametime + $::frame_timestride}]

	# DISPLAY PAKCETS!
}

proc readfile {filename} {
	global confobj
	set fd [open $filename r]

	set have_param no

	while { [gets $fd line] != -1 } {
		if { [string index $line 0] == ":" } {
			# Header with config data
			set confdata [string range $line 1 end]
			foreach d $confdata {
				lassign [split $d =] lab val
				switch -- $lab {
					F {dict set confobj framerate $val}
					U {dict set confobj unitsize $val}
					P {dict set confobj packetsize $val}
					default {
						error "Unknown config parameter '$lab'"
					}
				}
			}
			set have_param yes
			set ::frame_timestride [expr {1.0/[dict get $confobj framerate]}]
			continue
		}

		if {!$have_param} {
			error "Parameters not specified"
		}

		# Line with packet size
		lappend ::frames $line
	}

	close $fd
}

proc round {value unitsize} {
	return [expr {($value/$unitsize)*$unitsize}]
}

proc updiv {size unit} {
	set ov [expr {$size % $unit ? 1 : 0}]
	set div [expr {$size / $unit + $ov}]
	return $div
}

readfile [lindex $argv 0]

# Ok, read a single GOP to calculate the average rate.

set avg 0

set begin_time 0
set end_time 0

#set time 0
set time $::frame_timestride
set total_size 0
set nf 0
set nf_next 0
set end_size 0

set packetsize [round 1500 [dict get $confobj unitsize]]

proc EstimateBitrate {frames} {


set npackets 0

set insync no

foreach ef $frames {
	set flags [lassign $ef fram]
	set nf $nf_next
	incr nf_next

	if {!$insync} {
		if {"i" ni $flags} {
			puts "FRAME #$nf: NO I-Frame yet, skipping"
			continue
		}
		set insync yes
	} else {
		if {"i" in $flags} {
			puts "Frame #$nf: NEXT I-Frame, STOPPED"
			break
		}
	}

	set f [expr {$fram * [dict get $confobj unitsize]}]
	incr total_size $f
	set time [expr {$time + $::frame_timestride}]

	set crate [expr {$f / $::frame_timestride}]
	set trate [expr {$total_size / ($time - $begin_time)}]
	set end_time $time

	set avg $trate
	set end_size $total_size

	puts "FRAME #$nf: $fram size=$f playtime=$time SELF RATE:$crate AVG RATE:$trate"
	incr npackets [updiv $f $packetsize]
}

incr nf
# Total playtime: TOTAL_FRAMES / FPS
set total_playtime_us [expr {($nf) * 1000000 / [dict get $confobj framerate]}]

set packet_playtime_us [expr {$total_playtime_us / $npackets}]

puts "OVERALL: GOP=$nf SIZE=$end_size PKTS=$npackets*$packetsize TIME=[expr {$total_playtime_us/1000000.0}]s RATE\[B/s\]=$avg"
puts "OVERALL RATE: $avg = [expr {$avg*8}]bps FRAME PERIOD: $frame_timestride PACKET PERIOD: $packet_playtime_us us"




puts "PACKET DISTRIBUTION: size=$packetsize "

set playtime_duration_us [expr 1000000 / [dict get $confobj framerate]]


#set time 0
set time $playtime_duration_us
set send_time 0
set send_time_global_base $playtime_duration_us

proc glob-sendtime {} {
	global send_time
	global send_time_global_base
	return [expr {$send_time_global_base + $send_time}]
}

set px 0
set nf 0

foreach ef $frames {
	set flags [lassign $ef fram]
	if {"i" in $flags} {
		incr send_time_global_base $send_time
		set send_time 0
	}
	set size [expr {$fram * [dict get $confobj unitsize]}]
	puts "FRAME #$nf: u=$fram size=$size playtime=[expr {$time / 1000000.0}]"

	set remain $size
	while {$remain} {
		set takesize [expr {min($packetsize, $remain)}]
		puts " --- PACKET #$px: size=$takesize sendtime=[expr $send_time/1000000.0] globsend=[expr [glob-sendtime]/1000000.0]"
		set send_time [expr {$send_time + $packet_playtime_us}]
		incr remain -$takesize
		incr px
	}
	incr time $playtime_duration_us
	incr nf
}
