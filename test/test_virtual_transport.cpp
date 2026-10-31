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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <unistd.h>

#include "srt.h"

namespace
{

// Routes the datagrams sent by one virtual transport into the peer
// transport, reporting a fixed source address - a wire between two
// sockets. This is the shape of an application-level loopback transport.
struct Wire
{
    SRTVirtualTransport* peer;
    sockaddr_storage     source_addr;
    socklen_t            source_len;
    // When not NULL, datagrams carrying this byte sequence in the payload
    // are swallowed to emulate a loss. Retransmissions carry the same
    // payload, so every copy of the packet disappears on the wire.
    const char* swallow_tag = NULL;
    // Counts the send callbacks fired through this wire. The detach test
    // freezes it around a post-detach send to prove that a detached
    // transport stops calling the owner's callback.
    std::atomic<long long> send_count;

    Wire()
        : peer()
        , source_addr()
        , source_len()
        , swallow_tag(NULL)
        , send_count(0)
    {
    }
};

int WireSend(void* opaq, const char* data, int len, const struct sockaddr* /*dst*/, int /*dstlen*/)
{
    Wire* w = (Wire*)opaq;
    ++w->send_count;
    if (w->swallow_tag && std::search(data, data + len, w->swallow_tag, w->swallow_tag + strlen(w->swallow_tag)) != data + len)
        return len;

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

// The same source-address fill for the switch below, which reports the
// shared listener address as the source of everything it forwards.
void FillSource(sockaddr_storage& addr, socklen_t& addr_len, const sockaddr_in& sa)
{
    memset(&addr, 0, sizeof addr);
    memcpy(&addr, &sa, sizeof sa);
    addr_len = sizeof sa;
}

// Routes the datagrams sent by ONE shared transport to one of several
// virtual transports, by destination address - a miniature of a
// gateway demultiplexing WebTransport sessions onto SRT sockets. Used where several
// callers are served by one listener: every accepted socket sends through
// the listener's multiplexer, and the destination address (the caller's
// source label) picks the caller whose transport must receive it.
struct Switch
{
    struct Route
    {
        sockaddr_in          dst;
        SRTVirtualTransport* vt;
    };
    std::vector<Route> routes;
    sockaddr_storage   source_addr;
    socklen_t          source_len = 0;
};

int SwitchSend(void* opaq, const char* data, int len, const struct sockaddr* dst, int /*dstlen*/)
{
    Switch*            sw = (Switch*)opaq;
    const sockaddr_in* d  = (const sockaddr_in*)dst;
    for (Switch::Route& r : sw->routes)
    {
        if (d->sin_family == AF_INET && d->sin_port == r.dst.sin_port && d->sin_addr.s_addr == r.dst.sin_addr.s_addr)
        {
            if (srt_virtual_transport_inject(
                    r.vt, data, len, (const struct sockaddr*)&sw->source_addr, (int)sw->source_len) == -1)
                return -1;
            break;
        }
    }

    // An unroutable datagram is swallowed, as a datagram lost on a wire.
    return len;
}

// The size gates of srt_virtual_transport_inject, mirrored from the seam
// for the tests below: the fixed SRT header (HEADER_TYPE, 4 x uint32_t)
// and the sanity bound on a single datagram (VT_MAX_DATAGRAM).
const int srt_header_size = 16;
const int vt_max_datagram = 65536;

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
    // The wires must live as long as the sockets: srt_close in teardown()
    // sends the shutdown through the virtual transport, which calls WireSend
    // with the wire pointer - after the test body and its locals are gone.
    Wire                 m_caller_wire;
    Wire                 m_listener_wire;
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

    Wire& caller_wire   = m_caller_wire;   // what the caller sends is injected into the listener
    Wire& listener_wire = m_listener_wire; // what the listener sends is injected into the caller

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
    const char* payload     = "virtual transport seam";
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

// The same wire as in ConnectListenSendReceive, but with
// SRTO_SKIP_INDUCTION set on both endpoints: the caller opens with the
// Conclusion handshake (cookie 0) and the listener accepts it without the
// Induction/cookie exchange. Proves the skip-induction path leaves the
// connected state and the data flow intact.
TEST_F(TestVirtualTransport, ConnectListenSendReceiveSkipInduction)
{
    const sockaddr_in listener_addr = FillV4("10.213.1.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.213.1.1", 5556);

    Wire& caller_wire   = m_caller_wire;
    Wire& listener_wire = m_listener_wire;

    const int yes = 1;

    // Listener side: the option must be set before listen() - the
    // SRTO_R_PRE restriction must reject the attempt to set it afterwards.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);

    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();
    EXPECT_EQ(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR);

    // Caller side: the option turns the first handshake into the Conclusion.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
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

    EXPECT_EQ(accept_addr.sin_addr.s_addr, caller_addr.sin_addr.s_addr);
    EXPECT_EQ(accept_addr.sin_port, caller_addr.sin_port);

    EXPECT_EQ(srt_getsockstate(m_caller_sock), SRTS_CONNECTED);
    EXPECT_EQ(srt_getsockstate(m_accepted_sock), SRTS_CONNECTED);

    // One payload message each way over the conclusion-first connection.
    const char* payload     = "skip-induction round trip";
    const int   payload_len = (int)strlen(payload);

    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();

    char buf[1456];
    ASSERT_GT(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();

    ASSERT_GT(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
}

// The hardening counterpart of the test above: the SKIP-INDUCTION cookie
// bypass must apply only to requests that arrive over the virtual
// transport. A listener bound to a real UDP address - even with
// SRTO_SKIP_INDUCTION set - must keep the stock cookie validation, because
// over plain UDP no enclosing transport authenticates the peer; accepting
// Conclusion-first handshakes there would let any forged packet spawn an
// accepted socket, the resource-exhaustion vector the cookie exists to
// prevent. A skip-induction caller without a virtual transport is
// therefore refused by srt_connect itself - the mirror of the
// listener-side gate - and nothing may reach the listener's accept queue.
TEST_F(TestVirtualTransport, SkipInductionRejectedOverPlainUDP)
{
    // Reserve a unique UDP port on the loopback with a probe socket bound
    // to port 0; the listener then binds the very same address. The other
    // tests of this suite never open an OS socket, so the port cannot
    // collide inside the suite.
    const int probe = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_NE(probe, -1);
    sockaddr_in listener_addr = FillV4("127.0.0.1", 0);
    ASSERT_EQ(bind(probe, (const sockaddr*)&listener_addr, sizeof listener_addr), 0);
    socklen_t addr_len = sizeof listener_addr;
    ASSERT_EQ(getsockname(probe, (sockaddr*)&listener_addr, &addr_len), 0);
    ASSERT_EQ(::close(probe), 0);

    const int yes             = 1;
    const int conn_timeout_ms = 5000;

    // Listener: the option is set (it would accept a Conclusion-first
    // caller over a virtual transport), but this channel is plain UDP.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller: set up to send the Conclusion immediately (cookie 0), over
    // plain UDP. The connect timeout is deliberately long so that a
    // refusal by waiting it out stands clearly apart from the up-front
    // refusal under test.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    // The caller never gets to spam its cookie-0 Conclusion: srt_connect
    // refuses a skip-induction caller without a virtual transport up
    // front. The refusal is synchronous, so the measured duration doubles
    // as the guard against a regression into the
    // retransmit-until-connect-timeout path.
    const std::chrono::steady_clock::time_point connect_start = std::chrono::steady_clock::now();
    EXPECT_EQ(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();
    const long long connect_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - connect_start).count();
    EXPECT_LT(connect_ms, conn_timeout_ms / 2);

    // Nothing ever reached the accept queue; the listener stays listening.
    const int pollid = srt_epoll_create();
    ASSERT_NE(pollid, SRT_ERROR);
    const int events = SRT_EPOLL_IN;
    ASSERT_NE(srt_epoll_add_usock(pollid, m_listener_sock, &events), SRT_ERROR);

    SRT_EPOLL_EVENT ready[1];
    EXPECT_EQ(srt_epoll_uwait(pollid, ready, 1, 300), 0);
    srt_epoll_release(pollid);

    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_LISTENING);
}

// The seed must survive the connection completion on both sides: the
// caller's through postConnect, the accepted socket's through
// acceptAndRespond - both of which consult a per-peer-address RTT cache
// keyed by the (fake, reused) virtual transport addresses. The first live
// ACK/ACKACK samples must then replace the seed with the true in-memory RTT.
TEST_F(TestVirtualTransport, InitialRTTSeedThenLiveReplace)
{
    const sockaddr_in listener_addr = FillV4("10.215.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.215.0.1", 5556);

    Wire& caller_wire   = m_caller_wire;
    Wire& listener_wire = m_listener_wire;

    const int yes         = 1;
    const int seed_rtt_ms = 300;
    const int bad_rtt_ms  = 30001;

    // Both endpoints skip the Induction round trip (the phase that normally
    // delivers the first RTT sample) and seed the estimator explicitly, as
    // a WebTransport gateway does with the QUIC smoothed RTT.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_INITIAL_RTT, &seed_rtt_ms, sizeof seed_rtt_ms), SRT_ERROR)
        << srt_getlasterror_str();
    // The seed is range-checked: values above 30000 ms are rejected.
    EXPECT_EQ(srt_setsockopt(m_listener_sock, 0, SRTO_INITIAL_RTT, &bad_rtt_ms, sizeof bad_rtt_ms), SRT_ERROR);
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);

    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_INITIAL_RTT, &seed_rtt_ms, sizeof seed_rtt_ms), SRT_ERROR)
        << srt_getlasterror_str();
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

    // Pre-data: no ACK/ACKACK samples exist yet, so both estimators must
    // still hold the seed (300 ms), not the 100 ms default and not a stale
    // per-address cache value.
    SRT_TRACEBSTATS perf;
    ASSERT_EQ(srt_bstats(m_caller_sock, &perf, 0), 0);
    EXPECT_DOUBLE_EQ(perf.msRTT, 300.0);
    ASSERT_EQ(srt_bstats(m_accepted_sock, &perf, 0), 0);
    EXPECT_DOUBLE_EQ(perf.msRTT, 300.0);

    // Round trips of payload make both sides measure the true (in-memory,
    // sub-millisecond) RTT; the live samples must replace the seed.
    const char* payload     = "initial RTT seed";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];

    for (int i = 0; i < 20; ++i)
    {
        ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
            << srt_getlasterror_str();
        ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
        ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
            << srt_getlasterror_str();
        ASSERT_EQ(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
        usleep(10000);
    }

    ASSERT_EQ(srt_bstats(m_caller_sock, &perf, 0), 0);
    EXPECT_LT(perf.msRTT, 100.0);
    ASSERT_EQ(srt_bstats(m_accepted_sock, &perf, 0), 0);
    EXPECT_LT(perf.msRTT, 100.0);
}

// The receiver-side contract of SRTO_TLPKTDROP: a data packet lost on the
// wire while later packets keep flowing must be dropped as too-late-to-play
// once the first subsequent packet passes its TSBPD play time, instead of
// holding the stream back waiting for a retransmission. The wire swallows
// every copy (original and retransmissions) of one message; the stream must
// complete with exactly the other messages delivered in order, and the
// receiver must count the dropped packet in the stats. This is the receive
// side that makes the sender's too-late retransmit skip correct: rexmitting
// a packet the receiver would drop anyway wastes bandwidth.
TEST_F(TestVirtualTransport, ReceiverDropsTooLateMissingPacket)
{
    const sockaddr_in listener_addr = FillV4("10.216.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.216.0.1", 5556);

    Wire& caller_wire   = m_caller_wire;
    Wire& listener_wire = m_listener_wire;

    const int yes        = 1;
    const int latency_ms = 60;

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_LATENCY, &latency_ms, sizeof latency_ms), SRT_ERROR)
        << srt_getlasterror_str();
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);

    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_LATENCY, &latency_ms, sizeof latency_ms), SRT_ERROR)
        << srt_getlasterror_str();
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

    // Fail fast instead of hanging should the too-late drop not happen.
    const int rcv_timeout_ms = 5000;
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    // From now on every datagram carrying the victim message disappears.
    caller_wire.swallow_tag = "SWALLOW-ME";

    const int num_msgs = 10;
    char      payloads[num_msgs][64];
    for (int i = 0; i < num_msgs; ++i)
    {
        snprintf(payloads[i], sizeof payloads[i], "vt-late msg %02d%s", i + 1, i == 0 ? " SWALLOW-ME" : "");
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_sendmsg(m_caller_sock, payloads[i], len, SRT_MSGTTL_INF, false), len)
            << srt_getlasterror_str();
    }

    // Messages 2..N must be delivered, in order. The delivery of message 2
    // at all IS the too-late drop under test: TSBPD does not play a packet
    // positioned after a sequence gap until the gap is either filled by a
    // retransmission (swallowed here) or dropped as too late.
    char buf[1456];
    for (int i = 1; i < num_msgs; ++i)
    {
        const int recv_len = srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf);
        ASSERT_GT(recv_len, 0) << srt_getlasterror_str();
        ASSERT_EQ(recv_len, (int)strlen(payloads[i]));
        EXPECT_EQ(memcmp(buf, payloads[i], recv_len), 0) << "unexpected message at position " << i;
    }

    // The victim packet was counted as too-late-to-play on the receiver.
    SRT_TRACEBSTATS perf;
    ASSERT_EQ(srt_bstats(m_accepted_sock, &perf, 0), 0);
    EXPECT_GE(perf.pktRcvDropTotal, 1);
}

// Counts the open file descriptors of the current process that are sockets.
static int CountSocketFds()
{
    DIR* dir = opendir("/proc/self/fd");
    if (!dir)
        return -1;

    int            count = 0;
    struct dirent* entry;
    char           link[64];
    while ((entry = readdir(dir)))
    {
        if (entry->d_name[0] == '.')
            continue;
        const std::string path  = std::string("/proc/self/fd/") + entry->d_name;
        const ssize_t     nread = readlink(path.c_str(), link, sizeof link - 1);
        if (nread <= 0)
            continue;
        link[nread] = 0;
        if (strncmp(link, "socket:", 7) == 0)
            ++count;
    }
    closedir(dir);
    return count;
}

// The per-session gateway shape: attached listeners that enter the
// listening state with NO explicit srt_bind (the virtual binding is
// synthesized by srt_listen), several of them simultaneously, each wired to
// its own caller through an independent in-memory transport. Also asserts
// that the whole lifecycle creates no OS socket.
TEST_F(TestVirtualTransport, ListenWithoutBindConcurrentSessions)
{
    // Both auto-bound listeners synthesize the same address label; this must
    // be harmless because every attached socket owns a dedicated multiplexer.
    const sockaddr_in listener_addr = FillV4("10.213.255.254", 0);

    const int sockets_before = CountSocketFds();
    ASSERT_GE(sockets_before, 0);

    const int yes = 1;

    struct Session
    {
        SRTSOCKET            listener    = SRT_INVALID_SOCK;
        SRTSOCKET            caller      = SRT_INVALID_SOCK;
        SRTSOCKET            accepted    = SRT_INVALID_SOCK;
        SRTVirtualTransport* listener_vt = NULL;
        SRTVirtualTransport* caller_vt   = NULL;
        Wire                 caller_wire;
        Wire                 listener_wire;

        ~Session()
        {
            if (accepted != SRT_INVALID_SOCK)
                srt_close(accepted);
            if (caller != SRT_INVALID_SOCK)
                srt_close(caller);
            if (listener != SRT_INVALID_SOCK)
                srt_close(listener);
            if (caller_vt)
                srt_virtual_transport_release(caller_vt);
            if (listener_vt)
                srt_virtual_transport_release(listener_vt);
        }
    } a, b;

    const sockaddr_in a_caller_addr = FillV4("10.214.0.1", 5556);
    const sockaddr_in b_caller_addr = FillV4("10.214.1.1", 5556);

    // Two attached listeners, each set to accept Conclusion-first handshakes,
    // put into the listening state directly - no srt_bind() anywhere.
    for (Session* s : {&a, &b})
    {
        s->listener = srt_create_socket();
        ASSERT_NE(s->listener, SRT_INVALID_SOCK);
        ASSERT_NE(srt_setsockopt(s->listener, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
            << srt_getlasterror_str();
        s->listener_vt = srt_attach_virtual_transport(s->listener, WireSend, &s->listener_wire);
        ASSERT_NE(s->listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
        ASSERT_NE(srt_listen(s->listener, 4), SRT_ERROR) << srt_getlasterror_str();
    }

    // The synthesized binding is a pure label, reported by srt_getsockname.
    sockaddr_in self_addr;
    int         self_len = sizeof self_addr;
    ASSERT_NE(srt_getsockname(a.listener, (sockaddr*)&self_addr, &self_len), SRT_ERROR);
    EXPECT_EQ(self_addr.sin_addr.s_addr, listener_addr.sin_addr.s_addr);

    // Two attached callers with SRTO_SKIP_INDUCTION, each wired to its own
    // listener; both pairs are connected at the same time.
    struct
    {
        Session*          s;
        const sockaddr_in caller_addr;
    } callers[2] = {{&a, a_caller_addr}, {&b, b_caller_addr}};

    for (const auto& c : callers)
    {
        c.s->caller = srt_create_socket();
        ASSERT_NE(c.s->caller, SRT_INVALID_SOCK);
        ASSERT_NE(srt_setsockopt(c.s->caller, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
            << srt_getlasterror_str();
        c.s->caller_vt = srt_attach_virtual_transport(c.s->caller, WireSend, &c.s->caller_wire);
        ASSERT_NE(c.s->caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
        c.s->caller_wire.peer = c.s->listener_vt;
        FillWireSource(c.s->caller_wire, c.caller_addr);
        c.s->listener_wire.peer = c.s->caller_vt;
        FillWireSource(c.s->listener_wire, listener_addr);

        ASSERT_NE(srt_connect(c.s->caller, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
            << srt_getlasterror_str();

        sockaddr_in accept_addr;
        int         accept_len = sizeof accept_addr;
        c.s->accepted          = srt_accept(c.s->listener, (sockaddr*)&accept_addr, &accept_len);
        ASSERT_NE(c.s->accepted, SRT_INVALID_SOCK) << srt_getlasterror_str();
    }

    EXPECT_EQ(srt_getsockstate(a.caller), SRTS_CONNECTED);
    EXPECT_EQ(srt_getsockstate(a.accepted), SRTS_CONNECTED);
    EXPECT_EQ(srt_getsockstate(b.caller), SRTS_CONNECTED);
    EXPECT_EQ(srt_getsockstate(b.accepted), SRTS_CONNECTED);

    // Interleaved payload round trips over both sessions at once.
    const char* payload     = "attached concurrent session";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];

    for (Session* s : {&a, &b})
    {
        ASSERT_EQ(srt_sendmsg(s->caller, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
            << srt_getlasterror_str();
    }
    for (Session* s : {&a, &b})
    {
        ASSERT_EQ(srt_recvmsg(s->accepted, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
        EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
    }
    for (Session* s : {&a, &b})
    {
        ASSERT_EQ(srt_sendmsg(s->accepted, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
            << srt_getlasterror_str();
    }
    for (Session* s : {&a, &b})
    {
        ASSERT_EQ(srt_recvmsg(s->caller, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
        EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
    }

    // Everything above must have run without creating a single OS socket.
    EXPECT_LE(CountSocketFds(), sockets_before);
}

// The single-session regression test of the bind-less listen: a socket
// with an attached virtual transport enters the listening state through
// srt_listen() alone - the virtual binding is synthesized inside the call -
// and a virtual caller then completes the DEFAULT handshake (induction
// first, no options) with it, where ListenWithoutBindConcurrentSessions
// exercises the SRTO_SKIP_INDUCTION shape. The accepted socket must be a
// fully working data channel, proven by a payload round trip in both
// directions.
TEST_F(TestVirtualTransport, ListenWithoutBindSingleSession)
{
    // The caller dials the synthesized label itself: it is the address
    // srt_getsockname() reports, and the wire, not the address, routes the
    // datagrams. The caller label is unique to this test, as the address
    // labels of the other tests are to theirs.
    const sockaddr_in listener_addr = FillV4("10.213.255.254", 0);
    const sockaddr_in caller_addr   = FillV4("10.219.0.1", 5556);

    Wire& caller_wire   = m_caller_wire;
    Wire& listener_wire = m_listener_wire;

    // Listener side: attach the transport and listen - no srt_bind() anywhere.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);

    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // The synthesized binding is a pure label, reported by srt_getsockname.
    sockaddr_in self_addr;
    int         self_len = sizeof self_addr;
    ASSERT_NE(srt_getsockname(m_listener_sock, (sockaddr*)&self_addr, &self_len), SRT_ERROR)
        << srt_getlasterror_str();
    EXPECT_EQ(self_addr.sin_addr.s_addr, listener_addr.sin_addr.s_addr);

    // Caller side: plain socket, plain handshake; the multiplexer is created
    // on connect.
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

    EXPECT_EQ(srt_getsockstate(m_caller_sock), SRTS_CONNECTED);
    EXPECT_EQ(srt_getsockstate(m_accepted_sock), SRTS_CONNECTED);

    // One payload message each way over the bind-less accepted socket.
    const char* payload     = "bind-less listen round trip";
    const int   payload_len = (int)strlen(payload);

    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();

    char buf[1456];
    ASSERT_GT(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();

    ASSERT_GT(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
}

// The gate around the capability above: the synthesized binding is an
// attached-socket privilege, not a general relaxation of the listen state
// machine. A stock socket - no transport attached - that was never bound
// must keep being rejected by srt_listen() with SRT_EUNBOUNDSOCK, and the
// refusal must leave the socket untouched in the INIT state.
TEST_F(TestVirtualTransport, StockSocketListenWithoutBindRejected)
{
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);

    EXPECT_EQ(srt_listen(m_listener_sock, 4), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EUNBOUNDSOCK) << srt_getlasterror_str();

    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_INIT);
}

// The negative side of the attachment contract: a virtual transport can
// only be attached to a fresh socket. Attaching to a socket that is
// already bound must fail (the transport could not take over the datagram
// exchange of an existing multiplexer), and so must attaching a second
// transport to a socket that already has one. The positive half -
// attaching to a fresh socket and binding afterwards - is exercised by
// every other test of this suite.
TEST_F(TestVirtualTransport, AttachRejectedOnBoundOrAttachedSocket)
{
    // Leave the INIT state the ordinary way: bind to a real UDP address.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    const sockaddr_in udp_addr = FillV4("127.0.0.1", 0);
    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&udp_addr, (int)sizeof udp_addr), SRT_ERROR)
        << srt_getlasterror_str();

    EXPECT_EQ(srt_attach_virtual_transport(m_listener_sock, WireSend, &m_listener_wire), (SRTVirtualTransport*)NULL);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVOP) << srt_getlasterror_str();

    // A fresh socket takes one transport...
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    m_caller_vt = srt_attach_virtual_transport(m_caller_sock, WireSend, &m_caller_wire);
    ASSERT_NE(m_caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();

    // ...but not a second one; the first stays attached and is cleaned up
    // by the fixture teardown.
    EXPECT_EQ(srt_attach_virtual_transport(m_caller_sock, WireSend, &m_caller_wire), (SRTVirtualTransport*)NULL);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();
}

// The negative side of the detachment contract: a socket with no transport
// attached cannot detach one, and a transport that was detached from its
// socket no longer accepts injected datagrams - it is not a receiving
// channel of the socket anymore. The handle itself must remain valid
// through all of this, up to the srt_virtual_transport_release() call,
// which the fixture teardown performs after closing the socket; the clean,
// crash-free teardown of this test is the proof of that half.
TEST_F(TestVirtualTransport, DetachRejectedWithoutTransportInjectAfterDetach)
{
    // A socket that never had a transport attached.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    EXPECT_EQ(srt_detach_virtual_transport(m_listener_sock), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();

    // A bound socket with its transport attached - the state in which a
    // live session would detach mid-life. The binding label is unique to
    // this test, as the address labels of the other tests are to theirs.
    const sockaddr_in self_addr = FillV4("10.217.0.2", 5555);

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    m_caller_vt = srt_attach_virtual_transport(m_caller_sock, WireSend, &m_caller_wire);
    ASSERT_NE(m_caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    ASSERT_NE(srt_bind(m_caller_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_NE(srt_detach_virtual_transport(m_caller_sock), SRT_ERROR) << srt_getlasterror_str();

    // A datagram of a well-formed size (a full SRT header plus payload,
    // exactly as carried by UDP) injected into the detached handle must be
    // refused, the refusal being about the detached state and not the
    // datagram itself.
    const char        datagram[32] = {0};
    const sockaddr_in src_addr     = FillV4("10.217.0.1", 5556);
    EXPECT_EQ(srt_virtual_transport_inject(
                  m_caller_vt, datagram, (int)sizeof datagram, (const sockaddr*)&src_addr, (int)sizeof src_addr),
              SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_ESOCKFAIL) << srt_getlasterror_str();

    // The handle survives the failed injection and stays valid for the
    // release in the fixture teardown, after the socket is closed.
}

// The backpressure contract of the pending queue: the queue stands in for
// the kernel socket buffer of a UDP socket, so once it is full, further
// injected datagrams are dropped and srt_virtual_transport_inject reports
// the drop by failing - exactly as a UDP socket drops datagrams when its
// receive buffer is depleted. The queue is filled here before the socket
// is ever bound, the extreme of injecting faster than the socket consumes
// (there is no multiplexer yet, so nothing drains the queue); once the
// socket enters the listening state, its worker drains the backlog - every
// oversized datagram is discarded at the receive gate, without a line of
// SRT processing - and the very same transport then carries a complete
// working session: the overflow must leave the socket and the seam
// healthy.
TEST_F(TestVirtualTransport, QueuePressureOverflowDropsInjects)
{
    const sockaddr_in listener_addr = FillV4("10.220.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.220.0.1", 5556);

    Wire& caller_wire   = m_caller_wire;
    Wire& listener_wire = m_listener_wire;

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();

    // Junk datagrams: valid for inject (between the header size and the
    // sanity bound) but oversized for the receiving unit (capacity 1472
    // payload bytes = MTU 1500 - IP/UDP 28), so the worker later discards
    // every one of them at the receive() size gate, before any SRT-level
    // processing. No bind, listen or connect yet: no multiplexer exists,
    // no worker drains the transport, and every accepted inject stays
    // pending. The queue capacity (VT_MAX_PENDING in the seam) is 1024;
    // inject far past it.
    const int               junk_len = 4096;
    const std::vector<char> junk(junk_len, 0xAB);
    const sockaddr_in       junk_src    = FillV4("10.220.0.9", 9999);
    const int               max_pending = 1024;

    int  accepted   = 0;
    bool overflowed = false;
    for (int i = 0; i < max_pending * 2; ++i)
    {
        if (srt_virtual_transport_inject(
                m_listener_vt, junk.data(), junk_len, (const struct sockaddr*)&junk_src, (int)sizeof junk_src) ==
            SRT_ERROR)
        {
            overflowed = true;
            break;
        }
        ++accepted;
    }
    EXPECT_TRUE(overflowed) << "inject never reported backpressure over " << max_pending * 2 << " datagrams";
    EXPECT_GE(accepted, 1);
    // The drop reports through the API error channel, and it is sticky
    // while nothing consumes: the queue stays full.
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_ESOCKFAIL) << srt_getlasterror_str();
    EXPECT_EQ(srt_virtual_transport_inject(
                  m_listener_vt, junk.data(), junk_len, (const struct sockaddr*)&junk_src, (int)sizeof junk_src),
              SRT_ERROR);

    // The socket stays healthy: bound, listening, and carrying a full
    // session - handshake and payload both ways - over the very transport
    // whose queue was driven to overflow.
    const int yes = 1;
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    m_caller_vt = srt_attach_virtual_transport(m_caller_sock, WireSend, &caller_wire);
    ASSERT_NE(m_caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);
    listener_wire.peer = m_caller_vt;
    FillWireSource(listener_wire, listener_addr);

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    sockaddr_in accept_addr;
    int         accept_len = sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    const char* payload     = "queue pressure round trip";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];

    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
}

// The size gate of srt_virtual_transport_inject: a datagram shorter than
// the fixed SRT header (4 x 32-bit words) cannot even carry the header,
// and one above the seam's sanity bound cannot be a UDP datagram. Both
// must be rejected without queuing anything, the refusal must leave the
// socket and its transport fully usable, and the exact header size - the
// smallest datagram the seam accepts - must pass the gate.
TEST_F(TestVirtualTransport, InjectInvalidSizeRejected)
{
    const sockaddr_in listener_addr = FillV4("10.221.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.221.0.1", 5556);

    Wire& caller_wire   = m_caller_wire;
    Wire& listener_wire = m_listener_wire;

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();

    const char              small[16] = {0};
    const std::vector<char> huge((size_t)vt_max_datagram + 1, 0xCD);
    const sockaddr_in       src_addr = FillV4("10.221.0.9", 5556);

    // Below the SRT header size: nothing to dispatch on.
    for (const int len : {0, 8, 15})
    {
        EXPECT_EQ(srt_virtual_transport_inject(
                      m_listener_vt, small, len, (const struct sockaddr*)&src_addr, (int)sizeof src_addr),
                  SRT_ERROR)
            << "len " << len << " below the header size was not rejected";
    }
    // Above the datagram sanity bound.
    EXPECT_EQ(
        srt_virtual_transport_inject(
            m_listener_vt, huge.data(), (int)huge.size(), (const struct sockaddr*)&src_addr, (int)sizeof src_addr),
        SRT_ERROR)
        << "len " << huge.size() << " above the datagram bound was not rejected";
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_ESOCKFAIL) << srt_getlasterror_str();

    // Exactly the header size passes the gate - the rejects above were
    // about the size, not about a broken transport. The datagram carries
    // no payload, so the worker later discards it in the handshake
    // validation, the way any garbage datagram on a UDP listener is
    // discarded.
    EXPECT_NE(srt_virtual_transport_inject(
                  m_listener_vt, small, srt_header_size, (const struct sockaddr*)&src_addr, (int)sizeof src_addr),
              SRT_ERROR)
        << srt_getlasterror_str();

    // The socket is unaffected: a complete session over the same
    // transport follows.
    const int yes = 1;
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    m_caller_vt = srt_attach_virtual_transport(m_caller_sock, WireSend, &caller_wire);
    ASSERT_NE(m_caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);
    listener_wire.peer = m_caller_vt;
    FillWireSource(listener_wire, listener_addr);

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    sockaddr_in accept_addr;
    int         accept_len = sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    const char* payload     = "inject size gate round trip";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];

    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
}

// Detaching the transport of a socket in the middle of a live stream: the
// detach runs on its own thread while a sender keeps sending and the peer
// keeps receiving, and it must block only until the in-flight send
// callbacks return - never deadlock. Afterwards the owner's send callback
// must never fire again (the contract that frees the owner to release the
// callback resources once srt_detach_virtual_transport has returned), a
// further send must neither hang nor crash, and the peer must observe the
// end of the stream through the bounded receive timeout instead of
// waiting forever. Every join is bounded, so any deadlock fails the test
// rather than hanging it.
TEST_F(TestVirtualTransport, DetachDuringLiveTraffic)
{
    const sockaddr_in listener_addr = FillV4("10.223.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.223.0.1", 5556);

    Wire& caller_wire   = m_caller_wire;
    Wire& listener_wire = m_listener_wire;

    const int yes = 1;

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    m_listener_vt = srt_attach_virtual_transport(m_listener_sock, WireSend, &listener_wire);
    ASSERT_NE(m_listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    caller_wire.peer = m_listener_vt;
    FillWireSource(caller_wire, caller_addr);

    ASSERT_NE(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
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

    // Fail fast instead of hanging should the stream end go unobserved.
    // The window still sits well above the default 120 ms TSBPD latency,
    // so the healthy stream is never cut by it.
    const int rcv_timeout_ms = 500;
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    // A sanity round trip first, so the stream provably flows before the
    // detach.
    const char* warmup     = "vt-detach warmup";
    const int   warmup_len = (int)strlen(warmup);
    char        buf[1456];
    ASSERT_EQ(srt_sendmsg(m_caller_sock, warmup, warmup_len, SRT_MSGTTL_INF, false), warmup_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), warmup_len) << srt_getlasterror_str();

    // The mid-stream signal: the receiver announces through the promise
    // once a fair chunk of the stream has been delivered, and the main
    // thread waits for that with a deadline before detaching. The promise
    // is shared so that a failure path cannot leave it dangling.
    std::shared_ptr<std::promise<void> > stream_flowing(new std::promise<void>);
    auto                                 stream_flowing_f = stream_flowing->get_future();

    struct RecvStats
    {
        int  received         = 0;
        bool ended_by_timeout = false;
        bool sequence_ok      = true;
    };

    const int stream_msgs = 2000;

    // The sender paces the stream so that it is guaranteed to still be
    // flowing when the transport detaches; the pacing shapes the traffic,
    // it is not a wait for a condition.
    auto sender = std::async(std::launch::async,
                             [this, stream_msgs]()
                             {
                                 int sent_ok = 0;
                                 for (int i = 0; i < stream_msgs; ++i)
                                 {
                                     char      msg[64];
                                     const int len = snprintf(msg, sizeof msg, "vt-detach stream %05d", i);
                                     if (srt_sendmsg(m_caller_sock, msg, len, SRT_MSGTTL_INF, false) == SRT_ERROR)
                                         break; // a clean refusal is acceptable; a hang or a crash is not
                                     ++sent_ok;
                                     usleep(500);
                                 }
                                 return sent_ok;
                             });

    auto receiver = std::async(std::launch::async,
                               [this, stream_flowing]()
                               {
                                   RecvStats st;
                                   char      buf2[1456];
                                   for (;;)
                                   {
                                       const int len = srt_recvmsg(m_accepted_sock, buf2, (int)sizeof buf2);
                                       if (len == SRT_ERROR)
                                       {
                                           // The receive timeout is the stream-end signal: after a
                                           // few consecutive quiet windows the stream is over.
                                           if (st.received >= 50)
                                               st.ended_by_timeout = true;
                                           break;
                                       }

                                       char      want[64];
                                       const int wlen =
                                           snprintf(want, sizeof want, "vt-detach stream %05d", st.received);
                                       if (len != wlen || memcmp(buf2, want, len) != 0)
                                       {
                                           st.sequence_ok = false;
                                           break;
                                       }
                                       ++st.received;
                                       if (st.received == 50)
                                           stream_flowing->set_value();
                                   }
                                   return st;
                               });

    // Wait until the stream is provably flowing, bounded.
    ASSERT_EQ(stream_flowing_f.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "the stream never flowed before the detach";

    // The detach itself, on its own thread and bounded: it must return
    // even while the sender is in mid-stream.
    auto detacher = std::async(std::launch::async, [this]() { return srt_detach_virtual_transport(m_caller_sock); });
    ASSERT_EQ(detacher.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "srt_detach_virtual_transport deadlocked against the live traffic";
    EXPECT_EQ(detacher.get(), 0) << srt_getlasterror_str();

    // Both threads must finish well within their bounds - the sender runs
    // to the end of its paced stream, the receiver until its quiet
    // windows run out.
    ASSERT_EQ(sender.wait_for(std::chrono::seconds(15)), std::future_status::ready)
        << "the sender thread hung after the detach";
    const int sent_ok = sender.get();
    ASSERT_EQ(receiver.wait_for(std::chrono::seconds(15)), std::future_status::ready)
        << "the receiver thread hung after the detach";
    const RecvStats rs = receiver.get();

    EXPECT_GE(sent_ok, 1);
    EXPECT_TRUE(rs.sequence_ok) << "the received stream broke its sequence";
    EXPECT_GE(rs.received, 50);
    EXPECT_TRUE(rs.ended_by_timeout) << "the peer never observed the end of the stream";

    // After the detach the caller's datagrams are dropped inside the
    // seam: the send callback is never invoked again, which is what frees
    // the owner to release the callback resources once the detach has
    // returned. The send itself must neither hang nor crash; being
    // silently dropped, it may as well be refused.
    const long long callbacks_before = caller_wire.send_count.load();
    const char*     post_payload     = "vt-detach after detach";
    const int       post_len         = (int)strlen(post_payload);
    const int       post_res         = srt_sendmsg(m_caller_sock, post_payload, post_len, SRT_MSGTTL_INF, false);
    EXPECT_TRUE(post_res == post_len || post_res == SRT_ERROR)
        << "the post-detach send neither completed nor failed: " << srt_getlasterror_str();
    EXPECT_EQ(caller_wire.send_count.load(), callbacks_before)
        << "the send callback fired after srt_detach_virtual_transport returned";
}

// Several callers, ONE listener: every accepted socket shares the
// listener's virtual multiplexer, so all three sessions exchange their
// datagrams through the same listener transport, with the switch
// demultiplexing by the destination address (the caller's source label) -
// the shape of a gateway serving several WebTransport sessions over one SRT
// listener. The callers connect sequentially, and every accepted socket
// must independently carry payload, both ways, with all three connections
// open at once.
TEST_F(TestVirtualTransport, MultiAcceptSharedListenerMultiplexer)
{
    const sockaddr_in listener_addr = FillV4("10.222.0.2", 5555);

    const int yes = 1;

    // Everything lives in this struct because the wires must outlive the
    // sockets: the destructor closes the sockets first - which sends
    // their shutdown through the wires - and releases the transports
    // only afterwards.
    struct Session
    {
        SRTSOCKET            caller    = SRT_INVALID_SOCK;
        SRTSOCKET            accepted  = SRT_INVALID_SOCK;
        SRTVirtualTransport* caller_vt = NULL;
        Wire                 caller_wire;
        sockaddr_in          caller_addr;
    };

    struct Rig
    {
        SRTSOCKET            listener    = SRT_INVALID_SOCK;
        SRTVirtualTransport* listener_vt = NULL;
        Switch               listener_switch;
        Session              sessions[3];

        ~Rig()
        {
            for (Session& s : sessions)
            {
                if (s.accepted != SRT_INVALID_SOCK)
                    srt_close(s.accepted);
                if (s.caller != SRT_INVALID_SOCK)
                    srt_close(s.caller);
            }
            if (listener != SRT_INVALID_SOCK)
                srt_close(listener);
            for (Session& s : sessions)
                if (s.caller_vt)
                    srt_virtual_transport_release(s.caller_vt);
            if (listener_vt)
                srt_virtual_transport_release(listener_vt);
        }
    } rig;

    rig.listener = srt_create_socket();
    ASSERT_NE(rig.listener, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(rig.listener, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    rig.listener_vt = srt_attach_virtual_transport(rig.listener, SwitchSend, &rig.listener_switch);
    ASSERT_NE(rig.listener_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
    FillSource(rig.listener_switch.source_addr, rig.listener_switch.source_len, listener_addr);

    ASSERT_NE(srt_bind(rig.listener, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(rig.listener, 4), SRT_ERROR) << srt_getlasterror_str();

    // Three callers, sequentially - deterministically - each with its own
    // transport wired into the shared listener transport.
    const int num_sessions = (int)(sizeof rig.sessions / sizeof rig.sessions[0]);
    for (int i = 0; i < num_sessions; ++i)
    {
        Session& s    = rig.sessions[i];
        s.caller_addr = FillV4("10.222.1.1", (uint16_t)(5556 + i));

        s.caller = srt_create_socket();
        ASSERT_NE(s.caller, SRT_INVALID_SOCK);
        ASSERT_NE(srt_setsockopt(s.caller, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
            << srt_getlasterror_str();
        s.caller_vt = srt_attach_virtual_transport(s.caller, WireSend, &s.caller_wire);
        ASSERT_NE(s.caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();
        s.caller_wire.peer = rig.listener_vt;
        FillWireSource(s.caller_wire, s.caller_addr);
        rig.listener_switch.routes.push_back(Switch::Route{s.caller_addr, s.caller_vt});

        ASSERT_NE(srt_connect(s.caller, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
            << srt_getlasterror_str();

        sockaddr_in accept_addr;
        int         accept_len = sizeof accept_addr;
        s.accepted             = srt_accept(rig.listener, (sockaddr*)&accept_addr, &accept_len);
        ASSERT_NE(s.accepted, SRT_INVALID_SOCK) << srt_getlasterror_str();

        EXPECT_EQ(accept_addr.sin_addr.s_addr, s.caller_addr.sin_addr.s_addr);
        EXPECT_EQ(accept_addr.sin_port, s.caller_addr.sin_port);
        EXPECT_EQ(srt_getsockstate(s.caller), SRTS_CONNECTED);
        EXPECT_EQ(srt_getsockstate(s.accepted), SRTS_CONNECTED);

        // Fail fast instead of hanging should the shared-multiplexer
        // routing ever cross the sessions.
        const int rcv_timeout_ms = 5000;
        ASSERT_NE(srt_setsockopt(s.accepted, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
            << srt_getlasterror_str();
        ASSERT_NE(srt_setsockopt(s.caller, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
            << srt_getlasterror_str();
    }

    // Interleaved traffic with all three connections open: every caller
    // sends, every accepted socket receives its own - and only its own -
    // message; then the reverse direction.
    char payloads[3][64];
    char buf[1456];
    for (int i = 0; i < num_sessions; ++i)
    {
        snprintf(payloads[i], sizeof payloads[i], "multi-accept session %d", i);
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_sendmsg(rig.sessions[i].caller, payloads[i], len, SRT_MSGTTL_INF, false), len)
            << srt_getlasterror_str();
    }
    for (int i = 0; i < num_sessions; ++i)
    {
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_recvmsg(rig.sessions[i].accepted, buf, (int)sizeof buf), len) << srt_getlasterror_str();
        EXPECT_EQ(memcmp(buf, payloads[i], len), 0) << "session " << i << " received a foreign message";
    }
    for (int i = 0; i < num_sessions; ++i)
    {
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_sendmsg(rig.sessions[i].accepted, payloads[i], len, SRT_MSGTTL_INF, false), len)
            << srt_getlasterror_str();
    }
    for (int i = 0; i < num_sessions; ++i)
    {
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_recvmsg(rig.sessions[i].caller, buf, (int)sizeof buf), len) << srt_getlasterror_str();
        EXPECT_EQ(memcmp(buf, payloads[i], len), 0) << "session " << i << " received a foreign message";
    }
}

// SRTO_SKIP_INDUCTION has no rendezvous counterpart: a rendezvous
// connection has no listener that could accept a Conclusion-first
// handshake, so the cookie-less opener cannot be authenticated by either
// side. The combination must therefore be rejected in both orders in
// which the two options can be set. With SRTO_RENDEZVOUS set first, the
// rejection happens right at the setsockopt call and the rejected setting
// must not stick; with SRTO_SKIP_INDUCTION set first, both options are
// accepted at set time and srt_connect must refuse to proceed instead of
// silently starting an unauthenticated handshake.
TEST_F(TestVirtualTransport, SkipInductionRendezvousRejectedInBothOrders)
{
    const int yes = 1;

    // Order 1: rendezvous first, the option second - rejected at set time.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    EXPECT_EQ(srt_setsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();

    bool skip     = true;
    int  skip_len = sizeof skip;
    ASSERT_NE(srt_getsockopt(m_listener_sock, 0, SRTO_SKIP_INDUCTION, &skip, &skip_len), SRT_ERROR)
        << srt_getlasterror_str();
    EXPECT_FALSE(skip);

    // Order 2: the option first, rendezvous second - both sets succeed,
    // and the rejection happens at connect time. The wire peer stays NULL:
    // should the connect-time check ever disappear, the waveahands would
    // be dropped by WireSend and the bounded connect timeout below would
    // turn the silent proceeding into a fast failure of this test, not a
    // hang.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes), SRT_ERROR) << srt_getlasterror_str();
    m_caller_vt = srt_attach_virtual_transport(m_caller_sock, WireSend, &m_caller_wire);
    ASSERT_NE(m_caller_vt, (SRTVirtualTransport*)NULL) << srt_getlasterror_str();

    const sockaddr_in self_addr = FillV4("10.218.0.1", 5555);
    ASSERT_NE(srt_bind(m_caller_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr), SRT_ERROR)
        << srt_getlasterror_str();

    const int conn_timeout_ms = 100;
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    const sockaddr_in peer_addr = FillV4("10.218.0.2", 5555);
    EXPECT_EQ(srt_connect(m_caller_sock, (const sockaddr*)&peer_addr, (int)sizeof peer_addr), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();

    // The refused connect must leave no trace in the internal rendezvous
    // queue: startConnect() used to register the connector ahead of the
    // skip-induction refusal, and the abandoned entry lived until its TTL -
    // the rendezvous connect timeout times ten - expired. Closing the socket
    // inside that window deadlocked the library: the closer joined the
    // receiving worker while holding the global socket-table lock, which
    // the worker itself was waiting for while processing the stale entry.
    // The refusal now unregisters the connector before throwing, so the
    // fixture teardown closes the socket right after the refused connect
    // returns - should the registration ever leak again, this close hangs
    // and the timeout-wrapped test invocation fails.
}

// Both options introduced with the virtual transport seam must round-trip
// through srt_getsockopt() in their set states as well as in the disabled
// defaults the socket is created with: the boolean SRTO_SKIP_INDUCTION
// and the integer SRTO_INITIAL_RTT.
TEST_F(TestVirtualTransport, SkipInductionAndInitialRTTOptionReadback)
{
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);

    // The disabled defaults: induction not skipped, no explicit RTT seed.
    bool skip     = true;
    int  skip_len = sizeof skip;
    ASSERT_NE(srt_getsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &skip, &skip_len), SRT_ERROR)
        << srt_getlasterror_str();
    EXPECT_EQ(skip_len, (int)sizeof skip);
    EXPECT_FALSE(skip);

    int rtt     = -1;
    int rtt_len = sizeof rtt;
    ASSERT_NE(srt_getsockopt(m_caller_sock, 0, SRTO_INITIAL_RTT, &rtt, &rtt_len), SRT_ERROR) << srt_getlasterror_str();
    EXPECT_EQ(rtt_len, (int)sizeof rtt);
    EXPECT_EQ(rtt, 0);

    // What was set must be read back unchanged.
    const int yes         = 1;
    const int seed_rtt_ms = 1500;
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_INITIAL_RTT, &seed_rtt_ms, sizeof seed_rtt_ms), SRT_ERROR)
        << srt_getlasterror_str();

    skip     = false;
    skip_len = sizeof skip;
    ASSERT_NE(srt_getsockopt(m_caller_sock, 0, SRTO_SKIP_INDUCTION, &skip, &skip_len), SRT_ERROR)
        << srt_getlasterror_str();
    EXPECT_TRUE(skip);

    rtt     = 0;
    rtt_len = sizeof rtt;
    ASSERT_NE(srt_getsockopt(m_caller_sock, 0, SRTO_INITIAL_RTT, &rtt, &rtt_len), SRT_ERROR) << srt_getlasterror_str();
    EXPECT_EQ(rtt, seed_rtt_ms);
}
