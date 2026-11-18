#!/usr/bin/tclsh
#*
#* SRT - Secure, Reliable, Transport
#* Copyright (c) 2020 Haivision Systems Inc.
#* 
#* This Source Code Form is subject to the terms of the Mozilla Public
#* License, v. 2.0. If a copy of the MPL was not distributed with this
#* file, You can obtain one at http://mozilla.org/MPL/2.0/.
#* 
#*/
#
#*****************************************************************************
#written by
#  Haivision Systems Inc.
#*****************************************************************************

lassign $argv configfile

if {$configfile == ""} {
	puts stderr "Usage: [file tail $argv0] <configfile>"
	puts stderr "MIND that the script is run in accordance to relative directories defined there"
	exit 1
}

source $configfile

# COMMENTS NOT ALLOWED HERE! Only as C++ comments inside C++ model code.
# (NOTE: Tcl syntax highlighter will likely falsely highlight # as comment here)
#
# Model:  TARGET-NAME { format-model logger-pattern hidden-logger-pattern }
# where:
#
#   format-model: Text for the whole file.
#        - Use $loggers_globalheader in the beginning
#        - Use $entries to place loggers_table' entries.
#        - Use {%PROCEDURE_NAME} to call a procedure to generate the pattern
#   logger-pattern: Pattern for a single logger entry, expanded for all loggers_table
#        - $shortname: two-letter name used to compose the logger symbol name (with -log added)
#        - $longname: symbolic name suffix
#        - $description: to be placed in comments
#
# Special syntax:
#
# %<command> : a high-level command execution. This declares a command that
# must be executed to GENERATE the model. Then, [subst] is executed
# on the results.
#
# = : when placed as the hidden-logger-pattern, it's equal to logger-pattern.
#
# NOTE: NO TABS ALLOWED. INDENT WITH SPACES ONLY.
set generation {

    cpp {

        {
            $loggers_globalheader
            #include "logging.h"
            #include "${loggers_modulename_header}"

            $loggers_namespace_begin
                ${loggers_config_defn}
                ${loggers_generallink_defn}

                $entries
            $loggers_namespace_end
        }

        {
            // $description
            hvu::logging::Logger ${shortname}${loggers_varsuffix}("${longname}", ${loggers_config_use}, ${loggers_enabled}, "${loggers_prefix}${shortname}");
        }
    }

    h {
        {
            $loggers_globalheader
            #ifndef $loggers_modulename_macroguard
            #define $loggers_modulename_macroguard

            #include "logging.h"

            $loggers_namespace_begin
                ${loggers_config_decl}
                ${loggers_generallink_decl}

                $entries
            $loggers_namespace_end

            #endif
        }

        {
            extern hvu::logging::Logger ${shortname}${loggers_varsuffix};
        }
    }
}

# Post-processing of the configuration

if {![info exists loggers_modulename_header]} {
	set loggers_modulename_header "[file tail $loggers_modulename].h"
}

set upref [string map {. _} $loggers_namespace]
set ufile [file tail $loggers_modulename]
set loggers_modulename_macroguard [string toupper ${upref}_${ufile}_H]

proc DefineNamespaceBounds {nsspec r_begin r_end} {
	upvar $r_begin begin
	upvar $r_end end

	set parts [split $nsspec .]
	set ndepth [llength $parts]

	for {set i 0} {$i < $ndepth} {incr i} {
		append begin "namespace [lindex $parts $i] \{ "
		append end "\} "
	}
}

set loggers_namespace_begin ""
set loggers_namespace_end ""

DefineNamespaceBounds $loggers_namespace loggers_namespace_begin loggers_namespace_end

set loggers_config_decl "extern hvu::logging::LogConfig& ${loggers_configname}();"
set loggers_config_defn "hvu::logging::LogConfigSingleton ${loggers_configname}_si;
    hvu::logging::LogConfig& ${loggers_configname}() { return ${loggers_configname}_si.instance();}"
set loggers_config_use "${loggers_configname}()"

set loggers_generallink_decl ""
set loggers_generallink_defn ""

if {[info exists loggers_generallink]} {
	set loggers_generallink_decl "extern hvu::logging::Logger& $loggers_generallink;"
	set loggers_generallink_defn "hvu::logging::Logger& ${loggers_generallink} = ${loggers_config_use}.general;"
}

set pattern_vars [info vars loggers_*]

# Processing utilities for 'generation'

proc get_trim_prefix_length {model} {
	# The model consists of lines; we state that the
	# definition in the configuration should be appropriately
	# indented, but this indentation should be removed or at least
	# refaxed for the needs of generation.

	# So we take the indentation from the first line and that
	# should be the removable indentation. Any deeper indentation
	# in any next line should be only the extra indentation.

	set lines [split $model \n]
	foreach l $lines {
		set tl [string trim $l]
		if {$tl == ""} {
			continue
		}
		set tl [string trimleft $l]

		# The size of the indent prefix is the length
		# difference between the original and trimmed line
		return [expr { [string length $l] - [string length $tl] } ]
	}

	return 0
}

proc get_indent {l} {
	set tl [string trimleft $l]
	return [expr {[string length $l] - [string length $tl]} ]
}

# lprefix: indent size found in the line
# prefixlen: general prefix in the source format
proc indent_size {lprefix prefixlen} {
	if {$lprefix < $prefixlen} {
		return -1
	}

	return [expr {$lprefix - $prefixlen}]
}

proc reindent {line prefixlen} {
    set lprefix [get_indent $line]
    set indent [indent_size $lprefix $prefixlen]
    if {$indent == -1} {
		# Line is shorter than the original prefix, so return original
		return $line
	}

    return [string repeat " " $indent][string trimleft $line]
}


# EXECUTION

set here [file dirname [file normalize $argv0]]

# if {[lindex [file split $here] end] != "scripts"} {
# 	puts stderr "The script is in weird location."
# 	exit 1
# }

set outdir [file dirname $loggers_modulename]
if {![file exists $outdir] || ![file isdirectory $outdir]} {
	puts stderr "ERROR: The directory for the output files '$outdir' doesn't exist"
	exit 1
}

set path [file join {*}[lrange [file split $here] 0 end-1]]

# Utility. Allows to put line-oriented comments and have empty lines
proc no_comments {input} {
	set output ""
	foreach line [split $input \n] {
		set nn [string trim $line]
		if { $nn == "" || [string index $nn 0] == "#" } {
			continue
		}
		append output $line\n
	}

	return $output
}

proc generate_entries_from_table {ptabprefix pattern} {

	#puts "PTABPREFIX: '$ptabprefix'"

	foreach v $::pattern_vars {
		global $v
	}

	# For the [subst] call, use variables
	# longname
	# shortname
	# description

	foreach line [split $::loggers_table \n] {
		set line [string trim $line]

		# Skip empty lines and comment lines
		if {$line == "" || [string index $line 0] == "#"} {
			continue
		}

		set description [lassign $line longname shortname]

		# Strip one embedding level
		if {[llength $description] == 1} {
			set description [lindex $description 0]
		}
		append entries "${ptabprefix}[string trimleft [subst -nobackslashes $pattern]]\n"
	}

	return $entries
}

proc reindent_all {text indentsize} {
    set lines [split $text \n]

    # Find the first non-empty line
    for {set ix 0} {$ix < [llength $lines]} {incr ix} {
        if {[string trim [lindex $lines $ix]] != ""} {
            break
        }
    }
    if {$ix != 0} {
        set lines [lrange $lines $ix end]
    }

    set remindent [get_indent [lindex $lines 0]]

    set out ""

    foreach l $lines {
        set tl [string trimleft $l]
        if {$tl == ""} {
            append out \n
        } else {
            append out [string repeat " " $remindent]$tl\n
        }
    }
    return $out
}

proc generate_file {od target} {

	foreach v $::pattern_vars {
		global $v
	}

    # Here we have:
    # format_model: format for the whole file, should contain $entries
    # pattern: pattern for every entry, to be replaced by $entries
	lassign [dict get $::generation $target] format_model pattern hpattern

    set ptabprefix ""

	if {[string index $format_model 0] == "%"} {
		set command [string range $format_model 1 end]
		set format_model [eval $command]
	}

	if {$format_model != ""} {

        set indentlen [get_trim_prefix_length $format_model]

		set newformat ""
        set trimmed 0
		foreach line [split $format_model \n] {


			if {[string trim $line] == ""} {
                if {$trimmed} {
				    append newformat "\n"
                }
				continue
			}
            set trimmed 1

            set line [reindent $line $indentlen]
            append newformat $line\n

            set ie [string first {$} $line]
            if {$ie != -1} {
                if {[string range $line $ie end] == {$entries}} {
                    set ptabprefix [string repeat " " [get_indent $line]]
                    #puts "CAUGHT ENTRIES: '$line' indent size:[get_indent $line] PTAB '$ptabprefix'"
                }
            }
		}

		set format_model $newformat
		unset newformat
	}

	set entries ""

	if {[string trim $pattern] != "" } {

        set prevval 0
 		set pattern [reindent_all $pattern [string length $ptabprefix]]

        #puts "ENTRIES PATTERN: '$pattern' PTABPREFIX: '$ptabprefix'"

		append entries [generate_entries_from_table $ptabprefix $pattern]
	}

	if {$hpattern != ""} {
		if {$hpattern == "="} {
			set hpattern $pattern
		} else {
 			set hpattern [string trim $hpattern]
		}

		# Extra line to separate from the normal entries
		append entries "\n"
		append entries [generate_entries_from_table $ptabprefix $hpattern]
	}

    # --- if { [dict exists $::special $target] } {
    # --- 	set code [subst [dict get $::special $target]]
    # --- 
    # --- 	# The code should contain "append entries" !
    # --- 	eval $code
    # --- }

	#set entries [string trim $entries]
    set entries [string trimleft [reindent_all $entries [string length $ptabprefix]]]

    if {$format_model == ""} {
        set format_model $entries
    }

    #puts "ENTRY SUBST: '$format_model'"

	# For any case, cut external spaces
	puts $od [string trim [subst -nocommands -nobackslashes $format_model]]
}

proc debug_vars {list} {
	set output ""
	foreach name $list {
		upvar $name _${name}
		lappend output "${name}=[set _${name}]"
	}

	return $output
}

# MAIN

set entryfiles [dict keys $generation]

foreach suf $entryfiles {

	set f ${loggers_modulename}.$suf

	# Set simple relative path, if the file isn't defined as path.
	if { [llength [file split $f]] == 1 } {
		set filepath $f
	} else {
		set filepath [file join $path $f] 
	}

    puts stderr "Generating '$filepath'"
	set od [open $filepath.tmp w]
	generate_file $od $suf
	close $od
	if { [file exists $filepath] } {
		puts stderr "WARNING: will overwrite exiting '$f'. Hit ENTER to confirm, or Control-C to stop"
		gets stdin
	}

	file rename -force $filepath.tmp $filepath
}

puts stderr Done.

