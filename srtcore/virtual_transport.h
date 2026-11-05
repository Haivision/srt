/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef INC_SRT_VIRTUAL_TRANSPORT_H
#define INC_SRT_VIRTUAL_TRANSPORT_H

// This file is only meaningful in a build with the virtual transport
// seam enabled (CMake: ENABLE_VIRTUAL_TRANSPORT).
#ifdef SRT_ENABLE_VIRTUAL_TRANSPORT

#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

#include "srt.h"
#include "common.h"
#include "netinet_any.h"
#include "packet.h"
#include "sync.h"

namespace srt
{

/// @brief The userspace transport seam behind CChannel.
///
/// An object of this class is created by srt_attach_virtual_transport()
/// for a socket that is to exchange datagrams through an owner-supplied
/// transport (e.g. WebTransport) instead of an OS UDP socket. Once the
/// socket is bound or connected, the multiplexer channel delivers all
/// outgoing datagrams to the owner's send callback through send(), and
/// the owner delivers incoming datagrams to the receiving queue by
/// calling srt_virtual_transport_inject(), which lands in inject().
///
/// The object is reference-counted: one reference is held by the caller's
/// handle (dropped by srt_virtual_transport_release()), one by the socket
/// the transport is attached to, and one by the multiplexer channel the
/// transport is wired into. This keeps the caller's handle safe to use
/// concurrently with socket closure.
class CVirtualTransport
{
public:
    CVirtualTransport(SRTVirtualTransportSendFunc* send_fn, void* opaq);
    ~CVirtualTransport();

    CVirtualTransport(const CVirtualTransport&)            = delete;
    CVirtualTransport& operator=(const CVirtualTransport&) = delete;

    /// Take another reference to this object.
    /// @return This object.
    CVirtualTransport* grab();

    /// Drop a reference. Destroys the object when the last one is gone.
    void release();

    /// @brief Deliver a datagram to the transport owner (any thread).
    ///
    /// Invokes the send callback supplied at attachment. The header block
    /// and the payload block are gathered into one contiguous network-order
    /// datagram in an internal scratch buffer, which the callback receives
    /// together with its total size. The buffer is reused across calls -
    /// invocations are serialized by an internal mutex, so the reuse needs
    /// no further protection - and after detach() returns this function no
    /// longer calls the callback.
    ///
    /// @param [in] dst destination address for the datagram.
    /// @param [in] hdr header block of the datagram (network byte order).
    /// @param [in] hdrsize size of the header block, in bytes.
    /// @param [in] payload payload block of the datagram (network byte order).
    /// @param [in] payloadsize size of the payload block, in bytes.
    /// @return The value returned by the callback, or -1 if detached.
    int send(const sockaddr_any& dst, const char* hdr, size_t hdrsize, const char* payload, size_t payloadsize);

    /// @brief Inject a datagram as if received from the given source (any thread).
    ///
    /// The datagram is queued and the receiving queue worker is woken up
    /// through an internal condition variable, exactly one datagram per
    /// receive() call. Thread-safe.
    ///
    /// @param [in] data the serialized datagram (network byte order).
    /// @param [in] len datagram size in bytes.
    /// @param [in] src source address to report for the datagram.
    /// @return 0 on success, -1 on error (detached or storage depleted).
    int inject(const char* data, size_t len, const sockaddr_any& src);

    /// @brief Disable the transport.
    ///
    /// After this call send() and inject() fail, though datagrams queued
    /// before detaching can still be received. Blocks until an in-flight
    /// send callback returns, so once detach() returns the owner is free
    /// to release the resources the callback uses.
    void detach();

    /// @brief Receive one injected datagram (CRcvQueue worker thread only).
    ///
    /// Waits for an injected datagram with the same timeout as the select()
    /// call over the UDP socket in CChannel::recvfrom, then copies the
    /// earliest queued datagram into @a w_packet, the way recvfrom does.
    ///
    /// @param [out] w_src source address of the received datagram.
    /// @param [out] w_packet packet to fill with the received datagram.
    /// @return RST_OK if a datagram was received, RST_AGAIN on timeout or
    ///         when the datagram had to be discarded, RST_ERROR otherwise.
    EReadStatus receive(sockaddr_any& w_src, CPacket& w_packet);

private:
    struct Datagram
    {
        std::vector<char> data;
        sockaddr_any      src;
    };

    SRTVirtualTransportSendFunc* const m_SendFn;
    void* const                        m_Opaq;

    sync::Mutex m_SendLock; // serializes callback invocations, protects m_bSendActive and m_SendScratch
    bool        m_bSendActive;

    // Assembly area for the datagram handed to the send callback: the
    // header and payload blocks are gathered here into one contiguous
    // buffer. Grows on demand and is reused across calls, which is safe
    // because every send() runs under m_SendLock.
    std::vector<char> m_SendScratch;

    sync::Mutex          m_QueueLock; // protects m_Queue and m_bDetached
    std::deque<Datagram> m_Queue;
    bool                 m_bDetached;

    // Wake primitive for the receiving worker. The persistent m_bWoken flag
    // mirrors a level-kept signal: raised under the wake mutex before the
    // notification, drained by the waiter, so a wake that arrives while the
    // worker is between waits is never lost.
    std::mutex              m_WakeMutex;
    std::condition_variable m_WakeCV;
    bool                    m_bWoken = false;

    sync::atomic<int> m_iRefs;
};

} // namespace srt

#endif // SRT_ENABLE_VIRTUAL_TRANSPORT

#endif // INC_SRT_VIRTUAL_TRANSPORT_H
