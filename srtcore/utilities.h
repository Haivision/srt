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
#include <utility>

#if HAVE_CXX11
#include <type_traits>
#include <unordered_map>
#else

#if !defined(__GNUG__) || !(defined(__linux__) || defined(__MINGW32__))
#error C++03 compilation only allowed for Linux or MinGW with GNU Compiler
#endif

#include <ext/hash_map>
#endif

#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "ofmt.h"
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

// Bit numbering utility. See docs/dev/utilities.md.

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
// evaluates to false - stops infinite template instantiation recursion with error.
template <size_t L, size_t R>
struct BitsetMask<L, R, false>
{
};

template <size_t L, size_t R = L>
struct Bits
{
    // DID YOU GET a kind-of error: 'mask' is not a member of 'Bits<3u, 5u, false>'?
    // See the declaration of 'correct' in the master definition of struct BitsetMask.
    static const uint32_t mask = BitsetMask<L, R>::value;
    static const uint32_t offset = R;
    static const size_t size = L - R + 1;

    static bool fit(uint32_t value) { return (BitsetMask<L-R, 0>::value & value) == value; }

    static uint32_t wrap(uint32_t baseval) { return (baseval << offset) & mask; }

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

inline bool IsSet(int32_t bitset, int32_t flagset)
{
    return (bitset & flagset) == flagset;
}


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
        // Compile error pointing here (like 'int' is not a class or struct...)
        // means you have used invalid data type for operator[].
        typename AnyOther::type wrong_usage_of_operator_index = AnyOther::type;
        return inarray[size_t(ix)];
    }

    template<class AnyOther>
    FieldType& operator[](AnyOther ix)
    {
        // Compile error pointing here (like 'int' is not a class or struct...)
        // means you have used invalid data type for operator[].
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
        throw std::runtime_error(hvu::fmtcat(OFMT_RAWSTR("Index "), i, OFMT_RAWSTR(" out of range")));
    }

private:
    size_t      m_size;
    T* const    m_entries;
};

// HeapSet: The container implementing the heap tree algorithm.
// See docs/dev/utilities.md.

// NOTE: ALL logging instructions are commented-out here.
// They were used for debugging and can be also restored,
// but this header file should not include logging, hence
// this isn't implemented.
template <class NodeType, class Access = NodeType>
class HeapSet
{
    std::vector<NodeType> m_HeapArray;

public:

    // Convenience functions:

    // Return the key at given position
    typename Access::key_type keyat(size_t position) const
    {
        return Access::key(m_HeapArray[position]);
    }

    // Retuirn the value to compare as "no element"
    static NodeType none()
    {
        return Access::none();
    }

    // Provide the "npos" value to define a position value for
    // a node that is not in the heap.
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

    NodeType find_next(typename Access::key_type limit) const
    {
        // This function should find the first node that is next in order
        // towards the key value of 'limit'.

        // This is done by recursive search through the tree. The search
        // goes deeper, when found an element that is still earlier than
        // limit. When found elements in the path of both siblings, the
        // earlier of these two is returned. There could be none found,
        // and in this case none() is returned.

        if (m_HeapArray.empty())
            return Access::none();

        // Check the very first candidate; if it's already later, you
        // can return it. Otherwise check the children.

        if (!Access::order(keyat(0), limit))
            return m_HeapArray[0];

        if (left(0) >= m_HeapArray.size())
        {
            // There's no left, so there's no right either.
            return Access::none();
        }

        // We have left, but not necessarily right.
        size_t left_candidate = find_next_candidate(left(0), limit);

        size_t right_candidate = 0;
        if (right(0) < m_HeapArray.size())
            right_candidate = find_next_candidate(right(0), limit);

        if (right_candidate == 0)
        {
            // Only left can be taken into account, so return
            // whatever was found
            if (left_candidate == 0)
                return Access::none();
            return m_HeapArray[left_candidate];
        }

        if (left_candidate == 0 || Access::order(keyat(right_candidate), keyat(left_candidate)))
            return m_HeapArray[right_candidate];

        return m_HeapArray[left_candidate];
    }

private:

    // This function, per given node, should find the element that is next in
    // order towards 'limit', or return 0 if not found (0 can be used here as
    // a trap value because the first 3 items are checked on a fast path).
    size_t find_next_candidate(size_t position, typename Access::key_type limit) const
    {
        // It should be guaranteed before the call that position is still
        // within the range of existing elements.

        // Ok, so first you check the element at position. If this element
        // is already the next after limit, return it.
        if (!Access::order(keyat(position), limit))
            return position;

        // Otherwise check the children and if both are next to it, select the
        // earlier one in order.

        // If both children are prior to limit, call this function for both
        // children and select tne next one.

        size_t left_pos = left(position), right_pos = right(position);

        // Directional 3-way value:
        // -1 : no element here
        // 0 : the element is earlier, so follow down
        // 1 : the element is later, so it's a candidate
        int left_check = -1, right_check = -1;
        if (left_pos < m_HeapArray.size())
        {
            // Exists, so add 0/1 that define the order condition
            left_check = Access::order(limit, keyat(left_pos));
        }

        if (right_pos < m_HeapArray.size())
        {
            right_check = Access::order(limit, keyat(right_pos));
        }

        // Ok, now start from the left one, then take the right one.
        // If left doesn't exist, right wouldn't exist, too.
        if (left_check == -1)
            return 0; // no later found, so return none.

        // --- "ELIMINATE ZERO" phase
        // This does it first for the left_check, but then right_check.
        // For both, if they are 0, it is now turned into either 1 or -1.

        if (left_check == 0)
        {
            size_t deep_left = find_next_candidate(left_pos, limit);
            if (deep_left == 0)
                left_check = -1;
            else
            {
                left_check = 1;
                left_pos = deep_left;
            }
        }

        if (right_check == 0)
        {
            size_t deep_right = find_next_candidate(right_pos, limit);
            if (deep_right == 0) // not found anything
                right_check = -1; // pretend this element doesn't exist
            else
            {
                right_check = 1;
                right_pos = deep_right;
            }
        }

        // SINCE THIS LINE ON:
        // Both left_check and right_check can be either 1 or -1.

        // But potentially can have only left == -1.

        if (left_check == -1)
        {
            if (right_check == -1)
                return 0;

            // Otherwise we have left: -1 , right : 1
            return right_pos;
        }

        // [[assert(left_check == 1)]]
        // right_check can be 1 or -1

        if (right_check == 1) // Meaning: "BOTH", select the best one.
        {
            // Return right only if it's better.
            if (Access::order(keyat(left_pos), keyat(right_pos)))
                return left_pos;
            return right_pos;
        }

        // Otherwise right_check is -1, so left is the only one.
        // (this branch is execited if left_check == 1).
        return left_pos;
    }

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

    // Returns the minimum key (key at root) from min heap
    // This function is UNCHECKED. Call it only if you are
    // certain that the heap contains at least one element.
    NodeType top_raw()
    {
        return m_HeapArray[0];
    }

    NodeType top()
    {
        if (m_HeapArray.empty())
            return Access::none();
        return top_raw();
    }

    // Convenience wrapper to insert the node at the new key.
    // You can still assign the key first yourself and then request to insert it,
    // but this serves better as map-like insert.
    size_t insert(const typename Access::key_type& key, NodeType node)
    {
        Access::key(node) = key;
        return insert(node);
    }

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
        // Assume the node is in the heap; make sure about the position first.
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
    template <class Stream, class Value1, class Value2>
    Stream& operator<<(Stream& s, const std::pair<Value1, Value2>& v)
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


// We need to use UniquePtr, in the form of C++03 it will be a #define.
// Naturally will be used std::move() so that it can later painlessly
// switch to C++11.
template <class T>
using UniquePtr = std::unique_ptr<T>;

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
using __gnu_cxx::hash_map;

#endif

template<typename Map, typename Key>
inline std::pair<typename Map::mapped_type&, bool> map_tryinsert(Map& mp, const Key& k)
{
    typedef typename Map::mapped_type Value;
    size_t sizeb4 = mp.size();
    Value& ref = mp[k];

    return std::pair<Value&, bool>(ref, mp.size() > sizeb4);
}

template <class Container> inline
std::string Printable(const Container& in)
{
    using namespace srt_pair_op;
    typedef typename Container::value_type Value;

    std::ostringstream os;
    os << "[ ";
    typedef typename Container::const_iterator it_t;
    for (it_t i = in.begin(); i != in.end(); ++i)
        os << Value(*i) << " ";
    os << "]";
    return os.str();
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
struct pair_proxy
{
    Type1& v1;
    Type2& v2;

    pair_proxy(Type1& t1, Type2& t2): v1(t1), v2(t2) {}

    pair_proxy& operator=(const std::pair<Type1, Type2>& in)
    {
        v1 = in.first;
        v2 = in.second;
        return *this;
    }
};

template <class Type1, class Type2>
inline pair_proxy<Type1, Type2> Tie(Type1& var1, Type2& var2)
{
    return pair_proxy<Type1, Type2>(var1, var2);
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
        // NOTE: No poor-man's replacement can be done for C++03 because it's
        // not possible to fake calling a function without calling it and no
        // other operation can be done without extensive transformations on
        // the Signature type, still in C++03 possible only on functions up to
        // 2 arguments (callbacks in SRT usually have more).
        static_assert(std::is_function<Signature>::value, "CallbackHolder is for functions only!");
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

// Utilities used in window.cpp. See docs/dev/utilities.md for description.

inline PassFilter<int> GetPeakRange(const int* window, int* replica, size_t size)
{
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

inline std::string FormatBinaryString(const uint8_t* bytes, size_t size)
{
    using namespace hvu;

    if ( size == 0 )
        return "";

    ofmtbufstream os;
    os.setup(fmtc().fillzero().uhex());

    for (size_t i = 0; i < size; ++i)
    {
        os << fmtx<int>(bytes[i], fmtc().width(2));
    }
    return os.str();
}

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
    return hvu::fmts(sum, hvu::fmtc().fillzero().width(8).uhex());
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
// See docs/dev/utilities.md for details.
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
