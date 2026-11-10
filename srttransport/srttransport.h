/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2026 maxolgi.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef INC_SRT_TRANSPORT_H
#define INC_SRT_TRANSPORT_H

// The OPTIONAL srttransport helper library (built only with
// ENABLE_VIRTUAL_TRANSPORT): a push-queue model on top of the virtual
// transport callback seam of the public srt.h, for an application that
// owns a datagram medium (e.g. WebTransport). Not part of libsrt: it
// uses no srtcore internals, calls no srt_* function, and may use the
// standard C++ library freely. See README.md for the usage guide.

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
// <sys/socket.h> is fine here: this header is part of the srttransport
// helper, built only with ENABLE_VIRTUAL_TRANSPORT (Linux and Emscripten
// only, enforced in CMakeLists.txt), so POSIX definitions always exist.
#include <sys/socket.h>
#include <vector>

#include "srt.h"

namespace srt
{

/// @brief A plain contiguous-datagram send callback for the medium: the
/// common case, since most medium APIs (e.g. WebTransport datagrams)
/// accept one buffer. The queue gathers the scattered buffer array of
/// the seam into one block before calling this.
///
/// @param [in] opaq opaque value given to the SRTTransportQueue constructor.
/// @param [in] data the whole outgoing datagram, network byte order.
/// @param [in] len datagram size in bytes.
/// @param [in] dst destination address of the datagram.
/// @param [in] dstlen size of the address at @a dst.
/// @return The number of bytes accepted, or -1 if the datagram failed.
typedef int SRTTransportSendFunc(void* opaq, const char* data, int len, const struct sockaddr* dst, int dstlen);

/// @brief The push-queue model on top of the virtual transport seam.
///
/// The application pushes datagrams received from its medium into this
/// queue (push()) and supplies a plain contiguous send callback in the
/// constructor; getConfig() returns an SRT_VirtualTransportConfig
/// wiring the seam's callbacks to this object, ready for
/// srt_bind_virtual(). Incoming datagrams wait in a bounded,
/// preallocated ring (the kernel socket buffer's role).
///
/// Lifetime: the config hands this object to SRT as the callbacks'
/// opaque value, so it must outlive every socket bound with it.
///
/// Threading: push(), detach(), dropped(), getConfig() and send() are
/// safe from any number of threads (send() is serialized internally).
/// waitread() and deliver() are a single-consumer pair, called only
/// from the one receive worker serving the socket: the wake flag is a
/// single ticket, so a second consumer could drain it and starve the
/// other. The class calls no SRT library function.
class SRTTransportQueue
{
public:
    /// @brief Construct the queue around a plain contiguous send callback.
    /// @param [in] send_fn the medium's send callback; must not be null.
    /// @param [in] opaq opaque value passed back to @a send_fn.
    /// @param [in] max_pending bound on queued datagrams, the kernel socket buffer's role (min 1).
    /// @param [in] max_datagram per-datagram size bound, both directions (min 16) - over-sized
    /// datagrams fail without reaching the callback.
    ///
    /// All storage is allocated once, in full, by this constructor, so
    /// no hot path allocates. The defaults mirror SRT's own UDP numbers
    /// (1472 = header + 1456-byte MTU payload; 44 fit the default
    /// 64 KiB UDP buffer); larger bounds are an explicit opt-in.
    SRTTransportQueue(SRTTransportSendFunc* send_fn, void* opaq,
                      size_t max_pending  = 44,
                      size_t max_datagram = 1472);

    ~SRTTransportQueue();

    SRTTransportQueue(const SRTTransportQueue&)            = delete;
    SRTTransportQueue& operator=(const SRTTransportQueue&) = delete;

    /// @brief Push a datagram as if received from the given source (any
    /// thread; exactly one datagram per call). The datagram is queued
    /// and the receiving side is woken.
    ///
    /// @param [in] data the serialized datagram, network byte order.
    /// @param [in] len datagram size in bytes.
    /// @param [in] src source address to report for the datagram; must
    /// not be null, and its family must be AF_INET or AF_INET6.
    /// @param [in] srclen size of the address at @a src.
    ///
    /// @return 0 on success, -1 on error; never throws. -1 exactly for:
    /// invalid arguments (@a data null, @a len outside [16, max_datagram],
    /// @a src null/short/oversized, or a family other than AF_INET or
    /// AF_INET6 - undeliverable, so rejected here), a detached transport,
    /// or a full queue - the benign UDP-like drop, counted by dropped().
    int push(const void* data, size_t len, const struct sockaddr* src, socklen_t srclen);

    /// @brief Count the datagrams push() dropped because the queue was
    /// full (any thread).
    ///
    /// push() returns -1 alike for an overflow drop, invalid arguments,
    /// and a detached transport; this counter separates the first from
    /// the rest: it counts only the overflow drops - the benign, UDP-like
    /// losses - and nothing else. It is monotonic over the object's
    /// lifetime; there is no reset.
    ///
    /// @return The number of datagrams dropped due to a full queue since
    /// construction.
    size_t dropped() const;

    /// @brief Disable the transport (any thread).
    ///
    /// After this call push() and the send callback fail, and waitread()
    /// reports the medium as broken. Blocks until an in-flight send
    /// callback returns, so once detach() returns the application is free
    /// to release the resources the callback uses.
    void detach();

    /// @brief Get the config wiring the seam callbacks to this object.
    ///
    /// The returned config models the PUBLIC virtual transport
    /// configuration; it does not set the handshake form - the
    /// conclusion-first form is an internal srtcore setting, not part of
    /// the public API.
    ///
    /// @param [in] initial_rtt_ms initial RTT to assume for the connection
    /// (0 = disabled, otherwise 1..30000).
    /// @return The config to pass to srt_bind_virtual().
    SRT_VirtualTransportConfig getConfig(int initial_rtt_ms = 0);

    /// @brief Gather an outgoing datagram and hand it to the send callback.
    ///
    /// Implements the send side of the seam: the buffer entries are
    /// gathered into one reusable scratch block (normally two entries,
    /// the packet header block and the payload block), passed to the
    /// plain send callback with the destination address. The scratch is
    /// pre-reserved to max_datagram, so this path allocates nothing; a
    /// gathered datagram above it fails with -1 without the callback.
    ///
    /// @param [in] dst destination address for the datagram.
    /// @param [in] dstlen size of the address at @a dst.
    /// @param [in] vec buffer entries of the datagram, network byte order.
    /// @param [in] nvec number of entries at @a vec.
    /// @return The value returned by the callback, or -1 if detached or
    /// the gathered datagram exceeds max_datagram.
    int send(const struct sockaddr* dst, int dstlen, const SRT_VTBuf* vec, int nvec);

    /// @brief Wait for a pushed datagram to become ready (seam side).
    ///
    /// Implements the waitread callback: waits for up to @a timeout_ms
    /// milliseconds for a datagram to be deliverable. Wakes are level-kept
    /// in an internal flag, so a push arriving while the caller is between
    /// waits is never lost.
    ///
    /// @param [in] timeout_ms maximum time to wait, in milliseconds.
    /// @return SRT_VTWAIT_READY if a datagram can be picked by deliver(),
    /// SRT_VTWAIT_TIMEOUT if none arrived in time, SRT_VTWAIT_BROKEN if
    /// detached.
    SRT_VTWAIT waitread(int timeout_ms);

    /// @brief Pick up the earliest pushed datagram (seam side).
    ///
    /// Implements the deliver callback: copies the earliest queued
    /// datagram into the provided buffer entries, the header block first
    /// and the remainder into the next buffers, and its source address
    /// into @a w_src. Like a datagram too big for the receiving unit on a
    /// UDP socket, a datagram that does not fit the entries is discarded
    /// - and so is one whose source address does not fit @a w_src whole.
    ///
    /// @param [out] w_src buffer for the source address of the datagram.
    /// @param [in,out] w_srclen capacity of @a w_src on entry, the number
    /// of bytes written on return.
    /// @param [out] w_vec buffer entries to scatter the datagram into.
    /// @param [in] nvec number of entries at @a w_vec.
    ///
    /// @return The total number of bytes written, or -1 on error (nothing
    /// queued, datagram discarded).
    int deliver(struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec);

private:
    // Static trampolines for SRT_VirtualTransportConfig: the config's
    // opaque value is this object.
    static int    stubSend(void* opaq, const struct sockaddr* dst, int dstlen, const SRT_VTBuf* vec, int nvec);
    static SRT_VTWAIT stubWaitRead(void* opaq, int timeout_ms);
    static int    stubDeliver(void* opaq, struct sockaddr* w_src, int* w_srclen, SRT_VTBuf* w_vec, int nvec);

    // One slot of the receive ring, allocated by the constructor: the
    // buffer is max_datagram bytes and is reused by every datagram that
    // lands in this slot, so queuing allocates nothing.
    struct Slot
    {
        std::vector<char>       data;
        size_t                  len; // queued datagram size, valid while the slot is occupied
        struct sockaddr_storage src;
        socklen_t               srclen;
    };

    SRTTransportSendFunc* const m_SendFn;
    void* const                 m_Opaq;

    // Queue bounds, fixed at construction: the datagram-size check in
    // push() runs before the queue lock is taken, so these are const
    // members rather than mutable state.
    const size_t    m_MaxPending;
    const size_t    m_MaxDatagram;

    std::mutex      m_SendLock; // serializes send callback invocations, protects the members below
    bool            m_bSendActive;
    std::vector<char> m_SendScratch; // gather area for the send callback, reserved at construction, never grown

    std::mutex          m_QueueLock; // protects the members below
    std::vector<Slot>   m_Slots;     // the receive ring: m_MaxPending preallocated slots
    size_t              m_Head;      // index of the earliest queued datagram
    size_t              m_Count;     // queued datagrams; the occupied slots are m_Head .. m_Head+m_Count-1, mod m_MaxPending
    // Atomic: waitread() tests it under m_WakeMutex, without m_QueueLock;
    // detach() stores it before raising the wake flag, so the test cannot
    // miss a mid-wait detach.
    std::atomic<bool>   m_bDetached;

    // Count of datagrams dropped by push() because the queue was full
    // (see dropped()). Atomic rather than guarded by m_QueueLock alone:
    // push() increments it under that lock, but dropped() reads it
    // without taking any lock.
    std::atomic<size_t> m_Dropped;

    // Wake primitive for the receiving side. The persistent m_bWoken flag
    // mirrors a level-kept signal: raised before the notification, drained
    // by the waiter, so a wake that arrives while the waiter is between
    // waits is never lost.
    std::mutex              m_WakeMutex;
    std::condition_variable m_WakeCV;
    bool                    m_bWoken;
};

} // namespace srt

#endif // INC_SRT_TRANSPORT_H
