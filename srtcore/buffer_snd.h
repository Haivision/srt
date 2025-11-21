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
Copyright (c) 2001 - 2009, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 05/05/2009
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_BUFFER_SND_H
#define INC_SRT_BUFFER_SND_H

#include <deque>

#include "srt.h"
#include "packet.h"
#include "buffer_tools.h"
#include "list.h"

#define SRT_SNDBUF_NEW 1

// The notation used for "circular numbers" in comments:
// The "cicrular numbers" are numbers that when increased up to the
// maximum become zero, and similarly, when the zero value is decreased,
// it turns into the maximum value minus one. This wrapping works the
// same for adding and subtracting. Circular numbers cannot be multiplied.

// Operations done on these numbers are marked with additional % character:
// a %> b : a is later than b
// a ++% (++%a) : shift a by 1 forward
// a +% b : shift a by b
// a == b : equality is same as for just numbers

namespace srt {

struct CSndBlock
{
    typedef sync::steady_clock::time_point time_point;
    char* m_pcData;  // pointer to the data block
    int   m_iLength; // payload length of the block (excluding auth tag).

    int32_t    m_iMsgNoBitset; // message number and special bit flags
    int32_t    m_iSeqNo;       // sequence number for scheduling
    time_point m_tsOriginTime; // block origin time (either provided from above or equals the time a message was submitted for sending.
    time_point m_tsRexmitTime; // packet retransmission time
    int        m_iTTL; // time to live (milliseconds)

    int32_t getMsgSeq()
    {
        // NOTE: this extracts message ID with regard to REXMIT flag.
        // This is valid only for message ID that IS GENERATED in this instance,
        // not provided by the peer. This can be otherwise sent to the peer - it doesn't matter
        // for the peer that it uses LESS bits to represent the message.
        return m_iMsgNoBitset & MSGNO_SEQ::mask;
    }
};

#if SRT_SNDBUF_NEW

struct PacketContainer
{
    typedef sync::steady_clock::time_point time_point;

    struct Packet: CSndBlock
    {
        // Defines the time of the next retransmission.
        // If zero-time, this packet is not to be retransmitted.
        // If future-time, this packet should be skipped when looking for the
        // packets for retransmission, but the time remains unchanged. This
        // will be set to zero-time right after picking up for retransmission.
        time_point m_tsNextRexmitTime;

        // Rexmit system: linked list inside a container.
        //
        // m_iFirstRexmit and m_iLastRexmit keep the index of the first and
        // last retransmission request record. If there are no such records,
        // both should be -1. The last is to speed up place search for inserting
        // newly incoming loss reports.
        //
        // The packet cell pointed by m_iFirstRexmit contains information:
        // - m_iLossLength: how many consecutive packets belong to this group
        // - m_iNextLossGroupOffset: distance between this cell and the next group,
        //   or 0 if this is the last consecutive group.
        //
        // These data are meaningful only for packets that are first in the consecutive
        // group of retransmission schedule. In all other packets both should be 0.

        int m_iLossLength;
        int m_iNextLossGroupOffset;
    };

private:

    /// Spare storage with memory blocks used for a single packet.
    BufferedMessageStorage m_Storage;

    /// Container for the packets; managed internally with data consistency.
    std::deque<Packet> m_Container;

    /// Kept in sync with m_Container.size() to allow calling size() without locking.
    sync::atomic<int> m_iCachedSize;

    /// Distance between the newly stored packet and the end of container.
    /// This counts the number of packets since the push-end that were not
    /// yet sent as unique. Shifted by 1 (that is, decreased) with every packet
    /// extracted by readData(). If 0, there are no new unique packets.
    int m_iNewQueued;

    /// This field keeps the index of the first packet that has an active
    /// retransmission request. -1 if there are no retransmission requests.
    int m_iFirstRexmit;

    /// This keeps the index of the packet in the retransmission request
    /// list that was last inserted. This shortcuts searching for the
    /// existing retransmission records in a normal situation, when the
    /// incoming insertion request follows the last of the records. If
    /// this isn't the case, searching starts from the first record.
    /// -1 if there is no retransmission request.
    int m_iLastRexmit;

    /// Cached loss length. This is rarely required, but algorithms
    /// for NAK report use it to determine the period.
    sync::atomic<int> m_iLossLengthCache;

    // Retransmission request list structure:
    //
    // Packets that are requested retransmission are set m_tsNextRexmitTime
    // to the value of the time that must be in the past to be retransmitted.
    //
    // Any insertion also updates the following:
    // - m_iFirstRexmit is set to the index of the first packet, or unchanged
    //   if the inserted sequence pair was not the very first
    // - m_iLastRexmit is set to the first packet of the group, if this was
    //   the very last insertion (if the current inserion was past the previous
    //   last one)
    // - CPacket::m_iLossLength is the number of consecutive packets since
    //   this packet that belong to the retransmission-requested. Note that
    //   also only this packet contains a nonzero m_tsNextRexmitTime field.
    // - CPacket::m_iNextLossGroupOffset is set to 0 if this was inserted as
    //   the last one, or to the offset between this packet and the nearest
    //   packet beginning the next retransmission group
    //
    // Revocation of the packets updates the fields:
    // - If the series of packets are split in half, the first packet that
    //   survives the revocation is updated: the m_iLossLength is set to
    //   the new size of the group, m_iFirstRexmit is set to 0.
    // - If the whole series are revoked, only the m_iFirstRexmit field
    //   is updated to the new beginning.
    // - If all packets with retransmission requests are effectively removed,
    //   both m_iFirstRexmit and m_iLastRexmit fields are set to -1.
    // - No matter if any groups were removed or not, m_iFirstRexmit and
    //   m_iLastRexmit fields are being updated by decreasing by the number
    //   of revoked packets, if they are not set the new value.
    //
    // Expiration of a packet (per TTL, for example) causes m_tsNextRexmitTime
    // to be reset to zero, but no other action is undertaken. The packet is
    // still in the retransmission request record, just won't be retransmitted.
    //
    // Popping a loss does the following:
    // - The first packet group, pointed by m_iFirstRexmit, is checked and removed
    // - Removal means that the next packet is taken as the first one:
    //    - if m_iLossLength == 1, take the packet distant by m_iNextLossGroupOffset
    //    - if m_iLossLength > 1, take the packet distant by 1, set it the
    //      values of this packet's m_iLossLength and m_iNextLossGroupOffset
    //      decreased by 1
    //    - the m_iFirstRexmit index is updated to point to this new packet
    // - If the packet at this position has m_tsNextRexmitTime zero, this
    //   is not reported as retransmission-eligible, but still removed,
    //   that is, after removal the whole procedure starts over
    // - If the search with removed expired retransmission packets reaches
    //   a packet with m_iNextLossGroupOffset == 0, finally "no rexmit request"
    //   state is reported, same as when m_iFirstRexmit == -1.
    // - If a removal resulted in removing the last record, whether a valid
    //   retransmission request or not, m_iFirstRexmit and m_iLastRexmit
    //   are set to -1.

public:

    PacketContainer(size_t payload_len, size_t max_packets):
        m_Storage(payload_len, max_packets),
        m_iCachedSize(0),
        m_iNewQueued(0),
        m_iFirstRexmit(-1),
        m_iLastRexmit(-1),
        m_iLossLengthCache(0)
    {
    }

    int unique_size() const { return m_iNewQueued; }

    // GET, means the packet is still in the container, you just get access to it.
    // This call however changes the status of the retrieved packet by removing it
    // from the unique range and moving it to the history range.
    Packet* get_unique();

    void set_expired(int upindex)
    {
        int remain_unique = m_Container.size() - upindex;

        // if remain_unique > m_iNewQueued, it means that packets up to upindex
        // are already expired.
        m_iNewQueued = std::min(m_iNewQueued, remain_unique);
    }

    Packet& push()
    {
        m_Container.push_back(Packet());
        m_iCachedSize = m_Container.size();

        Packet& that = m_Container.back();

        // Allocate the packet payload space
        that.m_iLength = m_Storage.blocksize;
        that.m_pcData = m_Storage.get();

        // pushing is always treated as adding a new unique packet
        ++m_iNewQueued;

        // Return as is - without initialized fields.
        return that;
    }

    size_t pop(size_t n = 1);

    void remove_loss(int n);

    bool clear_loss(int index);

    //void remove_loss_seq(int32_t seqhi);
    bool insert_loss(int ixlo, int ixhi, const time_point&);

    void set_rexmit_time(int ixlo, int ixhi, const time_point& time)
    {
        for (int i = ixlo; i <= ixhi; ++i)
        {
            // Do not override existing time value, only set anew if 0
            if (is_zero(m_Container[i].m_tsNextRexmitTime))
                m_Container[i].m_tsNextRexmitTime = time;
        }
    }

    int next_loss(int current_loss)
    {
        if (current_loss == -1)
            return -1;

        Packet& p = m_Container[current_loss];
        if (p.m_iNextLossGroupOffset == 0)
            return -1; // The last loss

        return current_loss + p.m_iNextLossGroupOffset;
    }

    int loss_length() const { return m_iLossLengthCache; }

    int extractFirstLoss();

    size_t size() const
    {
        return m_iCachedSize;
    }
    bool empty() const { return m_iCachedSize == 0; }

    void clear();

    // NOTE: operator[] is unchecked. Use indirectly.
    Packet& operator[](size_t index) { return m_Container[index]; }
    const Packet& operator[](size_t index) const { return m_Container[index]; }

    struct PacketShowState
    {
        time_point begin_time;
        int remain_loss_group; // SIZE. 1+ if any loss noted, 0 if no loss.
        int next_loss_begin; // INDEX. -1 if no loss pointed

        PacketShowState(): remain_loss_group(0), next_loss_begin(-1) {}
    };

    void showline(int index, PacketShowState& st, hvu::ofmtbufstream& out) const;
};

#else
#endif

class CSndBuffer
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;

public:
    // XXX There's currently no way to access the socket ID set for
    // whatever the buffer is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

    // We have the following split for a single packet:
    //
    // [ ----------------------------- MSS ---------------------------------------------]
    // [HEADER(IP-dependent)][ ................... PAYLOAD .................. ][reserved]

    CSndBuffer(size_t bytesize,      // size limit in bytes (will be split into packets)
               size_t slicesize,     // size of the single memory chunk for payload buffers
               size_t mss,           // value of the MSS (default: 1500, take from settings)
               size_t headersize,    // size of the packet header (IP version dependent)
               size_t reservedsize,  // Size reserved in the payload, but not for the transferred data
               int flow_window_size  // required for loss list init
            );
    ~CSndBuffer();

public:
    /// Insert a user buffer into the sending list.
    /// For @a w_mctrl the following fields are used:
    /// INPUT:
    /// - msgttl: timeout for retransmitting the message, if lost
    /// - inorder: request to deliver the message in order of sending
    /// - srctime: local time as a base for packet's timestamp (0 if unused)
    /// - pktseq: sequence number to be stamped on the packet (-1 if unused)
    /// - msgno: message number to be stamped on the packet (-1 if unused)
    /// OUTPUT:
    /// - srctime: local time stamped on the packet (same as input, if input wasn't 0)
    /// - pktseq: sequence number to be stamped on the next packet
    /// - msgno: message number stamped on the packet
    /// @param [in] data pointer to the user data block.
    /// @param [in] len size of the block.
    /// @param [inout] w_mctrl Message control data
    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    void addBuffer(const char* data, int len, SRT_MSGCTRL& w_mctrl);

    /// Read a block of data from file and insert it into the sending list.
    /// @param [in] ifs input file stream.
    /// @param [in] len size of the block.
    /// @return actual size of data added from the file.
    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    int addBufferFromFile(std::fstream& ifs, int len);

    // Special values that can be returned by readData.
    static const int READ_NONE = 0;
    static const int READ_DROP = -1;

    /// Find data position to pack a DATA packet from the furthest reading point.
    /// @param [out] packet the packet to read.
    /// @param [out] origintime origin time stamp of the message
    /// @param [in] kflags Odd|Even crypto key flag
    /// @param [out] seqnoinc the number of packets skipped due to TTL, so that seqno should be incremented.
    /// @return Actual length of data read.
    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    int readData(CPacket& w_packet, time_point& w_origintime, int kflgs, int& w_seqnoinc);

    /// Peek an information on the next original data packet to send.
    /// @return origin time stamp of the next packet; epoch start time otherwise.
    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    time_point peekNextOriginal() const;

    struct DropRange
    {
        static const size_t BEGIN = 0, END = 1;
        int32_t seqno[2];
        int32_t msgno;
    };
    /// Find data position to pack a DATA packet for a retransmission.
    /// IMPORTANT: @a packet is [in,out] because it is expected to get set
    /// the sequence number of the packet expected to be sent next. The sender
    /// buffer normally doesn't handle sequence numbers and the consistency
    /// between the sequence number of a packet already sent and kept in the
    /// buffer is achieved by having the sequence number recorded in the
    /// CUDT::m_iSndLastDataAck field that should represent the oldest packet
    /// still in the buffer.
    /// @param [in] offset offset from the last ACK point (backward sequence number difference)
    /// @param [in,out] w_packet storage for the packet, preinitialized with sequence number
    /// @param [out] w_origintime origin time stamp of the message
    /// @param [out] w_drop the drop information in case when dropping is to be done instead
    /// @retval >0 Length of the data read.
    /// @retval READ_NONE No data available or @a offset points out of the buffer occupied space.
    /// @retval READ_DROP The call requested data drop due to TTL exceeded, to be handled first.
    // SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    // int readData(const int offset, CPacket& w_packet, time_point& w_origintime, DropRange& w_drop);

    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    int readOldPacket(int32_t seqno, CPacket& w_packet, time_point& w_origintime, DropRange& w_drop);

    /// Get the time of the last retransmission (if any) of the DATA packet.
    /// @param [in] offset offset from the last ACK point (backward sequence number difference)
    ///
    /// @return Last time of the last retransmission event for the corresponding DATA packet.
    // SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    // time_point getPacketRexmitTime(const int offset);

    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    time_point getRexmitTime(int32_t seqno);

    /// Update the ACK point and may release/unmap/return the user data according to the flag.
    /// @param [in] offset number of packets acknowledged.
    int32_t getMsgNoAtSeq(int32_t seqno);

    bool revoke(int32_t upto_seqno); // upto_seqno = past-the-end!

    /// Read size of data still in the sending list.
    /// @return Current size of the data in the sending list.
    int getCurrBufSize() const;

    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    int dropLateData(int& bytes, int32_t& w_first_msgno, const time_point& too_late_time);
    int dropAll(int& bytes);

    int  getAvgBufSize(int& bytes, int& timespan);

#if SRT_SNDBUF_NEW
    int getCurrBufSize(int& bytes, int& timespan) const
    {
        sync::ScopedLock lk (m_BufLock);
        return getBufferStats((bytes), (timespan));
    }
#else
    int  getCurrBufSize(int& bytes, int& timespan) const;
#endif

    /// Retrieve input bitrate in bytes per second
    int getInputRate() const { return m_rateEstimator.getInputRate(); }

    void enableRateEstimationIf(bool enable) { m_rateEstimator.resetInputRateSmpPeriod(!enable); }

    void saveEstimation(CRateEstimator& w_est)
    {
        w_est.saveFrom(m_rateEstimator);
    }

    void restoreEstimation(const CRateEstimator& r)
    {
        m_rateEstimator.restoreFrom(r);
    }

    /// @brief Get the buffering delay of the oldest message in the buffer.
    /// @return the delay value.
    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    duration getBufferingDelay(const time_point& tnow) const;

    /// Get maximum payload length per packet.
    int getMaxPacketLen() const;

    int32_t firstSeqNo() const { return m_iSndLastDataAck; }

    // Required in group sequence override
    void overrideFirstSeqNo(int32_t seq) { m_iSndLastDataAck = seq; }

    // Sender loss list management methods
    void removeLossUpTo(int32_t seqno);
    int insertLoss(int32_t lo, int32_t hi, const sync::steady_clock::time_point& pt = sync::steady_clock::time_point());
    int32_t popLostSeq(DropRange&);

    int getLossLength();

    bool cancelLostSeq(int32_t seq);

    /// @brief Count the number of required packets to store the payload (message).
    /// @param iPldLen the length of the payload to check.
    /// @return the number of required data packets.
    int countNumPacketsRequired(int iPldLen) const;

    std::string show() const;

    /// Reserves the sequence number that must prevail, even if
    /// it's covered by ACK.
    bool reserveSeqno(int32_t seq);
    bool releaseSeqno(int32_t new_ack_seq);

private:

    void initialize();

    void updAvgBufSize(const time_point& time);

    // SENDER BUFFER FUNCTIONAL FIELDS

    mutable sync::Mutex m_BufLock; // used to synchronize buffer operation

    // Note: as constants, these fields do not need mutex protection
    // also when they are used in calcualtions.
    const int m_iBlockLen;  // maximum length of a block holding packet payload and AUTH tag (excluding packet header).
    const int m_iReservedSize; // Authentication tag size (if GCM is enabled).

    sync::atomic<int32_t> m_iSndLastDataAck;     // The real last ACK that updates the sender buffer and loss list
    int32_t m_iNextMsgNo; // next message number
    int        m_iBytesCount; // number of payload bytes in queue
    time_point m_tsLastOriginTime;

    AvgBufSize m_mavg;
    CRateEstimator m_rateEstimator;

#if SRT_SNDBUF_NEW

    /// Buffer capacity (maximum size), used intermediately and in initialization only.
    int m_iCapacity;

    /// Reserved lowest sequence number that should guarantee being
    /// still in the buffer, or SRT_SEQNO_NONE if no guarantee was requested.
    /// The call to revoke will not remove these packets, even if succeeds this.
    sync::atomic<int32_t> m_iSndReservedSeq;

    PacketContainer m_Packets;

    int getBufferStats(int& bytes, int& timespan) const;

#else
    void increase();

    CSndLossList m_SndLossList;                // Sender loss list

    struct Block: CSndBlock
    {
        Block* m_pNext; // next block
    };

    Block* m_pBlock;
	Block* m_pFirstBlock;
	Block* m_pCurrBlock;
	Block* m_pLastBlock;

    // m_pBlock:         The head pointer
    // m_pFirstBlock:    The first block
    // m_pCurrBlock:	 The current block
    // m_pLastBlock:     The last block (if first == last, buffer is empty)

    struct MemSlice
    {
        char*   m_pcData; // buffer
        int     m_iSize;  // size
        MemSlice* m_pNext;  // next buffer
    } * m_pFirstMemSlice;        // physical buffer

    int m_iSize; // buffer size (number of packets)
    // NOTE: This is atomic AND under lock because the function getCurrBufSize()
    // is returning it WITHOUT locking. Modification, however, must stay under
    // a lock.
    sync::atomic<int> m_iCount; // number of used blocks

#endif


    // deleted copyers
    CSndBuffer(const CSndBuffer&);
    CSndBuffer& operator=(const CSndBuffer&);
};

} // namespace srt

#endif
