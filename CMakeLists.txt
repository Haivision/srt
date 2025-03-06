#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2018 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

cmake_minimum_required (VERSION 3.10 FATAL_ERROR)
set (SRT_VERSION 1.6.0)
# Also sets SRT_VERSION_MAJOR, SRT_VERSION_MINOR, SRT_VERSION_PATCH
project(SRT VERSION ${SRT_VERSION} LANGUAGES C CXX)


set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
include(haiUtil)
include(FindPkgConfig)
# XXX See 'if (MINGW)' condition below, may need fixing.
include(FindThreads)
include(CheckFunctionExists)
include(GNUInstallDirs)
include(FindPThreadGetSetName)
include(CheckGCCAtomicIntrinsics)
include(CheckCXXAtomic)
include(CheckCXXStdPutTime)

# This is required in some projects that add some other sources
# to the SRT library to be compiled together (aka "virtual library").
if (DEFINED SRT_EXTRA_LIB_INC)
	include(${SRT_EXTRA_LIB_INC}.cmake)
	# Expected to provide variables:
	# - SOURCES_srt_extra
	# - EXTRA_stransmit
endif()

# Required before platform shortcuts
option(USE_POSIX_CYGWIN "Should the POSIX API be used for cygwin. Ignored if the system isn't cygwin." OFF)

# ------------------
# Platform shortcuts
# ------------------
string(TOLOWER ${CMAKE_SYSTEM_NAME} SYSNAME_LC)
set1_if(DARWIN	   (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
					OR (${CMAKE_SYSTEM_NAME} MATCHES "iOS")
					OR (${CMAKE_SYSTEM_NAME} MATCHES "tvOS")
					OR (${CMAKE_SYSTEM_NAME} MATCHES "watchOS")
					OR (${CMAKE_SYSTEM_NAME} MATCHES "visionOS"))
set1_if(LINUX       ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
set1_if(BSD         ${SYSNAME_LC} MATCHES "bsd$")
# "MICROSOFT" means "compiling on Microsoft Windows using Visual Studio compiler"
set1_if(MICROSOFT   WIN32 AND (NOT MINGW AND NOT CYGWIN))
set1_if(GNU_OS      ${CMAKE_SYSTEM_NAME} MATCHES "GNU") # Use GNU_OS to not confuse with gcc
set1_if(ANDROID     ${SYSNAME_LC} MATCHES "android")
set1_if(SUNOS       ${SYSNAME_LC} MATCHES "sunos")
set1_if(POSIX       LINUX OR DARWIN OR BSD OR SUNOS OR ANDROID OR (CYGWIN AND USE_POSIX_CYGWIN) OR GNU_OS)
set1_if(SYMLINKABLE LINUX OR DARWIN OR BSD OR SUNOS OR CYGWIN OR GNU_OS)
set1_if(NEED_DESTINATION  ${CMAKE_VERSION} VERSION_LESS "3.14.0")


# This part to set the build type and associated other flags should not be done
# for build systems (cmake generators) that generate a multi-configuration
# build definition. At least it is known that MSVC does it and it sets _DEBUG
# and NDEBUG flags itself, so this shouldn't be done at all in this case.
if (CMAKE_CONFIGURATION_TYPES)
	if (DEFINED ENABLE_DEBUG)
		message(FATAL_ERROR "Do not define ENABLE_DEBUG in multi-config generators.")
	endif()
else()

	# In single configuration generators, you can do the following:
	# - do not provide ENABLE_DEBUG = rely on CMAKE_BUILD_TYPE.
	# - ENABLE_DEBUG=0 - force Release mode, define NDEBUG=1
	# - ENABLE_DEBUG=1 - force Debug mode, define _DEBUG=1
	# - ENABLE_DEBUG=2 - RelWithDebInfo mode, define _DEBUG=1 only if ENABLE_ASSERT.
	
	# You can enforce release by using --disable-debug and -DENABLE_DEBUG=0.
	# Check if this is the case.
	if (ENABLE_DEBUG EQUAL 0)
		set (CMAKE_BUILD_TYPE Release)
		set (SRT_DEBUG_MODE 0)
	elseif (ENABLE_DEBUG EQUAL 2)
		set (CMAKE_BUILD_TYPE RelWithDebInfo)
		set (SRT_DEBUG_MODE 2)
	elseif (ENABLE_DEBUG)
		set (CMAKE_BUILD_TYPE Debug)
		set (SRT_DEBUG_MODE 1)
	else()
		# Do not set CMAKE_BUILD_TYPE. Rely on it.
		if (CMAKE_BUILD_TYPE STREQUAL "Debug")
			set (SRT_DEBUG_MODE 1)
		elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
			set (SRT_DEBUG_MODE 2)
		else()
			set (SRT_DEBUG_MODE 0)
		endif()
	endif()
	# NOTE: DO NOT set CMAKE_BUILD_TYPE to anything if ENABLE_DEBUG
	# was not defined.

	# Set CMAKE_BUILD_TYPE properly, now that you know
	# that SRT_DEBUG_MODE is set as it should.
	if (SRT_DEBUG_MODE EQUAL 2)
		if (ENABLE_ASSERT)
			# Add _DEBUG macro if explicitly requested, to enable SRT_ASSERT().
			add_definitions(-D_DEBUG)
		else()
			add_definitions(-DNDEBUG)
		endif()
	elseif (SRT_DEBUG_MODE)
		# Add _DEBUG macro in debug mode only, to enable SRT_ASSERT().
		add_definitions(-D_DEBUG)
	else()
		add_definitions(-DNDEBUG)
	endif()

	# No longer needed and can be confusing
	unset (SRT_DEBUG_MODE)
endif()

message(STATUS "BUILD TYPE: ${CMAKE_BUILD_TYPE}")

# Add -D<macro>=1 for every ENFORCE_<macro> variable
addDefinitionsFromPrefixed( ENFORCE_ )
# NOTE: Known options you can change using ENFORCE_ variables:

# SRT_ENABLE_ECN 1                /* Early Congestion Notification (for source bitrate control) */
# SRT_DEBUG_TSBPD_OUTJITTER 1     /* Packet Delivery histogram */
# SRT_DEBUG_TRACE_DRIFT 1         /* Create a trace log for Encoder-Decoder Clock Drift */
# SRT_DEBUG_TSBPD_WRAP 1          /* Debug packet timestamp wraparound */
# SRT_DEBUG_TLPKTDROP_DROPSEQ 1
# SRT_DEBUG_SNDQ_HIGHRATE 1
# SRT_DEBUG_BONDING_STATES 1
# SRT_DEBUG_RTT 1                 /* RTT trace */
# SRT_MAVG_SAMPLING_RATE 40       /* Max sampling rate */
# SRT_ENABLE_FREQUENT_LOG_TRACE 0 : set to 1 to enable printing reason for suppressed freq logs

# ------------------------------------------
# Default option values (platform-dependent)
# (including autodetections if needed)
# ------------------------------------------

# Always turn logging on if the build type is debug
seton_if(ENABLE_PKTINFO_DEFAULT        BSD OR WIN32)

# seton_if somehow doesn't work with too complicated condition syntax
# We allow non-c++11 version for old GNU compiler only. All others must support C++11.
if (CMAKE_CXX_COMPILER_ID MATCHES "^GNU$" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.7)
	set (ENABLE_CXX11_DEFAULT OFF)
	set (ENABLE_STDCXX_SYNC_DEFAULT OFF)
else()
	set (ENABLE_CXX11_DEFAULT ON)
	set (ENABLE_STDCXX_SYNC_DEFAULT ON)
endif()

test_requires_clock_gettime(ENABLE_MONOTONIC_CLOCK_DEFAULT MONOTONIC_CLOCK_LINKLIB)

seton_if(ENABLE_PROFILE_DEFAULT  ENV{HAI_BUILD_PROFILE})

# -------
# options
# -------
# Build and code variant options
option(ENABLE_CXX11 "Should the c++11 parts (srt-live-transmit) be enabled" ${ENABLE_CXX11_DEFAULT})
option(ENABLE_APPS "Should the Support Applications be Built?" ON)
option(ENABLE_SHARED "Should libsrt be built as a shared library" ON)
option(ENABLE_STATIC "Should libsrt be built as a static library" ON)
option(ENABLE_PKTINFO "Enable using IP_PKTINFO to allow the listener extracting the target IP address from incoming packets" ${ENABLE_PKTINFO_DEFAULT})
option(ENABLE_RELATIVE_LIBPATH "Should application contain relative library paths, like ../lib" OFF)
option(ENABLE_CXX_DEPS "Extra library dependencies in srt.pc for the CXX libraries useful with C language" ON)
option(ENABLE_MONOTONIC_CLOCK "Enforced clock_gettime with monotonic clock on GC CV" ${ENABLE_MONOTONIC_CLOCK_DEFAULT})
option(ENABLE_STDCXX_SYNC "Use C++11 chrono and threads for timing instead of pthreads" ${ENABLE_STDCXX_SYNC_DEFAULT})
option(ENABLE_SOCK_CLOEXEC "Enable setting SOCK_CLOEXEC on a socket" ON)
# NOTE: Use SRT_USE_MUTEX_ATOMIC and will override the auto-detection of the
#  Atomic implemetation in srtcore/atomic.h.
option(SRT_USE_MUTEX_ATOMIC "Use srt::sync::Mutex to Implement Atomics" OFF)

# Development options
option(ENABLE_SHOW_PROJECT_CONFIG "Enable show Project Configuration" OFF)
option(ENABLE_CLANG_TSA "Enable Clang Thread Safety Analysis" OFF)
option(ENABLE_CODE_COVERAGE "Enable code coverage reporting" OFF)
option(ENABLE_PROFILE "Should instrument the code for profiling. Ignored for non-GNU compiler." ${ENABLE_PROFILE_DEFAULT})
option(ENABLE_TESTING "Should the Developer Test Applications be Built?" OFF)
option(ENABLE_GETNAMEINFO "In-logs sockaddr-to-string should do rev-dns" OFF)
option(ENABLE_LOGGING "Should logging be enabled" ON)
option(ENABLE_HEAVY_LOGGING "Should heavy debug logging be enabled" OFF)
option(ENABLE_HAICRYPT_LOGGING "Should logging in haicrypt be enabled" 0)
option(ENABLE_UNITTESTS "Enable unit tests" OFF)
option(ENABLE_UNITTESTS_DISCOVERY "Do not discover unit tests when enabled" ON)

# Built-in optional features
option(ENABLE_BONDING "Should the bonding functionality be enabled?" OFF)
option(ENABLE_ENCRYPTION "Enable encryption in SRT" ON)

# Experimental features
option(ENABLE_AEAD_API_PREVIEW "Enable AEAD API preview in SRT" Off)
option(ENABLE_MAXREXMITBW "Enable SRTO_MAXREXMITBW (v1.6.0 API preview)" Off)

# Variant selection
option(USE_STATIC_LIBSTDCXX "Should use static rather than shared libstdc++" OFF)
option(USE_OPENSSL_PC "Use pkg-config to find OpenSSL libraries" ON)
option(SRT_USE_OPENSSL_STATIC_LIBS "Link OpenSSL libraries statically." OFF)
option(USE_BUSY_WAITING "Enable more accurate sending times at a cost of potentially higher CPU load" OFF)
option(USE_GNUSTL "Get c++ library/headers from the gnustl.pc" OFF)

# Non-boolean options

set_default(USE_ENCLIB openssl-evp)
set(USE_ENCLIB "${USE_ENCLIB}" CACHE STRING "The crypto library that SRT uses")
set_property(CACHE USE_ENCLIB PROPERTY STRINGS "openssl" "openssl-evp" "gnutls" "mbedtls" "botan")

# --------------------------------------------
# Post-option variable synchronization
# and option-to-preprocessor-macro transition
# --------------------------------------------

# Global independet settings
add_definitions(
	-D_GNU_SOURCE
	-DSRT_VERSION="${SRT_VERSION}"
)

if (USE_BUSY_WAITING)
	message(STATUS "USE_BUSY_WAITING: ON")
	list(APPEND SRT_EXTRA_CFLAGS "-DUSE_BUSY_WAITING=1")
else()
	message(STATUS "USE_BUSY_WAITING: OFF (default)")
endif()

# Reduce the frequency of some frequent logs, milliseconds
set(SRT_LOG_SLOWDOWN_FREQ_MS_DEFAULT 1000) # 1s
if (NOT DEFINED SRT_LOG_SLOWDOWN_FREQ_MS)
	if (ENABLE_HEAVY_LOGGING)
		set(SRT_LOG_SLOWDOWN_FREQ_MS 0) # Just show every log message.
	else()
		set(SRT_LOG_SLOWDOWN_FREQ_MS ${SRT_LOG_SLOWDOWN_FREQ_MS_DEFAULT})
	endif()
endif()
list(APPEND SRT_EXTRA_CFLAGS "-DSRT_LOG_SLOWDOWN_FREQ_MS=${SRT_LOG_SLOWDOWN_FREQ_MS}")

if (ENABLE_MONOTONIC_CLOCK)
	if (NOT ENABLE_MONOTONIC_CLOCK_DEFAULT)
		message(FATAL_ERROR "Your platform does not support CLOCK_MONOTONIC. Build with -DENABLE_MONOTONIC_CLOCK=OFF.")
	endif()
	if (NOT MONOTONIC_CLOCK_LINKLIB STREQUAL "")
		set (WITH_EXTRALIBS "${WITH_EXTRALIBS} ${MONOTONIC_CLOCK_LINKLIB}")
	endif()
	add_definitions(-DENABLE_MONOTONIC_CLOCK=1)
endif()

if (USE_GNUSTL)
	pkg_check_modules (GNUSTL REQUIRED gnustl)
	link_directories(${GNUSTL_LIBRARY_DIRS})
	include_directories(${GNUSTL_INCLUDE_DIRS})
	set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
endif()

if (ENABLE_MAXREXMITBW)
	list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_MAXREXMITBW=1")
	message(STATUS "MAXREXMITBW API: ENABLED")
else()
	message(STATUS "MAXREXMITBW API: DISABLED")
endif()

# NOTE USAGE OF ENABLE_HEAVY_LOGGING:
# 
# - If defined as 1, add heavy logging to every configuration
# - If defined as 0, do not add heavy logging to any confiuration
# - If not defined, add heavy logging only for debug configuration

if (ENABLE_LOGGING)
	# Add -DENABLE_LOGGING=1 in any configuration
	# Add -DENABLE_HAICRYPT_LOGGING=1 if ENABLE_HAICRYPT_LOGGING
	# Add -DENABLE_HEAVY_LOGGING=1 if required

	set (LOGGING_ENABLE_DEFS "-DENABLE_LOGGING=1")
	if (ENABLE_HEAVY_LOGGING)
		list(APPEND LOGGING_ENABLE_DEFS "-DENABLE_HEAVY_LOGGING=1")
		message(STATUS "HEAVY LOGGING FLAGS: ENABLED (explicit)")
	elseif(DEFINED ENABLE_HEAVY_LOGGING AND ENABLE_HEAVY_LOGGING EQUAL 0)
		# If explicitly required to be off, do not add even for debug
		message(STATUS "HEAVY LOGGING FLAGS: DISABLED (explicit: '${ENABLE_HEAVY_LOGGING}')")
	else()
		# Otherwise add this, but only when generation debug mode
		message(STATUS "HEAVY LOGGING FLAGS: ENABLED (if debug build)")
		list(APPEND LOGGING_ENABLE_DEFS "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:ENABLE_HEAVY_LOGGING=1>")
	endif()

	if (ENABLE_HAICRYPT_LOGGING STREQUAL 2) # Allow value 2 for INSECURE DEBUG logging
		message(WARNING " *** ENABLED INSECURE HAICRYPT LOGGING - USE FOR TESTING ONLY!!! ***")
	endif()
	if (ENABLE_HAICRYPT_LOGGING)
		list(APPEND LOGGING_ENABLE_DEFS "-DENABLE_HAICRYPT_LOGGING=${ENABLE_HAICRYPT_LOGGING}")
	endif()
else()
	message(STATUS "LOGGING FLAGS: DISABLED")
endif()

# if (NOT ENABLE_LOGGING)
# 	set (ENABLE_HEAVY_LOGGING OFF)
# 	message(STATUS "LOGGING: DISABLED")
# else()
# 	if (ENABLE_HEAVY_LOGGING)
# 		message(STATUS "LOGGING: HEAVY")
# 	else()
# 		message(STATUS "LOGGING: ENABLED")
# 	endif()
# endif()
# 
# if (ENABLE_LOGGING)
# 	list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_LOGGING=1")
# 	if (ENABLE_HEAVY_LOGGING)
# 		list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_HEAVY_LOGGING=1")
# 	endif()
# 	if (ENABLE_HAICRYPT_LOGGING)
# 		if (ENABLE_HAICRYPT_LOGGING STREQUAL 2) # Allow value 2 for INSECURE DEBUG logging
# 			message(WARNING " *** ENABLED INSECURE HAICRYPT LOGGING - USE FOR TESTING ONLY!!! ***")
# 			list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_HAICRYPT_LOGGING=2")
# 		else()
# 			list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_HAICRYPT_LOGGING=1")
# 		endif()
# 	endif()
# endif()

if (ENABLE_GETNAMEINFO)
	list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_GETNAMEINFO=1")
endif()

if (ENABLE_PKTINFO)
	if (WIN32 OR BSD)
		message(FATAL_ERROR "PKTINFO is not implemented on Windows or *BSD.")
	endif()

	list(APPEND SRT_EXTRA_CFLAGS "-DSRT_ENABLE_PKTINFO=1")
endif()

if (ENABLE_BONDING)
	list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_BONDING=1")
	message(STATUS "ENABLE_BONDING: ON")
else()
	message(STATUS "ENABLE_BONDING: OFF")
endif()

if (ENABLE_THREAD_CHECK)
	if (NOT DEFINED WITH_THREAD_CHECK_INCLUDEDIR)
		message(FATAL_ERROR "If ENABLE_THREAD_CHECK, then WITH_THREAD_CHECK_INCLUDEDIR must be defined")
	endif()
	add_definitions(
		-DSRT_ENABLE_THREADCHECK=1
		-DFUGU_PLATFORM=1
		-I${WITH_THREAD_CHECK_INCLUDEDIR}
	)
endif()

if (ENABLE_CLANG_TSA)
	list(APPEND SRT_EXTRA_CFLAGS "-Wthread-safety")
	message(STATUS "Clang TSA: Enabled")
endif()

if (ENABLE_PROFILE)
	if (HAVE_COMPILER_GNU_COMPAT)
		# They are actually cflags, not definitions, but CMake is stupid enough.
		add_definitions(-g -pg)
		link_libraries(-g -pg)
	else()
		message(FATAL_ERROR "Profiling option is not supported on this platform")
	endif()
endif()

if (ENABLE_CODE_COVERAGE)
	if (HAVE_COMPILER_GNU_COMPAT)
		add_definitions(-g -O0 --coverage)
		link_libraries(--coverage)
		message(STATUS "ENABLE_CODE_COVERAGE: ON")
	else()
		message(FATAL_ERROR "ENABLE_CODE_COVERAGE: option is not supported on this platform")
	endif()
endif()

if (ENABLE_SOCK_CLOEXEC)
	add_definitions(-DENABLE_SOCK_CLOEXEC=1)
endif()

# For Cygwin with MinGW (cross-system compiling)
if (CYGWIN AND NOT USE_POSIX_CYGWIN)
	set(WIN32 1)
	set(CMAKE_LEGACY_CYGWIN_WIN32 1)
	add_definitions(-DWIN32=1 -DCYGWIN=1)
	message(STATUS "CYGWIN with NO POSIX. Setting backward compat CMAKE_LEGACY_CYGWIN_WIN32 and -DWIN32")
endif()

if (LINUX)
# This is an option supported only on Linux
	add_definitions(-DSRT_ENABLE_BINDTODEVICE)
endif()


# ---------------------------------------
# Configure compiler type and directories
# ---------------------------------------

# Make sure DLLs and executabes go to the same path regardles of subdirectory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

if (NOT DEFINED WITH_COMPILER_TYPE)

	# This is for a case when you provided the prefix, but you didn't
	# provide compiler type. This option is in this form predicted to work
	# only on POSIX systems. Just typical compilers for Linux and Mac are
	# included. 
	if (DARWIN)
		set (WITH_COMPILER_TYPE clang)
	elseif (POSIX) # Posix, but not DARWIN
		set(WITH_COMPILER_TYPE gcc)
	else()
		get_filename_component(WITH_COMPILER_TYPE ${CMAKE_C_COMPILER} NAME)
	endif()
	set (USING_DEFAULT_COMPILER_PREFIX 1)
endif()

if (NOT USING_DEFAULT_COMPILER_PREFIX OR DEFINED WITH_COMPILER_PREFIX)
	message(STATUS "Handling compiler with PREFIX=${WITH_COMPILER_PREFIX} TYPE=${WITH_COMPILER_TYPE}")

	parse_compiler_type(${WITH_COMPILER_TYPE} COMPILER_TYPE COMPILER_SUFFIX)

	if (${COMPILER_TYPE} STREQUAL gcc)
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}gcc${COMPILER_SUFFIX})
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}g++${COMPILER_SUFFIX})
		set (HAVE_COMPILER_GNU_COMPAT 1)
	elseif (${COMPILER_TYPE} STREQUAL cc)
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}cc${COMPILER_SUFFIX})
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}c++${COMPILER_SUFFIX})
		set (HAVE_COMPILER_GNU_COMPAT 1)
	elseif (${COMPILER_TYPE} STREQUAL icc)
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}icc${COMPILER_SUFFIX})
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}icpc${COMPILER_SUFFIX})
		set (HAVE_COMPILER_GNU_COMPAT 1)
	else()
		# Use blindly <command> for C compiler and <command>++ for C++.
		# At least this matches clang.
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}${WITH_COMPILER_TYPE})
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}${COMPILER_TYPE}++${COMPILER_SUFFIX})
		if (${COMPILER_TYPE} STREQUAL clang)
			set (HAVE_COMPILER_GNU_COMPAT 1)
		endif()
	endif()
	message(STATUS "Compiler type: ${WITH_COMPILER_TYPE}. C: ${CMAKE_C_COMPILER}; C++: ${CMAKE_CXX_COMPILER}")
	unset(USING_DEFAULT_COMPILER_PREFIX)
else()
	message(STATUS "No WITH_COMPILER_PREFIX - using C++ compiler ${CMAKE_CXX_COMPILER}")
endif()

if (USING_DEFAULT_COMPILER_PREFIX)
	# Detect if the compiler is GNU compatible for flags
	if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Intel|Clang|AppleClang")
		message(STATUS "COMPILER: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER}) - GNU compat")
		set(HAVE_COMPILER_GNU_COMPAT 1)

		# See https://gcc.gnu.org/projects/cxx-status.html
		# At the bottom there's information about C++98, which is default up to 6.1 version.
		# For all other compilers - including Clang - we state that the default C++ standard is AT LEAST 11.
		if (${CMAKE_CXX_COMPILER_ID} STREQUAL GNU AND ${CMAKE_CXX_COMPILER_VERSION} VERSION_LESS 6.1)
			message(STATUS "NOTE: GCC ${CMAKE_CXX_COMPILER_VERSION} is detected with default C++98. Forcing C++11 on applications.")
			set (FORCE_CXX_STANDARD 1)
		elseif (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang|AppleClang")
			message(STATUS "NOTE: CLANG ${CMAKE_CXX_COMPILER_VERSION} detected, unsure if >=C++11 is default, forcing C++11 on applications")
			set (FORCE_CXX_STANDARD 1)
		else() 
			message(STATUS "NOTE: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} - assuming default C++11.")
		endif()
	else()
		message(STATUS "COMPILER: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER}) - NOT GNU compat")
		set(HAVE_COMPILER_GNU_COMPAT 0)
	endif()

else() # Compiler altered by WITH_COMPILER_TYPE/PREFIX - can't rely on CMAKE_CXX_*

	# Force the C++ standard as C++11
	# HAVE_COMPILER_GNU_COMPAT was set in the handler of WITH_COMPILER_TYPE
	set (FORCE_CXX_STANDARD 1)
	message(STATUS "COMPILER CHANGED TO: ${COMPILER_TYPE} - forcing C++11 standard for apps")
endif()

# ---------------------------------------------------------
# Post-checks for having compiler type and version selected
# ---------------------------------------------------------

if (NOT ENABLE_CXX11)
	if (ENABLE_STDCXX_SYNC)
		message(FATAL_ERROR "ENABLE_STDCXX_SYNC is set, but C++11 is disabled by ENABLE_CXX11")
	endif()
	message(WARNING "Parts that require C++11 support will be disabled (srt-live-transmit)")
elseif (ENABLE_STDCXX_SYNC)
	add_definitions(-DENABLE_STDCXX_SYNC=1)
	if (DEFINED USE_CXX_STD)
		srt_check_cxxstd(${USE_CXX_STD} STDCXX STDPFX)
		# If defined, make sure it's at least C++11
		if (${STDCXX} LESS 11 OR ${STDCXX} EQUAL 98)
			message(FATAL_ERROR "If ENABLE_STDCXX_SYNC, then USE_CXX_STD must specify at least C++11")
		endif()
	else()
		set (USE_CXX_STD 11)
	endif()
endif()

if (DEFINED USE_CXX_STD)
	srt_check_cxxstd(${USE_CXX_STD} STDCXX STDPFX)

	if (${STDCXX} EQUAL 0)
		message(FATAL_ERROR "USE_CXX_STD: Must specify 03/11/14/17/20 possibly with c++/gnu++ prefix")
	endif()

	if (NOT STDCXX STREQUAL "")

		if (${STDCXX} LESS 11)
			if (ENABLE_STDCXX_SYNC)
				message(FATAL_ERROR "If ENABLE_STDCXX_SYNC, then you can't USE_CXX_STD less than 11")
			endif()
			# Set back to 98 because cmake doesn't understand 03.
			set (STDCXX 98)
			# This enforces C++03 standard on SRT.
			# Apps still use C++11

			# Set this through independent flags
			set (USE_CXX_STD_LIB ${STDCXX})
			set (FORCE_CXX_STANDARD 1)
			if (NOT ENABLE_APPS)
				set (USE_CXX_STD_APP ${STDCXX})
				message(STATUS "C++ STANDARD: library: C++${STDCXX}, apps disabled (examples will follow C++${STDCXX})")
			else()
				set (USE_CXX_STD_APP "")
				message(STATUS "C++ STANDARD: library: C++${STDCXX}, but apps still at least C++11")
			endif()
		elseif (FORCE_CXX_STANDARD_GNUONLY)
			# CMake is too old to handle CMAKE_CXX_STANDARD,
			# use bare GNU options.
			set (FORCE_CXX_STANDARD 1)
			set (USE_CXX_STD_APP ${STDCXX})
			set (USE_CXX_STD_LIB ${STDCXX})
			message(STATUS "C++ STANDARD: using C++${STDCXX} for all - GNU only")
		else()
			# This enforces this standard on both apps and library,
			# so set this as global C++ standard option
			set (CMAKE_CXX_STANDARD ${STDCXX})
			unset (FORCE_CXX_STANDARD)

			# Do not set variables to not duplicate flags
			set (USE_CXX_STD_LIB "")
			set (USE_CXX_STD_APP "")
			message(STATUS "C++ STANDARD: using C++${STDCXX} for all")
		endif()

		message(STATUS "C++: Setting C++ standard for gnu compiler: lib: ${USE_CXX_STD_LIB} apps: ${USE_CXX_STD_APP}")
	endif()
else()
	set (USE_CXX_STD_LIB "")
	set (USE_CXX_STD_APP "")
endif()

if (FORCE_CXX_STANDARD)
	message(STATUS "C++ STD: Forcing C++11 on applications")
	if (USE_CXX_STD_APP STREQUAL "")
		set (USE_CXX_STD_APP 11)
	endif()

	if (USE_CXX_STD_LIB STREQUAL "" AND ENABLE_STDCXX_SYNC)
		message(STATUS "C++ STD: Forcing C++11 on library, as C++11 sync requested")
		set (USE_CXX_STD_LIB 11)
	endif()
endif()

# add extra warning flags for gccish compilers
if (HAVE_COMPILER_GNU_COMPAT)
	set (SRT_GCC_WARN "-Wall -Wextra")
	if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		set (SRT_GCC_WARN "${SRT_GCC_WARN} -Wshadow=local")
	endif()
else()
	# cpp debugging on Windows :D
	#set (SRT_GCC_WARN "/showIncludes")
endif()

if (USE_STATIC_LIBSTDCXX)
	if (HAVE_COMPILER_GNU_COMPAT)
		set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++")
	else()
		message(FATAL_ERROR "On non-GNU-compat compiler it's not known how to use static C++ standard library.")
	endif()
endif()


# On Linux pthreads have to be linked even when using C++11 threads
if (ENABLE_STDCXX_SYNC AND NOT LINUX)
	message(STATUS "Pthread library: C++11")
elseif (PTHREAD_LIBRARY AND PTHREAD_INCLUDE_DIR)
	message(STATUS "Pthread library: ${PTHREAD_LIBRARY}")
	message(STATUS "Pthread include dir: ${PTHREAD_INCLUDE_DIR}")
elseif (MICROSOFT)
	# Formal, shoult never happen
	message(FATAL_ERROR "Using POSIX thread API with Windows is no longer supported. Use C++11 threads.")
else ()
	find_package(Threads REQUIRED)
	set(PTHREAD_LIBRARY ${CMAKE_THREAD_LIBS_INIT})
endif()


# ----------------------
# Basic system PP macros
# ----------------------

if(WIN32)
	message(STATUS "DETECTED SYSTEM: WINDOWS;  WIN32=1; PTW32_STATIC_LIB=1")
	add_definitions(-DWIN32=1 -DPTW32_STATIC_LIB=1)
elseif(DARWIN)
	message(STATUS "DETECTED SYSTEM: DARWIN (no macro added)")
elseif(BSD)
	message(STATUS "DETECTED SYSTEM: BSD;  BSD=1")
	add_definitions(-DBSD=1)
elseif(LINUX)
	add_definitions(-DLINUX=1)
	message(STATUS "DETECTED SYSTEM: LINUX;  LINUX=1" )
elseif(ANDROID)
	add_definitions(-DLINUX=1)
	message(STATUS "DETECTED SYSTEM: ANDROID;  LINUX=1" )
elseif(CYGWIN)
	add_definitions(-DCYGWIN=1)
	message(STATUS "DETECTED SYSTEM: CYGWIN (posix mode); CYGWIN=1")
elseif(GNU)
	add_definitions(-DGNU=1)
	message(STATUS "DETECTED SYSTEM: GNU;  GNU=1" )
elseif(SUNOS)
	add_definitions(-DSUNOS=1)
	message(STATUS "DETECTED SYSTEM: SunOS|Solaris;  SUNOS=1" )
else()
	message(FATAL_ERROR "Unsupported system: ${CMAKE_SYSTEM_NAME}")
endif()

# When you use crosscompiling, you have to take care that PKG_CONFIG_PATH
# and CMAKE_PREFIX_PATH are set properly.

# ----------------------------------------
# Code variants and explicit configuration
# ----------------------------------------

# symbol for inet_pton() exists in win32, but function does not.
if(WIN32)
	set(CMAKE_REQUIRED_LIBRARIES ws2_32)
	try_compile(AT_LEAST_VISTA
		${CMAKE_BINARY_DIR}
		"${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_vista.c")
	if(NOT AT_LEAST_VISTA)
		# force targeting Vista
		add_definitions(-D_WIN32_WINNT=0x0600)
	endif()
endif()

check_function_exists(inet_pton HAVE_INET_PTON)
if (DEFINED HAVE_INET_PTON)
	add_definitions(-DHAVE_INET_PTON=1)
endif()

# ---------------------------
# Configuration autodetection
# ---------------------------

# Find functions for getting/setting thread name
FindPThreadGetSetName()
# -> Defines HAVE_PTHREAD_GETNAME_* and HAVE_PTHREAD_SETNAME_*

# Check for GCC Atomic Intrinsics and C++11 Atomics.
# Sets:
#  HAVE_LIBATOMIC
#  HAVE_LIBATOMIC_COMPILES
#  HAVE_LIBATOMIC_COMPILES_STATIC
#  HAVE_GCCATOMIC_INTRINSICS
#  HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC
CheckGCCAtomicIntrinsics()
#  HAVE_CXX_ATOMIC
#  HAVE_CXX_ATOMIC_STATIC
CheckCXXAtomic()

if (SRT_USE_MUTEX_ATOMIC)
   add_definitions(-DATOMIC_USE_SRT_SYNC_MUTEX=1)
endif()


# Check for std::put_time():
# Sets:
#  HAVE_CXX_STD_PUT_TIME
CheckCXXStdPutTime()
if (HAVE_CXX_STD_PUT_TIME)
	add_definitions(-DHAVE_CXX_STD_PUT_TIME=1)
endif()



# -----------------------------------------------------------
# Configure encryption library dependency and haicrypt module
# -----------------------------------------------------------

if (ENABLE_ENCRYPTION)

	# XXX Backward compat. Remove if no longer needed.
	if (DEFINED OPENSSL_USE_STATIC_LIBS AND "${OPENSSL_USE_STATIC_LIBS}" STREQUAL "ON")
		message(WARNING "Use of OPENSSL_USE_STATIC_LIBS as SRT build option here is deprecated.
Please use SRT_USE_OPENSSL_STATIC_LIBS instead.")
		set(SRT_USE_OPENSSL_STATIC_LIBS ${OPENSSL_USE_STATIC_LIBS})
	endif()

	if ("${USE_ENCLIB}" STREQUAL "gnutls")
		set (SSL_REQUIRED_MODULES "gnutls nettle")
		if (WIN32)
			if (MINGW)
				set (SSL_REQUIRED_MODULES "${SSL_REQUIRED_MODULES} zlib")
			endif()
		endif()

		pkg_check_modules (SSL REQUIRED ${SSL_REQUIRED_MODULES})

		add_definitions(
			-DUSE_GNUTLS=1
		)

		link_directories(
			${SSL_LIBRARY_DIRS}
		)
	elseif ("${USE_ENCLIB}" STREQUAL "mbedtls")
		add_definitions(-DUSE_MBEDTLS=1)
		if ("${SSL_LIBRARY_DIRS}" STREQUAL "")
			set(MBEDTLS_PREFIX "${CMAKE_PREFIX_PATH}" CACHE PATH "The path of mbedtls")
			find_package(MbedTLS REQUIRED)
			set (SSL_INCLUDE_DIRS ${MBEDTLS_INCLUDE_DIR})
			set (SSL_LIBRARIES  ${MBEDTLS_LIBRARIES})
		endif()
		if (WIN32)
			set (SSL_LIBRARIES ${SSL_LIBRARIES} bcrypt)
		endif()
		if ("${SSL_LIBRARIES}" STREQUAL "")
			set (SSL_LIBRARIES  mbedtls mbedcrypto)
		endif()
		message(STATUS "SSL enforced mbedtls: -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")

		foreach(LIB ${SSL_LIBRARIES})
			if(IS_ABSOLUTE ${LIB} AND EXISTS ${LIB})
				set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${LIB})
			else()
				set(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} "-l${LIB}")
			endif()
		endforeach()
	elseif ("${USE_ENCLIB}" STREQUAL "openssl-evp")
		# Openssl-EVP requires CRYSPR2
		add_definitions(-DUSE_OPENSSL_EVP=1 -DCRYSPR2)
		set (SSL_REQUIRED_MODULES "openssl libcrypto")
		# Try using pkg-config method first if enabled,
		# fall back to find_package method otherwise
		if (USE_OPENSSL_PC)
			pkg_check_modules(SSL ${SSL_REQUIRED_MODULES})
			if (SRT_USE_OPENSSL_STATIC_LIBS)
				# use `pkg-config --static xxx` found libs
				set(SSL_LIBRARIES ${SSL_STATIC_LIBRARIES})
			endif()
		endif()
		if (SSL_FOUND)
			# We have some cases when pkg-config is improperly configured
			# When it doesn't ship the -L and -I options, and the CMAKE_PREFIX_PATH
			# is set (also through `configure`), then we have this problem. If so,
			# set forcefully the -I and -L contents to prefix/include and
			# prefix/lib.
			if ("${SSL_LIBRARY_DIRS}" STREQUAL "")
			if (NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
				message(STATUS "WARNING: pkg-config has incorrect prefix - enforcing target path prefix: ${CMAKE_PREFIX_PATH}")
				set (SSL_LIBRARY_DIRS ${CMAKE_PREFIX_PATH}/${CMAKE_INSTALL_LIBDIR})
				set (SSL_INCLUDE_DIRS ${CMAKE_PREFIX_PATH}/include)
			endif()
			endif()

			link_directories(
				${SSL_LIBRARY_DIRS}
			)
			message(STATUS "SSL via pkg-config: -L ${SSL_LIBRARY_DIRS} -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
		else()
			if (SRT_USE_OPENSSL_STATIC_LIBS)
				# use `pkg-config --static xxx` found libs
				set(OPENSSL_USE_STATIC_LIBS True)
				set(OPENSSL_MSVC_STATIC_RT True)
			endif()
			find_package(OpenSSL REQUIRED)
			set (SSL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
			set (SSL_LIBRARIES ${OPENSSL_LIBRARIES})
			message(STATUS "SSL via find_package(OpenSSL): -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
		endif()
	elseif ("${USE_ENCLIB}" STREQUAL "botan")
		add_definitions(-DUSE_BOTAN=1 -DCRYSPR2)
		set (SSL_REQUIRED_MODULES "botan")
		find_package(Botan 3.0.0 REQUIRED)
		botan_generate(
			botan
			ffi
			nist_keywrap
			aes_armv8
			aes_ni
			aes_power8
			aes_vperm
			idea_sse2
			serpent_avx2
			shacal2_armv8
			shacal2_avx2
			shacal2_x86
			sm4_armv8
			rdseed
			sha1_armv8
			sha1_sse2
			sha1_x86
			sha2_32_armv8
			sha2_32_bmi2
			sha2_32_x86
			sha2_64_bmi2
			sha3_bmi2
			zfec_sse2
			zfec_vperm
			argon2_avx2
			argon2_ssse3
			processor_rng
			chacha_avx2
			ghash_cpu
			ghash_vperm
			simd
			simd_avx2)
		target_compile_features("botan" PRIVATE "cxx_std_20")
		set (SSL_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR})
		set (SSL_LIBRARIES "botan")
	else() # openssl
		# Openssl (Direct-AES API) can use CRYSPR2
		add_definitions(-DUSE_OPENSSL=1 -DCRYSPR2)
			set (SSL_REQUIRED_MODULES "openssl libcrypto")
		# Try using pkg-config method first if enabled,
		# fall back to find_package method otherwise
		if (USE_OPENSSL_PC)
			pkg_check_modules(SSL ${SSL_REQUIRED_MODULES})
			if (SRT_USE_OPENSSL_STATIC_LIBS)
				# use `pkg-config --static xxx` found libs
				set(SSL_LIBRARIES ${SSL_STATIC_LIBRARIES})
			endif()
		endif()
		if (SSL_FOUND)
			# We have some cases when pkg-config is improperly configured
			# When it doesn't ship the -L and -I options, and the CMAKE_PREFIX_PATH
			# is set (also through `configure`), then we have this problem. If so,
			# set forcefully the -I and -L contents to prefix/include and
			# prefix/lib.
			if ("${SSL_LIBRARY_DIRS}" STREQUAL "")
			if (NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
				message(STATUS "WARNING: pkg-config has incorrect prefix - enforcing target path prefix: ${CMAKE_PREFIX_PATH}")
				set (SSL_LIBRARY_DIRS ${CMAKE_PREFIX_PATH}/${CMAKE_INSTALL_LIBDIR})
				set (SSL_INCLUDE_DIRS ${CMAKE_PREFIX_PATH}/include)
			endif()
			endif()

			link_directories(
				${SSL_LIBRARY_DIRS}
			)
			message(STATUS "SSL via pkg-config: -L ${SSL_LIBRARY_DIRS} -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
		else()
			if (SRT_USE_OPENSSL_STATIC_LIBS)
				# use `pkg-config --static xxx` found libs
				set(OPENSSL_USE_STATIC_LIBS True)
				set(OPENSSL_MSVC_STATIC_RT True)
			endif()
			find_package(OpenSSL REQUIRED)
			set (SSL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
			set (SSL_LIBRARIES ${OPENSSL_LIBRARIES})
			message(STATUS "SSL via find_package(OpenSSL): -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
		endif()

	endif()

	add_definitions(-DSRT_ENABLE_ENCRYPTION)
	message(STATUS "ENCRYPTION: ENABLED, using: ${SSL_REQUIRED_MODULES}")
	message (STATUS "SSL libraries: ${SSL_LIBRARIES}")

	if (ENABLE_AEAD_API_PREVIEW)
		if (("${USE_ENCLIB}" STREQUAL "openssl-evp") OR ("${USE_ENCLIB}" STREQUAL "botan"))
			add_definitions(-DENABLE_AEAD_API_PREVIEW)
			message(STATUS "ENCRYPTION AEAD API: ENABLED")
		else()
			message(FATAL_ERROR "ENABLE_AEAD_API_PREVIEW is only available with USE_ENCLIB=[openssl-evp | botan]!")
		endif()
	else()
		message(STATUS "ENCRYPTION AEAD API: DISABLED")
	endif()


	set (HAICRYPT_FILELIST_MAF "filelist-${USE_ENCLIB}.maf")

	MafReadDir(haicrypt ${HAICRYPT_FILELIST_MAF}
		SOURCES SOURCES_haicrypt
		PUBLIC_HEADERS HEADERS_haicrypt
		PROTECTED_HEADERS HEADERS_haicrypt
	)

else()
	message(STATUS "ENCRYPTION: DISABLED")
	message(STATUS "ENCRYPTION AEAD API: N/A")
endif()


message(STATUS "STDCXX_SYNC: ${ENABLE_STDCXX_SYNC}")
message(STATUS "MONOTONIC_CLOCK: ${ENABLE_MONOTONIC_CLOCK}")

# This options is necessary on some systems; on a cross-ARM compiler it
# has been detected, for example, that -lrt is necessary for some applications
# because clock_gettime is needed by some functions and it is alternatively
# provided by libc, but only in newer versions. This options is rarely necessary,
# but may help in several corner cases in unusual platforms.
if (WITH_EXTRALIBS)
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${WITH_EXTRALIBS}")
endif()

# CMake has only discovered in 3.3 version that some set-finder is
# necessary. Using variables for shortcut to a clumsy check syntax.

set(TARGET_srt "srt" CACHE STRING "The name for the SRT library")

set (srt_libspec_shared ${ENABLE_SHARED})
set (srt_libspec_static ${ENABLE_STATIC})

set (srt_library_targets)
if (srt_libspec_shared)
	list(APPEND srt_library_targets ${TARGET_srt}_shared)
endif()
if (srt_libspec_static)
	list(APPEND srt_library_targets ${TARGET_srt}_static)
endif()

set (SRT_SRC_HAICRYPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/haicrypt)
set (SRT_SRC_SRTCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/srtcore)
set (SRT_SRC_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set (SRT_SRC_TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools)
set (SRT_SRC_TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

# This is obligatory include directory for all targets. This is only
# for private headers. Installable headers should be exclusively used DIRECTLY.
include_directories(${SRT_SRC_COMMON_DIR} ${SRT_SRC_SRTCORE_DIR} ${SRT_SRC_HAICRYPT_DIR})


# ---------------------------------------------------------------------------

if (WIN32)
	MafReadDir(common filelist_win32.maf
		SOURCES SOURCES_common
		PUBLIC_HEADERS HEADERS_srt_win32
		PROTECTED_HEADERS HEADERS_srt_win32
	)
	message(STATUS "WINDOWS detected: adding compat sources: ${SOURCES_common}")
endif()


# Make the OBJECT library for haicrypt and srt. Then they'll be bound into
# real libraries later, either one common, or separate.

# This is needed for Xcode to properly handle CMake OBJECT Libraries
# From docs (https://cmake.org/cmake/help/latest/command/add_library.html#object-libraries):
#
# ... Some native build systems (such as Xcode) may not like targets that have only object files,
# so consider adding at least one real source file to any target that references $<TARGET_OBJECTS:objlib>.
set(OBJECT_LIB_SUPPORT "${PROJECT_SOURCE_DIR}/scripts/cmake_object_lib_support.c")

# NOTE: The "virtual library" is a library specification that cmake
# doesn't support (the library of OBJECT type is something in kind of that,
# but not fully supported - for example it doesn't support transitive flags,
# so this can't be used desired way). It's a private-only dependency type,
# where the project isn't compiled into any library file at all - instead, all
# of its source files are incorporated directly to the source list of the
# project that depends on it. In cmake this must be handled manually.


# ---
# Target: srt. DEFINITION ONLY. Haicrypt flag settings follow.
# ---

if (ENABLE_SHARED AND MICROSOFT)
	#add resource files to shared library, to set DLL metadata on Windows DLLs
	# (reference is in filelist.maf)
	set (EXTRA_WIN32_SHARED 1)
	message(STATUS "WIN32: extra resource file will be added")
endif()

# Variable used in this file:
# - ENABLE_BONDING
# - ENABLE_STDCXX_SYNC
# - EXTRA_WIN32_SHARED
MafReadDir(srtcore filelist.maf
	SOURCES SOURCES_srt
	PUBLIC_HEADERS HEADERS_srt
	PROTECTED_HEADERS HEADERS_srt
	PRIVATE_HEADERS HEADERS_srt_private
)

# Auto generated version file and add it to the HEADERS_srt list.
if(DEFINED ENV{APPVEYOR_BUILD_NUMBER})
	set(SRT_VERSION_BUILD ON)
	set(CI_BUILD_NUMBER_STRING $ENV{APPVEYOR_BUILD_NUMBER})
	message(STATUS "AppVeyor build environment detected: Adding build number to version header")
endif()
if(DEFINED ENV{TEAMCITY_VERSION})
	set(SRT_VERSION_BUILD ON)
	set(CI_BUILD_NUMBER_STRING $ENV{CI_BUILD_COUNTER})
	message(STATUS "TeamCity build environment detected: Adding build counter to version header")
endif()

# Variables used:
# - SRT_VERSION
# - SRT_VERSION_* (MAJOR, MINOR, PATCH)
# - CI_BUILD_NUMBER_STRING
configure_file("srtcore/version.h.in" "version.h" @ONLY)
list(INSERT HEADERS_srt 0 "${CMAKE_CURRENT_BINARY_DIR}/version.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

# ------------------------
# SRT Library definition
# ------------------------

# Target: srt_virtual
#
# This is an object library used to create both shared and static libraries.

add_library(srt_virtual OBJECT
	${SOURCES_srt}
	${SOURCES_srt_extra}
	${HEADERS_srt}
	${SOURCES_haicrypt}
	${SOURCES_common}
)

macro(srt_set_stdcxx targetname spec)
	set (stdcxxspec ${spec})
	if (NOT "${stdcxxspec}" STREQUAL "")
		if (FORCE_CXX_STANDARD_GNUONLY)
			target_compile_options(${targetname} PRIVATE -std=c++${stdcxxspec})
			message(STATUS "C++ STD: ${targetname}: forced C++${stdcxxspec} standard - GNU option: -std=c++${stdcxxspec}")
		else()
			set_target_properties(${targetname} PROPERTIES CXX_STANDARD ${stdcxxspec})
			message(STATUS "C++ STD: ${targetname}: forced C++${stdcxxspec} standard - portable way")
		endif()
	else()
		message(STATUS "APP: ${targetname}: using default C++ standard")
	endif()
endmacro()

macro(srt_set_stdc targetname spec)
	set (stdcspec ${spec})
	if (NOT "${stdcspec}" STREQUAL "")
		set_target_properties(${targetname} PROPERTIES C_STANDARD ${stdcspec})
		message(STATUS "C STD: ${targetname}: forced C${stdcspec} standard - portable way")
	else()
		message(STATUS "APP: ${targetname}: using default C standard")
	endif()
endmacro()

srt_set_stdcxx(srt_virtual "${USE_CXX_STD_LIB}")
srt_set_stdc(srt_virtual "99")

if (ENABLE_ENCRYPTION)
	target_include_directories(srt_virtual PRIVATE  ${SSL_INCLUDE_DIRS})

	if ("${USE_ENCLIB}" STREQUAL "botan")
		add_dependencies(srt_virtual botan)
	endif()
endif()

if (DEFINED LOGGING_ENABLE_DEFS)
	message(STATUS "LOGGING FLAGS: ${LOGGING_ENABLE_DEFS}")
	target_compile_definitions(srt_virtual PUBLIC ${LOGGING_ENABLE_DEFS})
endif()


# All *.o files created for srt_virtual target
# will be collected and used to create shared and static libraries
set (VIRTUAL_srt $<TARGET_OBJECTS:srt_virtual>)

# -------------------------------
# Shared/static library definition
# (based on srt_virtual target)
# -------------------------------

# Enable PIC on the virtual library if shared library is enabled at all
# (yes, it means it will be enabled on the static library, too).
# Otherwise it may not be accepted in the dependencies.
if (ENABLE_SHARED)
	# Set this to sources as well, as it won't be automatically handled
	set_target_properties(srt_virtual PROPERTIES POSITION_INDEPENDENT_CODE 1)
endif()

if (srt_libspec_shared)
	add_library(${TARGET_srt}_shared SHARED ${OBJECT_LIB_SUPPORT} ${VIRTUAL_srt})
	# shared libraries need PIC
	set (CMAKE_POSITION_INDEPENDENT_CODE ON)
	set_property(TARGET ${TARGET_srt}_shared PROPERTY OUTPUT_NAME ${TARGET_srt})
	set_target_properties (${TARGET_srt}_shared PROPERTIES VERSION ${SRT_VERSION} SOVERSION ${SRT_VERSION_MAJOR}.${SRT_VERSION_MINOR})
	list (APPEND INSTALL_TARGETS ${TARGET_srt}_shared)
	if (ENABLE_ENCRYPTION)
		target_link_libraries(${TARGET_srt}_shared PRIVATE ${SSL_LIBRARIES})
	endif()
	if (MICROSOFT)
		target_link_libraries(${TARGET_srt}_shared PUBLIC ws2_32.lib)
		if (NOT (ENABLE_ENCRYPTION AND "${USE_ENCLIB}" STREQUAL "botan"))
			if (SRT_USE_OPENSSL_STATIC_LIBS)
				target_link_libraries(${TARGET_srt}_shared PRIVATE crypt32.lib)
			else()
				set_target_properties(${TARGET_srt}_shared PROPERTIES LINK_FLAGS "/DELAYLOAD:libeay32.dll")
			endif()
		endif()
	elseif (MINGW)
		target_link_libraries(${TARGET_srt}_shared PRIVATE wsock32 ws2_32)
	elseif (APPLE)
		set_property(TARGET ${TARGET_srt}_shared PROPERTY MACOSX_RPATH ON)
	endif()
	if (USE_GNUSTL)
		target_link_libraries(${TARGET_srt}_shared PRIVATE ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
	endif()
endif()

if (srt_libspec_static)
	add_library(${TARGET_srt}_static STATIC ${OBJECT_LIB_SUPPORT} ${VIRTUAL_srt})

	# For Windows, leave the name to be "srt_static.lib".
	# Windows generates two different library files:
	# - a usual static library for static linkage
	# - a shared library exposer, which allows pre-resolution and later dynamic
	#   linkage when running the executable
	# Both having unfortunately the same names created by MSVC compiler.
	# It's not the case of Cygwin/MINGW - they are named there libsrt.a and libsrt.dll.a
	if (MICROSOFT)
		# Keep _static suffix. By unknown reason, the name must still be set explicitly.
		set_property(TARGET ${TARGET_srt}_static PROPERTY OUTPUT_NAME ${TARGET_srt}_static)
	else()
		set_property(TARGET ${TARGET_srt}_static PROPERTY OUTPUT_NAME ${TARGET_srt})
	endif()

	list (APPEND INSTALL_TARGETS ${TARGET_srt}_static)
	if (ENABLE_ENCRYPTION)
		target_link_libraries(${TARGET_srt}_static PRIVATE ${SSL_LIBRARIES})
	endif()
	if (MICROSOFT)
		target_link_libraries(${TARGET_srt}_static PRIVATE ws2_32.lib)
		if (SRT_USE_OPENSSL_STATIC_LIBS)
			target_link_libraries(${TARGET_srt}_static PRIVATE crypt32.lib)
		endif()
	elseif (MINGW)
		target_link_libraries(${TARGET_srt}_static PRIVATE wsock32 ws2_32)
	endif()
	if (USE_GNUSTL)
		target_link_libraries(${TARGET_srt}_static PRIVATE ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
	endif()
endif()

# This add things to the declarations of the dependent libraries when
# generating pkg-config.
if (MICROSOFT)
	if (SRT_USE_OPENSSL_STATIC_LIBS)
		set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ws2_32.lib crypt32.lib)
	else()
		set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ws2_32.lib)
	endif()
elseif (MINGW)
	set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} -lwsock32 -lws2_32)
endif()

# Applying this to public includes is not transitive enough.
# On Windows, apps require this as well, so it's safer to
# spread this to all targets.
if (PTHREAD_INCLUDE_DIR)
	include_directories(${PTHREAD_INCLUDE_DIR})
endif()

# Link libraries must be applied directly to the derivatives
# as virtual libraries (OBJECT-type) cannot have linkage declarations
# transitive or not.

foreach(tar ${srt_library_targets})
	message(STATUS "ADDING TRANSITIVE LINK DEP to:${tar} : ${PTHREAD_LIBRARY} ${dep}")
	target_link_libraries (${tar} PUBLIC ${PTHREAD_LIBRARY} ${dep})
endforeach()

set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${PTHREAD_LIBRARY})

target_compile_definitions(srt_virtual PRIVATE -DSRT_EXPORTS )
if (ENABLE_SHARED)
	target_compile_definitions(srt_virtual PUBLIC -DSRT_DYNAMIC)
endif()

# XXX DO NOT USE THIS if C++11 is in force.

# Required by some toolchains when statically linking this library if the
#  GCC Atomic Intrinsics are being used.
if (HAVE_GCCATOMIC_INTRINSICS_REQUIRES_LIBATOMIC AND HAVE_LIBATOMIC)
	foreach(tar ${srt_library_targets})
		target_link_libraries(${tar} PUBLIC atomic)
	endforeach()
elseif (HAVE_LIBATOMIC AND HAVE_LIBATOMIC_COMPILES_STATIC)
	# This is a workaround for ANDROID NDK<17 builds, which need to link
	#  to libatomic when linking statically to the SRT library.
	if (srt_libspec_static)
		target_link_libraries(${TARGET_srt}_static PUBLIC atomic)
	endif()
elseif (LINUX AND HAVE_LIBATOMIC AND HAVE_LIBATOMIC_COMPILES)
	# This is a workaround for some older Linux Toolchains.
	if (srt_libspec_static)
		target_link_libraries(${TARGET_srt}_static PUBLIC atomic)
	endif()
endif()

# Cygwin installs the *.dll libraries in bin directory and uses PATH.

set (INSTALL_SHARED_DIR ${CMAKE_INSTALL_LIBDIR})
if (CYGWIN)
	set (INSTALL_SHARED_DIR ${CMAKE_INSTALL_BINDIR})
endif()

message(STATUS "INSTALL DIRS: bin=${CMAKE_INSTALL_BINDIR} lib=${CMAKE_INSTALL_LIBDIR} shlib=${INSTALL_SHARED_DIR} include=${CMAKE_INSTALL_INCLUDEDIR}")
if (NEED_DESTINATION)
	if (DEFINED CMAKE_INSTALL_BINDIR AND DEFINED CMAKE_INSTALL_LIBDIR AND NOT INSTALL_SHARED_DIR STREQUAL "")
		install(TARGETS ${INSTALL_TARGETS}
			RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
			ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
			LIBRARY DESTINATION ${INSTALL_SHARED_DIR}
		)
	else()
		message(WARNING "No location to install ${INSTALL_TARGETS}")
	endif()
elseif (NOT INSTALL_SHARED_DIR STREQUAL "")
	install(TARGETS ${INSTALL_TARGETS}
		LIBRARY DESTINATION ${INSTALL_SHARED_DIR}
	)
else()
	install(TARGETS ${INSTALL_TARGETS})
endif()

if (DEFINED CMAKE_INSTALL_INCLUDEDIR)
	install(FILES ${HEADERS_srt} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/srt)
	if (WIN32)
		install(FILES ${HEADERS_srt_win32} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/srt/win)
	endif()
endif()

# ---
# That's all for target definition
# ---

join_arguments(SRT_EXTRA_CFLAGS ${SRT_EXTRA_CFLAGS})

#message(STATUS "Target srt: LIBSPEC: ${srt_library_targets} SOURCES: {${SOURCES_srt}}  HEADERS: {${HEADERS_srt}}")

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_CFLAGS} ${SRT_GCC_WARN}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_CFLAGS} ${SRT_GCC_WARN}")

# PC file generation.
if (NOT DEFINED INSTALLDIR)
	set (INSTALLDIR ${CMAKE_INSTALL_PREFIX})
	get_filename_component(INSTALLDIR ${INSTALLDIR} ABSOLUTE)
endif()

# Required if linking a C application.
# This may cause trouble when you want to compile your app with static libstdc++;
# if your build requires it, you'd probably remove -lstdc++ from the list
# obtained by `pkg-config --libs`.
if(ENABLE_CXX_DEPS)
	foreach(LIB ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
		if((IS_ABSOLUTE ${LIB} AND EXISTS ${LIB}) OR (${LIB} MATCHES "^-l"))
			set(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${LIB})
		else()
			set(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} "-l${LIB}")
		endif()
	endforeach()
endif()

join_arguments(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE})

# Variables used:
# - CMAKE_INSTALL_LIBDIR
# - CMAKE_INSTALL_INCLUDEDIR
# - SRT_VERSION
# - TARGET_srt
# - IFNEEDED_LINK_HAICRYPT
# - IFNEEDED_SRTBASE
# - IFNEEDED_SRT_LDFLAGS
# - SRT_LIBS_PRIVATE
# - SSL_REQUIRED_MODULES
if (DEFINED CMAKE_INSTALL_LIBDIR)
	configure_file(scripts/srt.pc.in srt.pc @ONLY)
	install(FILES ${CMAKE_CURRENT_BINARY_DIR}/srt.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()

# ------------
# Applications
# ------------

## FIXME: transmitmedia.cpp does not build on OpenBSD
##    Issue: https://github.com/Haivision/srt/issues/590
if (BSD
   AND ${SYSNAME_LC} MATCHES "^openbsd$")
   set(ENABLE_APPS OFF)
endif()
## The applications currently require c++11.
if (NOT ENABLE_CXX11)
   set(ENABLE_APPS OFF)
endif()

if (ENABLE_APPS)


	# If static is available, link apps against static one.
	# Otherwise link against shared one.

	if (srt_libspec_static)
		set (srt_libtarget_for_apps ${TARGET_srt}_static)
		if (ENABLE_RELATIVE_LIBPATH)
			message(STATUS "ENABLE_RELATIVE_LIBPATH=ON will be ignored due to static linking.")
		endif()
	elseif(srt_libspec_shared)
		set (srt_libtarget_for_apps ${TARGET_srt}_shared)
	else()
		message(FATAL_ERROR "Either ENABLE_STATIC or ENABLE_SHARED has to be ON!")
	endif()

	macro(srt_add_program_dont_install name)
		add_executable(${name} ${ARGN})
		target_include_directories(${name} PRIVATE apps)
		target_include_directories(${name} PRIVATE common)
	endmacro()

	macro(srt_add_program name)
		srt_add_program_dont_install(${name} ${ARGN})
		if(NOT NEED_DESTINATION)
			install(TARGETS ${name} RUNTIME)
		elseif (DEFINED CMAKE_INSTALL_BINDIR)
			install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
		else()
			message(WARNING "No location to install program ${name}")
		endif()
	endmacro()

	macro(srt_make_application name)

		srt_set_stdcxx(${name} "${USE_CXX_STD_APP}")
		
		# This is recommended by cmake, but it doesn't work anyway.
		# What is needed is that this below CMAKE_INSTALL_RPATH (yes, relative)
		# is added as is.
		# set (CMAKE_SKIP_RPATH FALSE)
		# set (CMAKE_SKIP_BUILD_RPATH FALSE)
		# set (CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
		# set (CMAKE_INSTALL_RPATH "../${CMAKE_INSTALL_LIBDIR}")
		# set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
		# set (FORCE_RPATH BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH_USE_LINK_PATH TRUE)

		if (LINUX AND ENABLE_RELATIVE_LIBPATH AND NOT srt_libspec_static)
			# This is only needed on Linux, on Windows (including Cygwin) the library file will
			# be placed into the binrary directory anyway.
			# XXX not sure about Mac.
			# See this name used already in install(${TARGET_srt} LIBRARY DESTINATION...).
			set(FORCE_RPATH LINK_FLAGS -Wl,-rpath,.,-rpath,../${CMAKE_INSTALL_LIBDIR} BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH_USE_LINK_PATH TRUE)

			set_target_properties(${name} PROPERTIES ${FORCE_RPATH})
		endif()

		target_link_libraries(${name} ${srt_libtarget_for_apps})
		if (USE_GNUSTL)
			target_link_libraries(${name} PRIVATE ${GNUSTL_LIBRARIES} ${GNUSTL_LDFLAGS})
		endif()
		if (srt_libspec_static AND CMAKE_DL_LIBS)
			target_link_libraries(${name} ${CMAKE_DL_LIBS})
		endif()
	endmacro()

	macro(srt_add_application name) # ARGN=sources...
		srt_add_program(${name} apps/${name}.cpp ${ARGN})
		srt_make_application(${name})
		if(NOT NEED_DESTINATION)
			install(TARGETS ${name} RUNTIME)
		elseif (DEFINED CMAKE_INSTALL_BINDIR)
			install(TARGETS ${name} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
		else()
			message(WARNING "No location to install program ${name}")
		endif()
	endmacro()

	message(STATUS "APPS: ENABLED, std=${USE_CXX_STD_APP}")

	# Make a virtual library of all shared app files
	MafReadDir(apps support.maf
		SOURCES SOURCES_support
	)

	# A special trick that makes the shared application sources
	# to be compiled once for all applications. Maybe this virtual
	# library should be changed into a static one and made useful
	# for users.
	add_library(srtsupport_virtual OBJECT ${SOURCES_support})
	srt_set_stdcxx(srtsupport_virtual "${USE_CXX_STD_APP}")
	set (VIRTUAL_srtsupport $<TARGET_OBJECTS:srtsupport_virtual>)

	# Applications

	srt_add_application(srt-live-transmit ${VIRTUAL_srtsupport})
	if (DEFINED EXTRA_stransmit)
		set_target_properties(srt-live-transmit PROPERTIES COMPILE_FLAGS "${EXTRA_stransmit}")
	endif()
	srt_add_application(srt-file-transmit ${VIRTUAL_srtsupport})

	if (MINGW)
		# FIXME: with MINGW, it fails to build apps that require C++11
		# https://github.com/Haivision/srt/issues/177
		message(WARNING "On MinGW, some C++11 apps are blocked due to lacking proper C++11 headers for <thread>. FIX IF POSSIBLE.")
	else()
		# srt-multiplex temporarily blocked
		#srt_add_application(srt-multiplex ${VIRTUAL_srtsupport})
		srt_add_application(srt-tunnel ${VIRTUAL_srtsupport})
	endif()

	if (ENABLE_TESTING)
		message(STATUS "DEVEL APPS (testing): ENABLED")

		macro(srt_add_testprogram name)
			# Variables in macros are not local. Clear them forcefully.
			set (SOURCES_app_indir "")
			set (SOURCES_app "")
			# Unlike Silvercat, in cmake you must know the full list
			# of source files at the moment when defining the target
			# and it can't be altered later.
			#
			# For testing applications, every application has its exclusive
			# list of source files in its own Manifest file.
			MafReadDir(testing ${name}.maf SOURCES SOURCES_app)
			srt_add_program_dont_install(${name} ${SOURCES_app})
		endmacro()

		srt_add_testprogram(utility-test)
		srt_set_stdcxx(utility-test "${USE_CXX_STD_APP}")
		if (NOT WIN32)
			# This program is symlinked under git-cygwin.
			# Avoid misleading syntax error.
			srt_add_testprogram(uriparser-test)
			target_compile_options(uriparser-test PRIVATE -DTEST)
			srt_set_stdcxx(uriparser-test "${USE_CXX_STD_APP}")
		endif()
		
		srt_add_testprogram(srt-test-live)
		srt_make_application(srt-test-live)

		srt_add_testprogram(srt-test-file)
		srt_make_application(srt-test-file)

		srt_add_testprogram(srt-test-relay)
		srt_make_application(srt-test-relay)

		srt_add_testprogram(srt-test-multiplex)
		srt_make_application(srt-test-multiplex)

		if (ENABLE_BONDING)
			srt_add_testprogram(srt-test-mpbond)
			srt_make_application(srt-test-mpbond)
		endif()

	else()
		message(STATUS "DEVEL APPS (testing): DISABLED")
	endif()


else()
	message(STATUS "APPS: DISABLED")
endif()

if (ENABLE_EXAMPLES)

	# No examples should need C++11
	macro(srt_add_example mainsrc)
		get_filename_component(name ${mainsrc} NAME_WE)
		srt_add_program_dont_install(${name} examples/${mainsrc} ${ARGN})
		target_link_libraries(${name} ${srt_libtarget_for_apps} ${DEPENDS_srt})
	endmacro()

	srt_add_example(recvlive.cpp)

	srt_add_example(sendfile.cpp)
		
	srt_add_example(recvfile.cpp)

	srt_add_example(sendmsg.cpp)
		
	srt_add_example(recvmsg.cpp)

	srt_add_example(test-c-client.c)

	srt_add_example(example-client-nonblock.c)

	srt_add_example(test-c-server.c)

if (ENABLE_BONDING)
	srt_add_example(test-c-client-bonding.c)

	srt_add_example(test-c-server-bonding.c)
endif()

	srt_add_example(testcapi-connect.c)
endif()


if (ENABLE_UNITTESTS AND ENABLE_CXX11)

	if (${CMAKE_VERSION} VERSION_LESS "3.10.0") 
		message(STATUS "VERSION < 3.10 -- adding test using the old method")
		set (USE_OLD_ADD_METHOD 1)
	else()
		message(STATUS "VERSION > 3.10 -- using NEW POLICY for in_list operator")
		cmake_policy(SET CMP0057 NEW) # Support the new IN_LIST operator.
	endif()


	set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

	# Version ranges are only supported with CMake 3.19 or later.
	# Need GTest v1.10 or higher to support GTEST_SKIP.
	if (${CMAKE_VERSION} VERSION_LESS "3.19.0")
		find_package(GTest 1.10)
	else()
		find_package(GTest 1.10...1.12)
	endif()
	if (NOT GTEST_FOUND)
		message(STATUS "GTEST not found! Fetching from git.")
		include(googletest)
		fetch_googletest(
			${PROJECT_SOURCE_DIR}/scripts
			${PROJECT_BINARY_DIR}/googletest
		)
		set(GTEST_BOTH_LIBRARIES "gtest_main" CACHE STRING "Add gtest_main target")
	endif()

	MafReadDir(test filelist.maf
		HEADERS SOURCES_unittests
		SOURCES SOURCES_unittests
	)

	srt_add_program_dont_install(test-srt ${SOURCES_unittests})
	srt_make_application(test-srt)
	target_include_directories(test-srt PRIVATE  ${SSL_INCLUDE_DIRS} ${GTEST_INCLUDE_DIRS})
	target_compile_definitions(test-srt PRIVATE "-DSRT_TEST_SYSTEM_NAME=\"${CMAKE_SYSTEM_NAME}\"")

	target_link_libraries(
		test-srt
		${GTEST_BOTH_LIBRARIES}
		${srt_libtarget_for_apps}
		${PTHREAD_LIBRARY}
		)

	if (USE_OLD_ADD_METHOD)
		add_test(
			NAME	test-srt
			COMMAND	${CMAKE_BINARY_DIR}/test-srt
		)
		#set_tests_properties(test-srt PROPERTIES RUN_SERIAL TRUE)
	else()
		set_tests_properties(${tests_srt} PROPERTIES RUN_SERIAL TRUE)
		if (ENABLE_UNITTESTS_DISCOVERY)
			gtest_discover_tests(test-srt)
		endif()
	endif()

	enable_testing()
endif()


if(NOT NEED_DESTINATION)
	install(PROGRAMS scripts/srt-ffplay TYPE BIN)
elseif (DEFINED CMAKE_INSTALL_BINDIR)
	install(PROGRAMS scripts/srt-ffplay DESTINATION ${CMAKE_INSTALL_BINDIR})
else()
	message(WARNING "No location to install scripts/srt-ffplay")
endif()


if (DEFINED SRT_EXTRA_APPS_INC)
	include(${SRT_EXTRA_APPS_INC}.cmake)
	# No extra variables expected. Just use the variables
	# already provided and define additional targets.
endif()

if (ENABLE_SHOW_PROJECT_CONFIG)
	include(ShowProjectConfig)
	ShowProjectConfig()
endif()
