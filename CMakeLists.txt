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
cmake_minimum_required (VERSION 2.8.12 FATAL_ERROR)
# XXX This can be potentially done in future, but there still exist
# some dependent project using cmake 2.8 - this can't be done this way.
#cmake_minimum_required (VERSION 3.0.2 FATAL_ERROR)
#project(SRT VERSION "1.3.0")
project(SRT C CXX)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
include(haiUtil)
include(FindPkgConfig)

set (SRT_VERSION 1.3.0)
set_version_variables(SRT_VERSION ${SRT_VERSION})

# The CMAKE_BUILD_TYPE seems not to be always set, weird.
if (NOT DEFINED ENABLE_DEBUG)

	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		set (ENABLE_DEBUG ON)
	else()
		set (ENABLE_DEBUG OFF)
	endif()
endif()

# Set CMAKE_BUILD_TYPE properly, now that you know
# that ENABLE_DEBUG is set as it should.

if (ENABLE_DEBUG EQUAL 1)
	set (CMAKE_BUILD_TYPE "Debug")
elseif (ENABLE_DEBUG EQUAL 2)
	set (CMAKE_BUILD_TYPE "RelWithDebInfo")
else()
	set (CMAKE_BUILD_TYPE "Release")
endif()

message(STATUS "BUILD TYPE: ${CMAKE_BUILD_TYPE}")

# option defaults
# XXX CHANGE: Logging is enabled now by default,
# use ENABLE_LOGGING=NO in cmake or
# --disable-logging in configure.
set(ENABLE_HEAVY_LOGGING_DEFAULT OFF)

# Always turn logging on if the build type is debug
if (ENABLE_DEBUG)
	set(ENABLE_HEAVY_LOGGING_DEFAULT ON)
endif()


# options
option(CYGWIN_USE_POSIX "Should the POSIX API be used for cygwin. Ignored if the system isn't cygwin." OFF)
option(ENABLE_CXX11 "Should the c++11 parts (srt-live-transmit) be enabled" ON)
option(ENABLE_PROFILE "Should instrument the code for profiling. Ignored for non-GNU compiler." $ENV{HAI_BUILD_PROFILE})
option(ENABLE_LOGGING "Should logging be enabled" ON)
option(ENABLE_HEAVY_LOGGING "Should heavy debug logging be enabled" ${ENABLE_HEAVY_LOGGING_DEFAULT})
option(ENABLE_SHARED "Should libsrt be built as a shared library" ON)
option(ENABLE_STATIC "Should libsrt be built as a static library" ON)
option(ENABLE_SUFLIP "Should suflip tool be built" OFF)
option(USE_GNUTLS "Should use gnutls instead of openssl" OFF)
option(ENABLE_C_DEPS "Extra library dependencies in srt.pc for C language" OFF)
option(USE_STATIC_LIBSTDCXX "Should use static rather than shared libstdc++" OFF)

set(TARGET_srt "srt" CACHE STRING "The name for the haisrt library")

if (NOT ENABLE_LOGGING)
	set (ENABLE_HEAVY_LOGGING OFF)
	message(STATUS "LOGGING: DISABLED")
else()
	if (ENABLE_HEAVY_LOGGING)
		message(STATUS "LOGGING: HEAVY")
	else()
		message(STATUS "LOGGING: ENABLED")
	endif()
endif()

if ( CYGWIN AND NOT CYGWIN_USE_POSIX )
	set(WIN32 1)
	set(CMAKE_LEGACY_CYGWIN_WIN32 1)
	add_definitions(-DWIN32=1 -DCYGWIN=1)
	message(STATUS "HAVE CYGWIN. Setting backward compat CMAKE_LEGACY_CYGWIN_WIN32 and -DWIN32")
endif()

# Make sure DLLs and executabes go to the same path regardles of subdirectory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Handle WITH_COMPILER_PREFIX and WITH_COMPILER_TYPE options
if (DEFINED WITH_COMPILER_PREFIX)
	message(STATUS "Handling compiler with WITH_COMPILER_PREFIX=${WITH_COMPILER_PREFIX}")
	# Check also type. Default is gcc.
	if (NOT DEFINED WIHT_COMPILER_TYPE)
		set (WITH_COMPILER_TYPE gcc)
	endif()

	if (${WITH_COMPILER_TYPE} STREQUAL gcc)
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}gcc)
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}g++)
	elseif (${WITH_COMPILER_TYPE} STREQUAL cc)
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}cc)
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}c++)
	else()
		# Use blindly <command> for C compiler and <command>++ for C++.
		# At least this matches clang.
		set (CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}${WITH_COMPILER_TYPE})
		set (CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}${WITH_COMPILER_TYPE}++)
	endif()
else()
	message(STATUS "No WITH_COMPILER_PREFIX - using C++ compiler ${CMAKE_CXX_COMPILER}")
endif()


if (DEFINED WITH_SRT_TARGET)
	set (TARGET_haisrt ${WITH_SRT_TARGET})
endif()

set_if(DARWIN ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
set_if(LINUX ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
set_if(MICROSOFT WIN32 AND (NOT MINGW AND NOT CYGWIN))

set_if(SYMLINKABLE LINUX OR DARWIN OR CYGWIN)

# When you use crosscompiling, you have to take care that PKG_CONFIG_PATH
# and CMAKE_PREFIX_PATH are set properly.

# find OpenSSL
if ( USE_GNUTLS )
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
	message(STATUS "SSL Dependency: using GNUTLS with Nettle, as requested")
else()
	set (SSL_REQUIRED_MODULES "openssl libcrypto zlib")

	if (USE_OPENSSL_PC)

		pkg_check_modules(SSL REQUIRED ${SSL_REQUIRED_MODULES})

		# We have some cases when pkg-config is improperly configured
		# When it doesn't ship the -L and -I options, and the CMAKE_PREFIX_PATH
		# is set (also through `configure`), then we have this problem. If so,
		# set forcefully the -I and -L contents to prefix/include and
		# prefix/lib.

		if ("${SSL_LIBRARY_DIRS}" STREQUAL "")
		if (NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
			message(STATUS "WARNING: pkg-config has incorrect prefix - enforcing target path prefix: ${CMAKE_PREFIX_PATH}")
			set (SSL_LIBRARY_DIRS ${CMAKE_PREFIX_PATH}/lib)
			set (SSL_INCLUDE_DIRS ${CMAKE_PREFIX_PATH}/include)
		endif()
		endif()

		link_directories(
			${SSL_LIBRARY_DIRS}
		)
		message(STATUS "SSL REPORT: -L ${SSL_LIBRARY_DIRS} -I ${SSL_INCLUDE_DIRS} -l;${SSL_LIBRARIES}")
	else()

		find_package(OpenSSL REQUIRED)
		set (SSL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
		set (SSL_LIBRARIES ${OPENSSL_LIBRARIES})
	endif()
	add_definitions(
		-DHAICRYPT_USE_OPENSSL_EVP=1
		-DHAICRYPT_USE_OPENSSL_AES
	)
	message(STATUS "SSL Dependency: using OpenSSL (default)")
endif()

message (STATUS "SSL libraries: ${SSL_LIBRARIES}")

# Detect if the compiler is GNU compatable for flags
set(HAVE_COMPILER_GNU_COMPAT 0)
foreach (gnid GNU Intel Clang AppleClang)
	if (${CMAKE_CXX_COMPILER_ID} STREQUAL ${gnid})
		set(HAVE_COMPILER_GNU_COMPAT 1)
		break()
	endif()
endforeach()

if (DISABLE_CXX11)
	set (ENABLE_CXX11 0)
elseif( DEFINED ENABLE_CXX11 )
else()
	set (ENABLE_CXX11 1)
endif()

if (NOT ENABLE_CXX11)
	message(WARNING "Parts that require C++11 support will be disabled (srt-live-transmit)")
endif()

if (HAVE_COMPILER_GNU_COMPAT)
	message(STATUS "COMPILER: GNU compat: ${CMAKE_CXX_COMPILER}")
else()
	message(STATUS "COMPILER: NOT GNU compat: ${CMAKE_CXX_COMPILER}")
endif()

# add extra warning flags for gccish compilers
if (HAVE_COMPILER_GNU_COMPAT)
	set (SRT_GCC_WARN "-Wall -Wextra")
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

# CMake has only discovered in 3.3 version that some set-finder is
# necessary. Using variables for shortcut to a clumsy check syntax.

set (srt_libspec_shared ${ENABLE_SHARED})
set (srt_libspec_static ${ENABLE_STATIC})

set (haicrypt_libspec VIRTUAL)

set (srtpack_libspec_common)
if (srt_libspec_shared)
	list(APPEND srtpack_libspec_common ${TARGET_srt}_shared)
endif()
if (srt_libspec_static)
	list(APPEND srtpack_libspec_common ${TARGET_srt}_static)
endif()

set (SRT_SRC_HAICRYPT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/haicrypt)
set (SRT_SRC_SRTCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/srtcore)
set (SRT_SRC_COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)
set (SRT_SRC_TOOLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tools)

if(WIN32)
    add_definitions(-DWIN32=1 -DPTW32_STATIC_LIB)
elseif(DARWIN)
    message(STATUS "DARWIN detected")
    add_definitions(-DOSX=1)
	set(MACOSX_RPATH OFF)
elseif(LINUX)
    add_definitions(-DLINUX=1)
    message(STATUS "LINUX detected" )
elseif(CYGWIN)
    add_definitions(-DCYGWIN=1)
	message(STATUS "CYGWIN (posix mode) detected")
else()
    message(FATAL_ERROR "Unsupported system")
endif()

add_definitions(
   -D_GNU_SOURCE
   -DHAI_PATCH=1
   -DHAI_ENABLE_SRT=1
   -DSRT_VERSION="${SRT_VERSION}"
)

# This is obligatory include directory for all targets. This is only
# for private headers. Installable headers should be exclusively used DIRECTLY.
include_directories(${SRT_SRC_COMMON_DIR} ${SRT_SRC_SRTCORE_DIR} ${SRT_SRC_HAICRYPT_DIR})

if (ENABLE_LOGGING)
    list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_LOGGING=1")
	if (ENABLE_HEAVY_LOGGING)
		list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_HEAVY_LOGGING=1")
	endif()
endif()

if (ENABLE_THREAD_CHECK)
      add_definitions(
               -DSRT_ENABLE_THREADCHECK=1
               -DFUGU_PLATFORM=1
               -I${WITH_THREAD_CHECK_INCLUDEDIR}
        )
endif()

if (${ENABLE_PROFILE} AND HAVE_COMPILER_GNU_COMPAT)
    # They are actually cflags, not definitions, but CMake is stupid enough.
    add_definitions(-g -pg)
	link_libraries(-g -pg)
endif()


if (MINGW)
	set (PTHREAD_LIBRARY -lpthreadGC2)
else()
	# find pthread
	find_path(PTHREAD_INCLUDE_DIR pthread.h HINTS C:/pthread-win32/include)
	if (PTHREAD_INCLUDE_DIR)
		message(STATUS "Pthread include dir: ${PTHREAD_INCLUDE_DIR}")
	else()
		message(FATAL_ERROR "Failed to find pthread.h. Specify PTHREAD_INCLUDE_DIR.")
	endif()

	find_library(PTHREAD_LIBRARY NAMES pthread pthread_dll pthread_lib HINTS C:/pthread-win32/lib)
	if (PTHREAD_LIBRARY)
		message(STATUS "Pthread library: ${PTHREAD_LIBRARY}")
	else()
		if (NOT "${CMAKE_PREFIX_PATH}" STREQUAL "")
			message(STATUS "WARNING: cross-compiler pthread not found - fallback to POSIX default")
			set (PTHREAD_LIBRARY -pthread)
			set (PTHREAD_INCLUDE_DIR "")
		else()
			message(FATAL_ERROR "Failed to find pthread library. Specify PTHREAD_LIBRARY.")
		endif()
	endif()
endif() # if (NOT MINGW)

# This is required in some projects that add some other sources
# to the SRT library to be compiled together (aka "virtual library").
if (DEFINED SRT_EXTRA_LIB_INC)
	include(${SRT_EXTRA_LIB_INC}.cmake)
	# Expected to provide variables:
	# - SOURCES_srt_extra
	# - EXTRA_stransmit
endif()

# ---------------------------------------------------------------------------

# ---
# Target: haicrypt.
# Completing sources and installable headers. Flag settings will follow.
# ---
if ( USE_GNUTLS )
	set (HAICRYPT_FILELIST_MAF "filelist-gnutls.maf")
else()
	set (HAICRYPT_FILELIST_MAF "filelist.maf")
endif()

MafReadDir(haicrypt ${HAICRYPT_FILELIST_MAF}
	SOURCES SOURCES_haicrypt
	PUBLIC_HEADERS HEADERS_haicrypt
	PROTECTED_HEADERS HEADERS_haicrypt
)

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

add_library(haicrypt_virtual OBJECT ${SOURCES_haicrypt} ${SOURCES_common})

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
MafReadDir(srtcore filelist.maf
	SOURCES SOURCES_srt
	PUBLIC_HEADERS HEADERS_srt
	PROTECTED_HEADERS HEADERS_srt
)

# Auto generated version file and add it to the HEADERS_srt list.
configure_file("srtcore/version.h.in" "version.h" @ONLY)
list(INSERT HEADERS_srt 0 "${CMAKE_CURRENT_BINARY_DIR}/version.h")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")

add_library(srt_virtual OBJECT ${SOURCES_srt} ${SOURCES_srt_extra})

if (ENABLE_SHARED)
	# Set this to sources as well, as it won't be automatically handled
	foreach (tar srt_virtual haicrypt_virtual)
		set_target_properties(${tar} PROPERTIES POSITION_INDEPENDENT_CODE 1)
	endforeach()
endif()

# Manual handling of dependency on virtual library
# By setting the target, all settings applied to the haicrypt target
# will now apply to the dependent library.
#list(APPEND SOURCES_srt ${SOURCES_haicrypt})
set (VIRTUAL_srt $<TARGET_OBJECTS:srt_virtual> $<TARGET_OBJECTS:haicrypt_virtual>)
set (HEADERS_srt ${HEADERS_srt} ${HEADERS_srt_win32})

if (srt_libspec_shared)
	message (STATUS "SRT: defining SHARED library: ${TARGET_srt} from: ${VIRTUAL_srt}")
	add_library(${TARGET_srt}_shared SHARED ${VIRTUAL_srt})
	# shared libraries need PIC
	set_property(TARGET ${TARGET_srt}_shared PROPERTY OUTPUT_NAME ${TARGET_srt})
    set_target_properties (${TARGET_srt}_shared PROPERTIES VERSION ${SRT_VERSION} SOVERSION ${SRT_VERSION_MAJOR})
	list (APPEND INSTALL_TARGETS ${TARGET_srt}_shared)
	target_link_libraries(${TARGET_srt}_shared PRIVATE ${SSL_LIBRARIES})
	if (MICROSOFT)
		target_link_libraries(${TARGET_srt}_shared PRIVATE ws2_32.lib)
	endif()
endif()

if (srt_libspec_static)
	message (STATUS "SRT: defining STATIC library: ${TARGET_srt} from: ${VIRTUAL_srt}")
	add_library(${TARGET_srt}_static STATIC ${VIRTUAL_srt})

	# For Windows, leave the name to be "srt_static.lib".
	# Windows generates two different library files:
	# - a usual static library for static linkage
	# - a shared library exposer, which allows pre-resolution and later dynamic
	#   linkage when running the executable
	# Both having unfortunately the same names created by MSVC compiler.
	# It's not the case of Cygwin - they are named there libsrt.a and libsrt.dll.a
	if (WIN32 AND NOT CYGWIN)
		# Keep _static suffix. By unknown reason, the name must still be set explicitly.
		set_property(TARGET ${TARGET_srt}_static PROPERTY OUTPUT_NAME ${TARGET_srt}_static)
	else()
		set_property(TARGET ${TARGET_srt}_static PROPERTY OUTPUT_NAME ${TARGET_srt})
	endif()

	list (APPEND INSTALL_TARGETS ${TARGET_srt}_static)
	target_link_libraries(${TARGET_srt}_static PRIVATE ${SSL_LIBRARIES})
	if (MICROSOFT)
		target_link_libraries(${TARGET_srt}_static PRIVATE ws2_32.lib)
	endif()
endif()



# ---
# And back to target: haicrypt. Both targets must be defined
# prior to setting flags, and after defining the list of sources
# can no longer be extended.
#
# For haicrypt.spec = VIRTUAL, these settings apply to srt.
# Otherwise they apply to haicrypt.
# ---


target_include_directories(haicrypt_virtual PRIVATE  ${SSL_INCLUDE_DIRS})

set (SRT_LIBS_PRIVATE ${SSL_LIBRARIES})

if (MICROSOFT)
	set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ws2_32.lib)
endif()

# ---
# So, back to target: srt. Setting the rest of the settings for srt target.
# ---

# Applying this to public includes is not transitive enough.
# On Windows, apps require this as well, so it's safer to
# spread this to all targets.
if (PTHREAD_INCLUDE_DIR)
	include_directories(${PTHREAD_INCLUDE_DIR})
endif()

# Link libraries must be applied directly to the derivatives
# as virtual libraries (OBJECT-type) cannot have linkage declarations
# transitive or not.

foreach(tar ${srtpack_libspec_common})
	message(STATUS "ADDING TRANSITIVE LINK DEP to:${tar} : ${PTHREAD_LIBRARY} ${dep}")
	target_link_libraries (${tar} PUBLIC ${PTHREAD_LIBRARY} ${dep})
endforeach()


set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${PTHREAD_LIBRARY})

if ( LINUX )
	# Unsure why libdl was required, keeping for the time being.
	#target_link_libraries(${TARGET_srt} PUBLIC dl)
	set (IFNEEDED_SRT_LDFLAGS -pthread)
endif()


target_compile_definitions(srt_virtual PRIVATE -DSRT_EXPORTS )
target_compile_definitions(haicrypt_virtual PUBLIC -DHAICRYPT_DYNAMIC)
if (ENABLE_SHARED)
	target_compile_definitions(srt_virtual PUBLIC -DSRT_DYNAMIC) 
	target_compile_definitions(haicrypt_virtual PRIVATE -DHAICRYPT_EXPORTS)
endif()

if (MICROSOFT)
    target_link_libraries(${TARGET_srt}_shared PUBLIC Ws2_32.lib)
endif()

# Cygwin installs the *.dll libraries in bin directory and uses PATH.

set (INSTALL_SHARED_DIR lib)
if (CYGWIN)
	set (INSTALL_SHARED_DIR bin)
endif()


install(TARGETS ${INSTALL_TARGETS}
		RUNTIME DESTINATION bin
		ARCHIVE DESTINATION lib
		LIBRARY DESTINATION ${INSTALL_SHARED_DIR}
)
install(FILES ${HEADERS_srt} DESTINATION include/srt)
if (WIN32)
	install(FILES ${HEADERS_srt_win32} DESTINATION include/srt/win)
endif()

# ---
# That's all for target definition
# ---

join_arguments(SRT_EXTRA_CFLAGS ${SRT_EXTRA_CFLAGS})

#message(STATUS "Target srt: LIBSPEC: ${srtpack_libspec_common} SOURCES: {${SOURCES_srt}}  HEADERS: {${HEADERS_srt}}")

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_CFLAGS} ${SRT_GCC_WARN}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_CFLAGS} ${SRT_GCC_WARN}")

# PC file generation.
if (NOT DEFINED INSTALLDIR)
	set (INSTALLDIR ${CMAKE_INSTALL_PREFIX})
	get_filename_component(INSTALLDIR ${INSTALLDIR} ABSOLUTE)
endif()

# XXX
# These two flags are required if compiling a C application
# The problem is that pkg-config cannot return flags that are
# suitable for C or C++ only - just "cflags", and for C by default.
# This may cause trouble when you want to compile your app with static libstdc++;
# if your build requires it, you'd probably remove -lstdc++ from the list
# obtained by `pkg-config --libs`.
#
# Some sensible solution for that is desired. Currently turned on only on demand.
if (ENABLE_C_DEPS)
if ( LINUX )
	set (IFNEEDED_SRT_LDFLAGS "${IFNEEDED_SRT_LDFLAGS} -lstdc++ -lm")
endif()
endif()

join_arguments(SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE})

# haisrt.pc left temporarily for backward compatibility. To be removed in future!
configure_file(scripts/srt.pc.in haisrt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/haisrt.pc DESTINATION lib/pkgconfig)
configure_file(scripts/srt.pc.in srt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/srt.pc DESTINATION lib/pkgconfig)

# Applications

if ( HAVE_COMPILER_GNU_COMPAT )
	message(STATUS "C++ VERSION: Setting C++11 compat flag for gnu compiler")
	set (CFLAGS_CXX_STANDARD "-std=c++11")
else()
	message(STATUS "C++ VERSION: leaving default, not a GNU compiler, assuming C++11 or newer is default.")
	set (CFLAGS_CXX_STANDARD "")
endif()

# If static is available, link apps against static one.
# Otherwise link against shared one.

if (srt_libspec_static)
	set (srt_link_library ${TARGET_srt}_static)
else()
	set (srt_link_library ${TARGET_srt}_shared)
endif()

if (ENABLE_CXX11 AND MINGW)
	# FIXME: with MINGW, it fails to build apps that require C++11
	# https://github.com/Haivision/srt/issues/177
	message(WARNING "On MinGW, C++11 apps are blocked due to lacking proper C++11 headers for <thread>. FIX IF POSSIBLE.")
	set (ENABLE_CXX11 0)
endif()

macro(srt_add_program name)
	add_executable(${name} ${ARGN})
	target_include_directories(${name} PRIVATE apps)
	target_include_directories(${name} PRIVATE common)
	install(TARGETS ${name} RUNTIME DESTINATION bin)
endmacro()

macro(srt_make_application name)
	target_compile_options(${name} PRIVATE ${CFLAGS_CXX_STANDARD})

	# This is recommended by cmake, but it doesn't work anyway.
	# What is needed is that this below CMAKE_INSTALL_RPATH (yes, relative)
	# is added as is.
	# set (CMAKE_SKIP_RPATH FALSE)
	# set (CMAKE_SKIP_BUILD_RPATH FALSE)
	# set (CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
	# set (CMAKE_INSTALL_RPATH "../lib")
	# set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
	# set (FORCE_RPATH BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH_USE_LINK_PATH TRUE)

	if (LINUX)
		# This is only needed on Linux, on Windows (including Cygwin) the library file will
		# be placed into the binrary directory anyway.
		# XXX not sure about Mac.
		# XXX This needs to be probably lib64 on 64-bit systems and 64-bit build.
		# See this name used already in install(${TARGET_srt} LIBRARY DESTINATION...).
		set(FORCE_RPATH LINK_FLAGS -Wl,-rpath,.,-rpath,../lib,-rpath,../lib64 BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH_USE_LINK_PATH TRUE)
	endif()

	# We state that Darwin always uses CLANG compiler, which honors this flag the same way.
	set_target_properties(${name} PROPERTIES COMPILE_FLAGS "${CFLAGS_CXX_STANDARD} ${EXTRA_stransmit}" ${FORCE_RPATH})

	target_link_libraries(${name} ${srt_link_library})
endmacro()

macro(srt_add_application name sources)
	srt_add_program(${name} apps/${name}.cpp ${sources})
	srt_make_application(${name})
	install(TARGETS ${name} RUNTIME DESTINATION bin)
endmacro()

if ( ENABLE_CXX11 )

	# Make a virtual library of all shared app files
	MafRead(apps/support.maf
		SOURCES SOURCES_support_indir
	)
	adddirname(apps "${SOURCES_support_indir}" SOURCES_support)

	# A special trick that makes the shared application sources
	# to be compiled once for all applications. Maybe this virtual
	# library should be changed into a static one and made useful
	# for users.
	add_library(srtsupport_virtual OBJECT ${SOURCES_support})
	target_compile_options(srtsupport_virtual PUBLIC ${CFLAGS_CXX_STANDARD})
	set (VIRTUAL_srtsupport $<TARGET_OBJECTS:srtsupport_virtual>)

	# Applications

	srt_add_application(srt-live-transmit ${VIRTUAL_srtsupport})

	# For backward compatibility with the old name
	if (SYMLINKABLE)
		set (REPLI_COMMAND create_symlink )
	else()
		set (REPLI_COMMAND copy)
	endif()

	set (stransmit_path $<TARGET_FILE_DIR:srt-live-transmit>/stransmit${CMAKE_EXECUTABLE_SUFFIX})
	add_custom_command(
		TARGET srt-live-transmit
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E ${REPLI_COMMAND} $<TARGET_FILE:srt-live-transmit> ${stransmit_path})

	if (SYMLINKABLE)
		message(STATUS "BACKWARD COMPATIBLE 'stransmit': will use symbolic link")
		srt_install_symlink(srt-live-transmit ${CMAKE_INSTALL_PREFIX}/bin/stransmit)
	elseif(${CMAKE_MAJOR_VERSION} LESS 3)
		message(FATAL_ERROR "Your system can't install symbolic link to 'stransmit', copy-on-install requires cmake at least 3.0.2")
	else()
		# This installation doesn't work with cmake earlier than 3.0
		# (looxlike cmake 2.8 somehow doesn't have a problem with resolving the $<TARGET_FILE_DIR:...>
		# inside the generated makefile, but does have problem with its own generated cmake_install.cmake :D)
		message(STATUS "BACKWARD COMPATIBLE 'stransmit': will use copying")
		install(PROGRAMS ${stransmit_path} DESTINATION bin)
	endif()

	# srt-file-transmit must be temporarily blocked on Windows because it's not yet portable
	if (NOT WIN32)
		srt_add_application(srt-file-transmit ${VIRTUAL_srtsupport})
	endif()
	srt_add_application(srt-multiplex ${VIRTUAL_srtsupport})

	if (ENABLE_TESTING)

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
			MafRead(testing/${name}.maf SOURCES SOURCES_app_indir)
			adddirname(testing "${SOURCES_app_indir}" SOURCES_app)
			srt_add_program(${name} ${SOURCES_app})
		endmacro()

		srt_add_testprogram(utility-test)
		srt_add_testprogram(uriparser-test)
		target_compile_options(uriparser-test PRIVATE -DTEST)
		target_compile_options(uriparser-test PRIVATE ${CFLAGS_CXX_STANDARD})
		
		srt_add_testprogram(srt-test-live)
		srt_make_application(srt-test-live)
		if (NOT WIN32)
			# Still portability problem on Windows
			srt_add_testprogram(srt-test-file)
			srt_make_application(srt-test-file)
		endif()
	endif()

endif()

if (ENABLE_EXAMPLES)

	# No examples should need C++11
	macro(srt_add_example mainsrc)
		get_filename_component(name ${mainsrc} NAME_WE)
		srt_add_program(${name} examples/${mainsrc} ${ARGN})
	endmacro()

	srt_add_example(sendfile.cpp apps/logsupport.cpp)
	srt_make_application(sendfile)

	srt_add_example(recvfile.cpp apps/logsupport.cpp)
	srt_make_application(recvfile)

	srt_add_example(testcapi-connect.c)
	target_link_libraries(testcapi-connect ${srt_link_library} ${DEPENDS_srt})

endif()

install(PROGRAMS scripts/srt-ffplay DESTINATION bin)


if (DEFINED SRT_EXTRA_APPS_INC)
	include(${SRT_EXTRA_APPS_INC}.cmake)
	# No extra variables expected. Just use the variables
	# already provided and define additional targets.
endif()

if ( ENABLE_SUFLIP )
	set (SOURCES_suflip
		${CMAKE_CURRENT_SOURCE_DIR}/apps/suflip.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/common/uriparser.cpp
	)
	srt_add_program(suflip ${SOURCES_suflip})
	target_link_libraries(suflip ${srt_link_library})
	install(TARGETS suflip RUNTIME DESTINATION bin)
endif ()
