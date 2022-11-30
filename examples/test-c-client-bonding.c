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
#ifdef _WIN32
#define usleep(x) Sleep(x / 1000)
#else
#include <unistd.h>
#endif

#include "srt.h"

struct
{
    const char* name;
    int gtype;
} group_types [] = {
    {
        "broadcast", SRT_GTYPE_BROADCAST
    }
    // Others will follow
};

#define SIZE(array) (sizeof array/sizeof(array[0]))

// Note that in this example application there's a socket
// used first to connect to the service and then it will be
// used for writing. Therefore the same function will be used
// for waiting for the socket to be connected and then to wait
// for write-ready on the socket used for transmission. For a
// model of waiting for read-ready see test-c-server-bonding.c file.
int WaitForWriteReady(int eid, SRTSOCKET ss)
{
    int ready_err[2];
    int ready_err_len = 2;
    int ready_out[2];
    int ready_out_len = 2;

    int st = srt_epoll_wait(eid, ready_err, &ready_err_len, ready_out, &ready_out_len, -1,
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
        int reason = srt_getrejectreason(ss);
        fprintf(stderr, "srt_epoll_wait: socket @%d reported error reason=%d: %s\n", ss, reason, srt_rejectreason_str(reason));
        return 0;
    }

    return 1;
}

int main(int argc, char** argv)
{
    int ss, st;
    struct sockaddr_in sa;
    //int yes = 1; // for options, none needed so far
    const char message [] = "This message should be sent to the other side";

    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <type> {<host> <port>}... \n", argv[0]);
        return 1;
    }

    int gtype = SRT_GTYPE_BROADCAST;
    size_t i;
    for (i = 0; i < SIZE(group_types); ++i)
        if (0 == strcmp(group_types[i].name, argv[1]))
        {
            gtype = group_types[i].gtype;
            break;
        }

    int is_nonblocking = 0;
    size_t nmemb = argc - 2;
    if (nmemb < 2)
    {
        fprintf(stderr, "Usage error: no members specified\n");
        return 1;
    }

    if (nmemb % 2)
    {
        // Last argument is then optionset
        --nmemb;
        const char* opt = argv[argc-1];
        if (strchr(opt, 'n'))
            is_nonblocking = 1;
    }

    nmemb /= 2;

    printf("srt startup\n");
    srt_startup();

    // Declare all variables before any destructive goto.
    // In C++ such a code that jumps over initialization would be illegal,
    // in C it causes an uninitialized value to be used.
    int eid = -1;
    int write_modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    SRT_SOCKGROUPDATA* grpdata = NULL;
    SRT_SOCKGROUPCONFIG* grpconfig = calloc(nmemb, sizeof (SRT_SOCKGROUPCONFIG));

    printf("srt group\n");
    ss = srt_create_group(gtype);
    if (ss == SRT_ERROR)
    {
        fprintf(stderr, "srt_create_group: %s\n", srt_getlasterror_str());
        goto end;
    }

    const int B = 2;

    for (i = 0; i < nmemb; ++i)
    {
        printf("srt remote address #%zi\n", i);

        sa.sin_family = AF_INET;
        sa.sin_port = htons(atoi(argv[B + 2*i + 1]));
        if (inet_pton(AF_INET, argv[B + 2*i], &sa.sin_addr) != 1)
        {
            fprintf(stderr, "inet_pton: can't resolve address: %s\n", argv[B + 2*i]);
            goto end;
        }

        grpconfig[i] = srt_prepare_endpoint(NULL, (struct sockaddr*)&sa, sizeof sa);
    }

    if (is_nonblocking)
    {
        int blockingmode = 0;
        srt_setsockflag(ss, SRTO_RCVSYN, &blockingmode, sizeof (blockingmode));
        eid = srt_epoll_create();
        srt_epoll_add_usock(eid, ss, &write_modes);
    }

    printf("srt connect (group)\n");

    // Note: this function unblocks at the moment when at least one connection
    // from the array is established (no matter which one); the others will
    // continue in background.
    st = srt_connect_group(ss, grpconfig, nmemb);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        goto end;
    }

    // In non-blocking mode the srt_connect function returns immediately
    // and displays only errors of the initial usage, not runtime errors.
    // These could be reported by epoll.
    if (is_nonblocking)
    {
        // WRITE-ready means connected

        printf("srt wait for socket reporting connection success\n");
        if (!WaitForWriteReady(eid, ss))
            goto end;
    }

    // In non-blocking mode now is the time to possibly change the epoll.
    // As this socket will be used for writing, it is in the right mode already.
    // Just set the right flag, as for non-blocking connect it needs RCVSYN.
    if (is_nonblocking)
    {
        int blockingmode = 0;
        srt_setsockflag(ss, SRTO_SNDSYN, &blockingmode, sizeof (blockingmode));
    }

    // Important: Normally you need that at least one link is ready for
    // the group link to be ready. All but first are done actually in
    // background, so this sleep only makes it more probable. If you'd like
    // to make sure that ALL links are established - by some reason - then
    // you'd have to subscribe for epoll event SRT_EPOLL_UPDATE and after the
    // connect function exits do checks by srt_group_data to see if all links
    // are established, and if not, repeat it after srt_epoll_wait for the
    // SRT_EPOLL_UPDATE signal.
    printf("sleeping 1s to make it probable all links are established\n");
    sleep(1);

    grpdata = calloc(nmemb, sizeof (SRT_SOCKGROUPDATA));

    for (i = 0; i < 100; i++)
    {
        printf("srt sendmsg2 #%zd >> %s\n",i,message);

        SRT_MSGCTRL mc = srt_msgctrl_default;
        mc.grpdata = grpdata;
        mc.grpdata_size = nmemb; // Set maximum known

        if (is_nonblocking)
        {
            // Block in epoll as srt_recvmsg2 will not block.
            if (!WaitForWriteReady(eid, ss))
                goto end;
        }

        st = srt_sendmsg2(ss, message, sizeof message, &mc);
        if (st == SRT_ERROR)
        {
            fprintf(stderr, "srt_sendmsg: %s\n", srt_getlasterror_str());
            goto end;
        }

        // Perform the group check. This can be used to recognize broken connections
        // and probably reestablish them by calling `srt_connect` for them. Here they
        // are only shown.
        printf(" ++ Group status [%zi]:", mc.grpdata_size);
        if (!mc.grpdata)
        {
            printf(" (ERROR: array too small!)\n");
        }
        else
        {
            for (i = 0; i < mc.grpdata_size; ++i)
            {
                printf( "[%zd] result=%d state=%s ", i, mc.grpdata[i].result,
                        mc.grpdata[i].sockstate <= SRTS_CONNECTING ? "pending" :
                        mc.grpdata[i].sockstate == SRTS_CONNECTED ? "connected" : "broken");
            }
            printf("\n");
        }

        usleep(1000);   // 1 ms
    }

end:
    if (eid != -1)
    {
        srt_epoll_release(eid);
    }
    printf("srt close\n");
    st = srt_close(ss);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
        return 1;
    }

    free(grpdata);
    free(grpconfig);

//cleanup:
    printf("srt cleanup\n");
    srt_cleanup();
    return 0;
}
