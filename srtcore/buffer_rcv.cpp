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

    // Check if iFirstNonreadPos is in range [iStartPos, (iStartPos + iMaxPosOff) % iSize].
    // The right edge is included because we expect iFirstNonreadPos to be
    // right after the last valid packet position if all packets are available.
    bool isInRange(int iStartPos, int iMaxPosOff, size_t iSize, int iFirstNonreadPos)
    {
        if (iFirstNonreadPos == iStartPos)
            return true;

        const int iLastPos = (iStartPos + iMaxPosOff) % iSize;
        const bool isOverrun = iLastPos < iStartPos;

        if (isOverrun)
            return iFirstNonreadPos > iStartPos || iFirstNonreadPos <= iLastPos;

        return iFirstNonreadPos > iStartPos && iFirstNonreadPos <= iLastPos;
    }
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
    , m_iEndPos(0)
    , m_iDropPos(0)
    , m_iFirstNonreadPos(0)
    , m_iMaxPosOff(0)
    , m_iNotch(0)
    , m_numRandomPackets(0)
    , m_iFirstRandomMsgPos(-1)
    , m_bPeerRexmitFlag(true)
    , m_bMessageAPI(bMessageAPI)
    , m_iBytesCount(0)
    , m_iPktsCount(0)
    , m_uAvgPayloadSz(SRT_LIVE_DEF_PLSIZE)
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
    HLOGC(brlog.Debug, log << "RCV-BUF-STATE(" << source << ") start=" << m_iStartPos << " end=" << m_iEndPos
            << " drop=" << m_iDropPos << " max-off=+" << m_iMaxPosOff << " seq[start]=%" << m_iStartSeqNo);
}

CRcvBuffer::InsertInfo CRcvBuffer::insert(CUnit* unit)
{
    SRT_ASSERT(unit != NULL);
    const int32_t seqno  = unit->m_Packet.getSeqNo();
    const int     offset = CSeqNo::seqoff(m_iStartSeqNo, seqno);

    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::insert: seqno " << seqno);
    IF_RCVBUF_DEBUG(scoped_log.ss << " msgno " << unit->m_Packet.getMsgSeq(m_bPeerRexmitFlag));
    IF_RCVBUF_DEBUG(scoped_log.ss << " m_iStartSeqNo " << m_iStartSeqNo << " offset " << offset);

    int32_t avail_seq;
    int avail_range;

    if (offset < 0)
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -2");
        return InsertInfo(InsertInfo::BELATED);
    }
    IF_HEAVY_LOGGING(string debug_source = "insert %" + Sprint(seqno));

    if (offset >= (int)capacity())
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -3");

        // Calculation done for the sake of possible discrepancy
        // in order to inform the caller what to do.
        if (m_entries[m_iStartPos].status == EntryState_Avail)
        {
            avail_seq = packetAt(m_iStartPos).getSeqNo();
            avail_range = m_iEndPos - m_iStartPos;
        }
        else if (m_iDropPos == m_iEndPos)
        {
            avail_seq = SRT_SEQNO_NONE;
            avail_range = 0;
        }
        else
        {
            avail_seq = packetAt(m_iDropPos).getSeqNo();

            // We don't know how many packets follow it exactly,
            // but in this case it doesn't matter. We know that
            // at least one is there.
            avail_range = 1;
        }

        IF_HEAVY_LOGGING(debugShowState((debug_source + " overflow").c_str()));

        return InsertInfo(InsertInfo::DISCREPANCY, avail_seq, avail_range);
    }

    // TODO: Don't do assert here. Process this situation somehow.
    // If >= 2, then probably there is a long gap, and buffer needs to be reset.
    SRT_ASSERT((m_iStartPos + offset) / m_szSize < 2);

    const int newpktpos = incPos(m_iStartPos, offset);
    const int prev_max_off = m_iMaxPosOff;
    bool extended_end = false;
    if (offset >= m_iMaxPosOff)
    {
        m_iMaxPosOff = offset + 1;
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
    time_point earlier_time;

    int prev_max_pos = incPos(m_iStartPos, prev_max_off);

    // Update flags
    // Case [A]
    if (extended_end)
    {
        // THIS means that the buffer WAS CONTIGUOUS BEFORE.
        if (m_iEndPos == prev_max_pos)
        {
            // THIS means that the new packet didn't CAUSE a gap
            if (m_iMaxPosOff == prev_max_off + 1)
            {
                // This means that m_iEndPos now shifts by 1,
                // and m_iDropPos must be shifted together with it,
                // as there's no drop to point.
                m_iEndPos = incPos(m_iStartPos, m_iMaxPosOff);
                m_iDropPos = m_iEndPos;
            }
            else
            {
                // Otherwise we have a drop-after-gap candidate
                // which is the currently inserted packet.
                // Therefore m_iEndPos STAYS WHERE IT IS.
                m_iDropPos = incPos(m_iStartPos, m_iMaxPosOff - 1);
            }
        }
    }
    //
    // Since this place, every newpktpos is in the range
    // between m_iEndPos (inclusive) and a position for m_iMaxPosOff.

    // Here you can use prev_max_pos as the position represented
    // by m_iMaxPosOff, as if !extended_end, it was unchanged.
    else if (newpktpos == m_iEndPos)
    {
        // Case [D]: inserted a packet at the first gap following the
        // contiguous region. This makes a potential to extend the
        // contiguous region and we need to find its end.

        // If insertion happened at the very first packet, it is the
        // new earliest packet now. In any other situation under this
        // condition there's some contiguous packet range preceding
        // this position.
        if (m_iEndPos == m_iStartPos)
        {
            earlier_time = getPktTsbPdTime(unit->m_Packet.getMsgTimeStamp());
        }

        updateGapInfo(prev_max_pos);
    }
    // XXX Not sure if that's the best performant comparison
    // What is meant here is that newpktpos is between
    // m_iEndPos and m_iDropPos, though we know it's after m_iEndPos.
    // CONSIDER: make m_iDropPos rather m_iDropOff, this will make
    // this comparison a simple subtraction. Note that offset will
    // have to be updated on every shift of m_iStartPos.
    else if (cmpPos(newpktpos, m_iDropPos) < 0)
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
        // by m_iDropPos, or it would point to some earlier packet in a
        // contiguous series of valid packets following a gap, hence
        // the above condition wouldn't be satisfied.
        m_iDropPos = newpktpos;

        // If there's an inserted packet BEFORE drop-pos (which makes it
        // a new drop-pos), while the very first packet is absent (the
        // below condition), it means we have a new earliest-available
        // packet. Otherwise we would have only a newly updated drop
        // position, but still following some earlier contiguous range
        // of valid packets - so it's earlier than previous drop, but
        // not earlier than the earliest packet.
        if (m_iStartPos == m_iEndPos)
        {
            earlier_time = getPktTsbPdTime(unit->m_Packet.getMsgTimeStamp());
        }
    }
    // OTHERWISE: case [D] in which nothing is to be updated.

    // If packet "in order" flag is zero, it can be read out of order.
    // With TSBPD enabled packets are always assumed in order (the flag is ignored).
    if (!m_tsbpd.isEnabled() && m_bMessageAPI && !unit->m_Packet.getMsgOrderFlag())
    {
        ++m_numRandomPackets;
        onInsertNotInOrderPacket(newpktpos);
    }

    updateNonreadPos();

    CPacket* avail_packet = NULL;

    if (m_entries[m_iStartPos].pUnit && m_entries[m_iStartPos].status == EntryState_Avail)
    {
        avail_packet = &packetAt(m_iStartPos);
        avail_range = m_iEndPos - m_iStartPos;
    }
    else if (!m_tsbpd.isEnabled() && m_iFirstRandomMsgPos != -1)
    {
        // In case when TSBPD is off, we take into account the message mode
        // where messages may potentially span for multiple packets, therefore
        // the only "next deliverable" is the first complete message that satisfies
        // the order requirement.
        avail_packet = &packetAt(m_iFirstRandomMsgPos);
        avail_range = 1;
    }
    else if (m_iDropPos != m_iEndPos)
    {
        avail_packet = &packetAt(m_iDropPos);
        avail_range = 1;
    }

    IF_RCVBUF_DEBUG(scoped_log.ss << " returns 0 (OK)");
    IF_HEAVY_LOGGING(debugShowState((debug_source + " ok").c_str()));

    if (avail_packet)
        return InsertInfo(InsertInfo::INSERTED, avail_packet->getSeqNo(), avail_range, earlier_time);
    else
        return InsertInfo(InsertInfo::INSERTED); // No packet candidate (NOTE: impossible in live mode)
}

// This function should be called after having m_iEndPos
// has somehow be set to position of a non-empty cell.
// This can happen by two reasons:
// - the cell has been filled by incoming packet
// - the value has been reset due to shifted m_iStartPos
// This means that you have to search for a new gap and
// update the m_iEndPos and m_iDropPos fields, or set them
// both to the end of range.
//
// prev_max_pos should be the position represented by m_iMaxPosOff.
// Passed because it is already calculated in insert(), otherwise
// it would have to be calculated here again.
void CRcvBuffer::updateGapInfo(int prev_max_pos)
{
    int pos = m_iEndPos;

    // First, search for the next gap, max until m_iMaxPosOff.
    for ( ; pos != prev_max_pos; pos = incPos(pos))
    {
        if (m_entries[pos].status == EntryState_Empty)
        {
            break;
        }
    }
    if (pos == prev_max_pos)
    {
        // Reached the end and found no gaps.
        m_iEndPos = prev_max_pos;
        m_iDropPos = prev_max_pos;
    }
    else
    {
        // Found a gap at pos
        m_iEndPos = pos;
        m_iDropPos = pos; // fallback, although SHOULD be impossible
        // So, search for the first position to drop up to.
        for ( ; pos != prev_max_pos; pos = incPos(pos))
        {
            if (m_entries[pos].status != EntryState_Empty)
            {
                m_iDropPos = pos;
                break;
            }
        }
    }
}

/// Request to remove from the receiver buffer
/// all packets with earlier sequence than @a seqno.
/// (Meaning, the packet with given sequence shall
/// be the first packet in the buffer after the operation).
int CRcvBuffer::dropUpTo(int32_t seqno)
{
    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::dropUpTo: seqno " << seqno << " m_iStartSeqNo " << m_iStartSeqNo);

    int len = CSeqNo::seqoff(m_iStartSeqNo, seqno);
    if (len <= 0)
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << ". Nothing to drop.");
        return 0;
    }

    m_iMaxPosOff -= len;
    if (m_iMaxPosOff < 0)
        m_iMaxPosOff = 0;

    const int iDropCnt = len;
    while (len > 0)
    {
        dropUnitInPos(m_iStartPos);
        m_entries[m_iStartPos].status = EntryState_Empty;
        SRT_ASSERT(m_entries[m_iStartPos].pUnit == NULL && m_entries[m_iStartPos].status == EntryState_Empty);
        m_iStartPos = incPos(m_iStartPos);
        --len;
    }

    // Update positions
    m_iStartSeqNo = seqno;
    // Move forward if there are "read/drop" entries.
    // (This call MAY shift m_iStartSeqNo further.)
    releaseNextFillerEntries();

    // Start from here and search fort the next gap
    m_iEndPos = m_iDropPos = m_iStartPos;
    updateGapInfo(incPos(m_iStartPos, m_iMaxPosOff));

    // Set nonread position to the starting position before updating,
    // because start position was increased, and preceeding packets are invalid. 
    m_iFirstNonreadPos = m_iStartPos;
    updateNonreadPos();
    if (!m_tsbpd.isEnabled() && m_bMessageAPI)
        updateFirstReadableRandom();

    IF_HEAVY_LOGGING(debugShowState(("drop %" + Sprint(seqno)).c_str()));
    return iDropCnt;
}

int CRcvBuffer::dropAll()
{
    if (empty())
        return 0;

    const int end_seqno = CSeqNo::incseq(m_iStartSeqNo, m_iMaxPosOff);
    return dropUpTo(end_seqno);
}

int CRcvBuffer::dropMessage(int32_t seqnolo, int32_t seqnohi, int32_t msgno)
{
    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::dropMessage: seqnolo " << seqnolo << " seqnohi " << seqnohi << " m_iStartSeqNo " << m_iStartSeqNo);
    // TODO: count bytes as removed?
    const int end_pos = incPos(m_iStartPos, m_iMaxPosOff);
    if (msgno > 0) // including SRT_MSGNO_NONE and SRT_MSGNO_CONTROL
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " msgno " << msgno);
        int minDroppedOffset = -1;
        int iDropCnt = 0;
        for (int i = m_iStartPos; i != end_pos; i = incPos(i))
        {
            // TODO: Maybe check status?
            if (!m_entries[i].pUnit)
                continue;

            // TODO: Break the loop if a massege has been found. No need to search further.
            const int32_t msgseq = packetAt(i).getMsgSeq(m_bPeerRexmitFlag);
            if (msgseq == msgno)
            {
                ++iDropCnt;
                dropUnitInPos(i);
                m_entries[i].status = EntryState_Drop;
                if (minDroppedOffset == -1)
                    minDroppedOffset = offPos(m_iStartPos, i);
            }
        }
        IF_RCVBUF_DEBUG(scoped_log.ss << " iDropCnt " << iDropCnt);
        // Check if units before m_iFirstNonreadPos are dropped.
        bool needUpdateNonreadPos = (minDroppedOffset != -1 && minDroppedOffset <= getRcvDataSize());
        releaseNextFillerEntries();

        // Start from here and search fort the next gap
        m_iEndPos = m_iDropPos = m_iStartSeqNo;
        updateGapInfo(end_pos);

        if (needUpdateNonreadPos)
        {
            m_iFirstNonreadPos = m_iStartPos;
            updateNonreadPos();
        }
        if (!m_tsbpd.isEnabled() && m_bMessageAPI)
        {
            if (!checkFirstReadableRandom())
                m_iFirstRandomMsgPos = -1;
            updateFirstReadableRandom();
        }
        IF_HEAVY_LOGGING(debugShowState(("dropmsg off %" + Sprint(seqnolo)).c_str()));
        return iDropCnt;
    }

    // Drop by packet seqno range.
    const int offset_a = CSeqNo::seqoff(m_iStartSeqNo, seqnolo);
    const int offset_b = CSeqNo::seqoff(m_iStartSeqNo, seqnohi);
    if (offset_b < 0)
    {
        LOGC(rbuflog.Debug, log << "CRcvBuffer.dropMessage(): nothing to drop. Requested [" << seqnolo << "; "
                                << seqnohi << "]. Buffer start " << m_iStartSeqNo << ".");
        return 0;
    }

    const int start_off = max(0, offset_a);
    const int last_pos = incPos(m_iStartPos, offset_b);
    int minDroppedOffset = -1;
    int iDropCnt = 0;
    for (int i = incPos(m_iStartPos, start_off); i != end_pos && i != last_pos; i = incPos(i))
    {
        // Don't drop messages, if all its packets are already in the buffer.
        // TODO: Don't drop a several-packet message if all packets are in the buffer.
        if (m_entries[i].pUnit && packetAt(i).getMsgBoundary() == PB_SOLO)
            continue;

        dropUnitInPos(i);
        ++iDropCnt;
        m_entries[i].status = EntryState_Drop;
        if (minDroppedOffset == -1)
            minDroppedOffset = offPos(m_iStartPos, i);
    }

    LOGC(rbuflog.Debug, log << "CRcvBuffer.dropMessage(): [" << seqnolo << "; "
        << seqnohi << "].");

    // Check if units before m_iFirstNonreadPos are dropped.
    bool needUpdateNonreadPos = (minDroppedOffset != -1 && minDroppedOffset <= getRcvDataSize());
    releaseNextFillerEntries();
    if (needUpdateNonreadPos)
    {
        m_iFirstNonreadPos = m_iStartPos;
        updateNonreadPos();
    }
    if (!m_tsbpd.isEnabled() && m_bMessageAPI)
    {
        if (!checkFirstReadableRandom())
            m_iFirstRandomMsgPos = -1;
        updateFirstReadableRandom();
    }

    IF_HEAVY_LOGGING(debugShowState(("dropmsg off %" + Sprint(seqnolo)).c_str()));
    return iDropCnt;
}

bool CRcvBuffer::getContiguousEnd(int32_t& w_seq) const
{
    if (m_iStartPos == m_iEndPos)
    {
        // Initial contiguous region empty (including empty buffer).
        HLOGC(rbuflog.Debug, log << "CONTIG: empty, give up base=%" << m_iStartSeqNo);
        w_seq = m_iStartSeqNo;
        return m_iMaxPosOff > 0;
    }

    int end_off = offPos(m_iStartPos, m_iEndPos);

    w_seq = CSeqNo::incseq(m_iStartSeqNo, end_off);

    HLOGC(rbuflog.Debug, log << "CONTIG: endD=" << end_off << " maxD=" << m_iMaxPosOff << " base=%" << m_iStartSeqNo
            << " end=%" << w_seq);

    return (end_off < m_iMaxPosOff);
}

int CRcvBuffer::readMessage(char* data, size_t len, SRT_MSGCTRL* msgctrl, pair<int32_t, int32_t>* pw_seqrange)
{
    const bool canReadInOrder = hasReadableInorderPkts();
    if (!canReadInOrder && m_iFirstRandomMsgPos < 0)
    {
        LOGC(rbuflog.Warn, log << "CRcvBuffer.readMessage(): nothing to read. Ignored isRcvDataReady() result?");
        return 0;
    }

    //const bool canReadInOrder = m_iFirstNonreadPos != m_iStartPos;
    const int readPos = canReadInOrder ? m_iStartPos : m_iFirstRandomMsgPos;
    const bool isReadingFromStart = (readPos == m_iStartPos); // Indicates if the m_iStartPos can be changed

    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::readMessage. m_iStartSeqNo " << m_iStartSeqNo << " m_iStartPos " << m_iStartPos << " readPos " << readPos);

    size_t remain = len;
    char* dst = data;
    int    pkts_read = 0;
    int    bytes_extracted = 0; // The total number of bytes extracted from the buffer.

    int32_t out_seqlo = SRT_SEQNO_NONE;
    int32_t out_seqhi = SRT_SEQNO_NONE;

    for (int i = readPos;; i = incPos(i))
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

        if (m_numRandomPackets && !packet.getMsgOrderFlag())
            --m_numRandomPackets;

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
            --m_iMaxPosOff;

            // m_iEndPos and m_iDropPos should be
            // equal to m_iStartPos only if the buffer
            // is empty - but in this case the extraction will
            // not be done. Otherwise m_iEndPos should
            // point to the first empty cell, and m_iDropPos
            // point to the first busy cell after a gap, or
            // at worst be equal to m_iEndPos.

            // Therefore none of them should be updated
            // because they should be constantly updated
            // on an incoming packet, while this function
            // should not read further than to the first
            // empty cell at worst.

            SRT_ASSERT(m_iMaxPosOff >= 0);
            m_iStartSeqNo = CSeqNo::incseq(pktseqno);
        }
        else
        {
            // If out of order, only mark it read.
            m_entries[i].status = EntryState_Read;
        }

        if (pbLast)
        {
            if (readPos == m_iFirstRandomMsgPos)
                m_iFirstRandomMsgPos = -1;
            break;
        }
    }

    countBytes(-pkts_read, -bytes_extracted);

    releaseNextFillerEntries();

    if (!isInRange(m_iStartPos, m_iMaxPosOff, m_szSize, m_iFirstNonreadPos))
    {
        m_iFirstNonreadPos = m_iStartPos;
        //updateNonreadPos();
    }

    // Now that we have m_iStartPos potentially shifted, reinitialize
    // m_iEndPos and m_iDropPos.

    int pend_pos = incPos(m_iStartPos, m_iMaxPosOff);

    // First check: is anything in the beginning
    if (m_entries[m_iStartPos].status == EntryState_Avail)
    {
        // If so, shift m_iEndPos up to the first nonexistent unit
        // XXX Try to optimize search by splitting into two loops if necessary.

        m_iEndPos = incPos(m_iStartPos);
        while (m_entries[m_iEndPos].status == EntryState_Avail)
        {
            m_iEndPos = incPos(m_iEndPos);
            if (m_iEndPos == pend_pos)
                break;
        }

        // If we had first packet available, then there's also no drop pos.
        m_iDropPos = m_iEndPos;

    }
    else
    {
        // If not, reset m_iEndPos and search for the first after-drop candidate.
        m_iEndPos = m_iStartPos;
        m_iDropPos = m_iEndPos;

        while (m_entries[m_iDropPos].status != EntryState_Avail)
        {
            m_iDropPos = incPos(m_iDropPos);
            if (m_iDropPos == pend_pos)
            {
                // Nothing found - set drop pos equal to end pos,
                // which means there's no drop
                m_iDropPos = m_iEndPos;
                break;
            }
        }
    }


    if (!m_tsbpd.isEnabled())
        // We need updateFirstReadableRandom() here even if we are reading inorder,
        // incase readable inorder packets are all read out.
        updateFirstReadableRandom();

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
    int p = m_iStartPos;
    const int end_pos = m_iFirstNonreadPos;

    const bool bTsbPdEnabled = m_tsbpd.isEnabled();
    const steady_clock::time_point now = (bTsbPdEnabled ? steady_clock::now() : steady_clock::time_point());

    int rs = len;
    while ((p != end_pos) && (rs > 0))
    {
        if (!m_entries[p].pUnit)
        {
            p = incPos(p);
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
                break; /* too early for this unit, return whatever was copied */
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
            m_iStartSeqNo = CSeqNo::incseq(m_iStartSeqNo);
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
    // because start position was increased, and preceeding packets are invalid. 
    if (!isInRange(m_iStartPos, m_iMaxPosOff, m_szSize, m_iFirstNonreadPos))
    {
        m_iFirstNonreadPos = m_iStartPos;
    }

    if (iBytesRead == 0)
    {
        LOGC(rbuflog.Error, log << "readBufferTo: 0 bytes read. m_iStartPos=" << m_iStartPos << ", m_iFirstNonreadPos=" << m_iFirstNonreadPos);
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
    return hasReadableInorderPkts() || (m_numRandomPackets > 0 && m_iFirstRandomMsgPos != -1);
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

    const int lastpos = incPos(m_iStartPos, m_iMaxPosOff - 1);
    // Should not happen if TSBPD is enabled (reading out of order is not allowed).
    SRT_ASSERT(m_entries[lastpos].pUnit != NULL);
    if (m_entries[lastpos].pUnit == NULL)
        return 0;

    int startpos = m_iStartPos;

    while (m_entries[startpos].pUnit == NULL)
    {
        if (startpos == lastpos)
            break;

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
    // Check the state of the very first packet first
    if (m_entries[m_iStartPos].status == EntryState_Avail)
    {
        SRT_ASSERT(m_entries[m_iStartPos].pUnit);
        return (PacketInfo) { m_iStartSeqNo, false /*no gap*/, getPktTsbPdTime(packetAt(m_iStartPos).getMsgTimeStamp()) };
    }
    // If not, get the information from the drop
    if (m_iDropPos != m_iEndPos)
    {
        const CPacket& pkt = packetAt(m_iDropPos);
        return (PacketInfo) { pkt.getSeqNo(), true, getPktTsbPdTime(pkt.getMsgTimeStamp()) };
    }

    return (PacketInfo) { SRT_SEQNO_NONE, false, time_point() };
}

std::pair<int, int> CRcvBuffer::getAvailablePacketsRange() const
{
    const int seqno_last = CSeqNo::incseq(m_iStartSeqNo, offPos(m_iStartPos, m_iFirstNonreadPos));
    return std::pair<int, int>(m_iStartSeqNo, seqno_last);
}

bool CRcvBuffer::isRcvDataReady(time_point time_now) const
{
    const bool haveInorderPackets = hasReadableInorderPkts();
    if (!m_tsbpd.isEnabled())
    {
        if (haveInorderPackets)
            return true;

        SRT_ASSERT((!m_bMessageAPI && m_numRandomPackets == 0) || m_bMessageAPI);
        return (m_numRandomPackets > 0 && m_iFirstRandomMsgPos != -1);
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
        SRT_ASSERT((!m_bMessageAPI && m_numRandomPackets == 0) || m_bMessageAPI);
        if (m_iFirstRandomMsgPos >= 0)
        {
            SRT_ASSERT(m_numRandomPackets > 0);
            const CPacket&   packet = packetAt(m_iFirstRandomMsgPos);
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
        m_uAvgPayloadSz = avg_iir<100>(m_uAvgPayloadSz, (unsigned) bytes);
}

void CRcvBuffer::releaseUnitInPos(int pos)
{
    CUnit* tmp = m_entries[pos].pUnit;
    m_entries[pos] = Entry(); // pUnit = NULL; status = Empty
    if (tmp != NULL)
        tmp->m_pParentQueue->makeUnitFree(tmp);
}

bool CRcvBuffer::dropUnitInPos(int pos)
{
    if (!m_entries[pos].pUnit)
        return false;
    if (m_tsbpd.isEnabled())
    {
        updateTsbPdTimeBase(packetAt(pos).getMsgTimeStamp());
    }
    else if (m_bMessageAPI && !packetAt(pos).getMsgOrderFlag())
    {
        --m_numRandomPackets;
        if (pos == m_iFirstRandomMsgPos)
            m_iFirstRandomMsgPos = -1;
    }
    releaseUnitInPos(pos);
    return true;
}

void CRcvBuffer::releaseNextFillerEntries()
{
    int pos = m_iStartPos;
    while (m_entries[pos].status == EntryState_Read || m_entries[pos].status == EntryState_Drop)
    {
        m_iStartSeqNo = CSeqNo::incseq(m_iStartSeqNo);
        releaseUnitInPos(pos);
        pos = incPos(pos);
        m_iStartPos = pos;
        --m_iMaxPosOff;
        if (m_iMaxPosOff < 0)
            m_iMaxPosOff = 0;
    }
}

// TODO: Is this function complete? There are some comments left inside.
void CRcvBuffer::updateNonreadPos()
{
    if (m_iMaxPosOff == 0)
        return;

    const int end_pos = incPos(m_iStartPos, m_iMaxPosOff); // The empty position right after the last valid entry.

    int pos = m_iFirstNonreadPos;
    while (m_entries[pos].pUnit && m_entries[pos].status == EntryState_Avail)
    {
        if (m_bMessageAPI && (packetAt(pos).getMsgBoundary() & PB_FIRST) == 0)
            break;

        for (int i = pos; i != end_pos; i = incPos(i))
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

int CRcvBuffer::findLastMessagePkt()
{
    for (int i = m_iStartPos; i != m_iFirstNonreadPos; i = incPos(i))
    {
        SRT_ASSERT(m_entries[i].pUnit);

        if (packetAt(i).getMsgBoundary() & PB_LAST)
        {
            return i;
        }
    }

    return -1;
}

void CRcvBuffer::onInsertNotInOrderPacket(int insertPos)
{
    if (m_numRandomPackets == 0)
        return;

    // If the following condition is true, there is already a packet,
    // that can be read out of order. We don't need to search for
    // another one. The search should be done when that packet is read out from the buffer.
    //
    // There might happen that the packet being added precedes the previously found one.
    // However, it is allowed to re bead out of order, so no need to update the position.
    if (m_iFirstRandomMsgPos >= 0)
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
    //    m_iFirstRandomMsgPos = insertPos;
    //    return;
    //}

    const int msgNo = pkt.getMsgSeq(m_bPeerRexmitFlag);
    // First check last packet, because it is expected to be received last.
    const bool hasLast = (boundary & PB_LAST) || (-1 < scanNotInOrderMessageRight(insertPos, msgNo));
    if (!hasLast)
        return;

    const int firstPktPos = (boundary & PB_FIRST)
        ? insertPos
        : scanNotInOrderMessageLeft(insertPos, msgNo);
    if (firstPktPos < 0)
        return;

    m_iFirstRandomMsgPos = firstPktPos;
    return;
}

bool CRcvBuffer::checkFirstReadableRandom()
{
    if (m_numRandomPackets <= 0 || m_iFirstRandomMsgPos < 0 || m_iMaxPosOff == 0)
        return false;

    const int endPos = incPos(m_iStartPos, m_iMaxPosOff);
    int msgno = -1;
    for (int pos = m_iFirstRandomMsgPos; pos != endPos; pos = incPos(pos))
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

void CRcvBuffer::updateFirstReadableRandom()
{
    if (hasReadableInorderPkts() || m_numRandomPackets <= 0 || m_iFirstRandomMsgPos >= 0)
        return;

    if (m_iMaxPosOff == 0)
        return;

    // TODO: unused variable outOfOrderPktsRemain?
    int outOfOrderPktsRemain = (int) m_numRandomPackets;

    // Search further packets to the right.
    // First check if there are packets to the right.
    const int lastPos = (m_iStartPos + m_iMaxPosOff - 1) % m_szSize;

    int posFirst = -1;
    int posLast = -1;
    int msgNo = -1;

    for (int pos = m_iStartPos; outOfOrderPktsRemain; pos = incPos(pos))
    {
        if (!m_entries[pos].pUnit)
        {
            posFirst = posLast = msgNo = -1;
            continue;
        }

        const CPacket& pkt = packetAt(pos);

        if (pkt.getMsgOrderFlag())   // Skip in order packet
        {
            posFirst = posLast = msgNo = -1;
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
            posFirst = posLast = msgNo = -1;
            continue;
        }

        if (boundary & PB_LAST)
        {
            m_iFirstRandomMsgPos = posFirst;
            return;
        }

        if (pos == lastPos)
            break;
    }

    return;
}

int CRcvBuffer::scanNotInOrderMessageRight(const int startPos, int msgNo) const
{
    // Search further packets to the right.
    // First check if there are packets to the right.
    const int lastPos = (m_iStartPos + m_iMaxPosOff - 1) % m_szSize;
    if (startPos == lastPos)
        return -1;

    int pos = startPos;
    do
    {
        pos = incPos(pos);
        if (!m_entries[pos].pUnit)
            break;

        const CPacket& pkt = packetAt(pos);

        if (pkt.getMsgSeq(m_bPeerRexmitFlag) != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_LAST packet for msgNo " << msgNo);
            return -1;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_LAST)
            return pos;
    } while (pos != lastPos);

    return -1;
}

int CRcvBuffer::scanNotInOrderMessageLeft(const int startPos, int msgNo) const
{
    // Search preceeding packets to the left.
    // First check if there are packets to the left.
    if (startPos == m_iStartPos)
        return -1;

    int pos = startPos;
    do
    {
        pos = decPos(pos);

        if (!m_entries[pos].pUnit)
            return -1;

        const CPacket& pkt = packetAt(pos);

        if (pkt.getMsgSeq(m_bPeerRexmitFlag) != msgNo)
        {
            LOGC(rbuflog.Error, log << "Missing PB_FIRST packet for msgNo " << msgNo);
            return -1;
        }

        const PacketBoundary boundary = pkt.getMsgBoundary();
        if (boundary & PB_FIRST)
            return pos;
    } while (pos != m_iStartPos);

    return -1;
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

string CRcvBuffer::strFullnessState(int iFirstUnackSeqNo, const time_point& tsNow) const
{
    stringstream ss;

    ss << "iFirstUnackSeqNo=" << iFirstUnackSeqNo << " m_iStartSeqNo=" << m_iStartSeqNo
       << " m_iStartPos=" << m_iStartPos << " m_iMaxPosOff=" << m_iMaxPosOff << ". ";

    ss << "Space avail " << getAvailSize(iFirstUnackSeqNo) << "/" << m_szSize << " pkts. ";

    if (m_tsbpd.isEnabled() && m_iMaxPosOff > 0)
    {
        const PacketInfo nextValidPkt = getFirstValidPacketInfo();
        ss << "(TSBPD ready in ";
        if (!is_zero(nextValidPkt.tsbpd_time))
        {
            ss << count_milliseconds(nextValidPkt.tsbpd_time - tsNow) << "ms";
            const int iLastPos = incPos(m_iStartPos, m_iMaxPosOff - 1);
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
    int offset = CSeqNo::seqoff(m_iStartSeqNo, fromseq);

    // Check if it's still inside the buffer
    if (offset < 0 || offset >= m_iMaxPosOff)
    {
        HLOGC(rbuflog.Debug, log << "getFirstLossSeq: offset=" << offset << " for %" << fromseq
                << " (with max=" << m_iMaxPosOff << ") - NO LOSS FOUND");
        return SRT_SEQNO_NONE;
    }

    // Start position
    int pos = incPos(m_iStartPos, offset);

    // Ok; likely we should stand at the m_iEndPos position.
    // If this given position is earlier than this, then
    // m_iEnd stands on the first loss, unless it's equal
    // to the position pointed by m_iMaxPosOff.

    int32_t ret_seq = SRT_SEQNO_NONE;
    int ret_off = m_iMaxPosOff;

    int end_off = offPos(m_iStartPos, m_iEndPos);
    if (pos < end_off)
    {
        // If m_iEndPos has such a value, then there are
        // no loss packets at all.
        if (end_off != m_iMaxPosOff)
        {
            ret_seq = CSeqNo::incseq(m_iStartSeqNo, end_off);
            ret_off = end_off;
        }
    }
    else
    {
        // Could be strange, but just as the caller wishes:
        // find the first loss since this point on
        // You can't rely on m_iEndPos, you are beyond that now.
        // So simply find the next hole.

        // REUSE offset as a control variable
        for (; offset < m_iMaxPosOff; ++offset)
        {
            int pos = incPos(m_iStartPos, offset);
            if (m_entries[pos].status == EntryState_Empty)
            {
                ret_off = offset;
                ret_seq = CSeqNo::incseq(m_iStartSeqNo, offset);
                break;
            }
        }
    }

    // If found no loss, just return this value and do not
    // rewrite nor look for anything.

    // Also no need to search anything if only the beginning was
    // being looked for.
    if (ret_seq == SRT_SEQNO_NONE || !pw_end)
        return ret_seq;

    // We want also the end range, so continue from where you
    // stopped.

    // Start from ret_off + 1 because we know already that ret_off
    // points to an empty cell.
    for (int off = ret_off + 1; off < m_iMaxPosOff; ++off)
    {
        int pos = incPos(m_iStartPos, off);
        if (m_entries[pos].status != EntryState_Empty)
        {
            *pw_end = CSeqNo::incseq(m_iStartSeqNo, off - 1);
            return ret_seq;
        }
    }

    // Fallback - this should be impossible, so issue a log.
    LOGC(rbuflog.Error, log << "IPE: empty cell pos=" << pos << " %" << CSeqNo::incseq(m_iStartSeqNo, ret_off) << " not followed by any valid cell");

    // Return this in the last resort - this could only be a situation when
    // a packet has somehow disappeared, but it contains empty cells up to the
    // end of buffer occupied range. This shouldn't be possible at all because
    // there must be a valid packet at least at the last occupied cell.
    return SRT_SEQNO_NONE;
}

void CRcvBuffer::getUnitSeriesInfo(int32_t fromseq, size_t maxsize, std::vector<SRTSOCKET>& w_sources)
{
    const int offset = CSeqNo::seqoff(m_iStartSeqNo, fromseq);

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
