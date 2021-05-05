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
#include "threadname.h"
#include "srt.h"
#include "udt.h"

#ifdef _WIN32
   #include <win/wintime.h>
#endif

#ifdef _MSC_VER
   #pragma warning(error: 4530)
#endif

using namespace std;
using namespace srt_logging;
using namespace srt::sync;
extern LogConfig srt_logger_config;


void CUDTSocket::construct()
{
#if ENABLE_EXPERIMENTAL_BONDING
   m_GroupOf = NULL;
   m_GroupMemberData = NULL;
#endif
   setupMutex(m_AcceptLock, "Accept");
   setupCond(m_AcceptCond, "Accept");
   setupMutex(m_ControlLock, "Control");
}

CUDTSocket::~CUDTSocket()
{

   delete m_pUDT;
   m_pUDT = NULL;

   releaseMutex(m_AcceptLock);
   releaseCond(m_AcceptCond);
   releaseMutex(m_ControlLock);
}


SRT_SOCKSTATUS CUDTSocket::getStatus()
{
    // TTL in CRendezvousQueue::updateConnStatus() will set m_bConnecting to false.
    // Although m_Status is still SRTS_CONNECTING, the connection is in fact to be closed due to TTL expiry.
    // In this case m_bConnected is also false. Both checks are required to avoid hitting
    // a regular state transition from CONNECTING to CONNECTED.

    if (m_pUDT->m_bBroken)
        return SRTS_BROKEN;

    // Connecting timed out
    if ((m_Status == SRTS_CONNECTING) && !m_pUDT->m_bConnecting && !m_pUDT->m_bConnected)
        return SRTS_BROKEN;

    return m_Status;
}

// [[using locked(m_GlobControlLock)]]
void CUDTSocket::breakSocket_LOCKED()
{
    // This function is intended to be called from GC,
    // under a lock of m_GlobControlLock. 
    m_pUDT->m_bBroken = true;
    m_pUDT->m_iBrokenCounter = 0;
    HLOGC(smlog.Debug, log << "@" << m_SocketID << " CLOSING AS SOCKET");
    m_pUDT->closeInternal();
    setClosed();
}

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
    m_pUDT->m_iBrokenCounter = 60;
    m_pUDT->m_bBroken = true;
    setClosed();
}

bool CUDTSocket::readReady()
{
    if (m_pUDT->m_bConnected && m_pUDT->m_pRcvBuffer->isRcvDataReady())
        return true;
    if (m_pUDT->m_bListening)
    {
        return m_QueuedSockets.size() > 0;
    }

    return broken();
}

bool CUDTSocket::writeReady() const
{
    return (m_pUDT->m_bConnected
                && (m_pUDT->m_pSndBuffer->getCurrBufSize() < m_pUDT->m_config.iSndBufSize))
        || broken();
}

bool CUDTSocket::broken() const
{
    return m_pUDT->m_bBroken || !m_pUDT->m_bConnected;
}

////////////////////////////////////////////////////////////////////////////////

CUDTUnited::CUDTUnited():
m_Sockets(),
m_GlobControlLock(),
m_IDLock(),
m_mMultiplexer(),
m_MultiplexerLock(),
m_pCache(NULL),
m_bClosing(false),
m_GCStopCond(),
m_InitLock(),
m_iInstanceCount(0),
m_bGCStatus(false),
m_ClosedSockets()
{
   // Socket ID MUST start from a random value
   // Note. Don't use CTimer here, because s_UDTUnited is a static instance of CUDTUnited
   // with dynamic initialization (calling this constructor), while CTimer has
   // a static member s_ullCPUFrequency with dynamic initialization.
   // The order of initialization is not guaranteed.
   timeval t;

   gettimeofday(&t, 0);
   srand((unsigned int)t.tv_usec);

   const double rand1_0 = double(rand())/RAND_MAX;

   // Motivation: in case when rand() returns the value equal to RAND_MAX,
   // rand1_0 == 1, so the below formula will be
   // 1 + (MAX_SOCKET_VAL-1) * 1 = 1 + MAX_SOCKET_VAL - 1 = MAX_SOCKET_VAL
   // which is the highest allowed value for the socket.
   m_SocketIDGenerator = 1 + int((MAX_SOCKET_VAL-1) * rand1_0);
   m_SocketIDGenerator_init = m_SocketIDGenerator;

   // XXX An unlikely exception thrown from the below calls
   // might destroy the application before `main`. This shouldn't
   // be a problem in general.
   setupMutex(m_GlobControlLock, "GlobControl");
   setupMutex(m_IDLock, "ID");
   setupMutex(m_InitLock, "Init");

   m_pCache = new CCache<CInfoBlock>;
}

CUDTUnited::~CUDTUnited()
{
    // Call it if it wasn't called already.
    // This will happen at the end of main() of the application,
    // when the user didn't call srt_cleanup().
    if (m_bGCStatus)
    {
        cleanup();
    }

    releaseMutex(m_GlobControlLock);
    releaseMutex(m_IDLock);
    releaseMutex(m_InitLock);

    delete m_pCache;
}

std::string CUDTUnited::CONID(SRTSOCKET sock)
{
    if ( sock == 0 )
        return "";

    std::ostringstream os;
    os << "@" << sock << ":";
    return os.str();
}

int CUDTUnited::startup()
{
   ScopedLock gcinit(m_InitLock);

   if (m_iInstanceCount++ > 0)
      return 1;

   // Global initialization code
#ifdef _WIN32
   WORD wVersionRequested;
   WSADATA wsaData;
   wVersionRequested = MAKEWORD(2, 2);

   if (0 != WSAStartup(wVersionRequested, &wsaData))
      throw CUDTException(MJ_SETUP, MN_NONE,  WSAGetLastError());
#endif

   PacketFilter::globalInit();

   if (m_bGCStatus)
      return 1;

   m_bClosing = false;

   try
   {
       setupMutex(m_GCStopLock, "GCStop");
       setupCond(m_GCStopCond, "GCStop");
   }
   catch (...)
   {
       return -1;
   }
   if (!StartThread(m_GCThread, garbageCollect, this, "SRT:GC"))
      return -1;

   m_bGCStatus = true;

   HLOGC(inlog.Debug, log << "SRT Clock Type: " << SRT_SYNC_CLOCK_STR);

   return 0;
}

int CUDTUnited::cleanup()
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
      return 0;

   if (!m_bGCStatus)
      return 0;

   m_bClosing = true;
   // NOTE: we can do relaxed signaling here because
   // waiting on m_GCStopCond has a 1-second timeout,
   // after which the m_bClosing flag is cheched, which
   // is set here above. Worst case secenario, this
   // pthread_join() call will block for 1 second.
   CSync::signal_relaxed(m_GCStopCond);
   m_GCThread.join();

   // XXX There's some weird bug here causing this
   // to hangup on Windows. This might be either something
   // bigger, or some problem in pthread-win32. As this is
   // the application cleanup section, this can be temporarily
   // tolerated with simply exit the application without cleanup,
   // counting on that the system will take care of it anyway.
#ifndef _WIN32
   releaseCond(m_GCStopCond);
#endif

   m_bGCStatus = false;

   // Global destruction code
#ifdef _WIN32
   WSACleanup();
#endif

   return 0;
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
    if ( sockval == m_SocketIDGenerator_init )
    {
        // Mark that since this point on the checks for
        // whether the socket ID is in use must be done.
        m_SocketIDGenerator_init = 0;
    }

    // This is when all socket numbers have been already used once.
    // This may happen after many years of running an application
    // constantly when the connection breaks and gets restored often.
    if ( m_SocketIDGenerator_init == 0 )
    {
        int startval = sockval;
        for (;;) // Roll until an unused value is found
        {
            enterCS(m_GlobControlLock);
            const bool exists =
#if ENABLE_EXPERIMENTAL_BONDING
                for_group
                ? m_Groups.count(sockval | SRTGROUP_MASK)
                :
#endif
                m_Sockets.count(sockval);
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

                    m_SocketIDGenerator = sockval+1; // so that any next call will cause the same error
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

    return sockval;
}

SRTSOCKET CUDTUnited::newSocket(CUDTSocket** pps)
{
   // XXX consider using some replacement of std::unique_ptr
   // so that exceptions will clean up the object without the
   // need for a dedicated code.
   CUDTSocket* ns = NULL;

   try
   {
      ns = new CUDTSocket;
      ns->m_pUDT = new CUDT(ns);
   }
   catch (...)
   {
      delete ns;
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
   }

   try
   {
      ns->m_SocketID = generateSocketID();
   }
   catch (...)
   {
       delete ns;
       throw;
   }
   ns->m_Status = SRTS_INIT;
   ns->m_ListenSocket = 0;
   ns->m_pUDT->m_SocketID = ns->m_SocketID;
   ns->m_pUDT->m_pCache = m_pCache;

   try
   {
      HLOGC(smlog.Debug, log << CONID(ns->m_SocketID)
         << "newSocket: mapping socket "
         << ns->m_SocketID);

      // protect the m_Sockets structure.
      ScopedLock cs(m_GlobControlLock);
      m_Sockets[ns->m_SocketID] = ns;
   }
   catch (...)
   {
      //failure and rollback
      delete ns;
      ns = NULL;
   }

   if (!ns)
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);

    if (pps)
        *pps = ns;

   return ns->m_SocketID;
}

int CUDTUnited::newConnection(const SRTSOCKET listen, const sockaddr_any& peer, const CPacket& hspkt,
        CHandShake& w_hs, int& w_error, CUDT*& w_acpu)
{
   CUDTSocket* ns = NULL;
   w_acpu = NULL;

   w_error = SRT_REJ_IPE;

   // Can't manage this error through an exception because this is
   // running in the listener loop.
   CUDTSocket* ls = locateSocket(listen);
   if (!ls)
   {
       LOGC(cnlog.Error, log << "IPE: newConnection by listener socket id=" << listen << " which DOES NOT EXIST.");
       return -1;
   }

   HLOGC(cnlog.Debug, log << "newConnection: creating new socket after listener @"
         << listen << " contacted with backlog=" << ls->m_uiBackLog);

   // if this connection has already been processed
   if ((ns = locatePeer(peer, w_hs.m_iID, w_hs.m_iISN)) != NULL)
   {
      if (ns->m_pUDT->m_bBroken)
      {
         // last connection from the "peer" address has been broken
         ns->setClosed();

         ScopedLock acceptcg(ls->m_AcceptLock);
         ls->m_QueuedSockets.erase(ns->m_SocketID);
      }
      else
      {
         // connection already exist, this is a repeated connection request
         // respond with existing HS information
         HLOGC(cnlog.Debug, log
               << "newConnection: located a WORKING peer @"
               << w_hs.m_iID << " - ADAPTING.");

         w_hs.m_iISN = ns->m_pUDT->m_iISN;
         w_hs.m_iMSS = ns->m_pUDT->MSS();
         w_hs.m_iFlightFlagSize = ns->m_pUDT->m_config.iFlightFlagSize;
         w_hs.m_iReqType = URQ_CONCLUSION;
         w_hs.m_iID = ns->m_SocketID;

         // Report the original UDT because it will be
         // required to complete the HS data for conclusion response.
         w_acpu = ns->m_pUDT;

         return 0;

         //except for this situation a new connection should be started
      }
   }
   else
   {
       HLOGC(cnlog.Debug, log << "newConnection: NOT located any peer @"
               << w_hs.m_iID << " - resuming with initial connection.");
   }

   // exceeding backlog, refuse the connection request
   if (ls->m_QueuedSockets.size() >= ls->m_uiBackLog)
   {
       w_error = SRT_REJ_BACKLOG;
       LOGC(cnlog.Note, log << "newConnection: listen backlog=" << ls->m_uiBackLog << " EXCEEDED");
       return -1;
   }

   try
   {
      ns = new CUDTSocket;
      ns->m_pUDT = new CUDT(ns, *(ls->m_pUDT));
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

   ns->m_pUDT->m_RejectReason = SRT_REJ_UNKNOWN; // pre-set a universal value

   try
   {
       ns->m_SocketID = generateSocketID();
   }
   catch (const CUDTException&)
   {
       LOGF(cnlog.Fatal, "newConnection: IPE: all sockets occupied? Last gen=%d", m_SocketIDGenerator);
       // generateSocketID throws exception, which can be naturally handled
       // when the call is derived from the API call, but here it's called
       // internally in response to receiving a handshake. It must be handled
       // here and turned into an erroneous return value.
       delete ns;
       return -1;
   }

   ns->m_ListenSocket = listen;
   ns->m_pUDT->m_SocketID = ns->m_SocketID;
   ns->m_PeerID = w_hs.m_iID;
   ns->m_iISN = w_hs.m_iISN;

   HLOGC(cnlog.Debug, log << "newConnection: DATA: lsnid=" << listen
            << " id=" << ns->m_pUDT->m_SocketID
            << " peerid=" << ns->m_pUDT->m_PeerID
            << " ISN=" << ns->m_iISN);

   int error = 0;
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
       HLOGF(cnlog.Debug,
               "newConnection: incoming %s, mapping socket %d",
               peer.str().c_str(), ns->m_SocketID);
       {
           ScopedLock cg(m_GlobControlLock);
           m_Sockets[ns->m_SocketID] = ns;
       }

       if (ls->m_pUDT->m_cbAcceptHook)
       {
           if (!ls->m_pUDT->runAcceptHook(ns->m_pUDT, peer.get(), w_hs, hspkt))
           {
               w_error = ns->m_pUDT->m_RejectReason;

               error = 1;
               goto ERR_ROLLBACK;
           }
       }

       // bind to the same addr of listening socket
       ns->m_pUDT->open();
       updateListenerMux(ns, ls);

       ns->m_pUDT->acceptAndRespond(ls->m_SelfAddr, peer, hspkt, (w_hs));
   }
   catch (...)
   {
       // Extract the error that was set in this new failed entity.
       w_error = ns->m_pUDT->m_RejectReason;
       error = 1;
       goto ERR_ROLLBACK;
   }

   ns->m_Status = SRTS_CONNECTED;

   // copy address information of local node
   // Precisely, what happens here is:
   // - Get the IP address and port from the system database
   ns->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr((ns->m_SelfAddr));
   // - OVERWRITE just the IP address itself by a value taken from piSelfIP
   // (the family is used exactly as the one taken from what has been returned
   // by getsockaddr)
   CIPAddress::pton((ns->m_SelfAddr), ns->m_pUDT->m_piSelfIP, peer);

   {
       // protect the m_PeerRec structure (and group existence)
       ScopedLock glock (m_GlobControlLock);
       try
       {
           HLOGF(cnlog.Debug,
                   "newConnection: mapping peer %d to that socket (%d)\n",
                   ns->m_PeerID, ns->m_SocketID);
           m_PeerRec[ns->getPeerSpec()].insert(ns->m_SocketID);
       }
       catch (...)
       {
           LOGC(cnlog.Error, log << "newConnection: error when mapping peer!");
           error = 2;
       }

       // The access to m_GroupOf should be also protected, as the group
       // could be requested deletion in the meantime. This will hold any possible
       // removal from group and resetting m_GroupOf field.

#if ENABLE_EXPERIMENTAL_BONDING
       if (ns->m_GroupOf)
       {
           // XXX this might require another check of group type.
           // For redundancy group, at least, update the status in the group
           CUDTGroup* g = ns->m_GroupOf;
           ScopedLock glock (g->m_GroupLock);
           if (g->m_bClosing)
           {
               error = 1; // "INTERNAL REJECTION"
               goto ERR_ROLLBACK;
           }

           // Check if this is the first socket in the group.
           // If so, give it up to accept, otherwise just do nothing
           // The client will be informed about the newly added connection at the
           // first moment when attempting to get the group status.
           for (CUDTGroup::gli_t gi = g->m_Group.begin(); gi != g->m_Group.end(); ++gi)
           {
               if (gi->laststatus == SRTS_CONNECTED)
               {
                   HLOGC(cnlog.Debug, log << "Found another connected socket in the group: $"
                           << gi->id << " - socket will be NOT given up for accepting");
                   should_submit_to_accept = false;
                   break;
               }
           }

           // Update the status in the group so that the next
           // operation can include the socket in the group operation.
           CUDTGroup::SocketData* gm = ns->m_GroupMemberData;

           HLOGC(cnlog.Debug, log << "newConnection(GROUP): Socket @" << ns->m_SocketID << " BELONGS TO $" << g->id()
                   << " - will " << (should_submit_to_accept? "" : "NOT ") << "report in accept");
           gm->sndstate = SRT_GST_IDLE;
           gm->rcvstate = SRT_GST_IDLE;
           gm->laststatus = SRTS_CONNECTED;

           if (!g->m_bConnected)
           {
               HLOGC(cnlog.Debug, log << "newConnection(GROUP): First socket connected, SETTING GROUP CONNECTED");
               g->m_bConnected = true;
           }

           // XXX PROLBEM!!! These events are subscribed here so that this is done once, lazily,
           // but groupwise connections could be accepted from multiple listeners for the same group!
           // m_listener MUST BE A CONTAINER, NOT POINTER!!!
           // ALSO: Maybe checking "the same listener" is not necessary as subscruption may be done
           // multiple times anyway?
           if (!g->m_listener)
           {
               // Newly created group from the listener, which hasn't yet
               // the listener set.
               g->m_listener = ls;

               // Listen on both first connected socket and continued sockets.
               // This might help with jump-over situations, and in regular continued
               // sockets the IN event won't be reported anyway.
               int listener_modes = SRT_EPOLL_ACCEPT | SRT_EPOLL_UPDATE;
               epoll_add_usock_INTERNAL(g->m_RcvEID, ls, &listener_modes);

               // This listening should be done always when a first connected socket
               // appears as accepted off the listener. This is for the sake of swait() calls
               // inside the group receiving and sending functions so that they get
               // interrupted when a new socket is connected.
           }

           // Add also per-direction subscription for the about-to-be-accepted socket.
           // Both first accepted socket that makes the group-accept and every next
           // socket that adds a new link.
           int read_modes = SRT_EPOLL_IN | SRT_EPOLL_ERR;
           int write_modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
           epoll_add_usock_INTERNAL(g->m_RcvEID, ns, &read_modes);
           epoll_add_usock_INTERNAL(g->m_SndEID, ns, &write_modes);

           // With app reader, do not set groupPacketArrival (block the
           // provider array feature completely for now).


           /* SETUP HERE IF NEEDED
              ns->m_pUDT->m_cbPacketArrival.set(ns->m_pUDT, &CUDT::groupPacketArrival);
            */
       }
       else
       {
           HLOGC(cnlog.Debug, log << "newConnection: Socket @" << ns->m_SocketID << " is not in a group");
       }
#endif
   }

   if (should_submit_to_accept)
   {
      enterCS(ls->m_AcceptLock);
      try
      {
         ls->m_QueuedSockets.insert(ns->m_SocketID);
      }
      catch (...)
      {
         LOGC(cnlog.Error, log << "newConnection: error when queuing socket!");
         error = 3;
      }
      leaveCS(ls->m_AcceptLock);

      HLOGC(cnlog.Debug, log << "ACCEPT: new socket @" << ns->m_SocketID << " submitted for acceptance");
      // acknowledge users waiting for new connections on the listening socket
      m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, SRT_EPOLL_ACCEPT, true);

      CGlobEvent::triggerEvent();

      // XXX the exact value of 'error' is ignored
      if (error > 0)
      {
         goto ERR_ROLLBACK;
      }

      // wake up a waiting accept() call
      CSync::lock_signal(ls->m_AcceptCond, ls->m_AcceptLock);
   }
   else
   {
      HLOGC(cnlog.Debug, log << "ACCEPT: new socket @" << ns->m_SocketID
            << " NOT submitted to acceptance, another socket in the group is already connected");

      // acknowledge INTERNAL users waiting for new connections on the listening socket
      // that are reported when a new socket is connected within an already connected group.
      m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, SRT_EPOLL_UPDATE, true);
      CGlobEvent::triggerEvent();
   }

ERR_ROLLBACK:
   // XXX the exact value of 'error' is ignored
   if (error > 0)
   {
#if ENABLE_LOGGING
       static const char* why [] = {
           "UNKNOWN ERROR",
           "INTERNAL REJECTION",
           "IPE when mapping a socket",
           "IPE when inserting a socket"
       };
       LOGC(cnlog.Warn, log << CONID(ns->m_SocketID) << "newConnection: connection rejected due to: "
               << why[error] << " - " << RequestTypeStr(URQFailure(w_error)));
#endif

      SRTSOCKET id = ns->m_SocketID;
      ns->m_pUDT->closeInternal();
      ns->setClosed();

      // The mapped socket should be now unmapped to preserve the situation that
      // was in the original UDT code.
      // In SRT additionally the acceptAndRespond() function (it was called probably
      // connect() in UDT code) may fail, in which case this socket should not be
      // further processed and should be removed.
      {
          ScopedLock cg(m_GlobControlLock);

#if ENABLE_EXPERIMENTAL_BONDING
          if (ns->m_GroupOf)
          {
              HLOGC(smlog.Debug, log << "@" << ns->m_SocketID << " IS MEMBER OF $" << ns->m_GroupOf->id() << " - REMOVING FROM GROUP");
              ns->removeFromGroup(true);
          }
#endif
          m_Sockets.erase(id);
          m_ClosedSockets[id] = ns;
      }

      return -1;
   }

   return 1;
}

// static forwarder
int CUDT::installAcceptHook(SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq)
{
    return s_UDTUnited.installAcceptHook(lsn, hook, opaq);
}

int CUDTUnited::installAcceptHook(const SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq)
{
    try
    {
        CUDTSocket* s = locateSocket(lsn, ERH_THROW);
        s->m_pUDT->installAcceptHook(hook, opaq);
    }
    catch (CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRT_ERROR;
    }

    return 0;
}

int CUDT::installConnectHook(SRTSOCKET lsn, srt_connect_callback_fn* hook, void* opaq)
{
    return s_UDTUnited.installConnectHook(lsn, hook, opaq);
}

int CUDTUnited::installConnectHook(const SRTSOCKET u, srt_connect_callback_fn* hook, void* opaq)
{
    try
    {
#if ENABLE_EXPERIMENTAL_BONDING
        if (u & SRTGROUP_MASK)
        {
            GroupKeeper k (*this, u, ERH_THROW);
            k.group->installConnectHook(hook, opaq);
            return 0;
        }
#endif
        CUDTSocket* s = locateSocket(u, ERH_THROW);
        s->m_pUDT->installConnectHook(hook, opaq);
    }
    catch (CUDTException& e)
    {
        SetThreadLocalError(e);
        return SRT_ERROR;
    }

    return 0;
}

SRT_SOCKSTATUS CUDTUnited::getStatus(const SRTSOCKET u)
{
    // protects the m_Sockets structure
    ScopedLock cg(m_GlobControlLock);

    sockets_t::const_iterator i = m_Sockets.find(u);

    if (i == m_Sockets.end())
    {
        if (m_ClosedSockets.find(u) != m_ClosedSockets.end())
            return SRTS_CLOSED;

        return SRTS_NONEXIST;
    }
    return i->second->getStatus();
}

int CUDTUnited::bind(CUDTSocket* s, const sockaddr_any& name)
{
   ScopedLock cg(s->m_ControlLock);

   // cannot bind a socket more than once
   if (s->m_Status != SRTS_INIT)
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

   s->m_pUDT->open();
   updateMux(s, name);
   s->m_Status = SRTS_OPENED;

   // copy address information of local node
   s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr((s->m_SelfAddr));

   return 0;
}

int CUDTUnited::bind(CUDTSocket* s, UDPSOCKET udpsock)
{
   ScopedLock cg(s->m_ControlLock);

   // cannot bind a socket more than once
   if (s->m_Status != SRTS_INIT)
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

   sockaddr_any name;
   socklen_t namelen = sizeof name; // max of inet and inet6

   // This will preset the sa_family as well; the namelen is given simply large
   // enough for any family here.
   if (::getsockname(udpsock, &name.sa, &namelen) == -1)
      throw CUDTException(MJ_NOTSUP, MN_INVAL);

   // Successfully extracted, so update the size
   name.len = namelen;

   s->m_pUDT->open();
   updateMux(s, name, &udpsock);
   s->m_Status = SRTS_OPENED;

   // copy address information of local node
   s->m_pUDT->m_pSndQueue->m_pChannel->getSockAddr((s->m_SelfAddr));

   return 0;
}

int CUDTUnited::listen(const SRTSOCKET u, int backlog)
{
   if (backlog <= 0)
      throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

   // Don't search for the socket if it's already -1;
   // this never is a valid socket.
   if (u == UDT::INVALID_SOCK)
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
      return 0;

   // a socket can listen only if is in OPENED status
   if (s->m_Status != SRTS_OPENED)
      throw CUDTException(MJ_NOTSUP, MN_ISUNBOUND, 0);

   // [[using assert(s->m_Status == OPENED)]];

   // listen is not supported in rendezvous connection setup
   if (s->m_pUDT->m_config.bRendezvous)
      throw CUDTException(MJ_NOTSUP, MN_ISRENDEZVOUS, 0);

   s->m_uiBackLog = backlog;

   // [[using assert(s->m_Status == OPENED)]]; // (still, unchanged)

   s->m_pUDT->setListenState();  // propagates CUDTException,
                                 // if thrown, remains in OPENED state if so.
   s->m_Status = SRTS_LISTENING;

   return 0;
}

SRTSOCKET CUDTUnited::accept_bond(const SRTSOCKET listeners [], int lsize, int64_t msTimeOut)
{
    CEPollDesc* ed = 0;
    int eid = m_EPoll.create(&ed);

    // Destroy it at return - this function can be interrupted
    // by an exception.
    struct AtReturn
    {
        int eid;
        CUDTUnited* that;
        AtReturn(CUDTUnited* t, int e): eid(e), that(t) {}
        ~AtReturn()
        {
            that->m_EPoll.release(eid);
        }
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
    int lsn = st.begin()->first;
    sockaddr_storage dummy;
    int outlen = sizeof dummy;
    return accept(lsn, ((sockaddr*)&dummy), (&outlen));
}

SRTSOCKET CUDTUnited::accept(const SRTSOCKET listen, sockaddr* pw_addr, int* pw_addrlen)
{
   if (pw_addr && !pw_addrlen)
   {
      LOGC(cnlog.Error, log << "srt_accept: provided address, but address length parameter is missing");
      throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
   }

   CUDTSocket* ls = locateSocket(listen);

   if (ls == NULL)
   {
      LOGC(cnlog.Error, log << "srt_accept: invalid listener socket ID value: " << listen);
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
   }

   // the "listen" socket must be in LISTENING status
   if (ls->m_Status != SRTS_LISTENING)
   {
      LOGC(cnlog.Error, log << "srt_accept: socket @" << listen << " is not in listening state (forgot srt_listen?)");
      throw CUDTException(MJ_NOTSUP, MN_NOLISTEN, 0);
   }

   // no "accept" in rendezvous connection setup
   if (ls->m_pUDT->m_config.bRendezvous)
   {
       LOGC(cnlog.Fatal, log << "CUDTUnited::accept: RENDEZVOUS flag passed through check in srt_listen when it set listen state");
       // This problem should never happen because `srt_listen` function should have
       // checked this situation before and not set listen state in result.
       // Inform the user about the invalid state in the universal way.
       throw CUDTException(MJ_NOTSUP, MN_NOLISTEN, 0);
   }

   SRTSOCKET u = CUDT::INVALID_SOCK;
   bool accepted = false;

   // !!only one conection can be set up each time!!
   while (!accepted)
   {
       UniqueLock accept_lock(ls->m_AcceptLock);
       CSync accept_sync(ls->m_AcceptCond, accept_lock);

       if ((ls->m_Status != SRTS_LISTENING) || ls->m_pUDT->m_bBroken)
       {
           // This socket has been closed.
           accepted = true;
       }
       else if (ls->m_QueuedSockets.size() > 0)
       {
           set<SRTSOCKET>::iterator b = ls->m_QueuedSockets.begin();
           u = *b;
           ls->m_QueuedSockets.erase(b);
           accepted = true;
       }
       else if (!ls->m_pUDT->m_config.bSynRecving)
       {
           accepted = true;
       }

       if (!accepted && (ls->m_Status == SRTS_LISTENING))
           accept_sync.wait();

       if (ls->m_QueuedSockets.empty())
           m_EPoll.update_events(listen, ls->m_pUDT->m_sPollID, SRT_EPOLL_ACCEPT, false);
   }

   if (u == CUDT::INVALID_SOCK)
   {
      // non-blocking receiving, no connection available
      if (!ls->m_pUDT->m_config.bSynRecving)
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

   // Set properly the SRTO_GROUPCONNECT flag
   s->core().m_config.iGroupConnect = 0;

   // Check if LISTENER has the SRTO_GROUPCONNECT flag set,
   // and the already accepted socket has successfully joined
   // the mirror group. If so, RETURN THE GROUP ID, not the socket ID.
#if ENABLE_EXPERIMENTAL_BONDING
   if (ls->m_pUDT->m_config.iGroupConnect == 1 && s->m_GroupOf)
   {
       // Put a lock to protect the group against accidental deletion
       // in the meantime.
       ScopedLock glock (m_GlobControlLock);
       // Check again; it's unlikely to happen, but
       // it's a theoretically possible scenario
       if (s->m_GroupOf)
       {
           u = s->m_GroupOf->m_GroupID;
           s->core().m_config.iGroupConnect = 1; // should be derived from ls, but make sure

           // Mark the beginning of the connection at the moment
           // when the group ID is returned to the app caller
            s->m_GroupOf->m_stats.tsLastSampleTime = steady_clock::now();
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
      // Check if the length of the buffer to fill the name in
      // was large enough.
      const int len = s->m_PeerAddr.size();
      if (*pw_addrlen < len)
          throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

      memcpy((pw_addr), &s->m_PeerAddr, len);
      *pw_addrlen = len;
   }

   return u;
}

int CUDTUnited::connect(SRTSOCKET u, const sockaddr* srcname, const sockaddr* tarname, int namelen)
{
    // Here both srcname and tarname must be specified
    if (!srcname || !tarname || size_t(namelen) < sizeof (sockaddr_in))
    {
        LOGC(aclog.Error, log << "connect(with source): invalid call: srcname="
                << srcname << " tarname=" << tarname << " namelen=" << namelen);
        throw CUDTException(MJ_NOTSUP, MN_INVAL);
    }

    sockaddr_any source_addr(srcname, namelen);
    if (source_addr.len == 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    sockaddr_any target_addr(tarname, namelen);
    if (target_addr.len == 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

#if ENABLE_EXPERIMENTAL_BONDING
    // Check affiliation of the socket. It's now allowed for it to be
    // a group or socket. For a group, add automatically a socket to
    // the group.
    if (u & SRTGROUP_MASK)
    {
        GroupKeeper k (*this, u, ERH_THROW);
        // Note: forced_isn is ignored when connecting a group.
        // The group manages the ISN by itself ALWAYS, that is,
        // it's generated anew for the very first socket, and then
        // derived by all sockets in the group.
        SRT_SOCKGROUPCONFIG gd[1] = { srt_prepare_endpoint(srcname, tarname, namelen) };

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
    return connectIn(s, target_addr, SRT_SEQNO_NONE);
}

int CUDTUnited::connect(const SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn)
{
    sockaddr_any target_addr(name, namelen);
    if (target_addr.len == 0)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

#if ENABLE_EXPERIMENTAL_BONDING
    // Check affiliation of the socket. It's now allowed for it to be
    // a group or socket. For a group, add automatically a socket to
    // the group.
    if (u & SRTGROUP_MASK)
    {
        GroupKeeper k (*this, u, ERH_THROW);

        // Note: forced_isn is ignored when connecting a group.
        // The group manages the ISN by itself ALWAYS, that is,
        // it's generated anew for the very first socket, and then
        // derived by all sockets in the group.
        SRT_SOCKGROUPCONFIG gd[1] = { srt_prepare_endpoint(NULL, name, namelen) };
        return singleMemberConnect(k.group, gd);
    }
#endif

    CUDTSocket* s = locateSocket(u);
    if (!s)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    return connectIn(s, target_addr, forced_isn);
}

#if ENABLE_EXPERIMENTAL_BONDING
int CUDTUnited::singleMemberConnect(CUDTGroup* pg, SRT_SOCKGROUPCONFIG* gd)
{
    int gstat = groupConnect(pg, gd, 1);
    if (gstat == -1)
    {
        // We have only one element here, so refer to it.
        // Sanity check
        if (gd->errorcode == SRT_SUCCESS)
            gd->errorcode = SRT_EINVPARAM;

        CodeMajor mj = CodeMajor(gd->errorcode / 1000);
        CodeMinor mn = CodeMinor(gd->errorcode % 1000);

        return CUDT::APIError(mj, mn);
    }

    return gstat;
}

// [[using assert(pg->m_iBusy > 0)]]
int CUDTUnited::groupConnect(CUDTGroup* pg, SRT_SOCKGROUPCONFIG* targets, int arraysize)
{
    CUDTGroup& g = *pg;
    SRT_ASSERT(g.m_iBusy > 0);

    // The group must be managed to use srt_connect on it,
    // as it must create particular socket automatically.

    // Non-managed groups can't be "connected" - at best you can connect
    // every socket individually.
    if (!g.managed())
        throw CUDTException(MJ_NOTSUP, MN_INVAL);

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

    // If the open state switched to OPENED, the blocking mode
    // must make it wait for connecting it. Doing connect when the
    // group is already OPENED returns immediately, regardless if the
    // connection is going to later succeed or fail (this will be
    // known in the group state information).
    bool block_new_opened = !g.m_bOpened && g.m_bSynRecving;
    const bool was_empty = g.groupEmpty();

    // In case the group was retried connection, clear first all epoll readiness.
    const int ncleared = m_EPoll.update_events(g.id(), g.m_sPollID, SRT_EPOLL_ERR, false);
    if (was_empty || ncleared)
    {
        HLOGC(aclog.Debug, log << "srt_connect/group: clearing IN/OUT because was_empty=" << was_empty << " || ncleared=" << ncleared);
        // IN/OUT only in case when the group is empty, otherwise it would
        // clear out correct readiness resulting from earlier calls.
        // This also should happen if ERR flag was set, as IN and OUT could be set, too.
        m_EPoll.update_events(g.id(), g.m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT, false);
    }
    SRTSOCKET retval = -1;

    int eid = -1;
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

    HLOGC(aclog.Debug, log << "groupConnect: will connect " << arraysize << " links and "
            << (block_new_opened ? "BLOCK until any is ready" : "leave the process in background"));

    for (int tii = 0; tii < arraysize; ++tii)
    {
        sockaddr_any target_addr(targets[tii].peeraddr);
        sockaddr_any source_addr(targets[tii].srcaddr);
        SRTSOCKET& sid_rloc = targets[tii].id;
        int& erc_rloc = targets[tii].errorcode;
        erc_rloc = SRT_SUCCESS; // preinitialized
        HLOGC(aclog.Debug, log << "groupConnect: taking on " << sockaddr_any(targets[tii].peeraddr).str());

        CUDTSocket* ns = 0;

        // NOTE: After calling newSocket, the socket is mapped into m_Sockets.
        // It must be MANUALLY removed from this list in case we need it deleted.
        SRTSOCKET sid = newSocket(&ns);

        if (pg->m_cbConnectHook)
        {
            // Derive the connect hook by the socket, if set on the group
            ns->m_pUDT->m_cbConnectHook = pg->m_cbConnectHook;
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
        string error_reason ATR_UNUSED;
        try
        {
            for (size_t i = 0; i < g.m_config.size(); ++i)
            {
                HLOGC(aclog.Debug, log << "groupConnect: OPTION @" << sid << " #" << g.m_config[i].so);
                error_reason = "setting group-derived option: #" + Sprint(g.m_config[i].so);
                ns->core().setOpt(g.m_config[i].so, &g.m_config[i].value[0], (int) g.m_config[i].value.size());
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

        srt::groups::SocketData data = srt::groups::prepareSocketData(ns);
        if (targets[tii].token != -1)
        {
            // Reuse the token, if specified by the caller
            data.token = targets[tii].token;
        }
        else
        {
            // Otherwise generate and write back the token
            data.token = CUDTGroup::genToken();
            targets[tii].token = data.token;
        }

        {
            ScopedLock cs(m_GlobControlLock);
            if (m_Sockets.count(sid) == 0)
            {
                HLOGC(aclog.Debug, log << "srt_connect_group: socket @" << sid << " deleted in process");
                // Someone deleted the socket in the meantime?
                // Unlikely, but possible in theory.
                // Don't delete anyhting - it's alreay done.
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
                HLOGC(aclog.Debug, log << "srt_connect_group: not processing @" << sid << " due to error in setting options");
                proceed = false;
            }

            if (g.m_bClosing)
            {
                HLOGC(aclog.Debug, log << "srt_connect_group: not processing @" << sid << " due to CLOSED GROUP $" << g.m_GroupID);
                proceed = false;
            }

            if (proceed)
            {
                CUDTGroup::SocketData* f = g.add(data);
                ns->m_GroupMemberData = f;
                ns->m_GroupOf = &g;
                f->weight = targets[tii].weight;
                LOGC(aclog.Note, log << "srt_connect_group: socket @" << sid << " added to group $" << g.m_GroupID);
            }
            else
            {
                targets[tii].id = CUDT::INVALID_SOCK;
                delete ns;
                m_Sockets.erase(sid);

                // If failed to set options, then do not continue
                // neither with binding, nor with connecting.
                continue;
            }
        }


        // XXX This should be reenabled later, this should
        // be probably still in use to exchange information about
        // packets assymetrically lost. But for no other purpose.
        /*
        ns->m_pUDT->m_cbPacketArrival.set(ns->m_pUDT, &CUDT::groupPacketArrival);
        */

        int isn = g.currentSchedSequence();

        // Don't synchronize ISN in case of synch on msgno. Every link
        // may send their own payloads independently.
        if (g.synconmsgno())
        {
            HLOGC(aclog.Debug, log << "groupConnect: NOT synchronizing sequence numbers: will sync on msgno");
            isn = -1;
        }

        // Set it the groupconnect option, as all in-group sockets should have.
        ns->m_pUDT->m_config.iGroupConnect = 1;

        // Every group member will have always nonblocking
        // (this implies also non-blocking connect/accept).
        // The group facility functions will block when necessary
        // using epoll_wait.
        ns->m_pUDT->m_config.bSynRecving = false;
        ns->m_pUDT->m_config.bSynSending = false;

        HLOGC(aclog.Debug, log << "groupConnect: NOTIFIED AS PENDING @" << sid << " both read and write");
        // If this socket is not to block the current connect process,
        // it may still be needed for the further check if the redundant
        // connection succeeded or failed and whether the new socket is
        // ready to use or needs to be closed.
        epoll_add_usock_INTERNAL(g.m_SndEID, ns, &connect_modes);
        epoll_add_usock_INTERNAL(g.m_RcvEID, ns, &connect_modes);

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
        catch (CUDTException& e)
        {
            LOGC(aclog.Error, log << "groupConnect: socket @" << sid << " in group " << pg->id() << " failed to connect");
            // We know it does belong to a group.
            // Remove it first because this involves a mutex, and we want
            // to avoid locking more than one mutex at a time.
            erc_rloc = e.getErrorCode();
            targets[tii].errorcode = e.getErrorCode();
            targets[tii].id = CUDT::INVALID_SOCK;

            ScopedLock cl (m_GlobControlLock);
            ns->removeFromGroup(false);
            m_Sockets.erase(ns->m_SocketID);
            // Intercept to delete the socket on failure.
            delete ns;
            continue;
        }
        catch (...)
        {
            LOGC(aclog.Fatal, log << "groupConnect: IPE: UNKNOWN EXCEPTION from connectIn");
            targets[tii].errorcode = SRT_ESYSOBJ;
            targets[tii].id = CUDT::INVALID_SOCK;
            ScopedLock cl (m_GlobControlLock);
            ns->removeFromGroup(false);
            m_Sockets.erase(ns->m_SocketID);
            // Intercept to delete the socket on failure.
            delete ns;

            // Do not use original exception, it may crash off a C API.
            throw CUDTException(MJ_SYSTEMRES, MN_OBJECT);
        }

        SRT_SOCKSTATUS st;
        {
            ScopedLock grd (ns->m_ControlLock);
            st = ns->getStatus();
        }

        {
            // NOTE: Not applying m_GlobControlLock because the group is now
            // set busy, so it won't be deleted, even if it was requested to be closed.
            ScopedLock grd (g.m_GroupLock);

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
                retval = -1;
                break;
            }

            HLOGC(aclog.Debug, log << "groupConnect: @" << sid << " connection successful, setting group OPEN (was "
                    << (g.m_bOpened ? "ALREADY" : "NOT") << "), will " << (block_new_opened ? "" : "NOT ")
                    << "block the connect call, status:" << SockStatusStr(st));

            // XXX OPEN OR CONNECTED?
            // BLOCK IF NOT OPEN OR BLOCK IF NOT CONNECTED?
            //
            // What happens to blocking when there are 2 connections
            // pending, about to be broken, and srt_connect() is called again?
            // SHOULD BLOCK the latter, even though is OPEN.
            // Or, OPEN should be removed from here and srt_connect(_group)
            // should block always if the group doesn't have neither 1 conencted link
            g.m_bOpened = true;

            g.m_stats.tsLastSampleTime = steady_clock::now();

            f->laststatus = st;
            // Check the socket status and update it.
            // Turn the group state of the socket to IDLE only if
            // connection is established or in progress
            f->agent = source_addr;
            f->peer = target_addr;

            if (st >= SRTS_BROKEN)
            {
                f->sndstate = SRT_GST_BROKEN;
                f->rcvstate = SRT_GST_BROKEN;
                epoll_remove_socket_INTERNAL(g.m_SndEID, ns);
                epoll_remove_socket_INTERNAL(g.m_RcvEID, ns);
            }
            else
            {
                f->sndstate = SRT_GST_PENDING;
                f->rcvstate = SRT_GST_PENDING;
                spawned[sid] = ns;

                sid_rloc = sid;
                erc_rloc = 0;
                retval = sid;
            }
        }
    }

    if (retval == -1)
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
            retval = -1;
            break;
        }
        HLOGC(aclog.Debug, log << "groupConnect: first connection, applying EPOLL WAITING.");
        int len = (int) spawned.size();
        vector<SRTSOCKET> ready(spawned.size());
        const int estat = srt_epoll_wait(eid,
                    NULL, NULL,  // IN/ACCEPT
                    &ready[0], &len, // OUT/CONNECT
                    -1, // indefinitely (FIXME Check if it needs to REGARD CONNECTION TIMEOUT!)
                    NULL, NULL,
                    NULL, NULL
                    );

        // Sanity check. Shouldn't happen if subs are in sync with spawned.
        if (estat == -1)
        {
#if ENABLE_LOGGING
            CUDTException& x = CUDT::getlasterror();
            if (x.getErrorCode() != SRT_EPOLLEMPTY)
            {
                LOGC(aclog.Error, log << "groupConnect: srt_epoll_wait failed not because empty, unexpected IPE:"
                        << x.getErrorMessage());
            }
#endif
            HLOGC(aclog.Debug, log << "groupConnect: srt_epoll_wait failed - breaking the wait loop");
            retval = -1;
            break;
        }

        // At the moment when you are going to work with real sockets,
        // lock the groups so that no one messes up with something here
        // in the meantime.

        ScopedLock lock (*g.exp_groupLock());

        // NOTE: UNDER m_GroupLock, NO API FUNCTION CALLS DARE TO HAPPEN BELOW!

        // Check first if a socket wasn't closed in the meantime. It will be
        // automatically removed from all EIDs, but there's no sense in keeping
        // them in 'spawned' map.
        for (map<SRTSOCKET, CUDTSocket*>::iterator y = spawned.begin();
                y != spawned.end(); ++y)
        {
            SRTSOCKET sid = y->first;
            if (y->second->getStatus() >= SRTS_BROKEN)
            {
                HLOGC(aclog.Debug, log << "groupConnect: Socket @" << sid << " got BROKEN in the meantine during the check, remove from candidates");
                // Remove from spawned and try again
                broken.push_back(sid);

                epoll_remove_socket_INTERNAL(eid, y->second);
                epoll_remove_socket_INTERNAL(g.m_SndEID, y->second);
                epoll_remove_socket_INTERNAL(g.m_RcvEID, y->second);
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

            SRTSOCKET sid = x->first;
            CUDTSocket* s = x->second;

            // Check status. If failed, remove from spawned
            // and try again.
            SRT_SOCKSTATUS st = s->getStatus();
            if (st >= SRTS_BROKEN)
            {
                HLOGC(aclog.Debug, log << "groupConnect: Socket @" << sid << " got BROKEN during background connect, remove & TRY AGAIN");
                // Remove from spawned and try again
                if (spawned.erase(sid))
                    broken.push_back(sid);

                epoll_remove_socket_INTERNAL(eid, s);
                epoll_remove_socket_INTERNAL(g.m_SndEID, s);
                epoll_remove_socket_INTERNAL(g.m_RcvEID, s);

                continue;
            }

            if (st == SRTS_CONNECTED)
            {
                HLOGC(aclog.Debug, log << "groupConnect: Socket @" << sid << " got CONNECTED as first in the group - reporting");
                retval = sid;
                g.m_bConnected = true;
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
            HLOGC(aclog.Debug, log << "groupConnect: Socket @" << sid << " got spurious wakeup in "
                    << SockStatusStr(st) << " TRY AGAIN");
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
        close(s);
    }

    // There's no possibility to report a problem on every connection
    // separately in case when every single connection has failed. What
    // is more interesting, it's only a matter of luck that all connections
    // fail at exactly the same time. OTOH if all are to fail, this
    // function will still be polling sockets to determine the last man
    // standing. Each one could, however, break by a different reason,
    // for example, one by timeout, another by wrong passphrase. Check
    // the `errorcode` field to determine the reaon for particular link.
    if (retval == -1)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    return retval;
}
#endif


int CUDTUnited::connectIn(CUDTSocket* s, const sockaddr_any& target_addr, int32_t forced_isn)
{
   ScopedLock cg(s->m_ControlLock);
   // a socket can "connect" only if it is in the following states:
   // - OPENED: assume the socket binding parameters are configured
   // - INIT: configure binding parameters here
   // - any other (meaning, already connected): report error

   if (s->m_Status == SRTS_INIT)
   {
       if (s->m_pUDT->m_config.bRendezvous)
           throw CUDTException(MJ_NOTSUP, MN_ISRENDUNBOUND, 0);

       // If bind() was done first on this socket, then the
       // socket will not perform this step. This actually does the
       // same thing as bind() does, just with empty address so that
       // the binding parameters are autoselected.

       s->m_pUDT->open();
       sockaddr_any autoselect_sa (target_addr.family());
       // This will create such a sockaddr_any that
       // will return true from empty(). 
       updateMux(s, autoselect_sa);  // <<---- updateMux
       // -> C(Snd|Rcv)Queue::init
       // -> pthread_create(...C(Snd|Rcv)Queue::worker...)
       s->m_Status = SRTS_OPENED;
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
       s->m_pUDT->startConnect(target_addr, forced_isn);
   }
   catch (CUDTException& e) // Interceptor, just to change the state.
   {
      s->m_Status = SRTS_OPENED;
      throw e;
   }

   // ScopedLock destructor will delete cg and unlock s->m_ControlLock

   return 0;
}


int CUDTUnited::close(const SRTSOCKET u)
{
#if ENABLE_EXPERIMENTAL_BONDING
    if (u & SRTGROUP_MASK)
    {
        GroupKeeper k (*this, u, ERH_THROW);
        k.group->close();
        deleteGroup(k.group);
        return 0;
    }
#endif
    CUDTSocket* s = locateSocket(u);
    if (!s)
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

    return close(s);
}

#if ENABLE_EXPERIMENTAL_BONDING
void CUDTUnited::deleteGroup(CUDTGroup* g)
{
    using srt_logging::gmlog;

    srt::sync::ScopedLock cg (m_GlobControlLock);
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
    for (sockets_t::iterator i = m_Sockets.begin();
            i != m_Sockets.end(); ++ i)
    {
        CUDTSocket* s = i->second;
        if (s->m_GroupOf == g)
        {
            HLOGC(smlog.Debug, log << "deleteGroup: IPE: existing @" << s->m_SocketID << " points to a dead group!");
            s->m_GroupOf = NULL;
            s->m_GroupMemberData = NULL;
        }
    }

    // Just in case, do it in closed sockets, too, although this should be
    // always done before moving to it.
    for (sockets_t::iterator i = m_ClosedSockets.begin();
            i != m_ClosedSockets.end(); ++ i)
    {
        CUDTSocket* s = i->second;
        if (s->m_GroupOf == g)
        {
            HLOGC(smlog.Debug, log << "deleteGroup: IPE: closed @" << s->m_SocketID << " points to a dead group!");
            s->m_GroupOf = NULL;
            s->m_GroupMemberData = NULL;
        }
    }
}
#endif

int CUDTUnited::close(CUDTSocket* s)
{
   HLOGC(smlog.Debug, log << s->m_pUDT->CONID() << " CLOSE. Acquiring control lock");

   ScopedLock socket_cg(s->m_ControlLock);

   HLOGC(smlog.Debug, log << s->m_pUDT->CONID() << " CLOSING (removing from listening, closing CUDT)");

   const bool synch_close_snd = s->m_pUDT->m_config.bSynSending;

   SRTSOCKET u = s->m_SocketID;

   if (s->m_Status == SRTS_LISTENING)
   {
      if (s->m_pUDT->m_bBroken)
         return 0;

      s->m_tsClosureTimeStamp = steady_clock::now();
      s->m_pUDT->m_bBroken    = true;

      // Change towards original UDT: 
      // Leave all the closing activities for garbageCollect to happen,
      // however remove the listener from the RcvQueue IMMEDIATELY.
      // Even though garbageCollect would eventually remove the listener
      // as well, there would be some time interval between now and the
      // moment when it's done, and during this time the application will
      // be unable to bind to this port that the about-to-delete listener
      // is currently occupying (due to blocked slot in the RcvQueue).

      HLOGC(smlog.Debug, log << s->m_pUDT->CONID() << " CLOSING (removing listener immediately)");
      s->m_pUDT->notListening();

      // broadcast all "accept" waiting
      CSync::lock_broadcast(s->m_AcceptCond, s->m_AcceptLock);
   }
   else
   {
       // Note: this call may be done on a socket that hasn't finished
       // sending all packets scheduled for sending, which means, this call
       // may block INDEFINITELY. As long as it's acceptable to block the
       // call to srt_close(), and all functions in all threads where this
       // very socket is used, this shall not block the central database.
       s->m_pUDT->closeInternal();

       // synchronize with garbage collection.
       HLOGC(smlog.Debug, log << "@" << u << "U::close done. GLOBAL CLOSE: " << s->m_pUDT->CONID() << ". Acquiring GLOBAL control lock");
       ScopedLock manager_cg(m_GlobControlLock);
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
           return 0;
       }
       s = i->second;
       s->setClosed();

#if ENABLE_EXPERIMENTAL_BONDING
       if (s->m_GroupOf)
       {
           HLOGC(smlog.Debug, log << "@" << s->m_SocketID << " IS MEMBER OF $" << s->m_GroupOf->id() << " - REMOVING FROM GROUP");
           s->removeFromGroup(true);
       }
#endif

       m_Sockets.erase(s->m_SocketID);
       m_ClosedSockets[s->m_SocketID] = s;
       HLOGC(smlog.Debug, log << "@" << u << "U::close: Socket MOVED TO CLOSED for collecting later.");

       CGlobEvent::triggerEvent();
   }

   HLOGC(smlog.Debug, log << "@" << u << ": GLOBAL: CLOSING DONE");

   // Check if the ID is still in closed sockets before you access it
   // (the last triggerEvent could have deleted it).
   if ( synch_close_snd )
   {
#if SRT_ENABLE_CLOSE_SYNCH

       HLOGC(smlog.Debug, log << "@" << u << " GLOBAL CLOSING: sync-waiting for releasing sender resources...");
       for (;;)
       {
           CSndBuffer* sb = s->m_pUDT->m_pSndBuffer;

           // Disconnected from buffer - nothing more to check.
           if (!sb)
           {
               HLOGC(smlog.Debug, log << "@" << u << " GLOBAL CLOSING: sending buffer disconnected. Allowed to close.");
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
               ScopedLock manager_cg(m_GlobControlLock);
               isgone = m_ClosedSockets.count(u) == 0;
           }
           if (!isgone)
           {
               isgone = !s->m_pUDT->m_bOpened;
           }
           if (isgone)
           {
               HLOGC(smlog.Debug, log << "@" << u << " GLOBAL CLOSING: ... gone in the meantime, whatever. Exiting close().");
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

   return 0;
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

   if (!s->m_pUDT->m_bConnected || s->m_pUDT->m_bBroken)
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

   if (s->m_pUDT->m_bBroken)
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);

   if (s->m_Status == SRTS_INIT)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   const int len = s->m_SelfAddr.size();
   if (*pw_namelen < len)
       throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

   memcpy((pw_name), &s->m_SelfAddr.sa, len);
   *pw_namelen = len;
}

int CUDTUnited::select(
   UDT::UDSET* readfds, UDT::UDSET* writefds, UDT::UDSET* exceptfds, const timeval* timeout)
{
   const steady_clock::time_point entertime = steady_clock::now();

   const long timeo_us = timeout
       ? timeout->tv_sec * 1000000 + timeout->tv_usec
       : -1;
   const steady_clock::duration timeo(microseconds_from(timeo_us));

   // initialize results
   int count = 0;
   set<SRTSOCKET> rs, ws, es;

   // retrieve related UDT sockets
   vector<CUDTSocket*> ru, wu, eu;
   CUDTSocket* s;
   if (readfds)
      for (set<SRTSOCKET>::iterator i1 = readfds->begin();
         i1 != readfds->end(); ++ i1)
      {
         if (getStatus(*i1) == SRTS_BROKEN)
         {
            rs.insert(*i1);
            ++ count;
         }
         else if (!(s = locateSocket(*i1)))
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
         else
            ru.push_back(s);
      }
   if (writefds)
      for (set<SRTSOCKET>::iterator i2 = writefds->begin();
         i2 != writefds->end(); ++ i2)
      {
         if (getStatus(*i2) == SRTS_BROKEN)
         {
            ws.insert(*i2);
            ++ count;
         }
         else if (!(s = locateSocket(*i2)))
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
         else
            wu.push_back(s);
      }
   if (exceptfds)
      for (set<SRTSOCKET>::iterator i3 = exceptfds->begin();
         i3 != exceptfds->end(); ++ i3)
      {
         if (getStatus(*i3) == SRTS_BROKEN)
         {
            es.insert(*i3);
            ++ count;
         }
         else if (!(s = locateSocket(*i3)))
            throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
         else
            eu.push_back(s);
      }

   do
   {
      // query read sockets
      for (vector<CUDTSocket*>::iterator j1 = ru.begin(); j1 != ru.end(); ++ j1)
      {
         s = *j1;

         if (s->readReady() || s->m_Status == SRTS_CLOSED)
         {
            rs.insert(s->m_SocketID);
            ++ count;
         }
      }

      // query write sockets
      for (vector<CUDTSocket*>::iterator j2 = wu.begin(); j2 != wu.end(); ++ j2)
      {
         s = *j2;

         if (s->writeReady() || s->m_Status == SRTS_CLOSED)
         {
            ws.insert(s->m_SocketID);
            ++ count;
         }
      }

      // query exceptions on sockets
      for (vector<CUDTSocket*>::iterator j3 = eu.begin(); j3 != eu.end(); ++ j3)
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

int CUDTUnited::selectEx(
   const vector<SRTSOCKET>& fds,
   vector<SRTSOCKET>* readfds,
   vector<SRTSOCKET>* writefds,
   vector<SRTSOCKET>* exceptfds,
   int64_t msTimeOut)
{
    const steady_clock::time_point entertime = steady_clock::now();

    const long timeo_us = msTimeOut >= 0
        ? msTimeOut * 1000
        : -1;
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
      for (vector<SRTSOCKET>::const_iterator i = fds.begin();
         i != fds.end(); ++ i)
      {
         CUDTSocket* s = locateSocket(*i);

         if ((!s) || s->m_pUDT->m_bBroken || (s->m_Status == SRTS_CLOSED))
         {
            if (exceptfds)
            {
               exceptfds->push_back(*i);
               ++ count;
            }
            continue;
         }

         if (readfds)
         {
            if ((s->m_pUDT->m_bConnected
                  && s->m_pUDT->m_pRcvBuffer->isRcvDataReady()
               )
               || (s->m_pUDT->m_bListening
                  && (s->m_QueuedSockets.size() > 0)))
            {
               readfds->push_back(s->m_SocketID);
               ++ count;
            }
         }

         if (writefds)
         {
            if (s->m_pUDT->m_bConnected
               && (s->m_pUDT->m_pSndBuffer->getCurrBufSize()
                  < s->m_pUDT->m_config.iSndBufSize))
            {
               writefds->push_back(s->m_SocketID);
               ++ count;
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

int CUDTUnited::epoll_clear_usocks(int eid)
{
    return m_EPoll.clear_usocks(eid);
}

int CUDTUnited::epoll_add_usock(
   const int eid, const SRTSOCKET u, const int* events)
{
   int ret = -1;
#if ENABLE_EXPERIMENTAL_BONDING
   if (u & SRTGROUP_MASK)
   {
       GroupKeeper k (*this, u, ERH_THROW);
       ret = m_EPoll.update_usock(eid, u, events);
       k.group->addEPoll(eid);
       return 0;
   }
#endif

   CUDTSocket* s = locateSocket(u);
   if (s)
   {
      ret = epoll_add_usock_INTERNAL(eid, s, events);
   }
   else
   {
      throw CUDTException(MJ_NOTSUP, MN_SIDINVAL);
   }

   return ret;
}

// NOTE: WILL LOCK (serially):
// - CEPoll::m_EPollLock
// - CUDT::m_RecvLock
int CUDTUnited::epoll_add_usock_INTERNAL(const int eid, CUDTSocket* s, const int* events)
{
    int ret = m_EPoll.update_usock(eid, s->m_SocketID, events);
    s->m_pUDT->addEPoll(eid);
    return ret;
}

int CUDTUnited::epoll_add_ssock(
   const int eid, const SYSSOCKET s, const int* events)
{
   return m_EPoll.add_ssock(eid, s, events);
}

int CUDTUnited::epoll_update_ssock(
   const int eid, const SYSSOCKET s, const int* events)
{
   return m_EPoll.update_ssock(eid, s, events);
}

template <class EntityType>
int CUDTUnited::epoll_remove_entity(const int eid, EntityType* ent)
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
    int ret = m_EPoll.update_usock(eid, ent->id(), &no_events);

    return ret;
}

// Needed internal access!
int CUDTUnited::epoll_remove_socket_INTERNAL(const int eid, CUDTSocket* s)
{
    return epoll_remove_entity(eid, s->m_pUDT);
}

#if ENABLE_EXPERIMENTAL_BONDING
int CUDTUnited::epoll_remove_group_INTERNAL(const int eid, CUDTGroup* g)
{
    return epoll_remove_entity(eid, g);
}
#endif

int CUDTUnited::epoll_remove_usock(const int eid, const SRTSOCKET u)
{
   CUDTSocket* s = 0;

#if ENABLE_EXPERIMENTAL_BONDING
   CUDTGroup* g = 0;
   if (u & SRTGROUP_MASK)
   {
       GroupKeeper k (*this, u, ERH_THROW);
       g = k.group;
       return epoll_remove_entity(eid, g);
   }
   else
#endif
   {
       s = locateSocket(u);
       if (s)
           return epoll_remove_entity(eid, s->m_pUDT);
   }

   LOGC(ealog.Error, log << "remove_usock: @" << u
           << " not found as either socket or group. Removing only from epoll system.");
   int no_events = 0;
   return m_EPoll.update_usock(eid, u, &no_events);
}

int CUDTUnited::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
   return m_EPoll.remove_ssock(eid, s);
}

int CUDTUnited::epoll_uwait(
   const int eid,
   SRT_EPOLL_EVENT* fdsSet,
   int fdsSize, 
   int64_t msTimeOut)
{
   return m_EPoll.uwait(eid, fdsSet, fdsSize, msTimeOut);
}

int32_t CUDTUnited::epoll_set(int eid, int32_t flags)
{
    return m_EPoll.setflags(eid, flags);
}

int CUDTUnited::epoll_release(const int eid)
{
   return m_EPoll.release(eid);
}

CUDTSocket* CUDTUnited::locateSocket(const SRTSOCKET u, ErrorHandling erh)
{
    ScopedLock cg (m_GlobControlLock);
    CUDTSocket* s = locateSocket_LOCKED(u);
    if (!s)
    {
        if (erh == ERH_RETURN)
            return NULL;
        throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
    }

    return s;
}

// [[using locked(m_GlobControlLock)]];
CUDTSocket* CUDTUnited::locateSocket_LOCKED(SRTSOCKET u)
{
    sockets_t::iterator i = m_Sockets.find(u);

    if ((i == m_Sockets.end()) || (i->second->m_Status == SRTS_CLOSED))
    {
        return NULL;
    }

    return i->second;
}

#if ENABLE_EXPERIMENTAL_BONDING
CUDTGroup* CUDTUnited::locateAcquireGroup(SRTSOCKET u, ErrorHandling erh)
{
   ScopedLock cg (m_GlobControlLock);

   const groups_t::iterator i = m_Groups.find(u);
   if ( i == m_Groups.end() )
   {
       if (erh == ERH_THROW)
           throw CUDTException(MJ_NOTSUP, MN_SIDINVAL, 0);
       return NULL;
   }

   ScopedLock cgroup (*i->second->exp_groupLock());
   i->second->apiAcquire();
   return i->second;
}

CUDTGroup* CUDTUnited::acquireSocketsGroup(CUDTSocket* s)
{
   ScopedLock cg (m_GlobControlLock);
   CUDTGroup* g = s->m_GroupOf;
   if (!g)
       return NULL;

   // With m_GlobControlLock locked, we are sure the group
   // still exists, if it wasn't removed from this socket.
   g->apiAcquire();
   return g;
}
#endif

CUDTSocket* CUDTUnited::locatePeer(
   const sockaddr_any& peer,
   const SRTSOCKET id,
   int32_t isn)
{
   ScopedLock cg(m_GlobControlLock);

   map<int64_t, set<SRTSOCKET> >::iterator i = m_PeerRec.find(
      CUDTSocket::getPeerSpec(id, isn));
   if (i == m_PeerRec.end())
      return NULL;

   for (set<SRTSOCKET>::iterator j = i->second.begin();
      j != i->second.end(); ++ j)
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
   ScopedLock cg(m_GlobControlLock);

   // set of sockets To Be Closed and To Be Removed
   vector<SRTSOCKET> tbc;
   vector<SRTSOCKET> tbr;

#if ENABLE_EXPERIMENTAL_BONDING
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

   for (sockets_t::iterator i = m_Sockets.begin();
      i != m_Sockets.end(); ++ i)
   {
       CUDTSocket* s = i->second;

      // check broken connection
      if (s->m_pUDT->m_bBroken)
      {
         if (s->m_Status == SRTS_LISTENING)
         {
            const steady_clock::duration elapsed = steady_clock::now() - s->m_tsClosureTimeStamp;
            // for a listening socket, it should wait an extra 3 seconds
            // in case a client is connecting
            if (elapsed < milliseconds_from(CUDT::COMM_CLOSE_BROKEN_LISTENER_TIMEOUT_MS))
            {
               continue;
            }
         }
         else if ((s->m_pUDT->m_pRcvBuffer != NULL)
            // FIXED: calling isRcvDataAvailable() just to get the information
            // whether there are any data waiting in the buffer,
            // NOT WHETHER THEY ARE ALSO READY TO PLAY at the time when
            // this function is called (isRcvDataReady also checks if the
            // available data is "ready to play").
            && s->m_pUDT->m_pRcvBuffer->isRcvDataAvailable()
            && (s->m_pUDT->m_iBrokenCounter -- > 0))
         {
            // HLOGF(smlog.Debug, "STILL KEEPING socket (still have data):
            // %d\n", i->first);
            // if there is still data in the receiver buffer, wait longer
            continue;
         }

#if ENABLE_EXPERIMENTAL_BONDING
         if (s->m_GroupOf)
         {
             LOGC(smlog.Note, log << "@" << s->m_SocketID << " IS MEMBER OF $" << s->m_GroupOf->id() << " - REMOVING FROM GROUP");
             s->removeFromGroup(true);
         }
#endif

         HLOGC(smlog.Debug, log << "checkBrokenSockets: moving BROKEN socket to CLOSED: @" << i->first);

         //close broken connections and start removal timer
         s->setClosed();
         tbc.push_back(i->first);
         m_ClosedSockets[i->first] = s;

         // remove from listener's queue
         sockets_t::iterator ls = m_Sockets.find(s->m_ListenSocket);
         if (ls == m_Sockets.end())
         {
            ls = m_ClosedSockets.find(s->m_ListenSocket);
            if (ls == m_ClosedSockets.end())
               continue;
         }

         enterCS(ls->second->m_AcceptLock);
         ls->second->m_QueuedSockets.erase(s->m_SocketID);
         leaveCS(ls->second->m_AcceptLock);
      }
   }

   for (sockets_t::iterator j = m_ClosedSockets.begin();
      j != m_ClosedSockets.end(); ++ j)
   {
      // HLOGF(smlog.Debug, "checking CLOSED socket: %d\n", j->first);
      if (!is_zero(j->second->m_pUDT->m_tsLingerExpiration))
      {
         // asynchronous close:
         if ((!j->second->m_pUDT->m_pSndBuffer)
            || (0 == j->second->m_pUDT->m_pSndBuffer->getCurrBufSize())
            || (j->second->m_pUDT->m_tsLingerExpiration <= steady_clock::now()))
         {
            HLOGC(smlog.Debug, log << "checkBrokenSockets: marking CLOSED qualified @" << j->second->m_SocketID);
            j->second->m_pUDT->m_tsLingerExpiration = steady_clock::time_point();
            j->second->m_pUDT->m_bClosing = true;
            j->second->m_tsClosureTimeStamp = steady_clock::now();
         }
      }

      // timeout 1 second to destroy a socket AND it has been removed from
      // RcvUList
      const steady_clock::time_point now = steady_clock::now();
      const steady_clock::duration closed_ago = now - j->second->m_tsClosureTimeStamp;
      if ((closed_ago > seconds_from(1))
         && ((!j->second->m_pUDT->m_pRNode)
            || !j->second->m_pUDT->m_pRNode->m_bOnList))
      {
         HLOGC(smlog.Debug, log << "checkBrokenSockets: @" << j->second->m_SocketID << " closed "
                 << FormatDuration(closed_ago) << " ago and removed from RcvQ - will remove");

         // HLOGF(smlog.Debug, "will unref socket: %d\n", j->first);
         tbr.push_back(j->first);
      }
   }

   // move closed sockets to the ClosedSockets structure
   for (vector<SRTSOCKET>::iterator k = tbc.begin(); k != tbc.end(); ++ k)
      m_Sockets.erase(*k);

   // remove those timeout sockets
   for (vector<SRTSOCKET>::iterator l = tbr.begin(); l != tbr.end(); ++ l)
      removeSocket(*l);
}

// [[using locked(m_GlobControlLock)]]
void CUDTUnited::removeSocket(const SRTSOCKET u)
{
   sockets_t::iterator i = m_ClosedSockets.find(u);

   // invalid socket ID
   if (i == m_ClosedSockets.end())
      return;

   CUDTSocket* const s = i->second;

#if ENABLE_EXPERIMENTAL_BONDING
   if (s->m_GroupOf)
   {
       HLOGC(smlog.Debug, log << "@" << s->m_SocketID << " IS MEMBER OF $" << s->m_GroupOf->id() << " - REMOVING FROM GROUP");
       s->removeFromGroup(true);
   }
#endif
   // decrease multiplexer reference count, and remove it if necessary
   const int mid = s->m_iMuxID;

   {
      ScopedLock cg(s->m_AcceptLock);

      // if it is a listener, close all un-accepted sockets in its queue
      // and remove them later
      for (set<SRTSOCKET>::iterator q = s->m_QueuedSockets.begin();
         q != s->m_QueuedSockets.end(); ++ q)
      {
         sockets_t::iterator si = m_Sockets.find(*q);
         if (si == m_Sockets.end())
         {
            // gone in the meantime
            LOGC(smlog.Error, log << "removeSocket: IPE? socket @" << (*q)
                    << " being queued for listener socket @" << s->m_SocketID
                    << " is GONE in the meantime ???");
            continue;
         }

         CUDTSocket* as = si->second;

         as->breakSocket_LOCKED();
         m_ClosedSockets[*q] = as;
         m_Sockets.erase(*q);
      }
   }

   // remove from peer rec
   map<int64_t, set<SRTSOCKET> >::iterator j = m_PeerRec.find(
      s->getPeerSpec());
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
   m_EPoll.update_events(u, s->m_pUDT->m_sPollID,
      SRT_EPOLL_IN|SRT_EPOLL_OUT|SRT_EPOLL_ERR, false);

   // delete this one
   m_ClosedSockets.erase(i);

   HLOGC(smlog.Debug, log << "GC/removeSocket: closing associated UDT @" << u);
   s->m_pUDT->closeInternal();
   HLOGC(smlog.Debug, log << "GC/removeSocket: DELETING SOCKET @" << u);
   delete s;

   if (mid == -1)
       return;

   map<int, CMultiplexer>::iterator m;
   m = m_mMultiplexer.find(mid);
   if (m == m_mMultiplexer.end())
   {
      LOGC(smlog.Fatal, log << "IPE: For socket @" << u << " MUXER id=" << mid << " NOT FOUND!");
      return;
   }

   CMultiplexer& mx = m->second;

   mx.m_iRefCount --;
   // HLOGF(smlog.Debug, "unrefing underlying socket for %u: %u\n",
   //    u, mx.m_iRefCount);
   if (0 == mx.m_iRefCount)
   {
       HLOGC(smlog.Debug, log << "MUXER id=" << mid << " lost last socket @"
           << u << " - deleting muxer bound to port "
           << mx.m_pChannel->bindAddressAny().hport());
      // The channel has no access to the queues and
      // it looks like the multiplexer is the master of all of them.
      // The queues must be silenced before closing the channel
      // because this will cause error to be returned in any operation
      // being currently done in the queues, if any.
      mx.m_pSndQueue->setClosing();
      mx.m_pRcvQueue->setClosing();
      mx.destroy();
      m_mMultiplexer.erase(m);
   }
}

void CUDTUnited::updateMux(
   CUDTSocket* s, const sockaddr_any& addr, const UDPSOCKET* udpsock /*[[nullable]]*/)
{
   ScopedLock cg(m_GlobControlLock);

   // Don't try to reuse given address, if udpsock was given.
   // In such a case rely exclusively on that very socket and
   // use it the way as it is configured, of course, create also
   // always a new multiplexer for that very socket.
   if (!udpsock && s->m_pUDT->m_config.bReuseAddr)
   {
      const int port = addr.hport();

      // find a reusable address
      for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin();
         i != m_mMultiplexer.end(); ++ i)
      {
          // Use the "family" value blindly from the address; we
          // need to find an existing multiplexer that binds to the
          // given port in the same family as requested address.
          if ((i->second.m_iIPversion == addr.family())
                  && i->second.m_mcfg == s->m_pUDT->m_config)
          {
            if (i->second.m_iPort == port)
            {
               // HLOGF(smlog.Debug, "reusing multiplexer for port
               // %hd\n", port);
               // reuse the existing multiplexer
               ++ i->second.m_iRefCount;
               s->m_pUDT->m_pSndQueue = i->second.m_pSndQueue;
               s->m_pUDT->m_pRcvQueue = i->second.m_pRcvQueue;
               s->m_iMuxID = i->second.m_iID;
               s->m_SelfAddr.family(addr.family());
               return;
            }
         }
      }
   }

   // a new multiplexer is needed
   CMultiplexer m;
   m.m_mcfg = s->m_pUDT->m_config;
   m.m_iIPversion = addr.family();
   m.m_iRefCount = 1;
   m.m_iID = s->m_SocketID;

   try
   {
       m.m_pChannel = new CChannel();
       m.m_pChannel->setConfig(m.m_mcfg);

       if (udpsock)
       {
           // In this case, addr contains the address
           // that has been extracted already from the
           // given socket
           m.m_pChannel->attach(*udpsock, addr);
       }
       else if (addr.empty())
       {
           // The case of previously used case of a NULL address.
           // This here is used to pass family only, in this case
           // just automatically bind to the "0" address to autoselect
           // everything.
           m.m_pChannel->open(addr.family());
       }
       else
       {
           // If at least the IP address is specified, then bind to that
           // address, but still possibly autoselect the outgoing port, if the
           // port was specified as 0.
           m.m_pChannel->open(addr);
       }

       sockaddr_any sa;
       m.m_pChannel->getSockAddr((sa));
       m.m_iPort = sa.hport();
       s->m_SelfAddr = sa; // Will be also completed later, but here it's needed for later checks

       m.m_pTimer = new CTimer;

       m.m_pSndQueue = new CSndQueue;
       m.m_pSndQueue->init(m.m_pChannel, m.m_pTimer);
       m.m_pRcvQueue = new CRcvQueue;
       m.m_pRcvQueue->init(
               32, s->m_pUDT->maxPayloadSize(), m.m_iIPversion, 1024,
               m.m_pChannel, m.m_pTimer);

       m_mMultiplexer[m.m_iID] = m;

       s->m_pUDT->m_pSndQueue = m.m_pSndQueue;
       s->m_pUDT->m_pRcvQueue = m.m_pRcvQueue;
       s->m_iMuxID = m.m_iID;
   }
   catch (const CUDTException&)
   {
       m.destroy();
       throw;
   }
   catch (...)
   {
       m.destroy();
       throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
   }

   HLOGF(smlog.Debug, 
      "creating new multiplexer for port %i\n", m.m_iPort);
}

// This function is going to find a multiplexer for the port contained
// in the 'ls' listening socket. The multiplexer must exist when the listener
// exists, otherwise the dispatching procedure wouldn't even call this
// function. By historical reasons there's also a fallback for a case when the
// multiplexer wasn't found by id, the search by port number continues.
bool CUDTUnited::updateListenerMux(CUDTSocket* s, const CUDTSocket* ls)
{
   ScopedLock cg(m_GlobControlLock);
   const int port = ls->m_SelfAddr.hport();

   HLOGC(smlog.Debug, log << "updateListenerMux: finding muxer of listener socket @"
           << ls->m_SocketID << " muxid=" << ls->m_iMuxID
           << " bound=" << ls->m_SelfAddr.str()
           << " FOR @" << s->m_SocketID << " addr="
           << s->m_SelfAddr.str() << "_->_" << s->m_PeerAddr.str());

   // First thing that should be certain here is that there should exist
   // a muxer with the ID written in the listener socket's mux ID.

   CMultiplexer* mux = map_getp(m_mMultiplexer, ls->m_iMuxID);

   // NOTE:
   // THIS BELOW CODE is only for a highly unlikely, and probably buggy,
   // situation when the Multiplexer wasn't found by ID recorded in the listener.
   CMultiplexer* fallback = NULL;
   if (!mux)
   {
       LOGC(smlog.Error, log << "updateListenerMux: IPE? listener muxer not found by ID, trying by port");

       // To be used as first found with different IP version

       // find the listener's address
       for (map<int, CMultiplexer>::iterator i = m_mMultiplexer.begin();
               i != m_mMultiplexer.end(); ++ i)
       {
           CMultiplexer& m = i->second;

#if ENABLE_HEAVY_LOGGING
           ostringstream that_muxer;
           that_muxer << "id=" << m.m_iID
               << " port=" << m.m_iPort
               << " ip=" << (m.m_iIPversion == AF_INET ? "v4" : "v6");
#endif

           if (m.m_iPort == port)
           {
               HLOGC(smlog.Debug, log << "updateListenerMux: reusing muxer: " << that_muxer.str());
               if (m.m_iIPversion == s->m_PeerAddr.family())
               {
                   mux = &m; // best match
                   break;
               }
               else
               {
                   fallback = &m;
               }
           }
           else
           {
               HLOGC(smlog.Debug, log << "updateListenerMux: SKIPPING muxer: " << that_muxer.str());
           }
       }

       if (!mux && fallback)
       {
           // It is allowed to reuse this multiplexer, but the socket must allow both IPv4 and IPv6
           if (fallback->m_mcfg.iIpV6Only == 0)
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
       ++ mux->m_iRefCount;
       s->m_pUDT->m_pSndQueue = mux->m_pSndQueue;
       s->m_pUDT->m_pRcvQueue = mux->m_pRcvQueue;
       s->m_iMuxID = mux->m_iID;
       return true;
   }

   return false;
}

void* CUDTUnited::garbageCollect(void* p)
{
   CUDTUnited* self = (CUDTUnited*)p;

   THREAD_STATE_INIT("SRT:GC");

   UniqueLock gclock(self->m_GCStopLock);

   while (!self->m_bClosing)
   {
       INCREMENT_THREAD_ITERATIONS();
       self->checkBrokenSockets();

       HLOGC(inlog.Debug, log << "GC: sleep 1 s");
       self->m_GCStopCond.wait_for(gclock, seconds_from(1));
   }

   // remove all sockets and multiplexers
   HLOGC(inlog.Debug, log << "GC: GLOBAL EXIT - releasing all pending sockets. Acquring control lock...");

   {
       ScopedLock glock (self->m_GlobControlLock);

       for (sockets_t::iterator i = self->m_Sockets.begin();
               i != self->m_Sockets.end(); ++ i)
       {
           CUDTSocket* s = i->second;
           s->breakSocket_LOCKED();

#if ENABLE_EXPERIMENTAL_BONDING
           if (s->m_GroupOf)
           {
               HLOGC(smlog.Debug, log << "@" << s->m_SocketID << " IS MEMBER OF $" << s->m_GroupOf->id() << " (IPE?) - REMOVING FROM GROUP");
               s->removeFromGroup(false);
           }
#endif
           self->m_ClosedSockets[i->first] = s;

           // remove from listener's queue
           sockets_t::iterator ls = self->m_Sockets.find(
                   s->m_ListenSocket);
           if (ls == self->m_Sockets.end())
           {
               ls = self->m_ClosedSockets.find(s->m_ListenSocket);
               if (ls == self->m_ClosedSockets.end())
                   continue;
           }

           enterCS(ls->second->m_AcceptLock);
           ls->second->m_QueuedSockets.erase(s->m_SocketID);
           leaveCS(ls->second->m_AcceptLock);
       }
       self->m_Sockets.clear();

       for (sockets_t::iterator j = self->m_ClosedSockets.begin();
               j != self->m_ClosedSockets.end(); ++ j)
       {
           j->second->m_tsClosureTimeStamp = steady_clock::time_point();
       }
   }

   HLOGC(inlog.Debug, log << "GC: GLOBAL EXIT - releasing all CLOSED sockets.");
   while (true)
   {
      self->checkBrokenSockets();

      enterCS(self->m_GlobControlLock);
      bool empty = self->m_ClosedSockets.empty();
      leaveCS(self->m_GlobControlLock);

      if (empty)
         break;

      srt::sync::this_thread::sleep_for(milliseconds_from(1));
   }

   THREAD_EXIT();
   return NULL;
}

////////////////////////////////////////////////////////////////////////////////

int CUDT::startup()
{
   return s_UDTUnited.startup();
}

int CUDT::cleanup()
{
   return s_UDTUnited.cleanup();
}

SRTSOCKET CUDT::socket()
{
   if (!s_UDTUnited.m_bGCStatus)
      s_UDTUnited.startup();

   try
   {
      return s_UDTUnited.newSocket();
   }
   catch (const CUDTException& e)
   {
      SetThreadLocalError(e);
      return INVALID_SOCK;
   }
   catch (const bad_alloc&)
   {
      SetThreadLocalError(CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return INVALID_SOCK;
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "socket: UNEXPECTED EXCEPTION: "
         << typeid(ee).name()
         << ": " << ee.what());
      SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return INVALID_SOCK;
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

#if ENABLE_EXPERIMENTAL_BONDING
// This is an internal function; 'type' should be pre-checked if it has a correct value.
// This doesn't have argument of GroupType due to header file conflicts.

// [[using locked(s_UDTUnited.m_GlobControlLock)]]
CUDTGroup& CUDT::newGroup(const int type)
{
    const SRTSOCKET id = s_UDTUnited.generateSocketID(true);

    // Now map the group
    return s_UDTUnited.addGroup(id, SRT_GROUP_TYPE(type)).set_id(id);
}

SRTSOCKET CUDT::createGroup(SRT_GROUP_TYPE gt)
{
    // Doing the same lazy-startup as with srt_create_socket()
    if (!s_UDTUnited.m_bGCStatus)
        s_UDTUnited.startup();

    try
    {
        srt::sync::ScopedLock globlock (s_UDTUnited.m_GlobControlLock);
        return newGroup(gt).id();
        // Note: potentially, after this function exits, the group
        // could be deleted, immediately, from a separate thread (tho
        // unlikely because the other thread would need some handle to
        // keep it). But then, the first call to any API function would
        // return invalid ID error.
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (...)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0);
    }

    return SRT_INVALID_SOCK;
}


int CUDT::addSocketToGroup(SRTSOCKET socket, SRTSOCKET group)
{
    // Check if socket and group have been set correctly.
    int32_t sid = socket & ~SRTGROUP_MASK;
    int32_t gm = group & SRTGROUP_MASK;

    if ( sid != socket || gm == 0 )
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    // Find the socket and the group
    CUDTSocket* s = s_UDTUnited.locateSocket(socket);
    CUDTUnited::GroupKeeper k (s_UDTUnited, group, s_UDTUnited.ERH_RETURN);

    if (!s || !k.group)
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    // Check if the socket is already IN SOME GROUP.
    if (s->m_GroupOf)
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    CUDTGroup* g = k.group;
    if (g->managed())
    {
        // This can be changed as long as the group is empty.
        if (!g->groupEmpty())
        {
            return APIError(MJ_NOTSUP, MN_INVAL, 0);
        }
        g->set_managed(false);
    }

    ScopedLock cg (s->m_ControlLock);
    ScopedLock cglob (s_UDTUnited.m_GlobControlLock);
    if (g->closing())
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    // Check if the socket already is in the group
    srt::groups::SocketData* f;
    if (g->contains(socket, (f)))
    {
        // XXX This is internal error. Report it, but continue
        LOGC(aclog.Error, log << "IPE (non-fatal): the socket is in the group, but has no clue about it!");
        s->m_GroupMemberData = f;
        s->m_GroupOf = g;
        return 0;
    }
    s->m_GroupMemberData = g->add(srt::groups::prepareSocketData(s));
    s->m_GroupOf = g;

    return 0;
}

// dead function as for now. This is only for non-managed
// groups.
int CUDT::removeSocketFromGroup(SRTSOCKET socket)
{
    CUDTSocket* s = s_UDTUnited.locateSocket(socket);
    if (!s)
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    if (!s->m_GroupOf)
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    ScopedLock cg (s->m_ControlLock);
    ScopedLock glob_grd (s_UDTUnited.m_GlobControlLock);
    s->removeFromGroup(false);
    return 0;
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
        m_GroupOf = NULL;
        m_GroupMemberData = NULL;

        bool still_have = g->remove(m_SocketID);
        if (broken)
        {
            // Activate the SRT_EPOLL_UPDATE event on the group
            // if it was because of a socket that was earlier connected
            // and became broken. This is not to be sent in case when
            // it is a failure during connection, or the socket was
            // explicitly removed from the group.
            g->activateUpdateEvent(still_have);
        }

        HLOGC(smlog.Debug, log << "removeFromGroup: socket @" << m_SocketID << " NO LONGER A MEMBER of $" << g->id() << "; group is "
                << (still_have ? "still ACTIVE" : "now EMPTY"));
    }
}

SRTSOCKET CUDT::getGroupOfSocket(SRTSOCKET socket)
{
    // Lock this for the whole function as we need the group
    // to persist the call.
    ScopedLock glock (s_UDTUnited.m_GlobControlLock);
    CUDTSocket* s = s_UDTUnited.locateSocket_LOCKED(socket);
    if (!s || !s->m_GroupOf)
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    return s->m_GroupOf->id();
}

int CUDT::configureGroup(SRTSOCKET groupid, const char* str)
{
    if ( (groupid & SRTGROUP_MASK) == 0)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    CUDTUnited::GroupKeeper k (s_UDTUnited, groupid, s_UDTUnited.ERH_RETURN);
    if (!k.group)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    return k.group->configure(str);
}

int CUDT::getGroupData(SRTSOCKET groupid, SRT_SOCKGROUPDATA* pdata, size_t* psize)
{
    if ((groupid & SRTGROUP_MASK) == 0 || !psize)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    CUDTUnited::GroupKeeper k (s_UDTUnited, groupid, s_UDTUnited.ERH_RETURN);
    if (!k.group)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    // To get only the size of the group pdata=NULL can be used
    return k.group->getGroupData(pdata, psize);
}
#endif

int CUDT::bind(SRTSOCKET u, const sockaddr* name, int namelen)
{
   try
   {
       sockaddr_any sa (name, namelen);
       if (sa.len == 0)
       {
           // This happens if the namelen check proved it to be
           // too small for particular family, or that family is
           // not recognized (is none of AF_INET, AF_INET6).
           // This is a user error.
           return APIError(MJ_NOTSUP, MN_INVAL, 0);
       }
       CUDTSocket* s = s_UDTUnited.locateSocket(u);
       if (!s)
           return APIError(MJ_NOTSUP, MN_INVAL, 0);

       return s_UDTUnited.bind(s, sa);
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
      LOGC(aclog.Fatal, log << "bind: UNEXPECTED EXCEPTION: "
         << typeid(ee).name()
         << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::bind(SRTSOCKET u, UDPSOCKET udpsock)
{
    try
    {
        CUDTSocket* s = s_UDTUnited.locateSocket(u);
        if (!s)
            return APIError(MJ_NOTSUP, MN_INVAL, 0);

        return s_UDTUnited.bind(s, udpsock);
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
        LOGC(aclog.Fatal, log << "bind/udp: UNEXPECTED EXCEPTION: "
                << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

int CUDT::listen(SRTSOCKET u, int backlog)
{
   try
   {
      return s_UDTUnited.listen(u, backlog);
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
      LOGC(aclog.Fatal, log << "listen: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

SRTSOCKET CUDT::accept_bond(const SRTSOCKET listeners [], int lsize, int64_t msTimeOut)
{
   try
   {
      return s_UDTUnited.accept_bond(listeners, lsize, msTimeOut);
   }
   catch (const CUDTException& e)
   {
      SetThreadLocalError(e);
      return INVALID_SOCK;
   }
   catch (bad_alloc&)
   {
      SetThreadLocalError(CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return INVALID_SOCK;
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "accept_bond: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return INVALID_SOCK;
   }
}

SRTSOCKET CUDT::accept(SRTSOCKET u, sockaddr* addr, int* addrlen)
{
   try
   {
      return s_UDTUnited.accept(u, addr, addrlen);
   }
   catch (const CUDTException& e)
   {
      SetThreadLocalError(e);
      return INVALID_SOCK;
   }
   catch (const bad_alloc&)
   {
      SetThreadLocalError(CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0));
      return INVALID_SOCK;
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "accept: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return INVALID_SOCK;
   }
}

int CUDT::connect(
    SRTSOCKET u, const sockaddr* name, const sockaddr* tname, int namelen)
{
   try
   {
      return s_UDTUnited.connect(u, name, tname, namelen);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (bad_alloc&)
   {
      return APIError(MJ_SYSTEMRES, MN_MEMORY, 0);
   }
   catch (std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "connect: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

#if ENABLE_EXPERIMENTAL_BONDING
int CUDT::connectLinks(SRTSOCKET grp,
        SRT_SOCKGROUPCONFIG targets [], int arraysize)
{
    if (arraysize <= 0)
        return APIError(MJ_NOTSUP, MN_INVAL, 0);

    if ( (grp & SRTGROUP_MASK) == 0)
    {
        // connectLinks accepts only GROUP id, not socket id.
        return APIError(MJ_NOTSUP, MN_SIDINVAL, 0);
    }

    try
    {
        CUDTUnited::GroupKeeper k(s_UDTUnited, grp, s_UDTUnited.ERH_THROW);
        return s_UDTUnited.groupConnect(k.group, targets, arraysize);
    }
    catch (CUDTException& e)
    {
        return APIError(e);
    }
    catch (bad_alloc&)
    {
        return APIError(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    catch (std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "connect: UNEXPECTED EXCEPTION: "
                << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}
#endif

int CUDT::connect(
   SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn)
{
   try
   {
      return s_UDTUnited.connect(u, name, namelen, forced_isn);
   }
   catch (const CUDTException &e)
   {
      return APIError(e);
   }
   catch (bad_alloc&)
   {
      return APIError(MJ_SYSTEMRES, MN_MEMORY, 0);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "connect: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::close(SRTSOCKET u)
{
   try
   {
      return s_UDTUnited.close(u);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "close: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::getpeername(SRTSOCKET u, sockaddr* name, int* namelen)
{
   try
   {
      s_UDTUnited.getpeername(u, name, namelen);
      return 0;
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "getpeername: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::getsockname(SRTSOCKET u, sockaddr* name, int* namelen)
{
   try
   {
      s_UDTUnited.getsockname(u, name, namelen);
      return 0;
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "getsockname: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::getsockopt(
   SRTSOCKET u, int, SRT_SOCKOPT optname, void* pw_optval, int* pw_optlen)
{
    if (!pw_optval || !pw_optlen)
    {
        return APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    try
    {
#if ENABLE_EXPERIMENTAL_BONDING
        if (u & SRTGROUP_MASK)
        {
            CUDTUnited::GroupKeeper k(s_UDTUnited, u, s_UDTUnited.ERH_THROW);
            k.group->getOpt(optname, (pw_optval), (*pw_optlen));
            return 0;
        }
#endif

        CUDT* udt = s_UDTUnited.locateSocket(u, s_UDTUnited.ERH_THROW)->m_pUDT;
        udt->getOpt(optname, (pw_optval), (*pw_optlen));
        return 0;
    }
    catch (const CUDTException& e)
    {
        return APIError(e);
    }
    catch (const std::exception& ee)
    {
        LOGC(aclog.Fatal, log << "getsockopt: UNEXPECTED EXCEPTION: "
                << typeid(ee).name() << ": " << ee.what());
        return APIError(MJ_UNKNOWN, MN_NONE, 0);
    }
}

int CUDT::setsockopt(SRTSOCKET u, int, SRT_SOCKOPT optname, const void* optval, int optlen)
{
   if (!optval)
       return APIError(MJ_NOTSUP, MN_INVAL, 0);

   try
   {
#if ENABLE_EXPERIMENTAL_BONDING
       if (u & SRTGROUP_MASK)
       {
           CUDTUnited::GroupKeeper k(s_UDTUnited, u, s_UDTUnited.ERH_THROW);
           k.group->setOpt(optname, optval, optlen);
           return 0;
       }
#endif

       CUDT* udt = s_UDTUnited.locateSocket(u, s_UDTUnited.ERH_THROW)->m_pUDT;
       udt->setOpt(optname, optval, optlen);
       return 0;
   }
   catch (const CUDTException& e)
   {
       return APIError(e);
   }
   catch (const std::exception& ee)
   {
       LOGC(aclog.Fatal, log << "setsockopt: UNEXPECTED EXCEPTION: "
               << typeid(ee).name() << ": " << ee.what());
       return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::send(SRTSOCKET u, const char* buf, int len, int)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    return sendmsg2(u, buf, len, (mctrl));
}

// --> CUDT::recv moved down

int CUDT::sendmsg(
   SRTSOCKET u, const char* buf, int len, int ttl, bool inorder,
   int64_t srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    mctrl.msgttl = ttl;
    mctrl.inorder = inorder;
    mctrl.srctime = srctime;
    return sendmsg2(u, buf, len, (mctrl));
}

int CUDT::sendmsg2(
   SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL& w_m)
{
   try
   {
#if ENABLE_EXPERIMENTAL_BONDING
       if (u & SRTGROUP_MASK)
       {
           CUDTUnited::GroupKeeper k (s_UDTUnited, u, s_UDTUnited.ERH_THROW);
           return k.group->send(buf, len, (w_m));
       }
#endif

       return s_UDTUnited.locateSocket(u, CUDTUnited::ERH_THROW)->core().sendmsg2(buf, len, (w_m));
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
      LOGC(aclog.Fatal, log << "sendmsg: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::recv(SRTSOCKET u, char* buf, int len, int)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    int ret = recvmsg2(u, buf, len, (mctrl));
    return ret;
}

int CUDT::recvmsg(SRTSOCKET u, char* buf, int len, int64_t& srctime)
{
    SRT_MSGCTRL mctrl = srt_msgctrl_default;
    int ret = recvmsg2(u, buf, len, (mctrl));
    srctime = mctrl.srctime;
    return ret;
}

int CUDT::recvmsg2(SRTSOCKET u, char* buf, int len, SRT_MSGCTRL& w_m)
{
   try
   {
#if ENABLE_EXPERIMENTAL_BONDING
      if (u & SRTGROUP_MASK)
      {
          CUDTUnited::GroupKeeper k(s_UDTUnited, u, s_UDTUnited.ERH_THROW);
          return k.group->recv(buf, len, (w_m));
      }
#endif

      return s_UDTUnited.locateSocket(u, CUDTUnited::ERH_THROW)->core().recvmsg2(buf, len, (w_m));
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "recvmsg: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int64_t CUDT::sendfile(
   SRTSOCKET u, fstream& ifs, int64_t& offset, int64_t size, int block)
{
   try
   {
      CUDT* udt = s_UDTUnited.locateSocket(u, s_UDTUnited.ERH_THROW)->m_pUDT;
      return udt->sendfile(ifs, offset, size, block);
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
      LOGC(aclog.Fatal, log << "sendfile: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int64_t CUDT::recvfile(
   SRTSOCKET u, fstream& ofs, int64_t& offset, int64_t size, int block)
{
   try
   {
       return s_UDTUnited.locateSocket(u, CUDTUnited::ERH_THROW)->core().recvfile(ofs, offset, size, block);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "recvfile: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::select(
   int,
   UDT::UDSET* readfds,
   UDT::UDSET* writefds,
   UDT::UDSET* exceptfds,
   const timeval* timeout)
{
   if ((!readfds) && (!writefds) && (!exceptfds))
   {
      return APIError(MJ_NOTSUP, MN_INVAL, 0);
   }

   try
   {
      return s_UDTUnited.select(readfds, writefds, exceptfds, timeout);
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
      LOGC(aclog.Fatal, log << "select: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::selectEx(
   const vector<SRTSOCKET>& fds,
   vector<SRTSOCKET>* readfds,
   vector<SRTSOCKET>* writefds,
   vector<SRTSOCKET>* exceptfds,
   int64_t msTimeOut)
{
   if ((!readfds) && (!writefds) && (!exceptfds))
   {
      return APIError(MJ_NOTSUP, MN_INVAL, 0);
   }

   try
   {
      return s_UDTUnited.selectEx(fds, readfds, writefds, exceptfds, msTimeOut);
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
      LOGC(aclog.Fatal, log << "selectEx: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN);
   }
}

int CUDT::epoll_create()
{
   try
   {
      return s_UDTUnited.epoll_create();
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_create: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_clear_usocks(int eid)
{
   try
   {
      return s_UDTUnited.epoll_clear_usocks(eid);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_clear_usocks: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_add_usock(const int eid, const SRTSOCKET u, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_add_usock(eid, u, events);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_add_usock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_add_ssock(eid, s, events);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_add_ssock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_update_usock(
   const int eid, const SRTSOCKET u, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_add_usock(eid, u, events);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_update_usock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_update_ssock(
   const int eid, const SYSSOCKET s, const int* events)
{
   try
   {
      return s_UDTUnited.epoll_update_ssock(eid, s, events);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_update_ssock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}


int CUDT::epoll_remove_usock(const int eid, const SRTSOCKET u)
{
   try
   {
      return s_UDTUnited.epoll_remove_usock(eid, u);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_remove_usock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_remove_ssock(const int eid, const SYSSOCKET s)
{
   try
   {
      return s_UDTUnited.epoll_remove_ssock(eid, s);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_remove_ssock: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_wait(
   const int eid,
   set<SRTSOCKET>* readfds,
   set<SRTSOCKET>* writefds,
   int64_t msTimeOut,
   set<SYSSOCKET>* lrfds,
   set<SYSSOCKET>* lwfds)
{
   try
   {
      return s_UDTUnited.epoll_ref().wait(
              eid, readfds, writefds, msTimeOut, lrfds, lwfds);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_wait: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_uwait(
   const int eid,
   SRT_EPOLL_EVENT* fdsSet,
   int fdsSize,
   int64_t msTimeOut)
{
   try
   {
      return s_UDTUnited.epoll_uwait(eid, fdsSet, fdsSize, msTimeOut);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_uwait: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int32_t CUDT::epoll_set(
   const int eid,
   int32_t flags)
{
   try
   {
      return s_UDTUnited.epoll_set(eid, flags);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_set: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

int CUDT::epoll_release(const int eid)
{
   try
   {
      return s_UDTUnited.epoll_release(eid);
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "epoll_release: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

CUDTException& CUDT::getlasterror()
{
   return GetThreadLocalError();
}

int CUDT::bstats(SRTSOCKET u, CBytePerfMon* perf, bool clear, bool instantaneous)
{
#if ENABLE_EXPERIMENTAL_BONDING
   if (u & SRTGROUP_MASK)
       return groupsockbstats(u, perf, clear);
#endif

   try
   {
      CUDT* udt = s_UDTUnited.locateSocket(u, s_UDTUnited.ERH_THROW)->m_pUDT;
      udt->bstats(perf, clear, instantaneous);
      return 0;
   }
   catch (const CUDTException& e)
   {
      return APIError(e);
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "bstats: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      return APIError(MJ_UNKNOWN, MN_NONE, 0);
   }
}

#if ENABLE_EXPERIMENTAL_BONDING
int CUDT::groupsockbstats(SRTSOCKET u, CBytePerfMon* perf, bool clear)
{
   try
   {
      CUDTUnited::GroupKeeper k(s_UDTUnited, u, s_UDTUnited.ERH_THROW);
      k.group->bstatsSocket(perf, clear);
      return 0;
   }
   catch (const CUDTException& e)
   {
      SetThreadLocalError(e);
      return ERROR;
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "bstats: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return ERROR;
   }
}
#endif

CUDT* CUDT::getUDTHandle(SRTSOCKET u)
{
   try
   {
      return s_UDTUnited.locateSocket(u, s_UDTUnited.ERH_THROW)->m_pUDT;
   }
   catch (const CUDTException& e)
   {
      SetThreadLocalError(e);
      return NULL;
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "getUDTHandle: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return NULL;
   }
}

vector<SRTSOCKET> CUDT::existingSockets()
{
    vector<SRTSOCKET> out;
    for (CUDTUnited::sockets_t::iterator i = s_UDTUnited.m_Sockets.begin();
            i != s_UDTUnited.m_Sockets.end(); ++i)
    {
        out.push_back(i->first);
    }
    return out;
}

SRT_SOCKSTATUS CUDT::getsockstate(SRTSOCKET u)
{
   try
   {
#if ENABLE_EXPERIMENTAL_BONDING
      if (isgroup(u))
      {
          CUDTUnited::GroupKeeper k(s_UDTUnited, u, s_UDTUnited.ERH_THROW);
          return k.group->getStatus();
      }
#endif
      return s_UDTUnited.getStatus(u);
   }
   catch (const CUDTException& e)
   {
      SetThreadLocalError(e);
      return SRTS_NONEXIST;
   }
   catch (const std::exception& ee)
   {
      LOGC(aclog.Fatal, log << "getsockstate: UNEXPECTED EXCEPTION: "
         << typeid(ee).name() << ": " << ee.what());
      SetThreadLocalError(CUDTException(MJ_UNKNOWN, MN_NONE, 0));
      return SRTS_NONEXIST;
   }
}


////////////////////////////////////////////////////////////////////////////////

namespace UDT
{

int startup()
{
   return CUDT::startup();
}

int cleanup()
{
   return CUDT::cleanup();
}

int bind(SRTSOCKET u, const struct sockaddr* name, int namelen)
{
   return CUDT::bind(u, name, namelen);
}

int bind2(SRTSOCKET u, UDPSOCKET udpsock)
{
   return CUDT::bind(u, udpsock);
}

int listen(SRTSOCKET u, int backlog)
{
   return CUDT::listen(u, backlog);
}

SRTSOCKET accept(SRTSOCKET u, struct sockaddr* addr, int* addrlen)
{
   return CUDT::accept(u, addr, addrlen);
}

int connect(SRTSOCKET u, const struct sockaddr* name, int namelen)
{
   return CUDT::connect(u, name, namelen, SRT_SEQNO_NONE);
}

int close(SRTSOCKET u)
{
   return CUDT::close(u);
}

int getpeername(SRTSOCKET u, struct sockaddr* name, int* namelen)
{
   return CUDT::getpeername(u, name, namelen);
}

int getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen)
{
   return CUDT::getsockname(u, name, namelen);
}

int getsockopt(
   SRTSOCKET u, int level, SRT_SOCKOPT optname, void* optval, int* optlen)
{
   return CUDT::getsockopt(u, level, optname, optval, optlen);
}

int setsockopt(
   SRTSOCKET u, int level, SRT_SOCKOPT optname, const void* optval, int optlen)
{
   return CUDT::setsockopt(u, level, optname, optval, optlen);
}

// DEVELOPER API

int connect_debug(
   SRTSOCKET u, const struct sockaddr* name, int namelen, int32_t forced_isn)
{
   return CUDT::connect(u, name, namelen, forced_isn);
}

int send(SRTSOCKET u, const char* buf, int len, int flags)
{
   return CUDT::send(u, buf, len, flags);
}

int recv(SRTSOCKET u, char* buf, int len, int flags)
{
   return CUDT::recv(u, buf, len, flags);
}


int sendmsg(
   SRTSOCKET u, const char* buf, int len, int ttl, bool inorder,
   int64_t srctime)
{
   return CUDT::sendmsg(u, buf, len, ttl, inorder, srctime);
}

int recvmsg(SRTSOCKET u, char* buf, int len, int64_t& srctime)
{
   return CUDT::recvmsg(u, buf, len, srctime);
}

int recvmsg(SRTSOCKET u, char* buf, int len)
{
   int64_t srctime;
   return CUDT::recvmsg(u, buf, len, srctime);
}

int64_t sendfile(
   SRTSOCKET u,
   fstream& ifs,
   int64_t& offset,
   int64_t size,
   int block)
{
   return CUDT::sendfile(u, ifs, offset, size, block);
}

int64_t recvfile(
   SRTSOCKET u,
   fstream& ofs,
   int64_t& offset,
   int64_t size,
   int block)
{
   return CUDT::recvfile(u, ofs, offset, size, block);
}

int64_t sendfile2(
   SRTSOCKET u,
   const char* path,
   int64_t* offset,
   int64_t size,
   int block)
{
   fstream ifs(path, ios::binary | ios::in);
   int64_t ret = CUDT::sendfile(u, ifs, *offset, size, block);
   ifs.close();
   return ret;
}

int64_t recvfile2(
   SRTSOCKET u,
   const char* path,
   int64_t* offset,
   int64_t size,
   int block)
{
   fstream ofs(path, ios::binary | ios::out);
   int64_t ret = CUDT::recvfile(u, ofs, *offset, size, block);
   ofs.close();
   return ret;
}

int select(
   int nfds,
   UDSET* readfds,
   UDSET* writefds,
   UDSET* exceptfds,
   const struct timeval* timeout)
{
   return CUDT::select(nfds, readfds, writefds, exceptfds, timeout);
}

int selectEx(
   const vector<SRTSOCKET>& fds,
   vector<SRTSOCKET>* readfds,
   vector<SRTSOCKET>* writefds,
   vector<SRTSOCKET>* exceptfds,
   int64_t msTimeOut)
{
   return CUDT::selectEx(fds, readfds, writefds, exceptfds, msTimeOut);
}

int epoll_create()
{
   return CUDT::epoll_create();
}

int epoll_clear_usocks(int eid)
{
    return CUDT::epoll_clear_usocks(eid);
}

int epoll_add_usock(int eid, SRTSOCKET u, const int* events)
{
   return CUDT::epoll_add_usock(eid, u, events);
}

int epoll_add_ssock(int eid, SYSSOCKET s, const int* events)
{
   return CUDT::epoll_add_ssock(eid, s, events);
}

int epoll_update_usock(int eid, SRTSOCKET u, const int* events)
{
   return CUDT::epoll_update_usock(eid, u, events);
}

int epoll_update_ssock(int eid, SYSSOCKET s, const int* events)
{
   return CUDT::epoll_update_ssock(eid, s, events);
}

int epoll_remove_usock(int eid, SRTSOCKET u)
{
   return CUDT::epoll_remove_usock(eid, u);
}

int epoll_remove_ssock(int eid, SYSSOCKET s)
{
   return CUDT::epoll_remove_ssock(eid, s);
}

int epoll_wait(
   int eid,
   set<SRTSOCKET>* readfds,
   set<SRTSOCKET>* writefds,
   int64_t msTimeOut,
   set<SYSSOCKET>* lrfds,
   set<SYSSOCKET>* lwfds)
{
   return CUDT::epoll_wait(eid, readfds, writefds, msTimeOut, lrfds, lwfds);
}

/*

#define SET_RESULT(val, num, fds, it) \
   if (val != NULL) \
   { \
      if (val->empty()) \
      { \
         if (num) *num = 0; \
      } \
      else \
      { \
         if (*num > static_cast<int>(val->size())) \
            *num = val->size(); \
         int count = 0; \
         for (it = val->begin(); it != val->end(); ++ it) \
         { \
            if (count >= *num) \
               break; \
            fds[count ++] = *it; \
         } \
      } \
   }

*/

template <class SOCKTYPE>
inline void set_result(set<SOCKTYPE>* val, int* num, SOCKTYPE* fds)
{
    if ( !val || !num || !fds )
        return;

    if (*num > int(val->size()))
        *num = int(val->size()); // will get 0 if val->empty()
    int count = 0;

    // This loop will run 0 times if val->empty()
    for (typename set<SOCKTYPE>::const_iterator it = val->begin(); it != val->end(); ++ it)
    {
        if (count >= *num)
            break;
        fds[count ++] = *it;
    }
}

int epoll_wait2(
   int eid, SRTSOCKET* readfds,
   int* rnum, SRTSOCKET* writefds,
   int* wnum,
   int64_t msTimeOut,
   SYSSOCKET* lrfds,
   int* lrnum,
   SYSSOCKET* lwfds,
   int* lwnum)
{
   // This API is an alternative format for epoll_wait, created for
   // compatability with other languages. Users need to pass in an array
   // for holding the returned sockets, with the maximum array length
   // stored in *rnum, etc., which will be updated with returned number
   // of sockets.

   set<SRTSOCKET> readset;
   set<SRTSOCKET> writeset;
   set<SYSSOCKET> lrset;
   set<SYSSOCKET> lwset;
   set<SRTSOCKET>* rval = NULL;
   set<SRTSOCKET>* wval = NULL;
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

   int ret = CUDT::epoll_wait(eid, rval, wval, msTimeOut, lrval, lwval);
   if (ret > 0)
   {
      //set<SRTSOCKET>::const_iterator i;
      //SET_RESULT(rval, rnum, readfds, i);
      set_result(rval, rnum, readfds);
      //SET_RESULT(wval, wnum, writefds, i);
      set_result(wval, wnum, writefds);

      //set<SYSSOCKET>::const_iterator j;
      //SET_RESULT(lrval, lrnum, lrfds, j);
      set_result(lrval, lrnum, lrfds);
      //SET_RESULT(lwval, lwnum, lwfds, j);
      set_result(lwval, lwnum, lwfds);
   }
   return ret;
}

int epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut)
{
   return CUDT::epoll_uwait(eid, fdsSet, fdsSize, msTimeOut);
}

int epoll_release(int eid)
{
   return CUDT::epoll_release(eid);
}

ERRORINFO& getlasterror()
{
   return CUDT::getlasterror();
}

int getlasterror_code()
{
   return CUDT::getlasterror().getErrorCode();
}

const char* getlasterror_desc()
{
   return CUDT::getlasterror().getErrorMessage();
}

int getlasterror_errno()
{
   return CUDT::getlasterror().getErrno();
}

// Get error string of a given error code
const char* geterror_desc(int code, int err)
{
   CUDTException e (CodeMajor(code/1000), CodeMinor(code%1000), err);
   return(e.getErrorMessage());
}

int bstats(SRTSOCKET u, SRT_TRACEBSTATS* perf, bool clear)
{
   return CUDT::bstats(u, perf, clear);
}

SRT_SOCKSTATUS getsockstate(SRTSOCKET u)
{
   return CUDT::getsockstate(u);
}

} // namespace UDT

namespace srt
{

void setloglevel(LogLevel::type ll)
{
    ScopedLock gg(srt_logger_config.mutex);
    srt_logger_config.max_level = ll;
}

void addlogfa(LogFA fa)
{
    ScopedLock gg(srt_logger_config.mutex);
    srt_logger_config.enabled_fa.set(fa, true);
}

void dellogfa(LogFA fa)
{
    ScopedLock gg(srt_logger_config.mutex);
    srt_logger_config.enabled_fa.set(fa, false);
}

void resetlogfa(set<LogFA> fas)
{
    ScopedLock gg(srt_logger_config.mutex);
    for (int i = 0; i <= SRT_LOGFA_LASTNONE; ++i)
        srt_logger_config.enabled_fa.set(i, fas.count(i));
}

void resetlogfa(const int* fara, size_t fara_size)
{
    ScopedLock gg(srt_logger_config.mutex);
    srt_logger_config.enabled_fa.reset();
    for (const int* i = fara; i != fara + fara_size; ++i)
        srt_logger_config.enabled_fa.set(*i, true);
}

void setlogstream(std::ostream& stream)
{
    ScopedLock gg(srt_logger_config.mutex);
    srt_logger_config.log_stream = &stream;
}

void setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler)
{
    ScopedLock gg(srt_logger_config.mutex);
    srt_logger_config.loghandler_opaque = opaque;
    srt_logger_config.loghandler_fn = handler;
}

void setlogflags(int flags)
{
    ScopedLock gg(srt_logger_config.mutex);
    srt_logger_config.flags = flags;
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

int setrejectreason(SRTSOCKET u, int value)
{
    return CUDT::rejectReason(u, value);
}

}  // namespace srt
