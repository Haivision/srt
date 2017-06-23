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
option(ENABLE_SHARED "Should haisrt and haicrypt be built as a shared library" ON)
option(ENABLE_SUFLIP "Shuld suflip be built" OFF)

# Always turn logging on if the build type is debug
if (ENABLE_DEBUG)
	set(SRT_ENABLE_LOGGING ON)
endif()

set(NAME_haisrt "haisrt" CACHE STRING "The name for the haisrt library")
set(NAME_haicrypt "haicrypt" CACHE STRING "The name for the haicrypt library")

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
foreach (gnid Intel Clang GNU)
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

add_subdirectory(haicrypt)
add_subdirectory(srtcore)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_FLAGS} ${SRT_GCC_WARN}")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SRT_DEBUG_OPT} ${SRT_EXTRA_FLAGS} ${SRT_GCC_WARN}")

# PC file generation.
if (NOT DEFINED INSTALLDIR)
	set (INSTALLDIR ${CMAKE_INSTALL_PREFIX})
	get_filename_component(INSTALLDIR ${INSTALLDIR} ABSOLUTE)
endif()
configure_file(scripts/haisrt.pc.in haisrt.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/haisrt.pc DESTINATION lib/pkgconfig)


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
	target_link_libraries(stransmit ${NAME_haisrt} ${NAME_haicrypt})
	target_link_libraries(utility-test ${NAME_haisrt})
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


if (SRT_ENABLE_LOGGING)
	target_compile_definitions(${NAME_haicrypt} PUBLIC -DENABLE_LOGGING=1)
endif()
