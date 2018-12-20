#include <gtest/gtest.h>

#ifdef _WIN32
#define _WINSOCKAPI_ // to include Winsock2.h instead of Winsock.h from windows.h
#include <winsock2.h>

#if defined(__GNUC__) || defined(__MINGW32__)
extern "C" {
    WINSOCK_API_LINKAGE  INT WSAAPI inet_pton( INT Family, PCSTR pszAddrString, PVOID pAddrBuf);
    WINSOCK_API_LINKAGE  PCSTR WSAAPI inet_ntop(INT  Family, PVOID pAddr, PSTR pStringBuf, size_t StringBufSize);
}
#endif

#define INC__WIN_WINTIME // exclude gettimeofday from srt headers
#endif

#include "srt.h"


TEST(Core, ConnectionTimeout) {
    ASSERT_EQ(srt_startup(), 0);

    const SRTSOCKET client_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GT(client_sock, 0);    // socket_id should be > 0

    // First let's check the default connection timeout value.
    // It should be 3 seconds (3000 ms)
    int conn_timeout     = 0;
    int conn_timeout_len = sizeof conn_timeout;
    EXPECT_EQ(srt_getsockopt(client_sock, 0, SRTO_CONNTIMEO, &conn_timeout, &conn_timeout_len), SRT_SUCCESS);
    EXPECT_EQ(conn_timeout, 3000);

    // Set connection timeout to 500 ms to reduce the test execution time
    const int connection_timeout_ms = 500;
    EXPECT_EQ(srt_setsockopt(client_sock, 0, SRTO_CONNTIMEO, &connection_timeout_ms, sizeof connection_timeout_ms), SRT_SUCCESS);

    const int yes = 1;
    const int no = 0;
    ASSERT_EQ(srt_setsockopt(client_sock, 0, SRTO_RCVSYN,    &no,  sizeof no),  SRT_SUCCESS); // for async connect
    ASSERT_EQ(srt_setsockopt(client_sock, 0, SRTO_SNDSYN,    &no,  sizeof no),  SRT_SUCCESS); // for async connect
    ASSERT_EQ(srt_setsockopt(client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_SUCCESS);
    ASSERT_EQ(srt_setsockflag(client_sock,   SRTO_SENDER,    &yes, sizeof yes), SRT_SUCCESS);

    const int pollid = srt_epoll_create();
    ASSERT_GE(pollid, 0);
    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(pollid, client_sock, &epoll_out), SRT_ERROR);

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);

    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr* psa = (sockaddr*)&sa;
    ASSERT_NE(srt_connect(client_sock, psa, sizeof sa), SRT_ERROR);

    // Socket readiness for connection is checked by polling on WRITE allowed sockets.
    {
        int rlen = 2;
        SRTSOCKET read[2];

        int wlen = 2;
        SRTSOCKET write[2];

        // Here we check the connection timeout.
        // Epoll timeout is set 100 ms greater than socket's TTL
        EXPECT_EQ(srt_epoll_wait(pollid, read, &rlen,
                                 write, &wlen,
                                 connection_timeout_ms + 100,   // +100 ms
                                 0, 0, 0, 0)
        /* Expected return value is 2. We have only 1 socket, but
         * sockets with exceptions are returned to both read and write sets.
        */
                 , 2);

        EXPECT_EQ(rlen, 1);
        EXPECT_EQ(read[0], client_sock);
        EXPECT_EQ(wlen, 1);
        EXPECT_EQ(write[0], client_sock);
    }

    EXPECT_EQ(srt_epoll_remove_usock(pollid, client_sock), SRT_SUCCESS);
    EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);
    (void)srt_epoll_release(pollid);
    (void)srt_cleanup();
}
