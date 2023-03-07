#include "gtest/gtest.h"
#include <thread>
#include <chrono>
#include <string>
#include "srt.h"
#include "sync.h"
#include "netinet_any.h"

using srt::sockaddr_any;
using namespace srt::sync;

class TestIPv6
    : public ::testing::Test
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
    void SetUp()
    {
        ASSERT_GE(srt_startup(), 0);

        m_caller_sock = srt_create_socket();
        ASSERT_NE(m_caller_sock, SRT_ERROR);
        // IPv6 calling IPv4 would otherwise fail if the system-default net.ipv6.bindv6only=1.
        ASSERT_NE(srt_setsockflag(m_caller_sock, SRTO_IPV6ONLY, &no, sizeof no), SRT_ERROR);

        m_listener_sock = srt_create_socket();
        ASSERT_NE(m_listener_sock, SRT_ERROR);
        m_CallerStarted = false;
    }

    void TearDown()
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        srt_close(m_listener_sock);
        srt_close(m_caller_sock);
        srt_cleanup();
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
    atomic<bool> m_CallerStarted;

    Condition m_ReadyToClose;
    Mutex m_ReadyToCloseLock;

    // "default parameter" version. Can't use default parameters because this goes
    // against binding parameters. Nor overloading.
    void ClientThread(int family, const std::string& address)
    {
        return ClientThreadFlex(family, address, true);
    }

    void ClientThreadFlex(int family, const std::string& address, bool shouldwork)
    {
        sockaddr_any sa (family);
        sa.hport(m_listen_port);
        EXPECT_EQ(inet_pton(family, address.c_str(), sa.get_addr()), 1);

        std::cout << "Calling: " << address << "(" << fam[family] << ") [LOCK...]\n";

        CUniqueSync before_closing(m_ReadyToCloseLock, m_ReadyToClose);

        std::cout << "[LOCKED] Connecting\n";

        m_CallerStarted = true;

        const int connect_res = srt_connect(m_caller_sock, (sockaddr*)&sa, sizeof sa);

        if (shouldwork)
        {
            // Version with expected success
            EXPECT_NE(connect_res, SRT_ERROR) << "srt_connect() failed with: " << srt_getlasterror_str();

            int size = sizeof (int);
            EXPECT_NE(srt_getsockflag(m_caller_sock, SRTO_PAYLOADSIZE, &m_CallerPayloadSize, &size), -1);

            PrintAddresses(m_caller_sock, "CALLER");

            if (connect_res == SRT_ERROR)
            {
                std::cout << "Connect failed - [UNLOCK]\n";
                before_closing.locker().unlock(); // We don't need this lock here and it may deadlock
                srt_close(m_listener_sock);
            }
            else
            {
                std::cout << "Connect succeeded, [UNLOCK-WAIT-CV...]\n";
                bool signaled = before_closing.wait_for(seconds_from(10));
                std::cout << "Connect: [" << (signaled ? "SIGNALED" : "EXPIRED") << "-LOCK]\n";
            }
        }
        else
        {
            // Version with expected failure
            EXPECT_EQ(connect_res, SRT_ERROR);
            EXPECT_EQ(srt_getrejectreason(m_caller_sock), SRT_REJ_SETTINGS);
            srt_close(m_listener_sock);
        }
        std::cout << "Connect: [UNLOCKING...]\n";
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

        using namespace std::chrono;

        SRTSOCKET accepted_sock = srt_accept(m_listener_sock, sc1.get(), &sc1.len);
        EXPECT_NE(accepted_sock, SRT_INVALID_SOCK) << "accept() failed with: " << srt_getlasterror_str();
        if (accepted_sock == SRT_INVALID_SOCK) {
            return sockaddr_any();
        }
        int size = sizeof (int);
        EXPECT_NE(srt_getsockflag(m_caller_sock, SRTO_PAYLOADSIZE, &m_AcceptedPayloadSize, &size), -1);

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

        std::cout << "DoAccept: [LOCK-SIGNAL]\n";

        // Travis makes problems here by unknown reason. Try waiting up to 10s
        // until it's possible, otherwise simply give up. The intention is to
        // prevent from closing too soon before the caller thread has a chance
        // to perform required verifications. After 10s we can consider it enough time.

        int nms = 10;
        while (!m_ReadyToCloseLock.try_lock())
        {
            this_thread::sleep_for(milliseconds_from(100));
            if (--nms == 0)
                break;
        }

        CSync::notify_all_relaxed(m_ReadyToClose);
        m_ReadyToCloseLock.unlock();
        std::cout << "DoAccept: [UNLOCKED] " << nms << "\n";

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
    do std::this_thread::sleep_for(milliseconds(100)); while (!m_CallerStarted);

    // Just in case of a test failure, kick CV to avoid deadlock
    std::cout << "TEST: [LOCK-SIGNAL]\n";
    CSync::lock_notify_all(m_ReadyToClose, m_ReadyToCloseLock);
    std::cout << "TEST: [UNLOCKED]\n";

    client.join();
}
