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
Copyright (c) 2001 - 2016, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 07/25/2010
modified by
   Haivision Systems Inc.
*****************************************************************************/

#define SRT_IMPORT_TIME 1
#include "platform_sys.h"

#include <string>
#include <sstream>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <vector>
#include "udt.h"
#include "md5.h"
#include "common.h"
#include "netinet_any.h"
#include "logging.h"
#include "packet.h"
#include "threadname.h"

#include <srt_compat.h> // SysStrError

using namespace srt::sync;


CUDTException::CUDTException(CodeMajor major, CodeMinor minor, int err):
m_iMajor(major),
m_iMinor(minor)
{
   if (err == -1)
       m_iErrno = NET_ERROR;
   else
      m_iErrno = err;
}

const char* CUDTException::getErrorMessage() const ATR_NOTHROW
{
    return getErrorString().c_str();
}

const string& CUDTException::getErrorString() const
{
   // translate "Major:Minor" code into text message.

   switch (m_iMajor)
   {
      case MJ_SUCCESS:
        m_strMsg = "Success";
        break;

      case MJ_SETUP:
        m_strMsg = "Connection setup failure";

        switch (m_iMinor)
        {
        case MN_TIMEOUT:
           m_strMsg += ": connection time out";
           break;

        case MN_REJECTED:
           m_strMsg += ": connection rejected";
           break;

        case MN_NORES:
           m_strMsg += ": unable to create/configure SRT socket";
           break;

        case MN_SECURITY:
           m_strMsg += ": abort for security reasons";
           break;

        case MN_CLOSED:
           m_strMsg += ": socket closed during operation";
           break;

        default:
           break;
        }

        break;

      case MJ_CONNECTION:
        switch (m_iMinor)
        {
        case MN_CONNLOST:
           m_strMsg = "Connection was broken";
           break;

        case MN_NOCONN:
           m_strMsg = "Connection does not exist";
           break;

        default:
           break;
        }

        break;

      case MJ_SYSTEMRES:
        m_strMsg = "System resource failure";

        switch (m_iMinor)
        {
        case MN_THREAD:
           m_strMsg += ": unable to create new threads";
           break;

        case MN_MEMORY:
           m_strMsg += ": unable to allocate buffers";
           break;


        case MN_OBJECT:
           m_strMsg += ": unable to allocate system object";
           break;

        default:
           break;
        }

        break;

      case MJ_FILESYSTEM:
        m_strMsg = "File system failure";

        switch (m_iMinor)
        {
        case MN_SEEKGFAIL:
           m_strMsg += ": cannot seek read position";
           break;

        case MN_READFAIL:
           m_strMsg += ": failure in read";
           break;

        case MN_SEEKPFAIL:
           m_strMsg += ": cannot seek write position";
           break;

        case MN_WRITEFAIL:
           m_strMsg += ": failure in write";
           break;

        default:
           break;
        }

        break;

      case MJ_NOTSUP:
        m_strMsg = "Operation not supported";
 
        switch (m_iMinor)
        {
        case MN_ISBOUND:
           m_strMsg += ": Cannot do this operation on a BOUND socket";
           break;

        case MN_ISCONNECTED:
           m_strMsg += ": Cannot do this operation on a CONNECTED socket";
           break;

        case MN_INVAL:
           m_strMsg += ": Bad parameters";
           break;

        case MN_SIDINVAL:
           m_strMsg += ": Invalid socket ID";
           break;

        case MN_ISUNBOUND:
           m_strMsg += ": Cannot do this operation on an UNBOUND socket";
           break;

        case MN_NOLISTEN:
           m_strMsg += ": Socket is not in listening state";
           break;

        case MN_ISRENDEZVOUS:
           m_strMsg += ": Listen/accept is not supported in rendezous connection setup";
           break;

        case MN_ISRENDUNBOUND:
           m_strMsg += ": Cannot call connect on UNBOUND socket in rendezvous connection setup";
           break;

        case MN_INVALMSGAPI:
           m_strMsg += ": Incorrect use of Message API (sendmsg/recvmsg).";
           break;

        case MN_INVALBUFFERAPI:
           m_strMsg += ": Incorrect use of Buffer API (send/recv) or File API (sendfile/recvfile).";
           break;

        case MN_BUSY:
           m_strMsg += ": Another socket is already listening on the same port";
           break;

        case MN_XSIZE:
           m_strMsg += ": Message is too large to send (it must be less than the SRT send buffer size)";
           break;

        case MN_EIDINVAL:
           m_strMsg += ": Invalid epoll ID";
           break;

        case MN_EEMPTY:
           m_strMsg += ": All sockets removed from epoll, waiting would deadlock";

        default:
           break;
        }

        break;

     case MJ_AGAIN:
        m_strMsg = "Non-blocking call failure";

        switch (m_iMinor)
        {
        case MN_WRAVAIL:
           m_strMsg += ": no buffer available for sending";
           break;

        case MN_RDAVAIL:
           m_strMsg += ": no data available for reading";
           break;

        case MN_XMTIMEOUT:
           m_strMsg += ": transmission timed out";
           break;

#ifdef SRT_ENABLE_ECN
        case MN_CONGESTION:
           m_strMsg += ": early congestion notification";
           break;
#endif /* SRT_ENABLE_ECN */
        default:
           break;
        }

        break;

     case MJ_PEERERROR:
        m_strMsg = "The peer side has signalled an error";

        break;

      default:
        m_strMsg = "Unknown error";
   }

   // Adding "errno" information
   if ((MJ_SUCCESS != m_iMajor) && (0 < m_iErrno))
   {
      m_strMsg += ": " + SysStrError(m_iErrno);
   }

   return m_strMsg;
}

#define UDT_XCODE(mj, mn) (int(mj)*1000)+int(mn)

int CUDTException::getErrorCode() const
{
    return UDT_XCODE(m_iMajor, m_iMinor);
}

int CUDTException::getErrno() const
{
   return m_iErrno;
}


void CUDTException::clear()
{
   m_iMajor = MJ_SUCCESS;
   m_iMinor = MN_NONE;
   m_iErrno = 0;
}

#undef UDT_XCODE

//
bool CIPAddress::ipcmp(const sockaddr* addr1, const sockaddr* addr2, int ver)
{
   if (AF_INET == ver)
   {
      sockaddr_in* a1 = (sockaddr_in*)addr1;
      sockaddr_in* a2 = (sockaddr_in*)addr2;

      if ((a1->sin_port == a2->sin_port) && (a1->sin_addr.s_addr == a2->sin_addr.s_addr))
         return true;
   }
   else
   {
      sockaddr_in6* a1 = (sockaddr_in6*)addr1;
      sockaddr_in6* a2 = (sockaddr_in6*)addr2;

      if (a1->sin6_port == a2->sin6_port)
      {
         for (int i = 0; i < 16; ++ i)
            if (*((char*)&(a1->sin6_addr) + i) != *((char*)&(a2->sin6_addr) + i))
               return false;

         return true;
      }
   }

   return false;
}

void CIPAddress::ntop(const sockaddr_any& addr, uint32_t ip[4])
{
    if (addr.family() == AF_INET)
    {
        ip[0] = addr.sin.sin_addr.s_addr;
    }
    else
    {
      const sockaddr_in6* a = &addr.sin6;
      ip[3] = (a->sin6_addr.s6_addr[15] << 24) + (a->sin6_addr.s6_addr[14] << 16) + (a->sin6_addr.s6_addr[13] << 8) + a->sin6_addr.s6_addr[12];
      ip[2] = (a->sin6_addr.s6_addr[11] << 24) + (a->sin6_addr.s6_addr[10] << 16) + (a->sin6_addr.s6_addr[9] << 8) + a->sin6_addr.s6_addr[8];
      ip[1] = (a->sin6_addr.s6_addr[7] << 24) + (a->sin6_addr.s6_addr[6] << 16) + (a->sin6_addr.s6_addr[5] << 8) + a->sin6_addr.s6_addr[4];
      ip[0] = (a->sin6_addr.s6_addr[3] << 24) + (a->sin6_addr.s6_addr[2] << 16) + (a->sin6_addr.s6_addr[1] << 8) + a->sin6_addr.s6_addr[0];
    }
}

// XXX This has void return and the first argument is passed by reference.
// Consider simply returning sockaddr_any by value.
void CIPAddress::pton(sockaddr_any& w_addr, const uint32_t ip[4], int agent_family, const sockaddr_any& peer)
{
   if (agent_family == AF_INET)
   {
      sockaddr_in* a = (&w_addr.sin);
      a->sin_addr.s_addr = ip[0];
   }
   else // AF_INET6
   {
      // Check if the peer address is a model of IPv4-mapped-on-IPv6.
      // If so, it means that the `ip` array should be interpreted as IPv4.

      uint16_t* peeraddr16 = (uint16_t*)peer.sin6.sin6_addr.s6_addr;
      static const uint16_t ipv4on6_model [8] =
      {
          0, 0, 0, 0, 0, 0xFFFF, 0, 0
      };

      // Compare only first 6 words. Remaining 2 words
      // comprise the IPv4 address, if these first 6 match.
      const uint16_t* mbegin = ipv4on6_model;
      const uint16_t* mend = ipv4on6_model + 6;

      bool is_mapped_ipv4 = (std::mismatch(mbegin, mend, peeraddr16).first == mend);

      sockaddr_in6* a = (&w_addr.sin6);

      if (!is_mapped_ipv4)
      {
          // Here both agent and peer use IPv6, in which case
          // `ip` contains the full IPv6 address, so just copy
          // it as is.

          // XXX Possibly, a simple
          // memcpy( (a->sin6_addr.s6_addr), ip, 16);
          // would do the same thing, and faster. The address in `ip`,
          // even though coded here as uint32_t, is still big endian.
          for (int i = 0; i < 4; ++ i)
          {
              a->sin6_addr.s6_addr[i * 4 + 0] = ip[i] & 0xFF;
              a->sin6_addr.s6_addr[i * 4 + 1] = (unsigned char)((ip[i] & 0xFF00) >> 8);
              a->sin6_addr.s6_addr[i * 4 + 2] = (unsigned char)((ip[i] & 0xFF0000) >> 16);
              a->sin6_addr.s6_addr[i * 4 + 3] = (unsigned char)((ip[i] & 0xFF000000) >> 24);
          }
      }
      else // IPv4 mapped on IPv6
      {
          // Here agent uses IPv6 with IPPROTO_IPV6/IPV6_V6ONLY == 0
          // In this case, the address in `ip` uses only the `ip[0]`
          // element carrying the IPv4 address, rest of the elements
          // is 0. This address must be interpreted as IPv4, but set
          // as the IPv4-on-IPv6 address.
          //
          // Unfortunately, sockaddr_in6 doesn't give any straightforward
          // method for it, although the official size of a single element
          // of the IPv6 address is 16-bit.

          memset((a->sin6_addr.s6_addr), 0, sizeof a->sin6_addr.s6_addr);

          // There are also available sin6_addr.s6_addr32, but
          // this is kinda nonportable.
          uint32_t* paddr32 = (uint32_t*)a->sin6_addr.s6_addr;
          uint16_t* paddr16 = (uint16_t*)a->sin6_addr.s6_addr;

          // layout: of IPv4 address 192.168.128.2
          // 16-bit:
          // [0000: 0000: 0000: 0000: 0000: FFFF: 192.168:128.2]
          // 8-bit
          // [00/00/00/00/00/00/00/00/00/00/FF/FF/192/168/128/2]
          // 32-bit
          // [00000000 && 00000000 && 0000FFFF && 192.168.128.2]

          // Spreading every 16-bit word separately to avoid endian dilemmas
          paddr16[2 * 2 + 0] = 0;
          paddr16[2 * 2 + 1] = 0xFFFF;

          paddr32[3] = ip[0]; // IPv4 address encoded here
      }
   }
}

using namespace std;


static string ShowIP4(const sockaddr_in* sin)
{
    ostringstream os;
    union
    {
        in_addr sinaddr;
        unsigned char ip[4];
    };
    sinaddr = sin->sin_addr;

    os << int(ip[0]);
    os << ".";
    os << int(ip[1]);
    os << ".";
    os << int(ip[2]);
    os << ".";
    os << int(ip[3]);
    return os.str();
}

static string ShowIP6(const sockaddr_in6* sin)
{
    ostringstream os;
    os.setf(ios::uppercase);

    bool sep = false;
    for (size_t i = 0; i < 16; ++i)
    {
        int v = sin->sin6_addr.s6_addr[i];
        if ( v )
        {
            if ( sep )
                os << ":";

            os << hex << v;
            sep = true;
        }
    }

    return os.str();
}

string CIPAddress::show(const sockaddr* adr)
{
    if ( adr->sa_family == AF_INET )
        return ShowIP4((const sockaddr_in*)adr);
    else if ( adr->sa_family == AF_INET6 )
        return ShowIP6((const sockaddr_in6*)adr);
    else
        return "(unsupported sockaddr type)";
}

//
void CMD5::compute(const char* input, unsigned char result[16])
{
   md5_state_t state;

   md5_init(&state);
   md5_append(&state, (const md5_byte_t *)input, strlen(input));
   md5_finish(&state, result);
}

std::string MessageTypeStr(UDTMessageType mt, uint32_t extt)
{
    using std::string;

    static const char* const udt_types [] = {
        "handshake",
        "keepalive",
        "ack",
        "lossreport",
        "cgwarning", //4
        "shutdown",
        "ackack",
        "dropreq",
        "peererror", //8
    };

    static const char* const srt_types [] = {
        "EXT:none",
        "EXT:hsreq",
        "EXT:hsrsp",
        "EXT:kmreq",
        "EXT:kmrsp",
        "EXT:sid",
        "EXT:congctl",
        "EXT:filter",
        "EXT:group"
    };


    if ( mt == UMSG_EXT )
    {
        if ( extt >= Size(srt_types) )
            return "EXT:unknown";

        return srt_types[extt];
    }

    if ( size_t(mt) > Size(udt_types) )
        return "unknown";

    return udt_types[mt];
}

std::string ConnectStatusStr(EConnectStatus cst)
{
    return
          cst == CONN_CONTINUE ? "INDUCED/CONCLUDING"
        : cst == CONN_RUNNING ? "RUNNING"
        : cst == CONN_ACCEPT ? "ACCEPTED"
        : cst == CONN_RENDEZVOUS ? "RENDEZVOUS (HSv5)"
        : cst == CONN_AGAIN ? "AGAIN"
        : cst == CONN_CONFUSED ? "MISSING HANDSHAKE"
        : "REJECTED";
}

std::string TransmissionEventStr(ETransmissionEvent ev)
{
    static const char* const vals [] =
    {
        "init",
        "ack",
        "ackack",
        "lossreport",
        "checktimer",
        "send",
        "receive",
        "custom"
    };

    size_t vals_size = Size(vals);

    if (size_t(ev) >= vals_size)
        return "UNKNOWN";
    return vals[ev];
}

extern const char* const srt_rejectreason_msg [] = {
    "Unknown or erroneous",
    "Error in system calls",
    "Peer rejected connection",
    "Resource allocation failure",
    "Rogue peer or incorrect parameters",
    "Listener's backlog exceeded",
    "Internal Program Error",
    "Socket is being closed",
    "Peer version too old",
    "Rendezvous-mode cookie collision",
    "Incorrect passphrase",
    "Password required or unexpected",
    "MessageAPI/StreamAPI collision",
    "Congestion controller type collision",
    "Packet Filter type collision",
    "Group settings collision",
    "Connection timeout"
};

const char* srt_rejectreason_str(int id)
{
    if (id >= SRT_REJC_PREDEFINED)
    {
        return "Application-defined rejection reason";
    }

    static const size_t ra_size = Size(srt_rejectreason_msg);
    if (size_t(id) >= ra_size)
        return srt_rejectreason_msg[0];
    return srt_rejectreason_msg[id];
}

bool SrtParseConfig(string s, SrtConfig& w_config)
{
    using namespace std;

    vector<string> parts;
    Split(s, ',', back_inserter(parts));

    w_config.type = parts[0];

    for (vector<string>::iterator i = parts.begin()+1; i != parts.end(); ++i)
    {
        vector<string> keyval;
        Split(*i, ':', back_inserter(keyval));
        if (keyval.size() != 2)
            return false;
        w_config.parameters[keyval[0]] = keyval[1];
    }

    return true;
}

uint64_t PacketMetric::fullBytes()
{
    static const int PKT_HDR_SIZE = CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
    return bytes + pkts * PKT_HDR_SIZE;
}


// Some logging imps
#if ENABLE_LOGGING

namespace srt_logging
{


std::string SockStatusStr(SRT_SOCKSTATUS s)
{
    if (int(s) < int(SRTS_INIT) || int(s) > int(SRTS_NONEXIST))
        return "???";

    static struct AutoMap
    {
        // Values start from 1, so do -1 to avoid empty cell
        std::string names[int(SRTS_NONEXIST)-1+1];

        AutoMap()
        {
#define SINI(statename) names[SRTS_##statename-1] = #statename
            SINI(INIT);
            SINI(OPENED);
            SINI(LISTENING);
            SINI(CONNECTING);
            SINI(CONNECTED);
            SINI(BROKEN);
            SINI(CLOSING);
            SINI(CLOSED);
            SINI(NONEXIST);
#undef SINI
        }
    } names;

    return names.names[int(s)-1];
}

std::string MemberStatusStr(SRT_MEMBERSTATUS s)
{
    if (int(s) < int(SRT_GST_PENDING) || int(s) > int(SRT_GST_BROKEN))
        return "???";

    static struct AutoMap
    {
        std::string names[int(SRT_GST_BROKEN)+1];

        AutoMap()
        {
#define SINI(statename) names[SRT_GST_##statename] = #statename
            SINI(PENDING);
            SINI(IDLE);
            SINI(RUNNING);
            SINI(BROKEN);
#undef SINI
        }
    } names;

    return names.names[int(s)];
}

LogDispatcher::Proxy::Proxy(LogDispatcher& guy) : that(guy), that_enabled(that.CheckEnabled())
{
    if (that_enabled)
    {
        i_file = "";
        i_line = 0;
        flags = that.src_config->flags;
        // Create logger prefix
        that.CreateLogLinePrefix(os);
    }
}

LogDispatcher::Proxy LogDispatcher::operator()()
{
    return Proxy(*this);
}

void LogDispatcher::CreateLogLinePrefix(std::ostringstream& serr)
{
    using namespace std;

    char tmp_buf[512];
    if ( !isset(SRT_LOGF_DISABLE_TIME) )
    {
        // Not necessary if sending through the queue.
        timeval tv;
        gettimeofday(&tv, 0);
        struct tm tm = SysLocalTime((time_t) tv.tv_sec);

        strftime(tmp_buf, 512, "%X.", &tm);
        serr << tmp_buf << setw(6) << setfill('0') << tv.tv_usec;
    }

    string out_prefix;
    if ( !isset(SRT_LOGF_DISABLE_SEVERITY) )
    {
        out_prefix = prefix;
    }

    // Note: ThreadName::get needs a buffer of size min. ThreadName::BUFSIZE
    if ( !isset(SRT_LOGF_DISABLE_THREADNAME) && ThreadName::get(tmp_buf) )
    {
        serr << "/" << tmp_buf << out_prefix << ": ";
    }
    else
    {
        serr << out_prefix << ": ";
    }
}

std::string LogDispatcher::Proxy::ExtractName(std::string pretty_function)
{
    if ( pretty_function == "" )
        return "";
    size_t pos = pretty_function.find('(');
    if ( pos == std::string::npos )
        return pretty_function; // return unchanged.

    pretty_function = pretty_function.substr(0, pos);

    // There are also template instantiations where the instantiating
    // parameters are encrypted inside. Therefore, search for the first
    // open < and if found, search for symmetric >.

    int depth = 1;
    pos = pretty_function.find('<');
    if ( pos != std::string::npos )
    {
        size_t end = pos+1;
        for(;;)
        {
            ++pos;
            if ( pos == pretty_function.size() )
            {
                --pos;
                break;
            }
            if ( pretty_function[pos] == '<' )
            {
                ++depth;
                continue;
            }

            if ( pretty_function[pos] == '>' )
            {
                --depth;
                if ( depth <= 0 )
                    break;
                continue;
            }
        }

        std::string afterpart = pretty_function.substr(pos+1);
        pretty_function = pretty_function.substr(0, end) + ">" + afterpart;
    }

    // Now see how many :: can be found in the name.
    // If this occurs more than once, take the last two.
    pos = pretty_function.rfind("::");

    if ( pos == std::string::npos || pos < 2 )
        return pretty_function; // return whatever this is. No scope name.

    // Find the next occurrence of :: - if found, copy up to it. If not,
    // return whatever is found.
    pos -= 2;
    pos = pretty_function.rfind("::", pos);
    if ( pos == std::string::npos )
        return pretty_function; // nothing to cut

    return pretty_function.substr(pos+2);
}

} // (end namespace srt_logging)

#endif
