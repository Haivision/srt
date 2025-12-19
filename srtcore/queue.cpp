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
#include "queue.h"

#include <cstring>

#include "common.h"
#include "api.h"
#include "netinet_any.h"
#include "hvu_threadname.h"
#include "sync.h"
#include "logging.h"

using namespace std;
using namespace srt::sync;
using namespace srt::logging;
using namespace hvu; // ThreadName

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

void CPacketUnitPool::allocateOneSeries(UnitContainer& w_series, size_t series_size, size_t unit_size)
{
    // Just in case when w_series contained anything by mistake, delete it first.
    w_series.clear();

    w_series.resize(series_size);

    for (size_t i = 0; i < series_size; ++i)
    {
        w_series[i].allocate(unit_size);
    }
}

bool CPacketUnitPool::retrieveSeries(UnitContainer& series)
{
    UniqueLock lk (m_UpperLock);
    // EXPECTED: series.empty()
    // Will be replaced by the existing series, if found
    if (m_Series.empty())
    {
        if (limitsExceeded())
        {
            return false;
        }
        size_t series_size = m_zSeriesSize;
        size_t unit_size = m_zUnitSize;

        // We don't need access to internal data since here.
        lk.unlock();

        // Allocate directly to the target vector.
        // You'll get them back here when they are recycled.
        allocateOneSeries((series), series_size, unit_size);
        return true;
    }

    // At least one element, take the last one.
    std::swap(m_Series.back(), series);
    m_Series.pop_back();
    return true;
}

void CPacketUnitPool::returnUnit(UnitPtr& returned_entry)
{
    ScopedLock lk (m_LowerLock);
    m_RecycledUnits.push_back(UnitPtr());
    m_RecycledUnits.back().swap(returned_entry);

    updateSeries();
}

void CPacketUnitPool::updateSeries()
{
    // Check if you have enough recycled units, and if so,
    // fold them into the series container
    if (m_RecycledUnits.size() >= m_zSeriesSize)
    {
        // NOTE ORDER: LowerLock, UpperLock
        ScopedLock lk (m_UpperLock);
        m_Series.push_back(UnitContainer());
        UnitContainer& newser = m_Series.back();
        newser.swap(m_RecycledUnits);
    }
}

// This should check if there are any excessive
// recycled blocks, and deletes them.
void CPacketUnitPool::updateLimits()
{
    // Roughly calculate the memory occupied by
    // existing series.
    ScopedLock lk (m_UpperLock);
    size_t max_units = m_zMaxMemory / m_zUnitSize;
    size_t max_series = (max_units + m_zUnitSize - 1) / m_zSeriesSize;

    // Calculate how many series we are allowed to have
    // NOTE: it is not allowed to have the size less than 3 series.
    // This is because we need to have at least one series for the
    // sole disposal of the multiplexer, one ready to pickup without hiccup
    // (a hiccup means that the unit series will be denied and multiplexer
    // will have to read and discard the packet), and one being reclaimed
    // from the receiver buffer.
    size_t max_remain_series = std::max(max_series, +MIN_SERIES_REQUIRED);

    if (max_remain_series < m_Series.size())
    {
        std::vector<UnitContainer>::iterator new_end = m_Series.begin() + max_series;
        m_Series.erase(new_end, m_Series.end());
    }
}

// CSendOrderList -- replacement for CSndUList
CSendOrderList::CSendOrderList()
{
    setupCond(m_ListCond, "CSndUListCond");
}

void CSendOrderList::resetAtFork()
{
    resetCond(m_ListCond);
}

bool CSendOrderList::update(SocketHolder::sockiter_t point, SocketHolder::EReschedule reschedule, sync::steady_clock::time_point ts)
{
    if (point == SocketHolder::none())
    {
        HLOGC(qslog.Error, log << "CSendOrderList: IPE: trying to schedule a socket outside of Multiplexer!");
        return false;
    }

    SocketHolder::SendNode& n = point->m_SendOrder;

#if HVU_ENABLE_HEAVY_LOGGING
    sync::steady_clock::time_point now = sync::steady_clock::now();
    std::ostringstream nowrel, oldrel;
    nowrel << " = now" << showpos << (ts - now).count() << "us";
    {
        ScopedLock listguard(m_ListLock);
        oldrel << " = now" << showpos << (n.time - now).count() << "us";
    }
#endif

    if (!n.pinned())
    {
        // New insert, not considering reschedule.
        HLOGC(qslog.Debug, log << "CSndUList: UPDATE: inserting @" << point->id() << " anew T=" << FormatTime(ts) << nowrel.str());

        ScopedLock listguard(m_ListLock);
        m_Schedule.insert(ts, point);
        if (n.is_top())
        {
            n.time = ts;
            m_ListCond.notify_all();
        }
        return true;
    }

    // EXISTING NODE - reschedule if requested
    if (reschedule == SocketHolder::DONT_RESCHEDULE)
    {
        HLOGC(qslog.Debug, log << "CSndUList: UPDATE: NOT rescheduling @" << point->id()
                << " - remains T=" << FormatTime(n.time) << oldrel.str());
        return false;
    }

    ScopedLock listguard(m_ListLock);

    // NOTE: Rescheduling means to speed up release time. So apply only if new time is earlier.
    if (n.time <= ts)
    {
        HLOGC(qslog.Debug, log << "CSndUList: UPDATE: NOT rescheduling @" << point->id()
                << " to +" << FormatDurationAuto(ts - n.time)
                << " - remains T=" << FormatTime(n.time) << oldrel.str());
        return false;
    }

    HLOGC(qslog.Debug, log << "CSndUList: UPDATE: rescheduling @" << point->id() << " T=" << FormatTime(n.time)
            << nowrel.str() << " - speedup by " << FormatDurationAuto(n.time - ts));

    // Special case for the first element - no replacement needed, just update.
    if (n.is_top())
    {
        n.time = ts;
        m_ListCond.notify_all();
        return true;
    }

    m_Schedule.update(n.pos, ts);

    return true;
}

void CSendOrderList::remove(SocketHolder::sockiter_t point)
{
    ScopedLock listguard(m_ListLock);
    m_Schedule.erase(point);
}


SocketHolder::sockiter_t CSendOrderList::wait()
{
    CUniqueSync lg (m_ListLock, m_ListCond);

    bool signaled = false;
    for (;;)
    {
        sync::steady_clock::time_point uptime;

        // Always pick up something if available, even if stopped.
        if (!m_Schedule.empty())
        {
            // Have at least one element in the list.
            // Check if the ship time is in the past
            SocketHolder::sockiter_t point = m_Schedule.top_raw();
            if (point->m_SendOrder.time < sync::steady_clock::now())
                return point;
            uptime = point->m_SendOrder.time;
            signaled = false;
        }
        // Always exit immediately if signalInterrupt was called
        else if (signaled || !m_bRunning)
        {
            // This happens if the waiting on a CV has exit on alleged notification:
            //   - spurious - just return to waiting
            //   - added a list element - pickup if ready, otherwise wait
            //   - signalInterrupt was called - always exit immediately
            return SocketHolder::none();
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

bool CSendOrderList::requeue(SocketHolder::sockiter_t point, const sync::steady_clock::time_point& uptime)
{
    if (point == SocketHolder::none())
    {
        HLOGC(qslog.Error, log << "CSendOrderList: IPE: trying to enqueue a socket outside of Multiplexer!");
        return false;
    }

    SocketHolder::SendNode& node = point->m_SendOrder;

    ScopedLock listguard(m_ListLock);

    // Should be.
    if (!node.pinned())
    {
        m_Schedule.insert(uptime, point); // 'node' is updated here!
        return node.is_top();
    }

    if (m_Schedule.size() == 1)
    {
        node.time = uptime;

        // Return true to declare that the top element was updated,
        // but don't do anything additionally, as this function is
        // to be used in the same thread that calls wait().
        return true;
    }

    m_Schedule.update(node.pos, uptime);
    return node.is_top();
}

void CSendOrderList::signalInterrupt()
{
    ScopedLock listguard(m_ListLock);
    m_bRunning = false;
    m_ListCond.notify_one();
}

///////////////////////////////////////////

//
CSndQueue::CSndQueue(CMultiplexer* parent):
    m_parent(parent),
    m_pChannel(NULL),
    m_bClosing(false)
{
}

void CSndQueue::resetAtFork()
{
    resetThread(&m_WorkerThread);
    m_SendOrderList.resetAtFork();
}

void CSndQueue::stop()
{
    // We use the decent way, so we say to the thread "please exit".
    m_bClosing = true;

    m_SendOrderList.signalInterrupt();

    // Sanity check of the function's affinity.
    if (sync::this_thread_is(m_WorkerThread))
    {
        LOGC(rslog.Error, log << "IPE: SndQ:WORKER TRIES TO CLOSE ITSELF!");
        return; // do nothing else, this would cause a hangup or crash.
    }

    HLOGC(rslog.Debug, log << "SndQueue: EXIT (forced)");
    // And we trust the thread that it does.
    if (m_WorkerThread.joinable())
        m_WorkerThread.join();
}

CSndQueue::~CSndQueue()
{
}

#if HVU_ENABLE_LOGGING
sync::atomic<int> CSndQueue::m_counter(0);
#endif

void CSndQueue::init(CChannel* c)
{
    m_pChannel  = c;

#if HVU_ENABLE_LOGGING
    ++m_counter;
    const string thrname = fmtcat("SRT:SndQ:w", m_counter.load());
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

void CSndQueue::workerSendOrder()
{
    string thname;
    ThreadName::get(thname);
    THREAD_STATE_INIT(thname.c_str());

    CSendOrderList& sched = m_SendOrderList;

    sched.setRunning();

#if SRT_ENABLE_THREAD_DEBUG
    Condition::ScopedNotifier nt(sched.m_ListCond);
#endif

    for (;;)
    {
        if (m_bClosing)
        {
            HLOGC(qslog.Debug, log << "SndQ: closed, exiting");
            break;
        }

        HLOGC(qslog.Debug, log << "SndQ: waiting to get next send candidate...");
        THREAD_PAUSED();
        SocketHolder::sockiter_t runner = sched.wait();
        THREAD_RESUMED();

        INCREMENT_THREAD_ITERATIONS();

        if (runner == SocketHolder::none())
        {
            HLOGC(qslog.Debug, log << "SndQ: wait interrupted...");
            if (m_bClosing)
            {
                HLOGC(qslog.Debug, log << "SndQ: interrupted, closed, exitting");
                break;
            }

            // REPORT IPE???
            // wait() should not exit if it wasn't forcefully interrupted
            HLOGC(qslog.Debug, log << "SndQ: interrupted, SPURIOUS??? IPE??? Repeating...");
            continue;
        }

        // Get a socket with a send request if any.
        CUDT& u = runner->m_pSocket->core();

#define UST(field) ((u.m_b##field) ? "+" : "-") << #field << " "
        HLOGC(qslog.Debug,
            log << "CSndQueue: requesting packet from @" << u.socketID() << " STATUS: " << UST(Listening)
                << UST(Connecting) << UST(Connected) << UST(Closing) << UST(Shutdown) << UST(Broken) << UST(PeerHealth)
                << UST(Opened));
#undef UST

        if (!u.m_bConnected || u.m_bBroken || u.m_bClosing)
        {
            HLOGC(qslog.Debug, log << "Socket to be processed is already broken, not packing");
            sched.remove(runner);
            continue;
        }

        // pack a packet from the socket
        CPacket pkt;
        steady_clock::time_point next_send_time;
        CNetworkInterface source_addr;
        const bool res = u.packData((pkt), (next_send_time), (source_addr));

        // Check if extracted anything to send
        if (res == false)
        {
            HLOGC(qslog.Debug, log << "packData: nothing to send, WITHDRAWING sender");
            sched.remove(runner);
            continue;
        }

        const sockaddr_any addr = u.m_PeerAddr;
        if (!is_zero(next_send_time))
        {
            sched.requeue(runner, next_send_time);
            IF_HEAVY_LOGGING(sync::steady_clock::time_point now = sync::steady_clock::now());
            HLOGC(qslog.Debug, log << "SND updated to " << FormatTime(next_send_time)
                    << " (now" << fmt((next_send_time - now).count(), showpos) << "us)");
        }
        else
        {
            sched.remove(runner);
        }

        HLOGC(qslog.Debug, log << CONID() << "chn:SENDING: " << pkt.Info());
        m_pChannel->sendto(addr, pkt, source_addr);
    }

    THREAD_EXIT();
}

// This is to satisfy the requirement of HeapSet class.
// The values kept in HeapSet must be capable of a trap representation
// to be returned from none(). Here it's returned as empty_list.end().
SocketHolder::socklist_t SocketHolder::empty_list;

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

#if HVU_ENABLE_HEAVY_LOGGING
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

        // NOTE: A socket that is broken and on the way for deletion shall
        // be at first removed from the queue dependencies and not present here.

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
                  log << "RID: socket @" << i->m_iID << " still active (remaining "
                      << fmt(count_microseconds(i->m_tsTTL - tsNow) / 1000000.0, fixed) << "s of TTL)...");
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
    m_RcvQueue.init(128, payload_size, m_pChannel);
}

void CMultiplexer::removeSender(CUDT* u)
{
    SocketHolder::sockiter_t pos = u->m_MuxNode;
    if (pos == SocketHolder::none())
        return;

    // This removes the socket from the Send Order List, but
    // not from the multiplexrer (that is, it will be readded, if
    // there's an API sending function called).
    m_SndQueue.m_SendOrderList.remove(pos);
}

//
CRcvQueue::CRcvQueue(CMultiplexer* parent):
    m_parent(parent),
    m_WorkerThread(),
    m_pUnitQueue(NULL),
    m_pChannel(NULL),
    m_szPayloadSize(),
    m_bClosing(false),
    m_mBuffer(),
    m_BufferCond()
{
    setupCond(m_BufferCond, "QueueBuffer");
}

void CRcvQueue::stop()
{
    m_bClosing = true;

    // It is allowed that the queue stops itself. It should just not try
    // to join itself.
    if (sync::this_thread_is(m_WorkerThread))
    {
        LOGC(rslog.Error, log << "RcvQueue: IPE: STOP REQUEST called from within worker thread - NOT EXITING.");
        return;
    }

    if (m_WorkerThread.joinable())
    {
        HLOGC(rslog.Debug, log << "RcvQueue: EXITing thread...");
        m_WorkerThread.join();
    }
    releaseCond(m_BufferCond);

    HLOGC(rslog.Debug, log << "RcvQueue: STOPPED.");
}

CRcvQueue::~CRcvQueue()
{
    stop();
    delete m_pUnitQueue;

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

void srt::CRcvQueue::resetAtFork()
{
    resetThread(&m_WorkerThread);
}

#if HVU_ENABLE_LOGGING
sync::atomic<int> CRcvQueue::m_counter(0);
#endif

void CRcvQueue::init(int qsize, size_t payload, CChannel* cc)
{
    m_szPayloadSize = payload;

    SRT_ASSERT(m_pUnitQueue == NULL);
    m_pUnitQueue = new CUnitQueue(qsize, (int)payload);


    m_pChannel = cc;

#if HVU_ENABLE_LOGGING
    const int cnt = ++m_counter;
    const string thrname = fmtcat("SRT:RcvQ:w", cnt);
#else
    const string thrname = "SRT:RcvQ:w";
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
    SRTSOCKET id = SRT_SOCKID_CONNREQ;

    string thname;
    ThreadName::get(thname);
    THREAD_STATE_INIT(thname.c_str());

    CUnit*         unit = 0;
    EConnectStatus cst  = CONN_AGAIN;
    sockaddr_any sa(m_parent->selfAddr().family());
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
                break;
            }
            else
            {
                LOGC(qrlog.Fatal,
                     log << CONID()
                         << "CChannel reported ERROR DURING TRANSMISSION - IPE. NOT INTERRUPTING the worker until it's explicitly closed.");

                // Issue #3185, blocking: -- break;
                // "break" should never be used because it causes worker thread to exit,
                // while this shall never be done, unless the multiplexer is broken and requested to exit.
            }
            cst = CONN_REJECT;
        }
        // OTHERWISE: this is an "AGAIN" situation. No data was read, but the process should continue.

        // take care of the timing event for all UDT sockets
        const steady_clock::time_point curtime_minus_syn =
            steady_clock::now() - microseconds_from(CUDT::COMM_SYN_INTERVAL_US);

        m_parent->rollUpdateSockets(curtime_minus_syn);

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
        //
        // NOTE: CONN_REJECT may be entering here, but it will be treated like CONN_AGAIN.
        updateConnStatus(rst, cst, unit);

        // XXX updateConnStatus may have removed the connector from the list,
        // however there's still m_mBuffer in CRcvQueue for that socket to care about.
    }

    HLOGC(qrlog.Debug, log << "worker: EXIT");

    THREAD_EXIT();
}

EReadStatus CRcvQueue::worker_RetrieveUnit(SRTSOCKET& w_id, CUnit*& w_unit, sockaddr_any& w_addr)
{

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
        CUDT*      pListener = m_pListener.get_locked(shl);

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
        hs.m_extensionType = SRT_CMD_HSRSP;

    // Ok, at last we have a peer ID info
    SRTSOCKET peerid = hs.m_iID;

    // Now search for a socket that has this peer ID
    CUDTSocket* s = m_parent->findPeer(peerid, addr, m_parent->ACQ_ACQUIRE);
    if (!s)
    {
        HLOGC(cnlog.Debug, log << "worker_TryAcceptedSocket: can't find accepted socket for peer -@" << peerid
                               << " and address: " << addr.str() << " - POSSIBLE ATTACK, rejecting");
        return false;
    }

    // Acquired in findPeer, so this can be now kept without acquiring m_GlobControlLock.
    CUDTUnited::SocketKeeper keep;
    keep.socket = s;

    CUDT* u = &s->core();
    if (u->m_bBroken || u->m_bClosing)
    {
        return false;
    }

    HLOGC(cnlog.Debug, log << "FOUND accepted socket @" << u->m_SocketID << " that is a peer for -@"
            << peerid << " - DISPATCHING to it to resend HS response");

    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t   kmdatasize = SRTDATA_MAXSIZE;
    if (u->craftKmResponse((kmdata), (kmdatasize)) != CONN_ACCEPT)
    {
        HLOGC(cnlog.Debug, log << "craftKmResponse: failed");
        return false;
    }

    return u->createSendHSResponse_WITHLOCK(kmdata, kmdatasize, pkt.udpDestAddr(), (hs));
}

EConnectStatus CRcvQueue::worker_ProcessAddressedPacket(SRTSOCKET id, CUnit* unit, const sockaddr_any& addr)
{
    SocketHolder::State hstate = SocketHolder::INIT;
    CUDTSocket* s = m_parent->findAgent(id, addr, (hstate), m_parent->ACQ_ACQUIRE);
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
    CUDTUnited::SocketKeeper sk;
    sk.socket = s; // Acquired by findAgent() call

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

    HLOGC(cnlog.Debug, log << "Dispatching a " << (unit->m_Packet.isControl() ? "CONTROL MESSAGE" : "DATA PACKET")
            << " to @" << id);
    if (unit->m_Packet.isControl())
        u->processCtrl(unit->m_Packet);
    else
        u->processData(unit);

    HLOGC(cnlog.Debug, log << "POST-DISPATCH update for @" << id);
    u->checkTimers();

    // XXX Optimize it better
    // The entry can't be modified without having the whole
    // function locked, as without locking you can't keep a reference
    // to the SocketHolder entry.
    // HINT: CUDT contains the mux node field, just need to check if it
    // can't be modified in the meantime, or have it locked for removal.
    m_parent->updateUpdateOrder(id, sync::steady_clock::now());

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

bool CRcvQueue::setListener(CUDT* u)
{
    return m_pListener.compare_exchange(NULL, u);
}

CUDT* CRcvQueue::getListener()
{
    SharedLock lkl (m_pListener);
    return m_pListener.get_locked(lkl);
}

// XXX NOTE: TSan reports here false positive against the call
// to locateSocket in CUDTUnited::newConnection. This here will apply
// exclusive lock on m_pListener, while keeping shared lock on
// CUDTUnited::m_GlobControlLock in CUDTUnited::closeAllSockets.
// As the other thread locks both as shared, this is no deadlock risk.
bool CRcvQueue::removeListener(CUDT* u)
{
    bool rem = m_pListener.compare_exchange(u, NULL);
    // DO NOT delete socket here. Just listener.
    return rem;
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
    if (!is_zero(m_UpdateOrder.time))
        out << " UP:" << FormatTime(m_UpdateOrder.time);
    if (!is_zero(m_SendOrder.time))
        out << " SN:" << FormatTime(m_SendOrder.time);

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
    s->core().m_MuxNode = last;
    ++m_zSockets;
    HLOGC(qmlog.Debug, log << "MUXER: id=" << m_iID << " added @" << s->core().m_SocketID << " (total of " << m_zSockets.load() << " sockets)");

#if SRT_ENABLE_THREAD_DEBUG
    last->addCondSanitizer(s->core().m_RcvTsbPdCond);
#endif

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

    m_UpdateOrderList.insert(steady_clock::now(), point);

    HLOGC(qmlog.Debug, log << "MUXER id=" << m_iID << ": connected: " << sh.report()
            << "UPDATE-LIST: pos=" << point->m_UpdateOrder.pos
            << " TIME:" << FormatTime(point->m_UpdateOrder.time) << " total "
            << m_UpdateOrderList.size() << " sockets");

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
    return setBrokenInternal(id);
}

bool CMultiplexer::setBrokenInternal(SRTSOCKET id)
{
    sockmap_t::iterator fo = m_SocketMap.find(id);
    if (fo == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "setBroken: MUXER id=" << m_iID << " NOT FOUND: @" << id);
        return false;
    }

    std::list<SocketHolder>::iterator point = fo->second;
    setBrokenDirect(point);
    return true;
}

void CMultiplexer::setBrokenDirect(sockiter_t point)
{
    m_RevPeerMap.erase(point->setBrokenPeer());

    HLOGC(qmlog.Debug, log << "setBroken: MUXER id=" << m_iID << " set to @" << point->id());
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

    // Remove from m_lRendezvousID (no longer valid after removal from here)
    for (list<CRL>::iterator i = m_lRendezvousID.begin(), i_next = i; i != m_lRendezvousID.end(); i = i_next)
    {
        // Safe iterator to the next element. If the current element is erased, the iterator is updated again.
        ++i_next;

        if (i->m_it == point)
            m_lRendezvousID.erase(i);
    }

    // Remove from the Update Lists, if present
    CUDTSocket* s = point->m_pSocket;

    // Remove from maps and list
    m_UpdateOrderList.erase(point);
    m_SndQueue.m_SendOrderList.remove(point);

    HLOGC(qmlog.Debug, log << "UPDATE-LIST: removed @" << id << " per removal from muxer");

    s->core().m_MuxNode = SocketHolder::none(); // rewrite before it becomes invalid
    m_RevPeerMap.erase(point->peerID());
    m_SocketMap.erase(id); // fo is no longer valid!
    m_Sockets.erase(point);
    --m_zSockets;
    HLOGC(qmlog.Debug, log << "deleteSocket: MUXER id=" << m_iID << " removed @" << id << " (remaining " << m_zSockets << ")");
    return true;
}

/// Find a mapped CUDTSocket whose id is @a id.
CUDTSocket* CMultiplexer::findAgent(SRTSOCKET id, const sockaddr_any& remote_addr,
        SocketHolder::State& w_state, AcquisitionControl acq)
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
    if (acq == ACQ_ACQUIRE)
        point->m_pSocket->apiAcquire();
    return point->m_pSocket;
}

string SocketHolder::MatchStr(SocketHolder::MatchState ms)
{
    static const string table [] = {
        "OK",
        "STATE",
        "ADDRESS",
        "DATA"
    };
    return table[int(ms)];
}

/// Find a mapped CUDTSocket for whom the peer ID has
/// been assigned as @a rid.
CUDTSocket* CMultiplexer::findPeer(SRTSOCKET rid, const sockaddr_any& remote_addr, AcquisitionControl acq)
{
    if (!m_zSockets)
    {
        HLOGC(qmlog.Debug, log << "findPeer: MUXER id=" << m_iID << " no sockets while looking for -@" << rid);
        return NULL;
    }

    sync::ScopedLock lk (m_SocketsLock);

    std::map<SRTSOCKET, SRTSOCKET>::iterator rfo = m_RevPeerMap.find(rid);
    if (rfo == m_RevPeerMap.end())
    {
        HLOGC(qmlog.Debug, log << "findPeer: MUXER id=" << m_iID << " -@" << rid << " not found in rev map");
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

    if (acq == ACQ_ACQUIRE)
        point->m_pSocket->apiAcquire();

    return point->m_pSocket;
}

steady_clock::time_point CMultiplexer::updateSendNormal(CUDTSocket* s)
{
    const steady_clock::time_point currtime = steady_clock::now();
    bool updated SRT_ATR_UNUSED =
        m_SndQueue.m_SendOrderList.update(s->core().m_MuxNode, SocketHolder::DONT_RESCHEDULE, currtime);
    HLOGC(qslog.Debug, log << s->core().CONID() << "NORMAL update: " << (updated ? "" : "NOT ")
            << "updated to " << FormatTime(currtime));
    return currtime;
}

void CMultiplexer::updateSendFast(CUDTSocket* s)
{
    steady_clock::duration immediate = milliseconds_from(1);
    steady_clock::time_point yesterday = steady_clock::time_point(immediate);
    bool updated SRT_ATR_UNUSED =
        m_SndQueue.m_SendOrderList.update(s->core().m_MuxNode, SocketHolder::DO_RESCHEDULE, yesterday);
    HLOGC(qslog.Debug, log << s->core().CONID() << "FAST update: " << (updated ? "" : "NOT ")
            << "updated");
}


void CMultiplexer::setReceiver(CUDT* u)
{
    SRT_ASSERT_AFFINITY(m_RcvQueue.m_WorkerThread.get_id());
    SRT_ASSERT(u->m_bOpened);

    HLOGC(qrlog.Debug, log << u->CONID() << " SOCKET pending for connection - ADDING TO RCV QUEUE/MAP (directly)");
    setConnected(u->m_SocketID);
    // Register in updates -- done in setConnected!
}

void CMultiplexer::updateUpdateOrder(SRTSOCKET id, const sync::steady_clock::time_point& tnow)
{
    if (!m_zSockets)
    {
        LOGC(qmlog.Error, log << "updateUpdateOrder: MUXER id=" << m_iID << " no sockets while looking for @" << id);
        return;
    }

    sync::ScopedLock lk (m_SocketsLock);

    sockmap_t::iterator fo = m_SocketMap.find(id);
    if (fo == m_SocketMap.end())
    {
        LOGC(qmlog.Error, log << "updateUpdateOrder: MUXER id=" << m_iID << " no socket @" << id);
        return;
    }

    std::list<SocketHolder>::iterator point = fo->second;
    if (point->m_UpdateOrder.pos == m_UpdateOrderList.npos)
    {
        // Weird, but don't add it either.
        HLOGC(qmlog.Error, log << "UPDATE-LIST: updateUpdateOrder: @" << id << " is NOT in the update list - NOT ADDING");
        return;
    }

    m_UpdateOrderList.update(point->m_UpdateOrder.pos, tnow);
    HLOGC(qmlog.Debug, log << "UPDATE-LIST: @" << id << " pos=" << point->m_UpdateOrder.pos
            << " updated to time " << FormatTime(point->m_UpdateOrder.time));
}

void CMultiplexer::rollUpdateSockets(const sync::steady_clock::time_point& curtime_minus_syn)
{
    sync::steady_clock::time_point tnow = sync::steady_clock::now();

    vector<CUDTSocket*> sockets_to_update;
    {
        sync::ScopedLock lk (m_SocketsLock);
        if (m_UpdateOrderList.empty())
        {
            return;
        }

        for (;;)
        {
            // Guaranteed at least one element, so top() is valid.
            sockiter_t point = m_UpdateOrderList.top();
            if (point != m_UpdateOrderList.none() && point->m_UpdateOrder.time < curtime_minus_syn)
            {
                HLOGC(qmlog.Debug, log << "UPDATE-LIST: roll: got @" << point->id() << " due in "
                        << FormatDuration<DUNIT_US>(curtime_minus_syn - point->m_UpdateOrder.time));
                // PASS
            }
            else
            {
                HLOGC(qmlog.Debug, log << "UPDATE-LIST: roll: no more past-time sockets (remain " << m_UpdateOrderList.size() << " future sockets)");
                break;
            }

            CUDT* u = &point->m_pSocket->core();

            if (u->m_bConnected && !u->m_bBroken && !u->m_bClosing)
            {
                // Lock the sockets being collected here to prevent unexpected deletion
                // SYMMETRY is ensured by adding them to this container.
                point->m_pSocket->apiAcquire();
                sockets_to_update.push_back(point->m_pSocket);

                // Now reinsert the item with the new time.
                m_UpdateOrderList.update(point->m_UpdateOrder.pos, tnow);

                HLOGC(qmlog.Debug, log << "UPDATE-LIST: reinserted @" << u->id() << " pos=" << point->m_UpdateOrder.pos
                        << " TIME:" << FormatTime(point->m_UpdateOrder.time) << " total "
                        << m_UpdateOrderList.size() << " update ordered sockets");
            }
            else
            {
                HLOGC(qrlog.Debug, log << CUDTUnited::CONID(u->m_SocketID)
                        << " UPDATE-LIST: SOCKET broken, removing from the list.");
                m_UpdateOrderList.pop();
                // the socket must be removed from Hash table first, then RcvUList

                // We should NOT let the m_SocketsLock be locked, and simultaneously
                // we know that the socket is there, so we don't need to pre-check the size.
                setBrokenInternal(u->m_SocketID);
                // Do nothing more. The socket is removed from update list,
                // so just do not reinsert it.
            }
        }
    }

    // Run the update outside the lock of m_SocketsLock. Some underlying
    // activities may need to lock m_GlobControlLock, so we need this fragment
    // to be lock-free. Instead, we have applied busy-lock, which will be also
    // freed here once we are done with the handling.
    for (size_t i = 0; i < sockets_to_update.size(); ++i)
    {
        CUDTSocket* s = sockets_to_update[i];
        s->core().checkTimers();
        s->apiRelease();
    }
}

bool CMultiplexer::tryCloseIfEmpty()
{
    if (!empty())
        return false;

    if (m_pChannel)
        m_pChannel->close();

    // CONSIDER - but this field is inter-thread with no mutex
    // m_SelfAddr.reset();
    return true;
}

void srt::CMultiplexer::resetAtFork()
{
    m_RcvQueue.resetAtFork();
    m_SndQueue.resetAtFork();
}

bool CMultiplexer::reserveDisposal()
{
    if (m_ReservedDisposal != CThread::id())
    {
        // Already reserved
        return false;
    }

    m_ReservedDisposal = sync::this_thread::get_id();
    return true;
}

CMultiplexer::~CMultiplexer()
{
    // Reverse order of the assigned.
    stop();
    close();
}

void CMultiplexer::close()
{
    if (m_pChannel)
    {
        m_pChannel->close();
        delete m_pChannel;
        m_pChannel = NULL;
    }
}

void srt::CMultiplexer::stop()
{
    m_RcvQueue.stop();
    m_SndQueue.stop();
}

string CMultiplexer::testAllSocketsClear()
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

string SocketHolder::StateStr(SocketHolder::State st)
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

