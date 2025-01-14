#include <stdio.h>
#include <stdlib.h>

#include "gtest/gtest.h"
#include "test_env.h"
#include "utilities.h"
#include "common.h"
#include "api.h"

using namespace srt;

TEST(General, Startup)
{
    // Should return 0 if it was run for the first time
    // and actually performed the initialization.
    EXPECT_EQ(srt_startup(), 0);

    EXPECT_EQ(srt::CUDT::uglobal().getInstanceStatus(), std::make_pair(1, true));

    // Every next one should return the number of nested instances
    // [reinstate in 1.6.0] EXPECT_EQ(srt_startup(), 2);
    srt_startup();
    EXPECT_EQ(srt::CUDT::uglobal().getInstanceStatus(), std::make_pair(2, true));

    // Now let's pair the first instance, should NOT execute
    // [reinstate in 1.6.0] EXPECT_EQ(srt_cleanup(), 1);
    srt_cleanup();

    EXPECT_EQ(srt_cleanup(), 0);

    // Second cleanup, should report successful cleanup even if nothing is done.
    EXPECT_EQ(srt_cleanup(), 0);

    // Now let's start with getting the number of instances
    // from implicitly created ones.
    SRTSOCKET sock = srt_create_socket();

    EXPECT_EQ(srt::CUDT::uglobal().getInstanceStatus(), std::make_pair(1, true));

    EXPECT_EQ(srt_close(sock), 0);

    // Do the cleanup again, to not leave it up to the global destructor.
    EXPECT_EQ(srt_cleanup(), 0);
}

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
    // Check the NEXT TEST of General/Startup, if it has done the cleanup.
    EXPECT_EQ(srt::CUDT::uglobal().getInstanceStatus(), std::make_pair(0, false));
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
