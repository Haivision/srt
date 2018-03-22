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
#ifdef SRT_ENABLE_BSTATS
,m_iBytesCount(0)
,m_LastOriginTime(0)
#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
,m_LastSamplingTime(0)
,m_iCountMAvg(0)
,m_iBytesCountMAvg(0)
,m_TimespanMAvg(0)
#endif
#endif /* SRT_ENABLE_BSTATS */
#ifdef SRT_ENABLE_INPUTRATE
,m_iInRatePktsCount(0)
,m_iInRateBytesCount(0)
,m_InRateStartTime(0)
,m_InRatePeriod(500000)   // 0.5 sec (fast start)
,m_iInRateBps(10000000/8) // 10 Mbps (1.25 MBps)
,m_iAvgPayloadSz(7*188)
#endif /* SRT_ENABLE_INPUTRATE */ 
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

#ifdef SRT_ENABLE_SRCTIMESTAMP
void CSndBuffer::addBuffer(const char* data, int len, int ttl, bool order, uint64_t srctime)
#else
void CSndBuffer::addBuffer(const char* data, int len, int ttl, bool order)
#endif
{
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   // dynamically increase sender buffer
   while (size + m_iCount >= m_iSize)
      increase();

   uint64_t time = CTimer::getTime();
   int32_t inorder = order ? MSGNO_PACKET_INORDER::mask : 0;

   LOGC(dlog.Debug) << CONID() << "CSndBuffer: adding " << size << " packets (" << len << " bytes) to send";

   Block* s = m_pLastBlock;
   for (int i = 0; i < size; ++ i)
   {
      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;

      memcpy(s->m_pcData, data + i * m_iMSS, pktlen);
      s->m_iLength = pktlen;

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

#ifdef SRT_ENABLE_SRCTIMESTAMP
      s->m_SourceTime = srctime;
#endif
      s->m_OriginTime = time;
      s->m_iTTL = ttl;

      // XXX unchecked condition: s->m_pNext == NULL.
      // Should never happen, as the call to increase() should ensure enough buffers.
      s = s->m_pNext;
   }
   m_pLastBlock = s;

   CGuard::enterCS(m_BufLock);
   m_iCount += size;

#ifdef SRT_ENABLE_BSTATS
   m_iBytesCount += len;
   m_LastOriginTime = time;
#endif /* SRT_ENABLE_BSTATS */

#ifdef SRT_ENABLE_INPUTRATE
   updInputRate(time, size, len);
#endif /* SRT_ENABLE_INRATE */

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
   updAvgBufSize(time);
#endif

   CGuard::leaveCS(m_BufLock);

   m_iNextMsgNo ++;

   // MSG_SEQ::mask has a form: 00000011111111...
   // At least it's known that it's from some index inside til the end (to bit 0).
   // If this value has been reached in a step of incrementation, it means that the
   // maximum value has been reached. Casting to int32_t to ensure the same sign
   // in comparison, although it's far from reaching the sign bit.
   if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
      m_iNextMsgNo = 1;
}

#ifdef SRT_ENABLE_INPUTRATE
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
         m_iInRateBytesCount += (m_iInRatePktsCount * SRT_DATA_PKTHDR_SIZE);
         m_iInRateBps = (int)(((int64_t)m_iInRateBytesCount * 1000000) / (time - m_InRateStartTime));

         LOGC(dlog.Debug).form("updInputRate: pkts:%d bytes:%d avg=%d rate=%d kbps interval=%llu\n",
            m_iInRateBytesCount, m_iInRatePktsCount, m_iAvgPayloadSz, (m_iInRateBps*8)/1000,
            (unsigned long long)(time - m_InRateStartTime));

         m_iInRatePktsCount = 0;
         m_iInRateBytesCount = 0;
         m_InRateStartTime = time;
      }
   }
}

int CSndBuffer::getInputRate(int& payloadsz, int& period)
{
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
   period = (int)m_InRatePeriod;
   return(m_iInRateBps);
}
#endif /* SRT_ENABLE_INPUTRATE */

int CSndBuffer::addBufferFromFile(fstream& ifs, int len)
{
   int size = len / m_iMSS;
   if ((len % m_iMSS) != 0)
      size ++;

   // dynamically increase sender buffer
   while (size + m_iCount >= m_iSize)
      increase();

   Block* s = m_pLastBlock;
   int total = 0;
   for (int i = 0; i < size; ++ i)
   {
      if (ifs.bad() || ifs.fail() || ifs.eof())
         break;

      int pktlen = len - i * m_iMSS;
      if (pktlen > m_iMSS)
         pktlen = m_iMSS;

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

   CGuard::enterCS(m_BufLock);
   m_iCount += size;
#ifdef SRT_ENABLE_BSTATS
   m_iBytesCount += total;
#endif

   CGuard::leaveCS(m_BufLock);

   m_iNextMsgNo ++;
   if (m_iNextMsgNo == int32_t(MSGNO_SEQ::mask))
      m_iNextMsgNo = 1;

   return total;
}

#if defined(SRT_ENABLE_TSBPD)
int CSndBuffer::readData(char** data, int32_t& msgno_bitset, uint64_t& srctime, unsigned kflgs)
#else
int CSndBuffer::readData(char** data, int32_t& msgno_bitset, unsigned kflgs)
#endif
{
   // No data to read
   if (m_pCurrBlock == m_pLastBlock)
      return 0;

   *data = m_pCurrBlock->m_pcData;
   int readlen = m_pCurrBlock->m_iLength;

   m_pCurrBlock->m_iMsgNoBitset |= MSGNO_ENCKEYSPEC::wrap(kflgs);

   msgno_bitset = m_pCurrBlock->m_iMsgNoBitset;
#ifdef SRT_ENABLE_TSBPD
   srctime = 
#ifdef SRT_ENABLE_SRCTIMESTAMP
      m_pCurrBlock->m_SourceTime ? m_pCurrBlock->m_SourceTime :
#endif /* SRT_ENABLE_SRCTIMESTAMP */
      m_pCurrBlock->m_OriginTime;
#endif /* SRT_ENABLE_TSBPD */

   m_pCurrBlock = m_pCurrBlock->m_pNext;

   LOGC(dlog.Debug) << CONID() << "CSndBuffer: extracting packet size=" << readlen << " to send";

   return readlen;
}

#ifdef SRT_ENABLE_TSBPD
int CSndBuffer::readData(char** data, const int offset, int32_t& msgno_bitset, uint64_t& srctime, int& msglen)
#else  /* SRT_ENABLE_TSBPD */
int CSndBuffer::readData(char** data, const int offset, int32_t& msgno_bitset, int& msglen)
#endif /* SRT_ENABLE_TSBPD */
{
   CGuard bufferguard(m_BufLock);

   Block* p = m_pFirstBlock;

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
   if ((p->m_iTTL >= 0) && ((CTimer::getTime() - p->m_OriginTime) / 1000 > (uint64_t)p->m_iTTL))
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

      // If readData returns -1, then msgno_bitset is understood as a Message ID to drop.
      // This means that in this case it should be written by the message sequence value only
      // (not the whole 4-byte bitset written at PH_MSGNO).
      msgno_bitset = msgno;
      return -1;
   }

   *data = p->m_pcData;
   int readlen = p->m_iLength;
   msgno_bitset = p->m_iMsgNoBitset;

#ifdef SRT_ENABLE_TSBPD
   srctime = 
#ifdef SRT_ENABLE_SRCTIMESTAMP
      p->m_SourceTime ? p->m_SourceTime :
#endif /* SRT_ENABLE_SRCTIMESTAMP */
      p->m_OriginTime;
#endif /* SRT_ENABLE_TSBPD */

   return readlen;
}

void CSndBuffer::ackData(int offset)
{
   CGuard bufferguard(m_BufLock);

#ifdef SRT_ENABLE_BSTATS
   bool move = false;
   for (int i = 0; i < offset; ++ i) {
      m_iBytesCount -= m_pFirstBlock->m_iLength;
      if (m_pFirstBlock == m_pCurrBlock) move = true;
      m_pFirstBlock = m_pFirstBlock->m_pNext;
   }
   if (move) m_pCurrBlock = m_pFirstBlock;
#else
   for (int i = 0; i < offset; ++ i)
      m_pFirstBlock = m_pFirstBlock->m_pNext;
#endif

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

#ifdef SRT_ENABLE_BSTATS
#ifdef SRT_ENABLE_SNDBUFSZ_MAVG

int CSndBuffer::getAvgBufSize(int &bytes, int &timespan)
{
   CGuard bufferguard(m_BufLock); /* Consistency of pkts vs. bytes vs. spantime */

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
      m_iCountMAvg = getCurrBufSize(m_iBytesCountMAvg, m_TimespanMAvg);
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
      int count = getCurrBufSize(bytescount, instspan);

      LOGC(dlog.Debug).form("updAvgBufSize: %6d: %6d %6d %6d ms\n", (int)elapsed, count, bytescount, instspan);

      m_iCountMAvg      = (int)(((count      * (1000 - elapsed)) + (count      * elapsed)) / 1000);
      m_iBytesCountMAvg = (int)(((bytescount * (1000 - elapsed)) + (bytescount * elapsed)) / 1000);
      m_TimespanMAvg    = (int)(((instspan   * (1000 - elapsed)) + (instspan   * elapsed)) / 1000);
      m_LastSamplingTime = now;
   }
}

#endif /* SRT_ENABLE_SNDBUFSZ_MAVG */

int CSndBuffer::getCurrBufSize(int &bytes, int &timespan)
{
   bytes = m_iBytesCount;
   /* 
   * Timespan can be less then 1000 us (1 ms) if few packets. 
   * Also, if there is only one pkt in buffer, the time difference will be 0.
   * Therefore, always add 1 ms if not empty.
   */
   timespan = 0 < m_iCount ? int((m_LastOriginTime - m_pFirstBlock->m_OriginTime) / 1000) + 1 : 0;

   return m_iCount;
}

#ifdef SRT_ENABLE_TLPKTDROP
int CSndBuffer::dropLateData(int &bytes, uint64_t latetime)
{
   int dpkts = 0;
   int dbytes = 0;
   bool move = false;

   CGuard bufferguard(m_BufLock);
   for (int i = 0; i < m_iCount && m_pFirstBlock->m_OriginTime < latetime; ++ i)
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
#endif /* SRT_ENABLE_TLPKTDROP */
#endif /* SRT_ENABLE_BSTATS */

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
#ifdef SRT_ENABLE_TSBPD
//const uint32_t CRcvBuffer::TSBPD_WRAP_PERIOD = (30*1000000);    //30 seconds (in usec)
//const int CRcvBuffer::TSBPD_DRIFT_MAX_VALUE   = 5000;  // usec
//const int CRcvBuffer::TSBPD_DRIFT_MAX_SAMPLES = 1000;  // ACK-ACK packets
#ifdef SRT_DEBUG_TSBPD_DRIFT
//const int CRcvBuffer::TSBPD_DRIFT_PRT_SAMPLES = 200;   // ACK-ACK packets
#endif
#endif

CRcvBuffer::CRcvBuffer(CUnitQueue* queue, int bufsize):
m_pUnit(NULL),
m_iSize(bufsize),
m_pUnitQueue(queue),
m_iStartPos(0),
m_iLastAckPos(0),
m_iMaxPos(0),
m_iNotch(0)
#ifdef SRT_ENABLE_BSTATS
,m_BytesCountLock()
,m_iBytesCount(0)
,m_iAckedPktsCount(0)
,m_iAckedBytesCount(0)
,m_iAvgPayloadSz(7*188)
#endif
#ifdef SRT_ENABLE_TSBPD
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
#endif
{
   m_pUnit = new CUnit* [m_iSize];
   for (int i = 0; i < m_iSize; ++ i)
      m_pUnit[i] = NULL;

#ifdef SRT_DEBUG_TSBPD_DRIFT
   memset(m_TsbPdDriftHisto100us, 0, sizeof(m_TsbPdDriftHisto100us));
   memset(m_TsbPdDriftHisto1ms, 0, sizeof(m_TsbPdDriftHisto1ms));
#endif

#ifdef SRT_ENABLE_BSTATS
   pthread_mutex_init(&m_BytesCountLock, NULL);
#endif /* SRT_ENABLE_BSTATS */
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

#ifdef SRT_ENABLE_BSTATS
   pthread_mutex_destroy(&m_BytesCountLock);
#endif /* SRT_ENABLE_BSTATS */
}

#ifdef SRT_ENABLE_BSTATS
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
   CGuard cg(m_BytesCountLock);

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
#endif /* SRT_ENABLE_BSTATS */

int CRcvBuffer::addData(CUnit* unit, int offset)
{
   int pos = (m_iLastAckPos + offset) % m_iSize;
#ifdef HAI_PATCH
   if (offset >= m_iMaxPos)
      m_iMaxPos = offset + 1;
#else
   if (offset > m_iMaxPos)
      m_iMaxPos = offset;
#endif

   if (m_pUnit[pos] != NULL) {
      return -1;
   }
   m_pUnit[pos] = unit;
#ifdef SRT_ENABLE_BSTATS
   countBytes(1, unit->m_Packet.getLength());
#endif /* SRT_ENABLE_BSTATS */

   unit->m_iFlag = CUnit::GOOD;
   ++ m_pUnitQueue->m_iCount;

   return 0;
}

int CRcvBuffer::readBuffer(char* data, int len)
{
   int p = m_iStartPos;
   int lastack = m_iLastAckPos;
   int rs = len;

#ifdef SRT_ENABLE_TSBPD
   uint64_t now = (m_bTsbPdMode ? CTimer::getTime() : 0LL);
#endif /* SRT_ENABLE_TSBPD */

   LOGC(mglog.Debug) << CONID() << "readBuffer: start=" << p << " lastack=" << lastack;
   while ((p != lastack) && (rs > 0))
   {
#ifdef SRT_ENABLE_TSBPD
      LOGC(mglog.Debug) << CONID() << "readBuffer: chk if time2play: NOW=" << now << " PKT TS=" << getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp());
      if (m_bTsbPdMode && (getPktTsbPdTime(m_pUnit[p]->m_Packet.getMsgTimeStamp()) > now))
         break; /* too early for this unit, return whatever was copied */
#endif /* SRT_ENABLE_TSBPD */

      int unitsize = m_pUnit[p]->m_Packet.getLength() - m_iNotch;
      if (unitsize > rs)
         unitsize = rs;

      memcpy(data, m_pUnit[p]->m_Packet.m_pcData + m_iNotch, unitsize);
      data += unitsize;

      if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
      {
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = CUnit::FREE;
         -- m_pUnitQueue->m_iCount;

         if (++ p == m_iSize)
            p = 0;

         m_iNotch = 0;
      }
      else
         m_iNotch += rs;

      rs -= unitsize;
   }

#ifdef SRT_ENABLE_BSTATS
   /* we removed acked bytes form receive buffer */
   countBytes(-1, -(len - rs), true);
#endif /* SRT_ENABLE_BSTATS */
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

      if ((rs > unitsize) || (rs == m_pUnit[p]->m_Packet.getLength() - m_iNotch))
      {
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = CUnit::FREE;
         -- m_pUnitQueue->m_iCount;

         if (++ p == m_iSize)
            p = 0;

         m_iNotch = 0;
      }
      else
         m_iNotch += rs;

      rs -= unitsize;
   }

#ifdef SRT_ENABLE_BSTATS
   /* we removed acked bytes form receive buffer */
   countBytes(-1, -(len - rs), true);
#endif /* SRT_ENABLE_BSTATS */
   m_iStartPos = p;

   return len - rs;
}

void CRcvBuffer::ackData(int len)
{
#ifdef SRT_ENABLE_BSTATS
   {
      int pkts = 0;
      int bytes = 0;
      for (int i = m_iLastAckPos, n = (m_iLastAckPos + len) % m_iSize; i != n; i = (i + 1) % m_iSize)
      {
          if (m_pUnit[i] != NULL)
          {
              pkts++;
              bytes += m_pUnit[i]->m_Packet.getLength();
          }
      }
      if (pkts > 0) countBytes(pkts, bytes, true);
   }
#endif
   m_iLastAckPos = (m_iLastAckPos + len) % m_iSize;
   m_iMaxPos -= len;
   if (m_iMaxPos < 0)
      m_iMaxPos = 0;

   CTimer::triggerEvent();
}

#ifdef SRT_ENABLE_TSBPD
#ifdef SRT_ENABLE_TLPKTDROP

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

bool CRcvBuffer::getRcvFirstMsg(uint64_t& tsbpdtime, bool& passack, int32_t& skipseqno, CPacket** pppkt)
{
    skipseqno = -1;

    /* Check the acknowledged packets */
    if (getRcvReadyMsg(tsbpdtime, pppkt))
    {
        passack = false;
        return true;
    }
    else if (tsbpdtime != 0)
    {
        passack = false;
        return false;
    }

    /* 
     * No acked packets ready but caller want to know next packet to wait for
     * Check the not yet acked packets that may be stuck by missing packet(s).
     */
    bool haslost = false;
    tsbpdtime = 0;
    passack = true;
    skipseqno = -1;

    for (int i = m_iLastAckPos, n = (m_iLastAckPos + m_iMaxPos) % m_iSize; i != n; i = (i + 1) % m_iSize)
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
            tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
            if (tsbpdtime <= CTimer::getTime())
            {
                /* Packet ready to play */
                if (haslost)
                {
                    /* 
                     * Packet stuck on non-acked side because of missing packets.
                     * Tell 1st valid packet seqno so caller can skip (drop) the missing packets.
                     */
                    skipseqno = m_pUnit[i]->m_Packet.m_iSeqNo;
                }
                return true;
            }
            return false;
        }
    }
    return false;
}
#endif /* SRT_ENABLE_TLPKTDROP */

bool CRcvBuffer::getRcvReadyMsg(uint64_t& tsbpdtime, CPacket** pppkt)
{
   tsbpdtime = 0;
#ifdef SRT_ENABLE_BSTATS
   int rmpkts = 0; 
   int rmbytes = 0;
#endif   /* SRT_ENABLE_BSTATS */
   for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = (i + 1) % m_iSize)
   {
      bool freeunit = false;

      /* Skip any invalid skipped/dropped packets */
      if (m_pUnit[i] == NULL)
      {
         if (++ m_iStartPos == m_iSize)
            m_iStartPos = 0;
         continue;
      }

      if ( pppkt )
          *pppkt = &m_pUnit[i]->m_Packet;

      if (m_pUnit[i]->m_iFlag != CUnit::GOOD)
      {
         freeunit = true;
      }
      else
      {
        tsbpdtime = getPktTsbPdTime(m_pUnit[i]->m_Packet.getMsgTimeStamp());
        if (tsbpdtime > CTimer::getTime())
            return false;

        if (m_pUnit[i]->m_Packet.getMsgCryptoFlags() != 0)
            freeunit = true; /* packet not decrypted */
        else
            return true;
      }

      if (freeunit)
      {
         CUnit* tmp = m_pUnit[i];
         m_pUnit[i] = NULL;
#ifdef   SRT_ENABLE_BSTATS
         rmpkts++;
         rmbytes += tmp->m_Packet.getLength();
#endif   /* SRT_ENABLE_BSTATS */
         tmp->m_iFlag = CUnit::FREE;
         --m_pUnitQueue->m_iCount;

         if (++m_iStartPos == m_iSize)
            m_iStartPos = 0;
      }
   }
#ifdef SRT_ENABLE_BSTATS
   /* removed skipped, dropped, undecryptable bytes from rcv buffer */        
   countBytes(-rmpkts, -rmbytes, true); 
#endif
   return false;
}


/*
* Return receivable data status (packet timestamp ready to play if TsbPd mode)
* Return playtime (tsbpdtime) of 1st packet in queue, ready to play or not
*/
/* 
* Return data ready to be received (packet timestamp ready to play if TsbPd mode)
* Using getRcvDataSize() to know if there is something to read as it was widely
* used in the code (core.cpp) is expensive in TsbPD mode, hence this simpler function
* that only check if first packet in queue is ready.
*/
bool CRcvBuffer::isRcvDataReady(uint64_t& tsbpdtime, CPacket** pppkt)
{
   tsbpdtime = 0;

   if (m_bTsbPdMode)
   {
       CPacket* pkt = getRcvReadyPacket();
       if ( pkt )
       {
            /* 
            * Acknowledged data is available,
            * Only say ready if time to deliver.
            * Report the timestamp, ready or not.
            */
            if ( pppkt )
               *pppkt = pkt;
            tsbpdtime = getPktTsbPdTime(pkt->getMsgTimeStamp());
            if (tsbpdtime <= CTimer::getTime())
               return true;
       }
       return false;
   }

   return isRcvDataAvailable();
}

// XXX This function may be called only after checking
// if m_bTsbPdMode.
CPacket* CRcvBuffer::getRcvReadyPacket()
{
    for (int i = m_iStartPos, n = m_iLastAckPos; i != n; i = (i + 1) % m_iSize)
    {
        /* 
         * Skip missing packets that did not arrive in time.
         */
        if ( m_pUnit[i] && m_pUnit[i]->m_iFlag == CUnit::GOOD )
            return &m_pUnit[i]->m_Packet;
    }

    return 0;
}

bool CRcvBuffer::isRcvDataReady()
{
   uint64_t tsbpdtime;

   return isRcvDataReady(tsbpdtime);
}

#else
bool CRcvBuffer::isRcvDataReady() const
{
    return(getRcvDataSize() > 0);
    /*
   int p, q;
   bool passack;

   return scanMsg(p, q, passack) ? 1 : 0;
   */
}

#endif /* SRT_ENABLE_TSBPD */ 

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


#ifdef SRT_ENABLE_BSTATS
#ifdef SRT_ENABLE_TSBPD

#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
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
   uint64_t elapsed = (now - m_LastSamplingTime) / 1000; //ms since last sampling

   if ((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 > elapsed)
      return; /* Last sampling too recent, skip */

   if (1000000 < elapsed)
   {
      /* No sampling in last 1 sec, initialize/reset moving average */
      m_iCountMAvg = getRcvDataSize(m_iBytesCountMAvg, m_TimespanMAvg);
      m_LastSamplingTime = now;

      LOGC(dlog.Debug).form("getRcvDataSize: %6d %6d %6d ms elapsed:%5llu ms\n", m_iCountMAvg, m_iBytesCountMAvg, m_TimespanMAvg, (unsigned long long)elapsed);
   }
   else if ((1000000 / SRT_MAVG_SAMPLING_RATE) / 1000 <= elapsed)
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

      LOGC(dlog.Debug).form("getRcvDataSize: %6d %6d %6d ms elapsed: %5llu ms\n", count, bytescount, instspan, (unsigned long long)elapsed);
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
      for (i = m_iStartPos, n = m_iLastAckPos; i != n; i = (i + 1) % m_iSize)
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
   LOGC(dlog.Debug).form("getRcvDataSize: %6d %6d %6d ms\n", m_iAckedPktsCount, m_iAckedBytesCount, timespan);
   bytes = m_iAckedBytesCount;
   return m_iAckedPktsCount;
}

#endif /* SRT_ENABLE_TSBPD */

int CRcvBuffer::getRcvAvgPayloadSize() const
{
   return m_iAvgPayloadSz;
}

#endif /* SRT_ENABLE_BSTATS */

void CRcvBuffer::dropMsg(int32_t msgno, bool using_rexmit_flag)
{
   for (int i = m_iStartPos, n = (m_iLastAckPos + m_iMaxPos) % m_iSize; i != n; i = (i + 1) % m_iSize)
      if ((m_pUnit[i] != NULL) 
              && (m_pUnit[i]->m_Packet.getMsgSeq(using_rexmit_flag) == msgno))
         m_pUnit[i]->m_iFlag = CUnit::DROPPED;
}

#ifdef SRT_ENABLE_TSBPD
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
    // XXX Note that this is completely wrong.
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

void CRcvBuffer::addRcvTsbPdDriftSample(uint32_t timestamp)
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

}

int CRcvBuffer::readMsg(char* data, int len)
{
    uint64_t tsbpdtime;
    return readMsg(data, len, tsbpdtime);
}

#endif

#ifdef SRT_ENABLE_TSBPD
int CRcvBuffer::readMsg(char* data, int len, uint64_t& tsbpdtime)
#else  /* SRT_ENABLE_TSBPD */
int CRcvBuffer::readMsg(char* data, int len)
#endif
{
   int p, q;
   bool passack;
   bool empty = true;

#ifdef SRT_ENABLE_TSBPD
   if (m_bTsbPdMode)
   {
      passack = false;

      if (getRcvReadyMsg(tsbpdtime))
      {
         empty = false;
         p = q = m_iStartPos;

#ifdef SRT_DEBUG_TSBPD_OUTJITTER
         uint64_t now = CTimer::getTime();
         if ((now - tsbpdtime)/10 < 10)
            m_ulPdHisto[0][(now - tsbpdtime)/10]++;
         else if ((now - tsbpdtime)/100 < 10)
            m_ulPdHisto[1][(now - tsbpdtime)/100]++;
         else if ((now - tsbpdtime)/1000 < 10)
            m_ulPdHisto[2][(now - tsbpdtime)/1000]++;
         else
            m_ulPdHisto[3][1]++;
#endif   /* SRT_DEBUG_TSBPD_OUTJITTER */
      }
   }
   else
#endif
   {
      tsbpdtime = 0;
      if (scanMsg(p, q, passack))
         empty = false;

   }

   if (empty)
      return 0;

   int rs = len;
   while (p != (q + 1) % m_iSize)
   {
      int unitsize = m_pUnit[p]->m_Packet.getLength();
      if ((rs >= 0) && (unitsize > rs))
         unitsize = rs;

      if (unitsize > 0)
      {
         memcpy(data, m_pUnit[p]->m_Packet.m_pcData, unitsize);
         data += unitsize;
         rs -= unitsize;
#ifdef SRT_ENABLE_BSTATS
         /* we removed bytes form receive buffer */
         countBytes(-1, -unitsize, true);
#endif /* SRT_ENABLE_BSTATS */


#if ENABLE_LOGGING
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

              LOGC(dlog.Debug) << CONID() << "readMsg: DELIVERED seq=" << seq << " T=" << logging::FormatTime(srctime) << " in " << (timediff/1000.0) << "ms - "
                  "TIME-PREVIOUS: PKT: " << (srctimediff/1000.0) << " LOCAL: " << (nowdiff/1000.0);

              prev_now = nowtime;
              prev_srctime = srctime;
          }
#endif
      }

      if (!passack)
      {
         CUnit* tmp = m_pUnit[p];
         m_pUnit[p] = NULL;
         tmp->m_iFlag = CUnit::FREE;
         -- m_pUnitQueue->m_iCount;
      }
      else
         m_pUnit[p]->m_iFlag = CUnit::PASSACK;

      if (++ p == m_iSize)
         p = 0;
   }

   if (!passack)
      m_iStartPos = (q + 1) % m_iSize;

   return len - rs;
}


bool CRcvBuffer::scanMsg(int& p, int& q, bool& passack)
{
   // empty buffer
   if ((m_iStartPos == m_iLastAckPos) && (m_iMaxPos <= 0))
      return false;

#ifdef SRT_ENABLE_BSTATS
   int rmpkts = 0;
   int rmbytes = 0;
#endif /* SRT_ENABLE_BSTATS */
   //skip all bad msgs at the beginning
   while (m_iStartPos != m_iLastAckPos)
   {
      if (NULL == m_pUnit[m_iStartPos])
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

      CUnit* tmp = m_pUnit[m_iStartPos];
      m_pUnit[m_iStartPos] = NULL;
#ifdef SRT_ENABLE_BSTATS
      rmpkts++;
      rmbytes += tmp->m_Packet.getLength();
#endif /* SRT_ENABLE_BSTATS */
      tmp->m_iFlag = CUnit::FREE;
      -- m_pUnitQueue->m_iCount;

      if (++ m_iStartPos == m_iSize)
         m_iStartPos = 0;
   }
#ifdef SRT_ENABLE_BSTATS
   /* we removed bytes form receive buffer */
   countBytes(-rmpkts, -rmbytes, true);
#endif /* SRT_ENABLE_BSTATS */

   p = -1;                  // message head
   q = m_iStartPos;         // message tail
   passack = m_iStartPos == m_iLastAckPos;
   bool found = false;

   // looking for the first message
#if defined(HAI_PATCH) //>>m_pUnit[size + m_iMaxPos] is not valid 
   for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i < n; ++ i)
#else
   for (int i = 0, n = m_iMaxPos + getRcvDataSize(); i <= n; ++ i)
#endif
   {
      if ((NULL != m_pUnit[q]) && (CUnit::GOOD == m_pUnit[q]->m_iFlag))
      {
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
            ; // do nothing
         }
      }
      else
      {
         // a hole in this message, not valid, restart search
         p = -1;
      }

      if (found)
      {
         // the msg has to be ack'ed or it is allowed to read out of order, and was not read before
         if (!passack || !m_pUnit[q]->m_Packet.getMsgOrderFlag())
            break;

         found = false;
      }

      if (++ q == m_iSize)
         q = 0;

      if (q == m_iLastAckPos)
         passack = true;
   }

   // no msg found
   if (!found)
   {
      // if the message is larger than the receiver buffer, return part of the message
      if ((p != -1) && ((q + 1) % m_iSize == p))
         found = true;
   }

   return found;
}
