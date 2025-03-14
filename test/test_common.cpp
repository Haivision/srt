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

    SRTSOCKET m_bindsock = srt_create_socket();
    ASSERT_NE(m_bindsock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_bindsock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(m_bindsock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
    ASSERT_EQ(srt_setsockopt(m_bindsock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes), 0);

    const int connection_timeout_ms = 1000; // rendezvous timeout is x10 hence 10seconds
    ASSERT_EQ(srt_setsockopt(m_bindsock, 0, SRTO_CONNTIMEO, &connection_timeout_ms, sizeof connection_timeout_ms), 0);

    sockaddr_in local_sa={};
    local_sa.sin_family = AF_INET;
    local_sa.sin_port = htons(9999);
    local_sa.sin_addr.s_addr = INADDR_ANY;

    sockaddr_in peer_sa= {};
    peer_sa.sin_family = AF_INET;
    peer_sa.sin_port = htons(9998);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &peer_sa.sin_addr), 1);

    uint64_t duration = 0;

    std::thread close_thread([&m_bindsock, &duration] {
        std::this_thread::sleep_for(std::chrono::seconds(1)); // wait till srt_rendezvous is called
        auto start = std::chrono::steady_clock::now();
        srt_close(m_bindsock);
        auto end = std::chrono::steady_clock::now();

        duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
    });

    EXPECT_EQ(srt_rendezvous(m_bindsock, (sockaddr*)&local_sa, sizeof local_sa,
              (sockaddr*)&peer_sa, sizeof peer_sa), SRT_ERROR);

    close_thread.join();
    ASSERT_LE(duration, 1lu); // Worst case it will compare uint64_t against uint32_t on 32-bit systems.
}

TEST(SRTAPI, RapidClose)
{
    srt::TestInit srtinit;

    SRTSOCKET sock = srt_create_socket();
    std::condition_variable cv_start;
    std::mutex cvm;
    sync::atomic<bool> started(false), ended(false);

    std::thread connect_thread([&sock, &cv_start, &started, &ended] {
        started = true;
        cv_start.notify_one();

        // Nonexistent address
        sockaddr_any sa = CreateAddr("localhost", 5555, AF_INET);
        srt_connect(sock, sa.get(), sa.size());
        // It doesn't matter if it succeeds. Important is that it exits.
        ended = true;
    });

    std::unique_lock<std::mutex> lk(cvm);

    // Wait until the thread surely starts
    while (!started)
        cv_start.wait(lk);

    srt_close(sock);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    EXPECT_TRUE(ended);
    connect_thread.join();
}
