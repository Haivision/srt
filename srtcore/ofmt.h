// Formatting library for C++ - C++03 compat version of on-demand tagged format API.
//
// Copyright (c) 2024 - present, Mikołaj Małecki
// All rights reserved.
//
// For the license information refer to format.h.

// This is a header-only lightweight C++03-compatible formatting library,
// which provides the on-demand tagged format API and iostream-style wrapper
// for FILE type from stdio. It has nothing to do with the rest of the {fmt}
// library, except that it reuses the namespace.

#ifndef INC_SRT_IFMT_H
#define INC_SRT_IFMT_H

#include <string>
#include <vector>
#include <list>
#include <iostream>

namespace srt
{

template<class CharType>
struct basic_fmtc
{
protected:
    // Find a way to adjust it to wchar_t if need be
    typedef std::basic_ios<CharType> ios;

    typedef typename ios::fmtflags fmtflg_t;
    fmtflg_t fmtflg;
    unsigned short widthval;
    unsigned short precisionval;
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
        uint8_t allbits;
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

    void apply(std::basic_ostream<CharType>& os) const
    {
        os.flags(fmtflg);

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
};

typedef basic_fmtc<char> fmtc;
typedef basic_fmtc<wchar_t> wfmtc;

// fmt(val, fmtc().alt().hex().width(10))

namespace internal
{
template <typename Value, typename CharType>
struct fmt_proxy
{
    Value val; // ERROR: invalidly declared function? -->
               // Iostream manipulators should not be sent to the stream.
               // use fmt() with fmtc() instead.
    basic_fmtc<CharType> format_spec;

    fmt_proxy(const Value& v, const basic_fmtc<CharType>& f): val(v), format_spec(f) {}
};

template <typename Value>
struct fmt_simple_proxy
{
    Value val; // ERROR: invalidly declared function? -->
               // Iostream manipulators should not be sent to the stream.
               // use fmt() with fmtc() instead.
    fmt_simple_proxy(const Value& v): val(v) {}
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
    // This trick is to prevent a possibility to use this class any
    // other way than for creating a temporary object.
    friend fmt_stringview create_stringview(const char* dd, size_t ss);

    const char* d;
    size_t s;

public:
    explicit fmt_stringview(const char* dd, size_t ss): d(dd), s(ss) {}

    const char* data() const { return d; }
    size_t size() const { return s; }
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


// XXX Make basic_ofmtstream etc.
class ofmtstream
{
protected:
    std::stringstream buffer;

public:

    ofmtstream() {}

    void clear()
    {
        buffer.clear();
    }

    // Expose
    ofmtstream& write(const char* buf, size_t size)
    {
        buffer.write(buf, size);
        return *this;
    }

    ofmtstream& operator<<(const char* t)
    {
        size_t len = strlen(t);
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
    template <size_t N>
    ofmtstream& operator<<(const char (&t)[N])
    {
        size_t len = strlen(t);
        buffer.write(t, len);
        return *this;
    }

    ofmtstream& operator<<(const std::string& s)
    {
        buffer.write(s.data(), s.size());
        return *this;
    }

    ofmtstream& operator<<(const internal::fmt_stringview& s)
    {
        buffer.write(s.data(), s.size());
        return *this;
    }

    template<class ValueType>
    ofmtstream& operator<<(const internal::fmt_simple_proxy<ValueType>& prox)
    {
        buffer << prox.val;
        return *this;
    }

    template<class ValueType>
    ofmtstream& operator<<(const internal::fmt_proxy<ValueType, char>& prox)
    {
        std::stringstream tmp;
        prox.format_spec.apply(tmp);
        tmp << prox.val;
        buffer << tmp.rdbuf();
        return *this;
    }

    template<class Value> inline
    ofmtstream& operator<<(const Value& val)
    {
        return *this << fmt(val);
    }


    ofmtstream& operator<<(const ofmtstream& source)
    {
        buffer << source.buffer.rdbuf();
        return *this;
    }

    std::string str() const
    {
        return buffer.str();
    }
};

// Additionally for C++11
#if (defined(__cplusplus) && __cplusplus > 199711L) \
 || (defined(_MSVC_LANG) && _MSVC_LANG > 199711L) // Some earlier versions get this wrong

inline internal::fmt_stringview operator""_V(const char* ptr, size_t s)
{
    return internal::fmt_stringview(ptr, s);
}

template<typename Stream> inline
Stream& oprint(Stream& out)
{
    return out;
}

template<typename Stream, typename Arg1, typename... Args> inline
Stream& oprint(Stream& out, const Arg1& arg1, const Args&... args)
{
    out << arg1;
    return oprint(out, args...);
}

template <typename... Args> inline
std::string ocat(const Args&... args)
{
    ofmtstream out;
    oprint(out, args...);
    return out.str();
}

#endif

template <class Value> inline
std::string fmts(const Value& val)
{
    ofmtstream out;
    out << val;
    return out.str();
}

template <class Value> inline
std::string fmts(const Value& val, const fmtc& fmtspec)
{
    ofmtstream out;
    out << fmt(val, fmtspec);
    return out.str();
}


}

// This prevents the macro from being used with anything else
// than a string literal
#define OFMT_RAWSTR(arg) srt::internal::CreateRawString_FWD("" arg)



#endif
