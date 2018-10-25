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


TEST(SRT, ConnectionTimeoutTest) {
    ASSERT_EQ(srt_startup(), 0);

    const int yes = 1;
    const int no = 0;

    SRTSOCKET m_client_sock = srt_socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(m_client_sock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(m_client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
    ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    const int pollid = srt_epoll_create();
    ASSERT_GE(pollid, 0);
    const int epoll_out = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    ASSERT_NE(srt_epoll_add_usock(pollid, m_client_sock, &epoll_out), SRT_ERROR);

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);

    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr* psa = (sockaddr*)&sa;

    ASSERT_NE(srt_connect(m_client_sock, psa, sizeof sa), SRT_ERROR);


    // Socket readiness for connection is checked by polling on WRITE allowed sockets.

    {
        int rlen = 2;
        SRTSOCKET read[2];

        int wlen = 2;
        SRTSOCKET write[2];

        ASSERT_NE(srt_epoll_wait(pollid, read, &rlen,
                                 write, &wlen,
                                 -1,
                                 0, 0, 0, 0), SRT_ERROR);

        ASSERT_EQ(rlen, 1);
        ASSERT_EQ(read[0], m_client_sock);

    }

    ASSERT_NE(srt_epoll_remove_usock(pollid, m_client_sock), SRT_ERROR);
    ASSERT_NE(srt_close(m_client_sock), SRT_ERROR);
    (void)srt_epoll_release(pollid);
    (void)srt_cleanup();
}
