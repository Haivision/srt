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

extract_sources(
	MANIFEST Manifest.txt
	COMPONENT srt
	PUBLIC_HEADERS PUBLIC_HEADERS_srt
	PROTECTED_HEADERS PROTECTED_HEADERS_srt
	PRIVATE_HEADERS PRIVATE_HEADERS_srt
	SOURCES SOURCES_srt
)


# HaiSRT: SRT library (UDT API only)
add_library(${NAME_haisrt} ${srt_libspec} ${PUBLIC_HEADERS_srt} ${PROTECTED_HEADERS_srt} ${PRIVATE_HEADERS_srt} ${SOURCES_srt})


target_link_libraries (${NAME_haisrt} PUBLIC ${PTHREAD_LIBRARY} ${NAME_haicrypt})
target_include_directories(${NAME_haisrt} PUBLIC ${PTHREAD_INCLUDE_DIR} ${SRT_SRC_SRTCORE_DIR})

# Not sure why it's required, but somehow only on Linux
if ( LINUX )
	target_link_libraries(${NAME_haisrt} PUBLIC rt)
endif()


target_compile_definitions(${NAME_haisrt} PRIVATE -DUDT_EXPORTS )
if (ENABLE_SHARED)
	target_compile_definitions(${NAME_haisrt} PUBLIC -DUDT_DYNAMIC) 
endif()

if ( WIN32 )
	if (NOT CYGWIN)
    	target_link_libraries(${NAME_haisrt} PUBLIC Ws2_32.lib)
	endif()
endif()

install(TARGETS ${NAME_haisrt} 
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)

install(FILES ${PUBLIC_HEADERS_srt} ${PROTECTED_HEADERS_srt} DESTINATION include/srt)

