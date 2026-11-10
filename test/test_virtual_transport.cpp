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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <dirent.h>
#include <sys/socket.h>
#include <unistd.h>

#include "srt.h"
#include "srttransport.h"

#include "core.h" // CHandShake, CPacket: the wire-level handshake pins; CUDT: the internal bind entry

namespace
{

// Routes the datagrams of one endpoint's queue into the peer endpoint's
// queue, reporting a fixed source address - a wire between two sockets,
// used the way an application owning a datagram medium would: the send
// callback below pushes each outgoing datagram into the peer queue as if
// it arrived from the wire's fixed source label.
struct Wire
{
    // The queue of the peer endpoint: everything sent through this wire is
    // pushed here. A null pointer leaves the wire unterminated - a
    // datagram sent through it is dropped, like on a wire to nowhere.
    srt::SRTTransportQueue* peer = nullptr;
    sockaddr_storage         source_addr;
    socklen_t                source_len = 0;
    // When not null, datagrams carrying this byte sequence in the payload
    // are swallowed to emulate a loss. Retransmissions carry the same
    // payload, so every copy of the packet disappears on the wire.
    // Atomic because tests arm it after the connection is live, with the
    // send callback reading it on SRT threads (like HeldWire::hold).
    std::atomic<const char*> swallow_tag{ nullptr };
    // Counts the send callbacks fired through this wire (called from SRT
    // threads).
    std::atomic<long long> send_count{ 0 };
    // The first datagram ever sent through this wire, captured once for
    // the wire-level handshake pins (see ExpectFirstHandshake). Guarded by
    // its own mutex because the send callback can fire on SRT threads.
    std::vector<char> first_datagram;
    bool              first_datagram_captured = false;
    std::mutex        first_datagram_lock;
    // The HANDSHAKE datagrams sent through this wire, per destination
    // socket id of their SRT header (the HeldWireSend parse): only that
    // id tells one caller's responses from another's, and counting
    // handshakes alone keeps the shared ACK/keepalive traffic out of the
    // counts. Latest datagram per destination kept whole for
    // request-type pins; guarded by first_datagram_lock.
    struct AddressedHandshakes
    {
        long long         count = 0;
        std::vector<char> latest;
    };
    std::map<SRTSOCKET, AddressedHandshakes> handshakes_to;
};

int WireSend(void* opaq, const char* data, int len, const struct sockaddr* /*dst*/, int /*dstlen*/)
{
    Wire* w = (Wire*)opaq;
    ++w->send_count;
    // One-shot capture of the opening flight, ahead of the swallow check
    // so that the first datagram is recorded even under loss emulation
    // (see ExpectFirstHandshake).
    {
        std::lock_guard<std::mutex> lock(w->first_datagram_lock);
        if (!w->first_datagram_captured)
        {
            w->first_datagram.assign(data, data + len);
            w->first_datagram_captured = true;
        }
        // The per-destination handshake capture (see
        // Wire::handshakes_to), ahead of the swallow check so that a
        // response attempt counts even when the datagram is then lost
        // on the wire. A control packet announces itself in the first
        // header word: the control flag with the message type
        // (SEQNO_CONTROL / SEQNO_MSGTYPE, packet.h).
        if (len >= (int)srt::CPacket::HDR_SIZE)
        {
            uint32_t seqno = 0, dstid = 0;
            memcpy(&seqno, data, sizeof seqno);
            memcpy(&dstid, data + srt::CPacket::HDR_SIZE - sizeof dstid, sizeof dstid);
            seqno = ntohl(seqno);
            if (srt::SEQNO_CONTROL::unwrap(seqno) != 0 && srt::SEQNO_MSGTYPE::unwrap(seqno) == srt::UMSG_HANDSHAKE)
            {
                Wire::AddressedHandshakes& h = w->handshakes_to[(SRTSOCKET)ntohl(dstid)];
                ++h.count;
                h.latest.assign(data, data + len);
            }
        }
    }
    const char* const swallow_tag = w->swallow_tag.load(std::memory_order_relaxed);
    if (swallow_tag &&
        std::search(data, data + len, swallow_tag, swallow_tag + strlen(swallow_tag)) != data + len)
        return len;

    if (!w->peer)
        return -1; // no peer attached: the datagram is lost on the wire

    if (w->peer->push(data, (size_t)len, (const struct sockaddr*)&w->source_addr, w->source_len) == -1)
        return -1;

    return len;
}

sockaddr_in FillV4(const char* ip, uint16_t port)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family      = AF_INET;
    sa.sin_port        = htons(port);
    inet_pton(AF_INET, ip, &sa.sin_addr);
    return sa;
}

// The IPv6 sibling of the label builder above, for the tests that bind
// with AF_INET6 labels: a full sockaddr_in6 source on a wire is 28
// bytes where an IPv4 one is 16, so the deliver path it exercises is
// capacity-sensitive in a way the IPv4 labels never reach.
sockaddr_in6 FillV6(const char* ip, uint16_t port)
{
    sockaddr_in6 sa;
    memset(&sa, 0, sizeof sa);
    sa.sin6_family = AF_INET6;
    sa.sin6_port   = htons(port);
    inet_pton(AF_INET6, ip, &sa.sin6_addr);
    return sa;
}

void FillWireSource(Wire& w, const sockaddr_in& sa)
{
    memset(&w.source_addr, 0, sizeof w.source_addr);
    memcpy(&w.source_addr, &sa, sizeof sa);
    w.source_len = sizeof sa;
}

void FillWireSource(Wire& w, const sockaddr_in6& sa)
{
    memset(&w.source_addr, 0, sizeof w.source_addr);
    memcpy(&w.source_addr, &sa, sizeof sa);
    w.source_len = sizeof sa;
}

// The conclusion-first (internal bind path) tests need CUDT::bind_virtual,
// a hidden-visibility internal symbol that links only against the static
// library. In a shared-only build CMake defines
// SRT_VT_TESTS_NO_INTERNAL_BIND on test-srt, so those tests - and the
// helpers only they use, which would otherwise warn as unused - compile
// out.
#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The same source-address fill for the switch below, which reports the
// shared listener address as the source of everything it forwards.
void FillSource(sockaddr_storage& addr, socklen_t& addr_len, const sockaddr_in& sa)
{
    memset(&addr, 0, sizeof addr);
    memcpy(&addr, &sa, sizeof sa);
    addr_len = sizeof sa;
}

// Routes the datagrams sent by ONE shared transport queue to one of several
// peer queues, by destination address - a miniature of a gateway
// demultiplexing WebTransport sessions onto SRT sockets. Used where several
// callers are served by one listener: every accepted socket sends through
// the listener's queue, and the destination address (the caller's source
// label) picks the caller whose queue must receive it.
struct Switch
{
    struct Route
    {
        sockaddr_in                  dst;
        srt::SRTTransportQueue* peer;
    };
    std::vector<Route> routes;
    sockaddr_storage         source_addr;
    socklen_t                source_len = 0;
};

int SwitchSend(void* opaq, const char* data, int len, const struct sockaddr* dst, int /*dstlen*/)
{
    Switch*            sw = (Switch*)opaq;
    const sockaddr_in* d  = (const sockaddr_in*)dst;
    for (Switch::Route& r : sw->routes)
    {
        if (d->sin_family == AF_INET && d->sin_port == r.dst.sin_port && d->sin_addr.s_addr == r.dst.sin_addr.s_addr)
        {
            if (r.peer->push(data, (size_t)len, (const struct sockaddr*)&sw->source_addr, sw->source_len) == -1)
                return -1;
            break;
        }
    }

    // An unroutable datagram is swallowed, as a datagram lost on a wire.
    return len;
}

// Routes one shared queue's datagrams to one of two peer queues by the
// SRT destination socket id - the last 32-bit word of the fixed 16-byte
// header (CPacket::HDR_SIZE), network byte order. Two callers may share
// one source label, so responses are told apart only by the addressed
// socket. While the hold flag is set, datagrams to the held socket are
// swallowed (and counted), so it stays SRTS_CONNECTING, retransmitting.
struct HeldWire
{
    srt::SRTTransportQueue* held_peer  = nullptr; // queue of the held caller (A)
    srt::SRTTransportQueue* other_peer = nullptr; // queue of the other caller (B)
    SRTSOCKET               held_sock  = SRT_INVALID_SOCK; // A's socket id
    SRTSOCKET               other_sock = SRT_INVALID_SOCK; // B's socket id
    // The hold: swallow (and count) every datagram addressed to A.
    std::atomic<bool> hold{ false };
    // Counts the swallowed response attempts towards A (called from SRT
    // threads).
    std::atomic<long long> held_sends{ 0 };
    sockaddr_storage       source_addr;
    socklen_t              source_len = 0;
};

int HeldWireSend(void* opaq, const char* data, int len, const struct sockaddr* /*dst*/, int /*dstlen*/)
{
    HeldWire* w = (HeldWire*)opaq;

    if (len < (int)srt::CPacket::HDR_SIZE)
        return len; // not a whole SRT packet: drop it, like a mangled datagram

    uint32_t dstid = 0;
    memcpy(&dstid, data + srt::CPacket::HDR_SIZE - sizeof dstid, sizeof dstid);
    const SRTSOCKET dst_sock = (SRTSOCKET)ntohl(dstid);

    if (dst_sock == w->held_sock)
    {
        if (w->hold.load(std::memory_order_relaxed))
        {
            ++w->held_sends;
            return len; // swallowed: the held caller never hears it
        }
        if (w->held_peer->push(data, (size_t)len, (const struct sockaddr*)&w->source_addr, w->source_len) == -1)
            return -1;
        return len;
    }

    if (dst_sock == w->other_sock && w->other_peer)
    {
        if (w->other_peer->push(data, (size_t)len, (const struct sockaddr*)&w->source_addr, w->source_len) == -1)
            return -1;
    }

    // An unroutable datagram is swallowed, as a datagram lost on a wire.
    return len;
}
#endif

// Decodes a datagram captured on a wire as an SRT handshake: the fixed
// 16-byte header (CPacket::HDR_SIZE) followed by the serialized
// CHandShake in network byte order. The decode reverses the send path
// the way the receive path does - NtoHLA over the payload words - and
// reads it with CHandShake::load_from, the library's own reader.
void DecodeHandshakeDatagram(const std::vector<char>& datagram, srt::CHandShake* decoded)
{
    ASSERT_FALSE(datagram.empty()) << "no handshake datagram was captured on the wire";
    ASSERT_GE(datagram.size(), srt::CPacket::HDR_SIZE + srt::CHandShake::m_iContentSize)
        << "the captured datagram is too short to carry the SRT header plus a handshake";

    const size_t          payload_bytes = datagram.size() - srt::CPacket::HDR_SIZE;
    std::vector<uint32_t> payload(payload_bytes / 4); // whole 32-bit words of the handshake
    memcpy(payload.data(), datagram.data() + srt::CPacket::HDR_SIZE, payload.size() * sizeof(uint32_t));
    srt::NtoHLA(payload.data(), payload.data(), payload.size());

    ASSERT_EQ(decoded->load_from((const char*)payload.data(), payload.size() * sizeof(uint32_t)), 0)
        << "the captured datagram payload does not decode as a handshake";
}

// Pins the handshake request type of the very first datagram sent through
// the wire - the opening flight of the socket behind it. When decoded is
// not null, it receives the parsed handshake for the caller's further
// pins.
void ExpectFirstHandshake(Wire& w, srt::UDTRequestType expect_req, srt::CHandShake* decoded = nullptr)
{
    std::vector<char> first;
    {
        std::lock_guard<std::mutex> lock(w.first_datagram_lock);
        first = w.first_datagram;
    }

    srt::CHandShake hs;
    DecodeHandshakeDatagram(first, &hs);
    ASSERT_EQ(hs.m_iReqType, expect_req);
    if (decoded)
        *decoded = hs;
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The count of handshake datagrams that left through the wire addressed
// to the given peer's socket id (see Wire::handshakes_to).
long long HandshakeSendsTo(Wire& w, SRTSOCKET dst)
{
    std::lock_guard<std::mutex> lock(w.first_datagram_lock);
    const std::map<SRTSOCKET, Wire::AddressedHandshakes>::const_iterator it = w.handshakes_to.find(dst);
    return it == w.handshakes_to.end() ? 0 : it->second.count;
}

// Pins the handshake request type of the latest datagram the wire sent
// to the given destination socket id - the response the socket behind
// the wire addressed to exactly that peer.
void ExpectLatestHandshakeTo(Wire& w, SRTSOCKET dst, srt::UDTRequestType expect_req)
{
    std::vector<char> captured;
    {
        std::lock_guard<std::mutex> lock(w.first_datagram_lock);
        const std::map<SRTSOCKET, Wire::AddressedHandshakes>::const_iterator it = w.handshakes_to.find(dst);
        if (it != w.handshakes_to.end())
            captured = it->second.latest;
    }

    srt::CHandShake hs;
    DecodeHandshakeDatagram(captured, &hs);
    EXPECT_EQ(hs.m_iReqType, expect_req);
}
#endif

// A bounded read-readiness check through epoll, so that a handshake or a
// delivery that never happens fails the test instead of hanging it in a
// blocking call.
bool WaitReadable(SRTSOCKET sock, int timeout_ms)
{
    const int pollid = srt_epoll_create();
    if (pollid == SRT_ERROR)
        return false;
    const int events = SRT_EPOLL_IN;
    if (srt_epoll_add_usock(pollid, sock, &events) == SRT_ERROR)
    {
        srt_epoll_release(pollid);
        return false;
    }

    SRT_EPOLL_EVENT ready[1];
    const int       nready = srt_epoll_uwait(pollid, ready, 1, timeout_ms);
    srt_epoll_release(pollid);
    return nready == 1 && (ready[0].events & SRT_EPOLL_IN) != 0;
}

// Pins the error code of the last failed SRT call, with its message in
// the failure log.
void ExpectLastErrno(int expected)
{
    EXPECT_EQ(srt_getlasterror(nullptr), expected) << srt_getlasterror_str();
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// Bounded wait for a socket state, so that a connection that never
// completes fails the test instead of hanging a subsequent call.
bool WaitState(SRTSOCKET sock, SRT_SOCKSTATUS state, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 10)
    {
        if (srt_getsockstate(sock) == state)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return srt_getsockstate(sock) == state;
}

// Bounded wait for a socket state against one shared deadline, so that
// several sockets can be asserted to break within the same time budget
// (the prompt-breakage contract is about "every socket", not one).
bool WaitStateUntil(SRTSOCKET sock, SRT_SOCKSTATUS state, std::chrono::steady_clock::time_point deadline)
{
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (srt_getsockstate(sock) == state)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return srt_getsockstate(sock) == state;
}

// The name of a socket status, for failure messages that report the
// status a socket was left in.
const char* SockStatusName(SRT_SOCKSTATUS status)
{
    switch (status)
    {
    case SRTS_INIT: return "SRTS_INIT";
    case SRTS_OPENED: return "SRTS_OPENED";
    case SRTS_LISTENING: return "SRTS_LISTENING";
    case SRTS_CONNECTING: return "SRTS_CONNECTING";
    case SRTS_CONNECTED: return "SRTS_CONNECTED";
    case SRTS_BROKEN: return "SRTS_BROKEN";
    case SRTS_CLOSING: return "SRTS_CLOSING";
    case SRTS_CLOSED: return "SRTS_CLOSED";
    case SRTS_NONEXIST: return "SRTS_NONEXIST";
    default: return "SRTS_?";
    }
}

// Counts the open file descriptors of the current process that are sockets
// (Linux-only; the feature itself is Linux/Emscripten-only). A virtual
// binding creates no OS socket, so a virtual-socket lifecycle must never
// raise the count above its starting level. (An SRT epoll id is an
// anon_inode, not a socket, so epoll-based waits do not disturb this
// count.)
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
#endif

// The label contract of srt_bind_virtual: srt_getsockname() must report
// exactly the address the socket was bound with - family, port, address -
// because the label is the socket's identity on the virtual medium.
void ExpectSockName(SRTSOCKET sock, const sockaddr_in& expect)
{
    sockaddr_in self;
    memset(&self, 0, sizeof self);
    int self_len = (int)sizeof self;
    ASSERT_EQ(srt_getsockname(sock, (sockaddr*)&self, &self_len), SRT_STATUS_OK) << srt_getlasterror_str();
    EXPECT_EQ(self_len, (int)sizeof self);
    EXPECT_EQ(self.sin_family, expect.sin_family);
    EXPECT_EQ(self.sin_port, expect.sin_port);
    EXPECT_EQ(self.sin_addr.s_addr, expect.sin_addr.s_addr);
}

// The IPv6 label twin of the check above.
void ExpectSockName(SRTSOCKET sock, const sockaddr_in6& expect)
{
    sockaddr_in6 self;
    memset(&self, 0, sizeof self);
    int self_len = (int)sizeof self;
    ASSERT_EQ(srt_getsockname(sock, (sockaddr*)&self, &self_len), SRT_STATUS_OK) << srt_getlasterror_str();
    EXPECT_EQ(self_len, (int)sizeof self);
    EXPECT_EQ(self.sin6_family, expect.sin6_family);
    EXPECT_EQ(self.sin6_port, expect.sin6_port);
    EXPECT_EQ(memcmp(&self.sin6_addr, &expect.sin6_addr, sizeof self.sin6_addr), 0);
}

// Binds a socket over a virtual transport in the conclusion-first
// handshake form, an INTERNAL srtcore setting the public srt_bind_virtual
// never configures. The tests enter it through CUDT::bind_virtual, the
// hidden-visibility symbol the SRT_VT_TESTS_NO_INTERNAL_BIND guard
// exists for.
#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
void BindVirtualConclusionFirst(SRTSOCKET sock, const sockaddr_in& label, const SRT_VirtualTransportConfig& cfg)
{
    ASSERT_EQ(srt::CUDT::bind_virtual(sock, (const sockaddr*)&label, (int)sizeof label, &cfg, true), SRT_STATUS_OK)
        << srt_getlasterror_str();
}
#endif

// Closes the still-valid sockets of a rig from its destructor (the
// fixture's teardown closes only its own three). Must be the LAST member,
// after the wires and queues: destroyed in reverse order, the sockets
// close first - their shutdown flights go through the wires.
struct SocketRig
{
    explicit SocketRig(std::initializer_list<SRTSOCKET*> watched_socks)
        : watched(watched_socks)
    {
    }

    ~SocketRig()
    {
        for (SRTSOCKET* sock : watched)
            if (*sock != SRT_INVALID_SOCK)
                srt_close(*sock);
    }

    std::vector<SRTSOCKET*> watched;
};

// A send callback for the queues of the channel tests below: nothing is
// ever forwarded through them, so every datagram is simply accepted and
// dropped, its full size reported.
int NullSend(void* /*opaq*/, const char* /*data*/, int len, const struct sockaddr* /*dst*/, int /*dstlen*/)
{
    return len;
}

// Lying deliver callbacks for TestVirtualTransport
// ChannelBoundsDeliverReportByCapacity: both call the queue's honest
// deliver() - so the datagram is really picked up and scattered into the
// provided buffers - but then report a size those buffers do not carry:
// one past the provided capacity, and a negative value other than -1.
int DeliverLyingLarge(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    srt::SRTTransportQueue* queue = (srt::SRTTransportQueue*)opaq;
    const int               honest = queue->deliver(w_src, w_srclen, w_vec, nvec);
    return honest == -1 ? -1 : honest + 1000;
}

int DeliverLyingNegative(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    srt::SRTTransportQueue* queue = (srt::SRTTransportQueue*)opaq;
    const int               honest = queue->deliver(w_src, w_srclen, w_vec, nvec);
    return honest == -1 ? -1 : -7;
}

// A lying deliver callback for TestVirtualTransport
// ChannelDropsDatagramWithBogusSourceFamily: like the two above, it calls
// the queue's honest deliver() and returns the honest size, but overwrites
// the reported source address family with a value no network stack would
// ever report.
int DeliverLyingSourceFamily(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    srt::SRTTransportQueue* queue = (srt::SRTTransportQueue*)opaq;
    const int               honest = queue->deliver(w_src, w_srclen, w_vec, nvec);
    if (honest != -1)
        w_src->sa_family = AF_UNSPEC;
    return honest;
}

// Lying deliver callbacks for TestVirtualTransport
// ChannelDropsDatagramWithBogusSourceLength: like DeliverLyingSourceFamily,
// they call the queue's honest deliver() and return the honest size, but
// overwrite the reported source address length: zero - no source address
// at all - a value past the capacity the callback was given, and a
// positive value too short for a whole address of the family reported.
int DeliverLyingSourceLenZero(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    srt::SRTTransportQueue* queue = (srt::SRTTransportQueue*)opaq;
    const int               honest = queue->deliver(w_src, w_srclen, w_vec, nvec);
    if (honest != -1)
        *w_srclen = 0;
    return honest;
}

int DeliverLyingSourceLenHuge(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    srt::SRTTransportQueue* queue = (srt::SRTTransportQueue*)opaq;
    const int               honest = queue->deliver(w_src, w_srclen, w_vec, nvec);
    if (honest != -1)
        *w_srclen = 9999;
    return honest;
}

int DeliverLyingSourceLenPartial(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    srt::SRTTransportQueue* queue = (srt::SRTTransportQueue*)opaq;
    const int               honest = queue->deliver(w_src, w_srclen, w_vec, nvec);
    if (honest != -1)
        *w_srclen = 4;
    return honest;
}

// Lying send callbacks for TestVirtualTransport
// ChannelNormalizesBogusSendCallbackReturn: a datagram is handed to the
// send callback as a whole, so the only honest non-failure return is the
// total of the buffer entries given to it. These two return one less and
// one more than that total - "partial accepts" no datagram medium can
// make sense of.
int SendLyingShort(void* /*opaq*/, const struct sockaddr* /*dst*/, int /*dstlen*/,
                   const SRT_VTBuf* vec, int nvec)
{
    int total = 0;
    for (int i = 0; i < nvec; ++i)
        total += vec[i].len;
    return total - 1;
}

int SendLyingLong(void* /*opaq*/, const struct sockaddr* /*dst*/, int /*dstlen*/,
                  const SRT_VTBuf* vec, int nvec)
{
    int total = 0;
    for (int i = 0; i < nvec; ++i)
        total += vec[i].len;
    return total + 1;
}

// A counting pass-through around an SRTTransportQueue for the
// callback-invocation pins of srt_bind_virtual: the config fields are
// plain function pointers, so invocations are only observable via a
// hand-built config of trampolines delegating to the queue's public
// methods, waitread counting first. Atomic: incremented on the SRT
// receive worker, read on the test thread.
struct CountingQueue
{
    srt::SRTTransportQueue* queue = nullptr;
    std::atomic<long long>  waitreads{ 0 };
};

int CountingQueueSend(void* opaq, const struct sockaddr* dst, int dstlen, const SRT_VTBuf* vec, int nvec)
{
    return ((CountingQueue*)opaq)->queue->send(dst, dstlen, vec, nvec);
}

SRT_VTWAIT CountingQueueWaitRead(void* opaq, int timeout_ms)
{
    CountingQueue* cq = (CountingQueue*)opaq;
    ++cq->waitreads;
    return cq->queue->waitread(timeout_ms);
}

int CountingQueueDeliver(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    return ((CountingQueue*)opaq)->queue->deliver(w_src, w_srclen, w_vec, nvec);
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
    }

    // Pairs the wires for one caller/listener pair: what the caller sends
    // is pushed into the listener's queue as if received from the caller
    // label, and what the listener (or its accepted socket) sends is
    // pushed into the caller's queue as if received from the listener
    // label.
    void PairWires(const sockaddr_in& caller_label, const sockaddr_in& listener_label)
    {
        m_caller_wire.peer = &m_listener_queue;
        FillWireSource(m_caller_wire, caller_label);
        m_listener_wire.peer = &m_caller_queue;
        FillWireSource(m_listener_wire, listener_label);
    }

    // The IPv6 twin of the pairing above: the wires report full
    // sockaddr_in6 sources, the label family both channels are bound
    // with.
    void PairWires(const sockaddr_in6& caller_label, const sockaddr_in6& listener_label)
    {
        m_caller_wire.peer = &m_listener_queue;
        FillWireSource(m_caller_wire, caller_label);
        m_listener_wire.peer = &m_caller_queue;
        FillWireSource(m_listener_wire, listener_label);
    }

    // The shared body of the two rendezvous-refusal tests (defined next
    // to them): rendezvous set after a virtual bind, refused at connect
    // time through either bind entry.
    void RendezvousRefusedAfterVirtualBind(bool bind_conclusion_first);

    TestVirtualTransport()
        : m_caller_queue(&WireSend, &m_caller_wire)
        , m_listener_queue(&WireSend, &m_listener_wire)
    {
    }

    SRTSOCKET m_caller_sock   = SRT_INVALID_SOCK;
    SRTSOCKET m_accepted_sock = SRT_INVALID_SOCK;
    SRTSOCKET m_listener_sock = SRT_INVALID_SOCK;

    // The wires and queues must live as long as the sockets: srt_close()
    // in teardown() (and in the test bodies) sends the shutdown through
    // the wire, which calls WireSend - after the test body and its locals
    // are gone.
    Wire m_caller_wire;
    Wire m_listener_wire;
    srt::SRTTransportQueue m_caller_queue;
    srt::SRTTransportQueue m_listener_queue;
};

// A stock caller-listener handshake (induction first) and payload round
// trips in both directions, carried end to end by the virtual transport
// seam: the listener is bound with srt_bind_virtual and listens, the
// caller is bound with srt_bind_virtual and connects, and everything both
// sides exchange passes through the wires between the two queues.
TEST_F(TestVirtualTransport, BindVirtualConnectListenSendReceive)
{
    // The addresses never reach the network; they only identify the
    // endpoints for the SRT dispatching machinery. They are unique to
    // this test - the RTT cache and the virtual demux key off them.
    const sockaddr_in listener_addr = FillV4("10.210.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.210.0.1", 5556);

    PairWires(caller_addr, listener_addr);

    // Listener side: the virtual bind creates a dedicated multiplexer
    // with a CChannelVirtual channel, then listen.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_listener_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }
    ExpectSockName(m_listener_sock, listener_addr);
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller side: virtual bind, then connect to the listener's label.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_caller_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&caller_addr, (int)sizeof caller_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }
    ExpectSockName(m_caller_sock, caller_addr);

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    // Bounded wait for the queued connection: a handshake that never
    // completes must fail the test, not hang it in srt_accept.
    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    // The peer of the accepted socket is the source address the caller's
    // wire reports for its datagrams.
    EXPECT_EQ(accept_addr.sin_family, AF_INET);
    EXPECT_EQ(accept_addr.sin_addr.s_addr, caller_addr.sin_addr.s_addr);
    EXPECT_EQ(accept_addr.sin_port, caller_addr.sin_port);

    // The accepted socket shares the listener's multiplexer, so it reports
    // the listener's label.
    ExpectSockName(m_accepted_sock, listener_addr);

    // The wire-level pin on the caller's opening flight: with the stock
    // handshake form (what the public bind configures), the first datagram
    // is the Induction handshake.
    ExpectFirstHandshake(m_caller_wire, srt::URQ_INDUCTION);

    // One payload message from the caller to the accepted socket. Use
    // epoll to also check that read-readiness signaling works for virtual
    // sockets.
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

    // The reverse direction: the accepted socket back to the caller.
    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_TRUE(WaitReadable(m_caller_sock, 5000)) << "the caller never became read-ready";
    ASSERT_GT(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    // Clean close - the shutdown flights go through the wires too.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}

// The port-0 label contract of srt_bind_virtual: a label's port is part
// of the name - port 0 "is not autoselected and stays 0" - so a caller
// may carry a port-0 label and connect to a port-0 listener. The
// multiplexer re-stamps its dispatch copy of the peer address from the
// CUDT entity even then; datagrams dispatch by that copy (findPeer), so
// a wrong stamp would break delivery, not just the reported name.
TEST_F(TestVirtualTransport, PortZeroLabelRoundTrip)
{
    // Labels unique to this test like every test's are to its, both
    // with port 0: the listener on 10.231.0.2 :0, the caller on
    // 10.231.0.1 :0.
    const sockaddr_in listener_addr = FillV4("10.231.0.2", 0);
    const sockaddr_in caller_addr   = FillV4("10.231.0.1", 0);

    PairWires(caller_addr, listener_addr);

    // Listener side: the virtual bind takes the port-0 label as given,
    // creating a dedicated multiplexer with a CChannelVirtual channel
    // bound with it, then listen.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_listener_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }
    ExpectSockName(m_listener_sock, listener_addr);
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller side: bound with its own port-0 label, then connected to
    // the listener's port-0 label.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_caller_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&caller_addr, (int)sizeof caller_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }
    ExpectSockName(m_caller_sock, caller_addr);

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    // Bounded wait for the queued connection: a handshake that never
    // completes must fail the test, not hang it in srt_accept.
    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    // The peer of the accepted socket is the caller's port-0 label,
    // exactly as the caller's wire reports it.
    EXPECT_EQ(accept_addr.sin_family, AF_INET);
    EXPECT_EQ(accept_addr.sin_addr.s_addr, caller_addr.sin_addr.s_addr);
    EXPECT_EQ(accept_addr.sin_port, caller_addr.sin_port);

    // Both ends report each other's port-0 label as the peer name.
    sockaddr_in peer;
    int         peer_len = (int)sizeof peer;
    ASSERT_EQ(srt_getpeername(m_caller_sock, (sockaddr*)&peer, &peer_len), SRT_STATUS_OK) << srt_getlasterror_str();
    EXPECT_EQ(peer.sin_family, AF_INET);
    EXPECT_EQ(peer.sin_addr.s_addr, listener_addr.sin_addr.s_addr);
    EXPECT_EQ(peer.sin_port, listener_addr.sin_port);
    peer_len = (int)sizeof peer;
    ASSERT_EQ(srt_getpeername(m_accepted_sock, (sockaddr*)&peer, &peer_len), SRT_STATUS_OK) << srt_getlasterror_str();
    EXPECT_EQ(peer.sin_family, AF_INET);
    EXPECT_EQ(peer.sin_addr.s_addr, caller_addr.sin_addr.s_addr);
    EXPECT_EQ(peer.sin_port, caller_addr.sin_port);

    // One payload message each way through the port-0 labels: the
    // incoming direction of each is dispatched by the receiving
    // multiplexer through its holder's copy of the peer address.
    const char* payload     = "virtual transport seam, port 0 label";
    const int   payload_len = (int)strlen(payload);
    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_accepted_sock, 5000)) << "the accepted socket never became read-ready";
    char buf[1456];
    ASSERT_GT(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    // The reverse direction: the accepted socket back to the caller.
    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_TRUE(WaitReadable(m_caller_sock, 5000)) << "the caller never became read-ready";
    ASSERT_GT(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    // Clean close - the shutdown flights go through the wires too.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}

// The IPv6 twin of the round trip above: both labels are AF_INET6, so
// the source address every datagram arrives with is a full sockaddr_in6
// - 28 bytes delivered into a buffer the receive worker sizes by the
// label's family, the capacity-sensitive path of the seam. The wires
// report the peer's label as the source, exactly like an IPv6 network
// stack would report the peer's address.
TEST_F(TestVirtualTransport, IPv6LabelRoundTrip)
{
    // Labels on the documentation prefix, unique to this test like the
    // labels of the other tests are to theirs: the listener on ::2
    // :5555, the caller on ::1 :5556.
    const sockaddr_in6 listener_addr = FillV6("fd00:db8:229::2", 5555);
    const sockaddr_in6 caller_addr   = FillV6("fd00:db8:229::1", 5556);

    PairWires(caller_addr, listener_addr);

    // Listener side: the virtual bind takes the IPv6 label as given,
    // creating a dedicated multiplexer with a CChannelVirtual channel
    // bound with it, then listen.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_listener_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }
    ExpectSockName(m_listener_sock, listener_addr);
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller side: virtual bind with its own IPv6 label, then connect
    // to the listener's label.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_caller_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&caller_addr, (int)sizeof caller_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }
    ExpectSockName(m_caller_sock, caller_addr);

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    // Bounded wait for the queued connection, like above.
    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in6 accept_addr;
    int          accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    // The peer of the accepted socket is the full IPv6 source address
    // the caller's wire reports for its datagrams.
    EXPECT_EQ(accept_addr.sin6_family, AF_INET6);
    EXPECT_EQ(accept_addr.sin6_port, caller_addr.sin6_port);
    EXPECT_EQ(memcmp(&accept_addr.sin6_addr, &caller_addr.sin6_addr, sizeof accept_addr.sin6_addr), 0);

    // The accepted socket shares the listener's multiplexer, so it
    // reports the listener's IPv6 label.
    ExpectSockName(m_accepted_sock, listener_addr);

    // One payload message from the caller to the accepted socket, over
    // the sockaddr_in6-sized source path of the deliver callback.
    const char* payload     = "virtual transport seam, ipv6 label";
    const int   payload_len = (int)strlen(payload);
    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_accepted_sock, 5000)) << "the accepted socket never became read-ready";
    char      buf[1456];
    const int recv_len = srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf);
    ASSERT_GT(recv_len, 0) << srt_getlasterror_str();
    EXPECT_EQ(recv_len, payload_len);
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    // The reverse direction: the accepted socket back to the caller.
    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_TRUE(WaitReadable(m_caller_sock, 5000)) << "the caller never became read-ready";
    ASSERT_GT(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    // Clean close - the shutdown flights go through the wires too.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}

// The config gate of srt_bind_virtual: the transport must be usable as
// given - no socket option can fix it up later. A null config, missing
// callback, out-of-range initial_rtt_ms, invalid/closed socket or a
// namelen too small for the family: all rejected with SRT_EINVPARAM.
// The boundary values of initial_rtt_ms (0 and 30000) are accepted, and
// every refusal leaves the socket untouched in the INIT state.
TEST_F(TestVirtualTransport, BindVirtualBadConfigRejected)
{
    // Unique to this test, like the labels of the other tests are to
    // theirs.
    const sockaddr_in self_addr = FillV4("10.211.0.2", 5555);

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);

    // The reference config, valid as given.
    const SRT_VirtualTransportConfig good = m_listener_queue.getConfig();

    // A null config.
    EXPECT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, nullptr),
              SRT_ERROR);
    ExpectLastErrno(SRT_EINVPARAM);

    // One missing callback each: send, waitread, deliver.
    {
        SRT_VirtualTransportConfig cfg = good;
        cfg.send = nullptr;
        EXPECT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, &cfg),
                  SRT_ERROR)
            << "a null send callback was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);
    }
    {
        SRT_VirtualTransportConfig cfg = good;
        cfg.waitread = nullptr;
        EXPECT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, &cfg),
                  SRT_ERROR)
            << "a null waitread callback was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);
    }
    {
        SRT_VirtualTransportConfig cfg = good;
        cfg.deliver = nullptr;
        EXPECT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, &cfg),
                  SRT_ERROR)
            << "a null deliver callback was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);
    }

    // initial_rtt_ms outside the 0..30000 range.
    {
        SRT_VirtualTransportConfig cfg = good;
        cfg.initial_rtt_ms = -1;
        EXPECT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, &cfg),
                  SRT_ERROR)
            << "initial_rtt_ms = -1 was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);
    }
    {
        SRT_VirtualTransportConfig cfg = good;
        cfg.initial_rtt_ms = 30001;
        EXPECT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, &cfg),
                  SRT_ERROR)
            << "initial_rtt_ms = 30001 was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);
    }

    // A namelen too small for the family, and a family no socket can be
    // bound with: in both cases there is no address to bind to.
    {
        SRT_VirtualTransportConfig cfg = good;
        EXPECT_EQ(
            srt_bind_virtual(m_listener_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr - 1, &cfg), SRT_ERROR)
            << "a namelen too small for sockaddr_in was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);

        sockaddr bad_family;
        memset(&bad_family, 0, sizeof bad_family);
        bad_family.sa_family = AF_UNIX;
        EXPECT_EQ(srt_bind_virtual(m_listener_sock, &bad_family, (int)sizeof bad_family, &cfg), SRT_ERROR)
            << "an unsupported address family was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);
    }

    // No refusal above may have bound the socket.
    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_INIT);

    // The boundary values of initial_rtt_ms are accepted (each on its own
    // fresh socket - a socket can be bound only once).
    {
        const SRT_VirtualTransportConfig cfg0  = m_listener_queue.getConfig(0);
        const sockaddr_in                addr0 = FillV4("10.211.0.3", 5555);
        m_caller_sock                      = srt_create_socket();
        ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&addr0, (int)sizeof addr0, &cfg0), SRT_STATUS_OK)
            << srt_getlasterror_str();
        EXPECT_EQ(srt_getsockstate(m_caller_sock), SRTS_OPENED);
        EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
        m_caller_sock = SRT_INVALID_SOCK;
    }
    {
        const SRT_VirtualTransportConfig cfg_max = m_listener_queue.getConfig(30000);
        const sockaddr_in                addr9   = FillV4("10.211.0.4", 5555);
        m_accepted_sock                    = srt_create_socket();
        ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(
            srt_bind_virtual(m_accepted_sock, (const sockaddr*)&addr9, (int)sizeof addr9, &cfg_max), SRT_STATUS_OK)
            << srt_getlasterror_str();
        EXPECT_EQ(srt_getsockstate(m_accepted_sock), SRTS_OPENED);
        EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
        m_accepted_sock = SRT_INVALID_SOCK;
    }

    // An invalid socket, and a socket that was already closed.
    EXPECT_EQ(srt_bind_virtual(SRT_INVALID_SOCK, (const sockaddr*)&self_addr, (int)sizeof self_addr, &good),
              SRT_ERROR);
    ExpectLastErrno(SRT_EINVPARAM);
    {
        SRTSOCKET closed_sock = srt_create_socket();
        ASSERT_NE(closed_sock, SRT_INVALID_SOCK);
        ASSERT_EQ(srt_close(closed_sock), SRT_STATUS_OK) << srt_getlasterror_str();
        EXPECT_EQ(srt_bind_virtual(closed_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, &good), SRT_ERROR)
            << "a bind on a closed socket was not rejected";
        ExpectLastErrno(SRT_EINVPARAM);
    }

    // The refused socket remains untouched, usable and closable.
    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_INIT);
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}

// The binding state machine around srt_bind_virtual: a socket binds only
// once, as with srt_bind. A second srt_bind_virtual, an srt_bind after a
// virtual bind and a virtual bind after listen are rejected with
// SRT_EINVOP; a rendezvous-preset socket is refused with SRT_ERDVNOSERV
// (no listener could complete that handshake). Rejections leave the
// socket usable and closable - no leaked binding state.
TEST_F(TestVirtualTransport, BindVirtualDoubleBindRejected)
{
    const sockaddr_in listener_addr = FillV4("10.212.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.212.0.3", 5555);
    const sockaddr_in rdv_addr      = FillV4("10.212.0.4", 5555);

    // A virtually bound listener...
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    const SRT_VirtualTransportConfig cfg = m_listener_queue.getConfig();
    ASSERT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr, &cfg),
              SRT_STATUS_OK)
        << srt_getlasterror_str();

    // ...takes no second virtual binding.
    EXPECT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr, &cfg),
              SRT_ERROR)
        << "a second srt_bind_virtual was not rejected";
    ExpectLastErrno(SRT_EINVOP);

    // ...and no binding over to an OS UDP socket either.
    EXPECT_EQ(srt_bind(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << "an srt_bind after srt_bind_virtual was not rejected";
    ExpectLastErrno(SRT_EINVOP);

    // The refusals leave the socket usable: it still enters the listening
    // state.
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // A virtual bind after listen is rejected as well.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    const SRT_VirtualTransportConfig caller_cfg = m_caller_queue.getConfig();
    ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&caller_addr, (int)sizeof caller_addr, &caller_cfg),
              SRT_STATUS_OK)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_caller_sock, 4), SRT_ERROR) << srt_getlasterror_str();
    EXPECT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&caller_addr, (int)sizeof caller_addr, &caller_cfg),
              SRT_ERROR)
        << "an srt_bind_virtual after listen was not rejected";
    ExpectLastErrno(SRT_EINVOP);

    // A rendezvous-preset socket is refused up front.
    m_accepted_sock = srt_create_socket(); // reused here as the rendezvous socket
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK);
    const int yes = 1;
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();
    EXPECT_EQ(srt_bind_virtual(m_accepted_sock, (const sockaddr*)&rdv_addr, (int)sizeof rdv_addr, &cfg), SRT_ERROR)
        << "a rendezvous-preset socket was not rejected";
    ExpectLastErrno(SRT_ERDVNOSERV);
    EXPECT_EQ(srt_getsockstate(m_accepted_sock), SRTS_INIT);

    // All sockets remain closable - the rejections leaked no state.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// BindVirtualConnectListenSendReceive's wire with BOTH endpoints in the
// INTERNAL conclusion-first form: the caller's opening flight is a
// CONCLUSION - HS_VERSION_SRT1, cookie 0, HSv5 blocks - accepted with no
// Induction/cookie exchange. The shape is observable only in that first
// datagram (a virtual listener answers stock Inductions too); the rest
// pins connected state and bidirectional data flow intact.
TEST_F(TestVirtualTransport, BindVirtualSkipInductionRoundTrip)
{
    const sockaddr_in listener_addr = FillV4("10.215.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.215.0.1", 5556);

    PairWires(caller_addr, listener_addr);

    // Listener side: bound through the internal conclusion-first entry, so
    // its channel reports it and processConnectRequest bypasses the cookie
    // check for a conclusion-first request.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig());
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller side: the internal conclusion-first bind turns the first
    // handshake into the CONCLUSION.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_caller_sock, caller_addr, m_caller_queue.getConfig());

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();
    EXPECT_EQ(accept_addr.sin_addr.s_addr, caller_addr.sin_addr.s_addr);
    EXPECT_EQ(accept_addr.sin_port, caller_addr.sin_port);

    // The wire-level pins on the caller's opening flight: a CONCLUSION of
    // HS_VERSION_SRT1 with no cookie - there was no Induction response to
    // take a cookie from. (The version constant is taken by value:
    // CUDT::HS_VERSION_SRT1 has no out-of-class definition to ODR-use.)
    const int       hs_version_srt1 = srt::CUDT::HS_VERSION_SRT1;
    srt::CHandShake first_flight;
    ExpectFirstHandshake(m_caller_wire, srt::URQ_CONCLUSION, &first_flight);
    ASSERT_EQ(first_flight.m_iVersion, hs_version_srt1);
    ASSERT_EQ(first_flight.m_iCookie, 0);

    // The first flight must also carry the HSv5 extension blocks (HSREQ is
    // serialized after the bare handshake structure), which a stock
    // Induction never has.
    {
        std::vector<char> first;
        {
            std::lock_guard<std::mutex> lock(m_caller_wire.first_datagram_lock);
            first = m_caller_wire.first_datagram;
        }
        EXPECT_GT(first.size(), srt::CPacket::HDR_SIZE + srt::CHandShake::m_iContentSize)
            << "the conclusion-first flight carries no handshake extension blocks";
    }

    EXPECT_EQ(srt_getsockstate(m_caller_sock), SRTS_CONNECTED);
    EXPECT_EQ(srt_getsockstate(m_accepted_sock), SRTS_CONNECTED);

    // One payload message each way over the conclusion-first connection.
    const char* payload     = "skip-induction round trip";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];

    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_TRUE(WaitReadable(m_accepted_sock, 5000)) << "the accepted socket never became read-ready";
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_TRUE(WaitReadable(m_caller_sock, 5000)) << "the caller never became read-ready";
    ASSERT_EQ(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    // Clean close - the shutdown flights go through the wires too.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The cookie bypass belongs to the listener's OWN bind configuration, not
// the medium: a listener bound through the PUBLIC srt_bind_virtual keeps
// the stock cookie validation even against a conclusion-first request -
// a cookie-0 CONCLUSION is silently ignored, nothing is ever queued, the
// listener stays listening, and the caller observes the refusal through
// its bounded connect timeout.
TEST_F(TestVirtualTransport, VirtualListenerWithoutSkipStillRequiresCookie)
{
    const sockaddr_in listener_addr = FillV4("10.216.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.216.0.1", 5556);

    PairWires(caller_addr, listener_addr);

    // Listener side: the PUBLIC bind - the stock handshake form.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_listener_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller side: the INTERNAL conclusion-first bind, so its opening flight
    // is a cookie-0 CONCLUSION. The connect timeout bounds the refusal: the
    // listener ignores every retransmission of the wrong-cookie request, so
    // the caller can only run out of its connect timeout.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    const int conn_timeout_ms = 500;
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(m_caller_sock, caller_addr, m_caller_queue.getConfig());

    EXPECT_EQ(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR);
    // The bounded refusal: the caller can only run out of its connect
    // timeout, the shape the wrong-cookie silence produces.
    ExpectLastErrno(SRT_ENOSERVER);

    // The refusal was about the listener's stock handshake form, not about
    // the caller's opening flight: it was the conclusion-first shape.
    const int       hs_version_srt1 = srt::CUDT::HS_VERSION_SRT1;
    srt::CHandShake first_flight;
    ExpectFirstHandshake(m_caller_wire, srt::URQ_CONCLUSION, &first_flight);
    ASSERT_EQ(first_flight.m_iVersion, hs_version_srt1);
    ASSERT_EQ(first_flight.m_iCookie, 0);

    // Nothing was ever queued on the listener, and it stays listening.
    EXPECT_FALSE(WaitReadable(m_listener_sock, 500)) << "a connection was queued on the stock-form listener";
    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_LISTENING);

    // Both sockets close cleanly after the refused handshake.
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The configured seed must survive connection completion on both sides
// (postConnect / acceptAndRespond), which consult a per-peer RTT cache
// keyed by the peer label - and virtual labels repeat across sessions.
// A first session drives the live RTT far below the seed; a second on
// the SAME labels (fresh listener) must still come up seeded, then the
// first live ACK/ACKACK samples must replace the seed.
TEST_F(TestVirtualTransport, InitialRTTSeedViaConfigThenLiveReplace)
{
    // Reused by the second session below - that is the point of this test.
    const sockaddr_in listener_addr = FillV4("10.217.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.217.0.1", 5556);

    const int seed_rtt_ms    = 300;
    const int rcv_timeout_ms = 5000;

    // ---- First session: seed, verify the seed, then replace it live. ----

    PairWires(caller_addr, listener_addr);

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    // Both endpoints skip the Induction round trip (bound conclusion-first
    // through the internal entry) and seed the estimator explicitly, as a
    // WebTransport gateway does with the QUIC smoothed RTT.
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig(seed_rtt_ms));
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(m_caller_sock, caller_addr, m_caller_queue.getConfig(seed_rtt_ms));

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    // Pre-data: no ACK/ACKACK samples exist yet, so both estimators must
    // still hold the seed (300 ms), not the open()-time default.
    {
        SRT_TRACEBSTATS perf;
        ASSERT_EQ(srt_bstats(m_caller_sock, &perf, 0), 0);
        EXPECT_DOUBLE_EQ(perf.msRTT, (double)seed_rtt_ms) << "caller: the seed did not survive postConnect";
        ASSERT_EQ(srt_bstats(m_accepted_sock, &perf, 0), 0);
        EXPECT_DOUBLE_EQ(perf.msRTT, (double)seed_rtt_ms) << "accepted: the seed did not survive acceptAndRespond";
    }

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
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    {
        SRT_TRACEBSTATS perf;
        ASSERT_EQ(srt_bstats(m_caller_sock, &perf, 0), 0);
        EXPECT_LT(perf.msRTT, 100.0) << "caller: the live RTT did not replace the seed";
        ASSERT_EQ(srt_bstats(m_accepted_sock, &perf, 0), 0);
        EXPECT_LT(perf.msRTT, 100.0) << "accepted: the live RTT did not replace the seed";
    }

    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;

    // ---- Second session on the SAME labels, through a FRESH listener. ----
    //
    // The just-closed sockets hand their last RTT values to the
    // per-address cache keyed by the peer label; a hit in postConnect
    // or acceptAndRespond would override the configured seed with the
    // first session's stale live values. The config must skip the cache
    // on both sides (the fresh listener avoids the duplicate-label rule).

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig(seed_rtt_ms));
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_caller_sock, caller_addr, m_caller_queue.getConfig(seed_rtt_ms));

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the second listener";

    accept_len      = (int)sizeof accept_addr;
    m_accepted_sock = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    // Pre-data again, on the reused labels: the seed, not the first
    // session's leftovers.
    {
        SRT_TRACEBSTATS perf;
        ASSERT_EQ(srt_bstats(m_caller_sock, &perf, 0), 0);
        EXPECT_DOUBLE_EQ(perf.msRTT, (double)seed_rtt_ms)
            << "caller: a stale per-address cache value replaced the seed on the reused label";
        ASSERT_EQ(srt_bstats(m_accepted_sock, &perf, 0), 0);
        EXPECT_DOUBLE_EQ(perf.msRTT, (double)seed_rtt_ms)
            << "accepted: a stale per-address cache value replaced the seed on the reused label";
    }

    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

// SRTO_RENDEZVOUS has no virtual counterpart: no listener could accept a
// conclusion-first handshake, so the cookie-less opener cannot be
// authenticated. This body covers rendezvous set AFTER the bind (the
// bind-time refusal is BindVirtualDoubleBindRejected's pin), refused at
// connect in startConnect (MJ_NOTSUP/MN_INVAL), synchronously - an
// abandoned connector would live out its TTL and deadlock the close.
void TestVirtualTransport::RendezvousRefusedAfterVirtualBind(bool bind_conclusion_first)
{
    // The labels stay unique to each scenario, like every test's are to
    // its own: the RTT cache and the virtual demux key off them.
    const sockaddr_in self_addr = FillV4(bind_conclusion_first ? "10.218.0.1" : "10.226.0.1", 5555);
    const sockaddr_in peer_addr = FillV4(bind_conclusion_first ? "10.218.0.2" : "10.226.0.2", 5555);

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    if (bind_conclusion_first)
    {
#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
        // A conclusion-first multiplexer through the internal bind - the
        // configuration the refusal is guarding.
        BindVirtualConclusionFirst(m_caller_sock, self_addr, m_caller_queue.getConfig());
#endif
    }
    else
    {
        // A stock multiplexer through the PUBLIC bind - the form the
        // refusal is guarding.
        const SRT_VirtualTransportConfig cfg = m_caller_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&self_addr, (int)sizeof self_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }

    // Rendezvous set AFTER the virtual bind: accepted at set time (the
    // option only demands that the socket not be connecting yet)...
    const int yes = 1;
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes), SRT_ERROR)
        << srt_getlasterror_str();

    // ...but refused at connect time, synchronously - the refusal must
    // not decay into a waveahand-retransmit loop ended by a timeout. The
    // measured duration doubles as the guard against that regression.
    const std::chrono::steady_clock::time_point connect_start = std::chrono::steady_clock::now();
    EXPECT_EQ(srt_connect(m_caller_sock, (const sockaddr*)&peer_addr, (int)sizeof peer_addr), SRT_ERROR);
    // MJ_NOTSUP/MN_INVAL, the synchronous refusal of startConnect.
    ExpectLastErrno(SRT_EINVPARAM);
    const long long connect_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - connect_start)
                                     .count();
    EXPECT_LT(connect_ms, 1500) << "the refusal took the retransmit-until-timeout path";

    // The CRITICAL pin: closing the refused socket must not hang. The
    // connector must have been unregistered at throw time; a leaked entry
    // would stall the close until the rendezvous-queue TTL expires (tens
    // of seconds with the default connect timeout), far past this bound.
    const std::chrono::steady_clock::time_point close_start = std::chrono::steady_clock::now();
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    const long long close_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - close_start).count();
    EXPECT_LT(close_ms, 10000) << "closing the refused rendezvous socket stalled - a connector leaked into the queue";
    m_caller_sock = SRT_INVALID_SOCK;
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// Rendezvous set after a virtual bind through the INTERNAL
// conclusion-first entry - the skip-induction configuration the refusal
// is guarding.
TEST_F(TestVirtualTransport, RendezvousWithVirtualRejected)
{
    RendezvousRefusedAfterVirtualBind(true);
}
#endif

// The same refusal on the PUBLIC binding - the stock handshake form
// srt_bind_virtual configures, as opposed to the conclusion-first
// internal form of the test above.
TEST_F(TestVirtualTransport, RendezvousAfterPublicVirtualBindRejected)
{
    RendezvousRefusedAfterVirtualBind(false);
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The peer-label uniqueness rule of a virtual multiplexer: the source
// label is an owner-assigned identity, so a second caller reusing the
// label of a served connection must be rejected - two concurrent
// connections on one label would make the per-socket link data
// ambiguous. The first connection stays alive; repeats of ITS handshake
// (same peer socket id and ISN) keep being re-answered.
TEST_F(TestVirtualTransport, DuplicateSourceLabelRejected)
{
    const sockaddr_in listener_addr = FillV4("10.219.0.2", 5555);
    // The shared source label: both callers report it to the listener.
    const sockaddr_in shared_addr = FillV4("10.219.0.1", 5556);

    PairWires(shared_addr, listener_addr);

    // The listener in the intended mode: conclusion-first through the
    // internal bind.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig());
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // The FIRST caller on the label connects fine.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_caller_sock, shared_addr, m_caller_queue.getConfig());

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    const int rcv_timeout_ms = 5000;
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    // Sanity: the first connection passes data before the duplicate
    // arrives, and its opening flight was the conclusion-first shape.
    ExpectFirstHandshake(m_caller_wire, srt::URQ_CONCLUSION);
    const char* payload     = "first connection stays alive";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];
    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();

    // The SECOND caller on the SAME label, through its own queue and wire
    // (the listener's responses route back over the shared label to the
    // first caller's queue, so the second caller observes the refusal
    // through its bounded connect timeout).
    Wire                   dup_wire;
    srt::SRTTransportQueue dup_queue(&WireSend, &dup_wire);
    dup_wire.peer = &m_listener_queue;
    FillWireSource(dup_wire, shared_addr);
    SRTSOCKET dup_sock = srt_create_socket();
    ASSERT_NE(dup_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(dup_sock, shared_addr, dup_queue.getConfig());
    const int conn_timeout_ms = 500;
    ASSERT_NE(srt_setsockopt(dup_sock, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    EXPECT_EQ(srt_connect(dup_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR);
    // The refusal observed from the second caller: bounded by its connect
    // timeout, as the rejection response routes to the first caller.
    ExpectLastErrno(SRT_ENOSERVER);

    // The wire pin: ENOSERVER would equally follow from a listener that
    // silently ignored the handshake (on this shared-label wire every
    // response routes into the first caller's queue). Only the wire
    // tells an active rejection apart - a response addressed to the
    // duplicate's socket id carrying the rejection reason.
    long long rejection_sends = 0;
    for (int waited = 0; waited < 500 && (rejection_sends = HandshakeSendsTo(m_listener_wire, dup_sock)) < 1;
         waited += 10)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_GE(rejection_sends, 1)
        << "the listener never answered the duplicate caller - a silent drop would look the same to the caller";
    ExpectLatestHandshakeTo(m_listener_wire, dup_sock, srt::URQFailure(SRT_REJ_BACKLOG));

    // The refusal was a rejection, not a queueing: no second connection
    // may appear on the listener, and the listener stays listening.
    EXPECT_FALSE(WaitReadable(m_listener_sock, 500)) << "a second connection was queued from the duplicate label";
    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_LISTENING);

    // The FIRST connection still passes data, both ways, after the
    // duplicate was rejected.
    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
    ASSERT_EQ(srt_sendmsg(m_accepted_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_caller_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();

    // Close the duplicate before its local wire and queue go away.
    EXPECT_EQ(srt_close(dup_sock), SRT_STATUS_OK) << srt_getlasterror_str();

    // Replays of the FIRST connection's handshake keep being re-answered:
    // a handshake addressed to the FIRST caller (only that counts - the
    // accepted socket's ACK/keepalive traffic shares the wire and would
    // satisfy a raw send count), carrying a conclusion response, not a
    // duplicate-rule failure. The replay queues nothing new.
    std::vector<char> replay;
    {
        std::lock_guard<std::mutex> lock(m_caller_wire.first_datagram_lock);
        replay = m_caller_wire.first_datagram;
    }
    ASSERT_FALSE(replay.empty());
    const long long answered_before = HandshakeSendsTo(m_listener_wire, m_caller_sock);
    ASSERT_EQ(m_listener_queue.push(replay.data(), replay.size(), (const struct sockaddr*)&shared_addr,
                                    (int)sizeof shared_addr),
              0);

    bool reanswered = false;
    for (int i = 0; i < 500 && !reanswered; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        reanswered = HandshakeSendsTo(m_listener_wire, m_caller_sock) > answered_before;
    }
    EXPECT_TRUE(reanswered) << "the repeat handshake of the existing connection was not re-answered";
    ExpectLatestHandshakeTo(m_listener_wire, m_caller_sock, srt::URQ_CONCLUSION);

    EXPECT_FALSE(WaitReadable(m_listener_sock, 500)) << "the replayed handshake queued a second connection";
    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_LISTENING);

    // And the first connection is still fully alive after the replay.
    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();

    // Clean close - the first connection and the listener.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The handshake-in-flight window of the duplicate-label rule: the label
// is taken once the accepted socket JOINS the multiplexer, before the
// connection completes and the dispatch copy is stamped. The first
// connection is held open by swallowing every listener response to it
// (HeldWire); its retransmits keep being re-answered, and after the hold
// is released it must still complete and carry data - the hold broke nothing.
TEST_F(TestVirtualTransport, DuplicateLabelRejectedWhileHandshakeInFlight)
{
    const sockaddr_in listener_addr = FillV4("10.227.0.2", 5555);
    // The shared source label: both callers report it to the listener.
    const sockaddr_in shared_addr = FillV4("10.227.0.1", 5556);

    struct Rig
    {
        SRTSOCKET listener  = SRT_INVALID_SOCK;
        SRTSOCKET held      = SRT_INVALID_SOCK; // caller A, its handshake held open
        SRTSOCKET dup       = SRT_INVALID_SOCK; // caller B, the duplicate label
        SRTSOCKET accepted  = SRT_INVALID_SOCK;
        HeldWire listener_wire; // routes the listener's datagrams to A or B
        Wire     held_wire;     // A's outgoing datagrams, sourced by the shared label
        Wire     dup_wire;      // B's outgoing datagrams, sourced by the shared label
        srt::SRTTransportQueue listener_queue{ &HeldWireSend, &listener_wire };
        srt::SRTTransportQueue held_queue{ &WireSend, &held_wire };
        srt::SRTTransportQueue dup_queue{ &WireSend, &dup_wire };
        SocketRig closer{ &accepted, &held, &dup, &listener };
    } rig;

    // Listener side: conclusion-first through the internal bind; its
    // responses report its own label as the source.
    rig.listener = srt_create_socket();
    ASSERT_NE(rig.listener, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(rig.listener, listener_addr, rig.listener_queue.getConfig());
    FillSource(rig.listener_wire.source_addr, rig.listener_wire.source_len, listener_addr);
    ASSERT_NE(srt_listen(rig.listener, 4), SRT_ERROR) << srt_getlasterror_str();

    // Caller A, asynchronous (its handshake will be held open, so a
    // blocking connect would only run into its timeout), with a connect
    // timeout generous enough to span the whole hold.
    rig.held = srt_create_socket();
    ASSERT_NE(rig.held, SRT_INVALID_SOCK);
    {
        const int no_block = 0;
        ASSERT_EQ(srt_setsockopt(rig.held, 0, SRTO_RCVSYN, &no_block, sizeof no_block), 0);
        const int conn_timeout_ms = 10000;
        ASSERT_NE(srt_setsockopt(rig.held, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), SRT_ERROR)
            << srt_getlasterror_str();
    }
    BindVirtualConclusionFirst(rig.held, shared_addr, rig.held_queue.getConfig());
    rig.held_wire.peer = &rig.listener_queue;
    FillWireSource(rig.held_wire, shared_addr);

    // The hold, armed before the very first flight: every listener
    // response addressed to A is swallowed, so A's accepted socket exists
    // on the listener's multiplexer while A - never hearing a response -
    // stays SRTS_CONNECTING and keeps retransmitting its handshake.
    rig.listener_wire.held_sock = rig.held;
    rig.listener_wire.held_peer = &rig.held_queue;
    rig.listener_wire.hold.store(true, std::memory_order_relaxed);

    ASSERT_NE(srt_connect(rig.held, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ExpectFirstHandshake(rig.held_wire, srt::URQ_CONCLUSION);

    // Sync point: the listener side has processed A's conclusion - its
    // first response attempt towards A was observed (and swallowed) on
    // the wire - before the duplicate is fired. The connection is queued
    // and taken off the listener here, so that a later readability check
    // can only mean a SECOND connection from the held label.
    for (int waited = 0; waited < 500 && rig.listener_wire.held_sends.load() < 1; waited += 10)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_GE(rig.listener_wire.held_sends.load(), 1) << "the listener never responded to the held caller";
    ASSERT_TRUE(WaitReadable(rig.listener, 5000)) << "the held connection was never queued on the listener";
    {
        sockaddr_in accept_addr;
        int         accept_len = (int)sizeof accept_addr;
        rig.accepted           = srt_accept(rig.listener, (sockaddr*)&accept_addr, &accept_len);
        ASSERT_NE(rig.accepted, SRT_INVALID_SOCK) << srt_getlasterror_str();
        EXPECT_EQ(accept_addr.sin_addr.s_addr, shared_addr.sin_addr.s_addr);
        EXPECT_EQ(accept_addr.sin_port, shared_addr.sin_port);
    }

    // Caller B: a NEW connection from the SAME label, through its own
    // socket and transport. Only A is held - the listener's responses
    // addressed to B are delivered - so B observes the refusal directly.
    rig.dup = srt_create_socket();
    ASSERT_NE(rig.dup, SRT_INVALID_SOCK);
    const int dup_timeout_ms = 3000;
    ASSERT_NE(srt_setsockopt(rig.dup, 0, SRTO_CONNTIMEO, &dup_timeout_ms, sizeof dup_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(rig.dup, shared_addr, rig.dup_queue.getConfig());
    rig.dup_wire.peer = &rig.listener_queue;
    FillWireSource(rig.dup_wire, shared_addr);
    rig.listener_wire.other_sock = rig.dup;
    rig.listener_wire.other_peer = &rig.dup_queue;

    // THE PIN: the duplicate label is rejected although A's connection
    // never completed - the label has been taken since A's accepted
    // socket joined the listener's multiplexer. Delivered straight back
    // to B, the refusal carries the duplicate-label rejection reason and
    // must not decay into a connect timeout.
    const std::chrono::steady_clock::time_point dup_start = std::chrono::steady_clock::now();
    EXPECT_EQ(srt_connect(rig.dup, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << "the duplicate label was accepted while the first handshake was still in flight";
    ExpectLastErrno(SRT_ECONNREJ);
    EXPECT_EQ(srt_getrejectreason(rig.dup), SRT_REJ_BACKLOG);
    const long long dup_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - dup_start)
                                  .count();
    EXPECT_LT(dup_ms, dup_timeout_ms) << "the refusal took the retransmit-until-timeout path";

    // The refusal was a rejection, not a queueing: no second connection
    // may appear on the listener, and the listener stays listening.
    EXPECT_FALSE(WaitReadable(rig.listener, 500)) << "a second connection was queued from the held label";
    EXPECT_EQ(srt_getsockstate(rig.listener), SRTS_LISTENING);

    // The held connection is untouched by the refusal: A is still
    // connecting, and its retransmitted handshake keeps being re-answered
    // (the swallowed response attempts towards A keep counting) - this is
    // the peer-socket-id exemption of the duplicate-label rejection.
    const long long answered_before = rig.listener_wire.held_sends.load();
    EXPECT_EQ(srt_getsockstate(rig.held), SRTS_CONNECTING);
    bool reanswered = false;
    for (int waited = 0; waited < 500 && !reanswered; waited += 10)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        reanswered = rig.listener_wire.held_sends.load() > answered_before;
    }
    EXPECT_TRUE(reanswered) << "the held connection's retransmitted handshake was not re-answered";
    EXPECT_EQ(srt_getsockstate(rig.held), SRTS_CONNECTING);

    // Release the hold: A's next retransmission is finally answered
    // through, and the held connection must complete fully - the hold
    // broke nothing on A's path.
    rig.listener_wire.hold.store(false, std::memory_order_relaxed);
    ASSERT_TRUE(WaitState(rig.held, SRTS_CONNECTED, 5000)) << "the held connection never completed";
    EXPECT_EQ(srt_getsockstate(rig.accepted), SRTS_CONNECTED);
    {
        const int rcv_timeout_ms = 5000;
        ASSERT_NE(srt_setsockopt(rig.accepted, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
            << srt_getlasterror_str();
    }

    // Data flows over the completed connection, both ways.
    const char* payload     = "held connection completes";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];
    ASSERT_EQ(srt_sendmsg(rig.held, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(rig.accepted, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);
    ASSERT_EQ(srt_sendmsg(rig.accepted, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_TRUE(WaitReadable(rig.held, 5000)) << "the held caller never became read-ready";
    ASSERT_GT(srt_recvmsg(rig.held, buf, (int)sizeof buf), 0) << srt_getlasterror_str();
    EXPECT_EQ(memcmp(buf, payload, payload_len), 0);

    // Clean close - the duplicate, the accepted socket, A, then the
    // listener.
    EXPECT_EQ(srt_close(rig.dup), SRT_STATUS_OK) << srt_getlasterror_str();
    rig.dup = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(rig.accepted), SRT_STATUS_OK) << srt_getlasterror_str();
    rig.accepted = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(rig.held), SRT_STATUS_OK) << srt_getlasterror_str();
    rig.held = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(rig.listener), SRT_STATUS_OK) << srt_getlasterror_str();
    rig.listener = SRT_INVALID_SOCK;
}
#endif

// The label-family gate of the connect target: a virtual transport
// label cannot carry a datagram to an address of another family, so
// the connect is refused up front - a UDP socket gets this enforcement
// from the OS. Without it the mismatch is a silent blackhole: the
// deliver-side family gate drops every response, and the caller times
// out on a medium that can never answer.
TEST_F(TestVirtualTransport, ConnectTargetFamilyMismatchRejected)
{
    // The labels stay unique to each scenario, like every test's are to
    // its own: the RTT cache and the virtual demux key off them.
    const sockaddr_in  v4_label     = FillV4("10.227.0.1", 5556);
    const sockaddr_in6 v6_label     = FillV6("fd00:227::1", 5556);
    const sockaddr_in6 v6_target    = FillV6("fd00:227::2", 5555);
    const sockaddr_in  v4_target    = FillV4("10.227.0.2", 5555);
    const SRT_VirtualTransportConfig cfg = m_caller_queue.getConfig();

    // v4 label, v6 target.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&v4_label, (int)sizeof v4_label, &cfg), SRT_STATUS_OK)
        << srt_getlasterror_str();
    EXPECT_EQ(srt_connect(m_caller_sock, (const sockaddr*)&v6_target, (int)sizeof v6_target), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();

    // Belt and braces, like the rendezvous refusal: the refused socket
    // must close without a stall. The refusal fires in connect() before
    // any connector registration, so nothing may leak even if the check
    // ever moves closer to startConnect().
    const std::chrono::steady_clock::time_point close_start = std::chrono::steady_clock::now();
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    const long long close_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - close_start).count();
    EXPECT_LT(close_ms, 10000) << "closing the refused socket stalled - a connector leaked into the queue";
    m_caller_sock = SRT_INVALID_SOCK;

    // The mirror direction: v6 label, v4 target.
    SRTSOCKET caller6 = srt_create_socket();
    ASSERT_NE(caller6, SRT_INVALID_SOCK);
    ASSERT_EQ(srt_bind_virtual(caller6, (const sockaddr*)&v6_label, (int)sizeof v6_label, &cfg), SRT_STATUS_OK)
        << srt_getlasterror_str();
    EXPECT_EQ(srt_connect(caller6, (const sockaddr*)&v4_target, (int)sizeof v4_target), SRT_ERROR);
    EXPECT_EQ(srt_getlasterror(nullptr), SRT_EINVPARAM) << srt_getlasterror_str();
    EXPECT_EQ(srt_close(caller6), SRT_STATUS_OK) << srt_getlasterror_str();
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// srt_close() strikes mid-stream on its own thread while sender and
// receiver threads keep the stream flowing: the close must tear the
// caller's dedicated multiplexer down (joining its workers) without
// deadlocking against the in-flight srt_sendmsg - every join is bounded.
// After the close returns, SRT must never invoke that socket's callbacks
// again (the send-count freeze frees the owner to release the wire).
TEST_F(TestVirtualTransport, CloseDuringLiveTraffic)
{
    const sockaddr_in listener_addr = FillV4("10.220.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.220.0.1", 5556);

    PairWires(caller_addr, listener_addr);

    // Listener side: conclusion-first both ways (the internal bind), as a
    // WebTransport gateway would configure its sessions.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig());
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_caller_sock, caller_addr, m_caller_queue.getConfig());

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    // Fail fast instead of hanging should the stream end go unobserved.
    // The window still sits well above the default 120 ms TSBPD latency,
    // so the healthy stream is never cut by it.
    const int rcv_timeout_ms = 500;
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    // A sanity round trip first, so the stream provably flows before the
    // close.
    const char* warm_up     = "vt-close warm-up";
    const int   warm_up_len = (int)strlen(warm_up);
    char        buf[1456];
    ASSERT_EQ(srt_sendmsg(m_caller_sock, warm_up, warm_up_len, SRT_MSGTTL_INF, false), warm_up_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), warm_up_len) << srt_getlasterror_str();

    // The mid-stream signal: the receiver announces through the promise
    // once a fair chunk of the stream has been delivered, and the main
    // thread waits for that with a deadline before closing. The promise
    // is shared so that a failure path cannot leave it dangling.
    std::shared_ptr<std::promise<void> > stream_flowing(new std::promise<void>);
    auto                                 stream_flowing_f = stream_flowing->get_future();

    struct RecvStats
    {
        int  received   = 0;
        bool ended      = false;
        bool sequence_ok = true;
        int  last_error = 0;
    };

    const int stream_msgs = 2000;

    // The sender paces the stream so that it is guaranteed to still be
    // flowing when the close strikes; the pacing shapes the traffic, it
    // is not a wait for a condition. The socket id is captured by value:
    // the main thread invalidates the member below while this thread is
    // still sending on the same (closed) socket - that overlap is the
    // scenario under test.
    const SRTSOCKET sender_sock = m_caller_sock;
    auto sender = std::async(std::launch::async,
                             [sender_sock, stream_msgs]()
                             {
                                 int sent_ok = 0;
                                 for (int i = 0; i < stream_msgs; ++i)
                                 {
                                     char      msg[64];
                                     const int len = snprintf(msg, sizeof msg, "vt-close stream %05d", i);
                                     if (srt_sendmsg(sender_sock, msg, len, SRT_MSGTTL_INF, false) == SRT_ERROR)
                                         break; // a clean refusal is acceptable; a hang or a crash is not
                                     ++sent_ok;
                                     std::this_thread::sleep_for(std::chrono::microseconds(500));
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
                                           // The bounded stream-end signal: the receive timeout,
                                           // or the connection broken by the peer's shutdown.
                                           st.last_error = srt_getlasterror(nullptr);
                                           if (st.received >= 50)
                                               st.ended = true;
                                           break;
                                       }

                                       char      want[64];
                                       const int wlen = snprintf(want, sizeof want, "vt-close stream %05d", st.received);
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
        << "the stream never flowed before the close";

    // The close itself, on its own thread and bounded: it must return
    // even while the sender is in mid-stream. The result carries the
    // closer thread's own last-error state; reading srt_getlasterror()
    // here would report this thread's, which made no call at all.
    struct CloseResult
    {
        SRTSTATUS   status = SRT_ERROR;
        int         error  = 0;
        std::string message;
    };
    auto closer = std::async(std::launch::async,
                             [this]()
                             {
                                 CloseResult r;
                                 r.status  = srt_close(m_caller_sock);
                                 r.error   = srt_getlasterror(nullptr);
                                 r.message = srt_getlasterror_str();
                                 return r;
                             });
    ASSERT_EQ(closer.wait_for(std::chrono::seconds(10)), std::future_status::ready)
        << "srt_close deadlocked against the live traffic";
    const CloseResult closed = closer.get();
    EXPECT_EQ(closed.status, SRT_STATUS_OK) << closed.message << " (closer-thread error " << closed.error << ")";
    // The caller socket is closed exactly once - by the closer thread.
    m_caller_sock = SRT_INVALID_SOCK;

    // Both traffic threads must finish well within their bounds - the
    // sender runs until its sends start failing on the closed socket, the
    // receiver until the stream end is reported.
    ASSERT_EQ(sender.wait_for(std::chrono::seconds(15)), std::future_status::ready)
        << "the sender thread hung after the close";
    const int sent_ok = sender.get();
    ASSERT_EQ(receiver.wait_for(std::chrono::seconds(15)), std::future_status::ready)
        << "the receiver thread hung after the close";
    const RecvStats rs = receiver.get();

    EXPECT_GE(sent_ok, 1);
    EXPECT_TRUE(rs.sequence_ok) << "the received stream broke its sequence";
    EXPECT_GE(rs.received, 50);
    EXPECT_TRUE(rs.ended) << "the peer never observed the end of the stream";
    EXPECT_TRUE(rs.last_error == SRT_ETIMEOUT || rs.last_error == SRT_ECONNLOST)
        << "the stream end was reported as an unexpected error " << rs.last_error;

    // After the close has returned (and the application has stopped
    // sending), SRT must never invoke the send callback again: the wire is
    // an application object, and this freeze is what frees the owner to
    // release it. A short grace period lets any callback already in
    // flight when srt_close returned complete, so the freeze window
    // measures the steady state, not the tail of the race.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const long long callbacks_frozen = m_caller_wire.send_count.load();
    bool           frozen           = true;
    for (int waited = 0; waited < 1000 && frozen; waited += 10)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (m_caller_wire.send_count.load() != callbacks_frozen)
            frozen = false;
    }
    EXPECT_TRUE(frozen) << "the send callback fired again after srt_close returned";

    // The teardown of the peer side must stay clean and bounded: the
    // accepted socket observed the stream end, and the listener leaves
    // with its accepted socket gone.
    const std::chrono::steady_clock::time_point teardown_start = std::chrono::steady_clock::now();
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
    const long long teardown_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - teardown_start)
                                      .count();
    EXPECT_LT(teardown_ms, 10000) << "closing the peer side after the mid-stream close stalled";
}
#endif

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// Several callers, ONE listener: all three sessions exchange datagrams
// through the same listener queue, the switch demultiplexing by
// destination address (the caller's source label) - a gateway serving
// several WebTransport sessions over one listener. Every accepted socket
// must independently carry payload, both ways, all connections open at
// once, with no cross-talk.
TEST_F(TestVirtualTransport, MultiAcceptSharedListenerMultiplexer)
{
    const sockaddr_in listener_addr = FillV4("10.221.0.2", 5555);

    struct Session
    {
        SRTSOCKET caller   = SRT_INVALID_SOCK;
        SRTSOCKET accepted = SRT_INVALID_SOCK;
        Wire      caller_wire;
        sockaddr_in caller_addr;
        // The wire must be declared before the queue that hands it to the
        // send callback: members are destroyed in reverse order, so the
        // queue goes away first.
        srt::SRTTransportQueue caller_queue{ &WireSend, &caller_wire };
    };

    struct Rig
    {
        SRTSOCKET listener = SRT_INVALID_SOCK;
        Switch    listener_switch;
        srt::SRTTransportQueue listener_queue{ &SwitchSend, &listener_switch };
        Session              sessions[3];
        SocketRig closer{ &sessions[0].accepted, &sessions[0].caller, &sessions[1].accepted, &sessions[1].caller,
                          &sessions[2].accepted, &sessions[2].caller, &listener };
    } rig;

    rig.listener = srt_create_socket();
    ASSERT_NE(rig.listener, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(rig.listener, listener_addr, rig.listener_queue.getConfig());
    // Everything the shared queue sends out reports the listener's label as
    // its source; the switch routes it to the caller the datagram is
    // addressed to.
    FillSource(rig.listener_switch.source_addr, rig.listener_switch.source_len, listener_addr);
    ASSERT_NE(srt_listen(rig.listener, 4), SRT_ERROR) << srt_getlasterror_str();

    // Three callers, sequentially - deterministically - each with its own
    // queue wired into the shared listener queue.
    const int num_sessions = (int)(sizeof rig.sessions / sizeof rig.sessions[0]);
    for (int i = 0; i < num_sessions; ++i)
    {
        Session& s    = rig.sessions[i];
        s.caller_addr = FillV4("10.221.1.1", (uint16_t)(5556 + i));

        s.caller = srt_create_socket();
        ASSERT_NE(s.caller, SRT_INVALID_SOCK);
        BindVirtualConclusionFirst(s.caller, s.caller_addr, s.caller_queue.getConfig());
        s.caller_wire.peer = &rig.listener_queue;
        FillWireSource(s.caller_wire, s.caller_addr);
        rig.listener_switch.routes.push_back(Switch::Route{ s.caller_addr, &s.caller_queue });

        ASSERT_NE(srt_connect(s.caller, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
            << srt_getlasterror_str();

        ASSERT_TRUE(WaitReadable(rig.listener, 15000)) << "no connection was ever queued on the listener";

        sockaddr_in accept_addr;
        int         accept_len = (int)sizeof accept_addr;
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

    // Clean close - the accepted sockets, the callers, then the listener.
    for (Session& s : rig.sessions)
    {
        EXPECT_EQ(srt_close(s.accepted), SRT_STATUS_OK) << srt_getlasterror_str();
        s.accepted = SRT_INVALID_SOCK;
    }
    for (Session& s : rig.sessions)
    {
        EXPECT_EQ(srt_close(s.caller), SRT_STATUS_OK) << srt_getlasterror_str();
        s.caller = SRT_INVALID_SOCK;
    }
    EXPECT_EQ(srt_close(rig.listener), SRT_STATUS_OK) << srt_getlasterror_str();
    rig.listener = SRT_INVALID_SOCK;
}
#endif

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// Two callers bound virtually to the IDENTICAL label: every
// srt_bind_virtual() creates a dedicated multiplexer never reused by
// another socket, so the callers coexist - told apart by which listener
// they went to, not the label. Both sessions run at once, and the
// lifecycle must create no OS socket fds (bounded via /proc/self/fd).
TEST_F(TestVirtualTransport, DedicatedMuxerSameLabel)
{
    // The identical label both callers report to their listeners.
    const sockaddr_in shared_label = FillV4("10.222.0.1", 5556);

    const int sockets_before = CountSocketFds();
    ASSERT_GE(sockets_before, 0);

    struct Pair
    {
        SRTSOCKET listener  = SRT_INVALID_SOCK;
        SRTSOCKET caller    = SRT_INVALID_SOCK;
        SRTSOCKET accepted  = SRT_INVALID_SOCK;
        sockaddr_in listener_addr;
        Wire        caller_wire;   // the caller's outgoing datagrams, sourced by the shared label
        Wire        listener_wire; // the listener's outgoing datagrams, sourced by its own label
        srt::SRTTransportQueue caller_queue{ &WireSend, &caller_wire };
        srt::SRTTransportQueue listener_queue{ &WireSend, &listener_wire };
        SocketRig closer{ &accepted, &caller, &listener };
    };

    struct Rig
    {
        Pair pairs[2];
    } rig;
    rig.pairs[0].listener_addr = FillV4("10.222.0.2", 5555);
    rig.pairs[1].listener_addr = FillV4("10.222.0.3", 5555);

    // Two listeners, each on its own label and its own queue.
    for (Pair& p : rig.pairs)
    {
        p.listener = srt_create_socket();
        ASSERT_NE(p.listener, SRT_INVALID_SOCK);
        BindVirtualConclusionFirst(p.listener, p.listener_addr, p.listener_queue.getConfig());
        p.listener_wire.peer = &p.caller_queue;
        FillWireSource(p.listener_wire, p.listener_addr);
        ASSERT_NE(srt_listen(p.listener, 4), SRT_ERROR) << srt_getlasterror_str();
    }

    // Two callers, both bound virtually to the IDENTICAL label, each wired
    // to its own listener.
    for (Pair& p : rig.pairs)
    {
        p.caller = srt_create_socket();
        ASSERT_NE(p.caller, SRT_INVALID_SOCK);
        BindVirtualConclusionFirst(p.caller, shared_label, p.caller_queue.getConfig());
        ExpectSockName(p.caller, shared_label);
        p.caller_wire.peer = &p.listener_queue;
        FillWireSource(p.caller_wire, shared_label);
    }

    // Both connections at the same time: both connects are placed before
    // either accept is taken.
    for (Pair& p : rig.pairs)
    {
        ASSERT_NE(srt_connect(p.caller, (const sockaddr*)&p.listener_addr, (int)sizeof p.listener_addr), SRT_ERROR)
            << srt_getlasterror_str();
    }
    for (Pair& p : rig.pairs)
    {
        ASSERT_TRUE(WaitReadable(p.listener, 15000)) << "no connection was ever queued on the listener";

        sockaddr_in accept_addr;
        int         accept_len = (int)sizeof accept_addr;
        p.accepted             = srt_accept(p.listener, (sockaddr*)&accept_addr, &accept_len);
        ASSERT_NE(p.accepted, SRT_INVALID_SOCK) << srt_getlasterror_str();
        // Both listeners see the very same peer label - harmless, because
        // neither multiplexer serves any other peer.
        EXPECT_EQ(accept_addr.sin_addr.s_addr, shared_label.sin_addr.s_addr);
        EXPECT_EQ(accept_addr.sin_port, shared_label.sin_port);
    }

    const int rcv_timeout_ms = 5000;
    for (Pair& p : rig.pairs)
    {
        ASSERT_TRUE(WaitState(p.caller, SRTS_CONNECTED, 5000)) << "the caller never completed the connection";
        EXPECT_EQ(srt_getsockstate(p.accepted), SRTS_CONNECTED);
        ASSERT_NE(srt_setsockopt(p.caller, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
            << srt_getlasterror_str();
        ASSERT_NE(srt_setsockopt(p.accepted, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
            << srt_getlasterror_str();
    }

    // The identical label must not cross the two sessions: each pair
    // exchanges its own payload, both ways, with both open at once.
    const char* payloads[2] = { "same-label session zero", "same-label session one" };
    char        buf[1456];
    for (int i = 0; i < 2; ++i)
    {
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_sendmsg(rig.pairs[i].caller, payloads[i], len, SRT_MSGTTL_INF, false), len)
            << srt_getlasterror_str();
    }
    for (int i = 0; i < 2; ++i)
    {
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_recvmsg(rig.pairs[i].accepted, buf, (int)sizeof buf), len) << srt_getlasterror_str();
        EXPECT_EQ(memcmp(buf, payloads[i], len), 0) << "session " << i << " received a foreign message";
    }
    for (int i = 0; i < 2; ++i)
    {
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_sendmsg(rig.pairs[i].accepted, payloads[i], len, SRT_MSGTTL_INF, false), len)
            << srt_getlasterror_str();
    }
    for (int i = 0; i < 2; ++i)
    {
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_recvmsg(rig.pairs[i].caller, buf, (int)sizeof buf), len) << srt_getlasterror_str();
        EXPECT_EQ(memcmp(buf, payloads[i], len), 0) << "session " << i << " received a foreign message";
    }

    // Clean close - the accepted sockets, the callers, then the listeners.
    for (Pair& p : rig.pairs)
    {
        EXPECT_EQ(srt_close(p.accepted), SRT_STATUS_OK) << srt_getlasterror_str();
        p.accepted = SRT_INVALID_SOCK;
    }
    for (Pair& p : rig.pairs)
    {
        EXPECT_EQ(srt_close(p.caller), SRT_STATUS_OK) << srt_getlasterror_str();
        p.caller = SRT_INVALID_SOCK;
    }
    for (Pair& p : rig.pairs)
    {
        EXPECT_EQ(srt_close(p.listener), SRT_STATUS_OK) << srt_getlasterror_str();
        p.listener = SRT_INVALID_SOCK;
    }

    // The whole lifecycle left no new OS socket fds: the count stays
    // within the pre-test bound (an upper bound only - the process-wide
    // count can also drop when other tests' sockets are swept meanwhile).
    EXPECT_LE(CountSocketFds(), sockets_before);
}
#endif

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The receiver side of SRTO_TLPKTDROP: a data packet lost while later
// packets keep flowing must be dropped as too-late-to-play once the first
// subsequent packet passes its TSBPD play time, not hold the stream for a
// retransmission. The wire swallows every copy of one message; the rest
// must arrive in order with the drop counted in the stats.
TEST_F(TestVirtualTransport, ReceiverDropsTooLateMissingPacket)
{
    const sockaddr_in listener_addr = FillV4("10.223.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.223.0.1", 5556);

    PairWires(caller_addr, listener_addr);

    const int latency_ms = 60;

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_LATENCY, &latency_ms, sizeof latency_ms), SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig());
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_LATENCY, &latency_ms, sizeof latency_ms), SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(m_caller_sock, caller_addr, m_caller_queue.getConfig());

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    // Fail fast instead of hanging should the too-late drop not happen.
    const int rcv_timeout_ms = 5000;
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    // From now on every datagram carrying the victim message disappears.
    m_caller_wire.swallow_tag = "SWALLOW-ME";

    const int num_msgs = 10;
    char      payloads[num_msgs][64];
    for (int i = 0; i < num_msgs; ++i)
    {
        snprintf(payloads[i], sizeof payloads[i], "vt-late msg %02d%s", i + 1, i == 0 ? " SWALLOW-ME" : "");
        const int len = (int)strlen(payloads[i]);
        ASSERT_EQ(srt_sendmsg(m_caller_sock, payloads[i], len, SRT_MSGTTL_INF, false), len) << srt_getlasterror_str();
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

    // Clean close - the shutdown flights go through the wires too.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

namespace
{

// One valid SRT datagram in network byte order - the shared input of the
// channel unit tests: the 16-byte header (solo DATA, sequence 1, message
// 1, destination socket id 2) plus payload_len bytes counting 0x00,
// 0x01, ... The payload is 600 bytes in every channel test: big enough
// that the +1000 size lie pushes the report past the 16 + 1500 buffers.
std::vector<char> MakeSoloDataDatagram(size_t payload_len)
{
    std::vector<char> datagram(srt::CPacket::HDR_SIZE + payload_len);
    uint32_t*         hdr = (uint32_t*)datagram.data();
    hdr[0] = htonl(1);                                                    // SEQNO: data, sequence 1
    hdr[1] = htonl((uint32_t)srt::PacketBoundaryBits(srt::PB_SOLO) | 1u); // MSGNO: solo, message 1
    hdr[2] = htonl(0);                                                    // TIMESTAMP
    hdr[3] = htonl(2);                                                    // destination socket id
    for (size_t i = 0; i < payload_len; ++i)
        datagram[srt::CPacket::HDR_SIZE + i] = (char)(i & 0xff);
    return datagram;
}

} // namespace

// The buffer-capacity contract of CChannelVirtual::recvfrom, unit-tested
// on the channel: the deliver callback's reported size is trusted only
// as far as the buffers SRT handed it - unlike on UDP, where the kernel
// drops what does not fit (MSG_TRUNC), the channel must enforce the
// bound itself. A report past the capacity plants an out-of-bounds
// length; a negative one other than -1 wraps into a huge size.
TEST_F(TestVirtualTransport, ChannelBoundsDeliverReportByCapacity)
{
    const sockaddr_in label_addr = FillV4("10.224.0.2", 5555);
    const sockaddr_in src_addr   = FillV4("10.224.0.1", 5556);

    const size_t            payload_len = 600;
    const std::vector<char> datagram    = MakeSoloDataDatagram(payload_len);

    // The honest deliver: the reported size is the size of the datagram,
    // and the datagram is taken in whole.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr, sizeof src_addr),
                  0);

        const srt::CChannelVirtual channel(
            queue.getConfig(), srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_OK);
        EXPECT_EQ(packet.getLength(), payload_len);
        // The datagram was really scattered into the provided buffers
        // (a data packet's payload stays in the network byte order).
        EXPECT_EQ(memcmp(packet.getData(), datagram.data() + srt::CPacket::HDR_SIZE, payload_len), 0);
    }

    // deliver reports 1000 more than it wrote - past the provided
    // buffers. Like MSG_TRUNC on a UDP socket, the datagram must be
    // dropped: RST_AGAIN with the packet length reset, never a length
    // planted beyond the payload buffer.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr, sizeof src_addr),
                  0);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.deliver = &DeliverLyingLarge; // the config's opaq stays the queue

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_AGAIN);
        EXPECT_EQ(packet.getLength(), (size_t)-1) << "a size past the provided buffers was planted as the length";
    }

    // deliver reports a negative size other than -1: it must not wrap
    // around into a huge unsigned value, but drop the datagram the same
    // way.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr, sizeof src_addr),
                  0);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.deliver = &DeliverLyingNegative;

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_AGAIN);
        EXPECT_EQ(packet.getLength(), (size_t)-1) << "a negative report wrapped into a bogus length";
    }
}

// The source-address twin of the capacity check above: the reported
// source family is trusted only as a family SRT can dispatch by - the
// kernel fills a valid one on UDP, but the arbitrary owner callback
// forces the channel to check, or a bogus family flows into the
// per-address dispatch. The lying deliver overwrites the family with
// AF_UNSPEC: the datagram must be discarded, never handed upstream.
TEST_F(TestVirtualTransport, ChannelDropsDatagramWithBogusSourceFamily)
{
    const sockaddr_in label_addr = FillV4("10.224.0.2", 5555);
    const sockaddr_in src_addr   = FillV4("10.224.0.1", 5556);

    // The queue's honest deliver must succeed on this datagram, so that
    // the only lie is the source family.
    const size_t            payload_len = 600;
    const std::vector<char> datagram    = MakeSoloDataDatagram(payload_len);

    // The bogus family: a value no network stack would ever report.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr, sizeof src_addr),
                  0);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.deliver = &DeliverLyingSourceFamily; // the config's opaq stays the queue

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        // The datagram is dropped like a truncated one on a UDP socket:
        // the channel reports nothing received and the packet length is
        // reset, and the bogus family never leaves the channel.
        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_AGAIN);
        EXPECT_EQ(packet.getLength(), (size_t)-1) << "a datagram with a bogus source family was accepted";
    }

    // The cross-family source: a valid IPv6 address delivered to a
    // channel bound with an IPv4 label. The address buffer handed to
    // deliver is sized by the label's family, so even the honest deliver
    // writes only sizeof(sockaddr_in) bytes - memory-safe but truncated
    // nonsense (an AF_INET UDP socket never receives AF_INET6). Same
    // drop as above.
    sockaddr_in6 src_addr6;
    memset(&src_addr6, 0, sizeof src_addr6);
    src_addr6.sin6_family         = AF_INET6;
    src_addr6.sin6_port           = htons(5558);
    src_addr6.sin6_addr.s6_addr[15] = 1; // ::1

    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr6, sizeof src_addr6),
                  0);

        const srt::CChannelVirtual channel(
            queue.getConfig(), srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_AGAIN);
        EXPECT_EQ(packet.getLength(), (size_t)-1) << "a datagram with a cross-family source was accepted";
    }
}

// The length twin of the bogus-family test: a deliver callback returning
// success must have reported the source address, so *w_srclen must land
// within (0, the given capacity] - the channel must check (the kernel
// always fills one on UDP), or the datagram flows upstream with whatever
// the pre-zeroed buffer holds. Lies: *w_srclen = 0, past-capacity, and
// positive but shorter than a whole address of the reported family; all
// must be discarded, never handed upstream.
TEST_F(TestVirtualTransport, ChannelDropsDatagramWithBogusSourceLength)
{
    const sockaddr_in label_addr = FillV4("10.224.0.2", 5555);
    const sockaddr_in src_addr   = FillV4("10.224.0.1", 5556);

    // The queue's honest deliver must succeed on this datagram, so that
    // the only lie is the source address length.
    const size_t            payload_len = 600;
    const std::vector<char> datagram    = MakeSoloDataDatagram(payload_len);

    // The source length zero: success reported, no source address.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr, sizeof src_addr),
                  0);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.deliver = &DeliverLyingSourceLenZero; // the config's opaq stays the queue

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        // The datagram is dropped like one with a bogus source family:
        // the channel reports nothing received and the packet length is
        // reset.
        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_AGAIN);
        EXPECT_EQ(packet.getLength(), (size_t)-1) << "a datagram reported without a source address was accepted";
    }

    // The source length past the given capacity: same drop.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr, sizeof src_addr),
                  0);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.deliver = &DeliverLyingSourceLenHuge; // the config's opaq stays the queue

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_AGAIN);
        EXPECT_EQ(packet.getLength(), (size_t)-1) << "a source length past the given capacity was accepted";
    }

    // The source length positive but partial - 4 bytes cannot hold a
    // sockaddr_in: same drop.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);
        ASSERT_EQ(queue.push(datagram.data(), datagram.size(), (const struct sockaddr*)&src_addr, sizeof src_addr),
                  0);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.deliver = &DeliverLyingSourceLenPartial; // the config's opaq stays the queue

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        srt::CPacket packet;
        packet.allocate(1500);
        srt::sockaddr_any from(AF_INET);

        EXPECT_EQ(channel.recvfrom(from, packet), srt::RST_AGAIN);
        EXPECT_EQ(packet.getLength(), (size_t)-1) << "a partial source address length was accepted";
    }
}

// The return contract of CChannelVirtual::sendto, unit-tested like the
// deliver tests above: the datagram is handed to the send callback as a
// whole, so the callback may report only the exact total of the buffer
// entries or -1 - a partial accept means nothing to SRT and must be
// normalized to -1; the honest total passes through; no retry either
// way (fire-and-forget, like a UDP sendmsg).
TEST_F(TestVirtualTransport, ChannelNormalizesBogusSendCallbackReturn)
{
    const sockaddr_in label_addr = FillV4("10.224.0.2", 5555);
    const sockaddr_in dst_addr   = FillV4("10.224.0.3", 5557);

    // A packet of the shape the queue sends: the fixed 16-byte header
    // block plus a payload block. The header fields mean nothing to
    // sendto() - they are only byte-swapped on the way through - so only
    // the sizes matter: the total handed to the callback is HDR_SIZE plus
    // the payload length.
    const size_t payload_len = 600;
    srt::CPacket packet;
    packet.allocate(1500);
    packet.setLength(payload_len);
    for (size_t i = 0; i < payload_len; ++i)
        packet.getData()[i] = (char)(i & 0xff);

    const srt::sockaddr_any     dst((const struct sockaddr*)&dst_addr, sizeof dst_addr);
    const srt::CNetworkInterface no_source;

    // The honest send: the queue's own callback gathers the entries and
    // hands them to NullSend, which returns the gathered size - the exact
    // total. It must pass through unchanged.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);

        const srt::CChannelVirtual channel(
            queue.getConfig(), srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        EXPECT_EQ(channel.sendto(dst, packet, no_source), (int)(srt::CPacket::HDR_SIZE + payload_len));
    }

    // One less than the total, then one more: both are contract
    // violations and must be normalized to -1, never passed through as a
    // byte count.
    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.send = &SendLyingShort;

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        EXPECT_EQ(channel.sendto(dst, packet, no_source), -1) << "a return of total-1 passed through as a byte count";
    }

    {
        srt::SRTTransportQueue queue(&NullSend, nullptr);

        SRT_VirtualTransportConfig cfg = queue.getConfig();
        cfg.send = &SendLyingLong;

        const srt::CChannelVirtual channel(
            cfg, srt::sockaddr_any((const struct sockaddr*)&label_addr, sizeof label_addr), false);

        EXPECT_EQ(channel.sendto(dst, packet, no_source), -1) << "a return of total+1 passed through as a byte count";
    }
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The contract of SRT_VTWAIT_BROKEN: once waitread() reports the medium
// dead (detach() on every queue), every socket of the multiplexer must
// break PROMPTLY, not sit out the peer-idle timeout on a medium that
// will never carry a datagram again. All four sockets must reach
// SRTS_BROKEN in the window: established pair, mid-handshake caller
// (wire to nowhere), and the listener (a dead medium cannot accept).
TEST_F(TestVirtualTransport, MediumBrokenBreaksConnection)
{
    const sockaddr_in listener_addr = FillV4("10.225.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.225.0.1", 5556);
    const sockaddr_in pending_addr  = FillV4("10.225.0.3", 5557);

    PairWires(caller_addr, listener_addr);

    // The idle and connect tolerances are set far ABOVE the assertion
    // window, so the prompt breakage cannot be an artifact of either
    // timeout firing: whatever breaks the sockets inside 2 s is the
    // broken-medium handling itself.
    const int peer_idle_timeout_ms = 10000;
    const int conn_timeout_ms      = 10000;

    // Listener side: conclusion-first both ways (the internal bind), as a
    // WebTransport gateway would configure its sessions; the accepted
    // socket inherits the listener's option.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_listener_sock, 0, SRTO_PEERIDLETIMEO, &peer_idle_timeout_ms, sizeof peer_idle_timeout_ms),
              SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig());
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_PEERIDLETIMEO, &peer_idle_timeout_ms, sizeof peer_idle_timeout_ms),
              SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(m_caller_sock, caller_addr, m_caller_queue.getConfig());

    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    m_accepted_sock        = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
    ASSERT_NE(m_accepted_sock, SRT_INVALID_SOCK) << srt_getlasterror_str();

    ASSERT_TRUE(WaitState(m_caller_sock, SRTS_CONNECTED, 5000)) << "the caller never completed the connection";
    EXPECT_EQ(srt_getsockstate(m_accepted_sock), SRTS_CONNECTED);

    // One payload message across the seam, so the session provably flows
    // before the medium dies (and the caller's last-response time is
    // fresh at the detach). Fail fast should it not.
    const int rcv_timeout_ms = 5000;
    ASSERT_NE(srt_setsockopt(m_accepted_sock, 0, SRTO_RCVTIMEO, &rcv_timeout_ms, sizeof rcv_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();
    const char* payload     = "vt-medium session alive";
    const int   payload_len = (int)strlen(payload);
    char        buf[1456];
    ASSERT_EQ(srt_sendmsg(m_caller_sock, payload, payload_len, SRT_MSGTTL_INF, false), payload_len)
        << srt_getlasterror_str();
    ASSERT_EQ(srt_recvmsg(m_accepted_sock, buf, (int)sizeof buf), payload_len) << srt_getlasterror_str();

    // A second caller that is still CONNECTING when the medium dies: its
    // wire leads nowhere (Wire::peer stays null), so every handshake
    // request is lost on it and the socket sits in SRTS_CONNECTING until
    // the break.
    struct ConnectingPeer
    {
        SRTSOCKET               sock = SRT_INVALID_SOCK;
        Wire                    wire;
        srt::SRTTransportQueue queue{ &WireSend, &wire };
        SocketRig closer{ &sock };
    } pending;

    pending.sock = srt_create_socket();
    ASSERT_NE(pending.sock, SRT_INVALID_SOCK);
    FillWireSource(pending.wire, pending_addr); // the peer stays null: a wire to nowhere

    const int no_blocking = 0; // SRTO_RCVSYN off: srt_connect() returns at once
    ASSERT_NE(srt_setsockopt(pending.sock, 0, SRTO_RCVSYN, &no_blocking, sizeof no_blocking), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(pending.sock, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_NE(srt_setsockopt(pending.sock, 0, SRTO_PEERIDLETIMEO, &peer_idle_timeout_ms, sizeof peer_idle_timeout_ms),
              SRT_ERROR)
        << srt_getlasterror_str();
    BindVirtualConclusionFirst(pending.sock, pending_addr, pending.queue.getConfig());
    ASSERT_NE(srt_connect(pending.sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();

    // Let the (lost) handshake request fly, then pin that the socket is
    // really mid-handshake at the moment the medium dies.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_EQ(srt_getsockstate(pending.sock), SRTS_CONNECTING) << "the second caller is not mid-handshake";

    // The medium dies: from now on every queue reports
    // SRT_VTWAIT_BROKEN to waitread() and fails every send - the whole
    // session carrying the sockets is gone, both directions.
    m_caller_queue.detach();
    m_listener_queue.detach();
    pending.queue.detach();

    // Generous-but-tight: the honest break lands within one waitread
    // granularity (~10 ms); only a QEMU-slowed CI runner needs the rest
    // of the window, while the 10 s idle/connect tolerances sit far
    // beyond it, so no timeout can masquerade as the prompt break.
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);

    EXPECT_TRUE(WaitStateUntil(m_caller_sock, SRTS_BROKEN, deadline))
        << "the medium broke mid-connection, but the caller never reached SRTS_BROKEN within the window (state "
        << SockStatusName(srt_getsockstate(m_caller_sock)) << ")";
    EXPECT_TRUE(WaitStateUntil(m_accepted_sock, SRTS_BROKEN, deadline))
        << "the medium broke mid-connection, but the accepted socket never reached SRTS_BROKEN within the window "
        << "(state " << SockStatusName(srt_getsockstate(m_accepted_sock)) << ")";
    EXPECT_TRUE(WaitStateUntil(pending.sock, SRTS_BROKEN, deadline))
        << "the medium broke mid-handshake, but the connecting socket never reached SRTS_BROKEN within the window "
        << "(state " << SockStatusName(srt_getsockstate(pending.sock)) << ")";
    EXPECT_TRUE(WaitStateUntil(m_listener_sock, SRTS_BROKEN, deadline))
        << "the medium broke, but the listener never reached SRTS_BROKEN within the window (state "
        << SockStatusName(srt_getsockstate(m_listener_sock)) << ")";

    // After BROKEN was reported, no callback of any configuration runs
    // again - the freeze is what frees the owner to release the medium.
    // A short grace period lets a callback already in flight when the
    // states settled complete, so the freeze window measures the steady
    // state, not the tail of the race.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const long long caller_sends   = m_caller_wire.send_count.load();
    const long long listener_sends = m_listener_wire.send_count.load();
    const long long pending_sends  = pending.wire.send_count.load();
    bool           frozen          = true;
    for (int waited = 0; waited < 500 && frozen; waited += 10)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        frozen = m_caller_wire.send_count.load() == caller_sends && m_listener_wire.send_count.load() == listener_sends
                 && pending.wire.send_count.load() == pending_sends;
    }
    EXPECT_TRUE(frozen) << "a transport callback fired again after the medium reported broken";

    // Clean close - closing the already-broken
    // sockets (including the connecting one and the listener) is part of
    // the coverage.
    EXPECT_EQ(srt_close(m_accepted_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_accepted_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(pending.sock), SRT_STATUS_OK) << srt_getlasterror_str();
    pending.sock = SRT_INVALID_SOCK;
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

// The medium-break contract seen from a thread parked in a BLOCKING
// srt_connect(): the break walk's CONNECTING branch ends the call - the
// sync-connect loop of CUDT::startConnect re-examines the broken-
// connection fields at its one-second poll of m_SendBlockCond, failing
// within one poll granularity with SRT_ENOSERVER, like an expired
// SRTO_CONNTIMEO (the timeout far above the window; wire unterminated).
TEST_F(TestVirtualTransport, MediumBrokenBreaksBlockingConnect)
{
    // Unique to this test, like the labels of the other tests are to
    // theirs. The target needs no socket behind it: the caller's wire
    // is unterminated, so every handshake request is dropped on it.
    const sockaddr_in target_addr = FillV4("10.229.0.2", 5555);
    const sockaddr_in caller_addr = FillV4("10.229.0.1", 5556);

    // No PairWires here: Wire::peer stays null - a wire to nowhere.
    FillWireSource(m_caller_wire, caller_addr);

    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);

    // Far above the 2 s assertion window below, so only the break can
    // end the connect inside it (and should the break handling regress,
    // the thread still self-terminates at this timeout, keeping the
    // failed run bounded instead of hung).
    const int conn_timeout_ms = 5000;
    ASSERT_NE(srt_setsockopt(m_caller_sock, 0, SRTO_CONNTIMEO, &conn_timeout_ms, sizeof conn_timeout_ms), SRT_ERROR)
        << srt_getlasterror_str();

    {
        const SRT_VirtualTransportConfig cfg = m_caller_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&caller_addr, (int)sizeof caller_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }

    // The connector records the outcome of its blocking srt_connect()
    // (default blocking mode); the error code is captured on the
    // connector's own thread, where srt_getlasterror reports it (the
    // last error is per-thread).
    std::atomic<bool>      finished(false);
    std::atomic<SRTSOCKET> connect_result(SRT_INVALID_SOCK);
    std::atomic<int>       connect_errno(0);
    std::thread            connecter([&]() {
        connect_result = srt_connect(m_caller_sock, (const sockaddr*)&target_addr, (int)sizeof target_addr);
        connect_errno  = srt_getlasterror(nullptr);
        finished       = true;
    });

    // Bounded wait for the connect to be in flight: the wire's
    // send_count rises with the first handshake request, which is the
    // socket provably sitting in SRTS_CONNECTING.
    bool in_flight = false;
    for (int waited = 0; waited < 2000; waited += 10)
    {
        if (m_caller_wire.send_count.load() > 0)
        {
            in_flight = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Re-check after the final sleep: the flight may land in it.
    in_flight = in_flight || m_caller_wire.send_count.load() > 0;

    if (!in_flight)
    {
        // The call still self-terminates at its timeout, so the join is
        // bounded; the test then fails on the missing flight.
        connecter.join();
        FAIL() << "the caller never sent its first handshake request";
    }

    // The medium dies: the caller's own queue reports SRT_VTWAIT_BROKEN
    // to waitread() and fails every send.
    m_caller_queue.detach();

    // The parked connector must fail within the window - well inside
    // the 5 s connect timeout, so only the break can have ended it.
    // The bounded poll keeps a regression a FAILED test, not a hung
    // one.
    bool done = false;
    for (int waited = 0; waited < 2000; waited += 10)
    {
        if (finished.load())
        {
            done = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Re-check after the final sleep: the failure may land in it.
    done = done || finished.load();

    if (!done)
    {
        // Unlike the parked accept of MediumBrokenWakesBlockingAccept,
        // the parked connect is recoverable: it ends by itself at the
        // connect timeout, so joining it keeps the failed run clean.
        connecter.join();
        FAIL() << "a blocking srt_connect() was not failed by the broken medium within 2 s";
    }
    connecter.join();

    EXPECT_EQ(connect_result.load(), SRT_INVALID_SOCK);
    EXPECT_EQ(connect_errno.load(), SRT_ENOSERVER)
        << "a blocking srt_connect() failed by the broken medium must report the connect-timeout error, like one "
           "whose SRTO_CONNTIMEO expired";

    // The walk stored SRTS_BROKEN, but the failure completion of a
    // blocking connect (the exception interceptor of
    // CUDTUnited::connectIn) resets the stored status on its way out, so
    // the socket left behind reports SRTS_OPENED; the terminal
    // SRTS_BROKEN is observable only on the async connecting socket of
    // MediumBrokenBreaksConnection above.
    EXPECT_EQ(srt_getsockstate(m_caller_sock), SRTS_OPENED);

    // Clean close of the socket the failed connect left behind.
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
}

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The medium-break contract seen from a thread parked in a blocking
// srt_accept(): the accepter waits on m_AcceptCond, notified only by a
// queued connection or srt_close(), so the break walk must broadcast it
// and drive the listener core to SSS_BROKEN, the accept loop's exit
// condition (a mere wake would spin forever), failing as "listener closed".
TEST_F(TestVirtualTransport, MediumBrokenWakesBlockingAccept)
{
    const sockaddr_in listener_addr = FillV4("10.228.0.2", 5555);

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig());
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    // The accepter records the outcome of its blocking srt_accept();
    // 'entered' is set right before the call so the main thread can
    // wait for it, 'finished' for the bounded outcome poll below.
    std::atomic<bool>      entered(false);
    std::atomic<bool>      finished(false);
    std::atomic<SRTSOCKET> accepted(SRT_INVALID_SOCK);
    std::atomic<int>       accept_errno(0);
    std::thread            accepter([&]() {
        sockaddr_in accept_addr;
        int         accept_len = (int)sizeof accept_addr;
        entered      = true;
        accepted     = srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len);
        accept_errno = srt_getlasterror(nullptr);
        finished     = true;
    });

    // Bounded wait for the entry, then a settle sleep so the accepter
    // is parked in the accept condition, not merely past the entry
    // checks.
    bool entered_ok = false;
    for (int waited = 0; waited < 2000; waited += 10)
    {
        if (entered.load())
        {
            entered_ok = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Re-check after the final sleep: the entry may land in it.
    entered_ok = entered_ok || entered.load();

    if (!entered_ok)
    {
        // Cannot realistically happen; treat it like the hang below -
        // the thread may be anywhere, so it cannot be joined.
        accepter.detach();
        ADD_FAILURE() << "the accepter thread never entered srt_accept";
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // The medium dies - the listener's queue is the only one here.
    m_listener_queue.detach();

    // The parked accepter must be woken and must fail within the
    // window. The bounded poll keeps a regression a FAILED test, not a
    // hung one; the timeout branch below handles what a missing wake
    // leaves behind.
    bool done = false;
    for (int waited = 0; waited < 2000; waited += 10)
    {
        if (finished.load())
        {
            done = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Re-check after the final sleep: the wake may land in it.
    done = done || finished.load();

    if (!done)
    {
        // With the wake missing, NOTHING can end the parked call: even
        // srt_close() skips its accept broadcast (the break already
        // changed the status it branches on), and the accepter holds
        // the socket's busy count, so cleanup would wait forever.
        // Detach the thread and exit with a failure instead of hanging.
        accepter.detach();
        // ADD_FAILURE, not FAIL: FAIL returns from the test body, and
        // the exit below must still run.
        ADD_FAILURE() << "a blocking srt_accept() was not woken by the broken medium within 2 s";
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
    accepter.join();

    EXPECT_EQ(accepted.load(), SRT_INVALID_SOCK);
    EXPECT_EQ(accept_errno.load(), SRT_ESCLOSED)
        << "a blocking srt_accept() woken by the broken medium must fail like one woken by srt_close()";
    EXPECT_EQ(srt_getsockstate(m_listener_sock), SRTS_BROKEN);

    // Clean close of the already-broken listener.
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

#if !defined(SRT_VT_TESTS_NO_INTERNAL_BIND)
// The medium-break contract seen from an epoll-based accepter: the break
// walk announces the listener with SRT_EPOLL_IN, of which
// SRT_EPOLL_ACCEPT is an alias, so an ACCEPT-subscribed waiter is
// reported ready - epoll waits poll notices at the 10 ms granularity of
// CGlobEvent::waitForEvent(), no explicit trigger needed. The follow-up
// srt_accept() then fails on the broken status like any dead listener.
TEST_F(TestVirtualTransport, MediumBrokenWakesEpollAcceptWaiter)
{
    const sockaddr_in listener_addr = FillV4("10.228.0.3", 5555);

    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    BindVirtualConclusionFirst(m_listener_sock, listener_addr, m_listener_queue.getConfig());
    ASSERT_NE(srt_listen(m_listener_sock, 4), SRT_ERROR) << srt_getlasterror_str();

    const int pollid = srt_epoll_create();
    ASSERT_NE(pollid, SRT_ERROR) << srt_getlasterror_str();
    const int events = SRT_EPOLL_ACCEPT;
    ASSERT_NE(srt_epoll_add_usock(pollid, m_listener_sock, &events), SRT_ERROR) << srt_getlasterror_str();

    // The medium dies - the listener's queue is the only one here.
    m_listener_queue.detach();

    // Generous-but-tight: the notice lands
    // within one 10 ms epoll poll granularity; only a QEMU-slowed CI
    // runner needs the rest of the window.
    SRT_EPOLL_EVENT ready[1];
    ASSERT_EQ(srt_epoll_uwait(pollid, ready, 1, 2000), 1)
        << "an ACCEPT-subscribed epoll waiter was not readied by the broken medium";

    EXPECT_EQ(ready[0].fd, m_listener_sock);
    EXPECT_NE(ready[0].events & SRT_EPOLL_ACCEPT, 0) << "the broken listener was reported without the ACCEPT event";

    // The follow-up accept of an epoll-based accepter fails on the
    // broken status, like any accept on a dead listener.
    sockaddr_in accept_addr;
    int         accept_len = (int)sizeof accept_addr;
    EXPECT_EQ(srt_accept(m_listener_sock, (sockaddr*)&accept_addr, &accept_len), SRT_INVALID_SOCK);
    ExpectLastErrno(SRT_ENOLISTEN);

    srt_epoll_release(pollid);
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;
}
#endif

// The callback-freeze contract of srt_bind_virtual, "leaky" form: a
// listener closed while it holds a connection QUEUED, never accepted -
// the queued socket pins the multiplexer until the GC reaps it, so a
// teardown disposing only an EMPTY multiplexer would leave the worker
// polling waitread (~10 ms) after the close. The contract (srt.h): no
// NEW callback invocation begins after the final close returns.
//
// The listener side counts its waitread invocations (the counting
// pass-through above); the caller side is wired like
// BindVirtualConnectListenSendReceive over the plain public bind. A
// positive control samples the counter over a live window first: a
// healthy receive worker polls continuously, so the counter must move -
// without that control the freeze assertion would be vacuously green.
TEST_F(TestVirtualTransport, NoWaitreadAfterLeakyListenerClose)
{
    // Unique to this test, like the labels of the other tests are to
    // theirs.
    const sockaddr_in listener_addr = FillV4("10.230.0.2", 5555);
    const sockaddr_in caller_addr   = FillV4("10.230.0.1", 5556);

    PairWires(caller_addr, listener_addr);

    // The listener's transport config, built by hand: every callback
    // delegates to the fixture's listener queue, the waitread trampoline
    // counting first. The queue stays wired to m_listener_wire by
    // PairWires above - the counter sits on top of a completely
    // ordinary queue.
    CountingQueue listener_counting;
    listener_counting.queue      = &m_listener_queue;
    SRT_VirtualTransportConfig listener_cfg;
    listener_cfg.send           = &CountingQueueSend;
    listener_cfg.waitread       = &CountingQueueWaitRead;
    listener_cfg.deliver        = &CountingQueueDeliver;
    listener_cfg.opaq           = &listener_counting;
    listener_cfg.initial_rtt_ms = 0;

    // The rig closes both sockets on ANY exit path - including ASSERT
    // failures - while listener_counting is still alive (declared
    // before it, destroyed after): a close deferred to the fixture
    // teardown would fire the counting trampolines into an already
    // destroyed local.
    SocketRig closer{ &m_listener_sock, &m_caller_sock };

    // Listener side: the public virtual bind with the counting config,
    // then listen with a backlog of one - the single connection this
    // test leaves in the accept queue.
    m_listener_sock = srt_create_socket();
    ASSERT_NE(m_listener_sock, SRT_INVALID_SOCK);
    ASSERT_EQ(srt_bind_virtual(m_listener_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr,
                               &listener_cfg),
              SRT_STATUS_OK)
        << srt_getlasterror_str();
    ASSERT_NE(srt_listen(m_listener_sock, 1), SRT_ERROR) << srt_getlasterror_str();

    // Caller side: a plain queue config, like every other test.
    m_caller_sock = srt_create_socket();
    ASSERT_NE(m_caller_sock, SRT_INVALID_SOCK);
    {
        const SRT_VirtualTransportConfig cfg = m_caller_queue.getConfig();
        ASSERT_EQ(srt_bind_virtual(m_caller_sock, (const sockaddr*)&caller_addr, (int)sizeof caller_addr, &cfg),
                  SRT_STATUS_OK)
            << srt_getlasterror_str();
    }

    // The stock handshake over the seam completes the connection on
    // both sides: the caller is done connecting, and the listener holds
    // the connection QUEUED - where it is left, never accepted, which
    // is the point of the scenario.
    ASSERT_NE(srt_connect(m_caller_sock, (const sockaddr*)&listener_addr, (int)sizeof listener_addr), SRT_ERROR)
        << srt_getlasterror_str();
    ASSERT_TRUE(WaitReadable(m_listener_sock, 15000)) << "no connection was ever queued on the listener";

    // POSITIVE CONTROL: with the connection queued, the receive worker
    // polls at one waitread per ~10 ms budget. Sampling the counter over
    // 200 ms and requiring >= 5 invocations proves the counting config
    // observes real invocations - without it the freeze assertion below
    // could pass vacuously. The bound of 5 sits far below the ~15-20 a
    // healthy runner racks up in that window.
    const long long control_before = listener_counting.waitreads.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const long long control_after = listener_counting.waitreads.load();
    ASSERT_GE(control_after - control_before, 5)
        << "the waitread counter saw " << (control_after - control_before)
        << " invocations in 200 ms with a queued connection - the counting config is not observing the receive worker";

    // THE SCENARIO: close the listener while the connection is still
    // queued. The close breaks the queued socket and - the contract
    // under test - must also stop the multiplexer's workers: the
    // pinned dead socket keeps the multiplexer itself alive until the
    // GC reaps it, but nothing may invoke the owner's callbacks
    // anymore once the close has returned.
    EXPECT_EQ(srt_close(m_listener_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_listener_sock = SRT_INVALID_SOCK;

    // A grace window for the ONE invocation that may still have been
    // in flight when srt_close() returned: it is bounded by its 10 ms
    // wait budget, so 100 ms is an order of magnitude of headroom.
    // After it, every invocation that follows is a new one - started
    // after the close returned, in violation of the contract.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // THE PIN: the counter must be frozen. Over the 500 ms window a
    // worker still polling at the 10 ms granularity would rack up ~50
    // new invocations; a close that stopped the workers lands at exactly
    // zero (the GC that later reaps the pinned socket cannot restart
    // them: the stopped channel runs no callback again).
    const long long closed_at  = listener_counting.waitreads.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const long long after_window = listener_counting.waitreads.load();
    EXPECT_EQ(after_window, closed_at)
        << "waitread was invoked " << (after_window - closed_at)
        << " more times after srt_close(listener) returned (counter " << closed_at << " -> " << after_window
        << " over 500 ms) - the multiplexer's receive worker is still running after the leaky close";

    // The caller socket closes cleanly afterwards: its own dedicated
    // multiplexer is untouched by the listener's teardown, and its
    // shutdown flight lands in the listener's queue, where it simply
    // waits unread - no worker is needed to accept a push.
    EXPECT_EQ(srt_close(m_caller_sock), SRT_STATUS_OK) << srt_getlasterror_str();
    m_caller_sock = SRT_INVALID_SOCK;
}
