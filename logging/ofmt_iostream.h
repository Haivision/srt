// Formatting library for C++ - C++03 compat version of on-demand tagged format API.
//
// This adds the abilities for formatting to be used with iostream.

#ifndef INC_SRT_OFMT_IOSTREAM_H
#define INC_SRT_OFMT_IOSTREAM_H

#include <iostream>
#include "ofmt.h"

template<
    class Value,
    class CharT,
    class Traits = std::char_traits<CharT>
>
inline std::basic_ostream<CharT, Traits>& operator<<(
        std::basic_ostream<CharT, Traits>& os,
        const hvu::internal::fmt_proxy<Value, CharT>& valproxy
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
        const hvu::internal::fmt_simple_proxy<Value>& valproxy
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
inline std::ostream& operator<<( std::ostream& os, const hvu::internal::fmt_stringview& v)
{
    os.write(v.data(), v.size());
    return os;
}


#endif
