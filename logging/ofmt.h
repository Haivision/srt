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
// for ostream-based types.

// Follow the ofmt.md file for details.

#ifndef INC_HVU_OFMT_H
#define INC_HVU_OFMT_H

#include <string>
#include <cstring>
#include <vector>
#include <list>
#include <sstream>

// Some earlier versions of MSVC get this wrong
#if (__cplusplus > 199711L) || (defined(_MSVC_LANG) && _MSVC_LANG > 199711L)
#define OFMT_HAVE_CXX11 1
#else
#define OFMT_HAVE_CXX11 0
#endif

// It's safest to bet 2000 + STDNUM + 00, as there is no
// month number 0 (201701 is January 2017).
#if (__cplusplus > 201700L)
#define OFMT_HAVE_CXX17 1
#else
#define OFMT_HAVE_CXX17 0
#endif

#if OFMT_HAVE_CXX11
#include <tuple>
#endif

#if OFMT_HAVE_CXX17
#include <string_view>
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

// !!! IMPORTANT !!!
// THIS CLASS IS FOR THE PURPOSE OF DIRECT WRITING TO THE STREAM ONLY.
// DO NOT use this class for any other purpose and use it also with
// EXTREME CARE.
// The only role of this class is to pass the string with KNOWN SIZE
// written in either a string literal or an array of characters to
// the output stream using its `write` method, that is, with bypassing
// any formatting facilities.

// NOTE: This is NOT the same as std::string_view available in C++17.
// OFMT provides specific overloads for this type, which force interpreting
// it as "always raw string" (bypasses format configuration). For convenience
// it has also added a constructor with std::string_view.
struct ofmt_stringview
{
private:
    const char* d;
    size_t s;

public:
    explicit ofmt_stringview(const char* dd, size_t ss): d(dd), s(ss) {}
#if OFMT_HAVE_CXX17
    ofmt_stringview(const std::string_view& svi): d(svi.data()), s(svi.size()) {}
#endif

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
inline ofmt_stringview CreateRawString_FWD(const char (&ref)[N])
{
    const char* ptr = ref;
    return ofmt_stringview(ptr, check_minus_1<N>::value);
}


// Use this as an overload for operator<< for a stream
// in order to make it support every possible sender produced by fmt().
template <typename Value, typename SenderType>
struct fmt_proxy_template: public SenderType
{
    typedef SenderType sender_t;

    const Value& val; // ERROR: invalidly declared function? -->
               // Iostream manipulators should not be sent to the stream.
               // use fmt() instead.

    fmt_proxy_template(const Value& v, const SenderType& s): SenderType(s), val(v) {}

    template <class OutStream>
    void sendto(OutStream& os) const
    {
        sender_t::format_send(val, os);
    }
};

// Simple sender: fmt(value)
struct snd_default
{
    snd_default() {}

    // General version for all values
    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
        std::stringstream tmp;
        tmp << val;
        if (tmp.tellp() > 0)
        {
            tmp.clear(); // clear the EOF flag that prevents copying
            os << tmp.rdbuf();
        }
    }

    // Specializations for direct types
    template <class OutStream>
    void format_send(const internal::ofmt_stringview& val, OutStream& os) const
    {
        os.write(val.data(), val.size());
    }

    template <class OutStream>
    void format_send(const char* val, OutStream& os) const
    {
        size_t len = strlen(val);
        format_send(internal::ofmt_stringview(val, len), os);
    }

    /* 
    // XXX This cannot be enabled because it makes the lvalue of type
    // const char[N] converted equally to const char (&)[N] and const char*;
    // OTOH it must be resolved to const char* eventually anyway because N is
    // the size of the spare buffer and not necessarily the size of the string.
    // To pass the static string's size directly, use OFMT_SV macro or the
    // _SV operator (note: sv operator in C++17 should also work).

    template <size_t N, class OutStream>
    void format_send(const char (&t)[N], OutStream& os) const
    {
        const char* raw = t;
        format_send(t, os);
    }
    // */

    template <class OutStream>
    void format_send(const std::string& val, OutStream& os) const
    {
        os.write(val.data(), val.size());
    }
};

struct snd_default_stateous
{
    snd_default_stateous() {}

    // General version for all values
    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
        os << val;
    }
};

// ofmt formatter sender: fmt(value, fmtc().parameters...)
template<typename CharType>
struct snd_fmtc
{
private:
    basic_fmtc<CharType> format_spec;
public:
    snd_fmtc(const basic_fmtc<CharType>& f): format_spec(f) {}

    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
        std::stringstream tmp;
        format_spec.apply(tmp);
        tmp << val;
        if (tmp.tellp() > 0)
        {
            tmp.clear(); // clear the EOF flag that prevents copying
            os << tmp.rdbuf();
        }
    }
};

// same as snd_fmtc, but without isolating the stream
// for the call to fmtx(value, fmtc()...)
template<typename CharType>
struct snd_fmtc_stateous
{
    basic_fmtc<CharType> format_spec;
    snd_fmtc_stateous(const basic_fmtc<CharType>& f): format_spec(f) {}

    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
        std::stringstream tmp;
        tmp.copyfmt(os);
        format_spec.apply_ontop(tmp);
        tmp << val;
        if (tmp.tellp() > 0)
        {
            tmp.clear(); // clear the EOF flag that prevents copying
            os << tmp.rdbuf();
        }
    }
};

// Facility for using iostream manipulators
// for the call to fmt(value, ios::hex, ios::setw(2) ... )
// For C++03 only available with up to 2 manipulators.
template <class Manip, class Stream>
inline void snd_ios_manipulate(Stream& os, const Manip& man)
{
    os << man;
}

template <class Manip1, class Manip2, class Stream>
inline void snd_ios_manipulate(Stream& os, const std::pair<Manip1, Manip2>& mans)
{
    os << (mans.first) << (mans.second);
}

// Handle special type manipulators like `endl`
// Unfortunately it must be handled directly in every API

// The idea behind the below "omsequence" type is to provide something like
// std::tuple, however this type needs to distinguish two types of manipulators:
// the ones with their own distinct type, and those being functions (including
// function templates, as it's with "endl"). The use of std::tuple requires too
// many too complicated declaration to work this around.

typedef std::ostream& ostream_manip_fn(std::ostream&);
typedef std::ios_base& ios_base_manip_fn(std::ios_base&);

#if OFMT_HAVE_CXX11

template<class StreamType>
using anystream_manip_fn = StreamType& (StreamType&);

template<typename... Types>
struct omsequence;

template<>
struct omsequence<>
{
    omsequence() {}
};

// Specialization for function-typed manipulators (left, hex, fixed etc.)
template<typename Type1, typename... Types>
struct omsequence<anystream_manip_fn<Type1>, Types...>: omsequence<Types...>
{
    typedef omsequence<Types...> base_t;
    anystream_manip_fn<Type1>* head;
    const omsequence<Types...>& next() const { return *this; }
    omsequence<Types...>& next() { return *this; }
    omsequence(anystream_manip_fn<Type1> arg1, const Types&... args): base_t(args...), head(arg1) {}
};

// Specialization for all others (works for setw, setprecision, setiosflags etc.)
template<typename Type1, typename... Types>
struct omsequence<Type1, Types...>: omsequence<Types...>
{
    typedef omsequence<Types...> base_t;
    Type1 head;
    const omsequence<Types...>& next() const { return *this; }
    omsequence<Types...>& next() { return *this; }
    omsequence(const Type1& arg1, const Types&... args): base_t(args...), head(arg1) {}
};


template <class Stream, class Manip1, class... Manips>
inline void snd_ios_manipulate(Stream& os, const omsequence<Manip1, Manips...>& mans)
{
    os << mans.head;
    snd_ios_manipulate(os, mans.next());
}

template <class Stream>
inline void snd_ios_manipulate(Stream& , const omsequence<>& )
{
}

#else

template<class Type>
struct anystream_manip_remap
{
    typedef Type& type(Type&);
};

template<class Type>
struct omremap
{
    typedef Type type;
};

template<class Stream>
struct omremap<Stream& (Stream&)>
{
    typedef typename anystream_manip_remap<Stream>::type* type;
};


#endif

template <typename Manip>
struct snd_ios
{
private:
    Manip manip; // Can't use reference - could happen to be to temporary.
public:
    snd_ios(const Manip& m): manip(m) {}

    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
        std::stringstream tmp;
        snd_ios_manipulate(tmp, manip);
        tmp << val;
        os << tmp.rdbuf();
    }
};

} // END: namespace internal

template <typename Value, typename SenderType>
inline internal::fmt_proxy_template<Value, SenderType> fmt_make_proxy(const Value& v, const SenderType& s)
{
    return internal::fmt_proxy_template<Value, SenderType>(v, s);
}

inline internal::ofmt_stringview fmt_rawstr(const char* dd, size_t ss)
{
    return internal::ofmt_stringview(dd, ss);
}

inline internal::ofmt_stringview fmt_rawstr(const std::string& s)
{
    return internal::ofmt_stringview(s.data(), s.size());
}

template <class Value> inline
internal::fmt_proxy_template<Value, internal::snd_default> fmt(const Value& val)
{
    return fmt_make_proxy(val, internal::snd_default());
}

template <class Value, class CharType> inline
internal::fmt_proxy_template<Value, internal::snd_fmtc<CharType> > fmt(const Value& val, const basic_fmtc<CharType>& config)
{
    return fmt_make_proxy(val, internal::snd_fmtc<CharType>(config));
}

template <class Value> inline
internal::fmt_proxy_template<Value, internal::snd_default_stateous> fmtx(const Value& val)
{
    return fmt_make_proxy(val, internal::snd_default_stateous());
}

template <class Value, class CharType> inline
internal::fmt_proxy_template<Value, internal::snd_fmtc_stateous<CharType> > fmtx(const Value& val, const basic_fmtc<CharType>& config)
{
    return fmt_make_proxy(val, internal::snd_fmtc_stateous<CharType>(config));
}

#if OFMT_HAVE_CXX11

template <class Value, class Manip> inline
internal::fmt_proxy_template<Value, internal::snd_ios<internal::omsequence<Manip>> > fmtm(const Value& val, const Manip& man)
{
    typedef internal::omsequence<Manip> Tuple;
    return fmt_make_proxy(val, internal::snd_ios<Tuple>(man));
}


template <class Value, class Stream> inline
internal::fmt_proxy_template<Value, internal::snd_ios<internal::anystream_manip_fn<Stream>*> >
    fmtm(const Value& val, internal::anystream_manip_fn<Stream> man)
{
    return fmt_make_proxy(val, internal::snd_ios<internal::anystream_manip_fn<Stream>*>(man));
}

template <class Value, class Stream> inline
internal::fmt_proxy_template<Value, internal::snd_ios<internal::anystream_manip_fn<Stream>*> >
    fmt(const Value& val, internal::anystream_manip_fn<Stream> man)
{
    return fmt_make_proxy(val, internal::snd_ios<internal::anystream_manip_fn<Stream>*>(man));
}

template <class Value, typename Manip1, typename... Manip> inline
internal::fmt_proxy_template<Value, internal::snd_ios<internal::omsequence<Manip1, Manip...>> >
            fmtm(const Value& val, const Manip1& man1, const Manip&... mans)
{
    typedef internal::omsequence<Manip1, Manip...> Tuple;
    return fmt_make_proxy(val, internal::snd_ios<Tuple>(Tuple(man1, mans...)));
}

template <class Value, typename Stream, typename... Manip> inline
internal::fmt_proxy_template<Value, internal::snd_ios<internal::omsequence<internal::anystream_manip_fn<Stream>, Manip...>> >
            fmt(const Value& val, internal::anystream_manip_fn<Stream> man1, const Manip&... mans)
{
    typedef internal::omsequence<internal::anystream_manip_fn<Stream>, Manip...> Tuple;
    return fmt_make_proxy(val, internal::snd_ios<Tuple>(Tuple(man1, mans...)));
}

#else

template<class T1, class T2>
std::pair<typename internal::omremap<T1>::type, typename internal::omremap<T2>::type> make_safe_pair(T1 t1, T2 t2)
{
    typedef typename internal::omremap<T1>::type CleanT1;
    typedef typename internal::omremap<T2>::type CleanT2;
    return std::pair<CleanT1, CleanT2>(t1, t2);
}

template <class Value, class Manip> inline
internal::fmt_proxy_template<Value, internal::snd_ios< typename internal::omremap<Manip>::type > > fmtm(const Value& val, const Manip& man)
{
    typedef typename internal::omremap<Manip>::type Tuple;
    return fmt_make_proxy(val, internal::snd_ios<Tuple>(man));
}

template <class Value, class Stream> inline
internal::fmt_proxy_template<Value, internal::snd_ios<typename internal::anystream_manip_remap<Stream>::type*> >
    fmt(const Value& val, Stream& man1(Stream&) )
{
    typedef Stream& (*fptr)(Stream&);
    return fmt_make_proxy(val, internal::snd_ios<fptr>(man1));
}


template <class Value, class Manip1, class Manip2> inline
internal::fmt_proxy_template<Value, internal::snd_ios<
    std::pair<typename internal::omremap<Manip1>::type, typename internal::omremap<Manip2>::type>
> > fmtm(const Value& val, const Manip1& man1, const Manip2& man2)
{
    typedef std::pair<typename internal::omremap<Manip1>::type, typename internal::omremap<Manip2>::type> Tuple;
    return fmt_make_proxy(val, internal::snd_ios<Tuple>(make_safe_pair(man1, man2)));
}

template <class Value, class Stream1, class Stream2> inline
internal::fmt_proxy_template<Value,
    internal::snd_ios<
        std::pair<Stream1& (*)(Stream1&), Stream2& (*)(Stream2&)> > >
    fmt(const Value& val, Stream1& (*man1)(Stream1&), Stream2& (*man2)(Stream2&))
{

    typedef std::pair<Stream1& (*)(Stream1&), Stream2& (*)(Stream2&)> Tuple;
    return fmt_make_proxy(val, internal::snd_ios<Tuple>(make_safe_pair(man1, man2)));
}


#endif

// Simple transformer
inline const char* fmt_if(bool value, const char* strue, const char* sfalse = "")
{
    return value ? strue : sfalse;
}

namespace internal
{
struct snd_boolean
{
    std::string strue, sfalse;
    snd_boolean(const std::string& st,
            const std::string& sf = std::string()):
        strue(st), sfalse(sf)
    {}

    template <class Value, class OutStream>
    void format_send(const Value& val, OutStream& os) const
    {
        os << (val ? strue : sfalse);
    }
};
}

inline internal::fmt_proxy_template<bool, internal::snd_boolean> fmt_if(
        bool val,
        const std::string& st,
        const std::string& sf = std::string())
{
    using namespace internal;
    return fmt_proxy_template<bool, snd_boolean>(val, snd_boolean(st, sf));
}

namespace internal
{
struct ofmtbase_ref
{
protected:
    std::ostream& refstream;
public:

    ofmtbase_ref(std::ostream& src): refstream(src) {}

    std::ostream& base() {return refstream;}
};

struct ofmtbase_buf
{
protected:
    std::stringstream buffer;
public:

    std::ostream& base() { return buffer; }

    ofmtbase_buf() {}
    ofmtbase_buf(const std::string& s)
    {
        buffer.write(s.data(), s.size());
    }

    size_t copy(char* buf, size_t bufsize)
    {
        return buffer.readsome(buf, bufsize);
    }

    // Provided so that you can prepare a big enough buffer to copy
    size_t size() const
    {
        return buffer.rdbuf()->in_avail();
    }

    std::string str() const
    {
        return buffer.str();
    }

    std::stringbuf* rdbuf() const
    {
        return buffer.rdbuf();
    }

    void clear()
    {
        buffer.clear();
    }

};

}

template<class Imp, class DefaultSender>
class tp_ofmtstream: public Imp
{
public:

#if OFMT_HAVE_CXX11
    // For C++11 try to just use inheriting constructors
    // so that you play safe. This gives this class also
    // more flexibility.
    using Imp::Imp;
#else

    // For C++03 simply replicate both constructors, even
    // if only some of them make sense. In this version also
    // this class cannot be used with a custom value type.
    tp_ofmtstream(std::ostream& src) : Imp(src) {}

    tp_ofmtstream() {}

    // Extra constructor that allows the stream to have some
    // initial contents. Only string types supported
    tp_ofmtstream(const internal::ofmt_stringview& s): Imp(s) {}

    tp_ofmtstream(const std::string& s): Imp(s) {}
#endif

    using Imp::base;

    template<class ValueType, class SenderType>
    tp_ofmtstream& operator<<(const internal::fmt_proxy_template<ValueType, SenderType>& prox)
    {
        prox.sendto(Imp::base());
        return *this;
    }

    template<class Value> inline
    tp_ofmtstream& operator<<(const Value& val)
    {
        DefaultSender().format_send(val, Imp::base());
        return *this;
    }

    template<class AnySender>
    tp_ofmtstream& operator<<(const tp_ofmtstream<internal::ofmtbase_buf, AnySender>& source)
    {
        Imp::base() << source.rdbuf();
        return *this;
    }

    tp_ofmtstream& operator<<(const std::stringstream& source)
    {
        Imp::base() << source.rdbuf();
        return *this;
    }

    tp_ofmtstream& operator<<(internal::ostream_manip_fn& man)
    {
        Imp::base() << (&man);
        return *this;
    }

    tp_ofmtstream& operator<<(const fmtc& fc)
    {
        fc.apply(Imp::base());
        return *this;
    }

    // A utility function to send the argument directly
    // to the buffer
    tp_ofmtstream& fwd()
    {
        return *this;
    }

// Additionally for C++11
#if OFMT_HAVE_CXX11
    template<class Value, class... Args>
    tp_ofmtstream& fwd(const Value& val, const Args&... args)
    {
        Imp::base() << val;
        return fwd(args...);
    }

    template<class... Args>
    tp_ofmtstream& fwd(internal::ostream_manip_fn* man, const Args&... args)
    {
        Imp::base() << man;
        return fwd(args...);
    }

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
    tp_ofmtstream& print(const Args&... args)
    {
        print_chain(args...);
        return *this;
    }

    template<typename... Args>
    tp_ofmtstream& printl(const Args&... args)
    {
        print_chain(args...);
        Imp::base() << std::endl;
        return *this;
    }
#else

    template<class Value>
    tp_ofmtstream& fwd(const Value& val)
    {
        Imp::base() << val;
        return *this;
    }

    template<class Value, class Value2>
    tp_ofmtstream& fwd(const Value& val, const Value2& val2)
    {
        Imp::base() << val << val2;
        return *this;
    }
#endif
};

typedef tp_ofmtstream<internal::ofmtbase_buf, internal::snd_default> ofmt_bufs;
typedef tp_ofmtstream<internal::ofmtbase_buf, internal::snd_default_stateous> ofmt_bufx;

typedef tp_ofmtstream<internal::ofmtbase_ref, internal::snd_default> ofmt_refs;
typedef tp_ofmtstream<internal::ofmtbase_ref, internal::snd_default_stateous> ofmt_refx;

template <class Value> inline
std::string fmts(const Value& val)
{
    ofmt_bufs out;
    out << val;
    return out.str();
}

template <class Value> inline
std::string fmts(const Value& val, const fmtc& fmtspec)
{
    ofmt_bufs out;
    out << fmt(val, fmtspec);
    return out.str();
}


// Additionally for C++11
#if OFMT_HAVE_CXX11

template<typename Stream>
inline Stream& ofwd(Stream& out)
{
    return out;
}

template<typename Stream, typename Arg1, typename... Args>
inline Stream& ofwd(Stream& out, const Arg1& arg1, const Args&... args)
{
    out << arg1;
    return ofwd(out, args...);
}

template<typename Container>
inline Container& ofpush(Container& out)
{
    return out;
}

template<typename Container, typename Arg1, typename... Args>
inline Container& ofpush(Container& out, const Arg1& arg1, const Args&... args)
{
    out.push_back(fmts(arg1));
    return ofpush(out, args...);
}


template<typename Stream, typename Arg1, typename... Args>
inline Stream& ofprint(Stream& out, const Arg1& arg1, const Args&... args)
{
    ofmt_refs sout(out);
    sout.print(arg1, args...);
    return out;
}

template<typename Stream, typename... Args>
inline Stream& ofprintl(Stream& out, const Args&... args)
{
    ofmt_refs sout(out);
    sout.printl(args...);
    return out;
}

template<typename Stream, typename Arg1, typename... Args>
inline Stream& ofprintx(Stream& out, const Arg1& arg1, const Args&... args)
{
    ofmt_bufx sout;
    sout.base().copyfmt(out);
    sout.print(arg1, args...);
    out << sout.rdbuf();
    return out;
}

template<typename Stream, typename Arg1, typename... Args>
inline Stream& ofprintxl(Stream& out, const Arg1& arg1, const Args&... args)
{
    ofmt_bufx sout;
    sout.base().copyfmt(out);
    sout.printl(arg1, args...);
    out << sout.rdbuf();
    return out;
}
inline internal::ofmt_stringview operator""_SV(const char* ptr, size_t s)
{
    return internal::ofmt_stringview(ptr, s);
}

template <typename... Args> inline
std::string ofcat(const Args&... args)
{
    ofmt_bufs out;
    out.print(args...);
    return out.str();
}

#else

// Provide ofcat for C++03 for up to 4 parameters

// The 1-argument version is for logical consistency.
template <typename Arg1> inline
std::string ofcat(const Arg1& arg1)
{
    return fmts(arg1);
}

template <typename Arg1, typename Arg2> inline
std::string ofcat(const Arg1& arg1, const Arg2& arg2)
{
    ofmt_bufs out;
    out << arg1 << arg2;
    return out.str();
}

template <typename Arg1, typename Arg2, typename Arg3> inline
std::string ofcat(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3)
{
    ofmt_bufs out;
    out << arg1 << arg2 << arg3;
    return out.str();
}

template <typename Arg1, typename Arg2, typename Arg3, typename Arg4> inline
std::string ofcat(const Arg1& arg1, const Arg2& arg2, const Arg3& arg3, const Arg4& arg4)
{
    ofmt_bufs out;
    out << arg1 << arg2 << arg3 << arg4;
    return out.str();
}

#endif


}

// This prevents the macro from being used with anything else
// than a string literal. Version of ""_SV UDL available for C++03.
#define OFMT_SV(arg) ::hvu::internal::CreateRawString_FWD("" arg)



#endif
