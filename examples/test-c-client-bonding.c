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

    printf("srt startup\n");
    srt_startup();

    size_t nmemb = argc - 2;
    if (nmemb % 2)
    {
        fprintf(stderr, "Usage error: after <type>, <host> <port> pairs are expected.\n");
        return 1;
    }

    nmemb /= 2;

    SRT_SOCKGROUPDATA* grpdata = calloc(nmemb, sizeof (SRT_SOCKGROUPDATA));

    printf("srt group\n");
    ss = srt_create_group(gtype);
    if (ss == SRT_ERROR)
    {
        fprintf(stderr, "srt_create_group: %s\n", srt_getlasterror_str());
        return 1;
    }

    const int B = 2;

    for (i = 0; i < nmemb; ++i)
    {
        printf("srt remote address #%zi\n", i);

        sa.sin_family = AF_INET;
        sa.sin_port = htons(atoi(argv[B + 2*i + 1]));
        if (inet_pton(AF_INET, argv[B + 2*i], &sa.sin_addr) != 1)
        {
            return 1;
        }

        grpdata[i] = srt_prepare_endpoint(NULL, (struct sockaddr*)&sa, sizeof sa);
    }

    printf("srt connect (group)\n");

    // Note: this function unblocks at the moment when at least one connection
    // from the array is established (no matter which one); the others will
    // continue in background.
    st = srt_connect_group(ss, grpdata, nmemb);
    if (st == SRT_ERROR)
    {
        fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
        return 1;
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

    for (i = 0; i < 100; i++)
    {
        printf("srt sendmsg2 #%zd >> %s\n",i,message);

        SRT_MSGCTRL mc = srt_msgctrl_default;
        mc.grpdata = grpdata;
        mc.grpdata_size = nmemb; // Set maximum known

        st = srt_sendmsg2(ss, message, sizeof message, &mc);
        if (st == SRT_ERROR)
        {
            fprintf(stderr, "srt_sendmsg: %s\n", srt_getlasterror_str());
            return 1;
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
                        mc.grpdata[i].status <= SRTS_CONNECTING ? "pending" :
                        mc.grpdata[i].status == SRTS_CONNECTED ? "connected" : "broken");
            }
            printf("\n");
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

    free(grpdata);

    printf("srt cleanup\n");
    srt_cleanup();
    return 0;
}
