#include <stdio.h>
#include <stdlib.h>

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

void testCookieContest(int32_t agent_cookie, int32_t peer_cookie)
{
    using namespace srt;
    using namespace std;

    SRTSOCKET agent = srt_create_socket(), peer = srt_create_socket();

    uint16_t agent_port = 5001, peer_port = 5002;
    cout << "TEST: Rem-addr: agent @" << agent << " PORT: " << peer_port
        << " peer " << peer << " PORT: " << agent_port << endl;

    cout << "TEST: Cookies: agent=" << hex << agent_cookie
        << " peer=" << peer_cookie << endl << dec;


    sockaddr_any agent_addr = srt::CreateAddr("127.0.0.1", 0, AF_INET);
    sockaddr_any peer_addr = agent_addr;

    agent_addr.hport(agent_port);
    peer_addr.hport(peer_port);

    // Bind sockets
    srt_bind(agent, agent_addr.get(), agent_addr.size());
    srt_bind(peer, peer_addr.get(), peer_addr.size());

    // Manipulate bake process

    // peer_addr will be used as target by agent and vv.
    RegisterCookieBase(peer_addr, agent_cookie);
    RegisterCookieBase(agent_addr, peer_cookie);

    int eid = srt_epoll_create();
    int event_connect = SRT_EPOLL_CONNECT;

    srt_epoll_add_usock(eid, agent, &event_connect);
    srt_epoll_add_usock(eid, peer, &event_connect);

    bool noblock = false;

    srt_setsockflag(agent, SRTO_RCVSYN, &noblock, sizeof noblock);
    //srt_setsockflag(peer, SRTO_RCVSYN, &noblock, sizeof noblock);

    bool rdv = true;

    srt_setsockflag(agent, SRTO_RENDEZVOUS, &rdv, sizeof rdv);
    srt_setsockflag(peer, SRTO_RENDEZVOUS, &rdv, sizeof rdv);

    // Set 500ms timeout - rendezvous has a builtin extended 10* this
    // time, so it will result in 5 seconds.
    int tmo = 500;
    srt_setsockflag(agent, SRTO_CONNTIMEO, &tmo, sizeof tmo);
    srt_setsockflag(peer, SRTO_CONNTIMEO, &tmo, sizeof tmo);

    SRTSOCKET sta = srt_connect(agent, peer_addr.get(), peer_addr.size());
    SRTSOCKET stp = srt_connect(peer, agent_addr.get(), agent_addr.size());

    cout << "Agent connect: " << sta << " Peer connect: " << stp << endl;

    int epoll_table[2];
    int epoll_table_size = 2;

    int nrdy = srt_epoll_wait(eid, 0, 0, epoll_table, &epoll_table_size, 1000, 0, 0, 0, 0);

    cout << "Ready sockets: " << nrdy << endl;

    EXPECT_GT(nrdy, 0);

    HandshakeSide agent_side = getHandshakeSide(agent), peer_side = getHandshakeSide(peer);

    EXPECT_EQ(agent_side, HSD_INITIATOR);
    EXPECT_EQ(peer_side, HSD_RESPONDER);

    srt_close(agent);
    srt_close(peer);

    sync::this_thread::sleep_for(sync::seconds_from(2));
    ClearCookieBase();
}

TEST(Common, CookieContest)
{
    srt::TestInit srtinit;
    using namespace std;

    srt_setloglevel(LOG_NOTICE);

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
