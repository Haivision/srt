/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "srttransport.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace srt
{

// The SRT packet header size, in bytes. Not exposed in the public
// headers, so restated here (srtcore's CPacket::HDR_SIZE): a pushed
// datagram must carry at least this much to be a whole SRT packet.
static const size_t SRT_PACKET_HDR_SIZE = 16;

SRTTransportQueue::SRTTransportQueue(SRTTransportSendFunc* send_fn, void* opaq,
                                     size_t max_pending, size_t max_datagram)
    : m_SendFn(send_fn)
    , m_Opaq(opaq)
    // Nonsense bounds are clamped, not rejected: a queue that cannot
    // hold even one datagram or accept one whole SRT packet cannot work.
    , m_MaxPending(max_pending < 1 ? 1 : max_pending)
    , m_MaxDatagram(max_datagram < SRT_PACKET_HDR_SIZE ? SRT_PACKET_HDR_SIZE : max_datagram)
    , m_bSendActive(true)
    , m_Head(0)
    , m_Count(0)
    , m_bDetached(false)
    , m_Dropped(0)
    , m_bWoken(false)
{
    // Size the send gather area once, here, in full, and never grow it:
    // send() runs on SRT's worker threads across a C function-pointer
    // seam with no exception handler, so its path must not allocate, and
    // its gather writes anywhere in [data(), data() + max_datagram) -
    // valid objects for the whole range, not just the reserved capacity.
    m_SendScratch.resize(m_MaxDatagram);

    // The receive ring is allocated in full for the same reason on the
    // incoming side: push() and deliver() must not allocate either.
    m_Slots.resize(m_MaxPending);
    for (size_t i = 0; i < m_MaxPending; ++i)
        m_Slots[i].data.resize(m_MaxDatagram);
}

SRTTransportQueue::~SRTTransportQueue() {}

int SRTTransportQueue::push(const void* data, size_t len, const struct sockaddr* src, socklen_t srclen)
{
    if (!data || len < SRT_PACKET_HDR_SIZE || len > m_MaxDatagram)
        return -1;

    // The source must be a complete AF_INET or AF_INET6 address: the
    // channel silently discards a datagram whose reported source is
    // missing, partial, or of a family it cannot dispatch by, so it
    // could never be delivered - reject it here, not at delivery.
    size_t minlen = sizeof(sa_family_t); // enough of the source to read its family
    if (src && srclen >= minlen)
        minlen = src->sa_family == AF_INET    ? sizeof(struct sockaddr_in)
               : src->sa_family == AF_INET6 ? sizeof(struct sockaddr_in6)
               : 0; // no family SRT can dispatch by
    if (!src || minlen == 0 || srclen < minlen || srclen > sizeof(struct sockaddr_storage))
        return -1;

    std::lock_guard<std::mutex> lk(m_QueueLock);

    if (m_bDetached)
        return -1;

    if (m_Count >= m_MaxPending)
    {
        // A full queue drops the datagram, like a full kernel socket
        // buffer. Counted: this is the one benign push failure, and
        // dropped() keeps it distinguishable from the rejections above.
        ++m_Dropped;
        return -1;
    }

    // Copy the datagram into the next free ring slot. The slots were
    // allocated by the constructor, so nothing here allocates or throws;
    // a full ring was already reported above, like a full kernel socket
    // buffer reporting a dropped datagram.
    Slot& s = m_Slots[(m_Head + m_Count) % m_MaxPending];
    memcpy(s.data.data(), data, len);
    memcpy(&s.src, src, srclen);
    s.srclen = srclen;
    s.len    = len;
    ++m_Count;

    // Wake up the receiving side. The flag is raised under the wake mutex
    // before the notification so that a wake arriving while the waiter is
    // between waits stays set and can never be lost. notify_all, not
    // notify_one: every waiter must be sent to the queue check, since
    // with a condition variable there is no level-kept signal that would
    // keep a later wait from blocking.
    {
        std::lock_guard<std::mutex> wlk(m_WakeMutex);
        m_bWoken = true;
    }
    m_WakeCV.notify_all();
    return 0;
}

size_t SRTTransportQueue::dropped() const
{
    return m_Dropped.load();
}

void SRTTransportQueue::detach()
{
    {
        // Wait for a possible in-flight send callback to return before
        // the application considers the callback resources freed.
        std::lock_guard<std::mutex> lk(m_SendLock);
        m_bSendActive = false;
    }

    std::lock_guard<std::mutex> lk(m_QueueLock);
    m_bDetached = true;

    // Release a possible waiter promptly - it must report BROKEN now, not
    // after its timeout expires.
    {
        std::lock_guard<std::mutex> wlk(m_WakeMutex);
        m_bWoken = true;
    }
    m_WakeCV.notify_all();
}

SRT_VirtualTransportConfig SRTTransportQueue::getConfig(int initial_rtt_ms)
{
    SRT_VirtualTransportConfig cfg;
    cfg.send           = &SRTTransportQueue::stubSend;
    cfg.waitread       = &SRTTransportQueue::stubWaitRead;
    cfg.deliver        = &SRTTransportQueue::stubDeliver;
    cfg.opaq           = this;
    cfg.initial_rtt_ms = initial_rtt_ms;
    return cfg;
}

int SRTTransportQueue::send(const struct sockaddr* dst, int dstlen, const SRT_VTBuf* vec, int nvec)
{
    if (!dst || !vec || nvec < 0)
        return -1;

    std::lock_guard<std::mutex> lk(m_SendLock);

    if (!m_bSendActive)
        return -1;

    // Gather the entries into one contiguous datagram for the callback.
    // The scratch block was reserved to m_MaxDatagram by the constructor
    // and is never grown here, so this path allocates nothing; a gathered
    // datagram above the bound fails with -1 outright, without the
    // application callback seeing it. m_SendLock held above serializes
    // the calls, so reusing the scratch needs no further protection.
    size_t len = 0;
    for (int i = 0; i < nvec; ++i)
    {
        if (vec[i].len < 0)
            return -1;
        len += (size_t)vec[i].len;
    }

    if (len > m_MaxDatagram)
        return -1;

    size_t offset = 0;
    for (int i = 0; i < nvec; ++i)
    {
        if (vec[i].len)
            memcpy(m_SendScratch.data() + offset, vec[i].data, (size_t)vec[i].len);
        offset += (size_t)vec[i].len;
    }

    return m_SendFn(m_Opaq, m_SendScratch.data(), (int)len, dst, dstlen);
}

SRT_VTWAIT SRTTransportQueue::waitread(int timeout_ms)
{
    // Wait for a pushed datagram, up to the given timeout. The persistent
    // woken flag mirrors a level-kept signal: raised before the notify,
    // drained here, so a wake is never lost; a return with the flag still
    // unset (spurious wake, ticket taken by another waiter) is re-waited
    // to the deadline. detach() publishes m_bDetached before raising the
    // wake flag, so the test under this mutex sees a mid-wait detach.
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::unique_lock<std::mutex> wlk(m_WakeMutex);
    while (!m_bWoken && !m_bDetached.load())
    {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (now >= deadline)
            return SRT_VTWAIT_TIMEOUT;
        m_WakeCV.wait_for(wlk, deadline - now);
    }

    // The flag may have been raised by detach(), not push(): BROKEN
    // either way, and the ticket stays undrained - after BROKEN srtcore
    // stops calling the callbacks, so a stale one is harmless.
    if (m_bDetached.load())
        return SRT_VTWAIT_BROKEN;

    m_bWoken = false;
    return SRT_VTWAIT_READY;
}

int SRTTransportQueue::deliver(struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    if (!w_src || !w_srclen || !w_vec || nvec <= 0)
        return -1;

    // The queue lock is held across the whole pickup, the scatter
    // included: once the slot is released below, a concurrent push may
    // eventually wrap the ring onto it, and only the lock keeps the
    // buffer stable until the last byte is read out.
    std::lock_guard<std::mutex> lk(m_QueueLock);

    if (m_Count == 0)
    {
        // Spurious wakeup, nothing to deliver.
        return -1;
    }

    // Pick up the earliest datagram; releasing its slot makes room for
    // one push. The slot's contents stay valid - the lock is still held.
    const Slot&  s   = m_Slots[m_Head];
    const size_t len = s.len;
    m_Head           = (m_Head + 1) % m_MaxPending;
    --m_Count;

    if (m_Count != 0)
    {
        // Signal again so that the receiving side keeps picking up the
        // remaining datagrams without waiting for the timeout.
        {
            std::lock_guard<std::mutex> wlk(m_WakeMutex);
            m_bWoken = true;
        }
        m_WakeCV.notify_all();
    }

    // The datagram must fit into the entries as a whole, and the source
    // address into its buffer - checked before any of them is written,
    // so a rejected datagram never touches the caller's buffers. One
    // that does not fit is discarded, like MSG_TRUNC on a UDP socket:
    // a truncated source address would be memory-safe nonsense.
    size_t capacity = 0;
    for (int i = 0; i < nvec; ++i)
        capacity += (size_t)(w_vec[i].len > 0 ? w_vec[i].len : 0);

    if (len > capacity)
        return -1;

    if (*w_srclen <= 0 || (size_t)*w_srclen < s.srclen)
        return -1;

    // Scatter the datagram over the caller's buffer entries, the header
    // block first, the remainder into the next buffers - the way recvmsg
    // scatters a datagram. offset stays below len <= max_datagram, so
    // the reads never leave the slot's preallocated buffer.
    size_t remaining = len;
    size_t offset    = 0;
    for (int i = 0; i < nvec && remaining > 0; ++i)
    {
        const size_t n = std::min(remaining, (size_t)(w_vec[i].len > 0 ? w_vec[i].len : 0));
        if (n)
            memcpy(w_vec[i].data, s.data.data() + offset, n);
        offset += n;
        remaining -= n;
    }

    memcpy(w_src, &s.src, s.srclen);
    *w_srclen = (int)s.srclen;

    return (int)len;
}

int SRTTransportQueue::stubSend(void* opaq, const struct sockaddr* dst, int dstlen, const SRT_VTBuf* vec, int nvec)
{
    return static_cast<SRTTransportQueue*>(opaq)->send(dst, dstlen, vec, nvec);
}

SRT_VTWAIT SRTTransportQueue::stubWaitRead(void* opaq, int timeout_ms)
{
    return static_cast<SRTTransportQueue*>(opaq)->waitread(timeout_ms);
}

int SRTTransportQueue::stubDeliver(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec)
{
    return static_cast<SRTTransportQueue*>(opaq)->deliver(w_src, w_srclen, w_vec, nvec);
}

} // namespace srt
