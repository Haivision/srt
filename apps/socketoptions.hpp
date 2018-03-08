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

extern const std::set<std::string> false_names, true_names;

struct SocketOption
{
    enum Type { STRING = 0, INT, INT64, BOOL, ENUM };
    enum Binding { PRE = 0, POST };
    enum Domain { SYSTEM, SRT };
    enum Mode {FAILURE = -1, LISTENER = 0, CALLER = 1, RENDEZVOUS = 2};

    std::string name;
    int protocol;
    int symbol;
    Binding binding;
    Type type;
    const std::map<std::string, int>* valmap;

    template <Domain D>
    bool apply(int socket, std::string value) const;

    template <Domain D, Type T>
    bool applyt(int socket, std::string value) const;

    template <Domain D>
    static int setso(int socket, int protocol, int symbol, const void* data, size_t size);

    template<Type T>
    void extract(std::string value, OptionValue& val) const;
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
void SocketOption::extract<SocketOption::STRING>(std::string value, OptionValue& o) const
{
    o.s = value;
    o.value = o.s.data();
    o.size = o.s.size();
}

template<>
inline void SocketOption::extract<SocketOption::INT>(std::string value, OptionValue& o) const
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
inline void SocketOption::extract<SocketOption::INT64>(std::string value, OptionValue& o) const
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
inline void SocketOption::extract<SocketOption::BOOL>(std::string value, OptionValue& o) const
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

template<>
inline void SocketOption::extract<SocketOption::ENUM>(std::string value, OptionValue& o) const
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
            return;
        }
    }

    // Fallback: try interpreting it as integer.
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
        SRT_HANDLE_TYPE(ENUM);

#undef SRT_HANDLE_TYPE
    }
    return false;
}

extern const std::map<std::string, int> enummap_transtype;

namespace {
const SocketOption srt_options [] {
    { "maxbw", 0, SRTO_MAXBW, SocketOption::PRE, SocketOption::INT64, nullptr},
    { "pbkeylen", 0, SRTO_PBKEYLEN, SocketOption::PRE, SocketOption::INT, nullptr},
    { "passphrase", 0, SRTO_PASSPHRASE, SocketOption::PRE, SocketOption::STRING, nullptr},

    { "mss", 0, SRTO_MSS, SocketOption::PRE, SocketOption::INT, nullptr},
    { "fc", 0, SRTO_FC, SocketOption::PRE, SocketOption::INT, nullptr},
    { "sndbuf", 0, SRTO_SNDBUF, SocketOption::PRE, SocketOption::INT, nullptr},
    { "rcvbuf", 0, SRTO_RCVBUF, SocketOption::PRE, SocketOption::INT, nullptr},
    { "ipttl", 0, SRTO_IPTTL, SocketOption::PRE, SocketOption::INT, nullptr},
    { "iptos", 0, SRTO_IPTOS, SocketOption::PRE, SocketOption::INT, nullptr},
    { "inputbw", 0, SRTO_INPUTBW, SocketOption::POST, SocketOption::INT64, nullptr},
    { "oheadbw", 0, SRTO_OHEADBW, SocketOption::POST, SocketOption::INT, nullptr},
    { "latency", 0, SRTO_LATENCY, SocketOption::PRE, SocketOption::INT, nullptr},
    { "tsbpddelay", 0, SRTO_TSBPDDELAY, SocketOption::PRE, SocketOption::INT, nullptr},
    { "tlpktdrop", 0, SRTO_TLPKTDROP, SocketOption::PRE, SocketOption::BOOL, nullptr},
    { "nakreport", 0, SRTO_NAKREPORT, SocketOption::PRE, SocketOption::BOOL, nullptr},
    { "conntimeo", 0, SRTO_CONNTIMEO, SocketOption::PRE, SocketOption::INT, nullptr},
    { "lossmaxttl", 0, SRTO_LOSSMAXTTL, SocketOption::PRE, SocketOption::INT, nullptr},
    { "rcvlatency", 0, SRTO_RCVLATENCY, SocketOption::PRE, SocketOption::INT, nullptr},
    { "peerlatency", 0, SRTO_PEERLATENCY, SocketOption::PRE, SocketOption::INT, nullptr},
    { "minversion", 0, SRTO_MINVERSION, SocketOption::PRE, SocketOption::INT, nullptr},
    { "streamid", 0, SRTO_STREAMID, SocketOption::PRE, SocketOption::STRING, nullptr},
    { "smoother", 0, SRTO_SMOOTHER, SocketOption::PRE, SocketOption::STRING, nullptr},
    { "messageapi", 0, SRTO_MESSAGEAPI, SocketOption::PRE, SocketOption::BOOL, nullptr},
    { "payloadsize", 0, SRTO_PAYLOADSIZE, SocketOption::PRE, SocketOption::INT, nullptr},
    { "transtype", 0, SRTO_TRANSTYPE, SocketOption::PRE, SocketOption::ENUM, &enummap_transtype },
    { "fastdrift", 0, SRTO_FASTDRIFT, SocketOption::PRE, SocketOption::BOOL, nullptr }
};
}

SocketOption::Mode SrtConfigurePre(SRTSOCKET socket, std::string host, std::map<std::string, std::string> options, std::vector<std::string>* failures = 0);
void SrtConfigurePost(SRTSOCKET socket, std::map<std::string, std::string> options, std::vector<std::string>* failures = 0);

#endif
