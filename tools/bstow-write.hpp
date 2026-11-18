#ifndef INC_BSTOW_WRITE_H
#define INC_BSTOW_WRITE_H

#include <ostream>
#include <string>
#include <stdexcept>
#include <cstdint>

#include "hvu_bigendian.h"
#include "bstow-read.hpp"

namespace bstow
{

// Writes a numeric parameter. Values that fit in 24 bits use the short form,
// others use the extended form with sizeof(IntType) bytes of the value.
template<class IntType>
inline void WriteValue(std::ostream& out, int label, IntType value)
{
    int32_t cutval = (value & 0x00FFFFFF);

    if (cutval == value)
    {
        // Usual 24-bit encoding.
        unsigned char data[4];
        data[0] = label & 0x7F;
        data[1] = (value >> 16) & 0xFF;
        data[2] = (value >> 8) & 0xFF;
        data[3] = value & 0xFF;
        out.write((char*)data, 4);
        return;
    }

    // Encode a greater value: [LA][BEL][LEN][GTH][payload-of-that-length]
    unsigned char data[4+8];
    unsigned short labform = unsigned(label) | 0x8000;
    unsigned char* p = data;
    hvu::FormatBE<2>(labform, (p));
    p += 2;
    // We'd ignore zero-skipping for now, just write out a 64-bit value
    hvu::FormatBE<2>(sizeof(IntType), (p));
    p += 2;
    hvu::FormatBE<sizeof(IntType)>(value, (p));

    out.write((char*)data, sizeof data);
}

// Writes a parameter with a byte string as a value (extended form only).
inline void WriteString(std::ostream& out, int label, const std::string& value)
{
    // The length field is 16-bit.
    if (value.empty() || value.size() > 0xFFFF)
        throw std::invalid_argument("BSTOW string parameter must be 1..65535 bytes long");

    unsigned char data[4];
    unsigned short labform = unsigned(label) | 0x8000;
    unsigned char* p = data;
    hvu::FormatBE<2>(labform, (p));
    p += 2;
    hvu::FormatBE<2>(value.size(), (p));

    out.write((char*)data, sizeof data);
    out.write(value.data(), value.size());
}

}

#endif
