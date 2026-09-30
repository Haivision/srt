
#include <chrono>
#include <thread>
#include <iostream>

#include "gtest/gtest.h"
#include "test_env.h"

#include "srt.h"
#include "netinet_any.h"
#include "common.h"
#include "core.h"
#include "api.h"

using namespace std;
using namespace std::chrono;
using namespace srt;
using namespace srt::logging;

TEST(SocketData, PeerName)
{
    srt::TestInit srtinit;

    // Single-threaded one-app connect/accept action

    int csock = srt_create_socket();
    int lsock = srt_create_socket();

    bool rd_nonblocking = false;

    srt_setsockflag(csock, SRTO_RCVSYN, &rd_nonblocking, sizeof (rd_nonblocking));
    //srt_setsockflag(lsock, SRTO_RCVSYN, &rd_nonblocking, sizeof (rd_nonblocking));

    sockaddr_any addr = srt::CreateAddr("127.0.0.1", 5000, AF_INET);

    ASSERT_NE(srt_bind(lsock, addr.get(), addr.size()), -1);

    ASSERT_NE(srt_listen(lsock, 5), -1);

    ASSERT_NE(srt_connect(csock, addr.get(), addr.size()), -1);

    sockaddr_any rev_addr;
    int accepted_sock = srt_accept(lsock, rev_addr.get(), &rev_addr.len);

    // Wait to make sure that the socket is connected.

    int retry = 10;
    while (srt_getsockstate(csock) != SRTS_CONNECTED && retry)
    {
        this_thread::sleep_for(milliseconds(500));
        --retry;
    }

    ASSERT_NE(retry, 0);

    // Now checkups
    sockaddr_any peer_addr;
    ASSERT_NE(srt_getpeername(csock, peer_addr.get(), &peer_addr.len), -1);
    sockaddr_any my_addr;
    ASSERT_NE(srt_getsockname(csock, my_addr.get(), &my_addr.len), -1);

    cout << "Connect address: " << addr.str() << endl;
    cout << "Peer address: " << peer_addr.str() << endl;
    cout << "Accept address: " << rev_addr.str() << endl;
    cout << "Caller address: " << my_addr.str() << endl;

    EXPECT_EQ(peer_addr, addr);
    EXPECT_EQ(my_addr, rev_addr);

    srt_close(csock);
    srt_close(accepted_sock);
    srt_close(lsock);
}

TEST(SocketData, CheckDragAccept)
{
    srt::TestInit testinit;
    using namespace std;

    MAKE_UNIQUE_SOCK(listener, "listener", srt_create_socket());


    sockaddr_any sa = srt::CreateAddr("127.0.0.1", 5000, AF_INET);

    srt_bind(listener, sa.get(), sa.size());
    srt_listen(listener, 1);

    SRTSOCKET caller = srt_create_socket();
    EXPECT_NE(caller, SRT_INVALID_SOCK);

    SRTSOCKET co = srt_connect(caller, sa.get(), sa.size());

    EXPECT_NE(co, SRT_INVALID_SOCK);

    SRTSOCKET acp = srt_accept(listener, 0, 0);

    EXPECT_NE(acp, SRT_INVALID_SOCK);

    SRT_SOCKSTATUS state;

    cout << "Accept done: @" << acp << ". Sending data through caller @" << caller << "\n";

    // Make a short data sending in order to make sure that
    // there is no late rejection situation due to closure.
    char data_in[1316] = { 1, 5, 3, 2 };
    char data_out[1316] = "";
    cout << "ACCEPTED. Sending data to @" << acp << ": 1 5 3 2 ...\n";
    EXPECT_EQ(srt_send(caller, data_in, 4), 4);
    cout << "Receiving data from @" << caller << "...\n";
    int recvd = -1;
    EXPECT_EQ(recvd = srt_recv(acp, data_out, 1316), 4) << "SRT ERROR: " << srt_getlasterror_str();
    if (recvd != -1)
    {
        cout << "Received data [size=" << recvd << "]: ";
        for (int i = 0; i < recvd; ++i)
            cout << int(data_out[i]) << " ";
        cout << endl;
    }

    cout << "Closing the caller @" << caller << "\n";
    srt_close(caller);

    state = srt_getsockstate(acp);

    // Both CONNECTED and BROKEN are accepted here.
    EXPECT_LE(state, SRTS_BROKEN);

    cout << "Caller closed. Sleep before checking accept...\n";
    std::this_thread::sleep_for(std::chrono::seconds(4));

    state = srt_getsockstate(acp);
    EXPECT_EQ(state, SRTS_BROKEN);

    EXPECT_NE(srt_close(acp), SRT_ERROR);

    cout << "Accept closed. Sleep before checking finally only listener...\n";
    // Check at the end if all sockets were wiped out, after 2s (make sure GC has run).
    std::this_thread::sleep_for(std::chrono::seconds(2));

    CUDTUnited& core = srt::CUDT::uglobal();

    vector<SRTSOCKET> existing_sockets = core.getSockets();

    // We haven't only closed the listener, so this one should remain.
    EXPECT_EQ(existing_sockets.size(), size_t(1));

    srt_cleanup();

    EXPECT_EQ(core.getSockets().size(), size_t());
    EXPECT_EQ(core.getClosedSockets().size(), size_t());
}

TEST(SocketData, CheckDragCaller)
{
    srt::TestInit testinit;
    using namespace std;

    MAKE_UNIQUE_SOCK(listener, "listener", srt_create_socket());

    sockaddr_any sa = srt::CreateAddr("127.0.0.1", 5000, AF_INET);

    EXPECT_NE(srt_bind(listener, sa.get(), sa.size()), SRT_ERROR);
    EXPECT_NE(srt_listen(listener, 1), SRT_ERROR);

    SRTSOCKET caller = srt_create_socket();
    EXPECT_NE(caller, SRT_INVALID_SOCK);

    SRTSOCKET co = srt_connect(caller, sa.get(), sa.size());
    EXPECT_NE(co, SRT_INVALID_SOCK);

    cout << "Caller connected: @" << caller << " - accepting connection...\n";

    SRTSOCKET acp = srt_accept(listener, 0, 0);
    EXPECT_NE(acp, SRT_INVALID_SOCK);

    // Make a short data sending in order to make sure that
    // there is no late rejection situation due to closure.
    char data_in[1316] = { 1, 5, 3, 2 };
    char data_out[1316] = "";
    cout << "ACCEPTED. Sending data to @" << acp << ": 1 5 3 2 ...\n";
    EXPECT_EQ(srt_send(caller, data_in, 4), 4);
    cout << "Receiving data from @" << caller << "...\n";
    int recvd = -1;
    EXPECT_EQ(recvd = srt_recv(acp, data_out, 1316), 4);
    if (recvd != -1)
    {
        cout << "Received data [size=" << recvd << "]: ";
        for (int i = 0; i < recvd; ++i)
            cout << int(data_out[i]) << " ";
        cout << endl;
    }

    SRT_SOCKSTATUS state;

    cout << "Closing the accepted socket @" << acp << "\n";

    //srt_setloglevel(LOG_DEBUG);
    EXPECT_NE(srt_close(acp), SRT_ERROR);

    state = srt_getsockstate(caller);

    // Both CONNECTED and BROKEN are accepted here.
    EXPECT_LE(state, SRTS_BROKEN);

    cout << "Accept closed. Sleep...\n";
    std::this_thread::sleep_for(std::chrono::seconds(4));

    state = srt_getsockstate(caller);
    EXPECT_EQ(state, SRTS_BROKEN) << "-> Value " << int(state) << " is " << SockStatusStr(state);
    EXPECT_NE(srt_close(caller), SRT_ERROR);

    cout << "Caller closed. Sleep before checking finally only listener...\n";
    // Check at the end if all sockets were wiped out, after 2s (make sure GC has run).
    std::this_thread::sleep_for(std::chrono::seconds(2));

    CUDTUnited& core = srt::CUDT::uglobal();

    vector<SRTSOCKET> existing_sockets = core.getSockets();

    // We haven't only closed the listener, so this one should remain.
    EXPECT_EQ(existing_sockets.size(), size_t(1));

    srt_cleanup();

    EXPECT_EQ(core.getSockets().size(), size_t());
    EXPECT_EQ(core.getClosedSockets().size(), size_t());
}

