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

namespace srt
{

//
//   Circular receiver buffer.
//
//   |<------------------- m_szSize ---------------------------->|
//   |       |<------------ m_iMaxPosOff ----------->|           |
//   |       |                                       |           |
//   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
//   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
//   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
//             |           |   |                   |
//             |           |   |                   \__last pkt received
//             |           |   |
//             |           |   \___ m_iDropPos
//             |           |
//             |           \___ m_iEndPos
//             |
//             \___ m_iStartPos: first packet position in the buffer
//
//   m_pUnit[i]->status_: 0: free, 1: good, 2: read, 3: dropped (can be combined with read?)
//
//   thread safety:
//    start_pos_:      CUDT::m_RecvLock
//    first_unack_pos_:    CUDT::m_AckLock
//    max_pos_inc_:        none? (modified on add and ack
//    first_nonread_pos_:
//
//
//    m_iStartPos: the first packet that should be read (might be empty)
//    m_iEndPos: the end of contiguous range. Empty if m_iEndPos == m_iStartPos
//    m_iDropPos: a packet available for retrieval after a drop. If == m_iEndPos, no such packet.
//
// Operational rules:
//
//    Initially:
//       m_iStartPos = 0
//       m_iEndPos = 0
//       m_iDropPos = 0
//
// When a packet has arrived, then depending on where it landed:
//
// 1. Position: next to the last read one and newest
//
//     m_iStartPos unchanged.
//     m_iEndPos shifted by 1
//     m_iDropPos = m_iEndPos
// 
// 2. Position: after a loss, newest.
//
//     m_iStartPos unchanged.
//     m_iEndPos unchanged.
//     m_iDropPos:
//       - if it was == m_iEndPos, set to this
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
    CRcvBuffer(int initSeqNo, size_t size, CUnitQueue* unitqueue, bool bMessageAPI);

    ~CRcvBuffer();

public:

    void debugShowState(const char* source);

    struct InsertInfo
    {
        enum Result { INSERTED = 0, REDUNDANT = -1, BELATED = -2, DISCREPANCY = -3 } result;

        // Below fields are valid only if result == INSERTED. Otherwise they have trap repro.

        int first_seq; // sequence of the first available readable packet
        time_point first_time; // Time of the new, earlier packet that appeared ready, or null-time if this didn't change.
        int avail_range;

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

    /// Insert a unit into the buffer.
    /// Similar to CRcvBuffer::addData(CUnit* unit, int offset)
    ///
    /// @param [in] unit pointer to a data unit containing new packet
    /// @param [in] offset offset from last ACK point.
    ///
    /// @return  0 on success, -1 if packet is already in buffer, -2 if packet is before m_iStartSeqNo.
    /// -3 if a packet is offset is ahead the buffer capacity.
    // TODO: Previously '-2' also meant 'already acknowledged'. Check usage of this value.
    InsertInfo insert(CUnit* unit);
    void updateGapInfo(int prev_max_pos);

    /// Drop packets in the receiver buffer from the current position up to the seqno (excluding seqno).
    /// @param [in] seqno drop units up to this sequence number
    /// @return  number of dropped packets.
    int dropUpTo(int32_t seqno);

    /// @brief Drop all the packets in the receiver buffer.
    /// The starting position and seqno are shifted right after the last packet in the buffer.
    /// @return the number of dropped packets.
    int dropAll();

    /// @brief Drop the whole message from the buffer.
    /// If message number is 0 or SRT_MSGNO_NONE, then use sequence numbers to locate sequence range to drop [seqnolo, seqnohi].
    /// When one packet of the message is in the range of dropping, the whole message is to be dropped.
    /// @param seqnolo sequence number of the first packet in the dropping range.
    /// @param seqnohi sequence number of the last packet in the dropping range.
    /// @param msgno message number to drop (0 if unknown)
    /// @return the number of packets actually dropped.
    int dropMessage(int32_t seqnolo, int32_t seqnohi, int32_t msgno);

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
    /// @param [in,out] data buffer to write the message into.
    /// @param [in] len size of the buffer.
    /// @param [in,out] message control data
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
    int getStartSeqNo() const { return m_iStartSeqNo; }

    /// Sets the start seqno of the buffer.
    /// Must be used with caution and only when the buffer is empty.
    void setStartSeqNo(int seqno) { m_iStartSeqNo = seqno; }

    /// Given the sequence number of the first unacknowledged packet
    /// tells the size of the buffer available for packets.
    /// Effective returns capacity of the buffer minus acknowledged packet still kept in it.
    // TODO: Maybe does not need to return minus one slot now to distinguish full and empty buffer.
    size_t getAvailSize(int iFirstUnackSeqNo) const
    {
        // Receiver buffer allows reading unacknowledged packets.
        // Therefore if the first packet in the buffer is ahead of the iFirstUnackSeqNo
        // then it does not have acknowledged packets and its full capacity is available.
        // Otherwise subtract the number of acknowledged but not yet read packets from its capacity.
        const int iRBufSeqNo  = getStartSeqNo();
        if (CSeqNo::seqcmp(iRBufSeqNo, iFirstUnackSeqNo) >= 0) // iRBufSeqNo >= iFirstUnackSeqNo
        {
            // Full capacity is available.
            return capacity();
        }

        // Note: CSeqNo::seqlen(n, n) returns 1.
        return capacity() - CSeqNo::seqlen(iRBufSeqNo, iFirstUnackSeqNo) + 1;
    }

    /// @brief Checks if the buffer has packets available for reading regardless of the TSBPD.
    /// @return true if there are packets available for reading, false otherwise.
    bool hasAvailablePackets() const;

    /// Query how many data has been continuously received (for reading) and available for reading out
    /// regardless of the TSBPD.
    /// TODO: Rename to countAvailablePackets().
    /// @return size of valid (continous) data for reading.
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
    /// @param [out] passack   true if 1st ready packet is not yet acknowleged (allowed to be delivered to the app)
    /// @param [out] skipseqno -1 or seq number of 1st unacknowledged pkt ready to play preceeded by missing packets.
    /// @retval true 1st packet ready to play (tsbpdtime <= now). Not yet acknowledged if passack == true
    /// @retval false IF tsbpdtime = 0: rcv buffer empty; ELSE:
    ///                   IF skipseqno != -1, packet ready to play preceeded by missing packets.;
    ///                   IF skipseqno == -1, no missing packet but 1st not ready to play.
    PacketInfo getFirstValidPacketInfo() const;

    PacketInfo getFirstReadablePacketInfo(time_point time_now) const;

    /// Get information on packets available to be read.
    /// @returns a pair of sequence numbers (first available; first unavailable).
    /// 
    /// @note CSeqNo::seqoff(first, second) is 0 if nothing to read.
    std::pair<int, int> getAvailablePacketsRange() const;

    int32_t getFirstLossSeq(int32_t fromseq, int32_t* opt_end = NULL);

    bool empty() const
    {
        return (m_iMaxPosOff == 0);
    }

    /// Return buffer capacity.
    /// One slot had to be empty in order to tell the difference between "empty buffer" and "full buffer".
    /// E.g. m_iFirstNonreadPos would again point to m_iStartPos if m_szSize entries are added continiously.
    /// TODO: Old receiver buffer capacity returns the actual size. Check for conflicts.
    size_t capacity() const
    {
        return m_szSize - 1;
    }

    /// Returns the currently used number of cells, including
    /// gaps with empty cells, or in other words, the distance
    /// between the initial position and the youngest received packet.
    size_t size() const
    {
        return m_iMaxPosOff;
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
    inline int incPos(int pos, int inc = 1) const { return (pos + inc) % m_szSize; }
    inline int decPos(int pos) const { return (pos - 1) >= 0 ? (pos - 1) : int(m_szSize - 1); }
    inline int offPos(int pos1, int pos2) const { return (pos2 >= pos1) ? (pos2 - pos1) : int(m_szSize + pos2 - pos1); }
    inline int cmpPos(int pos2, int pos1) const
    {
        // XXX maybe not the best implementation, but this keeps up to the rule
        int off1 = pos1 >= m_iStartPos ? pos1 - m_iStartPos : pos1 + m_szSize - m_iStartPos;
        int off2 = pos2 >= m_iStartPos ? pos2 - m_iStartPos : pos2 + m_szSize - m_iStartPos;

        return off2 - off1;
    }

    // NOTE: Assumes that pUnit != NULL
    CPacket& packetAt(int pos) { return m_entries[pos].pUnit->m_Packet; }
    const CPacket& packetAt(int pos) const { return m_entries[pos].pUnit->m_Packet; }

private:
    void countBytes(int pkts, int bytes);
    void updateNonreadPos();
    void releaseUnitInPos(int pos);

    /// @brief Drop a unit from the buffer.
    /// @param pos position in the m_entries of the unit to drop.
    /// @return false if nothing to drop, true if the unit was dropped successfully.
    bool dropUnitInPos(int pos);

    /// Release entries following the current buffer position if they were already
    /// read out of order (EntryState_Read) or dropped (EntryState_Drop).
    void releaseNextFillerEntries();

    bool hasReadableInorderPkts() const { return (m_iFirstNonreadPos != m_iStartPos); }

    /// Find position of the last packet of the message.
    int findLastMessagePkt();

    /// Scan for availability of out of order packets.
    void onInsertNotInOrderPacket(int insertpos);
    // Check if m_iFirstRandomMsgPos is still readable.
    bool checkFirstReadableRandom();
    void updateFirstReadableRandom();
    int  scanNotInOrderMessageRight(int startPos, int msgNo) const;
    int  scanNotInOrderMessageLeft(int startPos, int msgNo) const;

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

    //static Entry emptyEntry() { return Entry { NULL, EntryState_Empty }; }

    typedef FixedArray<Entry> entries_t;
    entries_t m_entries;

    const size_t m_szSize;     // size of the array of units (buffer)
    CUnitQueue*  m_pUnitQueue; // the shared unit queue

    int m_iStartSeqNo;
    int m_iStartPos;        // the head position for I/O (inclusive)
    int m_iEndPos;          // past-the-end of the contiguous region since m_iStartPos
    int m_iDropPos;         // points past m_iEndPos to the first deliverable after a gap, or == m_iEndPos if no such packet
    int m_iFirstNonreadPos; // First position that can't be read (<= m_iLastAckPos)
    int m_iMaxPosOff;       // the furthest data position
    int m_iNotch;           // index of the first byte to read in the first ready-to-read packet (used in file/stream mode)

    size_t m_numRandomPackets;  // The number of stored packets with "inorder" flag set to false

    /// Points to the first packet of a message that has out-of-order flag
    /// and is complete (all packets from first to last are in the buffer).
    /// If there is no such message in the buffer, it contains -1.
    int m_iFirstRandomMsgPos;
    bool m_bPeerRexmitFlag;         // Needed to read message number correctly
    const bool m_bMessageAPI;       // Operation mode flag: message or stream.

public: // TSBPD public functions
    /// Set TimeStamp-Based Packet Delivery Rx Mode
    /// @param [in] timebase localtime base (uSec) of packet time stamps including buffering delay
    /// @param [in] wrap Is in wrapping period
    /// @param [in] delay aggreed TsbPD delay
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
    std::string strFullnessState(int iFirstUnackSeqNo, const time_point& tsNow) const;

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
