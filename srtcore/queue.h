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

struct CSNode
{
    CUDT*                          m_pUDT; // Pointer to the instance of CUDT socket
    sync::steady_clock::time_point m_tsTimeStamp;

    sync::atomic<int> m_iHeapLoc; // location on the heap, -1 means not on the heap
};

class CSndUList
{
public:
    CSndUList(sync::CTimer* pTimer);
    ~CSndUList();

public:
    enum EReschedule
    {
        DONT_RESCHEDULE = 0,
        DO_RESCHEDULE   = 1
    };

    static EReschedule rescheduleIf(bool cond) { return cond ? DO_RESCHEDULE : DONT_RESCHEDULE; }

    /// Update the timestamp of the UDT instance on the list.
    /// @param [in] u pointer to the UDT instance
    /// @param [in] reschedule if the timestamp should be rescheduled
    /// @param [in] ts the next time to trigger sending logic on the CUDT
    void update(const CUDT* u, EReschedule reschedule, sync::steady_clock::time_point ts = sync::steady_clock::now());

    /// Retrieve the next (in time) socket from the heap to process its sending request.
    /// @return a pointer to CUDT instance to process next.
    CUDT* pop();

    /// Remove UDT instance from the list.
    /// @param [in] u pointer to the UDT instance
    void remove(const CUDT* u);// EXCLUDES(m_ListLock);

    /// Retrieve the next scheduled processing time.
    /// @return Scheduled processing time of the first UDT socket in the list.
    sync::steady_clock::time_point getNextProcTime();

    /// Wait for the list to become non empty.
    void waitNonEmpty() const;

    /// Signal to stop waiting in waitNonEmpty().
    void signalInterrupt() const;

private:
    /// Doubles the size of the list.
    ///
    void realloc_();// REQUIRES(m_ListLock);

    /// Insert a new UDT instance into the list with realloc if required.
    ///
    /// @param [in] ts time stamp: next processing time
    /// @param [in] u pointer to the UDT instance
    void insert_(const sync::steady_clock::time_point& ts, const CUDT* u);

    /// Insert a new UDT instance into the list without realloc.
    /// Should be called if there is a guaranteed space for the element.
    ///
    /// @param [in] ts time stamp: next processing time
    /// @param [in] u pointer to the UDT instance
    void insert_norealloc_(const sync::steady_clock::time_point& ts, const CUDT* u);// REQUIRES(m_ListLock);

    /// Removes CUDT entry from the list.
    /// If the last entry is removed, calls sync::CTimer::interrupt().
    void remove_(const CUDT* u);

private:
    CSNode** m_pHeap;        // The heap array
    int      m_iArrayLength; // physical length of the array
    int      m_iLastEntry;   // position of last entry on the heap array or -1 if empty.

    mutable sync::Mutex     m_ListLock; // Protects the list (m_pHeap, m_iArrayLength, m_iLastEntry).
    mutable sync::Condition m_ListCond;

    sync::CTimer* const m_pTimer;

private:
    CSndUList(const CSndUList&);
    CSndUList& operator=(const CSndUList&);
};

struct CRNode
{
    CUDT*                          m_pUDT;        // Pointer to the instance of CUDT socket
    sync::steady_clock::time_point m_tsTimeStamp; // Time Stamp

    CRNode* m_pPrev; // previous link
    CRNode* m_pNext; // next link

    sync::atomic<bool> m_bOnList; // if the node is already on the list
};

class CRcvUList
{
public:
    CRcvUList();
    ~CRcvUList();

public:
    /// Insert a new UDT instance to the list.
    /// @param [in] u pointer to the UDT instance

    void insert(const CUDT* u);

    /// Remove the UDT instance from the list.
    /// @param [in] u pointer to the UDT instance

    void remove(const CUDT* u);

    /// Move the UDT instance to the end of the list, if it already exists; otherwise, do nothing.
    /// @param [in] u pointer to the UDT instance

    void update(const CUDT* u);

public:
    CRNode* m_pUList; // the head node

private:
    CRNode* m_pLast; // the last node

private:
    CRcvUList(const CRcvUList&);
    CRcvUList& operator=(const CRcvUList&);
};

class CHash
{
public:
    CHash();
    ~CHash();

public:
    /// Initialize the hash table.
    /// @param [in] size hash table size

    void init(int size);

    /// Look for a UDT instance from the hash table.
    /// @param [in] id socket ID
    /// @return Pointer to a UDT instance, or NULL if not found.

    CUDT* lookup(SRTSOCKET id);

     /// Look for a UDT instance from the hash table by source ID
     /// @param [in] peerid socket ID of the peer reported as source ID
     /// @return Pointer to a UDT instance where m_PeerID == peerid, or NULL if not found

    CUDT* lookupPeer(SRTSOCKET peerid);

    /// Insert an entry to the hash table.
    /// @param [in] id socket ID
    /// @param [in] u pointer to the UDT instance

    void insert(SRTSOCKET id, CUDT* u);

    /// Remove an entry from the hash table.
    /// @param [in] id socket ID

    void remove(SRTSOCKET id);

private:
    struct CBucket
    {
        SRTSOCKET m_iID;  // Socket ID
        SRTSOCKET m_iPeerID;    // Peer ID
        CUDT*   m_pUDT; // Socket instance

        CBucket* m_pNext; // next bucket
    } * *m_pBucket;       // list of buckets (the hash table)

    int m_iHashSize; // size of hash table

    std::map<SRTSOCKET, SRTSOCKET> m_RevPeerMap;

    CBucket*& bucketAt(SRTSOCKET id)
    {
        return m_pBucket[int32_t(id) % m_iHashSize];
    }

private:
    CHash(const CHash&);
    CHash& operator=(const CHash&);
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

private:
    static void* worker_fwd(void* param)
    {
        CSndQueue* self = (CSndQueue*)param;
        self->worker();
        return NULL;
    }

    void worker();
    sync::CThread m_WorkerThread;

private:
    CSndUList*    m_pSndUList; // List of UDT instances for data sending
    CChannel*     m_pChannel;  // The UDP channel for data sending
    sync::CTimer  m_Timer;    // Timing facility

    sync::atomic<bool> m_bClosing;            // closing the worker

public:

    void tick() { return m_Timer.tick(); }

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

#if ENABLE_LOGGING
    static srt::sync::atomic<int> m_counter;
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

    void stopWorker();

    void setClosing() { m_bClosing = true; }

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
    CRcvUList*    m_pRcvUList;  // List of UDT instances that will read packets from the queue
    CChannel*     m_pChannel;   // UDP channel for receiving packets

    size_t m_szPayloadSize;     // packet payload size

    sync::atomic<bool> m_bClosing; // closing the worker
#if ENABLE_LOGGING
    static srt::sync::atomic<int> m_counter; // A static counter to log RcvQueue worker thread number.
#endif

private:
    int  setListener(CUDT* u);
    void removeListener(const CUDT* u);

    // UNUSED. Changed to CMultiplexer::setReceiver
    void  setNewEntry(CUDT* u);

    void storePktClone(SRTSOCKET id, const CPacket& pkt);

    void kick();


    /// @brief Update status of connections in the pending queue.
    /// Stop connecting if TTL expires. Resend handshake request every 250 ms if no response from the peer.
    /// @param rst result of reading from a UDP socket: received packet / nothin read / read error.
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

struct SocketHolder
{
    enum State
    {
        NONEXISTENT = -2,
        BROKEN = -1,
        INIT = 0,
        PENDING = 1,
        ACTIVE = 2
    };

    static std::string StateStr(State);

    State m_State;
    class CUDTSocket* m_pSocket;

    // Time when the connection request should expire. Contains zero,
    // if there was no request.
    sync::steady_clock::time_point m_tsRequestTTL;

    // SRT connection peer address
    sockaddr_any m_PeerAddr;

    // Time when the socket needs to be picked up for update.
    sync::steady_clock::time_point m_tsUpdateTime;

    // Time when sending through this socket should happen.
    sync::steady_clock::time_point m_tsSendTime;

    SocketHolder():
        m_State(INIT),
        m_pSocket(NULL),
        //m_PeerID(SRT_INVALID_SOCK),
        m_tsUpdateTime()
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

struct CMultiplexer
{
    typedef std::list<SocketHolder> socklist_t;
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

private:

    mutable sync::Mutex m_SocketsLock;

    socklist_t m_Sockets;
    sockmap_t m_SocketMap;

    std::map<SRTSOCKET, SRTSOCKET> m_RevPeerMap;
    sync::atomic<size_t> m_zSockets;

    CSndQueue     m_SndQueue; // The sending queue
    CRcvQueue     m_RcvQueue; // The receiving queue
    CChannel*     m_pChannel;  // The UDP channel for sending and receiving

    sockaddr_any m_SelfAddr;

    CSrtMuxerConfig m_mcfg;

    int m_iID; // multiplexer ID

public:

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

    // For testing
    std::string testAllSocketsClear();

    bool addSocket(CUDTSocket* s);
    bool deleteSocket(SRTSOCKET id);
    bool setConnected(SRTSOCKET id);
    bool setBroken(SRTSOCKET id);
    CUDTSocket* findAgent(SRTSOCKET id, const sockaddr_any& remote_addr, SocketHolder::State& w_state);
    CUDTSocket* findPeer(SRTSOCKET id, const sockaddr_any& remote_addr);

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

    CUnitQueue* getBufferQueue() { return m_RcvQueue.m_pUnitQueue; }

    // Constructor should reset all pointers to NULL
    // to prevent dangling pointer when checking for memory alloc fails
    CMultiplexer()
        : m_SndQueue(this)
        , m_RcvQueue(this)
        , m_pChannel(NULL)
        , m_iID(-1)
    {
    }

    ~CMultiplexer();

    //CSndUList* sndUList() { return m_SndQueue.m_pSndUList; }

    void removeListener(const CUDT* u) { return m_RcvQueue.removeListener(u); }
    int setListener(CUDT* u) { return m_RcvQueue.setListener(u); }

    void configure(int32_t id, const CSrtConfig& config, const sockaddr_any& reqaddr, const UDPSOCKET* udpsock);

    // Update the socket in the sender list according to current time.
    // Already scheduled sockets with future time will be ordered after it.
    sync::steady_clock::time_point updateSendNormal(CUDTSocket* s);

    // Update the socket in the sender list with high priority (should
    // precede everything that is in the list, except earlier added high
    // priority packets).
    void updateSendFast(CUDTSocket* s);

    void tickSender() { return m_SndQueue.tick(); }

    void removeSender(CUDT* u)
    {
        m_SndQueue.m_pSndUList->remove(u);
    }
};

} // namespace srt

#endif
