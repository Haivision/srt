#include "gtest/gtest.h"
#include "common.h"
#include "srt.h"
#include "udt.h"
#include <thread>

// Copied from ../apps/apputi.cpp, can't really link this file here.
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


int client_pollid = SRT_ERROR;
SRTSOCKET m_client_sock = SRT_ERROR;

void clientSocket(std::string ip, int port, bool expect_success)
{
    int yes = 1;
    int no = 0;

    m_client_sock = srt_create_socket();
    ASSERT_NE(m_client_sock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(m_client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    int epoll_out = SRT_EPOLL_OUT;
    srt_epoll_add_usock(client_pollid, m_client_sock, &epoll_out);

    sockaddr_any sa = CreateAddr(ip, port);

    std::cout << "Connecting to: " << sa.str() << std::endl;

    int connect_res = srt_connect(m_client_sock, sa.get(), sa.size());

    if (connect_res == -1)
    {
        std::cout << "srt_connect: " << srt_getlasterror_str() << std::endl;
    }

    if (expect_success)
    {
        EXPECT_NE(connect_res, -1);
        // Socket readiness for connection is checked by polling on WRITE allowed sockets.

        {
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            EXPECT_NE(srt_epoll_wait(client_pollid, read, &rlen,
                        write, &wlen,
                        -1, // -1 is set for debuging purpose.
                        // in case of production we need to set appropriate value
                        0, 0, 0, 0), SRT_ERROR);


            EXPECT_EQ(rlen, 0); // get exactly one write event without reads
            EXPECT_EQ(wlen, 1); // get exactly one write event without reads
            EXPECT_EQ(write[0], m_client_sock); // for our client socket
        }

        char buffer[1316] = {1, 2, 3, 4};
        EXPECT_NE(srt_sendmsg(m_client_sock, buffer, sizeof buffer,
                    -1, // infinit ttl
                    true // in order must be set to true
                    ),
                SRT_ERROR);

    }
    else
    {
        EXPECT_EQ(connect_res, -1);
    }

    std::cout << "Client exit\n";
}

int server_pollid = SRT_ERROR;

void serverSocket(std::string ip, int port, bool expect_success)
{
    int yes = 1;
    int no = 0;

	SRTSOCKET m_bindsock = srt_create_socket();
	ASSERT_NE(m_bindsock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_bindsock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(m_bindsock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    int epoll_in = SRT_EPOLL_IN;
    srt_epoll_add_usock(server_pollid, m_bindsock, &epoll_in);

    sockaddr_any sa = CreateAddr(ip, port);

    std::cout << "Bind to: " << sa.str() << std::endl;

    int bind_res = srt_bind(m_bindsock, sa.get(), sa.size());
    if (!expect_success)
    {
        std::cout << "Binding should fail: " << srt_getlasterror_str() << std::endl;
        ASSERT_EQ(bind_res, SRT_ERROR);
        return;
    }

    ASSERT_NE(bind_res, SRT_ERROR);

    ASSERT_NE(srt_listen(m_bindsock, SOMAXCONN), SRT_ERROR);

    if (ip == "0.0.0.0")
        ip = "127.0.0.1"; // override wildcard
    else if ( ip == "::")
        ip = "::1";

    std::thread client(clientSocket, ip, port, expect_success);

    { // wait for connection from client
	    int rlen = 2;
	    SRTSOCKET read[2];

	    int wlen = 2;
	    SRTSOCKET write[2];

        ASSERT_NE(srt_epoll_wait(server_pollid,
	                                     read,  &rlen,
	                                     write, &wlen,
	                                     3000, // -1 is set for debuging purpose.
	                                         // in case of production we need to set appropriate value
	                                     0, 0, 0, 0), SRT_ERROR );


	    ASSERT_EQ(rlen, 1); // get exactly one read event without writes
	    ASSERT_EQ(wlen, 0); // get exactly one read event without writes
	    ASSERT_EQ(read[0], m_bindsock); // read event is for bind socket    	
    }

    sockaddr_any scl;

	SRTSOCKET m_sock = srt_accept(m_bindsock, scl.get(), &scl.len);
    if (m_sock == -1)
    {
        std::cout << "srt_accept: " << srt_getlasterror_str() << std::endl;
    }
	ASSERT_NE(m_sock, SRT_INVALID_SOCK);

    sockaddr_any showacp = (sockaddr*)&scl;
    std::cout << "Accepted from: " << showacp.str() << std::endl;

    srt_epoll_add_usock(server_pollid, m_sock, &epoll_in); // wait for input

    char buffer[1316];
    { // wait for 1316 packet from client
        int rlen = 2;
        SRTSOCKET read[2];

        int wlen = 2;
        SRTSOCKET write[2];

        ASSERT_NE(srt_epoll_wait(server_pollid,
                                         read,  &rlen,
                                         write, &wlen,
                                         -1, // -1 is set for debuging purpose.
                                             // in case of production we need to set appropriate value
                                         0, 0, 0, 0), SRT_ERROR );


        ASSERT_EQ(rlen, 1); // get exactly one read event without writes
        ASSERT_EQ(wlen, 0); // get exactly one read event without writes
        ASSERT_EQ(read[0], m_sock); // read event is for bind socket        
    }

    char pattern[4] = {1, 2, 3, 4};

    ASSERT_EQ(srt_recvmsg(m_sock, buffer, sizeof buffer),
    	      1316);

    EXPECT_EQ(memcmp(pattern, buffer, sizeof pattern), 0);

    client.join();
    srt_close(m_sock);
    srt_close(m_bindsock);
    srt_close(m_client_sock); // cannot close m_client_sock after srt_sendmsg because of issue in api.c:2346 

    std::cout << "Server exit\n";
}

TEST(ReuseAddr, SameAddr1) {
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    std::thread server_1(serverSocket, "127.0.0.1", 5000, true);
    server_1.join();

    std::thread server_2(serverSocket, "127.0.0.1", 5000, true);
    server_2.join();

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, SameAddr2) {
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    std::thread server_1(serverSocket, "127.0.0.2", 5000, true);
    server_1.join();

    std::thread server_2(serverSocket, "127.0.0.2", 5000, true);
    server_2.join();

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, DiffAddr) {
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    serverSocket("127.0.0.1", 5000, true);
    serverSocket("127.0.0.2", 5000, true);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();
}

TEST(ReuseAddr, Wildcard)
{
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    serverSocket("0.0.0.0", 5000, true);
    serverSocket("127.0.0.2", 5000, false);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();

}


TEST(ReuseAddr, ProtocolVersion)
{
    ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    serverSocket("::", 5000, true);
    serverSocket("0.0.0.0", 5000, true);

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);
    srt_cleanup();

}
