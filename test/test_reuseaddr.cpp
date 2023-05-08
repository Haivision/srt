#include "gtest/gtest.h"
#include <thread>
#include <future>
#ifndef _WIN32
#include <ifaddrs.h>
#endif

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

// Copied from ../apps/apputil.cpp, can't really link this file here.
sockaddr_any CreateAddr(const std::string& name, unsigned short port, int pref_family = AF_INET)
{
    using namespace std;

    // Handle empty name.
    // If family is specified, empty string resolves to ANY of that family.
    // If not, it resolves to IPv4 ANY (to specify IPv6 any, use [::]).
    if (name == "")
    {
        sockaddr_any result(pref_family == AF_INET6 ? pref_family : AF_INET);
        result.hport(port);
        return result;
    }

    bool first6 = pref_family != AF_INET;
    int families[2] = {AF_INET6, AF_INET};
    if (!first6)
    {
        families[0] = AF_INET;
        families[1] = AF_INET6;
    }

    for (int i = 0; i < 2; ++i)
    {
        int family = families[i];
        sockaddr_any result (family);

        // Try to resolve the name by pton first
        if (inet_pton(family, name.c_str(), result.get_addr()) == 1)
        {
            result.hport(port); // same addr location in ipv4 and ipv6
            return result;
        }
    }

    // If not, try to resolve by getaddrinfo
    // This time, use the exact value of pref_family

    sockaddr_any result;
    addrinfo fo = {
        0,
        pref_family,
        0, 0,
        0, 0,
        NULL, NULL
    };

    addrinfo* val = nullptr;
    int erc = getaddrinfo(name.c_str(), nullptr, &fo, &val);
    if (erc == 0)
    {
        result.set(val->ai_addr);
        result.len = result.size();
        result.hport(port); // same addr location in ipv4 and ipv6
    }
    freeaddrinfo(val);

    return result;
}

#ifdef _WIN32

// On Windows there's a function for it, but it requires an extra
// iphlp library to be attached to the executable, which is kinda
// problematic. Temporarily block tests using this function on Windows.

std::string GetLocalIP(int af = AF_UNSPEC)
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

std::string GetLocalIP(int af = AF_UNSPEC)
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

int client_pollid = SRT_ERROR;
SRTSOCKET g_client_sock = SRT_ERROR;

void clientSocket(std::string ip, int port, bool expect_success)
{
    int yes = 1;
    int no = 0;

    int family = AF_INET;
    std::string famname = "IPv4";
    if (ip.substr(0, 2) == "6.")
    {
        family = AF_INET6;
        ip = ip.substr(2);
        famname = "IPv6";
    }

    std::cout << "[T/C] Creating client socket\n";

    g_client_sock = srt_create_socket();
    ASSERT_NE(g_client_sock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(g_client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(g_client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);

    ASSERT_NE(srt_setsockopt(g_client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    int epoll_out = SRT_EPOLL_OUT;
    srt_epoll_add_usock(client_pollid, g_client_sock, &epoll_out);

    sockaddr_any sa = CreateAddr(ip, port, family);

    std::cout << "[T/C] Connecting to: " << sa.str() << " (" << famname << ")" << std::endl;

    int connect_res = srt_connect(g_client_sock, sa.get(), sa.size());

    if (connect_res == -1)
    {
        std::cout << "srt_connect: " << srt_getlasterror_str() << std::endl;
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

            std::cout << "[T/C] Waiting for connection readiness...\n";

            EXPECT_NE(srt_epoll_wait(client_pollid, read, &rlen,
                        write, &wlen,
                        -1, // -1 is set for debuging purpose.
                        // in case of production we need to set appropriate value
                        0, 0, 0, 0), SRT_ERROR);


            EXPECT_EQ(rlen, 0); // get exactly one write event without reads
            EXPECT_EQ(wlen, 1); // get exactly one write event without reads
            EXPECT_EQ(write[0], g_client_sock); // for our client socket

            char buffer[1316] = {1, 2, 3, 4};
            EXPECT_NE(srt_sendmsg(g_client_sock, buffer, sizeof buffer,
                        -1, // infinit ttl
                        true // in order must be set to true
                        ),
                    SRT_ERROR);
        }
        else
        {
            std::cout << "[T/C] (NOT TESTING TRANSMISSION - CONNECTION FAILED ALREADY)\n";
        }
    }
    else
    {
        EXPECT_EQ(connect_res, -1);
    }

    std::cout << "[T/C] Client exit\n";
}

int server_pollid = SRT_ERROR;

SRTSOCKET prepareSocket()
{
    SRTSOCKET bindsock = srt_create_socket();
    EXPECT_NE(bindsock, SRT_ERROR);

    int yes = 1;
    int no = 0;

    EXPECT_NE(srt_setsockopt(bindsock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    EXPECT_NE(srt_setsockopt(bindsock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    int epoll_in = SRT_EPOLL_IN;

    std::cout << "[T/S] Listener/binder sock @" << bindsock << " added to server_pollid\n";
    srt_epoll_add_usock(server_pollid, bindsock, &epoll_in);

    return bindsock;
}

bool bindSocket(SRTSOCKET bindsock, std::string ip, int port, bool expect_success)
{
    sockaddr_any sa = CreateAddr(ip, port);

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

    SRTSOCKET bindsock = prepareSocket();

    if (!bindListener(bindsock, ip, port, expect_success))
        return SRT_INVALID_SOCK;

    return bindsock;
}

SRTSOCKET createBinder(std::string ip, int port, bool expect_success)
{
    std::cout << "[T/S] serverSocket: creating binder socket\n";

    SRTSOCKET bindsock = prepareSocket();

    if (!bindSocket(bindsock, ip, port, expect_success))
    {
        srt_close(bindsock);
        return SRT_INVALID_SOCK;
    }

    return bindsock;
}

void testAccept(SRTSOCKET bindsock, std::string ip, int port, bool expect_success)
{

    auto run = [ip, port, expect_success]() { clientSocket(ip, port, expect_success); };

    auto launched = std::async(std::launch::async, run);

    AtReturnJoin<decltype(launched)> atreturn_join {launched};

    { // wait for connection from client
        int rlen = 2;
        SRTSOCKET read[2];

        int wlen = 2;
        SRTSOCKET write[2];

        std::cout << "[T/S] Wait 10s for acceptance on @" << bindsock << " ...\n";

        ASSERT_NE(srt_epoll_wait(server_pollid,
                                         read,  &rlen,
                                         write, &wlen,
                                         10000, // -1 is set for debuging purpose.
                                             // in case of production we need to set appropriate value
                                         0, 0, 0, 0), SRT_ERROR );


        ASSERT_EQ(rlen, 1); // get exactly one read event without writes
        ASSERT_EQ(wlen, 0); // get exactly one read event without writes
        ASSERT_EQ(read[0], bindsock); // read event is for bind socket    	
    }

    sockaddr_any scl;

    SRTSOCKET accepted_sock = srt_accept(bindsock, scl.get(), &scl.len);
    if (accepted_sock == -1)
    {
        std::cout << "srt_accept: " << srt_getlasterror_str() << std::endl;
    }
    ASSERT_NE(accepted_sock, SRT_INVALID_SOCK);

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

        ASSERT_NE(srt_epoll_wait(server_pollid,
                                         read,  &rlen,
                                         write, &wlen,
                                         -1, // -1 is set for debuging purpose.
                                             // in case of production we need to set appropriate value
                                         0, 0, 0, 0), SRT_ERROR );


        ASSERT_EQ(rlen, 1); // get exactly one read event without writes
        ASSERT_EQ(wlen, 0); // get exactly one read event without writes
        ASSERT_EQ(read[0], accepted_sock); // read event is for bind socket        
    }

    char pattern[4] = {1, 2, 3, 4};

    ASSERT_EQ(srt_recvmsg(accepted_sock, buffer, sizeof buffer),
              1316);

    EXPECT_EQ(memcmp(pattern, buffer, sizeof pattern), 0);

    std::cout << "[T/S] closing sockets: ACP:@" << accepted_sock << " LSN:@" << bindsock << " CLR:@" << g_client_sock << " ...\n";
    ASSERT_NE(srt_close(accepted_sock), SRT_ERROR);
    ASSERT_NE(srt_close(g_client_sock), SRT_ERROR); // cannot close g_client_sock after srt_sendmsg because of issue in api.c:2346 

    std::cout << "[T/S] joining client async...\n";
    launched.get();
}

void shutdownListener(SRTSOCKET bindsock)
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

TEST(ReuseAddr, SameAddr1)
{
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    SRTSOCKET bindsock_1 = createBinder("127.0.0.1", 5000, true);
    SRTSOCKET bindsock_2 = createListener("127.0.0.1", 5000, true);

    testAccept(bindsock_2, "127.0.0.1", 5000, true);

    std::thread s1(shutdownListener, bindsock_1);
    std::thread s2(shutdownListener, bindsock_2);

    s1.join();
    s2.join();

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, SameAddr2)
{
    std::string localip = GetLocalIP(AF_INET);
    if (localip == "")
        return; // DISABLE TEST if this doesn't work.

    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    SRTSOCKET bindsock_1 = createBinder(localip, 5000, true);
    SRTSOCKET bindsock_2 = createListener(localip, 5000, true);

    testAccept(bindsock_2, localip, 5000, true);

    shutdownListener(bindsock_1);

    // Test simple close and reuse the multiplexer
    ASSERT_NE(srt_close(bindsock_2), SRT_ERROR);

    SRTSOCKET bindsock_3 = createListener(localip, 5000, true);
    testAccept(bindsock_3, localip, 5000, true);

    shutdownListener(bindsock_3);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, SameAddrV6)
{
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    SRTSOCKET bindsock_1 = createBinder("::1", 5000, true);
    SRTSOCKET bindsock_2 = createListener("::1", 5000, true);

    testAccept(bindsock_2, "::1", 5000, true);

    shutdownListener(bindsock_1);

    // Test simple close and reuse the multiplexer
    ASSERT_NE(srt_close(bindsock_2), SRT_ERROR);

    SRTSOCKET bindsock_3 = createListener("::1", 5000, true);
    testAccept(bindsock_3, "::1", 5000, true);

    shutdownListener(bindsock_3);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}


TEST(ReuseAddr, DiffAddr)
{
    std::string localip = GetLocalIP(AF_INET);
    if (localip == "")
        return; // DISABLE TEST if this doesn't work.

    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    SRTSOCKET bindsock_1 = createBinder("127.0.0.1", 5000, true);
    SRTSOCKET bindsock_2 = createListener(localip, 5000, true);

    //std::thread server_1(testAccept, bindsock_1, "127.0.0.1", 5000, true);
    //server_1.join();

    testAccept(bindsock_2, localip, 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, Wildcard)
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

    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    SRTSOCKET bindsock_1 = createListener("0.0.0.0", 5000, true);

    // Binding a certain address when wildcard is already bound should fail.
    SRTSOCKET bindsock_2 = createBinder(localip, 5000, false);

    testAccept(bindsock_1, "127.0.0.1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, Wildcard6)
{
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

    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    // This must be obligatory set before binding a socket to "::"
    int strict_ipv6 = 1;

    SRTSOCKET bindsock_1 = prepareSocket();
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

    bindsock_1 = prepareSocket();
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

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, ProtocolVersion6)
{
#if defined(_WIN32) || defined(CYGWIN)
    std::cout << "!!!WARNING!!!: On Windows connection to localhost this way isn't possible.\n"
        "Forcing test to pass, PLEASE FIX.\n";
    return;
#endif
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    SRTSOCKET bindsock_1 = createListener("0.0.0.0", 5000, true);

    // We need a small interception in this one.
    // createListener = prepareSocket | bindListener
    SRTSOCKET bindsock_2 = prepareSocket();
    {
        int yes = 1;

        ASSERT_NE(srt_setsockflag(bindsock_2, SRTO_IPV6ONLY, &yes, sizeof yes), SRT_ERROR);
        ASSERT_TRUE(bindListener(bindsock_2, "::", 5000, true));
    }

    testAccept(bindsock_1, "127.0.0.1", 5000, true);
    testAccept(bindsock_2, "::1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, ProtocolVersionFaux6)
{
#if defined(_WIN32) || defined(CYGWIN)
    std::cout << "!!!WARNING!!!: On Windows connection to localhost this way isn't possible.\n"
        "Forcing test to pass, PLEASE FIX.\n";
    return;
#endif
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    SRTSOCKET bindsock_1 = createListener("0.0.0.0", 5000, true);

    // We need a small interception in this one.
    // createListener = prepareSocket | bindListener
    SRTSOCKET bindsock_2 = prepareSocket();
    {
        int no = 0;

        ASSERT_NE(srt_setsockflag(bindsock_2, SRTO_IPV6ONLY, &no, sizeof no), SRT_ERROR);
        ASSERT_FALSE(bindListener(bindsock_2, "::", 5000, false));
    }

    testAccept(bindsock_1, "127.0.0.1", 5000, true);

    shutdownListener(bindsock_1);
    shutdownListener(bindsock_2);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}
