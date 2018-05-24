/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include <haisrt/srt.h>

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
