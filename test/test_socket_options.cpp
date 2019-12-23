/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Written by:
 *             Haivision Systems Inc.
 */

#include <gtest/gtest.h>
#include <future>
#include <thread>

#include "srt.h"

using namespace std;


class TestSocketOptions
    : public ::testing::Test
{
protected:
    TestSocketOptions()
    {
        // initialization code here
    }

    ~TestSocketOptions()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp()
    {
        ASSERT_EQ(srt_startup(), 0);
        const int yes = 1;

        m_caller_sock = srt_create_socket();
        ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_caller_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect

        m_listen_sock = srt_create_socket();
        ASSERT_NE(m_listen_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
        ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_SNDSYN, &yes, sizeof yes), SRT_SUCCESS); // for async connect
    }

    void TearDown()
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        ASSERT_NE(srt_close(m_caller_sock), SRT_ERROR);
        ASSERT_NE(srt_close(m_listen_sock), SRT_ERROR);
        srt_cleanup();
    }

protected:
    // put in any custom data members that you need

    SRTSOCKET m_caller_sock = SRT_INVALID_SOCK;
    SRTSOCKET m_listen_sock = SRT_INVALID_SOCK;

    int       m_pollid = 0;
};


/// A regression test for issue #735, fixed by PR #843.
/// Checks propagation of listener's socket option SRTO_LOSSMAXTTL
/// on SRT sockets being accepted.
TEST_F(TestSocketOptions, LossMaxTTL)
{
    const int loss_max_ttl = 5;
    ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_LOSSMAXTTL, &loss_max_ttl, sizeof loss_max_ttl), SRT_SUCCESS);

    // Specify address
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5200);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
    sockaddr* psa = (sockaddr*)&sa;
    ASSERT_NE(srt_bind(m_listen_sock, psa, sizeof sa), SRT_ERROR);

    srt_listen(m_listen_sock, 1);

    auto accept_async = [](SRTSOCKET listen_sock) {
        sockaddr_in client_address;
        int length = sizeof(sockaddr_in);
        const SRTSOCKET accepted_socket = srt_accept(listen_sock, (sockaddr*)&client_address, &length);
        return accepted_socket;
    };
    auto accept_res = async(launch::async, accept_async, m_listen_sock);

    const int connect_res = srt_connect(m_caller_sock, psa, sizeof sa);
    EXPECT_EQ(connect_res, SRT_SUCCESS);

    const SRTSOCKET accepted_sock = accept_res.get();
    ASSERT_NE(accepted_sock, SRT_INVALID_SOCK);

    int opt_val = 0;
    int opt_len = 0;
    ASSERT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_LOSSMAXTTL, &opt_val, &opt_len), SRT_SUCCESS);
    EXPECT_EQ(opt_val, loss_max_ttl) << "Wrong SRTO_LOSSMAXTTL value on the accepted socket";
    EXPECT_EQ(opt_len, sizeof opt_len) << "Wrong SRTO_LOSSMAXTTL value length on the accepted socket";

    SRT_TRACEBSTATS stats;
    EXPECT_EQ(srt_bstats(accepted_sock, &stats, 0), SRT_SUCCESS);
    EXPECT_EQ(stats.pktReorderTolerance, loss_max_ttl);

    ASSERT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_LOSSMAXTTL, &opt_val, &opt_len), SRT_SUCCESS);
    EXPECT_EQ(opt_val, loss_max_ttl) << "Wrong SRTO_LOSSMAXTTL value on the listener socket";
    EXPECT_EQ(opt_len, sizeof opt_len) << "Wrong SRTO_LOSSMAXTTL value length on the listener socket";

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}



/// A regression test for issue #735, fixed by PR #843.
/// Checks propagation of listener's socket option SRTO_LOSSMAXTTL
/// on SRT sockets being accepted.
TEST_F(TestSocketOptions, Linger)
{
    const int linger = 5;
    ASSERT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_LINGER, &linger, sizeof linger), SRT_SUCCESS);
}
