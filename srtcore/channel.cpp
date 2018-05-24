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

#ifndef WIN32
   #if __APPLE__
      #include "TargetConditionals.h"
   #endif
   #include <sys/socket.h>
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

#include "channel.h"
#include "packet.h"
#include "logging.h"

#ifdef WIN32
    typedef int socklen_t;
#endif

#ifndef WIN32
   #define NET_ERROR errno
#else
   #define NET_ERROR WSAGetLastError()
#endif

using namespace std;


extern logging::Logger mglog;

CChannel::CChannel():
m_iIPversion(AF_INET),
m_iSockAddrSize(sizeof(sockaddr_in)),
m_iSocket(),
#ifdef SRT_ENABLE_IPOPTS
m_iIpTTL(-1),
m_iIpToS(-1),
#endif
m_iSndBufSize(65536),
m_iRcvBufSize(65536)
{
}

CChannel::CChannel(int version):
m_iIPversion(version),
m_iSocket(),
#ifdef SRT_ENABLE_IPOPTS
m_iIpTTL(-1),
m_iIpToS(-1),
#endif
m_iSndBufSize(65536),
m_iRcvBufSize(65536)
{
   m_iSockAddrSize = (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
}

CChannel::~CChannel()
{
}

void CChannel::open(const sockaddr* addr)
{
   // construct an socket
   m_iSocket = ::socket(m_iIPversion, SOCK_DGRAM, 0);

   #ifdef WIN32
      if (INVALID_SOCKET == m_iSocket)
   #else
      if (m_iSocket < 0)
   #endif
      throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);

   if (NULL != addr)
   {
      socklen_t namelen = m_iSockAddrSize;

      if (0 != ::bind(m_iSocket, addr, namelen))
         throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
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

      ::freeaddrinfo(res);
   }

   setUDPSockOpt();
}

void CChannel::open(UDPSOCKET udpsock)
{
   m_iSocket = udpsock;
   setUDPSockOpt();
}

void CChannel::setUDPSockOpt()
{
   #if defined(BSD) || defined(OSX) || defined(TARGET_OS_IOS) || defined(TARGET_OS_TV)
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
      if ((-1 != m_iIpTTL)
      &&  (0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (const char*)&m_iIpTTL, sizeof(m_iIpTTL))))
         throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         
      if ((-1 != m_iIpToS)
      &&  (0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (const char*)&m_iIpToS, sizeof(m_iIpToS))))
         throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#endif

   timeval tv;
   tv.tv_sec = 0;
   #if defined (BSD) || defined (OSX) || defined(TARGET_OS_IOS) || defined(TARGET_OS_TV)
      // Known BSD bug as the day I wrote this code.
      // A small time out value will cause the socket to block forever.
      tv.tv_usec = 10000;
   #else
      tv.tv_usec = 100;
   #endif

   #ifdef UNIX
      // Set non-blocking I/O
      // UNIX does not support SO_RCVTIMEO
      int opts = ::fcntl(m_iSocket, F_GETFL);
      if (-1 == ::fcntl(m_iSocket, F_SETFL, opts | O_NONBLOCK))
         throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
   #elif defined(WIN32)
      DWORD ot = 1; //milliseconds
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&ot, sizeof(DWORD)))
         throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
   #else
      // Set receiving time-out value
      if (0 != ::setsockopt(m_iSocket, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(timeval)))
         throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
   #endif
}

void CChannel::close() const
{
   #ifndef WIN32
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
   ::getsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (char *)&m_iIpTTL, &size);
   return m_iIpTTL;
}

int CChannel::getIpToS() const
{
   socklen_t size = sizeof(m_iIpToS);
   ::getsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (char *)&m_iIpToS, &size);
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
   // convert control information into network order
   if (packet.isControl())
      for (int i = 0, n = packet.getLength() / 4; i < n; ++ i)
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

   #ifndef WIN32
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
      DWORD size = CPacket::HDR_SIZE + packet.getLength();
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
      for (int l = 0, n = packet.getLength() / 4; l < n; ++ l)
         *((uint32_t *)packet.m_pcData + l) = ntohl(*((uint32_t *)packet.m_pcData + l));
   }

   return res;
}

int CChannel::recvfrom(sockaddr* addr, CPacket& packet) const
{
#ifndef WIN32
    msghdr mh;   
    mh.msg_name = addr;
    mh.msg_namelen = m_iSockAddrSize;
    mh.msg_iov = packet.m_PacketVector;
    mh.msg_iovlen = 2;
    mh.msg_control = NULL;
    mh.msg_controllen = 0;
    mh.msg_flags = 0;

#ifdef UNIX
    fd_set set;
    timeval tv;
    FD_ZERO(&set);
    FD_SET(m_iSocket, &set);
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    ::select(m_iSocket+1, &set, NULL, &set, &tv);
#endif

    int res = ::recvmsg(m_iSocket, &mh, 0);
    int msg_flags = mh.msg_flags;
#else
    // XXX This procedure uses the WSARecvFrom function that just reads
    // into one buffer. On Windows, the equivalent for recvmsg, WSARecvMsg
    // uses the equivalent of msghdr - WSAMSG, which has different field
    // names and also uses the equivalet of iovec - WSABUF, which has different
    // field names and layout. It is important that this code be translated
    // to the "proper" solution, however this requires that CPacket::m_PacketVector
    // also uses the "platform independent" (or, better, platform-suitable) type
    // which can be appropriate for the appropriate system function, not just iovec.
    //
    // For the time being, the msg_flags variable is defined in both cases
    // so that it can be checked independently, however it won't have any other
    // value one Windows than 0, unless this procedure below is rewritten
    // to use WSARecvMsg().

    DWORD size = CPacket::HDR_SIZE + packet.getLength();
    DWORD flag = 0;
    int addrsize = m_iSockAddrSize;

    int res = ::WSARecvFrom(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, &flag, addr, &addrsize, NULL, NULL);
    res = (0 == res) ? size : -1;
    int msg_flags = 0;
#endif

    // These logs are theoretically errors, but this isn't anything problematic
    // for the application, and in certain conditions they can be spit out very
    // often and therefore influence the processing time.
    if ( res == -1 )
    {
#if ENABLE_LOGGING
        int err = NET_ERROR;
        if ( err != EAGAIN ) // For EAGAIN, this isn't an error, just a useless call.
        {
            LOGC(mglog.Debug) << CONID() << "(sys)recvmsg: " << SysStrError(err) << " [" << err << "]";
        }
#endif
        goto Return_error;
    }

    // Sanity check for a case when it didn't fill in even the header
    if ( size_t(res) < CPacket::HDR_SIZE )
    {
        LOGC(mglog.Debug) << CONID() << "POSSIBLE ATTACK: received too short packet with " << res << " bytes";
        goto Return_error;
    }

    // Fix for an issue found at Tenecent.
    // By some not well known reason, Linux kernel happens to copy only 20 bytes of
    // UDP payload and set the MSG_TRUNC flag, whereas pcap shows that full UDP
    // packet arrived at the network device, and the free space in a buffer is
    // always the same and >1332 bytes. Nice of it to set this flag, though.
    //
    // In normal conditions, no flags should be set. This shouldn't use any
    // other flags, but OTOH this situation also theoretically shouldn't happen
    // and it does. As a safe precaution, simply treat any flag set on the
    // messate as "some problem".
    //
    // As a response for this situation, fake that you received no package. This will be
    // then a "fake drop", which will result in reXmission. This isn't even much of a fake
    // because the packet is partially lost and this loss is irrecoverable.

    if ( msg_flags != 0 )
    {
        LOGC(mglog.Debug) << CONID() << "NET ERROR: packet size=" << res
            << " msg_flags=0x" << hex << msg_flags << ", possibly MSG_TRUNC (0x" << hex << int(MSG_TRUNC) << ")";
        goto Return_error;
    }

    packet.setLength(res - CPacket::HDR_SIZE);

    // convert back into local host order
    //for (int i = 0; i < 4; ++ i)
    //   packet.m_nHeader[i] = ntohl(packet.m_nHeader[i]);
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

    return packet.getLength();

Return_error:
    packet.setLength(-1);
    return -1;
}
