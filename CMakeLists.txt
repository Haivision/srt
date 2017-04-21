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

if ( CYGWIN AND NOT CYGWIN_USE_POSIX )
	set(WIN32 1)
	set(CMAKE_LEGACY_CYGWIN_WIN32 1)
	add_definitions(-DWIN32=1 -DCYGWIN=1)
	message("HAVE CYGWIN. Setting backward compat CMAKE_LEGACY_CYGWIN_WIN32 and -DWIN32")
endif()


set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
include(haiUtil)
include(FindPkgConfig)


set (SRT_VERSION "1.2.0")

set (NAME_haisrt haisrt)
set (NAME_haicrypt haicrypt)

if ( DEFINED WITH_SRT_NAME )
	set (NAME_haisrt ${WITH_SRT_NAME})
endif()

if ( DEFINED WITH_HAICRYPT_NAME )
	set (NAME_haicrypt ${WITH_HAICRYPT_NAME})
endif()

set_if(DARWIN ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
set_if(LINUX ${CMAKE_SYSTEM_NAME} MATCHES "Linux")

# Prepare openssl variables:
# WITH_OPENSSL: general
# WITH_OPENSSL_INCLUDEDIR: <openssl>/include
# WITH_OPENSSL_LIBDIR: <openssl>/lib
# WITH_OPENSSL_LIBRARIES: empty as default, in which case it uses -lcrypto -lpthread

set (HAVE_OPENSSL 0)
if (DEFINED WITH_OPENSSL_INCLUDEDIR AND (DEFINED WITH_OPENSSL_LIBDIR OR DEFINED WITH_OPENSSL_LDFLAGS))
	set (HAVE_OPENSSL 1)
else()

	if(DEFINED WITH_OPENSSL)
		set (WITH_OPENSSL_INCLUDEDIR ${WITH_OPENSSL}/include)
		set (WITH_OPENSSL_LIBDIR ${WITH_OPENSSL}/lib)
		set (HAVE_OPENSSL 1)
	endif()
endif()


if ( WITH_COMPILER_PREFIX )

	set(CMAKE_CXX_COMPILER ${WITH_COMPILER_PREFIX}g++)
	set(CMAKE_C_COMPILER ${WITH_COMPILER_PREFIX}gcc)

endif()

if ( CMAKE_CROSSCOMPILING )
	set (HAVE_CROSSCOMPILER 1)
endif()


set (cid ${CMAKE_CXX_COMPILER_ID})
set (gnu_compat_compilers Intel Clang GNU)

# Perfect function, however it somehow doesn't work on Cygwin with cmake 3.6.0 :D
#list(FIND "${gnu_compat_compilers}" ${CMAKE_CXX_COMPILER_ID} HAVE_COMPILER_GNU_COMPAT)

set(HAVE_COMPILER_GNU_COMPAT 0)
foreach (gnid Intel Clang GNU)
	if (${CMAKE_CXX_COMPILER_ID} STREQUAL ${gnid})
		set(HAVE_COMPILER_GNU_COMPAT 1)
		break()
	endif()
endforeach()

#message( "In ${gnu_compat_compilers} found ${CMAKE_CXX_COMPILER_ID} at position ${i}")
if (${HAVE_COMPILER_GNU_COMPAT})
	message("COMPILER: GNU compat: ${CMAKE_CXX_COMPILER}")
else()
	message("COMPILER: NOT GNU compat: ${CMAKE_CXX_COMPILER}")
endif()


if (NOT HAVE_OPENSSL)
	message(FATAL_ERROR "OpenSSL location not provided. Please define at least WITH_OPENSSL for prefix path.")
endif()

# Openssl should be added as a dependency for haisrt
if (NOT DEFINED WITH_OPENSSL_LIBRARIES)
	if (NOT DEFINED WITH_OPENSSL_LDFLAGS)
		message(FATAL_ERROR "Need defined one of WITH_OPENSSL_LIBRARIES (filenames), WITH_OPENSSL_LDFLAGS (-l flags)")
	endif()
	separate_arguments(WITH_OPENSSL_LDFLAGS)

else()
	# If so, then add each file from this list with the directory specified in _LIBDIR to the list
	separate_arguments(WITH_OPENSSL_LIBRARIES)
	adddirname(${WITH_OPENSSL_LIBDIR} "${WITH_OPENSSL_LIBRARIES}" WITH_OPENSSL_LDFLAGS)
	unset (WITH_OPENSSL_LIBDIR)
endif()

if (NOT ${WITH_OPENSSL} STREQUAL "/usr")
	include_directories(${WITH_OPENSSL_INCLUDEDIR})
endif()

# Pthread - optional
# The following things must be specified on particular platform:
# - the -lpthread (or -pthread?) flag on Linux (as link-libraries)
# - the path to pthread API library on Windows (as link-libraries)

if (DEFINED WITH_PTHREAD_INCLUDEDIR)
	include_directories(${WITH_PTHREAD_INCLUDEDIR})
endif()


if ( DISABLE_CXX11 )
	set (ENABLE_CXX11 0)
elseif( DEFINED ENABLE_CXX11 )
else()
	set (ENABLE_CXX11 1)
endif()

if ( ENABLE_CXX11 EQUAL 0 )
	message("NOTE: Parts that require C++11 support will be disabled (stransmit)")
endif()


# Define Haisrt version
set_version_variables(SRT_VERSION ${SRT_VERSION})
message( "VERSION: major=${SRT_VERSION_MAJOR} minor=${SRT_VERSION_MINOR} patch=${SRT_VERSION_PATCH}" )

set (PACKAGE haisrt)
set (PACKAGE_BUGREPORT to-be-set)

if (ENABLE_DEBUG)
	set (CMAKE_BUILD_TYPE "Debug")
endif()

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set (SRT_DEBUG_ENABLED 1)
    set (SRT_DEBUG_OPT "-ggdb -O0 -D_DEBUG")
    message("Using DEBUG mode")
else()
    set (SRT_DEBUG_ENABLED 0)
    set (SRT_DEBUG_OPT "-O2")
    message("Using RELEASE mode")
endif()

# Enuff.

if ( ENABLE_PROFILE OR "$ENV{HAI_BUILD_PROFILE}" STREQUAL "1" )
    message("+++ Build will include PROFILE INFORMATION")
    set (SRT_BUILD_PROFILE 1)
else()
    set (SRT_BUILD_PROFILE 0)
endif()

if (HAVE_COMPILER_GNU_COMPAT)
	set (SRT_GCC_WARN "-Wall -Wextra")
else()
	# cpp debugging on Windows :D
	#set (SRT_GCC_WARN "/showIncludes")
endif()

# Logging is always enabled when debug build, and can be
# additionally enabled by setting SRT_LOGGING_ENABLED env to 1.
if ( "$ENV{SRT_LOGGING_ENABLED}" STREQUAL "1" )
    set(ENABLE_LOGGING 1)
endif()

if ( ENABLE_LOGGING OR SRT_DEBUG_ENABLED )
    list(APPEND SRT_EXTRA_FLAGS "-DENABLE_LOGGING=1")
endif()

if ( ENABLE_DYNAMIC )
    set (srt_libspec SHARED)
    set (srt_install LIBRARY)
else()
    set (srt_libspec STATIC)
    set (srt_install ARCHIVE)
endif()


set (SRT_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)

set (SRT_SRC_HAICRYPT_DIR ${CMAKE_SOURCE_DIR}/haicrypt)
set (SRT_SRC_SRTCORE_DIR ${CMAKE_SOURCE_DIR}/srtcore)
set (SRT_SRC_COMMON_DIR ${CMAKE_SOURCE_DIR}/common)
set (SRT_SRC_TOOLS_DIR ${CMAKE_SOURCE_DIR}/tools)

# This means: set LIBSRTCORE_SRCFILES [pfind *.c *.cpp $SRT_SRC_SRTCORE_DIR]
aux_source_directory(${SRT_SRC_SRTCORE_DIR} LIBSRTCORE_SRCFILES)
if(WIN32)
    message( "WIN32 detected - including win_time.cpp" )
    
    # Add Windows specific source files here.
    # These should provide various POSIX/Linux features
    # to Windows.
    set(winspec_SOURCES ${SRT_SRC_SRTCORE_DIR}/windows/win_time.cpp)

	if (NOT DEFINED WITH_PTHREAD_LDFLAGS OR NOT DEFINED WITH_PTHREAD_INCLUDEDIR)
		message(FATAL_ERROR "On Windows, PTHREAD library is 3rd party. WITH_PTHREAD_* variables must be specified.")
	endif()
elseif(DARWIN)
    message( "DARWIN detected")
    add_definitions(-DOSX=1) 
elseif(LINUX)
    add_definitions(-DLINUX=1)
    message( "LINUX detected" )
else()
    message(FATAL_ERROR "Unsupported system")
endif()
aux_source_directory(${SRT_SRC_HAICRYPT_DIR} LIBHAICRYPT_SRCFILES)


set (SOURCES_haisrtbase
    ${winspec_SOURCES}  # Adds ONLY ON WINDOWS its gettimeofday (used in haicrypt and srtcore)
	${SOURCES_haicrypt_dep}
)

set (SOURCES_haisrt
    ${LIBSRTCORE_SRCFILES}
    ${SRT_SRC_COMMON_DIR}/srt_compat.c
)

set (SOURCES_haicrypt
    ${LIBHAICRYPT_SRCFILES}
)

include_directories(
    ${SRT_SRC_SRTCORE_DIR}
    ${SRT_SRC_HAICRYPT_DIR}
    ${SRT_INCLUDE_DIR}
)

add_definitions(
   -D_GNU_SOURCE
   -DHAI_PATCH=1
   -DHAI_ENABLE_SRT=1
   -DHAICRYPT_USE_OPENSSL_EVP=1
   -DHAICRYPT_USE_OPENSSL_AES
   -DSRT_VERSION="${SRT_VERSION}"
)

if ( ${SRT_BUILD_PROFILE} )
    # They are actually cflags, not definitions, but CMake is stupid enough.
    add_definitions(-g -pg)
endif()



set (HEADERS_haisrt 
    ${SRT_SRC_SRTCORE_DIR}/srt.h
    ${SRT_SRC_SRTCORE_DIR}/udt.h
    ${SRT_SRC_SRTCORE_DIR}/srt4udt.h
	${SRT_SRC_SRTCORE_DIR}/logging_api.h

)

set (HEADERS_haicrypt
    ${SRT_INCLUDE_DIR}/haicrypt.h
    ${SRT_INCLUDE_DIR}/hcrypt_ctx.h
    ${SRT_INCLUDE_DIR}/hcrypt_msg.h
)

set (ALL_LIBS "")

#message("LIBRARY SPECIFICATION: ${srt_libspec}")

# HaiCrypt: cryptography wrapper, used by HaiSRT.

#message("HaiCrypt: SRC: ${SOURCES_haicrypt} ${SOURCES_haisrtbase}")

add_library(${NAME_haicrypt} ${srt_libspec} ${SOURCES_haicrypt} ${SOURCES_haisrtbase})
if (DEFINED WITH_OPENSSL_LIBDIR)
	set_target_properties(${NAME_haicrypt} PROPERTIES LINK_DIRECTORIES ${WITH_OPENSSL_LIBDIR})
endif()
if (DEFINED WITH_OPENSSL_INCLUDEDIR)
	target_include_directories(${NAME_haicrypt} PRIVATE ${WITH_OPENSSL_INCLUDEDIR})
endif()
set (LDFLAGS_haicrypt_STATIC ${WITH_OPENSSL_LDFLAGS})
target_link_libraries(${NAME_haicrypt} ${LDFLAGS_haicrypt_STATIC})
get_target_property(LDFLAGS_haicrypt_RESULT ${NAME_haicrypt} LINK_LIBRARIES)
#message("HaiCrypt: LDFLAGS: ${LDFLAGS_haicrypt_RESULT}")

install(TARGETS ${NAME_haicrypt} ${srt_install} DESTINATION lib)
install(FILES ${HEADERS_haicrypt} DESTINATION include/haicrypt)
list(APPEND ALL_LIBS ${NAME_haicrypt})

# HaiSRT: SRT library (UDT API only)
#message("HaiSRT: SRC: ${SOURCES_haisrt}")
add_library(${NAME_haisrt} ${srt_libspec} ${SOURCES_haisrt})
set (LDFLAGS_haisrt ${WITH_PTHREAD_LDFLAGS})

# Not sure why it's required, but somehow only on Linux
if ( LINUX )
	list(APPEND LDFLAGS_haisrt -lrt)
endif()
list(APPEND LDFLAGS_srt_STATIC ${LDFLAGS_haisrt} ${LDFLAGS_haicrypt_STATIC})
target_link_libraries(${NAME_haisrt} ${NAME_haicrypt} ${LDFLAGS_srt_STATIC})

get_target_property(LDFLAGS_srt_RESULT ${NAME_haisrt} LINK_LIBRARIES)
#message("HaiSRT: LDFLAGS: ${LDFLAGS_srt_RESULT}")

install(TARGETS ${NAME_haisrt} ${srt_install} DESTINATION lib)
install(FILES ${HEADERS_haisrt} DESTINATION include/srt)
list(APPEND ALL_LIBS ${NAME_haisrt})

if ( ${SRT_BUILD_PROFILE} )
    target_link_libraries(${NAME_haisrt} -g -pg)
endif()

if ( WIN32 )
    target_compile_definitions(${NAME_haisrt} PRIVATE -DUDT_EXPORTS)
    link_libraries(Ws2_32.lib)
    add_definitions(-DPTW32_STATIC_LIB)
endif()


set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_FLAGS} ${SRT_GCC_WARN}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_FLAGS} ${SRT_GCC_WARN}")


# PC file generation.
if (NOT DEFINED INSTALLDIR)
	set (INSTALLDIR ${CMAKE_INSTALL_PREFIX})
	get_filename_component(INSTALLDIR ${INSTALLDIR} ABSOLUTE)
endif()

# SRT_VERSION is there
if ( ${srt_libspec} STREQUAL "SHARED" )
	if ( LINUX )
		set (IFNEEDED_SRT_LDFLAGS -pthread)
	endif()
	set (LIBS_srtapps ${NAME_haisrt} ${NAME_haicrypt})
else()
	# Well, cmake has a basic functionality problem.
	# Even string(REPLACE) cannot do this - it replaces it with empty string!!!
	#string(REPLACE ";" " " IFNEEDED_SRT_LDFLAGS ${IFNEEDED_SRT_LDFLAGS})
	set (IFNEEDED_SRT_LDFLAGS "")
	# If there's separate_arguments, maybe there's also join_arguments?
	foreach (OPT ${LDFLAGS_srt_STATIC})
		set (IFNEEDED_SRT_LDFLAGS "${IFNEEDED_SRT_LDFLAGS} ${OPT}")
	endforeach()
	set (LIBS_srtapps ${NAME_haisrt} ${NAME_haicrypt} ${LDFLAGS_srt_STATIC} ${LDFLAGS_haicrypt_STATIC})
endif()

# XXX
# These two flags are required if compiling a C application
# The problem is that pkg-config cannot return flags that are
# suitable for C or C++ only - just "cflags", and for C by default.
# This may cause trouble when you want to compile your app with static libstdc++;
# if your build requires it, you'd probably remove -lstdc++ from the list
# obtained by `pkg-config --libs`.
#
# Some sensible solution for that is desired.
if ( LINUX )
	set (IFNEEDED_SRT_LDFLAGS "${IFNEEDED_SRT_LDFLAGS} -lstdc++ -lm")
endif()

configure_file(scripts/haisrt.pc.in haisrt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/haisrt.pc DESTINATION lib/pkgconfig)

# Applications

if ( HAVE_COMPILER_GNU_COMPAT )
	message("C++ VERSION: Setting C++11 compat flag for gnu compiler")
	set (CFLAGS_CXX_STANDARD "-std=c++11")
else()
	message("C++ VERSION: leaving default, not a GNU compiler, assuming C++11 or newer is default.")
	set (CFLAGS_CXX_STANDARD "")
endif()

if ( ENABLE_CXX11 )

	add_executable(stransmit ${CMAKE_SOURCE_DIR}/apps/stransmit.cpp ${CMAKE_SOURCE_DIR}/apps/uriparser.cpp)

	# Test programs
	add_executable(utility-test ${CMAKE_SOURCE_DIR}/apps/utility-test.cpp)

	# We state that Darwin always uses CLANG compiler, which honors this flag the same way.
	set_target_properties(stransmit PROPERTIES COMPILE_FLAGS "${CFLAGS_CXX_STANDARD}")
	target_link_libraries(stransmit ${LIBS_srtapps})
	install(TARGETS stransmit RUNTIME DESTINATION bin)
	install(PROGRAMS scripts/sfplay DESTINATION bin)

endif()

if ( ENABLE_SUFLIP )
	set (SOURCES_suflip 
		${CMAKE_SOURCE_DIR}/apps/suflip.cpp ${CMAKE_SOURCE_DIR}/apps/uriparser.cpp
	)

	set(LIBS_suflip ${NAME_haicrypt} ${NAME_haisrt})

	add_executable(suflip ${SOURCES_suflip})
	target_link_libraries(suflip ${LIBS_suflip})
	install(TARGETS suflip RUNTIME DESTINATION bin)
endif ()
