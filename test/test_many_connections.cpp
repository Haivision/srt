#define _CRT_RAND_S // For Windows, rand_s 

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <future>
#include <random>

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
using srt::sockaddr_any;


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
    static const size_t NSOCK = 60;

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        ASSERT_EQ(srt_startup(), 0);

        m_sa.sin_family = AF_INET;
        m_sa.sin_addr.s_addr = INADDR_ANY;

        m_server_sock = srt_create_socket();
        ASSERT_NE(m_server_sock, SRT_INVALID_SOCK);

        // Find a port not used by another service.
        int bind_res = 0;
        const sockaddr* psa = reinterpret_cast<const sockaddr*>(&m_sa);
        for (int port = 5000; port <= 5100; ++port)
        {
            m_sa.sin_port = htons(port);

            bind_res = srt_bind(m_server_sock, psa, sizeof m_sa);
            if (bind_res == 0)
            {
                cerr << "Running test on port " << port << "\n";
                break;
            }
        }

        ASSERT_GE(bind_res, 0) << "srt_bind returned " << bind_res << ": " << srt_getlasterror_str();
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &m_sa.sin_addr), 1);

        // Fill the buffer with random data
        std::random_device rnd_device;
        std::mt19937 gen(rnd_device());
        std::uniform_int_distribution<short> dis(-128, 127);
        std::generate(m_buf.begin(), m_buf.end(), [dis, gen]() mutable { return (char)dis(gen); });

        ASSERT_NE(srt_listen(m_server_sock, NSOCK), -1);
    }

    void TearDown() override
    {
        srt_cleanup();
    }

    void AcceptLoop()
    {
        for (;;)
        {
            sockaddr_any addr;
            int len = sizeof addr;
            int acp = srt_accept(m_server_sock, addr.get(), &len);
            if (acp == -1)
            {
                cerr << "[T] Accept error at " << m_accepted.size()
                     << "/" << NSOCK << ": " << srt_getlasterror_str() << endl;
                break;
            }
            m_accepted.push_back(acp);
        }

        cerr << "[T] Closing those accepted ones\n";
        m_accept_exit = true;

        for (const auto s : m_accepted)
        {
            srt_close(s);
        }

        cerr << "[T] End Accept Loop\n";
    }

protected:
    sockaddr_in m_sa = sockaddr_in();
    SRTSOCKET m_server_sock = SRT_INVALID_SOCK;
    vector<SRTSOCKET> m_accepted;
    std::array<char, SRT_LIVE_DEF_PLSIZE> m_buf;
    SRTSOCKET m_connections[NSOCK];
    volatile bool m_accept_exit = false;
};


// This test establishes multiple connections to a single SRT listener on a localhost port.
// Packets are submitted for sending to all those connections in a non-blocking mode.
// Then all connections are closed. Some sockets may potentially still have undelivered packets.
// This test tries to reproduce the issue described in #1182, and fixed by #1315.
TEST_F(TestConnection, Multiple)
{
    const sockaddr_in lsa = m_sa;
    const sockaddr* psa = reinterpret_cast<const sockaddr*>(&lsa);

    auto ex = std::async(std::launch::async, [this] { return AcceptLoop(); });

    cerr << "Opening " << NSOCK << " connections\n";

    for (size_t i = 0; i < NSOCK; i++)
    {
        m_connections[i] = srt_create_socket();
        EXPECT_NE(m_connections[i], SRT_INVALID_SOCK);

        // Give it 60s timeout, many platforms fail to process
        // so many connections in a short time.
        int conntimeo = 60;
        srt_setsockflag(m_connections[i], SRTO_CONNTIMEO, &conntimeo, sizeof conntimeo);

        //cerr << "Connecting #" << i << " to " << sockaddr_any(psa).str() << "...\n";
        //cerr << "Connecting to: " << sockaddr_any(psa).str() << endl;
        ASSERT_NE(srt_connect(m_connections[i], psa, sizeof lsa), SRT_ERROR);

        // Set now async sending so that sending isn't blocked
        int no = 0;
        ASSERT_NE(srt_setsockflag(m_connections[i], SRTO_SNDSYN, &no, sizeof no), -1);
    }

    for (size_t j = 1; j <= 100; j++)
    {
        for (size_t i = 0; i < NSOCK; i++)
        {
            EXPECT_GT(srt_send(m_connections[i], m_buf.data(), (int) m_buf.size()), 0);
        }
    }
    cerr << "Sending finished, closing caller sockets\n";

    for (size_t i = 0; i < NSOCK; i++)
    {
        EXPECT_EQ(srt_close(m_connections[i]), SRT_SUCCESS);
    }

    EXPECT_FALSE(m_accept_exit) << "AcceptLoop already broken for some reason!";
    // Up to this moment the server sock should survive
    cerr << "Closing server socket\n";
    // Close server socket to break the accept loop
    EXPECT_EQ(srt_close(m_server_sock), 0);

    cerr << "Synchronize with the accepting thread\n";
    ex.wait();
    cerr << "Synchronization done\n";
}



