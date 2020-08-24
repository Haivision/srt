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

# What fields are there in every entry
set model {
	longname
	shortname
	id
}

# Logger definitions.
# Comments here allowed, just only for the whole line.

# Use values greater than 0. Value 0 is reserved for LOGFA_GENERAL,
# which is considered always enabled.
set loggers {
	GENERAL    gg  0
	SOCKMGMT   sm  1
	CONN       ca  2
	XTIMER     xt  3
	TSBPD      ts  4
	RSRC       rs  5
	CONGEST    cc  7
	PFILTER    pf  8
	API_CTRL   ac  11
	QUE_CTRL   qc  13
	EPOLL_UPD  ei  16

	API_RECV   ar  21
	BUF_RECV   br  22
	QUE_RECV   qr  23
	CHN_RECV   kr  24
	GRP_RECV   gr  25

	API_SEND   as  31
	BUF_SEND   bs  32
	QUE_SEND   qs  33
	CHN_SEND   ks  34
	GRP_SEND   gs  35

	INTERNAL   ip  41
	QUE_MGMT   qm  43
	CHN_MGMT   cm  44
	GRP_MGMT   gm  45
	EPOLL_API  ea  46
}

set hidden_loggers {
	# Haicrypt logging - usually off.
	HAICRYPT hc 6
 
    # defined in apps, this is only a stub to lock the value
	APPLOG   ap 10
}

set globalheader {
 /*
  WARNING: Generated from ../scripts/generate-logging-defs.tcl

  DO NOT MODIFY.

  Copyright applies as per the generator script.
 */

}


# This defines, what kind of definition will be generated
# for a given file out of the log FA entry list.

# Fields:
#  - prefix/postfix model
#  - logger_format
#  - hidden_logger_format

# COMMENTS NOT ALLOWED HERE! Only as C++ comments inside C++ model code.
set special {
	srtcore/logger_default.cpp {
		if {"$longname" == "HAICRYPT"} {
			puts $od "
#if ENABLE_HAICRYPT_LOGGING
		allfa.set(SRT_LOGFA_HAICRYPT, true);
#endif"
		}
	}
}

# COMMENTS NOT ALLOWED HERE! Only as C++ comments inside C++ model code.
set generation {
	srt.inc.h {
		{}
		{#define [format "%-20s %d" SRT_LOGFA_${longname} $id] // ${shortname}log}
		{#define [format "%-20s %d" SRT_LOGFA_${longname} $id] // ${shortname}log}
	}

    srtcore/logger_default.cpp {

        {
            $globalheader
            #include "srt.h"
            #include "logging.h"
            #include "logger_defs.h"

            namespace srt_logging
            {
                AllFaOn::AllFaOn()
                {
                    $entries
                }
            } // namespace srt_logging

        }

        {
            allfa.set(SRT_LOGFA_${longname}, true);
        }
    }

    srtcore/logger_defs.cpp {

        {
            $globalheader
            #include "srt.h"
            #include "logging.h"
            #include "logger_defs.h"

            namespace srt_logging { AllFaOn logger_fa_all; }
            // We need it outside the namespace to preserve the global name.
            // It's a part of "hidden API" (used by applications)
            SRT_API srt_logging::LogConfig srt_logger_config(srt_logging::logger_fa_all.allfa);

            namespace srt_logging
            {
                $entries
            } // namespace srt_logging
        }

        {
            Logger ${shortname}log(SRT_LOGFA_${longname}, srt_logger_config, "SRT.${shortname}");
        }
    }

    srtcore/logger_defs.h {
        {
            $globalheader
            #ifndef INC_SRT_LOGGER_DEFS_H
            #define INC_SRT_LOGGER_DEFS_H

            #include "srt.h"
            #include "logging.h"

            namespace srt_logging
            {
                struct AllFaOn
                {
                    LogConfig::fa_bitset_t allfa;
                    AllFaOn();
                };

                $entries

            } // namespace srt_logging

            #endif
        }

        {
            extern Logger ${shortname}log;
        }
    }

    apps/logsupport_appdefs.cpp {
        {
            $globalheader
            #include "logsupport.hpp"

            LogFANames::LogFANames()
            {
                $entries
            }
        }

        {
            Install("$longname", SRT_LOGFA_${longname});
        }

        {
            Install("$longname", SRT_LOGFA_${longname});
        }
    }
}

# EXECUTION

set here [file dirname [file normalize $argv0]]

if {[lindex [file split $here] end] != "scripts"} {
	puts stderr "The script is in weird location."
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

proc generate_file {od target} {

	global globalheader
	lassign [dict get $::generation $target] format_model pattern hpattern

    set ptabprefix ""

	if {$format_model != ""} {
		set beginindex 0
		while { [string index $format_model $beginindex] == "\n" } {
			incr beginindex
		}

		set endindex $beginindex
		while { [string is space [string index $format_model $endindex]] } {
			incr endindex
		}

		set tabprefix [string range $pattern $beginindex $endindex-1]

		set newformat ""
		foreach line [split $format_model \n] {
			if {[string trim $line] == ""} {
				append newformat "\n"
				continue
			}

			if {[string first $tabprefix $line] == 0} {
                set line [string range $line [string length $tabprefix] end]
			} 
            append newformat $line\n

            set ie [string first {$} $line]
            if {$ie != -1} {
                if {[string range $line $ie end] == {$entries}} {
                    set ptabprefix "[string range $line 0 $ie-1]"
                }
            }
		}

		set format_model $newformat
		unset newformat
	}

	set entries ""

	if {[string trim $pattern] != "" } {

        set prevval 0
 		set pattern [string trim $pattern]

		# The first "$::model" will expand into variable names
		# as defined there.
		foreach [list {*}$::model] [no_comments $::loggers] {
			if {$prevval + 1 != $id} {
				append entries "\n"
			}

			append entries ${ptabprefix}[subst -nobackslashes $pattern]\n
			set prevval $id
		}
	}

	if {$hpattern != ""} {
 		set hpattern [string trim $hpattern]
		foreach [list {*}$::model] [no_comments $::hidden_loggers] {
			append entries ${ptabprefix}[subst -nobackslashes $hpattern]\n
		}
	}

	if { [dict exists $::special $target] } {
		set code [subst [dict get $::special $target]]

		# The code should contain "append entries" !
		eval $code
	}

	set entries [string trim $entries]

    if {$format_model == ""} {
        set format_model $entries
    }

	# For any case, cut external spaces
	puts $od [string trim [subst -nocommands -nobackslashes $format_model]]
}

set entryfiles $argv

if {$entryfiles == ""} {
	set entryfiles [dict keys $generation]
} else {
	foreach ef $entryfiles {
		if { $ef ni [dict keys $generation] } {
			error "Unknown generation target: $entryfiles"
		}
	}
}

foreach f $entryfiles {

	# Set simple relative path, if the file isn't defined as path.
	if { [llength [file split $f]] == 1 } {
		set filepath $f
	} else {
		set filepath [file join $path $f] 
	}

	if { [file exists $filepath] } {
		puts "WARNING: will overwrite exiting '$f'. Hit ENTER to confirm, or Control-C to stop"
		gets stdin
	}

    puts stderr "Generating '$filepath'"
	set od [open $filepath w]
	generate_file $od $f
	close $od
}

puts stderr Done.

