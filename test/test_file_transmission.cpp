/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 * Based on the proposal by Russell Greene (Issue #440)
 *
 */

#include <gtest/gtest.h>
#include "test_env.h"

#ifdef _WIN32
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#endif

#include "srt.h"
#include "netinet_any.h"
#include "hvu_threadname.h"

#include <array>
#include <thread>
#include <fstream>
#include <ctime>
#include <random>
#include <vector>
#include <atomic>

//#pragma comment (lib, "ws2_32.lib")

TEST(FileTransmission, Upload)
{
    srt::TestInit srtinit;

    // Generate the source file
    // We need a file that will contain more data
    // than can be contained in one sender buffer.

    SRTSOCKET sock_lsn = srt_create_socket(), sock_clr = srt_create_socket();

    const int tt = SRTT_FILE;
    srt_setsockflag(sock_lsn, SRTO_TRANSTYPE, &tt, sizeof tt);
    srt_setsockflag(sock_clr, SRTO_TRANSTYPE, &tt, sizeof tt);

    // Configure listener 
    sockaddr_in sa_lsn = sockaddr_in();
    sa_lsn.sin_family = AF_INET;
    sa_lsn.sin_addr.s_addr = INADDR_ANY;
    sa_lsn.sin_port = htons(5555);

    MAKE_UNIQUE_SOCK(sock_lsn_u, "listener", sock_lsn);
    MAKE_UNIQUE_SOCK(sock_clr_u, "listener", sock_clr);

    // Find unused a port not used by any other service.    
    // Otherwise srt_connect may actually connect.
    SRTSTATUS bind_res = SRT_ERROR;
    std::cout << "Looking for a free port... " << std::flush;
    for (int port = 5000; port <= 5555; ++port)
    {
        sa_lsn.sin_port = htons(port);
        bind_res = srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);
        int bind_err = srt_getlasterror(NULL);
        if (bind_res == SRT_STATUS_OK)
        {
            std::cout << "Running test on port " << port << "\n";
            break;
        }

        ASSERT_TRUE(bind_err == SRT_EINVOP) << "Bind failed not due to an occupied port. Result " << bind_err;
    }

    ASSERT_GE((int)bind_res, 0);

    srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);

    int optval = 0;
    int optlen = sizeof optval;
    ASSERT_EQ(srt_getsockflag(sock_lsn, SRTO_SNDBUF, &optval, &optlen), 0);
    const size_t filesize = 7 * optval;

    {
        std::cout << "WILL CREATE source file with size=" << filesize << " (= 7 * " << optval << "[sndbuf])\n";
        std::ofstream outfile("file.source", std::ios::out | std::ios::binary);
        ASSERT_EQ(!!outfile, true) << srt_getlasterror_str();

        std::random_device rd;
        std::mt19937 mtrd(rd());
        std::uniform_int_distribution<short> dis(0, UINT8_MAX);

        for (size_t i = 0; i < filesize; ++i)
        {
            char outbyte = dis(mtrd);
            outfile.write(&outbyte, 1);
        }
    }

    srt_listen(sock_lsn, 1);

    // Start listener-receiver thread

    std::atomic<bool> thread_exit { false };

    std::cout << "Running accept [A] thread\n";

    auto client = std::thread([&]
    {
        hvu::ThreadName::set("TEST_RCV");
        SRTSOCKET accepted_sock;
        try
        {
            sockaddr_in remote;
            int len = sizeof remote;
            std::cout << "[A] waiting for connection\n";
            accepted_sock = srt_accept(sock_lsn, (sockaddr*)&remote, &len);
            EXPECT_GT(accepted_sock, 0) << srt_getlasterror_str();

            if (accepted_sock == SRT_INVALID_SOCK)
            {
                EXPECT_NE(srt_close(sock_lsn), SRT_ERROR);
                return;
            }

            std::ofstream copyfile("file.target", std::ios::out | std::ios::trunc | std::ios::binary);
            std::vector<char> buf(1456);

            std::cout << "[A] Connected, reading data...\n";

            int nblocks = 0, nbytes = 0;
            for (;;)
            {
                int n = srt_recv(accepted_sock, buf.data(), 1456);
                EXPECT_NE(n, SRT_ERROR) << "FAILURE: " << srt_getlasterror_str() << " (extracted up to " << nbytes << " bytes)";
                if (n == 0)
                {
                    std::cout << "[A] Received 0 bytes, breaking.\n";
                    break;
                }
                else if (n == -1)
                {
                    std::cout << "[A] READ FAILED, breaking anyway\n";
                    break;
                }

                if (!nblocks)
                    std::cout << "[A] READING STARTED\n";

                nblocks++;
                nbytes += n;

                //std::cout << "<." << std::flush;

                // Write to file any amount of data received
                copyfile.write(buf.data(), n);
            }
            std::cout << "[A] Written total of " << nbytes << "B (" << nblocks << " blocks)\n";
        }
        catch (...)
        {
            std::cout << "[A] EXCEPTION (unexpected)\n";
        }
        std::cout << "[A] Closing socket\n";

        EXPECT_NE(srt_close(accepted_sock), SRT_ERROR);

        std::cout << "[A] Exit\n";
        thread_exit = true;
    });

    sockaddr_in sa = sockaddr_in();
    sa.sin_family = AF_INET;
    sa.sin_port = sa_lsn.sin_port;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    try
    {
        std::cout << "Connecting...\n";
        SRTSOCKET conn = srt_connect(sock_clr, (sockaddr*)&sa, sizeof(sa));
        ASSERT_NE(conn, SRT_INVALID_SOCK);

        std::cout << "Connection initialized" << std::endl;

        std::ifstream ifile("file.source", std::ios::in | std::ios::binary);
        std::vector<char> buf(1456);

        std::cout << "Reading file and sending...\n";
        int nblocks = 0, nbytes = 0;
        for (;;)
        {
            size_t n = ifile.read(buf.data(), 1456).gcount();
            size_t shift = 0;
            int st = -1;
            while (n > 0)
            {
                st = srt_send(sock_clr, buf.data()+shift, int(n));
                EXPECT_GT(st, 0) << srt_getlasterror_str();
                if (st < 0)
                    break;

                ++nblocks;
                nbytes += st;

                //std::cout << ".>" << std::flush;

                n -= st;
                shift += st;
            }

            if (st < 0)
            {
                std::cout << "SENDING INTERRUPTED, status=" << st << std::endl;
                break;
            }

            if (ifile.eof())
            {
                std::cout << "SENDING COMPLETE " << nbytes << "B (" << nblocks << " blocks)\n";
                break;
            }

            EXPECT_EQ(ifile.good(), true);
        }

        // Finished sending, close the socket
        std::cout << "\nFinished sending, closing sockets:\n";
    }
    catch( ...)
    {
        std::cout << "EXCEPTION!\n";
    }

    std::cout << "Sockets closed, joining receiver thread\n";
    client.join();

    std::ifstream tarfile("file.target", std::ios::in | std::ios::binary);
    EXPECT_EQ(!!tarfile, true);

    tarfile.seekg(0, std::ios::end);
    size_t tar_size = tarfile.tellg();
    EXPECT_EQ(tar_size, filesize);

    std::cout << "Comparing files\n";
    // Compare files

    // Theoretically it should work if you just rewind to 0, but
    // on Windows this somehow doesn't work. 
    tarfile.close();
    tarfile.open("file.target", std::ios::in | std::ios::binary);

    std::ifstream ifile("file.source", std::ios::in | std::ios::binary);

    for (size_t i = 0; i < tar_size; ++i)
    {
        EXPECT_EQ(ifile.get(), tarfile.get());
    }

    EXPECT_EQ(ifile.get(), EOF);
    EXPECT_EQ(tarfile.get(), EOF);

    if (srt::TestEnv::me->OptionPresent("dont-remove"))
        return;

    remove("file.source");
    remove("file.target");
}

TEST(FileTransmission, Setup46)
{
    using namespace srt;
    SRTST_REQUIRES(IPv6);
    TestInit srtinit;

    MAKE_UNIQUE_SOCK(sock_lsn, "listener", srt_create_socket());
    MAKE_UNIQUE_SOCK(sock_clr, "caller", srt_create_socket());

    const int tt = SRTT_FILE;
    srt_setsockflag(sock_lsn, SRTO_TRANSTYPE, &tt, sizeof tt);
    srt_setsockflag(sock_clr, SRTO_TRANSTYPE, &tt, sizeof tt);

    std::cout << "Socket: listener=@" << sock_lsn << " caller=@" << sock_clr << std::endl;

    // Setup a connection with IPv6 caller and IPv4 listener,
    // then send data of 1456 size and make sure two packets were used.

    // So first configure a caller for IPv6 socket, capable of
    // using IPv4. As the IP version isn't specified now when
    // creating a socket, force binding explicitly.

    // This creates the "any" spec for IPv6 with port = 0
    sockaddr_any sa(AF_INET6);

    int ipv4_and_ipv6 = 0;
    ASSERT_NE(srt_setsockflag(sock_clr, SRTO_IPV6ONLY, &ipv4_and_ipv6, sizeof ipv4_and_ipv6), -1);

    ASSERT_NE(srt_bind(sock_clr, sa.get(), sa.size()), -1);

    int connect_port = 5555;

    // Configure listener 
    sockaddr_in sa_lsn = sockaddr_in();
    sa_lsn.sin_family = AF_INET;
    sa_lsn.sin_addr.s_addr = INADDR_ANY;
    sa_lsn.sin_port = htons(connect_port);

    // Find unused a port not used by any other service.    
    // Otherwise srt_connect may actually connect.
    int bind_res = -1;
    for (connect_port = 5000; connect_port <= 5555; ++connect_port)
    {
        sa_lsn.sin_port = htons(connect_port);
        bind_res = srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);
        if (bind_res == 0)
        {
            std::cout << "Running test on port " << connect_port << "\n";
            break;
        }

        ASSERT_TRUE(bind_res == SRT_EINVOP) << "Bind failed not due to an occupied port. Result " << bind_res;
    }

    ASSERT_GE(bind_res, 0);

    srt_listen(sock_lsn, 1);

    ASSERT_EQ(inet_pton(AF_INET6, "::FFFF:127.0.0.1", &sa.sin6.sin6_addr), 1);

    sa.hport(connect_port);

    ASSERT_EQ(srt_connect(sock_clr, sa.get(), sa.size()), 0);

    MAKE_UNIQUE_SOCK(sock_acp, "accepted", srt_accept(sock_lsn, sa.get(), &sa.len));
    ASSERT_NE(sock_acp, SRT_INVALID_SOCK);
    std::cout << "Accepted: @" << sock_acp << std::endl;

    const size_t SIZE = 1454; // Max payload for IPv4 minus 2 - still more than 1444 for IPv6
    char buffer[SIZE];

    std::random_device rd;
    std::mt19937 mtrd(rd());
    std::uniform_int_distribution<short> dis(0, UINT8_MAX);

    for (size_t i = 0; i < SIZE; ++i)
    {
        buffer[i] = dis(mtrd);
    }

    EXPECT_EQ(srt_send(sock_acp, buffer, SIZE), int(SIZE)) << srt_getlasterror_str();

    char resultbuf[SIZE];
    EXPECT_EQ(srt_recv(sock_clr, resultbuf, SIZE), int(SIZE)) << srt_getlasterror_str();

    // It should use the maximum payload size per packet reported from the option.
    int payloadsize_back = 0;
    int payloadsize_back_size = sizeof (payloadsize_back);
    EXPECT_EQ(srt_getsockflag(sock_clr, SRTO_PAYLOADSIZE, &payloadsize_back, &payloadsize_back_size), 0);
    EXPECT_EQ(payloadsize_back, SRT_MAX_PLSIZE_AF_INET);

    SRT_TRACEBSTATS snd_stats, rcv_stats;
    srt_bstats(sock_acp, &snd_stats, 0);
    srt_bstats(sock_clr, &rcv_stats, 0);

    EXPECT_EQ(snd_stats.pktSentUniqueTotal, 1);
    EXPECT_EQ(rcv_stats.pktRecvUniqueTotal, 1);

    std::cerr << "[TEST END, CLOSING UNIQUE SOCKETS]\n";
}

TEST(FileTransmission, Setup66)
{
    using namespace srt;
    SRTST_REQUIRES(IPv6);
    TestInit srtinit;

    MAKE_UNIQUE_SOCK(sock_lsn, "listener", srt_create_socket());
    MAKE_UNIQUE_SOCK(sock_clr, "caller", srt_create_socket());
    std::cout << "Socket: listener=@" << sock_lsn << " caller=@" << sock_clr << std::endl;

    const int tt = SRTT_FILE;
    srt_setsockflag(sock_lsn, SRTO_TRANSTYPE, &tt, sizeof tt);
    srt_setsockflag(sock_clr, SRTO_TRANSTYPE, &tt, sizeof tt);

    // Setup a connection with IPv6 caller and IPv4 listener,
    // then send data of 1456 size and make sure two packets were used.

    // So first configure a caller for IPv6 socket, capable of
    // using IPv4. As the IP version isn't specified now when
    // creating a socket, force binding explicitly.

    // This creates the "any" spec for IPv6 with port = 0
    sockaddr_any sa(AF_INET6);

    // Require that the connection allows both IP versions.
    int ipv4_and_ipv6 = 0;
    ASSERT_NE(srt_setsockflag(sock_clr, SRTO_IPV6ONLY, &ipv4_and_ipv6, sizeof ipv4_and_ipv6), -1);
    ASSERT_NE(srt_setsockflag(sock_lsn, SRTO_IPV6ONLY, &ipv4_and_ipv6, sizeof ipv4_and_ipv6), -1);

    ASSERT_NE(srt_bind(sock_clr, sa.get(), sa.size()), -1);

    int connect_port = 5555;

    // Configure listener 
    sockaddr_any sa_lsn(AF_INET6);

    // Find unused a port not used by any other service.    
    // Otherwise srt_connect may actually connect.
    int bind_res = -1;
    for (connect_port = 5000; connect_port <= 5555; ++connect_port)
    {
        sa_lsn.hport(connect_port);
        bind_res = srt_bind(sock_lsn, sa_lsn.get(), sa_lsn.size());
        if (bind_res == 0)
        {
            std::cout << "Running test on port " << connect_port << "\n";
            break;
        }

        ASSERT_TRUE(bind_res == SRT_EINVOP) << "Bind failed not due to an occupied port. Result " << bind_res;
    }

    ASSERT_GE(bind_res, 0);

    srt_listen(sock_lsn, 1);

    ASSERT_EQ(inet_pton(AF_INET6, "::1", &sa.sin6.sin6_addr), 1);

    sa.hport(connect_port);

    std::cout << "Connecting to: " << sa.str() << std::endl;

    int connect_result = srt_connect(sock_clr, sa.get(), sa.size());
    ASSERT_EQ(connect_result, 0) << srt_getlasterror_str();

    MAKE_UNIQUE_SOCK(sock_acp, "accepted", srt_accept(sock_lsn, sa.get(), &sa.len));
    ASSERT_NE(sock_acp, SRT_INVALID_SOCK);
    std::cout << "Accepted: @" << sock_acp << std::endl;

    const size_t SIZE = 1454; // Max payload for IPv4 minus 2 - still more than 1444 for IPv6
    char buffer[SIZE];

    std::random_device rd;
    std::mt19937 mtrd(rd());
    std::uniform_int_distribution<short> dis(0, UINT8_MAX);

    for (size_t i = 0; i < SIZE; ++i)
    {
        buffer[i] = dis(mtrd);
    }

    EXPECT_EQ(srt_send(sock_acp, buffer, SIZE), int(SIZE)) << srt_getlasterror_str();

    char resultbuf[SIZE];
    EXPECT_EQ(srt_recv(sock_clr, resultbuf, SIZE), int(SIZE)) << srt_getlasterror_str();

    // It should use the maximum payload size per packet reported from the option.
    int payloadsize_back = 0;
    int payloadsize_back_size = sizeof (payloadsize_back);
    EXPECT_EQ(srt_getsockflag(sock_clr, SRTO_PAYLOADSIZE, &payloadsize_back, &payloadsize_back_size), 0);
    EXPECT_EQ(payloadsize_back, SRT_MAX_PLSIZE_AF_INET6);
    std::cout << "Payload size: " << payloadsize_back << std::endl;

    SRT_TRACEBSTATS snd_stats, rcv_stats;
    srt_bstats(sock_acp, &snd_stats, 0);
    srt_bstats(sock_clr, &rcv_stats, 0);

    // We use the same data size that fit in 1 payload IPv4, but not IPv6.
    // Therefore sending should be here split into two packets.
    EXPECT_EQ(snd_stats.pktSentUniqueTotal, 2);
    EXPECT_EQ(rcv_stats.pktRecvUniqueTotal, 2);

}
