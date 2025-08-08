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
#include "ofmt.h"

template<
    class Value,
    class CharT,
    class Traits = std::char_traits<CharT>
>
inline std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& os,
        const srt::internal::fmt_proxy<Value, CharT>& valproxy
)
{
    valproxy.sendto(os);
    return os;
}

template<
    class Value,
    class CharT,
    class Traits = std::char_traits<CharT>
>
inline std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& os,
        const srt::internal::fmt_simple_proxy<Value>& valproxy
)
{
    valproxy.sendto(os);
    return os;
}

// Note: if you use iostream and sending to the stream, then
// sending std::string will still use the built-in formatting
// facilities, but you can pass the string through fmt() and
// this way you make a stringview-forwarder and formating gets
// bypassed.
inline std::ostream& operator<<( std::ostream& os, const srt::internal::fmt_stringview& v)
{
    os.write(v.data(), v.size());
    return os;
}

namespace srt
{
inline std::pair<const struct tm*, const char*> fmt(const struct tm& tim, const char* format)
{
    return std::make_pair(&tim, format);
}

template<class AnyFormatStream>
inline AnyFormatStream& operator<<(AnyFormatStream& out, std::pair<const struct tm*, const char*> args)
{
    out.forward(std::put_time(args.first, args.second));
    return out;
}

}


#endif
