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

public:
    void StartListener()
    {
        // Specify address of the listener
        sockaddr* psa = (sockaddr*)&m_sa;
        ASSERT_NE(srt_bind(m_listen_sock, psa, sizeof m_sa), SRT_ERROR);

        srt_listen(m_listen_sock, 1);
    }

    SRTSOCKET EstablishConnection()
    {
        auto accept_async = [](SRTSOCKET listen_sock) {
            sockaddr_in client_address;
            int length = sizeof(sockaddr_in);
            const SRTSOCKET accepted_socket = srt_accept(listen_sock, (sockaddr*)&client_address, &length);
            return accepted_socket;
        };
        auto accept_res = async(launch::async, accept_async, m_listen_sock);

        sockaddr* psa = (sockaddr*)&m_sa;
        const int connect_res = srt_connect(m_caller_sock, psa, sizeof m_sa);
        EXPECT_EQ(connect_res, SRT_SUCCESS);

        const SRTSOCKET accepted_sock = accept_res.get();
        EXPECT_NE(accepted_sock, SRT_INVALID_SOCK);

        return accepted_sock;
    }

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp()
    {
        ASSERT_GE(srt_startup(), 0);
        const int yes = 1;

        memset(&m_sa, 0, sizeof m_sa);
        m_sa.sin_family = AF_INET;
        m_sa.sin_port = htons(5200);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &m_sa.sin_addr), 1);

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

    sockaddr_in m_sa;
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

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

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

bool operator==(const SRT_PACING_CONFIG& lhs, const SRT_PACING_CONFIG& rhs)
{
    return lhs.mode == rhs.mode && lhs.bw == rhs.bw && lhs.bw_overhead == rhs.bw_overhead;
}

// Try to set/get SRTO_PACINGMODE with wrong optlen
TEST_F(TestSocketOptions, PacingModeGetSetWrongLen)
{
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg) - 1;
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_ERROR);

    cfg.mode = SRT_PACING_MAXBW_DEFAULT;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg - 1), SRT_ERROR);
}

// Check the default SRTO_PACINGMODE is SRT_PACING_MAXBW_DEFAULT
TEST_F(TestSocketOptions, PacingModeDefault)
{
    const int64_t def_maxbw = 1000000000 / 8;
    const SRT_PACING_CONFIG cfg_expected = { SRT_PACING_MAXBW_DEFAULT, def_maxbw, -1 };
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(optlen, (int) (sizeof cfg));
    EXPECT_EQ(cfg, cfg_expected);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check both listener and accepted socket have default values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optlen = (int) (sizeof cfg);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int) (sizeof cfg));
        EXPECT_EQ(cfg, cfg_expected);
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check with SRT_PACING_UNSET the actual mode is automatically detected
TEST_F(TestSocketOptions, PacingModeUnset)
{
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);
    cfg.mode = SRT_PACING_UNSET;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_SUCCESS);

    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_MAXBW_DEFAULT);
    EXPECT_EQ(cfg.bw, 1000000000/8);
    EXPECT_EQ(cfg.bw_overhead, -1);

    int64_t optval = 0;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_MAXBW, &optval, sizeof optval), SRT_SUCCESS);

    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_INBW_ESTIMATE);
    EXPECT_EQ(cfg.bw_overhead, 25);

    optval = 1000;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_INPUTBW, &optval, sizeof optval), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_INBW_SET);
    EXPECT_EQ(cfg.bw, optval);
    EXPECT_EQ(cfg.bw_overhead, 25);
}

// Check that setting SRT_PACING_MAXBW_DEFAULT results in proper mode
TEST_F(TestSocketOptions, PacingModeMaxBWDefault)
{
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);

    cfg.mode = SRT_PACING_MAXBW_DEFAULT;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_SUCCESS);

    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_MAXBW_DEFAULT);
    EXPECT_EQ(cfg.bw, 1000000000/8);
    EXPECT_EQ(cfg.bw_overhead, -1);
}

// Check setting and getting SRT_PACING_MAXBW_SET
TEST_F(TestSocketOptions, PacingModeMaxBWSet)
{
    const int64_t bw = 50000000;
    const int64_t def_maxbw = 1000000000 / 8;
    const SRT_PACING_CONFIG cfg_default  = { SRT_PACING_MAXBW_DEFAULT, def_maxbw, -1 };
    const SRT_PACING_CONFIG cfg_maxbwset = { SRT_PACING_MAXBW_SET, bw, -1 };
    
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);

    cfg.mode = SRT_PACING_MAXBW_SET;
    cfg.bw = 0;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_ERROR) << "BW has to be a positive number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_default);

    cfg.mode = SRT_PACING_MAXBW_SET;
    cfg.bw = -1;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_ERROR) << "BW has to be a positive number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_default);

    cfg.mode = SRT_PACING_MAXBW_SET;
    cfg.bw = bw;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_maxbwset);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optlen = (int) (sizeof cfg);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int) (sizeof cfg));
        EXPECT_EQ(cfg, cfg_maxbwset);
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check setting and getting SRT_PACING_MAXBW_SET in runtime
TEST_F(TestSocketOptions, PacingModeMaxBWRuntime)
{
    const int64_t bw = 50000000;
    const SRT_PACING_CONFIG cfg_maxbwset = { SRT_PACING_MAXBW_SET, bw, -1 };

    // Establish a connection
    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();
    
    // Test a connected socket
    SRT_PACING_CONFIG cfg = { SRT_PACING_MAXBW_SET, bw, -1 };
    int optlen = (int) (sizeof cfg);
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_maxbwset);

    const SRT_PACING_CONFIG cfg_maxbw_changed = { SRT_PACING_MAXBW_SET, 2 * bw, -1 };
    optlen = (int) (sizeof cfg);
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg_maxbw_changed, sizeof cfg), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_maxbw_changed);

    // This time change individual options (MAXBW)
    const SRT_PACING_CONFIG cfg_maxbw_changed2 = { SRT_PACING_MAXBW_SET, bw / 2, -1 };
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MAXBW, &cfg_maxbw_changed2.bw, sizeof cfg_maxbw_changed2.bw), SRT_SUCCESS);
    const int32_t bw_overhead = 5;
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_OHEADBW, &bw_overhead, sizeof bw_overhead), SRT_SUCCESS);
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_INPUTBW, &bw, sizeof bw), SRT_SUCCESS);
    optlen = (int) (sizeof cfg);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_maxbw_changed2);

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check that setting SRT_PACING_INBW_SET results in proper mode
TEST_F(TestSocketOptions, PacingModeInputBWSet)
{
    const int64_t bw = 50000000;
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);

    cfg.mode = SRT_PACING_INBW_SET;
    cfg.bw = 0;
    cfg.bw_overhead = 25;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_ERROR) << "BW has to be a positive number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode,SRT_PACING_MAXBW_DEFAULT);
    EXPECT_EQ(cfg.bw, 1000000000/8);
    EXPECT_EQ(cfg.bw_overhead, -1);

    cfg.mode = SRT_PACING_INBW_SET;
    cfg.bw = -1;
    cfg.bw_overhead = 25;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_ERROR) << "BW has to be a positive number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_MAXBW_DEFAULT);
    EXPECT_EQ(cfg.bw, 1000000000/8);
    EXPECT_EQ(cfg.bw_overhead, -1);

    const SRT_PACING_CONFIG cfg_inputbw = { SRT_PACING_INBW_SET, bw, 10 };
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg_inputbw, sizeof cfg_inputbw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_inputbw);
    
    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    // Check accepted socket inherits values
    for (SRTSOCKET sock : { m_listen_sock, accepted_sock })
    {
        optlen = (int) (sizeof cfg);
        EXPECT_EQ(srt_getsockopt(sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
        EXPECT_EQ(optlen, (int) (sizeof cfg));
        EXPECT_EQ(cfg, cfg_inputbw);
    }

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check setting SRT_PACING_INPUTBW in runtime
TEST_F(TestSocketOptions, PacingModeInputBWRuntime)
{
    const int64_t bw = 50000000;
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);

    StartListener();
    const SRTSOCKET accepted_sock = EstablishConnection();

    const SRT_PACING_CONFIG cfg_inputbw = { SRT_PACING_INBW_SET, bw, 10 };
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg_inputbw, sizeof cfg_inputbw), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_inputbw);

    const SRT_PACING_CONFIG cfg_changed = { SRT_PACING_INBW_SET, 2 * bw, 5 };
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg_changed, sizeof cfg_changed), SRT_SUCCESS);
    optlen = (int) (sizeof cfg);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_changed);

    // This time change individual options (INPUTBW and OHEADBW)
    const SRT_PACING_CONFIG cfg_changed2 = { SRT_PACING_INBW_SET, bw / 2, 10 };
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_INPUTBW, &cfg_changed2.bw, sizeof cfg_changed2.bw), SRT_SUCCESS);
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_OHEADBW, &cfg_changed2.bw_overhead, sizeof cfg_changed2.bw_overhead), SRT_SUCCESS);
    EXPECT_EQ(srt_setsockopt(accepted_sock, 0, SRTO_MAXBW, &bw, sizeof bw), SRT_SUCCESS) << "Must not affect the pacing mode";
    optlen = (int) (sizeof cfg);
    EXPECT_EQ(srt_getsockopt(accepted_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg, cfg_changed2);

    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
}

// Check that setting SRT_PACING_INBW_ESTIMATE results in proper mode
TEST_F(TestSocketOptions, PacingModeInBWEstimate)
{
    const int64_t bw = 50000000;
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);

    cfg.mode = SRT_PACING_INBW_ESTIMATE;
    cfg.bw = -1;
    cfg.bw_overhead = 0;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_ERROR) << "BW overhead has to be a positive number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_MAXBW_DEFAULT);
    EXPECT_EQ(cfg.bw, 1000000000/8);
    EXPECT_EQ(cfg.bw_overhead, -1);

    cfg.mode = SRT_PACING_INBW_ESTIMATE;
    cfg.bw = -1;
    cfg.bw_overhead = 25;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_SUCCESS) << "negative BW has to be ignored";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_INBW_ESTIMATE);
    EXPECT_EQ(cfg.bw, 0);
    EXPECT_EQ(cfg.bw_overhead, 25);

    cfg.mode = SRT_PACING_INBW_ESTIMATE;
    cfg.bw = bw;
    cfg.bw_overhead = 25;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_INBW_ESTIMATE);
    EXPECT_EQ(cfg.bw, 0);
    EXPECT_EQ(cfg.bw_overhead, 25);
}

// Check that setting SRT_PACING_INBW_MINESTIMATE results in proper mode
TEST_F(TestSocketOptions, PacingModeInBWMinEstimate)
{
    const int64_t bw = 50000000;
    SRT_PACING_CONFIG cfg;
    int optlen = (int) (sizeof cfg);

    cfg.mode = SRT_PACING_INBW_MINESTIMATE;
    cfg.bw = 0;
    cfg.bw_overhead = 25;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_ERROR) << "BW has to be a positive number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_MAXBW_DEFAULT);
    EXPECT_EQ(cfg.bw, 1000000000/8);
    EXPECT_EQ(cfg.bw_overhead, -1);

    cfg.mode = SRT_PACING_INBW_MINESTIMATE;
    cfg.bw = -1;
    cfg.bw_overhead = 25;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_ERROR) << "BW has to be a positive number";
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_MAXBW_DEFAULT);
    EXPECT_EQ(cfg.bw, 1000000000/8);
    EXPECT_EQ(cfg.bw_overhead, -1);

    cfg.mode = SRT_PACING_INBW_MINESTIMATE;
    cfg.bw = bw;
    cfg.bw_overhead = 25;
    EXPECT_EQ(srt_setsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, sizeof cfg), SRT_SUCCESS);
    EXPECT_EQ(srt_getsockopt(m_listen_sock, 0, SRTO_PACINGMODE, &cfg, &optlen), SRT_SUCCESS);
    EXPECT_EQ(cfg.mode, SRT_PACING_INBW_MINESTIMATE);
    EXPECT_EQ(cfg.bw, bw);
    EXPECT_EQ(cfg.bw_overhead, 25);
}


