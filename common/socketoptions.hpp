/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */
#ifndef INC__SOCKETOPTIONS_HPP
#define INC__SOCKETOPTIONS_HPP

#include <string>
#include <map>
#include <set>
#include <vector>
#include "../srtcore/srt.h" // Devel path

#ifdef WIN32
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

extern std::set<std::string> false_names, true_names;

struct SocketOption
{
    enum Type { STRING = 0, INT, INT64, BOOL };
    enum Binding { PRE = 0, POST };
    enum Domain { SYSTEM, SRT };
    enum Mode {FAILURE = -1, LISTENER = 0, CALLER = 1, RENDEZVOUS = 2};

    std::string name;
    int protocol;
    int symbol;
    Type type;
    Binding binding;

    template <Domain D>
    bool apply(int socket, std::string value) const;

    template <Domain D, Type T>
    bool applyt(int socket, std::string value) const;

    template <Domain D>
    static int setso(int socket, int protocol, int symbol, const void* data, size_t size);

    template<Type T>
    static void extract(std::string value, OptionValue& val);
};

template<> inline
int SocketOption::setso<SocketOption::SRT>(int socket, int /*ignored*/, int sym, const void* data, size_t size)
{
    return srt_setsockopt(socket, 0, SRT_SOCKOPT(sym), data, size);
}

template<> inline
int SocketOption::setso<SocketOption::SYSTEM>(int socket, int proto, int sym, const void* data, size_t size)
{
    return ::setsockopt(socket, proto, sym, (const char *)data, size);
}

template<> inline
void SocketOption::extract<SocketOption::STRING>(std::string value, OptionValue& o)
{
    o.s = value;
    o.value = o.s.data();
    o.size = o.s.size();
}

template<>
inline void SocketOption::extract<SocketOption::INT>(std::string value, OptionValue& o)
{
    try
    {
        o.i = stoi(value, 0, 0);
        o.value = &o.i;
        o.size = sizeof o.i;
        return;
    }
    catch (...) // stoi throws
    {
        return; // do not change o
    }
}

template<>
inline void SocketOption::extract<SocketOption::INT64>(std::string value, OptionValue& o)
{
    try
    {
        long long vall = stoll(value);
        o.l = vall; // int64_t resolves to either 'long long', or 'long' being 64-bit integer
        o.value = &o.l;
        o.size = sizeof o.l;
        return ;
    }
    catch (...) // stoll throws
    {
        return ;
    }
}

template<>
inline void SocketOption::extract<SocketOption::BOOL>(std::string value, OptionValue& o)
{
    bool val;
    if ( false_names.count(value) )
        val = false;
    else if ( true_names.count(value) )
        val = true;
    else
        return;

    o.b = val;
    o.value = &o.b;
    o.size = sizeof o.b;
}

template <SocketOption::Domain D, SocketOption::Type T>
inline bool SocketOption::applyt(int socket, std::string value) const
{
    OptionValue o; // common meet point
    extract<T>(value, o);
    int result = setso<D>(socket, protocol, symbol, o.value, o.size);
    return result != -1;
}


template<SocketOption::Domain D>
inline bool SocketOption::apply(int socket, std::string value) const
{
    switch ( type )
    {
#define SRT_HANDLE_TYPE(ty) case ty: return applyt<D, ty>(socket, value)

        SRT_HANDLE_TYPE(STRING);
        SRT_HANDLE_TYPE(INT);
        SRT_HANDLE_TYPE(INT64);
        SRT_HANDLE_TYPE(BOOL);

#undef SRT_HANDLE_TYPE
    }
    return false;
}

namespace {
SocketOption srt_options [] {
    { "maxbw", 0, SRTO_MAXBW, SocketOption::INT64, SocketOption::PRE },
    { "pbkeylen", 0, SRTO_PBKEYLEN, SocketOption::INT, SocketOption::PRE },
    { "passphrase", 0, SRTO_PASSPHRASE, SocketOption::STRING, SocketOption::PRE },

    { "mss", 0, SRTO_MSS, SocketOption::INT, SocketOption::PRE },
    { "fc", 0, SRTO_FC, SocketOption::INT, SocketOption::PRE },
    { "sndbuf", 0, SRTO_SNDBUF, SocketOption::INT, SocketOption::PRE },
    { "rcvbuf", 0, SRTO_RCVBUF, SocketOption::INT, SocketOption::PRE },
    { "ipttl", 0, SRTO_IPTTL, SocketOption::INT, SocketOption::PRE },
    { "iptos", 0, SRTO_IPTOS, SocketOption::INT, SocketOption::PRE },
    { "inputbw", 0, SRTO_INPUTBW, SocketOption::INT64, SocketOption::POST },
    { "oheadbw", 0, SRTO_OHEADBW, SocketOption::INT, SocketOption::POST },
    { "latency", 0, SRTO_LATENCY, SocketOption::INT, SocketOption::PRE },
    { "tsbpddelay", 0, SRTO_TSBPDDELAY, SocketOption::INT, SocketOption::PRE },
    { "tlpktdrop", 0, SRTO_TLPKTDROP, SocketOption::BOOL, SocketOption::PRE },
    { "nakreport", 0, SRTO_NAKREPORT, SocketOption::BOOL, SocketOption::PRE },
    { "conntimeo", 0, SRTO_CONNTIMEO, SocketOption::INT, SocketOption::PRE }
};
}

SocketOption::Mode SrtConfigurePre(SRTSOCKET socket, std::string host, std::map<std::string, std::string> options, std::vector<std::string>* failures = 0);
void SrtConfigurePost(SRTSOCKET socket, std::map<std::string, std::string> options, std::vector<std::string>* failures = 0);

#endif
