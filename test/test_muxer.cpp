#include "gtest/gtest.h"
#include <thread>
#include "srt.h"
#include "threadname.h"

int client_pollid = SRT_ERROR;
SRTSOCKET m_client_sock = SRT_ERROR;

void clientSocket()
{
    ThreadName::set("clientSocket");

    int yes = 1;
    int no = 0;

    m_client_sock = srt_create_socket();
    ASSERT_NE(m_client_sock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_SNDSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockflag(m_client_sock, SRTO_SENDER, &yes, sizeof yes), SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_client_sock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    int epoll_out = SRT_EPOLL_OUT;
    srt_epoll_add_usock(client_pollid, m_client_sock, &epoll_out);

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9999);

    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr* psa = (sockaddr*)&sa;

    ASSERT_NE(srt_connect(m_client_sock, psa, sizeof sa), SRT_ERROR);

    // Socket readiness for connection is checked by polling on WRITE allowed sockets.

    {
	    int rlen = 2;
	    SRTSOCKET read[2];

	    int wlen = 2;
	    SRTSOCKET write[2];

        ASSERT_NE(srt_epoll_wait(client_pollid, read, &rlen,
                                 write, &wlen,
                                 -1, // -1 is set for debuging purpose.
                                     // in case of production we need to set appropriate value
                                 0, 0, 0, 0), SRT_ERROR);

	    ASSERT_EQ(rlen, 0); // get exactly one write event without reads
	    ASSERT_EQ(wlen, 1); // get exactly one write event without reads
	    ASSERT_EQ(write[0], m_client_sock); // for our client socket
    }

    char buffer[1316] = {1, 2, 3, 4};
    ASSERT_NE(srt_sendmsg(m_client_sock, buffer, sizeof buffer,
    		              -1, // infinit ttl
                          true // in order must be set to true
                          ), SRT_ERROR);
}


TEST(Muxer, ipv4_only)
{
   	ASSERT_EQ(srt_startup(), 0);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    int server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    int yes = 1;
    int no = 0;

    SRTSOCKET m_bindsock = srt_create_socket();
    ASSERT_NE(m_bindsock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_bindsock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(m_bindsock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    int epoll_in = SRT_EPOLL_IN;
    srt_epoll_add_usock(server_pollid, m_bindsock, &epoll_in);

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9999);

    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr* psa = (sockaddr*)&sa;

    ASSERT_NE(srt_bind(m_bindsock, psa, sizeof sa), SRT_ERROR);
    ASSERT_NE(srt_listen(m_bindsock, SOMAXCONN), SRT_ERROR);
/*
    SRTSOCKET s = srt_create_socket();
    ASSERT_NE(s, SRT_ERROR);

    sockaddr_in6 sender_addr6{};
    sender_addr6.sin6_family = AF_INET6;
    sender_addr6.sin6_port   = htons(9999);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", &sender_addr6.sin6_addr), 1);
    ASSERT_NE(srt_bind(s, (sockaddr*)&sender_addr6, sizeof(sender_addr6)), SRT_ERROR);
*/
    std::thread client(&clientSocket);

    { // wait for connection from client
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
            //ASSERT_EQ(wlen, 0); // get exactly one read event without writes
            ASSERT_EQ(read[0], m_bindsock); // read event is for bind socket
    }

    sockaddr_in scl;
    int sclen = sizeof scl;

    SRTSOCKET m_sock = srt_accept(m_bindsock, (sockaddr*)&scl, &sclen);
    ASSERT_NE(m_sock, SRT_INVALID_SOCK);

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
        //ASSERT_EQ(wlen, 0); // get exactly one read event without writes
        ASSERT_EQ(read[0], m_sock); // read event is for bind socket
    }

    char pattern[4] = {1, 2, 3, 4};

    ASSERT_EQ(srt_recvmsg(m_sock, buffer, sizeof buffer), 1316);

    ASSERT_EQ(memcmp(pattern, buffer, 4), 0);

    //srt_close(s);    
    srt_close(m_sock);
    srt_close(m_bindsock);
    srt_close(m_client_sock); // cannot close m_client_sock after srt_sendmsg because of issue in api.c:2346

    client.join();

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);

    srt_cleanup();
}

TEST(Muxer, ipv4_with_ipv6)
{
   	ASSERT_EQ(srt_startup(), 0);

    //srt_setloglevel(LOG_DEBUG);

    client_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, client_pollid);

    int server_pollid = srt_epoll_create();
    ASSERT_NE(SRT_ERROR, server_pollid);

    int yes = 1;
    int no = 0;

    SRTSOCKET m_bindsock = srt_create_socket();
    ASSERT_NE(m_bindsock, SRT_ERROR);

    ASSERT_NE(srt_setsockopt(m_bindsock, 0, SRTO_RCVSYN, &no, sizeof no), SRT_ERROR); // for async connect
    ASSERT_NE(srt_setsockopt(m_bindsock, 0, SRTO_TSBPDMODE, &yes, sizeof yes), SRT_ERROR);

    int epoll_in = SRT_EPOLL_IN;
    srt_epoll_add_usock(server_pollid, m_bindsock, &epoll_in);

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(9999);

    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    sockaddr* psa = (sockaddr*)&sa;

    ASSERT_NE(srt_bind(m_bindsock, psa, sizeof sa), SRT_ERROR);
    ASSERT_NE(srt_listen(m_bindsock, SOMAXCONN), SRT_ERROR);

    SRTSOCKET s = srt_create_socket();
    ASSERT_NE(s, SRT_ERROR);

    sockaddr_in6 sender_addr6;
    memset(&sender_addr6, 0, sizeof sender_addr6);
    sender_addr6.sin6_family = AF_INET6;
    sender_addr6.sin6_port   = htons(9999);
    ASSERT_EQ(inet_pton(AF_INET6, "::1", &sender_addr6.sin6_addr), 1);

    // Set the IPv6only flag for the socket that should beind to the same port
    // as another socket binding to IPv4 address, otherwise the binding may fail,
    // depending on the current value of IPV6_V6ONLY option.
    ASSERT_EQ(srt_setsockflag(s, SRTO_IPV6ONLY, &yes, sizeof yes), 0);

    ASSERT_NE(srt_bind(s, (sockaddr*)&sender_addr6, sizeof(sender_addr6)), SRT_ERROR);

    std::thread client(&clientSocket);

    { // wait for connection from client
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
            //ASSERT_EQ(wlen, 0); // get exactly one read event without writes
            ASSERT_EQ(read[0], m_bindsock); // read event is for bind socket
    }

    sockaddr_storage scl;
    int sclen = sizeof scl;

    SRTSOCKET m_sock = srt_accept(m_bindsock, (sockaddr*)&scl, &sclen);
    ASSERT_NE(m_sock, SRT_INVALID_SOCK);

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
        //ASSERT_EQ(wlen, 0); // get exactly one read event without writes
        ASSERT_EQ(read[0], m_sock); // read event is for bind socket
    }

    char pattern[4] = {1, 2, 3, 4};

    ASSERT_EQ(srt_recvmsg(m_sock, buffer, sizeof buffer), 1316);

    ASSERT_EQ(memcmp(pattern, buffer, 4), 0);

    srt_close(s);    
    srt_close(m_sock);
    srt_close(m_bindsock);
    srt_close(m_client_sock); // cannot close m_client_sock after srt_sendmsg because of issue in api.c:2346

    client.join();

    (void)srt_epoll_release(client_pollid);
    (void)srt_epoll_release(server_pollid);

    srt_cleanup();
}
