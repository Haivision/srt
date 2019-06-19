#include <chrono>
#include <future>
#include <thread>
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

    try
    {
        EXPECT_EQ(srt_epoll_release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        EXPECT_EQ(0, 1);
    }

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

    try
    {
        EXPECT_EQ(srt_epoll_release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        EXPECT_EQ(0, 1);
    }
    EXPECT_EQ(srt_cleanup(), 0);
}


TEST(CEPoll, WaitEmptyCall)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
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

    ASSERT_EQ(srt_epoll_wait(epoll_id, 0, NULL, 0, NULL,
        -1, 0, 0, 0, 0), SRT_ERROR);

    try
    {
        EXPECT_EQ(srt_epoll_release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        EXPECT_EQ(0, 1);
    }
    EXPECT_EQ(srt_cleanup(), 0);
}


TEST(CEPoll, WaitAllSocketsInEpollReleased)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
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

    try
    {
        EXPECT_EQ(srt_epoll_release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        EXPECT_EQ(0, 1);
    }

    EXPECT_EQ(srt_cleanup(), 0);
}


TEST(CEPoll, WrongEepoll_idOnAddUSock)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
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
    /* We intentionally pass the wrong socket ID. The error should be returned.*/
    ASSERT_EQ(srt_epoll_add_usock(epoll_id + 1, client_sock, &epoll_out), SRT_ERROR);

    try
    {
        EXPECT_EQ(srt_epoll_release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        EXPECT_EQ(0, 1);
    }

    EXPECT_EQ(srt_cleanup(), 0);
}


TEST(CEPoll, HandleEpollEvent)
{
    ASSERT_EQ(srt_startup(), 0);

    SRTSOCKET client_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
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
        EXPECT_EQ(0, 1);
    }

    try
    {
        EXPECT_EQ(epoll.release(epoll_id), 0);
    }
    catch (CUDTException &ex)
    {
        cerr << ex.getErrorMessage() << endl;
        EXPECT_EQ(0, 1);
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
    SRTSOCKET client_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(client_sock, SRT_ERROR);

    const int yes = 1;
    const int no = 0;
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
    SRTSOCKET server_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
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
        this_thread::sleep_for(chrono::seconds(1));
        cout << "Closing client connection\n";
        return srt_close(client_sock);
        });

    int timeout_ms = -1;
    int ready[2] = { SRT_INVALID_SOCK, SRT_INVALID_SOCK };
    int len = 2;
    const int epoll_wait_res = srt_epoll_wait(epoll_io, ready, &len, nullptr, nullptr, timeout_ms, 0, 0, 0, 0);
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

