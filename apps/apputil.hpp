/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#ifndef INC__APPCOMMON_H
#define INC__APPCOMMON_H

#include <string>
#include <map>
#include <set>
#include <vector>

#if _WIN32

// Keep this below commented out.
// This is for a case when you need cpp debugging on Windows.
//#ifdef _WINSOCKAPI_
//#error "You include <winsock.h> somewhere, remove it. It causes conflicts"
//#endif

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// WIN32 API does not have sleep() and usleep(), Although MINGW does.
#ifdef __MINGW32__
#include <unistd.h>
#else
extern "C" inline int sleep(int seconds) { Sleep(seconds * 1000); return 0; }
#endif

inline bool SysInitializeNetwork()
{
    WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    return WSAStartup(wVersionRequested, &wsaData) == 0;
}

inline void SysCleanupNetwork()
{
    WSACleanup();
}

#else
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Nothing needs to be done on POSIX; this is a Windows problem.
inline bool SysInitializeNetwork() {return true;}
inline void SysCleanupNetwork() {}

#endif

#include "srt.h"

#ifdef _WIN32
inline int SysError() { return ::GetLastError(); }
#else
inline int SysError() { return errno; }
#endif

sockaddr_in CreateAddrInet(const std::string& name, unsigned short port);
std::string Join(const std::vector<std::string>& in, std::string sep);


inline bool CheckTrue(const std::vector<std::string>& in)
{
    if (in.empty())
        return true;

    const std::set<std::string> false_vals = { "0", "no", "off", "false" };
    if (false_vals.count(in[0]))
        return false;

    return true;

    //if (in[0] != "false" && in[0] != "off")
    //    return true;

    //return false;
}

template<class Number>
Number StrToNumber(const std::string& )
{
    typename Number::incorrect_version wrong = Number::incorrect_version;
    return Number();
}

#define STON(type, function) \
template<> inline type StrToNumber(const std::string& s) { return function (s, 0, 0); }

STON(int, stoi);
STON(size_t, stoul);
STON(unsigned int, stoul);
STON(long long, stoll);
STON(unsigned long long, stoull);

#undef STON

typedef std::map<std::string, std::vector<std::string>> options_t;

struct OutList
{
    typedef std::vector<std::string> type;
    static type process(const options_t::mapped_type& i) { return i; }
};

struct OutString
{
    typedef std::string type;
    static type process(const options_t::mapped_type& i) { return Join(i, " "); }
};

struct NumberAutoConvert
{
    std::string value;

    NumberAutoConvert() {}
    NumberAutoConvert(const std::string& arg): value(arg) {}
    NumberAutoConvert(const char* arg): value(arg) {}

    template<class Number>
    operator Number()
    {
        return StrToNumber<Number>(value);
    }
};

struct OutNumber
{
    typedef NumberAutoConvert type;
    static type process(const options_t::mapped_type& i)
    {
        // Numbers can't be joined, use the "last overrides" rule.
        if (i.empty())
            return {"0"};

        return type { i.back() };
    }
};

template <class Number>
struct OutNumberAs
{
    typedef Number type;
    static type process(const options_t::mapped_type& i)
    {
        return OutNumber::process(i);
    }
};


struct OutBool
{
    typedef bool type;
    static type process(const options_t::mapped_type& i) { return CheckTrue(i); }
};

struct OptionName
{
    std::string helptext;
    std::string main_name;
    std::set<std::string> names;

    template <class... Args>
    OptionName(std::string ht, std::string first, Args... rest)
        : helptext(ht), main_name(first),
          names {first, rest...}
    {
    }

    OptionName(std::initializer_list<std::string> args): main_name(*args.begin()), names(args) {}

    operator std::set<std::string>() { return names; }
    operator const std::set<std::string>() const { return names; }
};



template <class OutType, class OutValue> inline
typename OutType::type Option(const options_t&, OutValue deflt=OutValue()) { return deflt; }

template <class OutType, class OutValue, class... Args> inline
typename OutType::type Option(const options_t& options, OutValue deflt, std::string key, Args... further_keys)
{
    auto i = options.find(key);
    if ( i == options.end() )
        return Option<OutType>(options, deflt, further_keys...);
    return OutType::process(i->second);
}

template <class OutType, class OutValue> inline
typename OutType::type Option(const options_t& options, OutValue deflt, const OptionName& oname)
{
    for (auto key: oname.names)
    {
        auto i = options.find(key);
        if ( i != options.end() )
            return OutType::process(i->second);
    }
    return deflt;
}

inline bool OptionPresent(const options_t& options, const std::set<std::string>& keys)
{
    for (auto key: keys)
    {
        auto i = options.find(key);
        if ( i != options.end() )
            return true;
    }
    return false;
}

struct OptionScheme
{
    const OptionName& id;
    enum Args { ARG_NONE, ARG_ONE, ARG_VAR } type;
};

options_t ProcessOptions(char* const* argv, int argc, std::vector<OptionScheme> scheme);
std::string OptionHelpItem(const OptionName& o);

bool IsTargetAddrSelf(const sockaddr* boundaddr, const sockaddr* targetaddr);

// Statistics module

enum SrtStatsPrintFormat
{
    SRTSTATS_PROFMAT_INVALID = -1,
    SRTSTATS_PROFMAT_2COLS = 0,
    SRTSTATS_PROFMAT_JSON,
    SRTSTATS_PROFMAT_CSV
};

SrtStatsPrintFormat ParsePrintFormat(std::string pf);

class SrtStatsWriter
{
public:
    virtual std::string WriteStats(int sid, const CBytePerfMon& mon) = 0;
    virtual std::string WriteBandwidth(double mbpsBandwidth) = 0;
};

std::shared_ptr<SrtStatsWriter> SrtStatsWriterFactory(SrtStatsPrintFormat printformat);


#endif // INC__APPCOMMON_H
