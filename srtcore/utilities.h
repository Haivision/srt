/*****************************************************************************
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 * 
 *****************************************************************************/

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__SRT_UTILITIES_H
#define INC__SRT_UTILITIES_H


#ifndef __UDT_H__
#error Must include udt.h prior to this header!
#endif

#ifdef __GNUG__
#define ATR_UNUSED __attribute__((unused))
#define ATR_DEPRECATED __attribute__((deprecated))
#else
#define ATR_UNUSED
#define ATR_DEPRECATED
#endif

#if defined(__cplusplus) && __cplusplus > 199711L
#define HAVE_CXX11 1
#define ATR_NOEXCEPT noexcept
#define ATR_CONSTEXPR constexpr
// Microsoft Visual Studio supports C++11, but not fully,
// and still did not change the value of __cplusplus. Treat
// this special way.
// _MSC_VER == 1800  means Microsoft Visual Studio 2013.
#elif defined(_MSC_VER) && _MSC_VER >= 1800
#define HAVE_CXX11 1
#define ATR_NOEXCEPT
#define ATR_CONSTEXPR
#else
#define HAVE_CXX11 0
#define ATR_NOEXCEPT // throw() - bad idea
#define ATR_CONSTEXPR

#if defined(REQUIRE_CXX11) && REQUIRE_CXX11 == 1
#error "The currently compiled application required C++11, but your compiler doesn't support it."
#endif

#endif

// Windows warning disabler
#define _CRT_SECURE_NO_WARNINGS

#include <string>
#include <algorithm>
#include <bitset>
#include <functional>
#include <memory>
#include <sstream>
#include <cstdlib>
#include <cerrno>
#include <cstring>

// -------------- UTILITIES ------------------------

// Bit numbering utility.
// Usage: Bits<leftmost, rightmost>
//
// You can use it as a typedef (say, "MASKTYPE") and then use the following members:
// - MASKTYPE::mask - to get the int32_t value with bimask (used bits set to 1, others to 0)
// - MASKTYPE::offset - to get the lowermost bit number, or number of bits to shift
// - MASKTYPE::wrap(int value) - to create a bitset where given value is encoded in given bits
// - MASKTYPE::unwrap(int bitset) - to extract an integer value from the bitset basing on mask definition
// (rightmost defaults to leftmost)
// REMEMBER: leftmost > rightmost because bit 0 is the LEAST significant one!

template <size_t L, size_t R, bool parent_correct = true>
struct BitsetMask
{
    static const bool correct = L >= R;
    static const uint32_t value = (1u << L) | BitsetMask<L-1, R, correct>::value;
};

// This is kind-of functional programming. This describes a special case that is
// a "terminal case" in case when decreased L-1 (see above) reached == R.
template<size_t R>
struct BitsetMask<R, R, true>
{
    static const bool correct = true;
    static const uint32_t value = 1 << R;
};

// This is a trap for a case that BitsetMask::correct in the master template definition
// evaluates to false. This trap causes compile error and prevents from continuing
// recursive unwinding in wrong direction (and challenging the compiler's resistiveness
// for infinite loops).
template <size_t L, size_t R>
struct BitsetMask<L, R, false>
{
};

template <size_t L, size_t R = L>
struct Bits
{
    // DID YOU GET kind-of error: ‘mask’ is not a member of ‘Bits<3u, 5u, false>’ ?
    // See the the above declaration of 'correct' !
    static const uint32_t mask = BitsetMask<L, R>::value;
    static const uint32_t offset = R;
    static const size_t size = L - R + 1;

    // Example: if our bitset mask is 00111100, this checks if given value fits in
    // 00001111 mask (that is, does not exceed <0, 15>.
    static bool fit(uint32_t value) { return (BitsetMask<L-R, 0>::value & value) == value; }

    /// 'wrap' gets some given value that should be placed in appropriate bit range and
    /// returns a whole 32-bit word that has the value already at specified place.
    /// To create a 32-bit container that contains already all values destined for different
    /// bit ranges, simply use wrap() for each of them and bind them with | operator.
    static uint32_t wrap(uint32_t baseval) { return (baseval << offset) & mask; }

    /// Extracts appropriate bit range and returns them as normal integer value.
    static uint32_t unwrap(uint32_t bitset) { return (bitset & mask) >> offset; }
};


//inline int32_t Bit(size_t b) { return 1 << b; }
// XXX This would work only with 'constexpr', but this is
// available only in C++11. In C++03 this can be only done
// using a macro.
//
// Actually this can be expressed in C++11 using a better technique,
// such as user-defined literals:
// 2_bit  --> 1 >> 2

#ifdef BIT
#undef BIT
#endif
#define BIT(x) (1 << (x))


// ------------------------------------------------------------
// This is something that reminds a structure consisting of fields
// of the same type, implemented as an array. It's parametrized
// by the type of fields and the type, which's values should be
// used for indexing (preferably an enum type). Whatever type is
// used for indexing, it is converted to size_t for indexing the
// actual array.
// 
// The user should use it as an array: ds[DS_NAME], stating
// that DS_NAME is of enum type passed as 3rd parameter.
// However trying to do ds[0] would cause a compile error.
template <typename FieldType, size_t Size, typename IndexerType>
struct DynamicStruct
{
    FieldType inarray[Size];

    void clear()
    {
        // As a standard library, it can be believed that this call
        // can be optimized when FieldType is some integer.
        std::fill(inarray, inarray + Size, FieldType());
    }

    FieldType operator[](IndexerType ix) const { return inarray[size_t(ix)]; }
    FieldType& operator[](IndexerType ix) { return inarray[size_t(ix)]; }

    template<class AnyOther>
    FieldType operator[](AnyOther ix) const
    {
        // If you can see a compile error here ('int' is not a class or struct, or
        // that there's no definition of 'type' in given type), it means that you
        // have used invalid data type passed to [] operator. See the definition
        // of this type as DynamicStruct and see which type is required for indexing.
        typename AnyOther::type wrong_usage_of_operator_index = AnyOther::type;
        return inarray[size_t(ix)];
    }

    template<class AnyOther>
    FieldType& operator[](AnyOther ix)
    {
        // If you can see a compile error here ('int' is not a class or struct, or
        // that there's no definition of 'type' in given type), it means that you
        // have used invalid data type passed to [] operator. See the definition
        // of this type as DynamicStruct and see which type is required for indexing.
        typename AnyOther::type wrong_usage_of_operator_index = AnyOther::type;
        return inarray[size_t(ix)];
    }

    operator FieldType* () { return inarray; }
    operator const FieldType* () const { return inarray; }

    char* raw() { return (char*)inarray; }
};


// ------------------------------------------------------------



inline bool IsSet(int32_t bitset, int32_t flagset)
{
    return (bitset & flagset) == flagset;
}

inline void HtoNLA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = htonl(src[i]);
}

inline void NtoHLA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = ntohl(src[i]);
}

#if HAVE_CXX11

#include <functional>

// Replacement for a bare reference for passing a variable to be filled by a function call.
// To pass a variable, just use the std::ref(variable). The call will be accepted if you
// pass the result of ref(), but will be rejected if you just pass a variable.
template <class T>
struct ref_t: public std::reference_wrapper<T>
{
    typedef std::reference_wrapper<T> base;
    ref_t() {}
    ref_t(const ref_t& i): base(i) {}
    ref_t(const base& i): base(i) {}

    ref_t& operator=(const ref_t&) = default;

    void operator=(const T& i)
    {
        this->get() = i;
    }
};

template <class In>
inline auto Ref(In i) -> decltype(std::ref(i)) { return std::ref(i); }

template <class In>
inline auto Move(In i) -> decltype(std::move(i)) { return std::move(i); }

// Gluing string of any type, wrapper for operator <<

template <class Stream>
inline Stream& Print(Stream& in) { return in;}

template <class Stream, class Arg1, class... Args>
inline Stream& Print(Stream& sout, Arg1&& arg1, Args&&... args)
{
    sout << arg1;
    return Print(sout, args...);
}

template <class... Args>
inline std::string Sprint(Args&&... args)
{
    std::ostringstream sout;
    Print(sout, args...);
    return sout.str();
}

// We need to use UniquePtr, in the form of C++03 it will be a #define.
// Naturally will be used std::move() so that it can later painlessly
// switch to C++11.
template <class T>
using UniquePtr = std::unique_ptr<T>;

#else

// Homecooked version of ref_t. It's a copy of std::reference_wrapper
// voided of unwanted properties and renamed to ref_t.

template<typename Type>
class ref_t
{
    Type* m_data;

public:
    typedef Type type;

    explicit ref_t(Type& __indata)
        : m_data(&__indata)
        { }

    ref_t(const ref_t<Type>& inref)
        : m_data(inref.m_data)
    { }

    void operator=(const ref_t<Type>& inref)
    {
        m_data = inref.m_data;
    }

    void operator=(const Type& src)
    {
        *m_data = src;
    }

    operator Type&() const
    { return this->get(); }

    Type& get() const 
    { return *m_data; }
};

template <class Type>
ref_t<Type> Ref(Type& arg)
{
    return ref_t<Type>(arg);
}

// The unique_ptr requires C++11, and the rvalue-reference feature,
// so here we're simulate the behavior using the old std::auto_ptr.

// This is only to make a "move" call transparent and look ok towards
// the C++11 code.
template <class T>
std::auto_ptr_ref<T> Move(const std::auto_ptr_ref<T>& in) { return in; }

// We need to provide also some fixes for this type that were not present in auto_ptr,
// but they are present in unique_ptr.

// C++03 doesn't have a templated typedef, but still we need some things
// that can only function as a class.
template <class T>
class UniquePtr: public std::auto_ptr<T>
{
    typedef std::auto_ptr<T> Base;

public:

    // This is a template - so method names must be declared explicitly
    typedef typename Base::element_type element_type;
    using Base::get;
    using Base::reset;

    // All constructor declarations must be repeated.
    // "Constructor delegation" is also only C++11 feature.
    explicit UniquePtr(element_type* __p = 0) throw() : Base(__p) {}
    UniquePtr(UniquePtr& __a) throw() : Base(__a) { }
    template<typename _Tp1>
    UniquePtr(UniquePtr<_Tp1>& __a) throw() : Base(__a) {}

    UniquePtr& operator=(UniquePtr& __a) throw() { return Base::operator=(__a); }
    template<typename _Tp1>
    UniquePtr& operator=(UniquePtr<_Tp1>& __a) throw() { return Base::operator=(__a); }

    // Good, now we need to add some parts of the API of unique_ptr.

    bool operator==(const UniquePtr& two) const { return get() == two.get(); }
    bool operator!=(const UniquePtr& two) const { return get() != two.get(); }

    bool operator==(const element_type* two) const { return get() == two; }
    bool operator!=(const element_type* two) const { return get() != two; }

    operator bool () { return 0!= get(); }
};


#endif

class CTimer
{
public:
   CTimer();
   ~CTimer();

public:

      /// Sleep for "interval" CCs.
      /// @param interval [in] CCs to sleep.

   void sleep(uint64_t interval);

      /// Seelp until CC "nexttime".
      /// @param nexttime [in] next time the caller is waken up.

   void sleepto(uint64_t nexttime);

      /// Stop the sleep() or sleepto() methods.

   void interrupt();

      /// trigger the clock for a tick, for better granuality in no_busy_waiting timer.

   void tick();

public:

      /// Read the CPU clock cycle into x.
      /// @param x [out] to record cpu clock cycles.

   static void rdtsc(uint64_t &x);

      /// return the CPU frequency.
      /// @return CPU frequency.

   static uint64_t getCPUFrequency();

      /// check the current time, 64bit, in microseconds.
      /// @return current time in microseconds.

   static uint64_t getTime();

      /// trigger an event such as new connection, close, new data, etc. for "select" call.

   static void triggerEvent();

      /// wait for an event to br triggered by "triggerEvent".

   static void waitForEvent();

      /// sleep for a short interval. exact sleep time does not matter

   static void sleep();

private:
   uint64_t getTimeInMicroSec();

private:
   uint64_t m_ullSchedTime;             // next schedulled time

   pthread_cond_t m_TickCond;
   pthread_mutex_t m_TickLock;

   static pthread_cond_t m_EventCond;
   static pthread_mutex_t m_EventLock;

private:
   static uint64_t s_ullCPUFrequency;	// CPU frequency : clock cycles per microsecond
   static uint64_t readCPUFrequency();
   static bool m_bUseMicroSecond;       // No higher resolution timer available, use gettimeofday().
};

////////////////////////////////////////////////////////////////////////////////

class CGuard
{
public:
   /// Constructs CGuard, which locks the given mutex for
   /// the scope where this object exists.
   /// @param lock Mutex to lock
   /// @param if_condition If this is false, CGuard will do completely nothing
   CGuard(pthread_mutex_t& lock, bool if_condition = true);
   ~CGuard();

public:
   static int enterCS(pthread_mutex_t& lock);
   static int leaveCS(pthread_mutex_t& lock);

   static void createMutex(pthread_mutex_t& lock);
   static void releaseMutex(pthread_mutex_t& lock);

   static void createCond(pthread_cond_t& cond);
   static void releaseCond(pthread_cond_t& cond);

private:
   pthread_mutex_t& m_Mutex;            // Alias name of the mutex to be protected
   int m_iLocked;                       // Locking status

   CGuard& operator=(const CGuard&);
};

class InvertedGuard
{
    pthread_mutex_t* m_pMutex;
public:

    InvertedGuard(pthread_mutex_t* smutex): m_pMutex(smutex)
    {
        if ( !smutex )
            return;

        CGuard::leaveCS(*smutex);
    }

    ~InvertedGuard()
    {
        if ( !m_pMutex )
            return;

        CGuard::enterCS(*m_pMutex);
    }
};

////////////////////////////////////////////////////////////////////////////////

// UDT Sequence Number 0 - (2^31 - 1)

// seqcmp: compare two seq#, considering the wraping
// seqlen: length from the 1st to the 2nd seq#, including both
// seqoff: offset from the 2nd to the 1st seq#
// incseq: increase the seq# by 1
// decseq: decrease the seq# by 1
// incseq: increase the seq# by a given offset

class CSeqNo
{
public:
   inline static int seqcmp(int32_t seq1, int32_t seq2)
   {return (abs(seq1 - seq2) < m_iSeqNoTH) ? (seq1 - seq2) : (seq2 - seq1);}

   inline static int seqlen(int32_t seq1, int32_t seq2)
   {return (seq1 <= seq2) ? (seq2 - seq1 + 1) : (seq2 - seq1 + m_iMaxSeqNo + 2);}

   inline static int seqoff(int32_t seq1, int32_t seq2)
   {
      if (abs(seq1 - seq2) < m_iSeqNoTH)
         return seq2 - seq1;

      if (seq1 < seq2)
         return seq2 - seq1 - m_iMaxSeqNo - 1;

      return seq2 - seq1 + m_iMaxSeqNo + 1;
   }

   inline static int32_t incseq(int32_t seq)
   {return (seq == m_iMaxSeqNo) ? 0 : seq + 1;}

   inline static int32_t decseq(int32_t seq)
   {return (seq == 0) ? m_iMaxSeqNo : seq - 1;}

   inline static int32_t incseq(int32_t seq, int32_t inc)
   {return (m_iMaxSeqNo - seq >= inc) ? seq + inc : seq - m_iMaxSeqNo + inc - 1;}
   // m_iMaxSeqNo >= inc + sec  --- inc + sec <= m_iMaxSeqNo
   // if inc + sec > m_iMaxSeqNo then return seq + inc - (m_iMaxSeqNo+1)

   inline static int32_t decseq(int32_t seq, int32_t dec)
   {
       // Check if seq - dec < 0, but before it would have happened
       if ( seq < dec )
       {
           int32_t left = dec - seq; // This is so many that is left after dragging dec to 0
           // So now decrement the (m_iMaxSeqNo+1) by "left"
           return m_iMaxSeqNo - left + 1;
       }
       return seq - dec;
   }

public:
   static const int32_t m_iSeqNoTH = 0x3FFFFFFF;             // threshold for comparing seq. no.
   static const int32_t m_iMaxSeqNo = 0x7FFFFFFF;            // maximum sequence number used in UDT
};

////////////////////////////////////////////////////////////////////////////////

// UDT ACK Sub-sequence Number: 0 - (2^31 - 1)

class CAckNo
{
public:
   inline static int32_t incack(int32_t ackno)
   {return (ackno == m_iMaxAckSeqNo) ? 0 : ackno + 1;}

public:
   static const int32_t m_iMaxAckSeqNo = 0x7FFFFFFF;         // maximum ACK sub-sequence number used in UDT
};



////////////////////////////////////////////////////////////////////////////////

struct CIPAddress
{
   static bool ipcmp(const struct sockaddr* addr1, const struct sockaddr* addr2, int ver = AF_INET);
   static void ntop(const struct sockaddr* addr, uint32_t ip[4], int ver = AF_INET);
   static void pton(struct sockaddr* addr, const uint32_t ip[4], int ver = AF_INET);
   static std::string show(const struct sockaddr* adr);
};

////////////////////////////////////////////////////////////////////////////////

struct CMD5
{
   static void compute(const char* input, unsigned char result[16]);
};

// Debug stats
template <size_t SIZE>
class StatsLossRecords
{
    int32_t initseq;
    std::bitset<SIZE> array;

public:

    StatsLossRecords(): initseq(-1) {}

    // To check if this structure still keeps record of that sequence.
    // This is to check if the information about this not being found
    // is still reliable.
    bool exists(int32_t seq)
    {
        return initseq != -1 && CSeqNo::seqcmp(seq, initseq) >= 0;
    }

    int32_t base() { return initseq; }

    void clear()
    {
        initseq = -1;
        array.reset();
    }

    void add(int32_t lo, int32_t hi)
    {
        int32_t end = lo + CSeqNo::seqcmp(hi, lo);
        for (int32_t i = lo; i != end; i = CSeqNo::incseq(i))
            add(i);
    }

    void add(int32_t seq)
    {
        if ( array.none() )
        {
            // May happen it wasn't initialized. Set it as initial loss sequence.
            initseq = seq;
            array[0] = true;
            return;
        }

        // Calculate the distance between this seq and the oldest one.
        int seqdiff = CSeqNo::seqcmp(seq, initseq);
        if ( seqdiff > int(SIZE) )
        {
            // Size exceeded. Drop the oldest sequences.
            // First calculate how many must be removed.
            size_t toremove = seqdiff - SIZE;
            // Now, since that position, find the nearest 1
            while ( !array[toremove] && toremove <= SIZE )
                ++toremove;

            // All have to be dropped, so simply reset the array
            if ( toremove == SIZE )
            {
                initseq = seq;
                array[0] = true;
                return;
            }

            // Now do the shift of the first found 1 to position 0
            // and its index add to initseq
            initseq += toremove;
            seqdiff -= toremove;
            array >>= toremove;
        }

        // Now set appropriate bit that represents this seq
        array[seqdiff] = true;
    }

    StatsLossRecords& operator << (int32_t seq)
    {
        add(seq);
        return *this;
    }

    void remove(int32_t seq)
    {
        // Check if is in range. If not, ignore.
        int seqdiff = CSeqNo::seqcmp(seq, initseq);
        if ( seqdiff < 0 )
            return; // already out of array
        if ( seqdiff > SIZE )
            return; // never was added!

        array[seqdiff] = true;
    }

    bool find(int32_t seq) const
    {
        int seqdiff = CSeqNo::seqcmp(seq, initseq);
        if ( seqdiff < 0 )
            return false; // already out of array
        if ( size_t(seqdiff) > SIZE )
            return false; // never was added!

        return array[seqdiff];
    }

#if HAVE_CXX11

    std::string to_string() const
    {
        std::string out;
        for (size_t i = 0; i < SIZE; ++i)
        {
            if ( array[i] )
                out += std::to_string(initseq+i) + " ";
        }

        return out;
    }
#endif
};


template<unsigned MAX_SPAN, int MAX_DRIFT, bool CLEAR_ON_UPDATE = true>
class DriftTracer
{
    int64_t m_qDrift;
    int64_t m_qOverdrift;

    int64_t m_qDriftSum;
    unsigned m_uDriftSpan;

public:
    DriftTracer()
        : m_qDrift(),
        m_qOverdrift(),
        m_qDriftSum(),
        m_uDriftSpan()
    {}

    bool update(int64_t driftval)
    {
        m_qDriftSum += driftval;
        ++m_uDriftSpan;

        if ( m_uDriftSpan >= MAX_SPAN )
        {
            if ( CLEAR_ON_UPDATE )
                m_qOverdrift = 0;

            // Calculate the median of all drift values.
            // In most cases, the divisor should be == MAX_SPAN.
            m_qDrift = m_qDriftSum / m_uDriftSpan;

            // And clear the collection
            m_qDriftSum = 0;
            m_uDriftSpan = 0;

            // In case of "overdrift", save the overdriven value in 'm_qOverdrift'.
            // In clear mode, you should add this value to the time base when update()
            // returns true. The drift value will be since now measured with the
            // overdrift assumed to be added to the base.
            if (std::abs(m_qDrift) > MAX_DRIFT)
            {
                m_qOverdrift = m_qDrift < 0 ? -MAX_DRIFT : MAX_DRIFT;
                m_qDrift -= m_qOverdrift;
            }

            // printDriftOffset(m_qOverdrift, m_qDrift);

            // Timebase is separate
            // m_qTimeBase += m_qOverdrift;

            return true;
        }
        return false;
    }

    // These values can be read at any time, however if you want
    // to depend on the fact that they have been changed lately,
    // you have to check the return value from update().
    //
    // IMPORTANT: drift() can be called at any time, just remember
    // that this value may look different than before only if the
    // last update() return true, which need not be important for you.
    //
    // CASE: CLEAR_ON_UPDATE = true
    // overdrift() should be read only immediately after update() returned
    // true. It will stay available with this value until the next time when
    // update() returns true, in which case the value will be cleared.
    // Therefore, after calling update() if it retuns true, you should read
    // overdrift() immediately an make some use of it. Next valid overdrift
    // will be then relative to every previous overdrift.
    //
    // CASE: CLEAR_ON_UPDATE = false
    // overdrift() will start from 0, but it will always keep track on
    // any changes in overdrift. By manipulating the MAX_DRIFT parameter
    // you can decide how high the drift can go relatively to stay below
    // overdrift.
    int64_t drift() { return m_qDrift; }
    int64_t overdrift() { return m_qOverdrift; }
};


inline std::string FormatBinaryString(const uint8_t* bytes, size_t size)
{
    if ( size == 0 )
        return "";

    char buf[256];
    std::ostringstream os;

    // I know, it's funny to use sprintf and ostringstream simultaneously,
    // but " %02X" in iostream is: << " " << hex << uppercase << setw(2) << setfill('0') << VALUE << setw(1)
    // Too noisy. OTOH ostringstream solves the problem of memory allocation
    // for a string of unpredictable size.
    sprintf(buf, "%02X", int(bytes[0]));
    os << buf;
    for (size_t i = 1; i < size; ++i)
    {
        sprintf(buf, " %02X", int(bytes[i]));
        os << buf;
    }
    return os.str();
}


// Version parsing
inline ATR_CONSTEXPR uint32_t SrtVersion(int major, int minor, int patch)
{
    return patch + minor*0x100 + major*0x10000;
}

inline int32_t SrtParseVersion(const char* v)
{
    int major, minor, patch;
    int result = sscanf(v, "%d.%d.%d", &major, &minor, &patch);

    if ( result != 3 )
    {
        return 0;
        fprintf(stderr, "Invalid version format for HAISRT_VERSION: %s - use m.n.p\n", v);
        throw v; // Throwing exception, as this function will be run before main()
    }

    return major*0x10000 + minor*0x100 + patch;
}

inline std::string SrtVersionString(int version)
{
    int patch = version % 0x100;
    int minor = (version/0x100)%0x100;
    int major = version/0x10000;

    char buf[20];
    sprintf(buf, "%d.%d.%d", major, minor, patch);
    return buf;
}


#endif
