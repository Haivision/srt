#include <gtest/gtest.h>
#include <chrono>

#ifdef _WIN32
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#else
typedef int SOCKET;
#define INVALID_SOCKET ((SOCKET)-1)
#define closesocket close
#endif

#include"platform_sys.h"
#include "srt.h"

using namespace std;


class TestConnectionTimeout
    : public ::testing::Test
{
protected:
    TestConnectionTimeout()
    {
        // initialization code here
    }

    ~TestConnectionTimeout()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:

    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        ASSERT_EQ(srt_startup(), 0);

        m_sa.sin_family = AF_INET;
        m_sa.sin_addr.s_addr = INADDR_ANY;
        m_udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ASSERT_NE(m_udp_sock, -1);

        // Find unused a port not used by any other service.
        // Otherwise srt_connect may actually connect.
        int bind_res = -1;
        const sockaddr* psa = reinterpret_cast<const sockaddr*>(&m_sa);
        for (int port = 5000; port <= 5555; ++port)
        {
            m_sa.sin_port = htons(port);
            bind_res = ::bind(m_udp_sock, psa, sizeof m_sa);
            if (bind_res >= 0)
            {
                cerr << "Running test on port " << port << "\n";
                break;
            }
        }

        ASSERT_GE(bind_res, 0);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &m_sa.sin_addr), 1);
    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        ASSERT_NE(closesocket(m_udp_sock), -1);
        srt_cleanup();
    }

protected:

    SOCKET m_udp_sock = INVALID_SOCKET;
    sockaddr_in m_sa = sockaddr_in();

};



/**
 * The test creates a socket and tries to connect to a localhost port 5555
 * in a non-blocking mode. This means we wait on epoll for a notification
 * about SRT_EPOLL_OUT | SRT_EPOLL_ERR events on the socket calling srt_epoll_wait(...).
 * The test expects a connection timeout to happen within the time,
 * set with SRTO_CONNTIMEO (500 ms).
 * The expected behavior is to return from srt_epoll_wait(...)
 *
 * @remarks  Inspired by Max Tomilov (maxtomilov) in issue #468
*/
TEST_F(TestConnectionTimeout, Nonblocking) {

    const SRTSOCKET client_sock = srt_create_socket();
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

    const sockaddr* psa = reinterpret_cast<const sockaddr*>(&m_sa);
    ASSERT_NE(srt_connect(client_sock, psa, sizeof m_sa), SRT_ERROR);

    // Socket readiness for connection is checked by polling on WRITE allowed sockets.
    {
        int rlen = 2;
        SRTSOCKET read[2];

        int wlen = 2;
        SRTSOCKET write[2];

        const chrono::steady_clock::time_point chrono_ts_start = chrono::steady_clock::now();

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
        // Check the actual timeout
        const chrono::steady_clock::time_point chrono_ts_end = chrono::steady_clock::now();
        const auto delta_ms = chrono::duration_cast<chrono::milliseconds>(chrono_ts_end - chrono_ts_start).count();
        // Confidence interval border : +/-80 ms
        EXPECT_LE(delta_ms, connection_timeout_ms + 80) << "Timeout was: " << delta_ms;
        EXPECT_GE(delta_ms, connection_timeout_ms - 80) << "Timeout was: " << delta_ms;

        EXPECT_EQ(rlen, 1);
        EXPECT_EQ(read[0], client_sock);
        EXPECT_EQ(wlen, 1);
        EXPECT_EQ(write[0], client_sock);
    }

    EXPECT_EQ(srt_epoll_remove_usock(pollid, client_sock), SRT_SUCCESS);
    EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);
    (void)srt_epoll_release(pollid);
}

/**
 * The test creates a socket and tries to connect to a localhost port 5555
 * in a blocking mode. The srt_connect function is expected to return
 * SRT_ERROR, and the error_code should be SRT_ENOSERVER, meaning a
 * connection timeout.
 * This test is a regression test for an issue described in PR #833.
 * Under certain conditions m_bConnecting variable on a socket
 * might not be reset to false after a connection attempt has failed.
 * In that case any further call to srt_connect will return SRT_ECONNSOCK:
 * Operation not supported: Cannot do this operation on a CONNECTED socket
 *
*/
TEST_F(TestConnectionTimeout, BlockingLoop)
{
    const SRTSOCKET client_sock = srt_create_socket();
    ASSERT_GT(client_sock, 0);    // socket_id should be > 0

    // Set connection timeout to 999 ms to reduce the test execution time.
    // Also need to hit a time point between two threads:
    // srt_connect will check TTL every second,
    // CRcvQueue::worker will wait on a socket for 10 ms.
    // Need to have a condition, when srt_connect will process the timeout.
    const int connection_timeout_ms = 999;
    EXPECT_EQ(srt_setsockopt(client_sock, 0, SRTO_CONNTIMEO, &connection_timeout_ms, sizeof connection_timeout_ms), SRT_SUCCESS);

    const sockaddr* psa = reinterpret_cast<const sockaddr*>(&m_sa);
    for (int i = 0; i < 10; ++i)
    {
        const chrono::steady_clock::time_point chrono_ts_start = chrono::steady_clock::now();
        EXPECT_EQ(srt_connect(client_sock, psa, sizeof m_sa), SRT_ERROR);

        const auto delta_ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - chrono_ts_start).count();
        // Confidence interval border : +/-200 ms
        EXPECT_LE(delta_ms, connection_timeout_ms + 200) << "Timeout was: " << delta_ms;
        EXPECT_GE(delta_ms, connection_timeout_ms - 200) << "Timeout was: " << delta_ms;

        const int error_code = srt_getlasterror(nullptr);
        EXPECT_EQ(error_code, SRT_ENOSERVER);
        if (error_code != SRT_ENOSERVER)
        {
            cerr << "Connection attempt no. " << i << " resulted with: "
                << error_code << " " << srt_getlasterror_str() << "\n";
            break;
        }
    }

    EXPECT_EQ(srt_close(client_sock), SRT_SUCCESS);
}


