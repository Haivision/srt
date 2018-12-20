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
****************************************************************************/

/****************************************************************************
written by
   Yunhong Gu, last updated 01/27/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef _WIN32
   #if __APPLE__
      #include "TargetConditionals.h"
   #endif
   #include <sys/socket.h>
   #include <sys/ioctl.h>
   #include <netdb.h>
   #include <arpa/inet.h>
   #include <unistd.h>
   #include <fcntl.h>
   #include <cstring>
   #include <cstdio>
   #include <cerrno>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <mswsock.h>
#endif

#include <iostream>
#include <iomanip> // Logging 
#include <srt_compat.h>
#include <csignal>

#include "channel.h"
#include "packet.h"
#include "api.h" // SockaddrToString - possibly move it to somewhere else
#include "logging.h"
#include "utilities.h"

#ifdef _WIN32
    typedef int socklen_t;
#endif

#ifndef _WIN32
   #define NET_ERROR errno
#else
   #define NET_ERROR WSAGetLastError()
#endif

using namespace std;


extern logging::Logger mglog;

CChannel::CChannel(int version):
m_iIPversion(version),
m_iSocket(),
#ifdef SRT_ENABLE_IPOPTS
m_iIpTTL(-1),
m_iIpToS(-1),
#endif
m_iSndBufSize(65536),
m_iRcvBufSize(65536),
m_BindAddr(version)
{
   m_iSockAddrSize = (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
}

CChannel::~CChannel()
{
}

// Windows-only version. This should apply to:
// - MICROSOFT
// - MINGW
#ifdef _WIN32
void EventRunner::init(int socket)
{
    m_fdSocket = socket;
    m_state = PSG_NONE;
    m_sockstate = 0; //no event

    m_Event[WE_TRIGGER] = WSACreateEvent();
    m_Event[WE_SOCKET] = WSACreateEvent();
    if (WSAEventSelect(m_fdSocket, m_Event[WE_SOCKET], FD_READ | FD_CLOSE) != 0)
        throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);
}

EventRunner::~EventRunner()
{
    WSACloseEvent(m_Event[WE_TRIGGER]);
    WSACloseEvent(m_Event[WE_SOCKET]);
}

void EventRunner::poll(int64_t timeout_us)
{
    // Round this time up. It has been happening that
    // the exact sleep time even though specified in [ms]
    // was actually less than the given time. If this time
    // was detected as timeout then this wakeup is spurious
    DWORD timeout_ms = (timeout_us+400)/1000;

    HLOGC(mglog.Debug, log << "poll(win32): Wait*, timeout[ms]=" << timeout_ms);
    // Waiting.
    DWORD res = WSAWaitForMultipleEvents(2, m_Event, false, timeout_ms, false);
    if (res == WSA_WAIT_FAILED)
    {
        // Sanity check. This error is reported only in case of
        // a network shutdown or invalid argument. For any case,
        // report readiness of the socket so that the reading
        // happens and fails.
        LOGC(mglog.Error, log << "poll(WIN32): IPE: Error in Wait");
        m_sockstate = 2;
        m_permstate = PSG_CLOSE;
        return;
    }

    if (res == WSA_WAIT_TIMEOUT)
    {
        HLOGC(mglog.Debug, log << "poll(win32): timeout reached");
        // Timeout. Reset readiness on all events.
        m_permstate = PSG_NONE;
        m_sockstate = 0;
        return;
    }

    int eventx = res - WSA_WAIT_EVENT_0;
    HLOGC(mglog.Debug, log << "poll(win32): readiness: "
            << (eventx == WE_TRIGGER ? "TRIGGER" : eventx == WE_SOCKET ? "SOCKET" : "???UNKNOWN???")
            << " (" << eventx << ")");

    // Extra checks.
    // 1. If the returned value represents m_Event[WE_TRIGGER],
    //    rewrite m_permstate. This shall not be changed until this call.
    //    Note that the signalReading function sets m_state.
    if (eventx == WE_TRIGGER)
    {
        WSAResetEvent(m_Event[WE_TRIGGER]);
        m_permstate = m_state;
        // Although continue with checking if an event
        // occurred also on a socket. Both might have happened
        // at once. Therefore if this WASN'T the socket event,
        // clear the socket event, or otherwise this function
        // would exit immediately when called next time even
        // though the event was understood and cleared.
    }

    // 2. If the returned value represents m_Event[WE_SOCKET],
    //    additionally use WSAEnumNetworkEvents to check what
    //    happened and rewrite it into m_sockstate accordingly:
    //    1 - read ready, 2 - error.

    // Remaining is WE_SOCKET, but don't check it,
    // and test the socket anyway. When a socket readiness
    // is reported, but the trigger is also ready, it will have
    // to wait for the next time.

    WSANETWORKEVENTS nev;
    int resi = WSAEnumNetworkEvents(m_fdSocket, m_Event[WE_SOCKET], &nev);
    if (resi) // 0 == success
    {
        LOGC(mglog.Error, log << "poll(win32): IPE: Error in EnumNetworkEvents");
        m_sockstate = 2;
        WSAResetEvent(m_Event[WE_SOCKET]);
        return;
    }

    if (!nev.lNetworkEvents)
    {
        HLOGC(mglog.Debug, log << "WSAEnumNetworkEvents: no events on socket");
        m_sockstate = 0;
        return;
    }

    HLOGC(mglog.Debug, log << "poll(win32): socket readiness flags: " << hex << nev.lNetworkEvents
            << " check for read=" << FD_READ << " close=" << FD_CLOSE);

    WSAResetEvent(m_Event[WE_SOCKET]);

    if (nev.lNetworkEvents & FD_CLOSE)
    {
        // Close event - exit with error
        m_sockstate = 2;
    }
    else if (nev.lNetworkEvents & FD_READ)
    {
        // Check if this reading ended up with error,
        // if so, return 2.
        if (nev.iErrorCode[FD_READ_BIT] != 0)
        {
            HLOGC(mglog.Debug, log << "poll(win32): socket read error flag: " << nev.iErrorCode[FD_READ_BIT]);
            m_sockstate = 2;
        }
        else
        {
            m_sockstate = 1;
        }
    }
}

int EventRunner::socketReady() const
{
    return m_sockstate;
}

int EventRunner::signalReading(PipeSignal val) const
{
    // Setting this value as an intermediate passing storage.
    // This will be picked up by EventRunner::poll as this
    // shall cause immediate exit in WSAWaitForMultipleEvents().
    m_state = val;
    if (WSASetEvent(m_Event[WE_TRIGGER]))
        return 0;
    return WSAGetLastError();
}

PipeSignal EventRunner::clearSignalReading()
{
    WSAResetEvent(m_Event[WE_TRIGGER]);

    // The state that was rewritten once poll() returned.
    PipeSignal laststate = m_permstate;
    m_state = PSG_NONE;
    m_permstate = PSG_NONE;
    return laststate;
}

// POSIX/fallback version. This applies to all systems
// that can use ::select with both pipes and sockets.
#else
void EventRunner::init(int socket)
{
    m_fdSocket = socket;

    if (::pipe(m_fdTrigger) == -1)
        throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);

    int opts = ::fcntl(m_fdTrigger[PIPE_IN], F_GETFL);
    if (opts == -1 || ::fcntl(m_fdTrigger[PIPE_IN], F_SETFL, opts | O_NONBLOCK) == -1)
        throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);
}

EventRunner::~EventRunner()
{
    ::close(m_fdTrigger[PIPE_IN]);
    ::close(m_fdTrigger[PIPE_OUT]);
}

void EventRunner::poll(int64_t timeout_us)
{
    FD_ZERO(&in_set);
    FD_ZERO(&err_set);

    // Add the UDP socket
    FD_SET(m_fdSocket, &in_set);
    FD_SET(m_fdSocket, &err_set);
    FD_SET(m_fdTrigger[PIPE_IN], &in_set);
    int nsockets = std::max(m_fdSocket, m_fdTrigger[PIPE_IN]) + 1;
    timeval tv;
    tv.tv_usec = timeout_us;
    tv.tv_sec = 0;

    if (::select(nsockets, &in_set, NULL, &err_set, &tv) == -1)
    {
        LOGC(mglog.Error, log << "EventRunner::poll: IPE: select:"
                << SysStrError(NET_ERROR));
        // But continue the work.
    }
}

int EventRunner::socketReady() const
{
    return FD_ISSET(m_fdSocket, &in_set) + 2*FD_ISSET(m_fdSocket, &err_set);
}

int EventRunner::signalReading(PipeSignal val) const
{
    char data[2] = { val, 0 }; // could be 0, but this sounds bad.
    return ::write(m_fdTrigger[PIPE_OUT], data, 1);
}

PipeSignal EventRunner::clearSignalReading()
{
    int value = -1;

    if (!FD_ISSET(m_fdTrigger[PIPE_IN], &in_set))
        return PSG_NONE;

    char onebyte[2];

    // Ignore errors. This is in order to purge
    // the pipe of the data that were stuffed into it just
    // to cause a signal on the select that is waiting for
    // read-readiness of the UDP socket.
    int ret = ::read(m_fdTrigger[PIPE_IN], onebyte, 1);
    if (ret == -1)
        return PSG_NONE;

    value = onebyte[0];
    return PipeSignal(value);
}

#endif

void CChannel::open(const sockaddr* addr)
{
    // construct an socket
    m_iSocket = ::socket(m_iIPversion, SOCK_DGRAM, 0);

#ifdef _WIN32
    const int error = INVALID_SOCKET;
#else
    const int error = -1;
#endif
    if (m_iSocket == error)
        throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);

    m_EventRunner.init(m_iSocket);

    if (NULL != addr)
    {
        socklen_t namelen = m_iSockAddrSize;

        if (0 != ::bind(m_iSocket, addr, namelen))
            throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
        memcpy(&m_BindAddr, addr, namelen);
        m_BindAddr.len = namelen;
    }
    else
    {
        //sendto or WSASendTo will also automatically bind the socket
        addrinfo hints;
        addrinfo* res;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = m_iIPversion;
        hints.ai_socktype = SOCK_DGRAM;

        if (0 != ::getaddrinfo(NULL, "0", &hints, &res))
            throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);

        if (0 != ::bind(m_iSocket, res->ai_addr, res->ai_addrlen))
            throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
        memcpy(&m_BindAddr, res->ai_addr, res->ai_addrlen);
        m_BindAddr.len = res->ai_addrlen;

        ::freeaddrinfo(res);
    }

    HLOGC(mglog.Debug, log << "CHANNEL: Bound to local address: " << SockaddrToString(&m_BindAddr));

    setUDPSockOpt();
}

void CChannel::attach(UDPSOCKET udpsock)
{
   m_iSocket = udpsock;
   setUDPSockOpt();
}

void CChannel::setUDPSockOpt()
{
   #if defined(BSD) || defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
      // BSD system will fail setsockopt if the requested buffer size exceeds system maximum value
      int maxsize = 64000;
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char*)&m_iRcvBufSize, sizeof(int)))
         ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char*)&maxsize, sizeof(int));
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char*)&m_iSndBufSize, sizeof(int)))
         ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char*)&maxsize, sizeof(int));
   #else
      // for other systems, if requested is greated than maximum, the maximum value will be automactally used
      if ((0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char*)&m_iRcvBufSize, sizeof(int))) ||
          (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char*)&m_iSndBufSize, sizeof(int))))
         throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
   #endif

#ifdef SRT_ENABLE_IPOPTS
      if (-1 != m_iIpTTL)
      {
         if(m_iIPversion == AF_INET)
         {
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (const char*)&m_iIpTTL, sizeof(m_iIpTTL)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         }
         else //Assuming AF_INET6
         {
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (const char*)&m_iIpTTL, sizeof(m_iIpTTL)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         }
      }   
      if (-1 != m_iIpToS)
      {
         if(m_iIPversion == AF_INET)
         {
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (const char*)&m_iIpToS, sizeof(m_iIpToS)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         }
         else //Assuming AF_INET6
         {
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_TCLASS, (const char*)&m_iIpToS, sizeof(m_iIpToS)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         }
      }
#endif


#ifdef UNIX
   // Set non-blocking I/O
   // UNIX does not support SO_RCVTIMEO
   int opts = ::fcntl(m_iSocket, F_GETFL);
   if (-1 == ::fcntl(m_iSocket, F_SETFL, opts | O_NONBLOCK))
      throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#elif defined(_WIN32)
   u_long nonBlocking = 1;
   if (0 != ioctlsocket (m_iSocket, FIONBIO, &nonBlocking))
      throw CUDTException (MJ_SETUP, MN_NORES, NET_ERROR);
#else
   timeval tv;
   tv.tv_sec = 0;
#if defined (BSD) || defined (OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
   // Known BSD bug as the day I wrote this code.
   // A small time out value will cause the socket to block forever.
   tv.tv_usec = 10000;
#else
   tv.tv_usec = 100;
#endif
   // Set receiving time-out value
   if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(timeval)))
      throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#endif
}

void CChannel::close() const
{
    int ret SRT_ATR_UNUSED = m_EventRunner.signalReading(PSG_CLOSE);
    HLOGC(mglog.Debug, log << "CChannel::close: sendt TRIGGER signal first, ret=" << ret);

    // Need to clear out the socket variable because this function
    // isn't a destructor.
#ifndef _WIN32
    ::close(m_iSocket);
#else
    ::closesocket(m_iSocket);
#endif

}

int CChannel::getSndBufSize()
{
   socklen_t size = sizeof(socklen_t);
   ::getsockopt(m_iSocket, SOL_SOCKET, SO_SNDBUF, (char *)&m_iSndBufSize, &size);
   return m_iSndBufSize;
}

int CChannel::getRcvBufSize()
{
   socklen_t size = sizeof(socklen_t);
   ::getsockopt(m_iSocket, SOL_SOCKET, SO_RCVBUF, (char *)&m_iRcvBufSize, &size);
   return m_iRcvBufSize;
}

void CChannel::setSndBufSize(int size)
{
   m_iSndBufSize = size;
}

void CChannel::setRcvBufSize(int size)
{
   m_iRcvBufSize = size;
}

#ifdef SRT_ENABLE_IPOPTS
int CChannel::getIpTTL() const
{
   socklen_t size = sizeof(m_iIpTTL);
   if (m_iIPversion == AF_INET)
   {
      ::getsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (char *)&m_iIpTTL, &size);
   }
   else
   {
      ::getsockopt(m_iSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (char *)&m_iIpTTL, &size);
   }
   return m_iIpTTL;
}

int CChannel::getIpToS() const
{
   socklen_t size = sizeof(m_iIpToS);
   if(m_iIPversion == AF_INET)
   {
      ::getsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (char *)&m_iIpToS, &size);
   }
   else
   {
      ::getsockopt(m_iSocket, IPPROTO_IPV6, IPV6_TCLASS, (char *)&m_iIpToS, &size);
   }
   return m_iIpToS;
}

void CChannel::setIpTTL(int ttl)
{
   m_iIpTTL = ttl;
}

void CChannel::setIpToS(int tos)
{
   m_iIpToS = tos;
}

#endif

int CChannel::ioctlQuery(int type) const
{
#ifdef unix
    int value = 0;
    int res = ::ioctl(m_iSocket, type, &value);
    if ( res != -1 )
        return value;
#endif
    return -1;
}

int CChannel::sockoptQuery(int level, int option) const
{
#ifdef unix
    int value = 0;
    socklen_t len = sizeof (int);
    int res = ::getsockopt(m_iSocket, level, option, &value, &len);
    if ( res != -1 )
        return value;
#endif
    return -1;
}

void CChannel::getSockAddr(sockaddr* addr) const
{
   socklen_t namelen = m_iSockAddrSize;
   ::getsockname(m_iSocket, addr, &namelen);
}

void CChannel::getPeerAddr(sockaddr* addr) const
{
   socklen_t namelen = m_iSockAddrSize;
   ::getpeername(m_iSocket, addr, &namelen);
}


int CChannel::sendto(const sockaddr* addr, CPacket& packet) const
{
#if ENABLE_HEAVY_LOGGING
    std::ostringstream spec;

    if (packet.isControl())
    {
        spec << " CONTROL size=" << packet.getLength()
             << " cmd=" << MessageTypeStr(packet.getType(), packet.getExtendedType())
             << " arg=" << packet.getHeader()[CPacket::PH_MSGNO];
    }
    else
    {
        spec << " DATA size=" << packet.getLength()
             << " seq=" << packet.getSeqNo();
        if (packet.getRexmitFlag())
            spec << " [REXMIT]";
    }

    LOGC(mglog.Debug, log << "CChannel::sendto: SENDING NOW DST=" << SockaddrToString(addr)
        << " target=%" << packet.m_iID
        << spec.str());
#endif

   // convert control information into network order
   // XXX USE HtoNLA!
   if (packet.isControl())
      for (ptrdiff_t i = 0, n = packet.getLength() / 4; i < n; ++ i)
         *((uint32_t *)packet.m_pcData + i) = htonl(*((uint32_t *)packet.m_pcData + i));

   // convert packet header into network order
   //for (int j = 0; j < 4; ++ j)
   //   packet.m_nHeader[j] = htonl(packet.m_nHeader[j]);
   uint32_t* p = packet.m_nHeader;
   for (int j = 0; j < 4; ++ j)
   {
      *p = htonl(*p);
      ++ p;
   }

   #ifndef _WIN32
      msghdr mh;
      mh.msg_name = (sockaddr*)addr;
      mh.msg_namelen = m_iSockAddrSize;
      mh.msg_iov = (iovec*)packet.m_PacketVector;
      mh.msg_iovlen = 2;
      mh.msg_control = NULL;
      mh.msg_controllen = 0;
      mh.msg_flags = 0;

      int res = ::sendmsg(m_iSocket, &mh, 0);
   #else
      DWORD size = DWORD(CPacket::HDR_SIZE + packet.getLength());
      int addrsize = m_iSockAddrSize;
      int res = ::WSASendTo(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, 0, addr, addrsize, NULL, NULL);
      res = (0 == res) ? size : -1;
   #endif

   // convert back into local host order
   //for (int k = 0; k < 4; ++ k)
   //   packet.m_nHeader[k] = ntohl(packet.m_nHeader[k]);
   p = packet.m_nHeader;
   for (int k = 0; k < 4; ++ k)
   {
      *p = ntohl(*p);
       ++ p;
   }

   if (packet.isControl())
   {
      for (ptrdiff_t l = 0, n = packet.getLength() / 4; l < n; ++ l)
         *((uint32_t *)packet.m_pcData + l) = ntohl(*((uint32_t *)packet.m_pcData + l));
   }

   return res;
}

EReadStatus CChannel::sys_recvmsg(ref_t<CPacket> r_pkt, ref_t<int> r_result, ref_t<int> r_msg_flags, sockaddr* addr) const
{
    CPacket& packet = *r_pkt;

#ifndef _WIN32  // POSIX/fallback version

    msghdr mh;
    mh.msg_name = addr;
    mh.msg_namelen = m_iSockAddrSize;
    mh.msg_iov = packet.m_PacketVector;
    mh.msg_iovlen = 2;
    mh.msg_control = NULL;
    mh.msg_controllen = 0;
    mh.msg_flags = 0;

    *r_result = ::recvmsg(m_iSocket, &mh, 0);
    *r_msg_flags = mh.msg_flags;

    // Note that there are exactly four groups of possible errors
    // reported by recvmsg():

    // 1. Temporary error, can't get the data, but you can try again.
    // Codes: EAGAIN/EWOULDBLOCK, EINTR, ECONNREFUSED
    // Return: RST_AGAIN.
    //
    // 2. Problems that should never happen due to unused configurations.
    // Codes: ECONNREFUSED, ENOTCONN
    // Return: RST_ERROR, just formally treat this as IPE.
    //
    // 3. Unexpected runtime errors:
    // Codes: EINVAL, EFAULT, ENOMEM, ENOTSOCK
    // Return: RST_ERROR. Except ENOMEM, this can only be an IPE. ENOMEM
    // should make the program stop as lacking memory will kill the program anyway soon.
    //
    // 4. Expected socket closed in the meantime by another thread.
    // Codes: EBADF
    // Return: RST_ERROR. This will simply make the worker thread exit, which is
    // expected to happen after CChannel::close() is called by another thread.

    if (*r_result == -1)
    {
        const int err = NET_ERROR;
        if (err == EAGAIN || err == EINTR || err == ECONNREFUSED) // For EAGAIN, this isn't an error, just a useless call.
        {
            return RST_AGAIN;
        }

        HLOGC(mglog.Debug, log << CONID() << "(sys)recvmsg: " << SysStrError(err) << " [" << err << "]");
        return RST_ERROR;
    }

    return RST_OK;

#else // WIN32 version

    // XXX REFACTORING NEEDED!
    // This procedure uses the WSARecvFrom function that just reads
    // into one buffer. On Windows, the equivalent for recvmsg, WSARecvMsg
    // uses the equivalent of msghdr - WSAMSG, which has different field
    // names and also uses the equivalet of iovec - WSABUF, which has different
    // field names and layout. It is important that this code be translated
    // to the "proper" solution, however this requires that CPacket::m_PacketVector
    // also uses the "platform independent" (or, better, platform-suitable) type
    // which can be appropriate for the appropriate system function, not just iovec
    // (see a specifically provided definition for iovec for windows in packet.h).
    //
    // For the time being, the msg_flags variable is defined in both cases
    // so that it can be checked independently, however it won't have any other
    // value one Windows than 0, unless this procedure below is rewritten
    // to use WSARecvMsg().

    int& res = *r_result;
    int& msg_flags = *r_msg_flags;

    DWORD size = CPacket::HDR_SIZE + packet.getLength();
    DWORD flag = 0;
    int addrsize = m_iSockAddrSize;

    msg_flags = 0;
    int sockerror = ::WSARecvFrom(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, &flag, addr, &addrsize, NULL, NULL);
    if (sockerror == 0)
    {
        res = size;
    }
    else // == SOCKET_ERROR
    {
        EReadStatus status;
        res = -1;
        // On Windows this is a little bit more complicated, so simply treat every error
        // as an "again" situation. This should still be probably fixed, but it needs more
        // thorough research. For example, the problem usually reported from here is
        // WSAETIMEDOUT, which isn't mentioned in the documentation of WSARecvFrom at all.
        //
        // These below errors are treated as "fatal", all others are treated as "again".
        static const int fatals [] =
        {
            WSAEFAULT,
            WSAEINVAL,
            WSAENETDOWN,
            WSANOTINITIALISED,
            WSA_OPERATION_ABORTED
        };
        static const int* fatals_end = fatals + Size(fatals);
        int err = NET_ERROR;
        if (std::find(fatals, fatals_end, err) != fatals_end)
        {
            HLOGC(mglog.Debug, log << CONID() << "(sys)WSARecvFrom: " << SysStrError(err) << " [" << err << "]");
            status = RST_ERROR;
        }
        else
        {
            status = RST_AGAIN;
        }

        return status;
    }

    // Not sure if this problem has ever occurred on Windows, just a sanity check.
    if (flag & MSG_PARTIAL)
        msg_flags = 1;

    return RST_OK;
#endif
}

EReadStatus CChannel::recvfrom(sockaddr* addr, CPacket& packet, uint64_t uptime_us) const
{
    EReadStatus status = RST_OK;

    // uptime_us specifies the absolute time to wait. select uses
    // relative time. As absolute time, it might happen that the
    // uptime is in the past.

    uint64_t currtime_tk;
    CTimer::rdtsc(currtime_tk);
    uint64_t now = CTimer::tk2us(currtime_tk);
    uint64_t timeout_us;

    // The maximum time for poll is defined as 0.5s, so
    // we know it can't exceed 0.99s.
    if (uptime_us)
    {
        if (uptime_us <= now)
            timeout_us = 0; // the time is already in the past, don't wait.
        else
        {
            // Don't wait longer than 0.5s, even if this somehow results
            // from calculations.
            timeout_us = std::min(uptime_us - now, MAX_POLL_TIME_US);
        }
    }
    else
    {
        // Waiting forever is risky, so in case of no timeout
        // simply wait longer time - 0.5s
        timeout_us = MAX_POLL_TIME_US;
    }

    HLOGC(mglog.Debug, log << "CChannel::recvfrom: poll for event: usec=" << timeout_us);

    m_EventRunner.poll(timeout_us);

    int psg = m_EventRunner.clearSignalReading();
    int socket_ready = m_EventRunner.socketReady();

#if ENABLE_HEAVY_LOGGING
    {
        // The last one is rather impossible, but it is a potential value in the code.
        static const char* type [] = {"none", "read", "error", "read+error"};
        static const char* psgname [] = {"none", "close", "newunit", "newconn"};

        const char* sock_state = type[socket_ready];
        const char* signal_state = psgname[psg+1];

        LOGC(mglog.Debug, log << "CChannel::recvfrom: poll-ready: socket="
                << sock_state << " trigger=" << signal_state);
    }

#endif

    // If the select DID NOT report read-readiness of the UDP socket,
    // do not call the ::recvmsg function - it's useless and won't read
    // anything, but it will still take some CPU.
    if (!socket_ready || psg == PSG_CLOSE )
    {
        HLOGC(mglog.Debug, log << "CChannel::recvfrom: socket "
                << (psg == PSG_CLOSE ? "TO BE CLOSED" : "not ready")
                << ", possibly trigger or timeout. NOT READING.");
        status = psg == PSG_CLOSE ? RST_ERROR : RST_AGAIN;
        goto Return_error;
    }
    // NOTE: allowed to call reading in case when select reported error.
    // This should probably repeat this same error when ::recvfrom is called.
    // The handler of the error will decide what to do with this. It is not
    // expected that the error be reported every time at the call.

    int res, msg_flags;
    status = sys_recvmsg(Ref(packet), Ref(res), Ref(msg_flags), addr);

    // Sanity check for a case when it didn't fill in even the header
    if (size_t(res) < CPacket::HDR_SIZE)
    {
        status = RST_AGAIN;
        HLOGC(mglog.Debug, log << CONID() << "POSSIBLE ATTACK: received too short packet with " << res << " bytes");
        goto Return_error;
    }

    // Fix for an issue with Linux Kernel found during tests at Tencent.
    //
    // There was a bug in older Linux Kernel which caused that when the internal
    // buffer was depleted during reading from the network, not the whole buffer
    // was copied from the packet, EVEN THOUGH THE GIVEN BUFFER WAS OF ENOUGH SIZE.
    // It was still very kind of the buggy procedure, though, that at least
    // they inform the caller about that this has happened by setting MSG_TRUNC
    // flag.
    //
    // Normally this flag should be set only if there was too small buffer given
    // by the caller, so as this code knows that the size is enough, it never
    // predicted this to happen. Just for a case then when you run this on a buggy
    // system that suffers of this problem, the fix for this case is left here.
    //
    // When this happens, then you have at best a fragment of the buffer and it's
    // useless anyway. This is solved by dropping the packet and fake that no
    // packet was received, so the packet will be then retransmitted.
    if ( msg_flags != 0 )
    {
        HLOGC(mglog.Debug, log << CONID() << "NET ERROR: packet size=" << res
            << " msg_flags=0x" << hex << msg_flags << ", possibly MSG_TRUNC (0x" << hex << int(MSG_TRUNC) << ")");
        status = RST_AGAIN;
        goto Return_error;
    }

    packet.setLength(res - CPacket::HDR_SIZE);

    // convert back into local host order
    // XXX use NtoHLA().
    {
        uint32_t* p = packet.m_nHeader;
        for (size_t i = 0; i < CPacket::PH_SIZE; ++ i)
        {
            *p = ntohl(*p);
            ++ p;
        }
    }

    if (packet.isControl())
    {
        for (size_t j = 0, n = packet.getLength() / sizeof (uint32_t); j < n; ++ j)
            *((uint32_t *)packet.m_pcData + j) = ntohl(*((uint32_t *)packet.m_pcData + j));
    }

    return RST_OK;

Return_error:
    packet.setLength(-1);
    return status;
}
