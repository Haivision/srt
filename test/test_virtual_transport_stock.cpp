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
// virtual transport API symbols are declared in every build, and in a
// build without ENABLE_VIRTUAL_TRANSPORT they must fail cleanly - no
// working-by-accident, no crash. The positive paths are covered by the
// TestVirtualTransport suite, compiled only in seam-enabled builds; this
// file compiles empty there.

namespace
{

// A send callback that must never be called: the stock stub refuses the
// attachment before the callback could ever run.
int UnusedSendFunc(void*, const char*, int, const struct sockaddr*, int)
{
    return -1;
}

sockaddr_in FillV4(const char* ip, uint16_t port)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port);
    sa.sin_addr.s_addr = inet_addr(ip);
    return sa;
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

// The stock attachment contract: a fresh unbound socket is the one state
// in which a seam-enabled build accepts an attachment, so it is the state
// in which the stock refusal is proven - the stub returns NULL without
// adopting the callback or the opaque pointer.
TEST_F(TestVirtualTransportStock, AttachReturnsNullOnFreshSocket)
{
    m_sock = srt_create_socket();
    ASSERT_NE(m_sock, SRT_INVALID_SOCK);

    EXPECT_EQ(srt_attach_virtual_transport(m_sock, UnusedSendFunc, NULL), (SRTVirtualTransport*)NULL);
}

// The stock refusal shape of the int-returning calls: a socket that never
// had a transport attached cannot detach one, and a NULL transport handle
// has nothing to inject into. Both report SRT_EINVPARAM, the code of a
// rejected invalid argument, not SRT_EINVOP (no socket state is consulted:
// the seam simply does not exist in this build).
TEST_F(TestVirtualTransportStock, DetachAndInjectRejectedWithInvParam)
{
    m_sock = srt_create_socket();
    ASSERT_NE(m_sock, SRT_INVALID_SOCK);

    EXPECT_EQ(srt_detach_virtual_transport(m_sock), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();

    // A well-formed datagram and source address: the refusal must be
    // about the missing seam, not about the arguments.
    const char        datagram[32] = {0};
    const sockaddr_in src_addr     = FillV4("127.0.0.1", 5555);
    EXPECT_EQ(srt_virtual_transport_inject(
                  NULL, datagram, (int)sizeof datagram, (const struct sockaddr*)&src_addr, (int)sizeof src_addr),
              SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();
}

// The one stock operation that must succeed: releasing a NULL handle is a
// no-op. The completion of this test - without a crash - is the whole
// assertion.
TEST_F(TestVirtualTransportStock, ReleaseNullHandleIsNoOp)
{
    srt_virtual_transport_release(NULL);
}

// The options introduced with the seam exist in the SRT_SOCKOPT
// enumeration in every build, but a stock socket has no transport a
// skip-induction handshake or an explicit RTT seed could apply to, so
// both directions reject them as unknown options: the set call fails
// with SRT_EINVPARAM, and neither option can be read back.
TEST_F(TestVirtualTransportStock, SkipInductionAndInitialRTTOptionsRejected)
{
    m_sock = srt_create_socket();
    ASSERT_NE(m_sock, SRT_INVALID_SOCK);

    const int yes = 1;
    EXPECT_EQ(srt_setsockopt(m_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();

    bool skip     = false;
    int  skip_len = sizeof skip;
    EXPECT_EQ(srt_getsockopt(m_sock, 0, SRTO_SKIP_INDUCTION, &skip, &skip_len), SRT_ERROR);

    int rtt     = 0;
    int rtt_len = sizeof rtt;
    EXPECT_EQ(srt_getsockopt(m_sock, 0, SRTO_INITIAL_RTT, &rtt, &rtt_len), SRT_ERROR);
}

#endif // !defined(SRT_ENABLE_VIRTUAL_TRANSPORT)
