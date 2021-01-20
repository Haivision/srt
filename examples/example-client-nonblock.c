/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
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
#ifdef _WIN32
#define usleep(x) Sleep(x / 1000)
#else
#include <unistd.h>
#endif

#include "srt.h"

int main(int argc, char** argv)
{
    int ss, st;
    struct sockaddr_in sa;
    int yes = 1;
    const char message [] = "This message should be sent to the other side";

    if (argc != 3) {
      fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
      return 1;
    }

    printf("SRT startup\n");
    srt_startup();

    printf("Creating SRT socket\n");
    ss = srt_create_socket();
    if (ss == SRT_ERROR)
    {
        fprintf(stderr, "srt_socket: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("Creating remote address\n");
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &sa.sin_addr) != 1)
    {
        return 1;
    }

    int epollid = srt_epoll_create();
    if (epollid == -1)
    {
        fprintf(stderr, "srt_epoll_create: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt setsockflag\n");
    if (SRT_ERROR == srt_setsockflag(ss, SRTO_RCVSYN, &yes, sizeof yes)
        || SRT_ERROR == srt_setsockflag(ss, SRTO_SNDSYN, &yes, sizeof yes))
    {
        fprintf(stderr, "SRTO_SNDSYN or SRTO_RCVSYN: %s\n", srt_getlasterror_str());
        return 1;
    }

    int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    if (SRT_ERROR == srt_epoll_add_usock(epollid, ss, &modes))
    {
        fprintf(stderr, "srt_epoll_add_usock: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt connect\n");
    st = srt_connect(ss, (struct sockaddr*)&sa, sizeof sa);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        return 1;
    }

    // In case of a caller a connection event triggers write-readiness.
    int       len = 2;
    SRTSOCKET ready[2];
    if (srt_epoll_wait(epollid, 0, 0, ready, &len, -1, 0, 0, 0, 0) != -1)
    {
        SRT_SOCKSTATUS state = srt_getsockstate(ss);
        if (state != SRTS_CONNECTED)
        {
            fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
            return 1;
        }
    }
    else
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        return 1;
    }
    
    int i;
    for (i = 0; i < 100; i++)
    {
        int wready[2] = {SRT_INVALID_SOCK, SRT_INVALID_SOCK};
        int wlen      = 2;

        int timeout_ms = 5000; // ms
        int res = srt_epoll_wait(epollid, 0, 0, wready, &wlen, timeout_ms, 0, 0, 0, 0);
        if (res == SRT_ERROR)
        {
            fprintf(stderr, "srt_epoll_wait: %s\n", srt_getlasterror_str());
            return 1;
        }

        printf("srt sendmsg2 #%d >> %s\n", i, message);
        st = srt_sendmsg2(ss, message, sizeof message, NULL);
        if (st == SRT_ERROR)
        {
            fprintf(stderr, "srt_sendmsg: %s\n", srt_getlasterror_str());
            return 1;
        }

        usleep(1000);   // 1 ms
    }

    printf("srt close\n");
    st = srt_close(ss);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
        return 1;
    }

    printf("srt cleanup\n");
    srt_cleanup();
    return 0;
}
