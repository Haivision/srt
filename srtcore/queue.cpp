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
   Yunhong Gu, last updated 05/05/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <cstring>

#include "common.h"
#include "api.h"
#include "netinet_any.h"
#include "threadname.h"
#include "logging.h"
#include "queue.h"

using namespace std;
using namespace srt::sync;
using namespace srt_logging;

namespace srt
{

CUnitQueue::CUnitQueue(int initNumUnits, int mss)
    : m_iNumTaken(0)
    , m_iMSS(mss)
    , m_iBlockSize(initNumUnits)
{
    CQEntry* tempq = allocateEntry(m_iBlockSize, m_iMSS);

    if (tempq == NULL)
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY);

    m_pQEntry = m_pCurrQueue = m_pLastQueue = tempq;
    m_pQEntry->m_pNext = m_pQEntry;

    m_pAvailUnit = m_pCurrQueue->m_pUnit;

    m_iSize = m_iBlockSize;
}

CUnitQueue::~CUnitQueue()
{
    CQEntry* p = m_pQEntry;

    while (p != NULL)
    {
        delete[] p->m_pUnit;
        delete[] p->m_pBuffer;

        CQEntry* q = p;
        if (p == m_pLastQueue)
            p = NULL;
        else
            p = p->m_pNext;
        delete q;
    }
}

CUnitQueue::CQEntry* CUnitQueue::allocateEntry(const int iNumUnits, const int mss)
{
    CQEntry* tempq = NULL;
    CUnit* tempu   = NULL;
    char* tempb    = NULL;

    try
    {
        tempq = new CQEntry;
        tempu = new CUnit[iNumUnits];
        tempb = new char[iNumUnits * mss];
    }
    catch (...)
    {
        delete tempq;
        delete[] tempu;
        delete[] tempb;

        LOGC(rslog.Error, log << "CUnitQueue: failed to allocate " << iNumUnits << " units.");
        return NULL;
    }

    for (int i = 0; i < iNumUnits; ++i)
    {
        tempu[i].m_bTaken = false;
        tempu[i].m_Packet.m_pcData = tempb + i * mss;
    }

    tempq->m_pUnit   = tempu;
    tempq->m_pBuffer = tempb;
    tempq->m_iSize   = iNumUnits;

    return tempq;
}

int CUnitQueue::increase_()
{
    const int numUnits = m_iBlockSize;
    HLOGC(qrlog.Debug, log << "CUnitQueue::increase: Capacity" << capacity() << " + " << numUnits << " new units, " << m_iNumTaken << " in use.");

    CQEntry* tempq = allocateEntry(numUnits, m_iMSS);
    if (tempq == NULL)
        return -1;

    m_pLastQueue->m_pNext = tempq;
    m_pLastQueue          = tempq;
    m_pLastQueue->m_pNext = m_pQEntry;

    m_iSize += numUnits;

    return 0;
}

CUnit* CUnitQueue::getNextAvailUnit()
{
    const int iNumUnitsTotal = capacity();
    if (m_iNumTaken * 10 > iNumUnitsTotal * 9) // 90% or more are in use.
        increase_();

    if (m_iNumTaken >= capacity())
    {
        LOGC(qrlog.Error, log << "CUnitQueue: No free units to take. Capacity" << capacity() << ".");
        return NULL;
    }

    int units_checked = 0;
    do
    {
        const CUnit* end = m_pCurrQueue->m_pUnit + m_pCurrQueue->m_iSize;
        for (; m_pAvailUnit != end; ++m_pAvailUnit, ++units_checked)
        {
            if (!m_pAvailUnit->m_bTaken)
            {
                return m_pAvailUnit;
            }
        }

        m_pCurrQueue = m_pCurrQueue->m_pNext;
        m_pAvailUnit = m_pCurrQueue->m_pUnit;
    } while (units_checked < m_iSize);

    return NULL;
}

void CUnitQueue::makeUnitFree(CUnit* unit)
{
    SRT_ASSERT(unit != NULL);
    SRT_ASSERT(unit->m_bTaken);
    unit->m_bTaken.store(false);

    --m_iNumTaken;
}

void CUnitQueue::makeUnitTaken(CUnit* unit)
{
    ++m_iNumTaken;

    SRT_ASSERT(unit != NULL);
    SRT_ASSERT(!unit->m_bTaken);
    unit->m_bTaken.store(true);
}

CSndUList::CSndUList(sync::CTimer* pTimer)
    : m_pHeap(NULL)
    , m_iCapacity(512)
    , m_iLastEntry(-1)
    , m_ListLock()
    , m_pTimer(pTimer)
{
    setupCond(m_ListCond, "CSndUListCond");
    m_pHeap = new CSNode*[m_iCapacity];
}

CSndUList::~CSndUList()
{
    releaseCond(m_ListCond);
    delete[] m_pHeap;
}

bool CSndUList::update(const CUDT* u, EReschedule reschedule, sync::steady_clock::time_point ts)
{
    ScopedLock listguard(m_ListLock);

    CSNode* n = u->m_pSNode;

    IF_HEAVY_LOGGING(sync::steady_clock::time_point now = sync::steady_clock::now());
    IF_HEAVY_LOGGING(std::ostringstream nowrel, oldrel);
    IF_HEAVY_LOGGING(nowrel << " = now" << showpos << (ts - now).count() << "us");

    if (n->m_iHeapLoc >= 0)
    {
        IF_HEAVY_LOGGING(oldrel << " = now" << showpos << (n->m_tsTimeStamp - now).count() << "us");
        if (reschedule == DONT_RESCHEDULE)
        {
            HLOGC(qslog.Debug, log << "CSndUList: UPDATE: NOT rescheduling @" << u->id()
                    << " - remains T=" << FormatTime(n->m_tsTimeStamp) << oldrel.str());
            return false;
        }

        if (n->m_tsTimeStamp <= ts)
        {
            HLOGC(qslog.Debug, log << "CSndUList: UPDATE: NOT rescheduling @" << u->id()
                    << " to +" << FormatDurationAuto(ts - n->m_tsTimeStamp)
                    << " - remains T=" << FormatTime(n->m_tsTimeStamp) << oldrel.str());
            return false;
        }

        HLOGC(qslog.Debug, log << "CSndUList: UPDATE: rescheduling @" << u->id() << " T=" << FormatTime(n->m_tsTimeStamp)
                << nowrel.str() << " - speedup by " << FormatDurationAuto(n->m_tsTimeStamp - ts));

        // Special case for the first element - no replacement needed, just update.
        if (n->m_iHeapLoc == 0)
        {
            n->m_tsTimeStamp = ts;
            m_pTimer->interrupt();
            return true;
        }

        remove_(n);
        insert_norealloc_(ts, n);
        return true;
    }
    else
    {
        HLOGC(qslog.Debug, log << "CSndUList: UPDATE: inserting @" << u->id() << " anew T=" << FormatTime(ts) << nowrel.str());
    }

    insert_(ts, n);
    return true;
}

CUDT* CSndUList::pop()
{
    ScopedLock listguard(m_ListLock);

    if (-1 == m_iLastEntry)
    {
        HLOGC(qslog.Debug, log << "CSndUList: POP: empty");
        return NULL;
    }

    // no pop until the next scheduled time
    steady_clock::time_point now = steady_clock::now();
    if (m_pHeap[0]->m_tsTimeStamp > now)
    {
        HLOGC(qslog.Debug, log << "CSndUList: POP: T=" << FormatTime(m_pHeap[0]->m_tsTimeStamp) << " too early, next in " << FormatDurationAuto(m_pHeap[0]->m_tsTimeStamp - now));
        return NULL;
    }

    CUDT* u = m_pHeap[0]->m_pUDT;
    HLOGC(qslog.Debug, log << "CSndUList: POP: extracted @" << u->id());
    remove_(m_pHeap[0]);
    return u;
}

void CSndUList::remove(CSNode* n)
{
    ScopedLock listguard(m_ListLock);
    remove_(n);
}

void CSndUList::remove(const CUDT* u) { remove(u->m_pSNode); }

steady_clock::time_point CSndUList::getNextProcTime()
{
    ScopedLock listguard(m_ListLock);

    if (-1 == m_iLastEntry)
        return steady_clock::time_point();

    return m_pHeap[0]->m_tsTimeStamp;
}

void CSndUList::waitNonEmpty() const
{
    UniqueLock listguard(m_ListLock);
    if (m_iLastEntry >= 0)
        return;

    m_ListCond.wait(listguard);
}

CSNode* CSndUList::wait()
{
    CUniqueSync lg (m_ListLock, m_ListCond);

    bool signaled = false;
    for (;;)
    {
        sync::steady_clock::time_point uptime;
        if (m_iLastEntry > -1)
        {
            // Have at least one element in the list.
            // Check if the ship time is in the past
            if (m_pHeap[0]->m_tsTimeStamp < sync::steady_clock::now())
                return m_pHeap[0];
            uptime = m_pHeap[0]->m_tsTimeStamp;
            signaled = false;
        }
        else if (signaled)
        {
            return NULL;
        }
        // If not, continue waiting. Wait indefinitely if no time.
        // Hangup prevention should be provided by having a certain
        // interrupt request when closing a socket.

        if (is_zero(uptime))
        {
            signaled = true;
            lg.wait();
        }
        else
        {
            signaled = lg.wait_until(uptime);
        }
    }
}

CSNode* CSndUList::peek() const
{
    ScopedLock listguard(m_ListLock);
    if (m_iLastEntry == -1)
        return NULL;

    if (m_pHeap[0]->m_tsTimeStamp > sync::steady_clock::now())
        return NULL;

    return m_pHeap[0];
}

bool CSndUList::requeue(CSNode* node, const sync::steady_clock::time_point& uptime)
{
    ScopedLock listguard(m_ListLock);

    // Should be.
    if (!node->pinned())
    {
        insert_(uptime, node);
        return node == top();
    }

    if (m_iLastEntry == 0) // exactly one element; use short path
    {
        node->m_tsTimeStamp = uptime;

        // Return true to declare that the top element was updated,
        // but don't do anything additionally, as this function is
        // to be used in the same thread that calls wait().
        return true;
    }

    // Otherwise you need to remove the node and re-add it.
    remove_(node);
    insert_norealloc_(uptime, node);
    return node == top();
}

void CSndUList::signalInterrupt() const
{
    ScopedLock listguard(m_ListLock);
    m_ListCond.notify_one();
}

void CSndUList::realloc_()
{
    CSNode** temp = NULL;

    try
    {
        temp = new CSNode*[2 * m_iCapacity];
    }
    catch (...)
    {
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }

    memcpy((temp), m_pHeap, sizeof(CSNode*) * m_iCapacity);
    m_iCapacity *= 2;
    delete[] m_pHeap;
    m_pHeap = temp;
}

void CSndUList::insert_(const steady_clock::time_point& ts, CSNode* n)
{
    // increase the heap array size if necessary
    bool do_realloc = (m_iLastEntry == m_iCapacity - 1);
    if (do_realloc)
        realloc_();

    HLOGC(qslog.Debug, log << "CSndUList: inserting new @" << n->m_pUDT->id() << (do_realloc ? " (EXTENDED)" : ""));
    insert_norealloc_(ts, n);
}

void CSndUList::insert_norealloc_(const steady_clock::time_point& ts, CSNode* n)
{
    // do not insert repeated node
    if (n->m_iHeapLoc >= 0)
        return;

    SRT_ASSERT(m_iLastEntry < m_iCapacity);

    m_iLastEntry++;
    m_pHeap[m_iLastEntry] = n;
    n->m_tsTimeStamp      = ts;

    int q = m_iLastEntry;
    int p = q;
    while (p != 0)
    {
        p = parent(q);
        if (m_pHeap[p]->m_tsTimeStamp <= m_pHeap[q]->m_tsTimeStamp)
            break;

        swap(m_pHeap[p], m_pHeap[q]);
        m_pHeap[q]->m_iHeapLoc = q;
        q                      = p;
    }
    HLOGC(qslog.Debug, log << "CSndUList: inserted @" << n->m_pUDT->id() << " loc=" << q);

    n->m_iHeapLoc = q;

    // an earlier event has been inserted, wake up sending worker
    if (n->m_iHeapLoc == 0)
        m_pTimer->interrupt();

    // first entry, activate the sending queue
    if (0 == m_iLastEntry)
    {
        // m_ListLock is assumed to be locked.
        m_ListCond.notify_one();
    }
}

void CSndUList::remove_(CSNode* n)
{
    if (n->m_iHeapLoc >= 0)
    {
        // remove the node from heap
        m_pHeap[n->m_iHeapLoc] = m_pHeap[m_iLastEntry];
        m_iLastEntry--;
        m_pHeap[n->m_iHeapLoc]->m_iHeapLoc = n->m_iHeapLoc.load();

        int q = n->m_iHeapLoc;
        int p = q * 2 + 1;
        while (p <= m_iLastEntry)
        {
            if ((p + 1 <= m_iLastEntry) && (m_pHeap[p]->m_tsTimeStamp > m_pHeap[p + 1]->m_tsTimeStamp))
                p++;

            if (m_pHeap[q]->m_tsTimeStamp > m_pHeap[p]->m_tsTimeStamp)
            {
                swap(m_pHeap[p], m_pHeap[q]);
                m_pHeap[p]->m_iHeapLoc = p;
                m_pHeap[q]->m_iHeapLoc = q;

                q = p;
                p = q * 2 + 1;
            }
            else
                break;
        }

        HLOGC(qslog.Debug, log << "CSndUList: remove @" << n->m_pUDT->id() << " from pos=" << n->m_iHeapLoc
                << " last replaced into pos=" << q << " last=" << m_iLastEntry);
        n->m_iHeapLoc = -1;
    }
    else
    {
        HLOGC(qslog.Debug, log << "CSndUList: remove @" << n->m_pUDT->id() << ": NOT IN THE LIST");
    }

    // the only event has been deleted, wake up immediately
    if (0 == m_iLastEntry)
        m_pTimer->interrupt();
}

//
CSndQueue::CSndQueue(CMultiplexer* parent):
    m_parent(parent),
    m_pSndUList(NULL),
    m_pChannel(NULL),
    m_bClosing(false)
{
}

CSndQueue::~CSndQueue()
{
    m_bClosing = true;

    m_Timer.interrupt();

    // Unblock CSndQueue worker thread if it is waiting.
    m_pSndUList->signalInterrupt();

    if (m_WorkerThread.joinable())
    {
        HLOGC(rslog.Debug, log << "SndQueue: EXIT");
        m_WorkerThread.join();
    }

    delete m_pSndUList;
}

#if ENABLE_LOGGING
sync::atomic<int> CSndQueue::m_counter(0);
#endif

void CSndQueue::init(CChannel* c)
{
    m_pChannel  = c;
    m_pSndUList = new CSndUList(&m_Timer);

#if ENABLE_LOGGING
    ++m_counter;
    const std::string thrname = "SRT:SndQ:w" + Sprint(m_counter);
    const char*       thname  = thrname.c_str();
#else
    const char* thname = "SRT:SndQ";
#endif
    if (!StartThread((m_WorkerThread), CSndQueue::worker_fwd, this, thname))
    {
        throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
    }
}


#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
static void CSndQueueDebugHighratePrint(const CSndQueue* self, const steady_clock::time_point currtime)
{
    if (self->m_DbgTime <= currtime)
    {
        fprintf(stdout,
                "SndQueue %lu slt:%lu nrp:%lu snt:%lu nrt:%lu ctw:%lu\n",
                self->m_WorkerStats.lIteration,
                self->m_WorkerStats.lSleepTo,
                self->m_WorkerStats.lNotReadyPop,
                self->m_WorkerStats.lSendTo,
                self->m_WorkerStats.lNotReadyTs,
                self->m_WorkerStats.lCondWait);
        memset(&self->m_WorkerStats, 0, sizeof(self->m_WorkerStats));
        self->m_DbgTime = currtime + self->m_DbgPeriod;
    }
}
#endif

void CSndQueue::worker()
{
    std::string thname;
    ThreadName::get(thname);
    THREAD_STATE_INIT(thname.c_str());

    for (;;)
    {
        if (m_bClosing)
        {
            HLOGC(qslog.Debug, log << "SndQ: closed, exiting");
            break;
        }

        HLOGC(qslog.Debug, log << "SndQ: waiting to get next send candidate...");
        THREAD_PAUSED();
        CSNode* runner = m_pSndUList->wait();
        THREAD_RESUMED();

        INCREMENT_THREAD_ITERATIONS();

        if (!runner)
        {
            HLOGC(qslog.Debug, log << "SndQ: wait interrupted...");
            if (m_bClosing)
            {
                HLOGC(qslog.Debug, log << "SndQ: interrupted, closed, exitting");
                break;
            }

            // REPORT IPE???
            // wait() should not exit if it wasn't forcefully interrupted
            HLOGC(qslog.Debug, log << "SndQ: interrupted, SPURIOUS??? Repeating...");
            continue;
        }

        // Get a socket with a send request if any.
        CUDT* u = runner->m_pUDT;

        // Impossible, but whatever
        if (u == NULL)
        {
            LOGC(qslog.Error, log << "SndQ: IPE: EMPTY NODE");
            continue;
        }

#define UST(field) ((u->m_b##field) ? "+" : "-") << #field << " "
        HLOGC(qslog.Debug,
            log << "CSndQueue: requesting packet from @" << u->socketID() << " STATUS: " << UST(Listening)
                << UST(Connecting) << UST(Connected) << UST(Closing) << UST(Shutdown) << UST(Broken) << UST(PeerHealth)
                << UST(Opened));
#undef UST

        CUDTUnited::SocketKeeper sk (CUDT::uglobal(), u->id());
        if (!sk.socket)
        {
            HLOGC(qslog.Debug, log << "Socket to be processed was deleted in the meantime, not packing");
            continue;
        }

        if (!u->m_bConnected || u->m_bBroken)
        {
            HLOGC(qslog.Debug, log << "Socket to be processed is already broken, not packing");
            continue;
        }

        // pack a packet from the socket
        CPacket pkt;
        steady_clock::time_point next_send_time;
        CNetworkInterface source_addr;
        const bool res = u->packData((pkt), (next_send_time), (source_addr));

        // Check if extracted anything to send
        if (res == false)
        {
            HLOGC(qslog.Debug, log << "packData: nothing to send, WITHDRAWING sender");
            m_pSndUList->remove(runner);
            continue;
        }

        const sockaddr_any addr = u->m_PeerAddr;
        if (!is_zero(next_send_time))
        {
            m_pSndUList->requeue(runner, next_send_time);
            IF_HEAVY_LOGGING(sync::steady_clock::time_point now = sync::steady_clock::now());
            HLOGC(qslog.Debug, log << "SND updated to " << FormatTime(next_send_time)
                    << " (now" << showpos << (next_send_time - now).count() << "us)");
        }
        else
        {
            m_pSndUList->remove(runner);
        }

        HLOGC(qslog.Debug, log << CONID() << "chn:SENDING: " << pkt.Info());
        m_pChannel->sendto(addr, pkt, source_addr);
    }

    THREAD_EXIT();
}

//*
CRcvUList::CRcvUList()
    : m_pUList(NULL)
    , m_pLast(NULL)
{
}

CRcvUList::~CRcvUList() {}

void CRcvUList::insert(const CUDT* u)
{
    CRNode* n        = u->m_pRNode;
    SRT_ASSERT(n);
    n->m_tsTimeStamp = steady_clock::now();

    if (NULL == m_pUList)
    {
        // empty list, insert as the single node
        n->m_pPrev = n->m_pNext = NULL;
        m_pLast = m_pUList = n;

        return;
    }

    // always insert at the end for RcvUList
    n->m_pPrev       = m_pLast;
    n->m_pNext       = NULL;
    m_pLast->m_pNext = n;
    m_pLast          = n;

    // Set before calling. Without this it gets crashed
    // through having unopened CUDT added here...?
    // n->m_bOnList     = true;
}

void CRcvUList::remove(const CUDT* u)
{
    CRNode* n = u->m_pRNode;

    if (!n->m_bOnList)
        return;

    if (NULL == n->m_pPrev)
    {
        // n is the first node
        m_pUList = n->m_pNext;
        if (NULL == m_pUList)
            m_pLast = NULL;
        else
            m_pUList->m_pPrev = NULL;
    }
    else
    {
        n->m_pPrev->m_pNext = n->m_pNext;
        if (NULL == n->m_pNext)
        {
            // n is the last node
            m_pLast = n->m_pPrev;
        }
        else
            n->m_pNext->m_pPrev = n->m_pPrev;
    }

    n->m_pNext = n->m_pPrev = NULL;
}

void CRcvUList::update(const CUDT* u)
{
    CRNode* n = u->m_pRNode;

    if (!n->m_bOnList)
        return;

    n->m_tsTimeStamp = steady_clock::now();

    // if n is the last node, do not need to change
    if (NULL == n->m_pNext)
        return;

    if (NULL == n->m_pPrev)
    {
        m_pUList          = n->m_pNext;
        m_pUList->m_pPrev = NULL;
    }
    else
    {
        n->m_pPrev->m_pNext = n->m_pNext;
        n->m_pNext->m_pPrev = n->m_pPrev;
    }

    n->m_pPrev       = m_pLast;
    n->m_pNext       = NULL;
    m_pLast->m_pNext = n;
    m_pLast          = n;
}
// */

//
CHash::CHash()
    : m_pBucket(NULL)
    , m_iHashSize(0)
{
}

CHash::~CHash()
{
    for (int i = 0; i < m_iHashSize; ++i)
    {
        CBucket* b = m_pBucket[i];
        while (NULL != b)
        {
            CBucket* n = b->m_pNext;
            delete b;
            b = n;
        }
    }

    delete[] m_pBucket;
}

void CHash::init(int size)
{
    m_pBucket = new CBucket*[size];

    for (int i = 0; i < size; ++i)
        m_pBucket[i] = NULL;

    m_iHashSize = size;
}

CUDT* CHash::lookup(SRTSOCKET id)
{
    // simple hash function (% hash table size); suitable for socket descriptors
    CBucket* b = bucketAt(id);

    while (NULL != b)
    {
        if (id == b->m_iID)
            return b->m_pUDT;
        b = b->m_pNext;
    }

    return NULL;
}

CUDT* CHash::lookupPeer(SRTSOCKET peerid)
{
    // Decode back the socket ID if it has that peer
    SRTSOCKET id = map_get(m_RevPeerMap, peerid, SRT_INVALID_SOCK);
    if (id == SRT_INVALID_SOCK)
        return NULL; // no such peer id
    return lookup(id);
}

void CHash::insert(SRTSOCKET id, CUDT* u)
{
    CBucket* b = bucketAt(id);

    CBucket* n = new CBucket;
    n->m_iID   = id;
    n->m_iPeerID = u->peerID();
    n->m_pUDT  = u;
    n->m_pNext = b;

    bucketAt(id) = n;
    m_RevPeerMap[u->peerID()] = id;
}

void CHash::remove(SRTSOCKET id)
{
    CBucket* b = bucketAt(id);
    CBucket* p = NULL;

    while (NULL != b)
    {
        if (id == b->m_iID)
        {
            if (NULL == p)
                bucketAt(id) = b->m_pNext;
            else
                p->m_pNext = b->m_pNext;

            m_RevPeerMap.erase(b->m_iPeerID);
            delete b;

            return;
        }

        p = b;
        b = b->m_pNext;
    }
}

void CMultiplexer::removeRID(const SRTSOCKET& id)
{
    ScopedLock lkv(m_SocketsLock);

    for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++i)
    {
        if (i->m_iID == id)
        {
            expirePending(*i->m_it);
            m_lRendezvousID.erase(i);
            return;
        }
    }

    // In case it's not found in RID, simply mark it BROKEN in the muxer
    sockmap_t::iterator ish = m_SocketMap.find(id);
    if (ish == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "removeRID: IPE: @" << id << " not found (also among subscribed)");
        return;
    }
    HLOGC(qmlog.Debug, log << "removeRID: @" << id << " not found in RID, but found in muxer");
    expirePending(*ish->second);
}

void CMultiplexer::expirePending(SocketHolder& sh)
{
    // Removal from RID means that the socket is now connected.
    sh.setConnectedState();
    HLOGC(qmlog.Debug, log << "expirePending: expiring SH: " << sh.report());

    if (sh.peerID() != SRT_INVALID_SOCK)
        m_RevPeerMap.erase(sh.peerID());
}

void SocketHolder::setConnectedState()
{
    // Withdraws the state after connecting, but whether it's
    // connected or broken, it must be checked in the flags
    m_tsRequestTTL = sync::steady_clock::time_point();

    if (!m_pSocket)
    {
        m_State = BROKEN;
    }
    else
    {
        CUDT& u = m_pSocket->core();

        if (u.stillConnected())
        {
            m_State = ACTIVE;
        }
        else
        {
            m_State = BROKEN;
        }
    }
}

CUDT* CMultiplexer::retrieveRID(const sockaddr_any& addr, SRTSOCKET id) const
{
    ScopedLock vg(m_SocketsLock);

    IF_HEAVY_LOGGING(const char* const id_type = id == SRT_SOCKID_CONNREQ ? "A NEW CONNECTION" : "THIS ID" );

    // TODO: optimize search
    for (list<CRL>::const_iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++i)
    {
        if (i->m_PeerAddr == addr && ((id == SRT_SOCKID_CONNREQ) || (id == i->m_iID)))
        {
            // This procedure doesn't exactly respond to the original UDT idea.
            // As the "rendezvous queue" is used for handling rendezvous and
            // the caller sockets, the RID list should give up a socket entity
            // in the following cases:
            // 1. For THE SAME id as passed in w_id, respond always, as per a caller
            //    socket that is currently trying to connect and is managed with
            //    HS roundtrips in an event-style. Same for rendezvous.
            // 2. For the "connection request" ID=0 the found socket should be given up
            //    ONLY IF it is rendezvous. Normally ID=0 is only for listener as a
            //    connection request. But if there was a listener, then this function
            //    wouldn't even be called, as this case would be handled before trying
            //    to call this function.
            //
            // This means: if an incoming ID is 0, then this search should succeed ONLY
            // IF THE FOUND SOCKET WAS RENDEZVOUS.

            if (id == SRT_SOCKID_CONNREQ && !i->m_pUDT->m_config.bRendezvous)
            {
                HLOGC(cnlog.Debug,
                        log << "RID: found id @" << i->m_iID << " while looking for "
                        << id_type << " FROM " << i->m_PeerAddr.str()
                        << ", but it's NOT RENDEZVOUS, skipping");
                continue;
            }

            HLOGC(cnlog.Debug,
                    log << "RID: found id @" << i->m_iID << " while looking for "
                    << id_type << " FROM " << i->m_PeerAddr.str());
            return i->m_pUDT;
        }
    }

#if ENABLE_HEAVY_LOGGING
    std::ostringstream spec;
    if (id == SRT_SOCKID_CONNREQ)
        spec << "A NEW CONNECTION REQUEST";
    else
        spec << " AGENT @" << id;
    HLOGC(cnlog.Debug,
          log << "RID: NO CONNECTOR FOR ADR:" << addr.str() << " while looking for " << spec.str() << " ("
              << m_lRendezvousID.size() << " connectors total)");
#endif

    return NULL;
}

void CRcvQueue::updateConnStatus(EReadStatus rst, EConnectStatus cst, CUnit* unit)
{
    vector<LinkStatusInfo> toRemove, toProcess;

    const CPacket* pkt = unit ? &unit->m_Packet : NULL;

    // Need a stub value for a case when there's no unit provided ("storage depleted" case).
    // It should be normally NOT IN USE because in case of "storage depleted", rst != RST_OK.
    const SRTSOCKET dest_id = pkt ? pkt->id() : SRT_SOCKID_CONNREQ;

    // If no socket were qualified for further handling, finish here.
    // Otherwise toRemove and toProcess contain items to handle.
    if (!m_parent->qualifyToHandleRID(rst, cst, dest_id, (toRemove), (toProcess)))
        return;

    HLOGC(cnlog.Debug,
          log << "updateConnStatus: collected " << toProcess.size() << " for processing, " << toRemove.size()
              << " to close");

    // Repeat (resend) connection request.
    for (vector<LinkStatusInfo>::iterator i = toProcess.begin(); i != toProcess.end(); ++i)
    {
        // IMPORTANT INFORMATION concerning changes towards UDT legacy.
        // In the UDT code there was no attempt to interpret any incoming data.
        // All data from the incoming packet were considered to be already deployed into
        // m_ConnRes field, and m_ConnReq field was considered at this time accordingly updated.
        // Therefore this procedure did only one thing: craft a new handshake packet and send it.
        // In SRT this may also interpret extra data (extensions in case when Agent is Responder)
        // and the `pktIn` packet may sometimes contain no data. Therefore the passed `rst`
        // must be checked to distinguish the call by periodic update (RST_AGAIN) from a call
        // due to have received the packet (RST_OK).
        //
        // In the below call, only the underlying `processRendezvous` function will be attempting
        // to interpret these data (for caller-listener this was already done by `processConnectRequest`
        // before calling this function), and it checks for the data presence.

        EReadStatus    read_st = rst;
        EConnectStatus conn_st = cst;

        CUDTUnited::SocketKeeper sk (CUDT::uglobal(), i->id);
        if (!sk.socket)
        {
            // Socket deleted already, so stop this and proceed to the next loop.
            LOGC(cnlog.Error, log << "updateConnStatus: IPE: socket @" << i->id << " already closed, proceed to only removal from lists");
            toRemove.push_back(*i);
            continue;
        }


        if (cst != CONN_RENDEZVOUS && dest_id != SRT_SOCKID_CONNREQ)
        {
            if (i->id != dest_id)
            {
                HLOGC(cnlog.Debug, log << "updateConnStatus: cst=" << ConnectStatusStr(cst) << " but for RID @" << i->id
                        << " dest_id=@" << dest_id << " - resetting to AGAIN");

                read_st = RST_AGAIN;
                conn_st = CONN_AGAIN;
            }
            else
            {
                HLOGC(cnlog.Debug, log << "updateConnStatus: cst=" << ConnectStatusStr(cst) << " for @"
                        << i->id);
            }
        }
        else
        {
            HLOGC(cnlog.Debug, log << "updateConnStatus: cst=" << ConnectStatusStr(cst) << " and dest_id=@" << dest_id
                    << " - NOT checking against RID @" << i->id);
        }

        HLOGC(cnlog.Debug,
              log << "updateConnStatus: processing async conn for @" << i->id << " FROM " << i->peeraddr.str());

        if (!i->u->processAsyncConnectRequest(read_st, conn_st, pkt, i->peeraddr))
        {
            // cst == CONN_REJECT can only be result of worker_ProcessAddressedPacket and
            // its already set in this case.
            LinkStatusInfo fi = *i;
            fi.errorcode      = SRT_ECONNREJ;
            toRemove.push_back(fi);
            uint32_t res[1] = {SRT_CLS_DEADLSN};
            i->u->sendCtrl(UMSG_SHUTDOWN, NULL, res, sizeof res);
        }
    }

    // NOTE: it is "believed" here that all CUDT objects will not be
    // deleted in the meantime. This is based on a statement that at worst
    // they have been "just" declared failed and it will pass at least 1s until
    // they are moved to ClosedSockets and it is believed that this function will
    // not be held on mutexes that long.

    for (vector<LinkStatusInfo>::iterator i = toRemove.begin(); i != toRemove.end(); ++i)
    {
        HLOGC(cnlog.Debug, log << "updateConnStatus: COMPLETING dep objects update on failed @" << i->id);
        // Likely not necessary here - already removed by expiring and then in qualifyToHandleRID().
        // m_parent->removeRID(i->id);

        CUDTUnited::SocketKeeper sk (CUDT::uglobal(), i->id);
        if (!sk.socket)
        {
            // This actually shall never happen, so it's a kind of paranoid check.
            LOGC(cnlog.Error, log << "updateConnStatus: IPE: socket @" << i->id << " already closed, NOT ACCESSING its contents");
            continue;
        }

        // Setting m_bConnecting to false, and need to remove the socket from the rendezvous queue
        // because the next CUDT::close will not remove it from the queue when m_bConnecting = false,
        // and may crash on next pass.
        //
        // TODO: maybe lock i->u->m_ConnectionLock?
        i->u->m_bConnecting = false;

        // DO NOT close the socket here because in this case it might be
        // unable to get status from at the right moment. Also only member
        // sockets should be taken care of internally - single sockets should
        // be normally closed by the application, after it is done with them.

        // app can call any UDT API to learn the connection_broken error
        CUDT::uglobal().m_EPoll.update_events(
            i->u->m_SocketID, i->u->m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR, true);

        // Make sure that the socket wasn't deleted in the meantime.
        // Skip this part if it was. Note also that if the socket was
        // decided to be deleted, it's already moved to m_ClosedSockets
        // and should have been therefore already processed for deletion.

        i->u->completeBrokenConnectionDependencies(i->errorcode);
    }

    m_parent->resetExpiredRID(toRemove);
}

void CMultiplexer::resetExpiredRID(const std::vector<LinkStatusInfo>& toRemove)
{
    // Now, additionally for every failed link reset the TTL so that
    // they are set expired right now.
    ScopedLock vg(m_SocketsLock);
    for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++i)
    {
        if (find_if(toRemove.begin(), toRemove.end(), LinkStatusInfo::HasID(i->m_iID)) != toRemove.end())
        {
            LOGC(cnlog.Error, log << "updateConnStatus: processAsyncConnectRequest FAILED on @" << i->m_iID
                                  << ". Setting TTL as EXPIRED.");
            i->m_tsTTL = steady_clock::time_point(); // Make it expire right now, will be picked up at the next iteration
        }
    }
}

// Must be defined here due to implementation dependency
SRTSOCKET SocketHolder::id() const { return m_pSocket->core().id(); }
SRTSOCKET SocketHolder::peerID() const { return m_pSocket->core().peerID(); }
sockaddr_any SocketHolder::peerAddr() const { return m_pSocket->core().peerAddr(); }

bool CMultiplexer::qualifyToHandleRID(EReadStatus    rst,
                                       EConnectStatus cst      SRT_ATR_UNUSED,
                                       SRTSOCKET               iDstSockID,
                                       vector<LinkStatusInfo>& toRemove,
                                       vector<LinkStatusInfo>& toProcess)
{
    ScopedLock vg(m_SocketsLock);

    if (m_lRendezvousID.empty())
        return false; // nothing to process.

    HLOGC(cnlog.Debug,
          log << "updateConnStatus: updating after getting pkt with DST socket ID @" << iDstSockID
              << " status: " << ConnectStatusStr(cst));

    for (list<CRL>::iterator i = m_lRendezvousID.begin(), i_next = i; i != m_lRendezvousID.end(); i = i_next)
    {
        // Safe iterator to the next element. If the current element is erased, the iterator is updated again.
        ++i_next;

        const steady_clock::time_point tsNow = steady_clock::now();

        if (tsNow >= i->m_tsTTL)
        {
            HLOGC(cnlog.Debug,
                  log << "RID: socket @" << i->m_iID
                      << " removed - EXPIRED ("
                      // The "enforced on FAILURE" is below when processAsyncConnectRequest failed.
                      << (is_zero(i->m_tsTTL) ? "enforced on FAILURE" : "passed TTL") << "). WILL REMOVE from queue.");

            // Set appropriate error information, but do not update yet.
            // Exit the lock first. Collect objects to update them later.
            int ccerror = SRT_ECONNREJ;
            if (i->m_pUDT->m_RejectReason == SRT_REJ_UNKNOWN)
            {
                if (!is_zero(i->m_tsTTL))
                {
                    // Timer expired, set TIMEOUT forcefully
                    i->m_pUDT->m_RejectReason = SRT_REJ_TIMEOUT;
                    ccerror                   = SRT_ENOSERVER;
                }
                else
                {
                    // In case of unknown reason, rejection should at least
                    // suggest error on the peer
                    i->m_pUDT->m_RejectReason = SRT_REJ_PEER;
                }
            }

            // The call to completeBrokenConnectionDependencies() cannot happen here
            // under the lock of m_RIDListLock as it risks a deadlock.
            // Collect in 'toRemove' to update later.
            LinkStatusInfo fi = {i->m_pUDT, i->m_iID, ccerror, i->m_PeerAddr, -1};
            toRemove.push_back(fi);
            expirePending(*i->m_it);

            // i_next was preincremented, but this is guaranteed to point to
            // the element next to erased one.
            i_next = m_lRendezvousID.erase(i);
            continue;
        }
        else
        {
            HLOGC(cnlog.Debug,
                  log << "RID: socket @" << i->m_iID << " still active (remaining " << std::fixed
                      << (count_microseconds(i->m_tsTTL - tsNow) / 1000000.0) << "s of TTL)...");
        }

        const steady_clock::time_point tsLastReq = i->m_pUDT->m_tsLastReqTime;
        const steady_clock::time_point tsRepeat =
            tsLastReq + milliseconds_from(250); // Repeat connection request (send HS).

        // A connection request is repeated every 250 ms if there was no response from the peer:
        // - RST_AGAIN means no packet was received over UDP.
        // - a packet was received, but not for THIS socket.
        if ((rst == RST_AGAIN || i->m_iID != iDstSockID) && tsNow <= tsRepeat)
        {
            HLOGC(cnlog.Debug,
                  log << "RID:@" << i->m_iID << " " << FormatDurationAuto(tsNow - tsLastReq)
                      << " passed since last connection request.");

            continue;
        }

        HLOGC(cnlog.Debug,
              log << "RID:@" << i->m_iID << " cst=" << ConnectStatusStr(cst) << " -- repeating connection request.");

        // Collect them so that they can be updated out of m_RIDListLock.
        LinkStatusInfo fi = {i->m_pUDT, i->m_iID, SRT_SUCCESS, i->m_PeerAddr, -1};
        toProcess.push_back(fi);
    }

    return !toRemove.empty() || !toProcess.empty();
}

void CMultiplexer::configure(int32_t id, const CSrtConfig& config, const sockaddr_any& reqaddr, const UDPSOCKET* udpsock)
{
    m_mcfg = config;
    m_iID  = id;

    // XXX Leaving as dynamic due to a potential for abstracting out the channel class.
    m_pChannel = new CChannel();
    m_pChannel->setConfig(m_mcfg);

    if (udpsock)
    {
        // In this case, reqaddr contains the address
        // that has been extracted already from the
        // given socket
        m_pChannel->attach(*udpsock, reqaddr);
    }
    else if (reqaddr.empty())
    {
        // If reqaddr was set as empty, only with set family,
        // just automatically bind to the "0" address to autoselect
        // everything.
        m_pChannel->open(reqaddr.family());
    }
    else
    {
        // If at least the IP address is specified, then bind to that
        // address, but still possibly autoselect the outgoing port, if the
        // port was specified as 0.
        m_pChannel->open(reqaddr);
    }

    // After the system binding the 0 port could be reassigned by the
    // system-selected port; extract it.
    m_SelfAddr = m_pChannel->getSockAddr();

    // AFTER OPENING, check the matter of IPV6_V6ONLY option,
    // as it decides about the fact that the occupied binding address
    // in case of wildcard is both :: and 0.0.0.0, or only ::.
    if (reqaddr.family() == AF_INET6 && m_mcfg.iIpV6Only == -1)
    {
        // XXX We don't know how probable it is to get the error here
        // and resulting -1 value. As a fallback for that case, the value -1
        // is honored here, just all side-bindings for other sockes will be
        // rejected as a potential conflict, even if binding would be accepted
        // in these circumstances. Only a perfect match in case of potential
        // overlapping will be accepted on the same port.
        m_mcfg.iIpV6Only = m_pChannel->sockopt(IPPROTO_IPV6, IPV6_V6ONLY, -1);
    }

    m_SndQueue.init(m_pChannel);

    // We can't use maxPayloadSize() because this value isn't valid until the connection is established.
    // We need to "think big", that is, allocate a size that would fit both IPv4 and IPv6.
    const size_t payload_size = config.iMSS - CPacket::HDR_SIZE - CPacket::udpHeaderSize(AF_INET);

    // XXX m_pHash hash size passed HERE!
    // (Likely here configure the hash table for m_Sockets).
    HLOGC(smlog.Debug, log << "@" << id << ": configureMuxer: config rcv queue qsize=" << 128
            << " plsize=" << payload_size << " hsize=" << 1024);
    m_SocketMap.reserve(1024);
    m_RcvQueue.init(128, payload_size, m_pChannel);
}

//
CRcvQueue::CRcvQueue(CMultiplexer* parent):
    m_parent(parent),
    m_WorkerThread(),
    m_pUnitQueue(NULL),
    m_pRcvUList(NULL),
    m_pChannel(NULL),
    m_szPayloadSize(),
    m_bClosing(false),
    m_mBuffer(),
    m_BufferCond()
{
    setupCond(m_BufferCond, "QueueBuffer");
}

CRcvQueue::~CRcvQueue()
{
    m_bClosing = true;

    if (m_WorkerThread.joinable())
    {
        HLOGC(rslog.Debug, log << "RcvQueue: EXIT");
        m_WorkerThread.join();
    }
    releaseCond(m_BufferCond);

    delete m_pUnitQueue;
    delete m_pRcvUList;

    // remove all queued messages
    for (qmap_t::iterator i = m_mBuffer.begin(); i != m_mBuffer.end(); ++i)
    {
        while (!i->second.empty())
        {
            CPacket* pkt = i->second.front();
            delete pkt;
            i->second.pop();
        }
    }
}

#if ENABLE_LOGGING
sync::atomic<int> CRcvQueue::m_counter(0);
#endif

void CRcvQueue::init(int qsize, size_t payload, CChannel* cc)
{
    m_szPayloadSize = payload;

    SRT_ASSERT(m_pUnitQueue == NULL);
    m_pUnitQueue = new CUnitQueue(qsize, (int)payload);


    m_pChannel = cc;

    m_pRcvUList        = new CRcvUList;

#if ENABLE_LOGGING
    const int cnt = ++m_counter;
    const std::string thrname = "SRT:RcvQ:w" + Sprint(cnt);
#else
    const std::string thrname = "SRT:RcvQ:w";
#endif

    if (!StartThread((m_WorkerThread), CRcvQueue::worker_fwd, this, thrname.c_str()))
    {
        throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
    }
}

void* CRcvQueue::worker_fwd(void* param)
{
    CRcvQueue*   self = (CRcvQueue*)param;
    self->worker();
    return NULL;
}

void CRcvQueue::worker()
{
    sockaddr_any sa(m_parent->selfAddr().family());
    SRTSOCKET id = SRT_SOCKID_CONNREQ;

    std::string thname;
    ThreadName::get(thname);
    THREAD_STATE_INIT(thname.c_str());

    CUnit*         unit = 0;
    EConnectStatus cst  = CONN_AGAIN;
    while (!m_bClosing)
    {
        bool        have_received = false;
        EReadStatus rst           = worker_RetrieveUnit((id), (unit), (sa));

        INCREMENT_THREAD_ITERATIONS();
        if (rst == RST_OK)
        {
            if (int(id) < 0) // Any negative (illegal range) and SRT_INVALID_SOCK
            {
                // User error on peer. May log something, but generally can only ignore it.
                // XXX Think maybe about sending some "connection rejection response".
                HLOGC(qrlog.Debug,
                      log << CONID() << "RECEIVED negative socket id '" << id
                          << "', rejecting (POSSIBLE ATTACK)");
                continue;
            }

            // NOTE: cst state is being changed here.
            // This state should be maintained through any next failed calls to worker_RetrieveUnit.
            // Any error switches this to rejection, just for a case.

            // Note to rendezvous connection. This can accept:
            // - ID == 0 - take the first waiting rendezvous socket
            // - ID > 0  - find the rendezvous socket that has this ID.
            if (id == SRT_SOCKID_CONNREQ)
            {
                // ID 0 is for connection request, which should be passed to the listening socket or rendezvous sockets
                cst = worker_ProcessConnectionRequest(unit, sa);
            }
            else
            {
                // Otherwise ID is expected to be associated with:
                // - an enqueued rendezvous socket
                // - a socket connected to a peer
                cst = worker_ProcessAddressedPacket(id, unit, sa);
                // CAN RETURN CONN_REJECT, but m_RejectReason is already set
            }
            HLOGC(qrlog.Debug, log << CONID() << "worker: result for the unit: " << ConnectStatusStr(cst));
            if (cst == CONN_AGAIN)
            {
                HLOGC(qrlog.Debug, log << CONID() << "worker: packet not dispatched, continuing reading.");
                continue;
            }
            have_received = true;
        }
        else if (rst == RST_ERROR)
        {
            // According to the description by CChannel::recvfrom, this can be either of:
            // - IPE: all errors except EBADF
            // - socket was closed in the meantime by another thread: EBADF
            // If EBADF, then it's expected that the "closing" state is also set.
            // Check that just to report possible errors, but interrupt the loop anyway.
            if (m_bClosing)
            {
                HLOGC(qrlog.Debug,
                      log << CONID() << "CChannel reported error, but Queue is closing - INTERRUPTING worker.");
            }
            else
            {
                LOGC(qrlog.Fatal,
                     log << CONID()
                         << "CChannel reported ERROR DURING TRANSMISSION - IPE. INTERRUPTING worker anyway.");
            }
            cst = CONN_REJECT;
            break;
        }
        // OTHERWISE: this is an "AGAIN" situation. No data was read, but the process should continue.

        // take care of the timing event for all UDT sockets
        const steady_clock::time_point curtime_minus_syn =
            steady_clock::now() - microseconds_from(CUDT::COMM_SYN_INTERVAL_US);

        CRNode* ul = m_pRcvUList->m_pUList;
        while ((NULL != ul) && (ul->m_tsTimeStamp < curtime_minus_syn))
        {
            CUDT* u = ul->m_pUDT;

            if (u->m_bConnected && !u->m_bBroken && !u->m_bClosing)
            {
                u->checkTimers();
                m_pRcvUList->update(u);
            }
            else
            {
                HLOGC(qrlog.Debug,
                      log << CUDTUnited::CONID(u->m_SocketID) << " SOCKET broken, REMOVING FROM RCV QUEUE/MAP.");
                // the socket must be removed from Hash table first, then RcvUList
                m_parent->setBroken(u->m_SocketID);
                m_pRcvUList->remove(u);
                u->m_pRNode->m_bOnList = false;
            }

            ul = m_pRcvUList->m_pUList;
        }

        if (have_received)
        {
            HLOGC(qrlog.Debug,
                  log << "worker: RECEIVED PACKET --> updateConnStatus. cst=" << ConnectStatusStr(cst) << " id=" << id
                      << " pkt-payload-size=" << unit->m_Packet.getLength());
        }

        // Check connection requests status for all sockets in the RendezvousQueue.
        // Pass the connection status from the last call of:
        // worker_ProcessAddressedPacket --->
        // worker_TryAsyncRend_OrStore --->
        // CUDT::processAsyncConnectResponse --->
        // CUDT::processConnectResponse
        updateConnStatus(rst, cst, unit);

        // XXX updateConnStatus may have removed the connector from the list,
        // however there's still m_mBuffer in CRcvQueue for that socket to care about.
    }

    HLOGC(qrlog.Debug, log << "worker: EXIT");

    THREAD_EXIT();
}

EReadStatus CRcvQueue::worker_RetrieveUnit(SRTSOCKET& w_id, CUnit*& w_unit, sockaddr_any& w_addr)
{
//*
#if !USE_BUSY_WAITING
    // This might be not really necessary, and probably
    // not good for extensive bidirectional communication.
    m_parent->tickSender();
#endif
// */

    // find next available slot for incoming packet
    w_unit = m_pUnitQueue->getNextAvailUnit();
    if (!w_unit)
    {
        // no space, skip this packet
        CPacket temp;
        temp.allocate(m_szPayloadSize);
        THREAD_PAUSED();
        EReadStatus rst = m_pChannel->recvfrom((w_addr), (temp));
        THREAD_RESUMED();
        // Note: this will print nothing about the packet details unless heavy logging is on.
        LOGC(qrlog.Error, log << CONID() << "LOCAL STORAGE DEPLETED. Dropping 1 packet: " << temp.Info());

        // Be transparent for RST_ERROR, but ignore the correct
        // data read and fake that the packet was dropped.
        return rst == RST_ERROR ? RST_ERROR : RST_AGAIN;
    }

    w_unit->m_Packet.setLength(m_szPayloadSize);

    // reading next incoming packet, recvfrom returns -1 is nothing has been received
    THREAD_PAUSED();
    EReadStatus rst = m_pChannel->recvfrom((w_addr), (w_unit->m_Packet));
    THREAD_RESUMED();

    if (rst == RST_OK)
    {
        w_id = w_unit->m_Packet.id();
        HLOGC(qrlog.Debug,
              log << "INCOMING PACKET: FROM=" << w_addr.str() << " BOUND=" << m_pChannel->bindAddressAny().str() << " "
                  << w_unit->m_Packet.Info());
    }
    return rst;
}

EConnectStatus CRcvQueue::worker_ProcessConnectionRequest(CUnit* unit, const sockaddr_any& addr)
{
    HLOGC(cnlog.Debug,
          log << "Got sockID=0 from " << addr.str() << " - trying to resolve it as a connection request...");
    // Introduced protection because it may potentially happen
    // that another thread could have closed the socket at
    // the same time and inject a bug between checking the
    // pointer for NULL and using it.
    int  listener_ret  = SRT_REJ_UNKNOWN;
    bool have_listener = false;
    {
        SharedLock shl(m_pListener);
        CUDT*      pListener = m_pListener.getPtrNoLock();

        if (pListener)
        {
            LOGC(cnlog.Debug, log << "PASSING request from: " << addr.str() << " to listener:" << pListener->socketID());
            listener_ret = pListener->processConnectRequest(addr, unit->m_Packet);

            // This function does return a code, but it's hard to say as to whether
            // anything can be done about it. In case when it's stated possible, the
            // listener will try to send some rejection response to the caller, but
            // that's already done inside this function. So it's only used for
            // displaying the error in logs.

            have_listener = true;
        }
    }

    // NOTE: Rendezvous sockets do bind(), but not listen(). It means that the socket is
    // ready to accept connection requests, but they are not being redirected to the listener
    // socket, as this is not a listener socket at all. This goes then HERE.

    if (have_listener) // That is, the above block with m_pListener->processConnectRequest was executed
    {
        LOGC(cnlog.Debug,
             log << CONID() << "Listener got the connection request from: " << addr.str()
                 << " result:" << RequestTypeStr(UDTRequestType(listener_ret)));
        return listener_ret == SRT_REJ_UNKNOWN ? CONN_CONTINUE : CONN_REJECT;
    }

    if (worker_TryAcceptedSocket(unit, addr))
    {
        HLOGC(cnlog.Debug, log << "connection request to an accepted socket succeeded");
        return CONN_CONTINUE;
    }
    else
    {
        HLOGC(cnlog.Debug, log << "connection request to an accepted socket failed. Will retry RDV or store");
    }

    // If there is no listener waiting for that packet, try a rendezvous socket
    // for the incoming address. This is then regardless if the peer knows the
    // proper ID or not. Anyway, if the proper ID was supplied, it would be handled
    // earlier by retrievePending called from worker_ProcessAddressedPacket.
    CUDT* u = m_parent->retrieveRID(addr, SRT_SOCKID_CONNREQ);
    if (!u)
    {
        HLOGC(cnlog.Debug, log << CONID()
                << "worker_ProcessConnectionRequest: no sockets expect connection from " << addr.str()
                << " - POSSIBLE ATTACK, ignore packet");
        return CONN_AGAIN;
    }

    return worker_RetryOrRendezvous(u, unit);
}

bool CRcvQueue::worker_TryAcceptedSocket(CUnit* unit, const sockaddr_any& addr)
{
    // We are working with a possibly HS packet... check that.
    CPacket& pkt = unit->m_Packet;

    if (pkt.getLength() < CHandShake::m_iContentSize || !pkt.isControl(UMSG_HANDSHAKE))
        return false;

    CHandShake hs;
    if (0 != hs.load_from(pkt.data(), pkt.size()))
        return false;

    if (hs.m_iReqType != URQ_CONCLUSION)
        return false;

    if (hs.m_iVersion >= CUDT::HS_VERSION_SRT1)
        hs.m_extension = true;

    // Ok, at last we have a peer ID info
    SRTSOCKET peerid = hs.m_iID;

    // Now search for a socket that has this peer ID
    CUDTSocket* s = m_parent->findPeer(peerid, addr);
    if (!s)
    {
        HLOGC(cnlog.Debug, log << "worker_TryAcceptedSocket: can't find accepted socket for peer -@" << peerid
                               << " and address: " << addr.str() << " - POSSIBLE ATTACK, rejecting");
        return false;
    }
    CUDTUnited::SocketKeeper sk (CUDT::uglobal(), s);
    if (!sk.socket)
        return false;

    CUDT* u = &s->core();
    if (u->m_bBroken || u->m_bClosing)
        return false;

    HLOGC(cnlog.Debug, log << "FOUND accepted socket @" << u->m_SocketID << " that is a peer for -@"
            << peerid << " - DISPATCHING to it to resend HS response");

    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;
    if (u->craftKmResponse((kmdata), (kmdatasize)) != CONN_ACCEPT)
    {
        HLOGC(cnlog.Debug, log << "craftKmResponse: failed");
        return false;
    }

    return u->createSendHSResponse(kmdata, kmdatasize, pkt.udpDestAddr(), (hs));
}

EConnectStatus CRcvQueue::worker_ProcessAddressedPacket(SRTSOCKET id, CUnit* unit, const sockaddr_any& addr)
{
    SocketHolder::State hstate = SocketHolder::INIT;
    CUDTSocket* s = m_parent->findAgent(id, addr, (hstate));
    if (!s)
    {
        HLOGC(cnlog.Debug,
                log << CONID() << "worker_ProcessAddressedPacket: socket @"
                << id << " not found as expecting packet from " << addr.str()
                << " - POSSIBLE ATTACK, ignore packet");
        return CONN_AGAIN; // This means that the packet should be ignored.
    }
    // Although we don´t have an exclusive passing here,
    // we can count on that when the socket was once present in the hash,
    // it will not be deleted for at least one GC cycle. But we still need
    // to maintain the object existence as long as it's in use.
    // Note that here we are out of any locks, so m_GlobControlLock can be locked.
    CUDTUnited::SocketKeeper sk (CUDT::uglobal(), s);
    // Sanity check - should never happen, but better safe than sorry.
    if (!sk.socket)
    {
        HLOGC(cnlog.Debug, log << "worker_ProcessAddressedPacket: IPE/EPE: socket @" << id
                << " could not be dispatched, ignoring packet");
        return CONN_AGAIN;
    }

    CUDT* u = &s->core();
    if (hstate == SocketHolder::PENDING)
    {
        // Pass this to connection pending handler,
        // or store the packet in the queue.
        HLOGC(cnlog.Debug, log << "worker_ProcessAddressedPacket: resending to PENDING socket @" << id);
        return worker_RetryOrRendezvous(u, unit);
    }

    if (!u->m_bConnected || u->m_bBroken || u->m_bClosing)
    {
        if (u->m_RejectReason == SRT_REJ_UNKNOWN)
            u->m_RejectReason = SRT_REJ_CLOSE;
        HLOGC(cnlog.Debug, log << "worker_ProcessAddressedPacket: target @"
                << id << " is being closed, rejecting");
        // The socket is currently in the process of being disconnected
        // or destroyed. Ignore.
        // XXX send UMSG_SHUTDOWN in this case?
        // XXX May it require mutex protection?
        return CONN_REJECT;
    }

    if (unit->m_Packet.isControl())
        u->processCtrl(unit->m_Packet);
    else
        u->processData(unit);

    u->checkTimers();
    m_pRcvUList->update(u);

    return CONN_RUNNING;
}

EConnectStatus CRcvQueue::worker_RetryOrRendezvous(CUDT* u, CUnit* unit)
{
    HLOGC(cnlog.Debug, log << "worker_RetryOrRendezvous: packet RESOLVED TO @" << u->id() << " -- continuing as ASYNC CONNECT");
    // This is practically same as processConnectResponse, just this applies
    // appropriate mutex lock - which can't be done here because it's intentionally private.
    // OTOH it can't be applied to processConnectResponse because the synchronous
    // call to this method applies the lock by itself, and same-thread-double-locking is nonportable (crashable).
    EConnectStatus cst = u->processAsyncConnectResponse(unit->m_Packet);
    if (cst != CONN_CONFUSED)
        return cst;

    LOGC(cnlog.Warn, log << "worker_RetryOrRendezvous: PACKET NOT HANDSHAKE - re-requesting handshake from peer");
    storePktClone(u->id(), unit->m_Packet);
    if (!u->processAsyncConnectRequest(RST_AGAIN, CONN_CONTINUE, &unit->m_Packet, u->m_PeerAddr))
    {
        // Reuse previous behavior to reject a packet
        return CONN_REJECT;
    }
    return CONN_CONTINUE;
}

void CRcvQueue::stopWorker()
{
    // We use the decent way, so we say to the thread "please exit".
    m_bClosing = true;

    // Sanity check of the function's affinity.
    if (sync::this_thread::get_id() == m_WorkerThread.get_id())
    {
        LOGC(rslog.Error, log << "IPE: RcvQ:WORKER TRIES TO CLOSE ITSELF!");
        return; // do nothing else, this would cause a hangup or crash.
    }

    HLOGC(rslog.Debug, log << "RcvQueue: EXIT (forced)");
    // And we trust the thread that it does.
    m_WorkerThread.join();
}

int CRcvQueue::setListener(CUDT* u)
{
    if (!m_pListener.set(u))
        return -1;

    return 0;
}

void CRcvQueue::removeListener(const CUDT* u)
{
    m_pListener.clearIf(u);
    m_parent->deleteSocket(u->id());
}

void CMultiplexer::registerCRL(const CRL& setup)
{
    ScopedLock vg(m_SocketsLock);

    // Check first if the alleged socket is already in the map,
    // otherwise it wasn't bound. This should never happen, so
    // it's more a sanity check. The check is necessary because
    // the RID queue is not allowed to keep sockets that were not
    // previously assigned to this multiplexer.
    sockmap_t::iterator p = m_SocketMap.find(setup.m_iID);
    if (p == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "registerCRL: IPE: socket @" << setup.m_iID << " not found in muxer id=" << m_iID);
        return;
    }

    m_lRendezvousID.push_back(setup);
    std::list<CRL>::iterator last = m_lRendezvousID.end();
    --last;
    last->m_it = p->second;

    // Ok, the RID is only a helping map to extract incoming connection
    // request, but the caller or rendezvous socket, for which this function
    // is being called, needs to qualify this socket as a pending for connection.

    p->second->setConnector(setup.m_PeerAddr, setup.m_tsTTL);
}

// DEBUG SUPPORT
string SocketHolder::report() const
{
    // XXX make it better performant in the new logging format
    std::ostringstream out;

    out << "@";
    if (m_pSocket)
        out << m_pSocket->core().id();
    else
        out << "!!!";

    out << " s=" << StateStr(m_State);

    out << " PEER: @";
    if (peerID() <= 0)
        out << "NONE";
    else
        out << peerID();

    if (!m_PeerAddr.empty())
        out << " (" << m_PeerAddr.str() << ")";

    out << " TS:";

    if (!is_zero(m_tsRequestTTL))
        out << " RQ:" << FormatTime(m_tsRequestTTL);
    if (!is_zero(m_tsUpdateTime))
        out << " UP:" << FormatTime(m_tsUpdateTime);
    if (!is_zero(m_tsSendTime))
        out << " SN:" << FormatTime(m_tsSendTime);

    return out.str();
}

void CRcvQueue::removeConnector(const SRTSOCKET& id)
{
    HLOGC(cnlog.Debug, log << "removeConnector: removing @" << id);
    m_parent->removeRID(id);

    ScopedLock bufferlock(m_BufferLock);

    qmap_t::iterator i = m_mBuffer.find(id);
    if (i != m_mBuffer.end())
    {
        HLOGC(cnlog.Debug,
              log << "removeConnector: ... and its packet queue with " << i->second.size() << " packets collected");
        while (!i->second.empty())
        {
            delete i->second.front();
            i->second.pop();
        }
        m_mBuffer.erase(i);
    }
}

// XXX UNUSED. Moved to CMultiplexer::setReceiver
void srt::CRcvQueue::setNewEntry(CUDT* u)
{
    SRT_ASSERT_AFFINITY(m_WorkerThread.get_id());

    HLOGC(qrlog.Debug,
            log << u->CONID() << " SOCKET pending for connection - ADDING TO RCV QUEUE/MAP (directly)");
    m_pRcvUList->insert(u);
    m_parent->setConnected(u->m_SocketID);
}

void CRcvQueue::kick()
{
    CSync::lock_notify_all(m_BufferCond, m_BufferLock);
}

void CRcvQueue::storePktClone(SRTSOCKET id, const CPacket& pkt)
{
    CUniqueSync passcond(m_BufferLock, m_BufferCond);

    qmap_t::iterator i = m_mBuffer.find(id);

    if (i == m_mBuffer.end())
    {
        m_mBuffer[id].push(pkt.clone());
        passcond.notify_one();
    }
    else
    {
        // Avoid storing too many packets, in case of malfunction or attack.
        if (i->second.size() > 16)
            return;

        i->second.push(pkt.clone());
    }
}

bool CMultiplexer::addSocket(CUDTSocket* s)
{
    sync::ScopedLock lk (m_SocketsLock);

    // Check if socket is not added twice, just in case
    sockmap_t::iterator fo = m_SocketMap.find(s->core().id());
    if (fo != m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "IPE: attempting to add @" << s->core().m_SocketID << " TWICE (already found)");
        return false;
    }

    m_Sockets.push_back(SocketHolder::initial(s));
    std::list<SocketHolder>::iterator last = m_Sockets.end();
    --last; // guaranteed to be valid after push_back
    m_SocketMap[s->core().m_SocketID] = last;
    ++m_zSockets;
    HLOGC(qmlog.Debug, log << "MUXER: id=" << m_iID << " added @" << s->core().m_SocketID << " (total of " << m_zSockets.load() << " sockets)");
    return true;
}

bool CMultiplexer::setConnected(SRTSOCKET id)
{
    if (!m_zSockets)
    {
        LOGC(qmlog.Error, log << "setConnected: MUXER id=" << m_iID << " no sockets while looking for @" << id);
        return false;
    }

    sync::ScopedLock lk (m_SocketsLock);

    sockmap_t::iterator fo = m_SocketMap.find(id);
    if (fo == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "setConnected: MUXER id=" << m_iID << " NOT FOUND: @" << id);
        return false;
    }

    std::list<SocketHolder>::iterator point = fo->second;
    SocketHolder& sh = *point;

    // XXX assert?
    if (!sh.m_pSocket)
    {
        LOGC(qmlog.Error, log << "MUXER id=" << m_iID << " IPE: @" << id << " found, but NULL socket");
        return false;
    }

    // Unkown why it might happen, so leaving just in case.
    if (sh.m_pSocket->core().m_PeerID < 1)
    {
        LOGC(qmlog.Warn, log << "MUXER: @" << id << " has no peer set");
        return false;
    }

    // It's hard to distinguish the origin of the call,
    // and in case of caller you already have set the peer address
    // in advance, while for the accepted socket this is only
    // known when creating the socket, but passing it there is
    // complicated. So, instead we simply rewrite the peer address
    // from the settings in the CUDT entity, and only in case when
    // it wasn't yet set. We recognize it by having port == 0 because
    // this isn't a valid port number, at least not of the peer.
    if (sh.m_PeerAddr.hport() == 0)
    {
        sh.m_PeerAddr = sh.m_pSocket->core().m_PeerAddr;
    }

    SRTSOCKET prid = sh.m_pSocket->core().m_PeerID;
    m_RevPeerMap[prid] = id;
    sh.m_State = SocketHolder::ACTIVE;

    HLOGC(qmlog.Debug, log << "MUXER id=" << m_iID << ": connected: " << sh.report());
    return true;
}

bool CMultiplexer::setBroken(SRTSOCKET id)
{
    if (!m_zSockets)
    {
        LOGC(qmlog.Error, log << "setBroken: MUXER id=" << m_iID << " no sockets while looking for @" << id);
        return false;
    }

    sync::ScopedLock lk (m_SocketsLock);

    sockmap_t::iterator fo = m_SocketMap.find(id);
    if (fo == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "setBroken: MUXER id=" << m_iID << " NOT FOUND: @" << id);
        return false;
    }

    std::list<SocketHolder>::iterator point = fo->second;
    m_RevPeerMap.erase(point->setBrokenPeer());

    HLOGC(qmlog.Debug, log << "setBroken: MUXER id=" << m_iID << " set to @" << id);
    return true;
}

bool CMultiplexer::deleteSocket(SRTSOCKET id)
{
    if (!m_zSockets)
    {
        LOGC(qmlog.Error, log << "deleteSocket: MUXER id=" << m_iID << " no sockets while looking for @" << id);
        return false;
    }

    sync::ScopedLock lk (m_SocketsLock);

    sockmap_t::iterator fo = m_SocketMap.find(id);
    if (fo == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "deleteSocket: MUXER id=" << m_iID << " no socket @" << id);
        return false;
    }

    std::list<SocketHolder>::iterator point = fo->second;
    HLOGC(qmlog.Debug, log << "deleteSocket: removing: " << point->report());

    // Remove from maps and list
    m_RevPeerMap.erase(point->peerID());
    m_SocketMap.erase(id); // fo is no longer valid!
    m_Sockets.erase(point);
    --m_zSockets;
    HLOGC(qmlog.Debug, log << "deleteSocket: MUXER id=" << m_iID << " removed @" << id << " (remaining " << m_zSockets << ")");
    return true;
}

/// Find a mapped CUDTSocket whose id is @a id.
CUDTSocket* CMultiplexer::findAgent(SRTSOCKET id, const sockaddr_any& remote_addr,
        SocketHolder::State& w_state)
{
    if (!m_zSockets)
    {
        LOGC(qmlog.Error, log << "findAgent: MUXER id=" << m_iID << " no sockets while looking for @" << id);
        return NULL;
    }

    sync::ScopedLock lk (m_SocketsLock);

    sockmap_t::iterator fo = m_SocketMap.find(id);
    if (fo == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "findAgent: MUXER id=" << m_iID << " no socket @" << id);
        return NULL;
    }

    std::list<SocketHolder>::iterator point = fo->second;

    // Note that this finding function needs a socket
    // that is currently connected, so if it's not, behave
    // as if nothing was found.
    sync::steady_clock::time_point ttl;
    SocketHolder::MatchState ms = point->checkIncoming(remote_addr, (ttl), (w_state));

    if (ms != SocketHolder::MS_OK)
    {
        if (ms != SocketHolder::MS_INVALID_STATE)
        {
            LOGC(qmlog.Error, log << "findAgent: MUXER id=" << m_iID << ": " << point->report()
                    << " request from " << remote_addr.str() << " invalid "
                    << SocketHolder::MatchStr(ms));
            return NULL;
        }
        HLOGC(qmlog.Debug, log << "findAgent: MUXER id=" << m_iID << " INVALID STATE: " << point->report());
        return NULL;
    }

    HLOGC(qmlog.Debug, log << "findAgent: MUXER id=" << m_iID << " found " << point->report());
    return point->m_pSocket;
}

std::string SocketHolder::MatchStr(SocketHolder::MatchState ms)
{
    static const std::string table [] = {
        "OK",
        "STATE",
        "ADDRESS",
        "DATA"
    };
    return table[int(ms)];
}

/// Find a mapped CUDTSocket for whom the peer ID has
/// been assigned as @a rid.
CUDTSocket* CMultiplexer::findPeer(SRTSOCKET rid, const sockaddr_any& remote_addr)
{
    if (!m_zSockets)
    {
        LOGC(qmlog.Error, log << "findPeer: MUXER id=" << m_iID << " no sockets while looking for -@" << rid);
        return NULL;
    }

    sync::ScopedLock lk (m_SocketsLock);

    std::map<SRTSOCKET, SRTSOCKET>::iterator rfo = m_RevPeerMap.find(rid);
    if (rfo == m_RevPeerMap.end())
    {
        LOGC(qmlog.Error, log << "findPeer: MUXER id=" << m_iID << " -@" << rid << " not found in rev map");
        return NULL;
    }
    const int id = rfo->second;

    sockmap_t::iterator fo = m_SocketMap.find(id);
    if (fo == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "findPeer: IPE: MUXER id=" <<m_iID << ": for -@" << rid << " found assigned @" << id
                << " but not found in the map!");
        return NULL;
    }

    std::list<SocketHolder>::iterator point = fo->second;
    if (point->m_PeerAddr != remote_addr)
    {
        LOGC(qmlog.Error, log << "findPeer: MUXER id=" <<m_iID << ": for -@" << rid << " found assigned @" << id
                << " .addr=" << point->m_PeerAddr.str() << " differs to req " << remote_addr.str());
        return NULL;
    }

    return point->m_pSocket;
}

steady_clock::time_point CMultiplexer::updateSendNormal(CUDTSocket* s)
{
    const steady_clock::time_point currtime = steady_clock::now();
    bool updated SRT_ATR_UNUSED =
        m_SndQueue.m_pSndUList->update(&s->core(), CSndUList::DONT_RESCHEDULE, currtime);
    HLOGC(qslog.Debug, log << s->core().CONID() << "NORMAL update: " << (updated ? "" : "NOT ")
            << "updated to " << FormatTime(currtime));
    return currtime;
}

void CMultiplexer::updateSendFast(CUDTSocket* s)
{
    steady_clock::duration immediate = milliseconds_from(1);
    steady_clock::time_point yesterday = steady_clock::time_point(immediate);
    bool updated SRT_ATR_UNUSED =
        m_SndQueue.m_pSndUList->update(&s->core(), CSndUList::DO_RESCHEDULE, yesterday);
    HLOGC(qslog.Debug, log << s->core().CONID() << "FAST update: " << (updated ? "" : "NOT ")
            << "updated");
}


void CMultiplexer::setReceiver(CUDT* u)
{
    SRT_ASSERT_AFFINITY(m_RcvQueue.m_WorkerThread.get_id());
    SRT_ASSERT(u->m_bOpened && u->m_pRNode);

    HLOGC(qrlog.Debug, log << u->CONID() << " SOCKET pending for connection - ADDING TO RCV QUEUE/MAP (directly)");
    m_RcvQueue.m_pRcvUList->insert(u);
    setConnected(u->m_SocketID);
}


CMultiplexer::~CMultiplexer()
{
    if (m_pChannel)
    {
        m_pChannel->close();
        delete m_pChannel;
    }
}

std::string CMultiplexer::testAllSocketsClear()
{
    std::ostringstream out;
    ScopedLock lk (m_SocketsLock);

    for (sockmap_t::iterator i = m_SocketMap.begin(); i != m_SocketMap.end(); ++i)
    {
        // Do not notify those that are broken or nonexistent
        if (int(i->second->m_State) >= SocketHolder::INIT)
            out << " +" << i->first << "=" << SocketHolder::StateStr(i->second->m_State);
    }

    for (std::map<SRTSOCKET, SRTSOCKET>::iterator i = m_RevPeerMap.begin(); i != m_RevPeerMap.end(); ++i)
        out << " R[" << i->first << "]=" << i->second;

    return out.str();
}

std::string SocketHolder::StateStr(SocketHolder::State st)
{
    static const char* const state_names [] = {
        "INVALID",
        "BROKEN",
        "INIT",
        "PENDING",
        "ACTIVE"
    };
    int statex = int(st) + 2;
    if (statex < 0 || statex > 5)
        statex = 0;

    return state_names[statex];
}

} // end namespace

