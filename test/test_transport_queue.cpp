/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

#include "srttransport.h"

using namespace std;
using srt::SRTTransportQueue;

namespace {

// The SRT packet header size (srtcore's CPacket::HDR_SIZE), restated here
// because the public headers do not expose it.
const size_t HDR_SIZE = 16;

// A test source address: 192.168.10.<last> : 0x10<last>.
sockaddr_in TestSource(int last)
{
    sockaddr_in src;
    memset(&src, 0, sizeof src);
    src.sin_family      = AF_INET;
    src.sin_port        = htons(0x1000 + last);
    src.sin_addr.s_addr = htonl(0xC0A80A00 + last); // 192.168.10.last
    return src;
}

// Check that a delivered source address equals TestSource(last).
void ExpectSource(const sockaddr_in& got, int last)
{
    EXPECT_EQ(got.sin_family, AF_INET);
    EXPECT_EQ(got.sin_port, htons(0x1000 + last));
    EXPECT_EQ(got.sin_addr.s_addr, htonl(0xC0A80A00 + last));
}

// A whole SRT-sized datagram for index i: a distinguishable header block
// plus a payload block.
string TestDatagram(int i, size_t payload_size)
{
    string dg;
    dg.resize(HDR_SIZE + payload_size);
    dg[0] = 'H';
    dg[1] = 'D';
    dg[2] = 'R';
    dg[3] = (char)i; // the index, to verify the FIFO order
    for (size_t j = 0; j < payload_size; ++j)
        dg[HDR_SIZE + j] = (char)('a' + ((i + j) % 26));
    return dg;
}

// Deliver one datagram through the queue into header/payload buffers of
// the given payload capacity, the way the SRT receive worker does.
// Returns the deliver() result and fills the written length.
int DeliverOne(SRTTransportQueue& q, char* hdr, size_t hdr_cap, char* payload, size_t payload_cap, sockaddr_in& w_src,
               size_t& w_total)
{
    sockaddr_in src;
    memset(&src, 0, sizeof src);
    int srclen = (int)sizeof src;

    SRT_VTBuf vec[2];
    vec[0].data = hdr;
    vec[0].len  = (int)hdr_cap;
    vec[1].data = payload;
    vec[1].len  = (int)payload_cap;

    const int res = q.deliver(reinterpret_cast<sockaddr*>(&src), &srclen, vec, 2);
    w_src         = src;
    w_total       = res > 0 ? (size_t)res : 0;
    return res;
}

// The plain contiguous send callback the gather adapter wraps: captures
// the datagram and the destination.
struct SendCapture
{
    mutex mx;
    condition_variable cv;
    vector<string> datagrams;
    vector<sockaddr_in> dsts;
    int retval = 3; // what the callback reports as accepted

    void add(const char* data, int len, const sockaddr* dst)
    {
        lock_guard<mutex> lk(mx);
        datagrams.push_back(string(data, data + len));
        sockaddr_in d;
        memset(&d, 0, sizeof d);
        if (dst && dst->sa_family == AF_INET)
            memcpy(&d, dst, sizeof d);
        dsts.push_back(d);
        cv.notify_all();
    }

    size_t count()
    {
        lock_guard<mutex> lk(mx);
        return datagrams.size();
    }

    bool waitUntilCount(size_t n, chrono::milliseconds timeout)
    {
        unique_lock<mutex> lk(mx);
        return cv.wait_for(lk, timeout, [&] { return datagrams.size() >= n; });
    }
};

int captureSend(void* opaq, const char* data, int len, const sockaddr* dst, int)
{
    static_cast<SendCapture*>(opaq)->add(data, len, dst);
    return static_cast<SendCapture*>(opaq)->retval;
}

} // namespace

// Pushing datagrams wakes the waiting side and delivers them unchanged,
// in the FIFO order, with their source addresses.
TEST(TransportQueue, PushDeliverOrder)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const string dg0 = TestDatagram(0, 20);
    const string dg1 = TestDatagram(1, 300);
    const string dg2 = TestDatagram(2, 1);
    const sockaddr_in src0 = TestSource(1);
    const sockaddr_in src1 = TestSource(2);
    const sockaddr_in src2 = TestSource(3);

    EXPECT_EQ(q.push(dg0.data(), dg0.size(), (sockaddr*)&src0, sizeof src0), 0);
    EXPECT_EQ(q.push(dg1.data(), dg1.size(), (sockaddr*)&src1, sizeof src1), 0);
    EXPECT_EQ(q.push(dg2.data(), dg2.size(), (sockaddr*)&src2, sizeof src2), 0);

    const string* expected[3] = {&dg0, &dg1, &dg2};
    const int         expected_src[3] = {1, 2, 3};

    for (int i = 0; i < 3; ++i)
    {
        // The queued datagrams are reported ready without waiting; the
        // wake flag is re-raised by deliver() while datagrams remain.
        EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);

        char        hdr[HDR_SIZE];
        char        payload[1500];
        sockaddr_in got_src;
        size_t      total = 0;
        memset(hdr, 0, sizeof hdr);
        memset(payload, 0xEE, sizeof payload);

        ASSERT_GT(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), 0);
        ASSERT_EQ(total, expected[i]->size());
        EXPECT_EQ(0, memcmp(hdr, expected[i]->data(), HDR_SIZE));
        EXPECT_EQ(0, memcmp(payload, expected[i]->data() + HDR_SIZE, total - HDR_SIZE));
        ExpectSource(got_src, expected_src[i]);
    }

    // Nothing left: waiting times out (zero wait) and delivering fails.
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_TIMEOUT);

    char        hdr[HDR_SIZE];
    char        payload[8];
    sockaddr_in got_src;
    size_t      total = 0;
    EXPECT_EQ(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), -1);
}

// A header-only (16-byte) datagram is delivered whole with an empty
// payload: the scatter must handle a zero-length trailing segment
// (pointer one past the end of the data, zero bytes to copy).
TEST(TransportQueue, HeaderOnlyDatagram)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const string dg = TestDatagram(7, 0);
    ASSERT_EQ(dg.size(), HDR_SIZE);

    const sockaddr_in src = TestSource(4);
    ASSERT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0);
    EXPECT_EQ(q.waitread(100), SRT_VTWAIT_READY);

    char        hdr[HDR_SIZE];
    char        payload[1500];
    sockaddr_in got_src;
    size_t      total = 0;
    memset(payload, 0xEE, sizeof payload);

    ASSERT_EQ(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), (int)HDR_SIZE);
    EXPECT_EQ(total, HDR_SIZE);
    EXPECT_EQ(0, memcmp(hdr, dg.data(), HDR_SIZE));
    // The payload buffer is not touched and the source is delivered.
    EXPECT_EQ(payload[0], (char)0xEE);
    ExpectSource(got_src, 4);

    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_TIMEOUT);
}

// A datagram that does not fit the provided buffers is discarded, and the
// following one is delivered intact.
TEST(TransportQueue, OversizeDatagramDiscarded)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const string        dg0 = TestDatagram(0, 100);
    const string        dg1 = TestDatagram(1, 10);
    const sockaddr_in   src = TestSource(5);

    ASSERT_EQ(q.push(dg0.data(), dg0.size(), (sockaddr*)&src, sizeof src), 0);
    ASSERT_EQ(q.push(dg1.data(), dg1.size(), (sockaddr*)&src, sizeof src), 0);

    // A payload buffer for 50 bytes only: the 100-byte payload cannot fit.
    char        hdr[HDR_SIZE];
    char        payload[50];
    sockaddr_in got_src;
    size_t      total = 0;
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    EXPECT_EQ(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), -1);

    // The next datagram is delivered intact.
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    ASSERT_GT(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), 0);
    EXPECT_EQ(total, dg1.size());
    EXPECT_EQ(0, memcmp(hdr, dg1.data(), HDR_SIZE));
    EXPECT_EQ(0, memcmp(payload, dg1.data() + HDR_SIZE, dg1.size() - HDR_SIZE));
}

// A source address that does not fit the provided address buffer whole
// discards its datagram - a truncated address would be memory-safe
// nonsense for every reader - and a following datagram is delivered
// intact, with its source in full.
TEST(TransportQueue, SourceAddressDoesNotFitDiscarded)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const string      dg0 = TestDatagram(0, 10);
    const string      dg1 = TestDatagram(1, 10);
    const string      dg2 = TestDatagram(2, 10);
    const sockaddr_in src = TestSource(7);

    ASSERT_EQ(q.push(dg0.data(), dg0.size(), (sockaddr*)&src, sizeof src), 0);
    ASSERT_EQ(q.push(dg1.data(), dg1.size(), (sockaddr*)&src, sizeof src), 0);
    ASSERT_EQ(q.push(dg2.data(), dg2.size(), (sockaddr*)&src, sizeof src), 0);

    char        hdr[HDR_SIZE];
    char        payload[20];
    sockaddr_in got_src;
    memset(&got_src, 0, sizeof got_src);

    SRT_VTBuf vec[2];
    vec[0].data = hdr;
    vec[0].len  = (int)sizeof hdr;
    vec[1].data = payload;
    vec[1].len  = 10;

    // An address buffer of half an AF_INET address: the datagram fits
    // the data buffers, but its source cannot fit the address buffer.
    // A zero capacity is rejected alike. Both datagrams are discarded,
    // like an oversize one is.
    int srclen = (int)sizeof(sockaddr_in) / 2;
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    EXPECT_EQ(q.deliver((sockaddr*)&got_src, &srclen, vec, 2), -1);
    srclen = 0;
    EXPECT_EQ(q.deliver((sockaddr*)&got_src, &srclen, vec, 2), -1);

    // The next datagram is delivered intact, its source in full.
    srclen = (int)sizeof got_src;
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    EXPECT_EQ(q.deliver((sockaddr*)&got_src, &srclen, vec, 2), (int)dg2.size());
    EXPECT_EQ(srclen, (int)sizeof(sockaddr_in));
    ExpectSource(got_src, 7);
    EXPECT_EQ(0, memcmp(hdr, dg2.data(), HDR_SIZE));
}

// Invalid pushes are rejected: a datagram below the SRT header size, one
// above the datagram bound, a source pointer with a zero length, a
// source address too short to carry the family field read from it, and
// one shorter than the full address its family calls for.
TEST(TransportQueue, PushValidation)
{
    SendCapture cap;
    const size_t max_datagram = 128;
    SRTTransportQueue q(&captureSend, &cap, 8, max_datagram);

    const string tooshort = TestDatagram(0, 0).substr(0, HDR_SIZE - 1);
    const string huge(max_datagram + 1, 'x');
    const sockaddr_in src = TestSource(6);

    EXPECT_EQ(q.push(tooshort.data(), tooshort.size(), (sockaddr*)&src, sizeof src), -1);
    EXPECT_EQ(q.push(huge.data(), huge.size(), (sockaddr*)&src, sizeof src), -1);
    EXPECT_EQ(q.push(TestDatagram(0, 4).data(), HDR_SIZE + 4, (sockaddr*)&src, 0), -1);

    // A datagram without a source address is rejected: SRT dispatches by
    // the source address the deliver callback reports, and the channel
    // discards a datagram reported without one - a source-less push could
    // never be delivered, so it is refused at the door.
    const string ok = TestDatagram(0, 4);
    EXPECT_EQ(q.push(ok.data(), ok.size(), nullptr, 0), -1);

    // A source address shorter than the family field is rejected by the
    // length check alone, before the family is read: the buffer starts
    // with a valid AF_INET family byte, so only the length can refuse it.
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&src, 1), -1);

    // A source address exactly as long as the family field passes the
    // length check, and is then rejected by the family check: the family
    // must still be one SRT can dispatch by.
    sockaddr_in nofam = TestSource(6);
    nofam.sin_family  = AF_UNSPEC;
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&nofam, sizeof(sa_family_t)), -1);

    // A source address shorter than the full address its family calls
    // for is rejected alike: only a zero-padded one could ever be
    // delivered.
    sockaddr_in6 src6;
    memset(&src6, 0, sizeof src6);
    src6.sin6_family           = AF_INET6;
    src6.sin6_port             = htons(0x2000);
    src6.sin6_addr.s6_addr[15] = 17;
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&src, 8), -1);
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&src, sizeof(sockaddr_in) - 1), -1);
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&src6, sizeof(sockaddr_in)), -1);
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&src6, sizeof(sockaddr_in6) - 1), -1);

    // Every rejected push above left the queue empty; one valid push is
    // then deliverable, with its source address reported in full. Its
    // length, sizeof(sockaddr_in), is exactly the AF_INET minimum.
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&src, sizeof src), 0);

    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    sockaddr_in got_src;
    memset(&got_src, 0, sizeof got_src);
    int srclen = sizeof got_src;
    char     hdr[HDR_SIZE];
    char     payload[8];
    SRT_VTBuf vec[2];
    vec[0].data = hdr;
    vec[0].len  = sizeof hdr;
    vec[1].data = payload;
    vec[1].len  = sizeof payload;
    EXPECT_EQ(q.deliver((sockaddr*)&got_src, &srclen, vec, 2), (int)ok.size());
    EXPECT_EQ(srclen, (int)sizeof src);
    EXPECT_EQ(memcmp(&got_src, &src, sizeof src), 0);

    // The AF_INET6 minimum, sizeof(sockaddr_in6) bytes, is accepted and
    // reported whole likewise.
    EXPECT_EQ(q.push(ok.data(), ok.size(), (sockaddr*)&src6, sizeof src6), 0);
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    sockaddr_in6 got_src6;
    memset(&got_src6, 0, sizeof got_src6);
    int srclen6 = sizeof got_src6;
    EXPECT_EQ(q.deliver((sockaddr*)&got_src6, &srclen6, vec, 2), (int)ok.size());
    EXPECT_EQ(srclen6, (int)sizeof src6);
    EXPECT_EQ(memcmp(&got_src6, &src6, sizeof src6), 0);

    // Nothing remains.
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_TIMEOUT);
}

// A source address of a family SRT cannot dispatch by is rejected at
// push time: the channel would silently discard such a datagram at
// delivery, so push() reports the bug at the door instead. An AF_INET6
// source - the other dispatchable family - is not over-rejected.
TEST(TransportQueue, BadSourceFamilyRejected)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const string dg = TestDatagram(0, 4);

    sockaddr_in unspec = TestSource(16);
    unspec.sin_family  = AF_UNSPEC;
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&unspec, sizeof unspec), -1);

    // The rejected datagram left the queue empty.
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_TIMEOUT);

    // An AF_INET6 source is valid, and its 28-byte address fits the
    // source storage; the push lands and the source is reported whole.
    sockaddr_in6 src6;
    memset(&src6, 0, sizeof src6);
    src6.sin6_family           = AF_INET6;
    src6.sin6_port             = htons(0x2000);
    src6.sin6_addr.s6_addr[15] = 17;
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src6, sizeof src6), 0);

    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    sockaddr_in6 got;
    memset(&got, 0, sizeof got);
    int       srclen = sizeof got;
    char      hdr[HDR_SIZE];
    char      payload[8];
    SRT_VTBuf vec[2];
    vec[0].data = hdr;
    vec[0].len  = sizeof hdr;
    vec[1].data = payload;
    vec[1].len  = sizeof payload;
    EXPECT_EQ(q.deliver((sockaddr*)&got, &srclen, vec, 2), (int)dg.size());
    EXPECT_EQ(srclen, (int)sizeof src6);
    EXPECT_EQ(memcmp(&got, &src6, sizeof src6), 0);
}

// While the queue is full (its max_pending bound), pushes fail for as
// long as it stays full - like a full kernel socket buffer - and succeed
// again once the queue is drained. The delivered order is preserved.
TEST(TransportQueue, OverflowStickyThenRecover)
{
    SendCapture cap;
    const size_t capacity = 8;
    SRTTransportQueue q(&captureSend, &cap, capacity);

    const sockaddr_in src = TestSource(7);

    for (size_t i = 0; i < capacity; ++i)
    {
        const string dg = TestDatagram((int)i, 4);
        ASSERT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0) << "i=" << i;
    }

    // Full: a push is dropped, and keeps failing on retries (sticky).
    const string extra = TestDatagram(200, 4);
    EXPECT_EQ(q.push(extra.data(), extra.size(), (sockaddr*)&src, sizeof src), -1);
    EXPECT_EQ(q.push(extra.data(), extra.size(), (sockaddr*)&src, sizeof src), -1);

    // Draining one datagram makes room for exactly one more push.
    char        hdr[HDR_SIZE];
    char        payload[8];
    sockaddr_in got_src;
    size_t      total = 0;
    ASSERT_GT(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), 0);
    ASSERT_EQ(total, HDR_SIZE + 4);
    EXPECT_EQ(hdr[3], (char)0); // the earliest datagram, index 0

    // With a slot free again, pushing works.
    const string recovered = TestDatagram(201, 4);
    EXPECT_EQ(q.push(recovered.data(), recovered.size(), (sockaddr*)&src, sizeof src), 0);

    // The whole queue comes out in the pushed order, recovered one last.
    for (size_t i = 1; i < capacity; ++i)
    {
        ASSERT_EQ(q.waitread(0), SRT_VTWAIT_READY) << "i=" << i;
        ASSERT_EQ(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), (int)(HDR_SIZE + 4))
            << "i=" << i;
        ASSERT_EQ(hdr[3], (char)i) << "i=" << i;
    }
    ASSERT_EQ(q.waitread(0), SRT_VTWAIT_READY);
    ASSERT_EQ(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), (int)(HDR_SIZE + 4));
    EXPECT_EQ(hdr[3], (char)201);

    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_TIMEOUT);
}

// push() counts the datagrams it drops because the queue is full -
// dropped() reports exactly those, not the invalid-argument or detached
// rejections, and never resets - so a benign, UDP-like overflow loss
// stays distinguishable from a bug.
TEST(TransportQueue, OverflowDropsCounted)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap, 2);

    const sockaddr_in src = TestSource(17);
    const string      dg  = TestDatagram(0, 4);

    EXPECT_EQ(q.dropped(), (size_t)0);

    // An invalid-argument rejection is a bug report, not a drop.
    EXPECT_EQ(q.push(dg.data(), dg.size(), nullptr, 0), -1);
    EXPECT_EQ(q.dropped(), (size_t)0);

    // Fill both slots; the third push overflows and is dropped.
    ASSERT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0);
    ASSERT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0);
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), -1);
    EXPECT_EQ(q.dropped(), (size_t)1);

    // Draining one datagram makes room again; the counter stands.
    char        hdr[HDR_SIZE];
    char        payload[8];
    sockaddr_in got_src;
    size_t      total = 0;
    ASSERT_GT(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), 0);
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0);
    EXPECT_EQ(q.dropped(), (size_t)1);

    // A detached rejection is not a drop either.
    q.detach();
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), -1);
    EXPECT_EQ(q.dropped(), (size_t)1);
}

// Bounds below the minimums are clamped up rather than rejected:
// max_pending to 1, max_datagram to the SRT header size.
TEST(TransportQueue, BoundsClampedToMinimums)
{
    const sockaddr_in src = TestSource(11);

    {
        SendCapture cap;
        SRTTransportQueue q(&captureSend, &cap, 0, 64);

        // Room for exactly one datagram; the second is dropped.
        const string dg0 = TestDatagram(0, 4);
        const string dg1 = TestDatagram(1, 4);
        EXPECT_EQ(q.push(dg0.data(), dg0.size(), (sockaddr*)&src, sizeof src), 0);
        EXPECT_EQ(q.push(dg1.data(), dg1.size(), (sockaddr*)&src, sizeof src), -1);
    }

    {
        SendCapture cap;
        SRTTransportQueue q(&captureSend, &cap, 8, 0);

        // A header-only datagram (16 bytes) still fits; anything larger
        // does not.
        const string hdr_only = TestDatagram(0, 0);
        ASSERT_EQ(hdr_only.size(), HDR_SIZE);
        EXPECT_EQ(q.push(hdr_only.data(), hdr_only.size(), (sockaddr*)&src, sizeof src), 0);
        const string longer = TestDatagram(1, 1);
        EXPECT_EQ(q.push(longer.data(), longer.size(), (sockaddr*)&src, sizeof src), -1);
    }
}

// Default bounds mirror SRT's UDP numbers so a default queue
// preallocates about one kernel receive buffer, not more:
// max_datagram 1472 is one full classic SRT datagram, HDR_SIZE plus
// SRT_MAX_PLSIZE_AF_INET; max_pending 44 packs that many datagrams
// into the default 65536-byte UDP socket buffer.
TEST(TransportQueue, DefaultBoundsMirrorUdpBuffer)
{
    const sockaddr_in src = TestSource(19);

    // The size bound: a maximal classic SRT datagram is accepted, and
    // one byte larger is rejected.
    {
        SendCapture cap;
        SRTTransportQueue q(&captureSend, &cap);

        const size_t classic_max = HDR_SIZE + SRT_MAX_PLSIZE_AF_INET; // 1472
        ASSERT_EQ(classic_max, (size_t)1472);

        const string fits = TestDatagram(0, classic_max - HDR_SIZE);
        ASSERT_EQ(fits.size(), classic_max);
        EXPECT_EQ(q.push(fits.data(), fits.size(), (sockaddr*)&src, sizeof src), 0);

        const string toobig = TestDatagram(1, classic_max - HDR_SIZE + 1);
        EXPECT_EQ(q.push(toobig.data(), toobig.size(), (sockaddr*)&src, sizeof src), -1);
    }

    // The depth bound: exactly the 44 datagrams above fit - one UDP
    // receive buffer's worth - and the 45th is the benign overflow
    // drop, counted by dropped().
    {
        SendCapture cap;
        SRTTransportQueue q(&captureSend, &cap);

        const size_t udp_rcvbuf  = 65536; // DEF_UDP_BUFFER_SIZE (srtcore/socketconfig.h)
        const size_t max_pending = udp_rcvbuf / (HDR_SIZE + SRT_MAX_PLSIZE_AF_INET); // 44
        for (size_t i = 0; i < max_pending; ++i)
        {
            const string dg = TestDatagram((int)i, 4);
            ASSERT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0) << "i=" << i;
        }
        const string extra = TestDatagram(200, 4);
        EXPECT_EQ(q.push(extra.data(), extra.size(), (sockaddr*)&src, sizeof src), -1);
        EXPECT_EQ(q.dropped(), (size_t)1);
    }
}

// The gather adapter hands the app's plain send callback one contiguous
// datagram, byte-exact, with the destination address, and passes the
// callback's return value through.
TEST(TransportQueue, GatherAdapter)
{
    SendCapture cap;
    cap.retval = 42;
    SRTTransportQueue q(&captureSend, &cap);

    // The two blocks the SRT send callback passes: header and payload.
    const string hdr     = TestDatagram(0, 0); // 16 bytes
    const string payload = "The quick brown fox jumps over the lazy dog";

    SRT_VTBuf vec[2];
    vec[0].data = const_cast<char*>(hdr.data());
    vec[0].len  = (int)hdr.size();
    vec[1].data = const_cast<char*>(payload.data());
    vec[1].len  = (int)payload.size();

    const sockaddr_in dst = TestSource(8);
    EXPECT_EQ(q.send((sockaddr*)&dst, sizeof dst, vec, 2), 42);

    ASSERT_EQ(cap.count(), (size_t)1);
    EXPECT_EQ(cap.datagrams[0], hdr + payload);
    ExpectSource(cap.dsts[0], 8);

    // A header-only datagram gathers to just the header block.
    vec[1].data = nullptr;
    vec[1].len  = 0;
    const sockaddr_in dst2 = TestSource(9);
    EXPECT_EQ(q.send((sockaddr*)&dst2, sizeof dst2, vec, 2), 42);
    ASSERT_EQ(cap.count(), (size_t)2);
    EXPECT_EQ(cap.datagrams[1], hdr);
    ExpectSource(cap.dsts[1], 9);
}

// A gathered send datagram larger than the queue's max_datagram fails
// with -1 without the application callback ever being called, and a
// fitting datagram is still gathered whole afterwards: the scratch block
// is reserved once, at construction, so no send path allocates (send
// runs on SRT's worker threads, where an escaping exception would
// terminate the process).
TEST(TransportQueue, OversizeSendRejected)
{
    SendCapture cap;
    const size_t max_datagram = 64;
    SRTTransportQueue q(&captureSend, &cap, 8, max_datagram);
    const SRT_VirtualTransportConfig cfg = q.getConfig();

    const string hdr = TestDatagram(0, 0); // 16 bytes
    const sockaddr_in dst = TestSource(15);

    // A two-entry datagram of exactly max_datagram bytes is gathered and
    // passed through with the callback's return value.
    const string payload(max_datagram - HDR_SIZE, 'p');
    SRT_VTBuf fits[2];
    fits[0].data = const_cast<char*>(hdr.data());
    fits[0].len  = (int)hdr.size();
    fits[1].data = const_cast<char*>(payload.data());
    fits[1].len  = (int)payload.size();
    EXPECT_EQ(cfg.send(cfg.opaq, (sockaddr*)&dst, sizeof dst, fits, 2), cap.retval);
    ASSERT_EQ(cap.count(), (size_t)1);
    EXPECT_EQ(cap.datagrams[0], hdr + payload);

    // One byte over the bound: the datagram fails outright, without the
    // application callback being invoked.
    const string big_payload(max_datagram - HDR_SIZE + 1, 'b');
    SRT_VTBuf oversize[2];
    oversize[0].data = const_cast<char*>(hdr.data());
    oversize[0].len  = (int)hdr.size();
    oversize[1].data = const_cast<char*>(big_payload.data());
    oversize[1].len  = (int)big_payload.size();
    EXPECT_EQ(cfg.send(cfg.opaq, (sockaddr*)&dst, sizeof dst, oversize, 2), -1);
    EXPECT_EQ(cap.count(), (size_t)1); // the callback was NOT invoked

    // The queue still gathers a fitting datagram whole afterwards.
    EXPECT_EQ(cfg.send(cfg.opaq, (sockaddr*)&dst, sizeof dst, fits, 2), cap.retval);
    ASSERT_EQ(cap.count(), (size_t)2);
    EXPECT_EQ(cap.datagrams[1], hdr + payload);
}

// After detach(): push fails, waitread reports the medium broken, and the
// send callback is no longer called (quiescence); detach() itself blocks
// until an in-flight send callback returns.
TEST(TransportQueue, DetachBreaksAndQuiesces)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const sockaddr_in dst = TestSource(10);

    // A send callback that blocks until released, to check that detach()
    // waits for the in-flight callback.
    struct Blocking
    {
        mutex              mx;
        condition_variable cv;
        bool arrived = false;
        bool release = false;

        bool waitArrival(chrono::milliseconds timeout)
        {
            unique_lock<mutex> lk(mx);
            return cv.wait_for(lk, timeout, [&] { return arrived; });
        }

        void letGo()
        {
            lock_guard<mutex> lk(mx);
            release = true;
            cv.notify_all();
        }
    } blk;

    auto blockingSend = [](void* opaq, const char*, int, const sockaddr*, int) {
        Blocking* b = static_cast<Blocking*>(opaq);
        {
            lock_guard<mutex> lk(b->mx);
            b->arrived = true;
            b->cv.notify_all();
        }
        unique_lock<mutex> lk(b->mx);
        b->cv.wait(lk, [&] { return b->release; });
        return 0;
    };

    SRTTransportQueue qblock(blockingSend, &blk);
    const string      hdr = TestDatagram(0, 0);

    thread sender([&] {
        SRT_VTBuf vec[1];
        vec[0].data = const_cast<char*>(hdr.data());
        vec[0].len  = (int)hdr.size();
        qblock.send((sockaddr*)&dst, sizeof dst, vec, 1);
    });
    // Bounded wait for the callback entry signal, which it raises right
    // before parking on the release condition.
    ASSERT_TRUE(blk.waitArrival(chrono::seconds(5))) << "the send callback never ran";

    atomic<bool> detached(false);
    thread detacher([&] {
        qblock.detach();
        detached = true;
    });

    // The callback is parked, so detach() must be inside it, waiting for
    // its return; a short bounded check suffices - a detach that ignored
    // the in-flight callback would return within milliseconds.
    this_thread::sleep_for(chrono::milliseconds(50));
    EXPECT_FALSE(detached.load());

    blk.letGo();
    detacher.join();
    sender.join();
    EXPECT_TRUE(detached.load());

    // After detach: pushes fail, waitread reports broken, sends are
    // dropped without calling the callback.
    const string        dg  = TestDatagram(0, 4);
    const sockaddr_in   src = TestSource(11);
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0); // pushed before q's detach
    q.detach();
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), -1);
    EXPECT_EQ(q.waitread(100), SRT_VTWAIT_BROKEN);

    SRT_VTBuf vec[1];
    vec[0].data = const_cast<char*>(hdr.data());
    vec[0].len  = (int)hdr.size();
    const size_t sent_before = cap.count();
    EXPECT_EQ(q.send((sockaddr*)&dst, sizeof dst, vec, 1), -1);
    EXPECT_EQ(cap.count(), sent_before); // the callback was NOT invoked
}

// A push wakes a waitread() that is already blocked in waiting.
TEST(TransportQueue, PushWakesWaitingReader)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const sockaddr_in src = TestSource(12);

    thread reader([&] { EXPECT_EQ(q.waitread(2000), SRT_VTWAIT_READY); });

    // A pre-block sleep, a race-tolerant convenience only: the test
    // passes whichever way the interleaving lands - push first and
    // waitread reports READY at once, push after and the wake must
    // reach the blocked reader.
    this_thread::sleep_for(chrono::milliseconds(100));
    const string dg = TestDatagram(0, 4);
    EXPECT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0);
    reader.join();

    // The datagram is deliverable.
    char        hdr[HDR_SIZE];
    char        payload[8];
    sockaddr_in got_src;
    size_t      total = 0;
    ASSERT_EQ(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), (int)dg.size());
}

// A detach while waitread() is waiting breaks it at once, without
// spending the timeout budget - and whichever way the race lands: the
// reader parked before the detach, or the detach fully done before the
// reader reached the wake mutex (the post-ticket check breaks that).
TEST(TransportQueue, DetachWakesWaitingWaitread)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const auto start = chrono::steady_clock::now();

    atomic<SRT_VTWAIT> result(SRT_VTWAIT_TIMEOUT);
    thread reader([&] { result = q.waitread(30000); });

    // A pre-block sleep, race-tolerant only: the test passes whichever
    // way the interleaving lands, like PushWakesWaitingReader above.
    this_thread::sleep_for(chrono::milliseconds(50));
    q.detach();
    reader.join();

    EXPECT_EQ(result.load(), SRT_VTWAIT_BROKEN);

    // The whole exchange must stay far below the 30 s budget: a
    // regression to waiting the timeout out fails here, not by hanging
    // the suite.
    const auto elapsed_ms =
        chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
    EXPECT_LT(elapsed_ms, 5000);
}

// With nothing pushed, waitread() reports a timeout after (at least) the
// requested time.
TEST(TransportQueue, WaitTimeout)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const auto start = chrono::steady_clock::now();
    EXPECT_EQ(q.waitread(120), SRT_VTWAIT_TIMEOUT);
    const auto elapsed = chrono::steady_clock::now() - start;
    // The >=100 ms assertion for the 120 ms request is deliberate slop
    // for scheduler granularity.
    EXPECT_GE(chrono::duration_cast<chrono::milliseconds>(elapsed).count(), 100);

    // A second wait after a timeout behaves the same.
    EXPECT_EQ(q.waitread(0), SRT_VTWAIT_TIMEOUT);
}

// Two concurrent waitread() callers race for the single wake ticket: one
// consumes it and returns READY; the other is woken by the very same
// notification but finds the ticket gone, and must re-wait the remaining
// budget instead of returning an immediate timeout. That is the path
// wait_for also takes on a spurious wake, so this pins that waitread
// honors the requested time whenever the flag stays unset.
TEST(TransportQueue, WaitRewaitsAfterLostTicket)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const sockaddr_in src = TestSource(18);
    const string      dg  = TestDatagram(0, 4);

    // Both readers wait on the same 300 ms budget.
    const int         budget_ms = 300;
    atomic<int>       entered(0);
    vector<int>       results(2, -1);
    vector<long long> elapsed_ms(2, 0);

    thread readers[2];
    for (int i = 0; i < 2; ++i)
    {
        readers[i] = thread([&, i] {
            ++entered;
            const auto start = chrono::steady_clock::now();
            results[i]    = (int)q.waitread(budget_ms);
            elapsed_ms[i] = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
        });
    }

    // Let both get inside the wait, then hand out exactly one ticket:
    // a single push, whose notification reaches both waiters.
    while (entered.load() < 2)
        this_thread::sleep_for(chrono::milliseconds(1));
    this_thread::sleep_for(chrono::milliseconds(50));
    ASSERT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0);

    readers[0].join();
    readers[1].join();

    // Exactly one reader won the ticket; the other timed out only after
    // (most of) the full budget - an early return here is the wake that
    // was not re-waited.
    const int winner = (results[0] == SRT_VTWAIT_READY) ? 0 : 1;
    const int loser  = 1 - winner;
    EXPECT_EQ(results[winner], SRT_VTWAIT_READY);
    EXPECT_EQ(results[loser], SRT_VTWAIT_TIMEOUT);
    EXPECT_GE(elapsed_ms[loser], budget_ms - 100);

    // The pushed datagram is still deliverable.
    char        hdr[HDR_SIZE];
    char        payload[8];
    sockaddr_in got_src;
    size_t      total = 0;
    ASSERT_EQ(DeliverOne(q, hdr, sizeof hdr, payload, sizeof payload, got_src, total), (int)dg.size());
}

// getConfig() wires all three callbacks of the seam to the queue object,
// carries the settings, and the callbacks work through the config.
TEST(TransportQueue, GetConfigWiring)
{
    SendCapture cap;
    SRTTransportQueue q(&captureSend, &cap);

    const SRT_VirtualTransportConfig cfg = q.getConfig(2500);
    EXPECT_EQ(cfg.initial_rtt_ms, 2500);
    EXPECT_EQ(cfg.opaq, &q);
    ASSERT_NE(cfg.send, nullptr);
    ASSERT_NE(cfg.waitread, nullptr);
    ASSERT_NE(cfg.deliver, nullptr);

    // The seam callbacks drive the queue through the config, exactly what
    // srt_bind_virtual will do with it.
    const sockaddr_in src = TestSource(13);
    const string      dg  = TestDatagram(0, 4);
    ASSERT_EQ(q.push(dg.data(), dg.size(), (sockaddr*)&src, sizeof src), 0);

    EXPECT_EQ(cfg.waitread(cfg.opaq, 10), SRT_VTWAIT_READY);

    sockaddr_in got_src;
    memset(&got_src, 0, sizeof got_src);
    int srclen = sizeof got_src;

    char     hdr[HDR_SIZE];
    char     payload[8];
    SRT_VTBuf dvec[2];
    dvec[0].data = hdr;
    dvec[0].len  = sizeof hdr;
    dvec[1].data = payload;
    dvec[1].len  = sizeof payload;
    EXPECT_EQ(cfg.deliver(cfg.opaq, (sockaddr*)&got_src, &srclen, dvec, 2), (int)dg.size());
    ExpectSource(got_src, 13);

    // And the send side gathers into the app callback.
    SRT_VTBuf svec[2];
    svec[0].data = const_cast<char*>(dg.data());
    svec[0].len  = (int)HDR_SIZE;
    svec[1].data = const_cast<char*>(dg.data() + HDR_SIZE);
    svec[1].len  = (int)(dg.size() - HDR_SIZE);
    const sockaddr_in dst = TestSource(14);
    EXPECT_EQ(cfg.send(cfg.opaq, (sockaddr*)&dst, sizeof dst, svec, 2), 3); // cap.retval
    ASSERT_EQ(cap.count(), (size_t)1);
    EXPECT_EQ(cap.datagrams[0], dg);
}
