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

#include <cmath>
#include <limits>
#include "buffer_rcv.h"
#include "logging.h"

using namespace std;

using namespace srt::sync;
using namespace srt_logging;
namespace srt_logging
{
    extern Logger brlog;
}
#define rbuflog brlog

namespace srt {

namespace {
    struct ScopedLog
    {
        ScopedLog() {}

        ~ScopedLog()
        {
            LOGC(rbuflog.Warn, log << ss.str());
        }

        stringstream ss;
    };

#define IF_RCVBUF_DEBUG(instr) (void)0

}


/*
 *   RcvBufferNew (circular buffer):
 *
 *   |<------------------- m_iSize ----------------------------->|
 *   |       |<----------- m_iMaxPosOff ------------>|           |
 *   |       |                                       |           |
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
 *   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
 *             |                                   |
 *             |                                   |__last pkt received
 *             |___ m_iStartPos: first message to read
 *
 *   m_pUnit[i]->m_iFlag: 0:free, 1:good, 2:passack, 3:dropped
 *
 *   thread safety:
 *    m_iStartPos:   CUDT::m_RecvLock
 *    m_iLastAckPos: CUDT::m_AckLock
 *    m_iMaxPosOff:     none? (modified on add and ack
 */

CRcvBuffer::CRcvBuffer(int initSeqNo, size_t size, /*CUnitQueue* unitqueue, */ bool bMessageAPI)
    : m_entries(size)
    , m_szSize(size) // TODO: maybe just use m_entries.size()
    , m_iStartSeqNo(initSeqNo) // NOTE: SRT_SEQNO_NONE is allowed here.
    , m_iStartPos(0)
    , m_iEndOff(0)
    , m_iDropOff(0)
    , m_iFirstNonreadPos(0)
    , m_iMaxPosOff(0)
    , m_iNotch(0)
    , m_numNonOrderPackets(0)
    , m_iFirstNonOrderMsgPos(CPos_TRAP)
    , m_bPeerRexmitFlag(true)
    , m_bMessageAPI(bMessageAPI)
    , m_iBytesCount(0)
    , m_iPktsCount(0)
    , m_uAvgPayloadSz(0)
{
    SRT_ASSERT(size < size_t(std::numeric_limits<int>::max())); // All position pointers are integers
}

CRcvBuffer::~CRcvBuffer()
{
    // Can be optimized by only iterating m_iMaxPosOff from m_iStartPos.
    for (FixedArray<Entry>::iterator it = m_entries.begin(); it != m_entries.end(); ++it)
    {
        if (!it->pUnit)
            continue;

        it->pUnit->m_pParentQueue->makeUnitFree(it->pUnit);
        it->pUnit = NULL;
    }
}

void CRcvBuffer::debugShowState(const char* source SRT_ATR_UNUSED)
{
    HLOGC(brlog.Debug, log << "RCV-BUF-STATE(" << source
            << ") start=" << m_iStartPos
            << " end=+" << m_iEndOff
            << " drop=+" << m_iDropOff
            << " max-off=+" << m_iMaxPosOff
            << " seq[start]=%" << m_iStartSeqNo.val());
}

CRcvBuffer::InsertInfo CRcvBuffer::insert(CUnit* unit)
{
    SRT_ASSERT(unit != NULL);
    const int32_t seqno  = unit->m_Packet.getSeqNo();
    const COff offset = COff(CSeqNo(seqno) - m_iStartSeqNo);

    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::insert: seqno " << seqno);
    IF_RCVBUF_DEBUG(scoped_log.ss << " msgno " << unit->m_Packet.getMsgSeq(m_bPeerRexmitFlag));
    IF_RCVBUF_DEBUG(scoped_log.ss << " m_iStartSeqNo " << m_iStartSeqNo << " offset " << offset);

    if (offset < COff(0))
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -2");
        return InsertInfo(InsertInfo::BELATED);
    }
    IF_HEAVY_LOGGING(string debug_source = "insert %" + Sprint(seqno));

    if (offset >= COff(capacity()))
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -3");

        InsertInfo ireport (InsertInfo::DISCREPANCY);
        getAvailInfo((ireport));

        IF_HEAVY_LOGGING(debugShowState((debug_source + " overflow").c_str()));

        return ireport;
    }

    // TODO: Don't do assert here. Process this situation somehow.
    // If >= 2, then probably there is a long gap, and buffer needs to be reset.
    SRT_ASSERT((m_iStartPos + offset) / m_szSize < 2);

    const CPos newpktpos = incPos(m_iStartPos, offset);
    const COff prev_max_off = m_iMaxPosOff;
    bool extended_end = false;
    if (offset >= m_iMaxPosOff)
    {
        m_iMaxPosOff = offset + COff(1);
        extended_end = true;
    }

    // Packet already exists
    // (NOTE: the above extension of m_iMaxPosOff is
    // possible even before checking that the packet
    // exists because existence of a packet beyond
    // the current max position is not possible).
    SRT_ASSERT(newpktpos >= 0 && newpktpos < int(m_szSize));
    if (m_entries[newpktpos].status != EntryState_Empty)
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -1");
        IF_HEAVY_LOGGING(debugShowState((debug_source + " redundant").c_str()));
        return InsertInfo(InsertInfo::REDUNDANT);
    }
    SRT_ASSERT(m_entries[newpktpos].pUnit == NULL);

    CUnitQueue* q = unit->m_pParentQueue;
    q->makeUnitTaken(unit);
    m_entries[newpktpos].pUnit  = unit;
    m_entries[newpktpos].status = EntryState_Avail;
    countBytes(1, (int)unit->m_Packet.getLength());

    // Set to a value, if due to insertion there was added
    // a packet that is earlier to be retrieved than the earliest
    // currently available packet.
    time_point earlier_time = updatePosInfo(unit, prev_max_off, offset, extended_end);

    InsertInfo ireport (InsertInfo::INSERTED);
    ireport.first_time = earlier_time;

    // If packet "in order" flag is zero, it can be read out of order.
    // With TSBPD enabled packets are always assumed in order (the flag is ignored).
    if (!m_tsbpd.isEnabled() && m_bMessageAPI && !unit->m_Packet.getMsgOrderFlag())
    {
        ++m_numNonOrderPackets;
        onInsertNonOrderPacket(newpktpos);
    }

    updateNonreadPos();

    // This updates only the first_seq and avail_range fields.
    getAvailInfo((ireport));

    IF_RCVBUF_DEBUG(scoped_log.ss << " returns 0 (OK)");
    IF_HEAVY_LOGGING(debugShowState((debug_source + " ok").c_str()));

    return ireport;
}

void CRcvBuffer::getAvailInfo(CRcvBuffer::InsertInfo& w_if)
{
    // This finds the first possible available packet, which is
    // preferably at cell 0, but if not available, try also with
    // given fallback position, if it's set
    if (m_entries[m_iStartPos].status == EntryState_Avail)
    {
        const CPacket* pkt = &packetAt(m_iStartPos);
        SRT_ASSERT(pkt);
        w_if.avail_range = m_iEndOff;
        w_if.first_seq = CSeqNo(pkt->getSeqNo());
        return;
    }

    // If not the first position, probe the skipped positions:
    // - for live mode, check the DROP position
    //   (for potential after-drop reading)
    // - for message mode, check the non-order message position
    //   (for potential out-of-oder message delivery)

    const CPacket* pkt = NULL;
    if (m_tsbpd.isEnabled())
    {
        // With TSBPD you can rely on drop position, if set
        // Drop position must point always to a valid packet.
        // Drop position must start from +1; 0 means no drop.
        if (m_iDropOff)
        {
            pkt = &packetAt(incPos(m_iStartPos, m_iDropOff));
            SRT_ASSERT(pkt);
        }
    }
    else
    {
        // Message-mode: try non-order read position.
        if (m_iFirstNonOrderMsgPos != CPos_TRAP)
        {
            pkt = &packetAt(m_iFirstNonOrderMsgPos);
            SRT_ASSERT(pkt);
        }
    }

    if (!pkt)
    {
        // This is default, but set just in case
        // The default seq is SRT_SEQNO_NONE.
        w_if.avail_range = COff(0);
        return;
    }

    // TODO: we know that at least 1 packet is available, but only
    // with m_iEndOff we know where the true range is. This could also
    // be implemented for message mode, but still this would employ
    // a separate begin-end range declared for a complete out-of-order
    // message.
    w_if.avail_range = COff(1);
    w_if.first_seq = CSeqNo(pkt->getSeqNo());
}


// This function is called exclusively after packet insertion.
// This will update also m_iEndOff and m_iDropOff fields (the latter
// regardless of the TSBPD mode).
CRcvBuffer::time_point CRcvBuffer::updatePosInfo(const CUnit* unit, const COff prev_max_off,
        const COff offset,
        const bool extended_end)
{
   time_point earlier_time;

   // Update flags
   // Case [A]: insertion of the packet has extended the busy region.
   if (extended_end)
   {
       // THIS means that the buffer WAS CONTIGUOUS BEFORE.
       if (m_iEndOff == prev_max_off)
       {
           // THIS means that the new packet didn't CAUSE a gap
           if (m_iMaxPosOff == prev_max_off + 1)
           {
               // This means that m_iEndOff now shifts by 1,
               // and m_iDropOff is set to 0 as there's no gap.
               m_iEndOff = m_iMaxPosOff;
               m_iDropOff = 0;
           }
           else
           {
               // Otherwise we have a drop-after-gap candidate
               // which is the currently inserted packet.
               // Therefore m_iEndOff STAYS WHERE IT IS.
               m_iDropOff = m_iMaxPosOff - 1;
           }
       }
   }
   //
   // Since this place, every 'offset' is in the range
   // between m_iEndOff (inclusive) and m_iMaxPosOff.
   else if (offset == m_iEndOff)
   {
       // Case [D]: inserted a packet at the first gap following the
       // contiguous region. This makes a potential to extend the
       // contiguous region and we need to find its end.

       // If insertion happened at the very first packet, it is the
       // new earliest packet now. In any other situation under this
       // condition there's some contiguous packet range preceding
       // this position.
       if (m_iEndOff == 0)
       {
           earlier_time = getPktTsbPdTime(unit->m_Packet.getMsgTimeStamp());
       }

       updateGapInfo();
   }
   else if (offset < m_iDropOff)
   {
       // Case [C]: the newly inserted packet precedes the
       // previous earliest delivery position after drop,
       // that is, there is now a "better" after-drop delivery
       // candidate.

       // New position updated a valid packet on an earlier
       // position than the drop position was before, although still
       // following a gap.
       //
       // We know it because if the position has filled a gap following
       // a valid packet, this preceding valid packet would be pointed
       // by m_iDropOff, or it would point to some earlier packet in a
       // contiguous series of valid packets following a gap, hence
       // the above condition wouldn't be satisfied.
       m_iDropOff = offset;

       // If there's an inserted packet BEFORE drop-pos (which makes it
       // a new drop-pos), while the very first packet is absent (the
       // below condition), it means we have a new earliest-available
       // packet. Otherwise we would have only a newly updated drop
       // position, but still following some earlier contiguous range
       // of valid packets - so it's earlier than previous drop, but
       // not earlier than the earliest packet.
       if (m_iEndOff == 0)
       {
           earlier_time = getPktTsbPdTime(unit->m_Packet.getMsgTimeStamp());
       }
   }
   // OTHERWISE: case [B] in which nothing is to be updated.

   return earlier_time;
}

// This function is called when the m_iEndOff has been set to a new
// position and the m_iDropOff should be calculated since that position again.
void CRcvBuffer::updateGapInfo()
{
    COff from = m_iEndOff;
    SRT_ASSERT(m_entries[incPos(m_iStartPos, m_iMaxPosOff)].status == EntryState_Empty);

    CPos pos = incPos(m_iStartPos, from);

    if (m_entries[pos].status == EntryState_Avail)
    {
        CPos end_pos = incPos(m_iStartPos, m_iMaxPosOff);

        for (; pos != end_pos; pos = incPos(pos))
        {
            if (m_entries[pos].status != EntryState_Avail)
                break;
        }

        m_iEndOff = offPos(m_iStartPos, pos);
    }

    // XXX This should be this way, but there are still inconsistencies
    // in the message code.
    //USE: SRT_ASSERT(m_entries[incPos(m_iStartPos, m_iEndOff)].status == EntryState_Empty);
    SRT_ASSERT(m_entries[incPos(m_iStartPos, m_iEndOff)].status != EntryState_Avail);

    // XXX Controversy: m_iDropOff is only used in case when SRTO_TLPKTDROP
    // is set. This option is not handled in message mode, only in live mode.
    // Dropping by packet makes sense only in case of packetwise reading,
    // which isn't the case of neither stream nor message mode.
    if (!m_tsbpd.isEnabled())
    {
        m_iDropOff = 0;
        return;
    }

    // Do not touch m_iDropOff if it's still beside the contiguous
    // region. DO NOT SEARCH for m_iDropOff if m_iEndOff is max
    // because this means that the whole buffer is contiguous.
    // That would simply find nothing and only uselessly burden the
    // performance by searching for a not present empty cell.

    // Also check if the current drop position is a readable packet.
    // If not, start over.
    CPos drop_pos = incPos(m_iStartPos, m_iDropOff);

    if (m_iDropOff < m_iEndOff || m_entries[drop_pos].status != EntryState_Avail)
    {
        m_iDropOff = 0;
        if (m_iEndOff < m_iMaxPosOff)
        {
            CPos start = incPos(m_iStartPos, m_iEndOff + 1),
                 end = incPos(m_iStartPos, m_iEndOff);

            for (CPos i = start; i != end; i = incPos(i))
            {
                if (m_entries[i].status == EntryState_Avail)
                {
                    m_iDropOff = offPos(m_iStartPos, i);
                    break;
                }
            }

            // Must be found somewhere, worst case at the position
            // of m_iMaxPosOff-1. If no finding loop caught it somehow,
            // it will remain at 0. The case when you have empty packets
            // in the busy range is only with message mode after reading
            // packets out-of-order, but this doesn't use tsbpd mode.
            SRT_ASSERT(m_iDropOff != 0);
        }
    }
}

/// Request to remove from the receiver buffer
/// all packets with earlier sequence than @a seqno.
/// (Meaning, the packet with given sequence shall
/// be the first packet in the buffer after the operation).
std::pair<int, int> CRcvBuffer::dropUpTo(int32_t seqno)
{
    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::dropUpTo: seqno " << seqno << " m_iStartSeqNo " << m_iStartSeqNo);

    COff len = COff(CSeqNo(seqno) - m_iStartSeqNo);
    if (len <= 0)
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << ". Nothing to drop.");
        return std::make_pair(0, 0);
    }

    m_iMaxPosOff = decOff(m_iMaxPosOff, len);
    m_iEndOff = decOff(m_iEndOff, len);
    m_iDropOff = decOff(m_iDropOff, len);

    int iNumDropped = 0; // Number of dropped packets that were missing.
    int iNumDiscarded = 0; // The number of dropped packets that existed in the buffer.
    while (len > 0)
    {
        // Note! Dropping a EntryState_Read must not be counted as a drop because it was read.
        // Note! Dropping a EntryState_Drop must not be counted as a drop because it was already dropped and counted earlier.
        if (m_entries[m_iStartPos].status == EntryState_Avail)
            ++iNumDiscarded;
        else if (m_entries[m_iStartPos].status == EntryState_Empty)
            ++iNumDropped;
        dropUnitInPos(m_iStartPos);
        m_entries[m_iStartPos].status = EntryState_Empty;
        SRT_ASSERT(m_entries[m_iStartPos].pUnit == NULL && m_entries[m_iStartPos].status == EntryState_Empty);
        m_iStartPos = incPos(m_iStartPos);
        --len;
    }

    // Update positions
    m_iStartSeqNo = CSeqNo(seqno);
    // Move forward if there are "read/drop" entries.
    // (This call MAY shift m_iStartSeqNo further.)
    releaseNextFillerEntries();

    updateGapInfo();

    // If the nonread position is now behind the starting position, set it to the starting position and update.
    // Preceding packets were likely missing, and the non read position can probably be moved further now.
    if (!isInUsedRange(m_iFirstNonreadPos))
    {
        m_iFirstNonreadPos = m_iStartPos;
        updateNonreadPos();
    }
    if (!m_tsbpd.isEnabled() && m_bMessageAPI)
        updateFirstReadableNonOrder();
    IF_HEAVY_LOGGING(debugShowState(("drop %" + Sprint(seqno)).c_str()));
    return std::make_pair(iNumDropped, iNumDiscarded);
}

int CRcvBuffer::dropAll()
{
    if (empty())
        return 0;

    const int32_t end_seqno = CSeqNo::incseq(m_iStartSeqNo.val(), m_iMaxPosOff);
    const std::pair<int, int> numDropped = dropUpTo(end_seqno);
    return numDropped.first + numDropped.second;
}

int CRcvBuffer::dropMessage(int32_t seqnolo, int32_t seqnohi, int32_t msgno, DropActionIfExists actionOnExisting)
{
    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::dropMessage(): %(" << seqnolo << " - " << seqnohi << ")"
                                  << " #" << msgno << " actionOnExisting=" << actionOnExisting << " m_iStartSeqNo=%"
                                  << m_iStartSeqNo);

    // Drop by packet seqno range to also wipe those packets that do not exist in the buffer.
    const int offset_a = CSeqNo(seqnolo) - m_iStartSeqNo;
    const int offset_b = CSeqNo(seqnohi) - m_iStartSeqNo;
    if (offset_b < 0)
    {
        LOGC(rbuflog.Debug, log << "CRcvBuffer.dropMessage(): nothing to drop. Requested [" << seqnolo << "; "
            << seqnohi << "]. Buffer start " << m_iStartSeqNo.val() << ".");
        return 0;
    }

    const bool bKeepExisting = (actionOnExisting == KEEP_EXISTING);
    COff minDroppedOffset (-1);
    int iDropCnt = 0;
    const COff start_off = COff(max(0, offset_a));
    const CPos start_pos = incPos(m_iStartPos, start_off);
    const COff end_off = COff(min((int) m_szSize - 1, offset_b + 1));
    const CPos end_pos = incPos(m_iStartPos, end_off);
    bool bDropByMsgNo = msgno > SRT_MSGNO_CONTROL; // Excluding both SRT_MSGNO_NONE (-1) and SRT_MSGNO_CONTROL (0).
    for (CPos i = start_pos; i != end_pos; i = incPos(i))
    {
        // Check if the unit was already dropped earlier.
        if (m_entries[i].status == EntryState_Drop)
            continue;

        if (m_entries[i].pUnit)
        {
            const PacketBoundary bnd = packetAt(i).getMsgBoundary();

            // Don't drop messages, if all its packets are already in the buffer.
            // TODO: Don't drop a several-packet message if all packets are in the buffer.
            if (bKeepExisting && bnd == PB_SOLO)
            {
                bDropByMsgNo = false; // Solo packet, don't search for the rest of the message.
                HLOGC(rbuflog.Debug,
                     log << "CRcvBuffer::dropMessage(): Skipped dropping an existing SOLO packet %"
                         << packetAt(i).getSeqNo() << ".");
                continue;
            }

            const int32_t msgseq = packetAt(i).getMsgSeq(m_bPeerRexmitFlag);
            if (msgno > SRT_MSGNO_CONTROL && msgseq != msgno)
            {
                LOGC(rbuflog.Warn, log << "CRcvBuffer.dropMessage(): Packet seqno %" << packetAt(i).getSeqNo() << " has msgno " << msgseq << " differs from requested " << msgno);
            }

            if (bDropByMsgNo && bnd == PB_FIRST)
            {
                // First packet of the message is about to be dropped. That was the only reason to search for msgno.
                bDropByMsgNo = false;
            }
        }

        dropUnitInPos(i);
        ++iDropCnt;
        m_entries[i].status = EntryState_Drop;
        if (minDroppedOffset == -1)
            minDroppedOffset = offPos(m_iStartPos, i);
    }

    if (end_off > m_iMaxPosOff)
    {
        HLOGC(rbuflog.Debug, log << "CRcvBuffer::dropMessage: requested to drop up to %" << seqnohi
                << " with highest in the buffer %" << CSeqNo::incseq(m_iStartSeqNo.val(), end_off)
                << " - updating the busy region");
        m_iMaxPosOff = end_off;
    }

    if (bDropByMsgNo)
    {
        // If msgno is specified, potentially not the whole message was dropped using seqno range.
        // The sender might have removed the first packets of the message, and thus @a seqnolo may point to a packet in the middle.
        // The sender should have the last packet of the message it is requesting to be dropped.
        // Therefore we don't search forward, but need to check earlier packets in the RCV buffer.
        // Try to drop by the message number in case the message starts earlier than @a seqnolo.
        const CPos stop_pos = decPos(m_iStartPos);
        for (CPos i = start_pos; i != stop_pos; i = decPos(i))
        {
            // Can't drop if message number is not known.
            if (!m_entries[i].pUnit) // also dropped earlier.
                continue;

            const PacketBoundary bnd = packetAt(i).getMsgBoundary();
            const int32_t msgseq = packetAt(i).getMsgSeq(m_bPeerRexmitFlag);
            if (msgseq != msgno)
                break;

            if (bKeepExisting && bnd == PB_SOLO)
            {
                LOGC(rbuflog.Debug,
                     log << "CRcvBuffer::dropMessage(): Skipped dropping an existing SOLO message packet %"
                         << packetAt(i).getSeqNo() << ".");
                break;
            }

            ++iDropCnt;
            dropUnitInPos(i);
            m_entries[i].status = EntryState_Drop;
            // As the search goes backward, i is always earlier than minDroppedOffset.
            minDroppedOffset = offPos(m_iStartPos, i);

            // Break the loop if the start of the message has been found. No need to search further.
            if (bnd == PB_FIRST)
                break;
        }
        IF_RCVBUF_DEBUG(scoped_log.ss << " iDropCnt " << iDropCnt);
    }

    if (iDropCnt)
    {
        // We don't need the drop position, if we allow to drop messages by number
        // and with that value we risk that drop was pointing to a dropped packet.
        // Theoretically to make it consistent we need to shift the value to the
        // next found packet, but we don't need this information if we use the message
        // mode (because drop-by-packet is not supported in this mode) and this
        // will burden the performance for nothing.
        m_iDropOff = 0;
    }

    // Check if units before m_iFirstNonreadPos are dropped.
    const bool needUpdateNonreadPos = (minDroppedOffset != -1 && minDroppedOffset <= getRcvDataSize());
    releaseNextFillerEntries();

    updateGapInfo();

    IF_HEAVY_LOGGING(debugShowState(
                ("dropmsg off %" + Sprint(seqnolo) + " #" + Sprint(msgno)).c_str()));

    if (needUpdateNonreadPos)
    {
        m_iFirstNonreadPos = m_iStartPos;
        updateNonreadPos();
    }
    if (!m_tsbpd.isEnabled() && m_bMessageAPI)
    {
        if (!checkFirstReadableNonOrder())
            m_iFirstNonOrderMsgPos = CPos_TRAP;
        updateFirstReadableNonOrder();
    }

    IF_HEAVY_LOGGING(debugShowState(("dropmsg off %" + Sprint(seqnolo)).c_str()));
    return iDropCnt;
}

bool CRcvBuffer::getContiguousEnd(int32_t& w_seq) const
{
    if (m_iEndOff == 0)
    {
        // Initial contiguous region empty (including empty buffer).
        HLOGC(rbuflog.Debug, log << "CONTIG: empty, give up base=%" << m_iStartSeqNo.val());
        w_seq = m_iStartSeqNo.val();
        return m_iMaxPosOff > 0;
    }

    w_seq = CSeqNo::incseq(m_iStartSeqNo.val(), m_iEndOff);

    HLOGC(rbuflog.Debug, log << "CONTIG: endD=" << m_iEndOff
            << " maxD=" << m_iMaxPosOff
            << " base=%" << m_iStartSeqNo.val()
            << " end=%" << w_seq);

    return (m_iEndOff < m_iMaxPosOff);
}

int CRcvBuffer::readMessage(char* data, size_t len, SRT_MSGCTRL* msgctrl, pair<int32_t, int32_t>* pw_seqrange)
{
    const bool canReadInOrder = hasReadableInorderPkts();
    if (!canReadInOrder && m_iFirstNonOrderMsgPos == CPos_TRAP)
    {
        LOGC(rbuflog.Warn, log << "CRcvBuffer.readMessage(): nothing to read. Ignored isRcvDataReady() result?");
        return 0;
    }

    const CPos readPos = canReadInOrder ? m_iStartPos : m_iFirstNonOrderMsgPos;
    const bool isReadingFromStart = (readPos == m_iStartPos); // Indicates if the m_iStartPos can be changed

    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::readMessage. m_iStartSeqNo " << m_iStartSeqNo << " m_iStartPos " << m_iStartPos << " readPos " << readPos);

    size_t remain = len;
    char* dst = data;
    int    pkts_read = 0;
    int    bytes_extracted = 0; // The total number of bytes extracted from the buffer.

    int32_t out_seqlo = SRT_SEQNO_NONE;
    int32_t out_seqhi = SRT_SEQNO_NONE;

    // As we have a green light for reading, it is already known that
    // we're going to either remove or extract packets from the buffer,
    // so drop position won't count anymore.
    //
    // The END position should be updated, that is:
    // - remain just updated by the shifted start position if it's still ahead
    // - recalculated from 0 again otherwise
    m_iDropOff = 0;
    int nskipped = 0;

    for (CPos i = readPos;; i = incPos(i))
    {
        SRT_ASSERT(m_entries[i].pUnit);
        if (!m_entries[i].pUnit)
        {
            LOGC(rbuflog.Error, log << "CRcvBuffer::readMessage(): null packet encountered.");
            break;
        }

        const CPacket& packet  = packetAt(i);
        const size_t   pktsize = packet.getLength();
        const int32_t pktseqno = packet.getSeqNo();

        if (out_seqlo == SRT_SEQNO_NONE)
            out_seqlo = pktseqno;

        out_seqhi = pktseqno;

        // unitsize can be zero
        const size_t unitsize = std::min(remain, pktsize);
        memcpy(dst, packet.m_pcData, unitsize);
        remain -= unitsize;
        dst += unitsize;

        ++pkts_read;
        bytes_extracted += (int) pktsize;

        if (m_tsbpd.isEnabled())
            updateTsbPdTimeBase(packet.getMsgTimeStamp());

        if (m_numNonOrderPackets && !packet.getMsgOrderFlag())
            --m_numNonOrderPackets;

        const bool pbLast  = packet.getMsgBoundary() & PB_LAST;
        if (msgctrl && (packet.getMsgBoundary() & PB_FIRST))
        {
            msgctrl->msgno  = packet.getMsgSeq(m_bPeerRexmitFlag);
        }
        if (msgctrl && pbLast)
        {
            msgctrl->srctime = count_microseconds(getPktTsbPdTime(packet.getMsgTimeStamp()).time_since_epoch());
        }
        if (msgctrl)
            msgctrl->pktseq = pktseqno;

        releaseUnitInPos(i);
        if (isReadingFromStart)
        {
            m_iStartPos = incPos(i);
            m_iStartSeqNo = CSeqNo(pktseqno) + 1;
            ++nskipped;
        }
        else
        {
            // If out of order, only mark it read.
            m_entries[i].status = EntryState_Read;
        }

        if (pbLast)
        {
            if (readPos == m_iFirstNonOrderMsgPos)
            {
                m_iFirstNonOrderMsgPos = CPos_TRAP;
                m_iDropOff = 0; // always set to 0 in this mode.
            }
            break;
        }
    }

    if (nskipped)
    {
        // This means that m_iStartPos HAS BEEN shifted by that many packets.
        // Update offset variables
        m_iMaxPosOff -= nskipped;

        // This is checked as the PB_LAST flag marked packet should still
        // be extracted in the existing period.
        SRT_ASSERT(m_iMaxPosOff >= 0);

        m_iEndOff = decOff(m_iEndOff, len);
    }
    countBytes(-pkts_read, -bytes_extracted);

    releaseNextFillerEntries();

    // This will update the end position
    updateGapInfo();

    if (!isInUsedRange( m_iFirstNonreadPos))
    {
        m_iFirstNonreadPos = m_iStartPos;
        //updateNonreadPos();
    }

    if (!m_tsbpd.isEnabled())
        // We need updateFirstReadableNonOrder() here even if we are reading inorder,
        // incase readable inorder packets are all read out.
        updateFirstReadableNonOrder();

    const int bytes_read = int(dst - data);
    if (bytes_read < bytes_extracted)
    {
        LOGC(rbuflog.Error, log << "readMessage: small dst buffer, copied only " << bytes_read << "/" << bytes_extracted << " bytes.");
    }

    IF_RCVBUF_DEBUG(scoped_log.ss << " pldi64 " << *reinterpret_cast<uint64_t*>(data));

    if (pw_seqrange)
        *pw_seqrange = make_pair(out_seqlo, out_seqhi);

    IF_HEAVY_LOGGING(debugShowState("readmsg"));
    return bytes_read;
}

namespace {
    /// @brief Writes bytes to file stream.
    /// @param data pointer to data to write.
    /// @param len the number of bytes to write
    /// @param dst_offset ignored
    /// @param arg a void pointer to the fstream to write to.
    /// @return true on success, false on failure
    bool writeBytesToFile(char* data, int len, int dst_offset SRT_ATR_UNUSED, void* arg)
    {
        fstream* pofs = reinterpret_cast<fstream*>(arg);
        pofs->write(data, len);
        return !pofs->fail();
    }

    /// @brief Copies bytes to the destination buffer.
    /// @param data pointer to data to copy.
    /// @param len the number of bytes to copy
    /// @param dst_offset offset in destination buffer
    /// @param arg A pointer to the destination buffer
    /// @return true on success, false on failure
    bool copyBytesToBuf(char* data, int len, int dst_offset, void* arg)
    {
        char* dst = reinterpret_cast<char*>(arg) + dst_offset;
        memcpy(dst, data, len);
        return true;
    }
}

int CRcvBuffer::readBufferTo(int len, copy_to_dst_f funcCopyToDst, void* arg)
{
    CPos p = m_iStartPos;
    const CPos end_pos = m_iFirstNonreadPos;

    const bool bTsbPdEnabled = m_tsbpd.isEnabled();
    const steady_clock::time_point now = (bTsbPdEnabled ? steady_clock::now() : steady_clock::time_point());

    int rs = len;
    while ((p != end_pos) && (rs > 0))
    {
        if (!m_entries[p].pUnit)
        {
            // REDUNDANT? p = incPos(p); // Return abandons the loop anyway.
            LOGC(rbuflog.Error, log << "readBufferTo: IPE: NULL unit found in file transmission");
            return -1;
        }

        const srt::CPacket& pkt = packetAt(p);

        if (bTsbPdEnabled)
        {
            const steady_clock::time_point tsPlay = getPktTsbPdTime(pkt.getMsgTimeStamp());
            HLOGC(rbuflog.Debug,
                log << "readBuffer: check if time to play:"
                << " NOW=" << FormatTime(now)
                << " PKT TS=" << FormatTime(tsPlay));

            if ((tsPlay > now))
                break; // too early for this unit, return whatever was copied
        }

        const int pktlen = (int)pkt.getLength();
        const int remain_pktlen = pktlen - m_iNotch;
        const int unitsize = std::min(remain_pktlen, rs);

        if (!funcCopyToDst(pkt.m_pcData + m_iNotch, unitsize, len - rs, arg))
            break;

        if (rs >= remain_pktlen)
        {
            releaseUnitInPos(p);
            p = incPos(p);
            m_iNotch = 0;

            m_iStartPos = p;
            --m_iMaxPosOff;
            SRT_ASSERT(m_iMaxPosOff >= 0);
            m_iEndOff = decOff(m_iEndOff, 1);
            m_iDropOff = decOff(m_iDropOff, 1);

            ++m_iStartSeqNo;
        }
        else
            m_iNotch += rs;

        rs -= unitsize;
    }

    const int iBytesRead = len - rs;
    /* we removed acked bytes form receive buffer */
    countBytes(-1, -iBytesRead);

    // Update positions
    // Set nonread position to the starting position before updating,
    // because start position was increased, and preceding packets are invalid.
    if (!isInUsedRange( m_iFirstNonreadPos))
    {
        m_iFirstNonreadPos = m_iStartPos;
    }

    if (iBytesRead == 0)
    {
        LOGC(rbuflog.Error, log << "readBufferTo: 0 bytes read. m_iStartPos=" << m_iStartPos
                << ", m_iFirstNonreadPos=" << m_iFirstNonreadPos);
    }

    IF_HEAVY_LOGGING(debugShowState("readbuf"));
    return iBytesRead;
}

int CRcvBuffer::readBuffer(char* dst, int len)
{
    return readBufferTo(len, copyBytesToBuf, reinterpret_cast<void*>(dst));
}

int CRcvBuffer::readBufferToFile(fstream& ofs, int len)
{
    return readBufferTo(len, writeBytesToFile, reinterpret_cast<void*>(&ofs));
}

bool CRcvBuffer::hasAvailablePackets() const
{
    return hasReadableInorderPkts() || (m_numNonOrderPackets > 0 && m_iFirstNonOrderMsgPos != CPos_TRAP);
}

int CRcvBuffer::getRcvDataSize() const
{
    return offPos(m_iStartPos, m_iFirstNonreadPos);
}

int CRcvBuffer::getTimespan_ms() const
{
    if (!m_tsbpd.isEnabled())
        return 0;

    if (m_iMaxPosOff == 0)
        return 0;

    CPos lastpos = incPos(m_iStartPos, m_iMaxPosOff - 1);
    // Normally the last position should always be non empty
    // if TSBPD is enabled (reading out of order is not allowed).
    // However if decryption of the last packet fails, it may be dropped
    // from the buffer (AES-GCM), and the position will be empty.
    SRT_ASSERT(m_entries[lastpos].pUnit != NULL || m_entries[lastpos].status == EntryState_Drop);
    while (m_entries[lastpos].pUnit == NULL && lastpos != m_iStartPos)
    {
        lastpos = decPos(lastpos);
    }

    if (m_entries[lastpos].pUnit == NULL)
        return 0;

    CPos startpos = m_iStartPos;
    while (m_entries[startpos].pUnit == NULL && startpos != lastpos)
    {
        startpos = incPos(startpos);
    }

    if (m_entries[startpos].pUnit == NULL)
        return 0;

    const steady_clock::time_point startstamp =
        getPktTsbPdTime(packetAt(startpos).getMsgTimeStamp());
    const steady_clock::time_point endstamp = getPktTsbPdTime(packetAt(lastpos).getMsgTimeStamp());
    if (endstamp < startstamp)
        return 0;

    // One millisecond is added as a duration of a packet in the buffer.
    // If there is only one packet in the buffer, one millisecond is returned.
    return static_cast<int>(count_milliseconds(endstamp - startstamp) + 1);
}

int CRcvBuffer::getRcvDataSize(int& bytes, int& timespan) const
{
    ScopedLock lck(m_BytesCountLock);
    bytes = m_iBytesCount;
    timespan = getTimespan_ms();
    return m_iPktsCount;
}

CRcvBuffer::PacketInfo CRcvBuffer::getFirstValidPacketInfo() const
{
    // Default: no packet available.
    PacketInfo pi = { SRT_SEQNO_NONE, false, time_point() };

    const CPacket* pkt = NULL;

    // Very first packet available with no gap.
    if (m_entries[m_iStartPos].status == EntryState_Avail)
    {
        SRT_ASSERT(m_entries[m_iStartPos].pUnit);
        pkt = &packetAt(m_iStartPos);
    }
    // If not, get the information from the drop
    else if (m_iDropOff)
    {
        CPos drop_pos = incPos(m_iStartPos, m_iDropOff);
        SRT_ASSERT(m_entries[drop_pos].pUnit);
        pkt = &packetAt(drop_pos);
        pi.seq_gap = true; // Available, but after a drop.
    }
    else
    {
        // If none of them point to a valid packet,
        // there is no packet available;
        return pi;
    }

    pi.seqno = pkt->getSeqNo();
    pi.tsbpd_time = getPktTsbPdTime(pkt->getMsgTimeStamp());
    return pi;
}

std::pair<int, int> CRcvBuffer::getAvailablePacketsRange() const
{
    const COff nonread_off = offPos(m_iStartPos, m_iFirstNonreadPos);
    const CSeqNo seqno_last = m_iStartSeqNo + nonread_off;
    return std::pair<int, int>(m_iStartSeqNo.val(), seqno_last.val());
}

bool CRcvBuffer::isRcvDataReady(time_point time_now) const
{
    const bool haveInorderPackets = hasReadableInorderPkts();
    if (!m_tsbpd.isEnabled())
    {
        if (haveInorderPackets)
            return true;

        SRT_ASSERT((!m_bMessageAPI && m_numNonOrderPackets == 0) || m_bMessageAPI);
        return (m_numNonOrderPackets > 0 && m_iFirstNonOrderMsgPos != CPos_TRAP);
    }

    if (!haveInorderPackets)
        return false;

    const PacketInfo info = getFirstValidPacketInfo();

    return info.tsbpd_time <= time_now;
}

CRcvBuffer::PacketInfo CRcvBuffer::getFirstReadablePacketInfo(time_point time_now) const
{
    const PacketInfo unreadableInfo    = {SRT_SEQNO_NONE, false, time_point()};
    const bool       hasInorderPackets = hasReadableInorderPkts();

    if (!m_tsbpd.isEnabled())
    {
        if (hasInorderPackets)
        {
            const CPacket&   packet = packetAt(m_iStartPos);
            const PacketInfo info   = {packet.getSeqNo(), false, time_point()};
            return info;
        }
        SRT_ASSERT((!m_bMessageAPI && m_numNonOrderPackets == 0) || m_bMessageAPI);
        if (m_iFirstNonOrderMsgPos != CPos_TRAP)
        {
            SRT_ASSERT(m_numNonOrderPackets > 0);
            const CPacket&   packet = packetAt(m_iFirstNonOrderMsgPos);
            const PacketInfo info   = {packet.getSeqNo(), true, time_point()};
            return info;
        }
        return unreadableInfo;
    }

    if (!hasInorderPackets)
        return unreadableInfo;

    const PacketInfo info = getFirstValidPacketInfo();

    if (info.tsbpd_time <= time_now)
        return info;
    else
        return unreadableInfo;
}

void CRcvBuffer::countBytes(int pkts, int bytes)
{
    ScopedLock lock(m_BytesCountLock);
    m_iBytesCount += bytes; // added or removed bytes from rcv buffer
    m_iPktsCount  += pkts;
    if (bytes > 0)          // Assuming one pkt when adding bytes
    {
        if (!m_uAvgPayloadSz)
            m_uAvgPayloadSz = bytes;
        else
            m_uAvgPayloadSz = avg_iir<100>(m_uAvgPayloadSz, (unsigned) bytes);
    }
}

void CRcvBuffer::releaseUnitInPos(CPos pos)
{
    CUnit* tmp = m_entries[pos].pUnit;
    m_entries[pos] = Entry(); // pUnit = NULL; status = Empty
    if (tmp != NULL)
        tmp->m_pParentQueue->makeUnitFree(tmp);
}

bool CRcvBuffer::dropUnitInPos(CPos pos)
{
    if (!m_entries[pos].pUnit)
        return false;
    if (m_tsbpd.isEnabled())
    {
        updateTsbPdTimeBase(packetAt(pos).getMsgTimeStamp());
    }
    else if (m_bMessageAPI && !packetAt(pos).getMsgOrderFlag())
    {
        --m_numNonOrderPackets;
        if (pos == m_iFirstNonOrderMsgPos)
            m_iFirstNonOrderMsgPos = CPos_TRAP;
    }
    releaseUnitInPos(pos);
    return true;
}

int CRcvBuffer::releaseNextFillerEntries()
{
    CPos pos = m_iStartPos;
    int nskipped = 0;

    while (m_entries[pos].status == EntryState_Read || m_entries[pos].status == EntryState_Drop)
    {
        if (nskipped == m_iMaxPosOff)
        {
            // This should never happen. All the previously read- or drop-marked
            // packets should be contained in the range up to m_iMaxPosOff. Do not
            // let the buffer ride any further and report the problem. Still stay there.
            LOGC(rbuflog.Error, log << "releaseNextFillerEntries: IPE: Read/Drop status outside the busy range!");
            break;
        }

        ++m_iStartSeqNo;
        releaseUnitInPos(pos);
        pos = incPos(pos);
        m_iStartPos = pos;
        ++nskipped;
    }

    if (!nskipped)
    {
        return nskipped;
    }

    m_iMaxPosOff -= nskipped;
    m_iEndOff = decOff(m_iEndOff, nskipped);

    // Drop off will be updated after that call, if needed.
    m_iDropOff = 0;

    return nskipped;
}

// TODO: Is this function complete? There are some comments left inside.
void CRcvBuffer::updateNonreadPos()
{
    if (m_iMaxPosOff == 0)
        return;

    const CPos end_pos = incPos(m_iStartPos, m_iMaxPosOff); // The empty position right after the last valid entry.

    CPos pos = m_iFirstNonreadPos;
    while (m_entries[pos].pUnit && m_entries[pos].status == EntryState_Avail)
    {
        if (m_bMessageAPI && (packetAt(pos).getMsgBoundary() & PB_FIRST) == 0)
            break;

        for (CPos i = pos; i != end_pos; i = incPos(i))
        {
            if (!m_entries[i].pUnit || m_entries[pos].status != EntryState_Avail)
            {
                break;
            }

            // m_iFirstNonreadPos is moved to the first position BEHIND
            // the PB_LAST packet of the message. There's no guaratnee that
            // the cell at this position isn't empty.

            // Check PB_LAST only in message mode.
            if (!m_bMessageAPI || packetAt(i).getMsgBoundary() & PB_LAST)
            {
                m_iFirstNonreadPos = incPos(i);
                break;
            }
        }

        if (pos == m_iFirstNonreadPos || !m_entries[m_iFirstNonreadPos].pUnit)
            break;

        pos = m_iFirstNonreadPos;
    }
}

CPos CRcvBuffer::findLastMessagePkt()
{
    for (CPos i = m_iStartPos; i != m_iFirstNonreadPos; i = incPos(i))
    {
        SRT_ASSERT(m_entries[i].pUnit);

        if (packetAt(i).getMsgBoundary() & PB_LAST)
        {
            return i;
        }
    }

    return CPos_TRAP;
}

void CRcvBuffer::onInsertNonOrderPacket(CPos insertPos)
{
    if (m_numNonOrderPackets == 0)
        return;

    // If the following condition is true, there is already a packet,
    // that can be read out of order. We don't need to search for
    // another one. The search should be done when that packet is read out from the buffer.
    //
    // There might happen that the packet being added precedes the previously found one.
    // However, it is allowed to re bead out of order, so no need to update the position.
    if (m_iFirstNonOrderMsgPos != CPos_TRAP)
        return;

    // Just a sanity check. This function is called when a new packet is added.
    // So the should be unacknowledged packets.
    SRT_ASSERT(m_iMaxPosOff > 0);
    SRT_ASSERT(m_entries[insertPos].pUnit);
    const CPacket& pkt = packetAt(insertPos);
    const PacketBoundary boundary = pkt.getMsgBoundary();

    //if ((boundary & PB_FIRST) && (boundary & PB_LAST))
    //{
    //    // This packet can be read out of order
    //    m_iFirstNonOrderMsgPos = insertPos;
    //    return;
    //}

    const int msgNo = pkt.getMsgSeq(m_bPeerRexmitFlag);
    // First check last packet, because it is expected to be received last.
    const bool hasLast = (boundary & PB_LAST) || (scanNonOrderMessageRight(insertPos, msgNo) != CPos_TRAP);
    if (!hasLast)
        return;

    const CPos firstPktPos = (boundary & PB_FIRST)
        ? insertPos
        : scanNonOrderMessageLeft(insertPos, msgNo);
    if (firstPktPos == CPos_TRAP)
        return;

    m_iFirstNonOrderMsgPos = firstPktPos;
    return;
}

bool CRcvBuffer::checkFirstReadableNonOrder()
{
    if (m_numNonOrderPackets <= 0 || m_iFirstNonOrderMsgPos == CPos_TRAP || m_iMaxPosOff == COff(0))
        return false;

    const CPos endPos = incPos(m_iStartPos, m_iMaxPosOff);
    int msgno = -1;
    for (CPos pos = m_iFirstNonOrderMsgPos; pos != endPos; pos = incPos(pos)) //   ++pos)
    {
        if (!m_entries[pos].pUnit)
            return false;

        const CPacket& pkt = packetAt(pos);
        if (pkt.getMsgOrderFlag())
            return false;

        if (msgno == -1)
            msgno = pkt.getMsgSeq(m_bPeerRexmitFlag);
        else if (msgno != pkt.getMsgSeq(m_bPeerRexmitFlag))
            return false;

        if (pkt.getMsgBoundary() & PB_LAST)
            return true;
    }

    return false;
}

void CRcvBuffer::updateFirstReadableNonOrder()
{
    if (hasReadableInorderPkts() || m_numNonOrderPackets <= 0 || m_iFirstNonOrderMsgPos != CPos_TRAP)
        return;

    if (m_iMaxPosOff == 0)
        return;

    // TODO: unused variable outOfOrderPktsRemain?
    int outOfOrderPktsRemain = (int) m_numNonOrderPackets;

    // Search further packets to the right.
    // First check if there are packets to the right.
    const CPos lastPos = incPos(m_iStartPos, m_iMaxPosOff - 1);

    CPos posFirst = CPos_TRAP;
    CPos posLast = CPos_TRAP;
    int msgNo = -1;

    for (CPos pos = m_iStartPos; outOfOrderPktsRemain; pos = incPos(pos))
    {
        if (!m_entries[pos].pUnit)
        {
            posFirst = posLast = CPos_TRAP;
            msgNo = -1;
            continue;
        }

        const CPacket& pkt = packetAt(pos);

        if (pkt.getMsgOrderFlag())   // Skip in order packet
        {
            posFirst = posLast = CPos_TRAP;
            msgNo = -1;
            continue;
        }

        --outOfOrderPktsRemain;

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_FIRST)
        {
            posFirst = pos;
            msgNo = pkt.getMsgSeq(m_bPeerRexmitFlag);
        }

        if (pkt.getMsgSeq(m_bPeerRexmitFlag) != msgNo)
        {
            posFirst = posLast = CPos_TRAP;
            msgNo = -1;
            continue;
        }

        if (boundary & PB_LAST)
        {
            m_iFirstNonOrderMsgPos = posFirst;
            return;
        }

        if (pos == lastPos)
            break;
    }

    return;
}

CPos CRcvBuffer::scanNonOrderMessageRight(const CPos startPos, int msgNo) const
{
    // Search further packets to the right.
    // First check if there are packets to the right.
    const CPos lastPos = incPos(m_iStartPos, m_iMaxPosOff - 1);
    if (startPos == lastPos)
        return CPos_TRAP;

    CPos pos = startPos;
    do
    {
        pos = incPos(pos);
        if (!m_entries[pos].pUnit)
            break;

        const CPacket& pkt = packetAt(pos);

        if (pkt.getMsgSeq(m_bPeerRexmitFlag) != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_LAST packet for msgNo " << msgNo);
            return CPos_TRAP;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_LAST)
            return pos;
    } while (pos != lastPos);

    return CPos_TRAP;
}

CPos CRcvBuffer::scanNonOrderMessageLeft(const CPos startPos, int msgNo) const
{
    // Search preceding packets to the left.
    // First check if there are packets to the left.
    if (startPos == m_iStartPos)
        return CPos_TRAP;

    CPos pos = startPos;
    do
    {
        pos = decPos(pos);

        if (!m_entries[pos].pUnit)
            return CPos_TRAP;

        const CPacket& pkt = packetAt(pos);

        if (pkt.getMsgSeq(m_bPeerRexmitFlag) != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_FIRST packet for msgNo " << msgNo);
            return CPos_TRAP;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_FIRST)
            return pos;
    } while (pos != m_iStartPos);

    return CPos_TRAP;
}

bool CRcvBuffer::addRcvTsbPdDriftSample(uint32_t usTimestamp, const time_point& tsPktArrival, int usRTTSample)
{
    return m_tsbpd.addDriftSample(usTimestamp, tsPktArrival, usRTTSample);
}

void CRcvBuffer::setTsbPdMode(const steady_clock::time_point& timebase, bool wrap, duration delay)
{
    m_tsbpd.setTsbPdMode(timebase, wrap, delay);
}

void CRcvBuffer::applyGroupTime(const steady_clock::time_point& timebase,
    bool                            wrp,
    uint32_t                        delay,
    const steady_clock::duration& udrift)
{
    m_tsbpd.applyGroupTime(timebase, wrp, delay, udrift);
}

void CRcvBuffer::applyGroupDrift(const steady_clock::time_point& timebase,
    bool                            wrp,
    const steady_clock::duration& udrift)
{
    m_tsbpd.applyGroupDrift(timebase, wrp, udrift);
}

CRcvBuffer::time_point CRcvBuffer::getTsbPdTimeBase(uint32_t usPktTimestamp) const
{
    return m_tsbpd.getTsbPdTimeBase(usPktTimestamp);
}

void CRcvBuffer::updateTsbPdTimeBase(uint32_t usPktTimestamp)
{
    m_tsbpd.updateTsbPdTimeBase(usPktTimestamp);
}

string CRcvBuffer::strFullnessState(int32_t iFirstUnackSeqNo, const time_point& tsNow) const
{
    stringstream ss;

    ss << "iFirstUnackSeqNo=" << iFirstUnackSeqNo << " m_iStartSeqNo=" << m_iStartSeqNo.val()
       << " m_iStartPos=" << m_iStartPos << " m_iMaxPosOff=" << m_iMaxPosOff << ". ";

    ss << "Space avail " << getAvailSize(iFirstUnackSeqNo) << "/" << m_szSize << " pkts. ";

    if (m_tsbpd.isEnabled() && m_iMaxPosOff > 0)
    {
        const PacketInfo nextValidPkt = getFirstValidPacketInfo();
        ss << "(TSBPD ready in ";
        if (!is_zero(nextValidPkt.tsbpd_time))
        {
            ss << count_milliseconds(nextValidPkt.tsbpd_time - tsNow) << "ms";
            const CPos iLastPos = incPos(m_iStartPos, m_iMaxPosOff - 1);
            if (m_entries[iLastPos].pUnit)
            {
                ss << ", timespan ";
                const uint32_t usPktTimestamp = packetAt(iLastPos).getMsgTimeStamp();
                ss << count_milliseconds(m_tsbpd.getPktTsbPdTime(usPktTimestamp) - nextValidPkt.tsbpd_time);
                ss << " ms";
            }
        }
        else
        {
            ss << "n/a";
        }
        ss << "). ";
    }

    ss << SRT_SYNC_CLOCK_STR " drift " << getDrift() / 1000 << " ms.";
    return ss.str();
}

CRcvBuffer::time_point CRcvBuffer::getPktTsbPdTime(uint32_t usPktTimestamp) const
{
    return m_tsbpd.getPktTsbPdTime(usPktTimestamp);
}

/* Return moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBuffer::getRcvAvgDataSize(int& bytes, int& timespan)
{
    // Average number of packets and timespan could be small,
    // so rounding is beneficial, while for the number of
    // bytes in the buffer is a higher value, so rounding can be omitted,
    // but probably better to round all three values.
    timespan = static_cast<int>(round((m_mavg.timespan_ms())));
    bytes = static_cast<int>(round((m_mavg.bytes())));
    return static_cast<int>(round(m_mavg.pkts()));
}

/* Update moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
void CRcvBuffer::updRcvAvgDataSize(const steady_clock::time_point& now)
{
    if (!m_mavg.isTimeToUpdate(now))
        return;

    int       bytes = 0;
    int       timespan_ms = 0;
    const int pkts = getRcvDataSize(bytes, timespan_ms);
    m_mavg.update(now, pkts, bytes, timespan_ms);
}

int32_t CRcvBuffer::getFirstLossSeq(int32_t fromseq, int32_t* pw_end)
{
    // This means that there are no lost seqs at all, no matter
    // from which position they would have to be checked.
    if (m_iEndOff == m_iMaxPosOff)
        return SRT_SEQNO_NONE;

    COff offset = COff(CSeqNo(fromseq) - m_iStartSeqNo);

    // Check if it's still inside the buffer.
    // Skip the region from 0 to m_iEndOff because this
    // region is by definition contiguous and contains no loss.
    if (offset < m_iEndOff || offset >= m_iMaxPosOff)
    {
        HLOGC(rbuflog.Debug, log << "getFirstLossSeq: offset=" << offset << " for %" << fromseq
                << " (with max=" << m_iMaxPosOff << ") - NO LOSS FOUND");
        return SRT_SEQNO_NONE;
    }

    // Check if this offset is equal to m_iEndOff. If it is,
    // then you have the loss sequence exactly the one that
    // was passed. Skip now, pw_end was not requested.
    if (offset == m_iEndOff)
    {
        if (pw_end)
        {
            // If the offset is exactly at m_iEndOff, then
            // m_iDropOff will mark the end of gap.
            if (m_iDropOff)
                *pw_end = CSeqNo::incseq(m_iStartSeqNo.val(), m_iDropOff);
            else
            {
                LOGC(rbuflog.Error, log << "getFirstLossSeq: IPE: drop-off=0 while seq-off == end-off != max-off");
                *pw_end = fromseq;
            }
        }
        return fromseq;
    }

    int ret_seq = SRT_SEQNO_NONE;
    int loss_off = 0;
    // Now find the first empty position since here,
    // up to m_iMaxPosOff. Checking against m_iDropOff
    // makes no sense because if it is not 0, you'll 
    // find it earlier by checking packet presence.
    for (int off = offset; off < m_iMaxPosOff; ++off)
    {
        CPos ipos ((m_iStartPos + off) % m_szSize);
        if (m_entries[ipos].status == EntryState_Empty)
        {
            ret_seq = CSeqNo::incseq(m_iStartSeqNo.val(), off);
            loss_off = off;
            break;
        }
    }

    if (ret_seq == SRT_SEQNO_NONE)
    {
        // This is theoretically possible if we search from behind m_iEndOff,
        // after m_iDropOff. This simply means that we are trying to search
        // behind the last gap in the buffer.
        return ret_seq;
    }

    // We get this position, so search for the end of gap
    if (pw_end)
    {
        for (int off = loss_off+1; off < m_iMaxPosOff; ++off)
        {
            CPos ipos ((m_iStartPos + off) % m_szSize);
            if (m_entries[ipos].status != EntryState_Empty)
            {
                *pw_end = CSeqNo::incseq(m_iStartSeqNo.val(), off);
                return ret_seq;
            }
        }

        // Should not be possible to not find an existing packet
        // following the gap, otherwise there would be no gap.
        LOGC(rbuflog.Error, log << "getFirstLossSeq: IPE: gap since %" << ret_seq << " not covered by existing packet");
        *pw_end = ret_seq;
    }
    return ret_seq;
}

void CRcvBuffer::getUnitSeriesInfo(int32_t fromseq, size_t maxsize, std::vector<SRTSOCKET>& w_sources)
{
    const int offset = CSeqNo(fromseq) - m_iStartSeqNo;

    // Check if it's still inside the buffer
    if (offset < 0 || offset >= m_iMaxPosOff)
        return;

    // All you need to do is to check if there's a valid packet
    // at given position
    size_t pass = 0;
    for (int off = offset; off < m_iMaxPosOff; ++off)
    {
        int pos = incPos(m_iStartPos, off);
        if (m_entries[pos].pUnit)
        {
            w_sources.push_back(m_entries[pos].pUnit->m_pParentQueue->ownerID());
            ++pass;
            if (pass == maxsize)
                break;
        }
    }
}


} // namespace srt
