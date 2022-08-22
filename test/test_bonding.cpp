
#include <stdio.h>
#include <stdlib.h>
#include <future>
#include <thread>
#include <chrono>
#include <vector>
#include <functional>
#ifdef _WIN32
#define usleep(x) Sleep(x / 1000)
#else
#include <unistd.h>
#endif

#if ENABLE_BONDING

#include "gtest/gtest.h"

#include "srt.h"
#include "netinet_any.h"

TEST(Bonding, SRTConnectGroup)
{
    struct sockaddr_in sa;

    srt_startup();

    const int ss = srt_create_group(SRT_GTYPE_BROADCAST);
    ASSERT_NE(ss, SRT_ERROR);

    std::vector<SRT_SOCKGROUPCONFIG> targets;
    for (int i = 0; i < 2; ++i)
    {
        sa.sin_family = AF_INET;
        sa.sin_port = htons(4200 + i);
        ASSERT_EQ(inet_pton(AF_INET, "192.168.1.237", &sa.sin_addr), 1);

        const SRT_SOCKGROUPCONFIG gd = srt_prepare_endpoint(NULL, (struct sockaddr*)&sa, sizeof sa);
        targets.push_back(gd);
    }

    std::future<void> closing_promise = std::async(std::launch::async, [](int ss) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cerr << "Closing group" << std::endl;
        srt_close(ss);
    }, ss);

    std::cout << "srt_connect_group calling " << std::endl;
    const int st = srt_connect_group(ss, targets.data(), (int) targets.size());
    std::cout << "srt_connect_group returned " << st << ", waiting for srt_close() to finish" << std::endl;

    closing_promise.wait();

    std::cout << "TEST: closing future has exit. Deleting all other resources\n";

    // Delete config objects before prospective exception
    for (auto& gd: targets)
        srt_delete_config(gd.config);

    int res = srt_close(ss);

    std::cout << "TEST: closing ss has exit. Cleaning up\n";
    if (res == SRT_ERROR)
    {
        std::cerr << "srt_close: " << srt_getlasterror_str() << std::endl;
    }

    srt_cleanup();
}


void listening_thread(bool should_read)
{
    const SRTSOCKET server_sock = srt_create_socket();
    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(4200);

    ASSERT_NE(srt_bind(server_sock, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
    const int yes = 1;
    srt_setsockflag(server_sock, SRTO_GROUPCONNECT, &yes, sizeof yes);

    const int no = 1;
    srt_setsockflag(server_sock, SRTO_RCVSYN, &no, sizeof no);

    const int eid = srt_epoll_create();
    const int listen_event = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    srt_epoll_add_usock(eid, server_sock, &listen_event);

    ASSERT_NE(srt_listen(server_sock, 5), -1);
    std::cout << "Listen: wait for acceptability\n";
    int fds[2];
    int fds_len = 2;
    int ers[2];
    int ers_len = 2;
    int wr = srt_epoll_wait(eid, fds, &fds_len, ers, &ers_len, 5000,
            0, 0, 0, 0);

    ASSERT_NE(wr, -1);
    std::cout << "Listen: reported " << fds_len << " acceptable and " << ers_len << " errors\n";
    ASSERT_GT(fds_len, 0);
    ASSERT_EQ(fds[0], server_sock);

    srt::sockaddr_any scl;
    int acp = srt_accept(server_sock, (scl.get()), (&scl.len));
    ASSERT_NE(acp & SRTGROUP_MASK, 0);

    if (should_read)
    {
        std::cout << "Listener will read packets...\n";
        // Read everything until closed
        int n = 0;
        for (;;)
        {
            char buf[1500];
            int rd = srt_recv(acp, buf, 1500);
            if (rd == -1)
            {
                std::cout << "Listener read " << n << " packets, stopping\n";
                break;
            }
            ++n;
        }
    }

    srt_close(acp);

    std::cout << "Listen: wait 7 seconds\n";
    std::this_thread::sleep_for(std::chrono::seconds(7));
    // srt_accept..
}

void ConnectCallback(void* /*opaq*/, SRTSOCKET sock, int error, const sockaddr* /*peer*/, int token)
{
    std::cout << "Connect callback. Socket: " << sock
        << ", error: " << error
        << ", token: " << token << '\n';
}

TEST(Bonding, NonBlockingGroupConnect)
{
    srt_startup();
    
    const int ss = srt_create_group(SRT_GTYPE_BROADCAST);
    ASSERT_NE(ss, SRT_ERROR);
    std::cout << "Created group socket: " << ss << '\n';

    int no = 0;
    ASSERT_NE(srt_setsockopt(ss, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode
    ASSERT_NE(srt_setsockopt(ss, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode

    const int poll_id = srt_epoll_create();
    // Will use this epoll to wait for srt_accept(...)
    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(poll_id, ss, &epoll_out), SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr_in safail = sa;
    safail.sin_port = htons(4201); // port where we have no listener

    std::future<void> listen_promise = std::async(std::launch::async, std::bind(&listening_thread, false));
    
    std::cout << "Connecting two sockets " << std::endl;
    {
        const int sockid = srt_connect(ss, (sockaddr*) &sa, sizeof sa);
        EXPECT_GT(sockid, 0) << "Socket " << 1;
        sa.sin_port = htons(4201); // Changing port so that second connect fails
        std::cout << "Socket created: " << sockid << '\n';
        ASSERT_NE(srt_epoll_add_usock(poll_id, sockid, &epoll_out), SRT_ERROR);
    }
    {
        const int sockid = srt_connect(ss, (sockaddr*) &safail, sizeof safail);
        EXPECT_GT(sockid, 0) << "Socket " << 2;
        safail.sin_port = htons(4201); // Changing port so that second connect fails
        std::cout << "Socket created: " << sockid << '\n';
        ASSERT_NE(srt_epoll_add_usock(poll_id, sockid, &epoll_out), SRT_ERROR);
    }
    std::cout << "Returned from connecting two sockets " << std::endl;

    const int default_len = 3;
    int rlen = default_len;
    SRTSOCKET read[default_len];

    int wlen = default_len;
    SRTSOCKET write[default_len];

    for (int j = 0; j < 2; ++j)
    {
        const int epoll_res = srt_epoll_wait(poll_id, read, &rlen,
            write, &wlen,
            5000, /* timeout */
            0, 0, 0, 0);
            
        std::cout << "Epoll result: " << epoll_res << '\n';
        std::cout << "Epoll rlen: " << rlen << ", wlen: " << wlen << '\n';
        for (int i = 0; i < rlen; ++i)
        {
            std::cout << "Epoll read[" << i << "]: " << read[i] << '\n';
        }
        for (int i = 0; i < wlen; ++i)
        {
            std::cout << "Epoll write[" << i << "]: " << write[i] << " (removed from epoll)\n";
            EXPECT_EQ(srt_epoll_remove_usock(poll_id, write[i]), 0);
        }
    }

    listen_promise.wait();

    EXPECT_EQ(srt_close(ss), 0) << "srt_close: %s\n" << srt_getlasterror_str();

    srt_cleanup();
}

void ConnectCallback_Close(void* /*opaq*/, SRTSOCKET sock, int error, const sockaddr* /*peer*/, int token)
{
    std::cout << "Connect callback. Socket: " << sock
        << ", error: " << error
        << ", token: " << token << '\n';

    if (error == SRT_SUCCESS)
        return;

    // XXX WILL CAUSE DEADLOCK!
    srt_close(sock);
}

TEST(Bonding, CloseGroupAndSocket)
{
    srt_startup();
    
    const int ss = srt_create_group(SRT_GTYPE_BROADCAST);
    ASSERT_NE(ss, SRT_ERROR);
    std::cout << "Created group socket: " << ss << '\n';

    int no = 0;
    ASSERT_NE(srt_setsockopt(ss, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode
    ASSERT_NE(srt_setsockopt(ss, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode

    const int poll_id = srt_epoll_create();
    // Will use this epoll to wait for srt_accept(...)
    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(poll_id, ss, &epoll_out), SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback_Close, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    std::future<void> listen_promise = std::async(std::launch::async, std::bind(listening_thread, true));
    
    std::cout << "Connecting two sockets " << std::endl;
    for (int i = 0; i < 2; ++i)
    {
        const int sockid = srt_connect(ss, (sockaddr*) &sa, sizeof sa);
        EXPECT_GT(sockid, 0) << "Socket " << i;
        sa.sin_port = htons(4201); // Changing port so that second connect fails
        std::cout << "Socket created: " << sockid << '\n';
        ASSERT_NE(srt_epoll_add_usock(poll_id, sockid, &epoll_out), SRT_ERROR);
    }
    std::cout << "Returned from connecting two sockets " << std::endl;

    for (int j = 0; j < 2; ++j)
    {
        const int default_len = 3;
        int rlen = default_len;
        SRTSOCKET read[default_len];

        int wlen = default_len;
        SRTSOCKET write[default_len];

        const int epoll_res = srt_epoll_wait(poll_id, read, &rlen,
            write, &wlen,
            5000, /* timeout */
            0, 0, 0, 0);

        std::cout << "Epoll result: " << epoll_res << '\n';
        std::cout << "Epoll rlen: " << rlen << ", wlen: " << wlen << '\n';
        if (epoll_res < 0)
            continue;

        for (int i = 0; i < rlen; ++i)
        {
            std::cout << "Epoll read[" << i << "]: " << read[i] << '\n';
        }
        for (int i = 0; i < wlen; ++i)
        {
            std::cout << "Epoll write[" << i << "]: " << write[i] << " (removed from epoll)\n";
            EXPECT_EQ(srt_epoll_remove_usock(poll_id, write[i]), 0);
        }
    }

    // Some basic checks for group stats
    SRT_TRACEBSTATS stats;
    EXPECT_EQ(srt_bstats(ss, &stats, true), SRT_SUCCESS);
    EXPECT_EQ(stats.pktSent, 0);
    EXPECT_EQ(stats.pktSentTotal, 0);
    EXPECT_EQ(stats.pktSentUnique, 0);
    EXPECT_EQ(stats.pktSentUniqueTotal, 0);
    EXPECT_EQ(stats.pktRecv, 0);
    EXPECT_EQ(stats.pktRecvTotal, 0);
    EXPECT_EQ(stats.pktRecvUnique, 0);
    EXPECT_EQ(stats.pktRecvUniqueTotal, 0);
    EXPECT_EQ(stats.pktRcvDrop, 0);
    EXPECT_EQ(stats.pktRcvDropTotal, 0);

    std::cout << "Starting thread for sending:\n";
    std::thread sender([ss] {
        char buf[1316];
        memset(buf, 1, sizeof(buf));
        int n = 0;
        for (int i = 0; i < 10000; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (srt_send(ss, buf, 1316) == -1)
            {
                std::cout << "[Sender] sending failure, exitting after sending " << n << " packets\n";
                break;
            }

            ++n;
        }
    });

    std::cout << "Will close sending in 300ms...\n";

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    EXPECT_EQ(srt_close(ss), 0) << "srt_close: %s\n" << srt_getlasterror_str();

    std::cout << "CLOSED GROUP. Now waiting for sender to exit...\n";
    sender.join();
    listen_promise.wait();

    srt_cleanup();
}

#endif // ENABLE_BONDING
