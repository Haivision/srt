#ifndef INC_BSTOW_READ_H
#define INC_BSTOW_READ_H

#include "testmediabase.hpp"

namespace bstow
{


static const char g_header[4] = { char(0xbe), 0x57, 0x08, char(0x88) };

// STRUCTURE:
// HEADER: in the beginning of the file or first 4 bytes of transmission
// PROPERTIES:
// - {A, B, C, D}, where
//   - A & ~0x7F == 0 - label, B:C:D - value
//   - A & 0x80 == 0x80:
//         A:B & 0x7F - label
//         C:D  - length
//         followed by value of this length
//
// LABELS:
//
// - DEF_LENGTH:  = length of the payload
// - DEF_PLAYTIME: = time distance between packets in us
// - DEF_SENDTIME = relative timestamp of this unit
// - DEF_DATA: 0x7F (value: 0) = last property field, followed by data
//
// Single reading reports a UNIT. There are two types of units:
//
// - PUSI unit: first one; defines DEF_PLAYTIME (which is media timestamp)
// - TAIL unit: following;
//      - DEF_PLAYTIME is same as before or not specified,
//      - DEF_SENDTIME defines the relative value since previous one; 0 to guess the previous

const int
      // Length of the current piece, in bytes
      // ASSUMED: the size fits in one SRT packet (up to 1444 bytes)
      DEF_LENGTH = 1,
      // Datastream timestamp. Shall never be 0. OPTIONAL (if continued)
      DEF_PLAYTIME = 2,
      // Packet's timestamp delta (or, how fast this
      // packet should be sent after sending the previos one)
      DEF_SENDTIME = 3,
      // 
      DEF_DATA = 0x7F;

// Expected exactly 4 bytes in the array.
// Returns:
// -1 : invalid specification
// 0: w_val contains the value and nothing more to be extracted
// 1+: w_val == 0 and the actual value is following the array of this size
int Extract(const char* data, int& w_lab, int& w_val);

class PacketReader
{
    struct PIMP;
    PIMP* pimp;
    static PIMP* Factory(const std::string& spec);
    friend class ConsoleReader;
    friend class FileReader;

public:

    PacketReader(const std::string& spec);

    bool Prepare(int64_t basetime);
    MediaPacket Read();
    bool End() const;

    std::string ErrorStr() const;
    std::string type() const;
};

}

#endif
