/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include <gtest/gtest.h>
#include "test_env.h"

#ifdef _WIN32
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#else
typedef int SOCKET;
#define INVALID_SOCKET ((SOCKET)-1)
#define closesocket close
#endif

#include "platform_sys.h"
#include "srt.h"
#include "netinet_any.h"
#include "common.h"

// A connecting socket must never be observable as SRTS_BROKEN while its
// connection is still being established or completed. CUDTSocket::getStatus()
// used to synthesize SRTS_BROKEN out of the pair (m_State not in
// {CONNECTING, CONNECTED, BROKEN}, m_Status == SRTS_CONNECTING) - a
// combination that every state/status transition of a connecting caller
// produces on another thread's read interleaving (the two atomics are stored
// one after the other). An application polling srt_getsockstate() - e.g. a
// WebTransport gateway polling every session on its registry tick - can catch
// such a pair and tear down a live connection. This test drives many
// asynchronous loopback connects while one thread churns global-lock-holding
// API calls (stretching the transition windows) and another samples the
// socket state the way a polling application does; against a listener that
// accepts every caller a BROKEN observation can only be the false report.
class TestSockstateRace
    : public srt::Test
{
protected:
    TestSockstateRace() {}
    ~TestSockstateRace() {}

    void setup() override
    {
        // Find an unused UDP port for the listener, the same way
        // TestConnectionTimeout reserves one, but release the probe socket
        // so that the SRT listener can bind the port.
        m_sa.sin_family      = AF_INET;
        m_sa.sin_addr.s_addr = INADDR_ANY;
        m_udp_sock           = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ASSERT_NE(m_udp_sock, -1);

        int                  bind_res = -1;
        const struct sockaddr* psa    = reinterpret_cast<const struct sockaddr*>(&m_sa);
        for (int port = 5000; port <= 5555; ++port)
        {
            m_sa.sin_port = htons(port);
            bind_res      = ::bind(m_udp_sock, psa, sizeof m_sa);
            if (bind_res >= 0)
            {
                std::cerr << "Running test on port " << port << "\n";
                break;
            }
        }

        ASSERT_GE(bind_res, 0);
        closesocket(m_udp_sock);
        m_udp_sock = INVALID_SOCKET;
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &m_sa.sin_addr), 1);
    }

    void teardown() override {}

    // Wait until the socket is readable (a connection is queued for accept),
    // with a deadline so that a failed connect cannot hang the test.
    static bool WaitReadable(SRTSOCKET sock, int timeout_ms)
    {
        const int pollid = srt_epoll_create();
        if (pollid == SRT_ERROR)
            return false;
        const int events = SRT_EPOLL_IN;
        if (srt_epoll_add_usock(pollid, sock, &events) == SRT_ERROR)
        {
            srt_epoll_release(pollid);
            return false;
        }

        SRT_EPOLL_EVENT ready[1];
        const int       nready = srt_epoll_uwait(pollid, ready, 1, timeout_ms);
        srt_epoll_release(pollid);
        return nready == 1 && (ready[0].events & SRT_EPOLL_IN) != 0;
    }

protected:
    SOCKET      m_udp_sock = INVALID_SOCKET;
    sockaddr_in m_sa       = sockaddr_in();
};

TEST_F(TestSockstateRace, ConnectingStateNeverFalselyBroken)
{
    const int num_connects = 100;

    // The listener accepts every caller, so no connect in this test can be
    // genuinely rejected (a rejection would legitimately report SRTS_BROKEN).
    SRTSOCKET listener_sock = srt_create_socket();
    ASSERT_NE(listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_bind(listener_sock, reinterpret_cast<const struct sockaddr*>(&m_sa), sizeof m_sa), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // A churn thread: bare create/close cycles hold the global control lock
    // briefly each time, contending the very locks the connect completion
    // path takes (the more contention, the wider the state/status transition
    // windows - the test must catch them).
    std::atomic<bool> churn_stop{false};
    std::thread churn(
        [&churn_stop]
        {
            while (!churn_stop.load(std::memory_order_relaxed))
            {
                const SRTSOCKET dummy = srt_create_socket();
                if (dummy != SRT_INVALID_SOCK)
                    srt_close(dummy);
                std::this_thread::yield();
            }
        });

    for (int i = 0; i < num_connects; ++i)
    {
        SRTSOCKET caller_sock = srt_create_socket();
        ASSERT_NE(caller_sock, SRT_INVALID_SOCK);
        const int no_block = 0; // asynchronous connect, like a polling application initiator
        ASSERT_EQ(srt_setsockopt(caller_sock, 0, SRTO_RCVSYN, &no_block, sizeof no_block), 0);
        // Keep the worker-side TTL far beyond the sampler deadline so that a
        // legitimate connect timeout can never be mistaken for a false BROKEN.
        const int conn_timeout_ms = 10000;
        ASSERT_EQ(srt_setsockopt(caller_sock, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), 0);

        // The state sampler: spins on srt_getsockstate the way a polling
        // application does, until the socket is connected (or broken, or the
        // deadline hits). Any SRTS_BROKEN against the accepting listener is
        // the false report under test.
        std::atomic<bool> seen_broken{false};
        std::atomic<bool> seen_connected{false};
        std::thread sampler(
            [caller_sock, &seen_broken, &seen_connected]
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (std::chrono::steady_clock::now() < deadline)
                {
                    const SRT_SOCKSTATUS st = srt_getsockstate(caller_sock);
                    if (st == SRTS_BROKEN)
                    {
                        seen_broken.store(true, std::memory_order_relaxed);
                        return;
                    }
                    if (st == SRTS_CONNECTED)
                    {
                        seen_connected.store(true, std::memory_order_relaxed);
                        return;
                    }
                    std::this_thread::yield();
                }
            });

        const int connect_rc =
            srt_connect(caller_sock, reinterpret_cast<const struct sockaddr*>(&m_sa), sizeof m_sa);
        sampler.join();
        ASSERT_NE(connect_rc, SRT_ERROR) << "connect " << i << ": " << srt_getlasterror_str();

        EXPECT_TRUE(seen_connected.load()) << "connect " << i << ": the connection never completed";
        EXPECT_FALSE(seen_broken.load())
            << "connect " << i << ": a connecting socket was reported SRTS_BROKEN (false-broken window of the state/status transition)";

        // Accept and close, so the next connect meets a free backlog slot.
        if (WaitReadable(listener_sock, 5000))
        {
            sockaddr_in accept_addr;
            int         accept_len = (int)sizeof accept_addr;
            const SRTSOCKET accepted =
                srt_accept(listener_sock, reinterpret_cast<struct sockaddr*>(&accept_addr), &accept_len);
            if (accepted != SRT_INVALID_SOCK)
            {
                EXPECT_EQ(srt_close(accepted), SRT_STATUS_OK);
            }
        }
        EXPECT_EQ(srt_close(caller_sock), SRT_STATUS_OK);

        if (HasFailure())
            break;
    }

    churn_stop.store(true, std::memory_order_relaxed);
    churn.join();
    EXPECT_EQ(srt_close(listener_sock), SRT_STATUS_OK);
}
