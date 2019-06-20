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

/*
#ifndef _WIN32
   #if __APPLE__
      #define __APPLE_USE_RFC_3542
      //#define __POSIX_C_SOURCE
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
*/

#include "platform_sys.h"

#include <iostream>
#include <iomanip> // Logging 
#include <srt_compat.h>
#include <csignal>

#include "channel.h"
#include "packet.h"
#include "api.h" // SockaddrToString - possibly move it to somewhere else
#include "logging.h"
#include "netinet_any.h"
#include "utilities.h"

#ifdef _WIN32
    typedef int socklen_t;
#endif

using namespace std;
using namespace srt_logging;

CChannel::CChannel():
m_iSocket(),
#ifdef SRT_ENABLE_IPOPTS
m_iIpTTL(-1),   /* IPv4 TTL or IPv6 HOPs [1..255] (-1:undefined) */
m_iIpToS(-1),   /* IPv4 Type of Service or IPv6 Traffic Class [0x00..0xff] (-1:undefined) */
#endif
m_iSndBufSize(65536),
m_iRcvBufSize(65536),
m_iIpV6Only(-1)
#ifdef SRT_ENABLE_PKTINFO
, m_bBindMasked(true) // By default the socket is bound to ANY
#endif
{

#ifdef SRT_ENABLE_PKTINFO
   // Do the check for ancillary data buffer size, kinda assertion
   static const size_t CMSG_MAX_SPACE = sizeof (CMSGNodeAlike);

   if (CMSG_MAX_SPACE < CMSG_SPACE(sizeof(in_pktinfo))
           || CMSG_MAX_SPACE < CMSG_SPACE(sizeof(in6_pktinfo)))
   {
       LOGC(mglog.Fatal, log << "Size of CMSG_MAX_SPACE="
               << CMSG_MAX_SPACE << " too short for cmsg "
               << CMSG_SPACE(sizeof(in_pktinfo)) << ", "
               << CMSG_SPACE(sizeof(in6_pktinfo)) << " - PLEASE FIX");
       throw CUDTException(MJ_SETUP, MN_NONE, 0);
   }
#endif

}

CChannel::~CChannel()
{
}

void CChannel::createSocket(int family)
{
    // construct an socket
    m_iSocket = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);

   #ifdef _WIN32
    int invalid = INVALID_SOCKET;
#else
    int invalid = -1;
#endif

    if (m_iSocket == invalid)
        throw CUDTException(MJ_SETUP, MN_NONE, NET_ERROR);

   if ((m_iIpV6Only != -1) && (family == AF_INET6)) // (not an error if it fails)
      ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)(&m_iIpV6Only), sizeof(m_iIpV6Only));

}

void CChannel::open(const sockaddr_any& addr)
{
    createSocket(addr.family());
    socklen_t namelen = addr.size();

    if (::bind(m_iSocket, &addr.sa, namelen) == -1)
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);

    m_BindAddr = addr;
#ifdef SRT_ENABLE_PKTINFO
    m_bBindMasked = m_BindAddr.isany();
#endif
    LOGC(mglog.Debug, log << "CHANNEL: Bound to local address: " << SockaddrToString(m_BindAddr));

    setUDPSockOpt();
}

void CChannel::open(int family)
{
    createSocket(family);

    //sendto or WSASendTo will also automatically bind the socket
    addrinfo hints;
    addrinfo* res;

    memset(&hints, 0, sizeof(struct addrinfo));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;

    int eai = ::getaddrinfo(NULL, "0", &hints, &res);
    if (eai != 0)
    {
        // Controversial a little bit because this function occasionally
        // doesn't use errno (here: NET_ERROR for portability), instead
        // it returns 0 if succeeded or an error code. This error code
        // is passed here then. A controversy is around the fact that
        // the receiver of this error has completely no ability to know
        // what this error code's domain is, and it definitely isn't
        // the same as for errno.
        throw CUDTException(MJ_SETUP, MN_NORES, eai);
    }

      // On Windows ai_addrlen has type size_t (unsigned), while bind takes int.
      if (0 != ::bind(m_iSocket, res->ai_addr, (socklen_t) res->ai_addrlen))
        throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);

    m_BindAddr = sockaddr_any(res->ai_addr, res->ai_addrlen);

#ifdef SRT_ENABLE_PKTINFO
    // We know that this is intentionally bound now to "any",
    // so the requester-destination address must be remembered and passed.
    m_bBindMasked = true;
#endif

    ::freeaddrinfo(res);

    HLOGC(mglog.Debug, log << "CHANNEL: Bound to local address: " << SockaddrToString(m_BindAddr));

   setUDPSockOpt();
}

void CChannel::attach(int udpsock, const sockaddr_any& udpsocks_addr)
{
    // The getsockname() call is done before calling it and the
    // result is placed into udpsocks_addr.
    m_iSocket = udpsock;
    m_BindAddr = udpsocks_addr;
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
         if(m_BindAddr.family() == AF_INET)
         {
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (const char*)&m_iIpTTL, sizeof(m_iIpTTL)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         }
         else //Assuming AF_INET6
         {
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (const char*)&m_iIpTTL, sizeof(m_iIpTTL)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
            //For IPv4mapped-IPv6 accepted connection also set the IPV4 socket.
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (const char*)&m_iIpTTL, sizeof(m_iIpTTL)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         }
      }   
      if (-1 != m_iIpToS)
      {
         if(m_BindAddr.family() == AF_INET)
         {
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (const char*)&m_iIpToS, sizeof(m_iIpToS)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
         }
         else //Assuming AF_INET6
         {
#ifdef IPV6_TCLASS
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_TCLASS, (const char*)&m_iIpToS, sizeof(m_iIpToS)))
               throw CUDTException(MJ_SETUP, MN_NORES, NET_ERROR);
#endif
            //For IPv4mapped-IPv6 accepted connection also set the IPV4 socket.
            if(0 != ::setsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (const char*)&m_iIpToS, sizeof(m_iIpToS)))
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

#ifdef SRT_ENABLE_PKTINFO
    if (m_bBindMasked)
    {
        HLOGP(mglog.Debug, "Socket bound to ANY - setting PKTINFO for address retrieval");
        int on = 1, off = 0;
        ::setsockopt(m_iSocket, IPPROTO_IP, IP_PKTINFO, (char*)&on, sizeof(on));
        ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on));
        ::setsockopt(m_iSocket, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    }
#endif
}

void CChannel::close() const
{
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

void CChannel::setIpV6Only(int ipV6Only) 
{
   m_iIpV6Only = ipV6Only;
}

#ifdef SRT_ENABLE_IPOPTS
int CChannel::getIpTTL() const
{
   socklen_t size = sizeof(m_iIpTTL);
   if (m_BindAddr.family() == AF_INET)
   {
      ::getsockopt(m_iSocket, IPPROTO_IP, IP_TTL, (char *)&m_iIpTTL, &size);
   }
   else if (m_BindAddr.family() == AF_INET6)
   {
      ::getsockopt(m_iSocket, IPPROTO_IPV6, IPV6_UNICAST_HOPS, (char *)&m_iIpTTL, &size);
   }
   else
   {
       // If family is unspecified, the socket probably doesn't exist.
       return -1;
   }
   return m_iIpTTL;
}

int CChannel::getIpToS() const
{
   socklen_t size = sizeof(m_iIpToS);
   if(m_BindAddr.family() == AF_INET)
   {
      ::getsockopt(m_iSocket, IPPROTO_IP, IP_TOS, (char *)&m_iIpToS, &size);
   }
   else if (m_BindAddr.family() == AF_INET6)
   {
#ifdef IPV6_TCLASS
      ::getsockopt(m_iSocket, IPPROTO_IPV6, IPV6_TCLASS, (char *)&m_iIpToS, &size);
#endif
   }
   else
   {
       // If family is unspecified, the socket probably doesn't exist.
       return -1;
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
#if defined(unix) || defined(__APPLE__)
    int value = 0;
    int res = ::ioctl(m_iSocket, type, &value);
    if ( res != -1 )
        return value;
#endif
    return -1;
}

int CChannel::sockoptQuery(int level, int option) const
{
#if defined(unix) || defined(__APPLE__)
    int value = 0;
    socklen_t len = sizeof (int);
    int res = ::getsockopt(m_iSocket, level, option, &value, &len);
    if ( res != -1 )
        return value;
#endif
    return -1;
}

void CChannel::getSockAddr(ref_t<sockaddr_any> addr) const
{
    // The getsockname function requires only to have enough target
    // space to copy the socket name, it doesn't have to be corelated
    // with the address family. So the maximum space for any name,
    // regardless of the family, does the job.
    socklen_t namelen = sizeof(addr.get());
    ::getsockname(m_iSocket, &addr.get(), &namelen);
}

void CChannel::getPeerAddr(ref_t<sockaddr_any> addr) const
{
    socklen_t namelen = sizeof(addr.get());
    ::getpeername(m_iSocket, &addr.get(), &namelen);
}


int CChannel::sendto(const sockaddr_any& addr, CPacket& packet, const sockaddr_any& source_addr SRT_ATR_UNUSED) const
{
    HLOGC(mglog.Debug, log << "CChannel::sendto: SENDING NOW DST=" << SockaddrToString(addr)
        << " target=@" << packet.m_iID
#ifdef SRT_ENABLE_PKTINFO
        << " sourceIP="
        << (m_bBindMasked && !source_addr.isany() ? SockaddrToString(source_addr) : "default")
#endif
        << " " << packet.Info());

   // convert control information into network order
   // XXX USE HtoNLA!
   if (packet.isControl())
      for (ptrdiff_t i = 0, n = (ptrdiff_t) packet.getLength() / 4; i < n; ++i)
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
      mh.msg_name = (sockaddr*)&addr;
      mh.msg_namelen = addr.size();
      mh.msg_iov = (iovec*)packet.m_PacketVector;
      mh.msg_iovlen = 2;
      bool have_set_src = false;

#ifdef SRT_ENABLE_PKTINFO
      if (m_bBindMasked && !source_addr.isany())
      {
          if ( !setSourceAddress(mh, source_addr))
          {
              LOGC(mglog.Error, log << "CChannel::setSourceAddress: source address invalid family #" << source_addr.family() << ", NOT setting.");
          }
          else
          {
              HLOGC(mglog.Debug, log << "CChannel::setSourceAddress: setting as " << SockaddrToString(source_addr));
              have_set_src = true;
          }
      }
#endif

      if (!have_set_src)
      {
          mh.msg_control = NULL;
          mh.msg_controllen = 0;
      }
      mh.msg_flags = 0;

      int res = ::sendmsg(m_iSocket, &mh, 0);
   #else
      DWORD size = (DWORD) (CPacket::HDR_SIZE + packet.getLength());
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

EReadStatus CChannel::recvfrom(ref_t<sockaddr_any> r_addr, CPacket& packet) const
{
    EReadStatus status = RST_OK;
    sockaddr* addr = &r_addr.get();
    int msg_flags = 0;
    int recv_size = -1;

#if defined(UNIX) || defined(_WIN32)
    fd_set set;
    timeval tv;
    FD_ZERO(&set);
    FD_SET(m_iSocket, &set);
    tv.tv_sec  = 0;
    tv.tv_usec = 10000;
    const int select_ret = ::select((int) m_iSocket + 1, &set, NULL, &set, &tv);
#else
    const int select_ret = 1;   // the socket is expected to be in the blocking mode itself
#endif

    if (select_ret == 0)   // timeout
    {
        packet.setLength(-1);
        return RST_AGAIN;
    }

#ifndef _WIN32
    msghdr mh; // will not be used on failure

    if (select_ret > 0)
    {
        mh.msg_name = addr;
        mh.msg_namelen = r_addr.get().size();
        mh.msg_iov = packet.m_PacketVector;
        mh.msg_iovlen = 2;

#ifdef SRT_ENABLE_PKTINFO
        if (!m_bBindMasked)
        {
            // We don't need ancillary data - the source address
            // will always be the bound address.
            mh.msg_control = NULL;
            mh.msg_controllen = 0;
        }
        else
        {
            // Extract the destination IP address from the ancillary
            // data. This might be interesting for the connection to
            // know to which address the packet should be sent back during
            // the handshake and then addressed when sending during connection.
            mh.msg_control = m_acCmsgRecvBuffer;
            mh.msg_controllen = sizeof m_acCmsgRecvBuffer;
        }
#else
        mh.msg_control = NULL;
        mh.msg_controllen = 0;
#endif

        mh.msg_flags = 0;


        recv_size = ::recvmsg(m_iSocket, &mh, 0);
        msg_flags = mh.msg_flags;
    }

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

    // We do not handle <= SOCKET_ERROR as they are handled further by checking the recv_size
    if (select_ret == -1 || recv_size == -1)
    {
        const int err = NET_ERROR;
        if (err == EAGAIN || err == EINTR || err == ECONNREFUSED) // For EAGAIN, this isn't an error, just a useless call.
        {
            status = RST_AGAIN;
        }
        else
        {
            HLOGC(mglog.Debug, log << CONID() << "(sys)recvmsg: " << SysStrError(err) << " [" << err << "]");
            status = RST_ERROR;
        }

        goto Return_error;
    }

#ifdef SRT_ENABLE_PKTINFO
    if (m_bBindMasked)
    {
        // Extract the address. Set it explicitly; if this returns address that isany(),
        // it will simply set this on the packet so that it behaves as if nothing was
        // extracted (it will "fail the old way").
        packet.m_DestAddr = getTargetAddress(mh);
        HLOGC(mglog.Debug, log << CONID() << "(sys)recvmsg: ANY BOUND, retrieved DEST ADDR: " << SockaddrToString(packet.m_DestAddr));
    }
#endif
#else
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

    int recv_ret = SOCKET_ERROR;
    DWORD flag = 0;

    if (select_ret > 0)     // the total number of socket handles that are ready
    {
        DWORD size = (DWORD) (CPacket::HDR_SIZE + packet.getLength());
        int addrsize = m_iSockAddrSize;

        recv_ret = ::WSARecvFrom(m_iSocket, (LPWSABUF)packet.m_PacketVector, 2, &size, &flag, addr, &addrsize, NULL, NULL);
        if (recv_ret == 0)
            recv_size = size;
    }

    // We do not handle <= SOCKET_ERROR as they are handled further by checking the recv_size
    if (select_ret == SOCKET_ERROR || recv_ret == SOCKET_ERROR) // == SOCKET_ERROR
    {
        recv_size = -1;
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
        const int err = NET_ERROR;
        if (std::find(fatals, fatals_end, err) != fatals_end)
        {
            HLOGC(mglog.Debug, log << CONID() << "(sys)WSARecvFrom: " << SysStrError(err) << " [" << err << "]");
            status = RST_ERROR;
        }
        else
        {
            status = RST_AGAIN;
        }

        goto Return_error;
    }

    // Not sure if this problem has ever occurred on Windows, just a sanity check.
    if (flag & MSG_PARTIAL)
        msg_flags = 1;
#endif


    // Sanity check for a case when it didn't fill in even the header
    if (size_t(recv_size) < CPacket::HDR_SIZE)
    {
        status = RST_AGAIN;
        HLOGC(mglog.Debug, log << CONID() << "POSSIBLE ATTACK: received too short packet with " << recv_size << " bytes");
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
        HLOGC(mglog.Debug, log << CONID() << "NET ERROR: packet size=" << recv_size
            << " msg_flags=0x" << hex << msg_flags << ", possibly MSG_TRUNC (0x" << hex << int(MSG_TRUNC) << ")");
        status = RST_AGAIN;
        goto Return_error;
    }

    packet.setLength(recv_size - CPacket::HDR_SIZE);

    // convert back into local host order
    // XXX use NtoHLA().
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

    return RST_OK;

Return_error:
    packet.setLength(-1);
    return status;
}
