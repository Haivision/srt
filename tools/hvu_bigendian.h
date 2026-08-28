#ifndef INC_HVU_BIGENDIAN_H
#define INC_HVU_BIGENDIAN_H

#include <cstdint>
#include <cstring>

namespace hvu
{

template <size_t LEN>
struct BECodec
{
    static void parse(const uint8_t* buffer, uint64_t& out)
    {
        out = (out << 8) | *buffer;
        BECodec<LEN-1>::parse(buffer+1, (out));
    }

    // This is more universal, but it's clumsy to use as an
    // API so it's for internal purposes for ParseBE_Set.
    template <class Integer>
    static void parse_type(const uint8_t* buffer, Integer& out)
    {
        out = (out << 8) | *buffer;
        BECodec<LEN-1>::parse_type(buffer+1, (out));
    }

    template <class Integer>
    static void format(Integer value, void* buffer)
    {
        uint8_t* real_buffer = (uint8_t*)buffer;
        BECodec<LEN-1>::format(value >> 8, real_buffer);
        real_buffer[LEN-1] = value & 0xFF;
    }
};

template <>
struct BECodec<0>
{
    static void parse(const uint8_t* /*buffer*/, uint64_t& /*out*/)
    {
        // Do nothing to close the tail.
    }

    template <class Integer>
    static void parse_type(const uint8_t* , Integer& )
    {
        // Do nothing to close the tail.
    }

    template <class Integer>
    static void format(Integer /*value*/, void* /*buffer*/)
    {
        // Do nothing to close the tail.
    }
};

// Compile-time Big Endian parsing.
// The declared LEN will be used from the buffer.

template<size_t LEN>
inline uint64_t ParseBE(const void* buffer)
{
    const uint8_t* real_buffer = (const uint8_t*)buffer;
    uint64_t ret = 0;
    BECodec<LEN>::parse(real_buffer, (ret));
    return ret;
}

// Simpler version that takes the type of the output integer
// as a good deal for the size.
// Recommended for signed integers, as it doesn't align the
// type to uint64_t so that it can be returned.
template<class Integer>
inline void ParseBE_Set(Integer& w_out, const void* buffer)
{
    const uint8_t* real_buffer = (const uint8_t*)buffer;
    static const size_t LEN = sizeof (Integer);
    w_out = Integer(); // Zero the value first
    BECodec<LEN>::parse_type(real_buffer, (w_out));
}

// Compile-time Big Endian formatter.
// The declared LEN of the buffer will be written to.
// Note that you can use any integer and any target type.
template <size_t N, class Integer>
inline void FormatBE(Integer value, void* buffer)
{
    BECodec<N>::template format<Integer>(value, buffer);
}


// Runtime versions
inline uint64_t ParseBE(const uint8_t* buffer, size_t len)
{
    uint64_t out = 0;
    for (size_t i = 0; i < len; ++i)
    {
        out = (out << 8) | buffer[i];
    }
    return out;
}

// This function stores the 64-bit integer value into the given buffer
// in the Big Endian order. As per 64-bit, it uses the maximum of 8
// bytes of the buffer (if a bigger buffer is supplied, the parts over
// 8 bytes are never filled). The supplied buffer is allowed to be less than
// 8 bytes, but it must be big enough to accomodate the value or otherwise
// the value 0 is returned as error.
//
// With the right alignment, the whole 8-byte result is copied to the
// supplied buffer, with skipped the leftmost 0 bytes if the buffer is
// smaller. The number of filled bytes is the size of the buffer or 8
// if the buffer size was greater. The returned size is the number of
// significant bytes, not the number of filled buffer bytes.
//
// With left alignment, only since the first nonzero byte the value is
// copied to the supplied buffer. The returned value is the number of
// written bytes into the buffer (the remaining space of the buffer is
// untouched).
//
// Not exactly a good idea to have a boolean argument, but seems well to have
// LEFT : false, RIGHT : true.
inline size_t FormatBE(uint64_t value, uint8_t* pw_buffer, size_t bufsize, bool align_right = true)
{
    uint8_t buffer[8];
    buffer[0] = uint8_t(value >> 56);
    buffer[1] = uint8_t(value >> 48);
    buffer[2] = uint8_t(value >> 40);
    buffer[3] = uint8_t(value >> 32);
    buffer[4] = uint8_t(value >> 24);
    buffer[5] = uint8_t(value >> 16);
    buffer[6] = uint8_t(value >> 8);
    buffer[7] = uint8_t(value >> 0);

    size_t size = 8;
    while (buffer[8 - size] == 0)
    {
        --size;

        // If size reached 1, there still can be 0 at the [7]
        // position and pass the next check. If that happens,
        // we just have the zero value that can be encoded on 1 byte.
        if (size == 1)
            break;
    }

    const size_t zpos = 8 - size;

    // Buffer size can be less than 8, but
    // it must be capable of accommodating the
    // resulting value.
    if (size > bufsize)
        return 0;

    // Example:
    // bufsize = 3
    // zpos = 6 (all are zeros except last 2 bytes) [0 0 0 0 0 0 X Y]
    // size = 8 - 6 = 2                                          | |

    if (align_right)
    {
        // Copy the whole buffer, just skip zeros                | |
        // that don't fit in the target buffer

        // Continued example:                                    | |
        // startpos = 3 - 2 = 1
        // memcpy(pw_buffer, buffer + 1, 3); -------->        [0 X Y]

        // 
        size_t startpos;
        if (bufsize < 8)
        {
            // Skip so many initial zeros as needed to
            // only leave the zero-filling for the unused
            // part of the buffer
            startpos = 8 - bufsize;
        }
        else
        {
            startpos = 0;
            bufsize = 8; // Copy maximum 8 bytes
        }

        memcpy(pw_buffer, buffer + startpos, bufsize); //        | |
        return size;
    }

    // In the example:
    // zspace = 3 - 2 = 1                                        | |
    // memcpy(pw_buffer, buffer + 6, 2);  ------------------>   [X Y _]

    memcpy(pw_buffer, buffer + zpos, size);

    return size;
}

}

#endif
