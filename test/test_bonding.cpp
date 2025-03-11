#include <array>
#include <future>
#include <thread>
#include <chrono>
#include <vector>
#include "gtest/gtest.h"
#include "test_env.h"

#include "srt.h"
#include "udt.h"
#include "common.h"
#include "netinet_any.h"
#include "socketconfig.h"

#include "apputil.hpp"

TEST(Bonding, SRTConnectGroup)
{
    srt::TestInit srtinit;
    struct sockaddr_in sa;

    const int ss = srt_create_group(SRT_GTYPE_BROADCAST);
    ASSERT_NE(ss, SRT_ERROR);

    std::vector<SRT_SOCKGROUPCONFIG> targets;
    for (int i = 0; i < 2; ++i)
    {
        sa.sin_family = AF_INET;
        sa.sin_port = htons(4200 + i);
        EXPECT_EQ(inet_pton(AF_INET, "192.168.1.237", &sa.sin_addr), 1);

        const SRT_SOCKGROUPCONFIG gd = srt_prepare_endpoint(NULL, (struct sockaddr*)&sa, sizeof sa);
        targets.push_back(gd);
    }

    std::future<void> closing_promise = std::async(std::launch::async, [](int s) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cerr << "Closing group" << std::endl;
        srt_close(s);
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
}

#define EXPECT_SRT_SUCCESS(callform) EXPECT_NE(callform, -1) << "SRT ERROR: " << srt_getlasterror_str()

void listening_thread(bool should_read)
{
    const SRTSOCKET server_sock = srt_create_socket();
    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(4200);

    EXPECT_SRT_SUCCESS(srt_bind(server_sock, (sockaddr*)&bind_sa, sizeof bind_sa));
    const int yes = 1;
    EXPECT_SRT_SUCCESS(srt_setsockflag(server_sock, SRTO_GROUPCONNECT, &yes, sizeof yes));

    const int no = 1;
    EXPECT_SRT_SUCCESS(srt_setsockflag(server_sock, SRTO_RCVSYN, &no, sizeof no));

    const int eid = srt_epoll_create();
    const int listen_event = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    EXPECT_SRT_SUCCESS(srt_epoll_add_usock(eid, server_sock, &listen_event));

    EXPECT_SRT_SUCCESS(srt_listen(server_sock, 5));
    std::cout << "Listen: wait for acceptability\n";
    int fds[2];
    int fds_len = 2;
    int ers[2];
    int ers_len = 2;
    EXPECT_SRT_SUCCESS(srt_epoll_wait(eid, fds, &fds_len, ers, &ers_len, 5000,
            0, 0, 0, 0));

    std::cout << "Listen: reported " << fds_len << " acceptable and " << ers_len << " errors\n";
    EXPECT_GT(fds_len, 0);
    EXPECT_EQ(fds[0], server_sock);

    srt::sockaddr_any scl;
    int acp = srt_accept(server_sock, (scl.get()), (&scl.len));
    EXPECT_SRT_SUCCESS(acp);
    EXPECT_NE(acp & SRTGROUP_MASK, 0);

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
    srt_close(server_sock);

    std::cout << "Listen: wait 7 seconds\n";
    std::this_thread::sleep_for(std::chrono::seconds(7));
    // srt_accept..
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

TEST(Bonding, NonBlockingGroupConnect)
{
    srt::TestInit srtinit;

    const int ss = srt_create_group(SRT_GTYPE_BROADCAST);
    ASSERT_NE(ss, SRT_ERROR);
    std::cout << "Created group socket: " << ss << '\n';

    int no = 0;
    EXPECT_NE(srt_setsockopt(ss, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode
    EXPECT_NE(srt_setsockopt(ss, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode

    const int poll_id = srt_epoll_create();
    // Will use this epoll to wait for srt_accept(...)
    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    EXPECT_NE(srt_epoll_add_usock(poll_id, ss, &epoll_out), SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr_in safail = sa;
    safail.sin_port = htons(4201); // port where we have no listener

    std::future<void> listen_promise = std::async(std::launch::async, std::bind(&listening_thread, false));
    
    std::cout << "Connecting two sockets " << std::endl;
    {
        const int sockid = srt_connect(ss, (sockaddr*) &sa, sizeof sa);
        EXPECT_GT(sockid, 0) << "Socket " << 1;
        sa.sin_port = htons(4201); // Changing port so that second connect fails
        std::cout << "Socket created: " << sockid << '\n';
        EXPECT_NE(srt_epoll_add_usock(poll_id, sockid, &epoll_out), SRT_ERROR);
    }
    {
        const int sockid = srt_connect(ss, (sockaddr*) &safail, sizeof safail);
        EXPECT_GT(sockid, 0) << "Socket " << 2;
        safail.sin_port = htons(4201); // Changing port so that second connect fails
        std::cout << "Socket created: " << sockid << '\n';
        EXPECT_NE(srt_epoll_add_usock(poll_id, sockid, &epoll_out), SRT_ERROR);
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
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(ss, "broadcast group", srt_create_group(SRT_GTYPE_BROADCAST));

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

    ss.close();

    std::cout << "CLOSED GROUP. Now waiting for sender to exit...\n";
    sender.join();
    listen_promise.wait();
}

// (void* opaq, SRTSOCKET ns, int hsversion, const struct sockaddr* peeraddr, const char* streamid);
int ListenCallbackFn(void* expected_sid, SRTSOCKET, int /*hsversion*/, const sockaddr* /*peer*/, const char* streamid)
{
    const auto* p = (std::pair<const char*, int>*) expected_sid;
    // Note: It is not safe to access the streamid pointer by the expected size,
    // but there is no way to know the real size apart from finding the first null terminator.
    // See FR #3073.
    EXPECT_EQ(std::memcmp(streamid, p->first, p->second), 0);
    
    return 0;
}

TEST(Bonding, Options)
{
    using namespace std;
    using namespace srt;

    TestInit srtinit;

    // Create a group
    MAKE_UNIQUE_SOCK(grp, "broadcast group", srt_create_group(SRT_GTYPE_BROADCAST));

    // rendezvous shall not be allowed to be set on the group
    // XXX actually it is possible, but no one tested it. POSTPONE.
    //int yes = 1;
    //EXPECT_EQ(srt_setsockflag(grp, SRTO_RENDEZVOUS, &yes, sizeof yes), SRT_ERROR);

#ifdef SRT_ENABLE_ENCRYPTION
    const string pass = "longenoughpassword";
    // passphrase should be ok.
    EXPECT_NE(srt_setsockflag(grp, SRTO_PASSPHRASE, pass.c_str(), (int) pass.size()), SRT_ERROR);

    uint32_t val = 16;
    EXPECT_NE(srt_setsockflag(grp, SRTO_PBKEYLEN, &val, (int) sizeof val), SRT_ERROR);

    const bool bfalse = true;
    EXPECT_EQ(srt_setsockflag(grp, SRTO_RENDEZVOUS, &bfalse, (int)sizeof bfalse), SRT_ERROR);

#ifdef ENABLE_AEAD_API_PREVIEW
    val = 1;
    EXPECT_NE(srt_setsockflag(grp, SRTO_CRYPTOMODE, &val, sizeof val), SRT_ERROR);
#endif
#endif

    const string packet_filter = "fec,cols:10,rows:5";
    EXPECT_NE(srt_setsockflag(grp, SRTO_PACKETFILTER, packet_filter.c_str(), (int)packet_filter.size()), SRT_ERROR);

    // ================
    // Linger is an option of a trivial type, but differes from other integer-typed options.
    // Therefore checking it specifically.
    const linger l = {1, 10};
    srt_setsockflag(grp, SRTO_LINGER, &l, sizeof l);

    {
        linger l2;
        int optsize = sizeof l2;
        EXPECT_NE(srt_getsockflag(grp, SRTO_LINGER, &l2, &optsize), SRT_ERROR);
        EXPECT_EQ(optsize, (int)sizeof l2);
        EXPECT_EQ(l2.l_onoff, l.l_onoff);
        EXPECT_EQ(l2.l_linger, l.l_linger);
    }
    // ================

    const std::array<char, 10> streamid = { 's', 't', 'r', 'e', 0, 'm', 'i', 'd', '%', '&'};
    EXPECT_NE(srt_setsockflag(grp, SRTO_STREAMID, &streamid, streamid.size()), SRT_ERROR);

    auto check_streamid = [&streamid](SRTSOCKET sock) {
        std::array<char, srt::CSrtConfig::MAX_SID_LENGTH> tmpbuf;
        auto opt_len = (int)tmpbuf.size();
        EXPECT_EQ(srt_getsockflag(sock, SRTO_STREAMID, tmpbuf.data(), &opt_len), SRT_SUCCESS);
        EXPECT_EQ(size_t(opt_len), streamid.size());
        EXPECT_EQ(std::memcmp(tmpbuf.data(), streamid.data(), opt_len), 0);
    };

    check_streamid(grp);

    int lat = 500;
    EXPECT_NE(srt_setsockflag(grp, SRTO_RCVLATENCY, &lat, sizeof lat), SRT_ERROR);

    mutex mx;
    condition_variable latch;
    atomic<bool> started {false};

    thread accept_and_close { [&]() {

        unique_lock<mutex> ux(mx);

        SRTSOCKET lsn = srt_create_socket();

        auto expected_sid = std::make_pair<const char*, int>(streamid.data(), streamid.size());
        srt_listen_callback(lsn, &ListenCallbackFn, (void*) &expected_sid);

#ifdef SRT_ENABLE_ENCRYPTION
        EXPECT_NE(srt_setsockflag(lsn, SRTO_PASSPHRASE, pass.c_str(), pass.size()), SRT_ERROR);
#endif
        int allow = 1;
        EXPECT_NE(srt_setsockflag(lsn, SRTO_GROUPCONNECT, &allow, sizeof allow), SRT_ERROR);
        sockaddr_any sa = srt::CreateAddr("127.0.0.1", 5555, AF_INET);
        EXPECT_NE(srt_bind(lsn, sa.get(), sa.size()), SRT_ERROR);
        EXPECT_NE(srt_listen(lsn, 1), SRT_ERROR);
        started = true;

        // First wait - until it's let go with accepting
        latch.wait(ux);

        //sockaddr_any revsa;
        SRTSOCKET lsna [1] = { lsn };
        SRTSOCKET gs = srt_accept_bond(lsna, 1, 1000);
        ASSERT_NE(gs, SRT_INVALID_SOCK);

        check_streamid(gs);

        std::array<char, 800> tmpbuf;
        auto opt_len = (int)tmpbuf.size();
        EXPECT_EQ(srt_getsockflag(gs, SRTO_PACKETFILTER, tmpbuf.data(), &opt_len), SRT_SUCCESS);
        std::cout << "Packet filter: " << std::string(tmpbuf.data(), opt_len) << '\n';

        // Connected, wait to close
        latch.wait(ux);

        srt_close(gs);
        srt_close(lsn);
    }};

    // Give the thread a chance to start
    this_thread::yield();

    while (!started)
    {
        // In case of a bad luck, just wait for the thread to
        // acquire the mutex before you do
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    // Wait for the possibility to connect
    {
        // Make sure that the thread reached the wait() call.
        unique_lock<mutex> ux(mx);
        latch.notify_all();
    }

    // Now the thread is accepting, so we call the connect.
    sockaddr_any sa = srt::CreateAddr("127.0.0.1", 5555, AF_INET);
    SRTSOCKET member = srt_connect(grp, sa.get(), sa.size());

    // We've released the mutex and signaled the CV, so accept should proceed now.
    // Exit from srt_connect() means also exit from srt_accept().

    EXPECT_NE(member, SRT_INVALID_SOCK);

    // conenct_res should be a socket
    EXPECT_NE(member, 0); // XXX Change to SRT_SOCKID_CONNREQ

    // Now get the option value from the group

    int revlat = -1;
    int optsize = sizeof revlat;
    EXPECT_NE(srt_getsockflag(grp, SRTO_RCVLATENCY, &revlat, &optsize), SRT_ERROR);
    EXPECT_EQ(optsize, (int) sizeof revlat);
    EXPECT_EQ(revlat, 500);

    revlat = -1;
    optsize = sizeof revlat;
    // Expect the same value set on the member socket
    EXPECT_NE(srt_getsockflag(member, SRTO_RCVLATENCY, &revlat, &optsize), SRT_ERROR);
    EXPECT_EQ(optsize, (int) sizeof revlat);
    EXPECT_EQ(revlat, 500);

    // Individual socket option modified on group
    int ohead = 12;
    optsize = sizeof ohead;
    EXPECT_NE(srt_setsockflag(grp, SRTO_OHEADBW, &ohead, optsize), SRT_ERROR);

    // Modifyting a post-option should be possible on a socket
    ohead = 11;
    optsize = sizeof ohead;
    EXPECT_NE(srt_setsockflag(member, SRTO_OHEADBW, &ohead, optsize), SRT_ERROR);

    // But getting the option value should be equal to the group setting
    EXPECT_NE(srt_getsockflag(grp, SRTO_OHEADBW, &ohead, &optsize), SRT_ERROR);
    EXPECT_EQ(optsize, (int) sizeof ohead);
    EXPECT_EQ(ohead, 12);

#if SRT_ENABLE_ENCRYPTION

    uint32_t kms = -1;

    EXPECT_NE(srt_getsockflag(grp, SRTO_KMSTATE, &kms, &optsize), SRT_ERROR);
    EXPECT_EQ(optsize, (int) sizeof kms);
    EXPECT_EQ(kms, int(SRT_KM_S_SECURED));

    EXPECT_NE(srt_getsockflag(grp, SRTO_PBKEYLEN, &kms, &optsize), SRT_ERROR);
    EXPECT_EQ(optsize, (int) sizeof kms);
    EXPECT_EQ(kms, 16);

#ifdef ENABLE_AEAD_API_PREVIEW
    EXPECT_NE(srt_getsockflag(grp, SRTO_CRYPTOMODE, &kms, &optsize), SRT_ERROR);
    EXPECT_EQ(optsize, sizeof kms);
    EXPECT_EQ(kms, 1);
#endif
#endif

    // We're done, the thread can close connection and exit
    {
        // Make sure that the thread reached the wait() call.
        std::unique_lock<std::mutex> ux(mx);
        latch.notify_all();
    }

    accept_and_close.join();
}

inline SRT_SOCKGROUPCONFIG PrepareEndpoint(const std::string& host, int port)
{
    srt::sockaddr_any sa = srt::CreateAddr(host, port, AF_INET);
    return srt_prepare_endpoint(NULL, sa.get(), sa.size());
}

// This test will create a listener and then the group that should
// connect members, where the first one fail, and two next should
// succeed. Then sends a single packet over that link and makes sure
// it's properly received, then the second packet isn't read.
TEST(Bonding, InitialFailure)
{
    using namespace std;
    using namespace srt;

    TestInit srtinit;
    MAKE_UNIQUE_SOCK(lsn, "Listener", srt_create_socket());
    MAKE_UNIQUE_SOCK(grp, "GrpCaller", srt_create_group(SRT_GTYPE_BROADCAST));

    // Create the listener on port 5555.
    int allow = 1;
    ASSERT_NE(srt_setsockflag(lsn, SRTO_GROUPCONNECT, &allow, sizeof allow), SRT_ERROR);

    sockaddr_any sa = srt::CreateAddr("127.0.0.1", 5555, AF_INET);
    ASSERT_NE(srt_bind(lsn, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(lsn, 5), SRT_ERROR);

    // Create a group
    // Connect 3 members in the group.
    std::vector<SRT_SOCKGROUPCONFIG> targets;
    targets.push_back(PrepareEndpoint("127.0.0.1", 5556)); // NOTE: NONEXISTENT LISTENER
    targets.push_back(PrepareEndpoint("127.0.0.1", 5555));
    targets.push_back(PrepareEndpoint("127.0.0.1", 5555));

    // This should block until the connection is established, but
    // accepted socket should be spawned and just wait for extraction.
    const SRTSOCKET conn = srt_connect_group(grp, targets.data(), (int)targets.size());
    EXPECT_NE(conn, SRT_INVALID_SOCK);

    // Now check if the accept is ready
    sockaddr_any revsa;
    const SRTSOCKET gs = srt_accept(lsn, revsa.get(), &revsa.len);
    EXPECT_NE(gs, SRT_INVALID_SOCK);

    // Make sure that it was the group accepted
    EXPECT_EQ(gs & SRTGROUP_MASK, SRTGROUP_MASK);

    // Set 1s reading timeout on the socket so that reading won't wait forever,
    // as it should fail at the second reading.
    int read_timeout = 500; // 0.5s
    EXPECT_NE(srt_setsockflag(gs, SRTO_RCVTIMEO, &read_timeout, sizeof (read_timeout)), SRT_ERROR);

    int lsn_isn = -1, lsn_isn_size = sizeof (int);
    EXPECT_NE(srt_getsockflag(gs, SRTO_ISN, &lsn_isn, &lsn_isn_size), SRT_ERROR);

    // Now send a packet

    string packet_data = "PREDEFINED PACKET DATA";
    EXPECT_NE(srt_send(grp, packet_data.data(), packet_data.size()), SRT_ERROR);

    char outbuf[1316];
    SRT_MSGCTRL mc = srt_msgctrl_default;
    int recvlen = srt_recvmsg2(gs, outbuf, 1316, &mc);
    EXPECT_EQ(recvlen, int(packet_data.size()));

    if (recvlen > 0)
    {
        outbuf[recvlen] = 0;
        EXPECT_EQ(outbuf, packet_data);
    }
    EXPECT_EQ(mc.pktseq, lsn_isn);

    recvlen = srt_recv(gs, outbuf, 80);
    EXPECT_EQ(recvlen, int(SRT_ERROR));

    srt_close(gs);
    srt_close(grp);
    srt_close(lsn);
}



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
    using namespace srt;

    TestInit srtinit;
    MAKE_UNIQUE_SOCK(ss, "GrpCaller", srt_create_group(SRT_GTYPE_BROADCAST));

    std::vector<SRT_SOCKGROUPCONFIG> targets;
    for (int i = 0; i < 2; ++i)
    {
        sockaddr_any sa = srt::CreateAddr("192.168.1.237", 4200 + i, AF_INET);
        const SRT_SOCKGROUPCONFIG gd = srt_prepare_endpoint(NULL, sa.get(), sa.size());
        targets.push_back(gd);
    }

    std::future<void> closing_promise = std::async(std::launch::async, [](int s) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cerr << "Closing group" << std::endl;
        srt_close(s);
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
    using namespace srt;

    TestInit srtinit;

    const string ADDR = "127.0.0.1";
    const int PORT = 4209;

    // NOTE: Add more group types, if implemented!
    vector<SRT_GROUP_TYPE> types { SRT_GTYPE_BROADCAST, SRT_GTYPE_BACKUP };

    for (const auto GTYPE: types)
    {
        g_listen_socket = srt_create_socket();
        sockaddr_in bind_sa;
        memset(&bind_sa, 0, sizeof bind_sa);
        bind_sa.sin_family = AF_INET;
        EXPECT_EQ(inet_pton(AF_INET, ADDR.c_str(), &bind_sa.sin_addr), 1);
        bind_sa.sin_port = htons(PORT);

        EXPECT_NE(srt_bind(g_listen_socket, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
        const int yes = 1;
        srt_setsockflag(g_listen_socket, SRTO_GROUPCONNECT, &yes, sizeof yes);
        EXPECT_NE(srt_listen(g_listen_socket, 5), -1);

        int lsn_eid = srt_epoll_create();
        int lsn_events = SRT_EPOLL_IN | SRT_EPOLL_ERR | SRT_EPOLL_UPDATE;
        srt_epoll_add_usock(lsn_eid, g_listen_socket, &lsn_events);

        // Caller part

        const int ss = srt_create_group(GTYPE);
        EXPECT_NE(ss, SRT_ERROR);
        std::cout << "Created group socket: " << ss << '\n';

        int no = 0;
        EXPECT_NE(srt_setsockopt(ss, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode
        EXPECT_NE(srt_setsockopt(ss, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // non-blocking mode

        const int poll_id = srt_epoll_create();
        // Will use this epoll to wait for srt_accept(...)
        const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
        EXPECT_NE(srt_epoll_add_usock(poll_id, ss, &epoll_out), SRT_ERROR);

        srt_connect_callback(ss, &ConnectCallback, this);

        sockaddr_in sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(PORT);
        EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

        //srt_setloglevel(LOG_DEBUG);

        auto acthr = std::thread([&lsn_eid]() {
                SRT_EPOLL_EVENT ev[3];

                ThreadName::set("TEST_A");

                cout << "[A] Waiting for accept\n";

                // This can wait in infinity; worst case it will be killed in process.
                int uwait_res = srt_epoll_uwait(lsn_eid, ev, 3, -1);
                EXPECT_EQ(uwait_res, 1);
                EXPECT_EQ(ev[0].fd, g_listen_socket);

                // Check if the IN event is set, even if it's not the only event
                const int ev_in_bit = SRT_EPOLL_IN;
                EXPECT_NE(ev[0].events & ev_in_bit, 0);
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
                    // Wait up to 5s to avoid hangup in case of error
                    uwait_res = srt_epoll_uwait(lsn_eid, ev, 3, 5000);
                    EXPECT_EQ(uwait_res, 1);
                    EXPECT_EQ(ev[0].fd, g_listen_socket);
                    EXPECT_EQ(ev[0].events, (int)SRT_EPOLL_UPDATE);
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

        EXPECT_NE(srt_epoll_add_usock(poll_id, ss, &epoll_out), SRT_ERROR);

        int result = srt_connect_group(ss, cc, 2);
        EXPECT_NE(result, -1);
        char data[4] = { 1, 2, 3, 4};
        cout << "Sending...\n";
        int wrong_send = srt_send(ss, data, sizeof data);
        cout << "Getting error...\n";
        int errorcode = srt_getlasterror(NULL);
        EXPECT_EQ(wrong_send, -1);
        EXPECT_EQ(errorcode, SRT_EASYNCSND) << "REAL ERROR: " << srt_getlasterror_str();

        // Wait up to 2s
        SRT_EPOLL_EVENT ev[3];
        const int uwait_result = srt_epoll_uwait(poll_id, ev, 3, 2000);
        std::cout << "Returned from connecting two sockets " << std::endl;

        EXPECT_EQ(uwait_result, 1);  // Expect the group reported
        EXPECT_EQ(ev[0].fd, ss);

        // One second to make sure that both links are connected.
        this_thread::sleep_for(seconds(1));

        EXPECT_EQ(srt_close(ss), 0);
        acthr.join();

        srt_epoll_release(lsn_eid);
        srt_epoll_release(poll_id);

        srt_close(g_listen_socket);
    }

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
    using namespace srt;

    TestInit srtinit;

    g_nconnected = 0;
    g_nfailed = 0;

    g_listen_socket = srt_create_socket();
    ASSERT_NE(g_listen_socket, SRT_INVALID_SOCK);
    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(4200);

    EXPECT_NE(srt_bind(g_listen_socket, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
    const int yes = 1;
    srt_setsockflag(g_listen_socket, SRTO_GROUPCONNECT, &yes, sizeof yes);
    EXPECT_NE(srt_listen(g_listen_socket, 5), -1);

    // Caller part

    const int ss = srt_create_group(SRT_GTYPE_BACKUP);
    EXPECT_NE(ss, SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

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
            EXPECT_EQ(ds, 8);

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
    EXPECT_GT(result, 0); // blocking mode, first connection = returns Socket ID

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
    EXPECT_NE(nwait, size_t());

    // Now send one packet
    long long data = 0x1234123412341234;

    SRT_MSGCTRL mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 2;

    // This call should retrieve the group information
    // AFTER the transition has happened
    int sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, int(sizeof data));

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
    using namespace srt;

    TestInit srtinit;

    g_nconnected = 0;
    g_nfailed = 0;

    g_listen_socket = srt_create_socket();
    ASSERT_NE(g_listen_socket, SRT_INVALID_SOCK);
    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(4200);

    EXPECT_NE(srt_bind(g_listen_socket, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
    const int yes = 1;
    srt_setsockflag(g_listen_socket, SRTO_GROUPCONNECT, &yes, sizeof yes);
    EXPECT_NE(srt_listen(g_listen_socket, 5), -1);

    // Caller part

    const int ss = srt_create_group(SRT_GTYPE_BACKUP);
    EXPECT_NE(ss, SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

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
            EXPECT_EQ(ds, 8);

            cout << "[A] Receiving 2...\n";
            ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            EXPECT_EQ(ds, 8);

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
    EXPECT_GT(result, 0); // connect with only one element returns socket ID

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
    EXPECT_EQ(sendret, int(sizeof data));
    EXPECT_EQ(mc.grpdata_size, size_t(1));
    EXPECT_EQ(gdata[0].memberstate, SRT_GST_RUNNING);

    cout << "Connecting second link weight=1:\n";
    // Now prepare the second connection
    cc[0].token = 1;
    cc[0].weight = 1; // higher than the default 0
    result = srt_connect_group(ss, cc, 1);
    EXPECT_GT(result, 0); // connect with only one element returns socket ID

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
    EXPECT_NE(nwait, size_t(0));

    // Now send one packet (again)
    mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 2;

    cout << "Sending (2)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, int(sizeof data));

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
    using namespace srt;

    TestInit srtinit;

    g_nconnected = 0;
    g_nfailed = 0;
    volatile bool recvd = false;

    // 1.
    sockaddr_in bind_sa;
    memset(&bind_sa, 0, sizeof bind_sa);
    bind_sa.sin_family = AF_INET;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &bind_sa.sin_addr), 1);
    bind_sa.sin_port = htons(4200);

    g_listen_socket = srt_create_socket();
    ASSERT_NE(g_listen_socket, SRT_INVALID_SOCK);
    EXPECT_NE(srt_bind(g_listen_socket, (sockaddr*)&bind_sa, sizeof bind_sa), -1);
    const int yes = 1;
    srt_setsockflag(g_listen_socket, SRTO_GROUPCONNECT, &yes, sizeof yes);
    EXPECT_NE(srt_listen(g_listen_socket, 5), -1);

    // Caller part
    // 2.
    const int ss = srt_create_group(SRT_GTYPE_BACKUP);
    EXPECT_NE(ss, SRT_ERROR);

    srt_connect_callback(ss, &ConnectCallback, this);

    // Set the group's stability timeout to 1s, otherwise it will
    // declare the links unstable by not receiving ACKs.
    int stabtimeo = 1000;
    srt_setsockflag(ss, SRTO_GROUPMINSTABLETIMEO, &stabtimeo, sizeof stabtimeo);

    //srt_setloglevel(LOG_DEBUG);
    srt::resetlogfa( std::set<srt_logging::LogFA> {
            SRT_LOGFA_GRP_SEND,
            SRT_LOGFA_GRP_MGMT,
            SRT_LOGFA_CONN
            });

    sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(4200);
    EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

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
            EXPECT_EQ(ds, 8);

            // A3
            cout << "[A3] Receiving 2...\n";
            ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            if (ds == -1) { cout << "[A3] ERROR: " << srt_getlasterror(NULL) << " " << srt_getlasterror_str() << endl; }
            EXPECT_EQ(ds, 8);
            recvd = true;

            // A4
            cout << "[A4] Receiving 3...\n";
            ds = srt_recvmsg2(accept_id, (char*)data, sizeof data, (&mc));
            if (ds == -1) { cout << "[A4] ERROR: " << srt_getlasterror(NULL) << " " << srt_getlasterror_str() << endl; }
            EXPECT_EQ(ds, 8);

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
    EXPECT_GT(result, 0); // BLOCKING MODE, always returns the socket value

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

    EXPECT_EQ(sendret, int(sizeof data));

    EXPECT_EQ(mc.grpdata_size, size_t(2));

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
    EXPECT_GE(result, 0); // ONE connection only - will return socket id

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
    EXPECT_NE(nwait, size_t(0));

    // Now send one packet (again)
    mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 3;

    // 8.
    cout << "(8) Sending (2)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, int(sizeof data));
    EXPECT_EQ(mc.grpdata_size, size_t(3));

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

    EXPECT_NE(mane, nullptr);
    EXPECT_EQ(mane->weight, 1);

    // Spin-wait for making sure the reception succeeded before
    // closing. This shouldn't be a problem in general, but
    int ntry = 100;
    while (!recvd && --ntry)
        this_thread::sleep_for(milliseconds(200));
    EXPECT_NE(ntry, 0);

    cout << "(9) Found activated link: [" << mane->token << "] - closing after 0.5s...\n";

    // Waiting is to make sure that the listener thread has received packet 3.
    this_thread::sleep_for(milliseconds(500));
    EXPECT_NE(srt_close(mane->id), -1);

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
    EXPECT_NE(nwait, size_t(0));

    // Now send one packet (again)
    mc = srt_msgctrl_default;
    mc.grpdata = gdata;
    mc.grpdata_size = 2;

    cout << "(11) Sending (3)\n";
    // This call should retrieve the group information
    // AFTER the transition has happened
    sendret = srt_sendmsg2(ss, (char*)&data, sizeof data, (&mc));
    EXPECT_EQ(sendret, int(sizeof data));

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

    EXPECT_NE(mane, nullptr);
    EXPECT_NE(backup, nullptr);
    EXPECT_EQ(mane->weight, 1);
    EXPECT_EQ(backup->weight, 0);

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
}


