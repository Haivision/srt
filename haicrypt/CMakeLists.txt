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

set (SOURCES_haicrypt
    hcrypt.c
	hcrypt.h
	hcrypt_ctx_rx.c
	hcrypt_ctx_tx.c
	hcrypt_rx.c
	hcrypt_sa.c
	hcrypt_tx.c
	hcrypt_ut.c
	hcrypt_xpt_srt.c
	hcrypt_xpt_sta.c
	hc_openssl_aes.c
	hc_openssl_evp_cbc.c
	hc_openssl_evp_ctr.c
)

if (WIN32)
	message(STATUS "Compiling win_time.cpp because on windows...")
	
    list(APPEND SOURCES_haicrypt windows/win_time.cpp)
endif()


set (HEADERS_haicrypt
    ${SRT_INCLUDE_DIR}/haicrypt.h
    ${SRT_INCLUDE_DIR}/hcrypt_ctx.h
    ${SRT_INCLUDE_DIR}/hcrypt_msg.h
)


add_library(${NAME_haicrypt} ${srt_libspec} ${SOURCES_haicrypt} ${HEADERS_haicrypt})


if (${SRT_BUILD_PROFILE} AND HAVE_COMPILER_GNU_COMPAT)
    target_compile_options(${NAME_haicrypt} PUBLIC -g -pg)
	target_link_libraries(${NAME_haicrypt} PUBLIC -g -pg)
endif()


# add extra warning flags for gccish compilers
if (HAVE_COMPILER_GNU_COMPAT)
	target_compile_options (${NAME_haicrypt} PUBLIC -Wall -Wextra)
endif()

if (BUILD_SHARED)
	target_compile_definitions(${NAME_haicrypt} PUBLIC -DHAICRYPT_DYNAMIC)
endif()
target_compile_definitions(${NAME_haicrypt} PRIVATE -DHAICRYPT_EXPORTS)


target_include_directories(${NAME_haicrypt} PRIVATE  ${OPENSSL_INCLUDE_DIR} PUBLIC ${SRT_SRC_HAICRYPT_DIR} ${SRT_INCLUDE_DIR})

set (LDFLAGS_haicrypt_STATIC ${WITH_OPENSSL_LDFLAGS})
target_link_libraries(${NAME_haicrypt} PUBLIC ${OPENSSL_LIBRARIES})
if (WIN32)
	target_link_libraries(${NAME_haicrypt} PRIVATE ws2_32.lib)
endif()

install(TARGETS ${NAME_haicrypt} DESTINATION lib)
install(FILES ${HEADERS_haicrypt} DESTINATION include/haicrypt)


