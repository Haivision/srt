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

set (SOURCES_haisrt
	api.cpp
	buffer.cpp
	cache.cpp
	ccc.cpp
	channel.cpp
	common.cpp
	core.cpp
	csrtcc.cpp
	epoll.cpp
	list.cpp
	md5.cpp
	packet.cpp
	queue.cpp
	srt_c_api.cpp
	window.cpp
	../common/srt_compat.c
)

set (HEADERS_haisrt 
	api.h
	buffer.h
	cache.h
	ccc.h
	channel.h
	common.h
	core.h
	csrtcc.h
	epoll.h
	list.h
	logging.h
	logging_api.h
	md5.h
	netinet_any.h
	packet.h
	platform_sys.h
	queue.h
	srt.h
	srt4udt.h
	threadname.h
	udt.h
	utilities.h
	window.h
)


# HaiSRT: SRT library (UDT API only)
add_library(${NAME_haisrt} ${srt_libspec} ${SOURCES_haisrt} ${HEADERS_haisrt})


target_link_libraries (${NAME_haisrt} PUBLIC ${PTHREAD_LIBRARY} ${NAME_haicrypt})
target_include_directories(${NAME_haisrt} PUBLIC ${PTHREAD_INCLUDE_DIR} ${SRT_SRC_SRTCORE_DIR})

# Not sure why it's required, but somehow only on Linux
if ( LINUX )
	target_link_libraries(${NAME_haisrt} PUBLIC rt)
endif()


target_compile_definitions(${NAME_haisrt} PRIVATE -DUDT_EXPORTS PUBLIC -DUDT_DYNAMIC)

if ( WIN32 )
	if (NOT CYGWIN)
    	target_link_libraries(${NAME_haisrt} PUBLIC Ws2_32.lib)
	endif()
endif()

install(TARGETS ${NAME_haisrt} DESTINATION lib)
install(FILES ${HEADERS_haisrt} DESTINATION include/srt)


