#include <thread>
#include <future>
#include <sstream>
#ifndef _WIN32
#include <ifaddrs.h>
#endif
#include "gtest/gtest.h"
#include "test_env.h"

#include "common.h"
#include "srt.h"
#include "udt.h"

using srt::sockaddr_any;

template<typename Future>
struct AtReturnJoin
{
    Future& which_future;

    ~AtReturnJoin()
    {
        if (which_future.valid())
            which_future.wait();
    }
};

#ifdef _WIN32

// On Windows there's a function for it, but it requires an extra
// iphlp library to be attached to the executable, which is kinda
// problematic. Temporarily block tests using this function on Windows.

static std::string GetLocalIP(int af = AF_UNSPEC)
{
    std::cout << "!!!WARNING!!!: GetLocalIP not supported, test FORCEFULLY passed\n";
    return "";
}
#else
struct IfAddr
{
    ifaddrs* handle;
    IfAddr()
    {
        getifaddrs(&handle);
    }

    ~IfAddr()
    {
        freeifaddrs(handle);
    }
};

static std::string GetLocalIP(int af = AF_UNSPEC)
{
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;

    IfAddr ifAddr;

    for (ifa = ifAddr.handle; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (!ifa->ifa_addr)
        {
            continue;
        }

        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            // Ignore IPv4 address if not wanted.
            if (af == AF_INET6)
                continue;

            // is a valid IP4 Address
            sockaddr_in* psin = (struct sockaddr_in *)ifa->ifa_addr;
            tmpAddrPtr=&psin->sin_addr;

            if (ntohl(psin->sin_addr.s_addr) == 0x7f000001) // Skip 127.0.0.1
                continue;

            char addressBuffer[INET_ADDRSTRLEN] = "";
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            return addressBuffer;
            printf("%s IP Address %s\n", ifa->ifa_name, addressBuffer); 
        }
        else if (ifa->ifa_addr->sa_family == AF_INET6)
        { // check it is IP6

            // Ignore IPv6 address if not wanted.
            if (af == AF_INET)
                continue;

            // is a valid IP6 Address
            tmpAddrPtr=&((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
            char addressBuffer[INET6_ADDRSTRLEN] = "";
            inet_ntop(AF_INET6, tmpAddrPtr, addressBuffer, INET6_ADDRSTRLEN);
            return addressBuffer;
        }
    }

    return "";
}
#endif

class ReuseAddr : public srt::Test
{
protected:

    std::string showEpollContents(const char* label, int* array, int length)
    {
        std::ostringstream out;
        out << label << ":[";
        if (length)
        {
            // Now is at least 1
            out << "@" << array[0];

            for (int i = 1; i < length; ++i)
                out << " @" << array[i];
        }
        out << "]";
        return out.str();
    }

    struct UniquePollid
    {
        int pollid = SRT_ERROR;
        UniquePollid()
        {
            pollid = srt_epoll_create();
        }

        ~UniquePollid()
        {
            srt_epoll_release(pollid);
        }

        operator int() const
        {
            return pollid;
        }
    };

    void clientSocket(SRTSOCKET client_sock, std::string ip, int port, bool expect_success)
    {
        using namespace std;

        int yes = 1;
        int no = 0;

        int family = AF_INET;
        string famname = "IPv4";
        if (ip.substr(0, 2) == "6.")
        {
            family = AF_INET6;
            ip = ip.substr(2);
            famname = "IPv6";
        }

        cout << "[T/C] Setting up client socket\n";
        ASSERT_NE(client_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(srt_getsockstate(client_sock), SRTS_INIT);

        EXPECT_NE(srt_setsockflag(client_sock, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
        EXPECT_NE(srt_setsockflag(client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
        EXPECT_NE(srt_setsockflag(client_sock, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

        UniquePollid client_pollid;
        ASSERT_NE(int(client_pollid), SRT_ERROR);

        int epoll_out = SRT_EPOLL_OUT;
        srt_epoll_add_usock(client_pollid, client_sock, &epoll_out);

        sockaddr_any sa = srt::CreateAddr(ip, port, family);

        cout << "[T/C] Connecting to: " << sa.str() << " (" << famname << ")" << endl;

        int connect_res = srt_connect(client_sock, sa.get(), sa.size());

        if (connect_res == -1)
        {
            cout << "srt_connect: " << srt_getlasterror_str() << endl;
        }

        if (expect_success)
        {
            EXPECT_NE(connect_res, -1);
            if (connect_res == -1)
                return;

            // Socket readiness for connection is checked by polling on WRITE allowed sockets.

            if (connect_res != -1)
            {
                int rlen = 2;
                SRTSOCKET read[2];

                int wlen = 2;
                SRTSOCKET write[2];

                cout << "[T/C] Waiting for connection readiness...\n";

                EXPECT_NE(srt_epoll_wait(client_pollid, read, &rlen,
                            write, &wlen,
                            -1, // -1 is set for debuging purpose.
                            // in case of production we need to set appropriate value
                            0, 0, 0, 0), SRT_ERROR) << srt_getlasterror_str();

                EXPECT_EQ(rlen, 0) << showEpollContents("[T/C] R", read, rlen); // get exactly one write event without reads
                EXPECT_EQ(wlen, 1) << showEpollContents("[T/C] W", write, wlen); // get exactly one write event without reads
                EXPECT_EQ(write[0], client_sock); // for our client socket

                char buffer[1316] = {1, 2, 3, 4};
                EXPECT_NE(srt_sendmsg(client_sock, buffer, sizeof buffer,
                            -1, // infinit ttl
                            true // in order must be set to true
                            ),
                        SRT_ERROR);
            }
            else
            {
                cout << "[T/C] (NOT TESTING TRANSMISSION - CONNECTION FAILED ALREADY)\n";
            }
        }
        else
        {
            EXPECT_EQ(connect_res, -1);
        }

        cout << "[T/C] Client exit\n";
    }

    SRTSOCKET prepareServerSocket()
    {
        SRTSOCKET bindsock = srt_create_socket();
        EXPECT_NE(bindsock, SRT_ERROR);

        int yes = 1;
        int no = 0;

        EXPECT_NE(srt_setsockopt(bindsock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
        EXPECT_NE(srt_setsockopt(bindsock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

        return bindsock;
    }

    bool bindSocket(SRTSOCKET bindsock, std::string ip, int port, bool expect_success)
    {
        sockaddr_any sa = srt::CreateAddr(ip, port, AF_INET);

        std::string fam = (sa.family() == AF_INET) ? "IPv4" : "IPv6";

        std::cout << "[T/S] Bind @" << bindsock << " to: " << sa.str() << " (" << fam << ")" << std::endl;

        int bind_res = srt_bind(bindsock, sa.get(), sa.size());

        std::cout << "[T/S] ... result " << bind_res << " (expected to "
            << (expect_success ? "succeed" : "fail") << ")\n";

        if (!expect_success)
        {
            std::cout << "[T/S] Binding should fail: " << srt_getlasterror_str() << std::endl;
            EXPECT_EQ(bind_res, SRT_ERROR);
            return false;
        }

        EXPECT_NE(bind_res, SRT_ERROR);
        return true;
    }

    bool bindListener(SRTSOCKET bindsock, std::string ip, int port, bool expect_success)
    {
        if (!bindSocket(bindsock, ip, port, expect_success))
            return false;

        EXPECT_NE(srt_listen(bindsock, SOMAXCONN), SRT_ERROR);

        return true;
    }

    SRTSOCKET createListener(std::string ip, int port, bool expect_success)
    {
        std::cout << "[T/S] serverSocket: creating listener socket\n";

        SRTSOCKET bindsock = prepareServerSocket();

        if (!bindListener(bindsock, ip, port, expect_success))
            return SRT_INVALID_SOCK;

        return bindsock;
    }

    SRTSOCKET createBinder(std::string ip, int port, bool expect_success)
    {
        std::cout << "[T/S] serverSocket: creating binder socket\n";

        SRTSOCKET bindsock = prepareServerSocket();

        if (!bindSocket(bindsock, ip, port, expect_success))
        {
            srt_close(bindsock);
            return SRT_INVALID_SOCK;
        }

        return bindsock;
    }

    void testAccept(SRTSOCKET bindsock, std::string ip, int port, bool expect_success)
    {
        MAKE_UNIQUE_SOCK(client_sock, "[T/S]connect", srt_create_socket());

        auto run = [this, &client_sock, ip, port, expect_success]() { clientSocket(client_sock, ip, port, expect_success); };

        auto launched = std::async(std::launch::async, run);

        AtReturnJoin<decltype(launched)> atreturn_join {launched};

        int server_pollid = srt_epoll_create();
        int epoll_in = SRT_EPOLL_IN;
        std::cout << "[T/S] Listener/binder sock @" << bindsock << " added to server_pollid\n";
        srt_epoll_add_usock(server_pollid, bindsock, &epoll_in);

        { // wait for connection from client
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            std::cout << "[T/S] Wait 10s on E" << server_pollid << " for acceptance on @" << bindsock << " ...\n";

            EXPECT_NE(srt_epoll_wait(server_pollid,
                        read,  &rlen,
                        write, &wlen,
                        10000, // -1 is set for debuging purpose.
                        // in case of production we need to set appropriate value
                        0, 0, 0, 0), SRT_ERROR) << srt_getlasterror_str();


            EXPECT_EQ(rlen, 1) << showEpollContents("[T/S] R", read, rlen);  // get exactly one read event without writes
            EXPECT_EQ(wlen, 0) << showEpollContents("[T/S] W", write, wlen);  // get exactly one read event without writes
            ASSERT_EQ(read[0], bindsock); // read event is for bind socket
        }

        {
            sockaddr_any scl;
            MAKE_UNIQUE_SOCK(accepted_sock, "[T/S]accept", srt_accept(bindsock, scl.get(), &scl.len));

            if (accepted_sock == -1)
            {
                std::cout << "srt_accept: " << srt_getlasterror_str() << std::endl;
            }
            EXPECT_NE(accepted_sock.ref(), SRT_INVALID_SOCK);

            sockaddr_any showacp = (sockaddr*)&scl;
            std::cout << "[T/S] Accepted from: " << showacp.str() << std::endl;

            int epoll_in = SRT_EPOLL_IN;
            srt_epoll_add_usock(server_pollid, accepted_sock, &epoll_in); // wait for input

            char buffer[1316];
            { // wait for 1316 packet from client
                int rlen = 2;
                SRTSOCKET read[2];

                int wlen = 2;
                SRTSOCKET write[2];

                std::cout << "[T/S] Wait for data reception...\n";

                EXPECT_NE(srt_epoll_wait(server_pollid,
                            read,  &rlen,
                            write, &wlen,
                            -1, // -1 is set for debuging purpose.
                            // in case of production we need to set appropriate value
                            0, 0, 0, 0), SRT_ERROR) << srt_getlasterror_str();


                EXPECT_EQ(rlen, 1) << showEpollContents("[T/S] R", read, rlen);   // get exactly one read event without writes
                EXPECT_EQ(wlen, 0) << showEpollContents("[T/S] W", write, wlen);  // get exactly one read event without writes
                EXPECT_EQ(read[0], accepted_sock.ref()); // read event is for bind socket        
            }

            char pattern[4] = {1, 2, 3, 4};

            EXPECT_EQ(srt_recvmsg(accepted_sock, buffer, sizeof buffer),
                    1316);

            EXPECT_EQ(memcmp(pattern, buffer, sizeof pattern), 0);

            // XXX There is a possibility that a broken socket can be closed automatically,
            // just the srt_close() call would simply return error in case of nonexistent
            // socket. Therefore close them both at once; this problem needs to be fixed
            // separately.
            //
            // The test only intends to send one portion of data from the client, so once
            // received, the client has nothing more to do and should exit.
            std::cout << "[T/S] closing client socket\n";
            client_sock.close();
            std::cout << "[T/S] closing sockets: ACP:@" << accepted_sock << "...\n";
        }
        srt_epoll_release(server_pollid);

        // client_sock closed through UniqueSocket.
        // cannot close client_sock after srt_sendmsg because of issue in api.c:2346 

        std::cout << "[T/S] joining client async \n";
        launched.get();
    }

    static void shutdownListener(SRTSOCKET bindsock)
    {
        // Silently ignore. Usually it should have been checked earlier,
        // and an invalid sock might be expected in particular tests.
        if (bindsock == SRT_INVALID_SOCK)
            return;

        int yes = 1;
        EXPECT_NE(srt_setsockopt(bindsock, 0, SRTO_RCVSYN, &yes, sizeof yes), SRT_ERROR); // for async connect
        EXPECT_NE(srt_close(bindsock), SRT_ERROR);

        std::chrono::milliseconds check_period (100);
        int credit = 400; // 10 seconds
        auto then = std::chrono::steady_clock::now();

        std::cout << "[T/S] waiting for cleanup of @" << bindsock << " up to 10s" << std::endl;
        while (srt_getsockstate(bindsock) != SRTS_NONEXIST)
        {
            std::this_thread::sleep_for(check_period);
            --credit;
            if (!credit)
                break;
        }
        auto now = std::chrono::steady_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - then);

        // Keep as single string because this tends to be mixed from 2 threads.
        std::ostringstream sout;
        sout << "[T/S] @" << bindsock << " dissolved after "
            << (dur.count() / 1000.0) << "s" << std::endl;
        std::cout << sout.str() << std::flush;

        EXPECT_NE(credit, 0);
    }

private:

    void setup()
    {
    }

    void teardown()
    {
    }
};

TEST_F(ReuseAddr, SameAddr1)
{

    SRTSOCKET bindsock_1 = createBinder("127.0.0.1", 5000, true);
    SRTSOCKET bindsock_2 = createListener("127.0.0.1", 5000, true);

    testAccept(bindsock_2, "127.0.0.1", 5000, true);

    std::thread s1(shutdownListener, bindsock_1);
    std::thread s2(shutdownListener, bindsock_2);

    s1.join();
    s2.join();

}

TEST_F(ReuseAddr, SameAddr2)
{
    std::string localip = GetLocalIP(AF_INET);
    if (localip == "")
        return; // DISABLE TEST if this doesn't work.

    SRTSOCKET bindsock_1 = createBinder(localip, 5000, true);
    SRTSOCKET bindsock_2 = createListener(localip, 5000, true);

    testAccept(bindsock_2, localip, 5000, true);

    shutdownListener(bindsock_1);

    // Test simple close and reuse the multiplexer
    ASSERT_NE(srt_close(bindsock_2), SRT_ERROR);

    SRTSOCKET bindsock_3 = createListener(localip, 5000, true);
    testAccept(bindsock_3, localip, 5000, true);

    shutdownListener(bindsock_3);
}

TEST_F(ReuseAddr, SameAddrV6)
{
    SRTST_REQUIRES(IPv6);

    SRTSOCKET bindsock_1 = createBinder("::1", 5000, true);
    SRTSOCKET bindsock_2 = createListener("::1", 5000, true);

    testAccept(bindsock_2, "::1", 5000, true);

    shutdownListener(bindsock_1);

    // Test simple close and reuse the multiplexer
    ASSERT_NE(srt_close(bindsock_2), SRT_ERROR);

    SRTSOCKET bindsock_3 = createListener("::1", 5000, true);
    testAccept(bindsock_3, "::1", 5000, true);

    shutdownListener(bindsock_3);
}


TEST_F(ReuseAddr, DiffAddr)
{
    std::string localip = GetLocalIP(AF_INET);
    if (localip == "")
        return; // DISABLE TEST if this doesn't work.

    SRTSOCKET bindsock_1 = createBinder("127.0.0.1", 5000, true);
    SRTSOCKET bindsock_2 = createListener(localip, 5000, true);

    //std::thread server_1(testAccept, bindsock_1, "127.0.0.1", 5000, true);
    //server_1.join();

    testAccept(bindsock_2, localip, 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);
}

TEST_F(ReuseAddr, Wildcard)
{
#if defined(_WIN32) || defined(CYGWIN)
    std::cout << "!!!WARNING!!!: On Windows connection to localhost this way isn't possible.\n"
        "Forcing test to pass, PLEASE FIX.\n";
    return;
#endif

    // This time exceptionally require IPv4 because we'll be
    // checking it against 0.0.0.0 
    std::string localip = GetLocalIP(AF_INET);
    if (localip == "")
        return; // DISABLE TEST if this doesn't work.
    SRTSOCKET bindsock_1 = createListener("0.0.0.0", 5000, true);

    // Binding a certain address when wildcard is already bound should fail.
    SRTSOCKET bindsock_2 = createBinder(localip, 5000, false);

    testAccept(bindsock_1, "127.0.0.1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);
}

TEST_F(ReuseAddr, Wildcard6)
{
    SRTST_REQUIRES(IPv6);
#if defined(_WIN32) || defined(CYGWIN)
    std::cout << "!!!WARNING!!!: On Windows connection to localhost this way isn't possible.\n"
        "Forcing test to pass, PLEASE FIX.\n";
    return;
#endif

    // This time exceptionally require IPv6 because we'll be
    // checking it against ::
    std::string localip = GetLocalIP(AF_INET6);
    if (localip == "")
        return; // DISABLE TEST if this doesn't work.

    // This "should work", but there can also be platforms
    // that do not have IPv4, in which case this test can't be
    // performed there.
    std::string localip_v4 = GetLocalIP(AF_INET);

    // This must be obligatory set before binding a socket to "::"
    int strict_ipv6 = 1;

    SRTSOCKET bindsock_1 = prepareServerSocket();
    srt_setsockflag(bindsock_1, SRTO_IPV6ONLY, &strict_ipv6, sizeof strict_ipv6);
    bindListener(bindsock_1, "::", 5000, true);

    // Binding a certain address when wildcard is already bound should fail.
    SRTSOCKET bindsock_2 = createBinder(localip, 5000, false);

    SRTSOCKET bindsock_3 = SRT_INVALID_SOCK;
    if (localip_v4 != "")
    {
        // V6ONLY = 1, which means that binding to IPv4 should be possible.
        bindsock_3 = createBinder(localip_v4, 5000, true);
    }

    testAccept(bindsock_1, "::1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);
    shutdownListener(bindsock_3);

    // Now the same thing, except that we bind to both IPv4 and IPv6.

    strict_ipv6 = 0;

    bindsock_1 = prepareServerSocket();
    srt_setsockflag(bindsock_1, SRTO_IPV6ONLY, &strict_ipv6, sizeof strict_ipv6);
    bindListener(bindsock_1, "::", 5000, true);

    // Binding a certain address when wildcard is already bound should fail.
    bindsock_2 = createBinder(localip, 5000, false);
    bindsock_3 = SRT_INVALID_SOCK;

    if (localip_v4 != "")
    {
        // V6ONLY = 0, which means that binding to IPv4 should not be possible.
        bindsock_3 = createBinder(localip_v4, 5000, false);
    }

    testAccept(bindsock_1, "::1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);
    shutdownListener(bindsock_3);
}

TEST_F(ReuseAddr, ProtocolVersion6)
{
    SRTST_REQUIRES(IPv6);

#if defined(_WIN32) || defined(CYGWIN)
    std::cout << "!!!WARNING!!!: On Windows connection to localhost this way isn't possible.\n"
        "Forcing test to pass, PLEASE FIX.\n";
    return;
#endif

    SRTSOCKET bindsock_1 = createListener("0.0.0.0", 5000, true);

    // We need a small interception in this one.
    // createListener = prepareServerSocket | bindListener
    SRTSOCKET bindsock_2 = prepareServerSocket();
    {
        int yes = 1;

        ASSERT_NE(srt_setsockflag(bindsock_2, SRTO_IPV6ONLY, &yes, sizeof yes), SRT_ERROR);
        ASSERT_TRUE(bindListener(bindsock_2, "::", 5000, true));
    }

    testAccept(bindsock_1, "127.0.0.1", 5000, true);
    testAccept(bindsock_2, "::1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);
}

TEST_F(ReuseAddr, ProtocolVersionFaux6)
{
    SRTST_REQUIRES(IPv6);

#if defined(_WIN32) || defined(CYGWIN)
    std::cout << "!!!WARNING!!!: On Windows connection to localhost this way isn't possible.\n"
        "Forcing test to pass, PLEASE FIX.\n";
    return;
#endif

    SRTSOCKET bindsock_1 = createListener("0.0.0.0", 5000, true);

    // We need a small interception in this one.
    // createListener = prepareServerSocket | bindListener
    SRTSOCKET bindsock_2 = prepareServerSocket();
    {
        int no = 0;

        ASSERT_NE(srt_setsockflag(bindsock_2, SRTO_IPV6ONLY, &no, sizeof no), SRT_ERROR);
        ASSERT_FALSE(bindListener(bindsock_2, "::", 5000, false));
    }

    testAccept(bindsock_1, "127.0.0.1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);
}
