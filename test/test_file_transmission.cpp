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

#include <array>
#include <thread>
#include <fstream>
#include <ctime>
#include <random>
#include <vector>
#include <atomic>

//#pragma comment (lib, "ws2_32.lib")

TEST(Transmission, FileUpload)
{
    srt::TestInit srtinit;
    srtinit.HandlePerTestOptions();

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
    int bind_res = -1;
    std::cout << "Looking for a free port... " << std::flush;
    for (int port = 5000; port <= 5555; ++port)
    {
        sa_lsn.sin_port = htons(port);
        bind_res = srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);
        if (bind_res == 0)
        {
            std::cout << "Running test on port " << port << "\n";
            break;
        }

        ASSERT_TRUE(bind_res == SRT_EINVOP) << "Bind failed not due to an occupied port. Result " << bind_res;
    }

    ASSERT_GE(bind_res, 0);

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
        sockaddr_in remote;
        int len = sizeof remote;
        std::cout << "[A] waiting for connection\n";
        const SRTSOCKET accepted_sock = srt_accept(sock_lsn, (sockaddr*)&remote, &len);
        ASSERT_GT(accepted_sock, 0);

        if (accepted_sock == SRT_INVALID_SOCK)
        {
            std::cerr << srt_getlasterror_str() << std::endl;
            EXPECT_NE(srt_close(sock_lsn), SRT_ERROR);
            return;
        }

        std::ofstream copyfile("file.target", std::ios::out | std::ios::trunc | std::ios::binary);

        std::vector<char> buf(1456);

        std::cout << "[A] Connected, reading data...\n";
        for (;;)
        {
            int n = srt_recv(accepted_sock, buf.data(), 1456);
            EXPECT_NE(n, SRT_ERROR) << srt_getlasterror_str();
            if (n == 0)
            {
                std::cout << "Received 0 bytes, breaking.\n";
                break;
            }
            else if (n == -1)
            {
                std::cout << "READ FAILED, breaking anyway\n";
                break;
            }

            // Write to file any amount of data received
            copyfile.write(buf.data(), n);
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

    std::cout << "Connecting...\n";
    srt_connect(sock_clr, (sockaddr*)&sa, sizeof(sa));

    std::cout << "Connection initialized" << std::endl;

    std::ifstream ifile("file.source", std::ios::in | std::ios::binary);
    std::vector<char> buf(1456);

    std::cout << "Reading file and sending...\n";
    for (;;)
    {
        size_t n = ifile.read(buf.data(), 1456).gcount();
        size_t shift = 0;
        while (n > 0)
        {
            const int st = srt_send(sock_clr, buf.data()+shift, int(n));
            ASSERT_GT(st, 0) << srt_getlasterror_str();

            n -= st;
            shift += st;
        }

        if (ifile.eof())
        {
            break;
        }

        ASSERT_EQ(ifile.good(), true);
    }

    // Finished sending, close the socket
    std::cout << "Finished sending, closing sockets:\n";
    srt_close(sock_clr);
    srt_close(sock_lsn);

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

    ifile.close();
    ifile.open("file.source", std::ios::in | std::ios::binary);

    for (size_t i = 0; i < tar_size; ++i)
    {
        EXPECT_EQ(ifile.get(), tarfile.get());
    }

    EXPECT_EQ(ifile.get(), EOF);
    EXPECT_EQ(tarfile.get(), EOF);

    remove("file.source");
    remove("file.target");

}


TEST(Transmission, FileUploadInterrupted)
{
    srt::TestInit srtinit;
    srtinit.HandlePerTestOptions();

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
    int bind_res = -1;
    std::cout << "Looking for a free port... " << std::flush;
    for (int port = 5000; port <= 5555; ++port)
    {
        sa_lsn.sin_port = htons(port);
        bind_res = srt_bind(sock_lsn, (sockaddr*)&sa_lsn, sizeof sa_lsn);
        if (bind_res == 0)
        {
            std::cout << "Running test on port " << port << "\n";
            break;
        }

        ASSERT_TRUE(bind_res == SRT_EINVOP) << "Bind failed not due to an occupied port. Result " << bind_res;
    }

    ASSERT_GE(bind_res, 0);

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
        sockaddr_in remote;
        int len = sizeof remote;
        std::cout << "[A] waiting for connection\n";
        const SRTSOCKET accepted_sock = srt_accept(sock_lsn, (sockaddr*)&remote, &len);
        ASSERT_GT(accepted_sock, 0);

        if (accepted_sock == SRT_INVALID_SOCK)
        {
            std::cerr << srt_getlasterror_str() << std::endl;
            EXPECT_NE(srt_close(sock_lsn), SRT_ERROR);
            return;
        }

        std::ofstream copyfile("file.target", std::ios::out | std::ios::trunc | std::ios::binary);

        std::vector<char> buf(1456);

        std::cout << "[A] Connected, reading data...\n";
        int n = srt_recv(accepted_sock, buf.data(), 1456);
        EXPECT_NE(n, -1);

        // Read one, then immediately close the socket and exit.
        std::cout << "[A] Closing socket\n";

        EXPECT_NE(srt_close(accepted_sock), SRT_ERROR);

        std::cout << "[A] Exit\n";
        thread_exit = true;
    });

    sockaddr_in sa = sockaddr_in();
    sa.sin_family = AF_INET;
    sa.sin_port = sa_lsn.sin_port;
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    std::cout << "Connecting...\n";
    srt_connect(sock_clr, (sockaddr*)&sa, sizeof(sa));

    std::cout << "Connection initialized" << std::endl;

    std::ifstream ifile("file.source", std::ios::in | std::ios::binary);
    std::vector<char> buf(1456);

    std::cout << "Reading file and sending...\n";
    bool stop = false;
    for (;;)
    {
        size_t n = ifile.read(buf.data(), 1456).gcount();
        size_t shift = 0;
        while (n > 0)
        {
            const int st = srt_send(sock_clr, buf.data()+shift, int(n));
            if (st == SRT_ERROR)
            {
                std::cout << "SEND ERROR: " << srt_getlasterror_str() << " - ignoring.\n";
                stop = true;
                break;
            }

            n -= st;
            shift += st;
        }

        if (ifile.eof() || stop)
        {
            break;
        }

        ASSERT_EQ(ifile.good(), true);
    }

    // Finished sending, close the socket
    std::cout << "Finished sending, closing sockets:\n";
    srt_close(sock_clr);
    srt_close(sock_lsn);

    std::cout << "Sockets closed, joining receiver thread\n";
    client.join();

    remove("file.source");
    remove("file.target");

}

// Messages that expire (msgttl) before their first transmission are skipped by the sender; the
// receiver then reports them as lost and the retransmission path must request their drop and go on.
// Regression (since 1.5.5): after the first such drop the sender kept requesting the same drop
// forever and no further data was sent.
TEST(Transmission, MessageTTLDropDoesNotStallSender)
{
    srt::TestInit srtinit;
    srtinit.HandlePerTestOptions();

    SRTSOCKET sock_lsn = srt_create_socket(), sock_clr = srt_create_socket();
    MAKE_UNIQUE_SOCK(sock_lsn_u, "listener", sock_lsn);
    MAKE_UNIQUE_SOCK(sock_clr_u, "caller", sock_clr);

    const int tt = SRTT_FILE;
    const bool message_api = true;
    for (SRTSOCKET s : {sock_lsn, sock_clr})
    {
        ASSERT_NE(srt_setsockflag(s, SRTO_TRANSTYPE, &tt, sizeof tt), SRT_ERROR);
        ASSERT_NE(srt_setsockflag(s, SRTO_MESSAGEAPI, &message_api, sizeof message_api), SRT_ERROR);
    }
    // Paced at 1 MB/s, a 2 MB burst stays in the sender buffer longer than its TTL.
    const int64_t maxbw = 1000000;
    ASSERT_NE(srt_setsockflag(sock_clr, SRTO_MAXBW, &maxbw, sizeof maxbw), SRT_ERROR);

    sockaddr_in sa = sockaddr_in();
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int bind_res = -1;
    for (int port = 5000; port <= 5555; ++port)
    {
        sa.sin_port = htons(port);
        bind_res = srt_bind(sock_lsn, (sockaddr*)&sa, sizeof sa);
        if (bind_res == 0)
            break;
    }
    ASSERT_EQ(bind_res, 0);
    ASSERT_NE(srt_listen(sock_lsn, 1), SRT_ERROR);
    ASSERT_NE(srt_connect(sock_clr, (sockaddr*)&sa, sizeof sa), SRT_ERROR);
    sockaddr_in remote;
    int len = sizeof remote;
    const SRTSOCKET accepted = srt_accept(sock_lsn, (sockaddr*)&remote, &len);
    ASSERT_NE(accepted, SRT_INVALID_SOCK);
    MAKE_UNIQUE_SOCK(accepted_u, "accepted", accepted);
    const int rcv_timeout_ms = 100;
    srt_setsockflag(accepted, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms);

    std::vector<char> msg(100000, 'x');
    for (int i = 0; i < 20; ++i)
    {
        SRT_MSGCTRL mc = srt_msgctrl_default;
        mc.msgttl = 100;
        mc.inorder = 1;
        ASSERT_NE(srt_sendmsg2(sock_clr, msg.data(), (int)msg.size(), &mc), SRT_ERROR);
    }
    // Let the burst expire, then send a message without TTL: it must be delivered.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    msg[0] = 'L';
    // With the regression this call never returned: the send queue thread kept m_RecvAckLock.
    ASSERT_NE(srt_sendmsg(sock_clr, msg.data(), (int)msg.size(), -1, true), SRT_ERROR);

    bool last_received = false;
    std::vector<char> buf(msg.size() * 2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!last_received && std::chrono::steady_clock::now() < deadline)
    {
        const int n = srt_recvmsg(accepted, buf.data(), (int)buf.size());
        if (n == (int)msg.size() && buf[0] == 'L')
            last_received = true;
    }
    EXPECT_TRUE(last_received) << "the message sent after the TTL drops was not delivered";

    SRT_TRACEBSTATS st;
    ASSERT_NE(srt_bstats(accepted, &st, 0), SRT_ERROR);
    EXPECT_GT(st.pktRcvDropTotal, 0) << "no message expired: the test did not exercise the TTL drop";
}
