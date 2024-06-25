// SOURCE NOTE:
//
// This is copied directly from a public-domain library:
// 
// URI: https://github.com/ethouris/fmt
// PATH: include/fmt/sfmt.h
//
// with slight modifications.
//
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

#ifndef INC_FMT_SFMT_H
#define INC_FMT_SFMT_H

#include <cstdio>
#include <cstring>
#include <cmath> // std::abs
#include <cstdlib> // std::div
#include <string>
#include <vector>
#include <list>

namespace srt
{

namespace internal
{

template<size_t PARAM_INITIAL_SIZE = 512>
class form_memory_buffer
{
public:
    static const size_t INITIAL_SIZE = PARAM_INITIAL_SIZE;
    typedef std::list< std::vector<char> > slices_t;

private:
    char first[INITIAL_SIZE];
    slices_t slices;

    size_t initial; // size used in `first`
    size_t reserved; // total size plus slices in reserve
    size_t total; // total size in use

public:
    form_memory_buffer(): initial(0), reserved(0), total(0) {}

    // For constants
    template<size_t N>
    form_memory_buffer(const char (&array)[N]): initial(N-1), reserved(N-1), total(N-1)
    {
        memcpy(first, array, N);
    }

    size_t avail()
    {
        return reserved - total;
    }

    char* get_first() { return first; }
    const char* get_first() const { return first; }
    size_t first_size() const { return initial; }
    const slices_t& get_slices() const { return slices; }
    size_t size() const { return total; }
    bool empty() const { return total == 0; }

    void append(char c)
    {
        char wrap[1] = {c};
        append(wrap, 1);
    }

    // NOTE: append ignores the reservation. It writes
    // where the currently available space is. Use expose()
    // and commit() together or not at all.
    void append(const char* val, size_t size)
    {
        if (size == 0)
            return;

        if (slices.empty())
        {
            if (size < INITIAL_SIZE - initial)
            {
                // Still free space in first.
                memcpy(first + initial, val, size);
                initial += size;
                total = initial;
                if (reserved < total)
                    reserved = total;
                return;
            }
        }

        slices.push_back(std::vector<char>(val, val+size));
        total += size;
        if (reserved < total)
            reserved = total;
    }

    char* expose(size_t size)
    {
        // Repeated exposure simply extends the
        // reservation, if required more, or is ignored,
        // if required less.

        // Note that ort

        size_t already_reserved = reserved - total;
        if (already_reserved >= size)
        {
            // Identify the reserved region
            if (slices.empty())
            {
                reserved = total + size;
                return first + initial;
            }

            std::vector<char>& last = slices.back();
            // Exceptionally resize that part if it doesn't
            // fit.
            if (last.size() != size)
                last.resize(size);
            reserved = total + size;
            return &last[0];
        }

        // Check if you have any size available
        // beyond the current reserved space.
        // If not, allocate.
        if (slices.empty())
        {
            // Not yet resolved to use of the slices,
            // so check free space in first. The value of
            // 'reserved' should be still < INITIAL_SIZE.
            if (INITIAL_SIZE - total >= size)
            {
                char* b = first + total;
                reserved = total + size;
                return b;
            }
        }

        // Otherwise allocate a new slice
        // Check first if the last slice was already reserved
        std::vector<char>* plast = &slices.back();
        if (!already_reserved)
        {
            slices.push_back( std::vector<char>() );
            plast = &slices.back();
        }

        plast->reserve(size);
        plast->resize(size);
        reserved = total + size;
        return &(*plast)[0];
    }

    // Remove the last 'size' chars from reservation
    bool unreserve(size_t size)
    {
        if (size > reserved - total)
            return false;

        if (!slices.empty())
        {
            // Check the last slice if it contains that size
            std::vector<char>& last = slices.back();
            if (last.size() < size)
                return false;

            size_t remain = last.size() - size;
            if (!remain)
            {
                slices.pop_back();
                reserved -= size;
                return true;
            }

            last.erase(last.begin() + remain, last.end());
        }
        // Otherwise the space is in the initial buffer.

        reserved -= size;
        return true;
    }

    void commit()
    {
        total = reserved;
        if (slices.empty())
        {
            // This means we don't use extra slices, so
            // this size must be also repeated in initial
            initial = reserved;
        }
    }

    void clear()
    {
        slices.clear();
        total = 0;
        reserved = 0;
        initial = 0;
    }
};

template<size_t N, size_t I>
struct CheckChar
{
    static bool is(char c, const char (&series)[N])
    {
        return c == series[I] || CheckChar<N, I+1>::is(c, series);
    }
};

template<size_t N>
struct CheckChar<N, N>
{
    // Terminal version - if none interrupted with true,
    // eventually return false.
    static bool is(char , const char (&)[N]) { return false; }
};

template<size_t N> inline
bool isanyof(char c, const char (&series)[N])
{
    return CheckChar<N, 0>::is(c, series);
}

template<size_t N> inline
bool isnum_or(char c, const char (&series)[N])
{
    if (c >= '0' && c <= '9')
        return true;
    return isanyof(c, series);
}

template<class AnyType, bool Condition>
struct Ensure
{
};

template<class AnyType>
struct Ensure<AnyType, false>
{
    static void CheckIfCharsetNonEmpty()
    {
        typename AnyType::wrong_condition v = AnyType::wrong_condition;
        (void)v;
    }
};

template<size_t N1, size_t N2, size_t N3> inline
form_memory_buffer<> fix_format(const char* fmt,
        const char (&allowed)[N1],
        const char (&typed)[N2],
        const char (&deftype)[N3],
        const char* warn)
{
    // All these arrays must contain at least 2 elements,
    // that is one character and terminating zero.
    //Ensure<int, N1 >= 2> c1;
    Ensure<int, (N2 >= 2)> c2; (void)c2;
    Ensure<int, (N3 >= 2)> c3; (void)c3;

    form_memory_buffer<> buf;
    buf.append('%');

    bool warn_error = false;
    if (fmt)
    {
        size_t len = strlen(fmt);
        for (size_t i = 0; i < len; ++i)
        {
            char c = fmt[i];
            if (internal::isnum_or(c, allowed))
            {
                buf.append(c);
                continue;
            }

            if (internal::isanyof(c, typed))
            {
                // If you have found any numbase character,
                // add first all characters from the default,
                // EXCEPT the last one.
                buf.append(deftype, N3-2);

                // that's it, and we're done here.
                buf.append(c);
                return buf;
            }

            // If any other character is found, add the <!> warning
            warn_error = true;
            break;
        }
    }

    buf.append(deftype, N3);

    if (warn_error && warn)
    {
        buf.append(warn, strlen(warn));
    }
    return buf;
}


#define SFMT_FORMAT_FIXER(TYPE, ALLOWED, TYPED, DEFTYPE, WARN) \
inline form_memory_buffer<> apply_format_fix(TYPE, const char* fmt) \
{ \
    return fix_format(fmt, ALLOWED, TYPED, DEFTYPE, WARN); \
}

#define SFMT_FORMAT_FIXER_TPL(TPAR, TYPE, ALLOWED, TYPED, DEFTYPE, WARN) \
template<TPAR>\
inline form_memory_buffer<> apply_format_fix(TYPE, const char* fmt)\
{\
    return fix_format(fmt, ALLOWED, TYPED, DEFTYPE, WARN); \
}



// So, format in the format spec is:
//
// (missing): add the default format
// Using: diouxX - specify the numeric base
// Using efg - specify the float style

// Modifiers like "h", "l", or "L" shall not
// be used. They will be inserted if needed.

SFMT_FORMAT_FIXER(int, "+- '#", "dioxX", "i", "<!!!>");
SFMT_FORMAT_FIXER(short int, "+- '#", "dioxX", "hi", "<!!!>");
SFMT_FORMAT_FIXER(long int, "+- '#", "dioxX", "li", "<!!!>");
SFMT_FORMAT_FIXER(long long int, "+- '#", "dioxX", "lli", "<!!!>");

SFMT_FORMAT_FIXER(unsigned int, "+- '#", "uoxX", "u", "<!!!>");
SFMT_FORMAT_FIXER(unsigned short int, "+- '#", "uoxX", "hu", "<!!!>");
SFMT_FORMAT_FIXER(unsigned long int, "+- '#", "uoxX", "lu", "<!!!>");
SFMT_FORMAT_FIXER(unsigned long long int, "+- '#", "uoxX", "llu", "<!!!>");

SFMT_FORMAT_FIXER(double, "+- '#.", "EeFfgGaA", "g", "<!!!>");
SFMT_FORMAT_FIXER(float, "+- '#.", "EeFfgGaA", "g", "<!!!>");
SFMT_FORMAT_FIXER(long double, "+- '#.", "EeFfgGaA", "Lg", "<!!!>");

SFMT_FORMAT_FIXER(char, "-", "c", "c", "<!!!>");

SFMT_FORMAT_FIXER(std::string, "-.", "s", "s", "<!!!>");
SFMT_FORMAT_FIXER(const char*, "-.", "s", "s", "<!!!>");
SFMT_FORMAT_FIXER(char*, "-.", "s", "s", "<!!!>");
SFMT_FORMAT_FIXER_TPL(size_t N, const char (&)[N], "-.", "s", "s", "<!!!>");
SFMT_FORMAT_FIXER_TPL(size_t N, char (&)[N], "-.", "s", "s", "<!!!>");
SFMT_FORMAT_FIXER_TPL(class Type, Type*, "-", "p", "p", "<!!!>");

#undef SFMT_FORMAT_FIXER_TPL
#undef SFMT_FORMAT_FIXER

template<size_t N>
struct const_copy
{
    static void copy(char* target, const char *source)
    {
        *target = *source;
        return const_copy<N-1>::copy(target + 1, source + 1);
    }
};

template<>
struct const_copy<1> // 1 because the last item is the terminal '\0'.
{
    static void copy(char*, const char *) { } // do nothing, terminal value
};

// These maps must cover all allowed values, for safety.
static const char present_int_map[8] = { 'i', 'o', 'x', 'X', 'd', 'd', 'd', 'd'};
static const char present_float_map[8] = { 'g', 'G', 'a', 'A', 'e', 'E', 'f', 'g' };

}

struct sfmc
{
    enum postype { pos_no, pos_plus, pos_space, pos_invalid };
    enum flavor {
       flavor_dec = 0,
       flavor_oct = 1,
       flavor_hex = 2,
       flavor_uhex = 3,

       flavor_general = 0,
       flavor_ugeneral = 1,
       flavor_fhex = 2,
       flavor_ufhex = 3,
       flavor_scientific = 4,
       flavor_uscientific = 5,
       flavor_fixed = 6
    };

protected:
    short widthval;
    short precisionval;
    bool widthbit:1;
    bool precisionbit:1;
    bool altbit:1;
    bool leftbit:1;
    bool leadzerobit:1;
    postype postype:2;
    flavor presentation:3;
    bool localized:1;

public:
    sfmc():
        widthval(0),
        precisionval(6),
        widthbit(false),
        precisionbit(false),
        altbit(false),
        leftbit(false),
        leadzerobit(false),
        postype(pos_no),
        presentation(flavor_dec),
        localized(false)
    {
    }

#define SFMTC_TAG(name, body) sfmc& name () { body; return *this; }
#define SFMTC_TAG_VAL(name, body) sfmc& name (int val) { body; return *this; }

    SFMTC_TAG(alt, altbit = true);
    SFMTC_TAG(left, leftbit = true);
    SFMTC_TAG(right, (void)0);
    SFMTC_TAG_VAL(width, widthbit = true; widthval = std::abs(val));
    SFMTC_TAG_VAL(precision, precisionbit = true; precisionval = std::abs(val));
    SFMTC_TAG(dec, (void)0);
    SFMTC_TAG(hex, presentation = flavor_hex);
    SFMTC_TAG(oct, presentation = flavor_oct);
    SFMTC_TAG(uhex, presentation = flavor_uhex);
    SFMTC_TAG(general, (void)0);
    SFMTC_TAG(ugeneral, presentation = flavor_ugeneral);
    SFMTC_TAG(fhex, presentation = flavor_fhex);
    SFMTC_TAG(ufhex, presentation = flavor_ufhex);
    SFMTC_TAG(exp, presentation = flavor_scientific);
    SFMTC_TAG(uexp, presentation = flavor_uscientific);
    SFMTC_TAG(scientific, presentation = flavor_scientific);
    SFMTC_TAG(uscientific, presentation = flavor_uscientific);
    SFMTC_TAG(fixed, presentation = flavor_fixed);
    SFMTC_TAG(nopos, (void)0);
    SFMTC_TAG(posspace, postype = pos_space);
    SFMTC_TAG(posplus, postype = pos_plus);
    SFMTC_TAG(fillzero, leadzerobit = true);

#undef SFMTC_TAG
#undef SFMTC_TAG_VAL

    // Utility function to store the number for width/precision
    // For C++11 it could be constexpr, but this is C++03-compat code.
    // It's bound to this structure because it's unsafe.
    static std::size_t store_number(char* position, int number)
    {
        std::size_t shiftpos = 0;
        std::div_t dm = std::div(number, 10);
        if (dm.quot)
            shiftpos = store_number(position, dm.quot);
        position[shiftpos] = '0' + dm.rem;
        return shiftpos + 1;
    }

    template<size_t N>
    internal::form_memory_buffer<> create_format_int(const char (&lnspec)[N]) const
    {
        using namespace internal;

        Ensure<int, (N >= 2)> c3; (void)c3;

        form_memory_buffer<> form;

        // Use reservation as this will be faster and also
        // we don't know how many ciphers will be taken by width/precision
        // We need to reserve 12 + 12 + 4 + 4 = 32.
        char* buf = form.expose(32);
        size_t used = 0;
        buf[used++] = '%';

        if (altbit)
            buf[used++] = '#';

        if (leftbit)
            buf[used++] = '-';

        if (localized)
            buf[used++] = '\'';

        if (leadzerobit)
            buf[used++] = '0';

        if (widthbit)
        {
            size_t w = store_number(buf + used, widthval);
            used += w;
        }

        // That's it, now we copy the type length modifier
        const_copy<N-1>::copy(buf + used, lnspec);
        used += (N - 2);

        // And finally the flavor modifier
        if (int(presentation) == 0)
            buf[used++] = lnspec[N-2];
        else
            buf[used++] = present_int_map[presentation];
        buf[used] = '\0';

        size_t unused = 32 - used;
        if (unused > 0)
            form.unreserve(unused);
        form.commit();

        return form;
    }

    template<size_t N>
    internal::form_memory_buffer<> create_format_float(const char (&lnspec)[N]) const
    {
        using namespace internal;
        Ensure<int, N >= 2> c3; (void)c3;

        form_memory_buffer<> form;

        // Use reservation as this will be faster and also
        // we don't know how many ciphers will be taken by width/precision
        // We need to reserve 12 + 12 + 4 + 4 = 32.
        char* buf = form.expose(32);
        size_t used = 0;
        buf[used++] = '%';

        if (altbit)
            buf[used++] = '#';

        if (leftbit)
            buf[used++] = '-';

        if (localized)
            buf[used++] = '\'';

        if (leadzerobit)
            buf[used++] = '0';

        if (widthbit)
        {
            size_t w = store_number(buf + used, widthval);
            used += w;
        }

        if (precisionbit)
        {
            buf[used++] = '.';
            size_t w = store_number(buf + used, precisionval);
            used += w;
        }

        // That's it, now we copy the type length modifier
        const_copy<N-1>::copy(buf + used, lnspec);
        used += (N - 2);

        // And finally the flavor modifier
        if (int(presentation) == 0)
            buf[used++] = lnspec[N-2];
        else
            buf[used++] = present_float_map[presentation];
        buf[used] = '\0';

        size_t unused = 32 - used;
        if (unused > 0)
            form.unreserve(unused);
        form.commit();

        return form;
    }

    internal::form_memory_buffer<> create_format(int) const { return create_format_int("i"); }
    internal::form_memory_buffer<> create_format(short int) const { return create_format_int("hi"); };
    internal::form_memory_buffer<> create_format(long int) const { return create_format_int("li"); };
    internal::form_memory_buffer<> create_format(long long int) const { return create_format_int("lli"); };

    internal::form_memory_buffer<> create_format(unsigned int) const { return create_format_int("u"); };
    internal::form_memory_buffer<> create_format(unsigned short int) const { return create_format_int("hu"); };
    internal::form_memory_buffer<> create_format(unsigned long int) const { return create_format_int("lu"); };
    internal::form_memory_buffer<> create_format(unsigned long long int) const { return create_format_int("llu"); };

    internal::form_memory_buffer<> create_format(double) const { return create_format_float("g"); };
    internal::form_memory_buffer<> create_format(float) const { return create_format_float("g"); };
    internal::form_memory_buffer<> create_format(long double) const { return create_format_float("Lg"); };

    internal::form_memory_buffer<> create_format(char) const
    {
        internal::form_memory_buffer<> form;
        form.append("%c", 3);
        return form;
    }

    internal::form_memory_buffer<> create_format_string() const
    {
        using namespace internal;

        form_memory_buffer<> form;

        // Use reservation as this will be faster and also
        // we don't know how many ciphers will be taken by width/precision
        // We need to reserve 12 + 12 + 4 + 4 = 32.
        char* buf = form.expose(32);
        size_t used = 0;
        buf[used++] = '%';

        if (leftbit)
            buf[used++] = '-';

        if (widthbit)
        {
            size_t w = store_number(buf + used, widthval);
            used += w;
        }

        if (precisionbit)
        {
            buf[used++] = '.';
            size_t w = store_number(buf + used, precisionval);
            used += w;
        }

        // No modifier supported
        // XXX NOTE: wchar_t therefore not supported!
        buf[used++] = 's';
        buf[used] = '\0';

        size_t unused = 32 - used;
        if (unused > 0)
            form.unreserve(unused);
        form.commit();

        return form;
    }

    internal::form_memory_buffer<> create_format(char*) const { return create_format_string(); };
    internal::form_memory_buffer<> create_format(const char*) const { return create_format_string(); };
    internal::form_memory_buffer<> create_format(std::string) const { return create_format_string(); };
};

// fmt(val, sfmc().alt().hex().width(10))

namespace internal
{

template<class Value, class Stream> inline
void write_default(Stream& str, const Value& val);

}

class ostdiostream
{
protected:
    mutable FILE* in;

public:

    FILE* raw() const { return in; }

    ostdiostream(FILE* f): in(f) {}

    ostdiostream& operator<<(const char* t)
    {
        std::fputs(t, in);
        return *this;
    }

    ostdiostream& operator<<(const std::string& s)
    {
        std::fputs(s.c_str(), in);
        return *this;
    }

    template<size_t ANYSIZE>
    ostdiostream& operator<<(const internal::form_memory_buffer<ANYSIZE>& b)
    {
        using namespace internal;
        // Copy all pieces one by one
        if (b.size() == 0)
            return *this;

        std::fwrite(b.get_first(), 1, b.first_size(), in);
        for (form_memory_buffer<>::slices_t::const_iterator i = b.get_slices().begin();
                i != b.get_slices().end(); ++i)
        {
            const char* data = &(*i)[0];
            std::fwrite(data, 1, i->size(), in);
        }
        return *this;
    }

    template<class Value>
    ostdiostream& operator<<(const Value& v)
    {
        internal::write_default(*this, v);
        return *this;
    }
};


class ofilestream: public ostdiostream
{
public:

    ofilestream(): ostdiostream(0) {}

    ofilestream(const std::string& name, const std::string& mode = "")
        : ostdiostream(0) // Set NULL initially, but then override
    {
        open(name, mode);
    }

    bool good() const { return in; }

    void open(const std::string& name, const std::string& mode = "")
    {
        if (mode == "")
            in = std::fopen(name.c_str(), "w");
        else
            in = std::fopen(name.c_str(), mode.c_str());
    }

    // For the use of other functions than fopen() that can create the stream,
    // but they still create FILE* that should be closed using fclose().
    void attach(FILE* other)
    {
        in = other;
    }

    FILE* detach()
    {
        FILE* sav = in;
        in = 0;
        return sav;
    }

    int close()
    {
        int retval = 0;
        if (in)
        {
            retval = std::fclose(in);
            in = 0;
        }
        return retval;
    }

    ~ofilestream()
    {
        if (in)
            std::fclose(in);
    }
};

class obufstream
{
protected:
    internal::form_memory_buffer<> buffer;

public:

    obufstream() {}

    void clear()
    {
        buffer.clear();
    }

    obufstream& operator<<(const char* t)
    {
        size_t len = strlen(t);
        buffer.append(t, len);
        return *this;
    }

    obufstream& operator<<(const std::string& s)
    {
        buffer.append(s.data(), s.size());
        return *this;
    }

    obufstream& write(const char* t, size_t size)
    {
        buffer.append(t, size);
        return *this;
    }

    // For unusual manipulation, usually to add NUL termination.
    // NOTE: you must make sure that you won't use the extended
    // buffers if the intention was to get a string.
    void append(char c)
    {
        buffer.append(c);
    }

    const char* bufptr() const
    {
        return buffer.get_first();
    }

    template<size_t ANYSIZE>
    obufstream& operator<<(const internal::form_memory_buffer<ANYSIZE>& b)
    {
        using namespace internal;
        // Copy all pieces one by one
        if (b.size() == 0)
            return *this;

        buffer.append(b.get_first(), b.first_size());
        for (form_memory_buffer<>::slices_t::const_iterator i = b.get_slices().begin();
                i != b.get_slices().end(); ++i)
        {
            // Would be tempting to move the blocks, but C++03 doesn't feature moving.
            const char* data = &(*i)[0];
            buffer.append(data, i->size());
        }
        return *this;
    }

    obufstream& operator<<(const obufstream& source)
    {
        return *this << source.buffer;
    }

    template<class Value>
    obufstream& operator<<(const Value& v)
    {
        internal::write_default(*this, v);
        return *this;
    }

    std::string str() const
    {
        using namespace internal;
        std::string out;
        if (buffer.empty())
            return out;

        out.reserve(buffer.size() + 1);
        out.append(buffer.get_first(), buffer.first_size());
        for (form_memory_buffer<>::slices_t::const_iterator i = buffer.get_slices().begin();
                i != buffer.get_slices().end(); ++i)
        {
            // Would be tempting to move the blocks, but C++03 doesn't feature moving.
            const char* data = &(*i)[0];
            out.append(data, i->size());
        }
        return out;
    }

    size_t size() const { return buffer.size(); }

    template <class OutputContainer>
    void copy_to(OutputContainer& out) const
    {
        using namespace internal;

        std::copy(buffer.get_first(), buffer.get_first() + buffer.first_size(),
                std::back_inserter(out));

        for (form_memory_buffer<>::slices_t::const_iterator i = buffer.get_slices().begin();
                i != buffer.get_slices().end(); ++i)
        {
            // Would be tempting to move the blocks, but C++03 doesn't feature moving.
            const char* data = &(*i)[0];
            std::copy(data, data + i->size(), std::back_inserter(out));
        }
    }

    template <class OutputContainer>
    size_t copy_to(OutputContainer& out, size_t maxsize) const
    {
        using namespace internal;
        size_t avail = maxsize;
        if (avail < buffer.first_size())
        {
            std::copy(buffer.get_first(), buffer.get_first() + avail,
                    std::back_inserter(out));
            return maxsize;
        }

        std::copy(buffer.get_first(), buffer.get_first() + buffer.first_size(),
                std::back_inserter(out));

        avail -= buffer.first_size();

        for (form_memory_buffer<>::slices_t::const_iterator i = buffer.get_slices().begin();
                i != buffer.get_slices().end(); ++i)
        {
            // Would be tempting to move the blocks, but C++03 doesn't feature moving.
            const char* data = &(*i)[0];

            if (avail < i->size())
            {
                std::copy(data, data + avail, std::back_inserter(out));
                return maxsize;
            }
            std::copy(data, data + i->size(), std::back_inserter(out));
            avail -= i->size();
        }

        return maxsize - avail;
    }
};

namespace internal
{
#if defined(_MSC_VER) && _MSC_VER < 1900
#define FMT_SYM_SNPRINTF _snprintf
#define FMT_SIZE_SNPRINTF(bufsize) (bufsize-1)
#else
#define FMT_SYM_SNPRINTF std::snprintf
#define FMT_SIZE_SNPRINTF(bufsize) bufsize
#endif

template<class ValueType>
static inline size_t SNPrintfOne(char* buf, size_t bufsize, const char* fmt, const ValueType& val)
{ return FMT_SYM_SNPRINTF (buf, FMT_SIZE_SNPRINTF(bufsize), fmt, val); }

static inline size_t SNPrintfOne(char* buf, size_t bufsize, const char* fmt, const std::string& val)
{ return FMT_SYM_SNPRINTF(buf, FMT_SIZE_SNPRINTF(bufsize), fmt, val.c_str()); }

#undef FMT_SYM_SNPRINTF
#undef FMT_SIZE_SNPRINTF

template <class Value> inline
form_memory_buffer<> sfmt_imp(const Value& val, const form_memory_buffer<>& fstr, size_t bufsize)
{
    form_memory_buffer<> out;
    // Reserve the maximum initial first, then shrink.
    char* buf = out.expose(bufsize);

    size_t valsize = SNPrintfOne(buf, bufsize, fstr.get_first(), val);

    // Deemed impossible to happen, but still
    if (valsize == bufsize)
    {
        bufsize *= 2;
        // Just try again with one extra size, if this won't
        // suffice, just add <...> at the end.
        buf = out.expose(bufsize);
        valsize = SNPrintfOne(buf, bufsize, fstr.get_first(), val);
        if (valsize == bufsize)
        {
            char* end = buf + bufsize - 6;
            strcpy(end, "<...>");
        }
    }

    size_t unused = bufsize - valsize;
    out.unreserve(unused);
    out.commit();
    return out;
}
}

template <class Value> inline
internal::form_memory_buffer<> sfmt(const Value& val, const char* fmtspec = 0)
{
    using namespace internal;

    form_memory_buffer<> fstr = apply_format_fix(val, fmtspec);
    size_t bufsize = form_memory_buffer<>::INITIAL_SIZE;

    // We want to use this buffer as a NUL-terminated string.
    // So we need to add NUL character oursevles, form_memory_buffer<>
    // doesn't do it an doesn't use the NUL-termination.
    fstr.append('\0');

    return sfmt_imp(val, fstr, bufsize);
}

template <class Value> inline
internal::form_memory_buffer<> sfmt(const Value& val, const sfmc& config)
{
    using namespace internal;

    form_memory_buffer<> fstr = config.create_format(val);
    size_t bufsize = form_memory_buffer<>::INITIAL_SIZE;
    return sfmt_imp(val, fstr, bufsize);
}



namespace internal
{
template<class Value, class Stream> inline
void write_default(Stream& s, const Value& v)
{
    s << sfmt(v, "");
}


inline void sfmts_imp(const internal::form_memory_buffer<>& b, std::string& out)
{
    using namespace internal;

    if (b.size() == 0)
        return;

    out.reserve(b.size());
    out.append(b.get_first(), b.first_size());
    for (form_memory_buffer<>::slices_t::const_iterator i = b.get_slices().begin();
            i != b.get_slices().end(); ++i)
    {
        const char* data = &(*i)[0];
        out.append(data, i->size());
    }
}
}

template <class Value>
inline std::string sfmts(const Value& val, const char* fmtspec = 0)
{
    std::string out;
    internal::sfmts_imp(sfmt(val, fmtspec), (out));
    return out;
}

template <class Value>
inline std::string sfmts(const Value& val, const sfmc& fmtspec)
{
    std::string out;
    internal::sfmts_imp(sfmt(val, fmtspec), (out));
    return out;
}


// Semi-manipulator to add the end-of-line.
const internal::form_memory_buffer<2> seol ("\n");

// Another manipulator. You can add yourself others the same way.
const struct os_flush_manip {} sflush;

inline ostdiostream& operator<<(ostdiostream& sout, const os_flush_manip&)
{
        std::fflush(sout.raw());
        return sout;
};


}

#endif
