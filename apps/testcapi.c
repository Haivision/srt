/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 */

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "srt.h"

int main( int argc, char** argv )
{
    int ss, st;
    struct sockaddr_in sa;
    int yes = 1;
    const char message [] = "This message should be sent to the other side";

    srt_startup();

    ss = srt_socket(AF_INET, SOCK_DGRAM, 0);
    if ( ss == SRT_ERROR )
    {
        fprintf(stderr, "srt_socket: %s\n", srt_getlasterror_str());
        return 1;
    }

    sa.sin_port = htons(atoi(argv[2]));
    if ( inet_pton(AF_INET, argv[1], &sa.sin_addr) != 1)
    {
        return 1;
    }

    srt_setsockflag(ss, SRTO_SENDER, &yes, sizeof yes);

    st = srt_connect(ss, (struct sockaddr*)&sa, sizeof sa);
    if ( st == SRT_ERROR )
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        return 1;
    }

    st = srt_sendmsg2(ss, message, sizeof message, NULL);
    if ( st == SRT_ERROR )
    {
        fprintf(stderr, "srt_sendmsg: %s\n", srt_getlasterror_str());
        return 1;
    }

    st = srt_close(ss);
    if ( st == SRT_ERROR )
    {
        fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
        return 1;
    }

    srt_cleanup();
    return 0;
}
