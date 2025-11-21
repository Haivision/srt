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
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 03/12/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <cmath>
#include <fstream> // for debug purposes
#include "buffer_snd.h"
#include "packet.h"
#include "core.h" // provides some constants
#include "utilities.h"
#include "logging.h"

namespace srt {

using namespace std;
using namespace srt::logging;
using namespace sync;

CSndBuffer::CSndBuffer(size_t bytesize, size_t slicesize, size_t mss, size_t headersize, size_t reservedsize, int flwsize SRT_ATR_UNUSED) :
    m_BufLock(),
    m_iBlockLen(mss - headersize),
    m_iReservedSize(reservedsize),
    m_iSndLastDataAck(SRT_SEQNO_NONE),
    m_iNextMsgNo(1),
    m_iBytesCount(0),
#if SRT_SNDBUF_NEW
    m_iCapacity(number_slices<int>(bytesize, m_iBlockLen)),
    m_iSndReservedSeq(SRT_SEQNO_NONE),
    // To avoid performance degradation during the transmission,
    // we allocate in advance all required blocks so that they are
    // picked up from the storage when required.
    m_Packets(m_iBlockLen, m_iCapacity)
#else
    m_SndLossList(flwsize * 2),
    m_pBlock(NULL),
    m_pFirstBlock(NULL),
    m_pCurrBlock(NULL),
    m_pLastBlock(NULL),
    m_pFirstMemSlice(NULL),
    m_iCount(0)
#endif
{
#if SRT_SNDBUF_NEW
    (void)slicesize; // fake used, but not used
#else
    // XXX decide what would be better for the implementation - allocate a single
    // slice, allocate all the memory in slices, or just ignore slicesize.
    m_iSize = int(slicesize);
    (void)bytesize;
#endif

    m_rateEstimator.setHeaderSize(headersize);

    initialize();
    setupMutex(m_BufLock, "Buf");
}

#if SRT_SNDBUF_NEW

void CSndBuffer::initialize()
{
    // Here we can decide, how eagerly the memory can be allocated.
}

void CSndBuffer::addBuffer(const char* data, int len, SRT_MSGCTRL& w_mctrl)
{
    int32_t& w_msgno     = w_mctrl.msgno;
    int32_t& w_seqno     = w_mctrl.pktseq;
    int64_t& w_srctime   = w_mctrl.srctime;
    const int& ttl       = w_mctrl.msgttl;
    const int iPktLen    = getMaxPacketLen();
    const int iNumBlocks = number_slices(len, iPktLen);

    ScopedLock bufferguard(m_BufLock);
    if (m_iSndLastDataAck == SRT_SEQNO_NONE)
        m_iSndLastDataAck = w_seqno;

    HLOGC(bslog.Debug,
          log << "addBuffer: needs=" << iNumBlocks << " buffers for " << len << " bytes. Taken="
          << m_Packets.size() << "/" << m_iCapacity);
    // Retrieve current time before locking the mutex to be closer to packet submission event.
    const steady_clock::time_point tnow = steady_clock::now();
    const int32_t inorder = w_mctrl.inorder ? MSGNO_PACKET_INORDER::mask : 0;

    // Calculate origin time (same for all blocks of the message).
    m_tsLastOriginTime = w_srctime ? time_point() + microseconds_from(w_srctime) : tnow;
    // Rewrite back the actual value, even if it stays the same, so that the calling facilities can reuse it.
    // May also be a subject to conversion error, thus the actual value is signalled back.
    w_srctime = count_microseconds(m_tsLastOriginTime.time_since_epoch());

    if (w_msgno == SRT_MSGNO_NONE) // DEFAULT-UNCHANGED msgno supplied
    {
        HLOGC(bslog.Debug, log << "addBuffer: using internally managed msgno=" << m_iNextMsgNo);
        w_msgno = m_iNextMsgNo;
    }
    else
    {
        HLOGC(bslog.Debug, log << "addBuffer: OVERWRITTEN by msgno supplied by caller: msgno=" << w_msgno);
        m_iNextMsgNo = w_msgno;
    }

    for (int i = 0; i < iNumBlocks; ++i) // only 1 normally in live mode
    {
        int pktlen = len - i * iPktLen;
        if (pktlen > iPktLen)
            pktlen = iPktLen;

        // This will never fail; it is believed that if the buffer size reached
        // defined capacity, addBuffer will not be called.
        PacketContainer::Packet& p = m_Packets.push();

        HLOGC(bslog.Debug,
              log << "addBuffer: %" << w_seqno << " #" << w_msgno << " offset=" << (i * iPktLen)
                  << " size=" << pktlen << " TO BUFFER:" << (void*)p.m_pcData);
        memcpy((p.m_pcData), data + i * iPktLen, pktlen);
        p.m_iLength = pktlen;

        p.m_iSeqNo = w_seqno;
        w_seqno     = CSeqNo::incseq(w_seqno);

        p.m_iMsgNoBitset = m_iNextMsgNo | inorder;
        if (i == 0)
            p.m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == iNumBlocks - 1)
            p.m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: if i is neither 0 nor size-1, it results with PB_SUBSEQUENT.
        //       if i == 0 == size-1, it results with PB_SOLO.
        // Packets assigned to one message can be:
        // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST] - 4 packets per message
        // [PB_FIRST] [PB_LAST] - 2 packets per message
        // [PB_SOLO] - 1 packet per message

        p.m_iTTL = ttl;
        p.m_tsRexmitTime = time_point();
        p.m_tsOriginTime = m_tsLastOriginTime;
    }

    m_iBytesCount += len;

    m_rateEstimator.updateInputRate(m_tsLastOriginTime, iNumBlocks, len);
    updAvgBufSize(m_tsLastOriginTime);

    const int nextmsgno = ++MsgNo(m_iNextMsgNo);
    HLOGC(bslog.Debug, log << "CSndBuffer::addBuffer: updating msgno: #" << m_iNextMsgNo << " -> #" << nextmsgno);
    m_iNextMsgNo = nextmsgno;
}

int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
    const int iPktLen    = getMaxPacketLen();
    const int iNumBlocks = number_slices(len, iPktLen);

    ScopedLock bufferguard(m_BufLock);

    HLOGC(bslog.Debug,
          log << "addBufferFromFile: size=" << m_Packets.size() << " reserved=" << m_iCapacity << " needs=" << iPktLen
              << " buffers for " << len << " bytes, msg #" << m_iNextMsgNo);

    int    total = 0;
    for (int i = 0; i < iNumBlocks; ++i)
    {
        if (ifs.bad() || ifs.fail() || ifs.eof())
            break;

        int pktlen = len - i * iPktLen;
        if (pktlen > iPktLen)
            pktlen = iPktLen;

        PacketContainer::Packet& p = m_Packets.push();

        HLOGC(bslog.Debug,
              log << "addBufferFromFile: reading from=" << (i * iPktLen) << " size=" << pktlen
                  << " TO BUFFER:" << (void*)p.m_pcData);
        ifs.read(p.m_pcData, pktlen);
        if ((pktlen = int(ifs.gcount())) <= 0)
            break;

        // currently file transfer is only available in streaming mode, message is always in order, ttl = infinite
        p.m_iMsgNoBitset = m_iNextMsgNo | MSGNO_PACKET_INORDER::mask;
        if (i == 0)
            p.m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == iNumBlocks - 1)
            p.m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: PB_FIRST | PB_LAST == PB_SOLO.
        // none of PB_FIRST & PB_LAST == PB_SUBSEQUENT.

        p.m_iLength = pktlen;
        p.m_iTTL    = SRT_MSGTTL_INF;

        total += pktlen;
    }

    m_iBytesCount += total;

    m_iNextMsgNo++;
    if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
        m_iNextMsgNo = 1;

    return total;
}

PacketContainer::Packet* PacketContainer::get_unique()
{
    // It should be only possible to be 0, but just in case.
    if (m_iNewQueued <= 0)
        return NULL;

    SRT_ASSERT(m_iNewQueued <= int(m_Container.size()));

    // If m_iNewQueued == 1, then only the last item is the unique one,
    // which's index is size()-1.
    size_t index = int(m_Container.size()) - m_iNewQueued;
    --m_iNewQueued; // We checked in advance that it's > 0.
    return &m_Container[index];
}

int CSndBuffer::readData(CPacket& w_packet, steady_clock::time_point& w_srctime, int kflgs, int& w_seqnoinc)
{
    int readlen = 0;
    w_seqnoinc = 0;
    ScopedLock bufferguard(m_BufLock);

    // REPEATABLE BLOCK
    // In the block there will be skipped the TTL-expired messages, if any.
    for (;;)
    {
        PacketContainer::Packet* p = m_Packets.get_unique();
        if (!p)
            return 0;

        if ((p->m_iTTL >= 0) && (count_milliseconds(steady_clock::now() - w_srctime) > p->m_iTTL))
        {
            // Skip this packet due to TTL expiry.
            // Note: the packet is no longer unique, even though it was never sent.
            LOGC(bslog.Warn, log << CONID() << "CSndBuffer: skipping packet %" << p->m_iSeqNo << " #" << p->getMsgSeq()
                    << " with TTL=" << p->m_iTTL);

            // Just in case, but unique packets should have this field always 0.
            p->m_tsNextRexmitTime = time_point();

            readlen = 0;
            ++w_seqnoinc;
            continue;
        }

        // Make the packet REFLECT the data stored in the buffer.
        w_packet.m_pcData = p->m_pcData;
        readlen = p->m_iLength;
        w_packet.setLength(readlen, m_iBlockLen);
        w_packet.set_seqno(p->m_iSeqNo);

        // 1. On submission (addBuffer), the KK flag is set to EK_NOENC (0).
        // 2. The readData() is called to get the original (unique) payload not ever sent yet.
        //    The payload must be encrypted for the first time if the encryption
        //    is enabled (arg kflgs != EK_NOENC). The KK encryption flag of the data packet
        //    header must be set and remembered accordingly (see EncryptionKeySpec).
        // 3. The next time this packet is read (only for retransmission), the payload is already
        //    encrypted, and the proper flag value is already stored.

        // TODO: Alternatively, encryption could happen before the packet is submitted to the buffer
        // (before the addBuffer() call), and corresponding flags could be set accordingly.
        // This may also put an encryption burden on the application thread, rather than the sending thread,
        // which could be more efficient. Note that packet sequence number must be properly set in that case,
        // as it is used as a counter for the AES encryption.
        if (kflgs == -1)
        {
            HLOGC(bslog.Debug, log << CONID() << " CSndBuffer: ERROR: encryption required and not possible. NOT SENDING.");
            readlen = 0;
        }
        else
        {
            p->m_iMsgNoBitset |= MSGNO_ENCKEYSPEC::wrap(kflgs);
        }

        // Reserve this seqno to persist for the time when this packet is used
        // for being sent, until it's released.
        if (m_iSndReservedSeq == SRT_SEQNO_NONE || SeqNo(p->m_iSeqNo) < SeqNo(m_iSndReservedSeq))
        {
            m_iSndReservedSeq = p->m_iSeqNo;
        }

        HLOGC(bslog.Debug, log << CONID() << "CSndBuffer: UNIQUE packet to send: size=" << readlen
                << " #" << w_packet.getMsgSeq()
                << " %" << w_packet.seqno()
                << " !" << BufferStamp(w_packet.m_pcData, w_packet.getLength()));

        break;
    }

    return readlen;
}

CSndBuffer::time_point CSndBuffer::peekNextOriginal() const
{
    ScopedLock bufferguard(m_BufLock);

    // Use unique_size() because we want to access the next unique
    // packet without removing it from the unique range.
    if (m_Packets.unique_size() == 0)
        return time_point();

    size_t ux = int(m_Packets.size()) - m_Packets.unique_size();
    return m_Packets[ux].m_tsOriginTime;
}

int32_t CSndBuffer::getMsgNoAtSeq(const int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);

    if (offset < 0 || offset >= int(m_Packets.size()))
    {
        // Prevent accessing the last "marker" block
        LOGC(bslog.Error,
             log << "CSndBuffer::getMsgNoAtSeq: IPE: for %" << seqno << " offset=" << offset
             << " outside container; max offset=" << m_Packets.size());
        return SRT_MSGNO_CONTROL;
    }

    return m_Packets[offset].getMsgSeq();
}

int CSndBuffer::readOldPacket(int32_t seqno, CPacket& w_packet, steady_clock::time_point& w_srctime, DropRange& w_drop)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);
    if (offset < 0 || offset >= int(m_Packets.size()))
    {
        LOGC(qslog.Error, log << "CSndBuffer::readOldPacket: for %" << seqno << " offset " << offset << " out of buffer (earliest: %"
                << m_iSndLastDataAck << ")!");
        return READ_NONE;
    }

    // Unlike receiver buffer, in the sender buffer packets are always stored
    // one after another and there are no gaps. Checking the valid range of offset
    // suffices to grant existence of a packet.
    PacketContainer::Packet* p = &m_Packets[offset];

#if HVU_ENABLE_HEAVY_LOGGING
    const int32_t first_seq = p->m_iSeqNo;
    int32_t last_seq = p->m_iSeqNo;
#endif

    w_packet.set_seqno(seqno);

    // This is rexmit request, so the packet should have the sequence number
    // already set when it was once sent uniquely.
    SRT_ASSERT(p->m_iSeqNo == w_packet.seqno());

    // Check if the block that is the next candidate to send (m_pCurrBlock pointing) is stale.

    if ((p->m_iTTL >= 0) && (count_milliseconds(steady_clock::now() - p->m_tsOriginTime) > p->m_iTTL))
    {
        int same_msgno = p->getMsgSeq();

        int lastx = offset;

        // This loop may run also 0 times if we have one message per packet.
        // Note that the API theoretically allows scheduling data to the buffer
        // multiple times with the same message number, although you'd have to
        // enforce it in every call, and each case with a different TTL. That's
        // for the responsibility of the user.
        for (int i = offset+1; i < int(m_Packets.size()); ++i)
        {
            if (m_Packets[i].getMsgSeq() != same_msgno)
                break;

            // In the meantime, as it goes, revoke it from the retransmission schedule
            m_Packets[i].m_tsNextRexmitTime = time_point();

            lastx = i;
        }

        HLOGC(qslog.Debug,
              log << "CSndBuffer::readData: due to TTL exceeded, %(" << first_seq << " - " << last_seq << "), "
                  << (1 + lastx - offset) << " packets to drop with #" << w_drop.msgno);

        // Make sure that the packets belonging to the expired message are
        // no longer in the unique range, even if they were before.
        m_Packets.set_expired(lastx);
        w_drop.msgno = p->getMsgSeq();

        w_drop.seqno[DropRange::BEGIN] = w_packet.seqno();
        w_drop.seqno[DropRange::END] = CSeqNo::incseq(w_packet.seqno(), lastx - offset);

        // We let the caller handle it, while we state no packet delivered.
        // NOTE: the expiration of a message doesn't imply recovation from the buffer.
        // Revocation will still happen on ACK.
        return READ_DROP;
    }

    w_packet.m_pcData = p->m_pcData;
    const int readlen = p->m_iLength;
    w_packet.setLength(readlen, m_iBlockLen);

    // We state that the requested seqno refers to a historical (not unique)
    // packet, hence the encryption action has encrypted the data and updated
    // the flags.
    w_packet.set_msgflags(p->m_iMsgNoBitset);
    w_srctime = p->m_tsOriginTime;

    // This function is called when packet retransmission is triggered.
    // Therefore we are setting the rexmit time.
    p->m_tsRexmitTime = steady_clock::now();

    // Reserve this seqno to persist for the time when this packet is used
    // for being sent, until it's released.
    if (m_iSndReservedSeq == SRT_SEQNO_NONE || SeqNo(p->m_iSeqNo) < SeqNo(m_iSndReservedSeq))
    {
        m_iSndReservedSeq = p->m_iSeqNo;
    }

    HLOGC(qslog.Debug,
          log << CONID() << "CSndBuffer: getting packet %" << p->m_iSeqNo << " as per %" << w_packet.seqno()
              << " size=" << readlen << " to send [REXMIT]");

    return readlen;
}

sync::steady_clock::time_point CSndBuffer::getRexmitTime(const int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);
    if (offset < 0 || offset >= int(m_Packets.size()))
        return sync::steady_clock::time_point();

    return m_Packets[offset].m_tsRexmitTime;
}

bool CSndBuffer::reserveSeqno(int32_t seq)
{
    ScopedLock bufferguard(m_BufLock);
    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seq);

    // IF distance between m_iSndLastDataAck and ack is nonempty...
    if (offset < 0 || offset >= int(m_Packets.size()))
        return false;

    // If there exists any previous reservation, do not
    // overwrite it.
    if (m_iSndReservedSeq != SRT_SEQNO_NONE && SeqNo(m_iSndReservedSeq) < SeqNo(seq))
        return true;

    m_iSndReservedSeq = seq;
    return true;
}

bool CSndBuffer::releaseSeqno(int32_t new_ack_seq)
{
    ScopedLock bufferguard(m_BufLock);
    if (m_iSndReservedSeq == SRT_SEQNO_NONE)
    {
        // We state there is no reservation, so m_iSndLastDataAck == new_ack_seq.
        return false;
    }

    // Unreserve, regardless of the value of new_ack_seq. We need that
    // since this moment the buffer can be freely revoked by ACK.
    m_iSndReservedSeq = SRT_SEQNO_NONE;

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, new_ack_seq);
    if (offset <= 0) // new_ack_seq doesn't succeed any packets in the buffer
        return false;

    // Now continue with revoke. Not calling revoke() due to
    // mutex complications.
    m_Packets.pop(offset);

    m_iSndLastDataAck = new_ack_seq;

    updAvgBufSize(steady_clock::now());
    return true;
}

bool CSndBuffer::revoke(int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);

    // If there exists reservation marker, check if this is going to be
    // released. If so, keep all packets up to the reserved position.
    if (m_iSndReservedSeq != SRT_SEQNO_NONE && SeqNo(seqno) > SeqNo(m_iSndReservedSeq))
    {
        seqno = m_iSndReservedSeq;
    }

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);

    // IF distance between m_iSndLastDataAck and ack is nonempty...
    if (offset <= 0)
        return false;

    // NOTE: offset points to the first packet that should remain
    // in the buffer, hence it's already a past-the-end for the revoked.
    // The call is also safe for calling with excessive value of offset.
    m_Packets.pop(offset);

    // We don't check if the sequence is past the last scheduled one;
    // worst case scenario we'll just clear up the whole buffer.
    m_iSndLastDataAck = seqno;

    updAvgBufSize(steady_clock::now());
    return true;
}

// NOTE: 'n' is the index in the m_Container array
// up to which (including) the losses must be cleared off.
// This should only result in making the m_iFirstRexmit
// and m_iLastRexmit field pointing to either -1 or
// valid indexes in the container, but OUTSIDE the range
// from 0 to n.
void PacketContainer::remove_loss(int last_to_clear)
{
    // This is going to remove the loss records since the first one
    // up to the packet designated by the last_to_clear offset (same as pop()).

    // this empty() is just formally - with empty m_iFirstRexmit should be moreover -1
    if (m_iFirstRexmit == -1 || m_Container.empty()) // no loss records
        return; // last is also -1 in this situation

    const int LASTX = int(m_Container.size()) - 1;

    // Handle special case: if last_to_clear is the last index in the container,
    // simply remove everything. Just update all the loss nodes.
    if (last_to_clear >= LASTX)
    {
        for (int loss = m_iFirstRexmit, next; loss != -1; loss = next)
        {
            // use safe-loop rule because the node data will be cleared here.
            next = next_loss(loss);
            PacketContainer::Packet& p = m_Container[loss];
            p.m_iLossLength = 0;
            p.m_iNextLossGroupOffset = 0;
        }
        m_iFirstRexmit = -1;
        m_iLastRexmit= -1;
        m_iLossLengthCache = 0;
        return;
    }

    // The iteration rule here:
    // Make calculations on the indexes with unchanged relative values.
    // That is, just clear the records that point to a less index than last_to_clear.
    // Values of m_iFirstRexmit and m_iLastRexmit still refer to the unchanged
    // indexes in the container, just must be outside the removed region.
    int removed_loss_length = 0;
    int first_to_clear = -1;
    for (;;)
    {
        if (last_to_clear < m_iFirstRexmit)
        {
            // Found at THIS RECORD (possibly having dismissed earlier ones)
            // that it's already in the non-revoked region.

            // Update with the length of every loss record removed in this loop.
            m_iLossLengthCache = m_iLossLengthCache - removed_loss_length;
            // That's it, nothing more to do.
            break;
        }

        if (first_to_clear == -1)
            first_to_clear = m_iFirstRexmit;

        // Ride until you find a split-in-half record,
        // a new record beyond last_to_clear, or no more records.
        PacketContainer::Packet& p = m_Container[m_iFirstRexmit];

        int last_index = m_iFirstRexmit + p.m_iLossLength - 1;
        if (last_to_clear < last_index)
        {
            // split-in-half case. This will be the last on which the operation is done.

            int new_beginning = last_to_clear + 1; // The case when last_to_clear == m_Container.size() - 1 is handled already

            int revoked_length_fragment = new_beginning - m_iFirstRexmit;

            // Now shift the position
            bool is_last = false;
            m_Container[new_beginning].m_iLossLength = p.m_iLossLength - revoked_length_fragment;
            if (p.m_iNextLossGroupOffset)
            {
                int next_index = m_iFirstRexmit + p.m_iNextLossGroupOffset;
                // Replicate the distance at the new index
                m_Container[new_beginning].m_iNextLossGroupOffset = next_index - new_beginning;
            }
            else
            {
                // No next group, this is the last one.
                m_Container[new_beginning].m_iNextLossGroupOffset = 0;
                is_last = true;
            }

            // Cancel the previous first node
            p.m_iLossLength = 0;
            p.m_iNextLossGroupOffset = 0;

            // NOTE: the new values of m_iFirstRexmit and m_iLastRexmit set here
            // are valid indexes AFTER REMOVAL of the revoked elements from m_Container.
            m_iFirstRexmit = new_beginning;
            if (is_last)
                m_iLastRexmit = m_iFirstRexmit;
            // If not last, there is some record next to first which remains last.

            // Removed were all previous completely skipped record before length last_to_clear,
            // plus a fragment of the record that was split in half.
            m_iLossLengthCache = m_iLossLengthCache - removed_loss_length - revoked_length_fragment;

            break;
        }

        // Check if this one was the last record; if so, we have cleared all.
        if (p.m_iNextLossGroupOffset == 0)
        {
            p.m_iLossLength = 0;
            m_iFirstRexmit = -1;
            m_iLastRexmit = -1;
            m_iLossLengthCache = 0;
            break;
        }

        // Remaining case: the whole record is below last_to_clear (so remove it and try next)
        // Remove means that you need to clear this packet from being a hook of
        // a loss record, and move m_iFirstRexmit to the next record.
        removed_loss_length += p.m_iLossLength;
        m_iFirstRexmit += p.m_iNextLossGroupOffset;

        p.m_iLossLength = 0;
        p.m_iNextLossGroupOffset = 0;
    }
}

bool CSndBuffer::cancelLostSeq(int32_t seq)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seq);
    if (offset < 0 || offset >= int(m_Packets.size()))
        return false;

    return m_Packets.clear_loss(offset);
}

bool PacketContainer::clear_loss(int index)
{
    // Just access the record. Now return false only
    // if it turns out that it has never been rexmit-scheduled.
    PacketContainer::Packet& p = m_Container[index];
    if (is_zero(p.m_tsNextRexmitTime))
        return false;

    p.m_tsNextRexmitTime = time_point();
    return true;
}

void PacketContainer::clear()
{
    // pop() will do erase(begin(), end()) in this case,
    // but will also destroy every packet.
    pop(size());
}

// 'n' is the past-the-end index for removal
size_t PacketContainer::pop(size_t n)
{
    if (m_Container.empty() || !n)
        return 0; // The size is also unchanged

    if (n > m_Container.size())
        n = m_Container.size();

    // We consider that this call clears off losses in the container
    // calls from 0 to n (inc).
    remove_loss(n-1); // remove_loss includes given index

    deque<PacketContainer::Packet>::iterator i = m_Container.begin(), upto = i + n;
    for (; i != upto; ++i)
    {
        // Deallocate storage
        m_Storage.put(i->m_pcData);
    }

    m_Container.erase(m_Container.begin(), upto);
    m_iCachedSize = m_Container.size();

    // pop might have removed also packets from the unique range;
    // in that case just shrink it to the existing range.
    m_iNewQueued = std::min<int>(m_iNewQueued, m_Container.size());

    // These are indexes into the m_Container container, so with
    // removed n elements, their position in the container also
    // get shifted by n. 
    if (m_iFirstRexmit != -1)
    {
        // After remove_loss(), these indexes were updated so that they
        // do not (*should* not) refer to any elements earlier than n.
        SRT_ASSERT(m_iFirstRexmit >= int(n));
        SRT_ASSERT(m_iLastRexmit >= m_iFirstRexmit);

        m_iFirstRexmit -= n;
        m_iLastRexmit -= n;
    }

    return n;
}

bool PacketContainer::insert_loss(int offset_lo, int offset_hi, const time_point& next_rexmit_time)
{
    // Can't install loss to an empty container
    if (m_Container.empty())
        return false;

    // Fix the indexes if they are out of bound. Note that they can potentially
    // be very far from the point, but any rollovers should be ignored (there's
    // not much we can do about it). Check only if this really is lo-hi relationship
    // and whether at least a fragment of the range is in the buffer.

    if (offset_lo > offset_hi || offset_hi < 0 || offset_lo >= int(m_Container.size()))
        return false;

    if (offset_lo < 0)
    {
        //int fix = 0 - offset_lo;
        offset_lo = 0;
        //seqlo = CSeqNo::incseq(seqlo, fix);
    }

    // It was checked that size() is at least 1
    if (offset_hi >= int(m_Container.size()))
    {
        //int fix = offset_hi - m_Container.size();
        offset_hi = m_Container.size() - 1;
        //seqhi = CSeqNo::decseq(seqhi, fix);
    }

    int loss_length = offset_hi - offset_lo + 1;

    // Ok, check now where the position is towards the
    // existing records.
    //
    // First: check if there are no records yet.
    if (m_iFirstRexmit == -1)
    {
        // Add just one record and mark in both.
        PacketContainer::Packet& p = m_Container[offset_lo];
        p.m_iNextLossGroupOffset = 0;
        p.m_iLossLength = loss_length;
        m_iFirstRexmit = m_iLastRexmit = offset_lo;

        m_iLossLengthCache = loss_length;
        set_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
        return true;
    }

    // Check relationship with the last one. If past the last one,
    // simply pin in the new one.
    if (offset_lo >= m_iLastRexmit)
    {
        // This means, it's eaither overlapping with m_iLastRexmit,
        // or past the last record. What we know for sure is that it
        // doesn't overlap with any earlier loss records.

        PacketContainer::Packet& butlast = m_Container[m_iLastRexmit];

        // Still, the record can overlap.
        int last_end_ix = m_iLastRexmit + butlast.m_iLossLength; // past-the-end!

        // If the next to insert is exactly next to the last record,
        // only extend the existing last record
        if (last_end_ix >= offset_lo)
        {
            // Overlap. Update the loss length, all else remains untouched.
            // old_length defines the fragment that will be wiped due to being
            // merged with the current one.
            int old_length = butlast.m_iLossLength;
            int new_length = offset_hi - m_iLastRexmit + 1;
            butlast.m_iLossLength = new_length;

            m_iLossLengthCache = m_iLossLengthCache + new_length - old_length;
            set_rexmit_time(m_iLastRexmit, offset_hi, next_rexmit_time);
            return true;
        }

        // No overlaps, just add the new last one.
        PacketContainer::Packet& last = m_Container[offset_lo];

        int butlast_distance = offset_lo - m_iLastRexmit;

        // The length of the last record remains unchanged,
        // just the next-pointer needs to be set.
        butlast.m_iNextLossGroupOffset = butlast_distance;

        int new_length = offset_hi - offset_lo + 1;
        m_iLastRexmit = offset_lo;
        last.m_iNextLossGroupOffset = 0; // this is now the last one
        last.m_iLossLength = new_length;
        m_iLossLengthCache = m_iLossLengthCache + new_length;
        set_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
        return true;
    }

    // Ruled-out case: past-the-last insertion or overlap with the last record.
    // All other cases are:
    // - preceding the first
    // - overlapping or interleaving with existing records, EXCEPT the last one.
    //   (still may overlap with the last record, that is, extend its beginning)

    // --- PacketContainer::Packet& p = m_Container[m_iFirstRexmit];
    vector<int> index_to_clear;

    //  Current form:
    //     [FIRST...END] [FURTHER1...END] [FURTHER2...END]... [LAST...END]
    //   |           |                   \ |
    //   \            \                 [B]
    //   [A]           [C]
    //  Cases:
    //  [A] Very first (preceding the first record and not overlapping with it)
    //  [B] intermixed or overlapped with existing records
    //  Then the mix of two conditions:
    //  a.:
    //    1. Intermixed with existing
    //    2. Overlapping with existing

    // This determines the index of the first record that we will
    // consider as intermixing or overlapping.

    // --- int loss_index_end = m_iFirstRexmit + p.m_iLossLength;
    // --- SRT_ASSERT(loss_index_end > m_iFirstRexmit);

    int lowest_lo = m_iFirstRexmit;
    int last_preceding_index = -1;

    if (offset_lo < m_iFirstRexmit)
    {
        // Detemine and handle case A. This doesn't require any modification
        // of existing records, just inserting one as the first, and the sofar
        // first will be its next.
        if (offset_hi < m_iFirstRexmit)
        {
            PacketContainer::Packet& first = m_Container[offset_lo];
            first.m_iLossLength = loss_length;
            first.m_iNextLossGroupOffset = m_iFirstRexmit - offset_lo;
            m_iFirstRexmit = offset_lo;
            m_iLossLengthCache = m_iLossLengthCache + loss_length;
            set_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
            return true;
        }
        // Otherwise continue with first potentially overlapping
        // or preceding.
    }
    // We use else because if the lowest index precedes the first,
    // there couldn't exist a case that any whole records precede it.
    else
    {
        // As the lower range is already past the begin,
        // skip all records that whole precede the inserted record.
        for (int loss_index = m_iFirstRexmit; loss_index != -1; loss_index = next_loss(loss_index))
        {
            PacketContainer::Packet& ip = m_Container[loss_index];
            int loss_end = loss_index + ip.m_iLossLength;

            if (loss_end >= offset_lo)
            {
                lowest_lo = loss_index;
                break;
            }

            // If this value remains -1, it means that the record to
            // insert overlaps with the very first record. Otherwise
            // there will always be some records preceding and this
            // needs to be linked with the prospective next one.
            last_preceding_index = loss_index;
        }
    }

    //  Now the situation is:
    //  [potentially skipped preceding...] | [LOWEST_LO...END] [FURTHER1...END] [FURTHER2...END]... [LAST...END]
    //
    // Check now if there is a record that directly succeeds the inserted one.
    int next_succeeding = 0; // Default value to mark m_iNextLossGroupOffset, which is "no next record".

    int eclipsed_length = 0; // This is to collect length of all removed records to be re-added with this one.
    for (int loss_index = lowest_lo; loss_index != -1; loss_index = next_loss(loss_index))
    {
        // We search for the first non-overlapping one.
        if (loss_index > offset_hi)
        {
            next_succeeding = loss_index;
            break;
        }

        // All indices in the middle should be cleared
        index_to_clear.push_back(loss_index);
        eclipsed_length += m_Container[loss_index].m_iLossLength;
    }

    // Now its:
    //  [preceding...] | [LOWEST_LO...END] [FURTHER1...END] [FURTHER2...END]...             | [NEXT_SUCCEEDING...END]...
    // or
    //  [preceding...] | [LOWEST_LO...END] [FURTHER1...END] [FURTHER2...END]... [LAST...END]
    //
    // next_succeeding is only the value to set to m_iNextLossGroupOffset.
    // Length is determined by earliest index min(offset_lo, lowest_lo)  and max(offset_hi, last_hi+length-1)


    // Now clear everything in the records "in between", except that catch
    // the farthest index ever seen in this range.
    int furthest_hi = offset_hi;
    for (size_t i = 0; i < index_to_clear.size(); ++i)
    {
        int x = index_to_clear[i];
        int hi = x + m_Container[x].m_iLossLength - 1;
        m_Container[x].m_iLossLength = 0;
        m_Container[x].m_iNextLossGroupOffset = 0;
        furthest_hi = std::max(furthest_hi, hi);
    }

    if (lowest_lo != offset_lo)
    {
        // Clear the packet if it wasn't at the border
        m_Container[lowest_lo].m_iNextLossGroupOffset = 0;
        m_Container[lowest_lo].m_iLossLength = 0;
    }
    else
    {
        lowest_lo = min(lowest_lo, offset_lo);
    }
    loss_length = furthest_hi - lowest_lo + 1;

    m_Container[lowest_lo].m_iLossLength = loss_length;
    m_Container[lowest_lo].m_iNextLossGroupOffset = next_succeeding;

    int new_length = m_iLossLengthCache - eclipsed_length + loss_length;

    if (last_preceding_index == -1)
        m_iFirstRexmit = lowest_lo;
    else
        m_Container[last_preceding_index].m_iNextLossGroupOffset = lowest_lo;
    m_iLossLengthCache = new_length;

    // Set the rexmit time only to the range that was requested to be inserted,
    // even if this is effectively a fragment of a record.
    set_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
    return true;
}

int32_t CSndBuffer::popLostSeq(DropRange& w_drop)
{
    static const DropRange nodrop = { {SRT_SEQNO_NONE, SRT_SEQNO_NONE}, SRT_MSGNO_CONTROL };
    w_drop = nodrop;

    ScopedLock bufferguard(m_BufLock);

    // In this version we don't predict any drop requests;
    // the sequence is taken directly from the sender buffer, so there's
    // physically no possibility to have any sequence lost that is not 
    // among the packets in the buffer.

    // Ok, this is our sequence to report.
    int i = m_Packets.extractFirstLoss();
    if (i == -1)
        return SRT_SEQNO_NONE;

    int32_t seqno = CSeqNo::incseq(m_iSndLastDataAck, i);
    return seqno;
}

int PacketContainer::extractFirstLoss()
{
    // No loss at all
    if (m_iFirstRexmit == -1)
        return -1;

    // Theoretically you can take the cell at m_iFirstRexmit and revoke it,
    // but the record could have been rexmit-cleared, in which case you should
    // skip it, and try on the next one. All records skipped this way must be
    // revoked together with the first found valid loss.

    // However, you also have this time to check against now, so if
    // this time is in the future, the record must stay there; you can
    // still pick up any next cell, but you can't revoke anything,
    // except those preceding the "future rexmit" cell.
    int stop_revoke = -1;

    time_point now = steady_clock::now();

    // Walk over the container to find the valid loss sequence
    for (int loss_begin = m_iFirstRexmit; loss_begin != -1; loss_begin = next_loss(loss_begin))
    {
        int loss_end = loss_begin + m_Container[loss_begin].m_iLossLength;

        for (int i = loss_begin; i != loss_end; ++i)
        {
            PacketContainer::Packet& p = m_Container[i];
            if (!is_zero(p.m_tsNextRexmitTime))
            {
                // Ok, so this cell will be taken, but it might be the future.
                if (p.m_tsNextRexmitTime > now)
                {
                    if (stop_revoke == -1 && i > 0)
                        stop_revoke = i - 1;
                    continue;
                }


                // Clear that packet from being rexmit-eligible.
                p.m_tsNextRexmitTime = time_point();

                if (stop_revoke == -1)
                    remove_loss(i); // Remove all previous loss records, including this one
                else
                    remove_loss(stop_revoke);
                return i;
            }
            // If it was cleared, continue searching.
        }
    }

    return -1;
}

void CSndBuffer::removeLossUpTo(int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);
    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);

    if (offset < 0 || offset >= int(m_Packets.size()))
        return;

    m_Packets.remove_loss(offset);
}

int CSndBuffer::insertLoss(int32_t seqlo, int32_t seqhi, const time_point& pt)
{
    ScopedLock bufferguard(m_BufLock);
    int offset_lo = CSeqNo::seqoff(m_iSndLastDataAck, seqlo);
    int offset_hi = CSeqNo::seqoff(m_iSndLastDataAck, seqhi);

    return m_Packets.insert_loss(offset_lo, offset_hi, is_zero(pt) ? steady_clock::now(): pt);
}

int CSndBuffer::getLossLength()
{
    return m_Packets.loss_length();
}

int CSndBuffer::getCurrBufSize() const
{
    return m_Packets.size();
}

int CSndBuffer::getMaxPacketLen() const
{
    return m_iBlockLen - m_iReservedSize;
}

int CSndBuffer::countNumPacketsRequired(int iPldLen) const
{
    const int iPktLen = getMaxPacketLen();
    return number_slices(iPldLen, iPktLen);
}

int CSndBuffer::getAvgBufSize(int& w_bytes, int& w_tsp)
{
    ScopedLock bufferguard(m_BufLock); /* Consistency of pkts vs. bytes vs. spantime */

    /* update stats in case there was no add/ack activity lately */
    updAvgBufSize(steady_clock::now());

    // Average number of packets and timespan could be small,
    // so rounding is beneficial, while for the number of
    // bytes in the buffer is a higher value, so rounding can be omitted,
    // but probably better to round all three values.
    w_bytes = m_mavg.bytes() + 0.49;
    w_tsp   = m_mavg.timespan_ms() + 0.49;
    return int(m_mavg.pkts() + 0.49);
}

void CSndBuffer::updAvgBufSize(const steady_clock::time_point& now)
{
    if (!m_mavg.isTimeToUpdate(now))
        return;

    int       bytes       = 0;
    int       timespan_ms = 0;
    const int pkts        = getBufferStats((bytes), (timespan_ms));
    m_mavg.update(now, pkts, bytes, timespan_ms);
}

int CSndBuffer::getBufferStats(int& w_bytes, int& w_timespan) const
{
    w_bytes = m_iBytesCount;
    /*
     * Timespan can be less then 1000 us (1 ms) if few packets.
     * Also, if there is only one pkt in buffer, the time difference will be 0.
     * Therefore, always add 1 ms if not empty.
     */
    if (m_Packets.empty())
        w_timespan = 0;
    else
        w_timespan = count_milliseconds(m_tsLastOriginTime - m_Packets[0].m_tsOriginTime) + 1;


    return m_Packets.size();
}

CSndBuffer::duration CSndBuffer::getBufferingDelay(const time_point& tnow) const
{
    ScopedLock lck(m_BufLock);

    if (m_Packets.empty())
        return duration(0);

    return tnow - m_Packets[0].m_tsOriginTime;
}

int CSndBuffer::dropLateData(int& w_bytes, int32_t& w_first_msgno, const steady_clock::time_point& too_late_time)
{
    ScopedLock bufferguard(m_BufLock);

    int dbytes = 0;
    int32_t msgno = 0;
    // Reach out to the position that is less than too_late_time,
    // counting the bytes
    size_t i;
    for (i = 0; i < m_Packets.size() && m_Packets[i].m_tsOriginTime < too_late_time; ++i)
    {
        dbytes += m_Packets[i].m_iLength;
        msgno = m_Packets[i].getMsgSeq();
    }

    // Now delete all these packets from the container
    if (i)
    {
        m_Packets.pop(i);

        const int32_t fakeack = CSeqNo::incseq(m_iSndLastDataAck, i);
        m_iSndLastDataAck = fakeack;
    }

    w_bytes = dbytes; // even if 0

    // We report the increased number towards the last ever seen
    // by the loop, as this last one is the last received. So remained
    // (even if "should remain") is the first after the last removed one.
    w_first_msgno = ++MsgNo(msgno);

    updAvgBufSize(steady_clock::now());

    return i;
}

int CSndBuffer::dropAll(int& w_bytes)
{
    ScopedLock bufferguard(m_BufLock);
    // clear all

    int dpkts = m_Packets.size();
    m_Packets.clear();
    w_bytes = m_iBytesCount;
    m_iBytesCount = 0;

    updAvgBufSize(steady_clock::now());
    return dpkts;
}

CSndBuffer::~CSndBuffer()
{
    releaseMutex(m_BufLock);
}

string CSndBuffer::show() const
{
    using namespace hvu;
    ofmtbufstream out;

    int minw = 2;
    if (m_Packets.size() > 99)
        minw = 3;
    else if (m_Packets.size() > 999)
        minw = 4;

    fmtc findex = fmtc().width(minw).fillzero();

    ScopedLock bufferguard(m_BufLock);

    PacketContainer::PacketShowState st;
    for (size_t i = 0; i < m_Packets.size(); ++i)
    {
        int seqno = CSeqNo::incseq(m_iSndLastDataAck, i);
        out << "[" << fmt(i, findex) << "]%" << seqno << ": ";
        m_Packets.showline(i, (st), (out));
        out.puts();
    }

    return out.str();
}

void PacketContainer::showline(int index, PacketShowState& st, hvu::ofmtbufstream& out) const
{
    const Packet& p = m_Container[index];

    if (is_zero(st.begin_time))
        st.begin_time = steady_clock::now();

    out << p.m_iLength << "!" << BufferStamp(p.m_pcData, p.m_iLength);

    // Check beginning of the new loss group
    if (index == m_iFirstRexmit)
    {
        if (st.remain_loss_group || st.next_loss_begin != -1)
            out << " *** UNEXPECTED rem=" << st.remain_loss_group << " next=" << st.next_loss_begin << " at first=" << m_iFirstRexmit;

        // Configure context object
        st.remain_loss_group = p.m_iLossLength;
        st.next_loss_begin = p.m_iNextLossGroupOffset ? index + p.m_iNextLossGroupOffset : -1;
        if (st.remain_loss_group == 0)
            out << " *** UNEXPECTED index=" << index << " marked next, but length=0!";
    }
    else if (index == st.next_loss_begin)
    {
        if (st.remain_loss_group)
            out << " *** UNEXPECTED rem=" << st.remain_loss_group << " next=" << st.next_loss_begin << " at first=" << m_iFirstRexmit;

        // Configure context object
        st.remain_loss_group = p.m_iLossLength;
        st.next_loss_begin = p.m_iNextLossGroupOffset ? index + p.m_iNextLossGroupOffset : -1;
        if (st.remain_loss_group == 0)
            out << " *** UNEXPECTED index=" << index << " marked next, but length=0!";
    }
    else
    {
        if (p.m_iLossLength || p.m_iNextLossGroupOffset)
            out << " *** UNEXPECTED subseq loss-len=" << p.m_iLossLength << " next=" << p.m_iNextLossGroupOffset;
    }

    if (st.remain_loss_group)
    {
        out << " L.";
        if (is_zero(p.m_tsNextRexmitTime))
            out << "0";
        else
            out << FormatDurationAuto(st.begin_time - p.m_tsNextRexmitTime);

        out << "/" << st.remain_loss_group;

        --st.remain_loss_group;
    }

    int queued_range_begin = size() - m_iNewQueued;
    if (index >= queued_range_begin)
    {
        out << " NEW";
    }
}

#else

void CSndBuffer::addBuffer(const char* data, int len, SRT_MSGCTRL& w_mctrl)
{
    int32_t& w_msgno     = w_mctrl.msgno;
    int32_t& w_seqno     = w_mctrl.pktseq;
    int64_t& w_srctime   = w_mctrl.srctime;
    const int& ttl       = w_mctrl.msgttl;
    const int iPktLen    = getMaxPacketLen();
    const int iNumBlocks = number_slices(len, iPktLen);

    if (m_iSndLastDataAck == SRT_SEQNO_NONE)
        m_iSndLastDataAck = w_seqno;

    HLOGC(bslog.Debug,
          log << "addBuffer: needs=" << iNumBlocks << " buffers for " << len << " bytes. Taken="
          << m_iCount.load() << "/" << m_iSize);
    // Retrieve current time before locking the mutex to be closer to packet submission event.
    const steady_clock::time_point tnow = steady_clock::now();

    ScopedLock bufferguard(m_BufLock);
    // Dynamically increase sender buffer if there is not enough room.
    while (iNumBlocks + m_iCount >= m_iSize)
    {
        HLOGC(bslog.Debug, log << "addBuffer: ... still lacking " << (iNumBlocks + m_iCount - m_iSize) << " buffers...");
        increase();
    }

    const int32_t inorder = w_mctrl.inorder ? MSGNO_PACKET_INORDER::mask : 0;
    HLOGC(bslog.Debug,
          log << CONID() << "addBuffer: adding " << iNumBlocks << " packets (" << len << " bytes) to send, msgno="
              << (w_msgno > 0 ? w_msgno : m_iNextMsgNo) << (inorder ? "" : " NOT") << " in order");

    // Calculate origin time (same for all blocks of the message).
    m_tsLastOriginTime = w_srctime ? time_point() + microseconds_from(w_srctime) : tnow;
    // Rewrite back the actual value, even if it stays the same, so that the calling facilities can reuse it.
    // May also be a subject to conversion error, thus the actual value is signalled back.
    w_srctime = count_microseconds(m_tsLastOriginTime.time_since_epoch());

    // The sequence number passed to this function is the sequence number
    // that the very first packet from the packet series should get here.
    // If there's more than one packet, this function must increase it by itself
    // and then return the accordingly modified sequence number in the reference.

    Block* s = m_pLastBlock;

    if (w_msgno == SRT_MSGNO_NONE) // DEFAULT-UNCHANGED msgno supplied
    {
        HLOGC(bslog.Debug, log << "addBuffer: using internally managed msgno=" << m_iNextMsgNo);
        w_msgno = m_iNextMsgNo;
    }
    else
    {
        HLOGC(bslog.Debug, log << "addBuffer: OVERWRITTEN by msgno supplied by caller: msgno=" << w_msgno);
        m_iNextMsgNo = w_msgno;
    }

    for (int i = 0; i < iNumBlocks; ++i)
    {
        int pktlen = len - i * iPktLen;
        if (pktlen > iPktLen)
            pktlen = iPktLen;

        HLOGC(bslog.Debug,
              log << "addBuffer: %" << w_seqno << " #" << w_msgno << " offset=" << (i * iPktLen)
                  << " size=" << pktlen << " TO BUFFER:" << (void*)s->m_pcData);
        memcpy((s->m_pcData), data + i * iPktLen, pktlen);
        s->m_iLength = pktlen;

        s->m_iSeqNo = w_seqno;
        w_seqno     = CSeqNo::incseq(w_seqno);

        s->m_iMsgNoBitset = m_iNextMsgNo | inorder;
        if (i == 0)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == iNumBlocks - 1)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: if i is neither 0 nor size-1, it resuls with PB_SUBSEQUENT.
        //       if i == 0 == size-1, it results with PB_SOLO.
        // Packets assigned to one message can be:
        // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST] - 4 packets per message
        // [PB_FIRST] [PB_LAST] - 2 packets per message
        // [PB_SOLO] - 1 packet per message

        s->m_iTTL = ttl;
        s->m_tsRexmitTime = time_point();
        s->m_tsOriginTime = m_tsLastOriginTime;
        
        // Should never happen, as the call to increase() should ensure enough buffers.
        SRT_ASSERT(s->m_pNext);
        s = s->m_pNext;
    }
    m_pLastBlock = s;

    m_iCount = m_iCount + iNumBlocks;
    m_iBytesCount += len;

    m_rateEstimator.updateInputRate(m_tsLastOriginTime, iNumBlocks, len);
    updAvgBufSize(m_tsLastOriginTime);

    // MSGNO_SEQ::mask has a form: 00000011111111...
    // At least it's known that it's from some index inside til the end (to bit 0).
    // If this value has been reached in a step of incrementation, it means that the
    // maximum value has been reached. Casting to int32_t to ensure the same sign
    // in comparison, although it's far from reaching the sign bit.

    const int nextmsgno = ++MsgNo(m_iNextMsgNo);
    HLOGC(bslog.Debug, log << "CSndBuffer::addBuffer: updating msgno: #" << m_iNextMsgNo << " -> #" << nextmsgno);
    m_iNextMsgNo = nextmsgno;
}

int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
    const int iPktLen    = getMaxPacketLen();
    const int iNumBlocks = number_slices(len, iPktLen);

    HLOGC(bslog.Debug,
          log << "addBufferFromFile: size=" << m_iCount.load() << " reserved=" << m_iSize << " needs=" << iPktLen
              << " buffers for " << len << " bytes, msg #" << m_iNextMsgNo);

    // dynamically increase sender buffer
    while (iNumBlocks + m_iCount >= m_iSize)
    {
        HLOGC(bslog.Debug,
              log << "addBufferFromFile: ... still lacking " << (iNumBlocks + m_iCount - m_iSize) << " buffers...");
        increase();
    }

    Block* s     = m_pLastBlock;
    int    total = 0;
    for (int i = 0; i < iNumBlocks; ++i)
    {
        if (ifs.bad() || ifs.fail() || ifs.eof())
            break;

        int pktlen = len - i * iPktLen;
        if (pktlen > iPktLen)
            pktlen = iPktLen;

        HLOGC(bslog.Debug,
              log << "addBufferFromFile: reading from=" << (i * iPktLen) << " size=" << pktlen
                  << " TO BUFFER:" << (void*)s->m_pcData);
        ifs.read(s->m_pcData, pktlen);
        if ((pktlen = int(ifs.gcount())) <= 0)
            break;

        // currently file transfer is only available in streaming mode, message is always in order, ttl = infinite
        s->m_iMsgNoBitset = m_iNextMsgNo | MSGNO_PACKET_INORDER::mask;
        if (i == 0)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == iNumBlocks - 1)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: PB_FIRST | PB_LAST == PB_SOLO.
        // none of PB_FIRST & PB_LAST == PB_SUBSEQUENT.

        s->m_iLength = pktlen;
        s->m_iTTL    = SRT_MSGTTL_INF;
        s            = s->m_pNext;

        total += pktlen;
    }
    m_pLastBlock = s;

    enterCS(m_BufLock);
    m_iCount = m_iCount + iNumBlocks;
    m_iBytesCount += total;

    leaveCS(m_BufLock);

    m_iNextMsgNo++;
    if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
        m_iNextMsgNo = 1;

    return total;
}

int CSndBuffer::readData(CPacket& w_packet, steady_clock::time_point& w_srctime, int kflgs, int& w_seqnoinc)
{
    int readlen = 0;
    w_seqnoinc = 0;

    ScopedLock bufferguard(m_BufLock);
    while (m_pCurrBlock != m_pLastBlock)
    {
        // Make the packet REFLECT the data stored in the buffer.
        w_packet.m_pcData = m_pCurrBlock->m_pcData;
        readlen = m_pCurrBlock->m_iLength;
        w_packet.setLength(readlen, m_iBlockLen);
        w_packet.set_seqno(m_pCurrBlock->m_iSeqNo);

        // 1. On submission (addBuffer), the KK flag is set to EK_NOENC (0).
        // 2. The readData() is called to get the original (unique) payload not ever sent yet.
        //    The payload must be encrypted for the first time if the encryption
        //    is enabled (arg kflgs != EK_NOENC). The KK encryption flag of the data packet
        //    header must be set and remembered accordingly (see EncryptionKeySpec).
        // 3. The next time this packet is read (only for retransmission), the payload is already
        //    encrypted, and the proper flag value is already stored.
        
        // TODO: Alternatively, encryption could happen before the packet is submitted to the buffer
        // (before the addBuffer() call), and corresponding flags could be set accordingly.
        // This may also put an encryption burden on the application thread, rather than the sending thread,
        // which could be more efficient. Note that packet sequence number must be properly set in that case,
        // as it is used as a counter for the AES encryption.
        if (kflgs == -1)
        {
            HLOGC(bslog.Debug, log << CONID() << " CSndBuffer: ERROR: encryption required and not possible. NOT SENDING.");
            readlen = 0;
        }
        else
        {
            m_pCurrBlock->m_iMsgNoBitset |= MSGNO_ENCKEYSPEC::wrap(kflgs);
        }

        Block* p = m_pCurrBlock;
        w_packet.set_msgflags(m_pCurrBlock->m_iMsgNoBitset);
        w_srctime = m_pCurrBlock->m_tsOriginTime;
        m_pCurrBlock = m_pCurrBlock->m_pNext;

        if ((p->m_iTTL >= 0) && (count_milliseconds(steady_clock::now() - w_srctime) > p->m_iTTL))
        {
            LOGC(bslog.Warn, log << CONID() << "CSndBuffer: skipping packet %" << p->m_iSeqNo << " #" << p->getMsgSeq() << " with TTL=" << p->m_iTTL);
            // Skip this packet due to TTL expiry.
            readlen = 0;
            ++w_seqnoinc;
            continue;
        }

        HLOGC(bslog.Debug, log << CONID() << "CSndBuffer: picked up packet to send: size=" << readlen
                << " #" << w_packet.getMsgSeq()
                << " %" << w_packet.seqno()
                << " !" << BufferStamp(w_packet.m_pcData, w_packet.getLength()));

        break;
    }

    return readlen;
}

CSndBuffer::time_point CSndBuffer::peekNextOriginal() const
{
    ScopedLock bufferguard(m_BufLock);
    if (m_pCurrBlock == m_pLastBlock)
        return time_point();

    return m_pCurrBlock->m_tsOriginTime;
}

int32_t CSndBuffer::getMsgNoAtSeq(const int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);

    if (offset < 0 || offset >= m_iCount)
    {
        // Prevent accessing the last "marker" block
        LOGC(bslog.Error,
             log << "CSndBuffer::getMsgNoAtSeq: IPE: for %" << seqno << " offset=" << offset << " outside container; max offset=" << m_iCount.load());
        return SRT_MSGNO_CONTROL;
    }

    Block* p = m_pFirstBlock;
    if (p)
    {
        HLOGC(bslog.Debug,
              log << "CSndBuffer::getMsgNoAtSeq: FIRST MSG: size=" << p->m_iLength << " %" << p->m_iSeqNo << " #"
                  << p->getMsgSeq() << " !" << BufferStamp(p->m_pcData, p->m_iLength));
    }

    // XXX Suboptimal procedure to keep the blocks identifiable
    // by sequence number. Consider using some circular buffer.
    int       i;
    Block* ee SRT_ATR_UNUSED = 0;
    for (i = 0; i < offset && p; ++i)
    {
        ee = p;
        p  = p->m_pNext;
    }

    if (!p)
    {
        LOGC(bslog.Error,
             log << "CSndBuffer::getMsgNoAt: IPE: for %" << seqno << "offset=" << offset << " not found, stopped at " << i << " with #"
                 << (ee ? ee->getMsgSeq() : SRT_MSGNO_NONE));
        return SRT_MSGNO_CONTROL;
    }

    HLOGC(bslog.Debug,
          log << "CSndBuffer::getMsgNoAt: for %" << seqno << " offset=" << offset << " found, size=" << p->m_iLength << " %" << p->m_iSeqNo
              << " #" << p->getMsgSeq() << " !" << BufferStamp(p->m_pcData, p->m_iLength));

    return p->getMsgSeq();
}

int CSndBuffer::readOldPacket(int32_t seqno, CPacket& w_packet, steady_clock::time_point& w_srctime, DropRange& w_drop)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);

    Block* p = m_pFirstBlock;

    // XXX Suboptimal procedure to keep the blocks identifiable
    // by sequence number. Consider using some circular buffer.
    for (int i = 0; i < offset && p != m_pLastBlock; ++i)
    {
        p = p->m_pNext;
    }
    if (p == m_pLastBlock)
    {
        LOGC(qslog.Error, log << "CSndBuffer::readData: offset " << offset << " too large!");
        return READ_NONE;
    }
#if HVU_ENABLE_HEAVY_LOGGING
    const int32_t first_seq = p->m_iSeqNo;
    int32_t last_seq = p->m_iSeqNo;
#endif

    w_packet.set_seqno(seqno);

    // This is rexmit request, so the packet should have the sequence number
    // already set when it was once sent uniquely.
    SRT_ASSERT(p->m_iSeqNo == w_packet.seqno());

    // Check if the block that is the next candidate to send (m_pCurrBlock pointing) is stale.

    // If so, then inform the caller that it should first take care of the whole
    // message (all blocks with that message id). Shift the m_pCurrBlock pointer
    // to the position past the last of them. Then return -1 and set the
    // msgno bitset packet field to the message id that should be dropped as
    // a whole.

    // After taking care of that, the caller should immediately call this function again,
    // this time possibly in order to find the real data to be sent.

    // if found block is stale
    // (This is for messages that have declared TTL - messages that fail to be sent
    // before the TTL defined time comes, will be dropped).

    if ((p->m_iTTL >= 0) && (count_milliseconds(steady_clock::now() - p->m_tsOriginTime) > p->m_iTTL))
    {
        w_drop.msgno = p->getMsgSeq();
        int msglen   = 1;
        p            = p->m_pNext;
        bool move    = false;
        while (p != m_pLastBlock && w_drop.msgno == p->getMsgSeq())
        {
#if HVU_ENABLE_HEAVY_LOGGING
            last_seq = p->m_iSeqNo;
#endif
            if (p == m_pCurrBlock)
                move = true;
            p = p->m_pNext;
            if (move)
                m_pCurrBlock = p;
            msglen++;
        }

        HLOGC(qslog.Debug,
              log << "CSndBuffer::readData: due to TTL exceeded, %(" << first_seq << " - " << last_seq << "), "
                  << msglen << " packets to drop with #" << w_drop.msgno);

        // Theoretically as the seq numbers are being tracked, you should be able
        // to simply take the sequence number from the block. But this is a new
        // feature and should be only used after refax for the sender buffer to
        // make it manage the sequence numbers inside, instead of by CUDT::m_iSndLastDataAck.
        w_drop.seqno[DropRange::BEGIN] = w_packet.seqno();
        w_drop.seqno[DropRange::END] = CSeqNo::incseq(w_packet.seqno(), msglen - 1);

        m_SndLossList.removeUpTo(w_drop.seqno[DropRange::END]);

        // Note the rules: here `p` is pointing to the first block AFTER the
        // message to be dropped, so the end sequence should be one behind
        // the one for p. Note that the loop rolls until hitting the first
        // packet that doesn't belong to the message or m_pLastBlock, which
        // is past-the-end for the occupied range in the sender buffer.
        SRT_ASSERT(w_drop.seqno[DropRange::END] == CSeqNo::decseq(p->m_iSeqNo));
        return READ_DROP;
    }

    w_packet.m_pcData = p->m_pcData;
    const int readlen = p->m_iLength;
    w_packet.setLength(readlen, m_iBlockLen);

    // XXX Here the value predicted to be applied to PH_MSGNO field is extracted.
    // As this function is predicted to extract the data to send as a rexmited packet,
    // the packet must be in the form ready to send - so, in case of encryption,
    // encrypted, and with all ENC flags already set. So, the first call to send
    // the packet originally (readData) must set these flags first.
    w_packet.set_msgflags(p->m_iMsgNoBitset);
    w_srctime = p->m_tsOriginTime;

    // This function is called when packet retransmission is triggered.
    // Therefore we are setting the rexmit time.
    p->m_tsRexmitTime = steady_clock::now();

    HLOGC(qslog.Debug,
          log << CONID() << "CSndBuffer: getting packet %" << p->m_iSeqNo << " as per %" << w_packet.seqno()
              << " size=" << readlen << " to send [REXMIT]");

    return readlen;
}

sync::steady_clock::time_point CSndBuffer::getRexmitTime(const int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);
    const Block* p = m_pFirstBlock;

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);
    if (offset < 0 || offset >= m_iCount)
        return sync::steady_clock::time_point();

    // XXX Suboptimal procedure to keep the blocks identifiable
    // by sequence number. Consider using some circular buffer.
    for (int i = 0; i < offset; ++i)
    {
        SRT_ASSERT(p);
        p = p->m_pNext;
    }

    SRT_ASSERT(p);
    return p->m_tsRexmitTime;
}

bool CSndBuffer::revoke(int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);

    // IF distance between m_iSndLastDataAck and ack is nonempty...
    if (offset <= 0)
        return false;

    // remove any loss that predates 'ack' (not to be considered loss anymore)
    m_SndLossList.removeUpTo(CSeqNo::decseq(seqno));

    // We don't check if the sequence is past the last scheduled one;
    // worst case scenario we'll just clear up the whole buffer.
    m_iSndLastDataAck = seqno;

    bool move = false;
    for (int i = 0; i < offset; ++i)
    {
        m_iBytesCount -= m_pFirstBlock->m_iLength;
        if (m_pFirstBlock == m_pCurrBlock)
            move = true;
        m_pFirstBlock = m_pFirstBlock->m_pNext;
    }
    if (move)
        m_pCurrBlock = m_pFirstBlock;

    m_iCount = m_iCount - offset;

    updAvgBufSize(steady_clock::now());
    return true;
}

int32_t CSndBuffer::popLostSeq(DropRange& w_drop)
{
    static const DropRange nodrop = { {SRT_SEQNO_NONE, SRT_SEQNO_NONE}, SRT_MSGNO_CONTROL };
    w_drop = nodrop;

    // First attempt returned nothing, so return nothing and nodrop.
    int32_t seq = m_SndLossList.popLostSeq();
    if (seq == SRT_SEQNO_NONE)
        return seq;

    if (CSeqNo::seqoff(m_iSndLastDataAck, seq) < 0)
    {
        // Always request dropping up to the currently earliest remembered
        // sequence number in the buffer. The only other thing needed to be
        // cleaned up is to remove those outdated seqs from the loss list.
        w_drop.seqno[DropRange::BEGIN] = seq;
        w_drop.seqno[DropRange::END] = m_iSndLastDataAck;

        // In case when the loss record contains any sequences
        // behind m_iSndLastDataAck, collect and drop them.
        for (;;)
        {
            seq = m_SndLossList.popLostSeq();
            if (seq == SRT_SEQNO_NONE || CSeqNo::seqoff(m_iSndLastDataAck, seq) >= 0)
            {
                break;
            }
        }
    }

    return seq;
}

void CSndBuffer::removeLossUpTo(int32_t seqno)
{
    m_SndLossList.removeUpTo(seqno);
}

int CSndBuffer::insertLoss(int32_t lo, int32_t hi, const sync::steady_clock::time_point& pt SRT_ATR_UNUSED)
{
    return m_SndLossList.insert(lo, hi);
}

int CSndBuffer::getLossLength()
{
    return m_SndLossList.getLossLength();
}

int CSndBuffer::getCurrBufSize() const
{
    return m_iCount;
}

int CSndBuffer::getMaxPacketLen() const
{
    return m_iBlockLen - m_iReservedSize;
}

int CSndBuffer::countNumPacketsRequired(int iPldLen) const
{
    const int iPktLen = getMaxPacketLen();
    return number_slices(iPldLen, iPktLen);
}

int CSndBuffer::getAvgBufSize(int& w_bytes, int& w_tsp)
{
    ScopedLock bufferguard(m_BufLock); /* Consistency of pkts vs. bytes vs. spantime */

    /* update stats in case there was no add/ack activity lately */
    updAvgBufSize(steady_clock::now());

    // Average number of packets and timespan could be small,
    // so rounding is beneficial, while for the number of
    // bytes in the buffer is a higher value, so rounding can be omitted,
    // but probably better to round all three values.

    // Using simple rounding, as it should be guaranteed that
    // these values are never negative. If they are, the results
    // would be stupid anyway.
    w_bytes = m_mavg.bytes() + 0.49;
    w_tsp   = m_mavg.timespan_ms() + 0.49;
    return int(m_mavg.pkts() + 0.49);
}

void CSndBuffer::updAvgBufSize(const steady_clock::time_point& now)
{
    if (!m_mavg.isTimeToUpdate(now))
        return;

    int       bytes       = 0;
    int       timespan_ms = 0;
    const int pkts        = getCurrBufSize((bytes), (timespan_ms));
    m_mavg.update(now, pkts, bytes, timespan_ms);
}

int CSndBuffer::getCurrBufSize(int& w_bytes, int& w_timespan) const
{
    w_bytes = m_iBytesCount;
    /*
     * Timespan can be less then 1000 us (1 ms) if few packets.
     * Also, if there is only one pkt in buffer, the time difference will be 0.
     * Therefore, always add 1 ms if not empty.
     */
    if (m_iCount > 0)
        w_timespan = count_milliseconds(m_tsLastOriginTime - m_pFirstBlock->m_tsOriginTime) + 1;
    else
        w_timespan = 0;

    return m_iCount;
}

CSndBuffer::duration CSndBuffer::getBufferingDelay(const time_point& tnow) const
{
    ScopedLock lck(m_BufLock);
    SRT_ASSERT(m_pFirstBlock);
    if (m_iCount == 0)
        return duration(0);

    return tnow - m_pFirstBlock->m_tsOriginTime;
}

int CSndBuffer::dropLateData(int& w_bytes, int32_t& w_first_msgno, const steady_clock::time_point& too_late_time)
{
    int     dpkts  = 0;
    int     dbytes = 0;
    bool    move   = false;
    int32_t msgno  = 0;

    ScopedLock bufferguard(m_BufLock);
    for (int i = 0; i < m_iCount && m_pFirstBlock->m_tsOriginTime < too_late_time; ++i)
    {
        dpkts++;
        dbytes += m_pFirstBlock->m_iLength;
        msgno = m_pFirstBlock->getMsgSeq();

        if (m_pFirstBlock == m_pCurrBlock)
            move = true;
        m_pFirstBlock = m_pFirstBlock->m_pNext;
    }

    if (move)
    {
        m_pCurrBlock = m_pFirstBlock;
    }

    if (dpkts)
    {
        m_iCount = m_iCount - dpkts;

        const int32_t fakeack = CSeqNo::incseq(m_iSndLastDataAck, dpkts);
        m_iSndLastDataAck = fakeack;

        const int32_t minlastack = CSeqNo::decseq(m_iSndLastDataAck);
        m_SndLossList.removeUpTo(minlastack);

        m_iBytesCount -= dbytes;
    }

    w_bytes = dbytes; // even if 0

    // We report the increased number towards the last ever seen
    // by the loop, as this last one is the last received. So remained
    // (even if "should remain") is the first after the last removed one.
    w_first_msgno = ++MsgNo(msgno);
    // Note: this will be 1 if no packets were removed, but in this case
    // dpkts == 0 and the referenced results will not be interpreted.

    updAvgBufSize(steady_clock::now());

    return dpkts;
}

int CSndBuffer::dropAll(int& w_bytes)
{
    ScopedLock bufferguard(m_BufLock);
    const int  dpkts = m_iCount;
    w_bytes          = m_iBytesCount;
    m_pFirstBlock = m_pCurrBlock = m_pLastBlock;
    m_iCount                     = 0;
    m_iBytesCount                = 0;
    updAvgBufSize(steady_clock::now());
    return dpkts;
}

CSndBuffer::~CSndBuffer()
{
    Block* pb = m_pBlock->m_pNext;
    while (pb != m_pBlock)
    {
        Block* temp = pb;
        pb          = pb->m_pNext;
        delete temp;
    }
    delete m_pBlock;

    while (m_pFirstMemSlice != NULL)
    {
        MemSlice* temp = m_pFirstMemSlice;
        m_pFirstMemSlice    = m_pFirstMemSlice->m_pNext;
        delete[] temp->m_pcData;
        delete temp;
    }

    releaseMutex(m_BufLock);
}

// This does the same as increase(); try to find common parts or make
// these two common.
void CSndBuffer::initialize()
{
    // initial physical buffer of "size"
    m_pFirstMemSlice           = new MemSlice;
    m_pFirstMemSlice->m_pcData = new char[m_iSize * m_iBlockLen];
    m_pFirstMemSlice->m_iSize  = m_iSize;
    m_pFirstMemSlice->m_pNext  = NULL;

    // circular linked list for out bound packets
    m_pBlock  = new Block;
    Block* pb = m_pBlock;
    char* pslice  = m_pFirstMemSlice->m_pcData;

    for (int i = 0; i < m_iSize; ++i)
    {
        pb->m_iMsgNoBitset = 0;
        pb->m_pcData = pslice;
        pslice += m_iBlockLen;

        if (i < m_iSize - 1)
        {
            pb->m_pNext        = new Block;
            pb                 = pb->m_pNext;
        }
    }
    pb->m_pNext = m_pBlock;

    m_pFirstBlock = m_pCurrBlock = m_pLastBlock = m_pBlock;

}

void CSndBuffer::increase()
{
    int unitsize = m_pFirstMemSlice->m_iSize;

    // new physical buffer
    MemSlice* nbuf = NULL;
    try
    {
        nbuf           = new MemSlice;
        nbuf->m_pcData = new char[unitsize * m_iBlockLen];
    }
    catch (...)
    {
        delete nbuf;
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    nbuf->m_iSize = unitsize;
    nbuf->m_pNext = NULL;

    // insert the buffer at the end of the buffer list
    MemSlice* p = m_pFirstMemSlice;
    while (p->m_pNext != NULL)
        p = p->m_pNext;
    p->m_pNext = nbuf;

    // new packet blocks
    Block* nblk = NULL;
    try
    {
        nblk = new Block;
    }
    catch (...)
    {
        delete nblk;
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
    Block* pb = nblk;
    for (int i = 1; i < unitsize; ++i)
    {
        pb->m_pNext = new Block;
        pb          = pb->m_pNext;
    }

    // insert the new blocks onto the existing one
    pb->m_pNext           = m_pLastBlock->m_pNext;
    m_pLastBlock->m_pNext = nblk;

    pb       = nblk;
    char* pslice = nbuf->m_pcData;
    for (int i = 0; i < unitsize; ++i)
    {
        pb->m_pcData = pslice;
        pb           = pb->m_pNext;
        pslice += m_iBlockLen;
    }

    m_iSize += unitsize;

    HLOGC(bslog.Debug,
          log << "CSndBuffer: BUFFER FULL - adding " << (unitsize * m_iBlockLen) << " bytes spread to " << unitsize
              << " blocks"
              << " (total size: " << m_iSize << " bytes)");
}

std::string CSndBuffer::show() const
{
    using namespace hvu;

    ofmtbufstream out;

    int offset = 0;
    int seqno = m_iSndLastDataAck;

    fmtc findex = fmtc().width(3).fillzero();

    Block* p = m_pFirstBlock;

    while (p != m_pLastBlock)
    {
        out << "[" << fmt(offset, findex) << "]%" << seqno << ": "
            << p->m_iLength << "!" << BufferStamp(p->m_pcData, p->m_iLength);

        out.puts();
        ++offset;
        seqno = CSeqNo::incseq(seqno);
        p = p->m_pNext;
    }

    return out.str();

}

// Stubs, unused in old buffer

bool CSndBuffer::reserveSeqno(int32_t ) { return true; }
bool CSndBuffer::releaseSeqno(int32_t ) { return true; }
bool CSndBuffer::cancelLostSeq(int32_t) { return false; }

#endif

} // namespace srt
