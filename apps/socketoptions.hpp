/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_SOCKETOPTIONS_HPP
#define INC_SRT_SOCKETOPTIONS_HPP

#include <string>
#include <map>
#include <set>
#include <vector>
#include "../srtcore/srt.h" // Devel path

#ifdef _WIN32
#include "winsock2.h"
#endif

struct OptionValue
{
    std::string s;
    union {
        int i;
        int64_t l;
        bool b;
    };

    const void* value = nullptr;
    size_t size = 0;
};

extern const std::set<std::string> false_names, true_names;

struct SocketOption
{
    enum Type { STRING = 0, INT, INT64, BOOL, ENUM };
    enum Binding { PRE = 0, POST };
    enum Domain { SYSTEM, SRT };
    enum Mode {FAILURE = -1, LISTENER = 0, CALLER = 1, RENDEZVOUS = 2};
    static const char* const mode_names [3];

    std::string name;
    int protocol;
    int symbol;
    Binding binding;
    Type type;
    const std::map<std::string, int>* valmap;

    template <Domain D, typename Object = int>
    bool apply(Object socket, std::string value) const;

    template <Domain D, Type T, typename Object = int>
    bool applyt(Object socket, std::string value) const;

    template <Domain D, typename Object>
    static int setso(Object socket, int protocol, int symbol, const void* data, size_t size);

    template<Type T>
    bool extract(std::string value, OptionValue& val) const;
};

template<>
inline int SocketOption::setso<SocketOption::SRT, int>(int socket, int /*ignored*/, int sym, const void* data, size_t size)
{
    return srt_setsockopt(socket, 0, SRT_SOCKOPT(sym), data, (int) size);
}

#if ENABLE_BONDING
template<>
inline int SocketOption::setso<SocketOption::SRT, SRT_SOCKOPT_CONFIG*>(SRT_SOCKOPT_CONFIG* obj, int /*ignored*/, int sym, const void* data, size_t size)
{
    return srt_config_add(obj, SRT_SOCKOPT(sym), data, (int) size);
}
#endif


template<>
inline int SocketOption::setso<SocketOption::SYSTEM, int>(int socket, int proto, int sym, const void* data, size_t size)
{
    return ::setsockopt(socket, proto, sym, (const char *)data, (int) size);
}

template<>
inline bool SocketOption::extract<SocketOption::STRING>(std::string value, OptionValue& o) const
{
    o.s = value;
    o.value = o.s.data();
    o.size = o.s.size();
    return true;
}

template<>
inline bool SocketOption::extract<SocketOption::INT>(std::string value, OptionValue& o) const
{
    try
    {
        o.i = stoi(value, 0, 0);
        o.value = &o.i;
        o.size = sizeof o.i;
        return true;
    }
    catch (...) // stoi throws
    {
        return false; // do not change o
    }
    return false;
}

template<>
inline bool SocketOption::extract<SocketOption::INT64>(std::string value, OptionValue& o) const
{
    try
    {
        long long vall = stoll(value);
        o.l = vall; // int64_t resolves to either 'long long', or 'long' being 64-bit integer
        o.value = &o.l;
        o.size = sizeof o.l;
        return true;
    }
    catch (...) // stoll throws
    {
        return false;
    }
    return false;
}

template<>
inline bool SocketOption::extract<SocketOption::BOOL>(std::string value, OptionValue& o) const
{
    bool val;
    if ( false_names.count(value) )
        val = false;
    else if ( true_names.count(value) )
        val = true;
    else
        return false;

    o.b = val;
    o.value = &o.b;
    o.size = sizeof o.b;
    return true;
}

template<>
inline bool SocketOption::extract<SocketOption::ENUM>(std::string value, OptionValue& o) const
{
    if (valmap)
    {
        // Search value in the map. If found, set to o.
        auto p = valmap->find(value);
        if ( p != valmap->end() )
        {
            o.i = p->second;
            o.value = &o.i;
            o.size = sizeof o.i;
            return true;
        }
    }

    // Fallback: try interpreting it as integer.
    try
    {
        o.i = stoi(value, 0, 0);
        o.value = &o.i;
        o.size = sizeof o.i;
        return true;
    }
    catch (...) // stoi throws
    {
        return false; // do not change o
    }
    return false;
}

template <SocketOption::Domain D, SocketOption::Type T, typename Object>
inline bool SocketOption::applyt(Object socket, std::string value) const
{
    OptionValue o; // common meet point
    int result = -1;
    if (extract<T>(value, o))
        result = setso<D>(socket, protocol, symbol, o.value, o.size);
    return result != -1;
}


template<SocketOption::Domain D, typename Object>
inline bool SocketOption::apply(Object socket, std::string value) const
{
    switch ( type )
    {
#define SRT_HANDLE_TYPE(ty) case ty: return applyt<D, ty, Object>(socket, value)

        SRT_HANDLE_TYPE(STRING);
        SRT_HANDLE_TYPE(INT);
        SRT_HANDLE_TYPE(INT64);
        SRT_HANDLE_TYPE(BOOL);
        SRT_HANDLE_TYPE(ENUM);

#undef SRT_HANDLE_TYPE
    }
    return false;
}

extern const std::map<std::string, int> enummap_transtype;

namespace {
const SocketOption srt_options [] {
    { "transtype", 0, SRTO_TRANSTYPE, SocketOption::PRE, SocketOption::ENUM, &enummap_transtype },
    { "maxbw", 0, SRTO_MAXBW, SocketOption::POST, SocketOption::INT64, nullptr},
    { "pbkeylen", 0, SRTO_PBKEYLEN, SocketOption::PRE, SocketOption::INT, nullptr},
    { "passphrase", 0, SRTO_PASSPHRASE, SocketOption::PRE, SocketOption::STRING, nullptr},

    { "mss", 0, SRTO_MSS, SocketOption::PRE, SocketOption::INT, nullptr},
    { "fc", 0, SRTO_FC, SocketOption::PRE, SocketOption::INT, nullptr},
    { "sndbuf", 0, SRTO_SNDBUF, SocketOption::PRE, SocketOption::INT, nullptr},
    { "rcvbuf", 0, SRTO_RCVBUF, SocketOption::PRE, SocketOption::INT, nullptr},
    // linger option is handled outside of the common loop, therefore commented out.
    //{ "linger", 0, SRTO_LINGER, SocketOption::PRE, SocketOption::INT, nullptr},
    { "ipttl", 0, SRTO_IPTTL, SocketOption::PRE, SocketOption::INT, nullptr},
    { "iptos", 0, SRTO_IPTOS, SocketOption::PRE, SocketOption::INT, nullptr},
    { "inputbw", 0, SRTO_INPUTBW, SocketOption::POST, SocketOption::INT64, nullptr},
    { "mininputbw", 0, SRTO_MININPUTBW, SocketOption::POST, SocketOption::INT64, nullptr},
    { "oheadbw", 0, SRTO_OHEADBW, SocketOption::POST, SocketOption::INT, nullptr},
    { "latency", 0, SRTO_LATENCY, SocketOption::PRE, SocketOption::INT, nullptr},
    { "tsbpdmode", 0, SRTO_TSBPDMODE, SocketOption::PRE, SocketOption::BOOL, nullptr},
    { "tlpktdrop", 0, SRTO_TLPKTDROP, SocketOption::PRE, SocketOption::BOOL, nullptr},
    { "snddropdelay", 0, SRTO_SNDDROPDELAY, SocketOption::POST, SocketOption::INT, nullptr},
    { "nakreport", 0, SRTO_NAKREPORT, SocketOption::PRE, SocketOption::BOOL, nullptr},
    { "conntimeo", 0, SRTO_CONNTIMEO, SocketOption::PRE, SocketOption::INT, nullptr},
    { "drifttracer", 0, SRTO_DRIFTTRACER, SocketOption::POST, SocketOption::BOOL, nullptr},
    { "lossmaxttl", 0, SRTO_LOSSMAXTTL, SocketOption::POST, SocketOption::INT, nullptr},
    { "rcvlatency", 0, SRTO_RCVLATENCY, SocketOption::PRE, SocketOption::INT, nullptr},
    { "peerlatency", 0, SRTO_PEERLATENCY, SocketOption::PRE, SocketOption::INT, nullptr},
    { "minversion", 0, SRTO_MINVERSION, SocketOption::PRE, SocketOption::INT, nullptr},
    { "streamid", 0, SRTO_STREAMID, SocketOption::PRE, SocketOption::STRING, nullptr},
    { "congestion", 0, SRTO_CONGESTION, SocketOption::PRE, SocketOption::STRING, nullptr},
    { "messageapi", 0, SRTO_MESSAGEAPI, SocketOption::PRE, SocketOption::BOOL, nullptr},
    { "payloadsize", 0, SRTO_PAYLOADSIZE, SocketOption::PRE, SocketOption::INT, nullptr},
    { "kmrefreshrate", 0, SRTO_KMREFRESHRATE, SocketOption::PRE, SocketOption::INT, nullptr },
    { "kmpreannounce", 0, SRTO_KMPREANNOUNCE, SocketOption::PRE, SocketOption::INT, nullptr },
    { "enforcedencryption", 0, SRTO_ENFORCEDENCRYPTION, SocketOption::PRE, SocketOption::BOOL, nullptr },
    { "ipv6only", 0, SRTO_IPV6ONLY, SocketOption::PRE, SocketOption::INT, nullptr },
    { "peeridletimeo", 0, SRTO_PEERIDLETIMEO, SocketOption::PRE, SocketOption::INT, nullptr },
    { "packetfilter", 0, SRTO_PACKETFILTER, SocketOption::PRE, SocketOption::STRING, nullptr },
#if ENABLE_BONDING
    { "groupconnect", 0, SRTO_GROUPCONNECT, SocketOption::PRE, SocketOption::INT, nullptr},
    { "groupminstabletimeo", 0, SRTO_GROUPMINSTABLETIMEO, SocketOption::PRE, SocketOption::INT, nullptr},
#endif
#ifdef SRT_ENABLE_BINDTODEVICE
    { "bindtodevice", 0, SRTO_BINDTODEVICE, SocketOption::PRE, SocketOption::STRING, nullptr},
#endif
    { "retransmitalgo", 0, SRTO_RETRANSMITALGO, SocketOption::PRE, SocketOption::INT, nullptr }
#if defined(SRT_ENABLE_AEAD_API_PREVIEW) && (SRT_ENABLE_AEAD_API_PREVIEW == 1)
    ,{ "cryptomode", 0, SRTO_CRYPTOMODE, SocketOption::PRE, SocketOption::INT, nullptr }
#endif
#ifdef ENABLE_MAXREXMITBW
    ,{ "maxrexmitbw", 0, SRTO_MAXREXMITBW, SocketOption::POST, SocketOption::INT64, nullptr }
#endif
};
}

SocketOption::Mode SrtInterpretMode(const std::string& modestr, const std::string& host, const std::string& adapter);
SocketOption::Mode SrtConfigurePre(SRTSOCKET socket, std::string host, std::map<std::string, std::string> options, std::vector<std::string>* failures = 0);
void SrtConfigurePost(SRTSOCKET socket, std::map<std::string, std::string> options, std::vector<std::string>* failures = 0);

#endif
