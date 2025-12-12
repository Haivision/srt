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

CSndBuffer::CSndBuffer(size_t pktsize, size_t slicesize, size_t mss, size_t headersize, size_t reservedsize, int flwsize SRT_ATR_UNUSED) :
    m_BufLock(),
    m_iBlockLen(mss - headersize),
    m_iReservedSize(reservedsize),
    m_iSndLastDataAck(SRT_SEQNO_NONE),
    m_iSndUpdateAck(SRT_SEQNO_NONE),
    m_iNextMsgNo(1),
    m_iBytesCount(0),
    m_iCapacity(pktsize),
    // To avoid performance degradation during the transmission,
    // we allocate in advance all required blocks so that they are
    // picked up from the storage when required.
    m_Packets(m_iBlockLen, m_iCapacity, slicesize)
{
    m_rateEstimator.setHeaderSize(headersize);

    initialize();
    setupMutex(m_BufLock, "SndBuf");
}

void CSndBuffer::initialize()
{
    // If any further initialization is needed
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
    {
        m_iSndLastDataAck = m_iSndUpdateAck = w_seqno;
    }

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
        SndPktArray::Packet& p = m_Packets.push();

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

        SndPktArray::Packet& p = m_Packets.push();

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

int CSndBuffer::extractUniquePacket(CSndPacket& w_packet, steady_clock::time_point& w_srctime, int kflgs, int& w_seqnoinc)
{
    int readlen = 0;
    w_seqnoinc = 0;
    ScopedLock bufferguard(m_BufLock);

    // REPEATABLE BLOCK
    // In the block there will be skipped the TTL-expired messages, if any.
    for (;;)
    {
        SndPktArray::Packet* p = m_Packets.extract_unique();
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
        w_packet.pkt.m_pcData = p->m_pcData;
        readlen = p->m_iLength;
        w_packet.pkt.setLength(readlen, m_iBlockLen);
        w_packet.pkt.set_seqno(p->m_iSeqNo);

        // 1. On submission (addBuffer), the KK flag is set to EK_NOENC (0).
        // 2. The extractUniquePacket() is called to get the original (unique) payload not ever sent yet.
        //    The payload must be encrypted for the first time if the encryption
        //    is enabled (arg kflgs != EK_NOENC). The KK encryption flag of the data packet
        //    header must be set and remembered accordingly (see EncryptionKeySpec).
        // 3. The next time this packet is read (readOldPacket), the payload is already
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
        w_packet.pkt.set_msgflags(p->m_iMsgNoBitset);
        w_srctime = p->m_tsOriginTime;

        // Also make THIS packet busy.
        ++p->m_iBusy;
        w_packet.acquire_busy(p->m_iSeqNo, this);

        HLOGC(bslog.Debug, log << CONID() << "CSndBuffer: UNIQUE packet to send: size=" << readlen
                << " #" << w_packet.pkt.getMsgSeq()
                << " %" << w_packet.pkt.seqno()
                << " !" << BufferStamp(w_packet.pkt.m_pcData, w_packet.pkt.getLength()));

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

// XXX Likely unused (left for the use by tests)
int CSndBuffer::readOldPacket(int32_t seqno, CSndPacket& w_sndpkt, steady_clock::time_point& w_srctime, DropRange& w_drop)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);
    if (offset < 0 || offset >= int(m_Packets.size()))
    {
        LOGC(bslog.Error, log << "CSndBuffer::readOldPacket: for %" << seqno << " offset " << offset << " out of buffer (earliest: %"
                << m_iSndLastDataAck << ")!");
        return READ_NONE;
    }

    // Unlike receiver buffer, in the sender buffer packets are always stored
    // one after another and there are no gaps. Checking the valid range of offset
    // suffices to grant existence of a packet.

    w_sndpkt.pkt.set_seqno(seqno);

    return readPacketInternal(offset, (w_sndpkt), (w_srctime), (w_drop));
}

int CSndBuffer::readPacketInternal(int offset, CSndPacket& w_sndpkt, steady_clock::time_point& w_srctime, DropRange& w_drop)
{
    SndPktArray::Packet* p = &m_Packets[offset];

#if HVU_ENABLE_HEAVY_LOGGING
    const int32_t first_seq = p->m_iSeqNo;
    int32_t last_seq = p->m_iSeqNo;
#endif

    // This is rexmit request, so the packet should have the sequence number
    // already set when it was once sent uniquely.
    SRT_ASSERT(p->m_iSeqNo == w_sndpkt.pkt.seqno());

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

        HLOGC(bslog.Debug,
              log << "CSndBuffer::extractUniquePacket: due to TTL exceeded, %(" << first_seq << " - " << last_seq << "), "
                  << (1 + lastx - offset) << " packets to drop with #" << w_drop.msgno);

        // Make sure that the packets belonging to the expired message are
        // no longer in the unique range, even if they were before.
        m_Packets.set_expired(lastx);
        w_drop.msgno = p->getMsgSeq();

        w_drop.seqno[DropRange::BEGIN] = w_sndpkt.pkt.seqno();
        w_drop.seqno[DropRange::END] = CSeqNo::incseq(w_sndpkt.pkt.seqno(), lastx - offset);

        // We let the caller handle it, while we state no packet delivered.
        // NOTE: the expiration of a message doesn't imply recovation from the buffer.
        // Revocation will still happen on ACK.
        return READ_DROP;
    }

    w_sndpkt.pkt.m_pcData = p->m_pcData;
    const int readlen = p->m_iLength;
    w_sndpkt.pkt.setLength(readlen, m_iBlockLen);

    // We state that the requested seqno refers to a historical (not unique)
    // packet, hence the encryption action has encrypted the data and updated
    // the flags.
    w_sndpkt.pkt.set_msgflags(p->m_iMsgNoBitset);
    w_srctime = p->m_tsOriginTime;

    // This function is called when packet retransmission is triggered.
    // Therefore we are setting the rexmit time.
    p->m_tsRexmitTime = steady_clock::now();

    ++p->m_iBusy;
    w_sndpkt.acquire_busy(p->m_iSeqNo, this);

    HLOGC(bslog.Debug,
          log << CONID() << "CSndBuffer: getting packet %" << p->m_iSeqNo << " as per %" << w_sndpkt.pkt.seqno()
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

int CSndBuffer::extractFirstRexmitPacket(const duration& min_rexmit_interval, int32_t& w_current_seqno, CSndPacket& w_sndpkt,
        sync::steady_clock::time_point& w_tsOrigin, std::vector<CSndBuffer::DropRange>& w_drops)
{
    // Get the first sequence for retransmission, bypassing and taking care of
    // those that are in the forgotten region, as well as required to be rejected.
    // Look into the loss list and drop all sequences that are alrady revoked
    // from the sender buffer, send the drop request if needed, and return the
    // sender buffer offset for the next packet to retransmit, or -1 if there
    // is no retransmission candidate at the moment.

    ScopedLock bufferguard(m_BufLock);

    int seq = SRT_SEQNO_NONE;

    int offset = -1;
    int payload = 0; // default: no packet extracted

    HLOGC(qslog.Debug, log << "REXMIT: looking for loss report since %" << m_iSndLastDataAck << "...");

    // REPEATABLE BLOCK (not a real loop)
    // The call to readPacketInternal may result in a drop request, which must be
    // handled and then the call repeated, until it returns a valid packet
    // or no packet to retransmit.
    for (;;)
    {
        // This is preferably done only once; exceptionally it may be
        // repeated if it turns out that the message has expired
        // (a feature used exclusively in message-mode).
        offset = m_Packets.extractFirstLoss(min_rexmit_interval);

        // No loss found - return 0: no lost packets extracted.
        if (offset == -1)
        {
            HLOGC(qslog.Debug, log << "REXMIT: no loss found");
            break;
        }

        seq = CSeqNo::incseq(m_iSndLastDataAck, offset);

        HLOGC(qslog.Debug, log << "REXMIT: got %" << seq << ", requesting that packet from sndbuf with first %"
                << firstSeqNo());

        // Extract the packet from the sender buffer that is mapped to the expected sequence
        // number, bypassing and taking care of those that are decided to be dropped.

        typedef CSndBuffer::DropRange DropRange;
        DropRange buffer_drop;

        w_sndpkt.pkt.set_seqno(seq);

        // Might be that if you read THIS packet, it results in a drop request.
        // BUT: if you got drop request for this 'offset' (sequence effectively), then
        // you won't get this sequence again. Forget this then and pick up the next loss candidate.
        payload = readPacketInternal(offset, (w_sndpkt), (w_tsOrigin), (buffer_drop));
        if (payload == CSndBuffer::READ_DROP)
        {
            SRT_ASSERT(CSeqNo::seqoff(buffer_drop.seqno[DropRange::BEGIN], buffer_drop.seqno[DropRange::END]) >= 0);

            HLOGC(qslog.Debug,
                    log << "... loss-reported packets expired in SndBuf - requesting DROP: #"
                    << buffer_drop.msgno << " %(" << buffer_drop.seqno[DropRange::BEGIN] << " - "
                    << buffer_drop.seqno[DropRange::END] << ")");
            w_drops.push_back(buffer_drop);

            // skip all dropped packets
            w_current_seqno = CSeqNo::maxseq(w_current_seqno, buffer_drop.seqno[DropRange::END]);
            continue;
        }

        break;
    }

    return payload;
}

void CSndBuffer::releasePacket(int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);
    if (offset < 0 || offset >= int(m_Packets.size()))
    {
        // XXX Issue a log; should never happen
        // (a packet shall not be removed from the sender buffer if it is
        // busy, no matter for what reason it's attempted to be removed)
        return;
    }

    if (m_Packets[offset].m_iBusy <= 0)
    {
        // XXX Issue a log - this is OOS or memover case.
        return;
    }

    --m_Packets[offset].m_iBusy;

    // After releasing this packet try to revoke as many packets
    // as possible, up to m_iSndUpdateAck.
    IF_HEAVY_LOGGING(bool logged = false);
    if (m_iSndUpdateAck != SRT_SEQNO_NONE && m_iSndUpdateAck != m_iSndLastDataAck)
    {
        int latest_offset = CSeqNo::seqoff(m_iSndLastDataAck, m_iSndUpdateAck);
        if (latest_offset > 0)
        {
            int removed = m_Packets.pop(latest_offset);
            m_iSndLastDataAck = CSeqNo::incseq(m_iSndLastDataAck, removed);
            HLOGC(bslog.Debug, log << "CSndBuffer::releasePacket %" << seqno << ": ACK-revoked " << removed
                    << " more packets up to %" << m_iSndLastDataAck);
            IF_HEAVY_LOGGING(logged = true);
        }
    }
    IF_HEAVY_LOGGING(if (!logged) LOGC(bslog.Debug, log << "CSndBuffer::releasePacket: %" << seqno << ": non+ pkts revoked"));
}

bool CSndBuffer::revoke(int32_t seqno)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seqno);

    // IF distance between m_iSndLastDataAck and ack is nonempty...
    if (offset <= 0)
        return false;

    // NOTE: offset points to the first packet that should remain
    // in the buffer, hence it's already a past-the-end for the revoked.
    // The call is also safe for calling with excessive value of offset.
    int popped_up_to = m_Packets.pop(offset);
    if (popped_up_to == offset)
    {
        m_iSndLastDataAck = seqno;
        m_iSndUpdateAck = seqno;
        HLOGC(bslog.Debug, log << "CSndBuffer::revoke: all up to ACK %" << seqno);
    }
    else
    {
        // We have removed less packets than required because some were
        // currently reserved as busy, therefore only remember the original
        // sequence number so that these packets are removed later.
        m_iSndUpdateAck = seqno;
        m_iSndLastDataAck = CSeqNo::incseq(m_iSndLastDataAck, popped_up_to);
        HLOGC(bslog.Debug, log << "CSndBuffer::revoke: ONLY UP TO first busy %" << m_iSndLastDataAck
                << " with postponed ACK %" << m_iSndUpdateAck);
    }

    updAvgBufSize(steady_clock::now());
    return true;
}

bool CSndBuffer::cancelLostSeq(int32_t seq)
{
    ScopedLock bufferguard(m_BufLock);

    int offset = CSeqNo::seqoff(m_iSndLastDataAck, seq);
    if (offset < 0 || offset >= int(m_Packets.size()))
        return false;

    return m_Packets.clear_loss(offset);
}

// XXX likely unused
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
    for (i = 0; i < m_Packets.size(); ++i)
    {
        SndPktArray::Packet& p = m_Packets[i];
        // Stop on first busy or young enough
        if (p.m_iBusy || p.m_tsOriginTime >= too_late_time)
            break;
        dbytes += p.m_iLength;
        msgno = p.getMsgSeq();
    }

    // Now delete all these packets from the container
    if (i)
    {
        // As the loop stopped on first busy, we ignore the return value
        // because there should be no busy packets in this range.
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

    SndPktArray::PacketShowState st;
    for (size_t i = 0; i < m_Packets.size(); ++i)
    {
        int seqno = CSeqNo::incseq(m_iSndLastDataAck, i);
        out << "[" << fmt(i, findex) << "]%" << seqno << ": ";
        m_Packets.showline(i, (st), (out));
        out.base() << endl;
    }

    return out.str();
}


// SndPktArray implementation

SndPktArray::Packet* SndPktArray::extract_unique()
{
    // It should be only possible to be 0, but just in case.
    if (m_iNewQueued <= 0)
        return NULL;

    SRT_ASSERT(m_iNewQueued <= int(m_PktQueue.size()));

    // If m_iNewQueued == 1, then only the last item is the unique one,
    // which's index is size()-1.
    size_t index = int(m_PktQueue.size()) - m_iNewQueued;
    --m_iNewQueued; // We checked in advance that it's > 0.
    return &m_PktQueue[index];
}

int SndPktArray::next_loss(int current_loss)
{
    if (current_loss == -1)
        return -1;

    SRT_ASSERT(current_loss < int(m_PktQueue.size()));

    Packet& p = m_PktQueue[current_loss];
    SRT_ASSERT(p.m_iLossLength > 0);

    if (p.m_iNextLossGroupOffset == 0)
        return -1; // The last loss

    SRT_ASSERT(p.m_iLossLength < p.m_iNextLossGroupOffset);

    SRT_ASSERT(current_loss + p.m_iNextLossGroupOffset < int(m_PktQueue.size()));

    return current_loss + p.m_iNextLossGroupOffset;
}

// NOTE: 'n' is the index in the m_PktQueue array
// up to which (including) the losses must be cleared off.
// This should only result in making the m_iFirstRexmit
// and m_iLastRexmit field pointing to either -1 or
// valid indexes in the container, but OUTSIDE the range
// from 0 to n.
void SndPktArray::remove_loss(int last_to_clear)
{
    // This is going to remove the loss records since the first one
    // up to the packet designated by the last_to_clear offset (same as pop()).

    // this empty() is just formally - with empty m_iFirstRexmit should be moreover -1
    if (m_iFirstRexmit == -1 || m_PktQueue.empty()) // no loss records
        return; // last is also -1 in this situation

    const int LASTX = int(m_PktQueue.size()) - 1;

    // Handle special case: if last_to_clear is the last index in the container,
    // simply remove everything. Just update all the loss nodes.
    if (last_to_clear >= LASTX)
    {
        for (int loss = m_iFirstRexmit, next; loss != -1; loss = next)
        {
            // use safe-loop rule because the node data will be cleared here.
            next = next_loss(loss);
            SndPktArray::Packet& p = m_PktQueue[loss];
            p.m_iLossLength = 0;
            p.m_iNextLossGroupOffset = 0;
        }
        m_iFirstRexmit = -1;
        m_iLastRexmit= -1;
        m_iLossLengthCache = 0;
        string msg SRT_ATR_UNUSED;
        SRT_ASSERT(validateLossIntegrity((msg)));
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
        SndPktArray::Packet& p = m_PktQueue[m_iFirstRexmit];

        int last_index = m_iFirstRexmit + p.m_iLossLength - 1;
        if (last_to_clear < last_index)
        {
            // split-in-half case. This will be the last on which the operation is done.

            int new_beginning = last_to_clear + 1; // The case when last_to_clear == m_PktQueue.size() - 1 is handled already
            SRT_ASSERT(new_beginning > m_iFirstRexmit);
            SRT_ASSERT(new_beginning < int(m_PktQueue.size()));

            int revoked_length_fragment = new_beginning - m_iFirstRexmit;

            // Now shift the position
            bool is_last = false;
            m_PktQueue[new_beginning].m_iLossLength = p.m_iLossLength - revoked_length_fragment;
            if (p.m_iNextLossGroupOffset)
            {
                int next_index = m_iFirstRexmit + p.m_iNextLossGroupOffset;
                // Replicate the distance at the new index
                m_PktQueue[new_beginning].m_iNextLossGroupOffset = next_index - new_beginning;
                SRT_ASSERT(new_beginning + m_PktQueue[new_beginning].m_iNextLossGroupOffset < int(m_PktQueue.size()));
            }
            else
            {
                // No next group, this is the last one.
                m_PktQueue[new_beginning].m_iNextLossGroupOffset = 0;
                is_last = true;
            }

            // Cancel the previous first node
            p.m_iLossLength = 0;
            p.m_iNextLossGroupOffset = 0;

            // NOTE: the new values of m_iFirstRexmit and m_iLastRexmit set here
            // are valid indexes AFTER REMOVAL of the revoked elements from m_PktQueue.
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

    string msg SRT_ATR_UNUSED;
    SRT_ASSERT(validateLossIntegrity((msg)));
}

bool SndPktArray::clear_loss(int index)
{
    // Just access the record. Now return false only
    // if it turns out that it has never been rexmit-scheduled.
    SndPktArray::Packet& p = m_PktQueue[index];
    if (is_zero(p.m_tsNextRexmitTime))
        return false;

    p.m_tsNextRexmitTime = time_point();
    return true;
}

void SndPktArray::clear()
{
    // pop() will do erase(begin(), end()) in this case,
    // but will also destroy every packet.
    pop(size());
}

SndPktArray::Packet& SndPktArray::push()
{
    m_PktQueue.push_back(Packet());
    m_iCachedSize = m_PktQueue.size();

    Packet& that = m_PktQueue.back();

    IF_HEAVY_LOGGING(size_t storage_before = m_Storage.storage.size());

    // Allocate the packet payload space
    that.m_iBusy = 0;
    that.m_iLength = m_Storage.blocksize;
    that.m_pcData = m_Storage.get();

    HLOGC(bslog.Debug, log << "SndPktArray::push: new buffer ("
            << (storage_before == m_Storage.storage.size() ? "ALLOCATED" : "RECYCLED")
            << "), active " << m_iCachedSize.load() << ", archived " << m_Storage.storage.size() << " buffers");

    // pushing is always treated as adding a new unique packet
    ++m_iNewQueued;

    // Return as is - without initialized fields.
    return that;
}

// 'n' is the past-the-end index for removal
size_t SndPktArray::pop(size_t n)
{
    if (m_PktQueue.empty() || !n)
        return 0; // The size is also unchanged

    if (n > m_PktQueue.size())
        n = m_PktQueue.size();

    // We consider that this call clears off losses in the container
    // calls from 0 to n (inc).

    // NOTE: Losses are removed anyway, regardless of the busy status.
    remove_loss(n-1); // remove_loss includes given index

    deque<SndPktArray::Packet>::iterator i = m_PktQueue.begin(), upto = i + n;
    for (; i != upto; ++i)
    {
        // Stop at first busy.
        if (i->m_iBusy)
        {
            //prematurely interrupted; update n.
            upto = i;
            n = std::distance(m_PktQueue.begin(), upto);
            break;
        }
        // Deallocate storage
        m_Storage.put(i->m_pcData);
    }

    m_PktQueue.erase(m_PktQueue.begin(), upto);
    m_iCachedSize = m_PktQueue.size();

    // pop might have removed also packets from the unique range;
    // in that case just shrink it to the existing range.
    m_iNewQueued = std::min<int>(m_iNewQueued, m_PktQueue.size());

    HLOGC(bslog.Debug, log << "SndPktArray::pop: released " << n << " buffers, active "
            << m_iCachedSize.load() << ", archived " << m_Storage.storage.size() << " buffers");

    // These are indexes into the m_PktQueue container, so with
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

// Destructor does the same as pop(size()), except that we can´t deny deletion
// for any reason. If m_iBusy found, all we can do is to issue an error log,
// but deletion must still happen otherwise it will be a leak.
SndPktArray::~SndPktArray()
{
    for (deque<SndPktArray::Packet>::iterator i = m_PktQueue.begin();
            i != m_PktQueue.end(); ++i)
    {
        // Stop at first busy.
        if (i->m_iBusy)
        {
            LOGC(bslog.Fatal, log << "IPE: CSndBuffer.Array packet =" << distance(m_PktQueue.begin(), i)
                    << " %" << i->m_iSeqNo << " HAS STILL " << i->m_iBusy << " USERS!");
        }
        // Deallocate storage
        m_Storage.put(i->m_pcData);
    }

    m_PktQueue.clear();
}

bool SndPktArray::insert_loss(int offset_lo, int offset_hi, const time_point& next_rexmit_time)
{
    // Can't install loss to an empty container
    if (m_PktQueue.empty())
    {
        HLOGC(bslog.Debug, log << "insert_loss: no packets, no loss inserted");
        return false;
    }

    // Fix the indexes if they are out of bound. Note that they can potentially
    // be very far from the point, but any rollovers should be ignored (there's
    // not much we can do about it). Check only if this really is lo-hi relationship
    // and whether at least a fragment of the range is in the buffer.

    if (offset_lo > offset_hi || offset_hi < 0 || offset_lo >= int(m_PktQueue.size()))
    {
        HLOGC(bslog.Debug, log << "insert_loss: invalid offset range " << offset_lo << "..." << offset_hi
                << " with size=" << m_PktQueue.size());
        return false;
    }

    if (offset_lo < 0)
    {
        offset_lo = 0;
    }

    // It was checked that size() is at least 1
    if (offset_hi >= int(m_PktQueue.size()))
    {
        offset_hi = m_PktQueue.size() - 1;
    }

    HLOGC(bslog.Debug, log << "insert_loss: INSERTING offset " << offset_lo << "..." << offset_hi);

    int loss_length = offset_hi - offset_lo + 1;

    // Ok, check now where the position is towards the
    // existing records.
    //
    // First: check if there are no records yet.
    if (m_iFirstRexmit == -1)
    {
        // Add just one record and mark in both.
        SndPktArray::Packet& p = m_PktQueue[offset_lo];
        p.m_iNextLossGroupOffset = 0;
        p.m_iLossLength = loss_length;
        m_iFirstRexmit = m_iLastRexmit = offset_lo;

        m_iLossLengthCache = loss_length;
        update_next_rexmit_time(offset_lo, offset_hi, next_rexmit_time);

        HLOGC(bslog.Debug, log << "insert_loss: 1&1 record: "
                << offset_lo << "..." << offset_hi << " (" << loss_length << " cells)");

        SRT_ASSERT(offset_lo + loss_length <= int(m_PktQueue.size()));

        string msg SRT_ATR_UNUSED;
        SRT_ASSERT(validateLossIntegrity((msg)));
        return true;
    }

    // Now we have at least one element, so we treat this now as
    // a general case, where we need to find the following:
    // - ranges that are before offset_hi
    // - ranges that are after offset_lo
    //    - if no such ranges on any side, set up new_first and new_last
    // All other ranges are "joint"; all need to be removed
    // and a new node should be defined.

    // NOTE: all local variables from here on must have suffix:
    // -  _index : position of a meaningful element
    // -  _shift : relative offset between two indexes
    // -  _end : the past-the-end INDEX value (an element following
    //           the last element in the range)

    int last_node_end = getEndIndex(m_iLastRexmit);
    int offset_end = offset_hi + 1;

    // Step 1: determine the surrounding ranges.
    int before_node_index = -1,// last node disjoint before the current one
        after_node_index = -1, // first node disjoint after the current one
        lowest_inserted_index = offset_lo,
        highest_inserted_index = offset_hi;

    vector<int> removed_node_indexes;

    // 1a. Disjoint preceding/succeeding ranges

    bool outside_disjoint = false, outside_disjoint_front = false;

    if (offset_lo < m_iFirstRexmit)
    {
        // if offset_end == m_iFirstRexmit, they are glued together!
        if (offset_end < m_iFirstRexmit)
        {
            // We have the very first node. So, all nodes are disjoint after.
            after_node_index = m_iFirstRexmit;
            outside_disjoint = true;
            outside_disjoint_front = true;
        }
    }
    else if (offset_hi > last_node_end)
    {
        if (offset_lo > last_node_end)
        {
            before_node_index = m_iLastRexmit;
            outside_disjoint = true;
        }
    }

    // Ok, handle the outside disjoint case now; there's no need
    // to do any looping in this case, just hook up the nodes.
    if (outside_disjoint)
    {
        int extra_length;
        if (outside_disjoint_front)
        {
            int previous_first_index = m_iFirstRexmit;
            m_iFirstRexmit = offset_lo;
            extra_length = setupNode(offset_lo, offset_hi, previous_first_index);
            HLOGC(bslog.Debug, log << "insert_loss: DISJOINT front: [INSERTED] | " << previous_first_index);
        }
        else // outside disjoint back
        {
            int previous_last_index = m_iLastRexmit;
            m_iLastRexmit = offset_lo;
            extra_length = offset_hi - offset_lo + 1;

            HLOGC(bslog.Debug, log << "insert_loss: DISJOINT back: " << previous_last_index << "..."
                    << (getEndIndex(previous_last_index)-1) << " | [INSERTED]");

            // Length remains unchanged; just pin in the new last one.
            m_PktQueue[previous_last_index].m_iNextLossGroupOffset = offset_lo - previous_last_index;
            setupNode(offset_lo, offset_hi);
        }

        update_next_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
        string msg SRT_ATR_UNUSED;
        SRT_ASSERT(validateLossIntegrity((msg)));

        m_iLossLengthCache = m_iLossLengthCache + extra_length;
        return true;
    }

    // Now we need to walk through the elements to separate cases:
    // - PREDECESSOR: node with end < offset_lo
    // - SUCCESSOR: node with first > offset_hi + 1 (offset_end)
    // - OVERLAPPING: nodes (possibly series) that satisfy none of the above.
    // While searching for overlapping you may find, after cutting off
    // all PREDECESSOR, that the next node is a SUCCESSOR. If this is found,
    // it's a MIDDLE-DISJOINT and should be handled in place.

    // The loop doesn't have the same body in both cases, so use a disjoint loop.

    // Immutables:
    // Inserted range is overlapping or sticking to any of the existing ranges.
    // NOTE: embraces cases when:
    // - offset_hi == 2, m_iFirstRexmit == 3 (adjacent)
    // - offset_hi == 0, m_iFirstRexmit == 0 (then it will be 0 >= -1)
    SRT_ASSERT(offset_hi >= m_iFirstRexmit - 1 && offset_lo <= last_node_end);

    int iloss = m_iFirstRexmit, iloss_end;

    // Collect all nodes that precede the inserted one first.
    for (; iloss != -1; iloss = next_loss(iloss))
    {
        iloss_end = getEndIndex(iloss);

        // [iloss ...  ] iloss_end) | offset_lo ...
        if (iloss_end < offset_lo)
        {
            // PREDECESSOR.
            // Continue, but rewrite that as last found such record
            before_node_index = iloss;
            // NOTE: this node stays as is.
        }
        // [offset_lo ... offset_hi] <offset_end> | [iloss ... iloss_end)
        else if (iloss > offset_end)
        {
            // MIDDLE-DISJOINT. 
            // HANDLE IT HERE, as this is also a simple insertion.
            // - before_node_index: the node to which this is inserted as next.
            // - new_next_index: the node inserted to this a next
            int new_next_index = iloss;
            int added_length = setupNode(offset_lo, offset_hi, new_next_index);

            // Should be not possible because a case when m_iFirstRexmit > offset_hi
            // is already handled as "very first" (one of outside_disjoint).
            SRT_ASSERT(before_node_index != -1);

            HLOGC(bslog.Debug, log << "insert_loss: DISJOINT middle: ..."
                    << (getEndIndex(before_node_index)-1) << " | [INSERTED] | " << new_next_index);

            m_PktQueue[before_node_index].m_iNextLossGroupOffset = offset_lo - before_node_index;

            update_next_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
            string msg SRT_ATR_UNUSED;
            SRT_ASSERT(validateLossIntegrity((msg)));

            m_iLossLengthCache = m_iLossLengthCache + added_length;
            return true;
        }
        // [iloss ... | offset_lo ... offset_hi | iloss_end) 
        // or
        // [iloss ... | offset_lo | iloss_end)  ... offset_hi 
        // or
        // offset_lo ... | [iloss ... iloss_end] ... offset_hi
        //
        // We don't care so far where offset_hi is towards the rest of the ranges.
        // That's about to be seen in the next loop continuation.
        else
        {
            // By elimination, this is OVERLAPPING.
            // Stop here and note the earliest index
            lowest_inserted_index = std::min(iloss, offset_lo);
            break;
        }
    }

    // One special case can be handled here: if the newly inserted range
    // is completely covered by the node pointed by iloss.
    if (offset_lo >= iloss && offset_end <= iloss_end)
    {
        HLOGC(bslog.Debug, log << "insert_loss: SWALLOW: " << iloss << "..." << (iloss_end-1));
        // Just update the time for the requested range, but do nothing else.
        // The inserted records completely overlap with the existing ones,
        // so no changes are necessary, except updating the retransmission time.
        update_next_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
        return true;
    }

    // Here we have hit the first node OVERLAPPING with the inserted one,
    // possibly one of the series. Continue looping to find the first
    // following disjoint, if any.
    for (; iloss != -1; iloss = next_loss(iloss))
    {
        if (iloss > offset_end)
        {
            // This may never happen, if there's no following disjoint.
            // This will not happen in the first iteration (handled already).
            after_node_index = iloss;
            break;
        }
        removed_node_indexes.push_back(iloss);

        iloss_end = getEndIndex(iloss);
        highest_inserted_index = std::max(offset_end, iloss_end) - 1;
    }

    // Current situation:
    //
    // [predecessors...; before_node_index...end] | 
    //                     [lowest_inserted_index ... highest_inserted_index] |
    //                                   [after_node_index...end; successors...]
    // If no predecessors, before_node_index == -1.
    // If no successors, after_node_index == -1.

    // 1. remove all nodes qualified as overlapping (even if the first
    // one begins the newly inserted range).
    int removed_length = 0;
    IF_HEAVY_LOGGING(int removed_nodes_n = removed_node_indexes.size());
    for (size_t i = 0; i < removed_node_indexes.size(); ++i)
    {
        int x = removed_node_indexes[i];
        removed_length += m_PktQueue[x].m_iLossLength;
        m_PktQueue[x].m_iLossLength = 0;
        m_PktQueue[x].m_iNextLossGroupOffset = 0;
    }

    // 2. Insert a new node at `lowest_inserted_index` length up to `highest_inserted_index`
    int inserted_length = highest_inserted_index - lowest_inserted_index + 1;

    // We do not insert empty ranges anyway.
    SRT_ASSERT(inserted_length > 0);

    // Could be false, unless we have handled the "swallow" case already.
    SRT_ASSERT(inserted_length > removed_length);

    HLOGC(bslog.Debug, log << "insert_loss: REPLACED " << removed_nodes_n << " nodes with new "
            << lowest_inserted_index << "..." << highest_inserted_index << " FOLLOWS:" << after_node_index
            << " PRECEDES: " << before_node_index << "..." << (getEndIndex(before_node_index)-1));

    m_PktQueue[lowest_inserted_index].m_iLossLength = inserted_length;

    // 6. if `after_node_index`, set it as next to this - otherwise set 0 next
    if (after_node_index != -1)
    {
        m_PktQueue[lowest_inserted_index].m_iNextLossGroupOffset = after_node_index - lowest_inserted_index;
    }
    else
    {
        m_PktQueue[lowest_inserted_index].m_iNextLossGroupOffset = 0;
        // This one is the very last then.
        m_iLastRexmit = lowest_inserted_index;
    }

    // 5. if `before_node_index`, set this one as next to it.
    if (before_node_index != -1)
    {
        m_PktQueue[before_node_index].m_iNextLossGroupOffset = lowest_inserted_index - before_node_index;
    }
    else
    {
        m_iFirstRexmit = lowest_inserted_index;
    }

    // Update the length
    m_iLossLengthCache = m_iLossLengthCache + inserted_length - removed_length;

    // Set the rexmit time only to the range that was requested to be inserted,
    // even if this is effectively a fragment of a record.
    update_next_rexmit_time(offset_lo, offset_hi, next_rexmit_time);
    string msg SRT_ATR_UNUSED;
    SRT_ASSERT(validateLossIntegrity((msg)));
    return true;
}

bool SndPktArray::validateLossIntegrity(std::string& w_message)
{
    if (m_iFirstRexmit == -1)
    {
        w_message = "Only first empty";
        return m_iLastRexmit == -1;
    }

    // Now First is not -1, last must be this one or later
    if (m_iLastRexmit == -1)
    {
        w_message = "Only last empty";
        return false;
    }

    // Ok, both have values, check relationship
    if (m_iFirstRexmit > m_iLastRexmit)
    {
        w_message = "FIRST > LAST inconsistency!";
        return false;
    }

    // Now trace the whole buffer if elements are consistent:
    // - all elements that are not loss nodes, must have len & next = 0
    // - nodes that mark loss area, must have their data > 0, or last should be == 0

    // First, take the easiest part if there's only one loss.
    bool result = true;
    if (m_iFirstRexmit == m_iLastRexmit)
    {
        for (size_t i = 0; i < m_PktQueue.size(); ++i)
        {
            SndPktArray::Packet& p = m_PktQueue[i];
            if (int(i) == m_iFirstRexmit)
            {
                // For this, check if the lenght is > 0 and if
                // if fits in the container, also next must be 0.
                if (p.m_iNextLossGroupOffset != 0
                        || p.m_iLossLength < 1
                        || (p.m_iLossLength + i) > m_PktQueue.size())
                {
                    w_message += "WRONG DATA at (the only) loss position; ";
                    result = false;
                }
                // But still check the others
            }
            else
            {
                // If this isn't the node marking element, all must be 0.
                if (p.m_iNextLossGroupOffset != 0
                        || p.m_iLossLength != 0)
                {
                    w_message += hvu::fmtcat("Non-node element ", i, " has wrong data; ");
                }
            }
        }
        return result;
    }

    // Now trace everything since the beginning, using states.
    PacketShowState st;

    hvu::ofmtbufstream os;

    int last_node = m_iFirstRexmit;
    for (size_t i = 0; i < m_PktQueue.size(); ++i)
    {
        SndPktArray::Packet& p = m_PktQueue[i];

        if (st.next_loss_begin == -1)
        {
            // Before any loss report yet.
            if (int(i) == m_iFirstRexmit)
            {
                // Hit the first one. Check and record.
                // First record must have next because we have handled the single case already
                if (p.m_iLossLength < 1 || p.m_iNextLossGroupOffset < 2)
                {
                    os << "FIRST@multiple hit wrong data: len=" << p.m_iLossLength
                        << " off=" << p.m_iNextLossGroupOffset << " ; ";
                    result = false;
                }

                // Check if exceeds container
                int remaining_length = int(m_PktQueue.size()) - i;
                int next_index = i + p.m_iNextLossGroupOffset;

                if (next_index >= int(m_PktQueue.size()) || p.m_iLossLength > remaining_length)
                {
                    os << "FIRST@multiple: wrong offset data; ";
                    result = false;
                }

                // Still, we caught the first record, so update
                // the state.
                st.next_loss_begin = next_index;
                st.remain_loss_group = p.m_iLossLength; // it includes [i] !
                continue;
            }

            // No data with no previous record yet.
            if (p.m_iLossLength != 0 || p.m_iNextLossGroupOffset != 0)
            {
                os << "WRONG DATA on <first #" << i << "; ";
                result = false;
            }
            continue;
        }

        // We have something in the next loss begin, so we
        // have passed already the first one.

        // Check if the next record was hit.
        if (int(i) == st.next_loss_begin)
        {
            // Can be the last one, but must have at leats 1 length
            if (p.m_iLossLength < 1 || p.m_iNextLossGroupOffset < 0)
            {
                os << "WRONG DATA at #" << i << " found as next loss; ";
                result = false;
            }

            int remaining_length = m_PktQueue.size() - i;
            int next_index = i + p.m_iNextLossGroupOffset;

            if (next_index >= int(m_PktQueue.size()) || p.m_iLossLength > remaining_length)
            {
                os << "AT #" << i << ": wrong offset data; ";
                result = false;
            }

            if (st.remain_loss_group)
            {
                os << "AT #" << i << ": still expected " << st.remain_loss_group << " loss packets; ";
                result = false;
            }

            last_node = i;

            // Still, we caught the first record, so update
            // the state.
            st.next_loss_begin = i + p.m_iNextLossGroupOffset;
            st.remain_loss_group = p.m_iLossLength; // it includes [i] !
            continue;
        }

        if (st.remain_loss_group)
        {
            // update the current one
            --st.remain_loss_group;

            // If the counter has reached 0, this is the
            // first cell past the loss record, but it is
            // still a separation record, so it must be 0 as well.
        }

        // Anyways, we expect here no node data
        if (p.m_iLossLength || p.m_iNextLossGroupOffset)
        {
            os << "AT #" << i << ", group remain " << st.remain_loss_group << ", unexpected nonzero node data; ";
            result = false;
        }
    }

    if (last_node != m_iLastRexmit)
    {
        os << "LAST found " << last_node << " != last=" << m_iLastRexmit << "; ";
        result = false;
    }

    if (!result)
        w_message = os.str();

    return result;

}

int SndPktArray::extractFirstLoss(const duration& miniv)
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

    int last_cleared = -1;

    // Walk over the container to find the valid loss sequence
    for (int loss_begin = m_iFirstRexmit; loss_begin != -1; loss_begin = next_loss(loss_begin))
    {
        int loss_end = loss_begin + m_PktQueue[loss_begin].m_iLossLength;

        for (int i = loss_begin; i != loss_end; ++i)
        {
            SndPktArray::Packet& p = m_PktQueue[i];
            if (!is_zero(p.m_tsNextRexmitTime))
            {
                // Ok, so this cell will be taken, but it might be the future.
                if (!p.updated_rexmit_time_passed(now, miniv))
                {
                    if (stop_revoke == -1 && i > 0)
                        stop_revoke = i - 1;
                    HLOGC(qslog.Debug, log << "... skipped +" << i << " - too early by "
                            << FormatDurationAuto(now + miniv - p.m_tsNextRexmitTime));
                    continue;
                }

                // Clear that packet from being rexmit-eligible.
                p.m_tsNextRexmitTime = time_point();

                if (stop_revoke == -1)
                {
                    HLOGC(qslog.Debug, log << "... FOUND +" << i << " - removing up to this one");
                    remove_loss(i); // Remove all previous loss records, including this one
                }
                else
                {
                    HLOGC(qslog.Debug, log << "... FOUND +" << i << " - removing up to +" << stop_revoke);
                    remove_loss(stop_revoke);
                }
                return i;
            }
            else
            {
                HLOGC(qslog.Debug, log << "... skipped +" << i << " - cleared earlier");
                // This will be done while the loop is searching
                // for the first FILLED record. When hit the first
                // filled record, it will be reported, and all losses
                // up to this one will be cleared. This one is required
                // for a case when occasionally all existing loss entries
                // were selectively cleared.
                last_cleared = i;
            }
            // If it was cleared, continue searching.
        }
    }

    if (last_cleared != -1)
        remove_loss(last_cleared);

    return -1;
}

// Debug support
string SndPktArray::show_external(int32_t seqno) const
{
    using namespace hvu;
    ofmtbufstream out;

    int minw = 2;
    if (size() > 99)
        minw = 3;
    else if (size() > 999)
        minw = 4;

    fmtc findex = fmtc().width(minw).fillzero();

    SndPktArray::PacketShowState st;
    for (size_t i = 0; i < size(); ++i)
    {
        seqno = CSeqNo::incseq(seqno);
        out << "[" << fmt(i, findex) << "]%" << seqno << ": ";
        showline(i, (st), (out));
        out.base() << endl;
    }

    return out.str();
}

void SndPktArray::showline(int index, PacketShowState& st, hvu::ofmtbufstream& out) const
{
    const Packet& p = m_PktQueue[index];

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

    if (p.m_iBusy)
    {
        out << " <" << p.m_iBusy << ">";
    }
}


} // namespace srt
