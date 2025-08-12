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

#ifndef INC_SRT_UTILITIES_H
#define INC_SRT_UTILITIES_H

// Windows warning disabler
#define _CRT_SECURE_NO_WARNINGS 1

#include "platform_sys.h"
#include "srt_attr_defs.h" // defines HAVE_CXX11

// Happens that these are defined, undefine them in advance
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <string>
#include <algorithm>
#include <bitset>
#include <map>
#include <vector>
#include <functional>
#include <memory>
#include <iomanip>
#include <sstream>
#include <utility>

#if HAVE_CXX11
#include <type_traits>
#include <unordered_map>
#else

#if !defined(__linux__) || !defined(__GNUG__)
#error C++03 compilation only allowed for Linux with GNU Compiler
#endif

#include <ext/hash_map>
#endif

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "byte_order.h"

namespace srt {

// -------------- UTILITIES ------------------------

// ENDIAN-dependent array copying functions

/// Hardware --> Network (big-endian) byte order conversion
/// @param size source length in four octets
inline void HtoNLA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = htobe32(src[i]);
}

/// Network (big-endian) --> Hardware byte order conversion
/// @param size source length in four octets
inline void NtoHLA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = be32toh(src[i]);
}

// Hardware <--> Intel (little endian) convention
inline void HtoILA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = htole32(src[i]);
}

inline void ItoHLA(uint32_t* dst, const uint32_t* src, size_t size)
{
    for (size_t i = 0; i < size; ++ i)
        dst[i] = le32toh(src[i]);
}

// Bit numbering utility.
//
// This is something that allows you to turn 32-bit integers into bit fields.
// Although bitfields are part of C++ language, they are not designed to be
// interchanged with 32-bit numbers, and any attempt to doing it (by placing
// inside a union, for example) is nonportable (order of bitfields inside
// same-covering 32-bit integer number is dependent on the endian), so they are
// popularly disregarded as useless. Instead the 32-bit numbers with bits
// individually selected is preferred, with usually manual playing around with
// & and | operators, as well as << and >>. This tool is designed to simplify
// the use of them. This can be used to qualify a range of bits inside a 32-bit
// number to be a separate number, you can "wrap" it by placing the integer
// value in the range of these bits, as well as "unwrap" (extract) it from
// the given place. For your own safety, use one prefix to all constants that
// concern bit ranges intended to be inside the same "bit container".
//
// Usage: typedef Bits<leftmost, rightmost> MASKTYPE;  // MASKTYPE is a name of your choice.
//
// With this defined, you can use the following members:
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
    static const uint32_t value = 1u << R;
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
    // DID YOU GET a kind-of error: 'mask' is not a member of 'Bits<3u, 5u, false>'?
    // See the the above declaration of 'correct'!
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

    template<class T>
    static T unwrapt(uint32_t bitset) { return static_cast<T>(unwrap(bitset)); }
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
template <typename FieldType, size_t NoOfFields, typename IndexerType>
struct DynamicStruct
{
    FieldType inarray[NoOfFields];

    void clear()
    {
        // As a standard library, it can be believed that this call
        // can be optimized when FieldType is some integer.
        std::fill(inarray, inarray + NoOfFields, FieldType());
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


/// Fixed-size array template class.
template <class T, class Indexer = size_t>
class FixedArray
{
public:
    FixedArray(size_t size)
        : m_size(size)
        , m_entries(new T[size])
    {
    }

    ~FixedArray()
    {
        delete [] m_entries;
    }

public:
    const T& operator[](Indexer index) const
    {
        if (int(index) >= int(m_size))
            throw_invalid_index(int(index));

        return m_entries[int(index)];
    }

    T& operator[](Indexer index)
    {
        if (int(index) >= int(m_size))
            throw_invalid_index(int(index));

        return m_entries[int(index)];
    }


    size_t size() const { return m_size; }

    typedef T* iterator;
    typedef const T* const_iterator;

    iterator begin() { return m_entries; }
    iterator end() { return m_entries + m_size; }

    const_iterator cbegin() const { return m_entries; }
    const_iterator cend() const { return m_entries + m_size; }

    T* data() { return m_entries; }

private:
    FixedArray(const FixedArray<T>& );
    FixedArray<T>& operator=(const FixedArray<T>&);

    void throw_invalid_index(int i) const
    {
        std::stringstream ss;
        ss << "Index " << i << "out of range";
        throw std::runtime_error(ss.str());
    }

private:
    size_t      m_size;
    T* const    m_entries;
};

// NOTE: ALL logging instructions are commented-out here.
// They were used for debugging and can be also restored,
// but this header file should not include logging, hence
// this isn't implemented.
template <class NodeType, class Access = NodeType>
class HeapSet
{
    std::vector<NodeType> m_HeapArray;
public:

    static const size_t npos = std::string::npos;

    // Constructor
    HeapSet(size_t capa = 0)
    {
        if (capa)
            m_HeapArray.reserve(capa);
    }

    const std::vector<NodeType>& raw() const { return m_HeapArray; }

    bool empty() const { return m_HeapArray.empty(); }
    bool size() const { return m_HeapArray.size(); }
    const NodeType operator[](size_t ix) const
    {
        return m_HeapArray[ix];
    }

    static size_t parent(size_t i) { return (i-1)/2; }

    // to get index of left child of node at index i
    static size_t left(size_t i) { return (2*i + 1); }

    // to get index of right child of node at index i
    static size_t right(size_t i) { return (2*i + 2); }

private:

    NodeType pop_last()
    {
        NodeType out = m_HeapArray[m_HeapArray.size()-1];
        //LOG("POP-LAST: reheap after removal of: ", Access::print(out));
        m_HeapArray.pop_back();
        Access::position(out) = npos;
        return out;
    }

    // This function shall only be called if m_HeapArray.size() == 1.
    // It simply removes and returns one and the only element it contains.
    NodeType pop_one()
    {
        NodeType nod = m_HeapArray[0];
        Access::position(nod) = npos;
        m_HeapArray.clear();
        return nod;
    }

public:

    // to extract the root which is the minimum element
    NodeType pop()
    {
        size_t s = m_HeapArray.size();
        if (s == 0)
        {
            //LOG("POP: empty");
            return Access::none();
        }
        if (s == 1)
        {
            //LOG("POP: one");
            return pop_one();
        }

        //LOG("POP: SWAP [0]", Access::print(m_HeapArray[0]), " <-> [", (s-1), "]", Access::print(m_HeapArray[s-1]) );

        std::swap(m_HeapArray[0], m_HeapArray[s-1]);
        Access::position(m_HeapArray[0]) = 0;

        NodeType last = pop_last();
        reheap(0);
        return last;
    }

    // Decreases key value of key at index i to new_val
    //void decreaseKey(int i, int new_val);

    // Returns the minimum key (key at root) from min heap
    NodeType top() { return m_HeapArray[0]; }

    // Inserts a new key 'k'
    size_t insert(NodeType node)
    {
        // First insert the new key at the end
        Access::position(node) = m_HeapArray.size();
        m_HeapArray.push_back(node);

        // LOG("INSERT: ", Access::print(node), " initial position: ", Access::position(node) );

        // Fix the min heap property if it is violated
        for (size_t i = m_HeapArray.size() - 1; i != 0; i = parent(i))
        {
            // LOG("INSERT: CHECK ORDER: [", i, "]", Access::print(m_HeapArray[i]), "  <  [", parent(i), "]", Access::print(m_HeapArray[parent(i)]) );
            if (Access::order(Access::key(m_HeapArray[i]), Access::key(m_HeapArray[parent(i)])))
            {
                // LOG("INSERT: SWAP ", Access::print(m_HeapArray[i]), " <-> ", Access::print(m_HeapArray[parent(i)]) );
                std::swap(m_HeapArray[i], m_HeapArray[parent(i)]);
                // After swapping restore their original positions
                Access::position(m_HeapArray[i]) = i;
                Access::position(m_HeapArray[parent(i)]) = parent(i);
            }
            else
                break;
        }
        return Access::position(node);
    }

    bool erase(NodeType node)
    {
        // Assume the node is in the hash; make sure about the position first.
        size_t pos = Access::position(node);
        if (pos == npos)
           return false;

        //assert(pos < m_HeapArray.size() && m_HeapArray[pos] == node);

        size_t lastx = m_HeapArray.size() - 1;
        if (lastx == 0)
        {
            // LOG("ERASE: one element, clearing");
            // One and the only element; enough to clear the container.
            Access::position(node) = npos;
            m_HeapArray.clear();
            return true;
        }

        // If position is the last element in the array, there's
        // nothing to swap anyway.
        if (pos != lastx)
        {
            // LOG("ERASE: SWAP ", Access::print(m_HeapArray[pos]), " <-> ", Access::print(m_HeapArray[lastx]) );
            std::swap(m_HeapArray[pos], m_HeapArray[lastx]);
            Access::position(m_HeapArray[pos]) = pos;
        }

        pop_last();
        reheap(0);
        if (pos != lastx)
        {
            reheap(pos);
        }
        return true;
    }

    // to heapify a subtree with the root at given index
    void reheap(size_t i)
    {
        size_t l = left(i);
        size_t r = right(i);
        size_t earliest = i;

#if 0 // ENABLE_LOGGING
        std::string which = "parent";
        // LOGN("REHEAP: [", i, "]", Access::print(m_HeapArray[i]), " -> ");
        if (l < m_HeapArray.size())
        {
            // LOGN("[", l, "]", Access::print(m_HeapArray[l]));
            if (r < m_HeapArray.size())
            {
                // LOGN(" , [", r, "]", Access::print(m_HeapArray[r]));
            }
            else
            {
            // LOGN("[", r, "] (OVER ", m_HeapArray.size(), ")");
            }
        }
        else
        {
            // LOGN("[", l, "] (OVER ", m_HeapArray.size(), ")");
        }
        // LOG();
#endif

        if (l < m_HeapArray.size() && Access::order(Access::key(m_HeapArray[l]), Access::key(m_HeapArray[i])))
        {
            earliest = l;
            // IF_LOGGING(which = "left");
        }
        if (r < m_HeapArray.size() && Access::order(Access::key(m_HeapArray[r]), Access::key(m_HeapArray[earliest])))
        {
            earliest = r;
            // IF_LOGGING(which = "right");
        }
        // LOG("REHEAP: EARLIEST: ", which, ": -> [", earliest, "]", Access::print(m_HeapArray[earliest]) );

        if (earliest != i)
        {
            // LOG("REHEAP: SWAP ", Access::print(m_HeapArray[i]), " <-> ", Access::print(m_HeapArray[earliest]), " CONTINUE FROM [", earliest, "]");
            std::swap(m_HeapArray[i], m_HeapArray[earliest]);
            Access::position(m_HeapArray[i]) = i;
            Access::position(m_HeapArray[earliest]) = earliest;
            reheap(earliest);
        }
        else
        {
            // LOG("REHEAP: parent earlier than children, exitting procedure");
        }
    }

    // Change the key value and let the element flow through
    template <class KeyType>
    void update(NodeType node, const KeyType& newkey)
    {
        size_t pos = Access::position(node);
        return update(pos, newkey);
    }

    template <class KeyType>
    void update(size_t pos, const KeyType& newkey)
    {
        NodeType node = m_HeapArray[pos];
        const KeyType& oldkey = Access::key(node);
        Access::key(node) = newkey;

        // LOG("UPDATE: rewind from [", pos, "]:");
        for (size_t i = pos; i != 0; i = parent(i))
        {
            if (Access::order(Access::key(m_HeapArray[i]), Access::key(m_HeapArray[parent(i)])))
            {
                // LOG("UPDATE: SWAP ", Access::print(m_HeapArray[i]), " <-> ", Access::print(m_HeapArray[parent(i)]), " CONTINUE FROM [", parent(i), "]");
                std::swap(m_HeapArray[i], m_HeapArray[parent(i)]);
                Access::position(m_HeapArray[i]) = i;
                Access::position(m_HeapArray[parent(i)]) = parent(i);
            }
            else
                break;
        }
    }

    // Note: Access::print is optional, as long as you don't use this function.
    void print_tree(std::ostream& out, size_t from = 0, int tabs = 0) const
    {
        for (size_t t = 0; t < tabs; ++t)
            out << "  ";
        out << "[" << from << "]";
        if (from != Access::position(m_HeapArray[from]))
            out << "!POS=" << Access::position(m_HeapArray[from]) << "!";
        out << "=" << Access::print(m_HeapArray[from]) << std::endl;
        size_t l = left(from), r = right(from);
        size_t size = m_HeapArray.size();

        if (l < size)
        {
            print_tree(out, l, tabs + 1);
            if (r < size)
                print_tree(out, r, tabs + 1);
        }
    }

};

// ------------------------------------------------------------



inline bool IsSet(int32_t bitset, int32_t flagset)
{
    return (bitset & flagset) == flagset;
}

// std::addressof in C++11,
// needs to be provided for C++03
template <class RefType>
inline RefType* AddressOf(RefType& r)
{
    return (RefType*)(&(unsigned char&)(r));
}

template <class T>
struct explicit_t
{
    T inobject;
    explicit_t(const T& uo): inobject(uo) {}

    operator T() const { return inobject; }

private:
    template <class X>
    explicit_t(const X& another);
};

// This is required for Printable function if you have a container of pairs,
// but this function has a different definition for C++11 and C++03.
namespace srt_pair_op
{
    template <class Value1, class Value2>
    std::ostream& operator<<(std::ostream& s, const std::pair<Value1, Value2>& v)
    {
        s << "{" << v.first << " " << v.second << "}";
        return s;
    }
}

namespace any_op
{
    template <class T>
    struct AnyProxy
    {
        const T& value;
        bool result;

        AnyProxy(const T& x, bool res): value(x), result(res) {}

        AnyProxy<T>& operator,(const T& val)
        {
            if (result)
                return *this;
            result = value == val;
            return *this;
        }

        operator bool() { return result; }
    };

    template <class T> inline
    AnyProxy<T> EqualAny(const T& checked_val)
    {
        return AnyProxy<T>(checked_val, false);
    }
}

#if HAVE_CXX11

template <class In>
inline auto Move(In& i) -> decltype(std::move(i)) { return std::move(i); }

// Gluing string of any type, wrapper for operator <<

template <class Stream>
inline Stream& Print(Stream& in) { return in;}

template <class Stream, class Arg1, class... Args>
inline Stream& Print(Stream& sout, Arg1&& arg1, Args&&... args)
{
    sout << std::forward<Arg1>(arg1);
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

template <class Container, class Value = typename Container::value_type, typename... Args> inline
std::string Printable(const Container& in, Value /*pseudoargument*/, Args&&... args)
{
    using namespace srt_pair_op;
    std::ostringstream os;
    Print(os, args...);
    os << "[ ";
    for (auto i: in)
        os << Value(i) << " ";
    os << "]";
    return os.str();
}

template <class Container> inline
std::string Printable(const Container& in)
{
    using namespace srt_pair_op;
    using Value = typename Container::value_type;
    return Printable(in, Value());
}

template<typename Map, typename Key>
auto map_get(Map& m, const Key& key, typename Map::mapped_type def = typename Map::mapped_type()) -> typename Map::mapped_type
{
    auto it = m.find(key);
    return it == m.end() ? def : it->second;
}

template<typename Map, typename Key>
auto map_getp(Map& m, const Key& key) -> typename Map::mapped_type*
{
    auto it = m.find(key);
    return it == m.end() ? nullptr : std::addressof(it->second);
}

template<typename Map, typename Key>
auto map_getp(const Map& m, const Key& key) -> typename Map::mapped_type const*
{
    auto it = m.find(key);
    return it == m.end() ? nullptr : std::addressof(it->second);
}


// C++11 allows us creating template type aliases, so we can rename unordered_map
// into hash_map easily.

template<class _Key, class _Tp, class _HashFn = std::hash<_Key>,
	   class _EqualKey = std::equal_to<_Key>>
using hash_map = std::unordered_map<_Key, _Tp, _HashFn, _EqualKey>;

#else

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
    explicit UniquePtr(element_type* p = 0) throw() : Base(p) {}
    UniquePtr(UniquePtr& a) throw() : Base(a) { }
    template<typename Type1>
    UniquePtr(UniquePtr<Type1>& a) throw() : Base(a) {}

    UniquePtr& operator=(UniquePtr& a) throw() { return Base::operator=(a); }
    template<typename Type1>
    UniquePtr& operator=(UniquePtr<Type1>& a) throw() { return Base::operator=(a); }

    // Good, now we need to add some parts of the API of unique_ptr.

    bool operator==(const UniquePtr& two) const { return get() == two.get(); }
    bool operator!=(const UniquePtr& two) const { return get() != two.get(); }

    bool operator==(const element_type* two) const { return get() == two; }
    bool operator!=(const element_type* two) const { return get() != two; }

    operator bool () const { return 0!= get(); }
};

// A primitive one-argument versions of Sprint and Printable
template <class Arg1>
inline std::string Sprint(const Arg1& arg)
{
    std::ostringstream sout;
    sout << arg;
    return sout.str();
}

// Ok, let it be 2-arg, in case when a manipulator is needed
template <class Arg1, class Arg2>
inline std::string Sprint(const Arg1& arg1, const Arg2& arg2)
{
    std::ostringstream sout;
    sout << arg1 << arg2;
    return sout.str();
}

template <class Container> inline
std::string Printable(const Container& in)
{
    using namespace srt_pair_op;
    typedef typename Container::value_type Value;
    std::ostringstream os;
    os << "[ ";
    for (typename Container::const_iterator i = in.begin(); i != in.end(); ++i)
        os << Value(*i) << " ";
    os << "]";

    return os.str();
}

template<typename Map, typename Key>
typename Map::mapped_type map_get(Map& m, const Key& key, typename Map::mapped_type def = typename Map::mapped_type())
{
    typename Map::iterator it = m.find(key);
    return it == m.end() ? def : it->second;
}

template<typename Map, typename Key>
typename Map::mapped_type map_get(const Map& m, const Key& key, typename Map::mapped_type def = typename Map::mapped_type())
{
    typename Map::const_iterator it = m.find(key);
    return it == m.end() ? def : it->second;
}

template<typename Map, typename Key>
typename Map::mapped_type* map_getp(Map& m, const Key& key)
{
    typename Map::iterator it = m.find(key);
    return it == m.end() ? (typename Map::mapped_type*)0 : &(it->second);
}

template<typename Map, typename Key>
typename Map::mapped_type const* map_getp(const Map& m, const Key& key)
{
    typename Map::const_iterator it = m.find(key);
    return it == m.end() ? (typename Map::mapped_type*)0 : &(it->second);
}

// Hash map: simply use the original name "hash_map".
// NOTE: Since 1.6.0 version, the only allowed build configuration for
// using C++03 is GCC on Linux. For all other compiler and platform types
// a C++11 capable compiler is requried.
namespace srt
{
    using __gnu_cxx::hash_map;
}

#endif

// This function replaces partially the functionality of std::map::insert.
// Differences:
// - inserts only a default value
// - returns the reference to the value in the map
// - works for value types that are not copyable
// The reference is returned because to return the node you would have
// to search for it after using operator[].
// NOTE: In C++17 it's possible to simply use map::try_emplace with only
// the key argument and this would do the same thing, while returning a
// pair with iterator.
template<typename Map, typename Key>
inline std::pair<typename Map::mapped_type&, bool> map_tryinsert(Map& mp, const Key& k)
{
    typedef typename Map::mapped_type Value;
    size_t sizeb4 = mp.size();
    Value& ref = mp[k];

    return std::pair<Value&, bool>(ref, mp.size() > sizeb4);
}

// Printable with prefix added for every element.
// Useful when printing a container of sockets or sequence numbers.
template <class Container> inline
std::string PrintableMod(const Container& in, const std::string& prefix)
{
    using namespace srt_pair_op;
    typedef typename Container::value_type Value;
    std::ostringstream os;
    os << "[ ";
    for (typename Container::const_iterator y = in.begin(); y != in.end(); ++y)
        os << prefix << Value(*y) << " ";
    os << "]";
    return os.str();
}

template<typename InputIterator, typename OutputIterator, typename TransFunction>
inline void FilterIf(InputIterator bg, InputIterator nd,
        OutputIterator out, TransFunction fn)
{
    for (InputIterator i = bg; i != nd; ++i)
    {
        std::pair<typename TransFunction::result_type, bool> result = fn(*i);
        if (!result.second)
            continue;
        *out++ = result.first;
    }
}

template <class Value, class ArgValue>
inline void insert_uniq(std::vector<Value>& v, const ArgValue& val)
{
    typename std::vector<Value>::iterator i = std::find(v.begin(), v.end(), val);
    if (i != v.end())
        return;

    v.push_back(val);
}

template <class Type1, class Type2>
inline std::pair<Type1&, Type2&> Tie(Type1& var1, Type2& var2)
{
    return std::pair<Type1&, Type2&>(var1, var2);
}

// This can be used in conjunction with Tie to simplify the code
// in loops around a whole container:
// list<string>::const_iterator it, end;
// Tie(it, end) = All(list_container);
template<class Container>
std::pair<typename Container::iterator, typename Container::iterator>
inline All(Container& c) { return std::make_pair(c.begin(), c.end()); }

template<class Container>
std::pair<typename Container::const_iterator, typename Container::const_iterator>
inline All(const Container& c) { return std::make_pair(c.begin(), c.end()); }


template <class Container, class Value>
inline void FringeValues(const Container& from, std::map<Value, size_t>& out)
{
    for (typename Container::const_iterator i = from.begin(); i != from.end(); ++i)
        ++out[*i];
}

template <class Signature, class Opaque = void*>
struct CallbackHolder
{
    Opaque opaque;
    Signature* fn;

    CallbackHolder(): opaque(NULL), fn(NULL)  {}
    CallbackHolder(Opaque o, Signature* f): opaque(o), fn(f) {}

    void set(Opaque o, Signature* f)
    {
        // Test if the pointer is a pointer to function. Don't let
        // other type of pointers here.
#if HAVE_CXX11
        static_assert(std::is_function<Signature>::value, "CallbackHolder is for functions only!");
#else
        // This is a poor-man's replacement, which should in most compilers
        // generate a warning, if `Signature` resolves to a value type.
        // This would make an illegal pointer cast from a value to a function type.
        // Casting function-to-function, however, should not. Unfortunately
        // newer compilers disallow that, too (when a signature differs), but
        // then they should better use the C++11 way, much more reliable and safer.
        Opaque (*testfn)(Opaque) = (Opaque(*)(Opaque))f;
        (void)(testfn);
#endif
        opaque = o;
        fn = f;
    }

    operator bool() { return fn != NULL; }
};

#define CALLBACK_CALL(holder,...) (*holder.fn)(holder.opaque, __VA_ARGS__)
// The version of std::tie from C++11, but for pairs only.
template <class T1, class T2>
struct PairProxy
{
    T1& v1;
    T2& v2;

    PairProxy(T1& c1, T2& c2): v1(c1), v2(c2) {}

    void operator=(const std::pair<T1, T2>& p)
    {
        v1 = p.first;
        v2 = p.second;
    }
};

template <class T1, class T2> inline
PairProxy<T1, T2> Tie2(T1& v1, T2& v2)
{
    return PairProxy<T1, T2>(v1, v2);
}

template<class T>
struct PassFilter
{
    T lower, median, upper;

    bool encloses(const T& value)
    {
        // Throw away those that don't fit in the filter
        return value > lower && value < upper;
    }
};

// This utility is used in window.cpp where it is required to calculate
// the median value basing on the value in the very middle and filtered
// out values exceeding its range of 1/8 and 8 times. Returned is a structure
// that shows the median and also the lower and upper value used for filtering.
inline PassFilter<int> GetPeakRange(const int* window, int* replica, size_t size)
{
    // This calculation does more-less the following:
    //
    // 1. Having example window:
    //  - 50, 51, 100, 55, 80, 1000, 600, 1500, 1200, 10, 90
    // 2. This window is now sorted, but we only know the value in the middle:
    //  - 10, 50, 51, 55, 80, [[90]], 100, 600, 1000, 1200, 1500
    // 3. Now calculate:
    //   - lower: 90/8 = 11.25
    //   - upper: 90*8 = 720
    // 4. Now calculate the arithmetic median from all these values,
    //    but drop those from outside the <lower, upper> range:
    //  - 10, (11<) [ 50, 51, 55, 80, 90, 100, 600, ] (>720) 1000, 1200, 1500
    // 5. Calculate the median from the extracted range,
    //    NOTE: the median is actually repeated once, so size is +1.
    //
    //    values = { 50, 51, 55, 80, 90, 100, 600 };
    //    sum = 90 + accumulate(values); ==> 1026
    //    median = sum/(1 + values.size()); ==> 147
    //
    // For comparison: the overall arithmetic median from this window == 430
    //
    // 6. Returned value = 1M/median

    // get median value, but cannot change the original value order in the window
    std::copy(window, window + size, replica);
    std::nth_element(replica, replica + (size / 2), replica + size);
    //std::sort(replica, replica + psize); <--- was used for debug, just leave it as a mark

    PassFilter<int> filter;
    filter.median = replica[size / 2];
    filter.upper = filter.median << 3; // median*8
    filter.lower = filter.median >> 3; // median/8

    return filter;
}

// This function sums up all values in the array (from p to end),
// except those that don't fit in the low- and high-pass filter.
// Returned is the sum and the number of elements taken into account.
inline std::pair<int, int> AccumulatePassFilter(const int* p, size_t size, PassFilter<int> filter)
{
    int count = 0;
    int sum = 0;
    const int* const end = p + size;
    for (; p != end; ++p)
    {
        // Throw away those that don't fit in the filter
        if (!filter.encloses(*p))
            continue;

        sum += *p;
        ++count;
    }

    return std::make_pair(sum, count);
}

// This function sums up all values in the array (from p to end)
// and simultaneously elements from `para`, stated it points to
// an array of the same size. The first array is used as a driver
// for which elements to include and which to skip, and this is done
// for both arrays at particular index position. Returner is the sum
// of the elements passed from the first array and from the `para`
// array, as well as the number of included elements.
template <class IntCount, class IntParaCount>
inline void AccumulatePassFilterParallel(const int* p, size_t size, PassFilter<int> filter,
        const int* para,
        int& w_sum, IntCount& w_count, IntParaCount& w_paracount)
{
    IntCount count = 0;
    int sum = 0;
    IntParaCount parasum = 0;
    const int* const end = p + size;
    for (; p != end; ++p, ++para)
    {
        // Throw away those that don't fit in the filter
        if (!filter.encloses(*p))
            continue;

        sum += *p;
        parasum += *para;
        ++count;
    }
    w_count = count;
    w_sum = sum;
    w_paracount = parasum;
}


inline std::string FormatBinaryString(const uint8_t* bytes, size_t size)
{
    if ( size == 0 )
        return "";

    using namespace std;

    ostringstream os;
    os << setfill('0') << setw(2) << hex << uppercase;

    for (size_t i = 0; i < size; ++i)
    {
        os << int(bytes[i]);
    }
    return os.str();
}


/// This class is useful in every place where
/// the time drift should be traced. It's currently in use in every
/// solution that implements any kind of TSBPD.
template<unsigned MAX_SPAN, int MAX_DRIFT, bool CLEAR_ON_UPDATE = true>
class DriftTracer
{
    int64_t  m_qDrift;
    int64_t  m_qOverdrift;

    int64_t  m_qDriftSum;
    unsigned m_uDriftSpan;

public:
    DriftTracer()
        : m_qDrift(0)
        , m_qOverdrift(0)
        , m_qDriftSum(0)
        , m_uDriftSpan(0)
    {}

    bool update(int64_t driftval)
    {
        m_qDriftSum += driftval;
        ++m_uDriftSpan;

        // I moved it here to calculate accumulated overdrift.
        if (CLEAR_ON_UPDATE)
            m_qOverdrift = 0;

        if (m_uDriftSpan < MAX_SPAN)
            return false;


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

    // For group overrides
    void forceDrift(int64_t driftval)
    {
        m_qDrift = driftval;
    }

    // These values can be read at any time, however if you want
    // to depend on the fact that they have been changed lately,
    // you have to check the return value from update().
    //
    // IMPORTANT: drift() can be called at any time, just remember
    // that this value may look different than before only if the
    // last update() returned true, which need not be important for you.
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
    int64_t drift() const { return m_qDrift; }
    int64_t overdrift() const { return m_qOverdrift; }
};

template <class KeyType, class ValueType>
struct MapProxy
{
    std::map<KeyType, ValueType>& mp;
    const KeyType& key;

    MapProxy(std::map<KeyType, ValueType>& m, const KeyType& k): mp(m), key(k) {}

    void operator=(const ValueType& val)
    {
        mp[key] = val;
    }

    typename std::map<KeyType, ValueType>::iterator find()
    {
        return mp.find(key);
    }

    typename std::map<KeyType, ValueType>::const_iterator find() const
    {
        return mp.find(key);
    }

    operator ValueType() const
    {
        typename std::map<KeyType, ValueType>::const_iterator p = find();
        if (p == mp.end())
            return ValueType();
        return p->second;
    }

    ValueType deflt(const ValueType& defval) const
    {
        typename std::map<KeyType, ValueType>::const_iterator p = find();
        if (p == mp.end())
            return defval;
        return p->second;
    }

    bool exists() const
    {
        return find() != mp.end();
    }

    std::pair<ValueType&, bool> dig()
    {
        return map_tryinsert(mp, key);
    }
};

/// Print some hash-based stamp of the first 16 bytes in the buffer
inline std::string BufferStamp(const char* mem, size_t size)
{
    using namespace std;
    char spread[16];

    if (size < 16)
        memset((spread + size), 0, 16 - size);
    memcpy((spread), mem, min(size_t(16), size));

    // Now prepare 4 cells for uint32_t.
    union
    {
        uint32_t sum;
        char cells[4];
    };
    memset((cells), 0, 4);

    for (size_t x = 0; x < 4; ++x)
        for (size_t y = 0; y < 4; ++y)
        {
            cells[x] += spread[x+4*y];
        }

    // Convert to hex string
    ostringstream os;
    os << hex << uppercase << setfill('0') << setw(8) << sum;

    return os.str();
}

template <class OutputIterator>
inline void Split(const std::string & str, char delimiter, OutputIterator tokens)
{
    if ( str.empty() )
        return; // May cause crash and won't extract anything anyway

    std::size_t start;
    std::size_t end = -1;

    do
    {
        start = end + 1;
        end = str.find(delimiter, start);
        *tokens = str.substr(
                start,
                (end == std::string::npos) ? std::string::npos : end - start);
        ++tokens;
    } while (end != std::string::npos);
}

inline std::string SelectNot(const std::string& unwanted, const std::string& s1, const std::string& s2)
{
    if (s1 == unwanted)
        return s2; // might be unwanted, too, but then, there's nothing you can do anyway
    if (s2 == unwanted)
        return s1;

    // Both have wanted values, so now compare if they are same
    if (s1 == s2)
        return s1; // occasionally there's a winner

    // Irresolvable situation.
    return std::string();
}

inline std::string SelectDefault(const std::string& checked, const std::string& def)
{
    if (checked == "")
        return def;
    return checked;
}

template <class It>
inline size_t safe_advance(It& it, size_t num, It end)
{
    while ( it != end && num )
    {
        --num;
        ++it;
    }

    return num; // will be effectively 0, if reached the required point, or >0, if end was by that number earlier
}

// This is available only in C++17, dunno why not C++11 as it's pretty useful.
template <class V, size_t N> inline
ATR_CONSTEXPR size_t Size(const V (&)[N]) ATR_NOEXCEPT { return N; }

template <size_t DEPRLEN, typename ValueType>
inline ValueType avg_iir(ValueType old_value, ValueType new_value)
{
    return (old_value * (DEPRLEN - 1) + new_value) / DEPRLEN;
}

template <size_t DEPRLEN, typename ValueType>
inline ValueType avg_iir_w(ValueType old_value, ValueType new_value, size_t new_val_weight)
{
    return (old_value * (DEPRLEN - new_val_weight) + new_value * new_val_weight) / DEPRLEN;
}

template <class T>
inline T CountIIR(T base, T newval, double factor)
{
    if ( base == 0.0 )
        return newval;

    T diff = newval - base;
    return base+T(diff*factor);
}


// Property accessor definitions
//
// "Property" is a special method that accesses given field.
// This relies only on a convention, which is the following:
//
// V x = object.prop(); <-- get the property's value
// object.set_prop(x); <-- set the property a value
//
// Properties might be also chained when setting:
//
// object.set_prop1(v1).set_prop2(v2).set_prop3(v3);
//
// Properties may be defined various even very complicated
// ways, which is simply providing a method with body. In order
// to define a property simplest possible way, that is, refer
// directly to the field that keeps it, here are the following macros:
//
// Prefix: SRTU_PROPERTY_
// Followed by:
//  - access type: RO, WO, RW, RR, RRW
//  - chain flag: optional _CHAIN
// Where access type is:
// - RO - read only. Defines reader accessor. The accessor method will be const.
// - RR - read reference. The accessor isn't const to allow reference passthrough.
// - WO - write only. Defines writer accessor.
// - RW - combines RO and WO.
// - RRW - combines RR and WO.
//
// The _CHAIN marker is optional for macros providing writable accessors
// for properties. The difference is that while simple write accessors return
// void, the chaining accessors return the reference to the object for which
// the write accessor was called so that you can call the next accessor (or
// any other method as well) for the result.

#define SRTU_PROPERTY_RR(type, name, field) type name() { return field; }
#define SRTU_PROPERTY_RO(type, name, field) type name() const { return field; }
#define SRTU_PROPERTY_WO(type, name, field) void set_##name(type arg) { field = arg; }
#define SRTU_PROPERTY_WO_ARG(type, name, expr) void set_##name(type arg) { expr; }
#define SRTU_PROPERTY_WO_CHAIN(otype, type, name, field) otype& set_##name(type arg) { field = arg; return *this; }
#define SRTU_PROPERTY_RW(type, name, field) SRTU_PROPERTY_RO(type, name, field); SRTU_PROPERTY_WO(type, name, field)
#define SRTU_PROPERTY_RRW(type, name, field) SRTU_PROPERTY_RR(type, name, field); SRTU_PROPERTY_WO(type, name, field)
#define SRTU_PROPERTY_RW_CHAIN(otype, type, name, field) SRTU_PROPERTY_RO(type, name, field); SRTU_PROPERTY_WO_CHAIN(otype, type, name, field)
#define SRTU_PROPERTY_RRW_CHAIN(otype, type, name, field) SRTU_PROPERTY_RR(type, name, field); SRTU_PROPERTY_WO_CHAIN(otype, type, name, field)

} // namespace srt

#endif
