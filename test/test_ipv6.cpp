#include <thread>
#include <chrono>
#include <string>
#include <future>
#include "gtest/gtest.h"
#include "test_env.h"

#include "srt.h"
#include "sync.h"
#include "netinet_any.h"

using srt::sockaddr_any;
using namespace srt::sync;

class TestIPv6
    : public srt::Test
{
protected:
    int yes = 1;
    int no = 0;

    TestIPv6()
    {
        // initialization code here
    }

    ~TestIPv6()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:
    // SetUp() is run immediately before a test starts.
    void setup() override
    {
        m_caller_sock = srt_create_socket();
        ASSERT_NE(m_caller_sock, SRT_ERROR);
        // IPv6 calling IPv4 would otherwise fail if the system-default net.ipv6.bindv6only=1.
        ASSERT_NE(srt_setsockflag(m_caller_sock, SRTO_IPV6ONLY, &no, sizeof no), SRT_ERROR);

        m_listener_sock = srt_create_socket();
        ASSERT_NE(m_listener_sock, SRT_ERROR);

        m_CallerStarted.reset(new std::promise<void>);
        m_ReadyCaller.reset(new std::promise<void>);
        m_ReadyAccept.reset(new std::promise<void>);

    }

    void teardown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        srt_close(m_listener_sock);
        srt_close(m_caller_sock);
    }

public:

    void SetupFileMode()
    {
        int val = SRTT_FILE;
        ASSERT_NE(srt_setsockflag(m_caller_sock, SRTO_TRANSTYPE, &val, sizeof val), -1);
        ASSERT_NE(srt_setsockflag(m_listener_sock, SRTO_TRANSTYPE, &val, sizeof val), -1);
    }

    int m_CallerPayloadSize = 0;
    int m_AcceptedPayloadSize = 0;

    std::unique_ptr<std::promise<void>> m_CallerStarted, m_ReadyCaller, m_ReadyAccept;

    // "default parameter" version. Can't use default parameters because this goes
    // against binding parameters. Nor overloading.
    void ClientThread(int family, const std::string& address)
    {
        return ClientThreadFlex(family, address, true);
    }

    void ClientThreadFlex(int family, const std::string& address, bool shouldwork)
    {
        std::future<void> ready_accepter = m_ReadyAccept->get_future();

        sockaddr_any sa (family);
        sa.hport(m_listen_port);
        EXPECT_EQ(inet_pton(family, address.c_str(), sa.get_addr()), 1);

        std::cout << "Calling: " << address << "(" << fam[family] << ") [LOCK...]\n";

        m_CallerStarted->set_value();

        const int connect_res = srt_connect(m_caller_sock, (sockaddr*)&sa, sizeof sa);

        if (shouldwork)
        {
            // Version with expected success
            EXPECT_NE(connect_res, SRT_ERROR) << "srt_connect() failed with: " << srt_getlasterror_str();

            int size = sizeof (int);
            EXPECT_NE(srt_getsockflag(m_caller_sock, SRTO_PAYLOADSIZE, &m_CallerPayloadSize, &size), -1);

            m_ReadyCaller->set_value();

            PrintAddresses(m_caller_sock, "CALLER");

            if (connect_res == SRT_ERROR)
            {
                std::cout << "Connect failed - [UNLOCK]\n";
                srt_close(m_listener_sock);
            }
            else
            {
                std::cout << "Connect succeeded, [FUTURE-WAIT...]\n";
                ready_accepter.wait();
            }
        }
        else
        {
            // Version with expected failure
            EXPECT_EQ(connect_res, SRT_ERROR);
            EXPECT_EQ(srt_getrejectreason(m_caller_sock), SRT_REJ_SETTINGS);
            srt_close(m_listener_sock);
        }
        std::cout << "Connect: exit\n";
    }

    std::map<int, std::string> fam = { {AF_INET, "IPv4"}, {AF_INET6, "IPv6"} };

    void ShowAddress(std::string src, const sockaddr_any& w)
    {
        EXPECT_NE(fam.count(w.family()), 0U) << "INVALID FAMILY";
        // Printing may happen from different threads, avoid intelining.
        std::ostringstream sout;
        sout << src << ": " << w.str() << " (" << fam[w.family()] << ")" << std::endl;
        std::cout << sout.str();
    }

    sockaddr_any DoAccept()
    {
        sockaddr_any sc1;

        // Make sure the caller started
        m_CallerStarted->get_future().wait();
        std::cout << "DoAccept: caller started, proceeding to accept\n";

        SRTSOCKET accepted_sock = srt_accept(m_listener_sock, sc1.get(), &sc1.len);
        EXPECT_NE(accepted_sock, SRT_INVALID_SOCK) << "accept() failed with: " << srt_getlasterror_str();
        if (accepted_sock == SRT_INVALID_SOCK) {
            return sockaddr_any();
        }
        PrintAddresses(accepted_sock, "ACCEPTED");

        sockaddr_any sn;
        EXPECT_NE(srt_getsockname(accepted_sock, sn.get(), &sn.len), SRT_ERROR);
        EXPECT_NE(sn.get_addr(), nullptr);
        int size = sizeof (int);
        EXPECT_NE(srt_getsockflag(m_caller_sock, SRTO_PAYLOADSIZE, &m_AcceptedPayloadSize, &size), -1);

        m_ReadyCaller->get_future().wait();

        if (sn.get_addr() != nullptr)
        {
            const int32_t ipv6_zero[] = { 0, 0, 0, 0 };
            EXPECT_NE(memcmp(ipv6_zero, sn.get_addr(), sizeof ipv6_zero), 0)
                << "EMPTY address in srt_getsockname";
        }

        std::cout << "DoAccept: ready accept - promise SET\n";
        m_ReadyAccept->set_value();

        srt_close(accepted_sock);
        return sn;
    }

private:
    void PrintAddresses(SRTSOCKET sock, const char* who)
    {
        sockaddr_any sa;
        int sa_len = (int) sa.storage_size();
        srt_getsockname(sock, sa.get(), &sa_len);
        ShowAddress(std::string(who) + " Sock name: ", sa);
        //std::cout << who << " Sock name: " << << sa.str() << std::endl;

        sa_len = (int) sa.storage_size();
        srt_getpeername(sock, sa.get(), &sa_len);
        //std::cout << who << " Peer name: " << << sa.str() << std::endl;
        ShowAddress(std::string(who) + " Peer name: ", sa);
    }

protected:
    SRTSOCKET m_caller_sock;
    SRTSOCKET m_listener_sock;
    const int m_listen_port = 4200;
};


TEST_F(TestIPv6, v4_calls_v6_mapped)
{
    sockaddr_any sa (AF_INET6);
    sa.hport(m_listen_port);

    ASSERT_EQ(srt_setsockflag(m_listener_sock, SRTO_IPV6ONLY, &no, sizeof no), 0);
    ASSERT_NE(srt_bind(m_listener_sock, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock, SOMAXCONN), SRT_ERROR);

    std::thread client(&TestIPv6::ClientThread, this, AF_INET, "127.0.0.1");

    const sockaddr_any sa_accepted = DoAccept();
    EXPECT_EQ(sa_accepted.str(), "::ffff:127.0.0.1:4200");

    client.join();
}

TEST_F(TestIPv6, v6_calls_v6_mapped)
{
    SRTST_REQUIRES(IPv6);

    sockaddr_any sa (AF_INET6);
    sa.hport(m_listen_port);

    ASSERT_EQ(srt_setsockflag(m_listener_sock, SRTO_IPV6ONLY, &no, sizeof no), 0);
    ASSERT_NE(srt_bind(m_listener_sock, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock, SOMAXCONN), SRT_ERROR);

    std::thread client(&TestIPv6::ClientThread, this, AF_INET6, "::1");

    const sockaddr_any sa_accepted = DoAccept();
    EXPECT_EQ(sa_accepted.str(), "::1:4200");

    client.join();
}

TEST_F(TestIPv6, v6_calls_v6)
{
    SRTST_REQUIRES(IPv6);

    sockaddr_any sa (AF_INET6);
    sa.hport(m_listen_port);

    // This time bind the socket exclusively to IPv6.
    ASSERT_EQ(srt_setsockflag(m_listener_sock, SRTO_IPV6ONLY, &yes, sizeof yes), 0);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", sa.get_addr()), 1);

    ASSERT_NE(srt_bind(m_listener_sock, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock, SOMAXCONN), SRT_ERROR);

    std::thread client(&TestIPv6::ClientThread, this, AF_INET6, "::1");

    const sockaddr_any sa_accepted = DoAccept();
    EXPECT_EQ(sa_accepted.str(), "::1:4200");

    client.join();
}

TEST_F(TestIPv6, v6_calls_v4)
{
    sockaddr_any sa (AF_INET);
    sa.hport(m_listen_port);

    // This time bind the socket exclusively to IPv4.
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", sa.get_addr()), 1);

    ASSERT_NE(srt_bind(m_listener_sock, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock, SOMAXCONN), SRT_ERROR);

    std::thread client(&TestIPv6::ClientThread, this, AF_INET6, "0::FFFF:127.0.0.1");

    const sockaddr_any sa_accepted = DoAccept();
    EXPECT_EQ(sa_accepted.str(), "127.0.0.1:4200");

    client.join();
}

TEST_F(TestIPv6, plsize_v6)
{
    SRTST_REQUIRES(IPv6);

    SetupFileMode();

    sockaddr_any sa (AF_INET6);
    sa.hport(m_listen_port);

    // This time bind the socket exclusively to IPv6.
    ASSERT_EQ(srt_setsockflag(m_listener_sock, SRTO_IPV6ONLY, &yes, sizeof yes), 0);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", sa.get_addr()), 1);

    ASSERT_NE(srt_bind(m_listener_sock, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock, SOMAXCONN), SRT_ERROR);

    std::thread client(&TestIPv6::ClientThread, this, AF_INET6, "::1");

    DoAccept();

    EXPECT_EQ(m_CallerPayloadSize, 1444); // == 1500 - 32[IPv6] - 8[UDP] - 16[SRT]
    EXPECT_EQ(m_AcceptedPayloadSize, 1444);

    client.join();
}

TEST_F(TestIPv6, plsize_v4)
{
    SetupFileMode();

    sockaddr_any sa (AF_INET);
    sa.hport(m_listen_port);

    // This time bind the socket exclusively to IPv4.
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", sa.get_addr()), 1);

    ASSERT_NE(srt_bind(m_listener_sock, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock, SOMAXCONN), SRT_ERROR);

    std::thread client(&TestIPv6::ClientThread, this, AF_INET6, "0::FFFF:127.0.0.1");

    DoAccept();

    EXPECT_EQ(m_CallerPayloadSize, 1456); // == 1500 - 20[IPv4] - 8[UDP] - 16[SRT]
    EXPECT_EQ(m_AcceptedPayloadSize, 1456);

    client.join();
}

TEST_F(TestIPv6, plsize_faux_v6)
{
    SRTST_REQUIRES(IPv6);

    using namespace std::chrono;
    SetupFileMode();

    sockaddr_any sa (AF_INET6);
    sa.hport(m_listen_port);

    // This time bind the socket exclusively to IPv6.
    ASSERT_EQ(srt_setsockflag(m_listener_sock, SRTO_IPV6ONLY, &yes, sizeof yes), 0);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", sa.get_addr()), 1);

    ASSERT_NE(srt_bind(m_listener_sock, sa.get(), sa.size()), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock, SOMAXCONN), SRT_ERROR);

    int oversize = 1450;
    ASSERT_NE(srt_setsockflag(m_caller_sock, SRTO_PAYLOADSIZE, &oversize, sizeof (int)), -1);

    std::thread client(&TestIPv6::ClientThreadFlex, this, AF_INET6, "::1", false);

    // Set on sleeping to make sure that the thread started.
    // Sleeping isn't reliable so do a dampened spinlock here.
    // This flag also confirms that the caller acquired the mutex and will
    // unlock it for CV waiting - so we can proceed to notifying it.
    m_CallerStarted->get_future().wait();

    // Just in case of a test failure, kick CV to avoid deadlock
    std::cout << "TEST: [PROMISE-SET]\n";
    m_ReadyAccept->set_value();

    client.join();
}
