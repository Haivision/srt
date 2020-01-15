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
Copyright (c) 2001 - 2010, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 09/28/2010
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef __UDT_API_H__
#define __UDT_API_H__


#include <map>
#include <vector>
#include <string>
#include "netinet_any.h"
#include "udt.h"
#include "packet.h"
#include "queue.h"
#include "cache.h"
#include "epoll.h"
#include "handshake.h"

class CUDT;

class CUDTSocket
{
public:
   CUDTSocket():
       m_Status(SRTS_INIT),
       m_SocketID(0),
       m_ListenSocket(0),
       m_PeerID(0),
       m_iISN(0),
       m_pUDT(NULL),
       m_pQueuedSockets(NULL),
       m_pAcceptSockets(NULL),
       m_AcceptCond(),
       m_AcceptLock(),
       m_uiBackLog(0),
       m_iMuxID(-1)
   {
       construct();
   }

   ~CUDTSocket();

   void construct();

   SRT_SOCKSTATUS m_Status;                  //< current socket state

   /// Time when the socket is closed.
   /// When the socket is closed, it is not removed immediately from the list
   /// of sockets in order to prevent other methods from accessing invalid address.
   /// A timer is started and the socket will be removed after approximately
   /// 1 second (see CUDTUnited::checkBrokenSockets()).
   srt::sync::steady_clock::time_point m_tsClosureTimeStamp;

   sockaddr_any m_SelfAddr;                    //< local address of the socket
   sockaddr_any m_PeerAddr;                    //< peer address of the socket

   SRTSOCKET m_SocketID;                     //< socket ID
   SRTSOCKET m_ListenSocket;                 //< ID of the listener socket; 0 means this is an independent socket

   SRTSOCKET m_PeerID;                       //< peer socket ID
   int32_t m_iISN;                           //< initial sequence number, used to tell different connection from same IP:port

   CUDT* m_pUDT;                             //< pointer to the UDT entity

   std::set<SRTSOCKET>* m_pQueuedSockets;    //< set of connections waiting for accept()
   std::set<SRTSOCKET>* m_pAcceptSockets;    //< set of accept()ed connections

   srt::sync::CCondition m_AcceptCond;              //< used to block "accept" call
   srt::sync::CMutex m_AcceptLock;             //< mutex associated to m_AcceptCond

   unsigned int m_uiBackLog;                 //< maximum number of connections in queue

   int m_iMuxID;                             //< multiplexer ID

   srt::sync::CMutex m_ControlLock;            //< lock this socket exclusively for control APIs: bind/listen/connect

   static int64_t getPeerSpec(SRTSOCKET id, int32_t isn)
   {
       return (id << 30) + isn;
   }
   int64_t getPeerSpec()
   {
       return getPeerSpec(m_PeerID, m_iISN);
   }

   SRT_SOCKSTATUS getStatus();

   // This function shall be called always wherever
   // you'd like to call cudtsocket->m_pUDT->close().
   void makeClosed();

    // Instrumentally used by select() and also required for non-blocking
    // mode check in groups
    bool readReady();
    bool writeReady();
    bool broken();

private:
   CUDTSocket(const CUDTSocket&);
   CUDTSocket& operator=(const CUDTSocket&);
};

////////////////////////////////////////////////////////////////////////////////

class CUDTUnited
{
friend class CUDT;
friend class CRendezvousQueue;

public:
   CUDTUnited();
   ~CUDTUnited();

public:

   enum ErrorHandling { ERH_RETURN, ERH_THROW, ERH_ABORT };
   static std::string CONID(SRTSOCKET sock);

      /// initialize the UDT library.
      /// @return 0 if success, otherwise -1 is returned.

   int startup();

      /// release the UDT library.
      /// @return 0 if success, otherwise -1 is returned.

   int cleanup();

      /// Create a new UDT socket.
      /// @return The new UDT socket ID, or INVALID_SOCK.

   SRTSOCKET newSocket();

      /// Create a new UDT connection.
      /// @param [in] listen the listening UDT socket;
      /// @param [in] peer peer address.
      /// @param [in,out] hs handshake information from peer side (in), negotiated value (out);
      /// @return If the new connection is successfully created: 1 success, 0 already exist, -1 error.

   int newConnection(const SRTSOCKET listen, const sockaddr_any& peer, const CPacket& hspkt,
           CHandShake& w_hs, SRT_REJECT_REASON& w_error);

   int installAcceptHook(const SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq);

      /// Check the status of the UDT socket.
      /// @param [in] u the UDT socket ID.
      /// @return UDT socket status, or NONEXIST if not found.

   SRT_SOCKSTATUS getStatus(const SRTSOCKET u);

      // socket APIs

   int bind(CUDTSocket* u, const sockaddr_any& name);
   int bind(CUDTSocket* u, UDPSOCKET udpsock);
   int listen(const SRTSOCKET u, int backlog);
   SRTSOCKET accept(const SRTSOCKET listen, sockaddr* addr, int* addrlen);
   int connect(SRTSOCKET u, const sockaddr* srcname, int srclen, const sockaddr* tarname, int tarlen);
   int connect(const SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn);
   int connectIn(CUDTSocket* s, const sockaddr_any& target, int32_t forced_isn);
   int close(const SRTSOCKET u);
   int close(CUDTSocket* s);
   void getpeername(const SRTSOCKET u, sockaddr* name, int* namelen);
   void getsockname(const SRTSOCKET u, sockaddr* name, int* namelen);
   int select(ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout);
   int selectEx(const std::vector<SRTSOCKET>& fds, std::vector<SRTSOCKET>* readfds, std::vector<SRTSOCKET>* writefds, std::vector<SRTSOCKET>* exceptfds, int64_t msTimeOut);
   int epoll_create();
   int epoll_add_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
   int epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
   int epoll_remove_usock(const int eid, const SRTSOCKET u);
   int epoll_remove_ssock(const int eid, const SYSSOCKET s);
   int epoll_update_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
   int epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
   int epoll_wait(const int eid, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* lwfds = NULL);
   int epoll_uwait(const int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
   int32_t epoll_set(const int eid, int32_t flags);
   int epoll_release(const int eid);

      /// record the UDT exception.
      /// @param [in] e pointer to a UDT exception instance.

   void setError(CUDTException* e);

      /// look up the most recent UDT exception.
      /// @return pointer to a UDT exception instance.

   CUDTException* getError();

private:
//   void init();

   SRTSOCKET generateSocketID();

private:
   typedef std::map<SRTSOCKET, CUDTSocket*> sockets_t;       // stores all the socket structures
   sockets_t m_Sockets;

   srt::sync::CMutex m_GlobControlLock;                // used to synchronize UDT API

   srt::sync::CMutex m_IDLock;                         // used to synchronize ID generation

   static const int32_t MAX_SOCKET_VAL = 1 << 29;    // maximum value for a regular socket

   SRTSOCKET m_SocketIDGenerator;                    // seed to generate a new unique socket ID
   SRTSOCKET m_SocketIDGenerator_init;               // Keeps track of the very first one

   std::map<int64_t, std::set<SRTSOCKET> > m_PeerRec;// record sockets from peers to avoid repeated connection request, int64_t = (socker_id << 30) + isn

private:
   pthread_key_t m_TLSError;                         // thread local error record (last error)
   static void TLSDestroy(void* e) {if (NULL != e) delete (CUDTException*)e;}

private:
   CUDTSocket* locateSocket(SRTSOCKET u, ErrorHandling erh = ERH_RETURN);
   CUDTSocket* locatePeer(const sockaddr_any& peer, const SRTSOCKET id, int32_t isn);
   void updateMux(CUDTSocket* s, const sockaddr_any& addr, const UDPSOCKET* = NULL);
   void updateListenerMux(CUDTSocket* s, const CUDTSocket* ls);

private:
   std::map<int, CMultiplexer> m_mMultiplexer;		// UDP multiplexer
   srt::sync::CMutex m_MultiplexerLock;

private:
   CCache<CInfoBlock>* m_pCache;			// UDT network information cache

private:
   volatile bool m_bClosing;
   srt::sync::CMutex m_GCStopLock;
   srt::sync::CCondition m_GCStopCond;

   srt::sync::CMutex m_InitLock;
   int m_iInstanceCount;				// number of startup() called by application
   bool m_bGCStatus;					// if the GC thread is working (true)

   pthread_t m_GCThread;
   static void* garbageCollect(void*);

   sockets_t m_ClosedSockets;   // temporarily store closed sockets

   void checkBrokenSockets();
   void removeSocket(const SRTSOCKET u);

   CEPoll m_EPoll;                                     // handling epoll data structures and events

private:
   CUDTUnited(const CUDTUnited&);
   CUDTUnited& operator=(const CUDTUnited&);
};

// Debug support
inline std::string SockaddrToString(const sockaddr_any& sadr)
{
    if (sadr.family() != AF_INET && sadr.family() != AF_INET6)
        return "unknown:0";

    std::ostringstream output;
    char hostbuf[1024];
    int flags;

#if ENABLE_GETNAMEINFO
    flags = NI_NAMEREQD;
#else
    flags = NI_NUMERICHOST | NI_NUMERICSERV;
#endif

    if (!getnameinfo(sadr.get(), sadr.size(), hostbuf, 1024, NULL, 0, flags))
    {
        output << hostbuf;
    }

    output << ":" << sadr.hport();
    return output.str();
}


#endif
