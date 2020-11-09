
#include <stdio.h>
#include <stdlib.h>
#include <future>
#include <thread>
#include <chrono>
#include <vector>
#ifdef _WIN32
#define usleep(x) Sleep(x / 1000)
#else
#include <unistd.h>
#endif

#if ENABLE_EXPERIMENTAL_BONDING

#include "gtest/gtest.h"

#include "srt.h"

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
    const int st = srt_connect_group(ss, targets.data(), targets.size());
    std::cout << "srt_connect_group returned " << st << std::endl;

    closing_promise.wait();
    // Delete config objects before prospective exception
    for (auto& gd: targets)
        srt_delete_config(gd.config);

    int res = srt_close(ss);
    if (res == SRT_ERROR)
    {
        std::cerr << "srt_close: " << srt_getlasterror_str() << std::endl;
    }

    srt_cleanup();
}


void listening_thread()
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
    ASSERT_NE(srt_listen(server_sock, 5), -1);
    
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

    std::future<void> listen_promise = std::async(std::launch::async, listening_thread);
    
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

#endif // ENABLE_EXPERIMENTAL_BONDING
