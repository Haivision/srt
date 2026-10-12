/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "gtest/gtest.h"
#include "test_env.h"

#include <cstring>

#include <arpa/inet.h>

#include "srt.h"

namespace
{

// Routes the datagrams sent by one virtual transport into the peer
// transport, reporting a fixed source address - a wire between two
// sockets. This is the shape of the future WebSRT loopback transport.
struct Wire
{
    SRTVirtualTransport* peer;
    sockaddr_storage     source_addr;
    socklen_t            source_len;
};

int WireSend(void* opaq, const char* data, int len, const struct sockaddr* /*dst*/, int /*dstlen*/)
{
    Wire* w = (Wire*)opaq;
    if (srt_virtual_transport_inject(w->peer, data, len, (const struct sockaddr*)&w->source_addr, (int)w->source_len) ==
        -1)
        return -1;

    return len;
}

sockaddr_in FillV4(const char* ip, uint16_t port)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port);
    sa.sin_addr.s_addr = inet_addr(ip);
    return sa;
}

void FillWireSource(Wire& w, const sockaddr_in& sa)
{
    memset(&w.source_addr, 0, sizeof w.source_addr);
    memcpy(&w.source_addr, &sa, sizeof sa);
    w.source_len = sizeof sa;
}

} // namespace

class TestVirtualTransport : public srt::Test
{
protected:
    void setup() override {}

    void teardown() override
    {
        if (m_caller_sock != SRT_INVALID_SOCK)
            srt_close(m_caller_sock);
        if (m_accepted_sock != SRT_INVALID_SOCK)
            srt_close(m_accepted_sock);
        if (m_listener_sock != SRT_INVALID_SOCK)
            srt_close(m_listener_sock);
        if (m_caller_vt)
            srt_virtual_transport_release(m_caller_vt);
        if (m_listener_vt)
            srt_virtual_transport_release(m_listener_vt);
    }

    SRTSOCKET            m_caller_sock   = SRT_INVALID_SOCK;
    SRTSOCKET            m_accepted_sock = SRT_INVALID_SOCK;
    SRTSOCKET            m_listener_sock = SRT_INVALID_SOCK;
    SRTVirtualTransport* m_caller_vt     = NULL;
    SRTVirtualTransport* m_listener_vt   = NULL;
};

// A stock caller-listener handshake (induction first) and one payload
// message, with both directions carried by the virtual transport seam
// instead of UDP sockets.
TEST_F(TestVirtualTransport, ConnectListenSendReceive)
{
    // The addresses never reach the network; they only identify the
    // endpoints for the SRT dispatching machinery.
    const sockaddr_in listener_addr = FillV4("10.213.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.213.0.1", 5556);

    Wire caller_wire;   // what the caller sends is injected into the listener
    Wire listener_wire; // what the listener sends is injected into the caller

    // Listener side: attach the transport, then bind (virtually) and listen.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);

    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller side: attach the transport; the multiplexer is created on connect.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    m_caller_vt = srt_attach_virtual_transport(m_caller_sock, WireSend, &caller_wire);
    ASSERT_NE(m_caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    listener_wire.peer = m_caller_vt;
    FillWireSource(listener_wire, listener_addr);

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    sockaddr_in accept_addr;
    int         accept_len = sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    // The peer of the accepted socket is the source address carried by the
    // injected handshake packets.
    EXPECT_EQ(accept_addr.sin_addr.s_addr, caller_addr.sin_addr.s_addr);
    EXPECT_EQ(accept_addr.sin_port, caller_addr.sin_port);

    // One payload message from the caller to the accepted socket. Use epoll
    // to also check that read-readiness signaling works for virtual sockets.
    const char* payload     = "WebSRT virtual transport seam";
    const int   payload_len = (int)strlen(payload);
    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();

    const int pollid = srt_epoll_create();
    ASSERT_NE(pollid, SRT_ERROR);
    const int events = SRT_EPOLL_IN;
    ASSERT_NE(srt_epoll_add_usock(pollid, m_accepted_sock, &events), SRT_ERROR);

    SRT_EPOLL_EVENT ready[1];
    const int       nready = srt_epoll_uwait(pollid, ready, 1, 5000);
    EXPECT_EQ(nready, 1);
    if (nready == 1)
    {
        EXPECT_EQ(ready[0].fd, m_accepted_sock);
        EXPECT_NE(ready[0].events & SRT_EPOLL_IN, 0);
    }
    srt_epoll_release(pollid);

    char      buf[1456];
    const int recv_len = srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf);
    ASSERT_GT(recv_len, 0) << srt_getlasterror_str();
    EXPECT_EQ(recv_len, payload_len);
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
}
