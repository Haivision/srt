#include <iostream>
#include <chrono>
#include <future>
#include <thread>
#include <condition_variable>
#include "gtest/gtest.h"
#include "test_env.h"
#include "api.h"
#include "epoll.h"
#include "apputil.hpp"

using namespace std;
using namespace srt;

#define TEST_UDP_PORT 9990

TEST(CEPoll, InfiniteWait)
{
    srt::TestInit srtinit;
    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    ASSERT_EQ(srt_epoll_wait(epoll_id, nullptr, nullptr,
        nullptr, nullptr,
        -1,
        0, 0, 0, 0), int(SRT_ERROR));

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);
}

TEST(CEPoll, WaitNoSocketsInEpoll)
{
    srt::TestInit srtinit;

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    int rlen = 2;
    SRTSOCKET read[2];

    int wlen = 2;
    SRTSOCKET write[2];

    ASSERT_EQ(srt_epoll_wait(epoll_id, read, &rlen, write, &wlen,
        -1, 0, 0, 0, 0), int(SRT_ERROR));

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);

}

TEST(CEPoll, WaitNoSocketsInEpoll2)
{
    srt::TestInit srtinit;

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    SRT_EPOLL_EVENT events[2];

    ASSERT_EQ(srt_epoll_uwait(epoll_id, events, 2, -1), int(SRT_ERROR));

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);

}

TEST(CEPoll, WaitEmptyCall)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    ASSERT_NE(client_sock, SRT_INVALID_SOCK);

    const int no = 0;
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

    ASSERT_EQ(srt_epoll_wait(epoll_id, 0, NULL, 0, NULL,
                -1, 0, 0, 0, 0), int(SRT_ERROR));

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);
}

TEST(CEPoll, UWaitEmptyCall)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    ASSERT_NE(client_sock, SRT_INVALID_SOCK);

    const int no = 0;
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

    ASSERT_EQ(srt_epoll_uwait(epoll_id, NULL, 10, -1), int(SRT_ERROR));

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);

}

TEST(CEPoll, WaitAllSocketsInEpollReleased)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    ASSERT_NE(client_sock, SRT_INVALID_SOCK);

    const int yes = 1;
    const int no = 0;
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);
    ASSERT_NE(srt_epoll_remove_usock(epoll_id, client_sock), SRT_ERROR);

    int rlen = 2;
    SRTSOCKET read[2];

    int wlen = 2;
    SRTSOCKET write[2];

    ASSERT_EQ(srt_epoll_wait(epoll_id, read, &rlen, write, &wlen,
        -1, 0, 0, 0, 0), int(SRT_ERROR));

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);

}

TEST(CEPoll, WaitAllSocketsInEpollReleased2)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    ASSERT_NE(client_sock, SRT_INVALID_SOCK);

    const int yes = 1;
    const int no = 0;
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);
    ASSERT_NE(srt_epoll_remove_usock(epoll_id, client_sock), SRT_ERROR);

    SRT_EPOLL_EVENT events[2];

    ASSERT_EQ(srt_epoll_uwait(epoll_id, events, 2, -1), int(SRT_ERROR));

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);
}

TEST(CEPoll, WrongEpoll_idOnAddUSock)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    ASSERT_NE(client_sock, SRT_INVALID_SOCK);

    const int no  = 0;
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    /* We intentionally pass the wrong socket ID. The error should be returned.*/
    ASSERT_EQ(srt_epoll_add_usock(epoll_id + 1, client_sock, &epoll_out), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), SRT_STATUS_OK);

}


TEST(CEPoll, HandleEpollEvent)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    EXPECT_NE(client_sock, SRT_INVALID_SOCK);

    const int yes = 1;
    const int no  = 0;
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_SENDER,    &yes, sizeof yes), SRT_ERROR);
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    CEPoll epoll;
    const int epoll_id = epoll.create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    epoll.update_usock(epoll_id, client_sock, &epoll_out);

    set<int> epoll_ids = { epoll_id };

    epoll.update_events(client_sock, epoll_ids, SRT_EPOLL_ERR, true);

    set<SRTSOCKET> readset;
    set<SRTSOCKET> writeset;
    set<SRTSOCKET>* rval = &readset;
    set<SRTSOCKET>* wval = &writeset;

    ASSERT_NE(epoll.wait(epoll_id, rval, wval, -1, nullptr, nullptr), int(SRT_ERROR));

    try
    {
        int no_events = 0;
        epoll.update_usock(epoll_id, client_sock, &no_events);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        epoll.release(epoll_id);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

}



// In this test case a caller connects to a listener on a localhost.
// Then the caller closes the connection, and listener is expected to
// be notified about connection break via polling the accepted socket.
TEST(CEPoll, NotifyConnectionBreak)
{
    srt::TestInit srtinit;

    // 1. Prepare client
    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    ASSERT_NE(client_sock, SRT_INVALID_SOCK);

    const int yes SRT_ATR_UNUSED = 1;
    const int no SRT_ATR_UNUSED = 0;
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int client_epoll_id = srt_epoll_create();
    ASSERT_GE(client_epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    /* We intentionally pass the wrong socket ID. The error should be returned.*/
    EXPECT_EQ(srt_epoll_add_usock(client_epoll_id, client_sock, &epoll_out), SRT_STATUS_OK);

    sockaddr_in sa_client;
    memset(&sa_client, 0, sizeof sa_client);
    sa_client.sin_family = AF_INET;
    sa_client.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa_client.sin_addr), 1);

    // 2. Prepare server
    MAKE_UNIQUE_SOCK(server_sock, "server_sock", srt_create_socket());
    ASSERT_NE(server_sock, SRT_INVALID_SOCK);

    ASSERT_NE(srt_setsockflag(server_sock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(server_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int server_epoll_id = srt_epoll_create();
    ASSERT_GE(server_epoll_id, 0);

    int epoll_mode = SRT_EPOLL_IN | SRT_EPOLL_ERR;
    srt_epoll_add_usock(server_epoll_id, server_sock, &epoll_mode);

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(server_sock, (sockaddr*)& sa, sizeof(sa));
    srt_listen(server_sock, 1);


    auto connect_res = std::async(std::launch::async, [&client_sock, &sa]() {
        return srt_connect(client_sock, (sockaddr*)& sa, sizeof(sa));
        });


    const int default_len = 3;
    int rlen = default_len;
    SRTSOCKET read[default_len];
    int wlen = default_len;
    SRTSOCKET write[default_len];
    // Wait on epoll for connection
    const int epoll_res = srt_epoll_wait(server_epoll_id, read, &rlen,
        write, &wlen,
        5000, /* timeout */
        0, 0, 0, 0);

    EXPECT_EQ(epoll_res, 1);
    if (epoll_res == int(SRT_ERROR))
    {
        std::cerr << "Epoll returned error: " << srt_getlasterror_str() << " (code " << srt_getlasterror(NULL) << ")\n";
    }

    // Wait for the caller connection thread to return connection result
    EXPECT_EQ(connect_res.get(), SRT_STATUS_OK);

    sockaddr_in scl;
    int sclen = sizeof scl;
    SRTSOCKET sock = srt_accept(server_sock, (sockaddr*)& scl, &sclen);
    EXPECT_NE(sock, SRT_INVALID_SOCK);

    int epoll_io = srt_epoll_create();
    int modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    EXPECT_NE(srt_epoll_add_usock(epoll_io, sock, &modes), SRT_ERROR);

    // The caller will close connection after 1 second
    auto close_res = std::async(std::launch::async, [&client_sock]() {
        cout << "(async call): WILL CLOSE client connection in 3s\n";
        this_thread::sleep_for(chrono::seconds(1));
        cout << "(async call): Closing client connection\n";
        return srt_close(client_sock);
    });

    int timeout_ms = -1;
    SRTSOCKET ready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
    int len = 2;
    cout << "TEST: entering INFINITE WAIT\n";
    const int epoll_wait_res = srt_epoll_wait(epoll_io, ready, &len, nullptr, nullptr, timeout_ms, 0, 0, 0, 0);
    cout << "TEST: return from INFINITE WAIT\n";
    if (epoll_wait_res == int(SRT_ERROR))
        cerr << "socket::read::epoll " << to_string(srt_getlasterror(nullptr));
    EXPECT_EQ(epoll_wait_res, 1);
    EXPECT_EQ(len, 1);
    EXPECT_EQ(ready[0], sock);

    // Wait for the caller to close connection
    // There should be no wait, as epoll should wait until connection is closed.
    EXPECT_EQ(close_res.get(), SRT_STATUS_OK);
    const SRT_SOCKSTATUS state = srt_getsockstate(sock);
    const bool state_valid = state == SRTS_BROKEN || state == SRTS_CLOSING || state == SRTS_CLOSED;
    EXPECT_TRUE(state_valid);
    if (!state_valid)
        cerr << "socket state: " << state << endl;

}


TEST(CEPoll, HandleEpollEvent2)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    EXPECT_NE(client_sock, SRT_INVALID_SOCK);

    const int yes = 1;
    const int no  = 0;
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock,    SRTO_SENDER,    &yes, sizeof yes), SRT_ERROR);
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    CEPoll epoll;
    const int epoll_id = epoll.create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR | SRT_EPOLL_ET;
    epoll.update_usock(epoll_id, client_sock, &epoll_out);

    set<int> epoll_ids = { epoll_id };

    epoll.update_events(client_sock, epoll_ids, SRT_EPOLL_ERR, true);

    SRT_EPOLL_EVENT fds[1024];

    int result = epoll.uwait(epoll_id, fds, 1024, -1);
    ASSERT_EQ(result, 1); 
    ASSERT_EQ(fds[0].events, int(SRT_EPOLL_ERR));

    // Edge-triggered means that after one wait call was done, the next
    // call to this event should no longer report it. Now use timeout 0
    // to return immediately.
    result = epoll.uwait(epoll_id, fds, 1024, 0);
    ASSERT_EQ(result, 0);

    try
    {
        int no_events = 0;
        epoll.update_usock(epoll_id, client_sock, &no_events);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        epoll.release(epoll_id);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

}


TEST(CEPoll, HandleEpollNoEvent)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    EXPECT_NE(client_sock, SRT_INVALID_SOCK);

    const int yes = 1;
    const int no  = 0;
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock,    SRTO_SENDER,    &yes, sizeof yes), SRT_ERROR);
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    CEPoll epoll;
    const int epoll_id = epoll.create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    epoll.update_usock(epoll_id, client_sock, &epoll_out);

    SRT_EPOLL_EVENT fds[1024];

    // Use timeout 0 because with -1 this call would hang up
    int result = epoll.uwait(epoll_id, fds, 1024, 0);
    ASSERT_EQ(result, 0); 

    try
    {
        int no_events = 0;
        epoll.update_usock(epoll_id, client_sock, &no_events);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        epoll.release(epoll_id);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

}

TEST(CEPoll, ThreadedUpdate)
{
    srt::TestInit srtinit;

    MAKE_UNIQUE_SOCK(client_sock, "client", srt_create_socket());
    EXPECT_NE(client_sock, SRT_INVALID_SOCK);

    const int no  = 0;
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect

    CEPoll epoll;
    const int epoll_id = epoll.create();
    ASSERT_GE(epoll_id, 0);
    ASSERT_EQ(epoll.setflags(epoll_id, SRT_EPOLL_ENABLE_EMPTY), 0);

    thread td = thread( [&epoll, epoll_id, client_sock]()
    {
        cerr << "Spawned thread to add sockets to eid (wait 1s to order execution)\n";
        this_thread::sleep_for(chrono::seconds(1)); // Make sure that uwait will be called as first
        cerr << "ADDING sockets to eid\n";
        const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
        epoll.update_usock(epoll_id, client_sock, &epoll_out);

        set<int> epoll_ids = { epoll_id };

        epoll.update_events(client_sock, epoll_ids, SRT_EPOLL_ERR, true);
        cerr << "THREAD END\n";
    });

    SRT_EPOLL_EVENT fds[1024];

    cerr << "Entering infinite-wait by uwait:\n";

    int result = epoll.uwait(epoll_id, fds, 1024, -1);
    cerr << "Exit no longer infinite-wait by uwait, result=" << result << "\n";
    ASSERT_EQ(result, 1); 
    ASSERT_EQ(fds[0].events, int(SRT_EPOLL_ERR));

    cerr << "THREAD JOIN...\n";
    td.join();
    cerr << "...JOINED\n";

    try
    {
        int no_events = 0;
        epoll.update_usock(epoll_id, client_sock, &no_events);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        epoll.release(epoll_id);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }
}

void testListenerReady(const bool LATE_CALL, size_t nmembers)
{
    bool is_single = true;
    bool want_sleep = !TestEnv::me->OptionPresent("nosleep");

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    TestInit init;

    SRTSOCKET server_sock, caller_sock;
    server_sock = srt_create_socket();

    if (nmembers > 0)
    {
        caller_sock = srt_create_group(SRT_GTYPE_BROADCAST);
        int on = 1;
        EXPECT_NE(srt_setsockflag(server_sock, SRTO_GROUPCONNECT, &on, sizeof on), SRT_ERROR);
        is_single = false;
    }
    else
    {
        caller_sock = srt_create_socket();
        nmembers = 1; // Set to 1 so that caller starts at least once.
    }

    srt_bind(server_sock, (sockaddr*)& sa, sizeof(sa));
    srt_listen(server_sock, nmembers+1);

    srt::setopt(server_sock)[SRTO_RCVSYN] = false;

    // Ok, the listener socket is ready; now make a call, but
    // do not do anything on the listener socket yet.

    std::cout << "Using " << (LATE_CALL ? "LATE" : "EARLY") << " call\n";

    std::vector<std::future<int>> connect_res;

    if (LATE_CALL)
    {
        // We don't need the caller to be async, it can hang up here.
        for (size_t i = 0; i < nmembers; ++i)
        {
            connect_res.push_back(std::async(std::launch::async, [&caller_sock, &sa, i]() {
                std::cout << "[T:" << i << "] CALLING\n";
                return srt_connect(caller_sock, (sockaddr*)& sa, sizeof(sa));
            }));
        }

        std::cout << "STARTED connecting...\n";
    }

    if (want_sleep)
    {
        std::cout << "Sleeping 1s...\n";
        this_thread::sleep_for(chrono::milliseconds(1000));
    }

    // What is important is that the accepted socket is now reporting in
    // on the listener socket. So let's create an epoll.

    int eid = srt_epoll_create();
    int eid_postcheck = srt_epoll_create();

    // and add this listener to it
    int modes = SRT_EPOLL_IN;
    int modes_postcheck = SRT_EPOLL_IN | SRT_EPOLL_UPDATE;
    EXPECT_NE(srt_epoll_add_usock(eid, server_sock, &modes), SRT_ERROR);
    EXPECT_NE(srt_epoll_add_usock(eid_postcheck, server_sock, &modes_postcheck), SRT_ERROR);

    if (!LATE_CALL)
    {
        // We don't need the caller to be async, it can hang up here.
        for (size_t i = 0; i < nmembers; ++i)
        {
            connect_res.push_back(std::async(std::launch::async, [&caller_sock, &sa, i]() {
                std::cout << "[T:" << i << "] CALLING\n";
                return srt_connect(caller_sock, (sockaddr*)& sa, sizeof(sa));
            }));
        }

        std::cout << "STARTED connecting...\n";
    }

    std::cout << "Waiting for readiness...\n";
    // And see now if the waiting accepted socket reports it.
    SRT_EPOLL_EVENT fdset[1];
    EXPECT_EQ(srt_epoll_uwait(eid, fdset, 1, 5000), 1);

    std::cout << "Accepting...\n";
    sockaddr_in scl;
    int sclen = sizeof scl;
    SRTSOCKET sock = srt_accept(server_sock, (sockaddr*)& scl, &sclen);
    EXPECT_NE(sock, SRT_INVALID_SOCK);

    if (nmembers > 1)
    {
        std::cout << "With >1 members, check if there's still UPDATE pending\n";
        // Spawn yet another connection within the group, just to get the update
        auto extra_call = std::async(std::launch::async, [&caller_sock, &sa]() {
                std::cout << "[T:X] CALLING (expected failure)\n";
                return srt_connect(caller_sock, (sockaddr*)& sa, sizeof(sa));
        });
        // For 2+ members, additionally check if there AREN'T any
        // further acceptance members, but there are UPDATEs.
        EXPECT_EQ(srt_epoll_uwait(eid_postcheck, fdset, 1, 5000), 1);

        // SUBSCRIBED EVENTS: IN, UPDATE.
        // expected: UPDATE only.
        EXPECT_EQ(SRT_EPOLL_OPT(fdset[0].events), SRT_EPOLL_UPDATE);
        SRTSOCKET joined = extra_call.get();
        EXPECT_NE(joined, SRT_INVALID_SOCK);
        std::cout << fmtcat("Extra joined: @", joined, "\n");
    }

    std::vector<SRT_SOCKGROUPDATA> gdata;

    if (!is_single)
    {
        EXPECT_EQ(sock & SRTGROUP_MASK, SRTGROUP_MASK);
        // +1 because we have added one more caller to check UPDATE event.
        size_t inoutlen = nmembers+1;
        gdata.resize(inoutlen);
        int groupndata = srt_group_data(sock, gdata.data(), (&inoutlen));
        EXPECT_NE(groupndata, SRT_ERROR);

        std::ostringstream sout;
        if (groupndata == SRT_ERROR)
            sout << "ERROR: " << srt_getlasterror_str() << " OUTLEN: " << inoutlen << std::endl;
        else
        {
            // Just to display the members
            sout << "(Listener) Members: ";

            for (int i = 0; i < groupndata; ++i)
                sout << "@" << gdata[i].id << " ";
            sout << std::endl;
        }

        std::cout << sout.str();
    }

    std::cout << "Joining connector thread(s)\n";
    for (size_t i = 0; i < nmembers; ++i)
    {
        std::cout << "Join: #" << i << ":\n";
        SRTSOCKET called_socket = connect_res[i].get();
        std::cout << "... " << called_socket << std::endl;
        EXPECT_NE(called_socket, SRT_INVALID_SOCK);
    }

    if (!is_single)
    {
        EXPECT_EQ(caller_sock & SRTGROUP_MASK, SRTGROUP_MASK);
        // +1 because we have added one more caller to check UPDATE event.
        size_t inoutlen = nmembers+1;
        gdata.resize(inoutlen);
        int groupndata = srt_group_data(caller_sock, gdata.data(), (&inoutlen));
        EXPECT_NE(groupndata, SRT_ERROR);

        std::ostringstream sout;
        if (groupndata == SRT_ERROR)
            sout << "ERROR: " << srt_getlasterror_str() << " OUTLEN: " << inoutlen << std::endl;
        else
        {
            // Just to display the members
            sout << "(Caller) Members: ";

            for (int i = 0; i < groupndata; ++i)
                sout << "@" << gdata[i].id << " ";
            sout << std::endl;
        }

        std::cout << sout.str();

        if (want_sleep)
        {
            std::cout << "Sleep for 3 seconds to avoid closing-in-between\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
    }

    std::cout << "Releasing EID resources and all sockets\n";

    srt_epoll_release(eid);
    srt_epoll_release(eid_postcheck);

    srt_close(server_sock);
    srt_close(caller_sock);
    srt_close(sock);
}

TEST(CEPoll, EarlyListenerReady)
{
    testListenerReady(false, 0);
}

TEST(CEPoll, LateListenerReady)
{
    testListenerReady(true, 0);
}

#if ENABLE_BONDING

TEST(CEPoll, EarlyGroupListenerReady_1)
{
    testListenerReady(false, 1);
}

TEST(CEPoll, LateGroupListenerReady_1)
{
    testListenerReady(true, 1);
}

TEST(CEPoll, EarlyGroupListenerReady_3)
{
    testListenerReady(false, 3);
}

TEST(CEPoll, LateGroupListenerReady_3)
{
    testListenerReady(true, 3);
}


void testMultipleListenerReady(const bool LATE_CALL)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr_in sa2;
    memset(&sa2, 0, sizeof sa2);
    sa2.sin_family = AF_INET;
    sa2.sin_port = htons(5556);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa2.sin_addr), 1);

    TestInit init;

    SRTSOCKET server_sock, server_sock2, caller_sock;
    server_sock = srt_create_socket();
    server_sock2 = srt_create_socket();

    caller_sock = srt_create_group(SRT_GTYPE_BROADCAST);
    int on = 1;
    EXPECT_NE(srt_setsockflag(server_sock, SRTO_GROUPCONNECT, &on, sizeof on), SRT_ERROR);
    EXPECT_NE(srt_setsockflag(server_sock2, SRTO_GROUPCONNECT, &on, sizeof on), SRT_ERROR);

    srt_bind(server_sock, (sockaddr*)& sa, sizeof(sa));
    srt_listen(server_sock, 3);
    srt::setopt(server_sock)[SRTO_RCVSYN] = false;

    srt_bind(server_sock2, (sockaddr*)& sa2, sizeof(sa2));
    srt_listen(server_sock2, 3);
    srt::setopt(server_sock2)[SRTO_RCVSYN] = false;

    // Ok, the listener socket is ready; now make a call, but
    // do not do anything on the listener socket yet.

    std::cout << "Using " << (LATE_CALL ? "LATE" : "EARLY") << " call\n";

    std::vector<std::future<int>> connect_res;

    if (LATE_CALL)
    {
        connect_res.push_back(std::async(std::launch::async, [&caller_sock, &sa]() {
            this_thread::sleep_for(chrono::milliseconds(1));
            return srt_connect(caller_sock, (sockaddr*)& sa, sizeof(sa));
        }));

        connect_res.push_back(std::async(std::launch::async, [&caller_sock, &sa2]() {
            this_thread::sleep_for(chrono::milliseconds(1));
            return srt_connect(caller_sock, (sockaddr*)& sa2, sizeof(sa2));
        }));


        std::cout << "STARTED connecting...\n";
    }

    std::cout << "Sleeping 1s...\n";
    this_thread::sleep_for(chrono::milliseconds(1000));

    // What is important is that the accepted socket is now reporting in
    // on the listener socket. So let's create an epoll.

    int eid = srt_epoll_create();
    int eid_postcheck = srt_epoll_create();

    // and add this listener to it
    int modes = SRT_EPOLL_IN;
    int modes_postcheck = SRT_EPOLL_IN | SRT_EPOLL_UPDATE;
    EXPECT_NE(srt_epoll_add_usock(eid, server_sock, &modes), SRT_ERROR);
    EXPECT_NE(srt_epoll_add_usock(eid, server_sock2, &modes), SRT_ERROR);
    EXPECT_NE(srt_epoll_add_usock(eid_postcheck, server_sock, &modes_postcheck), SRT_ERROR);
    EXPECT_NE(srt_epoll_add_usock(eid_postcheck, server_sock2, &modes_postcheck), SRT_ERROR);

    if (!LATE_CALL)
    {
        connect_res.push_back(std::async(std::launch::async, [&caller_sock, &sa]() {
            this_thread::sleep_for(chrono::milliseconds(1));
            return srt_connect(caller_sock, (sockaddr*)& sa, sizeof(sa));
        }));

        connect_res.push_back(std::async(std::launch::async, [&caller_sock, &sa2]() {
            this_thread::sleep_for(chrono::milliseconds(1));
            return srt_connect(caller_sock, (sockaddr*)& sa2, sizeof(sa2));
        }));

        std::cout << "STARTED connecting...\n";
    }

    // Sleep to make sure that the connection process has started.
    this_thread::sleep_for(chrono::milliseconds(100));

    std::cout << "Waiting for readiness on @" << server_sock << " and @" << server_sock2 << "\n";
    // And see now if the waiting accepted socket reports it.

    // This time we should expect that the connection reports in
    // on two listener sockets
    SRT_EPOLL_EVENT fdset[2] = {};
    std::ostringstream out;

    int nready = srt_epoll_uwait(eid, fdset, 2, 5000);
    EXPECT_EQ(nready, 2);
    out << "Ready socks:";
    for (int i = 0; i < nready; ++i)
    {
        out << " @" << fdset[i].fd;
        PrintEpollEvent(out, fdset[i].events);
    }
    out << std::endl;
    std::cout << out.str();

    std::cout << "Accepting...\n";
    sockaddr_in scl;
    int sclen = sizeof scl;

    // We choose the SECOND one to extract the group connection.
    SRTSOCKET sock = srt_accept(server_sock2, (sockaddr*)& scl, &sclen);
    EXPECT_NE(sock, SRT_INVALID_SOCK);

    // Make sure this time that the accepted connection is a group.
    EXPECT_EQ(sock & SRTGROUP_MASK, SRTGROUP_MASK);

    std::cout << "Check if there's still UPDATE pending\n";
    // Spawn yet another connection within the group, just to get the update
    auto extra_call = std::async(std::launch::async, [&caller_sock, &sa]() {
            return srt_connect(caller_sock, (sockaddr*)& sa, sizeof(sa));
            });
    // For 2+ members, additionally check if there AREN'T any
    // further acceptance members, but there are UPDATEs.
    // Note that if this was done AFTER accepting, the UPDATE would
    // be only set one one socket.
    nready = srt_epoll_uwait(eid_postcheck, fdset, 1, 5000);
    EXPECT_EQ(nready, 1);

    std::cout << "Ready socks:";
    for (int i = 0; i < nready; ++i)
    {
        std::cout << " @" << fdset[i].fd;
        PrintEpollEvent(std::cout, fdset[i].events);
    }
    std::cout << std::endl;

    // SUBSCRIBED EVENTS: IN, UPDATE.
    // expected: UPDATE only.
    EXPECT_EQ(SRT_EPOLL_OPT(fdset[0].events), SRT_EPOLL_UPDATE);
    EXPECT_NE(extra_call.get(), SRT_INVALID_SOCK);

    std::cout << "Joining connector thread(s)\n";
    for (size_t i = 0; i < connect_res.size(); ++i)
    {
        EXPECT_NE(connect_res[i].get(), SRT_INVALID_SOCK);
    }

    srt_epoll_release(eid);
    srt_epoll_release(eid_postcheck);

    srt_close(server_sock);
    srt_close(server_sock2);
    srt_close(caller_sock);
    srt_close(sock);
}

TEST(CEPoll, EarlyGroupMultiListenerReady)
{
    testMultipleListenerReady(false);
}

TEST(CEPoll, LateGroupMultiListenerReady)
{
    testMultipleListenerReady(true);
}



#endif


class TestEPoll: public srt::Test
{
protected:

    int m_client_pollid = int(SRT_ERROR);
    SRTSOCKET m_client_sock = SRT_INVALID_SOCK;

    void clientSocket()
    {
        int yes = 1;
        int no = 0;

        m_client_sock = srt_create_socket();
        ASSERT_NE(m_client_sock, SRT_INVALID_SOCK);

        ASSERT_NE(srt_setsockflag(m_client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
        ASSERT_NE(srt_setsockflag(m_client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);

        ASSERT_NE(srt_setsockflag(m_client_sock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

        int epoll_out = SRT_EPOLL_OUT;
        srt_epoll_add_usock(m_client_pollid, m_client_sock, &epoll_out);

        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(TEST_UDP_PORT);

        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

        sockaddr* psa = (sockaddr*)&sa;

        ASSERT_NE(srt_connect(m_client_sock, psa, sizeof sa), SRT_INVALID_SOCK);


        // Socket readiness for connection is checked by polling on WRITE allowed sockets.

        {
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            ASSERT_NE(srt_epoll_wait(m_client_pollid, read, &rlen,
                        write, &wlen,
                        -1, // -1 is set for debuging purpose.
                        // in case of production we need to set appropriate value
                        0, 0, 0, 0), int(SRT_ERROR));

            ASSERT_EQ(rlen, 0); // get exactly one write event without reads
            ASSERT_EQ(wlen, 1); // get exactly one write event without reads
            ASSERT_EQ(write[0], m_client_sock); // for our client socket
        }

        char buffer[1316] = {1, 2, 3, 4};
        ASSERT_NE(srt_sendmsg(m_client_sock, buffer, sizeof buffer,
                    -1, // infinit ttl
                    true // in order must be set to true
                    ),
                int(SRT_ERROR));

        // disable receiving OUT events
        int epoll_err = SRT_EPOLL_ERR;
        ASSERT_EQ(srt_epoll_update_usock(m_client_pollid, m_client_sock, &epoll_err), SRT_STATUS_OK);
        {
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            EXPECT_EQ(SRT_ERROR, srt_epoll_wait(m_client_pollid, read, &rlen,
                        write, &wlen,
                        1000,
                        0, 0, 0, 0));
            const int last_error = srt_getlasterror(NULL);
            EXPECT_EQ(last_error, (int)SRT_ETIMEOUT) << last_error;
        }
    }

    int m_server_pollid = int(SRT_ERROR);

    void createServerSocket(SRTSOCKET& w_servsock)
    {
        int yes = 1;
        int no = 0;

        SRTSOCKET servsock = srt_create_socket();
        ASSERT_NE(servsock, SRT_INVALID_SOCK);

        ASSERT_NE(srt_setsockflag(servsock, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
        ASSERT_NE(srt_setsockflag(servsock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

        int epoll_in = SRT_EPOLL_IN;
        srt_epoll_add_usock(m_server_pollid, servsock, &epoll_in);

        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(TEST_UDP_PORT);
        sa.sin_addr.s_addr = INADDR_ANY;
        sockaddr* psa = (sockaddr*)&sa;

        ASSERT_NE(srt_bind(servsock, psa, sizeof sa), SRT_ERROR);
        ASSERT_NE(srt_listen(servsock, SOMAXCONN), SRT_ERROR);

        w_servsock = servsock;
    }

    void runServer(SRTSOCKET servsock)
    {
        int epoll_in = SRT_EPOLL_IN;

        { // wait for connection from client
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            ASSERT_NE(srt_epoll_wait(m_server_pollid,
                        read,  &rlen,
                        write, &wlen,
                        -1, // -1 is set for debuging purpose.
                        // in case of production we need to set appropriate value
                        0, 0, 0, 0), int(SRT_ERROR));

            ASSERT_EQ(rlen, 1); // get exactly one read event without writes
            ASSERT_EQ(wlen, 0); // get exactly one read event without writes
            ASSERT_EQ(read[0], servsock); // read event is for bind socket
        }

        sockaddr_in scl;
        int sclen = sizeof scl;

        SRTSOCKET acpsock = srt_accept(servsock, (sockaddr*)&scl, &sclen);
        ASSERT_NE(acpsock, SRT_INVALID_SOCK);

        srt_epoll_add_usock(m_server_pollid, acpsock, &epoll_in); // wait for input

        { // wait for 1316 packet from client
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            ASSERT_NE(srt_epoll_wait(m_server_pollid,
                        read,  &rlen,
                        write, &wlen,
                        -1, // -1 is set for debuging purpose.
                        // in case of production we need to set appropriate value
                        0, 0, 0, 0), int(SRT_ERROR));

            ASSERT_EQ(rlen, 1); // get exactly one read event without writes
            ASSERT_EQ(wlen, 0); // get exactly one read event without writes
            ASSERT_EQ(read[0], acpsock); // read event is for bind socket
        }

        char buffer[1316];
        ASSERT_EQ(srt_recvmsg(acpsock, buffer, sizeof buffer), 1316);

        char pattern[4] = {1, 2, 3, 4};
        EXPECT_TRUE(std::mismatch(pattern, pattern+4, buffer).first == pattern+4);

        std::cout << "serverSocket waiting..." << std::endl;
        {
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            ASSERT_EQ(-1, srt_epoll_wait(m_server_pollid,
                        read,  &rlen,
                        write, &wlen,
                        2000,
                        0, 0, 0, 0));
            const int last_error = srt_getlasterror(NULL);
            ASSERT_EQ(SRT_ETIMEOUT, last_error) << last_error;
        }
        std::cout << "serverSocket finished waiting" << std::endl;

        srt_close(acpsock);
        srt_close(servsock);
    }

    void setup() override
    {
        m_client_pollid = srt_epoll_create();
        ASSERT_NE(m_client_pollid, int(SRT_ERROR));

        m_server_pollid = srt_epoll_create();
        ASSERT_NE(m_server_pollid, int(SRT_ERROR));

    }

    void teardown() override
    {
        (void)srt_epoll_release(m_client_pollid);
        (void)srt_epoll_release(m_server_pollid);
    }
};


TEST_F(TestEPoll, SimpleAsync)
{
    srt::UniqueSocket ss;
    createServerSocket( (ss.ref()) );

    std::thread client([this] { clientSocket(); });

    runServer(ss);

    client.join(); // Make sure client has exit before you delete the socket

    srt_close(m_client_sock); // cannot close m_client_sock after srt_sendmsg because of issue in api.c:2346 
}

