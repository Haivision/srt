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

CRcvBuffer::CRcvBuffer(int initSeqNo, size_t size, CUnitQueue* unitqueue, bool bMessageAPI)
    : m_entries(size)
    , m_szSize(size) // TODO: maybe just use m_entries.size()
    , m_pUnitQueue(unitqueue)
    , m_iStartSeqNo(initSeqNo)
    , m_iStartPos(0)
    , m_iFirstNonreadPos(0)
    , m_iMaxPosOff(0)
    , m_iNotch(0)
    , m_numOutOfOrderPackets(0)
    , m_iFirstReadableOutOfOrder(-1)
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
        
        m_pUnitQueue->makeUnitFree(it->pUnit);
        it->pUnit = NULL;
    }
}

int CRcvBuffer::insert(CUnit* unit)
{
    SRT_ASSERT(unit != NULL);
    const int32_t seqno  = unit->m_Packet.getSeqNo();
    const int     offset = CSeqNo::seqoff(m_iStartSeqNo, seqno);

    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::insert: seqno " << seqno);
    IF_RCVBUF_DEBUG(scoped_log.ss << " msgno " << unit->m_Packet.getMsgSeq(m_bPeerRexmitFlag));
    IF_RCVBUF_DEBUG(scoped_log.ss << " m_iStartSeqNo " << m_iStartSeqNo << " offset " << offset);

    if (offset < 0)
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -2");
        return -2;
    }

    if (offset >= (int)capacity())
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -3");
        return -3;
    }

    // TODO: Don't do assert here. Process this situation somehow.
    // If >= 2, then probably there is a long gap, and buffer needs to be reset.
    SRT_ASSERT((m_iStartPos + offset) / m_szSize < 2);

    const int pos = (m_iStartPos + offset) % m_szSize;
    if (offset >= m_iMaxPosOff)
        m_iMaxPosOff = offset + 1;

    // Packet already exists
    SRT_ASSERT(pos >= 0 && pos < int(m_szSize));
    if (m_entries[pos].status != EntryState_Empty)
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << " returns -1");
        return -1;
    }
    SRT_ASSERT(m_entries[pos].pUnit == NULL);

    m_pUnitQueue->makeUnitTaken(unit);
    m_entries[pos].pUnit  = unit;
    m_entries[pos].status = EntryState_Avail;
    countBytes(1, (int)unit->m_Packet.getLength());

    // If packet "in order" flag is zero, it can be read out of order.
    // With TSBPD enabled packets are always assumed in order (the flag is ignored).
    if (!m_tsbpd.isEnabled() && m_bMessageAPI && !unit->m_Packet.getMsgOrderFlag())
    {
        ++m_numOutOfOrderPackets;
        onInsertNotInOrderPacket(pos);
    }

    updateNonreadPos();
    IF_RCVBUF_DEBUG(scoped_log.ss << " returns 0 (OK)");
    return 0;
}

std::pair<int, int> CRcvBuffer::dropUpTo(int32_t seqno)
{
    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::dropUpTo: seqno " << seqno << " m_iStartSeqNo " << m_iStartSeqNo);

    int len = CSeqNo::seqoff(m_iStartSeqNo, seqno);
    if (len <= 0)
    {
        IF_RCVBUF_DEBUG(scoped_log.ss << ". Nothing to drop.");
        return std::make_pair(0, 0);
    }

    m_iMaxPosOff -= len;
    if (m_iMaxPosOff < 0)
        m_iMaxPosOff = 0;

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
    m_iStartSeqNo = seqno;
    // Move forward if there are "read/drop" entries.
    releaseNextFillerEntries();

    // If the nonread position is now behind the starting position, set it to the starting position and update.
    // Preceding packets were likely missing, and the non read position can probably be moved further now.
    if (!isInRange(m_iStartPos, m_iMaxPosOff, m_szSize, m_iFirstNonreadPos))
    {
        m_iFirstNonreadPos = m_iStartPos;
        updateNonreadPos();
    }
    if (!m_tsbpd.isEnabled() && m_bMessageAPI)
        updateFirstReadableOutOfOrder();
    return std::make_pair(iNumDropped, iNumDiscarded);
}

int CRcvBuffer::dropAll()
{
    if (empty())
        return 0;

    const int end_seqno = CSeqNo::incseq(m_iStartSeqNo, m_iMaxPosOff);
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
    const int offset_a = CSeqNo::seqoff(m_iStartSeqNo, seqnolo);
    const int offset_b = CSeqNo::seqoff(m_iStartSeqNo, seqnohi);
    if (offset_b < 0)
    {
        LOGC(rbuflog.Debug, log << "CRcvBuffer.dropMessage(): nothing to drop. Requested [" << seqnolo << "; "
            << seqnohi << "]. Buffer start " << m_iStartSeqNo << ".");
        return 0;
    }

    const bool bKeepExisting = (actionOnExisting == KEEP_EXISTING);
    int minDroppedOffset = -1;
    int iDropCnt = 0;
    const int start_off = max(0, offset_a);
    const int start_pos = incPos(m_iStartPos, start_off);
    const int end_off = min((int) m_szSize - 1, offset_b + 1);
    const int end_pos = incPos(m_iStartPos, end_off);
    bool bDropByMsgNo = msgno > SRT_MSGNO_CONTROL; // Excluding both SRT_MSGNO_NONE (-1) and SRT_MSGNO_CONTROL (0).
    for (int i = start_pos; i != end_pos; i = incPos(i))
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
                LOGC(rbuflog.Debug,
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

    if (bDropByMsgNo)
    {
        // If msgno is specified, potentially not the whole message was dropped using seqno range.
        // The sender might have removed the first packets of the message, and thus @a seqnolo may point to a packet in the middle.
        // The sender should have the last packet of the message it is requesting to be dropped.
        // Therefore we don't search forward, but need to check earlier packets in the RCV buffer.
        // Try to drop by the message number in case the message starts earlier than @a seqnolo.
        const int stop_pos = decPos(m_iStartPos);
        for (int i = start_pos; i != stop_pos; i = decPos(i))
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

    // Check if units before m_iFirstNonreadPos are dropped.
    const bool needUpdateNonreadPos = (minDroppedOffset != -1 && minDroppedOffset <= getRcvDataSize());
    releaseNextFillerEntries();
    if (needUpdateNonreadPos)
    {
        m_iFirstNonreadPos = m_iStartPos;
        updateNonreadPos();
    }
    if (!m_tsbpd.isEnabled() && m_bMessageAPI)
    {
        if (!checkFirstReadableOutOfOrder())
            m_iFirstReadableOutOfOrder = -1;
        updateFirstReadableOutOfOrder();
    }

    return iDropCnt;
}

int CRcvBuffer::readMessage(char* data, size_t len, SRT_MSGCTRL* msgctrl)
{
    const bool canReadInOrder = hasReadableInorderPkts();
    if (!canReadInOrder && m_iFirstReadableOutOfOrder < 0)
    {
        LOGC(rbuflog.Warn, log << "CRcvBuffer.readMessage(): nothing to read. Ignored isRcvDataReady() result?");
        return 0;
    }

    const int readPos = canReadInOrder ? m_iStartPos : m_iFirstReadableOutOfOrder;

    IF_RCVBUF_DEBUG(ScopedLog scoped_log);
    IF_RCVBUF_DEBUG(scoped_log.ss << "CRcvBuffer::readMessage. m_iStartSeqNo " << m_iStartSeqNo << " m_iStartPos " << m_iStartPos << " readPos " << readPos);

    size_t remain = len;
    char* dst = data;
    int    pkts_read = 0;
    int    bytes_extracted = 0; // The total number of bytes extracted from the buffer.
    const bool updateStartPos = (readPos == m_iStartPos); // Indicates if the m_iStartPos can be changed
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

        // unitsize can be zero
        const size_t unitsize = std::min(remain, pktsize);
        memcpy(dst, packet.m_pcData, unitsize);
        remain -= unitsize;
        dst += unitsize;

        ++pkts_read;
        bytes_extracted += (int) pktsize;

        if (m_tsbpd.isEnabled())
            updateTsbPdTimeBase(packet.getMsgTimeStamp());

        if (m_numOutOfOrderPackets && !packet.getMsgOrderFlag())
            --m_numOutOfOrderPackets;

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
        if (updateStartPos)
        {
            m_iStartPos = incPos(i);
            --m_iMaxPosOff;
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
            if (readPos == m_iFirstReadableOutOfOrder)
                m_iFirstReadableOutOfOrder = -1;
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

    if (!m_tsbpd.isEnabled())
        // We need updateFirstReadableOutOfOrder() here even if we are reading inorder,
        // incase readable inorder packets are all read out.
        updateFirstReadableOutOfOrder();

    const int bytes_read = int(dst - data);
    if (bytes_read < bytes_extracted)
    {
        LOGC(rbuflog.Error, log << "readMessage: small dst buffer, copied only " << bytes_read << "/" << bytes_extracted << " bytes.");
    }

    IF_RCVBUF_DEBUG(scoped_log.ss << " pldi64 " << *reinterpret_cast<uint64_t*>(data));

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
    // because start position was increased, and preceding packets are invalid.
    if (!isInRange(m_iStartPos, m_iMaxPosOff, m_szSize, m_iFirstNonreadPos))
    {
        m_iFirstNonreadPos = m_iStartPos;
    }

    if (iBytesRead == 0)
    {
        LOGC(rbuflog.Error, log << "readBufferTo: 0 bytes read. m_iStartPos=" << m_iStartPos << ", m_iFirstNonreadPos=" << m_iFirstNonreadPos);
    }

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
    return hasReadableInorderPkts() || (m_numOutOfOrderPackets > 0 && m_iFirstReadableOutOfOrder != -1);
}

int CRcvBuffer::getRcvDataSize() const
{
    if (m_iFirstNonreadPos >= m_iStartPos)
        return m_iFirstNonreadPos - m_iStartPos;

    return int(m_szSize + m_iFirstNonreadPos - m_iStartPos);
}

int CRcvBuffer::getTimespan_ms() const
{
    if (!m_tsbpd.isEnabled())
        return 0;

    if (m_iMaxPosOff == 0)
        return 0;

    int lastpos = incPos(m_iStartPos, m_iMaxPosOff - 1);
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

    int startpos = m_iStartPos;
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
    const int end_pos = incPos(m_iStartPos, m_iMaxPosOff);
    for (int i = m_iStartPos; i != end_pos; i = incPos(i))
    {
        // TODO: Maybe check status?
        if (!m_entries[i].pUnit)
            continue;

        const CPacket& packet = packetAt(i);
        const PacketInfo info = { packet.getSeqNo(), i != m_iStartPos, getPktTsbPdTime(packet.getMsgTimeStamp()) };
        return info;
    }

    const PacketInfo info = { -1, false, time_point() };
    return info;
}

std::pair<int, int> CRcvBuffer::getAvailablePacketsRange() const
{
    const int seqno_last = CSeqNo::incseq(m_iStartSeqNo, (int) countReadable());
    return std::pair<int, int>(m_iStartSeqNo, seqno_last);
}

size_t CRcvBuffer::countReadable() const
{
    if (m_iFirstNonreadPos >= m_iStartPos)
        return m_iFirstNonreadPos - m_iStartPos;
    return m_szSize + m_iFirstNonreadPos - m_iStartPos;
}

bool CRcvBuffer::isRcvDataReady(time_point time_now) const
{
    const bool haveInorderPackets = hasReadableInorderPkts();
    if (!m_tsbpd.isEnabled())
    {
        if (haveInorderPackets)
            return true;

        SRT_ASSERT((!m_bMessageAPI && m_numOutOfOrderPackets == 0) || m_bMessageAPI);
        return (m_numOutOfOrderPackets > 0 && m_iFirstReadableOutOfOrder != -1);
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
        SRT_ASSERT((!m_bMessageAPI && m_numOutOfOrderPackets == 0) || m_bMessageAPI);
        if (m_iFirstReadableOutOfOrder >= 0)
        {
            SRT_ASSERT(m_numOutOfOrderPackets > 0);
            const CPacket&   packet = packetAt(m_iFirstReadableOutOfOrder);
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

int32_t CRcvBuffer::getFirstNonreadSeqNo() const
{
    const int offset = offPos(m_iStartPos, m_iFirstNonreadPos);
    return CSeqNo::incseq(m_iStartSeqNo, offset);
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

void CRcvBuffer::releaseUnitInPos(int pos)
{
    CUnit* tmp = m_entries[pos].pUnit;
    m_entries[pos] = Entry(); // pUnit = NULL; status = Empty
    if (tmp != NULL)
        m_pUnitQueue->makeUnitFree(tmp);
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
        --m_numOutOfOrderPackets;
        if (pos == m_iFirstReadableOutOfOrder)
            m_iFirstReadableOutOfOrder = -1;
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
    if (m_numOutOfOrderPackets == 0)
        return;

    // If the following condition is true, there is already a packet,
    // that can be read out of order. We don't need to search for
    // another one. The search should be done when that packet is read out from the buffer.
    //
    // There might happen that the packet being added precedes the previously found one.
    // However, it is allowed to re bead out of order, so no need to update the position.
    if (m_iFirstReadableOutOfOrder >= 0)
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
    //    m_iFirstReadableOutOfOrder = insertPos;
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

    m_iFirstReadableOutOfOrder = firstPktPos;
    return;
}

bool CRcvBuffer::checkFirstReadableOutOfOrder()
{
    if (m_numOutOfOrderPackets <= 0 || m_iFirstReadableOutOfOrder < 0 || m_iMaxPosOff == 0)
        return false;

    const int endPos = incPos(m_iStartPos, m_iMaxPosOff);
    int msgno = -1;
    for (int pos = m_iFirstReadableOutOfOrder; pos != endPos; pos = incPos(pos))
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

void CRcvBuffer::updateFirstReadableOutOfOrder()
{
    if (hasReadableInorderPkts() || m_numOutOfOrderPackets <= 0 || m_iFirstReadableOutOfOrder >= 0)
        return;

    if (m_iMaxPosOff == 0)
        return;

    // TODO: unused variable outOfOrderPktsRemain?
    int outOfOrderPktsRemain = (int) m_numOutOfOrderPackets;

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
            m_iFirstReadableOutOfOrder = posFirst;
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
    // Search preceding packets to the left.
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

} // namespace srt
