#include <stdio.h>
#include <stdlib.h>

#include <srt.h>

#include <thread>
#include <condition_variable>
#include <chrono>

#include "gtest/gtest.h"
#include "test_env.h"
#include "utilities.h"
#include "common.h"
#include "core.h"

using namespace srt;

void test_cipaddress_pton(const char* peer_ip, int family, const uint32_t (&ip)[4])
{
    const int port = 4200;

    // Peer
    sockaddr_storage ss;
    ss.ss_family = family;

    void* sin_addr = nullptr;
    if (family == AF_INET)
    {
        sockaddr_in* const sa = (sockaddr_in*)&ss;
        sa->sin_port          = htons(port);
        sin_addr              = &sa->sin_addr;
    }
    else // IPv6
    {
        sockaddr_in6* const sa = (sockaddr_in6*)&ss;
        sa->sin6_port          = htons(port);
        sin_addr               = &sa->sin6_addr;
    }

    ASSERT_EQ(inet_pton(family, peer_ip, sin_addr), 1);
    const sockaddr_any peer(ss);

    // HOST
    sockaddr_any host(family);
    host.hport(port);

    srt::CIPAddress::pton(host, ip, peer);
    EXPECT_EQ(peer, host) << "Peer " << peer.str() << " host " << host.str();
}

// Example IPv4 address: 192.168.0.1
TEST(CIPAddress, IPv4_pton)
{
    srt::TestInit srtinit;
    const char*    peer_ip = "192.168.0.1";
    const uint32_t ip[4]   = {htobe32(0xC0A80001), 0, 0, 0};
    test_cipaddress_pton(peer_ip, AF_INET, ip);
}

// Example IPv6 address: 2001:db8:85a3:8d3:1319:8a2e:370:7348
TEST(CIPAddress, IPv6_pton)
{
    srt::TestInit srtinit;
    const char*    peer_ip = "2001:db8:85a3:8d3:1319:8a2e:370:7348";
    const uint32_t ip[4]   = {htobe32(0x20010db8), htobe32(0x85a308d3), htobe32(0x13198a2e), htobe32(0x03707348)};

    test_cipaddress_pton(peer_ip, AF_INET6, ip);
}

// Example IPv4 address: 192.168.0.1
// Maps to IPv6 address: 0:0:0:0:0:FFFF:192.168.0.1
// Simplified:                   ::FFFF:192.168.0.1
TEST(CIPAddress, IPv4_in_IPv6_pton)
{
    srt::TestInit srtinit;
    const char*    peer_ip = "::ffff:192.168.0.1";
    const uint32_t ip[4]   = {0, 0, htobe32(0x0000FFFF), htobe32(0xC0A80001)};

    test_cipaddress_pton(peer_ip, AF_INET6, ip);
}

TEST(SRTAPI, SyncRendezvousHangs)
{
    srt::TestInit srtinit;
    int yes = 1;

    SRTSOCKET sock = srt_create_socket();
    ASSERT_NE(sock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
    ASSERT_EQ(srt_setsockopt(sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes), 0);

    const int connection_timeout_ms = 1000; // rendezvous timeout is x10 hence 10seconds
    ASSERT_EQ(srt_setsockopt(sock, 0, SRTO_CONNTIMEO, &connection_timeout_ms, sizeof connection_timeout_ms), 0);

    sockaddr_in local_sa={};
    local_sa.sin_family = AF_INET;
    local_sa.sin_port = htons(9999);
    local_sa.sin_addr.s_addr = INADDR_ANY;

    sockaddr_in peer_sa= {};
    peer_sa.sin_family = AF_INET;
    peer_sa.sin_port = htons(9998);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &peer_sa.sin_addr), 1);

    uint64_t duration = 0;

    std::thread close_thread([&sock, &duration] {
        std::this_thread::sleep_for(std::chrono::seconds(1)); // wait till srt_rendezvous is called
        auto start = std::chrono::steady_clock::now();
        EXPECT_NE(srt_close(sock), SRT_ERROR);
        auto end = std::chrono::steady_clock::now();
        std::cout << "[T] @" << sock << " closed.\n";

        duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    });
    std::cout << "In-thread closing of @" << sock << std::endl;

    EXPECT_EQ(srt_rendezvous(sock, (sockaddr*)&local_sa, sizeof local_sa,
              (sockaddr*)&peer_sa, sizeof peer_sa), SRT_ERROR);

    std::cout << "After-rendezvous @" << sock << " state: " << SockStatusStr(srt_getsockstate(sock)) << std::endl;

    close_thread.join();
    std::cout << "After-thread @" << sock << " state: " << SockStatusStr(srt_getsockstate(sock)) << std::endl;
    ASSERT_LE(duration, 1lu); // Worst case it will compare uint64_t against uint32_t on 32-bit systems.
}

TEST(SRTAPI, RapidClose)
{
    srt::TestInit srtinit;
    using namespace std;


    SRTSOCKET sock = srt_create_socket();
    std::condition_variable cv_start;
    std::mutex cvm;
    sync::atomic<bool> started(false), ended(false);

    std::thread connect_thread([&sock, &cv_start, &started, &ended] {

        // Nonexistent address
        sockaddr_any sa = CreateAddr("localhost", 5555, AF_INET);
        cerr << "[T] Start connect\n";
        started = true;
        cv_start.notify_one();
        srt_connect(sock, sa.get(), sa.size());
        // It doesn't matter if it succeeds. Important is that it exits.
        ended = true;
        cerr << "[T] exit\n";
    });

    std::unique_lock<std::mutex> lk(cvm);

    // Wait until the thread surely starts
    cerr << "Waiting for thread start...\n";
    while (!started)
        cv_start.wait(lk);

    cerr << "Closing socket\n";
    srt_close(sock);
    cerr << "Waiting 250ms\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EXPECT_TRUE(ended);

    cerr << "Joining [T]\n";
    connect_thread.join();
}

void testCookieContest(int32_t agent_cookie, int32_t peer_cookie)
{
    using namespace srt;
    using namespace std;

    cout << "TEST: Cookies: agent=" << hex << agent_cookie
        << " peer=" << peer_cookie << endl << dec;
    HandshakeSide agent_side = CUDT::compareCookies(agent_cookie, peer_cookie);
    EXPECT_EQ(agent_side, HSD_INITIATOR);
    HandshakeSide peer_side =  CUDT::compareCookies(peer_cookie, agent_cookie);
    EXPECT_EQ(peer_side, HSD_RESPONDER);
}

TEST(Common, CookieContest)
{
    srt::TestInit srtinit;
    using namespace std;

    srt_setloglevel(LOG_NOTICE);

    // In this function you should pass cookies always in the order: INITIATOR, RESPONDER.
    cout << "TEST 1: two easy comparable values\n";
    testCookieContest(100, 50);
    testCookieContest(-1, -1000);
    testCookieContest(10055, -10000);

    // In this function you should pass cookies always in the order: INITIATOR, RESPONDER.

    // Values from PR 1517
    cout << "TEST 2: Values from PR 1517\n";
    testCookieContest(811599203, -1480577720);
    testCookieContest(2147483647, -2147483648);

    cout << "TEST 3: wrong post-fix\n";
    // NOTE: 0x80000001 is a negative number in hex
    testCookieContest(0x00000001, 0x80000001);
}
