# 
# SRT - Secure, Reliable, Transport
# Copyright (c) 2017 Haivision Systems Inc.
# 
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>
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

# Options processed here internally, not passed to cmake
set internal_options {
	with-compiler-prefix=<prefix> "set C/C++ toolchains <prefix>gcc and <prefix>g++"
	with-compiler-type=<name> "compiler type: gcc(default), cc, others simply add ++ for C++"
	with-srt-name=<name> "Override srt library name"
	with-haicrypt-name=<name> "Override haicrypt library name (if compiled separately)"
	enable-debug "turn on debug+nonoptimized build mode (if =2, debug+optimized)"
}

# Options that refer directly to variables used in CMakeLists.txt
set cmake_options {
    cygwin-use-posix "Should the POSIX API be used for cygwin. Ignored if the system isn't cygwin. (default: OFF)"
    enable-c++11 "Should the c++11 parts (srt-live-transmit) be enabled (default: ON)"
    enable-c-deps "Extra library dependencies in srt.pc for C language (default: OFF)"
    enable-heavy-logging "Should heavy debug logging be enabled (default: OFF)"
    enable-logging "Should logging be enabled (default: ON)"
    enable-profile "Should instrument the code for profiling. Ignored for non-GNU compiler. (default: OFF)"
    enable-separate-haicrypt "Should haicrypt be built as a separate library file (default: OFF)"
    enable-shared "Should libsrt be built as a shared library (default: ON)"
    enable-static "Should libsrt be built as a static library (default: ON)"
    enable-suflip "Shuld suflip tool be built (default: OFF)"
    enable-thread-check "Enable #include <threadcheck.h> that implements THREAD_* macros"
    openssl-crypto-library=<filepath> "Path to a library."
    openssl-include-dir=<path> "Path to a file."
    openssl-ssl-library=<filepath> "Path to a library."
    pkg-config-executable=<filepath> "pkg-config executable"
    pthread-include-dir=<path> "Path to a file."
    pthread-library=<filepath> "Path to a library."
    use-gnutls "Should use gnutls instead of openssl (default: OFF)"
    use-static-libstdc++ "Should use static rather than shared libstdc++ (default: OFF)"
}

set options $internal_options$cmake_options

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

set haicrypt_name ""
set srt_name ""

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

	# Alias to old name --with-gnutls, which enforces using gnutls instead of openssl
	if { [info exists ::optval(--with-gnutls)] } {
		unset ::optval(--with-gnutls)
		set ::optval(--use-gnutls) ON
		puts "WARNING: --with-gnutls is a deprecated alias to --use-gnutls, please use the latter one"
	}

	if { [info exists ::optval(--with-target-path)] } {
		set ::target_path $::optval(--with-target-path)
		unset ::optval(--with-target-path)
		puts "NOTE: Explicit target path: $::target_path"
	}

	if { "--with-srt-name" in $::optkeys } {
		set ::srt_name $::optval(--with-srt-name)
		unset ::optval(--with-srt-name)
	}

	if { "--with-haicrypt-name" in $::optkeys } {
		set ::haicrypt_name $::optval(--with-haicrypt-name)
		unset ::optval(--with-haicrypt-name)
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
		set compiler_path ""
		set cmd [GetCompilerCommand]
		if { $cmd != "" } {
			set gcc_version [exec $cmd -v 2>@1]
			set target ""
			set compiler_path [file dirname $cmd]
			foreach l [split $gcc_version \n] {
				if { [string match Target:* $l] } {
					set target [lindex $l 1] ;# [0]Target: [1]x86_64-some-things-further
					set target_platform [lindex [split $target -] 0]  ;# [0]x86_64 [1]redhat [2]linux
					break
				}
			}

			if { $target_platform == "" } {
				puts "NOTE: can't obtain target from gcc -v: $l"
			} else {
				if { $target_platform != $::tcl_platform(machine) } {
					puts "NOTE: foreign target type detected ($target)" ;# - setting CROSSCOMPILING flag"
					#lappend ::cmakeopt "-DHAVE_CROSSCOMPILER=1"
					set iscross 1
				}
			}
		}
	}

	if { $::srt_name != "" } {
		lappend ::cmakeopt "-DTARGET_srt=$::srt_name"
	}

	if { $::haicrypt_name != "" } {
		lappend ::cmakeopt "-DTARGET_haicrypt=$::haicrypt_name"
	}

	set have_openssl 0
	if { [lsearch -glob $::optkeys --openssl*] != -1 } {
		set have_openssl 1
	}

	set have_gnutls 0
	if { [lsearch -glob $::optkeys --use-gnutls] != -1 } {
		set have_gnutls 1
	}

	if { $have_openssl && $have_gnutls } {
		puts "NOTE: SSL library is exclusively selectable. Thus, --use-gnutls option will be ignored"
		set have_gnutls 0
	}

	if { $have_gnutls } {
		lappend ::cmakeopt "-DUSE_GNUTLS=ON"
	}

	if {$iscross} {

		proc check-target-path {path} {
			puts "Checking path '$path'"
			if { [file isdir $path]
					&& [file isdir $path/bin]
					&& [file isdir $path/include]
					&& ([file isdir $path/lib] || [file isdir $path/lib64]) } {
				return yes
			}
			return no
		}

		if { ![info exists ::target_path] } {
			# Try to autodetect the target path by having the basic 3 directories.
			set target_path ""
			set compiler_prefix [file dirname $compiler_path] ;# strip 'bin' directory
			puts "NOTE: no --with-target-path found, will try to autodetect at $compiler_path"
			foreach path [list $compiler_path $compiler_prefix/$target] {
				if { [check-target-path $path] } {
					set target_path $path
					puts "NOTE: target path detected: $target_path"
					break
				}
			}

			if { $target_path == "" } {
				puts "ERROR: Can't determine compiler's platform files root path (using compiler command path). Specify --with-target-path."
				exit 1
			}
		} else {
			set target_path $::target_path
			# Still, check if correct.
			if { ![check-target-path $target_path] } {
				puts "ERROR: path in --with-target-path does not contain typical subdirectories"
				exit 1
			}
			puts "NOTE: Using explicit target path: $target_path"
		}

		# Add this for cmake, should it need for something
		lappend ::cmakeopt "-DCMAKE_PREFIX_PATH=$target_path"

		# Add explicitly the path for pkg-config
		# which lib
		if { [file isdir $target_path/lib64/pkgconfig] } {
			set ::env(PKG_CONFIG_PATH) $target_path/lib64/pkgconfig
			puts "PKG_CONFIG_PATH: Found pkgconfig in lib64 for '$target_path' - using it"
		} elseif { [file isdir $target_path/lib/pkgconfig] } {
			set ::env(PKG_CONFIG_PATH) $target_path/lib/pkgconfig
			puts "PKG_CONFIG_PATH: Found pkgconfig in lib for '$target_path' - using it"
		} else {
			puts "PKG_CONFIG_PATH: NOT changed, no pkgconfig in '$target_path'"
		}
		# Otherwise don't set PKG_CONFIG_PATH and we'll see.
	}

	if { $::HAVE_DARWIN && !$toolchain_changed} {

		if { $have_gnutls } {
			# Use gnutls explicitly, as found in brew
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

