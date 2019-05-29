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
