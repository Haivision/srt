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


#include "srt.h"

// Note that in this example application there's a listening
// socket, off which then a transmission socket is accepted,
// then this socket will be used for reading. Therefore the same
// function will be used for waiting for the listener to get the
// accepted socket ready and then to wait for read-readiness on
// the transmission socket. For a model of waiting for write-ready
// see test-c-client-bonding.c file.
int WaitForReadReady(int eid, SRTSOCKET ss)
{
    int ready_in[2];
    int ready_in_len = 2;
    int ready_err[2];
    int ready_err_len = 2;

    int st = srt_epoll_wait(eid, ready_in, &ready_in_len, ready_err, &ready_err_len, -1,
            0, 0, 0, 0);

    // Note: with indefinite wait time we can either have a connection reported
    // or possibly error. Also srt_epoll_wait never returns 0 - at least the number
    // of ready connections is reported or -1 is returned for error, including timeout.
    if (st < 1)
    {
        fprintf(stderr, "srt_epoll_wait: %s\n", srt_getlasterror_str());
        return 0;
    }

    // Check if this was reported as error-ready, in which case it doesn't
    // matter if read-ready.
    if (ready_err[0] == ss)
    {
        fprintf(stderr, "srt_epoll_wait: socket @%d reported error\n", ss);
        return 0;
    }

    if (ready_in[0] != ss)
    {
        fprintf(stderr, "srt_epoll_wait: socket @%d not reported ready\n", ss);
        return 0;
    }

    return 1;
}

int main(int argc, char** argv)
{
    int globstatus = 0;

    int ss, st;
    struct sockaddr_in sa;
    int yes = 1;
    struct sockaddr_storage their_addr;
    SRT_SOCKGROUPDATA* grpdata = NULL;

    if (argc < 3 || argc > 4)
    {
        fprintf(stderr, "Usage: %s <host> <port> [options]\n", argv[0]);
        return 1;
    }

    printf("srt startup\n");
    srt_startup();
    // Since now, srt_cleanup() must be done before exiting.

    printf("srt socket\n");
    ss = srt_create_socket();
    if (ss == SRT_ERROR)
    {
        fprintf(stderr, "srt_socket: %s\n", srt_getlasterror_str());
        globstatus = 1;
        goto cleanup;
    }
    // Now that the socket is created, jump to 'end' on error.

    // Check options
    int is_nonblocking = 0;
    SRTSOCKET their_fd = SRT_INVALID_SOCK; // declared early because of gotos
    int eid = -1;
    int lsn_modes = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    int read_modes = lsn_modes;
    if (argc > 3)
    {
        const char* opt = argv[3];
        if (strchr(opt, 'n'))
            is_nonblocking = 1;
    }

    printf("srt bind address\n");
    if (0 == strcmp(argv[1], "0"))
    {
        memset(&sa, 0, sizeof sa);
    }
    else if (inet_pton(AF_INET, argv[1], &sa.sin_addr) != 1)
    {
        fprintf(stderr, "srt_bind: Can't resolve address: %s\n", argv[1]);
        globstatus = 1;
        goto end;
    }
    sa.sin_family = AF_INET;
    sa.sin_port = htons(atoi(argv[2]));

    printf("srt setsockflag: groupconnect\n");
    srt_setsockflag(ss, SRTO_GROUPCONNECT, &yes, sizeof yes);

    printf("srt bind\n");
    st = srt_bind(ss, (struct sockaddr*)&sa, sizeof sa);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_bind: %s\n", srt_getlasterror_str());
        globstatus = 1;
        goto end;
    }

    if (is_nonblocking)
    {
        int blockingmode = 0;
        srt_setsockflag(ss, SRTO_RCVSYN, &blockingmode, sizeof (blockingmode));
        eid = srt_epoll_create();
        srt_epoll_add_usock(eid, ss, &lsn_modes);
    }

    printf("srt listen\n");

    // We set here 10, just for a case. Every unit in this number
    // defines a maximum number of connections that can be pending
    // simultaneously - it doesn't matter here if particular connection
    // will belong to a bonding group or will be a single-socket connection.
    st = srt_listen(ss, 10);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_listen: %s\n", srt_getlasterror_str());
        globstatus = 1;
        goto end;
    }


    // In this example, there will be prepared an array of 10 items.
    // The listener, however, doesn't know how many member connections
    // one bonded connection will contain, so a real application should be
    // prepared for dynamically adjusting the array size.
    const size_t N = 10;
    grpdata = calloc(N, sizeof (SRT_SOCKGROUPDATA));

    // In non-blocking mode you can't call srt_accept immediately.
    // You must first wait for readiness on the listener socket.
    if (is_nonblocking)
    {
        printf("srt wait for listener socket reporting in a new connection\n");
        if (!WaitForReadReady(eid, ss))
            goto end;
    }

    printf("srt accept\n");
    int addr_size = sizeof their_addr;
    their_fd = srt_accept(ss, (struct sockaddr *)&their_addr, &addr_size);

    if (their_fd == -1)
    {
        fprintf(stderr, "srt_accept: %s\n", srt_getlasterror_str());
        goto end;
    }

    printf("accepted socket: @%d\n", their_fd);

    // You never know if `srt_accept` is going to give you a socket or a group.
    // You have to check it on your own. The SRTO_GROUPCONNECT flag doesn't disallow
    // single socket connections.
    int isgroup = their_fd & SRTGROUP_MASK;

    if (!isgroup)
    {
        fprintf(stderr, "srt_accept: Accepted @%d is not a group???\n", their_fd);
        goto end;
    }

    if (is_nonblocking)
    {
        // NOTE: The SRTO_RCVSYN = false flag will be derived from
        // the listener socket and we are going to read, so it matches
        // the need. In case when you'd like to write to the accepted
        // socket, you'd have to set also SRTO_SNDSYN = false.
        srt_epoll_add_usock(eid, their_fd, &read_modes);

        // The listener socket is no longer important.
        srt_epoll_remove_usock(eid, ss);
    }

    // Still, use the same procedure for receiving, no matter if
    // this is a bonded or single connection.
    int i;
    for (i = 0; i < 100; i++)
    {
        printf("srt recvmsg #%d... ",i);
        char msg[2048];
        SRT_MSGCTRL mc = srt_msgctrl_default;
        mc.grpdata = grpdata;
        mc.grpdata_size = N;

        if (is_nonblocking)
        {
            // Block in epoll as srt_recvmsg2 will not block.
            if (!WaitForReadReady(eid, their_fd))
                goto end;
        }

        st = srt_recvmsg2(their_fd, msg, sizeof msg, &mc);
        if (st == SRT_ERROR)
        {
            fprintf(stderr, "srt_recvmsg: %s\n", srt_getlasterror_str());
            goto end;
        }

        printf("Got msg of len %d << %s (%s)\n", st, msg, isgroup ? "group" : "single");

        if (!isgroup)
            continue;

        if (!mc.grpdata)
            printf("Group status: [%zi] members > %zi, can't handle.\n", mc.grpdata_size, N);
        else
        {
            printf(" ++ Group status [%zi]: ", mc.grpdata_size);
            size_t z;
            for (z = 0; z < mc.grpdata_size; ++z)
            {
                printf( "[%zd] result=%d state=%s ", z, mc.grpdata[z].result,
                        mc.grpdata[z].sockstate <= SRTS_CONNECTING ? "pending" :
                        mc.grpdata[z].sockstate == SRTS_CONNECTED ? "connected" : "broken");
            }
            printf("\n");
        }
    }

end:
    if (eid != -1)
    {
        srt_epoll_release(eid);
    }
    free(grpdata);
    printf("srt close\n");
    st = srt_close(their_fd); // just for a case; broken socket should be wiped out anyway
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
        // But not matter, we're finishing here.
    }
    st = srt_close(ss);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
        // But not matter, we're finishing here.
    }

cleanup:
    printf("srt cleanup\n");
    srt_cleanup();
    return globstatus;
}
