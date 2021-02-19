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
   Haivision Systems Inc.
*****************************************************************************/
#include <utility>

#include "srt.h"
#include "socketconfig.h"

extern const int32_t SRT_DEF_VERSION = SrtParseVersion(SRT_VERSION);

static struct SrtConfigSetter
{
    setter_function* fn[SRTO_E_SIZE];

    SrtConfigSetter()
    {
        memset(fn, 0, sizeof fn);

#define DISPATCH(optname) fn[optname] = &CSrtConfigSetter<optname>::set;

        DISPATCH(SRTO_MSS);
        DISPATCH(SRTO_FC);
        DISPATCH(SRTO_SNDBUF);
        DISPATCH(SRTO_RCVBUF);
        DISPATCH(SRTO_LINGER);
        DISPATCH(SRTO_UDP_SNDBUF);
        DISPATCH(SRTO_UDP_RCVBUF);
        DISPATCH(SRTO_RENDEZVOUS);
        DISPATCH(SRTO_SNDTIMEO);
        DISPATCH(SRTO_RCVTIMEO);
        DISPATCH(SRTO_SNDSYN);
        DISPATCH(SRTO_RCVSYN);
        DISPATCH(SRTO_REUSEADDR);
        DISPATCH(SRTO_MAXBW);
        DISPATCH(SRTO_IPTTL);
        DISPATCH(SRTO_IPTOS);
        DISPATCH(SRTO_BINDTODEVICE);
        DISPATCH(SRTO_INPUTBW);
        DISPATCH(SRTO_MININPUTBW);
        DISPATCH(SRTO_OHEADBW);
        DISPATCH(SRTO_SENDER);
        DISPATCH(SRTO_TSBPDMODE);
        DISPATCH(SRTO_LATENCY);
        DISPATCH(SRTO_RCVLATENCY);
        DISPATCH(SRTO_PEERLATENCY);
        DISPATCH(SRTO_TLPKTDROP);
        DISPATCH(SRTO_SNDDROPDELAY);
        DISPATCH(SRTO_PASSPHRASE);
        DISPATCH(SRTO_PBKEYLEN);
        DISPATCH(SRTO_NAKREPORT);
        DISPATCH(SRTO_CONNTIMEO);
        DISPATCH(SRTO_DRIFTTRACER);
        DISPATCH(SRTO_LOSSMAXTTL);
        DISPATCH(SRTO_VERSION);
        DISPATCH(SRTO_MINVERSION);
        DISPATCH(SRTO_STREAMID);
        DISPATCH(SRTO_CONGESTION);
        DISPATCH(SRTO_MESSAGEAPI);
        DISPATCH(SRTO_PAYLOADSIZE);
        DISPATCH(SRTO_TRANSTYPE);
#if ENABLE_EXPERIMENTAL_BONDING
        DISPATCH(SRTO_GROUPCONNECT);
#endif
        DISPATCH(SRTO_KMREFRESHRATE);
        DISPATCH(SRTO_KMPREANNOUNCE);
        DISPATCH(SRTO_ENFORCEDENCRYPTION);
        DISPATCH(SRTO_PEERIDLETIMEO);
        DISPATCH(SRTO_IPV6ONLY);
        DISPATCH(SRTO_PACKETFILTER);
#if ENABLE_EXPERIMENTAL_BONDING
        DISPATCH(SRTO_GROUPSTABTIMEO);
#endif
        DISPATCH(SRTO_RETRANSMITALGO);

#undef DISPATCH
    }
} srt_config_setter;

int CSrtConfig::set(SRT_SOCKOPT optName, const void* optval, int optlen)
{
    setter_function* fn = srt_config_setter.fn[optName];
    if (!fn)
        return -1; // No such option

    fn(*this, optval, optlen); // MAY THROW EXCEPTION.
    return 0;
}

#if ENABLE_EXPERIMENTAL_BONDING
bool SRT_SocketOptionObject::add(SRT_SOCKOPT optname, const void* optval, size_t optlen)
{
    // Check first if this option is allowed to be set
    // as on a member socket.

    switch (optname)
    {
    case SRTO_BINDTODEVICE:
    case SRTO_CONNTIMEO:
    case SRTO_DRIFTTRACER:
        //SRTO_FC - not allowed to be different among group members
    case SRTO_GROUPSTABTIMEO:
        //SRTO_INPUTBW - per transmission setting
    case SRTO_IPTOS:
    case SRTO_IPTTL:
    case SRTO_KMREFRESHRATE:
    case SRTO_KMPREANNOUNCE:
        //SRTO_LATENCY - per transmission setting
        //SRTO_LINGER - not for managed sockets
    case SRTO_LOSSMAXTTL:
        //SRTO_MAXBW - per transmission setting
        //SRTO_MESSAGEAPI - groups are live mode only
        //SRTO_MINVERSION - per group connection setting
    case SRTO_NAKREPORT:
        //SRTO_OHEADBW - per transmission setting
        //SRTO_PACKETFILTER - per transmission setting
        //SRTO_PASSPHRASE - per group connection setting
        //SRTO_PASSPHRASE - per transmission setting
        //SRTO_PBKEYLEN - per group connection setting
    case SRTO_PEERIDLETIMEO:
    case SRTO_RCVBUF:
        //SRTO_RCVSYN - must be always false in groups
        //SRTO_RCVTIMEO - must be alwyas -1 in groups
    case SRTO_SNDBUF:
    case SRTO_SNDDROPDELAY:
        //SRTO_TLPKTDROP - per transmission setting
        //SRTO_TSBPDMODE - per transmission setting
    case SRTO_UDP_RCVBUF:
    case SRTO_UDP_SNDBUF:
        break;

    default:
        // Other options are not allowed
        return false;
    }

    // Header size will get the size likely aligned, but it won't
    // hurt if the memory size will be up to 4 bytes more than
    // needed - and it's better to not risk that alighment rules
    // will make these calculations result in less space than needed.
    const size_t headersize = sizeof(SingleOption);
    const size_t payload = std::min(sizeof(uint32_t), optlen);
    unsigned char* mem = new unsigned char[headersize + payload];
    SingleOption* option = reinterpret_cast<SingleOption*>(mem);
    option->option = optname;
    option->length = (uint16_t) optlen;
    memcpy(option->storage, optval, optlen);

    options.push_back(option);

    return true;
}
#endif
