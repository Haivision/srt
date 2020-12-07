
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
#include "udt.h"
#include "common.h"
#include "netinet_any.h"

// General idea:
// This should try to connect to two nonexistent links,
// the connecting function (working in blocking mode)
// should exit with error, after the group has been closed
// in a separate thread.
//
// Steps:
// 1. Create group
// 2. Use a nonexistent endpoints 192.168.1.237:4200 and *:4201
// 3. Close the group in a thread
// 4. Wait for error
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

    int res = srt_close(ss);
    if (res == SRT_ERROR)
    {
        std::cerr << "srt_close: " << srt_getlasterror_str() << std::endl;
    }

    srt_cleanup();
}

SRTSOCKET g_listen_socket = -1;
int g_nconnected = 0;
int g_nfailed = 0;

// This ConnectCallback is mainly informative, but it also collects the
// number of succeeded and failed links.
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

// TEST IDEA:
// This uses srt_connect_group in non-blocking mode. The listener
// is also created to respond to the connection. Expected is to
// continue the connecting in background and report a success,
// and report the epoll IN on listener for the first connection,
// and UPDATE For the second one.
//
// TEST STEPS:
// 1. Create a listener socket and a group.
// 2. Set the group and the listener socket non-blocking mode
// 3. Start the accepting thread
//    - wait for IN event ready on the listener socket
//    - accept a connection
//    - wait for UPDATE event ready on the listener socket
//    - wait for any event up to 5s (possibly ERR)
//    - close the listener socket
// 4. Prepare two connections and start connecting
// 5. Wait for the OUT readiness event on the group
// 6. Close the group.
// 7. Join the thread
TEST(Bonding, ConnectNonBlocking)
{
    using namespace std;
    using namespace std::chrono;

    const string ADDR = "127.0.0.1";
    const int PORT = 4209;

    srt_startup();

    // NOTE: Add more group types, if implemented!
    vector<SRT_GROUP_TYPE> types { SRT_GTYPE_BROADCAST, SRT_GTYPE_BACKUP };

    for (const auto GTYPE: types)
    {
        g_listen_socket = srt_create_socket();
        sockaddr_in bind_sa;
        memset(&bind_sa, 0, sizeof bind_sa);
        bind_sa.sin_family = AF_INET;
        ASSERT_EQ(inet_pton(AF_INET, ADDR.c_str(), &bind_sa.sin_addr), 1);
        bind_sa.sin_port = htons(PORT);

        ASSERT_NE(srt_bind(g_listen_socket, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
        const int yes = 1;
        srt_setsockflag(g_listen_socket, SRTO_GROUPCONNECT, &yes, sizeof yes);
        ASSERT_NE(srt_listen(g_listen_socket, 5), -1);

        int lsn_eid = srt_epoll_create();
        int lsn_events = SRT_EPOLL_IN | SRT_EPOLL_ERR | SRT_EPOLL_UPDATE;
        srt_epoll_add_usock(lsn_eid, g_listen_socket, &lsn_events);

        // Caller part

        const int ss = srt_create_group(GTYPE);
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
        sa.sin_port = htons(PORT);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

        //srt_setloglevel(LOG_DEBUG);

        auto acthr = std::thread([&lsn_eid]() {
                SRT_EPOLL_EVENT ev[3];

                cout << "[A] Waiting for accept\n";

                // This can wait in infinity; worst case it will be killed in process.
                int uwait_res = srt_epoll_uwait(lsn_eid, ev, 3, -1);
                ASSERT_EQ(uwait_res, 1);
                ASSERT_EQ(ev[0].fd, g_listen_socket);

                // Check if the IN event is set, even if it's not the only event
                ASSERT_EQ(ev[0].events & SRT_EPOLL_IN, SRT_EPOLL_IN);
                bool have_also_update = ev[0].events & SRT_EPOLL_UPDATE;

                sockaddr_any adr;
                int accept_id = srt_accept(g_listen_socket, adr.get(), &adr.len);

                // Expected: group reporting
                EXPECT_NE(accept_id & SRTGROUP_MASK, 0);

                if (have_also_update)
                {
                    cout << "[A] NOT waiting for update - already reported previously\n";
                }
                else
                {
                    cout << "[A] Waiting for update\n";
                    // Now another waiting is required and expected the update event
                    uwait_res = srt_epoll_uwait(lsn_eid, ev, 3, -1);
                    ASSERT_EQ(uwait_res, 1);
                    ASSERT_EQ(ev[0].fd, g_listen_socket);
                    ASSERT_EQ(ev[0].events, SRT_EPOLL_UPDATE);
                }

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
        char data[4] = { 1, 2, 3, 4};
        int wrong_send = srt_send(ss, data, sizeof data);
        int errorcode = srt_getlasterror(NULL);
        EXPECT_EQ(wrong_send, -1);
        EXPECT_EQ(errorcode, SRT_EASYNCSND);

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

        srt_epoll_release(lsn_eid);
        srt_epoll_release(poll_id);

        srt_close(g_listen_socket);
    }

    srt_cleanup();
}

// TEST IDEA:
// In this test there is created a working listener socket to
// accept the connection, and we use a Backup-type group with
// two links, but different weights. We connect them both and
// make sure that both are ready for use. Then we send a packet
// over the group and see, which link got activated and which
// remained idle. Expected is to have the link with higher
// priority (greater weight) to be activated.
//
// TEST STEPS:
// 1. Create a listener socket and a group.
// 3. Start the accepting thread
//    - accept a connection
//    - read one packet from the accepted entity
//    - close the listener socket
// 4. Prepare two connections (one with weight=1) and connect the group
// 5. Wait for having all links connected
// 6. Send one packet and check which link was activated
// 6. Close the group.
// 7. Join the thread
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

    // Caller part

    const int ss = srt_create_group(SRT_GTYPE_BACKUP);
    ASSERT_NE(ss, SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    auto acthr = std::thread([]() {
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


// TEST IDEA:
// In this test there is created a working listener socket to
// accept the connection, and we use a Backup-type group with
// two links, but different weights. We connect the first link
// with less weight and send one packet to make sure this only
// link was activated. Then we connect a second link with weight=1.
// Then we send the packet again and see if the new link was
// immediately activated. The first link should be silenced after
// time, but there's no possibility to check this in such a
// simple test.
//
// TEST STEPS:
// 1. Create a listener socket and a group.
// 3. Start the accepting thread
//    - accept a connection
//    - read one packet from the accepted entity
//    - read the second packet from the accepted entity
//    - close the listener socket
// 4. Prepare one connection with weight=0 and connect the group
// 5. Send a packet to enforce activation of one link
// 6. Prepare another connection with weight=1 and connect the group
// 7. Send a packet
// 8. Check member status - both links should be running.
// 9. Close the group.
// 10. Join the thread
TEST(Bonding, BackupPriorityTakeover)
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

    // Caller part

    const int ss = srt_create_group(SRT_GTYPE_BACKUP);
    ASSERT_NE(ss, SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    auto acthr = std::thread([]() {
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

            cout << "[A] Receiving 1...\n";
            int ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            ASSERT_EQ(ds, 8);

            cout << "[A] Receiving 2...\n";
            ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            ASSERT_EQ(ds, 8);

            // To make it possible that the state is checked before it is closed.
            this_thread::sleep_for(seconds(1));

            cout << "[A] Closing\n";
            srt_close(accept_id);
            cout << "[A] thread finished\n";
    });

    cout << "Connecting first link weight=0:\n";

    SRT_SOCKGROUPCONFIG cc[2];
    cc[0] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);
    cc[0].token = 0;

    int result = srt_connect_group(ss, cc, 1);
    ASSERT_EQ(result, 0);

    // As we have one link, after `srt_connect_group` returns, we have
    // this link now connected. Send one data portion.

    SRT_SOCKGROUPDATA gdata[2];

    long long data = 0x1234123412341234;
    SRT_MSGCTRL mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 2;

    cout << "Sending (1)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    int sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, sizeof data);
    ASSERT_EQ(mc.grpdata_size, 1);
    EXPECT_EQ(gdata[0].memberstate, SRT_GST_RUNNING);

    cout << "Connecting second link weight=1:\n";
    // Now prepare the second connection
    cc[0].token = 1;
    cc[0].weight = 1; // higher than the default 0
    result = srt_connect_group(ss, cc, 1);
    ASSERT_EQ(result, 0);

    // Make sure both links are connected
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

    // Now send one packet (again)
    mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 2;

    cout << "Sending (2)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
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

    // Ok, now both links should be running (this state lasts
    // for the "temporary activation" period.
    EXPECT_EQ(mane->memberstate, SRT_GST_RUNNING);
    EXPECT_EQ(backup->memberstate, SRT_GST_RUNNING);

    acthr.join();

    srt_cleanup();
}


// TEST IDEA:
// In this test there is created a working listener socket to
// accept the connection, and we use a Backup-type group with
// two links, but different weights. We connect then two links
// both with weight=1. Then we send a packet to make sure that
// exactly one of them got activated. Then we connect another
// link with weight=0. Then we send a packet again, which should
// not change the link usage. Then we check which link was
// active so far, and we close the socket for that link to make
// it broken, then we wait for having only two links connected.
// Then a packet is sent to activate a link. We expect the link
// with higher weight is activated.
//
// TEST STEPS:
// 1. Create a listener socket.
// 2. Create and setup a group.
// 3. Start the accepting thread
//    A1. accept a connection
//    A2. read one packet from the accepted entity
//    A3. read the second packet from the accepted entity
//    A4. read the third packet from the accepted entity
//    A5. close the listener socket
// 4. Prepare two connections with weight=1 and connect the group
// 5. Send a packet to enforce activation of one link
// 6. Prepare another connection with weight=0 and connect the group
// 7. Wait for having all 3 links connected.
// 8. Send a packet
// 9. Find which link is currently active and close it
// 10. Wait for having only two links.
// 11. Send a packet.
// 12. Find one link active and one idle
// 13. Check if the link with weight=1 is active and the one with weight=0 is idle.
// 14. Close the group.
// 15. Join the thread
TEST(Bonding, BackupPrioritySelection)
{
    using namespace std;
    using namespace std::chrono;

    g_nconnected = 0;
    g_nfailed = 0;
    volatile bool recvd = false;

    srt_startup();

    // 1.
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

    // Caller part
    // 2.
    const int ss = srt_create_group(SRT_GTYPE_BACKUP);
    ASSERT_NE(ss, SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    // Set the group's stability timeout to 1s, otherwise it will
    // declare the links unstable by not receiving ACKs.
    int stabtimeo = 1000;
    srt_setsockflag(ss, SRTO_GROUPSTABTIMEO, &stabtimeo, sizeof stabtimeo);

    //srt_setloglevel(LOG_DEBUG);
    srt::resetlogfa( std::set<srt_logging::LogFA> {
            SRT_LOGFA_GRP_SEND,
            SRT_LOGFA_GRP_MGMT,
            SRT_LOGFA_CONN
            });

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    // 3.
    auto acthr = std::thread([&recvd]() {
            sockaddr_any adr;
            cout << "[A1] Accepting a connection...\n";

            // A1
            int accept_id = srt_accept(g_listen_socket, adr.get(), &adr.len);

            // Expected: group reporting
            EXPECT_NE(accept_id & SRTGROUP_MASK, 0);

            SRT_SOCKGROUPDATA gdata[2];
            SRT_MSGCTRL mc = srt_msgctrl_default;
            mc.grpdata = gdata;
            mc.grpdata_size = 2;
            long long data[1320/8];

            // A2
            cout << "[A2] Receiving 1...\n";
            int ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            if (ds == -1) { cout << "[A2] ERROR: " << srt_getlasterror(NULL) << " " << srt_getlasterror_str() << endl; }
            ASSERT_EQ(ds, 8);

            // A3
            cout << "[A3] Receiving 2...\n";
            ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            if (ds == -1) { cout << "[A3] ERROR: " << srt_getlasterror(NULL) << " " << srt_getlasterror_str() << endl; }
            ASSERT_EQ(ds, 8);
            recvd = true;

            // A4
            cout << "[A4] Receiving 3...\n";
            ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            if (ds == -1) { cout << "[A4] ERROR: " << srt_getlasterror(NULL) << " " << srt_getlasterror_str() << endl; }
            ASSERT_EQ(ds, 8);

            cout << "[A] Waiting 5s...\n";
            // To make it possible that the state is checked before it is closed.
            this_thread::sleep_for(seconds(5));

            // A5
            cout << "[A5] Closing\n";
            srt_close(accept_id);
            cout << "[A] thread finished\n";
    });


    cout << "(4) Connecting first 2 links weight=1:\n";

    SRT_SOCKGROUPCONFIG cc[2];
    cc[0] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);
    cc[0].token = 0;
    cc[0].weight = 1;
    cc[1] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);
    cc[1].token = 1;
    cc[1].weight = 1;

    // 4.
    int result = srt_connect_group(ss, cc, 2);
    ASSERT_EQ(result, 0);

    // As we have one link, after `srt_connect_group` returns, we have
    // this link now connected. Send one data portion.

    SRT_SOCKGROUPDATA gdata[3];
    memset(gdata, 0, sizeof(gdata));

    long long data = 0x1234123412341234;
    SRT_MSGCTRL mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 3;

    // We can send now. We know that we have at least one
    // link connected and it already has the same priority
    // as the other.

    //srt_setloglevel(LOG_DEBUG);
    // 5.
    cout << "(5) Sending (1)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    int sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    // In case when this was an error, display the code
    if (sendret == -1) { cout << "[A4] ERROR: " << srt_getlasterror(NULL) << " " << srt_getlasterror_str() << endl; }

    EXPECT_EQ(sendret, sizeof data);


    ASSERT_EQ(mc.grpdata_size, 2);

    int state0 = gdata[0].memberstate;
    int state1 = gdata[1].memberstate;

    cout << "States: [0]=" << state0 << " [1]=" << state1 << endl;
    EXPECT_TRUE(state0 == SRT_GST_RUNNING || state1 == SRT_GST_RUNNING);

    // 6.
    cout << "(6) Connecting third link weight=0:\n";
    // Now prepare the third connection
    cc[0] = srt_prepare_endpoint(NULL, (sockaddr*)&sa, sizeof sa);
    cc[0].token = 2;
    cc[0].weight = 0; // higher than the default 0
    result = srt_connect_group(ss, cc, 1);
    ASSERT_EQ(result, 0);

    // Make sure all 3 links are connected
    size_t psize = 3;
    size_t nwait = 10;
    set<SRT_MEMBERSTATUS> states;

    // 7.
    cout << "(7) Waiting for getting 3 links:\n";
    while (--nwait)
    {
        srt_group_data(ss, gdata, &psize);
        if (psize == 3)
        {
            states.clear();
            for (int i = 0; i < 3; ++i)
                states.insert(gdata[i].memberstate);

            if (states.count(SRT_GST_PENDING))
            {
                cout << "Still not all links...\n";
            }
            else
            {
                cout << "All links up\n";
                break;
            }
        }
        else
        {
            cout << "Still " << psize << endl;
        }
        this_thread::sleep_for(milliseconds(500));
    }
    ASSERT_NE(nwait, 0);

    // Now send one packet (again)
    mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 3;

    // 8.
    cout << "(8) Sending (2)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, sizeof data);
    ASSERT_EQ(mc.grpdata_size, 3);

    // So, let's check which link is in RUNNING state
    // TOKEN value is the index in cc array, and we should
    // also have the weight value there.

    SRT_SOCKGROUPDATA* mane = nullptr;

    for (size_t i = 0; i < mc.grpdata_size; ++i)
    {
        if (gdata[i].memberstate == SRT_GST_RUNNING)
        {
            mane = &gdata[i];
            break;
        }
    }

    ASSERT_NE(mane, nullptr);
    ASSERT_EQ(mane->weight, 1);

    // Spin-wait for making sure the reception succeeded before
    // closing. This shouldn't be a problem in general, but
    int ntry = 100;
    while (!recvd && --ntry)
        this_thread::sleep_for(milliseconds(200));
    ASSERT_NE(ntry, 0);

    cout << "(9) Found activated link: [" << mane->token << "] - closing after 0.5s...\n";

    // Waiting is to make sure that the listener thread has received packet 3.
    this_thread::sleep_for(milliseconds(500));
    ASSERT_NE(srt_close(mane->id), -1);

    // Now expect to have only 2 links, wait for it if needed.
    psize = 2;
    nwait = 10;

    cout << "(10) Waiting for ONLY 2 links:\n";
    while (--nwait)
    {
        srt_group_data(ss, gdata, &psize);
        if (psize == 2)
        {
            break;
        }
        else
        {
            cout << "Still " << psize << endl;
        }
        this_thread::sleep_for(milliseconds(500));
    }
    ASSERT_NE(nwait, 0);

    // Now send one packet (again)
    mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 2;

    cout << "(11) Sending (3)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, sizeof data);

    cout << "(sleep)\n";
    this_thread::sleep_for(seconds(1));

    mane = nullptr;
    SRT_SOCKGROUPDATA* backup = nullptr;
    cout << "(12) Checking main/backup:";
    int repeat_check = 1; // 50;
CheckLinksAgain:
    for (size_t i = 0; i < mc.grpdata_size; ++i)
    {
        cout << "[" << i << "]" << srt_logging::MemberStatusStr(gdata[i].memberstate)
            << " weight=" << gdata[i].weight;
        if (gdata[i].memberstate == SRT_GST_RUNNING)
        {
            cout << " (main) ";
            mane = &gdata[i];
        }
        else
        {
            cout << " (backup) ";
            backup = &gdata[i];
        }
    }
    if (backup == nullptr)
    {
        if (--repeat_check)
        {
            cout << "BACKUP STILL RUNNING. AGAIN\n";
            this_thread::sleep_for(milliseconds(250));
            goto CheckLinksAgain;
        }
    }
    cout << endl;

    ASSERT_NE(mane, nullptr);
    ASSERT_NE(backup, nullptr);
    ASSERT_EQ(mane->weight, 1);
    ASSERT_EQ(backup->weight, 0);

    cout << "MAIN (expected active):[" << mane->token << "] weight=" << mane->weight << endl;
    cout << "BACKUP (expected idle):[" << backup->token << "] weight=" << backup->weight << endl;

    // Ok, now both links should be running (this state lasts
    // for the "temporary activation" period.
    EXPECT_EQ(mane->memberstate, SRT_GST_RUNNING);
    EXPECT_EQ(backup->memberstate, SRT_GST_IDLE);

    this_thread::sleep_for(seconds(1));

    cout << "Closing receiver thread [A]\n";

    acthr.join();

    srt_close(ss);

    srt_cleanup();
}


