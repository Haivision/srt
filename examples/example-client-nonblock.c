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
    const int no = 0;
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
    if (SRT_ERROR == srt_setsockflag(ss, SRTO_RCVSYN, &no, sizeof no)
        || SRT_ERROR == srt_setsockflag(ss, SRTO_SNDSYN, &no, sizeof no))
    {
        fprintf(stderr, "SRTO_SNDSYN or SRTO_RCVSYN: %s\n", srt_getlasterror_str());
        return 1;
    }

    // When a caller is connected, a write-readiness event is triggered.
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

    // We had subscribed for write-readiness or error.
    // Write readiness comes in wready array,
    // error is notified via rready in this case.
    int       rlen = 1;
    SRTSOCKET rready;
    int       wlen = 1;
    SRTSOCKET wready;
    if (srt_epoll_wait(epollid, &rready, &rlen, &wready, &wlen, -1, 0, 0, 0, 0) != -1)
    {
        SRT_SOCKSTATUS state = srt_getsockstate(ss);
        if (state != SRTS_CONNECTED || rlen > 0) // rlen > 0 - an error notification
        {
            fprintf(stderr, "srt_epoll_wait: reject reason %s\n", srt_rejectreason_str(srt_getrejectreason(rready)));
            return 1;
        }

        if (wlen != 1 || wready != ss)
        {
            fprintf(stderr, "srt_epoll_wait: wlen %d, wready %d, socket %d\n", wlen, wready, ss);
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
        rready = SRT_INVALID_SOCK;
        rlen   = 1;
        wready = SRT_INVALID_SOCK;
        wlen   = 1;

        // As we have subscribed only for write-readiness or error events,
        // but have not subscribed for read-readiness,
        // through readfds we are notified about an error.
        int timeout_ms = 5000; // ms
        int res = srt_epoll_wait(epollid, &rready, &rlen, &wready, &wlen, timeout_ms, 0, 0, 0, 0);
        if (res == SRT_ERROR || rlen > 0)
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

    // Let's wait a bit so that all packets reach destination
    usleep(100000);   // 100 ms

    // In live mode the connection will be closed even if some packets were not yet acknowledged.
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
