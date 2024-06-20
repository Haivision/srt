/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#ifndef INC_SRT_BUFFER_RCV_H
#define INC_SRT_BUFFER_RCV_H

#include "buffer_tools.h" // AvgBufSize
#include "common.h"
#include "queue.h"
#include "tsbpd_time.h"
#include "utilities.h"

#define USE_WRAPPERS 0
#define USE_OPERATORS 0

namespace srt
{

// DEVELOPMENT TOOL - TO BE MOVED ELSEWHERE (like common.h)

// NOTE: This below series of definitions for CPos and COff
// are here for development support only, but they are not in
// use in the release code - there CPos and COff are aliases to int.
#if USE_WRAPPERS
struct CPos
{
    int value;
#if USE_OPERATORS
    const size_t* psize;
    int isize() const {return *psize;}
#endif

#if USE_OPERATORS
    explicit CPos(const size_t* ps SRT_ATR_UNUSED, int val)
        : value(val)
        , psize(ps)
    {}

#else
    explicit CPos(int val): value(val) {}
#endif

    int val() const { return value; }
    explicit operator int() const {return value;}

    CPos(const CPos& src): value(src.value)
#if USE_OPERATORS
                           , psize(src.psize) 
#endif
    {}
    CPos& operator=(const CPos& src)
    {
#if USE_OPERATORS
        psize = src.psize;
#endif
        value = src.value;
        return *this;
    }

#if USE_OPERATORS
    int cmp(CPos other, CPos start) const
    {
        int pos2 = value;
        int pos1 = other.value;

        const int off1 = pos1 >= start.value ? pos1 - start.value : pos1 + start.isize() - start.value;
        const int off2 = pos2 >= start.value ? pos2 - start.value : pos2 + start.isize() - start.value;

        return off2 - off1;
    }

    CPos& operator--()
    {
        if (value == 0)
            value = isize() - 1;
        else
            --value;
        return *this;
    }

    CPos& operator++()
    {
        ++value;
        if (value == isize())
            value = 0;
        return *this;
    }
#endif

    bool operator == (CPos other) const { return value == other.value; }
    bool operator != (CPos other) const { return value != other.value; }
};

struct COff
{
    int value;
    explicit COff(int v): value(v) {}
    COff& operator=(int v) { value = v; return *this; }

    int val() const { return value; }
    explicit operator int() const {return value;}

    COff& operator--() { --value; return *this; }
    COff& operator++() { ++value; return *this; }

    COff operator--(int) { int v = value; --value; return COff(v); }
    COff operator++(int) { int v = value; ++value; return COff(v); }

    COff operator+(COff other) const { return COff(value + other.value); }
    COff operator-(COff other) const { return COff(value - other.value); }
    COff& operator+=(COff other) { value += other.value; return *this;}
    COff& operator-=(COff other) { value -= other.value; return *this;}

    bool operator == (COff other) const { return value == other.value; }
    bool operator != (COff other) const { return value != other.value; }
    bool operator < (COff other) const { return value < other.value; }
    bool operator > (COff other) const { return value > other.value; }
    bool operator <= (COff other) const { return value <= other.value; }
    bool operator >= (COff other) const { return value >= other.value; }

    // Exceptionally allow modifications of COff by a bare integer
    COff operator+(int other) const { return COff(value + other); }
    COff operator-(int other) const { return COff(value - other); }
    COff& operator+=(int other) { value += other; return *this;}
    COff& operator-=(int other) { value -= other; return *this;}

    bool operator == (int other) const { return value == other; }
    bool operator != (int other) const { return value != other; }
    bool operator < (int other) const { return value < other; }
    bool operator > (int other) const { return value > other; }
    bool operator <= (int other) const { return value <= other; }
    bool operator >= (int other) const { return value >= other; }

    friend bool operator == (int value, COff that) { return value == that.value; }
    friend bool operator != (int value, COff that) { return value != that.value; }
    friend bool operator < (int value, COff that) { return value < that.value; }
    friend bool operator > (int value, COff that) { return value > that.value; }
    friend bool operator <= (int value, COff that) { return value <= that.value; }
    friend bool operator >= (int value, COff that) { return value >= that.value; }

    operator bool() const { return value != 0; }
};

#if USE_OPERATORS

inline CPos operator+(const CPos& pos, COff off)
{
    int val = pos.value + off.value;
    while (val >= pos.isize())
        val -= pos.isize();
    return CPos(pos.psize, val);
}

inline CPos operator-(const CPos& pos, COff off)
{
    int val = pos.value - off.value;
    while (val < 0)
        val += pos.isize();
    return CPos(pos.psize, val);
}

// Should verify that CPos use the same size!
inline COff operator-(CPos later, CPos earlier)
{
    if (later.value < earlier.value)
        return COff(later.value + later.isize() - earlier.value);

    return COff(later.value - earlier.value);
}

inline CSeqNo operator+(CSeqNo seq, COff off)
{
    int32_t val = CSeqNo::incseq(seq.val(), off.val());
    return CSeqNo(val);
}

inline CSeqNo operator-(CSeqNo seq, COff off)
{
    int32_t val = CSeqNo::decseq(seq.val(), off.val());
    return CSeqNo(val);
}


#endif
const CPos CPos_TRAP (-1);

#else
typedef int CPos;
typedef int COff;
const int CPos_TRAP = -1;
#endif

//
//   Circular receiver buffer.
//
//   ICR = Initial Contiguous Region: all cells here contain valid packets
//   SCRAP REGION: Region with possibly filled or empty cells
//      NOTE: in scrap region, the first cell is empty and the last one filled.
//   SPARE REGION: Region without packets
//
//           |      BUSY REGION                      | 
//           |           |                           |           |
//           |    ICR    |  SCRAP REGION             | SPARE REGION...->
//   ......->|           |                           |           |
//           |             /FIRST-GAP                |           |
//   |<------------------- m_szSize ---------------------------->|
//   |       |<------------ m_iMaxPosOff ----------->|           |
//   |       |           |                           |   |       |
//   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
//   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
//   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
//           |           |   |                   |
//           |           |   |                   \__last pkt received
//           |<------------->| m_iDropOff        |
//           |           |                       |
//           |<--------->| m_iEndOff             |
//           |
//           \___ m_iStartPos: first packet position in the buffer
//
//   m_pUnit[i]->status:
//             EntryState_Empty: No packet was ever received here
//             EntryState_Avail: The packet is ready for reading
//             EntryState_Read: The packet is non-order-read
//             EntryState_Drop: The packet was requested to drop
//
//   thread safety:
//    m_iStartPos:      CUDT::m_RecvLock
//    first_unack_pos_:    CUDT::m_AckLock
//    m_iMaxPosOff:        none? (modified on add and ack
//    m_iFirstNonreadPos:
//
//
//    m_iStartPos: the first packet that should be read (might be empty)
//    m_iEndOff: shift to the end of contiguous range. This points always to an empty cell.
//    m_iDropPos: shift a packet available for retrieval after a drop. If 0, no such packet.
//
// Operational rules:
//
//    Initially:
//       m_iStartPos = 0
//       m_iEndOff = 0
//       m_iDropOff = 0
//
// When a packet has arrived, then depending on where it landed:
//
// 1. Position: next to the last read one and newest
//
//     m_iStartPos unchanged.
//     m_iEndOff shifted by 1
//     m_iDropOff = 0
// 
// 2. Position: after a loss, newest.
//
//     m_iStartPos unchanged.
//     m_iEndOff unchanged.
//     m_iDropOff:
//       - set to this packet, if m_iDropOff == 0 or m_iDropOff is past this packet
//       - otherwise unchanged
//
// 3. Position: after a loss, but belated (retransmitted) -- not equal to m_iEndPos
//
//    m_iStartPos unchanged.
//    m_iEndPos unchanged.
//    m_iDropPos:
//       - if m_iDropPos == m_iEndPos, set to this
//       - if m_iDropPos %> this sequence, set to this
//       - otherwise unchanged
//
// 4. Position: after a loss, sealing -- seq equal to position of m_iEndPos
//   
//    m_iStartPos unchanged.
//    m_iEndPos:
//      - since this position, search the first free cell
//      - if reached the end of filled region (m_iMaxPosOff), stay there.
//    m_iDropPos:
//      - start from the value equal to m_iEndPos
//      - walk at maximum to m_iMaxPosOff
//      - find the first existing packet
//    NOTE:
//    If there are no "after gap" packets, then m_iMaxPosOff == m_iEndPos.
//    If there is one existing packet, then one loss, then one packet, it
//    should be that m_iEndPos = m_iStartPos %+ 1, m_iDropPos can reach
//    to m_iStartPos %+ 2 position, and m_iMaxPosOff == m_iStartPos %+ 3.
//
// To wrap up:
//
// Let's say we have the following possibilities in a general scheme:
//
//
//                 [D]   [C]             [B]                   [A] (insertion cases)
//  | (start) --- (end) ===[gap]=== (after-loss) ... (max-pos) |
//
// See the CRcvBuffer::updatePosInfo method for detailed implementation.
//
// WHEN INSERTING A NEW PACKET:
//
// If the incoming sequence maps to newpktpos that is:
//
// * newpktpos <% (start) : discard the packet and exit
// * newpktpos %> (size)  : report discrepancy, discard and exit
// * newpktpos %> (start) and:
//    * EXISTS: discard and exit (NOTE: could be also < (end))
// [A]* seq == m_iMaxPosOff
//       --> INC m_iMaxPosOff
//       * m_iEndPos == previous m_iMaxPosOff
//            * previous m_iMaxPosOff + 1 == m_iMaxPosOff
//                --> m_iEndPos = m_iMaxPosOff
//                --> m_iDropPos = m_iEndPos
//            * otherwise (means the new packet caused a gap)
//                --> m_iEndPos REMAINS UNCHANGED
//                --> m_iDropPos = POSITION(m_iMaxPosOff)
//       COMMENT:
//       If this above condition isn't satisfied, then there are
//       gaps, first at m_iEndPos, and m_iDropPos is at furthest
//       equal to m_iMaxPosOff %- 1. The inserted packet is outside
//       both the contiguous region and the following scratched region,
//       so no updates on m_iEndPos and m_iDropPos are necessary.
//
// NOTE
// SINCE THIS PLACE seq cannot be a sequence of an existing packet,
// which means that earliest newpktpos == m_iEndPos, up to == m_iMaxPosOff -% 2.
//
//    * otherwise (newpktpos <% max-pos):
//    [D]* newpktpos == m_iEndPos:
//             --> (search FIRST GAP and FIRST AFTER-GAP)
//             --> m_iEndPos: increase until reaching m_iMaxPosOff
//             * m_iEndPos <% m_iMaxPosOff:
//                 --> m_iDropPos = first VALID packet since m_iEndPos +% 1
//             * otherwise:
//                 --> m_iDropPos = m_iEndPos
//    [B]* newpktpos %> m_iDropPos
//             --> store, but do not update anything
//    [C]* otherwise (newpktpos %> m_iEndPos && newpktpos <% m_iDropPos)          
//             --> store
//             --> set m_iDropPos = newpktpos
//       COMMENT: 
//       It is guaratneed that between m_iEndPos and m_iDropPos
//       there is only a gap (series of empty cells). So wherever
//       this packet lands, if it's next to m_iEndPos and before m_iDropPos
//       it will be the only packet that violates the gap, hence this
//       can be the only drop pos preceding the previous m_iDropPos.
//
// -- information returned to the caller should contain:
// 1. Whether adding to the buffer was successful.
// 2. Whether the "freshest" retrievable packet has been changed, that is:
//    * in live mode, a newly added packet has earlier delivery time than one before
//    * in stream mode, the newly added packet was at cell[0]
//    * in message mode, if the newly added packet has:
//      * completed the very first message
//      * completed any message further than first that has out-of-order flag
//
// The information about a changed packet is important for the caller in
// live mode in order to notify the TSBPD thread.
//
//
//
// WHEN CHECKING A PACKET
//
// 1. Check the position at m_iStartPos. If there is a packet,
// return info at its position.
//
// 2. If position on m_iStartPos is empty, get the value of m_iDropPos.
//
// NOTE THAT:
//   * if the buffer is empty, m_iDropPos == m_iStartPos and == m_iEndPos;
//     note that m_iDropPos == m_iStartPos suffices to check that
//   * if there is a packet in the buffer, but the first cell is empty,
//     then m_iDropPos points to this packet, while m_iEndPos == m_iStartPos.
//     Check then m_iStartPos == m_iEndPos to recognize it, and if then
//     m_iDropPos isn't equal to them, you can read with dropping.
//   * If cell[0] is valid, there could be only at worst cell[1] empty
//     and cell[2] pointed by m_iDropPos.
//
// 3. In case of time-based checking for live mode, return empty packet info,
// if this packet's time is later than given time.
//
// WHEN EXTRACTING A PACKET
//
// 1. Extraction is only possible if there is a packet at cell[0].
// 2. If there's no packet at cell[0], the application may request to
//    drop up to the given packet, or drop the whole message up to
//    the beginning of the next message.
// 3. In message mode, extraction can only extract a full message, so
//    if there's no full message ready, nothing is extracted.
// 4. When the extraction region is defined, the m_iStartPos is shifted
//    by the number of extracted packets.
// 5. If m_iEndPos <% m_iStartPos (after update), m_iEndPos should be
//    set by searching from m_iStartPos up to m_iMaxPosOff for an empty cell.
// 6. m_iDropPos must be always updated. If m_iEndPos == m_iMaxPosOff,
//    m_iDropPos is set to their value. Otherwise start from m_iEndPos
//    and search a valid packet up to m_iMaxPosOff.
// 7. NOTE: m_iMaxPosOff is a delta, hence it must be set anew after update
//    for m_iStartPos.
//

class CRcvBuffer
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;

public:
    CRcvBuffer(int initSeqNo, size_t size, /*CUnitQueue* unitqueue, */ bool bMessageAPI);

    ~CRcvBuffer();

public:

    void debugShowState(const char* source);

    struct InsertInfo
    {
        enum Result { INSERTED = 0, REDUNDANT = -1, BELATED = -2, DISCREPANCY = -3 } result;

        // Below fields are valid only if result == INSERTED. Otherwise they have trap repro.

        CSeqNo first_seq; // sequence of the first available readable packet
        time_point first_time; // Time of the new, earlier packet that appeared ready, or null-time if this didn't change.
        COff avail_range;

        InsertInfo(Result r, int fp_seq = SRT_SEQNO_NONE, int range = 0,
                time_point fp_time = time_point())
            : result(r), first_seq(fp_seq), first_time(fp_time), avail_range(range)
        {
        }

        InsertInfo()
            : result(REDUNDANT), first_seq(SRT_SEQNO_NONE), avail_range(0)
        {
        }

    };

    /// Inserts the unit with the data packet into the receiver buffer.
    /// The result inform about the situation with the packet attempted
    /// to be inserted and the readability of the buffer.
    ///
    /// @param [PASS] unit The unit that should be placed in the buffer
    ///
    /// @return The InsertInfo structure where:
    ///   * result: the result of insertion, which is:
    ///      * INSERTED: successfully placed in the buffer
    ///      * REDUNDANT: not placed, the packet is already there
    ///      * BELATED: not placed, its sequence is in the past
    ///      * DISCREPANCY: not placed, the sequence is far future or OOTB
    ///   * first_seq: the earliest sequence number now avail for reading
    ///   * avail_range: how many packets are available for reading (1 if unknown)
    ///   * first_time: the play time of the earliest read-available packet
    /// If there is no available packet for reading, first_seq == SRT_SEQNO_NONE.
    ///
    InsertInfo insert(CUnit* unit);

    time_point updatePosInfo(const CUnit* unit, const COff prev_max_off, const COff offset, const bool extended_end);
    void getAvailInfo(InsertInfo& w_if);

    /// Update the values of `m_iEndPos` and `m_iDropPos` in
    /// case when `m_iEndPos` was updated to a position of a
    /// nonempty cell.
    ///
    /// This function should be called after having m_iEndPos
    /// has somehow be set to position of a non-empty cell.
    /// This can happen by two reasons:
    ///
    ///  - the cell has been filled by incoming packet
    ///  - the value has been reset due to shifted m_iStartPos
    ///
    /// This means that you have to search for a new gap and
    /// update the m_iEndPos and m_iDropPos fields, or set them
    /// both to the end of range if there are no loss gaps.
    ///
    void updateGapInfo();

    /// Drop packets in the receiver buffer from the current position up to the seqno (excluding seqno).
    /// @param [in] seqno drop units up to this sequence number
    /// @return number of dropped (missing) and discarded (available) packets as a pair(dropped, discarded).
    std::pair<int, int> dropUpTo(int32_t seqno);

    /// @brief Drop all the packets in the receiver buffer.
    /// The starting position and seqno are shifted right after the last packet in the buffer.
    /// @return the number of dropped packets.
    int dropAll();

    enum DropActionIfExists {
        DROP_EXISTING = 0,
        KEEP_EXISTING = 1
    };

    /// @brief Drop a sequence of packets from the buffer.
    /// If @a msgno is valid, sender has requested to drop the whole message by TTL. In this case it has to also provide a pkt seqno range.
    /// However, if a message has been partially acknowledged and already removed from the SND buffer,
    /// the @a seqnolo might specify some position in the middle of the message, not the very first packet.
    /// If those packets have been acknowledged, they must exist in the receiver buffer unless already read.
    /// In this case the @a msgno should be used to determine starting packets of the message.
    /// Some packets of the message can be missing on the receiver, therefore the actual drop should still be performed by pkt seqno range.
    /// If message number is 0 or SRT_MSGNO_NONE, then use sequence numbers to locate sequence range to drop [seqnolo, seqnohi].
    /// A SOLO message packet can be kept depending on @a actionOnExisting value.
    /// TODO: A message in general can be kept if all of its packets are in the buffer, depending on @a actionOnExisting value.
    /// This is done to avoid dropping existing packet when the sender was asked to re-transmit a packet from an outdated loss report,
    /// which is already not available in the SND buffer.
    /// @param seqnolo sequence number of the first packet in the dropping range.
    /// @param seqnohi sequence number of the last packet in the dropping range.
    /// @param msgno message number to drop (0 if unknown)
    /// @param actionOnExisting Should an exising SOLO packet be dropped from the buffer or preserved?
    /// @return the number of packets actually dropped.
    int dropMessage(int32_t seqnolo, int32_t seqnohi, int32_t msgno, DropActionIfExists actionOnExisting);

    /// Extract the "expected next" packet sequence.
    /// Extract the past-the-end sequence for the first packet
    /// that is expected to arrive next with preserving the packet order.
    /// If the buffer is empty or the very first cell is lacking a packet,
    /// it returns the sequence assigned to the first cell. Otherwise it
    /// returns the sequence representing the first empty cell (the next
    /// cell to the last received packet, if there are no loss-holes).
    /// @param [out] w_seq: returns the sequence (always valid)
    /// @return true if this sequence is followed by any valid packets
    bool getContiguousEnd(int32_t& w_seq) const;

    /// Read the whole message from one or several packets.
    ///
    /// @param [out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @param [out,opt] message control data to be filled
    /// @param [out,opt] pw_seqrange range of sequence numbers for packets belonging to the message
    ///
    /// @return actual number of bytes extracted from the buffer.
    ///          0 if nothing to read.
    ///         -1 on failure.
    int readMessage(char* data, size_t len, SRT_MSGCTRL* msgctrl = NULL, std::pair<int32_t, int32_t>* pw_seqrange = NULL);

    /// Read acknowledged data into a user buffer.
    /// @param [in, out] dst pointer to the target user buffer.
    /// @param [in] len length of user buffer.
    /// @return size of data read. -1 on error.
    int readBuffer(char* dst, int len);

    /// Read acknowledged data directly into file.
    /// @param [in] ofs C++ file stream.
    /// @param [in] len expected length of data to write into the file.
    /// @return size of data read. -1 on error.
    int readBufferToFile(std::fstream& ofs, int len);

public:
    /// Get the starting position of the buffer as a packet sequence number.
    int32_t getStartSeqNo() const { return m_iStartSeqNo.val(); }

    /// Sets the start seqno of the buffer.
    /// Must be used with caution and only when the buffer is empty.
    void setStartSeqNo(int32_t seqno) { m_iStartSeqNo = CSeqNo(seqno); }

    /// Given the sequence number of the first unacknowledged packet
    /// tells the size of the buffer available for packets.
    /// Effective returns capacity of the buffer minus acknowledged packet still kept in it.
    // TODO: Maybe does not need to return minus one slot now to distinguish full and empty buffer.
    size_t getAvailSize(int32_t iFirstUnackSeqNo) const
    {
        // Receiver buffer allows reading unacknowledged packets.
        // Therefore if the first packet in the buffer is ahead of the iFirstUnackSeqNo
        // then it does not have acknowledged packets and its full capacity is available.
        // Otherwise subtract the number of acknowledged but not yet read packets from its capacity.
        const int32_t iRBufSeqNo  = m_iStartSeqNo.val();
        if (CSeqNo::seqcmp(iRBufSeqNo, iFirstUnackSeqNo) >= 0) // iRBufSeqNo >= iFirstUnackSeqNo
        //if (iRBufSeqNo >= CSeqNo(iFirstUnackSeqNo))
        {
            // Full capacity is available.
            return capacity();
        }

        // Note: CSeqNo::seqlen(n, n) returns 1.
        return capacity() - CSeqNo::seqlen(iRBufSeqNo, iFirstUnackSeqNo) + 1;
    }

    /// @brief Checks if the buffer has packets available for reading regardless of the TSBPD.
    /// A message is available for reading only if all of its packets are present in the buffer.
    /// @return true if there are packets available for reading, false otherwise.
    bool hasAvailablePackets() const;

    /// Query how many data has been continuously received (for reading) and available for reading out
    /// regardless of the TSBPD.
    /// TODO: Rename to countAvailablePackets().
    /// @return size of valid (continuous) data for reading.
    int getRcvDataSize() const;

    /// Get the number of packets, bytes and buffer timespan.
    /// Differs from getRcvDataSize() that it counts all packets in the buffer, not only continious.
    int getRcvDataSize(int& bytes, int& timespan) const;

    struct PacketInfo
    {
        int        seqno;
        bool       seq_gap; //< true if there are missing packets in the buffer, preceding current packet
        time_point tsbpd_time;
    };

    /// Get information on the 1st message in queue.
    /// Similar to CRcvBuffer::getRcvFirstMsg
    /// Parameters (of the 1st packet queue, ready to play or not):
    /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay of 1st packet or 0 if
    /// none
    /// @param [out] passack   true if 1st ready packet is not yet acknowledged (allowed to be delivered to the app)
    /// @param [out] skipseqno -1 or sequence number of 1st unacknowledged packet (after one or more missing packets) that is ready to play.
    /// @retval true 1st packet ready to play (tsbpdtime <= now). Not yet acknowledged if passack == true
    /// @retval false IF tsbpdtime = 0: rcv buffer empty; ELSE:
    ///                   IF skipseqno != -1, packet ready to play preceded by missing packets.;
    ///                   IF skipseqno == -1, no missing packet but 1st not ready to play.
    PacketInfo getFirstValidPacketInfo() const;

    PacketInfo getFirstReadablePacketInfo(time_point time_now) const;

    /// Get information on packets available to be read.
    /// @returns a pair of sequence numbers (first available; first unavailable).
    /// 
    /// @note CSeqNo::seqoff(first, second) is 0 if nothing to read.
    std::pair<int, int> getAvailablePacketsRange() const;

    int32_t getFirstLossSeq(int32_t fromseq, int32_t* opt_end = NULL);
    void getUnitSeriesInfo(int32_t fromseq, size_t maxsize, std::vector<SRTSOCKET>& w_sources);

    bool empty() const
    {
        return (m_iMaxPosOff == COff(0));
    }

    /// Returns the currently used number of cells, including
    /// gaps with empty cells, or in other words, the distance
    /// between the initial position and the youngest received packet.
    size_t size() const
    {
        return m_iMaxPosOff;
    }

    // Returns true if the buffer is full. Requires locking.
    bool full() const
    {
        return size() == capacity();
    }

    /// Return buffer capacity.
    /// One slot had to be empty in order to tell the difference between "empty buffer" and "full buffer".
    /// E.g. m_iFirstNonreadPos would again point to m_iStartPos if m_szSize entries are added continiously.
    /// TODO: Old receiver buffer capacity returns the actual size. Check for conflicts.
    size_t capacity() const
    {
        return m_szSize - 1;
    }

    int64_t getDrift() const { return m_tsbpd.drift(); }

    // TODO: make thread safe?
    int debugGetSize() const
    {
        return getRcvDataSize();
    }

    /// Zero time to include all available packets.
    /// TODO: Rename to 'canRead`.
    bool isRcvDataReady(time_point time_now = time_point()) const;

    int  getRcvAvgDataSize(int& bytes, int& timespan);
    void updRcvAvgDataSize(const time_point& now);

    unsigned getRcvAvgPayloadSize() const { return m_uAvgPayloadSz; }

    void getInternalTimeBase(time_point& w_timebase, bool& w_wrp, duration& w_udrift)
    {
        return m_tsbpd.getInternalTimeBase(w_timebase, w_wrp, w_udrift);
    }

public: // Used for testing
    /// Peek unit in position of seqno
    const CUnit* peek(int32_t seqno);

private:
    //*
    CPos incPos(CPos pos, COff inc = COff(1)) const { return CPos((pos + inc) % m_szSize); }
    CPos decPos(CPos pos) const { return (pos - 1) >= 0 ? CPos(pos - 1) : CPos(m_szSize - 1); }
    COff offPos(CPos pos1, CPos pos2) const
    {
        int diff = pos2 - pos1;
        if (diff >= 0)
        {
            return COff(diff);
        }
        return COff(m_szSize + diff);
    }

    COff posToOff(CPos pos) const { return offPos(m_iStartPos, pos); }

    static COff decOff(COff val, int shift)
    {
        int ival = val - shift;
        if (ival < 0)
            return COff(0);
        return COff(ival);
    }

    /// @brief Compares the two positions in the receiver buffer relative to the starting position.
    /// @param pos2 a position in the receiver buffer.
    /// @param pos1 a position in the receiver buffer.
    /// @return a positive value if pos2 is ahead of pos1; a negative value, if pos2 is behind pos1; otherwise returns 0.
    inline COff cmpPos(CPos pos2, CPos pos1) const
    {
        // XXX maybe not the best implementation, but this keeps up to the rule.
        // Maybe use m_iMaxPosOff to ensure a position is not behind the m_iStartPos.

        return posToOff(pos2) - posToOff(pos1);
    }
    // */

    // Check if iFirstNonreadPos is in range [iStartPos, (iStartPos + iMaxPosOff) % iSize].
    // The right edge is included because we expect iFirstNonreadPos to be
    // right after the last valid packet position if all packets are available.
    static bool isInRange(CPos iStartPos, COff iMaxPosOff, size_t iSize, CPos iFirstNonreadPos)
    {
        if (iFirstNonreadPos == iStartPos)
            return true;

        const CPos iLastPos = CPos((iStartPos + iMaxPosOff) % int(iSize));
        const bool isOverrun = iLastPos < iStartPos;

        if (isOverrun)
            return iFirstNonreadPos > iStartPos || iFirstNonreadPos <= iLastPos;

        return iFirstNonreadPos > iStartPos && iFirstNonreadPos <= iLastPos;
    }

    bool isInUsedRange(CPos iFirstNonreadPos)
    {
        if (iFirstNonreadPos == m_iStartPos)
            return true;

        // DECODE the iFirstNonreadPos
        int diff = iFirstNonreadPos - m_iStartPos;
        if (diff < 0)
            diff += m_szSize;

        return diff <= m_iMaxPosOff;
    }

    // NOTE: Assumes that pUnit != NULL
    CPacket& packetAt(CPos pos) { return m_entries[pos].pUnit->m_Packet; }
    const CPacket& packetAt(CPos pos) const { return m_entries[pos].pUnit->m_Packet; }

private:
    void countBytes(int pkts, int bytes);
    void updateNonreadPos();
    void releaseUnitInPos(CPos pos);

    /// @brief Drop a unit from the buffer.
    /// @param pos position in the m_entries of the unit to drop.
    /// @return false if nothing to drop, true if the unit was dropped successfully.
    bool dropUnitInPos(CPos pos);

    /// Release entries following the current buffer position if they were already
    /// read out of order (EntryState_Read) or dropped (EntryState_Drop).
    ///
    /// @return the range for which the start pos has been shifted
    int releaseNextFillerEntries();

    bool hasReadableInorderPkts() const { return (m_iFirstNonreadPos != m_iStartPos); }

    /// Find position of the last packet of the message.
    CPos findLastMessagePkt();

    /// Scan for availability of out of order packets.
    void onInsertNonOrderPacket(CPos insertpos);
    // Check if m_iFirstNonOrderMsgPos is still readable.
    bool checkFirstReadableNonOrder();
    void updateFirstReadableNonOrder();
    CPos scanNonOrderMessageRight(CPos startPos, int msgNo) const;
    CPos scanNonOrderMessageLeft(CPos startPos, int msgNo) const;

    typedef bool copy_to_dst_f(char* data, int len, int dst_offset, void* arg);

    /// Read acknowledged data directly into file.
    /// @param [in] ofs C++ file stream.
    /// @param [in] len expected length of data to write into the file.
    /// @return size of data read.
    int readBufferTo(int len, copy_to_dst_f funcCopyToDst, void* arg);

    /// @brief Estimate timespan of the stored packets (acknowledged and unacknowledged).
    /// @return timespan in milliseconds
    int getTimespan_ms() const;

private:
    // TODO: Call makeUnitTaken upon assignment, and makeUnitFree upon clearing.
    // TODO: CUnitPtr is not in use at the moment, but may be a smart pointer.
    // class CUnitPtr
    // {
    // public:
    //     void operator=(CUnit* pUnit)
    //     {
    //         if (m_pUnit != NULL)
    //         {
    //             // m_pUnitQueue->makeUnitFree(m_entries[i].pUnit);
    //         }
    //         m_pUnit = pUnit;
    //     }
    // private:
    //     CUnit* m_pUnit;
    // };

    enum EntryStatus
    {
        EntryState_Empty,   //< No CUnit record.
        EntryState_Avail,   //< Entry is available for reading.
        EntryState_Read,    //< Entry has already been read (out of order).
        EntryState_Drop     //< Entry has been dropped.
    };
    struct Entry
    {
        Entry()
            : pUnit(NULL)
            , status(EntryState_Empty)
        {}

        CUnit*      pUnit;
        EntryStatus status;
    };

    typedef FixedArray<Entry> entries_t;
    entries_t m_entries;

    const size_t m_szSize;     // size of the array of units (buffer)

    //XXX removed. In this buffer the units may come from various different
    // queues, and the unit has a pointer pointing to the queue from which
    // it comes, and it should be returned to the same queue.
    //CUnitQueue*  m_pUnitQueue; // the shared unit queue

    CSeqNo m_iStartSeqNo;
    CPos m_iStartPos;        // the head position for I/O (inclusive)
    COff m_iEndOff;          // past-the-end of the contiguous region since m_iStartOff
    COff m_iDropOff;         // points past m_iEndOff to the first deliverable after a gap, or == m_iEndOff if no such packet
    CPos m_iFirstNonreadPos; // First position that can't be read (<= m_iLastAckPos)
    COff m_iMaxPosOff;       // the furthest data position
    int m_iNotch;           // index of the first byte to read in the first ready-to-read packet (used in file/stream mode)

    size_t m_numNonOrderPackets;  // The number of stored packets with "inorder" flag set to false

    /// Points to the first packet of a message that has out-of-order flag
    /// and is complete (all packets from first to last are in the buffer).
    /// If there is no such message in the buffer, it contains -1.
    CPos m_iFirstNonOrderMsgPos;
    bool m_bPeerRexmitFlag;         // Needed to read message number correctly
    const bool m_bMessageAPI;       // Operation mode flag: message or stream.

public: // TSBPD public functions
    /// Set TimeStamp-Based Packet Delivery Rx Mode
    /// @param [in] timebase localtime base (uSec) of packet time stamps including buffering delay
    /// @param [in] wrap Is in wrapping period
    /// @param [in] delay agreed TsbPD delay
    ///
    /// @return 0
    void setTsbPdMode(const time_point& timebase, bool wrap, duration delay);

    void setPeerRexmitFlag(bool flag) { m_bPeerRexmitFlag = flag; } 

    void applyGroupTime(const time_point& timebase, bool wrp, uint32_t delay, const duration& udrift);

    void applyGroupDrift(const time_point& timebase, bool wrp, const duration& udrift);

    bool addRcvTsbPdDriftSample(uint32_t usTimestamp, const time_point& tsPktArrival, int usRTTSample);

    time_point getPktTsbPdTime(uint32_t usPktTimestamp) const;

    time_point getTsbPdTimeBase(uint32_t usPktTimestamp) const;
    void       updateTsbPdTimeBase(uint32_t usPktTimestamp);

    bool isTsbPd() const { return m_tsbpd.isEnabled(); }

    /// Form a string of the current buffer fullness state.
    /// number of packets acknowledged, TSBPD readiness, etc.
    std::string strFullnessState(int32_t iFirstUnackSeqNo, const time_point& tsNow) const;

private:
    CTsbpdTime  m_tsbpd;

private: // Statistics
    AvgBufSize m_mavg;

    // TODO: m_BytesCountLock is probably not needed as the buffer has to be protected from simultaneous access.
    mutable sync::Mutex m_BytesCountLock;   // used to protect counters operations
    int         m_iBytesCount;      // Number of payload bytes in the buffer
    int         m_iPktsCount;       // Number of payload bytes in the buffer
    unsigned    m_uAvgPayloadSz;    // Average payload size for dropped bytes estimation
};

} // namespace srt

#endif // INC_SRT_BUFFER_RCV_H
