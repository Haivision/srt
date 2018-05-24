#
# SRT - Secure, Reliable, Transport
# Copyright (c) 2018 Haivision Systems Inc.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#

cmake_minimum_required (VERSION 2.8.12 FATAL_ERROR)
# XXX This can be potentially done in future, but there still exist
# some dependent project using cmake 2.8 - this can't be done this way.
#cmake_minimum_required (VERSION 3.0.2 FATAL_ERROR)
#project(SRT VERSION "1.2.1")
project(SRT C CXX)

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
include(haiUtil)
include(FindPkgConfig)
include(FindThreads)

set (SRT_VERSION 1.2.3)
set_version_variables(SRT_VERSION ${SRT_VERSION})

if (NOT DEFINED ENABLE_DEBUG)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set (ENABLE_DEBUG ON)
else()
	set (ENABLE_DEBUG OFF)
endif()
elseif (ENABLE_DEBUG)
	set (CMAKE_BUILD_TYPE "Debug")
endif()

# option defaults
if (ENV{SRT_LOGGING_ENABLED})
	set(ENABLE_LOGGING_DEFAULT ON)
else()
	set(ENABLE_LOGGING_DEFAULT OFF)
endif()

# options
option(CYGWIN_USE_POSIX "Should the POSIX API be used for cygwin. Ignored if the system isn't cygwin." OFF)
option(ENABLE_CXX11 "Should the c++11 parts (stransmit) be enabled" ON)
option(ENABLE_PROFILE "Should the build contain profile information. Ignored if compiler isn't GNU compatable." env{HAI_BUILD_PROFILE})
option(ENABLE_LOGGING "Should logging be enabled" ${ENABLE_LOGGING_DEFAULT})
option(ENABLE_SHARED "Should libsrt be built as a shared library" ON)
option(ENABLE_SEPARATE_HAICRYPT "Should haicrypt be built as a separate library file" OFF)
option(ENABLE_SUFLIP "Shuld suflip tool be built" OFF)
option(USE_GNUTLS "Should use gnutls instead of openssl" OFF)

# Always turn logging on if the build type is debug
if (ENABLE_DEBUG)
	set(ENABLE_LOGGING ON)
endif()

set(TARGET_srt "srt" CACHE STRING "The name for the haisrt library")
set(TARGET_haicrypt "haicrypt" CACHE STRING "The name for the haicrypt library, if compiled separately")

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

if (DEFINED WITH_SRT_NAME)
	set (TARGET_haisrt ${WITH_SRT_TARGET})
endif()

if (DEFINED WITH_HAICRYPT_NAME)
	set (TARGET_haicrypt ${WITH_HAICRYPT_TARGET})
endif()

set_if(DARWIN ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
set_if(LINUX ${CMAKE_SYSTEM_NAME} MATCHES "Linux")

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
else()
	set (SSL_REQUIRED_MODULES "openssl libcrypto zlib")
	find_package(OpenSSL REQUIRED)
	set (SSL_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
	set (SSL_LIBRARIES ${OPENSSL_LIBRARIES})

	add_definitions(
		-DHAICRYPT_USE_OPENSSL_EVP=1
		-DHAICRYPT_USE_OPENSSL_AES
	)
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
	message(WARNING "Parts that require C++11 support will be disabled (stransmit)")
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


if ( ENABLE_SHARED )
    set (srt_libspec SHARED)
else()
    set (srt_libspec STATIC)
endif()

if (ENABLE_SEPARATE_HAICRYPT)
	set (haicrypt_libspec ${srt_libspec})
else()
	set (haicrypt_libspec VIRTUAL)
endif()


set (SRT_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/common)

set (SRT_SRC_HAICRYPT_DIR ${CMAKE_SOURCE_DIR}/haicrypt)
set (SRT_SRC_SRTCORE_DIR ${CMAKE_SOURCE_DIR}/srtcore)
set (SRT_SRC_COMMON_DIR ${CMAKE_SOURCE_DIR}/common)
set (SRT_SRC_TOOLS_DIR ${CMAKE_SOURCE_DIR}/tools)

if(WIN32)
    add_definitions(-DWIN32=1 -DPTW32_STATIC_LIB)
elseif(DARWIN)
    message(STATUS "DARWIN detected")
    add_definitions(-DOSX=1)
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
include_directories(${SRT_INCLUDE_DIR})

if (ENABLE_LOGGING)
    list(APPEND SRT_EXTRA_CFLAGS "-DENABLE_LOGGING=1")
endif()

if (${ENABLE_PROFILE} AND HAVE_COMPILER_GNU_COMPAT)
    # They are actually cflags, not definitions, but CMake is stupid enough.
    add_definitions(-g -pg)
	link_libraries(-g -pg)
endif()


if (NOT MINGW)
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
	message(FATAL_ERROR "Failed to find pthread library. Specify PTHREAD_LIBRARY.")
endif()

elseif(THREADS_FOUND)
	set(PTHREAD_LIBRARY ${CMAKE_THREAD_LIBS_INIT})
else()
	find_library(PTHREAD_LIBRARY NAMES pthread pthreadGC2 pthreadGC)
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
	set (HAICRYPT_FILELIST_MAF "haicrypt/filelist-gnutls.maf")
else()
	set (HAICRYPT_FILELIST_MAF "haicrypt/filelist.maf")
endif()

MafRead(${HAICRYPT_FILELIST_MAF}
	SOURCES SOURCES_haicrypt_indir
	PUBLIC_HEADERS HEADERS_haicrypt_indir
	PROTECTED_HEADERS HEADERS_haicrypt_indir
)

adddirname(haicrypt "${SOURCES_haicrypt_indir}" SOURCES_haicrypt)
adddirname(haicrypt "${HEADERS_haicrypt_indir}" HEADERS_haicrypt)

if (WIN32)
	MafRead(common/filelist_win32.maf
		SOURCES SOURCES_common_indir
		PUBLIC_HEADERS HEADERS_common_indir
		PROTECTED_HEADERS HEADERS_common_indir
	)
	message(STATUS "WINDOWS detected: adding compat sources: ${SOURCES_common_indir}")
	adddirname(common "${SOURCES_common_indir}" SOURCES_haicrypt)

	# WARNING!
	# The common headers are attached to haicrypt in case when
	# the haicrypt library is a separate library. If this is a
	# virtual library, the common headers will be attached to
	# srt headers, and the haicrypt headers will not be published.
	adddirname(common "${HEADERS_common_indir}" HEADERS_srt_win32)
endif()

message(STATUS "SOURCES(haicrypt): ${SOURCES_haicrypt}")

# NOTE: The "virtual library" is a library specification that cmake
# doesn't support. It's a private-only dependency type, where the project
# isn't compiled into any library file at all - instead, all of its source
# files are incorporated directly to the source list of the project that
# depends on it. In cmake this must be handled manually.

# For a separate haicrypt library it's allowed that the common compat
# things are attached to haicrypt library. In the known uses of the
# library separation, however, there is no extra compat stuff on the
# platform where it's being used.
if (NOT haicrypt_libspec STREQUAL VIRTUAL)
	message(STATUS "Making haicrypt as a ${haicrypt_libspec} library")
	add_library(${TARGET_haicrypt} ${haicrypt_libspec} ${SOURCES_haicrypt})

	# Note: POSIX specific; this is used in haisrt.pc.in
	set (IFNEEDED_LINK_HAICRYPT -l${TARGET_haicrypt})

	# Add these extra settings only in case when haicrypt is compiled
	# as a separate library. They are public interface oriented, so for
	# virtual library - which is always a private dependency - it's not needed.

	if (ENABLE_SHARED)
		target_compile_definitions(${TARGET_haicrypt} PUBLIC -DHAICRYPT_DYNAMIC)
	endif()
	target_compile_definitions(${TARGET_haicrypt} PRIVATE -DHAICRYPT_EXPORTS)

	install(TARGETS ${TARGET_haicrypt}
		RUNTIME DESTINATION bin
		ARCHIVE DESTINATION lib
		LIBRARY DESTINATION lib
	)
	install(FILES ${HEADERS_haicrypt} DESTINATION include/srt)
	if (WIN32)
		install(FILES ${HEADERS_srt_win32} DESTINATION include/srt/win)
	endif()
endif()
# NOTE: rest of the settings for haicrypt follow.


# ---
# Target: srt. DEFINITION ONLY. Haicrypt flag settings follow.
# ---
MafRead(srtcore/filelist.maf
	SOURCES SOURCES_srt_indir
	PUBLIC_HEADERS HEADERS_srt_indir
	PROTECTED_HEADERS HEADERS_srt_indir
)

adddirname(srtcore "${SOURCES_srt_indir}" SOURCES_srt)
adddirname(srtcore "${HEADERS_srt_indir}" HEADERS_srt)

# Manual handling of dependency on virtual library
if (haicrypt_libspec STREQUAL VIRTUAL)
	message(STATUS "Haicrypt attached to sources of srt")
	# By setting the target, all settings applied to the haicrypt target
	# will now apply to the dependent library.
	set (TARGET_haicrypt ${TARGET_srt}) 
	list(APPEND SOURCES_srt ${SOURCES_haicrypt})
	set (DEPENDS_srt)
	set (HEADERS_srt ${HEADERS_srt} ${HEADERS_srt_win32})
else()
	set (DEPENDS_srt ${TARGET_haicrypt})
endif()

add_library(${TARGET_srt} ${srt_libspec} ${SOURCES_srt} ${SOURCES_srt_extra})

# ---
# And back to target: haicrypt. Both targets must be defined
# prior to setting flags, and after defining the list of sources
# can no longer be extended.
#
# For haicrypt.spec = VIRTUAL, these settings apply to srt.
# Otherwise they apply to haicrypt.
# ---


target_include_directories(${TARGET_haicrypt}
	PRIVATE  ${SSL_INCLUDE_DIRS}
	PUBLIC ${SRT_SRC_HAICRYPT_DIR}
)

set_target_properties (${TARGET_haicrypt} PROPERTIES VERSION ${SRT_VERSION} SOVERSION ${SRT_VERSION_MAJOR})
target_link_libraries(${TARGET_haicrypt} PRIVATE ${SRT_LIBS_PRIVATE} ${SSL_LIBRARIES})

if ( WIN32 AND NOT CYGWIN )
	target_link_libraries(${TARGET_haicrypt} PRIVATE ws2_32)
	set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} "-lws2_32")
endif()

# ---
# So, back to target: srt. Setting the rest of the settings for srt target.
# ---

set_target_properties (${TARGET_srt} PROPERTIES VERSION ${SRT_VERSION} SOVERSION ${SRT_VERSION_MAJOR})
target_link_libraries (${TARGET_srt} PUBLIC ${PTHREAD_LIBRARY} ${DEPENDS_srt})
set (SRT_LIBS_PRIVATE ${SRT_LIBS_PRIVATE} ${PTHREAD_LIBRARY})
target_include_directories(${TARGET_srt} PUBLIC ${PTHREAD_INCLUDE_DIR} ${SRT_SRC_SRTCORE_DIR})

# Not sure why it's required, but somehow only on Linux
if ( LINUX )
	target_link_libraries(${TARGET_srt} PUBLIC rt)
	set (IFNEEDED_SRT_LDFLAGS -pthread)
endif()


target_compile_definitions(${TARGET_srt} PRIVATE -DUDT_EXPORTS )
if (ENABLE_SHARED)
	target_compile_definitions(${TARGET_srt} PUBLIC -DUDT_DYNAMIC) 
endif()

if ( WIN32 AND NOT CYGWIN )
	target_link_libraries(${TARGET_srt} PUBLIC ws2_32)
endif()

install(TARGETS ${TARGET_srt}
		RUNTIME DESTINATION bin
		ARCHIVE DESTINATION lib
		LIBRARY DESTINATION lib
)
install(FILES ${HEADERS_srt} DESTINATION include/srt)
if (WIN32 AND TARGET_haicrypt STREQUAL TARGET_srt)
	install(FILES ${HEADERS_srt_win32} DESTINATION include/srt/win)
endif()

# ---
# That's all for target definition
# ---

message(STATUS "Target haicrypt: TYPE:${haicrypt_libspec} ACTUAL TARGET: ${TARGET_haicrypt} HEADERS: {${HEADERS_haicrypt}}")
message(STATUS "Target srt: TYPE:${srt_libspec} SOURCES: {${SOURCES_srt}} DEPENDS: {${DEPENDS_srt}} HEADERS: {${HEADERS_srt}}")

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
configure_file(scripts/haisrt.pc.in haisrt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/haisrt.pc DESTINATION lib/pkgconfig)
configure_file(scripts/haisrt.pc.in srt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/srt.pc DESTINATION lib/pkgconfig)

# Applications

if ( HAVE_COMPILER_GNU_COMPAT )
	message(STATUS "C++ VERSION: Setting C++11 compat flag for gnu compiler")
	set (CFLAGS_CXX_STANDARD "-std=c++11")
else()
	message(STATUS "C++ VERSION: leaving default, not a GNU compiler, assuming C++11 or newer is default.")
	set (CFLAGS_CXX_STANDARD "")
endif()

if ( ENABLE_CXX11 )

# FIXME: with MINGW, it fails to build stransmit
# https://github.com/Haivision/srt/issues/177
if ( NOT MINGW )
	add_executable(stransmit
		${CMAKE_SOURCE_DIR}/apps/stransmit.cpp
		${CMAKE_SOURCE_DIR}/common/uriparser.cpp
		${CMAKE_SOURCE_DIR}/common/socketoptions.cpp
	)

	# Test programs
	add_executable(utility-test ${CMAKE_SOURCE_DIR}/apps/utility-test.cpp)

	# We state that Darwin always uses CLANG compiler, which honors this flag the same way.
	set_target_properties(stransmit PROPERTIES COMPILE_FLAGS "${CFLAGS_CXX_STANDARD} ${EXTRA_stransmit}")
	target_link_libraries(stransmit ${TARGET_srt} ${TARGET_haicrypt})
	target_link_libraries(utility-test ${TARGET_srt})
	install(TARGETS stransmit RUNTIME DESTINATION bin)
	install(PROGRAMS scripts/sfplay DESTINATION bin)

endif() # if ( NOT MINGW )

endif()

if (DEFINED SRT_EXTRA_APPS_INC)
	include(${SRT_EXTRA_APPS_INC}.cmake)
	# No extra variables expected. Just use the variables
	# already provided and define additional targets.
endif()

if ( ENABLE_SUFLIP )
	set (SOURCES_suflip
		${CMAKE_SOURCE_DIR}/apps/suflip.cpp
		${CMAKE_SOURCE_DIR}/common/uriparser.cpp
	)

	set(LIBS_suflip ${TARGET_haicrypt} ${TARGET_srt})

	add_executable(suflip ${SOURCES_suflip})
	target_link_libraries(suflip ${LIBS_suflip})
	install(TARGETS suflip RUNTIME DESTINATION bin)
endif ()
