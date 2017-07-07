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

if (WIN32)
	message(STATUS "Compiling win_time.cpp because on windows...")
	
    list(APPEND SOURCES_haicrypt ../common/win_time.cpp)
endif()

extract_sources(
	MANIFEST Manifest.txt
	COMPONENT haicrypt
	PUBLIC_HEADERS PUBLIC_HEADERS_haicrypt
	PROTECTED_HEADERS PROTECTED_HEADERS_haicrypt
	PRIVATE_HEADERS PRIVATE_HEADERS_haicrypt
	SOURCES SOURCES_haicrypt
)

add_library(${NAME_haicrypt} ${srt_libspec} ${PUBLIC_HEADERS_haicrypt} ${PROTECTED_HEADERS_haicrypt} ${PRIVATE_HEADERS_haicrypt} ${SOURCES_haicrypt})


if (${SRT_BUILD_PROFILE} AND HAVE_COMPILER_GNU_COMPAT)
    target_compile_options(${NAME_haicrypt} PUBLIC -g -pg)
	target_link_libraries(${NAME_haicrypt} PUBLIC -g -pg)
endif()


# add extra warning flags for gccish compilers
if (HAVE_COMPILER_GNU_COMPAT)
	target_compile_options (${NAME_haicrypt} PUBLIC -Wall -Wextra)
endif()

if (ENABLE_SHARED)
	target_compile_definitions(${NAME_haicrypt} PUBLIC -DHAICRYPT_DYNAMIC)
endif()
target_compile_definitions(${NAME_haicrypt} PRIVATE -DHAICRYPT_EXPORTS)


target_include_directories(${NAME_haicrypt} PRIVATE  ${OPENSSL_INCLUDE_DIR} PUBLIC ${SRT_SRC_HAICRYPT_DIR} ${SRT_INCLUDE_DIR})


target_link_libraries(${NAME_haicrypt} PRIVATE ${OPENSSL_LIBRARIES})
if (WIN32)
	target_link_libraries(${NAME_haicrypt} PRIVATE ws2_32.lib)
endif()

install(TARGETS ${NAME_haicrypt} 
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)

install(FILES ${HEADERS_haicrypt} ${PROTECTED_HEADERS_haicrypt} DESTINATION include/haicrypt)
