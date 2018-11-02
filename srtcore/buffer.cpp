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

#include <cstring>
#include <cmath>
#include "buffer.h"
#include "packet.h"
#include "core.h" // provides some constants
#include "logging.h"

using namespace std;

extern logging::Logger mglog, dlog, tslog;

CSndBuffer::CSndBuffer(int size, int mss):
m_BufLock(),
m_pBlock(NULL),
m_pFirstBlock(NULL),
m_pCurrBlock(NULL),
m_pLastBlock(NULL),
m_pBuffer(NULL),
m_iNextMsgNo(1),
m_iSize(size),
m_iMSS(mss),
m_iCount(0)
,m_iBytesCount(0)
,m_ullLastOriginTime_us(0)
#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
,m_LastSamplingTime(0)
,m_iCountMAvg(0)
,m_iBytesCountMAvg(0)
,m_TimespanMAvg(0)
#endif
,m_iInRatePktsCount(0)
,m_iInRateBytesCount(0)
,m_InRateStartTime(0)
,m_InRatePeriod(CUDT::SND_INPUTRATE_FAST_START_US)   // 0.5 sec (fast start)
,m_iInRateBps(CUDT::SND_INPUTRATE_INITIAL_BPS)
,m_iAvgPayloadSz(SRT_LIVE_DEF_PLSIZE)
{
   // initial physical buffer of "size"
   m_pBuffer = new Buffer;
   m_pBuffer->m_pcData = new char [m_iSize * m_iMSS];
   m_pBuffer->m_iSize = m_iSize;
   m_pBuffer->m_pNext = NULL;

   // circular linked list for out bound packets
   m_pBlock = new Block;
   Block* pb = m_pBlock;
   for (int i = 1; i < m_iSize; ++ i)
   {
      pb->m_pNext = new Block;
      pb->m_iMsgNoBitset = 0;
      pb = pb->m_pNext;
   }
   pb->m_pNext = m_pBlock;

   pb = m_pBlock;
   char* pc = m_pBuffer->m_pcData;
   for (int i = 0; i < m_iSize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;
   }

   m_pFirstBlock = m_pCurrBlock = m_pLastBlock = m_pBlock;

   pthread_mutex_init(&m_BufLock, NULL);
}

CSndBuffer::~CSndBuffer()
{
   Block* pb = m_pBlock->m_pNext;
   while (pb != m_pBlock)
   {
      Block* temp = pb;
      pb = pb->m_pNext;
      delete temp;
   }
   delete m_pBlock;

   while (m_pBuffer != NULL)
   {
      Buffer* temp = m_pBuffer;
      m_pBuffer = m_pBuffer->m_pNext;
      delete [] temp->m_pcData;
      delete temp;
   }

   pthread_mutex_destroy(&m_BufLock);
}

void CSndBuffer::addBuffer(const char* data, int len, int ttl, bool order, uint64_t srctime, ref_t<int32_t> r_seqno, ref_t<int32_t> r_msgno)
{
    int32_t& msgno = *r_msgno;
    int32_t& seqno = *r_seqno;

    int size = len / m_iMSS;
    if ((len % m_iMSS) != 0)
        size ++;

    HLOGC(mglog.Debug, log << "addBuffer: size=" << m_iCount << " reserved=" << m_iSize << " needs=" << size << " buffers for " << len << " bytes");

    // dynamically increase sender buffer
    while (size + m_iCount >= m_iSize)
    {
        HLOGC(mglog.Debug, log << "addBuffer: ... still lacking " << (size + m_iCount - m_iSize) << " buffers...");
        increase();
    }

    uint64_t time = CTimer::getTime();
    int32_t inorder = order ? MSGNO_PACKET_INORDER::mask : 0;

    HLOGC(dlog.Debug, log << CONID() << "addBuffer: adding "
        << size << " packets (" << len << " bytes) to send, msgno=" << m_iNextMsgNo
        << (inorder ? "" : " NOT") << " in order");

    // The sequence number passed to this function is the sequence number
    // that the very first packet from the packet series should get here.
    // If there's more than one packet, this function must increase it by itself
    // and then return the accordingly modified sequence number in the reference.

    Block* s = m_pLastBlock;
    msgno = m_iNextMsgNo;
    for (int i = 0; i < size; ++ i)
    {
        int pktlen = len - i * m_iMSS;
        if (pktlen > m_iMSS)
            pktlen = m_iMSS;

        HLOGC(dlog.Debug, log << "addBuffer: %" << seqno << " #" << msgno
                << " spreading from=" << (i*m_iMSS) << " size=" << pktlen
                << " TO BUFFER:" << (void*)s->m_pcData);
        memcpy(s->m_pcData, data + i * m_iMSS, pktlen);
        s->m_iLength = pktlen;

        s->m_iSeqNo = seqno;
        seqno = CSeqNo::incseq(seqno);

        s->m_iMsgNoBitset = m_iNextMsgNo | inorder;
        if (i == 0)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
        if (i == size - 1)
            s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
        // NOTE: if i is neither 0 nor size-1, it resuls with PB_SUBSEQUENT.
        //       if i == 0 == size-1, it results with PB_SOLO. 
        // Packets assigned to one message can be:
        // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST] - 4 packets per message
        // [PB_FIRST] [PB_LAST] - 2 packets per message
        // [PB_SOLO] - 1 packet per message

        s->m_ullSourceTime_us = srctime;
        s->m_ullOriginTime_us = time;
        s->m_iTTL = ttl;

        // XXX unchecked condition: s->m_pNext == NULL.
        // Should never happen, as the call to increase() should ensure enough buffers.
        s = s->m_pNext;
    }
    m_pLastBlock = s;

    CGuard::enterCS(m_BufLock, "Buf");
    m_iCount += size;

    m_iBytesCount += len;
    m_ullLastOriginTime_us = time;

    updInputRate(time, size, len);

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
    updAvgBufSize(time);
#endif

    CGuard::leaveCS(m_BufLock, "Buf");


    // MSGNO_SEQ::mask has a form: 00000011111111...
    // At least it's known that it's from some index inside til the end (to bit 0).
    // If this value has been reached in a step of incrementation, it means that the
    // maximum value has been reached. Casting to int32_t to ensure the same sign
    // in comparison, although it's far from reaching the sign bit.

    m_iNextMsgNo ++;
    if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
        m_iNextMsgNo = 1;
}

void CSndBuffer::setInputRateSmpPeriod(int period)
{
   m_InRatePeriod = (uint64_t)period; //(usec) 0=no input rate calculation
}

void CSndBuffer::updInputRate(uint64_t time, int pkts, int bytes)
{
   if (m_InRatePeriod == 0)
      ;//no input rate calculation
   else if (m_InRateStartTime == 0)
      m_InRateStartTime = time;
   else
   {
      m_iInRatePktsCount += pkts;
      m_iInRateBytesCount += bytes;
      if ((time - m_InRateStartTime) > m_InRatePeriod) {
         //Payload average size
         m_iAvgPayloadSz = m_iInRateBytesCount / m_iInRatePktsCount;
         //Required Byte/sec rate (payload + headers)
         m_iInRateBytesCount += (m_iInRatePktsCount * CPacket::SRT_DATA_HDR_SIZE);
         m_iInRateBps = (int)(((int64_t)m_iInRateBytesCount * 1000000) / (time - m_InRateStartTime));

         HLOGC(dlog.Debug, log << "updInputRate: pkts:" << m_iInRateBytesCount << " bytes:" << m_iInRatePktsCount
                 << " avg=" << m_iAvgPayloadSz << " rate=" << (m_iInRateBps*8)/1000
                 << "kbps interval=" << (time - m_InRateStartTime));

         m_iInRatePktsCount = 0;
         m_iInRateBytesCount = 0;
         m_InRateStartTime = time;
      }
   }
}

int CSndBuffer::getInputRate(ref_t<int> r_payloadsz, ref_t<uint64_t> r_period)
{
    int& payloadsz = *r_payloadsz;
    uint64_t& period = *r_period;
    uint64_t time = CTimer::getTime();

    if ((m_InRatePeriod != 0)
            &&  (m_InRateStartTime != 0) 
            &&  ((time - m_InRateStartTime) > m_InRatePeriod))
    {
        //Packet size with headers
        if (m_iInRatePktsCount == 0)
            m_iAvgPayloadSz = 0;
        else
            m_iAvgPayloadSz = m_iInRateBytesCount / m_iInRatePktsCount;

        //include packet headers: SRT + UDP + IP
        int64_t llBytesCount = (int64_t)m_iInRateBytesCount + (m_iInRatePktsCount * (CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE));
        //Byte/sec rate
        m_iInRateBps = (int)((llBytesCount * 1000000) / (time - m_InRateStartTime));
        m_iInRatePktsCount = 0;
        m_iInRateBytesCount = 0;
        m_InRateStartTime = time;
    }
    payloadsz = m_iAvgPayloadSz;
    period = m_InRatePeriod;
    return(m_iInRateBps);
}

int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   HLOGC(mglog.Debug, log << "addBufferFromFile: size=" << m_iCount << " reserved=" << m_iSize << " needs=" << size << " buffers for " << len << " bytes");

   // dynamically increase sender buffer
   while (size + m_iCount >= m_iSize)
   {
      HLOGC(mglog.Debug, log << "addBufferFromFile: ... still lacking " << (size + m_iCount - m_iSize) << " buffers...");
      increase();
   }

   HLOGC(dlog.Debug, log << CONID() << "addBufferFromFile: adding "
       << size << " packets (" << len << " bytes) to send, msgno=" << m_iNextMsgNo);

   Block* s = m_pLastBlock;
   int total = 0;
   for (int i = 0; i < size; ++ i)
   {
      if (ifs.bad() || ifs.fail() || ifs.eof())
         break;

      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;

      HLOGC(dlog.Debug, log << "addBufferFromFile: reading from=" << (i*m_iMSS) << " size=" << pktlen << " TO BUFFER:" << (void*)s->m_pcData);
      ifs.read(s->m_pcData, pktlen);
      if ((pktlen = int(ifs.gcount())) <= 0)
         break;

      // currently file transfer is only available in streaming mode, message is always in order, ttl = infinite
      s->m_iMsgNoBitset = m_iNextMsgNo | MSGNO_PACKET_INORDER::mask;
      if (i == 0)
         s->m_iMsgNoBitset |= PacketBoundaryBits(PB_FIRST);
      if (i == size - 1)
         s->m_iMsgNoBitset |= PacketBoundaryBits(PB_LAST);
      // NOTE: PB_FIRST | PB_LAST == PB_SOLO.
      // none of PB_FIRST & PB_LAST == PB_SUBSEQUENT.

      s->m_iLength = pktlen;
      s->m_iTTL = -1;
      s = s->m_pNext;

      total += pktlen;
   }
   m_pLastBlock = s;

   CGuard::enterCS(m_BufLock, "Buf");
   m_iCount += size;
   m_iBytesCount += total;

   CGuard::leaveCS(m_BufLock, "Buf");

   m_iNextMsgNo ++;
   if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
      m_iNextMsgNo = 1;

   return total;
}

int CSndBuffer::readData(ref_t<CPacket> r_packet, ref_t<uint64_t> srctime, int kflgs)
{
   // No data to read
   if (m_pCurrBlock == m_pLastBlock)
      return 0;

   // Make the packet REFLECT the data stored in the buffer.
   r_packet.get().m_pcData = m_pCurrBlock->m_pcData;
   int readlen = m_pCurrBlock->m_iLength;
   r_packet.get().setLength(readlen);
   r_packet.get().m_iSeqNo = m_pCurrBlock->m_iSeqNo;

   // XXX This is probably done because the encryption should happen
   // just once, and so this sets the encryption flags to both msgno bitset
   // IN THE PACKET and IN THE BLOCK. This is probably to make the encryption
   // happen at the time when scheduling a new packet to send, but the packet
   // must remain in the send buffer until it's ACKed. For the case of rexmit
   // the packet will be taken "as is" (that is, already encrypted).
   //
   // The problem is in the order of things:
   // 0. When the application stores the data, some of the flags for PH_MSGNO are set.
   // 1. The readData() is called to get the original data sent by the application.
   // 2. The data are original and must be encrypted. They WILL BE encrypted, later.
   // 3. So far we are in readData() so the encryption flags must be updated NOW because
   //    later we won't have access to the block's data.
   // 4. After exiting from readData(), the packet is being encrypted. It's immediately
   //    sent, however the data must remain in the sending buffer until they are ACKed.
   // 5. In case when rexmission is needed, the second overloaded version of readData
   //    is being called, and the buffer + PH_MSGNO value is extracted. All interesting
   //    flags must be present and correct at that time.
   //
   // The only sensible way to fix this problem is to encrypt the packet not after
   // extracting from here, but when the packet is stored into CSndBuffer. The appropriate
   // flags for PH_MSGNO will be applied directly there. Then here the value for setting
   // PH_MSGNO will be set as is.

   if (kflgs == -1)
   {
       HLOGC(dlog.Debug, log << CONID() << " CSndBuffer: ERROR: encryption required and not possible. NOT SENDING.");
       readlen = 0;
   }
   else
   {
       m_pCurrBlock->m_iMsgNoBitset |= MSGNO_ENCKEYSPEC::wrap(kflgs);
   }
   r_packet.get().m_iMsgNo = m_pCurrBlock->m_iMsgNoBitset;

   *srctime =
      m_pCurrBlock->m_ullSourceTime_us ? m_pCurrBlock->m_ullSourceTime_us :
      m_pCurrBlock->m_ullOriginTime_us;

   m_pCurrBlock = m_pCurrBlock->m_pNext;

   HLOGC(dlog.Debug, log << CONID() << "CSndBuffer: extracting packet size=" << readlen << " to send");

   return readlen;
}

int CSndBuffer::readData(const int offset, ref_t<CPacket> r_packet, ref_t<uint64_t> r_srctime, ref_t<int> r_msglen)
{
   int32_t& msgno_bitset = r_packet.get().m_iMsgNo;
   uint64_t& srctime = *r_srctime;
   int& msglen = *r_msglen;

   CGuard bufferguard(m_BufLock, "Buf");

   Block* p = m_pFirstBlock;

   // XXX Suboptimal procedure to keep the blocks identifiable
   // by sequence number. Consider using some circular buffer.
   for (int i = 0; i < offset; ++ i)
      p = p->m_pNext;

   // Check if the block that is the next candidate to send (m_pCurrBlock pointing) is stale.

   // If so, then inform the caller that it should first take care of the whole
   // message (all blocks with that message id). Shift the m_pCurrBlock pointer
   // to the position past the last of them. Then return -1 and set the
   // msgno_bitset return reference to the message id that should be dropped as
   // a whole.

   // After taking care of that, the caller should immediately call this function again,
   // this time possibly in order to find the real data to be sent.

   // if found block is stale
   // (This is for messages that have declared TTL - messages that fail to be sent
   // before the TTL defined time comes, will be dropped).
   if ((p->m_iTTL >= 0) && ((CTimer::getTime() - p->m_ullOriginTime_us) / 1000 > (uint64_t)p->m_iTTL))
   {
      int32_t msgno = p->getMsgSeq();
      msglen = 1;
      p = p->m_pNext;
      bool move = false;
      while (msgno == p->getMsgSeq())
      {
         if (p == m_pCurrBlock)
            move = true;
         p = p->m_pNext;
         if (move)
            m_pCurrBlock = p;
         msglen ++;
      }

      HLOGC(dlog.Debug, log << "CSndBuffer::readData: due to TTL exceeded, " << msglen << " messages to drop, up to " << msgno);

      // If readData returns -1, then msgno_bitset is understood as a Message ID to drop.
      // This means that in this case it should be written by the message sequence value only
      // (not the whole 4-byte bitset written at PH_MSGNO).
      msgno_bitset = msgno;
      return -1;
   }

   r_packet.get().m_pcData = p->m_pcData;
   int readlen = p->m_iLength;
   r_packet.get().setLength(readlen);

   // XXX Here the value predicted to be applied to PH_MSGNO field is extracted.
   // As this function is predicted to extract the data to send as a rexmited packet,
   // the packet must be in the form ready to send - so, in case of encryption,
   // encrypted, and with all ENC flags already set. So, the first call to send
   // the packet originally (the other overload of this function) must set these
   // flags.
   r_packet.get().m_iMsgNo = p->m_iMsgNoBitset;

   srctime = 
      p->m_ullSourceTime_us ? p->m_ullSourceTime_us :
      p->m_ullOriginTime_us;

   HLOGC(dlog.Debug, log << CONID() << "CSndBuffer: getting packet %"
           << p->m_iSeqNo << " as per %" << r_packet.get().m_iSeqNo
           << " size=" << readlen << " to send [REXMIT]");

   return readlen;
}

void CSndBuffer::ackData(int offset)
{
   CGuard bufferguard(m_BufLock, "Buf");

   bool move = false;
   for (int i = 0; i < offset; ++ i)
   {
      m_iBytesCount -= m_pFirstBlock->m_iLength;
      if (m_pFirstBlock == m_pCurrBlock)
          move = true;
      m_pFirstBlock = m_pFirstBlock->m_pNext;
   }
   if (move)
       m_pCurrBlock = m_pFirstBlock;

   m_iCount -= offset;

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
   updAvgBufSize(CTimer::getTime());
#endif

   CTimer::triggerEvent();
}

int CSndBuffer::getCurrBufSize() const
{
   return m_iCount;
}

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG

int CSndBuffer::getAvgBufSize(ref_t<int> r_bytes, ref_t<int> r_tsp)
{
    int& bytes = *r_bytes;
    int& timespan = *r_tsp;
    CGuard bufferguard(m_BufLock, "Buf"); /* Consistency of pkts vs. bytes vs. spantime */

    /* update stats in case there was no add/ack activity lately */
    updAvgBufSize(CTimer::getTime());

    bytes = m_iBytesCountMAvg;
    timespan = m_TimespanMAvg;
    return(m_iCountMAvg);
}

void CSndBuffer::updAvgBufSize(uint64_t now)
{
   uint64_t elapsed = (now - m_LastSamplingTime) / 1000; //ms since last sampling

   if ((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 > elapsed)
      return;

   if (1000000 < elapsed)
   {
      /* No sampling in last 1 sec, initialize average */
      m_iCountMAvg = getCurrBufSize(Ref(m_iBytesCountMAvg), Ref(m_TimespanMAvg));
      m_LastSamplingTime = now;
   } 
   else //((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 <= elapsed)
   {
      /*
      * weight last average value between -1 sec and last sampling time (LST)
      * and new value between last sampling time and now
      *                                      |elapsed|
      *   +----------------------------------+-------+
      *  -1                                 LST      0(now)
      */
      int instspan;
      int bytescount;
      int count = getCurrBufSize(Ref(bytescount), Ref(instspan));

      HLOGC(dlog.Debug, log << "updAvgBufSize: " << elapsed
              << ": " << count << " " << bytescount
              << " " << instspan << "ms");

      m_iCountMAvg      = (int)(((count      * (1000 - elapsed)) + (count      * elapsed)) / 1000);
      m_iBytesCountMAvg = (int)(((bytescount * (1000 - elapsed)) + (bytescount * elapsed)) / 1000);
      m_TimespanMAvg    = (int)(((instspan   * (1000 - elapsed)) + (instspan   * elapsed)) / 1000);
      m_LastSamplingTime = now;
   }
}

#endif /* SRT_ENABLE_SNDBUFSZ_MAVG */

int CSndBuffer::getCurrBufSize(ref_t<int> bytes, ref_t<int> timespan)
{
   *bytes = m_iBytesCount;
   /* 
   * Timespan can be less then 1000 us (1 ms) if few packets. 
   * Also, if there is only one pkt in buffer, the time difference will be 0.
   * Therefore, always add 1 ms if not empty.
   */
   *timespan = 0 < m_iCount ? int((m_ullLastOriginTime_us - m_pFirstBlock->m_ullOriginTime_us) / 1000) + 1 : 0;

   return m_iCount;
}

int CSndBuffer::dropLateData(int &bytes, uint64_t latetime)
{
   int dpkts = 0;
   int dbytes = 0;
   bool move = false;

   CGuard bufferguard(m_BufLock, "Buf");
   for (int i = 0; i < m_iCount && m_pFirstBlock->m_ullOriginTime_us < latetime; ++ i)
   {
      dpkts++;
      dbytes += m_pFirstBlock->m_iLength;

      if (m_pFirstBlock == m_pCurrBlock) move = true;
      m_pFirstBlock = m_pFirstBlock->m_pNext;
   }
   if (move) m_pCurrBlock = m_pFirstBlock;
   m_iCount -= dpkts;

   m_iBytesCount -= dbytes;
   bytes = dbytes;

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
   updAvgBufSize(CTimer::getTime());
#endif /* SRT_ENABLE_SNDBUFSZ_MAVG */

// CTimer::triggerEvent();
   return(dpkts);
}

void CSndBuffer::increase()
{
   int unitsize = m_pBuffer->m_iSize;

   // new physical buffer
   Buffer* nbuf = NULL;
   try
   {
      nbuf  = new Buffer;
      nbuf->m_pcData = new char [unitsize * m_iMSS];
   }
   catch (...)
   {
      delete nbuf;
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
   }
   nbuf->m_iSize = unitsize;
   nbuf->m_pNext = NULL;

   // insert the buffer at the end of the buffer list
   Buffer* p = m_pBuffer;
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
   for (int i = 1; i < unitsize; ++ i)
   {
      pb->m_pNext = new Block;
      pb = pb->m_pNext;
   }

   // insert the new blocks onto the existing one
   pb->m_pNext = m_pLastBlock->m_pNext;
   m_pLastBlock->m_pNext = nblk;

   pb = nblk;
   char* pc = nbuf->m_pcData;
   for (int i = 0; i < unitsize; ++ i)
   {
      pb->m_pcData = pc;
      pb = pb->m_pNext;
      pc += m_iMSS;
   }

   m_iSize += unitsize;

   HLOGC(dlog.Debug, log << "CSndBuffer: BUFFER FULL - adding " << (unitsize*m_iMSS) << " bytes spread to " << unitsize << " blocks"
       << " (total size: " << m_iSize << " bytes)");

}

////////////////////////////////////////////////////////////////////////////////

/*
*   RcvBuffer (circular buffer):
*
*   |<------------------- m_iSize ----------------------------->|
*   |       |<--- acked pkts -->|<--- m_iMaxPos --->|           |
*   |       |                   |                   |           |
*   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
*   | 0 | 0 | 1 | 1 | 1 | 0 | 1 | 1 | 1 | 1 | 0 | 1 | 0 |...| 0 | m_pUnit[]
*   +---+---+---+---+---+---+---+---+---+---+---+---+---+   +---+
*             |                 | |               |
*             |                   |               \__last pkt received
*             |                   \___ m_iLastAckPos: last ack sent
*             \___ m_iStartPos: first message to read
*                      
*   m_pUnit[i]->m_iFlag: 0:free, 1:good, 2:passack, 3:dropped
* 
*   thread safety:
*    m_iStartPos:   CUDT::m_RecvLock 
*    m_iLastAckPos: CUDT::m_AckLock 
*    m_iMaxPos:     none? (modified on add and ack
*/


// XXX Init values moved to in-class.
//const uint32_t CRcvBuffer::TSBPD_WRAP_PERIOD = (30*1000000);    //30 seconds (in usec)
//const int CRcvBuffer::TSBPD_DRIFT_MAX_VALUE   = 5000;  // usec
//const int CRcvBuffer::TSBPD_DRIFT_MAX_SAMPLES = 1000;  // ACK-ACK packets
#ifdef SRT_DEBUG_TSBPD_DRIFT
//const int CRcvBuffer::TSBPD_DRIFT_PRT_SAMPLES = 200;   // ACK-ACK packets
#endif

CRcvBuffer::CRcvBuffer(CUnitQueue* queue, int bufsize):
m_pUnit(NULL),
m_iSize(bufsize),
m_pUnitQueue(queue),
m_iStartPos(0),
m_iLastAckPos(0),
m_iMaxPos(0),
m_iNotch(0)
,m_BytesCountLock()
,m_iBytesCount(0)
,m_iAckedPktsCount(0)
,m_iAckedBytesCount(0)
,m_iAvgPayloadSz(7*188)
,m_bTsbPdMode(false)
,m_uTsbPdDelay(0)
,m_ullTsbPdTimeBase(0)
,m_bTsbPdWrapCheck(false)
//,m_iTsbPdDrift(0)
//,m_TsbPdDriftSum(0)
//,m_iTsbPdDriftNbSamples(0)
#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
,m_LastSamplingTime(0)
,m_TimespanMAvg(0)
,m_iCountMAvg(0)
,m_iBytesCountMAvg(0)
#endif
{
   m_pUnit = new CUnit* [m_iSize];
   for (int i = 0; i < m_iSize; ++ i)
      m_pUnit[i] = NULL;

#ifdef SRT_DEBUG_TSBPD_DRIFT
   memset(m_TsbPdDriftHisto100us, 0, sizeof(m_TsbPdDriftHisto100us));
   memset(m_TsbPdDriftHisto1ms, 0, sizeof(m_TsbPdDriftHisto1ms));
#endif

   pthread_mutex_init(&m_BytesCountLock, NULL);
}

CRcvBuffer::~CRcvBuffer()
{
   for (int i = 0; i < m_iSize; ++ i)
   {
      if (m_pUnit[i] != NULL)
      {
         m_pUnit[i]->m_iFlag = CUnit::FREE;
         -- m_pUnitQueue->m_iCount;
      }
   }

   delete [] m_pUnit;

   pthread_mutex_destroy(&m_BytesCountLock);
}

void CRcvBuffer::countBytes(int pkts, int bytes, bool acked)
{
   /*
   * Byte counter changes from both sides (Recv & Ack) of the buffer
   * so the higher level lock is not enough for thread safe op.
   *
   * pkts are...
   *  added (bytes>0, acked=false),
   *  acked (bytes>0, acked=true),
   *  removed (bytes<0, acked=n/a)
   */
   CGuard cg(m_BytesCountLock, "BytesCount");

   if (!acked) //adding new pkt in RcvBuffer
   {
       m_iBytesCount += bytes; /* added or removed bytes from rcv buffer */
       if (bytes > 0) /* Assuming one pkt when adding bytes */
          m_iAvgPayloadSz = ((m_iAvgPayloadSz * (100 - 1)) + bytes) / 100; 
   }
   else // acking/removing pkts to/from buffer
   {
       m_iAckedPktsCount += pkts; /* acked or removed pkts from rcv buffer */
       m_iAckedBytesCount += bytes; /* acked or removed bytes from rcv buffer */

       if (bytes < 0) m_iBytesCount += bytes; /* removed bytes from rcv buffer */
   }
}

int CRcvBuffer::addData(CUnit* unit, int offset)
{
   int pos = (m_iLastAckPos + offset) % m_iSize;
   if (offset >= m_iMaxPos)
      m_iMaxPos = offset + 1;

   if (m_pUnit[pos] != NULL) {
      return -1;
   }
   m_pUnit[pos] = unit;
   countBytes(1, unit->m_Packet.getLength());

   unit->m_iFlag = CUnit::GOOD;
   ++ m_pUnitQueue->m_iCount;

   return 0;
}

int CRcvBuffer::readBuffer(char* data, int len)
{
   int p = m_iStartPos;
   int lastack = m_iLastAckPos;
   int rs = len;
#if ENABLE_HEAVY_LOGGING
   char* begin = data;
#endif

   uint64_t now = (m_bTsbPdMode ? CTimer::getTime() : uint64_t());

   HLOGC(dlog.Debug, log << CONID() << "readBuffer: start=" << p << " lastack=" << lastack);
   while ((p != lastack) && (rs > 0))
   {
      if (m_bTsbPdMode)
      {
          HLOGC(dlog.Debug, log << CONID() << "readBuffer: chk if time2play: NOW=" << now << " PKT TS=" << getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp()));
          if ((getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp()) > now))
              break; /* too early for this unit, return whatever was copied */
      }

      int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      if (unitsize > rs)
         unitsize = rs;

      HLOGC(dlog.Debug, log << CONID() << "readBuffer: copying buffer #" << p
          << " targetpos=" << int(data-begin) << " sourcepos=" << m_iNotch << " size=" << unitsize << " left=" << (unitsize-rs));
      memcpy(data, m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      data += unitsize;

      if ((rs > unitsize) || (rs == int(m_pUnit[p]->m_Packet.getLength()) - m_iNotch))
      {
          freeUnitAt(p);
          p = shift_forward(p);

         m_iNotch = 0;
      }
      else
         m_iNotch += rs;

      rs -= unitsize;
   }

   /* we removed acked bytes form receive buffer */
   countBytes(-1, -(len - rs), true);
   m_iStartPos = p;

   return len - rs;
}

int CRcvBuffer::readBufferToFile(fstream& ofs, int len)
{
   int p = m_iStartPos;
   int lastack = m_iLastAckPos;
   int rs = len;

   while ((p != lastack) && (rs > 0))
   {
      int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      if (unitsize > rs)
         unitsize = rs;

      ofs.write(m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      if (ofs.fail())
         break;

      if ((rs > unitsize) || (rs == int(m_pUnit[p]->m_Packet.getLength()) - m_iNotch))
      {
         freeUnitAt(p);

         p = shift_forward(p);

         m_iNotch = 0;
      }
      else
         m_iNotch += rs;

      rs -= unitsize;
   }

   /* we removed acked bytes form receive buffer */
   countBytes(-1, -(len - rs), true);
   m_iStartPos = p;

   return len - rs;
}

void CRcvBuffer::ackData(int len)
{
   int end = shift(m_iLastAckPos, len);
   {
      int pkts = 0;
      int bytes = 0;
      for (int i = m_iLastAckPos; i != end; i = shift_forward(i))
      {
          if (m_pUnit[i] != NULL)
          {
              pkts++;
              bytes += m_pUnit[i]->m_Packet.getLength();
          }
      }
      if (pkts > 0) countBytes(pkts, bytes, true);
   }

   HLOGC(mglog.Debug, log << "ackData: shift by " << len << ", start=" << m_iStartPos
           << " end=" << m_iLastAckPos << " -> " << end);

   m_iLastAckPos = end;
   m_iMaxPos -= len;
   if (m_iMaxPos < 0)
      m_iMaxPos = 0;
}

void CRcvBuffer::skipData(int len)
{
   /* 
   * Caller need protect both AckLock and RecvLock
   * to move both m_iStartPos and m_iLastAckPost
   */
   if (m_iStartPos == m_iLastAckPos)
      m_iStartPos = (m_iStartPos + len) % m_iSize;
   m_iLastAckPos = (m_iLastAckPos + len) % m_iSize;
   m_iMaxPos -= len;
   if (m_iMaxPos < 0)
      m_iMaxPos = 0;
}

size_t CRcvBuffer::dropData(int len)
{
    // This function does the same as skipData, although skipData
    // should work in the condition of absence of data, so no need
    // to force the units in the range to be freed. This function
    // works in more general condition where we don't know if there
    // are any data in the given range, but want to remove these
    // "sequence positions" from the buffer, whether there are data
    // at them or not.

    size_t stats_bytes = 0;

    int p = m_iStartPos;
    int past_q = shift(p, len);
    while (p != past_q)
    {
        if (m_pUnit[p] && m_pUnit[p]->m_iFlag == CUnit::GOOD)
        {
            stats_bytes += m_pUnit[p]->m_Packet.getLength();
            freeUnitAt(p);
        }

        p = shift_forward(p);
    }

    m_iStartPos = past_q;
    return stats_bytes;
}

bool CRcvBuffer::getRcvFirstMsg(ref_t<uint64_t> r_tsbpdtime, ref_t<bool> r_passack, ref_t<int32_t> r_skipseqno, ref_t<int32_t> r_curpktseq)
{
    int32_t& skipseqno = *r_skipseqno;
    bool& passack = *r_passack;
    skipseqno = -1;
    passack = false;
    // tsbpdtime will be retrieved by the below call
    // Returned values:
    // - tsbpdtime: real time when the packet is ready to play (whether ready to play or not)
    // - passack: false (the report concerns a packet with an exactly next sequence)
    // - skipseqno == -1: no packets to skip towards the first RTP
    // - curpktseq: sequence number for reported packet (for debug purposes)
    // - @return: whether the reported packet is ready to play

    /* Check the acknowledged packets */

    // getRcvReadyMsg returns true if the time to play for the first message
    // (returned in r_tsbpdtime) is in the past.
    if (getRcvReadyMsg(r_tsbpdtime, r_curpktseq, -1))
    {
        return true;
    }
    else if (*r_tsbpdtime != 0)
    {
        // This means that a message next to be played, has been found,
        // but the time to play is in future.
        return false;
    }

    // Falling here means that there are NO PACKETS in the ACK-ed region
    // (m_iStartPos - m_iLastAckPos), but we may have something in the
    // region (m_iLastAckPos - (m_iLastAckPos+m_iMaxPos)), that is, packets
    // that may be separated from the last ACK-ed by lost ones.

    // Below this line we have only two options:
    // - m_iMaxPos == 0, which means that no more packets are in the buffer
    //    - returned: tsbpdtime=0, passack=true, skipseqno=-1, curpktseq=0, @return false
    // - m_iMaxPos > 0, which means that there are packets arrived after a lost packet:
    //    - returned: tsbpdtime=PKT.TS, passack=true, skipseqno=PKT.SEQ, ppkt=PKT, @return LOCAL(PKT.TS) <= NOW

    /* 
     * No acked packets ready but caller want to know next packet to wait for
     * Check the not yet acked packets that may be stuck by missing packet(s).
     */
    bool haslost = false;
    *r_tsbpdtime = 0; // redundant, for clarity
    passack = true;

    // XXX SUSPECTED ISSUE with this algorithm:
    // The above call to getRcvReadyMsg() should report as to whether:
    // - there is an EXACTLY NEXT SEQUENCE packet
    // - this packet is ready to play.
    //
    // Situations handled after the call are when:
    // - there's the next sequence packet available and it is ready to play
    // - there are no packets at all, ready to play or not
    //
    // So, the remaining situation is that THERE ARE PACKETS that follow
    // the current sequence, but they are not ready to play. This includes
    // packets that have the exactly next sequence and packets that jump
    // over a lost packet.
    //
    // As the getRcvReadyMsg() function walks through the incoming units
    // to see if there's anything that satisfies these conditions, it *SHOULD*
    // be also capable of checking if the next available packet, if it is
    // there, is the next sequence packet or not. Retrieving this exactly
    // packet would be most useful, as the test for play-readiness and
    // sequentiality can be done on it directly.
    //
    // When done so, the below loop would be completely unnecessary.

    // Logical description of the below algorithm:
    // 1. Check if the VERY FIRST PACKET is valid; if so then:
    //    - check if it's ready to play, return boolean value that marks it.

    for (int i = m_iLastAckPos, n = shift(m_iLastAckPos, m_iMaxPos); i != n; i = shift_forward(i))
    {
        if ( !m_pUnit[i]
                || m_pUnit[i]->m_iFlag != CUnit::GOOD )
        {
            /* There are packets in the sequence not received yet */
            haslost = true;
        }
        else
        {
            /* We got the 1st valid packet */
            *r_tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
            if (*r_tsbpdtime <= CTimer::getTime())
            {
                /* Packet ready to play */
                if (haslost)
                {
                    /* 
                     * Packet stuck on non-acked side because of missing packets.
                     * Tell 1st valid packet seqno so caller can skip (drop) the missing packets.
                     */
                    skipseqno = m_pUnit[i]->m_Packet.m_iSeqNo;
                    *r_curpktseq = skipseqno;
                }

                // NOTE: if haslost is not set, it means that this is the VERY FIRST
                // packet, that is, packet currently at pos = m_iLastAckPos. There's no
                // possibility that it is so otherwise because:
                // - if this first good packet is ready to play, THIS HERE RETURNS NOW.
                // ...
                return true;
            }
            // ... and if this first good packet WASN'T ready to play, THIS HERE RETURNS NOW, TOO,
            // just states that there's no ready packet to play.
            // ...
            return false;
        }
        // ... and if this first packet WASN'T GOOD, the loop continues, however since now
        // the 'haslost' is set, which means that it continues only to find the first valid
        // packet after stating that the very first packet isn't valid.
    }
    return false;
}

bool CRcvBuffer::getRcvReadyMsg(ref_t<uint64_t> r_tsbpdtime, ref_t<int32_t> curpktseq, int upto)
{
    *r_tsbpdtime = 0;
    int rmpkts = 0;
    int rmbytes = 0;
    bool havelimit = upto != -1;
    int end = -1, past_end = -1;
    if (havelimit)
    {
        int stretch = (m_iSize + m_iStartPos - m_iLastAckPos) % m_iSize;
        if (upto > stretch)
        {
            HLOGC(dlog.Debug, log << "position back " << upto << " exceeds stretch " << stretch);
            // Do nothing. This position is already gone.
            return false;
        }

        end = m_iLastAckPos - upto;
        if (end < 0)
            end += m_iSize;
        past_end = shift_forward(end); // For in-loop comparison
        HLOGC(dlog.Debug, log << "getRcvReadyMsg: will read from position " << end);
    }

    // NOTE: position m_iLastAckPos in the buffer represents the sequence number of
    // CUDT::m_iRcvLastSkipAck. Therefore 'upto' contains a positive value that should
    // be decreased from m_iLastAckPos to get the position in the buffer that represents
    // the sequence number up to which we'd like to read.

    string reason = "NOT RECEIVED";
    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = shift_forward(i))
    {
        // In case when we want to read only up to given sequence number, stop
        // the loop if this number was reached. This number must be extracted from
        // the buffer and any following must wait here for "better times". Note
        // that the unit that points to the requested sequence must remain in
        // the buffer, unless there is no valid packet at that position, in which
        // case it is allowed to point to the NEXT sequence towards it, however
        // if it does, this cell must remain in the buffer for prospective recovery.
        if (havelimit && i == past_end)
            break;

        bool freeunit = false;

        /* Skip any invalid skipped/dropped packets */
        if (m_pUnit[i] == NULL)
        {
            m_iStartPos = shift_forward(m_iStartPos);
            continue;
        }

        *curpktseq = m_pUnit[i]->m_Packet.getSeqNo();

        if (m_pUnit[i]->m_iFlag != CUnit::GOOD)
        {
            freeunit = true;
        }
        else
        {
            // This does:
            // 1. Get the TSBPD time of the unit. Stop and return false if this unit
            //    is not yet ready to play.
            // 2. If it's ready to play, check also if it's decrypted. If not, skip it.
            // 3. If it's ready to play and decrypted, stop and return it.
            if (!havelimit)
            {
                *r_tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
                int64_t towait = (*r_tsbpdtime - CTimer::getTime());
                if (towait > 0)
                {
                    HLOGC(mglog.Debug, log << "getRcvReadyMsg: found packet, but not ready to play (only in " << (towait/1000.0) << "ms)");
                    return false;
                }

                if (m_pUnit[i]->m_Packet.getMsgCryptoFlags() != EK_NOENC)
                {
                    reason = "DECRYPTION FAILED";
                    freeunit = true; /* packet not decrypted */
                }
                else
                {
                    HLOGC(mglog.Debug, log << "getRcvReadyMsg: packet seq=" << curpktseq.get() << " ready to play (delayed " << (-towait/1000.0) << "ms)");
                    return true;
                }
            }
            // In this case:
            // 1. We don't even look into the packet if this is not the requested sequence.
            //    All packets that are earlier than the required sequence will be dropped.
            // 2. When found the packet with expected sequence number, and the condition for
            //    good unit is passed, we get the timestamp.
            // 3. If the packet is not decrypted, we allow it to be removed
            // 4. If we reached the required sequence, and the packet is good, KEEP IT in the buffer,
            //    and return with the pointer pointing to this very buffer. Only then return true.
            else
            {
                // We have a limit up to which the reading will be done,
                // no matter if the time has come or not - although retrieve it.
                if (i == end)
                {
                    HLOGC(dlog.Debug, log << "CAUGHT required seq position " << i);
                    // We have the packet we need. Extract its data.
                    *r_tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());

                    // If we have a decryption failure, allow the unit to be released.
                    if (m_pUnit[i]->m_Packet.getMsgCryptoFlags() != EK_NOENC)
                    {
                        reason = "DECRYPTION FAILED";
                        freeunit = true; /* packet not decrypted */
                    }
                    else
                    {
                        // Stop here and keep the packet in the buffer, so it will be
                        // next extracted.
                        HLOGC(mglog.Debug, log << "getRcvReadyMsg: packet seq=" << curpktseq.get() << " ready for extraction");
                        return true;
                    }
                }
                else
                {
                    HLOGC(dlog.Debug, log << "SKIPPING position " << i);
                    // Continue the loop and remove the current packet because
                    // its sequence number is too old.
                    freeunit = true;
                }
            }
        }

        if (freeunit)
        {
            rmpkts++;
            rmbytes += freeUnitAt(i);
            m_iStartPos = shift_forward(m_iStartPos);
        }
    }

    HLOGC(mglog.Debug, log << "getRcvReadyMsg: nothing to deliver: " << reason);
    /* removed skipped, dropped, undecryptable bytes from rcv buffer */
    countBytes(-rmpkts, -rmbytes, true);
    return false;
}


/*
* Return receivable data status (packet timestamp ready to play if TsbPd mode)
* Return playtime (tsbpdtime) of 1st packet in queue, ready to play or not
*
* Return data ready to be received (packet timestamp ready to play if TsbPd mode)
* Using getRcvDataSize() to know if there is something to read as it was widely
* used in the code (core.cpp) is expensive in TsbPD mode, hence this simpler function
* that only check if first packet in queue is ready.
*/
bool CRcvBuffer::isRcvDataReady(ref_t<uint64_t> tsbpdtime, ref_t<int32_t> curpktseq, int32_t seqdistance)
{
   *tsbpdtime = 0;

   if (m_bTsbPdMode)
   {
       CPacket* pkt = getRcvReadyPacket(seqdistance);
       if ( pkt )
       {
            /* 
            * Acknowledged data is available,
            * Only say ready if time to deliver.
            * Report the timestamp, ready or not.
            */
            *curpktseq = pkt->getSeqNo();
            *tsbpdtime = getPktTsbPdTime(pkt->getMsgTimeStamp());

            // If seqdistance was passed, then return true no matter what the
            // TSBPD time states.
            if (seqdistance != -1 || *tsbpdtime <= CTimer::getTime())
            {
                HLOGC(dlog.Debug, log << "isRcvDataReady: packet extracted seqdistance=" << seqdistance
                        << " TsbPdTime=" << logging::FormatTime(*tsbpdtime));
               return true;
            }
       }

       HLOGC(dlog.Debug, log << "isRcvDataReady: packet "
               << (pkt ? "" : "NOT ") << "extracted, NOT READY.");
       return false;
   }

   return isRcvDataAvailable();
}

// XXX This function may be called only after checking
// if m_bTsbPdMode.
CPacket* CRcvBuffer::getRcvReadyPacket(int32_t seqdistance)
{
    // If asked for readiness of a packet at given sequence distance
    // (that is, we need to extract the packet with given sequence number),
    // only check if this cell is occupied in the buffer, and if so,
    // if it's occupied with a "good" unit. That's all. It doesn't
    // matter whether it's ready to play.
    if (seqdistance != -1)
    {
        // Note: seqdistance is the value to to go BACKWARDS from m_iLastAckPos,
        // which is the position that is in sync with CUDT::m_iRcvLastSkipAck. This
        // position is the sequence number of a packet that is NOT received, but it's
        // expected to be received as next. So the minimum value of seqdistance is 1.

        // SANITY CHECK
        if (seqdistance == 0)
        {
            LOGC(mglog.Fatal, log << "IPE: trying to extract packet past the last ACK-ed!");
            return 0;
        }

        if (seqdistance > getRcvDataSize())
        {
            HLOGC(dlog.Debug, log << "getRcvReadyPacket: Sequence offset=" << seqdistance << " is in the past (start=" << m_iStartPos
                    << " end=" << m_iLastAckPos << ")");
            return 0;
        }

        int i = shift(m_iLastAckPos, -seqdistance);
        if ( m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD )
        {
            HLOGC(dlog.Debug, log << "getRcvReadyPacket: FOUND PACKET %" << m_pUnit[i]->m_Packet.getSeqNo());
            return &m_pUnit[i]->m_Packet;
        }

        HLOGC(dlog.Debug, log << "getRcvReadyPacket: Sequence offset=" << seqdistance << " IS NOT RECEIVED.");
        return 0;
    }
#if ENABLE_HEAVY_LOGGING
    int nskipped = 0;
#endif

    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = shift_forward(i))
    {
        /* 
         * Skip missing packets that did not arrive in time.
         */
        if ( m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD )
        {
            HLOGC(dlog.Debug, log << "getRcvReadyPacket: Found next packet seq=%" << m_pUnit[i]->m_Packet.getSeqNo()
                    << " (" << nskipped << " empty cells skipped)");
            return &m_pUnit[i]->m_Packet;
        }
#if ENABLE_HEAVY_LOGGING
        ++nskipped;
#endif
    }

    return 0;
}

bool CRcvBuffer::isRcvDataReady()
{
   uint64_t tsbpdtime;
   int32_t seq;

   return isRcvDataReady(Ref(tsbpdtime), Ref(seq), -1);
}

int CRcvBuffer::getAvailBufSize() const
{
   // One slot must be empty in order to tell the difference between "empty buffer" and "full buffer"
   return m_iSize - getRcvDataSize() - 1;
}

int CRcvBuffer::getRcvDataSize() const
{
   if (m_iLastAckPos >= m_iStartPos)
      return m_iLastAckPos - m_iStartPos;

   return m_iSize + m_iLastAckPos - m_iStartPos;
}

int CRcvBuffer::debugGetSize() const
{
    // Does exactly the same as getRcvDataSize, but
    // it should be used FOR INFORMATIONAL PURPOSES ONLY.
    // The source values might be changed in another thread
    // during the calculation, although worst case the
    // resulting value may differ to the real buffer size by 1.
    int from = m_iStartPos, to = m_iLastAckPos;
    int size = to - from;
    if (size < 0)
        size += m_iSize;

    return size;
}


#ifdef SRT_ENABLE_RCVBUFSZ_MAVG

#define SRT_MAVG_BASE_PERIOD 1000000 // us
#define SRT_us2ms 1000

/* Return moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBuffer::getRcvAvgDataSize(int &bytes, int &timespan)
{
   timespan = m_TimespanMAvg;
   bytes = m_iBytesCountMAvg;
   return(m_iCountMAvg);
}

/* Update moving average of acked data pkts, bytes, and timespan (ms) of the receive buffer */
void CRcvBuffer::updRcvAvgDataSize(uint64_t now)
{
   uint64_t elapsed = (now - m_LastSamplingTime) / SRT_us2ms; //ms since last sampling

   if (elapsed < (SRT_MAVG_BASE_PERIOD / SRT_MAVG_SAMPLING_RATE) / SRT_us2ms)
      return; /* Last sampling too recent, skip */

   if (elapsed > SRT_MAVG_BASE_PERIOD)
   {
      /* No sampling in last 1 sec, initialize/reset moving average */
      m_iCountMAvg = getRcvDataSize(m_iBytesCountMAvg, m_TimespanMAvg);
      m_LastSamplingTime = now;

      HLOGC(dlog.Debug, log << "getRcvDataSize: " << m_iCountMAvg << " " << m_iBytesCountMAvg
              << " " << m_TimespanMAvg << " ms elapsed: " << elapsed << " ms");
   }
   else if (elapsed >= (SRT_MAVG_BASE_PERIOD / SRT_MAVG_SAMPLING_RATE) / SRT_us2ms)
   {
      /*
      * Weight last average value between -1 sec and last sampling time (LST)
      * and new value between last sampling time and now
      *                                      |elapsed|
      *   +----------------------------------+-------+
      *  -1                                 LST      0(now)
      */
      int instspan;
      int bytescount;
      int count = getRcvDataSize(bytescount, instspan);

      m_iCountMAvg      = (int)(((count      * (1000 - elapsed)) + (count      * elapsed)) / 1000);
      m_iBytesCountMAvg = (int)(((bytescount * (1000 - elapsed)) + (bytescount * elapsed)) / 1000);
      m_TimespanMAvg    = (int)(((instspan   * (1000 - elapsed)) + (instspan   * elapsed)) / 1000);
      m_LastSamplingTime = now;

      HLOGC(dlog.Debug, log << "getRcvDataSize: " << count << " " << bytescount << " " << instspan
              << " ms elapsed: " << elapsed << " ms");
   }
}
#endif /* SRT_ENABLE_RCVBUFSZ_MAVG */

/* Return acked data pkts, bytes, and timespan (ms) of the receive buffer */
int CRcvBuffer::getRcvDataSize(int &bytes, int &timespan)
{
   timespan = 0;
   if (m_bTsbPdMode)
   {
      /* skip invalid entries */
      int i,n;
      for (i = m_iStartPos, n = m_iLastAckPos; i != n; i = shift_forward(i))
      {
         if ((NULL != m_pUnit[i]) && (CUnit::GOOD == m_pUnit[i]->m_iFlag))
             break;
      }

      /* Get a valid startpos */
      int startpos = i;
      int endpos = n;

      if (m_iLastAckPos != startpos) 
      {
         /*
         *     |<--- DataSpan ---->|<- m_iMaxPos ->|
         * +---+---+---+---+---+---+---+---+---+---+---+---
         * |   | 1 | 1 | 1 | 0 | 0 | 1 | 1 | 0 | 1 |   |     m_pUnits[]
         * +---+---+---+---+---+---+---+---+---+---+---+---
         *       |                   |
         *       \_ m_iStartPos      \_ m_iLastAckPos
         *        
         * m_pUnits[startpos] shall be valid (->m_iFlag==CUnit::GOOD).
         * If m_pUnits[m_iLastAckPos-1] is not valid (NULL or ->m_iFlag!=CUnit::GOOD), 
         * it means m_pUnits[m_iLastAckPos] is valid since a valid unit is needed to skip.
         * Favor m_pUnits[m_iLastAckPos] if valid over [m_iLastAckPos-1] to include the whole acked interval.
         */
         if ((m_iMaxPos <= 0)
                 || (!m_pUnit[m_iLastAckPos])
                 || (m_pUnit[m_iLastAckPos]->m_iFlag != CUnit::GOOD))
         {
            endpos = (m_iLastAckPos == 0 ? m_iSize - 1 : m_iLastAckPos - 1);
         }

         if ((NULL != m_pUnit[endpos]) && (NULL != m_pUnit[startpos]))
         {
            uint64_t startstamp = getPktTsbPdTime(m_pUnit[startpos]->m_Packet.getMsgTimeStamp());
            uint64_t endstamp = getPktTsbPdTime(m_pUnit[endpos]->m_Packet.getMsgTimeStamp());
            /* 
            * There are sampling conditions where spantime is < 0 (big unsigned value).
            * It has been observed after changing the SRT latency from 450 to 200 on the sender.
            *
            * Possible packet order corruption when dropping packet, 
            * cause by bad thread protection when adding packet in queue
            * was later discovered and fixed. Security below kept. 
            *
            * DateTime                 RecvRate LostRate DropRate AvailBw     RTT   RecvBufs PdDelay
            * 2014-12-08T15:04:25-0500     4712      110        0   96509  33.710        393     450
            * 2014-12-08T15:04:35-0500     4512       95        0  107771  33.493 1496542976     200
            * 2014-12-08T15:04:40-0500     4213      106        3  107352  53.657    9499425     200
            * 2014-12-08T15:04:45-0500     4575      104        0  102194  53.614      59666     200
            * 2014-12-08T15:04:50-0500     4475      124        0  100543  53.526        505     200
            */
            if (endstamp > startstamp)
                timespan = (int)((endstamp - startstamp) / 1000);
         }
         /* 
         * Timespan can be less then 1000 us (1 ms) if few packets. 
         * Also, if there is only one pkt in buffer, the time difference will be 0.
         * Therefore, always add 1 ms if not empty.
         */
         if (0 < m_iAckedPktsCount)
            timespan += 1;
      }
   }
   HLOGF(dlog.Debug, "getRcvDataSize: %6d %6d %6d ms\n", m_iAckedPktsCount, m_iAckedBytesCount, timespan);
   bytes = m_iAckedBytesCount;
   return m_iAckedPktsCount;
}

int CRcvBuffer::getRcvAvgPayloadSize() const
{
   return m_iAvgPayloadSz;
}

void CRcvBuffer::dropMsg(int32_t msgno, bool using_rexmit_flag)
{
   for (int i = m_iStartPos, n = shift(m_iLastAckPos, m_iMaxPos); i != n; i = shift_forward(i))
      if ((m_pUnit[i] != NULL) 
              && (m_pUnit[i]->m_Packet.getMsgSeq(using_rexmit_flag) == msgno))
         m_pUnit[i]->m_iFlag = CUnit::DROPPED;
}

uint64_t CRcvBuffer::getTsbPdTimeBase(uint32_t timestamp)
{
   /* 
   * Packet timestamps wrap around every 01h11m35s (32-bit in usec)
   * When added to the peer start time (base time), 
   * wrapped around timestamps don't provide a valid local packet delevery time.
   *
   * A wrap check period starts 30 seconds before the wrap point.
   * In this period, timestamps smaller than 30 seconds are considered to have wrapped around (then adjusted).
   * The wrap check period ends 30 seconds after the wrap point, afterwhich time base has been adjusted.
   */ 
   uint64_t carryover = 0;

   // This function should generally return the timebase for the given timestamp.
   // It's assumed that the timestamp, for which this function is being called,
   // is received as monotonic clock. This function then traces the changes in the
   // timestamps passed as argument and catches the moment when the 64-bit timebase
   // should be increased by a "segment length" (MAX_TIMESTAMP+1).

   // The checks will be provided for the following split:
   // [INITIAL30][FOLLOWING30]....[LAST30] <-- == CPacket::MAX_TIMESTAMP
   //
   // The following actions should be taken:
   // 1. Check if this is [LAST30]. If so, ENTER TSBPD-wrap-check state
   // 2. Then, it should turn into [INITIAL30] at some point. If so, use carryover MAX+1.
   // 3. Then it should switch to [FOLLOWING30]. If this is detected,
   //    - EXIT TSBPD-wrap-check state
   //    - save the carryover as the current time base.

   if (m_bTsbPdWrapCheck) 
   {
       // Wrap check period.

       if (timestamp < TSBPD_WRAP_PERIOD)
       {
           carryover = uint64_t(CPacket::MAX_TIMESTAMP) + 1;
       }
       // 
       else if ((timestamp >= TSBPD_WRAP_PERIOD)
               &&  (timestamp <= (TSBPD_WRAP_PERIOD * 2)))
       {
           /* Exiting wrap check period (if for packet delivery head) */
           m_bTsbPdWrapCheck = false;
           m_ullTsbPdTimeBase += uint64_t(CPacket::MAX_TIMESTAMP) + 1;
           tslog.Debug("tsppd wrap period ends");
       }
   }
   // Check if timestamp is in the last 30 seconds before reaching the MAX_TIMESTAMP.
   else if (timestamp > (CPacket::MAX_TIMESTAMP - TSBPD_WRAP_PERIOD))
   {
      /* Approching wrap around point, start wrap check period (if for packet delivery head) */
      m_bTsbPdWrapCheck = true;
      tslog.Debug("tsppd wrap period begins");
   }
   return(m_ullTsbPdTimeBase + carryover);
}

uint64_t CRcvBuffer::getPktTsbPdTime(uint32_t timestamp)
{
   return(getTsbPdTimeBase(timestamp) + m_uTsbPdDelay + timestamp + m_DriftTracer.drift());
}

int CRcvBuffer::setRcvTsbPdMode(uint64_t timebase, uint32_t delay)
{
    m_bTsbPdMode = true;
    m_bTsbPdWrapCheck = false;

    // Timebase passed here comes is calculated as:
    // >>> CTimer::getTime() - ctrlpkt->m_iTimeStamp
    // where ctrlpkt is the packet with SRT_CMD_HSREQ message.
    //
    // This function is called in the HSREQ reception handler only.
    m_ullTsbPdTimeBase = timebase;
    // XXX Seems like this may not work correctly.
    // At least this solution this way won't work with application-supplied
    // timestamps. For that case the timestamps should be taken exclusively
    // from the data packets because in case of application-supplied timestamps
    // they come from completely different server and undergo different rules
    // of network latency and drift.
    m_uTsbPdDelay = delay;
    return 0;
}

#ifdef SRT_DEBUG_TSBPD_DRIFT
void CRcvBuffer::printDriftHistogram(int64_t iDrift)
{
     /*
      * Build histogram of drift values
      * First line  (ms): <=-10.0 -9.0 ... -1.0 - 0.0 + 1.0 ... 9.0 >=10.0
      * Second line (ms):         -0.9 ... -0.1 - 0.0 + 0.1 ... 0.9
      *  0    0    0    0    0    0    0    0    0    0 -    0 +    0    0    0    1    0    0    0    0    0    0
      *       0    0    0    0    0    0    0    0    0 -    0 +    0    0    0    0    0    0    0    0    0
      */
    iDrift /= 100;  // uSec to 100 uSec (0.1ms)
    if (-10 < iDrift && iDrift < 10)
    {
        /* Fill 100us histogram -900 .. 900 us 100 us increments */
        m_TsbPdDriftHisto100us[10 + iDrift]++;
    }
    else
    {
        /* Fill 1ms histogram <=-10.0, -9.0 .. 9.0, >=10.0 ms in 1 ms increments */
        iDrift /= 10;   // 100uSec to 1ms
        if (-10 < iDrift && iDrift < 10) m_TsbPdDriftHisto1ms[10 + iDrift]++;
        else if (iDrift <= -10)          m_TsbPdDriftHisto1ms[0]++;
        else                             m_TsbPdDriftHisto1ms[20]++;
    }

    if ((m_iTsbPdDriftNbSamples % TSBPD_DRIFT_PRT_SAMPLES) == 0)
    {
        int *histo = m_TsbPdDriftHisto1ms;

        fprintf(stderr, "%4d %4d %4d %4d %4d %4d %4d %4d %4d %4d - %4d + ",
                histo[0],histo[1],histo[2],histo[3],histo[4],
                histo[5],histo[6],histo[7],histo[8],histo[9],histo[10]);
        fprintf(stderr, "%4d %4d %4d %4d %4d %4d %4d %4d %4d %4d\n",
                histo[11],histo[12],histo[13],histo[14],histo[15],
                histo[16],histo[17],histo[18],histo[19],histo[20]);

        histo = m_TsbPdDriftHisto100us;
        fprintf(stderr, "     %4d %4d %4d %4d %4d %4d %4d %4d %4d - %4d + ",
                histo[1],histo[2],histo[3],histo[4],histo[5],
                histo[6],histo[7],histo[8],histo[9],histo[10]);
        fprintf(stderr, "%4d %4d %4d %4d %4d %4d %4d %4d %4d\n",
                histo[11],histo[12],histo[13],histo[14],histo[15],
                histo[16],histo[17],histo[18],histo[19]);
    }
}

void CRcvBuffer::printDriftOffset(int tsbPdOffset, int tsbPdDriftAvg)
{
    char szTime[32] = {};
    uint64_t now = CTimer::getTime();
    time_t tnow = (time_t)(now/1000000);
    strftime(szTime, sizeof(szTime), "%H:%M:%S", localtime(&tnow));
    fprintf(stderr, "%s.%03d: tsbpd offset=%d drift=%d usec\n", 
            szTime, (int)((now%1000000)/1000), tsbPdOffset, tsbPdDriftAvg);
    memset(m_TsbPdDriftHisto100us, 0, sizeof(m_TsbPdDriftHisto100us));
    memset(m_TsbPdDriftHisto1ms, 0, sizeof(m_TsbPdDriftHisto1ms));
}
#endif /* SRT_DEBUG_TSBPD_DRIFT */

void CRcvBuffer::addRcvTsbPdDriftSample(uint32_t timestamp, pthread_mutex_t& mutex_to_lock)
{
    if (!m_bTsbPdMode) // Not checked unless in TSBPD mode
        return;
    /*
     * TsbPD time drift correction
     * TsbPD time slowly drift over long period depleting decoder buffer or raising latency
     * Re-evaluate the time adjustment value using a receiver control packet (ACK-ACK).
     * ACK-ACK timestamp is RTT/2 ago (in sender's time base)
     * Data packet have origin time stamp which is older when retransmitted so not suitable for this.
     *
     * Every TSBPD_DRIFT_MAX_SAMPLES packets, the average drift is calculated
     * if -TSBPD_DRIFT_MAX_VALUE < avgTsbPdDrift < TSBPD_DRIFT_MAX_VALUE uSec, pass drift value to RcvBuffer to adjust delevery time.
     * if outside this range, adjust this->TsbPdTimeOffset and RcvBuffer->TsbPdTimeBase by +-TSBPD_DRIFT_MAX_VALUE uSec
     * to maintain TsbPdDrift values in reasonable range (-5ms .. +5ms).
     */

    // Note important thing: this function is being called _EXCLUSIVELY_ in the handler
    // of UMSG_ACKACK command reception. This means that the timestamp used here comes
    // from the CONTROL domain, not DATA domain (timestamps from DATA domain may be
    // either schedule time or a time supplied by the application).

    int64_t iDrift = CTimer::getTime() - (getTsbPdTimeBase(timestamp) + timestamp);

    CGuard::enterCS(mutex_to_lock, "ack");

    bool updated = m_DriftTracer.update(iDrift);

#ifdef SRT_DEBUG_TSBPD_DRIFT
    printDriftHistogram(iDrift);
#endif /* SRT_DEBUG_TSBPD_DRIFT */

    if ( updated )
    {
#ifdef SRT_DEBUG_TSBPD_DRIFT
        printDriftOffset(m_DriftTracer.overdrift(), m_DriftTracer.drift());
#endif /* SRT_DEBUG_TSBPD_DRIFT */

        m_ullTsbPdTimeBase += m_DriftTracer.overdrift();
    }

    CGuard::leaveCS(mutex_to_lock, "ack");
}

int CRcvBuffer::readMsg(char* data, int len)
{
    SRT_MSGCTRL dummy = srt_msgctrl_default;
    return readMsg(data, len, Ref(dummy), -1);
}

int CRcvBuffer::readMsg(char* data, int len, ref_t<SRT_MSGCTRL> r_msgctl, int upto)
{
    SRT_MSGCTRL& msgctl = *r_msgctl;
    int p = -1, q = -1;
    bool passack;

    bool empty = accessMsg(Ref(p), Ref(q), Ref(passack), Ref(msgctl.srctime), upto);
    if (empty)
        return 0;

    // This should happen just once. By 'empty' condition
    // we have a guarantee that m_pUnit[p] exists and is valid.
    CPacket& pkt1 = m_pUnit[p]->m_Packet;

    // This returns the sequence number and message number to
    // the API caller.
    msgctl.pktseq = pkt1.getSeqNo();
    msgctl.msgno = pkt1.getMsgSeq();

    return extractData(data, len, p, q, passack);

}

/*
int CRcvBuffer::readMsg(char* data, int len, ref_t<SRT_MSGCTRL> r_msgctl)
{
    SRT_MSGCTRL& msgctl = *r_msgctl;
    int p, q;
    bool passack;

    bool empty = accessMsg(Ref(p), Ref(q), Ref(passack), Ref(msgctl.srctime), -1);
    if (empty)
        return 0;

    // This should happen just once. By 'empty' condition
    // we have a guarantee that m_pUnit[p] exists and is valid.
    CPacket& pkt1 = m_pUnit[p]->m_Packet;

    // This returns the sequence number and message number to
    // the API caller.
    msgctl.pktseq = pkt1.getSeqNo();
    msgctl.msgno = pkt1.getMsgSeq();

    return extractData(data, len, p, q, passack);
}
*/

#ifdef SRT_DEBUG_TSBPD_OUTJITTER
void CRcvBuffer::debugJitter(uint64_t rplaytime)
{
    uint64_t now = CTimer::getTime();
    if ((now - rplaytime)/10 < 10)
        m_ulPdHisto[0][(now - rplaytime)/10]++;
    else if ((now - rplaytime)/100 < 10)
        m_ulPdHisto[1][(now - rplaytime)/100]++;
    else if ((now - rplaytime)/1000 < 10)
        m_ulPdHisto[2][(now - rplaytime)/1000]++;
    else
        m_ulPdHisto[3][1]++;
}
#endif   /* SRT_DEBUG_TSBPD_OUTJITTER */

bool CRcvBuffer::accessMsg(ref_t<int> r_p, ref_t<int> r_q, ref_t<bool> r_passack, ref_t<uint64_t> r_playtime, int upto)
{
    // This function should do the following:
    // 1. Find the first packet starting the next message (or just next packet)
    // 2. When found something ready for extraction, return true.
    // 3. p and q point the index range for extraction
    // 4. passack decides if this range shall be removed after extraction

    int& p = *r_p, & q = *r_q;
    bool& passack = *r_passack;
    uint64_t& rplaytime = *r_playtime;
    bool empty = true;

    if (m_bTsbPdMode)
    {
        passack = false;
        int seq = 0;

        if (getRcvReadyMsg(r_playtime, Ref(seq), upto))
        {
            empty = false;
            // In TSBPD mode you always read one message
            // at a time and a message always fits in one UDP packet,
            // so in one "unit".
            p = q = m_iStartPos;

            debugJitter(rplaytime);
        }

    }
    else
    {
        rplaytime = 0;
        if (scanMsg(Ref(p), Ref(q), Ref(passack)))
            empty = false;

    }

    return empty;
}

int CRcvBuffer::extractData(char* data, int len, int p, int q, bool passack)
{
    int rs = len;
    int past_q = shift_forward(q);
    while (p != past_q)
    {
        int unitsize = m_pUnit[p]->m_Packet.getLength();
        if ((rs >= 0) && (unitsize > rs))
            unitsize = rs;

        if (unitsize > 0)
        {
            memcpy(data, m_pUnit[p]->m_Packet.m_pcData, unitsize);
            data += unitsize;
            rs -= unitsize;
            /* we removed bytes form receive buffer */
            countBytes(-1, -unitsize, true);


#if ENABLE_HEAVY_LOGGING
            {
                static uint64_t prev_now;
                static uint64_t prev_srctime;

                int32_t seq = m_pUnit[p]->m_Packet.m_iSeqNo;

                uint64_t nowtime = CTimer::getTime();
                //CTimer::rdtsc(nowtime);
                uint64_t srctime = getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp());

                int64_t timediff = nowtime - srctime;
                int64_t nowdiff = prev_now ? (nowtime - prev_now) : 0;
                uint64_t srctimediff = prev_srctime ? (srctime - prev_srctime) : 0;

                HLOGC(dlog.Debug, log << CONID() << "readMsg: DELIVERED seq=" << seq << " T=" << logging::FormatTime(srctime) << " in " << (timediff/1000.0) << "ms - "
                    "TIME-PREVIOUS: PKT: " << (srctimediff/1000.0) << " LOCAL: " << (nowdiff/1000.0));

                prev_now = nowtime;
                prev_srctime = srctime;
            }
#endif
        }

        // Note special case for live mode (one packet per message and TSBPD=on):
        //  - p == q (that is, this loop passes only once)
        //  - no passack (the unit is always removed from the buffer)
        if (!passack)
        {
            freeUnitAt(p);
        }
        else
            m_pUnit[p]->m_iFlag = CUnit::PASSACK;

        p = shift_forward(p);
    }

    if (!passack)
        m_iStartPos = past_q;

    HLOGC(dlog.Debug, log << "rcvBuf/extractData: begin=" << m_iStartPos << " reporting extraction size=" << (len - rs));

    return len - rs;
}


bool CRcvBuffer::scanMsg(ref_t<int> r_p, ref_t<int> r_q, ref_t<bool> passack)
{
    int& p = *r_p;
    int& q = *r_q;

    // empty buffer
    if ((m_iStartPos == m_iLastAckPos) && (m_iMaxPos <= 0))
    {
        HLOGC(mglog.Debug, log << "scanMsg: empty buffer");
        return false;
    }

    int rmpkts = 0;
    int rmbytes = 0;
    //skip all bad msgs at the beginning
    // This loop rolls until the "buffer is empty" (head == tail),
    // in particular, there's no units accessible for the reader.
    while (m_iStartPos != m_iLastAckPos)
    {
        // Roll up to the first valid unit
        if (!m_pUnit[m_iStartPos])
        {
            if (++ m_iStartPos == m_iSize)
                m_iStartPos = 0;
            continue;
        }

        // Note: PB_FIRST | PB_LAST == PB_SOLO.
        // testing if boundary() & PB_FIRST tests if the msg is first OR solo.
        if ( m_pUnit[m_iStartPos]->m_iFlag == CUnit::GOOD
                && m_pUnit[m_iStartPos]->m_Packet.getMsgBoundary() & PB_FIRST )
        {
            bool good = true;

            // look ahead for the whole message

            // We expect to see either of:
            // [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENT] [PB_LAST]
            // [PB_SOLO]
            // but not:
            // [PB_FIRST] NULL ...
            // [PB_FIRST] FREE/PASSACK/DROPPED...
            // If the message didn't look as expected, interrupt this.

            // This begins with a message starting at m_iStartPos
            // up to m_iLastAckPos OR until the PB_LAST message is found.
            // If any of the units on this way isn't good, this OUTER loop
            // will be interrupted.
            for (int i = m_iStartPos; i != m_iLastAckPos;)
            {
                if (!m_pUnit[i] || m_pUnit[i]->m_iFlag != CUnit::GOOD)
                {
                    good = false;
                    break;
                }

                // Likewise, boundary() & PB_LAST will be satisfied for last OR solo.
                if ( m_pUnit[i]->m_Packet.getMsgBoundary() & PB_LAST )
                    break;

                if (++ i == m_iSize)
                    i = 0;
            }

            if (good)
                break;
        }

        rmpkts++;
        rmbytes += freeUnitAt(m_iStartPos);

        m_iStartPos = shift_forward(m_iStartPos);
    }
    /* we removed bytes form receive buffer */
    countBytes(-rmpkts, -rmbytes, true);

    // Not sure if this is correct, but this above 'while' loop exits
    // under the following conditions only:
    // - m_iStartPos == m_iLastAckPos (that makes passack = true)
    // - found at least GOOD unit with PB_FIRST and not all messages up to PB_LAST are good,
    //   in which case it returns with m_iStartPos <% m_iLastAckPos (earlier)
    // Also all units that lied before m_iStartPos are removed.

    p = -1;                  // message head
    q = m_iStartPos;         // message tail
    *passack = m_iStartPos == m_iLastAckPos;
    bool found = false;

    // looking for the first message
    //>>m_pUnit[size + m_iMaxPos] is not valid 

    // XXX Would be nice to make some very thorough refactoring here.

    // This rolls by q variable from m_iStartPos up to m_iLastAckPos,
    // actually from the first message up to the one with PB_LAST
    // or PB_SOLO boundary.

    // The 'i' variable used in this loop is just a stub and it's
    // even hard to define the unit here. It is "shift towards
    // m_iStartPos", so the upper value is m_iMaxPos + size.
    // m_iMaxPos is itself relative to m_iLastAckPos, so
    // the upper value is m_iMaxPos + difference between
    // m_iLastAckPos and m_iStartPos, so that this value is relative
    // to m_iStartPos.
    //
    // The 'i' value isn't used anywhere, although the 'q' value rolls
    // in this loop in sync with 'i', with the difference that 'q' is
    // wrapped around, and 'i' is just incremented normally.
    //
    // This makes that this loop rolls in the range by 'q' from
    // m_iStartPos to m_iStartPos + UPPER,
    // where UPPER = m_iLastAckPos -% m_iStartPos + m_iMaxPos
    // This embraces the range from the current reading head up to
    // the last packet ever received.
    //
    // 'passack' is set to true when the 'q' has passed through
    // the border of m_iLastAckPos and fallen into the range
    // of unacknowledged packets.

    for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i < n; ++ i)
    {
        if (m_pUnit[q] && m_pUnit[q]->m_iFlag == CUnit::GOOD)
        {
            // Equivalent pseudocode:
            // PacketBoundary bound = m_pUnit[q]->m_Packet.getMsgBoundary();
            // if ( IsSet(bound, PB_FIRST) )
            //     p = q;
            // if ( IsSet(bound, PB_LAST) && p != -1 ) 
            //     found = true;
            //
            // Not implemented this way because it uselessly check p for -1
            // also after setting it explicitly.

            switch (m_pUnit[q]->m_Packet.getMsgBoundary())
            {
            case PB_SOLO: // 11
                p = q;
                found = true;
                break;

            case PB_FIRST: // 10
                p = q;
                break;

            case PB_LAST: // 01
                if (p != -1)
                    found = true;
                break;

            case PB_SUBSEQUENT:
                ; // do nothing (caught first, rolling for last)
            }
        }
        else
        {
            // a hole in this message, not valid, restart search
            p = -1;
        }

        // 'found' is set when the current iteration hit a message with PB_LAST
        // (including PB_SOLO since the very first message).
        if (found)
        {
            // the msg has to be ack'ed or it is allowed to read out of order, and was not read before
            if (!*passack || !m_pUnit[q]->m_Packet.getMsgOrderFlag())
            {
                HLOGC(mglog.Debug, log << "scanMsg: found next-to-broken message, delivering OUT OF ORDER.");
                break;
            }

            found = false;
        }

        if (++ q == m_iSize)
            q = 0;

        if (q == m_iLastAckPos)
            *passack = true;
    }

    // no msg found
    if (!found)
    {
        // NOTE:
        // This situation may only happen if:
        // - Found a packet with PB_FIRST, so p = q at the moment when it was found
        // - Possibly found following components of that message up to shifted q
        // - Found no terminal packet (PB_LAST) for that message.

        // if the message is larger than the receiver buffer, return part of the message
        if ((p != -1) && (shift_forward(q) == p))
        {
            HLOGC(mglog.Debug, log << "scanMsg: BUFFER FULL and message is INCOMPLETE. Returning PARTIAL MESSAGE.");
            found = true;
        }
        else
        {
            HLOGC(mglog.Debug, log << "scanMsg: PARTIAL or NO MESSAGE found: p=" << p << " q=" << q);
        }
    }
    else
    {
        HLOGC(mglog.Debug, log << "scanMsg: extracted message p=" << p << " q=" << q << " (" << ((q-p+m_iSize+1)%m_iSize) << " packets)");
    }

    return found;
}
