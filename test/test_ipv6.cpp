#include <thread>
#include <string>
#include "gtest/gtest.h"
#include "test_env.h"

#include "srt.h"
#include "netinet_any.h"

using srt::sockaddr_any;

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
    }

    void teardown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        srt_close(m_listener_sock);
        srt_close(m_caller_sock);
    }

public:
    void ClientThread(int family, const std::string& address)
    {
        sockaddr_any sa (family);
        sa.hport(m_listen_port);
        EXPECT_EQ(inet_pton(family, address.c_str(), sa.get_addr()), 1);

        std::cout << "Calling: " << address << "(" << fam[family] << ")\n";

        const int connect_res = srt_connect(m_caller_sock, (sockaddr*)&sa, sizeof sa);
        EXPECT_NE(connect_res, SRT_ERROR) << "srt_connect() failed with: " << srt_getlasterror_str();
        if (connect_res == SRT_ERROR)
            srt_close(m_listener_sock);

        PrintAddresses(m_caller_sock, "CALLER");
    }

    std::map<int, std::string> fam = { {AF_INET, "IPv4"}, {AF_INET6, "IPv6"} };

    void ShowAddress(std::string src, const sockaddr_any& w)
    {
        EXPECT_NE(fam.count(w.family()), 0U) << "INVALID FAMILY";
        std::cout << src << ": " << w.str() << " (" << fam[w.family()] << ")" << std::endl;
    }

    sockaddr_any DoAccept()
    {
        sockaddr_any sc1;

        SRTSOCKET accepted_sock = srt_accept(m_listener_sock, sc1.get(), &sc1.len);
        EXPECT_NE(accepted_sock, SRT_INVALID_SOCK) << "accept() failed with: " << srt_getlasterror_str();
        if (accepted_sock == SRT_INVALID_SOCK) {
            return sockaddr_any();
        }

        PrintAddresses(accepted_sock, "ACCEPTED");

        sockaddr_any sn;
        EXPECT_NE(srt_getsockname(accepted_sock, sn.get(), &sn.len), SRT_ERROR);
        EXPECT_NE(sn.get_addr(), nullptr);

        if (sn.get_addr() != nullptr)
        {
            const int32_t ipv6_zero[] = { 0, 0, 0, 0 };
            EXPECT_NE(memcmp(ipv6_zero, sn.get_addr(), sizeof ipv6_zero), 0)
                << "EMPTY address in srt_getsockname";
        }

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

