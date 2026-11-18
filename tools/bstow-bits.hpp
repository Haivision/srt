#ifndef INC_BSTOW_BITS_H
#define INC_BSTOW_BITS_H

#include <cstddef>
#include <type_traits>

// Local copy of the Bits utility from srtcore/utilities.h, extended with
// the integer type parameter. Bits are numbered from the most significant
// to the least significant, so for int32_t the sign bit is 31.
//
// Bits<6, 1>::unwrap(b) is the same as (b >> 1) & 0x7F
// Bits<6, 1>::wrap(v) is the same as (v << 1) & 0x7E

namespace bstow
{

template <size_t L, size_t R = L, typename IntType = unsigned int>
struct Bits
{
    static_assert(L >= R, "Bits<L, R>: L must not be lower than R");
    static_assert(L < sizeof(IntType) * 8, "Bits<L, R, T>: L exceeds the size of T");

    typedef typename std::make_unsigned<IntType>::type UType;

    static const size_t offset = R;
    static const size_t size = L - R + 1;

    // The shift by 1 first avoids shifting by the full width of the type.
    static const UType mask = static_cast<UType>(((static_cast<UType>(2) << (L - R)) - 1) << R);

    static bool fit(IntType value)
    {
        return (static_cast<UType>(value) & ~(mask >> R)) == 0;
    }

    static IntType wrap(IntType baseval)
    {
        return static_cast<IntType>((static_cast<UType>(baseval) << offset) & mask);
    }

    static IntType unwrap(IntType bitset)
    {
        return static_cast<IntType>((static_cast<UType>(bitset) & mask) >> offset);
    }
};

}

#endif
