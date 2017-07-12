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
cmake_minimum_required (VERSION 3.0.2 FATAL_ERROR)



project(SRT VERSION "1.2.0")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set (ENABLE_DEBUG ON)
else()
	set (ENABLE_DEBUG OFF)
endif()

# option defaults
if (ENV{SRT_LOGGING_ENABLED})
	set(SRT_ENABLE_LOGGING_DEFAULT ON)
else()
	set(SRT_ENABLE_LOGGING_DEFAULT OFF)
endif()

# options
option(CYGWIN_USE_POSIX "Should the POSIX API be used for cygwin. Ignored if the system isn't cygwin." OFF)
option(ENABLE_CXX11 "Should the c++11 parts (stransmit) be enabled" ON)
option(SRT_BUILD_PROFILE "Should the build contain profile information. Ignored if compiler isn't GNU compatable." env{HAI_BUILD_PROFILE})
option(SRT_ENABLE_LOGGING "Should logging be enabled" ${SRT_ENABLE_LOGGING_DEFAULT})
option(ENABLE_SHARED "Should libsrt be built as a shared library" ON)
option(ENABLE_SEPARATE_HAICRYPT "Should haicrypt be built as a separate library file" OFF)
option(ENABLE_SUFLIP "Shuld suflip be built" OFF)

# Always turn logging on if the build type is debug
if (ENABLE_DEBUG)
	set(SRT_ENABLE_LOGGING ON)
endif()

set(TARGET_srt "srt" CACHE STRING "The name for the haisrt library")
set(TARGET_haicrypt "haicrypt" CACHE STRING "The name for the haicrypt library")

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

set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/scripts")
include(haiUtil)

set_if(DARWIN ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
set_if(LINUX ${CMAKE_SYSTEM_NAME} MATCHES "Linux")

# find OpenSSL
find_package(OpenSSL REQUIRED)
message (STATUS "OpenSSL libraries: ${OPENSSL_LIBRARIES}")

# Detect if the compiler is GNU compatable for flags
set(HAVE_COMPILER_GNU_COMPAT 0)
foreach (gnid Intel Clang GNU AppleClang)
	if (${CMAKE_CXX_COMPILER_ID} STREQUAL ${gnid})
		set(HAVE_COMPILER_GNU_COMPAT 1)
		break()
	endif()
endforeach()

if (HAVE_COMPILER_GNU_COMPAT)
	message(STATUS "COMPILER: GNU compat: ${CMAKE_CXX_COMPILER}")
else()
	message(STATUS "COMPILER: NOT GNU compat: ${CMAKE_CXX_COMPILER}")
endif()

if (NOT ENABLE_CXX11)
	message(WARNING "Parts that require C++11 support will be disabled (stransmit)")
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

# get the windows specific source files
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
   -DHAICRYPT_USE_OPENSSL_EVP=1
   -DHAICRYPT_USE_OPENSSL_AES
   -DSRT_VERSION="${SRT_VERSION}"
)

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

# ---
# Target: haicrypt.
# Completing sources and installable headers. Flag settings will follow.
# ---
MafRead(haicrypt/filelist.maf
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

	install(TARGETS ${TARGET_haicrypt} DESTINATION lib)
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

add_library(${TARGET_srt} ${srt_libspec} ${SOURCES_srt})

# ---
# And back to target: haicrypt. Both targets must be defined
# prior to setting flags, and after defining the list of sources
# can no longer be extended.
#
# For haicrypt.spec = VIRTUAL, these settings apply to srt.
# Otherwise they apply to haicrypt.
# ---

if (${SRT_BUILD_PROFILE} AND HAVE_COMPILER_GNU_COMPAT)
    target_compile_options(${TARGET_haicrypt} PUBLIC -g -pg)
	target_link_libraries(${TARGET_haicrypt} PUBLIC -g -pg)
endif()

# add extra warning flags for gccish compilers
if (HAVE_COMPILER_GNU_COMPAT)
	target_compile_options (${TARGET_haicrypt} PUBLIC -Wall -Wextra)
endif()

target_include_directories(${TARGET_haicrypt}
	PRIVATE  ${OPENSSL_INCLUDE_DIR}
	PUBLIC ${SRT_SRC_HAICRYPT_DIR} ${SRT_INCLUDE_DIR}
)

target_link_libraries(${TARGET_haicrypt} PRIVATE ${OPENSSL_LIBRARIES})
if (WIN32)
	target_link_libraries(${TARGET_haicrypt} PRIVATE ws2_32.lib)
endif()

# ---
# So, back to target: srt. Setting the rest of the settings for srt target.
# ---

target_link_libraries (${TARGET_srt} PUBLIC ${PTHREAD_LIBRARY} ${DEPENDS_srt})
target_include_directories(${TARGET_srt} PUBLIC ${PTHREAD_INCLUDE_DIR} ${SRT_SRC_SRTCORE_DIR})

# Not sure why it's required, but somehow only on Linux
if ( LINUX )
	target_link_libraries(${TARGET_srt} PUBLIC rt)
endif()


target_compile_definitions(${TARGET_srt} PRIVATE -DUDT_EXPORTS )
if (ENABLE_SHARED)
	target_compile_definitions(${TARGET_srt} PUBLIC -DUDT_DYNAMIC) 
endif()

if ( WIN32 )
	if (NOT CYGWIN)
    	target_link_libraries(${TARGET_srt} PUBLIC Ws2_32.lib)
	endif()
endif()

install(TARGETS ${TARGET_srt} DESTINATION lib)
install(FILES ${HEADERS_srt} DESTINATION include/srt)
if (WIN32 AND TARGET_haicrypt STREQUAL TARGET_srt)
	install(FILES ${HEADERS_srt_win32} DESTINATION include/srt/win)
endif()

# ---
# That's all for target definition
# ---

message(STATUS "Target haicrypt: TYPE:${haicrypt_libspec} ACTUAL TARGET: ${TARGET_haicrypt} HEADERS: {${HEADERS_haicrypt}}")
message(STATUS "Target srt: TYPE:${srt_libspec} SOURCES: {${SOURCES_srt}} DEPENDS: {${DEPENDS_srt}} HEADERS: {${HEADERS_srt}}")

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_FLAGS} ${SRT_GCC_WARN}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_FLAGS} ${SRT_GCC_WARN}")

# PC file generation.
if (NOT DEFINED INSTALLDIR)
	set (INSTALLDIR ${CMAKE_INSTALL_PREFIX})
	get_filename_component(INSTALLDIR ${INSTALLDIR} ABSOLUTE)
endif()
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

	add_executable(stransmit
		${CMAKE_SOURCE_DIR}/apps/stransmit.cpp
		${CMAKE_SOURCE_DIR}/common/uriparser.cpp
		${CMAKE_SOURCE_DIR}/common/socketoptions.cpp
	)

	# Test programs
	add_executable(utility-test ${CMAKE_SOURCE_DIR}/apps/utility-test.cpp)

	# We state that Darwin always uses CLANG compiler, which honors this flag the same way.
	set_target_properties(stransmit PROPERTIES COMPILE_FLAGS "${CFLAGS_CXX_STANDARD}")
	target_link_libraries(stransmit ${TARGET_srt} ${TARGET_haicrypt})
	target_link_libraries(utility-test ${TARGET_srt})
	install(TARGETS stransmit RUNTIME DESTINATION bin)
	install(PROGRAMS scripts/sfplay DESTINATION bin)

endif()

if ( ENABLE_SUFLIP )
	set (SOURCES_suflip
		${CMAKE_SOURCE_DIR}/apps/suflip.cpp ${CMAKE_SOURCE_DIR}/apps/uriparser.cpp
	)

	set(LIBS_suflip ${TARGET_haicrypt} ${TARGET_srt})

	add_executable(suflip ${SOURCES_suflip})
	target_link_libraries(suflip ${LIBS_suflip})
	install(TARGETS suflip RUNTIME DESTINATION bin)
endif ()


if (SRT_ENABLE_LOGGING)
	target_compile_definitions(${TARGET_haicrypt} PUBLIC -DENABLE_LOGGING=1)
endif()
