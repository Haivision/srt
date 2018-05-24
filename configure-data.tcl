#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2018 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

# API description:

# Expected variables:
# - options: dictionary "option-name" : "description"
#   if there's '=' in option name, it expects an argument. Otherwise it's boolean.
# - alias: optional, you can make shortcuts to longer named options. Remember to use = in target name.
# 
# Optional procedures:
# - preprocess: run before command-line arguments ($argv) are reviewed
# - postprocess: run after options are reviewed and all data filled in
#
# Available variables in postprocess:
#
# - optval (array): contains all option names with their assigned values
# - cmakeopt (scalar): a list of all options for "cmake" command line


set options {
	enable-dynamic "compile SRT parts as shared objects (dynamic libraries)"
	disable-c++11 "turn off parts that require C++11 support"
	enable-debug "turn on debug+nonoptimized build mode"
	enable-profile "turn on profile instrumentation"
	with-compiler-prefix=<prefix> "set C/C++ toolchains <prefix>gcc and <prefix>g++"
	with-openssl=<prefix> "Prefix for OpenSSL installation (adds include,lib)"
	with-openssl-includedir=<incdir> "Use given path for OpenSSL header files"
	with-openssl-libdir=<libdir> "Use given path  for OpenSSL library path"
	with-openssl-libraries=<files> "Use given file list instead of standard -lcrypto"
	with-openssl-ldflags=<ldflags> "Use given -lDIR values for OpenSSL or absolute library filename"
	with-pthread-includedir=<incdir> "Use extra path for pthreads (usually for Windows)"
	with-pthread-ldflags=<flags> "Use specific flags for pthreads (some platforms require -pthread)"
	with-gnutls "Use GnuTLS"
}

# Just example. Available in the system.
set alias {
	--prefix --cmake-install-prefix=
}

proc pkg-config args {
	return [string trim [exec pkg-config {*}$args]]
}

proc flagval v {
	set out ""
	foreach o $v {
		lappend out [string trim [string range $o 2 en]]
	}
	return $out
}

proc preprocess {} {

	# Prepare windows basic path info
	set ::CYGWIN 0
	set e [catch {exec uname -o} res]
	# We have Cygwin, if uname -o returns "cygwin" and does not fail.
	if { !$e && $res == "Cygwin" } {
		set ::CYGWIN 1
		puts "CYGWIN DETECTED"
	}

	set ::HAVE_LINUX [expr {$::tcl_platform(os) == "Linux"}]
	set ::HAVE_DARWIN [expr {$::tcl_platform(os) == "Darwin"}]

	set ::CYGWIN_USE_POSIX 0
	if { "--cygwin-use-posix" in $::optkeys } {
		set ::CYGWIN_USE_POSIX 1
	}

	set ::HAVE_WINDOWS 0
	if { $::tcl_platform(platform) == "windows" } {
		puts "WINDOWS PLATFORM detected"
		set ::HAVE_WINDOWS 1
	}

	if { $::CYGWIN && !$::CYGWIN_USE_POSIX } {
		puts "CYGWIN - MINGW enforced"
		# Make Cygwin tools see it right, to compile for MinGW

		if { "--with-compiler-prefix" ni $::optkeys } {
			set ::optval(--with-compiler-prefix) /bin/x86_64-w64-mingw32-
		}

		# Extract drive C: information
		set drive_path [exec mount -p | tail -1 | cut {-d } -f 1]
		set ::DRIVE_C $drive_path/c
		set ::HAVE_WINDOWS 1
	} else {

		# Don't check for Windows, non-Windows parts will not use it.
		set ::DRIVE_C C:
	}

}

proc GetCompilerCommand {} {
	# Expect that the compiler was set through:
	# --with-compiler-prefix
	# --cmake-c[++]-compiler
	# (cmake-toolchain-file will set things up without the need to check things here)

	if { [info exists ::optval(--with-compiler-prefix)] } {
		set prefix $::optval(--with-compiler-prefix)
		return ${prefix}gcc
	}

	if { [info exists ::optval(--cmake-c-compiler)] } {
		return $::optval(--cmake-c-compiler)
	}

	if { [info exists ::optval(--cmake-c++-compiler)] } {
		return $::optval(--cmake-c++-compiler)
	}

	if { [info exists ::optval(--cmake-cxx-compiler)] } {
		return $::optval(--cmake-cxx-compiler)
	}

	puts "NOTE: Cannot obtain compiler, assuming toolchain file will do what's necessary"

	return ""
}

proc postprocess {} {

	set iscross 0

	# Check if there was any option that changed the toolchain. If so, don't apply any autodetection-based toolchain change.
	set all_options [array names ::optval]
	set toolchain_changed no
	foreach changer {
		--with-compiler-prefix
		--cmake-c-compiler
		--cmake-c++-compiler
		--cmake-cxx-compiler
		--cmake-toolchain-file
	} {
		if { $changer in $all_options } {
			puts "NOTE: toolchain changed by '$changer' option"
			set toolchain_changed yes
			break
		}
	}

	set cygwin_posix 0
	if { "--cygwin-use-posix" in $all_options } {
		# Will enforce OpenSSL autodetection
		set cygwin_posix 1
	}

	if { $toolchain_changed } {
		# Check characteristics of the compiler - in particular, whether the target is different
		# than the current target.
		set cmd [GetCompilerCommand]
		if { $cmd != "" } {
			set gcc_version [exec $cmd -v 2>@1]
			set target ""
			foreach l [split $gcc_version \n] {
				if { [string match Target:* $l] } {
					set name [lindex $l 1] ;# [0]Target: [1]x86_64-some-things-further
					set target [lindex [split $name -] 0]  ;# [0]x86_64 [1]redhat [2]linux
					break
				}
			}

			if { $target == "" } {
				puts "NOTE: can't obtain target from gcc -v: $l"
			} else {
				if { $target != $::tcl_platform(machine) } {
					puts "NOTE: foreign target type detected ($target) - setting CROSSCOMPILING flag"
					lappend ::cmakeopt "-DHAVE_CROSSCOMPILER=1"
					set iscross 1
				}
			}
		}
	}

	# Check if --with-openssl and the others are defined.

	set have_openssl 0
	if { [lsearch -glob $::optkeys --with-openssl*] != -1 } {
		set have_openssl 1
	}

	set have_gnutls 0
	if { [lsearch -glob $::optkeys --with-gnutls] != -1 } {
		set have_gnutls 1
	}

	if { $have_openssl && $have_gnutls } {
		puts "NOTE: SSL library is exclusively selectable. Thus, --with-gnutls option will be ignored"
		set have_gnutls 0
	}

	if { $have_gnutls } {
		lappend ::cmakeopt "-DUSE_GNUTLS=ON"
	}

	set have_pthread 0
	if { [lsearch -glob $::optkeys --with-pthread*] != -1 } {
		set have_pthread 1
	}

	# Autodetect OpenSSL and pthreads
	if { $::HAVE_WINDOWS } {

		if { !$have_openssl || !$have_gnutls } {
			puts "Letting cmake detect OpenSSL installation"
		} elseif { $have_gnutls } {
			puts "Letting cmake detect GnuTLS installation"
		} else {
			puts "HAVE_OPENSSL: [lsearch -inline $::optkeys --with-openssl*]"
		}


		if { !$have_pthread } {
			puts "Letting cmake detect PThread installation"
		} else {
			puts "HAVE_PTHREADS: [lsearch -inline $::optkeys --with-pthread*]"
		}
	}

	if { $::HAVE_LINUX || $cygwin_posix } {
		# Let cmake find openssl and pthread
	}

	if { $::HAVE_DARWIN } {

		if { $have_gnutls } {
			set er [catch {exec brew info gnutls} res]
			if { $er } {
				error "Cannot find gnutls in brew"
			}
		} else {
			# ON Darwin there's a problem with linking against the Mac-provided OpenSSL.
			# This must use brew-provided OpenSSL.
			#
			if { !$have_openssl } {
		
				set er [catch {exec brew info openssl} res]
				if { $er } {
					error "You must have OpenSSL installed from 'brew' tool. The standard Mac version is inappropriate."
				}

				lappend ::cmakeopt "-DOPENSSL_INCLUDE_DIR=/usr/local/opt/openssl/include"
				lappend ::cmakeopt "-DOPENSSL_LIBRARIES=/usr/local/opt/openssl/lib/libcrypto.a"
			}
		}
	}

}

