/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "gtest/gtest.h"
#include "test_env.h"

#include <cstring>

#include "srt.h"

#if !defined(SRT_ENABLE_VIRTUAL_TRANSPORT)

// The stock-build counterpart of the TestVirtualTransport suite: the
// srt_bind_virtual() API is declared in every build, and in a build without
// ENABLE_VIRTUAL_TRANSPORT it must fail cleanly - no working-by-accident,
// no crash. The positive paths are covered by the TestVirtualTransport
// suite, compiled only in feature-enabled builds; this file compiles empty
// there.

namespace
{

// Callbacks that must never be called: the stock stub refuses the binding
// before any callback could ever run.
int UnusedSendFunc(void*, const struct sockaddr*, int, const SRT_VTBuf*, int)
{
    return -1;
}

SRT_VTWAIT UnusedWaitReadFunc(void*, int)
{
    return SRT_VTWAIT_BROKEN;
}

int UnusedDeliverFunc(void*, struct sockaddr*, int*, SRT_VTBuf*, int)
{
    return -1;
}

sockaddr_in FillV4(const char* ip, uint16_t port)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(port);
    inet_pton(AF_INET, ip, &sa.sin_addr);
    return sa;
}

// The reference config, valid as given in a feature-enabled build: the
// stock refusal must be about the missing feature, not about the
// arguments.
SRT_VirtualTransportConfig GoodConfig()
{
    SRT_VirtualTransportConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.send           = &UnusedSendFunc;
    cfg.waitread       = &UnusedWaitReadFunc;
    cfg.deliver        = &UnusedDeliverFunc;
    cfg.opaq           = NULL;
    cfg.initial_rtt_ms = 0;
    return cfg;
}

} // namespace

class TestVirtualTransportStock : public srt::Test
{
protected:
    void setup() override {}

    void teardown() override
    {
        if (m_sock != SRT_INVALID_SOCK)
            srt_close(m_sock);
    }

    SRTSOCKET m_sock = SRT_INVALID_SOCK;
};

// srt.h documents that without ENABLE_VIRTUAL_TRANSPORT
// srt_bind_virtual always fails with SRT_EINVOP. A fresh unbound
// socket - the one state a feature-enabled build accepts - proves
// the stock refusal leaves it untouched: still SRTS_INIT, closable.
TEST_F(TestVirtualTransportStock, BindVirtualFailsOnStockBuild)
{
    const sockaddr_in           self_addr = FillV4("127.0.0.1", 5555);
    const SRT_VirtualTransportConfig cfg  = GoodConfig();

    m_sock = srt_create_socket();
    ASSERT_NE(m_sock, SRT_INVALID_SOCK);

    EXPECT_EQ(srt_bind_virtual(m_sock, (const struct sockaddr*)&self_addr, (int)sizeof self_addr, &cfg), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVOP) << srt_getlasterror_str();

    // The refusal left the socket usable: unbound as before, and closable.
    EXPECT_EQ(srt_getsockstate(m_sock), SRTS_INIT);
    EXPECT_EQ(srt_close(m_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_sock = SRT_INVALID_SOCK;
}

// The stock refusal is uniform, independent of argument validation:
// even an invalid socket or NULL config fails with SRT_EINVOP, not
// the SRT_EINVPARAM a feature-enabled build reports (pinned by
// BindVirtualBadConfigRejected).
TEST_F(TestVirtualTransportStock, BindVirtualBadArgsOnStockBuild)
{
    const sockaddr_in                self_addr = FillV4("127.0.0.1", 5555);
    const SRT_VirtualTransportConfig cfg       = GoodConfig();

    // An invalid socket handle.
    EXPECT_EQ(srt_bind_virtual(SRT_INVALID_SOCK, (const struct sockaddr*)&self_addr, (int)sizeof self_addr, &cfg),
              SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVOP) << srt_getlasterror_str();

    // A NULL config on a fresh socket: still the feature refusal.
    m_sock = srt_create_socket();
    ASSERT_NE(m_sock, SRT_INVALID_SOCK);
    EXPECT_EQ(srt_bind_virtual(m_sock, (const struct sockaddr*)&self_addr, (int)sizeof self_addr, NULL), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(NULL), SRT_EINVOP) << srt_getlasterror_str();

    // The refusals left the socket usable.
    EXPECT_EQ(srt_getsockstate(m_sock), SRTS_INIT);
    EXPECT_EQ(srt_close(m_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_sock = SRT_INVALID_SOCK;
}

#endif // !defined(SRT_ENABLE_VIRTUAL_TRANSPORT)
