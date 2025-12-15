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

// Circular buffer base
template <class RandomAccessContainer>
struct CiBuffer
{
    typedef RandomAccessContainer entries_t;
    typedef typename RandomAccessContainer::value_type value_type;

    entries_t m_entries;
    CPos m_iStartPos;        // the head position for I/O (inclusive)

    // ATOMIC: sometimes this value is checked for buffer emptiness
    sync::atomic<COff> m_iMaxPosOff;       // the furthest data position

    CiBuffer(size_t size):
        m_entries(size),
        m_iStartPos(0),
        m_iMaxPosOff(0)
    {}

    enum LoopStatus { BREAK, CONTINUE };

    typedef LoopStatus entry_fn(value_type& e);

    // walkEntries: loop over the range of entries from startoff to endoff.
    // @param startoff The first position to operate
    // @param endoff The past-the-end of the last position
    // @param fn Function to call matching the signature of @a entry_fn
    // @return endoff, if all iterations passed, or earlier offset, if interrupted
    //
    // This function walks over the elements for the given offset range and
    // executes the function. The function is free to modify the element and
    // it should return CONTINUE, if after this iteration the loop should pass
    // to the next element, or BREAK, if it should stop after this iteration.
    // Elements are passed in the order of appearance, the implementation splits
    // the range in two, if the end of container happens to be in the middle of
    // the used range.
    template<class Callable>
    COff walkEntries(COff startoff, COff endoff, Callable fn)
    {
        SRT_ASSERT(startoff <= endoff && endoff <= COff(hsize()));

        if (startoff == endoff)
            return CONTINUE;

        // Use manual counting because endoff defines past-the-end,
        // so the position shall be allowed to be equal to hsize(),
        // which isn't possible with incPos().

        CPos startpos, endpos;

        int start_avail = hsize() - m_iStartPos;
        bool two_loops = false;
        if (startoff > start_avail) // startoff is already off-range
        {
            int offshift = endoff - startoff;
            startpos = m_iStartPos + startoff - hsize();
            // so end pos cannot be in the next section
            endpos = startpos + offshift;
            // Example:
            // capacity=16  startpos=10
            // startoff=7 endoff=10
            //
            // begin = 10+7 = 17 - 16 = 1; end = 20-16 = 4
            // One loop: [1] - [3]
        }
        else if (endoff < start_avail) // endoff fits, and so does startoff
        {
            startpos = m_iStartPos + startoff;
            endpos = m_iStartPos + endoff;
            // Example:
            // capacity=16  startpos=10
            // startoff=0 endoff=5
            //
            // begin = 10; end = 10+5 = 15
            // One loop: [10] - [14]
        }
        else
        {
            // We have a split-region.
            startpos = m_iStartPos + startoff;
            endpos = m_iStartPos + endoff - hsize();
            two_loops = true;
            // Example:
            // capacity=16  startpos=10
            // startoff=5 endoff=10
            //
            // begin = 10+4 = 14; end = 10+10 = 20 - 16 = 4
            // First loop: [14] - [15]
            // Second loop: [0] - [3]
        }

        if (!two_loops)
        {
            for (CPos i = startpos; i < endpos; ++i)
            {
                value_type& e = m_entries[i];
                LoopStatus st = fn(e);
                if (st == BREAK)
                    return (i - startpos) + startoff;
            }
            return endoff;
        }

        for (CPos i = startpos; i < CPos(hsize()); ++i)
        {
            value_type& e = m_entries[i];
            LoopStatus st = fn(e);
            if (st == BREAK)
                return (i - startpos) + startoff;
        }

        for (CPos i = CPos(0); i < endpos; ++i)
        {
            value_type& e = m_entries[i];
            LoopStatus st = fn(e);
            if (st == BREAK)
                return (i + hsize() - startpos) + startoff;
        }

        return endoff;
    }

    size_t hsize() const { return m_entries.size(); }


    CPos incPos(CPos position, COff offset = COff(1)) const
    {
        // THEORETICAL implementation: (pos + inc) % hsize()
        CPos sum = position + offset;
        CPos posmax = hsize();
        if (sum >= posmax)
            return sum - posmax;
        return sum;
    }
    CPos decPos(CPos pos) const
    {
        int diff = pos - 1;
        if (diff >= 0)
        {
            return CPos(diff);
        }
        return CPos(hsize() - 1);
    }
    COff offPos(CPos pos1, CPos pos2) const
    {
        int diff = pos2 - pos1;
        if (diff >= 0)
        {
            return COff(diff);
        }
        return COff(hsize() + diff);
    }

    static COff decOff(COff val, int shift)
    {
        int ival = val - shift;
        if (ival < 0)
            return COff(0);
        return COff(ival);
    }

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
    size_t capacity() const
    {
        return hsize() - 1;
    }

    struct ClearGapEntries
    {
        LoopStatus operator()(value_type& v) const
        {
            v = value_type();
            return CONTINUE;
        }
    };

    CPos accessPos(COff offset)
    {
        if (offset >= m_iMaxPosOff)
        {
            walkEntries(m_iMaxPosOff, offset, ClearGapEntries());
            m_iMaxPosOff = offset + COff(1);
        }

        return incPos(m_iStartPos, offset);
    }

    value_type& access(COff offset)
    {
        return m_entries[accessPos(offset)];
    }

    void drop(COff offset)
    {
        if (offset >= m_iMaxPosOff)
        {
            walkEntries(0, m_iMaxPosOff, ClearGapEntries());

            // Clear all
            m_iStartPos = 0;
            m_iMaxPosOff = 0;
            return;
        }

        walkEntries(0, offset, ClearGapEntries());
        m_iStartPos = incPos(m_iStartPos, offset);
        m_iMaxPosOff = m_iMaxPosOff - offset;
    }

};

struct ReceiverBufferBase
{
    enum EntryStatus
    {
        EntryState_Empty = 0,   //< No CUnit record.
        EntryState_Avail,   //< Entry is available for reading.
        EntryState_Read,    //< Entry has already been read (out of order).
        EntryState_Drop     //< Entry has been dropped.
    };

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
};


//
//   Circular receiver buffer.
//   The detailed description is provided in docs/dev/containers.md

class CRcvBuffer: public ReceiverBufferBase, private CiBuffer< FixedArray<ReceiverBufferBase::Entry> >
{
    typedef CiBuffer< FixedArray<ReceiverBufferBase::Entry> > BufferBase;
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;

public:
    CRcvBuffer(int initSeqNo, size_t size, CUnitQueue* unitqueue, bool bMessageAPI);

    ~CRcvBuffer();

    // Publish some of the methods; everything else stays private.
    using BufferBase::empty;
    using BufferBase::full;
    using BufferBase::size;
    using BufferBase::capacity;

public:

    void debugShowState(const char* source);

    struct InsertInfo
    {
        enum Result { INSERTED = 0, REDUNDANT = -1, BELATED = -2, DISCREPANCY = -3 } result;

        // Below fields are valid only if result == INSERTED. Otherwise they have trap repro.

        SeqNo first_seq; // sequence of the first available readable packet
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

    time_point updatePosInfo(const CUnit* unit, const COff prev_max_off, const COff offset);
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
    void setStartSeqNo(int32_t seqno) { m_iStartSeqNo.set(seqno); }

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

    /// @brief Get the sequence number of the first packet that can't be read
    /// (either because it is missing, or because it is a part of a bigger message
    /// that is not fully available yet).
    int32_t getFirstNonreadSeqNo() const;

    /// Get information on packets available to be read.
    /// @returns a pair of sequence numbers (first available; first unavailable).
    /// 
    /// @note CSeqNo::seqoff(first, second) is 0 if nothing to read.
    std::pair<int, int> getAvailablePacketsRange() const;

    int32_t getFirstLossSeq(int32_t fromseq, int32_t* opt_end = NULL);

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

    bool isInUsedRange(CPos iFirstNonreadPos)
    {
        if (iFirstNonreadPos == m_iStartPos)
            return true;

        // DECODE the iFirstNonreadPos
        int diff = iFirstNonreadPos - m_iStartPos;
        if (diff < 0)
            diff += hsize();

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

    CUnitQueue*  m_pUnitQueue; // the shared unit queue

    // ATOMIC because getStartSeqNo() may be called from other thread
    // than CUDT's receiver worker thread. Even if it's not a problem
    // if this value is a bit outdated, it must be read solid.
    SeqNoT< sync::atomic<int32_t> > m_iStartSeqNo;
    COff m_iEndOff;          // past-the-end of the contiguous region since m_iStartOff
    COff m_iDropOff;         // points past m_iEndOff to the first deliverable after a gap; value 0 if no first gap
    CPos m_iFirstNonreadPos; // First position that can't be read (<= m_iLastAckPos)
    int m_iNotch;           // the starting read point of the first unit

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
    sync::atomic<unsigned> m_uAvgPayloadSz;    // Average payload size for dropped bytes estimation
};

} // namespace srt

#endif // INC_SRT_BUFFER_RCV_H
