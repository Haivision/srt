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

#if _WIN32
 #if SRT_ENABLE_LOCALIF_WIN32
  #include <iphlpapi.h>
 #endif
#else
 #include <ifaddrs.h>
#endif

#include "api.h"
#include "md5.h"
#include "common.h"
#include "netinet_any.h"
#include "logging.h"
#include "packet.h"
#include "logger_fas.h"
#include "handshake.h"

using namespace std;
using namespace srt::sync;
using namespace srt::logging;

namespace srt
{

const char* strerror_get_message(size_t major, size_t minor);


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
    return strerror_get_message(m_iMajor, m_iMinor);
}

string CUDTException::getErrorString() const
{
    return getErrorMessage();
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

void CIPAddress::encode(const sockaddr_any& addr, uint32_t (&ip)[4])
{
    if (addr.family() == AF_INET)
    {
        // SRT internal format of IPv4 address.
        // The IPv4 address is in the first field. The rest is 0.
        ip[0] = addr.sin.sin_addr.s_addr;
        ip[1] = ip[2] = ip[3] = 0;
    }
    else
    {
        std::memcpy(ip, addr.sin6.sin6_addr.s6_addr, 16);
    }
}

bool checkMappedIPv4(const uint16_t* addr)
{
    static const uint16_t ipv4on6_model [8] =
    {
        0, 0, 0, 0, 0, 0xFFFF, 0, 0
    };

    // Compare only first 6 words. Remaining 2 words
    // comprise the IPv4 address, if these first 6 match.
    const uint16_t* mbegin = ipv4on6_model;
    const uint16_t* mend = ipv4on6_model + 6;

    return std::equal(mbegin, mend, addr);
}

// This function gets w_addr by reference because it only overwrites the address part.
void CIPAddress::decode(const uint32_t (&ip)[4], const sockaddr_any& peer, sockaddr_any& w_addr)
{
    uint32_t* target_ipv4_addr = NULL;

    if (peer.family() == AF_INET)
    {
        sockaddr_in* a = (&w_addr.sin);
        target_ipv4_addr = (uint32_t*) &a->sin_addr.s_addr;
    }
    else // AF_INET6
    {
        // Check if the peer address is a model of IPv4-mapped-on-IPv6.
        // If so, it means that the `ip` array should be interpreted as IPv4.
        const bool is_mapped_ipv4 = checkMappedIPv4(peer.sin6);

        sockaddr_in6* a = (&w_addr.sin6);

        // This whole above procedure was only in order to EXCLUDE the
        // possibility of IPv4-mapped-on-IPv6. This below may only happen
        // if BOTH peers are IPv6. Otherwise we have a situation of cross-IP
        // version connection in which case the address in question is always
        // IPv4 in various mapping formats.
        if (!is_mapped_ipv4)
        {
            // Here both agent and peer use IPv6, in which case
            // `ip` contains the full IPv6 address, so just copy
            // it as is.
            std::memcpy(a->sin6_addr.s6_addr, ip, 16);
            return; // The address is written, nothing left to do.
        }

        // 
        // IPv4 mapped on IPv6

        // Here agent uses IPv6 with IPPROTO_IPV6/IPV6_V6ONLY == 0
        // In this case, the address in `ip` is always an IPv4,
        // although we are not certain as to whether it's using the
        // IPv6 encoding (0::FFFF:IPv4) or SRT encoding (IPv4::0);
        // this must be extra determined.
        //
        // Unfortunately, sockaddr_in6 doesn't give any straightforward
        // method for it, although the official size of a single element
        // of the IPv6 address is 16-bit.

        memset((a->sin6_addr.s6_addr), 0, sizeof a->sin6_addr.s6_addr);

        // The sin6_addr.s6_addr32 is non that portable to use here.
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
        paddr16[2 * 2 + 1] = 0xFFFF;

        target_ipv4_addr = &paddr32[3];
    }

    // Now we have two possible formats of encoding the IPv4 address:
    // 1. If peer is IPv4, it's IPv4::0
    // 2. If peer is IPv6, it's 0::FFFF:IPv4.
    //
    // Has any other possibility happen here, copy an empty address,
    // which will be the only sign of an error.

    const uint16_t* peeraddr16 = (uint16_t*)ip;
    const bool is_mapped_ipv4 = checkMappedIPv4(peeraddr16);

    if (is_mapped_ipv4)
    {
        *target_ipv4_addr = ip[3];
        HLOGC(inlog.Debug, log << "pton: Handshake address: " << w_addr.str() << " provided in IPv6 mapping format");
    }
    // Check SRT IPv4 format.
    else if ((ip[1] | ip[2] | ip[3]) == 0)
    {
        *target_ipv4_addr = ip[0];
        HLOGC(inlog.Debug, log << "pton: Handshake address: " << w_addr.str() << " provided in SRT IPv4 format");
    }
    else
    {
#if HVU_ENABLE_LOGGING
        using namespace hvu;

        ofmtbufstream peeraddr_form;
        fmtc hex04 = fmtc().hex().fillzero().width(4);
        peeraddr_form << fmt(peeraddr16[0], hex04);
        for (int i = 1; i < 8; ++i)
            peeraddr_form << ":" << fmt(peeraddr16[i], hex04);

        LOGC(inlog.Error, log << "pton: IPE or net error: can't determine IPv4 carryover format: " << peeraddr_form);
#endif
        *target_ipv4_addr = 0;
        if (peer.family() != AF_INET)
        {
            // Additionally overwrite the 0xFFFF that has been
            // just written 50 lines above.
            w_addr.sin6.sin6_addr.s6_addr[10] = 0;
            w_addr.sin6.sin6_addr.s6_addr[11] = 0;
        }
    }

}

static inline void PrintIPv4(uint32_t aval, hvu::ofmtbufstream& os)
{
    typedef Bits<8+8+8+7, 8+8+8> q0;
    typedef Bits<8+8+7, 8+8> q1;
    typedef Bits<8+7, 8> q2;
    typedef Bits<7, 0> q3;

    os << q3::unwrap(aval) << ".";
    os << q2::unwrap(aval) << ".";
    os << q1::unwrap(aval) << ".";
    os << q0::unwrap(aval);
}

std::string CIPAddress::show(const uint32_t (&ip)[4])
{
    const uint16_t* peeraddr16 = (uint16_t*)ip;
    const bool is_mapped_ipv4 = checkMappedIPv4(peeraddr16);

    using namespace hvu;

    ofmtbufstream out;
    if (is_mapped_ipv4)
    {
        out << "::FFFF:";
        PrintIPv4(ip[3], (out));
    }
    // Check SRT IPv4 format.
    else if ((ip[1] | ip[2] | ip[3]) == 0)
    {
        PrintIPv4(ip[0], (out));
        out << "[SRT]";
    }
    else
    {
        fmtc hex04 = fmtc().hex().fillzero().width(4);
        out << fmt(peeraddr16[0], hex04);
        for (int i = 1; i < 8; ++i)
            out << ":" << fmt(peeraddr16[i], hex04);
    }

    return out.str();
}

void CMD5::compute(const char* input, unsigned char result[16])
{
   md5_state_t state;

   md5_init(&state);
   md5_append(&state, (const md5_byte_t *)input, (int) strlen(input));
   md5_finish(&state, result);
}

string MessageTypeStr(UDTMessageType mt, uint32_t extt)
{
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

string ConnectStatusStr(EConnectStatus cst)
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

string TransmissionEventStr(ETransmissionEvent ev)
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
        "custom",
        "sync"
    };

    size_t vals_size = Size(vals);

    if (size_t(ev) >= vals_size)
        return "UNKNOWN";
    return vals[ev];
}

bool SrtParseConfig(const string& s, SrtConfig& w_config)
{
    using namespace std;

    vector<string> parts;
    Split(s, ',', back_inserter(parts));
    if (parts.empty())
        return false;

    w_config.type = parts[0];

    for (vector<string>::iterator i = parts.begin()+1; i != parts.end(); ++i)
    {
        vector<string> keyval;
        Split(*i, ':', back_inserter(keyval));
        if (keyval.size() != 2)
            return false;
        if (keyval[1] != "")
            w_config.parameters[keyval[0]] = keyval[1];
    }

    return true;
}

string FormatLossArray(const vector< pair<int32_t, int32_t> >& lra)
{
    ostringstream os;

    os << "[ ";
    for (vector< pair<int32_t, int32_t> >::const_iterator i = lra.begin(); i != lra.end(); ++i)
    {
        int len = CSeqNo::seqoff(i->first, i->second);
        os << "%" << i->first;
        if (len > 1)
            os << "+" << len;
        os << " ";
    }

    os << "]";
    return os.str();
}

string FormatValue(int value, int factor, const char* unit)
{
    ostringstream out;
    double showval = value;
    showval /= factor;
    out << std::fixed << showval << unit;
    return out.str();
}


ostream& PrintEpollEvent(ostream& os, int events, int et_events)
{
    static pair<int, const char*> const namemap [] = {
        make_pair(SRT_EPOLL_IN, "R"),
        make_pair(SRT_EPOLL_OUT, "W"),
        make_pair(SRT_EPOLL_ERR, "E"),
        make_pair(SRT_EPOLL_UPDATE, "U")
    };
    bool any = false;

    const int N = (int)Size(namemap);

    for (int i = 0; i < N; ++i)
    {
        if (events & namemap[i].first)
        {
            os << "[";
            if (et_events & namemap[i].first)
                os << "^";
            os << namemap[i].second << "]";
            any = true;
        }
    }

    if (!any)
        os << "[]";

    return os;
}

vector<LocalInterface> GetLocalInterfaces()
{
    vector<LocalInterface> locals;
#ifdef _WIN32
 // If not enabled, simply an empty local vector will be returned
 #if SRT_ENABLE_LOCALIF_WIN32
	ULONG flags = GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_ALL_INTERFACES;
	ULONG outBufLen = 0;

    // This function doesn't allocate memory by itself, you have to do it
    // yourself, worst case when it's too small, the size will be corrected
    // and the function will do nothing. So, simply, call the function with
    // always too little 0 size and make it show the correct one.
    GetAdaptersAddresses(AF_UNSPEC, flags, NULL, NULL, &outBufLen);
    // Ignore errors. Check errors on the real call.
	// (Have doubts about this "max" here, as VC reports errors when
	// using std::max, so it will likely resolve to a macro - hope this
	// won't cause portability problems, this code is Windows only.

    // Good, now we can allocate memory
    PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)::operator new(outBufLen);
    ULONG st = GetAdaptersAddresses(AF_UNSPEC, flags, NULL, pAddresses, &outBufLen);
    if (st == ERROR_SUCCESS)
    {
        for (PIP_ADAPTER_ADDRESSES i = pAddresses; i; i = pAddresses->Next)
        {
            string name = i->AdapterName;
            PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pAddresses->FirstUnicastAddress;
            while (pUnicast)
            {
                LocalInterface a;
                if (pUnicast->Address.lpSockaddr)
                    a.addr = pUnicast->Address.lpSockaddr;
                if (a.addr.len > 0)
                {
                    // DO NOT collect addresses that are not of
                    // AF_INET or AF_INET6 family.
                    a.name = name;
                    locals.push_back(a);
                }
                pUnicast = pUnicast->Next;
            }
        }
    }

    ::operator delete(pAddresses);
 #endif

#else
    // Use POSIX method: getifaddrs
    struct ifaddrs* pif, * pifa;
    int st = getifaddrs(&pifa);
    if (st == 0)
    {
        for (pif = pifa; pif; pif = pif->ifa_next)
        {
            LocalInterface i;
            if (pif->ifa_addr)
                i.addr = pif->ifa_addr;
            if (i.addr.len > 0)
            {
                // DO NOT collect addresses that are not of
                // AF_INET or AF_INET6 family.
                i.name = pif->ifa_name ? pif->ifa_name : "";
                locals.push_back(i);
            }
        }
    }

    freeifaddrs(pifa);
#endif
    return locals;
}

SRTSOCKET SocketKeeper::id() const { return socket ? socket->id() : SRT_INVALID_SOCK; }


// Value display utilities
// (also useful for applications)

string SockStatusStr(SRT_SOCKSTATUS s)
{
    if (int(s) < int(SRTS_INIT) || int(s) > int(SRTS_NONEXIST))
        return "???";

    static struct AutoMap
    {
        // Values start from 1, so do -1 to avoid empty cell
        string names[int(SRTS_NONEXIST)-1+1];

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

string MemberStatusStr(SRT_MEMBERSTATUS s)
{
    if (int(s) < int(SRT_GST_PENDING) || int(s) > int(SRT_GST_BROKEN))
        return "???";

    static struct AutoMap
    {
        string names[int(SRT_GST_BROKEN)+1];

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

string SrtCmdName(int cmd)
{
    if (cmd < 0 || cmd >= SRT_CMD_E_SIZE)
        return "???";

    static struct AutoMap
    {
        string names[SRT_CMD_E_SIZE];

        AutoMap()
        {
            names[0] = "noext"; // Use special case for 0
#define SINI(statename) names[SRT_CMD_##statename] = #statename
            SINI(HSREQ);
            SINI(HSRSP);
            SINI(KMREQ);
            SINI(KMRSP);
            SINI(SID);
            SINI(CONGESTION);
            SINI(FILTER);
            SINI(GROUP);
#undef SINI
        }
    } names;

    return names.names[cmd];
}


} // namespace srt

