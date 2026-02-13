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
   Yunhong Gu, last updated 07/09/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <exception>
#include <stdexcept>
#include <typeinfo>
#include <iterator>
#include <vector>

#include <cstring>
#include "utilities.h"
#include "netinet_any.h"
#include "api.h"
#include "core.h"
#include "epoll.h"
#include "logging.h"
#include "hvu_compat.h"
#include "srt.h"

#ifdef _WIN32
#include <win/wintime.h>
#endif

#ifdef _MSC_VER
#pragma warning(error : 4530)
#endif

using namespace std;
using namespace srt::logging;
using namespace srt::sync;

namespace srt
{

void CUDTSocket::construct()
{
#if SRT_ENABLE_BONDING
    m_GroupOf         = NULL;
    m_GroupMemberData = NULL;
#endif
    setupMutex(m_AcceptLock, "Accept");
    setupCond(m_AcceptCond, "Accept");
    setupMutex(m_ControlLock, "Control");
}

CUDTSocket::~CUDTSocket()
{
    releaseMutex(m_AcceptLock);
    releaseCond(m_AcceptCond);
    releaseMutex(m_ControlLock);
}

int CUDTSocket::apiAcquire()
{
    ++m_iBusy;
    HLOGC(smlog.Debug, log << "@" << id() << " ACQUIRE; BUSY=" << int(m_iBusy) << " {");
    return m_iBusy;
}

int CUDTSocket::apiRelease()
{
    --m_iBusy;
    HLOGC(smlog.Debug, log << "@" << id() << " RELEASE; BUSY=" << int(m_iBusy) << " }");
    return m_iBusy;
}

void CUDTSocket::resetAtFork()
{
    m_UDT.resetAtFork();
    resetCond(m_AcceptCond);
}

SRT_TSA_DISABLED // Uses m_Status that should be guarded, but for reading it is enough to be atomic
SRT_SOCKSTATUS CUDTSocket::getStatus()
{
    // TTL in CRendezvousQueue::updateConnStatus() will set m_bConnecting to false.
    // Although m_Status is still SRTS_CONNECTING, the connection is in fact to be closed due to TTL expiry.
    // In this case m_bConnected is also false. Both checks are required to avoid hitting
    // a regular state transition from CONNECTING to CONNECTED.

    if (m_UDT.m_bBroken)
        return SRTS_BROKEN;

    // Connecting timed out
    if ((m_Status == SRTS_CONNECTING) && !m_UDT.m_bConnecting && !m_UDT.m_bConnected)
        return SRTS_BROKEN;

    return m_Status;
}

// [[using locked(m_GlobControlLock)]]
void CUDTSocket::breakSocket_LOCKED(int reason)
{
    // This function is intended to be called from GC,
    // under a lock of m_GlobControlLock.
    m_UDT.m_bBroken        = true;

    // SET THIS to true because this function is called always for a socket
    // that will never have any chance in the future to be manually closed.
    m_UDT.m_bManaged       = true;
    m_UDT.m_iBrokenCounter = 0;
    HLOGC(smlog.Debug, log << "@" << m_UDT.m_SocketID << " CLOSING AS SOCKET");
    m_UDT.closeEntity(reason);
    setClosed();
}

SRT_TSA_DISABLED // Uses m_Status that should be guarded, but for reading it is enough to be atomic
void CUDTSocket::setClosed()
{
    m_Status = SRTS_CLOSED;

    // a socket will not be immediately removed when it is closed
    // in order to prevent other methods from accessing invalid address
    // a timer is started and the socket will be removed after approximately
    // 1 second
    m_tsClosureTimeStamp = steady_clock::now();
}

void CUDTSocket::setBrokenClosed()
{
    m_UDT.m_iBrokenCounter = 60;
    m_UDT.m_bBroken        = true;
    setClosed();
}

bool CUDTSocket::readReady() const
{
#if SRT_ENABLE_BONDING

    // If this is a group member socket, then reading happens exclusively from
    // the group and the socket is only used as a connection point, packet
    // dispatching and single link management. Data buffering and hence ability
    // to deliver a packet through API is exclusively the matter of group,
    // therefore a single socket is never "read ready".

    if (m_GroupOf)
        return false;
#endif
    if (m_UDT.m_bConnected && m_UDT.isRcvBufferReady())
        return true;

    if (m_UDT.m_bListening)
        return !m_QueuedSockets.empty();

    return broken();
}

bool CUDTSocket::writeReady() const
{
    return (m_UDT.m_bConnected && (m_UDT.m_pSndBuffer->getCurrBufSize() < m_UDT.m_config.iSndBufSize)) || broken();
}

bool CUDTSocket::broken() const
{
    return m_UDT.m_bBroken || !m_UDT.m_bConnected;
}

////////////////////////////////////////////////////////////////////////////////

CUDTUnited::CUDTUnited()
    : m_Sockets()
    , m_GlobControlLock()
    , m_IDLock()
    , m_mMultiplexer()
    , m_pCache(new CCache<CInfoBlock>)
    , m_bGCClosing(false)
    , m_GCStopCond()
    , m_InitLock()
    , m_iInstanceCount(0)
    , m_bGCStatus(false)
{
    // Socket ID MUST start from a random value
    m_SocketIDGenerator      = genRandomInt(1, MAX_SOCKET_VAL);
    m_SocketIDGenerator_init = m_SocketIDGenerator;

    // XXX An unlikely exception thrown from the below calls
    // might destroy the application before `main`. This shouldn't
    // be a problem in general.
    setupMutex(m_GCStartLock, "GCStart");
    setupMutex(m_GCStopLock, "GCStop");
    setupCond(m_GCStopCond, "GCStop");
    setupMutex(m_GlobControlLock, "GlobControl");
    setupMutex(m_IDLock, "ID");
    setupMutex(m_InitLock, "Init");
    // Global initialization code
#ifdef _WIN32
    WORD    wVersionRequested;
    WSADATA wsaData;
    wVersionRequested = MAKEWORD(2, 2);

    if (0 != WSAStartup(wVersionRequested, &wsaData))
        throw CUDTException(MJ_SETUP, MN_NONE, WSAGetLastError());
#endif
    CCryptoControl::globalInit();
    HLOGC(inlog.Debug, log << "SRT Clock Type: " << SRT_SYNC_CLOCK_STR);
}

CUDTUnited::~CUDTUnited()
{
    // Call it if it wasn't called already.
    // This will happen at the end of main() of the application,
    // when the user didn't call srt_cleanup().
    enterCS(m_InitLock);
    stopGarbageCollector();
    leaveCS(m_InitLock);
    closeAllSockets();
    releaseMutex(m_GlobControlLock);
    releaseMutex(m_IDLock);
    releaseMutex(m_InitLock);
    // XXX There's some weird bug here causing this
    // to hangup on Windows. This might be either something
    // bigger, or some problem in pthread-win32. As this is
    // the application cleanup section, this can be temporarily
    // tolerated with simply exit the application without cleanup,
    // counting on that the system will take care of it anyway.
#ifndef _WIN32
    releaseCond(m_GCStopCond);
#endif
    releaseMutex(m_GCStopLock);
    releaseMutex(m_GCStartLock);
    delete m_pCache;
#ifdef _WIN32
    WSACleanup();
#endif
}

string CUDTUnited::CONID(SRTSOCKET sock)
{
    if (int32_t(sock) <= 0) // embraces SRT_INVALID_SOCK, SRT_SOCKID_CONNREQ and illegal negative domain
        return "";

    return hvu::fmtcat("@", int(sock), ":");
}

bool CUDTUnited::startGarbageCollector()
{

    ScopedLock guard(m_GCStartLock);
    if (!m_bGCStatus)
    {
        m_bGCClosing = false;
        m_bGCStatus = StartThread(m_GCThread, garbageCollect, this, "SRT:GC");
    }
    return m_bGCStatus;
}

void CUDTUnited::stopGarbageCollector()
{

    ScopedLock guard(m_GCStartLock);
    if (m_bGCStatus)
    {
        m_bGCStatus = false;
        {
            CUniqueSync gclock (m_GCStopLock, m_GCStopCond);
            m_bGCClosing = true;
            gclock.notify_all();
        }
        m_GCThread.join();
    }
}

void CUDTUnited::cleanupAllSockets()
{
    for (sockets_t::iterator i = m_Sockets.begin(); i != m_Sockets.end(); ++i)
    {
        CUDTSocket* s = i->second;

#if SRT_ENABLE_BONDING
        if (s->m_GroupOf)
        {
            s->removeFromGroup(false);
        }
#endif

        // remove from listener's queue
        sockets_t::iterator ls = m_Sockets.find(s->m_ListenSocket);
        if (ls == m_Sockets.end())
        {
            ls = m_ClosedSockets.find(s->m_ListenSocket);
        }
        if (ls != m_ClosedSockets.end())
        {
            ls->second->m_QueuedSockets.erase(s->id());
        }
        s->core().closeAtFork();
        s->resetAtFork();
        delete(s);
    }
    m_Sockets.clear();

#if SRT_ENABLE_BONDING
    for (groups_t::iterator j = m_Groups.begin(); j != m_Groups.end(); ++j)
    {
        delete j->second;
    }
    m_Groups.clear();
#endif
    for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin(); i != m_mMultiplexer.end(); ++i)
    {
        CMultiplexer &multiplexer = i->second;
        multiplexer.resetAtFork();
    }
    m_mMultiplexer.clear();
}

void CUDTUnited::closeAllSockets()
{
    // remove all sockets and multiplexers
    HLOGC(inlog.Debug, log << "GC: GLOBAL EXIT - releasing all pending sockets. Acquiring control lock...");

    {
        // Pre-closing: run over all open sockets and close them.
        SharedLock glock(m_GlobControlLock);

        for (sockets_t::iterator i = m_Sockets.begin(); i != m_Sockets.end(); ++i)
        {
            CUDTSocket* s = i->second;
            s->breakSocket_LOCKED(SRT_CLS_CLEANUP);

#if SRT_ENABLE_BONDING
            if (s->m_GroupOf)
            {
                HLOGC(smlog.Debug,
                      log << "@" << s->id() << " IS MEMBER OF $" << s->m_GroupOf->id()
                          << " (IPE?) - REMOVING FROM GROUP");
                s->removeFromGroup(false);
            }
#endif
        }
    }

    {
        ExclusiveLock glock(m_GlobControlLock);

        // Do not do generative expiry removal - there's no chance
        // anyone can extract the close reason information since this point on.
        m_ClosedDatabase.clear();

        for (sockets_t::iterator i = m_Sockets.begin(); i != m_Sockets.end(); ++i)
        {
            CUDTSocket* s = i->second;

            // NOTE: not removing the socket from m_Sockets.
            // This is a loop over m_Sockets and after this loop ends,
            // this whole container will be cleared.
            swipeSocket_LOCKED(i->first, s, SWIPE_LATER);

            if (s->m_ListenSocket != SRT_SOCKID_CONNREQ)
            {
                // remove from listener's queue
                sockets_t::iterator ls = m_Sockets.find(s->m_ListenSocket);
                if (ls == m_Sockets.end())
                {
                    ls = m_ClosedSockets.find(s->m_ListenSocket);
                    if (ls == m_ClosedSockets.end())
                        continue;
                }

                HLOGC(smlog.Debug, log << "@" << s->id() << " removed from queued sockets of listener @" << ls->second->id());
                enterCS(ls->second->m_AcceptLock);
                ls->second->m_QueuedSockets.erase(s->id());
                leaveCS(ls->second->m_AcceptLock);
            }
        }
        m_Sockets.clear();

        for (sockets_t::iterator j = m_ClosedSockets.begin(); j != m_ClosedSockets.end(); ++j)
        {
            j->second->m_tsClosureTimeStamp = steady_clock::time_point();
        }

#if SRT_ENABLE_BONDING
        for (groups_t::iterator j = m_Groups.begin(); j != m_Groups.end(); ++j)
        {
            SRTSOCKET id = j->second->m_GroupID;
            m_ClosedGroups[id] = j->second;
        }
        m_Groups.clear();
#endif
    }

    HLOGC(inlog.Debug, log << "GC: GLOBAL EXIT - releasing all CLOSED sockets.");
    while (true)
    {
        checkBrokenSockets();

        enterCS(m_GlobControlLock);
        bool empty = m_ClosedSockets.empty();
        size_t remmuxer = m_mMultiplexer.size();
#if HVU_ENABLE_HEAVY_LOGGING
        ostringstream om;
        if (remmuxer)
        {
            om << "[";
            for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin(); i != m_mMultiplexer.end(); ++i)
                om << " " << i->first;
            om << " ]";

        }
#endif
        leaveCS(m_GlobControlLock);

        if (empty && remmuxer == 0)
            break;


        HLOGC(inlog.Debug, log << "GC: checkBrokenSockets didn't wipe all sockets or muxers="
                << remmuxer << om.str() << ", repeating after 0.1s sleep");
        sync::this_thread::sleep_for(milliseconds_from(100));
    }


}


SRTRUNSTATUS CUDTUnited::startup()
{
    ScopedLock gcinit(m_InitLock);
    m_iInstanceCount++;
    if (m_bGCStatus)
        return (m_iInstanceCount == 1) ? SRT_RUN_ALREADY : SRT_RUN_OK;
    else
        return startGarbageCollector() ? SRT_RUN_OK : SRT_RUN_ERROR; 
}

int CUDTUnited::cleanupAtFork()
{
    cleanupAllSockets();
    resetThread(&m_GCThread);
    resetCond(m_GCStopCond);
    m_GCStopLock.unlock();
    setupCond(m_GCStopCond, "GCStop");
    m_iInstanceCount=0;
    m_bGCStatus = false;
    return 0;
}

SRTSTATUS CUDTUnited::cleanup()
{
    // IMPORTANT!!!
    // In this function there must be NO LOGGING AT ALL.  This function may
    // potentially be called from within the global program destructor, and
    // therefore some of the facilities used by the logging system - including
    // the default std::cerr object bound to it by default, but also a different
    // stream that the user's app has bound to it, and which got destroyed
    // together with already exited main() - may be already deleted when
    // executing this procedure.
    ScopedLock gcinit(m_InitLock);

    if (--m_iInstanceCount > 0)
        return SRT_STATUS_OK;

    stopGarbageCollector();
    closeAllSockets();
    return SRT_STATUS_OK;
}

SRTSOCKET CUDTUnited::generateSocketID(bool for_group)
{
    ScopedLock guard(m_IDLock);

    int sockval = m_SocketIDGenerator - 1;

    // First problem: zero-value should be avoided by various reasons.

    if (sockval <= 0)
    {
        // We have a rollover on the socket value, so
        // definitely we haven't made the Columbus mistake yet.
        m_SocketIDGenerator = MAX_SOCKET_VAL;
        sockval = MAX_SOCKET_VAL;
    }

    // Check all sockets if any of them has this value.
    // Socket IDs are begin created this way:
    //
    //                              Initial random
    //                              |
    //                             |
    //                            |
    //                           |
    // ...
    // The only problem might be if the number rolls over
    // and reaches the same value from the opposite side.
    // This is still a valid socket value, but this time
    // we have to check, which sockets have been used already.
    if (sockval == m_SocketIDGenerator_init)
    {
        // Mark that since this point on the checks for
        // whether the socket ID is in use must be done.
        m_SocketIDGenerator_init = 0;
    }

    // This is when all socket numbers have been already used once.
    // This may happen after many years of running an application
    // constantly when the connection breaks and gets restored often.
    if (m_SocketIDGenerator_init == 0)
    {
        int startval = sockval;
        for (;;) // Roll until an unused value is found
        {
            enterCS(m_GlobControlLock);
            const bool exists =
#if SRT_ENABLE_BONDING
                for_group
                ? m_Groups.count(SRTSOCKET(sockval | SRTGROUP_MASK))
                :
#endif
                m_Sockets.count(SRTSOCKET(sockval));
            leaveCS(m_GlobControlLock);

            if (exists)
            {
                // The socket value is in use.
                --sockval;
                if (sockval <= 0)
                    sockval = MAX_SOCKET_VAL;

                // Before continuing, check if we haven't rolled back to start again
                // This is virtually impossible, so just make an RTI error.
                if (sockval == startval)
                {
                    // Of course, we don't lack memory, but actually this is so impossible
                    // that a complete memory extinction is much more possible than this.
                    // So treat this rather as a formal fallback for something that "should
                    // never happen". This should make the socket creation functions, from
                    // socket_create and accept, return this error.

                    m_SocketIDGenerator = sockval + 1; // so that any next call will cause the same error
                    throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
                }

                // try again, if this is a free socket
                continue;
            }

            // No socket found, this ID is free to use
            m_SocketIDGenerator = sockval;
            break;
        }
    }
    else
    {
        m_SocketIDGenerator = sockval;
    }

    // The socket value counter remains with the value rolled
    // without the group bit set; only the returned value may have
    // the group bit set.

    if (for_group)
        sockval = m_SocketIDGenerator | SRTGROUP_MASK;
    else
        sockval = m_SocketIDGenerator;

    LOGC(smlog.Debug, log << "generateSocketID: " << (for_group ? "(group)" : "") << ": @" << sockval);

    return SRTSOCKET(sockval);
}

SRTSOCKET CUDTUnited::newSocket(CUDTSocket** pps, bool managed)
{
    // XXX consider using some replacement of std::unique_ptr
    // so that exceptions will clean up the object without the
    // need for a dedicated code.
    CUDTSocket* ns = NULL;

    try
    {
        ns = new CUDTSocket;
    }
    catch (...)
    {
        delete ns;
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }

    try
    {
        ns->core().m_SocketID = generateSocketID();
    }
    catch (...)
    {
        delete ns;
        throw;
    }
    ns->m_Status          = SRTS_INIT;
    ns->m_ListenSocket    = SRT_SOCKID_CONNREQ; // A value used for socket if it wasn't listener-spawned
    ns->core().m_pCache   = m_pCache;
    ns->core().m_bManaged = managed;

    try
    {
        HLOGC(smlog.Debug, log << CONID(ns->id()) << "newSocket: mapping socket " << ns->id());

        // protect the m_Sockets structure.
        ExclusiveLock cs(m_GlobControlLock);
        m_Sockets[ns->id()] = ns;
    }
    catch (...)
    {
        // failure and rollback
        delete ns;
        ns = NULL;
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }

    {
        ScopedLock glk (m_InitLock);
        startGarbageCollector();
    }
    if (pps)
        *pps = ns;

    return ns->id();
}

// [[using locked(m_GlobControlLock)]]
void CUDTUnited::swipeSocket_LOCKED(SRTSOCKET id, CUDTSocket* s, CUDTUnited::SwipeSocketTerm lateremove)
{
    m_ClosedSockets[id] = s;
    if (!lateremove)
    {
        m_Sockets.erase(id);
    }
}

// XXX NOTE: TSan reports here false positive against the call
// to CRcvQueue::removeListener. This here will apply shared
// lock on m_GlobControlLock in the call of locateSocket, while
// having applied a shared lock on CRcvQueue::m_pListener in
// CRcvQueue::worker_ProcessConnectionRequest. As this thread
// locks both mutexes as shared, it doesn't form a deadlock.
int CUDTUnited::newConnection(const SRTSOCKET     listener,
                                   const sockaddr_any& peer,
                                   const CPacket&      hspkt,
                                   CHandShake&         w_hs,
                                   int&                w_error,
                                   CUDT*&              w_acpu)
{
    CUDTSocket* ns = NULL;
    w_acpu         = NULL;

    w_error = SRT_REJ_IPE;

    // Can't manage this error through an exception because this is
    // running in the listener loop.
    CUDTSocket* ls = locateSocket(listener);
    if (!ls)
    {
        LOGC(cnlog.Error, log << "IPE: newConnection by listener socket id=" << listener << " which DOES NOT EXIST.");
        return -1;
    }

    HLOGC(cnlog.Debug,
          log << "newConnection: creating new socket after listener @" << listener
              << " contacted with backlog=" << ls->m_uiBackLog);

    // if this connection has already been processed
    if ((ns = locatePeer(peer, w_hs.m_iID, w_hs.m_iISN)) != NULL)
    {
        if (ns->core().m_bBroken)
        {
            // last connection from the "peer" address has been broken
            ns->setClosed();
            HLOGC(cnlog.Debug, log << "newConnection: @" << ns->id() << " broken - deleting from queued");

            ScopedLock acceptcg(ls->m_AcceptLock);
            ls->m_QueuedSockets.erase(ns->id());
        }
        else
        {
            // connection already exist, this is a repeated connection request
            // respond with existing HS information
            HLOGC(cnlog.Debug, log << "newConnection: located a WORKING peer @" << w_hs.m_iID << " - ADAPTING.");

            w_hs.m_iISN            = ns->core().m_iISN;
            w_hs.m_iMSS            = ns->core().MSS();
            w_hs.m_iFlightFlagSize = ns->core().m_config.iFlightFlagSize;
            w_hs.m_iReqType        = URQ_CONCLUSION;
            w_hs.m_iID             = ns->id();

            // Report the original UDT because it will be
            // required to complete the HS data for conclusion response.
            w_acpu = &ns->core();

            return 0;

            // except for this situation a new connection should be started
        }
    }
    else
    {
        HLOGC(cnlog.Debug,
              log << "newConnection: NOT located any peer @" << w_hs.m_iID << " - resuming with initial connection.");
    }

    // exceeding backlog, refuse the connection request

    enterCS(ls->m_AcceptLock);
    size_t backlog = ls->m_QueuedSockets.size();
    leaveCS(ls->m_AcceptLock);
    if (backlog >= ls->m_uiBackLog)
    {
        w_error = SRT_REJ_BACKLOG;
        LOGC(cnlog.Note, log << "newConnection: listen backlog=" << ls->m_uiBackLog << " EXCEEDED");
        return -1;
    }

    try
    {
        // Protect the config of the listener socket from a data race.
        ScopedLock lck(ls->core().m_ConnectionLock);
        ns = new CUDTSocket(*ls);
        // No need to check the peer, this is the address from which the request has come.
        ns->m_PeerAddr = peer;
    }
    catch (...)
    {
        w_error = SRT_REJ_RESOURCE;
        delete ns;
        LOGC(cnlog.Error, log << "IPE: newConnection: unexpected exception (probably std::bad_alloc)");
        return -1;
    }

    ns->core().m_RejectReason = SRT_REJ_UNKNOWN; // pre-set a universal value

    try
    {
        ns->core().m_SocketID = generateSocketID();
    }
    catch (const CUDTException&)
    {
        LOGC(cnlog.Fatal, log << "newConnection: IPE: all sockets occupied? Last gen=" << m_SocketIDGenerator);
        // generateSocketID throws exception, which can be naturally handled
        // when the call is derived from the API call, but here it's called
        // internally in response to receiving a handshake. It must be handled
        // here and turned into an erroneous return value.
        delete ns;
        return -1;
    }

    ns->m_ListenSocket    = listener;
    ns->core().m_PeerID          = w_hs.m_iID;
    ns->m_iISN            = w_hs.m_iISN;

    HLOGC(cnlog.Debug,
          log << "newConnection: DATA: lsnid=" << listener << " id=" << ns->id()
              << " peerid=" << ns->core().m_PeerID << " ISN=" << ns->m_iISN);

    int  error                   = 0;
    bool should_submit_to_accept = true;

    // Set the error code for all prospective problems below.
    // It won't be interpreted when result was successful.
    w_error = SRT_REJ_RESOURCE;

    // These can throw exception only when the memory allocation failed.
    // CUDT::connect() translates exception into CUDTException.
    // CUDT::open() may only throw original std::bad_alloc from new.
    // This is only to make the library extra safe (when your machine lacks
    // memory, it will continue to work, but fail to accept connection).

    try
    {
        // This assignment must happen b4 the call to CUDT::connect() because
        // this call causes sending the SRT Handshake through this socket.
        // Without this mapping the socket cannot be found and therefore
        // the SRT Handshake message would fail.
        HLOGC(cnlog.Debug, log <<
                "newConnection: incoming " << peer.str() << ", mapping socket " << ns->id());
        {
            ExclusiveLock cg(m_GlobControlLock);
            m_Sockets[ns->id()] = ns;
        }

        if (ls->core().m_cbAcceptHook)
        {
            if (!ls->core().runAcceptHook(&ns->core(), peer.get(), w_hs, hspkt))
            {
                w_error = ns->core().m_RejectReason;

                error = 1;
                goto ERR_ROLLBACK;
            }
        }

        // bind to the same addr of listening socket
        ns->core().open();
        if (!updateListenerMux(ns, ls))
        {
            // This is highly unlikely if not impossible, but there's
            // a theoretical runtime chance of failure so it should be
            // handled
            ns->core().m_RejectReason = SRT_REJ_IPE;
            throw false; // let it jump directly into the omni exception handler
        }

        ns->core().acceptAndRespond(ls, peer, hspkt, (w_hs));
    }
    catch (...)
    {
        // Extract the error that was set in this new failed entity.
        w_error = ns->core().m_RejectReason;
        error   = 1;
        goto ERR_ROLLBACK;
    }

    ns->m_Status = SRTS_CONNECTED;

    // copy address information of local node
    // Precisely, what happens here is:
    // - Get the IP address and port from the system database
    ns->m_SelfAddr = ns->core().channel()->getSockAddr();
    // - OVERWRITE just the IP address itself by a value taken from piSelfIP
    // (the family is used exactly as the one taken from what has been returned
    // by getsockaddr)
    CIPAddress::decode(ns->core().m_piSelfIP, peer, (ns->m_SelfAddr));

    {
        // protect the m_PeerRec structure (and group existence)
        ExclusiveLock glock(m_GlobControlLock);
        try
        {
            HLOGC(cnlog.Debug, log << "newConnection: mapping peer " << ns->core().m_PeerID
                    << " to that socket (" << ns->id() << ")");
            m_PeerRec[ns->getPeerSpec()].insert(ns->id());

            LOGC(cnlog.Note, log << "@" << ns->id() << " connection on listener @" << listener
                << " (" << ns->m_SelfAddr.str() << ") from peer @" << ns->core().m_PeerID << " (" << peer.str() << ")");
        }
        catch (...)
        {
            LOGC(cnlog.Error, log << "newConnection: error when mapping peer!");
            error = 2;
        }

        // The access to m_GroupOf should be also protected, as the group
        // could be requested deletion in the meantime. This will hold any possible
        // removal from group and resetting m_GroupOf field.

#if SRT_ENABLE_BONDING
        if (ns->m_GroupOf)
        {
            // XXX this might require another check of group type.
            // For redundancy group, at least, update the status in the group
            CUDTGroup* g = ns->m_GroupOf;
            ScopedLock grlock(g->m_GroupLock);
            if (g->m_bClosing)
            {
                error = 1; // "INTERNAL REJECTION"
                goto ERR_ROLLBACK;
            }

            // Acceptance of the group will have to be done through accepting
            // of one of the pending sockets. There can be, however, multiple
            // such sockets at a time, some of them might get broken before
            // being accepted, and therefore we need to make all sockets ready.
            // But then, acceptance of a group may happen only once, so if any
            // sockets of the same group were submitted to accept, they must
            // be removed from the accept queue at this time.
            should_submit_to_accept = g->groupPending_LOCKED();

            // Ok, whether handled in the background, or reported through accept,
            // all group-member sockets should be managed.
            ns->core().m_bManaged = true;

            // Update the status in the group so that the next
            // operation can include the socket in the group operation.
            CUDTGroup::SocketData* gm = ns->m_GroupMemberData;

            HLOGC(cnlog.Debug,
                  log << "newConnection(GROUP): Socket @" << ns->id() << " BELONGS TO $" << g->id() << " - will "
                      << (should_submit_to_accept ? "" : "NOT ") << "report in accept");
            gm->sndstate   = SRT_GST_IDLE;
            gm->rcvstate   = SRT_GST_IDLE;
            gm->laststatus = SRTS_CONNECTED;

            g->setGroupConnected();
            // In the new recvbuffer mode (and common receiver buffer) there's no waiting for reception
            // on a socket and no reading from a socket directly is being done; instead the reading API
            // is directly bound to the group and reading happens directly from the group's buffer.
            // This includes also a situation of a newly connected socket, which will be delivering packets
            // into the same common receiver buffer for the group, so readable will be the group itself
            // when it has its own common buffer read-ready, by whatever reason. Packets to the buffer
            // will be delivered by the sockets' receiver threads, so all these things happen strictly
            // in the background.

            // Keep per-socket sender ready EID.
            int write_modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
            epoll_add_usock_INTERNAL(g->m_SndEID, ns, &write_modes);

            // With app reader, do not set groupPacketArrival (block the
            // provider array feature completely for now).

            /* SETUP HERE IF NEEDED
               ns->core().m_cbPacketArrival.set(ns->m_pUDT, &CUDT::groupPacketArrival);
             */
        }
        else
        {
            HLOGC(cnlog.Debug, log << "newConnection: Socket @" << ns->id() << " is not in a group");
        }
#endif
    }

    if (should_submit_to_accept)
    {
        enterCS(ls->m_AcceptLock);
        try
        {
            ls->m_QueuedSockets[ns->id()] = ns->m_PeerAddr;
            HLOGC(cnlog.Debug, log << "newConnection: Socket @" << ns->id() << " added to queued of @" << ls->id());
        }
        catch (...)
        {
            LOGC(cnlog.Error, log << "newConnection: error when queuing socket!");
            error = 3;
        }
        leaveCS(ls->m_AcceptLock);

        HLOGC(cnlog.Debug, log << "ACCEPT: new socket @" << ns->id() << " submitted for acceptance");
        // acknowledge users waiting for new connections on the listening socket
        m_EPoll.update_events(listener, ls->core().m_sPollID, SRT_EPOLL_ACCEPT, true);

        CGlobEvent::triggerEvent();

        // XXX the exact value of 'error' is ignored
        if (error > 0)
        {
            goto ERR_ROLLBACK;
        }

        // wake up a waiting accept() call
        CSync::lock_notify_one(ls->m_AcceptCond, ls->m_AcceptLock);
    }
    else
    {
        HLOGC(cnlog.Debug,
              log << "ACCEPT: new socket @" << ns->id()
                  << " NOT submitted to acceptance, another socket in the group is already connected");

        // acknowledge INTERNAL users waiting for new connections on the listening socket
        // that are reported when a new socket is connected within an already connected group.
        m_EPoll.update_events(listener, ls->core().m_sPollID, SRT_EPOLL_UPDATE, true);
#if SRT_ENABLE_BONDING
      // Note that the code in this current IF branch can only be executed in case
      // of group members. Otherwise should_submit_to_accept will be always true.
      if (ns->m_GroupOf)
      {
          HLOGC(gmlog.Debug, log << "GROUP UPDATE $" << ns->m_GroupOf->id() << " per connected socket @" << ns->id());
          m_EPoll.update_events(ns->m_GroupOf->id(), ns->m_GroupOf->m_sPollID, SRT_EPOLL_UPDATE, true);
      }
#endif
        CGlobEvent::triggerEvent();
    }

ERR_ROLLBACK:
    // XXX the exact value of 'error' is ignored
    if (error > 0)
    {
#if HVU_ENABLE_LOGGING
        static const char* why[] = {
            "UNKNOWN ERROR", "INTERNAL REJECTION", "IPE when mapping a socket", "IPE when inserting a socket"};
        LOGC(cnlog.Warn,
             log << CONID(ns->id()) << "newConnection: connection rejected due to: " << why[error] << " - "
                 << RequestTypeStr(URQFailure(w_error)));
#endif

        SRTSOCKET id = ns->id();
        ns->closeInternal(SRT_CLS_LATE);
        ns->setClosed();

        // The mapped socket should be now unmapped to preserve the situation that
        // was in the original UDT code.
        // In SRT additionally the acceptAndRespond() function (it was called probably
        // connect() in UDT code) may fail, in which case this socket should not be
        // further processed and should be removed.
        {
            ExclusiveLock cg(m_GlobControlLock);

#if SRT_ENABLE_BONDING
            if (ns->m_GroupOf)
            {
                HLOGC(smlog.Debug,
                      log << "@" << ns->id() << " IS MEMBER OF $" << ns->m_GroupOf->id()
                          << " - REMOVING FROM GROUP");
                ns->removeFromGroup(true);
            }
#endif
            // You won't be updating any EIDs anymore.
            m_EPoll.wipe_usock(id, ns->core().m_sPollID);

            swipeSocket_LOCKED(id, ns, SWIPE_NOW);
        }

        return -1;
    }

    return 1;
}

// [[using locked_shared(m_GlobControlLock)]]
SRT_EPOLL_T CUDTSocket::getListenerEvents()
{
    // You need to check EVERY socket that has been queued
    // and verify its internals. With independent socket the
    // matter is simple - if it's present, you light up the
    // SRT_EPOLL_ACCEPT flag.

#if !SRT_ENABLE_BONDING
    ScopedLock accept_lock (m_AcceptLock);

    // Make it simplified here - nonempty container = have acceptable sockets.
    // Might make sometimes spurious acceptance, but this can also happen when
    // the incoming accepted socket was suddenly broken.
    return m_QueuedSockets.empty() ? 0 : int(SRT_EPOLL_ACCEPT);

#else // Could do #endif here, but the compiler would complain about unreachable code.

    map<SRTSOCKET, sockaddr_any> sockets_copy;
    {
        ScopedLock accept_lock (m_AcceptLock);
        sockets_copy = m_QueuedSockets;
    }

    // NOTE: m_GlobControlLock is required here, but this is applied already
    // on this whole function.  (see CUDT::addEPoll)
    return CUDT::uglobal().checkQueuedSocketsEvents(sockets_copy);

#endif
}

#if SRT_ENABLE_BONDING
int CUDTUnited::checkQueuedSocketsEvents(const map<SRTSOCKET, sockaddr_any>& sockets)
{
    SRT_EPOLL_T flags = 0;

    // But with the member sockets an appropriate check must be
    // done first: if this socket belongs to a group that is
    // already in the connected state, you should light up the
    // SRT_EPOLL_UPDATE flag instead. This flag is only for
    // internal informing the waiters on the listening sockets
    // that they should re-read the group list and re-check readiness.

    // Now we can do lock once and for all
    for (map<SRTSOCKET, sockaddr_any>::const_iterator i = sockets.begin(); i != sockets.end(); ++i)
    {
        CUDTSocket* s = locateSocket_LOCKED(i->first);
        if (!s)
            continue; // wiped in the meantime - ignore

        // If this pending socket is a group member, but the group
        // to which it belongs is NOT waiting to be accepted, then
        // light up the UPDATE event only. Light up ACCEPT only if
        // this is a single socket, or this single socket has turned
        // the mirror group to be first time available for accept(),
        // and this accept() hasn't been done yet.
        if (s->m_GroupOf && !s->m_GroupOf->groupPending())
            flags |= SRT_EPOLL_UPDATE;
        else
            flags |= SRT_EPOLL_ACCEPT;
    }

    return flags;
}
#endif

// static forwarder
SRTSTATUS CUDT::installAcceptHook(SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq)
{
    return uglobal().installAcceptHook(lsn, hook, opaq);
}

SRTSTATUS CUDTUnited::installAcceptHook(const SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq)
{
    try
    {
        CUDTSocket* s = locateSocket(lsn, ERH_THROW);
        s->core().installAcceptHook(hook, opaq);
    }
    catch (CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRT_ERROR;
    }

    return SRT_STATUS_OK;
}

SRTSTATUS CUDT::installConnectHook(SRTSOCKET lsn, srt_connect_callback_fn* hook, void* opaq)
{
    return uglobal().installConnectHook(lsn, hook, opaq);
}

SRTSTATUS CUDTUnited::installConnectHook(const SRTSOCKET u, srt_connect_callback_fn* hook, void* opaq)
{
    try
    {
#if SRT_ENABLE_BONDING
        if (CUDT::isgroup(u))
        {
            GroupKeeper k(*this, u, ERH_THROW);
            k.group->installConnectHook(hook, opaq);
            return SRT_STATUS_OK;
        }
#endif
        CUDTSocket* s = locateSocket(u, ERH_THROW);
        s->core().installConnectHook(hook, opaq);
    }
    catch (CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRT_ERROR;
    }

    return SRT_STATUS_OK;
}

SRT_SOCKSTATUS CUDTUnited::getStatus(const SRTSOCKET u)
{
    // protects the m_Sockets structure
    SharedLock cg(m_GlobControlLock);

    sockets_t::const_iterator i = m_Sockets.find(u);

    if (i == m_Sockets.end())
    {
        if (m_ClosedSockets.find(u) != m_ClosedSockets.end())
            return SRTS_CLOSED;

        return SRTS_NONEXIST;
    }
    return i->second->getStatus();
}

SRTSTATUS CUDTUnited::getCloseReason(const SRTSOCKET u, SRT_CLOSE_INFO& info)
{
    // protects the m_Sockets structure
    SharedLock cg(m_GlobControlLock);

    // We need to search for the socket in:
    // m_Sockets, if it is somehow still alive,
    // m_ClosedSockets, if it's when it should be,
    // m_ClosedDatabase, if it has been already garbage-collected and deleted.

    sockets_t::const_iterator i = m_Sockets.find(u);
    if (i != m_Sockets.end())
    {
        i->second->core().copyCloseInfo((info));
        return SRT_STATUS_OK;
    }

    i = m_ClosedSockets.find(u);
    if (i != m_ClosedSockets.end())
    {
        i->second->core().copyCloseInfo((info));
    }

    map<SRTSOCKET, CloseInfo>::iterator c = m_ClosedDatabase.find(u);
    if (c == m_ClosedDatabase.end())
        return SRT_ERROR;

    info = c->second.info;
    return SRT_STATUS_OK;
}

SRTSTATUS CUDTUnited::bind(CUDTSocket* s, const sockaddr_any& name)
{
    ScopedLock cg(s->m_ControlLock);

    // cannot bind a socket more than once
    if (s->m_Status != SRTS_INIT)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    if (s->core().m_config.iIpV6Only == -1 && name.family() == AF_INET6 && name.isany())
    {
        // V6ONLY option must be set explicitly if you want to bind to a wildcard address in IPv6
        HLOGP(smlog.Error,
                "bind: when binding to :: (IPv6 wildcard), SRTO_IPV6ONLY option must be set explicitly to 0 or 1");

        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    bindSocketToMuxer(s, name);
    return SRT_STATUS_OK;
}

SRTSTATUS CUDTUnited::bind(CUDTSocket* s, UDPSOCKET udpsock)
{
    ScopedLock cg(s->m_ControlLock);

    // cannot bind a socket more than once
    if (s->m_Status != SRTS_INIT)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    sockaddr_any name;
    socklen_t    namelen = sizeof name; // max of inet and inet6

    // This will preset the sa_family as well; the namelen is given simply large
    // enough for any family here.
    if (::getsockname(udpsock, &name.sa, &namelen) == -1)
        throw CUDTException(MJ_NOTSUP, MN_INVAL);

    // Successfully extracted, so update the size
    name.len = namelen;
    bindSocketToMuxer(s, name, &udpsock);
    return SRT_STATUS_OK;
}

void CUDTUnited::bindSocketToMuxer(CUDTSocket* s, const sockaddr_any& address, UDPSOCKET* psocket)
{
    if (address.hport() == 0 && s->core().m_config.bRendezvous)
        throw CUDTException(MJ_NOTSUP, MN_ISRENDUNBOUND, 0);

    s->core().open();
    updateMux(s, address, psocket);
    // -> C(Snd|Rcv)Queue::init
    // -> pthread_create(...C(Snd|Rcv)Queue::worker...)
    s->m_Status = SRTS_OPENED;

    // copy address information of local node
    s->m_SelfAddr = s->core().channel()->getSockAddr();
}

SRTSTATUS CUDTUnited::listen(const SRTSOCKET u, int backlog)
{
    if (backlog <= 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    // Don't search for the socket if it's already -1;
    // this never is a valid socket.
    if (u == SRT_INVALID_SOCK)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    CUDTSocket* s = locateSocket(u);
    if (!s)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    ScopedLock cg(s->m_ControlLock);

    // NOTE: since now the socket is protected against simultaneous access.
    // In the meantime the socket might have been closed, which means that
    // it could have changed the state. It could be also set listen in another
    // thread, so check it out.

    // do nothing if the socket is already listening
    if (s->m_Status == SRTS_LISTENING)
        return SRT_STATUS_OK;

    // a socket can listen only if is in OPENED status
    if (s->m_Status != SRTS_OPENED)
        throw CUDTException(MJ_NOTSUP, MN_ISUNBOUND, 0);

    // [[using assert(s->m_Status == OPENED)]];

    // listen is not supported in rendezvous connection setup
    if (s->core().m_config.bRendezvous)
        throw CUDTException(MJ_NOTSUP, MN_ISRENDEZVOUS, 0);

    s->m_uiBackLog = backlog;

    // [[using assert(s->m_Status == OPENED)]]; // (still, unchanged)

    s->core().setListenState(); // propagates CUDTException,
                                // if thrown, remains in OPENED state if so.
    s->m_Status = SRTS_LISTENING;

    return SRT_STATUS_OK;
}

SRTSOCKET CUDTUnited::accept_bond(const SRTSOCKET listeners[], int lsize, int64_t msTimeOut)
{
    CEPollDesc* ed  = 0;
    int         eid = m_EPoll.create(&ed);

    // Destroy it at return - this function can be interrupted
    // by an exception.
    struct AtReturn
    {
        int         eid;
        CUDTUnited* that;
        AtReturn(CUDTUnited* t, int e)
            : eid(e)
            , that(t)
        {
        }
        ~AtReturn() { that->m_EPoll.release(eid); }
    } l_ar(this, eid);

    // Subscribe all of listeners for accept
    int events = SRT_EPOLL_ACCEPT;

    for (int i = 0; i < lsize; ++i)
    {
        srt_epoll_add_usock(eid, listeners[i], &events);
    }

    CEPoll::fmap_t st;
    m_EPoll.swait(*ed, (st), msTimeOut, true);

    if (st.empty())
    {
        // Sanity check
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);
    }

    // Theoretically we can have a situation that more than one
    // listener is ready for accept. In this case simply get
    // only the first found.
    SRTSOCKET        lsn = st.begin()->first;
    sockaddr_storage dummy;
    int              outlen = sizeof dummy;
    return accept(lsn, ((sockaddr*)&dummy), (&outlen));
}

SRTSOCKET CUDTUnited::accept(const SRTSOCKET listen, sockaddr* pw_addr, int* pw_addrlen)
{
    if (pw_addr && !pw_addrlen)
    {
        LOGC(cnlog.Error, log << "srt_accept: provided address, but address length parameter is missing");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    CUDTSocket* ls;
    SocketKeeper keep_ls;

    // We keep the mutex locked for the whole time of instant checks.
    // Once they pass, extend the life for the scope by SocketKeeper.
    {
        SharedLock lkg (m_GlobControlLock);
        ls = locateSocket_LOCKED(listen, ERH_THROW);

        // the "listen" socket must be in LISTENING status
        if (ls->m_Status != SRTS_LISTENING)
        {
            LOGC(cnlog.Error, log << "srt_accept: socket @" << listen << " is not in listening state (forgot srt_listen?)");
            throw CUDTException(MJ_NOTSUP, MN_NOLISTEN, 0);
        }

        // no "accept" in rendezvous connection setup
        if (ls->core().m_config.bRendezvous)
        {
            LOGC(cnlog.Fatal,
                    log << "CUDTUnited::accept: RENDEZVOUS flag passed through check in srt_listen when it set listen state");
            // This problem should never happen because `srt_listen` function should have
            // checked this situation before and not set listen state in result.
            // Inform the user about the invalid state in the universal way.
            throw CUDTException(MJ_NOTSUP, MN_NOLISTEN, 0);
        }

        // Artificially acquire by SocketKeeper, to be properly released.
        keep_ls.acquire_LOCKED(ls);
    }

    SRTSOCKET u = SRT_INVALID_SOCK;
    bool accepted = false;

    // !!only one connection can be set up each time!!
    while (!accepted)
    {
        UniqueLock accept_lock(ls->m_AcceptLock);
        CSync      accept_sync(ls->m_AcceptCond, accept_lock);

        if ((ls->m_Status != SRTS_LISTENING) || ls->core().m_bBroken)
        {
            // This socket has been closed.
            accepted = true;
        }
        else if (ls->m_QueuedSockets.size() > 0)
        {
            map<SRTSOCKET, sockaddr_any>::iterator b = ls->m_QueuedSockets.begin();

            if (pw_addr != NULL && pw_addrlen != NULL)
            {
                // Check if the length of the buffer to fill the name in
                // was large enough.
                const int len = b->second.size();
                if (*pw_addrlen < len)
                {
                    // In case when the address cannot be rewritten,
                    // DO NOT accept, but leave the socket in the queue.
                    break;
                }
            }

            u = b->first;
            HLOGC(cnlog.Debug, log << "accept: @" << u << " extracted from @" << ls->id() << " - deleting from queued");
            ls->m_QueuedSockets.erase(b);
            accepted = true;
        }
        else if (!ls->core().m_config.bSynRecving)
        {
            accepted = true;
        }

        if (!accepted && (ls->m_Status == SRTS_LISTENING))
            accept_sync.wait();

        if (ls->m_QueuedSockets.empty())
            m_EPoll.update_events(listen, ls->core().m_sPollID, SRT_EPOLL_ACCEPT, false);
    }

    int lsn_group_connect = ls->core().m_config.iGroupConnect;
    bool lsn_syn_recv = ls->core().m_config.bSynRecving;

    // NOTE: release() locks m_GlobControlLock.
    // Once we extracted the accepted socket, we don't need to keep ls busy.
    keep_ls.release(*this);
    ls = NULL; // NOT USABLE ANYMORE!

    if (!accepted) // The loop was interrupted
    {
        LOGC(cnlog.Error, log << "srt_accept: can't extract address - target object too small");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    if (u == SRT_INVALID_SOCK)
    {
        // non-blocking receiving, no connection available
        if (!lsn_syn_recv)
        {
            LOGC(cnlog.Error, log << "srt_accept: no pending connection available at the moment");
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }

        LOGC(cnlog.Error, log << "srt_accept: listener socket @" << listen << " is already closed");
        // listening socket is closed
        throw CUDTException(MJ_SETUP, MN_CLOSED, 0);
    }

    CUDTSocket* s = locateSocket(u);
    if (s == NULL)
    {
        LOGC(cnlog.Error, log << "srt_accept: pending connection has unexpectedly closed");
        throw CUDTException(MJ_SETUP, MN_CLOSED, 0);
    }

    SRT_ASSERT(s->core().m_bConnected);

    // Set properly the SRTO_GROUPCONNECT flag (for general case; may be overridden later)
    s->core().m_config.iGroupConnect = 0;

    // Check if LISTENER has the SRTO_GROUPCONNECT flag set,
    // and the already accepted socket has successfully joined
    // the mirror group. If so, RETURN THE GROUP ID, not the socket ID.
#if SRT_ENABLE_BONDING
    if (lsn_group_connect == 1 && s->m_GroupOf)
    {
        // Put a lock to protect the group against accidental deletion
        // in the meantime.
        SharedLock glock(m_GlobControlLock);
        // Check again; it's unlikely to happen, but
        // it's a theoretically possible scenario
        if (s->m_GroupOf)
        {
            CUDTGroup* g = s->m_GroupOf;
            // Mark the beginning of the connection at the moment
            // when the group ID is returned to the app caller
            g->m_stats.tsLastSampleTime = steady_clock::now();

            // Ok, now that we have to get the group:
            // 1. Get all listeners that have so far reported any pending connection
            //    for this group.
            // 2. THE VERY LISTENER that provided this connection should be only
            //    checked if it contains ANY FURTHER queued sockets than this.

            HLOGC(cnlog.Debug, log << "accept: reporting group $" << g->m_GroupID << " instead of member socket @" << u);
            u                                = g->m_GroupID;
            s->core().m_config.iGroupConnect = 1; // should be derived from ls, but make sure

            vector<SRTSOCKET> listeners = g->clearPendingListeners();
            removePendingForGroup(g, listeners, s->id());
        }
        else
        {
            LOGC(smlog.Error, log << "accept: IPE: socket's group deleted in the meantime of accept process???");
        }
    }
#endif

    ScopedLock cg(s->m_ControlLock);

    if (pw_addr != NULL && pw_addrlen != NULL)
    {
        memcpy((pw_addr), s->m_PeerAddr.get(), s->m_PeerAddr.size());
        *pw_addrlen = s->m_PeerAddr.size();
    }

    return u;
}

#if SRT_ENABLE_BONDING

// [[using locked(m_GlobControlLock)]]
void CUDTUnited::removePendingForGroup(const CUDTGroup* g, const vector<SRTSOCKET>& listeners, SRTSOCKET this_socket)
{
    set<SRTSOCKET> members;
    g->getMemberSockets((members));

    IF_HEAVY_LOGGING(ostringstream outl);
    IF_HEAVY_LOGGING(for (vector<SRTSOCKET>::const_iterator lp = listeners.begin(); lp != listeners.end(); ++lp) { outl << " @" << (*lp); });

    HLOGC(cnlog.Debug, log << "removePendingForGroup: " << listeners.size() << " listeners collected: " << outl.str());

    // What we need to do is:
    // 1. Walk through the listener sockets and check their accept queue.
    // 2. Skip a socket that:
    //    - Is equal to this_socket (was removed from the queue already and triggered group accept)
    //    - Does not belong to group members (should remain there for other purposes)
    // 3. Any member socket found in that listener:
    //    - this socket must be removed from the queue
    //    - the listener containing this socket must be added UPDATE event.

    map<CUDTSocket*, int> listeners_to_update;

    for (vector<SRTSOCKET>::const_iterator i = listeners.begin(); i != listeners.end(); ++i)
    {
        CUDTSocket* ls = locateSocket_LOCKED(*i);
        if (!ls)
        {
            HLOGC(cnlog.Debug, log << "Group-pending lsn @" << (*i) << " deleted in the meantime");
            continue;
        }
        vector<SRTSOCKET> swipe_members;

        ScopedLock alk (ls->m_AcceptLock);

        for (map<SRTSOCKET, sockaddr_any>::const_iterator q = ls->m_QueuedSockets.begin(); q != ls->m_QueuedSockets.end(); ++q)
        {
            HLOGC(cnlog.Debug, log << "Group-pending lsn @" << (*i) << " queued socket @" << q->first << ":");
            // 1. Check if it was the accept-triggering socket
            if (q->first == this_socket)
            {
                listeners_to_update[ls] += 0;
                HLOGC(cnlog.Debug, log << "... is the accept-trigger; will only possibly silence the listener");
                continue;
            }

            // 2. Check if it was this group's member socket
            if (members.find(q->first) == members.end())
            {
                // Increase the number of not-member-related sockets to know if
                // the read-ready status from the listener should be cleared.
                listeners_to_update[ls]++;
                HLOGC(cnlog.Debug, log << "... is not a member of $" << g->id() << "; skipping");
                continue;
            }

            // 3. Found at least one socket that is this group's member
            //    and is not the socket that triggered accept.
            swipe_members.push_back(q->first);
            listeners_to_update[ls] += 0;
            HLOGC(cnlog.Debug, log << "... is to be unqueued");
        }
        if (ls->m_QueuedSockets.empty())
        {
            HLOGC(cnlog.Debug, log << "Group-pending lsn @" << (*i) << ": NO QUEUED SOCKETS");
        }

        for (vector<SRTSOCKET>::iterator is = swipe_members.begin(); is != swipe_members.end(); ++is)
        {
            ls->m_QueuedSockets.erase(*is);
        }
    }

    // Now; for every listener, which contained at least one socket that is
    // this group's member:
    // - ADD UPDATE event
    // - REMOVE ACCEPT event, if the number of "other sockets" is zero.

    // NOTE: "map" container is used because we need to have unique listener container,
    // while the listener may potentially be added multiple times in the loop of queued sockets.
    for (map<CUDTSocket*, int>::iterator mi = listeners_to_update.begin(); mi != listeners_to_update.end(); ++mi)
    {
        CUDTSocket* s;
        int nothers;
        Tie(s, nothers) = *mi;

        HLOGC(cnlog.Debug, log << "Group-pending lsn @" << s->id() << " had in-group accepted sockets and " << nothers << " other sockets");
        if (nothers == 0)
        {
            m_EPoll.update_events(s->id(), s->core().m_sPollID, SRT_EPOLL_ACCEPT, false);
        }

        m_EPoll.update_events(s->id(), s->core().m_sPollID, SRT_EPOLL_UPDATE, true);
    }

}

#endif

SRTSOCKET CUDTUnited::connect(SRTSOCKET u, const sockaddr* srcname, const sockaddr* tarname, int namelen)
{
    // Here both srcname and tarname must be specified
    if (!srcname || !tarname || namelen < int(sizeof(sockaddr_in)))
    {
        LOGC(aclog.Error,
             log << "connect(with source): invalid call: srcname=" << srcname << " tarname=" << tarname
                 << " namelen=" << namelen);
        throw CUDTException(MJ_NOTSUP, MN_INVAL);
    }

    sockaddr_any source_addr(srcname, namelen);
    if (source_addr.len == 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    sockaddr_any target_addr(tarname, namelen);
    if (target_addr.len == 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

#if SRT_ENABLE_BONDING
    // Check affiliation of the socket. It's now allowed for it to be
    // a group or socket. For a group, add automatically a socket to
    // the group.
    if (CUDT::isgroup(u))
    {
        GroupKeeper k(*this, u, ERH_THROW);
        // Note: forced_isn is ignored when connecting a group.
        // The group manages the ISN by itself ALWAYS, that is,
        // it's generated anew for the very first socket, and then
        // derived by all sockets in the group.
        SRT_SOCKGROUPCONFIG gd[1] = {srt_prepare_endpoint(srcname, tarname, namelen)};

        // When connecting to exactly one target, only this very target
        // can be returned as a socket, so rewritten back array can be ignored.
        return singleMemberConnect(k.group, gd);
    }
#endif

    CUDTSocket* s = locateSocket(u);
    if (s == NULL)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    // For a single socket, just do bind, then connect
    bind(s, source_addr);
    connectIn(s, target_addr, SRT_SEQNO_NONE);
    return SRT_SOCKID_CONNREQ;
}

SRTSOCKET CUDTUnited::connect(const SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn)
{
    if (!name || namelen < int(sizeof(sockaddr_in)))
    {
        LOGC(aclog.Error, log << "connect(): invalid call: name=" << name << " namelen=" << namelen);
        throw CUDTException(MJ_NOTSUP, MN_INVAL);
    }

    sockaddr_any target_addr(name, namelen);
    if (target_addr.len == 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

#if SRT_ENABLE_BONDING
    // Check affiliation of the socket. It's now allowed for it to be
    // a group or socket. For a group, add automatically a socket to
    // the group.
    if (CUDT::isgroup(u))
    {
        GroupKeeper k(*this, u, ERH_THROW);

        // Note: forced_isn is ignored when connecting a group.
        // The group manages the ISN by itself ALWAYS, that is,
        // it's generated anew for the very first socket, and then
        // derived by all sockets in the group.
        SRT_SOCKGROUPCONFIG gd[1] = {srt_prepare_endpoint(NULL, name, namelen)};
        return singleMemberConnect(k.group, gd);
    }
#endif

    CUDTSocket* s = locateSocket(u);
    if (!s)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    connectIn(s, target_addr, forced_isn);
    return SRT_SOCKID_CONNREQ;
}

#if SRT_ENABLE_BONDING
SRTSOCKET CUDTUnited::singleMemberConnect(CUDTGroup* pg, SRT_SOCKGROUPCONFIG* gd)
{
    SRTSOCKET gstat = groupConnect(pg, gd, 1);
    if (gstat == SRT_INVALID_SOCK)
    {
        // We have only one element here, so refer to it.
        // Sanity check
        if (gd->errorcode == SRT_SUCCESS)
            gd->errorcode = SRT_EINVPARAM;

        return CUDT::APIError(gd->errorcode), SRT_INVALID_SOCK;
    }

    return gstat;
}

// [[using assert(pg->m_iBusy > 0)]]
SRTSOCKET CUDTUnited::groupConnect(CUDTGroup* pg, SRT_SOCKGROUPCONFIG* targets, int arraysize)
{
    CUDTGroup& g = *pg;
    SRT_ASSERT(g.m_iBusy > 0);

    // Check and report errors on data brought in by srt_prepare_endpoint,
    // as the latter function has no possibility to report errors.
    for (int tii = 0; tii < arraysize; ++tii)
    {
        if (targets[tii].srcaddr.ss_family != targets[tii].peeraddr.ss_family)
        {
            LOGC(aclog.Error, log << "srt_connect/group: family differs on source and target address");
            throw CUDTException(MJ_NOTSUP, MN_INVAL);
        }

        if (targets[tii].weight > CUDT::MAX_WEIGHT)
        {
            LOGC(aclog.Error, log << "srt_connect/group: weight value must be between 0 and " << (+CUDT::MAX_WEIGHT));
            throw CUDTException(MJ_NOTSUP, MN_INVAL);
        }
    }

    // Synchronize on simultaneous group-locking
    enterCS(*g.exp_groupLock());

    // If the open state switched to OPENED, the blocking mode
    // must make it wait for connecting it. Doing connect when the
    // group is already OPENED returns immediately, regardless if the
    // connection is going to later succeed or fail (this will be
    // known in the group state information).
    bool       block_new_opened = !g.m_bOpened && g.m_bSynRecving;
    const bool was_empty        = g.groupEmpty_LOCKED();

    // In case the group was retried connection, clear first all epoll readiness.
    const int ncleared = m_EPoll.update_events(g.id(), g.m_sPollID, SRT_EPOLL_ERR, false);
    if (was_empty || ncleared)
    {
        HLOGC(aclog.Debug,
              log << "srt_connect/group: clearing IN/OUT because was_empty=" << was_empty
                  << " || ncleared=" << ncleared);
        // IN/OUT only in case when the group is empty, otherwise it would
        // clear out correct readiness resulting from earlier calls.
        // This also should happen if ERR flag was set, as IN and OUT could be set, too.
        m_EPoll.update_events(g.id(), g.m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT, false);
    }

    leaveCS(*g.exp_groupLock());

    SRTSOCKET retval = SRT_INVALID_SOCK;

    int eid           = -1;
    int connect_modes = SRT_EPOLL_CONNECT | SRT_EPOLL_ERR;
    if (block_new_opened)
    {
        // Create this eid only to block-wait for the first
        // connection.
        eid = srt_epoll_create();
    }

    // Use private map to avoid searching in the
    // overall map.
    map<SRTSOCKET, CUDTSocket*> spawned;

    HLOGC(aclog.Debug,
          log << "groupConnect: will connect " << arraysize << " links and "
              << (block_new_opened ? "BLOCK until any is ready" : "leave the process in background"));

    for (int tii = 0; tii < arraysize; ++tii)
    {
        sockaddr_any target_addr(targets[tii].peeraddr);
        sockaddr_any source_addr(targets[tii].srcaddr);
        SRTSOCKET&   sid_rloc = targets[tii].id;
        int&         erc_rloc = targets[tii].errorcode;
        erc_rloc              = SRT_SUCCESS; // preinitialized
        HLOGC(aclog.Debug, log << "groupConnect: taking on " << sockaddr_any(targets[tii].peeraddr).str());

        CUDTSocket* ns = 0;

        // NOTE: After calling newSocket, the socket is mapped into m_Sockets.
        // It must be MANUALLY removed from this list in case we need it deleted.
        SRTSOCKET sid = newSocket(&ns, true); // Create MANAGED socket (auto-deleted when broken)

        if (pg->m_cbConnectHook)
        {
            // Derive the connect hook by the socket, if set on the group
            ns->core().m_cbConnectHook = pg->m_cbConnectHook;
        }

        SRT_SocketOptionObject* config = targets[tii].config;

        // XXX Support non-blocking mode:
        // If the group has nonblocking set for connect (SNDSYN),
        // then it must set so on the socket. Then, the connection
        // process is asynchronous. The socket appears first as
        // GST_PENDING state, and only after the socket becomes
        // connected does its status in the group turn into GST_IDLE.

        // Set all options that were requested by the options set on a group
        // prior to connecting.
        string error_reason SRT_ATR_UNUSED;
        try
        {
            for (size_t i = 0; i < g.m_config.size(); ++i)
            {
                HLOGC(aclog.Debug, log << "groupConnect: OPTION @" << sid << " #" << g.m_config[i].so);
                error_reason = hvu::fmtcat("group-derived option: #", g.m_config[i].so);
                ns->core().setOpt(g.m_config[i].so, &g.m_config[i].value[0], (int)g.m_config[i].value.size());
            }

            // Do not try to set a user option if failed already.
            if (config)
            {
                error_reason = "user option";
                ns->core().applyMemberConfigObject(*config);
            }

            error_reason = "bound address";
            // We got it. Bind the socket, if the source address was set
            if (!source_addr.empty())
                bind(ns, source_addr);
        }
        catch (CUDTException& e)
        {
            // Just notify the problem, but the loop must continue.
            // Set the original error as reported.
            targets[tii].errorcode = e.getErrorCode();
            LOGC(aclog.Error, log << "srt_connect_group: failed to set " << error_reason);
        }
        catch (...)
        {
            // Set the general EINVPARAM - this error should never happen
            LOGC(aclog.Error, log << "IPE: CUDT::setOpt reported unknown exception");
            targets[tii].errorcode = SRT_EINVPARAM;
        }

        // Add socket to the group.
        // Do it after setting all stored options, as some of them may
        // influence some group data.

        groups::SocketData data = groups::prepareSocketData(ns, g.type());
        if (targets[tii].token != -1)
        {
            // Reuse the token, if specified by the caller
            data.token = targets[tii].token;
        }
        else
        {
            // Otherwise generate and write back the token
            data.token         = CUDTGroup::genToken();
            targets[tii].token = data.token;
        }

        {
            ExclusiveLock cs(m_GlobControlLock);
            if (m_Sockets.count(sid) == 0)
            {
                HLOGC(aclog.Debug, log << "srt_connect_group: socket @" << sid << " deleted in process");
                // Someone deleted the socket in the meantime?
                // Unlikely, but possible in theory.
                // Don't delete anything - it's already done.
                continue;
            }

            // There's nothing wrong with preparing the data first
            // even if this happens for nothing. But now, under the lock
            // and after checking that the socket still exists, check now
            // if this succeeded, and then also if the group is still usable.
            // The group will surely exist because it's set busy, until the
            // end of this function. But it might be simultaneously requested closed.
            bool proceed = true;

            if (targets[tii].errorcode != SRT_SUCCESS)
            {
                HLOGC(aclog.Debug,
                      log << "srt_connect_group: not processing @" << sid << " due to error in setting options");
                proceed = false;
            }

            if (g.m_bClosing)
            {
                HLOGC(aclog.Debug,
                      log << "srt_connect_group: not processing @" << sid << " due to CLOSED GROUP $" << g.m_GroupID);
                proceed = false;
            }

            if (proceed)
            {
                CUDTGroup::SocketData* f = g.add(data);
                ns->m_GroupMemberData    = f;
                ns->m_GroupOf            = &g;
                f->weight                = targets[tii].weight;
                HLOGC(aclog.Debug, log << "srt_connect_group: socket @" << sid << " added to group $" << g.m_GroupID);
            }
            else
            {
                targets[tii].id = SRT_INVALID_SOCK;
                delete ns;
                m_Sockets.erase(sid);

                // If failed to set options, then do not continue
                // neither with binding, nor with connecting.
                continue;
            }
        }

        // XXX This should be re-enabled later, this should
        // be probably still in use to exchange information about
        // packets asymmetrically lost. But for no other purpose.
        /*
        ns->core().m_cbPacketArrival.set(ns->m_pUDT, &CUDT::groupPacketArrival);
        */

        // XXX Check if needed SharedLock cs(m_GlobControlLock);

        int isn = g.currentSchedSequence();

        // Set it the groupconnect option, as all in-group sockets should have.
        ns->core().m_config.iGroupConnect = 1;

        // Every group member will have always nonblocking
        // (this implies also non-blocking connect/accept).
        // The group facility functions will block when necessary
        // using epoll_wait.
        ns->core().m_config.bSynRecving = false;
        ns->core().m_config.bSynSending = false;

        HLOGC(aclog.Debug, log << "groupConnect: NOTIFIED AS PENDING @" << sid << " both read and write");
        // If this socket is not to block the current connect process,
        // it may still be needed for the further check if the redundant
        // connection succeeded or failed and whether the new socket is
        // ready to use or needs to be closed.
        epoll_add_usock_INTERNAL(g.m_SndEID, ns, &connect_modes);

        // Adding a socket on which we need to block to BOTH these tracking EIDs
        // and the blocker EID. We'll simply remove from them later all sockets that
        // got connected state or were broken.

        if (block_new_opened)
        {
            HLOGC(aclog.Debug, log << "groupConnect: WILL BLOCK on @" << sid << " until connected");
            epoll_add_usock_INTERNAL(eid, ns, &connect_modes);
        }

        // And connect
        try
        {
            HLOGC(aclog.Debug, log << "groupConnect: connecting a new socket with ISN=" << isn);
            connectIn(ns, target_addr, isn);
        }
        catch (const CUDTException& e)
        {
            LOGC(aclog.Error,
                 log << "groupConnect: socket @" << sid << " in group " << pg->id() << " failed to connect");
            // We know it does belong to a group.
            // Remove it first because this involves a mutex, and we want
            // to avoid locking more than one mutex at a time.
            erc_rloc               = e.getErrorCode();
            targets[tii].errorcode = e.getErrorCode();
            targets[tii].id        = SRT_INVALID_SOCK;

            ExclusiveLock cl(m_GlobControlLock);

            // You won't be updating any EIDs anymore.
            m_EPoll.wipe_usock(ns->id(), ns->core().m_sPollID);
            ns->removeFromGroup(false);
            m_Sockets.erase(ns->id());
            // Intercept to delete the socket on failure.
            delete ns;
            continue;
        }
        catch (...)
        {
            LOGC(aclog.Fatal, log << "groupConnect: IPE: UNKNOWN EXCEPTION from connectIn");
            targets[tii].errorcode = SRT_ESYSOBJ;
            targets[tii].id        = SRT_INVALID_SOCK;
            ExclusiveLock cl(m_GlobControlLock);
            ns->removeFromGroup(false);
            // You won't be updating any EIDs anymore.
            m_EPoll.wipe_usock(ns->id(), ns->core().m_sPollID);
            m_Sockets.erase(ns->id());
            // Intercept to delete the socket on failure.
            delete ns;

            // Do not use original exception, it may crash off a C API.
            throw CUDTException(MJ_SYSTEMRES, MN_OBJECT);
        }

        SRT_SOCKSTATUS st;
        {
            ScopedLock grd(ns->m_ControlLock);
            st = ns->getStatus();
        }

        {
            // NOTE: Not applying m_GlobControlLock because the group is now
            // set busy, so it won't be deleted, even if it was requested to be closed.
            ScopedLock grd(g.m_GroupLock);

            if (!ns->m_GroupOf)
            {
                // The situation could get changed between the unlock and lock of m_GroupLock.
                // This must be checked again.
                // If a socket has been removed from group, it means that some other thread is
                // currently trying to delete the socket. Therefore it doesn't have, and even shouldn't,
                // be deleted here. Just exit with error report.
                LOGC(aclog.Error, log << "groupConnect: self-created member socket deleted during process, SKIPPING.");

                // Do not report the error from here, just ignore this socket.
                continue;
            }

            // If m_GroupOf is not NULL, the m_IncludedIter is still valid.
            CUDTGroup::SocketData* f = ns->m_GroupMemberData;

            // Now under a group lock, we need to make sure the group isn't being closed
            // in order not to add a socket to a dead group.
            if (g.m_bClosing)
            {
                LOGC(aclog.Error, log << "groupConnect: group deleted while connecting; breaking the process");

                // Set the status as pending so that the socket is taken care of later.
                // Note that all earlier sockets that were processed in this loop were either
                // set BROKEN or PENDING.
                f->sndstate = SRT_GST_PENDING;
                f->rcvstate = SRT_GST_PENDING;
                retval      = SRT_INVALID_SOCK;
                break;
            }

            HLOGC(aclog.Debug,
                  log << "groupConnect: @" << sid << " connection successful, setting group OPEN (was "
                      << (g.m_bOpened ? "ALREADY" : "NOT") << "), will " << (block_new_opened ? "" : "NOT ")
                      << "block the connect call, status:" << SockStatusStr(st));

            // XXX OPEN OR CONNECTED?
            // BLOCK IF NOT OPEN OR BLOCK IF NOT CONNECTED?
            //
            // What happens to blocking when there are 2 connections
            // pending, about to be broken, and srt_connect() is called again?
            // SHOULD BLOCK the latter, even though is OPEN.
            // Or, OPEN should be removed from here and srt_connect(_group)
            // should block always if the group doesn't have neither 1 connected link
            g.m_bOpened = true;

            g.m_stats.tsLastSampleTime = steady_clock::now();

            f->laststatus = st;
            // Check the socket status and update it.
            // Turn the group state of the socket to IDLE only if
            // connection is established or in progress
            f->agent = source_addr;
            f->peer  = target_addr;

            if (st >= SRTS_BROKEN)
            {
                f->sndstate = SRT_GST_BROKEN;
                f->rcvstate = SRT_GST_BROKEN;
                epoll_remove_socket_INTERNAL(g.m_SndEID, ns);
            }
            else
            {
                f->sndstate  = SRT_GST_PENDING;
                f->rcvstate  = SRT_GST_PENDING;
                spawned[sid] = ns;

                sid_rloc = sid;
                erc_rloc = 0;
                retval   = sid;
            }
        }
    }

    if (retval == SRT_INVALID_SOCK)
    {
        HLOGC(aclog.Debug, log << "groupConnect: none succeeded as background-spawn, exit with error");
        block_new_opened = false; // Avoid executing further while loop
    }

    vector<SRTSOCKET> broken;

    while (block_new_opened)
    {
        if (spawned.empty())
        {
            // All were removed due to errors.
            retval = SRT_INVALID_SOCK;
            break;
        }
        HLOGC(aclog.Debug, log << "groupConnect: first connection, applying EPOLL WAITING.");
        int               len = (int)spawned.size();
        vector<SRTSOCKET> ready(spawned.size());
        const int estat = srt_epoll_wait(eid,
                                         NULL,
                                         NULL, // IN/ACCEPT
                                         &ready[0],
                                         &len, // OUT/CONNECT
                                         -1, // indefinitely (FIXME Check if it needs to REGARD CONNECTION TIMEOUT!)
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL);

        // Sanity check. Shouldn't happen if subs are in sync with spawned.
        if (estat == int(SRT_ERROR))
        {
#if HVU_ENABLE_LOGGING
            CUDTException& x = CUDT::getlasterror();
            if (x.getErrorCode() != SRT_EPOLLEMPTY)
            {
                LOGC(aclog.Error,
                     log << "groupConnect: srt_epoll_wait failed not because empty, unexpected IPE:"
                         << x.getErrorMessage());
            }
#endif
            HLOGC(aclog.Debug, log << "groupConnect: srt_epoll_wait failed - breaking the wait loop");
            retval = SRT_INVALID_SOCK;
            break;
        }

        // At the moment when you are going to work with real sockets,
        // lock the groups so that no one messes up with something here
        // in the meantime.

        ScopedLock lock(*g.exp_groupLock());

        // NOTE: UNDER m_GroupLock, NO API FUNCTION CALLS DARE TO HAPPEN BELOW!

        // Check first if a socket wasn't closed in the meantime. It will be
        // automatically removed from all EIDs, but there's no sense in keeping
        // them in 'spawned' map.
        for (map<SRTSOCKET, CUDTSocket*>::iterator y = spawned.begin(); y != spawned.end(); ++y)
        {
            SRTSOCKET sid = y->first;
            if (y->second->getStatus() >= SRTS_BROKEN)
            {
                HLOGC(aclog.Debug,
                      log << "groupConnect: Socket @" << sid
                          << " got BROKEN in the meantine during the check, remove from candidates");
                // Remove from spawned and try again
                broken.push_back(sid);

                epoll_remove_socket_INTERNAL(eid, y->second);
                epoll_remove_socket_INTERNAL(g.m_SndEID, y->second);
            }
        }

        // Remove them outside the loop because this can't be done
        // while iterating over the same container.
        for (size_t i = 0; i < broken.size(); ++i)
            spawned.erase(broken[i]);

        // Check the sockets if they were reported due
        // to have connected or due to have failed.
        // Distill successful ones. If distilled nothing, return -1.
        // If not all sockets were reported in this instance, repeat
        // the call until you get information about all of them.
        for (int i = 0; i < len; ++i)
        {
            map<SRTSOCKET, CUDTSocket*>::iterator x = spawned.find(ready[i]);
            if (x == spawned.end())
            {
                // Might be removed above - ignore it.
                continue;
            }

            SRTSOCKET   sid = x->first;
            CUDTSocket* s   = x->second;

            // Check status. If failed, remove from spawned
            // and try again.
            SRT_SOCKSTATUS st = s->getStatus();
            if (st >= SRTS_BROKEN)
            {
                HLOGC(aclog.Debug,
                      log << "groupConnect: Socket @" << sid
                          << " got BROKEN during background connect, remove & TRY AGAIN");
                // Remove from spawned and try again
                if (spawned.erase(sid))
                    broken.push_back(sid);

                epoll_remove_socket_INTERNAL(eid, s);
                epoll_remove_socket_INTERNAL(g.m_SndEID, s);

                continue;
            }

            if (st == SRTS_CONNECTED)
            {
                HLOGC(aclog.Debug,
                      log << "groupConnect: Socket @" << sid << " got CONNECTED as first in the group - reporting");
                retval           = sid;

                // XXX Race against postConnect/setGroupConnected in the worker thread.
                // XXX POTENTIAL BUG: Possibly this supersedes the same setting done from postConnect
                //     and this way the epoll readiness isn't set.
                // In this thread the group is also set connected after the connection process is done.
                // Might be that this here isn't required.
                g.m_bConnected   = true;
                block_new_opened = false; // Interrupt also rolling epoll (outer loop)

                // Remove this socket from SND EID because it doesn't need to
                // be connection-tracked anymore. Don't remove from the RCV EID
                // however because RCV procedure relies on epoll also for reading
                // and when found this socket connected it will "upgrade" it to
                // read-ready tracking only.
                epoll_remove_socket_INTERNAL(g.m_SndEID, s);
                break;
            }

            // Spurious?
            HLOGC(aclog.Debug,
                  log << "groupConnect: Socket @" << sid << " got spurious wakeup in " << SockStatusStr(st)
                      << " TRY AGAIN");
        }
        // END of m_GroupLock CS - you can safely use API functions now.
    }
    // Finished, delete epoll.
    if (eid != -1)
    {
        HLOGC(aclog.Debug, log << "connect FIRST IN THE GROUP finished, removing E" << eid);
        srt_epoll_release(eid);
    }

    for (vector<SRTSOCKET>::iterator b = broken.begin(); b != broken.end(); ++b)
    {
        CUDTSocket* s = locateSocket(*b, ERH_RETURN);
        if (!s)
            continue;

        // This will also automatically remove it from the group and all eids
        close(s, SRT_CLS_INTERNAL);
    }

    // There's no possibility to report a problem on every connection
    // separately in case when every single connection has failed. What
    // is more interesting, it's only a matter of luck that all connections
    // fail at exactly the same time. OTOH if all are to fail, this
    // function will still be polling sockets to determine the last man
    // standing. Each one could, however, break by a different reason,
    // for example, one by timeout, another by wrong passphrase. Check
    // the `errorcode` field to determine the reason for particular link.
    if (retval == SRT_INVALID_SOCK)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    return retval;
}
#endif

void CUDTUnited::connectIn(CUDTSocket* s, const sockaddr_any& target_addr, int32_t forced_isn)
{
    ScopedLock cg(s->m_ControlLock);
    // a socket can "connect" only if it is in the following states:
    // - OPENED: assume the socket binding parameters are configured
    // - INIT: configure binding parameters here
    // - any other (meaning, already connected): report error

    if (s->m_Status == SRTS_INIT)
    {
        // If bind() was done first on this socket, then the
        // socket will not perform this step. This actually does the
        // same thing as bind() does, just with empty address so that
        // the binding parameters are autoselected.

        // This will create such a sockaddr_any that
        // will return true from empty().
        bindSocketToMuxer(s, sockaddr_any(target_addr.family()));
    }
    else
    {
        if (s->m_Status != SRTS_OPENED)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        // status = SRTS_OPENED, so family should be known already.
        if (target_addr.family() != s->m_SelfAddr.family())
        {
            LOGP(cnlog.Error, "srt_connect: socket is bound to a different family than target address");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }
    }

    // connect_complete() may be called before connect() returns.
    // So we need to update the status before connect() is called,
    // otherwise the status may be overwritten with wrong value
    // (CONNECTED vs. CONNECTING).
    s->m_Status = SRTS_CONNECTING;

    /*
     * In blocking mode, connect can block for up to 30 seconds for
     * rendez-vous mode. Holding the s->m_ControlLock prevent close
     * from cancelling the connect
     */
    try
    {
        // record peer address
        s->m_PeerAddr = target_addr;
        s->core().startConnect(target_addr, forced_isn);
    }
    catch (const CUDTException&) // Interceptor, just to change the state.
    {
        s->m_Status = SRTS_OPENED;
        throw;
    }
}

SRTSTATUS CUDTUnited::close(const SRTSOCKET u, int reason)
{
#if SRT_ENABLE_BONDING
    if (CUDT::isgroup(u))
    {
        GroupKeeper k(*this, u, ERH_THROW);
        k.group->close();
        deleteGroup(k.group);
        return SRT_STATUS_OK;
    }
#endif
#if HVU_ENABLE_HEAVY_LOGGING
    // Wrapping the log into a destructor so that it
    // is printed AFTER the destructor of SocketKeeper.
    struct ScopedExitLog
    {
        const CUDTSocket* const ps;
        ScopedExitLog(const CUDTSocket* p): ps(p){}
        ~ScopedExitLog()
        {
            if (ps) // Could be not acquired by SocketKeeper, occasionally
            {
                HLOGC(smlog.Debug, log << "CUDTUnited::close/end: @" << ps->id() << " busy=" << ps->isStillBusy());
            }
        }
    };
#endif

    SocketKeeper k(*this, u, ERH_THROW);
    IF_HEAVY_LOGGING(ScopedExitLog slog(k.socket));
    HLOGC(smlog.Debug, log << "CUDTUnited::close/begin: @" << u << " busy=" << k.socket->isStillBusy());

    SRTSTATUS cstatus = close(k.socket, reason);
    HLOGC(smlog.Debug, log << "CUDTUnited::close: internal close status " << cstatus);

    // Releasing under the global lock to avoid even theoretical
    // data race.

    k.release(*this);
    return cstatus;
}

#if SRT_ENABLE_BONDING
void CUDTUnited::deleteGroup(CUDTGroup* g)
{
    sync::ExclusiveLock cg(m_GlobControlLock);
    return deleteGroup_LOCKED(g);
}

// [[using locked(m_GlobControlLock)]]
void CUDTUnited::deleteGroup_LOCKED(CUDTGroup* g)
{
    SRT_ASSERT(g->groupEmpty());

    // After that the group is no longer findable by GroupKeeper
    m_Groups.erase(g->m_GroupID);
    m_ClosedGroups[g->m_GroupID] = g;

    // Paranoid check: since the group is in m_ClosedGroups
    // it may potentially be deleted. Make sure no socket points
    // to it. Actually all sockets should have been already removed
    // from the group container, so if any does, it's invalid.
    for (sockets_t::iterator i = m_Sockets.begin(); i != m_Sockets.end(); ++i)
    {
        CUDTSocket* s = i->second;
        if (s->m_GroupOf == g)
        {
            LOGC(smlog.Error, log << "deleteGroup: IPE: existing @" << s->id() << " points to a dead group!");
            s->m_GroupOf         = NULL;
            s->m_GroupMemberData = NULL;
        }
    }

    // Just in case, do it in closed sockets, too, although this should be
    // always done before moving to it.
    for (sockets_t::iterator i = m_ClosedSockets.begin(); i != m_ClosedSockets.end(); ++i)
    {
        CUDTSocket* s = i->second;
        if (s->m_GroupOf == g)
        {
            LOGC(smlog.Error, log << "deleteGroup: IPE: closed @" << s->id() << " points to a dead group!");
            s->m_GroupOf         = NULL;
            s->m_GroupMemberData = NULL;
        }
    }
}
#endif

// [[using locked(m_GlobControlLock)]]
void CUDTUnited::recordCloseReason(CUDTSocket* s)
{
    CloseInfo ci;
    ci.info.agent = SRT_CLOSE_REASON(s->core().m_AgentCloseReason.load());
    ci.info.peer = SRT_CLOSE_REASON(s->core().m_PeerCloseReason.load());
    ci.info.time = s->core().m_CloseTimeStamp.load().time_since_epoch().count();

    m_ClosedDatabase[s->id()] = ci;

    // As a DOS attack prevention, do not allow to keep more than 10 records.
    // In a normal functioning of the application this shouldn't be necessary,
    // but it is still needed that a record of a dead socket is kept for
    // 10 gc cycles more to ensure that the application can obtain it even after
    // the socket has been physically removed. But if we don't limit the number
    // of these records, this could be vulnerable for DOS attack if the user
    // forces the application to create and close SRT sockets very quickly.
    // Hence remove the oldest record, which can be recognized from the `time`
    // field, if the number of records exceeds 10.
    if (m_ClosedDatabase.size() > MAX_CLOSE_RECORD_SIZE)
    {
        // remove the oldest one
        // This can only be done by collecting all time info
        map<int32_t, SRTSOCKET> which;

        for (map<SRTSOCKET, CloseInfo>::iterator x = m_ClosedDatabase.begin();
                x != m_ClosedDatabase.end(); ++x)
        {
            which[x->second.info.time] = x->first;
        }

        map<int32_t, SRTSOCKET>::iterator y = which.begin();
        size_t ntodel = m_ClosedDatabase.size() - MAX_CLOSE_RECORD_SIZE;
        for (size_t i = 0; i < ntodel; ++i)
        {
            // Sanity check - should never happen because it's unlikely
            // that two different sockets were closed exactly at the same
            // nanosecond time.
            if (y == which.end())
                break;

            m_ClosedDatabase.erase(y->second);
            ++y;
        }
    }
}

bool CUDTSocket::closeInternal(int reason) ATR_NOEXCEPT
{
    bool done = m_UDT.closeEntity(reason);
    breakNonAcceptedSockets(); // XXX necessary?

    return done;
}

void CUDTSocket::breakNonAcceptedSockets()
{
    // In case of a listener socket, close also all incoming connection
    // sockets that have not been extracted as accepted.

    vector<SRTSOCKET> accepted;
    if (m_UDT.m_bListening)
    {
        HLOGC(smlog.Debug, log << "breakNonAcceptedSockets: @" << m_UDT.id() << " CHECKING ACCEPTED LEAKS:");
        ScopedLock lk (m_AcceptLock);

        for (map<SRTSOCKET, sockaddr_any>::iterator q = m_QueuedSockets.begin();
                q != m_QueuedSockets.end(); ++ q)
        {
            accepted.push_back(q->first);
        }
    }

    if (!accepted.empty())
    {
        HLOGC(smlog.Debug, log << "breakNonAcceptedSockets: found " << accepted.size() << " leaky accepted sockets");
        for (vector<SRTSOCKET>::iterator i = accepted.begin(); i != accepted.end(); ++i)
        {
            CUDTUnited::SocketKeeper sk(m_UDT.uglobal(), *i);
            if (sk.socket)
            {
                sk.socket->m_UDT.m_bBroken = true;
                sk.socket->m_UDT.m_iBrokenCounter = 0;
                sk.socket->m_UDT.m_bClosing = true;
                sk.socket->m_Status = SRTS_CLOSING;
            }
        }
    }
    else
    {
        HLOGC(smlog.Debug, log << "breakNonAcceptedSockets: no queued sockets");
    }
}

SRTSTATUS CUDTUnited::close(CUDTSocket* s, int reason)
{
    // Set the closing flag BEFORE you attempt to acquire
    s->setBreaking();

    HLOGC(smlog.Debug, log << s->core().CONID() << "CLOSE. Acquiring control lock");
    ScopedLock socket_cg(s->m_ControlLock);

    // The check for whether m_pRcvQueue isn't NULL is safe enough;
    // it can either be NULL after socket creation and without binding
    // and then once it's assigned, it's never reset to NULL even when
    // destroying the socket.
    CUDT& e = s->core();

    // Allow the socket to be closed by gc, if needed.
    e.m_bManaged = true;

    // Status is required to make sure that the socket passed through
    // the updateMux() and inside installMuxer() calls so that m_pRcvQueue
    // has been set to a non-NULL value. The value itself can't be checked
    // as such because it causes a data race. All checked data here are atomic.
    SRT_SOCKSTATUS st = s->m_Status;
    if (e.m_bConnecting && !e.m_bConnected && st >= SRTS_OPENED)
    {
        // Workaround for a design flaw.
        // It's to work around the case when the socket is being
        // closed in another thread while it's in the process of
        // connecting in the blocking mode, that is, it runs the
        // loop in `CUDT::startConnect` whole time under the lock
        // of CUDT::m_ConnectionLock and CUDTSocket::m_ControlLock
        // this way blocking the `srt_close` API call from continuing.
        // We are setting here the m_bClosing flag prematurely so
        // that the loop may check this flag periodically and exit
        // immediately if it's set.
        //
        // The problem is that this flag shall NOT be set in case
        // when you have a CONNECTED socket because not only isn't it
        // not a problem in this case, but also it additionally
        // turns the socket in a "confused" state in which it skips
        // vital part of closing itself and therefore runs an infinite
        // loop when trying to purge the sender buffer of the closing
        // socket.
        //
        // XXX Consider refax on CUDT::startConnect and removing the
        // connecting loop there and replace the "blocking mode specific"
        // connecting procedure with delegation to the receiver queue,
        // which will be then common with non-blocking mode, and synchronize
        // the blocking through a CV.

        e.m_bClosing = true;

        // XXX Kicking rcv q is no longer necessary. This was kicking the CV
        // that was sleeping on packet reception in CRcvQueue::m_mBuffer,
        // which was only used for communication with the blocking-mode
        // caller in original code. This code is now removed and the
        // blocking mode is using non-blocking mode with stalling on CV.
    }

    HLOGC(smlog.Debug, log << s->core().CONID() << "CLOSING (removing from listening, closing CUDT)");

    const bool synch_close_snd = s->core().m_config.bSynSending;

    SRTSOCKET u = s->id();

    if (s->m_Status == SRTS_LISTENING)
    {
        if (s->core().m_bBroken)
            return SRT_STATUS_OK;

        s->m_tsClosureTimeStamp = steady_clock::now();
        s->core().m_bBroken     = true;

        // Change towards original UDT:
        // Leave all the closing activities for garbageCollect to happen,
        // however remove the listener from the RcvQueue IMMEDIATELY.
        // Even though garbageCollect would eventually remove the listener
        // as well, there would be some time interval between now and the
        // moment when it's done, and during this time the application will
        // be unable to bind to this port that the about-to-delete listener
        // is currently occupying (due to blocked slot in the RcvQueue).

        HLOGC(smlog.Debug, log << s->core().CONID() << "CLOSING (removing listener immediately)");
        s->breakNonAcceptedSockets();

        // Do not lock m_GlobControlLock for that call; this would deadlock.
        // We also get the ID of the muxer, not the muxer object because to get
        // the muxer object you need to lock m_GlobControlLock. The ID may exist
        // without a multiplexer and we have a guarantee it will not be reused
        // for a long enough time. Worst case scenario, it won't be dispatched
        // to a multiplexer - already under a lock, of course.
        s->core().notListening();

        {
            // Need to protect the existence of the multiplexer.
            // Multiple threads are allowed to dispose it and only
            // one can succeed. But in this case here we need it
            // out possibly immediately.
            ExclusiveLock manager_cg(m_GlobControlLock);
            CMultiplexer* mux = tryUnbindClosedSocket(s->id());
            s->m_Status = SRTS_CLOSING;

            // As the listener that contains no spawned-off accepted
            // socket is being closed, it's withdrawn from the muxer.
            // This is the only way how it can be checked that this
            // multiplexer has lost all its sockets and therefore
            // should be deleted.

            // WARNING: checkRemoveMux is like "delete this".
            if (mux)
                checkRemoveMux(*mux);
        }

        // broadcast all "accept" waiting
        CSync::lock_notify_all(s->m_AcceptCond, s->m_AcceptLock);

        s->core().setAgentCloseReason(reason);
    }
    else
    {
        s->m_Status = SRTS_CLOSING;
        // Note: this call may be done on a socket that hasn't finished
        // sending all packets scheduled for sending, which means, this call
        // may block INDEFINITELY. As long as it's acceptable to block the
        // call to srt_close(), and all functions in all threads where this
        // very socket is used, this shall not block the central database.
        s->closeInternal(reason);

        // synchronize with garbage collection.
        HLOGC(smlog.Debug,
              log << "@" << u << "U::close done. GLOBAL CLOSE: " << s->core().CONID()
                  << "Acquiring GLOBAL control lock");
        ExclusiveLock manager_cg(m_GlobControlLock);
        // since "s" is located before m_GlobControlLock, locate it again in case
        // it became invalid
        // XXX This is very weird; if we state that the CUDTSocket object
        // could not be deleted between locks, then definitely it couldn't
        // also change the pointer value. There's no other reason for getting
        // this iterator but to obtain the 's' pointer, which is impossible to
        // be different than previous 's' (m_Sockets is a map that stores pointers
        // transparently). This iterator isn't even later used to delete the socket
        // from the container, though it would be more efficient.
        // FURTHER RESEARCH REQUIRED.
        sockets_t::iterator i = m_Sockets.find(u);
        if ((i == m_Sockets.end()) || (i->second->m_Status == SRTS_CLOSED))
        {
            HLOGC(smlog.Debug, log << "@" << u << "U::close: NOT AN ACTIVE SOCKET, returning.");
            return SRT_STATUS_OK;
        }
        s = i->second;
        s->setClosed();

#if SRT_ENABLE_BONDING
        if (s->m_GroupOf)
        {
            HLOGC(smlog.Debug,
                  log << "@" << s->id() << " IS MEMBER OF $" << s->m_GroupOf->id() << " - REMOVING FROM GROUP");
            s->removeFromGroup(true);
        }
#endif

        recordCloseReason(s);

        // You won't be updating any EIDs anymore.
        m_EPoll.wipe_usock(s->id(), s->core().m_sPollID);

        swipeSocket_LOCKED(s->id(), s, SWIPE_NOW);

        // Run right now the function that should attempt to delete the socket.
        // XXX Right now it will never work because the busy lock is applied on
        // the whole code calling this function, and with this lock, removal will
        // never happen.
        CMultiplexer* mux = tryUnbindClosedSocket(u);
        if (mux && mux->tryCloseIfEmpty())
        {
            // NOTE: ONLY AFTER stopping the workers can the SOCKET be deleted,
            // even after moving to closed and being unbound!
            checkRemoveMux(*mux);
        }

        HLOGC(smlog.Debug, log << "@" << u << "U::close: Socket MOVED TO CLOSED for collecting later.");

        CGlobEvent::triggerEvent();
    }

    HLOGC(smlog.Debug, log << "@" << u << ": GLOBAL: CLOSING DONE");

    // Check if the ID is still in closed sockets before you access it
    // (the last triggerEvent could have deleted it).
    if (synch_close_snd)
    {
#if SRT_ENABLE_CLOSE_SYNCH

        HLOGC(smlog.Debug, log << "@" << u << " GLOBAL CLOSING: sync-waiting for releasing sender resources...");
        for (;;)
        {
            CSndBuffer* sb = s->core().m_pSndBuffer;

            // Disconnected from buffer - nothing more to check.
            if (!sb)
            {
                HLOGC(smlog.Debug,
                      log << "@" << u << " GLOBAL CLOSING: sending buffer disconnected. Allowed to close.");
                break;
            }

            // Sender buffer empty
            if (sb->getCurrBufSize() == 0)
            {
                HLOGC(smlog.Debug, log << "@" << u << " GLOBAL CLOSING: sending buffer depleted. Allowed to close.");
                break;
            }

            // Ok, now you are keeping GC thread hands off the internal data.
            // You can check then if it has already deleted the socket or not.
            // The socket is either in m_ClosedSockets or is already gone.

            // Done the other way, but still done. You can stop waiting.
            bool isgone = false;
            {
                SharedLock manager_cg(m_GlobControlLock);
                isgone = m_ClosedSockets.count(u) == 0;
            }
            if (!isgone)
            {
                isgone = !s->core().m_bOpened;
            }
            if (isgone)
            {
                HLOGC(smlog.Debug,
                      log << "@" << u << " GLOBAL CLOSING: ... gone in the meantime, whatever. Exiting close().");
                break;
            }

            HLOGC(smlog.Debug, log << "@" << u << " GLOBAL CLOSING: ... still waiting for any update.");
            // How to handle a possible error here?
            CGlobEvent::waitForEvent();

            // Continue waiting in case when an event happened or 1s waiting time passed for checkpoint.
        }
#endif
    }

    /*
       This code is PUT ASIDE for now.
       Most likely this will be never required.
       It had to hold the closing activity until the time when the receiver buffer is depleted.
       However the closing of the socket should only happen when the receiver has received
       an information about that the reading is no longer possible (error report from recv/recvfile).
       When this happens, the receiver buffer is definitely depleted already and there's no need to check
       anything.

       Should there appear any other conditions in future under which the closing process should be
       delayed until the receiver buffer is empty, this code can be filled here.

    if ( synch_close_rcv )
    {
    ...
    }
    */
    CSync::notify_one_relaxed(m_GCStopCond);

    return SRT_STATUS_OK;
}

void CUDTUnited::getpeername(const SRTSOCKET u, sockaddr* pw_name, int* pw_namelen)
{
    if (!pw_name || !pw_namelen)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    if (getStatus(u) != SRTS_CONNECTED)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    CUDTSocket* s = locateSocket(u);

    if (!s)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    if (!s->core().m_bConnected || s->core().m_bBroken)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    const int len = s->m_PeerAddr.size();
    if (*pw_namelen < len)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    memcpy((pw_name), &s->m_PeerAddr.sa, len);
    *pw_namelen = len;
}

void CUDTUnited::getsockname(const SRTSOCKET u, sockaddr* pw_name, int* pw_namelen)
{
    if (!pw_name || !pw_namelen)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    CUDTSocket* s = locateSocket(u);

    if (!s)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    if (s->core().m_bBroken)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    if (s->m_Status == SRTS_INIT)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    const int len = s->m_SelfAddr.size();
    if (*pw_namelen < len)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    memcpy((pw_name), &s->m_SelfAddr.sa, len);
    *pw_namelen = len;
}

void CUDTUnited::getsockdevname(const SRTSOCKET u, char* pw_name, size_t* pw_namelen)
{
    if (!pw_name || !pw_namelen)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    CUDTSocket* s = locateSocket(u);

    if (!s)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    if (s->core().m_bBroken)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    if (s->m_Status == SRTS_INIT)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

    const vector<LocalInterface>& locals = GetLocalInterfaces();

    for (vector<LocalInterface>::const_iterator i = locals.begin(); i != locals.end(); ++i)
    {
        if (i->addr.equal_address(s->m_SelfAddr))
        {
            if (*pw_namelen < i->name.size() + 1)
                throw CUDTException(MJ_NOTSUP, MN_INVAL);
            memcpy((pw_name), i->name.c_str(), i->name.size()+1);
            *pw_namelen = i->name.size();
            return;
        }
    }

    *pw_namelen = 0; // report empty one
}

int CUDTUnited::select(std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, std::set<SRTSOCKET>* exceptfds, const timeval* timeout)
{
    const steady_clock::time_point entertime = steady_clock::now();

    const int64_t timeo_us = timeout ? static_cast<int64_t>(timeout->tv_sec) * 1000000 + timeout->tv_usec : -1;
    const steady_clock::duration timeo(microseconds_from(timeo_us));

    // initialize results
    int            count = 0;
    set<SRTSOCKET> rs, ws, es;

    // retrieve related UDT sockets
    vector<CUDTSocket*> ru, wu, eu;
    CUDTSocket*         s;
    if (readfds)
        for (set<SRTSOCKET>::iterator i1 = readfds->begin(); i1 != readfds->end(); ++i1)
        {
            if (getStatus(*i1) == SRTS_BROKEN)
            {
                rs.insert(*i1);
                ++count;
            }
            else if (!(s = locateSocket(*i1)))
                throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
            else
                ru.push_back(s);
        }
    if (writefds)
        for (set<SRTSOCKET>::iterator i2 = writefds->begin(); i2 != writefds->end(); ++i2)
        {
            if (getStatus(*i2) == SRTS_BROKEN)
            {
                ws.insert(*i2);
                ++count;
            }
            else if (!(s = locateSocket(*i2)))
                throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
            else
                wu.push_back(s);
        }
    if (exceptfds)
        for (set<SRTSOCKET>::iterator i3 = exceptfds->begin(); i3 != exceptfds->end(); ++i3)
        {
            if (getStatus(*i3) == SRTS_BROKEN)
            {
                es.insert(*i3);
                ++count;
            }
            else if (!(s = locateSocket(*i3)))
                throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
            else
                eu.push_back(s);
        }

    do
    {
        // query read sockets
        for (vector<CUDTSocket*>::iterator j1 = ru.begin(); j1 != ru.end(); ++j1)
        {
            s = *j1;

            if (s->readReady() || s->m_Status == SRTS_CLOSED)
            {
                rs.insert(s->id());
                ++count;
            }
        }

        // query write sockets
        for (vector<CUDTSocket*>::iterator j2 = wu.begin(); j2 != wu.end(); ++j2)
        {
            s = *j2;

            if (s->writeReady() || s->m_Status == SRTS_CLOSED)
            {
                ws.insert(s->id());
                ++count;
            }
        }

        // query exceptions on sockets
        for (vector<CUDTSocket*>::iterator j3 = eu.begin(); j3 != eu.end(); ++j3)
        {
            // check connection request status, not supported now
        }

        if (0 < count)
            break;

        CGlobEvent::waitForEvent();
    } while (timeo > steady_clock::now() - entertime);

    if (readfds)
        *readfds = rs;

    if (writefds)
        *writefds = ws;

    if (exceptfds)
        *exceptfds = es;

    return count;
}

// XXX This may crash when a member socket is added to selectEx.
// Consider revising to prevent a member socket from being used.
int CUDTUnited::selectEx(const vector<SRTSOCKET>& fds,
                              vector<SRTSOCKET>*       readfds,
                              vector<SRTSOCKET>*       writefds,
                              vector<SRTSOCKET>*       exceptfds,
                              int64_t                  msTimeOut)
{
    const steady_clock::time_point entertime = steady_clock::now();

    const int64_t                timeo_us = msTimeOut >= 0 ? msTimeOut * 1000 : -1;
    const steady_clock::duration timeo(microseconds_from(timeo_us));

    // initialize results
    int count = 0;
    if (readfds)
        readfds->clear();
    if (writefds)
        writefds->clear();
    if (exceptfds)
        exceptfds->clear();

    do
    {
        for (vector<SRTSOCKET>::const_iterator i = fds.begin(); i != fds.end(); ++i)
        {
            CUDTSocket* s = locateSocket(*i);

            if ((!s) || s->core().m_bBroken || (s->m_Status == SRTS_CLOSED) || s->m_GroupOf)
            {
                if (exceptfds)
                {
                    exceptfds->push_back(*i);
                    ++count;
                }
                continue;
            }

            if (readfds)
            {
                if ((s->core().m_bConnected && s->core().isRcvBufferReady()) ||
                    (s->core().m_bListening && (s->m_QueuedSockets.size() > 0)))
                {
                    readfds->push_back(s->id());
                    ++count;
                }
            }

            if (writefds)
            {
                if (s->core().m_bConnected &&
                    (s->core().m_pSndBuffer->getCurrBufSize() < s->core().m_config.iSndBufSize))
                {
                    writefds->push_back(s->id());
                    ++count;
                }
            }
        }

        if (count > 0)
            break;

        CGlobEvent::waitForEvent();
    } while (timeo > steady_clock::now() - entertime);

    return count;
}

int CUDTUnited::epoll_create()
{
    return m_EPoll.create();
}

void CUDTUnited::epoll_clear_usocks(int eid)
{
    return m_EPoll.clear_usocks(eid);
}

void CUDTUnited::epoll_add_usock(const int eid, const SRTSOCKET u, const int* events)
{
#if SRT_ENABLE_BONDING
    if (CUDT::isgroup(u))
    {
        GroupKeeper k(*this, u, ERH_THROW);
        m_EPoll.update_usock(eid, u, events);
        k.group->addEPoll(eid);
        return;
    }
#endif

    // The call to epoll_add_usock_INTERNAL is expected
    // to be called under m_GlobControlLock, so use this lock here, too.
    {
        SharedLock cs (m_GlobControlLock);
        CUDTSocket* s = locateSocket_LOCKED(u);
        if (s)
        {
            epoll_add_usock_INTERNAL(eid, s, events);
        }
        else
        {
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL);
        }
    }
}

// NOTE: WILL LOCK (serially):
// - CEPoll::m_EPollLock
// - CUDT::m_RecvLock
void CUDTUnited::epoll_add_usock_INTERNAL(const int eid, CUDTSocket* s, const int* events)
{
    m_EPoll.update_usock(eid, s->id(), events);
    s->core().addEPoll(eid);
}

void CUDTUnited::epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events)
{
    return m_EPoll.add_ssock(eid, s, events);
}

void CUDTUnited::epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events)
{
    return m_EPoll.update_ssock(eid, s, events);
}

template <class EntityType>
void CUDTUnited::epoll_remove_entity(const int eid, EntityType* ent)
{
    // XXX Not sure if this is anyhow necessary because setting readiness
    // to false doesn't actually trigger any action. Further research needed.
    HLOGC(ealog.Debug, log << "epoll_remove_usock: CLEARING readiness on E" << eid << " of @" << ent->id());
    ent->removeEPollEvents(eid);

    // First remove the EID from the subscribed in the socket so that
    // a possible call to update_events:
    // - if happens before this call, can find the epoll bit update possible
    // - if happens after this call, will not strike this EID
    HLOGC(ealog.Debug, log << "epoll_remove_usock: REMOVING E" << eid << " from back-subscirbers in @" << ent->id());
    ent->removeEPollID(eid);

    HLOGC(ealog.Debug, log << "epoll_remove_usock: CLEARING subscription on E" << eid << " of @" << ent->id());
    int no_events = 0;
    m_EPoll.update_usock(eid, ent->id(), &no_events);
}

// Needed internal access!
void CUDTUnited::epoll_remove_socket_INTERNAL(const int eid, CUDTSocket* s)
{
    return epoll_remove_entity(eid, &s->core());
}

#if SRT_ENABLE_BONDING
void CUDTUnited::epoll_remove_group_INTERNAL(const int eid, CUDTGroup* g)
{
    return epoll_remove_entity(eid, g);
}
#endif

void CUDTUnited::epoll_remove_usock(const int eid, const SRTSOCKET u)
{
    CUDTSocket* s = 0;

#if SRT_ENABLE_BONDING
    CUDTGroup* g = 0;
    if (CUDT::isgroup(u))
    {
        GroupKeeper k(*this, u, ERH_THROW);
        g = k.group;
        return epoll_remove_entity(eid, g);
    }
    else
#endif
    {
        s = locateSocket(u);
        if (s)
            return epoll_remove_entity(eid, &s->core());
    }

    LOGC(ealog.Error,
         log << "remove_usock: @" << u << " not found as either socket or group. Removing only from epoll system.");
    int no_events = 0;
    return m_EPoll.update_usock(eid, u, &no_events);
}

void CUDTUnited::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
    return m_EPoll.remove_ssock(eid, s);
}

int CUDTUnited::epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut)
{
    return m_EPoll.uwait(eid, fdsSet, fdsSize, msTimeOut);
}

int32_t CUDTUnited::epoll_set(int eid, int32_t flags)
{
    return m_EPoll.setflags(eid, flags);
}

void CUDTUnited::epoll_release(const int eid)
{
    return m_EPoll.release(eid);
}

CUDTSocket* CUDTUnited::locateSocket(const SRTSOCKET u, ErrorHandling erh)
{
    SharedLock cg(m_GlobControlLock);
    return locateSocket_LOCKED(u, erh);
}

// [[using locked(m_GlobControlLock)]];
CUDTSocket* CUDTUnited::locateSocket_LOCKED(SRTSOCKET u, ErrorHandling erh)
{
    sockets_t::iterator i = m_Sockets.find(u);

    if ((i == m_Sockets.end()) || (i->second->m_Status == SRTS_CLOSED))
    {
        if (erh == ERH_RETURN)
            return NULL;
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
    }

    return i->second;
}

#if SRT_ENABLE_BONDING
CUDTGroup* CUDTUnited::locateAcquireGroup(SRTSOCKET u, ErrorHandling erh)
{
    SharedLock cg(m_GlobControlLock);

    const groups_t::iterator i = m_Groups.find(u);
    if (i == m_Groups.end())
    {
        if (erh == ERH_THROW)
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
        return NULL;
    }

    ScopedLock cgroup(*i->second->exp_groupLock());
    i->second->apiAcquire();
    return i->second;
}

CUDTGroup* CUDTUnited::acquireSocketsGroup(CUDTSocket* s)
{
    SharedLock cg(m_GlobControlLock);
    CUDTGroup* g = s->m_GroupOf;
    if (!g)
        return NULL;

    // With m_GlobControlLock locked, we are sure the group
    // still exists, if it wasn't removed from this socket.
    g->apiAcquire();
    return g;
}
#endif

CUDTSocket* CUDTUnited::locateAcquireSocket(SRTSOCKET u, ErrorHandling erh)
{
    SharedLock cg(m_GlobControlLock);

    CUDTSocket* s = locateSocket_LOCKED(u);
    if (!s)
    {
        if (erh == ERH_THROW)
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
        return NULL;
    }

    s->apiAcquire();
    return s;
}

bool CUDTUnited::acquireSocket(CUDTSocket* s)
{
    // Note that before using this function you must be certain
    // that the socket isn't broken already and it still has at least
    // one more GC cycle to live. In other words, you must be certain
    // that this pointer passed here isn't dangling and was obtained
    // directly from m_Sockets, or even better, has been acquired
    // by some other functionality already, which is only about to
    // be released earlier than you need.
    SharedLock cg(m_GlobControlLock);
    s->apiAcquire();
    // Keep the lock so that no one changes anything in the meantime.
    // If the socket m_Status == SRTS_CLOSED (set by setClosed()), then
    // this socket is no longer present in the m_Sockets container
    if (s->m_Status >= SRTS_CLOSED)
    {
        s->apiRelease();
        return false;
    }

    return true;
}

void CUDTUnited::releaseSocket(CUDTSocket* s)
{
    SRT_ASSERT(s && s->isStillBusy() > 0);

    SharedLock cg(m_GlobControlLock);
    s->apiRelease();
}

CUDTSocket* CUDTUnited::locatePeer(const sockaddr_any& peer, const SRTSOCKET id, int32_t isn)
{
    SharedLock cg(m_GlobControlLock);

    map<int64_t, set<SRTSOCKET> >::iterator i = m_PeerRec.find(CUDTSocket::getPeerSpec(id, isn));
    if (i == m_PeerRec.end())
        return NULL;

    for (set<SRTSOCKET>::iterator j = i->second.begin(); j != i->second.end(); ++j)
    {
        sockets_t::iterator k = m_Sockets.find(*j);
        // this socket might have been closed and moved m_ClosedSockets
        if (k == m_Sockets.end())
            continue;

        if (k->second->m_PeerAddr == peer)
        {
            return k->second;
        }
    }

    return NULL;
}

void CUDTUnited::checkBrokenSockets()
{
    ExclusiveLock cg(m_GlobControlLock);

#if SRT_ENABLE_BONDING
    vector<SRTSOCKET> delgids;

    for (groups_t::iterator i = m_ClosedGroups.begin(); i != m_ClosedGroups.end(); ++i)
    {
        // isStillBusy requires lock on the group, so only after an API
        // function that uses it returns, and so clears the busy flag,
        // a new API function won't be called anyway until it can acquire
        // GlobControlLock, and all functions that have already seen this
        // group as closing will not continue with the API and return.
        // If we caught some API function still using the closed group,
        // it's not going to wait, will be checked next time.
        if (i->second->isStillBusy())
            continue;

        delgids.push_back(i->first);
        delete i->second;
        i->second = NULL; // just for a case, avoid a dangling pointer
    }

    for (vector<SRTSOCKET>::iterator di = delgids.begin(); di != delgids.end(); ++di)
    {
        m_ClosedGroups.erase(*di);
    }
#endif

    // set of sockets To Be Closed and To Be Removed
    vector<SRTSOCKET> tbc;
    vector<SRTSOCKET> tbr;

    for (sockets_t::iterator i = m_Sockets.begin(); i != m_Sockets.end(); ++i)
    {
        CUDTSocket* s = i->second;
        CUDT& c = s->core();
        if (!c.m_bBroken)
            continue;

        if (!m_bGCClosing && !c.m_bManaged)
        {
            HLOGC(cnlog.Debug, log << "Socket @" << s->id() << " isn't managed and wasn't explicitly closed - NOT collecting");
            continue;
        }

        HLOGC(cnlog.Debug, log << "Socket @" << s->id() << " considered wiped: managed=" <<
                c.m_bManaged << " broken=" << c.m_bBroken << " closing=" << c.m_bClosing);

        if (s->m_Status == SRTS_LISTENING)
        {
            const steady_clock::duration elapsed = steady_clock::now() - s->m_tsClosureTimeStamp.load();
            // A listening socket should wait an extra 3 seconds
            // in case a client is connecting.
            if (elapsed < milliseconds_from(CUDT::COMM_CLOSE_BROKEN_LISTENER_TIMEOUT_MS))
                continue;
        }
        else

        // Additional note on group receiver: with the new group
        // receiver m_pRcvBuffer in the socket core is NULL always,
        // but that's not a problem - you can close the member socket
        // safely without worrying about reading data because they are
        // in the group anyway.
        {
            CUDT& u = s->core();

            enterCS(u.m_RcvBufferLock);
            bool has_avail_packets = u.m_pRcvBuffer && u.m_pRcvBuffer->hasAvailablePackets();
            leaveCS(u.m_RcvBufferLock);

            if (has_avail_packets)
            {
                const int bc = u.m_iBrokenCounter.load();
                if (bc > 0)
                {
                    // if there is still data in the receiver buffer, wait longer
                    s->core().m_iBrokenCounter.store(bc - 1);
                    continue;
                }
            }
        }

#if SRT_ENABLE_BONDING
        if (s->m_GroupOf)
        {
            HLOGC(smlog.Debug,
                 log << "@" << s->id() << " IS MEMBER OF $" << s->m_GroupOf->id() << " - REMOVING FROM GROUP");
            s->removeFromGroup(true);
        }
#endif

        HLOGC(smlog.Debug, log << "checkBrokenSockets: moving BROKEN socket to CLOSED: @" << i->first);

        // Note that this will not override the value that has been already
        // set by some other functionality, only set it when not yet set.
        s->core().setAgentCloseReason(SRT_CLS_INTERNAL);

        recordCloseReason(s);

        // close broken connections and start removal timer
        s->setClosed();
        tbc.push_back(i->first);

        // NOTE: removal from m_SocketID POSTPONED
        // to loop over removal of all from the `tbc` list
        swipeSocket_LOCKED(i->first, s, SWIPE_LATER);

        if (s->m_ListenSocket != SRT_SOCKID_CONNREQ)
        {
            // remove from listener's queue
            sockets_t::iterator ls = m_Sockets.find(s->m_ListenSocket);
            if (ls == m_Sockets.end())
            {
                ls = m_ClosedSockets.find(s->m_ListenSocket);
                if (ls == m_ClosedSockets.end())
                    continue;
            }

            HLOGC(smlog.Debug, log << "checkBrokenSockets: removing queued socket: @" << s->id()
                    << " from listener @" << ls->second->id());
            enterCS(ls->second->m_AcceptLock);
            ls->second->m_QueuedSockets.erase(s->id());
            leaveCS(ls->second->m_AcceptLock);
        }
    }

    for (sockets_t::iterator j = m_ClosedSockets.begin(); j != m_ClosedSockets.end(); ++j)
    {
        CUDTSocket* ps = j->second;

        // NOTE: There is still a hypothetical risk here that ps
        // was made busy while the socket was already moved to m_ClosedSocket,
        // if the socket was acquired through CUDTUnited::acquireSocket (that is,
        // busy flag acquisition was done through the CUDTSocket* pointer rather
        // than through the numeric ID). Therefore this way of busy acquisition
        // should be done only if at the moment of acquisition there are certainly
        // other conditions applying on the socket that prevent it from being deleted.
        if (ps->isStillBusy())
        {
            HLOGC(smlog.Debug, log << "checkBrokenSockets: @" << ps->id() << " is still busy, SKIPPING THIS CYCLE.");
            continue;
        }

        CUDT& u = ps->core();

        // HLOGC(smlog.Debug, log << "checking CLOSED socket: " << j->first);
        if (!is_zero(u.m_tsLingerExpiration))
        {
            // asynchronous close:
            if ((!u.m_pSndBuffer) || (0 == u.m_pSndBuffer->getCurrBufSize()) ||
                (u.m_tsLingerExpiration <= steady_clock::now()))
            {
                HLOGC(smlog.Debug, log << "checkBrokenSockets: marking CLOSED linger-expired @" << ps->id());
                u.m_tsLingerExpiration = steady_clock::time_point();
                u.m_bClosing           = true;
                ps->m_tsClosureTimeStamp        = steady_clock::now();
            }
            else
            {
                HLOGC(smlog.Debug, log << "checkBrokenSockets: linger; remains @" << ps->id());
            }
        }

        // timeout 1 second to destroy a socket AND it has been removed from
        // RcvUList
        const steady_clock::time_point now        = steady_clock::now();
        const steady_clock::duration   closed_ago = now - ps->m_tsClosureTimeStamp.load();
        if (closed_ago > seconds_from(1))
        {
            HLOGC(smlog.Debug, log << "checkBrokenSockets: @" << ps->id() << " closed "
                    << FormatDuration(closed_ago) << " ago and removed from RcvQ - will remove");

            // HLOGC(smlog.Debug, log << "will unref socket: " << j->first);
            tbr.push_back(j->first);
        }
    }

    // move closed sockets to the ClosedSockets structure
    for (vector<SRTSOCKET>::iterator k = tbc.begin(); k != tbc.end(); ++k)
        m_Sockets.erase(*k);

    // remove those timeout sockets
    for (vector<SRTSOCKET>::iterator l = tbr.begin(); l != tbr.end(); ++l)
    {
        CMultiplexer* mux = tryRemoveClosedSocket(*l);
        if (mux)
            checkRemoveMux(*mux);
    }

    HLOGC(smlog.Debug, log << "checkBrokenSockets: after removal: m_ClosedSockets.size()=" << m_ClosedSockets.size());
}

// [[using locked(m_GlobControlLock)]]
void CUDTUnited::closeLeakyAcceptSockets(CUDTSocket* s)
{
    ScopedLock cg(s->m_AcceptLock);

    // if it is a listener, close all un-accepted sockets in its queue
    // and remove them later
    for (map<SRTSOCKET, sockaddr_any>::iterator q = s->m_QueuedSockets.begin();
            q != s->m_QueuedSockets.end(); ++ q)
    {
        sockets_t::iterator si = m_Sockets.find(q->first);
        if (si == m_Sockets.end())
        {
            // gone in the meantime
            LOGC(smlog.Error,
                    log << "closeLeakyAcceptSockets: IPE? socket @" << (q->first) << " being queued for listener socket @"
                    << s->id() << " is GONE in the meantime ???");
            continue;
        }

        CUDTSocket* as = si->second;

        as->breakSocket_LOCKED(SRT_CLS_DEADLSN);

        // You won't be updating any EIDs anymore.
        m_EPoll.wipe_usock(as->id(), as->core().m_sPollID);

        swipeSocket_LOCKED(q->first, as, SWIPE_NOW);
    }
}

// Unbind the socket, and if it was the only user of the multiplexer, delete it
// (otherwise there would be no one to delete it later). If this is not
// possible, keep it bound and let this be repeated in the GC. The goal is to
// free the bindpoint when closing a socket, IF POSSIBLE.
// [[using locked(m_GlobControlLock)]]
CMultiplexer* CUDTUnited::tryUnbindClosedSocket(const SRTSOCKET u)
{
    sockets_t::iterator i = m_ClosedSockets.find(u);

    // invalid socket ID
    if (i == m_ClosedSockets.end())
        return NULL;

    CUDTSocket* s = i->second;

    // (just in case, this should be wiped out already)
    m_EPoll.wipe_usock(u, s->core().m_sPollID);

    // IMPORTANT!!!
    //
    // The order of deletion must be: first delete socket, then multiplexer.
    // The socket keeps the objects of CUnit type that belong to the multiplexer's
    // unit queue, so the socket must free them first before the multiplexer is deleted.
    const int mid = s->m_iMuxID;
    if (mid == -1)
    {
        HLOGC(smlog.Debug, log << CONID(u) << "has NO MUXER ASSOCIATED, ok.");
        return NULL;
    }

    CMultiplexer* mux = map_getp(m_mMultiplexer, mid);
    if (!mux)
    {
        LOGC(smlog.Fatal, log << "IPE: MUXER id=" << mid << " NOT FOUND!");
        return NULL;
    }

    // NOTE: This function must be obligatory called before attempting
    // to call CMultiplexer::stop() - unbinding shall never happen from
    // a multiplexer's worker thread; that would be a self-destruction.
    if (mux->isSelfDestructAttempt())
    {
        LOGC(smlog.Error, log << "tryUnbindClosedSocket: IPE: ATTEMPTING TO CALL from a worker thread - NOT REMOVING");
        return NULL;
    }

    // Unpin this socket from the multiplexer.
    s->m_iMuxID = -1;
    mux->deleteSocket(u);
    HLOGC(smlog.Debug, log << CONID(u) << "deleted from MUXER and cleared muxer ID, BUT NOT CLOSED");

    return mux;
}

// [[using locked(m_GlobControlLock)]]
CMultiplexer* CUDTUnited::tryRemoveClosedSocket(const SRTSOCKET u)
{
    sockets_t::iterator i = m_ClosedSockets.find(u);

    // invalid socket ID
    if (i == m_ClosedSockets.end())
        return NULL;

    CUDTSocket* s = i->second;

    if (s->isStillBusy())
    {
        HLOGC(smlog.Debug, log << "@" << s->id() << " is still busy, NOT deleting");
        return NULL;
    }

    HLOGC(smlog.Debug, log << "@" << s->id() << " busy=" << s->isStillBusy());

#if SRT_ENABLE_BONDING
    if (s->m_GroupOf)
    {
        HLOGC(smlog.Debug,
              log << "@" << s->id() << " IS MEMBER OF $" << s->m_GroupOf->id() << " - REMOVING FROM GROUP");
        s->removeFromGroup(true);
    }
#endif

    closeLeakyAcceptSockets(s);

    // remove from peer rec
    map<int64_t, set<SRTSOCKET> >::iterator j = m_PeerRec.find(s->getPeerSpec());
    if (j != m_PeerRec.end())
    {
        j->second.erase(u);
        if (j->second.empty())
            m_PeerRec.erase(j);
    }

    /*
     * Socket may be deleted while still having ePoll events set that would
     * remains forever causing epoll_wait to unblock continuously for inexistent
     * sockets. Get rid of all events for this socket.
     */
    // (just in case, this should be wiped out already)
    m_EPoll.wipe_usock(u, s->core().m_sPollID);

    // delete this one
    m_ClosedSockets.erase(i);

   // XXX This below section can unlock m_GlobControlLock
   // just for calling CUDT::closeInternal(), which is needed
   // to avoid locking m_ConnectionLock after m_GlobControlLock,
   // while m_ConnectionLock orders BEFORE m_GlobControlLock.
   // This should be perfectly safe thing to do after the socket
   // ID has been erased from m_ClosedSockets. No container access
   // is done in this case.
   //
   // Report: P04-1.28, P04-2.27, P04-2.50, P04-2.55

    HLOGC(smlog.Debug, log << "GC/tryRemoveClosedSocket: closing associated UDT @" << u);

    leaveCS(m_GlobControlLock);
    s->closeInternal(SRT_CLS_INTERNAL);
    enterCS(m_GlobControlLock);

    // Check again after reacquisition
    if (s->isStillBusy())
    {
        HLOGC(smlog.Debug, log << "@" << s->id() << " is still busy, NOT deleting");
        return NULL;
    }

    // IMPORTANT!!!
    //
    // The order of deletion must be: first delete socket, then multiplexer.
    // The receiver buffer shares the use of CUnits from the multiplexer's unit queue,
    // which is assigned to the multiplexer because this is where the incoming
    // UDP packets are placed. The receiver buffer must be first deleted and
    // so unreference all CUnits. Then the multiplexer can be deleted and drag all
    // CUnits with itself.
    const int mid = s->m_iMuxID;
    CMultiplexer* mux = NULL;
    if (mid == -1)
    {
        HLOGC(smlog.Debug, log << CONID(u) << "has NO MUXER ASSOCIATED, ok.");
    }
    else
    {
        mux = map_getp(m_mMultiplexer, mid);
        if (!mux)
        {
            LOGC(smlog.Fatal, log << "IPE: MUXER id=" << mid << " NOT FOUND!");
        }
        else
        {
            // Unpin this socket from the multiplexer.
            s->m_iMuxID = -1;
            mux->deleteSocket(u);
            HLOGC(smlog.Debug, log << CONID(u) << "deleted from MUXER and cleared muxer ID");
        }
    }
    HLOGC(smlog.Debug, log << "GC/tryRemoveClosedSocket: DELETING SOCKET @" << u);
    delete s;
    HLOGC(smlog.Debug, log << "GC/tryRemoveClosedSocket: socket @" << u << " DELETED. Checking muxer id=" << mid);

    return mux;
}

/// Check after removal of a socket from the multiplexer if it was the
/// last one and hence the multiplexer itself should be removed.
///
/// @param mid Muxer ID that identifies the multiplexer in the socket
/// @param u Socket ID that was the last multiplexer's user (logging only)
// [[using locked(m_GlobControlLock)]]
void CUDTUnited::checkRemoveMux(CMultiplexer& mx)
{
    const int mid = mx.id();
    HLOGC(smlog.Debug, log << "checkRemoveMux: unrefing muxer " << mid << ", with " << mx.nsockets() << " sockets");
    if (mx.empty())
    {
        HLOGC(smlog.Debug, log << "MUXER id=" << mid << " lost last socket - deleting muxer bound to "
                << mx.channel()->bindAddressAny().str());
        // The channel has no access to the queues and
        // it looks like the multiplexer is the master of all of them.
        // The queues must be silenced before closing the channel
        // because this will cause error to be returned in any operation
        // being currently done in the queues, if any.
        mx.setClosing();

        if (mx.reserveDisposal())
        {
            CGlobEvent::triggerEvent(); // make sure no hangs when exiting workers
            HLOGC(smlog.Debug, log << "... RESERVED for disposal. Stopping threads..");
            // Disposal reserved to this thread. Now you can safely
            // unlock m_GlobControlLock and be sure that no other thread
            // is going to dispose this multiplexer. Some may attempt to also
            // reserve disposal, but they will fail.
            {
                leaveCS(m_GlobControlLock);
                mx.stopWorkers();
                HLOGC(smlog.Debug, log << "... Worker threads stopped, reacquiring mutex..");
                enterCS(m_GlobControlLock);
            }
            // After re-locking m_GlobControlLock we are certain
            // that the privilege of deleting this multiplexer is still
            // on this thread.
            HLOGC(smlog.Debug, log << "... Muxer destroyed, removing");
            m_mMultiplexer.erase(mid);
        }
        else
        {
            HLOGC(smlog.Debug, log << "... NOT RESERVED to disposal, already reserved");
            // Some other thread has already reserved disposal for itself
            // hence promising to dispose this multiplexer. You can safely leave
            // it here.
        }
    }
    else
    {
#if HVU_ENABLE_HEAVY_LOGGING
        string users = mx.nsockets() ? mx.testAllSocketsClear() : string();

        LOGC(smlog.Debug, log << "MUXER id=" << mid << " has still " << mx.nsockets() << " users" << users);
#endif
    }
}

void CUDTUnited::checkTemporaryDatabases()
{
    ExclusiveLock cg(m_GlobControlLock);

    // It's not very efficient to collect first the keys of all
    // elements to remove and then remove from the map by key.

    // In C++20 this is possible by doing
    //    m_ClosedDatabase.erase_if([](auto& c) { return --c.generation <= 0; });
    // but nothing equivalent in the earlier standards.

    vector<SRTSOCKET> expired;

    for (map<SRTSOCKET, CloseInfo>::iterator c = m_ClosedDatabase.begin();
            c != m_ClosedDatabase.end(); ++c)
    {
        --c->second.generation;
        if (c->second.generation <= 0)
            expired.push_back(c->first);
    }

    for (vector<SRTSOCKET>::iterator i = expired.begin(); i != expired.end(); ++i)
        m_ClosedDatabase.erase(*i);
}

// Muxer in this function is added a socket to its lists and pinning
// it into the socket, but does not modify any multiplexer's data.
void CUDTUnited::installMuxer(CUDTSocket* pw_s, CMultiplexer* fw_pm)
{
    pw_s->core().m_pMuxer    = fw_pm;
    pw_s->m_iMuxID           = fw_pm->id();

    pw_s->m_SelfAddr = fw_pm->selfAddr();
    fw_pm->addSocket(pw_s);
}

#if HVU_ENABLE_LOGGING
inline static const char* IPv6OnlyStr(int val)
{
    if (val == 0)
        return "IPv4+IPv6";
    if (val == 1)
        return "IPv6-only";
    return "UNSET";
}
#endif

bool CUDTUnited::inet6SettingsCompat(const sockaddr_any& muxaddr, const CSrtMuxerConfig& cfgMuxer,
        const sockaddr_any& reqaddr, const CSrtMuxerConfig& cfgSocket)
{
    if (muxaddr.family() != AF_INET6)
        return true; // Don't check - the family has been checked already

    if (reqaddr.isany())
    {
        if (cfgSocket.iIpV6Only == -1) // Treat as "adaptive"
            return true;

        // If set explicitly, then it must be equal to the one of found muxer.
        if (cfgSocket.iIpV6Only != cfgMuxer.iIpV6Only)
        {
            LOGC(smlog.Error, log << "inet6SettingsCompat: incompatible IPv6: muxer="
                    << IPv6OnlyStr(cfgMuxer.iIpV6Only) << " socket=" << IPv6OnlyStr(cfgSocket.iIpV6Only));
            return false;
        }
    }

    // If binding to the certain IPv6 address, then this setting doesn't matter.
    return true;
}

bool CUDTUnited::channelSettingsMatch(const CSrtMuxerConfig& cfgMuxer, const CSrtConfig& cfgSocket)
{
    if (!cfgMuxer.bReuseAddr)
    {
        HLOGP(smlog.Debug, "channelSettingsMatch: fail: the multiplexer is not reusable");
        return false;
    }

    if (cfgMuxer.isCompatWith(cfgSocket))
        return true;

    HLOGP(smlog.Debug, "channelSettingsMatch: fail: some options have different values");
    return false;
}

void CUDTUnited::updateMux(CUDTSocket* s, const sockaddr_any& reqaddr, const UDPSOCKET* udpsock /*[[nullable]]*/)
{
    ExclusiveLock cg(m_GlobControlLock);

    // If udpsock is provided, then this socket will be simply
    // taken for binding as a good deal. It would be nice to make
    // a sanity check to see if this UDP socket isn't already installed
    // in some multiplexer, but we state this UDP socket isn't accessible
    // anyway so this wouldn't be possible.
    if (!udpsock)
    {
        CMultiplexer* pmux = findSuitableMuxer(s, reqaddr);
        if (pmux)
        {
            HLOGC(smlog.Debug, log << "bind: reusing multiplexer for " << pmux->selfAddr().str());
            // reuse the existing multiplexer
            installMuxer((s), (pmux));
            return;
        }
    }
    // We state that if the user has passed their own UDP socket,
    // it is either bound already - and did so without any conflicts
    // with the existing SRT socket's multiplexer - or is not bound.

    // a new multiplexer is needed
    int muxid = (int32_t)s->id();

    try
    {
        std::pair<CMultiplexer&, bool> is = map_tryinsert(m_mMultiplexer, muxid);

        // Should be impossible, but must be prevented.
        // NOTE: map::insert with a pair simply ignores the passed value,
        // if the key is already found.
        if (!is.second)
        {
            LOGC(smlog.Error, log << "IPE: Trying to add multiplexer with id=" << muxid << " which is already busy");
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND);
        }
        CMultiplexer& m = is.first;
        m.configure(int32_t(s->id()), s->core().m_config, reqaddr, udpsock);
        installMuxer((s), (&m));
    }
    catch (const CUDTException& x)
    {
        HLOGC(smlog.Debug, log << "installMuxer: FAILED; removing multiplexer: ERROR #" << x.getErrorCode()
                << ": " << x.getErrorMessage() << ": errno=" << x.getErrno() << ": " << hvu::SysStrError(x.getErrno()));
        m_mMultiplexer.erase(muxid);
        throw;
    }
    catch (...)
    {
        HLOGC(smlog.Debug, log << "installMuxer: FAILED; removing multiplexer (IPE: UNKNOWN EXCEPTION)");
        m_mMultiplexer.erase(muxid);
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }

    HLOGC(smlog.Debug, log << "bind: creating new multiplexer bound to " << reqaddr.str());
}

CMultiplexer* CUDTUnited::findSuitableMuxer(CUDTSocket* s, const sockaddr_any& reqaddr)
{
    // If not, we need to see if there exist already a multiplexer bound
    // to the same endpoint.
    const int         port      = reqaddr.hport();
    const CSrtConfig& cfgSocket = s->core().m_config;

    // This loop is going to check the attempted binding of
    // address:port and socket settings against every existing
    // multiplexer. Possible results of the check are:

    // 1. MATCH: identical address - reuse it and quit.
    // 2. CONFLICT: report error: the binding partially overlaps
    //    so it neither can be reused nor is free to bind.
    // 3. PASS: different and not overlapping - continue searching.

    // In this function the convention is:
    // MATCH: do nothing and proceed with binding reusage, THEN break.
    // CONFLICT: throw an exception.
    // PASS: use 'continue' to pass to the next element.

    bool reuse_attempt = false;
    for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin(); i != m_mMultiplexer.end(); ++i)
    {
        CMultiplexer const& m = i->second;

        sockaddr_any mux_addr = m.selfAddr();

        // Check if the address was reset. If so, this means this muxer is
        // about to be deleted, so definitely don't use it.
        if (mux_addr.family() == AF_UNSPEC)
            continue;

        // First, we need to find a multiplexer with the same port.
        if (mux_addr.hport() != port)
        {
            HLOGC(smlog.Debug,
                    log << "bind: muxer @" << m.id() << " found, but for port " << mux_addr.hport()
                    << " (requested port: " << port << ")");
            continue;
        }

        HLOGC(smlog.Debug,
                log << "bind: Found existing muxer @" << m.id() << " : " << mux_addr.str() << " - check against "
                << reqaddr.str());

        // If this is bound to the wildcard address, it can be reused if:
        // - reqaddr is also a wildcard
        // - channel settings match
        // Otherwise it's a conflict.

        if (mux_addr.isany())
        {
            if (mux_addr.family() == AF_INET6)
            {
                // With IPv6 we need to research two possibilities:
                // iIpV6Only == 1 -> This means that it binds only :: wildcard, but not 0.0.0.0
                // iIpV6Only == 0 -> This means that it binds both :: and 0.0.0.0.
                // iIpV6Only == -1 -> Hard to say what to do, but treat it as a potential conflict in any doubtful case.

                if (m.cfg().iIpV6Only == 1)
                {
                    // PASS IF: candidate is IPv4, no matter the address
                    // MATCH IF: candidate is IPv6 with only=1
                    // CONFLICT IF: candidate is IPv6 with only != 1 or IPv6 non-wildcard.

                    if (reqaddr.family() == AF_INET)
                    {
                        HLOGC(smlog.Debug, log << "bind: muxer @" << m.id()
                                << " is :: v6only - requested IPv4 ANY is NOT IN THE WAY. Searching on.");
                        continue;
                    }

                    // Candidate is AF_INET6

                    if (cfgSocket.iIpV6Only != 1 || !reqaddr.isany())
                    {
                        // CONFLICT:
                        // 1. attempting to make a wildcard IPv4 + IPv6
                        // while the multiplexer for wildcard IPv6 exists.
                        // 2. If binding to a given address, it conflicts with the wildcard
                        LOGC(smlog.Error,
                                log << "bind: Address: " << reqaddr.str()
                                << " conflicts with existing IPv6 wildcard binding: " << mux_addr.str());
                        throw CUDTException(MJ_NOTSUP, MN_BUSYPORT, 0);
                    }

                    // Otherwise, MATCH.
                }
                else if (m.cfg().iIpV6Only == 0)
                {
                    // Muxer's address is a wildcard for :: and 0.0.0.0 at once.
                    // This way only IPv6 wildcard with v6only=0 is a perfect match and everything
                    // else is a conflict.

                    if (reqaddr.family() == AF_INET6 && reqaddr.isany() && cfgSocket.iIpV6Only == 0)
                    {
                        // MATCH
                    }
                    else
                    {
                        // CONFLICT: attempting to make a wildcard IPv4 + IPv6 while
                        // the multiplexer for wildcard IPv6 exists.
                        LOGC(smlog.Error,
                                log << "bind: Address: " << reqaddr.str() << " v6only=" << cfgSocket.iIpV6Only
                                << " conflicts with existing IPv6 + IPv4 wildcard binding: " << mux_addr.str());
                        throw CUDTException(MJ_NOTSUP, MN_BUSYPORT, 0);
                    }
                }
                else // Case -1, by unknown reason. Accept only with -1 setting, others are conflict.
                {
                    if (reqaddr.family() == AF_INET6 && reqaddr.isany() && cfgSocket.iIpV6Only == -1)
                    {
                        // MATCH
                    }
                    else
                    {
                        LOGC(smlog.Error,
                                log << "bind: Address: " << reqaddr.str() << " v6only=" << cfgSocket.iIpV6Only
                                << " conflicts with existing IPv6 v6only=unknown wildcard binding: " << mux_addr.str());
                        throw CUDTException(MJ_NOTSUP, MN_BUSYPORT, 0);
                    }
                }
            }
            else // muxer is IPv4 wildcard
            {
                // Then only IPv4 wildcard is a match and:
                // - IPv6 with only=true is PASS (not a conflict)
                // - IPv6 with only=false is CONFLICT
                // - IPv6 with only=undefined is CONFLICT
                // REASON: we need to make a potential conflict a conflict as there will be
                // no bind() call to check if this wouldn't be a conflict in result. If you want
                // to have a binding to IPv6 that should avoid conflict with IPv4 wildcard binding,
                // then SRTO_IPV6ONLY option must be explicitly set before binding.
                // Also:
                if (reqaddr.family() == AF_INET)
                {
                    if (reqaddr.isany())
                    {
                        // MATCH
                    }
                    else
                    {
                        LOGC(smlog.Error,
                                log << "bind: Address: " << reqaddr.str()
                                << " conflicts with existing IPv4 wildcard binding: " << mux_addr.str());
                        throw CUDTException(MJ_NOTSUP, MN_BUSYPORT, 0);
                    }
                }
                else // AF_INET6
                {
                    if (cfgSocket.iIpV6Only == 1 || !reqaddr.isany())
                    {
                        // PASS
                        HLOGC(smlog.Debug, log << "bind: muxer @" << m.id()
                                << " is IPv4 wildcard - requested " << reqaddr.str() << " v6only=" << cfgSocket.iIpV6Only
                                << " is NOT IN THE WAY. Searching on.");
                        continue;
                    }
                    else
                    {
                        LOGC(smlog.Error,
                                log << "bind: Address: " << reqaddr.str() << " v6only=" << cfgSocket.iIpV6Only
                                << " conflicts with existing IPv4 wildcard binding: " << mux_addr.str());
                        throw CUDTException(MJ_NOTSUP, MN_BUSYPORT, 0);
                    }
                }
            }

            reuse_attempt = true;
            HLOGC(smlog.Debug, log << "bind: wildcard address - multiplexer reusable");
        }
        // Muxer address is NOT a wildcard, so conflicts only with WILDCARD of the same type
        else if (reqaddr.isany() && reqaddr.family() == mux_addr.family())
        {
            LOGC(smlog.Error,
                    log << "bind: Wildcard address: " << reqaddr.str()
                    << " conflicts with existing IP binding: " << mux_addr.str());
            throw CUDTException(MJ_NOTSUP, MN_BUSYPORT, 0);
        }
        // If this is bound to a certain address, AND:
        else if (mux_addr.equal_address(reqaddr))
        {
            // - the address is the same as reqaddr
            reuse_attempt = true;
            HLOGC(smlog.Debug, log << "bind: same IP address - multiplexer reusable");
        }
        else
        {
            HLOGC(smlog.Debug, log << "bind: IP addresses differ - ALLOWED to create a new multiplexer");
            continue;
        }
        // Otherwise:
        // - the address is different than reqaddr
        //   - the address can't be reused, but this can go on with new one.

        // If this is a reusage attempt:
        if (reuse_attempt)
        {
            //   - if the channel settings match, it can be reused
            if (channelSettingsMatch(m.cfg(), cfgSocket)
                    && inet6SettingsCompat(mux_addr, m.cfg(), reqaddr, cfgSocket))
            {
                return &i->second;
            }
            //   - if not, it's a conflict
            LOGC(smlog.Error,
                    log << "bind: Address: " << reqaddr.str() << " conflicts with binding: " << mux_addr.str()
                    << " due to channel settings");
            throw CUDTException(MJ_NOTSUP, MN_BUSYPORT, 0);
        }
        // If not, proceed to the next one, and when there are no reusage
        // candidates, proceed with creating a new multiplexer.

        // Note that a binding to a different IP address is not treated
        // as a candidate for either reusage or conflict.
        LOGC(smlog.Fatal, log << "SHOULD NOT GET HERE!!!");
        SRT_ASSERT(false);
    }

    HLOGC(smlog.Debug, log << "bind: No suitable multiplexer for " << reqaddr.str() << " - can go on with new one");

    // No suitable muxer found - create a new multiplexer.
    return NULL;
}

// This function is going to find a multiplexer for the port contained
// in the 'ls' listening socket. The multiplexer must exist when the listener
// exists, otherwise the dispatching procedure wouldn't even call this
// function. By historical reasons there's also a fallback for a case when the
// multiplexer wasn't found by id, the search by port number continues.
bool CUDTUnited::updateListenerMux(CUDTSocket* s, const CUDTSocket* ls)
{
    ExclusiveLock cg(m_GlobControlLock);
    const int  port = ls->m_SelfAddr.hport();

    HLOGC(smlog.Debug,
          log << "updateListenerMux: finding muxer of listener socket @" << ls->id() << " muxid=" << ls->m_iMuxID
              << " bound=" << ls->m_SelfAddr.str() << " FOR @" << s->id() << " addr=" << s->m_SelfAddr.str()
              << "_->_" << s->m_PeerAddr.str());

    // First thing that should be certain here is that there should exist
    // a muxer with the ID written in the listener socket's mux ID.

    CMultiplexer* mux = map_getp(m_mMultiplexer, ls->m_iMuxID);

    // NOTE:
    // THIS BELOW CODE is only for a highly unlikely situation when the listener
    // socket has been closed in the meantime when the accepted socket is being
    // processed. This procedure is different than updateMux because this time we
    // only want to have a multiplexer socket to be assigned to the accepted socket.
    // It is also unlikely that the listener socket is garbage-collected so fast, so
    // this procedure will most likely find the multiplexer of the zombie listener socket,
    // which no longer accepts new connections (the listener is withdrawn immediately from
    // the port) that wasn't yet completely deleted.
    CMultiplexer* fallback = NULL;
    if (!mux)
    {
        LOGC(smlog.Error, log << "updateListenerMux: IPE? listener muxer not found by ID, trying by port");

        // To be used as first found with different IP version

        // find the listener's address
        for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin(); i != m_mMultiplexer.end(); ++i)
        {
            CMultiplexer& m = i->second;

#if HVU_ENABLE_HEAVY_LOGGING
            hvu::ofmtbufstream that_muxer;
            that_muxer << "id=" << m.id() << " addr=" << m.selfAddr().str();
#endif

            if (m.selfAddr().hport() == port)
            {
                HLOGC(smlog.Debug, log << "updateListenerMux: reusing muxer: " << that_muxer);
                if (m.selfAddr().family() == s->m_PeerAddr.family())
                {
                    mux = &m; // best match
                    break;
                }
                else if (m.selfAddr().family() == AF_INET6)
                {
                    // Allowed fallback case when we only need an accepted socket.
                    fallback = &m;
                }
            }
            else
            {
                HLOGC(smlog.Debug, log << "updateListenerMux: SKIPPING muxer: " << that_muxer);
            }
        }

        if (!mux && fallback)
        {
            // It is allowed to reuse this multiplexer, but the socket must allow both IPv4 and IPv6
            if (fallback->cfg().iIpV6Only == 0)
            {
                HLOGC(smlog.Warn, log << "updateListenerMux: reusing multiplexer from different family");
                mux = fallback;
            }
        }
    }

    // Checking again because the above procedure could have set it
    if (mux)
    {
        // reuse the existing multiplexer
        installMuxer((s), (mux));
        return true;
    }

    return false;
}

void* CUDTUnited::garbageCollect(void* p)
{
    CUDTUnited* self = (CUDTUnited*)p;

    THREAD_STATE_INIT("SRT:GC");

    UniqueLock gclock(self->m_GCStopLock);

    // START LIBRARY RUNNING LOOP
    while (!self->m_bGCClosing)
    {
        INCREMENT_THREAD_ITERATIONS();
        self->checkBrokenSockets();
        self->checkTemporaryDatabases();

        HLOGC(inlog.Debug, log << "GC: sleep 1 s");
        self->m_GCStopCond.wait_for(gclock, seconds_from(1));
    }
    // END.

    THREAD_EXIT();
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////

SRTRUNSTATUS CUDT::startup()
{
#if HAVE_PTHREAD_ATFORK
    static bool registered = false;
    if (!registered)
    {
        pthread_atfork(NULL, NULL, (void (*)()) srt::CUDT::cleanupAtFork);
        registered = true;
    }
#endif 
    return uglobal().startup();
}

SRTSTATUS CUDT::cleanup()
{
    return uglobal().cleanup();
}

int CUDT::cleanupAtFork()
{
    CUDTUnited &context = uglobal();
    context.cleanupAtFork();
    new (&context) CUDTUnited();

    return context.startup();
}

SRTSOCKET CUDT::socket()
{
    try
    {
        return uglobal().newSocket();
    }
    catch (const CUDTException& e)
    {
        APIError a(e);
        return SRT_INVALID_SOCK;
    }
    catch (const bad_alloc&)
    {
        APIError a(MJ_SYSTEMRES, MN_MEMORY, 0);
        return SRT_INVALID_SOCK;
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "socket: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        APIError a(MJ_UNKNOWN, MN_NONE, 0);
        return SRT_INVALID_SOCK;
    }
}

CUDT::APIError::APIError(const CUDTException& e)
{
    SetThreadLocalError(e);
}

CUDT::APIError::APIError(CodeMajor mj, CodeMinor mn, int syserr)
{
    SetThreadLocalError(CUDTException(mj, mn, syserr));
}

CUDT::APIError::APIError(int errorcode)
{
    CodeMajor mj = CodeMajor(errorcode / 1000);
    CodeMinor mn = CodeMinor(errorcode % 1000);
    SetThreadLocalError(CUDTException(mj, mn, 0));
}

#if SRT_ENABLE_BONDING
// This is an internal function; 'type' should be pre-checked if it has a correct value.
// This doesn't have argument of GroupType due to header file conflicts.

// [[using locked(s_UDTUnited.m_GlobControlLock)]]
CUDTGroup& CUDTUnited::newGroup(const int type)
{
    const SRTSOCKET id = generateSocketID(true);

    // Now map the group
    return addGroup(id, SRT_GROUP_TYPE(type)).set_id(id);
}

SRTSOCKET CUDT::createGroup(SRT_GROUP_TYPE gt)
{
    try
    {
        sync::ExclusiveLock globlock(uglobal().m_GlobControlLock);
        return uglobal().newGroup(gt).id();
        // Note: potentially, after this function exits, the group
        // could be deleted, immediately, from a separate thread (though
        // unlikely because the other thread would need some handle to
        // keep it). But then, the first call to any API function would
        // return invalid ID error.
    }
    catch (const CUDTException& e)
    {
        return APIError(e), SRT_INVALID_SOCK;
    }
    catch (...)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0), SRT_INVALID_SOCK;
    }
}

// [[using locked(m_ControlLock)]]
// [[using locked(CUDT::s_UDTUnited.m_GlobControlLock)]]
void CUDTSocket::removeFromGroup(bool broken)
{
    CUDTGroup* g = m_GroupOf;
    if (g)
    {
        // Reset group-related fields immediately. They won't be accessed
        // in the below calls, while the iterator will be invalidated for
        // a short moment between removal from the group container and the end,
        // while the GroupLock would be already taken out. It is safer to reset
        // it to a NULL iterator before removal.
        m_GroupOf         = NULL;
        m_GroupMemberData = NULL;

        bool still_have = g->remove(id());
        if (broken)
        {
            // Activate the SRT_EPOLL_UPDATE event on the group
            // if it was because of a socket that was earlier connected
            // and became broken. This is not to be sent in case when
            // it is a failure during connection, or the socket was
            // explicitly removed from the group.
            g->activateUpdateEvent(still_have);
        }

        HLOGC(smlog.Debug,
              log << "removeFromGroup: socket @" << id() << " NO LONGER A MEMBER of $" << g->id() << "; group is "
                  << (still_have ? "still ACTIVE" : "now EMPTY"));
    }
}

SRTSOCKET CUDT::getGroupOfSocket(SRTSOCKET socket)
{
    // Lock this for the whole function as we need the group
    // to persist the call.
    SharedLock glock(uglobal().m_GlobControlLock);
    CUDTSocket* s = uglobal().locateSocket_LOCKED(socket);
    if (!s || !s->m_GroupOf)
        return APIError(MJ_NOTSUP, MN_INVAL, 0), SRT_INVALID_SOCK;

    return s->m_GroupOf->id();
}

SRTSTATUS CUDT::getGroupData(SRTSOCKET groupid, SRT_SOCKGROUPDATA* pdata, size_t* psize)
{
    if (!CUDT::isgroup(groupid) || !psize)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    CUDTUnited::GroupKeeper k(uglobal(), groupid, CUDTUnited::ERH_RETURN);
    if (!k.group)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    // To get only the size of the group pdata=NULL can be used
    return k.group->getGroupData(pdata, psize);
}
#endif

SRTSTATUS CUDT::bind(SRTSOCKET u, const sockaddr* name, int namelen)
{
    try
    {
        sockaddr_any sa(name, namelen);
        if (sa.len == 0)
        {
            // This happens if the namelen check proved it to be
            // too small for particular family, or that family is
            // not recognized (is none of AF_INET, AF_INET6).
            // This is a user error.
            return APIError(MJ_NOTSUP, MN_INVAL, 0);
        }
        CUDTSocket* s = uglobal().locateSocket(u);
        if (!s)
            return APIError(MJ_NOTSUP, MN_INVAL, 0);

        return uglobal().bind(s, sa);
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "bind: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::bind(SRTSOCKET u, UDPSOCKET udpsock)
{
    try
    {
        CUDTSocket* s = uglobal().locateSocket(u);
        if (!s)
            return APIError(MJ_NOTSUP, MN_INVAL, 0);

        return uglobal().bind(s, udpsock);
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "bind/udp: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::listen(SRTSOCKET u, int backlog)
{
    try
    {
        return uglobal().listen(u, backlog);
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "listen: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSOCKET CUDT::accept_bond(const SRTSOCKET listeners[], int lsize, int64_t msTimeOut)
{
    try
    {
        return uglobal().accept_bond(listeners, lsize, msTimeOut);
    }
    catch (const CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRT_INVALID_SOCK;
    }
    catch (bad_alloc&)
    {
        SetThreadLocalError(CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
        return SRT_INVALID_SOCK;
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "accept_bond: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
        return SRT_INVALID_SOCK;
    }
}

SRTSOCKET CUDT::accept(SRTSOCKET u, sockaddr* addr, int* addrlen)
{
    try
    {
        return uglobal().accept(u, addr, addrlen);
    }
    catch (const CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRT_INVALID_SOCK;
    }
    catch (const bad_alloc&)
    {
        SetThreadLocalError(CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
        return SRT_INVALID_SOCK;
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "accept: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
        return SRT_INVALID_SOCK;
    }
}

SRTSOCKET CUDT::connect(SRTSOCKET u, const sockaddr* name, const sockaddr* tname, int namelen)
{
    try
    {
        return uglobal().connect(u, name, tname, namelen);
    }
    catch (const CUDTException& e)
    {
        return APIError(e), SRT_INVALID_SOCK;
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0), SRT_INVALID_SOCK;
    }
    catch (std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "connect: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0), SRT_INVALID_SOCK;
    }
}

#if SRT_ENABLE_BONDING
SRTSOCKET CUDT::connectLinks(SRTSOCKET grp, SRT_SOCKGROUPCONFIG targets[], int arraysize)
{
    if (arraysize <= 0)
        return APIError(MJ_NOTSUP, MN_INVAL, 0), SRT_INVALID_SOCK;

    if (!CUDT::isgroup(grp))
    {
        // connectLinks accepts only GROUP id, not socket id.
        return APIError(MJ_NOTSUP, MN_SIDINVAL, 0), SRT_INVALID_SOCK;
    }

    try
    {
        CUDTUnited::GroupKeeper k(uglobal(), grp, CUDTUnited::ERH_THROW);
        return uglobal().groupConnect(k.group, targets, arraysize);
    }
    catch (CUDTException& e)
    {
        return APIError(e), SRT_INVALID_SOCK;
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0), SRT_INVALID_SOCK;
    }
    catch (std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "connect: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0), SRT_INVALID_SOCK;
    }
}
#endif

SRTSOCKET CUDT::connect(SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn)
{
    try
    {
        return uglobal().connect(u, name, namelen, forced_isn);
    }
    catch (const CUDTException& e)
    {
        return APIError(e), SRT_INVALID_SOCK;
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0), SRT_INVALID_SOCK;
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "connect: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0), SRT_INVALID_SOCK;
    }
}

SRTSTATUS CUDT::close(SRTSOCKET u, int reason)
{
    try
    {
        return uglobal().close(u, reason);
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "close: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::getpeername(SRTSOCKET u, sockaddr* name, int* namelen)
{
    try
    {
        uglobal().getpeername(u, name, namelen);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "getpeername: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::getsockname(SRTSOCKET u, sockaddr* name, int* namelen)
{
    try
    {
        uglobal().getsockname(u, name, namelen);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "getsockname: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::getsockdevname(SRTSOCKET u, char* name, size_t* namelen)
{
    try
    {
        uglobal().getsockdevname(u, name, namelen);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "getsockname: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::getsockopt(SRTSOCKET u, int, SRT_SOCKOPT optname, void* pw_optval, int* pw_optlen)
{
    if (!pw_optval || !pw_optlen)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    try
    {
#if SRT_ENABLE_BONDING
        if (CUDT::isgroup(u))
        {
            CUDTUnited::GroupKeeper k(uglobal(), u, CUDTUnited::ERH_THROW);
            k.group->getOpt(optname, (pw_optval), (*pw_optlen));
            return SRT_STATUS_OK;
        }
#endif

        CUDT& udt = uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core();
        udt.getOpt(optname, (pw_optval), (*pw_optlen));
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "getsockopt: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::setsockopt(SRTSOCKET u, int, SRT_SOCKOPT optname, const void* optval, int optlen)
{
    if (!optval || optlen < 0)
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    try
    {
#if SRT_ENABLE_BONDING
        if (CUDT::isgroup(u))
        {
            CUDTUnited::GroupKeeper k(uglobal(), u, CUDTUnited::ERH_THROW);
            k.group->setOpt(optname, optval, optlen);
            return SRT_STATUS_OK;
        }
#endif

        CUDT& udt = uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core();
        udt.setOpt(optname, optval, optlen);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "setsockopt: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

int CUDT::send(SRTSOCKET u, const char* buf, int len, int)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    return sendmsg2(u, buf, len, (mctrl));
}

// --> CUDT::recv moved down

int CUDT::sendmsg(SRTSOCKET u, const char* buf, int len, int ttl, bool inorder, int64_t srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    mctrl.msgttl      = ttl;
    mctrl.inorder     = inorder;
    mctrl.srctime     = srctime;
    return sendmsg2(u, buf, len, (mctrl));
}

int CUDT::sendmsg2(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL& w_m)
{
    try
    {
#if SRT_ENABLE_BONDING
        if (CUDT::isgroup(u))
        {
            CUDTUnited::GroupKeeper k(uglobal(), u, CUDTUnited::ERH_THROW);
            return k.group->send(buf, len, (w_m));
        }
#endif

        return uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core().sendmsg2(buf, len, (w_m));
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int>();
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0).as<int>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "sendmsg: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int>();
    }
}

int CUDT::recv(SRTSOCKET u, char* buf, int len, int)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    int         ret   = recvmsg2(u, buf, len, (mctrl));
    return ret;
}

int CUDT::recvmsg(SRTSOCKET u, char* buf, int len, int64_t& srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    int         ret   = recvmsg2(u, buf, len, (mctrl));
    srctime           = mctrl.srctime;
    return ret;
}

int CUDT::recvmsg2(SRTSOCKET u, char* buf, int len, SRT_MSGCTRL& w_m)
{
    try
    {
#if SRT_ENABLE_BONDING
        if (CUDT::isgroup(u))
        {
            CUDTUnited::GroupKeeper k(uglobal(), u, CUDTUnited::ERH_THROW);
            return k.group->recv(buf, len, (w_m));
        }
#endif

        return uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core().recvmsg2(buf, len, (w_m));
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "recvmsg: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int>();
    }
}

int64_t CUDT::sendfile(SRTSOCKET u, fstream& ifs, int64_t& offset, int64_t size, int block)
{
    try
    {
        CUDT& udt = uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core();
        return udt.sendfile(ifs, offset, size, block);
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int64_t>();
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0).as<int64_t>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "sendfile: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int64_t>();
    }
}

int64_t CUDT::recvfile(SRTSOCKET u, fstream& ofs, int64_t& offset, int64_t size, int block)
{
    try
    {
        return uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core().recvfile(ofs, offset, size, block);
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int64_t>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "recvfile: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int64_t>();
    }
}

int CUDT::select(int, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, std::set<SRTSOCKET>* exceptfds, const timeval* timeout)
{
    if ((!readfds) && (!writefds) && (!exceptfds))
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0).as<int>();
    }

    try
    {
        return uglobal().select(readfds, writefds, exceptfds, timeout);
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int>();
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0).as<int>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "select: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int>();
    }
}

int CUDT::selectEx(const vector<SRTSOCKET>& fds,
                        vector<SRTSOCKET>*       readfds,
                        vector<SRTSOCKET>*       writefds,
                        vector<SRTSOCKET>*       exceptfds,
                        int64_t                  msTimeOut)
{
    if ((!readfds) && (!writefds) && (!exceptfds))
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0).as<int>();
    }

    try
    {
        return uglobal().selectEx(fds, readfds, writefds, exceptfds, msTimeOut);
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int>();
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0).as<int>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "selectEx: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN).as<int>();
    }
}

int CUDT::epoll_create()
{
    try
    {
        return uglobal().epoll_create();
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "epoll_create: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int>();
    }
}

SRTSTATUS CUDT::epoll_clear_usocks(int eid)
{
    try
    {
        uglobal().epoll_clear_usocks(eid);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (std::exception& ee)
    {
        LOGC(aclog.Fatal,
             log << "epoll_clear_usocks: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::epoll_add_usock(const int eid, const SRTSOCKET u, const int* events)
{
    try
    {
        uglobal().epoll_add_usock(eid, u, events);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "epoll_add_usock: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events)
{
    try
    {
        uglobal().epoll_add_ssock(eid, s, events);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "epoll_add_ssock: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::epoll_update_usock(const int eid, const SRTSOCKET u, const int* events)
{
    try
    {
        uglobal().epoll_add_usock(eid, u, events);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal,
             log << "epoll_update_usock: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events)
{
    try
    {
        uglobal().epoll_update_ssock(eid, s, events);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal,
             log << "epoll_update_ssock: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::epoll_remove_usock(const int eid, const SRTSOCKET u)
{
    try
    {
        uglobal().epoll_remove_usock(eid, u);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal,
             log << "epoll_remove_usock: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

SRTSTATUS CUDT::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
    try
    {
        uglobal().epoll_remove_ssock(eid, s);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal,
             log << "epoll_remove_ssock: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

int CUDT::epoll_wait(const int       eid,
                          set<SRTSOCKET>* readfds,
                          set<SRTSOCKET>* writefds,
                          int64_t         msTimeOut,
                          set<SYSSOCKET>* lrfds,
                          set<SYSSOCKET>* lwfds)
{
    try
    {
        return uglobal().epoll_ref().wait(eid, readfds, writefds, msTimeOut, lrfds, lwfds);
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "epoll_wait: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int>();
    }
}

int CUDT::epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut)
{
    try
    {
        return uglobal().epoll_uwait(eid, fdsSet, fdsSize, msTimeOut);
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "epoll_uwait: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int>();
    }
}

int32_t CUDT::epoll_set(const int eid, int32_t flags)
{
    try
    {
        return uglobal().epoll_set(eid, flags);
    }
    catch (const CUDTException& e)
    {
        return APIError(e).as<int32_t>();
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "epoll_set: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0).as<int32_t>();
    }
}

SRTSTATUS CUDT::epoll_release(const int eid)
{
    try
    {
        uglobal().epoll_release(eid);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "epoll_release: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

CUDTException& CUDT::getlasterror()
{
    return GetThreadLocalError();
}

SRTSTATUS CUDT::bstats(SRTSOCKET u, CBytePerfMon* perf, bool clear, bool instantaneous)
{
#if SRT_ENABLE_BONDING
    if (CUDT::isgroup(u))
        return groupsockbstats(u, perf, clear);
#endif

    try
    {
        CUDT& udt = uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core();
        udt.bstats(perf, clear, instantaneous);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "bstats: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

#if SRT_ENABLE_BONDING
SRTSTATUS CUDT::groupsockbstats(SRTSOCKET u, CBytePerfMon* perf, bool clear)
{
    try
    {
        CUDTUnited::GroupKeeper k(uglobal(), u, CUDTUnited::ERH_THROW);
        k.group->bstatsSocket(perf, clear);
        return SRT_STATUS_OK;
    }
    catch (const CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRT_ERROR;
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "bstats: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
        return SRT_ERROR;
    }
}
#endif

CUDT* CUDT::getUDTHandle(SRTSOCKET u)
{
    try
    {
        return &uglobal().locateSocket(u, CUDTUnited::ERH_THROW)->core();
    }
    catch (const CUDTException& e)
    {
        SetThreadLocalError(e);
        return NULL;
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "getUDTHandle: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
        return NULL;
    }
}

SRT_SOCKSTATUS CUDT::getsockstate(SRTSOCKET u)
{
    try
    {
#if SRT_ENABLE_BONDING
        if (CUDT::isgroup(u))
        {
            CUDTUnited::GroupKeeper k(uglobal(), u, CUDTUnited::ERH_THROW);
            return k.group->getStatus();
        }
#endif
        return uglobal().getStatus(u);
    }
    catch (const CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRTS_NONEXIST;
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "getsockstate: UNEXPECTED EXCEPTION: " << typeid(ee).name() << ": " << ee.what());
        SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
        return SRTS_NONEXIST;
    }
}

int CUDT::getMaxPayloadSize(SRTSOCKET id)
{
    return uglobal().getMaxPayloadSize(id);
}

int CUDTUnited::getMaxPayloadSize(SRTSOCKET id)
{
    CUDTSocket* s = locateSocket(id);
    if (!s)
    {
        return CUDT::APIError(MJ_NOTSUP, MN_SIDINVAL).as<int>();
    }

    if (s->m_SelfAddr.family() == AF_UNSPEC)
    {
        return CUDT::APIError(MJ_NOTSUP, MN_ISUNBOUND).as<int>();
    }

    int fam = s->m_SelfAddr.family();
    CUDT& u = s->core();

    std::string errmsg;
    int extra = u.m_config.extraPayloadReserve((errmsg));
    if (extra == -1)
    {
        LOGP(aclog.Error, errmsg);
        return CUDT::APIError(MJ_NOTSUP, MN_INVAL).as<int>();
    }

    // Prefer transfer IP version, if defined. This is defined after
    // the connection is established. Note that the call is rejected
    // if the socket isn't bound, be it explicitly or implicitly by
    // calling srt_connect().
    if (u.m_TransferIPVersion != AF_UNSPEC)
        fam = u.m_TransferIPVersion;

    int payload_size = u.m_config.iMSS - CPacket::HDR_SIZE - CPacket::udpHeaderSize(fam) - extra;

    return payload_size;
}

string CUDTUnited::testSocketsClear()
{
    std::ostringstream out;

    SharedLock lk (m_GlobControlLock);

    // The multiplexer should be empty, but even if it isn't by some reason
    // (some sockets were not yet wiped out by gc), it should contain empty its own containers.
    for (std::map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin(); i != m_mMultiplexer.end(); ++i)
    {
        std::string remain = i->second.testAllSocketsClear();
        if (!remain.empty())
            out << " *" << remain << "*";

        if (!i->second.empty())
            out << " ^DANG^" << i->second.id() << "^";
    }

    for (sockets_t::iterator i = m_Sockets.begin(); i != m_Sockets.end(); ++i)
    {
        out << " !" << i->first;
    }

    return out.str();
}

template <class SOCKTYPE>
inline void set_result(set<SOCKTYPE>* val, int* num, SOCKTYPE* fds)
{
    if (!val || !num || !fds)
        return;

    if (*num > int(val->size()))
        *num = int(val->size()); // will get 0 if val->empty()
    int count = 0;

    // This loop will run 0 times if val->empty()
    for (typename set<SOCKTYPE>::const_iterator it = val->begin(); it != val->end(); ++it)
    {
        if (count >= *num)
            break;
        fds[count++] = *it;
    }
}

int CUDT::epoll_wait2(int        eid,
                SRTSOCKET* readfds,
                int*       rnum,
                SRTSOCKET* writefds,
                int*       wnum,
                int64_t    msTimeOut,
                SYSSOCKET* lrfds,
                int*       lrnum,
                SYSSOCKET* lwfds,
                int*       lwnum)
{
    // This API is an alternative format for epoll_wait, created for
    // compatibility with other languages. Users need to pass in an array
    // for holding the returned sockets, with the maximum array length
    // stored in *rnum, etc., which will be updated with returned number
    // of sockets.

    set<SRTSOCKET>  readset;
    set<SRTSOCKET>  writeset;
    set<SYSSOCKET>  lrset;
    set<SYSSOCKET>  lwset;
    set<SRTSOCKET>* rval  = NULL;
    set<SRTSOCKET>* wval  = NULL;
    set<SYSSOCKET>* lrval = NULL;
    set<SYSSOCKET>* lwval = NULL;
    if ((readfds != NULL) && (rnum != NULL))
        rval = &readset;
    if ((writefds != NULL) && (wnum != NULL))
        wval = &writeset;
    if ((lrfds != NULL) && (lrnum != NULL))
        lrval = &lrset;
    if ((lwfds != NULL) && (lwnum != NULL))
        lwval = &lwset;

    int ret = epoll_wait(eid, rval, wval, msTimeOut, lrval, lwval);
    if (ret > 0)
    {
        // set<SRTSOCKET>::const_iterator i;
        // SET_RESULT(rval, rnum, readfds, i);
        set_result(rval, rnum, readfds);
        // SET_RESULT(wval, wnum, writefds, i);
        set_result(wval, wnum, writefds);

        // set<SYSSOCKET>::const_iterator j;
        // SET_RESULT(lrval, lrnum, lrfds, j);
        set_result(lrval, lrnum, lrfds);
        // SET_RESULT(lwval, lwnum, lwfds, j);
        set_result(lwval, lwnum, lwfds);
    }
    return ret;
}

void setloglevel(hvu::logging::LogLevel::type ll)
{
    srt::logging::logger_config().set_maxlevel(ll);
}

void addlogfa(int fa)
{
    srt_addlogfa(fa);
}

void dellogfa(int fa)
{
    srt_dellogfa(fa);
}

void resetlogfa(set<int> fas)
{
    std::vector<int> faval;
    std::copy(fas.begin(), fas.end(), std::back_inserter(faval));

    srt_resetlogfa(&faval[0], faval.size());
}

void resetlogfa(const int* fara, size_t fara_size)
{
    srt_resetlogfa(fara, fara_size);
}

void setlogstream(std::ostream& stream)
{
    srt::logging::logger_config().set_stream(stream);
}

void setloghandler(void* opaque, HVU_LOG_HANDLER_FN* handler)
{
    srt::logging::logger_config().set_handler(opaque, handler);
}

void setlogflags(int flags)
{
    srt::logging::logger_config().set_flags(flags);
}

SRT_API bool setstreamid(SRTSOCKET u, const std::string& sid)
{
    return CUDT::setstreamid(u, sid);
}
SRT_API std::string getstreamid(SRTSOCKET u)
{
    return CUDT::getstreamid(u);
}

int getrejectreason(SRTSOCKET u)
{
    return CUDT::rejectReason(u);
}

SRTSTATUS setrejectreason(SRTSOCKET u, int value)
{
    return CUDT::rejectReason(u, value);
}

} // namespace srt
