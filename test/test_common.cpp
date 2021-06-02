#include <stdio.h>
#include <stdlib.h>

#include <srt.h>

#include <thread>
#include <chrono>

#include "gtest/gtest.h"
#include "utilities.h"
#include "common.h"

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

    CIPAddress::pton(host, ip, peer);
    EXPECT_EQ(peer, host) << "Peer " << peer.str() << " host " << host.str();
}

// Example IPv4 address: 192.168.0.1
TEST(CIPAddress, IPv4_pton)
{
    const char*    peer_ip = "192.168.0.1";
    const uint32_t ip[4]   = {htobe32(0xC0A80001), 0, 0, 0};
    test_cipaddress_pton(peer_ip, AF_INET, ip);
}

// Example IPv6 address: 2001:db8:85a3:8d3:1319:8a2e:370:7348
TEST(CIPAddress, IPv6_pton)
{
    const char*    peer_ip = "2001:db8:85a3:8d3:1319:8a2e:370:7348";
    const uint32_t ip[4]   = {htobe32(0x20010db8), htobe32(0x85a308d3), htobe32(0x13198a2e), htobe32(0x03707348)};

    test_cipaddress_pton(peer_ip, AF_INET6, ip);
}

// Example IPv4 address: 192.168.0.1
// Maps to IPv6 address: 0:0:0:0:0:FFFF:192.168.0.1
// Simplified: 	                 ::FFFF:192.168.0.1
TEST(CIPAddress, IPv4_in_IPv6_pton)
{
    const char*    peer_ip = "::ffff:192.168.0.1";
    const uint32_t ip[4]   = {0, 0, htobe32(0x0000FFFF), htobe32(0xC0A80001)};

    test_cipaddress_pton(peer_ip, AF_INET6, ip);
}


TEST(SRTAPI, SyncRendezvousHangs) {
    ASSERT_EQ(srt_startup(), 0);

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

    ASSERT_EQ(srt_rendezvous(m_bindsock, (sockaddr*)&local_sa, sizeof local_sa,
              (sockaddr*)&peer_sa, sizeof peer_sa), SRT_ERROR);

    close_thread.join();
    ASSERT_LE(duration, 1);
    srt_close(m_bindsock);
    srt_cleanup();
}
