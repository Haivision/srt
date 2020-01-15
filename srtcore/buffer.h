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

#ifndef __UDT_BUFFER_H__
#define __UDT_BUFFER_H__


#include "udt.h"
#include "list.h"
#include "queue.h"
#include "utilities.h"
#include <fstream>

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


class CSndBuffer
{
public:

   // XXX There's currently no way to access the socket ID set for
   // whatever the buffer is currently working for. Required to find
   // some way to do this, possibly by having a "reverse pointer".
   // Currently just "unimplemented".
   std::string CONID() const { return ""; }

   CSndBuffer(int size = 32, int mss = 1500);
   ~CSndBuffer();

public:

      /// Insert a user buffer into the sending list.
      /// @param [in] data pointer to the user data block.
      /// @param [in] len size of the block.
      /// @param [in] ttl time to live in milliseconds
      /// @param [in] order if the block should be delivered in order, for DGRAM only

   void addBuffer(const char* data, int len, int ttl, bool order, uint64_t srctime, int32_t& w_msgno);

      /// Read a block of data from file and insert it into the sending list.
      /// @param [in] ifs input file stream.
      /// @param [in] len size of the block.
      /// @return actual size of data added from the file.

   int addBufferFromFile(std::fstream& ifs, int len);

      /// Find data position to pack a DATA packet from the furthest reading point.
      /// @param [out] data the pointer to the data position.
      /// @param [out] msgno message number of the packet.
      /// @param [out] origintime origin time stamp of the message
      /// @param [in] kflags Odd|Even crypto key flag
      /// @return Actual length of data read.

   int readData(char** data, int32_t& msgno, srt::sync::steady_clock::time_point& origintime, int kflgs);


      /// Find data position to pack a DATA packet for a retransmission.
      /// @param [out] data the pointer to the data position.
      /// @param [in] offset offset from the last ACK point.
      /// @param [out] msgno message number of the packet.
      /// @param [out] origintime origin time stamp of the message
      /// @param [out] msglen length of the message
      /// @return Actual length of data read.

   int readData(char** data, const int offset, int32_t &msgno, srt::sync::steady_clock::time_point& origintime, int& msglen);

      /// Update the ACK point and may release/unmap/return the user data according to the flag.
      /// @param [in] offset number of packets acknowledged.

   void ackData(int offset);

      /// Read size of data still in the sending list.
      /// @return Current size of the data in the sending list.

   int getCurrBufSize() const;

   int dropLateData(int& bytes, const srt::sync::steady_clock::time_point& too_late_time);

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
   void updAvgBufSize(const srt::sync::steady_clock::time_point& time);
   int getAvgBufSize(int& bytes, int& timespan);
#endif /* SRT_ENABLE_SNDBUFSZ_MAVG */
   int getCurrBufSize(int& bytes, int& timespan);

   uint64_t getInRatePeriod() const { return m_InRatePeriod; }

   /// Retrieve input bitrate in bytes per second
   int getInputRate() const { return m_iInRateBps; }

   /// Update input rate calculation.
   /// @param [in] time   current time in microseconds
   /// @param [in] pkts   number of packets newly added to the buffer
   /// @param [in] bytes  number of payload bytes in those newly added packets
   ///
   /// @return Current size of the data in the sending list.
   void updateInputRate(const srt::sync::steady_clock::time_point& time, int pkts = 0, int bytes = 0);


   void resetInputRateSmpPeriod(bool disable = false)
   {
       setInputRateSmpPeriod(disable ? 0 : INPUTRATE_FAST_START_US);
   }


private:

   void increase();
   void setInputRateSmpPeriod(int period);

private:    // Constants

    static const uint64_t INPUTRATE_FAST_START_US   =      500000;    //  500 ms
    static const uint64_t INPUTRATE_RUNNING_US      =     1000000;    // 1000 ms
    static const int64_t  INPUTRATE_MAX_PACKETS     =        2000;    // ~ 21 Mbps of 1316 bytes payload
    static const int      INPUTRATE_INITIAL_BYTESPS = BW_INFINITE;

private:
   srt::sync::CMutex m_BufLock;           // used to synchronize buffer operation

   struct Block
   {
      char* m_pcData;                   // pointer to the data block
      int m_iLength;                    // length of the block

      int32_t m_iMsgNoBitset;           // message number
      srt::sync::steady_clock::time_point m_tsOriginTime;            // original request time
      uint64_t m_ullSourceTime_us;
      int m_iTTL;                       // time to live (milliseconds)

      Block* m_pNext;                   // next block

      int32_t getMsgSeq()
      {
          // NOTE: this extracts message ID with regard to REXMIT flag.
          // This is valid only for message ID that IS GENERATED in this instance,
          // not provided by the peer. This can be otherwise sent to the peer - it doesn't matter
          // for the peer that it uses LESS bits to represent the message.
          return m_iMsgNoBitset & MSGNO_SEQ::mask;
      }

   } *m_pBlock, *m_pFirstBlock, *m_pCurrBlock, *m_pLastBlock;

   // m_pBlock:         The head pointer
   // m_pFirstBlock:    The first block
   // m_pCurrBlock:	The current block
   // m_pLastBlock:     The last block (if first == last, buffer is empty)

   struct Buffer
   {
      char* m_pcData;                   // buffer
      int m_iSize;                      // size
      Buffer* m_pNext;                  // next buffer
   } *m_pBuffer;                        // physical buffer

   int32_t m_iNextMsgNo;                // next message number

   int m_iSize;                         // buffer size (number of packets)
   int m_iMSS;                          // maximum seqment/packet size

   int m_iCount;                        // number of used blocks

   int m_iBytesCount;                   // number of payload bytes in queue
   srt::sync::steady_clock::time_point m_tsLastOriginTime;

#ifdef SRT_ENABLE_SNDBUFSZ_MAVG
   srt::sync::steady_clock::time_point m_tsLastSamplingTime;
   int m_iCountMAvg;
   int m_iBytesCountMAvg;
   int m_TimespanMAvg;
#endif /* SRT_ENABLE_SNDBUFSZ_MAVG */

   int m_iInRatePktsCount;  // number of payload bytes added since InRateStartTime
   int m_iInRateBytesCount;  // number of payload bytes added since InRateStartTime
   srt::sync::steady_clock::time_point m_tsInRateStartTime;
   uint64_t m_InRatePeriod; // usec
   int m_iInRateBps;        // Input Rate in Bytes/sec
   int m_iAvgPayloadSz;     // Average packet payload size

private:
   CSndBuffer(const CSndBuffer&);
   CSndBuffer& operator=(const CSndBuffer&);
};

////////////////////////////////////////////////////////////////////////////////


class CRcvBuffer
{
public:

    // XXX There's currently no way to access the socket ID set for
    // whatever the queue is currently working for. Required to find
    // some way to do this, possibly by having a "reverse pointer".
    // Currently just "unimplemented".
    std::string CONID() const { return ""; }

   static const int DEFAULT_SIZE = 65536;
      /// Construct the buffer.
      /// @param [in] queue  CUnitQueue that actually holds the units (packets)
      /// @param [in] bufsize_pkts in units (packets)
   CRcvBuffer(CUnitQueue* queue, int bufsize_pkts = DEFAULT_SIZE);
   ~CRcvBuffer();


public:

      /// Write data into the buffer.
      /// @param [in] unit pointer to a data unit containing new packet
      /// @param [in] offset offset from last ACK point.
      /// @return 0 is success, -1 if data is repeated.

   int addData(CUnit* unit, int offset);

      /// Read data into a user buffer.
      /// @param [in] data pointer to user buffer.
      /// @param [in] len length of user buffer.
      /// @return size of data read.

   int readBuffer(char* data, int len);

      /// Read data directly into file.
      /// @param [in] file C++ file stream.
      /// @param [in] len expected length of data to write into the file.
      /// @return size of data read.

   int readBufferToFile(std::fstream& ofs, int len);

      /// Update the ACK point of the buffer.
      /// @param [in] len number of units to be acknowledged.
      /// @return 1 if a user buffer is fulfilled, otherwise 0.

   void ackData(int len);

      /// Query how many buffer space left for data receiving.
      /// Actually only acknowledged packets, that are still in the buffer,
      /// are considered to take buffer space.
      ///
      /// @return size of available buffer space (including user buffer) for data receiving.
      ///         Not counting unacknowledged packets.

   int getAvailBufSize() const;

      /// Query how many data has been continuously received (for reading) and ready to play (tsbpdtime < now).
      /// @return size of valid (continous) data for reading.

   int getRcvDataSize() const;

      /// Query how many data was received and acknowledged.
      /// @param [out] bytes bytes
      /// @param [out] spantime spantime
      /// @return size in pkts of acked data.

   int getRcvDataSize(int& bytes, int &spantime);
#if SRT_ENABLE_RCVBUFSZ_MAVG

      /// Query a 1 sec moving average of how many data was received and acknowledged.
      /// @param [out] bytes bytes
      /// @param [out] spantime spantime
      /// @return size in pkts of acked data.

   int getRcvAvgDataSize(int& bytes, int& spantime);

      /// Query how many data of the receive buffer is acknowledged.
      /// @param [in] now current time in us.
      /// @return none.

   void updRcvAvgDataSize(const srt::sync::steady_clock::time_point& now);
#endif /* SRT_ENABLE_RCVBUFSZ_MAVG */

      /// Query the received average payload size.
      /// @return size (bytes) of payload size

   int getRcvAvgPayloadSize() const;


      /// Mark the message to be dropped from the message list.
      /// @param [in] msgno message number.
      /// @param [in] using_rexmit_flag whether the MSGNO field uses rexmit flag (if not, one more bit is part of the msgno value)

   void dropMsg(int32_t msgno, bool using_rexmit_flag);

      /// read a message.
      /// @param [out] data buffer to write the message into.
      /// @param [in] len size of the buffer.
      /// @return actuall size of data read.

   int readMsg(char* data, int len);

#if ENABLE_HEAVY_LOGGING
   void readMsgHeavyLogging(int p);
#endif

      /// read a message.
      /// @param [out] data buffer to write the message into.
      /// @param [in] len size of the buffer.
      /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
      /// @return actuall size of data read.

   int readMsg(char* data, int len, SRT_MSGCTRL& w_mctrl);

      /// Query if data is ready to read (tsbpdtime <= now if TsbPD is active).
      /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay
      ///                        of next packet in recv buffer, ready or not.
      /// @param [out] curpktseq Sequence number of the packet if there is one ready to play
      /// @return true if ready to play, false otherwise (tsbpdtime may be !0 in
      /// both cases).

   bool isRcvDataReady(srt::sync::steady_clock::time_point& w_tsbpdtime, int32_t& w_curpktseq);
#ifdef SRT_DEBUG_TSBPD_OUTJITTER
   void debugTraceJitter(uint64_t);
#else
   void debugTraceJitter(uint64_t) {}
#endif   /* SRT_DEBUG_TSBPD_OUTJITTER */

   bool isRcvDataReady();
   bool isRcvDataAvailable()
   {
       return m_iLastAckPos != m_iStartPos;
   }
   CPacket* getRcvReadyPacket();

      ///    Set TimeStamp-Based Packet Delivery Rx Mode
      ///    @param [in] timebase localtime base (uSec) of packet time stamps including buffering delay
      ///    @param [in] delay aggreed TsbPD delay
      /// @return 0

   int setRcvTsbPdMode(const srt::sync::steady_clock::time_point& timebase, const srt::sync::steady_clock::duration& delay);

      /// Add packet timestamp for drift caclculation and compensation
      /// @param [in] timestamp packet time stamp
      /// @param [ref] lock Mutex that should be locked for the operation

   void addRcvTsbPdDriftSample(uint32_t timestamp, srt::sync::CMutex& lock);

#ifdef SRT_DEBUG_TSBPD_DRIFT
   void printDriftHistogram(int64_t iDrift);
   void printDriftOffset(int tsbPdOffset, int tsbPdDriftAvg);
#endif

      /// Get information on the 1st message in queue.
      // Parameters (of the 1st packet queue, ready to play or not):
      /// @param [out] w_tsbpdtime localtime-based (uSec) packet time stamp including buffering delay of 1st packet or 0 if none
      /// @param [out] w_passack   true if 1st ready packet is not yet acknowleged (allowed to be delivered to the app)
      /// @param [out] w_skipseqno -1 or seq number of 1st unacknowledged pkt ready to play preceeded by missing packets.
      /// @retval true 1st packet ready to play (tsbpdtime <= now). Not yet acknowledged if passack == true
      /// @retval false IF tsbpdtime = 0: rcv buffer empty; ELSE:
      ///                   IF skipseqno != -1, packet ready to play preceeded by missing packets.;
      ///                   IF skipseqno == -1, no missing packet but 1st not ready to play.


   bool getRcvFirstMsg(srt::sync::steady_clock::time_point& w_tsbpdtime, bool& w_passack, int32_t& w_skipseqno, int32_t& w_curpktseq);

      /// Update the ACK point of the buffer.
      /// @param [in] len size of data to be skip & acknowledged.

   void skipData(int len);

#if ENABLE_HEAVY_LOGGING
   void reportBufferStats() const; // Heavy logging Debug only
#endif
   bool empty() const
   {
       // This will not always return the intended value,
       // that is, it may return false when the buffer really is
       // empty - but it will return true then in one of next calls.
       // This function will be always called again at some point
       // if it returned false, and on true the connection
       // is going to be broken - so this behavior is acceptable.
       return m_iStartPos == m_iLastAckPos;
   }
   bool full() const { return m_iStartPos == (m_iLastAckPos+1)%m_iSize; }
   int capacity() const { return m_iSize; }


private:
   /// This gives up unit at index p. The unit is given back to the
   /// free unit storage for further assignment for the new incoming
   /// data.
   size_t freeUnitAt(size_t p)
   {
       CUnit* u = m_pUnit[p];
       m_pUnit[p] = NULL;
       size_t rmbytes = u->m_Packet.getLength();
       m_pUnitQueue->makeUnitFree(u);
       return rmbytes;
   }

      /// Adjust receive queue to 1st ready to play message (tsbpdtime < now).
      // Parameters (of the 1st packet queue, ready to play or not):
      /// @param [out] tsbpdtime localtime-based (uSec) packet time stamp including buffering delay of 1st packet or 0 if none
      /// @retval true 1st packet ready to play without discontinuity (no hole)
      /// @retval false tsbpdtime = 0: no packet ready to play


   bool getRcvReadyMsg(srt::sync::steady_clock::time_point& w_tsbpdtime, int32_t& w_curpktseq);

public:

      // (This is exposed as used publicly in logs)
      /// Get packet delivery local time base (adjusted for wrap around)
      /// @param [in] timestamp packet timestamp (relative to peer StartTime), wrapping around every ~72 min
      /// @return local delivery time (usec)

   srt::sync::steady_clock::time_point getTsbPdTimeBase(uint32_t timestamp_us);

      /// Get packet local delivery time
      /// @param [in] timestamp packet timestamp (relative to peer StartTime), wrapping around every ~72 min
      /// @return local delivery time (usec)

public:
   srt::sync::steady_clock::time_point getPktTsbPdTime(uint32_t timestamp);
   int debugGetSize() const;
   srt::sync::steady_clock::time_point debugGetDeliveryTime(int offset);
   
   // Required by PacketFilter facility to use as a storage
   // for provided packets
   CUnitQueue* getUnitQueue()
   {
       return m_pUnitQueue;
   }

private:

   /// thread safe bytes counter of the Recv & Ack buffer
   /// @param [in] pkts  acked or removed pkts from rcv buffer (used with acked = true)
   /// @param [in] bytes number of bytes added/delete (if negative) to/from rcv buffer.
   /// @param [in] acked true when adding new pkt in RcvBuffer; false when acking/removing pkts to/from buffer

   void countBytes(int pkts, int bytes, bool acked = false);

private:
   bool scanMsg(int& w_start, int& w_end, bool& w_passack);

   int shift(int basepos, int shift) const
   {
       return (basepos + shift) % m_iSize;
   }

   // Simplified versions with ++ and --; avoid using division instruction
   int shiftFwd(int basepos) const
   {
       if (++basepos == m_iSize)
           return 0;
       return basepos;
   }

   int shiftBack(int basepos) const
   {
       if (basepos == 0)
           return m_iSize-1;
       return --basepos;
   }

private:
   CUnit** m_pUnit;                     // Array of pointed units collected in the buffer
   const int m_iSize;                   // Size of the internal array of CUnit* items
   CUnitQueue* m_pUnitQueue;            // the shared unit queue

   int m_iStartPos;                     // HEAD: first packet available for reading
   int m_iLastAckPos;                   // the last ACKed position (exclusive), follows the last readable
                                        // EMPTY: m_iStartPos = m_iLastAckPos   FULL: m_iStartPos = m_iLastAckPos + 1
   int m_iMaxPos;                       // delta between acked-TAIL and reception-TAIL


   int m_iNotch;                        // the starting read point of the first unit
                                        // (this is required for stream reading mode; it's
                                        // the position in the first unit in the list
                                        // up to which data are already retrieved;
                                        // in message reading mode it's unused and always 0)

   srt::sync::CMutex m_BytesCountLock;    // used to protect counters operations
   int m_iBytesCount;                   // Number of payload bytes in the buffer
   int m_iAckedPktsCount;               // Number of acknowledged pkts in the buffer
   int m_iAckedBytesCount;              // Number of acknowledged payload bytes in the buffer
   int m_iAvgPayloadSz;                 // Average payload size for dropped bytes estimation

   bool m_bTsbPdMode;                   // true: apply TimeStamp-Based Rx Mode
   srt::sync::steady_clock::duration m_tdTsbPdDelay;        // aggreed delay
   srt::sync::steady_clock::time_point m_tsTsbPdTimeBase;   // localtime base for TsbPd mode
   // Note: m_tsTsbPdTimeBase cumulates values from:
   // 1. Initial SRT_CMD_HSREQ packet returned value diff to current time:
   //    == (NOW - PACKET_TIMESTAMP), at the time of HSREQ reception
   // 2. Timestamp overflow (@c CRcvBuffer::getTsbPdTimeBase), when overflow on packet detected
   //    += CPacket::MAX_TIMESTAMP+1 (it's a hex round value, usually 0x1*e8).
   // 3. Time drift (CRcvBuffer::addRcvTsbPdDriftSample, executed exclusively
   //    from UMSG_ACKACK handler). This is updated with (positive or negative) TSBPD_DRIFT_MAX_VALUE
   //    once the value of average drift exceeds this value in whatever direction.
   //    += (+/-)CRcvBuffer::TSBPD_DRIFT_MAX_VALUE
   //
   // XXX Application-supplied timestamps won't work therefore. This requires separate
   // calculation of all these things above.

   bool m_bTsbPdWrapCheck;              // true: check packet time stamp wrap around
   static const uint32_t TSBPD_WRAP_PERIOD = (30*1000000);    //30 seconds (in usec)

   static const int TSBPD_DRIFT_MAX_VALUE = 5000;   // Max drift (usec) above which TsbPD Time Offset is adjusted
   static const int TSBPD_DRIFT_MAX_SAMPLES = 1000;  // Number of samples (UMSG_ACKACK packets) to perform drift caclulation and compensation
   //int m_iTsbPdDrift;                           // recent drift in the packet time stamp
   //int64_t m_TsbPdDriftSum;                     // Sum of sampled drift
   //int m_iTsbPdDriftNbSamples;                  // Number of samples in sum and histogram
   DriftTracer<TSBPD_DRIFT_MAX_SAMPLES, TSBPD_DRIFT_MAX_VALUE> m_DriftTracer;
#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
   srt::sync::steady_clock::time_point m_tsLastSamplingTime;
   int m_TimespanMAvg;
   int m_iCountMAvg;
   int m_iBytesCountMAvg;
#endif /* SRT_ENABLE_RCVBUFSZ_MAVG */
#ifdef SRT_DEBUG_TSBPD_DRIFT
   int m_TsbPdDriftHisto100us[22];              // Histogram of 100us TsbPD drift (-1.0 .. +1.0 ms in 0.1ms increment)
   int m_TsbPdDriftHisto1ms[22];                // Histogram of TsbPD drift (-10.0 .. +10.0 ms, in 1.0 ms increment)
   static const int TSBPD_DRIFT_PRT_SAMPLES = 200;    // Number of samples (UMSG_ACKACK packets) to print hostogram
#endif /* SRT_DEBUG_TSBPD_DRIFT */

#ifdef SRT_DEBUG_TSBPD_OUTJITTER
   unsigned long m_ulPdHisto[4][10];
#endif /* SRT_DEBUG_TSBPD_OUTJITTER */

private:
   CRcvBuffer();
   CRcvBuffer(const CRcvBuffer&);
   CRcvBuffer& operator=(const CRcvBuffer&);
};


#endif
