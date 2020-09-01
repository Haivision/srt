#include "gtest/gtest.h"
#include <thread>
#include "srt.h"

class TestMuxer
    : public ::testing::Test
{
protected:
    TestMuxer()
    {
        // initialization code here
    }

    ~TestMuxer()
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

        m_server_pollid = srt_epoll_create();
        ASSERT_NE(SRT_ERROR, m_server_pollid);

        m_client_pollid = srt_epoll_create();
        ASSERT_NE(SRT_ERROR, m_client_pollid);

        int yes = 1;
        int no = 0;
        ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
        ASSERT_NE(srt_setsockflag(m_caller_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);
        ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

        int epoll_out = SRT_EPOLL_OUT;
        srt_epoll_add_usock(m_client_pollid, m_caller_sock, &epoll_out);
    }

    void TearDown()
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        srt_epoll_release(m_client_pollid);
        srt_epoll_release(m_server_pollid);
        srt_close(m_listener_sock_ipv4);
        srt_close(m_listener_sock_ipv6);
        srt_cleanup();
    }

public:
    void ClientThread()
    {
        sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons(m_listen_port);
        ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
        ASSERT_NE(srt_connect(m_caller_sock, (sockaddr*)&sa, sizeof sa), SRT_ERROR);

        // Socket readiness for connection is checked by polling on WRITE allowed sockets.
        {
            int rlen = 2;
            SRTSOCKET read[2];

            int wlen = 2;
            SRTSOCKET write[2];

            ASSERT_NE(srt_epoll_wait(m_client_pollid, read, &rlen,
                                    write, &wlen,
                                    -1, // -1 is set for debuging purpose.
                                        // in case of production we need to set appropriate value
                                    0, 0, 0, 0), SRT_ERROR);

            ASSERT_EQ(rlen, 0); // get exactly one write event without reads
            ASSERT_EQ(wlen, 1); // get exactly one write event without reads
            ASSERT_EQ(write[0], m_caller_sock); // for our client socket
        }

        char buffer[1316] = {1, 2, 3, 4};
        ASSERT_NE(srt_sendmsg(m_caller_sock, buffer, sizeof buffer,
                            -1, // infinit ttl
                            true // in order must be set to true
                            ), SRT_ERROR);
    }

protected:
    SRTSOCKET m_caller_sock;
    SRTSOCKET m_listener_sock_ipv4;
    SRTSOCKET m_listener_sock_ipv6;
    int m_client_pollid = SRT_ERROR;
    int m_server_pollid = SRT_ERROR;
    const int m_listen_port = 4200;
};


TEST_F(TestMuxer, IPv4_and_IPv6)
{
    int yes = 1;
    int no = 0;

    // 1. Create IPv4 listening socket
    m_listener_sock_ipv4 = srt_create_socket();
    ASSERT_NE(m_listener_sock_ipv4, SRT_ERROR);
    ASSERT_NE(srt_setsockopt(m_listener_sock_ipv4, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(m_listener_sock_ipv4, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    // 2. Add the IPv4 socket to epoll
    int epoll_in = SRT_EPOLL_IN;
    srt_epoll_add_usock(m_server_pollid, m_listener_sock_ipv4, &epoll_in);

    // 3. Bind to IPv4 address.
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(m_listen_port);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);
    ASSERT_NE(srt_bind(m_listener_sock_ipv4, (sockaddr*)&sa, sizeof sa), SRT_ERROR);
    ASSERT_NE(srt_listen(m_listener_sock_ipv4, SOMAXCONN), SRT_ERROR);

    // 4. Create IPv6 socket bound to the same port as IPv4 socket
    m_listener_sock_ipv6 = srt_create_socket();
    ASSERT_NE(m_listener_sock_ipv6, SRT_ERROR);
    sockaddr_in6 sa_v6;
    memset(&sa_v6, 0, sizeof sa_v6);
    sa_v6.sin6_family = AF_INET6;
    sa_v6.sin6_port   = htons(m_listen_port);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", &sa_v6.sin6_addr), 1);

    // Set the IPv6only flag for the socket that should be bound to the same port
    // as another socket binding to IPv4 address, otherwise the binding may fail,
    // depending on the current value of IPV6ONLY option.
    ASSERT_EQ(srt_setsockflag(m_listener_sock_ipv6, SRTO_IPV6ONLY, &yes, sizeof yes), 0);
    ASSERT_NE(srt_bind(m_listener_sock_ipv6, (sockaddr*)&sa_v6, sizeof(sa_v6)), SRT_ERROR);

    std::thread client(&TestMuxer::ClientThread, this);

    { // wait for connection from client
        int rlen = 2;
        SRTSOCKET read[2];

        int wlen = 2;
        SRTSOCKET write[2];

        ASSERT_NE(srt_epoll_wait(m_server_pollid,
                                 read,  &rlen,
                                 write, &wlen,
                                 -1, // -1 is set for debuging purpose.
                                     // in case of production we need to set appropriate value
                                 0, 0, 0, 0), SRT_ERROR );

        ASSERT_EQ(rlen, 1); // get exactly one read event without writes
        ASSERT_EQ(read[0], m_listener_sock_ipv4) << "Read event on wrong socket";
    }

    sockaddr_storage scl;
    int sclen = sizeof scl;

    SRTSOCKET accepted_sock = srt_accept(m_listener_sock_ipv4, (sockaddr*)&scl, &sclen);
    ASSERT_NE(accepted_sock, SRT_INVALID_SOCK);

    srt_epoll_add_usock(m_server_pollid, accepted_sock, &epoll_in); // wait for input

    char buffer[1316];
    {   // wait for 1316 packet from client
        int rlen = 2;
        SRTSOCKET read[2];

        int wlen = 2;
        SRTSOCKET write[2];

        ASSERT_NE(srt_epoll_wait(m_server_pollid,
                                 read,  &rlen,
                                 write, &wlen,
                                 -1, // -1 is set for debuging purpose.
                                     // in case of production we need to set appropriate value
                                  0, 0, 0, 0), SRT_ERROR );


        ASSERT_EQ(rlen, 1); // get exactly one read event without writes
        //ASSERT_EQ(wlen, 0); // get exactly one read event without writes
        ASSERT_EQ(read[0], accepted_sock); // read event is for bind socket
    }

    char pattern[4] = {1, 2, 3, 4};
    ASSERT_EQ(srt_recvmsg(accepted_sock, buffer, sizeof buffer), 1316);
    ASSERT_EQ(memcmp(pattern, buffer, 4), 0);

    srt_close(accepted_sock);
    client.join();
}
