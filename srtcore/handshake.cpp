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

#include "platform_sys.h"

#include <cstring>
#include <string>
#include <sstream>
#include <iterator>
#include <algorithm>

#include "udt.h"
#include "api.h"
#include "core.h"
#include "handshake.h"
#include "utilities.h"

using namespace std;
using namespace srt;


srt::CHandShake::CHandShake()
    : m_iVersion(0)
    , m_iType(0) // Universal: UDT_UNDEFINED or no flags
    , m_iISN(0)
    , m_iMSS(0)
    , m_iFlightFlagSize(0)
    , m_iReqType(URQ_WAVEAHAND)
    , m_iID(0)
    , m_iCookie(0)
    , m_extension(false)
{
   for (int i = 0; i < 4; ++ i)
      m_piPeerIP[i] = 0;
}

int srt::CHandShake::store_to(char* buf, size_t& w_size)
{
   if (w_size < m_iContentSize)
      return -1;

   int32_t* p = reinterpret_cast<int32_t*>(buf);
   *p++ = m_iVersion;
   *p++ = m_iType;
   *p++ = m_iISN;
   *p++ = m_iMSS;
   *p++ = m_iFlightFlagSize;
   *p++ = int32_t(m_iReqType);
   *p++ = m_iID;
   *p++ = m_iCookie;
   for (int i = 0; i < 4; ++ i)
      *p++ = m_piPeerIP[i];

   w_size = m_iContentSize;

   return 0;
}

int srt::CHandShake::load_from(const char* buf, size_t size)
{
   if (size < m_iContentSize)
      return -1;

   const int32_t* p = reinterpret_cast<const int32_t*>(buf);

   m_iVersion = *p++;
   m_iType = *p++;
   m_iISN = *p++;
   m_iMSS = *p++;
   m_iFlightFlagSize = *p++;
   m_iReqType = UDTRequestType(*p++);
   m_iID = *p++;
   m_iCookie = *p++;
   for (int i = 0; i < 4; ++ i)
      m_piPeerIP[i] = *p++;

   return 0;
}

#ifdef ENABLE_LOGGING

namespace srt
{
const char* srt_rejectreason_name [] = {
    "UNKNOWN",
    "SYSTEM",
    "PEER",
    "RESOURCE",
    "ROGUE",
    "BACKLOG",
    "IPE",
    "CLOSE",
    "VERSION",
    "RDVCOOKIE",
    "BADSECRET",
    "UNSECURE",
    "MESSAGEAPI",
    "CONGESTION",
    "FILTER",
    "GROUP",
    "TIMEOUT",
    "CRYPTO"
};
}

std::string srt::RequestTypeStr(UDTRequestType rq)
{
    if (rq >= URQ_FAILURE_TYPES)
    {
        std::ostringstream rt;
        rt << "ERROR:";
        int id = RejectReasonForURQ(rq);
        if (id < (int) Size(srt_rejectreason_name))
            rt << srt_rejectreason_name[id];
        else if (id < SRT_REJC_USERDEFINED)
        {
            if (id < SRT_REJC_PREDEFINED)
                rt << "UNKNOWN:" << id;
            else
                rt << "PREDEFINED:" << (id - SRT_REJC_PREDEFINED);
        }
        else
            rt << "USERDEFINED:" << (id - SRT_REJC_USERDEFINED);

        return rt.str();
    }

    switch ( rq )
    {
    case URQ_INDUCTION: return "induction";
    case URQ_WAVEAHAND: return "waveahand";
    case URQ_CONCLUSION: return "conclusion";
    case URQ_AGREEMENT: return "agreement";

    default: return "INVALID";
    }
}

string srt::CHandShake::RdvStateStr(CHandShake::RendezvousState s)
{
    switch (s)
    {
    case RDV_WAVING: return "waving";
    case RDV_ATTENTION: return "attention";
    case RDV_FINE: return "fine";
    case RDV_INITIATED: return "initiated";
    case RDV_CONNECTED: return "connected";
    default: ;
    }

    return "invalid";
}
#endif

bool srt::CHandShake::valid()
{
    if (m_iVersion < CUDT::HS_VERSION_UDT4
            || m_iISN < 0 || m_iISN >= CSeqNo::m_iMaxSeqNo
            || m_iMSS < 32
            || m_iFlightFlagSize < 2)
        return false;

    return true;
}

string srt::CHandShake::show()
{
    ostringstream so;

    so << "version=" << m_iVersion << " type=0x" << hex << m_iType << dec
        << " ISN=" << m_iISN << " MSS=" << m_iMSS << " FLW=" << m_iFlightFlagSize
        << " reqtype=" << RequestTypeStr(m_iReqType) << " srcID=" << m_iID
        << " cookie=" << hex << m_iCookie << dec
        << " srcIP=";

    const unsigned char* p  = (const unsigned char*)m_piPeerIP;
    const unsigned char* pe = p + 4 * (sizeof(uint32_t));

    copy(p, pe, ostream_iterator<unsigned>(so, "."));

    // XXX HS version symbols should be probably declared inside
    // CHandShake, not CUDT.
    if ( m_iVersion > CUDT::HS_VERSION_UDT4 )
    {
        const int flags = SrtHSRequest::SRT_HSTYPE_HSFLAGS::unwrap(m_iType);
        so << "FLAGS: ";
        if (flags == SrtHSRequest::SRT_MAGIC_CODE)
            so << "MAGIC";
        else if (m_iType == 0)
            so << "NONE"; // no flags and no advertised pbkeylen
        else
            so << ExtensionFlagStr(m_iType);
    }

    return so.str();
}

string srt::CHandShake::ExtensionFlagStr(int32_t fl)
{
    std::ostringstream out;
    if ( fl & HS_EXT_HSREQ )
        out << " hsx";
    if ( fl & HS_EXT_KMREQ )
        out << " kmx";
    if ( fl & HS_EXT_CONFIG )
        out << " config";

    const int kl = SrtHSRequest::SRT_HSTYPE_ENCFLAGS::unwrap(fl) << 6;
    if (kl != 0)
    {
        out << " AES-" << kl;
    }
    else
    {
        out << " no-pbklen";
    }

    return out.str();
}


// XXX This code isn't currently used. Left here because it can
// be used in future, should any refactoring for the "manual word placement"
// code be done.
bool srt::SrtHSRequest::serialize(char* buf, size_t size) const
{
    if (size < SRT_HS_SIZE)
        return false;

    int32_t* p = reinterpret_cast<int32_t*>(buf);

    *p++ = m_iSrtVersion;
    *p++ = m_iSrtFlags;
    *p++ = m_iSrtTsbpd;
    *p++ = 0; // SURPRISE! Seriously, use (something) if this "reserved" is going to be used for something.
    return true;
}


bool srt::SrtHSRequest::deserialize(const char* buf, size_t size)
{
    m_iSrtVersion = 0; // just to let users recognize if it succeeded or not.

    if (size < SRT_HS_SIZE)
        return false;

   const int32_t* p = reinterpret_cast<const int32_t*>(buf);

    m_iSrtVersion = (*p++);
    m_iSrtFlags = (*p++);
    m_iSrtTsbpd = (*p++);
    m_iSrtReserved = (*p++);
    return true;
}

std::string srt::SrtFlagString(int32_t flags)
{
#define LEN(arr) (sizeof (arr)/(sizeof ((arr)[0])))

    std::string output;
    static std::string namera[] = { "TSBPD-snd", "TSBPD-rcv", "haicrypt", "TLPktDrop", "NAKReport", "ReXmitFlag", "StreamAPI", "FilterCapable" };

    size_t i = 0;
    for (; i < LEN(namera); ++i)
    {
        if ((flags & 1) == 1)
        {
            output += "+" + namera[i] + " ";
        }
        else
        {
            output += "-" + namera[i] + " ";
        }

        flags >>= 1;
    }

#undef LEN

    if (flags != 0)
    {
        output += "+unknown";
    }

    return output;
}
