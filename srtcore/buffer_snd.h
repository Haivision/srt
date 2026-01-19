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

class CSndBuffer;

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

struct SndPktArray
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration duration;

    // Note: this structure has no constructor, so fields must be updated
    // upon creation. Currently only m_PktQueue.push_back() calls do this.
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

        // Busy flag. This is set by the extractor to mark this cell
        // as must-stay, until it's cleared.
        int m_iBusy;

        // This function must ensure that m_tsNextRexmitTime, if set,
        // is distant by at least `miniv` towards m_tsRexmitTime; if not,
        // m_tsNextRexmitTime will be updated to the minimum acceptable value.
        // Returns:
        // - false: the next rexmit time is in the future
        // - true: the next rexmit time is in the past, or we don't care (if miniv is zero)
        bool updated_rexmit_time_passed(const time_point& now, const duration& miniv)
        {
            // We don't check this; just make sure about that during the call
            // --- [[assert (!is_zero(m_tsNextRexmitTime)]]

            // 1. Fix m_tsNextRexmitTime if it's too early after m_tsRexmitTime.
            // 2. After that, check if m_tsNextRexmitTime is in the past.
            if (miniv != duration() && !sync::is_zero(m_tsRexmitTime))
            {
                const duration rxiv = m_tsNextRexmitTime - m_tsRexmitTime;
                if (rxiv < miniv)
                    m_tsNextRexmitTime = m_tsRexmitTime + miniv;
            }

            return m_tsNextRexmitTime < now;
        }
    };

private:

    /// Spare storage with memory blocks used for a single packet.
    BufferedMessageStorage m_Storage;

    /// Container for the packets; managed internally with data consistency.
    std::deque<Packet> m_PktQueue;

    /// Kept in sync with m_PktQueue.size() to allow calling size() without locking.
    sync::atomic<int> m_iCachedSize;

    /// Distance between the newly stored packet and the end of container.
    /// This counts the number of packets since the push-end that were not
    /// yet sent as unique. Shifted by 1 (that is, decreased) with every packet
    /// extracted by extractUniquePacket(). If 0, there are no new unique packets.
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

    SndPktArray(size_t payload_len, size_t max_packets, size_t reserved):
        m_Storage(payload_len, max_packets),
        m_iCachedSize(0),
        m_iNewQueued(0),
        m_iFirstRexmit(-1),
        m_iLastRexmit(-1),
        m_iLossLengthCache(0)
    {
        m_Storage.reserve(reserved);
    }

    ~SndPktArray();

    // Expose for TESTING PURPOSES
    int unique_size() const { return m_iNewQueued; }
    int first_loss() const { return m_iFirstRexmit; }
    int last_loss() const { return m_iLastRexmit; }

    void force_next_time(int offset, const time_point& newtime)
    {
        // NOTE: offset is not checked here. Use for testing only!
        m_PktQueue[offset].m_tsNextRexmitTime = newtime;
    }


    // GET, means the packet is still in the container, you just get access to it.
    // This call however changes the status of the retrieved packet by removing it
    // from the unique range and moving it to the history range.
    Packet* extract_unique();

    void set_expired(int upindex)
    {
        int remain_unique = m_PktQueue.size() - upindex;

        // if remain_unique > m_iNewQueued, it means that packets up to upindex
        // are already expired.
        m_iNewQueued = std::min(m_iNewQueued, remain_unique);
    }

    Packet& push();
    size_t pop(size_t n = 1);

    void remove_loss(int n);

    bool clear_loss(int index);

    bool insert_loss(int ixlo, int ixhi, const time_point& nowtime = sync::steady_clock::now());

    void update_next_rexmit_time(int ixlo, int ixhi, const time_point& time)
    {
        for (int i = ixlo; i <= ixhi; ++i)
        {
            // Do not override existing time value, only set anew if 0
            if (sync::is_zero(m_PktQueue[i].m_tsNextRexmitTime))
                m_PktQueue[i].m_tsNextRexmitTime = time;
        }
    }

    int next_loss(int current_loss);

    int loss_length() const { return m_iLossLengthCache; }

    int extractFirstLoss(const duration& min_interval = duration());

    size_t size() const
    {
        return m_iCachedSize;
    }
    bool empty() const { return m_iCachedSize == 0; }

    void clear();

    // NOTE: operator[] is unchecked. Use indirectly.
    Packet& operator[](size_t index) { return m_PktQueue[index]; }
    const Packet& operator[](size_t index) const { return m_PktQueue[index]; }

    int setupNode(int first_node_index, int last_node_index, int next_node_index = -1)
    {
        int next_index_shift = next_node_index == -1 ? 0 : next_node_index - first_node_index;
        m_PktQueue[first_node_index].m_iNextLossGroupOffset = next_index_shift;
        m_PktQueue[first_node_index].m_iLossLength = last_node_index - first_node_index + 1;
        return m_PktQueue[first_node_index].m_iLossLength;
    }

    int getEndIndex(int first_index)
    {
        return first_index + m_PktQueue[first_index].m_iLossLength;
    }

    int getLastIndex(int first_index) // will return -1 if not a node.
    {
        int end = getEndIndex(first_index);
        return end == first_index ? -1 : end - 1;
    }

    void linkPreviousNode(int previous_node_index, int next_node_index)
    {
        m_PktQueue[previous_node_index].m_iNextLossGroupOffset = next_node_index - previous_node_index;
    }

    void clearNode(int x)
    {
        m_PktQueue[x].m_iLossLength = 0;
        m_PktQueue[x].m_iNextLossGroupOffset = 0;
    }

    // testing purposes
    void clearAllLoss()
    {
        for (int loss = m_iFirstRexmit, next = loss; loss != -1; loss = next)
        {
            next = next_loss(loss);
            clearNode(loss);
        }
        m_iFirstRexmit = -1;
        m_iLastRexmit = -1;
    }

    // Helper state struct used in showline() only.
    struct PacketShowState
    {
        time_point begin_time;
        int remain_loss_group; // SIZE. 1+ if any loss noted, 0 if no loss.
        int next_loss_begin; // INDEX. -1 if no loss pointed

        PacketShowState(): remain_loss_group(0), next_loss_begin(-1) {}
    };

    void showline(int index, PacketShowState& st, hvu::ofmtbufstream& out) const;

    std::string show_external(int32_t seqno) const;

    // Debug/assert only
    bool validateLossIntegrity(std::string& msg);
};


struct CSndPacket
{
    CPacket pkt; // real contents
    CSndBuffer* srcbuf; // NULL if this object doesn't lock a packet
    int32_t seqno; // seq representing the packet in sndbuf, or SRT_SEQNO_NONE if no packet

    CSndPacket(): srcbuf(NULL), seqno(SRT_SEQNO_NONE) {}

    // This function should be called by the sender buffer AFTER
    // it has updated the busy flag, and still under buffer and ack lock.
    void acquire_busy(int32_t seq, CSndBuffer* buf)
    {
        seqno = seq;
        srcbuf = buf;
    }

    // Release the binding, withdraw the busy flag from the sender
    // buffer cell assigned to seqno, and try to revoke as much cells
    // as possible, up to first busy and the registered last ACK.
    void release();

    ~CSndPacket()
    {
        release();
    }
};

class CSndBuffer
{
    typedef sync::steady_clock::time_point time_point;
    typedef sync::steady_clock::duration   duration;

    friend struct CSndPacket;

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

    CSndBuffer(size_t pktsize,       // size limit in packets (of payload size)
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
    ///
    /// IMPORTANT: all facilities that check the buffer size by getSndBufSize()
    /// must be called in THE SAME THREAD as addBuffer(). And only this thread
    /// should be allowed to add packets to this buffer.
    ///
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

    // Special values that can be returned by extractUniquePacket.
    static const int READ_NONE = 0;
    static const int READ_DROP = -1;

    /// Get access to the packet at the next unique position. The unique position
    /// will be moved after extraction.
    ///
    /// @param [out] packet the packet to read.
    /// @param [out] origintime origin time stamp of the message
    /// @param [in] kflags Odd|Even crypto key flag
    /// @param [out] seqnoinc the number of packets skipped due to TTL, so that seqno should be incremented.
    /// @return Actual length of data read.
    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    int extractUniquePacket(CSndPacket& w_packet, time_point& w_origintime, int kflgs, int& w_seqnoinc);

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

    // THIS IS FOR TESTING PURPOSES ONLY. In the normal code
    // the retransmission extraction is done through extractFirstRexmitPacket,
    // which extracts the packet marked loss in the buffer and fills the packet in one call.
    SRT_TSA_NEEDS_NONLOCKED(m_BufLock)
    int readOldPacket(int32_t seqno, CSndPacket& w_packet, time_point& w_origintime, DropRange& w_drop);

    int extractFirstRexmitPacket(const duration& min_rexmit_interval, int32_t& w_current_seqno, CSndPacket& w_sndpkt,
            sync::steady_clock::time_point& w_tsOrigin, std::vector<CSndBuffer::DropRange>& w_drops);

private:
    SRT_TSA_NEEDS_LOCKED(m_BufLock)
    int readPacketInternal(int offset, CSndPacket& w_packet, time_point& w_origintime, DropRange& w_drop);

public:

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

    int getCurrBufSize(int& bytes, int& timespan) const
    {
        sync::ScopedLock lk (m_BufLock);
        return getBufferStats((bytes), (timespan));
    }

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
    void overrideFirstSeqNo(int32_t seq) { m_iSndLastDataAck = seq; m_iSndUpdateAck = SRT_SEQNO_NONE; }

    // Sender loss list management methods
    void removeLossUpTo(int32_t seqno);
    int insertLoss(int32_t lo, int32_t hi, const sync::steady_clock::time_point& pt = sync::steady_clock::time_point());

    // For testing purposes only. Not used in the code.
    int32_t popLostSeq(DropRange&);

    int getLossLength();

    bool cancelLostSeq(int32_t seq);

    /// @brief Count the number of required packets to store the payload (message).
    /// @param iPldLen the length of the payload to check.
    /// @return the number of required data packets.
    int countNumPacketsRequired(int iPldLen) const;

    std::string show() const;

private:

    void initialize();

    void updAvgBufSize(const time_point& time);

    // SENDER BUFFER FUNCTIONAL FIELDS

    mutable sync::Mutex m_BufLock; // used to synchronize buffer operation

    // Note: as constants, these fields do not need mutex protection
    // also when they are used in calcualtions.
    const int m_iBlockLen;  // maximum length of a block holding packet payload and AUTH tag (excluding packet header).
    const int m_iReservedSize; // Authentication tag size (if GCM is enabled).

    sync::atomic<int32_t> m_iSndLastDataAck; // seqno of the packet in cell [0].
    sync::atomic<int32_t> m_iSndUpdateAck; // seqno up to which the last ACK was received (%>= m_iSndLastDataAck)
    int32_t m_iNextMsgNo; // next message number
    int        m_iBytesCount; // number of payload bytes in queue
    time_point m_tsLastOriginTime;

    AvgBufSize m_mavg;
    CRateEstimator m_rateEstimator;

    void releasePacket(int32_t seqno);

    /// Buffer capacity (maximum size), used intermediately and in initialization only.
    int m_iCapacity;

    SndPktArray m_Packets;

    int getBufferStats(int& bytes, int& timespan) const;



    // deleted copyers
    CSndBuffer(const CSndBuffer&);
    CSndBuffer& operator=(const CSndBuffer&);
};

inline void CSndPacket::release()
{
    if (!srcbuf || seqno == SRT_SEQNO_NONE)
        return;

    srcbuf->releasePacket(seqno);
    seqno = SRT_SEQNO_NONE;
    srcbuf = NULL;
}

} // namespace srt

#endif
