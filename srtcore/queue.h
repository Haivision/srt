/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/12/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_QUEUE_H
#define INC_SRT_QUEUE_H

#include "common.h"
#include "packet.h"
#include "socketconfig.h"
#include "netinet_any.h"
#include "utilities.h"
#include <list>
#include <map>
#include <queue>
#include <vector>

namespace srt
{
class CChannel;
class CUDT;

struct CUnit
{
    CPacket m_Packet; // packet
    sync::atomic<bool> m_bTaken; // true if the unit is is use (can be stored in the RCV buffer).
};

class CUnitQueue
{
public:
    /// @brief Construct a unit queue.
    /// @param mss Initial number of units to allocate.
    /// @param mss Maximum segment size meaning the size of each unit.
    /// @throws CUDTException SRT_ENOBUF.
    CUnitQueue(int initNumUnits, int mss);
    ~CUnitQueue();

public:
    int capacity() const { return m_iSize; }
    int size() const { return m_iSize - m_iNumTaken; }

public:
    /// @brief Find an available unit for incoming packet. Allocate new units if 90% or more are in use.
    /// @note This function is not thread-safe. Currently only CRcvQueue::worker thread calls it, thus
    /// it is not an issue. However, must be protected if used from several threads in the future.
    /// @return Pointer to the available unit, NULL if not found.
    CUnit* getNextAvailUnit();

    void makeUnitFree(CUnit* unit);

    void makeUnitTaken(CUnit* unit);

private:
    struct CQEntry
    {
        CUnit* m_pUnit;   // unit queue
        char*  m_pBuffer; // data buffer
        int    m_iSize;   // size of each queue

        CQEntry* m_pNext;
    };

    /// Increase the unit queue size (by @a m_iBlockSize units).
    /// Uses m_mtx to protect access and changes of the queue state.
    /// @return 0: success, -1: failure.
    int increase_();

    /// @brief Allocated a CQEntry of iNumUnits with each unit of mss bytes.
    /// @param iNumUnits a number of units to allocate
    /// @param mss the size of each unit in bytes.
    /// @return a pointer to a newly allocated entry on success, NULL otherwise.
    static CQEntry* allocateEntry(const int iNumUnits, const int mss);

private:
    CQEntry* m_pQEntry;    // pointer to the first unit queue
    CQEntry* m_pCurrQueue; // pointer to the current available queue
    CQEntry* m_pLastQueue; // pointer to the last unit queue
    CUnit* m_pAvailUnit; // recent available unit
    int m_iSize;  // total size of the unit queue, in number of packets
    sync::atomic<int> m_iNumTaken; // total number of valid (occupied) packets in the queue
    const int m_iMSS; // unit buffer size
    const int m_iBlockSize; // Number of units in each CQEntry.

private:
    CUnitQueue(const CUnitQueue&);
    CUnitQueue& operator=(const CUnitQueue&);
};


// NOTE: SocketHolder was moved here because it's a dependency of
// CSendOrderList, so it must be first defined.
struct SocketHolder
{
    typedef std::list<SocketHolder> socklist_t;
    typedef typename socklist_t::iterator sockiter_t;
    static socklist_t empty_list;
    static const size_t heap_npos = std::string::npos;
    static sockiter_t none() { return empty_list.end(); }

    enum State
    {
        NONEXISTENT = -2,
        BROKEN = -1,
        INIT = 0,
        PENDING = 1,
        ACTIVE = 2
    };

    // Used by SendOrder.
    enum EReschedule
    {
        DONT_RESCHEDULE = 0,
        DO_RESCHEDULE   = 1
    };

    static std::string StateStr(State);

    State m_State;
    class CUDTSocket* m_pSocket;

    // Time when the connection request should expire. Contains zero,
    // if there was no request.
    sync::steady_clock::time_point m_tsRequestTTL;

    // SRT connection peer address
    sockaddr_any m_PeerAddr;

    struct UpdateNode
    {
        // Time when the socket needs to be picked up for update.
        typedef sync::steady_clock::time_point key_type;
        key_type time;
        size_t pos;

        // Access methods
        static key_type& key(sockiter_t i) { return i->m_UpdateOrder.time; }
        static size_t& position(sockiter_t i) { return i->m_UpdateOrder.pos; }
        static sockiter_t none() { return empty_list.end(); }
        static bool order(key_type left, key_type right) { return left < right; }

        UpdateNode() : pos(heap_npos) {}

    } m_UpdateOrder;

    struct SendNode
    {
        // Time when sending through this socket should happen.
        typedef sync::steady_clock::time_point key_type;
        key_type time;
        size_t pos;

        // Access methods
        static key_type& key(sockiter_t i) { return i->m_SendOrder.time; }
        static size_t& position(sockiter_t i) { return i->m_SendOrder.pos; }
        static sockiter_t none() { return empty_list.end(); }
        static bool order(key_type left, key_type right) { return left < right; }

        SendNode() : pos(heap_npos) {}

        // private utilities

        // Checks if the position is not set to a trap representation
        bool pinned() const { return pos != heap_npos; }

        // Position 0 means that this is the earliest element and this
        // element would be returned from the next pop() call.
        bool is_top() const { return pos == 0; }

    } m_SendOrder;

#if SRT_ENABLE_THREAD_DEBUG
    UniquePtr<sync::Condition::ScopedNotifier> m_sanitized_cond;

    // Declare given condition variable that the thread running this
    // object will be responsible for notifying this CV.
    void addCondSanitizer(sync::Condition& cond)
    {
        m_sanitized_cond.reset(new sync::Condition::ScopedNotifier(cond));
    }
#endif

    SocketHolder():
        m_State(INIT),
        m_pSocket(NULL),
        m_tsRequestTTL()
    {
    }

    // To return true the socket must be:
    // - at least in PENDING state
    // - have equal address
    // The w_ttl and w_state are filled always, regardless of the result.
    enum MatchState { MS_OK = 0, MS_INVALID_STATE = 1, MS_INVALID_ADDRESS = 2, MS_INVALID_DATA = 3 };
    static std::string MatchStr(MatchState);

    MatchState checkIncoming(const sockaddr_any& peer_addr,
            sync::steady_clock::time_point& w_ttl,
            State& w_state) const
    {
        w_ttl = m_tsRequestTTL;
        w_state = m_State;

        if (!m_pSocket)
            return MS_INVALID_DATA;

        if (peer_addr != m_PeerAddr)
            return MS_INVALID_ADDRESS;

        if (int(m_State) > int(INIT))
            return MS_OK;

        return MS_INVALID_STATE;
    }

    SRTSOCKET id() const;
    SRTSOCKET peerID() const;
    sockaddr_any peerAddr() const;

    static SocketHolder initial(CUDTSocket* so)
    {
        SocketHolder that;

        that.m_pSocket = so;
        that.m_State = INIT;

        return that;
    }

    void setConnector(const sockaddr_any& addr, const sync::steady_clock::time_point& ttl)
    {
        m_State = PENDING;
        m_PeerAddr = addr;
        m_tsRequestTTL = ttl;
    }

    // This function is executed when the connection-pending state
    // is withdrawn and the socket turns into CONNECTED or BROKEN
    // state, according to the flags.
    void setConnectedState();

    SRTSOCKET setBrokenPeer()
    {
        m_State = BROKEN;
        return peerID();
    }

    // Debug support
    std::string report() const;
};

// REPLACEMENT FOR CSndUList 

class CSendOrderList
{
    // TEST IF REQUIRED API
public:
    CSendOrderList();

    void resetAtFork();

    /// Advice the given socket to be scheduled for sending in the sender queue.
    /// If the socket isn't yet in the queue, it will be added with given time.
    /// If it's there already, then depending on @a reschedule:
    ///    * with DONT_RESCHEDULE, nothing will be done
    ///    * with DO_RESCHEDULE, the socket will be updated with given time (@a ts)
    /// @param [in] point node pointer to the socket holder in the multiplexer
    /// @param [in] reschedule if the timestamp should be rescheduled
    /// @param [in] ts the next time to trigger sending logic on the CUDT
    /// @return True, if the socket was scheduled for given time
    SRT_TSA_NEEDS_NONLOCKED(m_ListLock)
    bool update(SocketHolder::sockiter_t point, SocketHolder::EReschedule reschedule, sync::steady_clock::time_point ts = sync::steady_clock::now());

    /// Blocks until the time comes to pick up the heap top.
    /// The call remains blocked as long as:
    /// - the heap is empty
    /// - the heap top element's run time is in the future
    /// - no other thread has forcefully interrupted the wait
    /// @return the node that is ready to run, or NULL on interrupt
    SRT_TSA_NEEDS_NONLOCKED(m_ListLock)
    SocketHolder::sockiter_t wait();

    // This function moves the node throughout the heap to put
    // it into the right place.
    SRT_TSA_NEEDS_NONLOCKED(m_ListLock)
    bool requeue(SocketHolder::sockiter_t point, const sync::steady_clock::time_point& uptime);

    /// Remove UDT instance from the list.
    /// @param [in] u pointer to the UDT instance
    SRT_TSA_NEEDS_NONLOCKED(m_ListLock)
    void remove(SocketHolder::sockiter_t point);

    /// Signal to stop waiting in waitNonEmpty().
    SRT_TSA_NEEDS_NONLOCKED(m_ListLock)
    void signalInterrupt();

    void setRunning()
    {
        m_bRunning = true;
    }

    void stop()
    {
        m_bRunning = false;
    }

private:

    HeapSet<SocketHolder::sockiter_t, SocketHolder::SendNode> m_Schedule;

    friend class CSndQueue;

    mutable sync::Mutex     m_ListLock; // Protects the list (m_pHeap, m_iCapacity, m_iLastEntry).
    mutable sync::Condition m_ListCond;
    sync::atomic<bool> m_bRunning;
};

struct LinkStatusInfo
{
    CUDT*        u;
    SRTSOCKET    id;
    int          errorcode;
    sockaddr_any peeraddr;
    int          token;

    struct HasID
    {
        SRTSOCKET id;
        HasID(SRTSOCKET p)
            : id(p)
        {
        }
        bool operator()(const LinkStatusInfo& i) { return i.id == id; }
    };
};

struct CMultiplexer;

class CSndQueue
{
    friend class CUDT;
    friend class CUDTUnited;
    friend struct CMultiplexer;

    CMultiplexer* m_parent;

    CSndQueue(CMultiplexer* parent);
public:
    ~CSndQueue();

public:
    void resetAtFork();
    // XXX There's currently no way to access the socket ID set for
    // whatever the queue is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    /// Initialize the sending queue.
    /// @param [in] c UDP channel to be associated to the queue
    /// @param [in] t Timer
    void init(CChannel* c);

    void setClosing() { m_bClosing = true; }
    void stop();

private:
    static void* worker_fwd(void* param)
    {
        CSndQueue* self = (CSndQueue*)param;
        self->workerSendOrder();

        return NULL;
    }

    void workerSendOrder();
    sync::CThread m_WorkerThread;

private:
    CSendOrderList m_SendOrderList; // List of socket instances for data sending
    CChannel*     m_pChannel;  // The UDP channel for data sending

    sync::atomic<bool> m_bClosing;            // closing the worker

public:


#if defined(SRT_DEBUG_SNDQ_HIGHRATE) //>>debug high freq worker
    sync::steady_clock::duration m_DbgPeriod;
    mutable sync::steady_clock::time_point m_DbgTime;
    struct
    {
        unsigned long lIteration;   //
        unsigned long lSleepTo;     // SleepTo
        unsigned long lNotReadyPop; // Continue
        unsigned long lSendTo;
        unsigned long lNotReadyTs;
        unsigned long lCondWait; // block on m_WindowCond
    } mutable m_WorkerStats;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

private:

#if HVU_ENABLE_LOGGING
    static sync::atomic<int> m_counter;
#endif

    CSndQueue(const CSndQueue&);
    CSndQueue& operator=(const CSndQueue&);
};

class CRcvQueue
{
    friend class CUDT;
    friend class CUDTUnited;
    friend struct CMultiplexer;

    CMultiplexer* m_parent;

    CRcvQueue(CMultiplexer* parent);
public:
    ~CRcvQueue();

public:
    void resetAtFork();
    // XXX There's currently no way to access the socket ID set for
    // whatever the queue is currently working. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    /// Initialize the receiving queue.
    /// @param [in] size queue size
    /// @param [in] mss maximum packet size
    /// @param [in] version IP version
    /// @param [in] hsize hash table size
    /// @param [in] c UDP channel to be associated to the queue
    /// @param [in] t timer
    void init(int size, size_t payload, CChannel* c);

    /// Read a packet for a specific UDT socket id.
    /// @param [in] id Socket ID
    /// @param [out] packet received packet
    /// @return Data size of the packet
    //int recvfrom(SRTSOCKET id, CPacket& to_packet);

    void setClosing() { m_bClosing = true; }

    void stop();
private:
    static void*  worker_fwd(void* param);
    void worker();
    sync::CThread m_WorkerThread;
    // Subroutines of worker
    EReadStatus    worker_RetrieveUnit(SRTSOCKET& id, CUnit*& unit, sockaddr_any& sa);
    EConnectStatus worker_ProcessConnectionRequest(CUnit* unit, const sockaddr_any& sa);
    EConnectStatus worker_RetryOrRendezvous(CUDT* u, CUnit* unit);
    EConnectStatus worker_ProcessAddressedPacket(SRTSOCKET id, CUnit* unit, const sockaddr_any& sa);
    bool worker_TryAcceptedSocket(CUnit* unit, const sockaddr_any& addr);

private:
    CUnitQueue*   m_pUnitQueue; // The received packet queue
    CChannel*     m_pChannel;   // UDP channel for receiving packets

    size_t m_szPayloadSize;     // packet payload size

    sync::atomic<bool> m_bClosing; // closing the worker
#if HVU_ENABLE_LOGGING
    static sync::atomic<int> m_counter; // A static counter to log RcvQueue worker thread number.
#endif

private:
    bool setListener(CUDT* u);
    CUDT* getListener();
    bool removeListener(CUDT* u);
    void storePktClone(SRTSOCKET id, const CPacket& pkt);
    void kick();

    /// @brief Update status of connections in the pending queue.
    /// Stop connecting if TTL expires. Resend handshake request every 250 ms if no response from the peer.
    /// @param rst result of reading from a UDP socket: received packet / nothing read / read error.
    /// @param cst target status for pending connection: reject or proceed.
    /// @param pktIn packet received from the UDP socket.
    void updateConnStatus(EReadStatus rst, EConnectStatus cst, CUnit* unit);

private:
    sync::CSharedObjectPtr<CUDT> m_pListener;        // pointer to the (unique, if any) listening UDT entity

    void registerConnector(const SRTSOCKET&                      id,
                           CUDT*                                 u,
                           const sockaddr_any&                   addr,
                           const sync::steady_clock::time_point& ttl);
    void removeConnector(const SRTSOCKET& id);

    typedef std::map<SRTSOCKET, std::queue<CPacket*> > qmap_t;
    qmap_t          m_mBuffer; // temporary buffer for rendezvous connection request
    sync::Mutex     m_BufferLock;
    sync::Condition m_BufferCond;

private:
    CRcvQueue(const CRcvQueue&);
    CRcvQueue& operator=(const CRcvQueue&);
};


struct CMultiplexer
{
    typedef std::list<SocketHolder> socklist_t;
    typedef typename socklist_t::iterator sockiter_t;
    typedef srt::hash_map<SRTSOCKET, socklist_t::iterator> sockmap_t;

    struct CRL
    {
        SRTSOCKET                      m_iID;      // SRT socket ID (self)
        CUDT*                          m_pUDT;     // CUDT instance
        socklist_t::iterator           m_it;
        sockaddr_any                   m_PeerAddr; // SRT sonnection peer address
        sync::steady_clock::time_point m_tsTTL;    // the time that this request expires
    };
    std::list<CRL> m_lRendezvousID; // The sockets currently in rendezvous mode

    size_t nsockets() const { return m_zSockets; }
    bool empty() const { return m_zSockets == 0; }

    enum AcquisitionControl
    {
        ACQ_RELAXED = 0,
        ACQ_ACQUIRE = 1
    };

private:

    // Clang TSA is so stupid that it would block ordering
    // declaration just because it is in the private section
#if SRT_ENABLE_CLANG_TSA
    friend class CUDTUnited;
#endif

    int m_iID; // multiplexer ID

    mutable sync::Mutex m_SocketsLock;

    socklist_t m_Sockets;
    sync::atomic<size_t> m_zSockets; // size cache

    // Mapper id -> node
    sockmap_t m_SocketMap;

    // Functional orders
    HeapSet<sockiter_t, SocketHolder::UpdateNode> m_UpdateOrderList;
    // Send order is contained in the CSndQueue class.

    // Peer ID to Agent ID mapping
    std::map<SRTSOCKET, SRTSOCKET> m_RevPeerMap;

    CSndQueue     m_SndQueue; // The sending queue
    CRcvQueue     m_RcvQueue; // The receiving queue
    CChannel*     m_pChannel;  // The UDP channel for sending and receiving

    sockaddr_any m_SelfAddr;

    CSrtMuxerConfig m_mcfg;

    // XXX if this helps anyhow, this field can be also
    // just boolean. It's not checked, if it contains the
    // right thread number, only if this is set to a valid
    // thread value or remains default ("no thread"). This
    // value might be useful with debugging though.
    sync::CThread::id m_ReservedDisposal;

public:

    // CAREFUL with this function. This will close the channel
    // regardless if it's in use.
    bool tryCloseIfEmpty();

    CChannel* channel() { return m_pChannel; }
    const CChannel* channel() const { return m_pChannel; }
    int id() const { return m_iID; }
    sockaddr_any selfAddr() const { return m_SelfAddr; }
    const CSrtMuxerConfig& cfg() const { return m_mcfg; }

    void setClosing()
    {
        m_SndQueue.setClosing();
        m_RcvQueue.setClosing();
    }

    // This function checks if the current thread isn't any of
    // the worker threads. If it is, destruction of a multiplexer
    // must be rejected. This may still happen later in the GC,
    // but synchronous closing in this situation is simply not possible.
    bool isSelfDestructAttempt()
    {
        return sync::this_thread_is(m_SndQueue.m_WorkerThread)
            || sync::this_thread_is(m_RcvQueue.m_WorkerThread);
    }

    void stopWorkers()
    {
        m_SndQueue.stop();
        m_RcvQueue.stop();
    }

    // This call attempts to reserve the disposal action to the
    // current thread. This is successful, if the disposal reservation
    // has not been set. If it was set, reservation fails, and this
    // function returns false; in this case the thread that attempted
    // the reservation shall not try to access this multiplexer after
    // it releases m_GlobControlLock. If reservation succeeds, the thread
    // that attempted the reservation is obliged to call stopWorkers()
    // to make sure that all threads using this multiplexer have exit
    // (with m_GlobControlLock lifted for that action), and then delete it,
    // under restored m_GlobControlLock.
    bool reserveDisposal();

    // For testing
    std::string testAllSocketsClear();

    bool addSocket(CUDTSocket* s);
    bool deleteSocket(SRTSOCKET id);
    bool setConnected(SRTSOCKET id);
    bool setBroken(SRTSOCKET id);

    SRT_TSA_NEEDS_LOCKED(m_SocketsLock)
    bool setBrokenInternal(SRTSOCKET id);

    void setBrokenDirect(sockiter_t);

    CUDTSocket* findAgent(SRTSOCKET id, const sockaddr_any& remote_addr, SocketHolder::State& w_state, AcquisitionControl acq = ACQ_RELAXED);
    CUDTSocket* findPeer(SRTSOCKET id, const sockaddr_any& remote_addr, AcquisitionControl acq = ACQ_RELAXED);

    /// @brief Remove a socket from the connection pending list.
    /// @param id socket ID.
    void removeRID(const SRTSOCKET& id);

    void expirePending(SocketHolder& sh);

    bool qualifyToHandleRID(EReadStatus                  rst,
                         EConnectStatus               cst,
                         SRTSOCKET                    iDstSockID,
                         std::vector<LinkStatusInfo>& toRemove,
                         std::vector<LinkStatusInfo>& toProcess);

    /// @brief Locate a socket in the connection pending queue.
    /// @param addr source address of the packet received over UDP (peer address).
    /// @param id socket ID.
    /// @return a pointer to CUDT instance retrieved, or NULL if nothing was found.
    CUDT* retrieveRID(const sockaddr_any& addr, SRTSOCKET id) const;

    void resetExpiredRID(const std::vector<LinkStatusInfo>& toRemove);
    void registerCRL(const CRL& setup);
    void removeConnector(const SRTSOCKET& id) { return m_RcvQueue.removeConnector(id); }
    void setReceiver(CUDT* u);

    // Order handling
    void updateUpdateOrder(SRTSOCKET id, const sync::steady_clock::time_point& tnow);
    void rollUpdateSockets(const sync::steady_clock::time_point& tnow_minus_syn);

    CUnitQueue* getBufferQueue() { return m_RcvQueue.m_pUnitQueue; }

    // Constructor should reset all pointers to NULL
    // to prevent dangling pointer when checking for memory alloc fails
#if HAVE_CXX11

    CMultiplexer()
        : m_iID(-1)
        , m_SndQueue(this)
        , m_RcvQueue(this)
        , m_pChannel(NULL)
        , m_ReservedDisposal()
    {
        m_SocketMap.reserve(1024); // reserve buckets - std::unordered_map version
    }

#else

    CMultiplexer()
        : m_iID(-1)
        , m_SocketMap(1024) // reserve buckets - gnu::hash_map version
        , m_SndQueue(this)
        , m_RcvQueue(this)
        , m_pChannel(NULL)
        , m_ReservedDisposal()
    {
    }

    // "Copying" means to create an empty multiplexer. You can't
    // copy a multiplexer; copying is only formally required to
    // fulfill the Copiable requirements so that it can be used
    // in containers. This is required for map::operator[], and
    // CopyConstructible requirement for a mapped type is lifted
    // only in C++11.
    CMultiplexer(const CMultiplexer&)
        : m_iID(-1)
        , m_SocketMap(1024) // reserve buckets - gnu::hash_map version
        , m_SndQueue(this)
        , m_RcvQueue(this)
        , m_pChannel(NULL)
        , m_ReservedDisposal()
    {
    }
#endif

    void resetAtFork();
    void close();
    void stop();
    ~CMultiplexer();

    bool removeListener(CUDT* u) { return m_RcvQueue.removeListener(u); }
    int setListener(CUDT* u) { return m_RcvQueue.setListener(u); }
    CUDT* getListener() { return m_RcvQueue.getListener(); }

    void configure(int32_t id, const CSrtConfig& config, const sockaddr_any& reqaddr, const UDPSOCKET* udpsock);

    // Update the socket in the sender list according to current time.
    // Already scheduled sockets with future time will be ordered after it.
    sync::steady_clock::time_point updateSendNormal(CUDTSocket* s);

    // Update the socket in the sender list with high priority (should
    // precede everything that is in the list, except earlier added high
    // priority packets).
    void updateSendFast(CUDTSocket* s);

    void removeSender(CUDT* u);
};

} // namespace srt

#endif
