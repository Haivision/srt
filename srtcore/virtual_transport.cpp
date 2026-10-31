/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include "platform_sys.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>

#include <srt_compat.h>

#include "virtual_transport.h"

#include "logging.h"
#include "logger_defs.h"

using namespace std;
using namespace srt_logging;
using namespace srt::sync;

namespace srt
{

// Bound on the number of datagrams pending reception. This takes the role
// of the kernel socket buffer of a UDP socket; when full, injected
// datagrams are dropped, like UDP would do.
static const size_t VT_MAX_PENDING = 1024;

// Sanity bound for the size of a single injected datagram.
static const size_t VT_MAX_DATAGRAM = 65536;

CVirtualTransport::CVirtualTransport(SRTVirtualTransportSendFunc* send_fn, void* opaq)
    : m_SendFn(send_fn)
    , m_Opaq(opaq)
    , m_bSendActive(true)
    , m_Queue()
    , m_bDetached(false)
    , m_iRefs(1)
{
}

CVirtualTransport::~CVirtualTransport() {}

CVirtualTransport* CVirtualTransport::grab()
{
    ++m_iRefs;
    return this;
}

void CVirtualTransport::release()
{
    if (--m_iRefs == 0)
        delete this;
}

int CVirtualTransport::send(
    const sockaddr_any& dst, const char* hdr, size_t hdrsize, const char* payload, size_t payloadsize)
{
    ScopedLock lk(m_SendLock);

    if (!m_bSendActive)
    {
        LOGC(kmlog.Note,
             log << "VT: send after detach - DROPPING " << (hdrsize + payloadsize) << " bytes to " << dst.str());
        return -1;
    }

    // Gather the two blocks into one contiguous datagram for the callback.
    // The scratch buffer is only reallocated when a larger datagram is
    // sent; m_SendLock held above serializes the calls, so reusing it
    // needs no further protection.
    const size_t len = hdrsize + payloadsize;
    m_SendScratch.resize(len);
    if (hdrsize)
        memcpy(m_SendScratch.data(), hdr, hdrsize);
    if (payload)
        memcpy(m_SendScratch.data() + hdrsize, payload, payloadsize);

    return m_SendFn(m_Opaq, m_SendScratch.data(), (int)len, dst.get(), (int)dst.size());
}

int CVirtualTransport::inject(const char* data, size_t len, const sockaddr_any& src)
{
    if (len < CPacket::HDR_SIZE || len > VT_MAX_DATAGRAM)
    {
        LOGC(kmlog.Error, log << "VT: inject: invalid datagram size " << len);
        return -1;
    }

    ScopedLock lk(m_QueueLock);

    if (m_bDetached)
        return -1;

    if (m_Queue.size() >= VT_MAX_PENDING)
    {
        LOGC(kmlog.Error, log << "VT: inject: local storage depleted (" << m_Queue.size() << " pending) - DROPPING");
        return -1;
    }

    Datagram dg;
    dg.data.assign(data, data + len);
    dg.src = src;
    m_Queue.push_back(std::move(dg));

    // Wake up the receiving worker. The flag is raised under the wake
    // mutex before the notification so that a wake arriving while the
    // worker is between waits stays set and can never be lost. notify_all,
    // not notify_one: every waiter must be sent to the queue check, since
    // with a condition variable there is no level-kept signal that would
    // keep a later wait from blocking.
    {
        std::lock_guard<std::mutex> wlk(m_WakeMutex);
        m_bWoken = true;
    }
    m_WakeCV.notify_all();
    return 0;
}

void CVirtualTransport::detach()
{
    {
        // Wait for a possible in-flight send callback to return before
        // the owner considers the callback resources freed.
        ScopedLock lk(m_SendLock);
        m_bSendActive = false;
    }

    ScopedLock lk(m_QueueLock);
    m_bDetached = true;
}

EReadStatus CVirtualTransport::receive(sockaddr_any& w_src, CPacket& w_packet)
{
    // Wait for an injected datagram, with the same timeout as the select()
    // over the UDP socket in CChannel::recvfrom. The persistent woken flag
    // mirrors a level-kept signal: set by inject() before the notification,
    // drained here, and raised again below when datagrams remain queued, so
    // the worker keeps picking them up without waiting for the timeout.
    {
        std::unique_lock<std::mutex> wlk(m_WakeMutex);
        if (!m_bWoken)
            m_WakeCV.wait_for(wlk, chrono::milliseconds(10));

        if (!m_bWoken) // timed out and still no wake
        {
            w_packet.setLength(-1);
            return RST_AGAIN;
        }

        m_bWoken = false;
    }

    Datagram dg;
    {
        ScopedLock lk(m_QueueLock);
        if (m_Queue.empty())
        {
            // Spurious wakeup, nothing to receive.
            w_packet.setLength(-1);
            return RST_AGAIN;
        }

        dg = std::move(m_Queue.front());
        m_Queue.pop_front();
        if (!m_Queue.empty())
        {
            // Signal again so that the worker keeps picking up the
            // remaining datagrams without waiting for the timeout.
            {
                std::lock_guard<std::mutex> wlk(m_WakeMutex);
                m_bWoken = true;
            }
            m_WakeCV.notify_all();
        }
    }

    // Sanity check for a case when it didn't fill in even the header, and
    // against a datagram too big for the receiving unit - a UDP socket
    // would report MSG_TRUNC for it and the packet would be dropped.
    const size_t len      = dg.data.size();
    const size_t capacity = w_packet.getLength(); // set by the caller to the unit's payload size
    if (len < CPacket::HDR_SIZE || len - CPacket::HDR_SIZE > capacity)
    {
        LOGC(kmlog.Error, log << "VT: receive: DROPPING datagram of " << len << " bytes (capacity " << capacity << ")");
        w_packet.setLength(-1);
        return RST_AGAIN;
    }

    IOVector* pv = w_packet.getPacketVector();
    memcpy(pv[CPacket::PV_HEADER].iov_base, &dg.data[0], CPacket::HDR_SIZE);
    memcpy(pv[CPacket::PV_DATA].iov_base, &dg.data[CPacket::HDR_SIZE], len - CPacket::HDR_SIZE);
    w_src = dg.src;
    w_packet.setLength(len - CPacket::HDR_SIZE);
    w_packet.toHostByteOrder();

    return RST_OK;
}

} // namespace srt
