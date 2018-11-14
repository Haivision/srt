#!/usr/bin/tclsh

set server_running 0
set theend 0

set nread 0
set nwritten 0

proc ReadBack {fd} {

	set r [read $fd 4096]
	if {$r == ""} {

		if {[eof $fd]} {
			set ::server_running 0
			close $fd
			return
		}
		if {!$::server_running} {
			# --- puts stderr "NOTHING MORE TO BE READ - exitting"
			set ::theend 1
		}

		# --- puts stderr "SPURIOUS, not reading"
		return
	}

	# --- puts stderr "REPRINTING [string bytelength $r] bytes"
	puts -nonewline stdout $r
	incr ::nwritten [string bytelength $r]
	# --- puts stderr "DONE"

	if {[fblocked $fd]} {
		# Nothing more to read
		if {$::nread < $::nwritten && !$::server_running} {
			puts stderr "NOTHING MORE TO BE READ - exitting"
			set ::theend 1
		}
		return
	}

	after idle "ReadBack $fd"
}

proc SendToSocket {fd} {
	global theend

	if { !$::server_running } {
		# --- puts stderr "SERVER DOWN, not reading"
		fileevent stdin readable {}
		return
	}
	# --- puts stderr "READING cin"
	set r [read stdin 4096]
	if {$r == ""} {
		if {[eof stdin]} {
			# --- puts stderr "EOF, setting server off"
			set ::server_running 0
			return
		}
		# --- puts stderr "SPURIOUS, not reading"
		return
	}

	# --- puts stderr "SENDING [string bytelength $r] bytes"
	# Set blocking for a short moment of sending
	# in order to prevent losing data that must wait
	fconfigure $fd -blocking yes
	puts -nonewline $fd $r
	incr ::nread [string bytelength $r]
	fconfigure $fd -blocking no

	if {[fblocked stdin]} {
		# Nothing more to read
		return
	}
	after idle "SendToSocket $fd"
}

set fd [socket {*}$argv]
fconfigure $fd -encoding binary -translation binary -blocking no -buffering none
fileevent $fd readable "ReadBack $fd"

fconfigure stdin -encoding binary -translation binary -blocking no
fconfigure stdout -encoding binary -translation binary
fileevent stdin readable "SendToSocket $fd"

# --- puts stderr "READY, sending"
set server_running 1

vwait theend

close $fd
