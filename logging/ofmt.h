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
// This is a header-only lightweight C++03-compatible formatting library,
// which provides the on-demand tagged format API and iostream-style wrapper
// for FILE type from stdio. It has nothing to do with the rest of the {fmt}
// library, except that it reuses the namespace.

// USAGE:
//
// 1. Using iostream style:
//
// ofmtbufstream sout;
//
// sout << "Value: " << v << " (" << fmt(v, fmtc().hex().width(2).fillzero()) << ")\n";
//
// NOTE: When passing a string literal, consider using "Value"_V (C++11 only)
// or OFMT_RAWSTR("Value"). Unfortunately C++ doesn't distinguish "Value" and
// char [20] v = "Value"; both here contain "Value\0", but sizeof(v) for them
// returns the size of the allocated space, not size of the string. Although
// the compiler should expand strlen() in place for literals, note that it
// won't do it if optimizations are turned off.
//
// 2. Using variadic style:
//
// sout.print("Value: ", v, " (", fmt(v, fmtc().hex().width(2).fillzero()), ")\n");
//
//
// OFMT has also a potential to be used together with iostream, but it requires more
// definition support. This is only the basic fragment to be used with the logging system,
// hence it provides only a wrapper over std::stringstream.

#ifndef INC_HVU_OFMT_H
#define INC_HVU_OFMT_H

#include <string>
#include <cstring>
#include <vector>
#include <list>
#include <sstream>

#if (defined(__cplusplus) && __cplusplus > 199711L) \
 || (defined(_MSVC_LANG) && _MSVC_LANG > 199711L) // Some earlier versions get this wrong
#define OFMT_HAVE_CXX11 1
#else
#define OFMT_HAVE_CXX11 0
#endif


namespace hvu
{

template<class CharType>
struct basic_fmtc
{
protected:
    typedef std::basic_ios<CharType> ios;

    typedef typename ios::fmtflags fmtflg_t;
    fmtflg_t fmtflg;
    unsigned short widthval;
    unsigned short precisionval;
    // Find a way to adjust it to wchar_t if need be
    char fillval;

    union
    {
        struct
        {
            bool widthbit:1;
            bool precisionbit:1;
            bool leadzerobit:1;
            bool fillbit:1;
        } flags;
        unsigned char allbits;
    };

    // Mimics the ios::flags, althouh as unsafe it's internal.
    void setf(fmtflg_t flags, fmtflg_t mask)
    {
        fmtflg_t old = fmtflg & ~mask;
        fmtflg = old | flags;
    }

    void setf(fmtflg_t f)
    {
        fmtflg |= f;
    }

public:
    basic_fmtc():
        fmtflg(fmtflg_t()),
        widthval(0),
        precisionval(6),
        fillval(' '),
        allbits(0)
    {
    }

#define OFMTC_TAG(name, body) basic_fmtc& name () { body; return *this; }
#define OFMTC_TAG_VAL(name, body) basic_fmtc& name (int val) { body; return *this; }
#define OFMTC_TAG_VAL_TYPE(type, name, body) basic_fmtc& name (type val) { body; return *this; }

    OFMTC_TAG_VAL(width, flags.widthbit = true; widthval = std::abs(val));
    OFMTC_TAG_VAL(precision, flags.precisionbit = true; precisionval = std::abs(val));
    OFMTC_TAG_VAL_TYPE(CharType, fill, flags.fillbit = true; fillval = val);

    OFMTC_TAG(left, setf(ios::left, ios::adjustfield));
    OFMTC_TAG(right, setf(ios::right, ios::adjustfield));
    OFMTC_TAG(internal, setf(ios::internal, ios::adjustfield));
    OFMTC_TAG(dec, setf(ios::dec, ios::basefield));
    OFMTC_TAG(hex, setf(ios::hex, ios::basefield));
    OFMTC_TAG(oct, setf(ios::oct, ios::basefield));
    OFMTC_TAG(uhex, setf(ios::hex, ios::basefield); setf(ios::uppercase));
    OFMTC_TAG(uoct, setf(ios::oct, ios::basefield); setf(ios::uppercase));
    OFMTC_TAG(general, (void)0);
    OFMTC_TAG(ugeneral, setf(ios::uppercase));
#if __cplusplus > 201103L
    OFMTC_TAG(fhex, setf(ios::fixed | ios::scientific, ios::floatfield));
    OFMTC_TAG(ufhex, setf(ios::uppercase); setf(ios::fixed | ios::scientific, ios::floatfield));
#endif
    OFMTC_TAG(exp, setf(ios::scientific, ios::floatfield));
    OFMTC_TAG(scientific, setf(ios::scientific, ios::floatfield));
    OFMTC_TAG(uexp, setf(ios::scientific, ios::floatfield); setf(ios::uppercase));
    OFMTC_TAG(uscientific, setf(ios::scientific, ios::floatfield); setf(ios::uppercase));
    OFMTC_TAG(fixed, setf(ios::fixed, ios::floatfield));
    OFMTC_TAG(nopos, (void)0);
    OFMTC_TAG(showpos, setf(ios::showpos));
    OFMTC_TAG(showbase, setf(ios::showbase));
    OFMTC_TAG(showpoint, setf(ios::showpoint));
    OFMTC_TAG(fillzero, flags.leadzerobit = true);

#undef OFMTC_TAG
#undef OFMTC_TAG_VAL
#undef OFMTC_TAG_VAL_TYPE

    void apply_detailed(std::basic_ostream<CharType>& os) const
    {
        if (flags.widthbit)
            os.width(widthval);

        if (flags.precisionbit)
            os.precision(precisionval);

        if (flags.leadzerobit)
        {
            os.setf(ios::internal, ios::adjustfield);
            os.fill(os.widen('0'));
        }
        else if (flags.fillbit)
        {
            os.fill(os.widen(fillval));
        }
    }

    void apply(std::basic_ostream<CharType>& os) const
    {
        os.flags(fmtflg);
        apply_detailed(os);
    }

    void apply_ontop(std::basic_ostream<CharType>& os) const
    {
        fmtflg_t oldflags = os.flags();

        // "unfielded" are flags that are single only.
        //
        // So, all single-bit only flags should be copied as they are
        static const fmtflg_t unfielded = ~(ios::adjustfield | ios::basefield | ios::floatfield);
        fmtflg_t newflags = fmtflg | (oldflags & unfielded);

        // For "fielded" flags, copy the value from the existing flags
        // only if none of the flags in particular field are set in THIS configuration.
        if ((newflags & ios::adjustfield) == 0)
            newflags |= oldflags & ios::adjustfield;
        if ((newflags & ios::basefield) == 0)
            newflags |= oldflags & ios::basefield;
        if ((newflags & ios::floatfield) == 0)
            newflags |= oldflags & ios::floatfield;

        os.flags(newflags);

        apply_detailed(os);
    }

};

typedef basic_fmtc<char> fmtc;
typedef basic_fmtc<wchar_t> wfmtc;

// fmt(val, fmtc().alt().hex().width(10))

namespace internal
{
template <typename Value, typename CharType>
struct fmt_proxy
{
    const Value& val; // ERROR: invalidly declared function? -->
               // Iostream manipulators should not be sent to the stream.
               // use fmt() with fmtc() instead.
    basic_fmtc<CharType> format_spec;

    fmt_proxy(const Value& v, const basic_fmtc<CharType>& f): val(v), format_spec(f) {}

    template <class OutStream>
    void sendto(OutStream& os) const
    {
        std::stringstream tmp;
        format_spec.apply(tmp);
        tmp << val;
        os << tmp.rdbuf();
    }
};

template <typename Value, typename CharType>
struct fmt_stateous_proxy
{
    const Value& val; // ERROR: invalidly declared function? -->
               // Iostream manipulators should not be sent to the stream.
               // use fmt() with fmtc() instead.
    basic_fmtc<CharType> format_spec;

    fmt_stateous_proxy(const Value& v, const basic_fmtc<CharType>& f): val(v), format_spec(f) {}

    template <class OutStream>
    void sendto(OutStream& os) const
    {
        format_spec.apply_ontop(os);
        os << val;
    }
};


template <typename Value, typename CharType, typename Manip>
struct fmt_ios_proxy_1
{
    const Value& val; // ERROR: invalidly declared function? -->
               // Iostream manipulators should not be sent to the stream.
               // use fmt() with fmtc() instead.
    const Manip& manip;

    fmt_ios_proxy_1(const Value& v, const Manip& m): val(v), manip(m) {}

    template <class OutStream>
    void sendto(OutStream& os) const
    {
        std::stringstream tmp;
        tmp << manip << val;
        os << tmp.rdbuf();
    }
};

template <typename Value>
struct fmt_simple_proxy
{
    const Value& val; // ERROR: invalidly declared function? -->
               // Iostream manipulators should not be sent to the stream.
               // use fmt() with fmtc() instead.
    fmt_simple_proxy(const Value& v): val(v) {}

    template <class OutStream>
    void sendto(OutStream& os) const
    {
        os << val;
    }
};

// !!! IMPORTANT !!!
// THIS CLASS IS FOR THE PURPOSE OF DIRECT WRITING TO THE STREAM ONLY.
// DO NOT use this class for any other purpose and use it also with
// EXTREME CARE.
// The only role of this class is to pass the string with KNOWN SIZE
// written in either a string literal or an array of characters to
// the output stream using its `write` method, that is, with bypassing
// any formatting facilities.
struct fmt_stringview
{
private:
    const char* d;
    size_t s;

public:
    explicit fmt_stringview(const char* dd, size_t ss): d(dd), s(ss) {}

    const char* data() const { return d; }
    size_t size() const { return s; }

    const char* begin() const { return d; }
    const char* end() const { return d + s; }
};

template <size_t N>
struct check_minus_1
{
    static const size_t value = N - 1;
};

template<>
struct check_minus_1<0>
{
};

// NOTE: DO NOT USE THIS FUNCTION DIRECTLY.
template<size_t N>
inline fmt_stringview CreateRawString_FWD(const char (&ref)[N])
{
    const char* ptr = ref;
    return fmt_stringview(ptr, check_minus_1<N>::value);
}
}

inline internal::fmt_stringview fmt_rawstr(const char* dd, size_t ss)
{
    return internal::fmt_stringview(dd, ss);
}

inline internal::fmt_stringview fmt_rawstr(const std::string& s)
{
    return internal::fmt_stringview(s.data(), s.size());
}

template <class Value> inline
internal::fmt_simple_proxy<Value> fmt(const Value& val)
{
    return internal::fmt_simple_proxy<Value>(val);
}

template <class Value> inline
internal::fmt_proxy<Value, char> fmt(const Value& val, const fmtc& config)
{
    return internal::fmt_proxy<Value, char>(val, config);
}

template <class Value> inline
internal::fmt_stateous_proxy<Value, char> fmtx(const Value& val, const fmtc& config)
{
    return internal::fmt_stateous_proxy<Value, char>(val, config);
}

// A special version that allows using iomanip manipulators
// with the `fmt` call. Currently available only a simple version
// with a single manipulator. The version with multiple manipulators
// requires collecting manipulators in a tuple without knowing their
// types and with sending the tuple elements one by one to the stream
// using operator<<, which isn't trivial in C++11 and requires extra
// facilities to make it possible. Postponed.
template <class Value, class Manip> inline
internal::fmt_ios_proxy_1<Value, char, Manip> fmt(const Value& val, const Manip& man)
{
    return internal::fmt_ios_proxy_1<Value, char, Manip>(val, man);
}

// XXX Make basic_ofmtbufstream etc.
class ofmtbufstream
{
    friend class ofmtrefstream;
protected:
    std::stringstream buffer;

public:
    ofmtbufstream() {}

    std::ostream& base() { return buffer; }

    // Extra constructor that allows the stream to have some
    // initial contents. Only string types supported
    ofmtbufstream(const internal::fmt_stringview& s)
    {
        buffer.write(s.data(), s.size());
    }

    ofmtbufstream(const std::string& s)
    {
        buffer.write(s.data(), s.size());
    }

    // This is to allow pre-configuration before sending.
    // Only use fmtx (not fmt) if you want to keep the state.
    void setup(const basic_fmtc<char>& fc)
    {
        fc.apply(buffer);
    }

    void clear()
    {
        buffer.clear();
    }

    // Expose
    ofmtbufstream& write(const char* buf, size_t size)
    {
        buffer.write(buf, size);
        return *this;
    }

    ofmtbufstream& operator<<(const char* t)
    {
        size_t len = std::strlen(t);
        buffer.write(t, len);
        return *this;
    }

    // Treat a fixed-size array just like a pointer
    // to the first only and still use strlen(). This
    // is because it usually designates a buffer that
    // has N as the spare space, so you still need to
    // mind the NUL terminator character. For string
    // literals you should use OFMT_RAWSTR macro that
    // gets the set of pointer and size from the string
    // as an array, but also makes sure that the argument
    // is a string literal.
    // Unfortunately C++ is unable to distinguish the
    // fixed array (with spare buffer space) from a string
    // literal (which has only one extra termination character).
    // The compiler still can usually call strlen at
    // compile time, but not if you are in a debug mode.
    template <size_t N>
    ofmtbufstream& operator<<(const char (&t)[N])
    {
        size_t len = std::strlen(t);
        buffer.write(t, len);
        return *this;
    }

    ofmtbufstream& operator<<(const std::string& s)
    {
        buffer.write(s.data(), s.size());
        return *this;
    }

    // XXX Add also a version for std::string_view, if C++17.
    ofmtbufstream& operator<<(const internal::fmt_stringview& s)
    {
        buffer.write(s.data(), s.size());
        return *this;
    }

    template<class ValueType>
    ofmtbufstream& operator<<(const internal::fmt_simple_proxy<ValueType>& prox)
    {
        prox.sendto(buffer);
        return *this;
    }

    template<class ValueType>
    ofmtbufstream& operator<<(const internal::fmt_proxy<ValueType, char>& prox)
    {
        prox.sendto(buffer);
        return *this;
    }

    template<class ValueType>
    ofmtbufstream& operator<<(const internal::fmt_stateous_proxy<ValueType, char>& prox)
    {
        prox.sendto(buffer);
        return *this;
    }

    template<class ValueType, class Manip>
    ofmtbufstream& operator<<(const internal::fmt_ios_proxy_1<ValueType, char, Manip>& prox)
    {
        prox.sendto(buffer);
        return *this;
    }

    template<class Value> inline
    ofmtbufstream& operator<<(const Value& val)
    {
        return *this << fmt(val);
    }

    // A utility function to send the argument directly
    // to the buffer
    template<class Value> inline
    ofmtbufstream& forward(const Value& val)
    {
        buffer << val;
        return *this;
    }

    ofmtbufstream& operator<<(const ofmtbufstream& source)
    {
        buffer << source.buffer.rdbuf();
        return *this;
    }

    std::string str() const
    {
        return buffer.str();
    }

// Additionally for C++11
#if OFMT_HAVE_CXX11
    void print_chain()
    {
    }

    template<typename Arg1, typename... Args>
    void print_chain(const Arg1& arg1, const Args&... args)
    {
        *this << arg1;
        print_chain(args...);
    }

    template<typename... Args>
    ofmtbufstream& print(const Args&... args)
    {
        print_chain(args...);
        return *this;
    }

    template<typename... Args>
    ofmtbufstream& puts(const Args&... args)
    {
        print_chain(args...);
        buffer << std::endl;
        return *this;
    }
#endif
};

class ofmtrefstream
{
protected:
    std::ostream& refstream;

public:
    ofmtrefstream(std::ostream& src) : refstream(src) {}

    std::ostream& base() { return refstream; }

    // Expose
    ofmtrefstream& write(const char* buf, size_t size)
    {
        refstream.write(buf, size);
        return *this;
    }

    ofmtrefstream& operator<<(const char* t)
    {
        size_t len = std::strlen(t);
        this->write(t, len);
        return *this;
    }

    // Treat a fixed-size array just like a pointer
    // to the first only and still use strlen(). This
    // is because it usually designates a buffer that
    // has N as the spare space, so you still need to
    // mind the NUL terminator character. For string
    // literals you should use OFMT_RAWSTR macro that
    // gets the set of pointer and size from the string
    // as an array, but also makes sure that the argument
    // is a string literal.
    // Unfortunately C++ is unable to distinguish the
    // fixed array (with spare buffer space) from a string
    // literal (which has only one extra termination character).
    // The compiler still can usually call strlen at
    // compile time, but not if you are in a debug mode.
    template <size_t N>
    ofmtrefstream& operator<<(const char (&t)[N])
    {
        size_t len = std::strlen(t);
        this->write(t, len);
        return *this;
    }

    ofmtrefstream& operator<<(const std::string& s)
    {
        this->write(s.data(), s.size());
        return *this;
    }

    // XXX Add also a version for std::string_view, if C++17.
    ofmtrefstream& operator<<(const internal::fmt_stringview& s)
    {
        this->write(s.data(), s.size());
        return *this;
    }

    template<class ValueType>
    ofmtrefstream& operator<<(const internal::fmt_simple_proxy<ValueType>& prox)
    {
        prox.sendto(refstream);
        return *this;
    }

    template<class ValueType>
    ofmtrefstream& operator<<(const internal::fmt_proxy<ValueType, char>& prox)
    {
        prox.sendto(refstream);
        return *this;
    }

    template<class ValueType>
    ofmtrefstream& operator<<(const internal::fmt_stateous_proxy<ValueType, char>& prox)
    {
        prox.sendto(refstream);
        return *this;
    }

    template<class ValueType, class Manip>
    ofmtrefstream& operator<<(const internal::fmt_ios_proxy_1<ValueType, char, Manip>& prox)
    {
        prox.sendto(refstream);
        return *this;
    }

    template<class Value> inline
    ofmtrefstream& operator<<(const Value& val)
    {
        return *this << fmt(val);
    }

    // A utility function to send the argument directly
    // to the buffer
    template<class Value> inline
    ofmtrefstream& forward(const Value& val)
    {
        refstream << val;
        return *this;
    }

    ofmtrefstream& operator<<(const ofmtbufstream& source)
    {
        refstream << source.buffer.rdbuf();
        return *this;
    }

// Additionally for C++11
#if (defined(__cplusplus) && __cplusplus > 199711L) \
 || (defined(_MSVC_LANG) && _MSVC_LANG > 199711L) // Some earlier versions get this wrong
    void print_chain()
    {
    }

    template<typename Arg1, typename... Args>
    void print_chain(const Arg1& arg1, const Args&... args)
    {
        *this << arg1;
        print_chain(args...);
    }

    template<typename... Args>
    ofmtrefstream& print(const Args&... args)
    {
        print_chain(args...);
        return *this;
    }

    template<typename... Args>
    ofmtrefstream& puts(const Args&... args)
    {
        print_chain(args...);
        refstream << std::endl;
        return *this;
    }
#endif
};

// Additionally for C++11
#if (defined(__cplusplus) && __cplusplus > 199711L) \
 || (defined(_MSVC_LANG) && _MSVC_LANG > 199711L) // Some earlier versions get this wrong

inline internal::fmt_stringview operator""_V(const char* ptr, size_t s)
{
    return internal::fmt_stringview(ptr, s);
}

template <typename... Args> inline
std::string fmtcat(const Args&... args)
{
    ofmtbufstream out;
    out.print(args...);
    return out.str();
}

#else

// Provide fmtcat for C++03 for up to 4 parameters

// The 1-argument version is for logical consistency.
template <typename Arg1> inline
std::string fmtcat(const Arg1& arg1)
{
    return fmts(arg1);
}

template <typename Arg1, typename Arg2> inline
std::string fmtcat(const Arg1& arg1, const Arg2& arg2)
{
    ofmtbufstream out;
    out << arg1 << arg2;
    return out.str();
}

template <typename Arg1, typename Arg2, typename Arg3> inline
std::string fmtcat(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3)
{
    ofmtbufstream out;
    out << arg1 << arg2 << arg3;
    return out.str();
}

template <typename Arg1, typename Arg2, typename Arg3, typename Arg4> inline
std::string fmtcat(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, const Arg4& arg4)
{
    ofmtbufstream out;
    out << arg1 << arg2 << arg3 << arg4;
    return out.str();
}

#endif

template <class Value> inline
std::string fmts(const Value& val)
{
    ofmtbufstream out;
    out << val;
    return out.str();
}

template <class Value> inline
std::string fmts(const Value& val, const fmtc& fmtspec)
{
    ofmtbufstream out;
    out << fmt(val, fmtspec);
    return out.str();
}


}

// This prevents the macro from being used with anything else
// than a string literal. Version of ""_V UDL available for C++03.
#define OFMT_RAWSTR(arg) ::hvu::internal::CreateRawString_FWD("" arg)



#endif
