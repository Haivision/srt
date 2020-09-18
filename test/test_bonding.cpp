
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

#include "gtest/gtest.h"

#include "srt.h"
#include "netinet_any.h"

TEST(Bonding, ConnectBlind)
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
    EXPECT_EQ(st, -1);

    // Delete config objects before prospective exception
    for (auto& gd: targets)
        srt_delete_config(gd.config);

    srt_cleanup();
}

SRTSOCKET g_listen_socket = -1;

void listening_thread()
{


    //this_thread::sleep_for(seconds(7));
}

int g_nconnected = 0;
int g_nfailed = 0;

void ConnectCallback(void* , SRTSOCKET sock, int error, const sockaddr* /*peer*/, int token)
{
    std::cout << "Connect callback. Socket: " << sock
        << ", error: " << error
        << ", token: " << token << '\n';

    if (error == SRT_SUCCESS)
        ++g_nconnected;
    else
        ++g_nfailed;
}

TEST(Bonding, ConnectNonBlocking)
{
    using namespace std;
    using namespace std::chrono;

    srt_startup();

    g_listen_socket = srt_create_socket();
    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(4200);

    ASSERT_NE(srt_bind(g_listen_socket, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
    const int yes = 1;
    srt_setsockflag(g_listen_socket, SRTO_GROUPCONNECT, &yes, sizeof yes);
    ASSERT_NE(srt_listen(g_listen_socket, 5), -1);

    int lsn_eid = srt_epoll_create();
    int lsn_events = SRT_EPOLL_IN | SRT_EPOLL_ERR | SRT_EPOLL_UPDATE;
    srt_epoll_add_usock(lsn_eid, g_listen_socket, &lsn_events);

    // Caller part

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

    auto acthr = std::thread([&lsn_eid]() {
            SRT_EPOLL_EVENT ev[3];

            cout << "[A] Waiting for accept\n";

            // This can wait in infinity; worst case it will be killed in process.
            int uwait_res = srt_epoll_uwait(lsn_eid, ev, 3, -1);
            ASSERT_EQ(uwait_res, 1);
            ASSERT_EQ(ev[0].fd, g_listen_socket);
            ASSERT_EQ(ev[0].events, SRT_EPOLL_IN);

            sockaddr_any adr;
            int accept_id = srt_accept(g_listen_socket, adr.get(), &adr.len);

            // Expected: group reporting
            EXPECT_NE(accept_id & SRTGROUP_MASK, 0);

            cout << "[A] Waiting for update\n";
            // Now another waiting is required and expected the update event
            uwait_res = srt_epoll_uwait(lsn_eid, ev, 3, -1);
            ASSERT_EQ(uwait_res, 1);
            ASSERT_EQ(ev[0].fd, g_listen_socket);
            ASSERT_EQ(ev[0].events, SRT_EPOLL_UPDATE);

            cout << "[A] Waitig for close (up to 5s)\n";
            // Wait up to 5s for an error
            srt_epoll_uwait(lsn_eid, ev, 3, 5000);

            srt_close(accept_id);
            cout << "[A] thread finished\n";
    });

    cout << "Connecting two sockets\n";

    SRT_SOCKGROUPCONFIG cc[2];
    cc[0] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);
    cc[1] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);

    ASSERT_NE(srt_epoll_add_usock(poll_id, ss, &epoll_out), SRT_ERROR);

    int result = srt_connect_group(ss, cc, 2);
    ASSERT_EQ(result, 0);

    // Wait up to 2s
    SRT_EPOLL_EVENT ev[3];
    const int uwait_result = srt_epoll_uwait(poll_id, ev, 3, 2000);
    std::cout << "Returned from connecting two sockets " << std::endl;

    ASSERT_EQ(uwait_result, 1);  // Expect the group reported
    EXPECT_EQ(ev[0].fd, ss);

    // One second to make sure that both links are connected.
    this_thread::sleep_for(seconds(1));

    EXPECT_EQ(srt_close(ss), 0);
    acthr.join();

    srt_cleanup();
}


TEST(Bonding, BackupPriorityBegin)
{
    using namespace std;
    using namespace std::chrono;

    g_nconnected = 0;
    g_nfailed = 0;

    srt_startup();

    g_listen_socket = srt_create_socket();
    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(4200);

    ASSERT_NE(srt_bind(g_listen_socket, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
    const int yes = 1;
    srt_setsockflag(g_listen_socket, SRTO_GROUPCONNECT, &yes, sizeof yes);
    ASSERT_NE(srt_listen(g_listen_socket, 5), -1);

    int lsn_eid = srt_epoll_create();
    int lsn_events = SRT_EPOLL_IN | SRT_EPOLL_ERR | SRT_EPOLL_UPDATE;
    srt_epoll_add_usock(lsn_eid, g_listen_socket, &lsn_events);

    // Caller part

    const int ss = srt_create_group(SRT_GTYPE_BACKUP);
    ASSERT_NE(ss, SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    auto acthr = std::thread([&lsn_eid]() {
            sockaddr_any adr;
            cout << "[A] Accepting a connection...\n";
            int accept_id = srt_accept(g_listen_socket, adr.get(), &adr.len);

            // Expected: group reporting
            EXPECT_NE(accept_id & SRTGROUP_MASK, 0);

            SRT_SOCKGROUPDATA gdata[2];
            SRT_MSGCTRL mc = srt_msgctrl_default;
            mc.grpdata = gdata;
            mc.grpdata_size = 2;
            long long data[1320/8];

            cout << "[A] Receiving...\n";
            int ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            ASSERT_EQ(ds, 8);

            cout << "[A] Closing\n";
            srt_close(accept_id);
            cout << "[A] thread finished\n";
    });

    cout << "Connecting two sockets\n";

    SRT_SOCKGROUPCONFIG cc[2];
    cc[0] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);
    cc[0].token = 0;
    cc[1] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);
    cc[1].token = 1;
    cc[1].weight = 1; // higher than the default 0

    int result = srt_connect_group(ss, cc, 2);
    ASSERT_EQ(result, 0);

    // Make sure both links are connected
    SRT_SOCKGROUPDATA gdata[2];
    size_t psize = 2;
    size_t nwait = 10;
    cout << "Waiting for getting 2 links:\n";
    while (--nwait)
    {
        srt_group_data(ss, gdata, &psize);
        if (psize == 2)
        {
            int l1, l2;
            l1 = gdata[0].memberstate;
            l2 = gdata[1].memberstate;

            if (l1 > SRT_GST_PENDING && l2 > SRT_GST_PENDING)
            {
                cout << "Both up: [0]=" << l1 << " [1]=" << l2 << "\n";
                break;
            }
            else
            {
                cout << "Still link states [0]=" << l1 << " [1]=" << l2 << "\n";
            }
        }
        else
        {
            cout << "Still " << psize << endl;
        }
        this_thread::sleep_for(milliseconds(500));
    }
    ASSERT_NE(nwait, 0);

    // Now send one packet
    long long data = 0x1234123412341234;

    SRT_MSGCTRL mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 2;

    // This call should retrieve the group information
    // AFTER the transition has happened
    int sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, sizeof data);

    // So, let's check which link is in RUNNING state
    // TOKEN value is the index in cc array, and we should
    // also have the weight value there.

    SRT_SOCKGROUPDATA* mane, * backup;
    if (gdata[0].weight == 0)
    {
        backup = &gdata[0];
        mane = &gdata[1];
    }
    else
    {
        mane = &gdata[0];
        backup = &gdata[1];
    }

    cout << "MAIN:[" << mane->token << "] weight=" << mane->weight << endl;
    cout << "BACKUP:[" << backup->token << "] weight=" << backup->weight << endl;

    // Ok, now mane link should be active, backup idle
    EXPECT_EQ(mane->memberstate, SRT_GST_RUNNING);
    EXPECT_EQ(backup->memberstate, SRT_GST_IDLE);

    acthr.join();

    srt_cleanup();
}



