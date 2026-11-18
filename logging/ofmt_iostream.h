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
written by
   Haivision Systems Inc.
 *****************************************************************************/

// Formatting library for C++ - C++03 compat version of on-demand tagged format API.
//
// This adds the abilities for formatting to be used with iostream.

#ifndef INC_SRT_OFMT_IOSTREAM_H
#define INC_SRT_OFMT_IOSTREAM_H

#include <iostream>
#include <iomanip>
#include <utility>
#include <ctime>
#include "ofmt.h"

template<
    class Value,
    class CharT,
    class Sender,
    class Traits
>
inline std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& os,
        const hvu::internal::fmt_proxy_template<Value, Sender>& valproxy
)
{
    valproxy.sendto(os);
    return os;
}


// Note: if you use iostream and sending to the stream, then
// sending std::string will still use the built-in formatting
// facilities, but you can pass the string through fmt() and
// this way you make a stringview-forwarder and formatting gets
// bypassed.
inline std::ostream& operator<<(std::ostream& os, const hvu::internal::ofmt_stringview& v)
{
    os.write(v.data(), v.size());
    return os;
}

namespace hvu
{
namespace internal
{
struct snd_time_tm
{
    const char* format;

    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
#if OFMT_HAVE_CXX11
        os << std::put_time(&val, format);
#else
        // The version of std::put_time is better, but only available in C++11
        // Use a bit harsher version with strftime and crazy large enough
        // buffer to accommodate most crazy format, and even so prone to
        // format errors.
        char ftimebuf[256];
        if (strftime(ftimebuf, sizeof (ftimebuf), format, &val))
        {
            os << ftimebuf;
        }
        else
        {
            os << CreateRawString_FWD("<TIME FORMAT ERROR>");
        }
#endif
    }
};
}

inline internal::fmt_proxy_template<struct tm, internal::snd_time_tm> fmt(const struct tm& tim, const char* format)
{
    internal::snd_time_tm fmter = {format}; // C++98 friendly version
    return fmt_make_proxy(tim, fmter);
}

#if OFMT_HAVE_CXX11
// As per iostream, add quick wrappers for cout/cerr
template<class... Args>
inline void ofcout(const Args&... args)
{
    ofmt_refs sout(std::cout);
    sout.print(args...);
}

template<class... Args>
inline void ofcoutl(const Args&... args)
{
    ofmt_refs sout(std::cout);
    sout.print(args...);
    std::cout << std::endl;
}

template<class... Args>
inline void ofcerr(const Args&... args)
{
    ofmt_refs sout(std::cerr);
    sout.print(args...);
}

template<class... Args>
inline void ofcerrl(const Args&... args)
{
    ofmt_refs sout(std::cerr);
    sout.print(args...);
    std::cerr << std::endl;
}

#endif

}

#endif
