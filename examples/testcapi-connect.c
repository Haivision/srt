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

#include "srt.h"

int main( int argc, char** argv )
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <remote host> <remote port>\n", argv[0]);
        return 1;
    }

    int ss, st;
    struct sockaddr_in sa;
    int yes = 1;
    const char message [] = "This message should be sent to the other side";

    srt_startup();

    ss = srt_create_socket();
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

    // This is obligatory only in live mode, if you predict to connect
    // to a peer with SRT version 1.2.0 or older. Not required since
    // 1.3.0, and all older versions support only live mode.
    //srt_setsockflag(ss, SRTO_SENDER, &yes, sizeof yes);
    //
    // In order to make sure that the client supports non-live message
    // mode, let's require this.
    int minversion = SRT_VERSION_FEAT_HSv5;
    srt_setsockflag(ss, SRTO_MINVERSION, &minversion, sizeof minversion);

    // Require also non-live message mode.
    int file_mode = SRTT_FILE;
    srt_setsockflag(ss, SRTO_TRANSTYPE, &file_mode, sizeof file_mode);
    srt_setsockflag(ss, SRTO_MESSAGEAPI, &yes, sizeof yes);

    // Note that the other side will reject the connection if the
    // listener didn't set the same mode.

    st = srt_connect(ss, (struct sockaddr*)&sa, sizeof sa);
    if ( st == SRT_ERROR )
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        return 1;
    }

    st = srt_send(ss, message, sizeof message);
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
