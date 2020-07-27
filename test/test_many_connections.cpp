#define _CRT_RAND_S // For Windows, rand_s 

#include <gtest/gtest.h>
#include <chrono>
#include <future>

#ifdef _WIN32
#include <stdlib.h>
#define rand_r rand_s
#define INC_SRT_WIN_WINTIME // exclude gettimeofday from srt headers
#else
typedef int SOCKET;
#define INVALID_SOCKET ((SOCKET)-1)
#define closesocket close
#endif

#include"platform_sys.h"
#include "srt.h"
#include "netinet_any.h"
#include "api.h"

using namespace std;


class TestConnection
    : public ::testing::Test
{
protected:
    TestConnection()
    {
        // initialization code here
    }

    ~TestConnection()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

    // It should be as much as possible, but how many sockets can
    // be withstood, depends on the platform. Currently used CI test
    // servers seem not to withstand more than 240.
    static const size_t NSOCK = 200;

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

        // Close the socket to free the port.
        ASSERT_NE(closesocket(m_udp_sock), -1);
        ASSERT_GE(bind_res, 0);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &m_sa.sin_addr), 1);

        // Fill the buffer with random data
        time_t now;
        time(&now);
        unsigned int randseed = now % 255;
        srand(randseed);
        for (int i = 0; i < SRT_LIVE_DEF_PLSIZE; ++i)
            buf[i] = rand_r(&randseed) % 255;

        m_server_sock = srt_create_socket();

        ASSERT_NE(srt_bind(m_server_sock, psa, sizeof m_sa), -1);
        ASSERT_NE(srt_listen(m_server_sock, NSOCK), -1);
    }

    void TearDown() override
    {
        srt_cleanup();
    }

    void AcceptLoop()
    {
        //cerr << "[T] Accepting connections\n";
        for (;;)
        {
            sockaddr_any addr;
            int len = sizeof addr;
            int acp = srt_accept(m_server_sock, addr.get(), &len);
            if (acp == -1)
            {
                cerr << "[T] Accept error: " << srt_getlasterror_str();
                break;
            }
            //cerr << "[T] Got new acp @" << acp << endl;
            m_accepted.push_back(acp);
        }

        m_accept_exit = true;

        for (auto s: m_accepted)
        {
            srt_close(s);
        }
    }

protected:
    SOCKET m_udp_sock = INVALID_SOCKET;
    sockaddr_in m_sa = sockaddr_in();
    SRTSOCKET m_server_sock = SRT_INVALID_SOCK;
    vector<SRTSOCKET> m_accepted;
    char buf[SRT_LIVE_DEF_PLSIZE];
    SRTSOCKET srt_socket_list[NSOCK];
    volatile bool m_accept_exit = false;
};



TEST_F(TestConnection, Multiple)
{
    size_t size = SRT_LIVE_DEF_PLSIZE;

    sockaddr_in lsa = m_sa;

    const sockaddr* psa = reinterpret_cast<const sockaddr*>(&lsa);

    auto ex = std::async([this] { return AcceptLoop(); });

    cout << "Opening " << NSOCK << " connections\n";

    for (size_t i = 0; i < NSOCK; i++)
    {
        srt_socket_list[i] = srt_create_socket();

        // Give it 60s timeout, many platforms fail to process
        // so many connections in a short time.
        int conntimeo = 60;
        srt_setsockflag(srt_socket_list[i], SRTO_CONNTIMEO, &conntimeo, sizeof conntimeo);

        //cout << "Connecting #" << i << " to " << SockaddrToString(sockaddr_any(psa)) << "...\n";

        //cerr << "Connecting to: " << SockaddrToString(sockaddr_any(psa)) << endl;
        ASSERT_NE(srt_connect(srt_socket_list[i], psa, sizeof lsa), SRT_ERROR);

        // Set now async sending so that sending isn't blocked
        int no = 0;
        ASSERT_NE(srt_setsockflag(srt_socket_list[i], SRTO_SNDSYN, &no, sizeof no), -1);
    }

    for (size_t j = 1; j <= 100; j++)
    {
        for (size_t i = 0; i < NSOCK; i++)
        {
            EXPECT_GT(srt_send(srt_socket_list[i], buf, size), 0);
        }
    }
    cout << "Sending finished, closing caller sockets\n";

    for (size_t i = 0; i < NSOCK; i++)
    {
        EXPECT_EQ(srt_close(srt_socket_list[i]), SRT_SUCCESS);
    }
    // Up to this moment the server sock should survive
    cout << "Closing server socket\n";

    EXPECT_EQ(m_accept_exit, false);

    // Close server socket to break the accept loop
    EXPECT_EQ(srt_close(m_server_sock), 0);

    ex.wait();
}



