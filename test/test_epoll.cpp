#include <iostream>
#include <chrono>
#include <future>
#include <thread>
#include <condition_variable>
#include "gtest/gtest.h"
#include "api.h"
#include "epoll.h"


using namespace std;


TEST(CEPoll, InfiniteWait)
{
    ASSERT_EQ(srt_startup(), 0);

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    ASSERT_EQ(srt_epoll_wait(epoll_id, nullptr, nullptr,
        nullptr, nullptr,
        -1,
        0, 0, 0, 0), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);

    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, WaitNoSocketsInEpoll)
{
    ASSERT_EQ(srt_startup(), 0);

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    int rlen = 2;
    SRTSOCKET read[2];

    int wlen = 2;
    SRTSOCKET write[2];

    ASSERT_EQ(srt_epoll_wait(epoll_id, read, &rlen, write, &wlen,
        -1, 0, 0, 0, 0), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);

    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, WaitNoSocketsInEpoll2)
{
    ASSERT_EQ(srt_startup(), 0);

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    SRT_EPOLL_EVENT events[2];

    ASSERT_EQ(srt_epoll_uwait(epoll_id, events, 2, -1), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);

    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, WaitEmptyCall)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    ASSERT_NE(client_sock, SRT_ERROR);

    const int no = 0;
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

    ASSERT_EQ(srt_epoll_wait(epoll_id, 0, NULL, 0, NULL,
                -1, 0, 0, 0, 0), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);
    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, UWaitEmptyCall)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    ASSERT_NE(client_sock, SRT_ERROR);

    const int no = 0;
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

    ASSERT_EQ(srt_epoll_uwait(epoll_id, NULL, 10, -1), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);

    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, WaitAllSocketsInEpollReleased)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    ASSERT_NE(client_sock, SRT_ERROR);

    const int yes = 1;
    const int no = 0;
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

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
        -1, 0, 0, 0, 0), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);

    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, WaitAllSocketsInEpollReleased2)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    ASSERT_NE(client_sock, SRT_ERROR);

    const int yes = 1;
    const int no = 0;
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);
    ASSERT_NE(srt_epoll_remove_usock(epoll_id, client_sock), SRT_ERROR);

    SRT_EPOLL_EVENT events[2];

    ASSERT_EQ(srt_epoll_uwait(epoll_id, events, 2, -1), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);

    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, WrongEpoll_idOnAddUSock)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    ASSERT_NE(client_sock, SRT_ERROR);

    const int no  = 0;
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int epoll_id = srt_epoll_create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    /* We intentionally pass the wrong socket ID. The error should be returned.*/
    ASSERT_EQ(srt_epoll_add_usock(epoll_id + 1, client_sock, &epoll_out), SRT_ERROR);

    EXPECT_EQ(srt_epoll_release(epoll_id), 0);

    EXPECT_EQ(srt_cleanup(), 0);
}


TEST(CEPoll, HandleEpollEvent)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    EXPECT_NE(client_sock, SRT_ERROR);

    const int yes = 1;
    const int no  = 0;
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock,    SRTO_SENDER,    &yes, sizeof yes), SRT_ERROR);
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    CEPoll epoll;
    const int epoll_id = epoll.create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(epoll.add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

    set<int> epoll_ids = { epoll_id };

    epoll.update_events(client_sock, epoll_ids, SRT_EPOLL_ERR, true);

    set<SRTSOCKET> readset;
    set<SRTSOCKET> writeset;
    set<SRTSOCKET>* rval = &readset;
    set<SRTSOCKET>* wval = &writeset;

    ASSERT_NE(epoll.wait(epoll_id, rval, wval, -1, nullptr, nullptr), SRT_ERROR);

    try
    {
        EXPECT_EQ(epoll.remove_usock(epoll_id, client_sock), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        EXPECT_EQ(epoll.release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    EXPECT_EQ(srt_cleanup(), 0);
}



// In this test case a caller connects to a listener on a localhost.
// Then the caller closes the connection, and listener is expected to
// be notified about connection break via polling the accepted socket.
TEST(CEPoll, NotifyConnectionBreak)
{
    ASSERT_EQ(srt_startup(), 0);

    // 1. Prepare client
    SRTSOCKET client_sock = srt_create_socket();
    ASSERT_NE(client_sock, SRT_ERROR);

    const int yes SRT_ATR_UNUSED = 1;
    const int no SRT_ATR_UNUSED = 0;
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

    const int client_epoll_id = srt_epoll_create();
    ASSERT_GE(client_epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    /* We intentionally pass the wrong socket ID. The error should be returned.*/
    EXPECT_EQ(srt_epoll_add_usock(client_epoll_id, client_sock, &epoll_out), SRT_SUCCESS);

    sockaddr_in sa_client;
    memset(&sa_client, 0, sizeof sa_client);
    sa_client.sin_family = AF_INET;
    sa_client.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa_client.sin_addr), 1);

    // 2. Prepare server
    SRTSOCKET server_sock = srt_create_socket();
    ASSERT_NE(server_sock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(server_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(server_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect

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
    if (epoll_res == SRT_ERROR)
    {
        std::cerr << "Epoll returned error: " << srt_getlasterror_str() << " (code " << srt_getlasterror(NULL) << ")\n";
    }

    // Wait for the caller connection thread to return connection result
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    sockaddr_in scl;
    int sclen = sizeof scl;
    SRTSOCKET sock = srt_accept(server_sock, (sockaddr*)& scl, &sclen);
    EXPECT_NE(sock, SRT_INVALID_SOCK);

    int epoll_io = srt_epoll_create();
    int modes = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    EXPECT_NE(srt_epoll_add_usock(epoll_io, sock, &modes), SRT_ERROR);

    // The caller will close connection after 1 second
    auto close_res = std::async(std::launch::async, [&client_sock]() {
        cout << "TEST(async call): WILL CLOSE client connection in 3s\n";
        this_thread::sleep_for(chrono::seconds(1));
        cout << "TEST(async call): Closing client connection\n";
        return srt_close(client_sock);
        });

    int timeout_ms = -1;
    int ready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
    int len = 2;
    cout << "TEST: entering INFINITE WAIT\n";
    const int epoll_wait_res = srt_epoll_wait(epoll_io, ready, &len, nullptr, nullptr, timeout_ms, 0, 0, 0, 0);
    cout << "TEST: return from INFINITE WAIT\n";
    if (epoll_wait_res == SRT_ERROR)
        cerr << "socket::read::epoll " << to_string(srt_getlasterror(nullptr));
    EXPECT_EQ(epoll_wait_res, 1);
    EXPECT_EQ(len, 1);
    EXPECT_EQ(ready[0], sock);

    // Wait for the caller to close connection
    // There should be no wait, as epoll should wait untill connection is closed.
    EXPECT_EQ(close_res.get(), SRT_SUCCESS);
    const SRT_SOCKSTATUS state = srt_getsockstate(sock);
    const bool state_valid = state == SRTS_BROKEN || state == SRTS_CLOSING || state == SRTS_CLOSED;
    EXPECT_TRUE(state_valid);
    if (!state_valid)
        cerr << "socket state: " << state << endl;

    EXPECT_EQ(srt_cleanup(), 0);
}


TEST(CEPoll, HandleEpollEvent2)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    EXPECT_NE(client_sock, SRT_ERROR);

    const int yes = 1;
    const int no  = 0;
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock,    SRTO_SENDER,    &yes, sizeof yes), SRT_ERROR);
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    CEPoll epoll;
    const int epoll_id = epoll.create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR | SRT_EPOLL_ET;
    ASSERT_NE(epoll.add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

    set<int> epoll_ids = { epoll_id };

    epoll.update_events(client_sock, epoll_ids, SRT_EPOLL_ERR, true);

    SRT_EPOLL_EVENT fds[1024];

    int result = epoll.uwait(epoll_id, fds, 1024, -1);
    ASSERT_EQ(result, 1); 
    ASSERT_EQ(fds[0].events, SRT_EPOLL_ERR);

    // Edge-triggered means that after one wait call was done, the next
    // call to this event should no longer report it. Now use timeout 0
    // to return immediately.
    result = epoll.uwait(epoll_id, fds, 1024, 0);
    ASSERT_EQ(result, 0);

    try
    {
        EXPECT_EQ(epoll.remove_usock(epoll_id, client_sock), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        EXPECT_EQ(epoll.release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    EXPECT_EQ(srt_cleanup(), 0);
}


TEST(CEPoll, HandleEpollNoEvent)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    EXPECT_NE(client_sock, SRT_ERROR);

    const int yes = 1;
    const int no  = 0;
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockflag(client_sock,    SRTO_SENDER,    &yes, sizeof yes), SRT_ERROR);
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    CEPoll epoll;
    const int epoll_id = epoll.create();
    ASSERT_GE(epoll_id, 0);

    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(epoll.add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

    SRT_EPOLL_EVENT fds[1024];

    // Use timeout 0 because with -1 this call would hang up
    int result = epoll.uwait(epoll_id, fds, 1024, 0);
    ASSERT_EQ(result, 0); 

    try
    {
        EXPECT_EQ(epoll.remove_usock(epoll_id, client_sock), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        EXPECT_EQ(epoll.release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    EXPECT_EQ(srt_cleanup(), 0);
}

TEST(CEPoll, ThreadedUpdate)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_create_socket();
    EXPECT_NE(client_sock, SRT_ERROR);

    const int no  = 0;
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_RCVSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockopt (client_sock, 0, SRTO_SNDSYN,    &no,  sizeof no),  SRT_ERROR); // for async connect

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
        ASSERT_NE(epoll.add_usock(epoll_id, client_sock, &epoll_out), SRT_ERROR);

        set<int> epoll_ids = { epoll_id };

        epoll.update_events(client_sock, epoll_ids, SRT_EPOLL_ERR, true);
        cerr << "THREAD END\n";
    });

    SRT_EPOLL_EVENT fds[1024];

    cerr << "Entering infinite-wait by uwait:\n";

    int result = epoll.uwait(epoll_id, fds, 1024, -1);
    cerr << "Exit no longer infinite-wait by uwait, result=" << result << "\n";
    ASSERT_EQ(result, 1); 
    ASSERT_EQ(fds[0].events, SRT_EPOLL_ERR);

    cerr << "THREAD JOIN...\n";
    td.join();
    cerr << "...JOINED\n";

    try
    {
        EXPECT_EQ(epoll.remove_usock(epoll_id, client_sock), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }

    try
    {
        EXPECT_EQ(epoll.release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        throw;
    }


    EXPECT_EQ(srt_cleanup(), 0);
}


class TestEPoll: public testing::Test
{
protected:

    int m_client_pollid = SRT_ERROR;
    SRTSOCKET m_client_sock = SRT_ERROR;

    void clientSocket()
    {
        int yes = 1;
        int no = 0;

        m_client_sock = srt_create_socket();
        ASSERT_NE(m_client_sock, SRT_ERROR);

        ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
        ASSERT_NE(srt_setsockflag(m_client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);

        ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

        int epoll_out = SRT_EPOLL_OUT;
        srt_epoll_add_usock(m_client_pollid, m_client_sock, &epoll_out);

        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(9999);

        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

        sockaddr* psa = (sockaddr*)&sa;

        ASSERT_NE(srt_connect(m_client_sock, psa, sizeof sa), SRT_ERROR);


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
                        0, 0, 0, 0), SRT_ERROR);

            ASSERT_EQ(rlen, 0); // get exactly one write event without reads
            ASSERT_EQ(wlen, 1); // get exactly one write event without reads
            ASSERT_EQ(write[0], m_client_sock); // for our client socket
        }

        char buffer[1316] = {1, 2, 3, 4};
        ASSERT_NE(srt_sendmsg(m_client_sock, buffer, sizeof buffer,
                    -1, // infinit ttl
                    true // in order must be set to true
                    ),
                SRT_ERROR);

        // disable receiving OUT events
        int epoll_err = SRT_EPOLL_ERR;
        ASSERT_EQ(0, srt_epoll_update_usock(m_client_pollid, m_client_sock, &epoll_err));
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
            EXPECT_EQ(SRT_ETIMEOUT, last_error) << last_error;
        }
    }

    int m_server_pollid = SRT_ERROR;

    void createServerSocket(SRTSOCKET& w_servsock)
    {
        int yes = 1;
        int no = 0;

        SRTSOCKET servsock = srt_create_socket();
        ASSERT_NE(servsock, SRT_ERROR);

        ASSERT_NE(srt_setsockopt(servsock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
        ASSERT_NE(srt_setsockopt(servsock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

        int epoll_in = SRT_EPOLL_IN;
        srt_epoll_add_usock(m_server_pollid, servsock, &epoll_in);

        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(9999);
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
                        0, 0, 0, 0), SRT_ERROR );

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
                        0, 0, 0, 0), SRT_ERROR );

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

    void SetUp() override
    {
        ASSERT_EQ(srt_startup(), 0);

        m_client_pollid = srt_epoll_create();
        ASSERT_NE(SRT_ERROR, m_client_pollid);

        m_server_pollid = srt_epoll_create();
        ASSERT_NE(SRT_ERROR, m_server_pollid);

    }

    void TearDown() override
    {
        (void)srt_epoll_release(m_client_pollid);
        (void)srt_epoll_release(m_server_pollid);
        srt_cleanup();
    }
};


TEST_F(TestEPoll, SimpleAsync)
{
    SRTSOCKET ss = SRT_INVALID_SOCK;
    createServerSocket( (ss) );

    std::thread client([this] { clientSocket(); });

    runServer(ss);

    client.join(); // Make sure client has exit before you delete the socket

    srt_close(m_client_sock); // cannot close m_client_sock after srt_sendmsg because of issue in api.c:2346 
}

