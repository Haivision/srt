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
   Yunhong Gu, last updated 01/22/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef __UDT_WINDOW_H__
#define __UDT_WINDOW_H__


#ifndef WIN32
   #include <sys/time.h>
   #include <time.h>
#endif
#include "udt.h"
#include "packet.h"

namespace ACKWindowTools
{
   struct Seq
   {
       int32_t iACKSeqNo;       // Seq. No. for the ACK packet
       int32_t iACK;            // Data Seq. No. carried by the ACK packet
       uint64_t TimeStamp;      // The timestamp when the ACK was sent
   };

   void store(Seq* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t seq, int32_t ack);
   int acknowledge(Seq* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t seq, int32_t& r_ack);
}

template <size_t SIZE>
class CACKWindow
{
public:
    CACKWindow() :
        m_aSeq(),
        m_iHead(0),
        m_iTail(0)
    {
        m_aSeq[0].iACKSeqNo = -1;
    }

   ~CACKWindow() {}

      /// Write an ACK record into the window.
      /// @param [in] seq ACK seq. no.
      /// @param [in] ack DATA ACK no.

   void store(int32_t seq, int32_t ack)
   {
       return ACKWindowTools::store(m_aSeq, SIZE, m_iHead, m_iTail, seq, ack);
   }

      /// Search the ACK-2 "seq" in the window, find out the DATA "ack" and caluclate RTT .
      /// @param [in] seq ACK-2 seq. no.
      /// @param [out] ack the DATA ACK no. that matches the ACK-2 no.
      /// @return RTT.

   int acknowledge(int32_t seq, int32_t& r_ack)
   {
       return ACKWindowTools::acknowledge(m_aSeq, SIZE, m_iHead, m_iTail, seq, r_ack);
   }

private:

   typedef ACKWindowTools::Seq Seq;

   Seq m_aSeq[SIZE];
   int m_iHead;                 // Pointer to the lastest ACK record
   int m_iTail;                 // Pointer to the oldest ACK record

private:
   CACKWindow(const CACKWindow&);
   CACKWindow& operator=(const CACKWindow&);
};

////////////////////////////////////////////////////////////////////////////////

class CPktTimeWindowTools
{
public:
   static int getPktRcvSpeed_in(const int* window, int* replica, const int* bytes, size_t asize, int& bytesps);
   static int getBandwidth_in(const int* window, int* replica, size_t psize);

   static void initializeWindowArrays(int* r_pktWindow, int* r_probeWindow, int* r_bytesWindow, size_t asize, size_t psize);
};

template <size_t ASIZE = 16, size_t PSIZE = 16>
class CPktTimeWindow: CPktTimeWindowTools
{
public:
    CPktTimeWindow():
        m_aPktWindow(),
        m_aBytesWindow(),
        m_iPktWindowPtr(0),
        m_aProbeWindow(),
        m_iProbeWindowPtr(0),
        m_iLastSentTime(0),
        m_iMinPktSndInt(1000000),
        m_LastArrTime(),
        m_CurrArrTime(),
        m_ProbeTime()
    {
        pthread_mutex_init(&m_lockPktWindow, NULL);
        pthread_mutex_init(&m_lockProbeWindow, NULL);
        m_LastArrTime = CTimer::getTime();
        CPktTimeWindowTools::initializeWindowArrays(m_aPktWindow, m_aProbeWindow, m_aBytesWindow, ASIZE, PSIZE);
    }

   ~CPktTimeWindow()
   {
       pthread_mutex_destroy(&m_lockPktWindow);
       pthread_mutex_destroy(&m_lockProbeWindow);
   }


   /// read the minimum packet sending interval.
   /// @return minimum packet sending interval (microseconds).

   int getMinPktSndInt() const { return m_iMinPktSndInt; }

   /// Calculate the packets arrival speed.
   /// @return Packet arrival speed (packets per second).

   int getPktRcvSpeed(ref_t<int> bytesps) const
   {
       // Lock access to the packet Window
       CGuard cg(m_lockPktWindow);

       int pktReplica[ASIZE];          // packet information window (inter-packet time)
       return getPktRcvSpeed_in(m_aPktWindow, pktReplica, m_aBytesWindow, ASIZE, *bytesps);
   }

   int getPktRcvSpeed() const
   {
       int bytesps;
       return getPktRcvSpeed(Ref(bytesps));
   }

   /// Estimate the bandwidth.
   /// @return Estimated bandwidth (packets per second).

   int getBandwidth() const
   {
       // Lock access to the packet Window
       CGuard cg(m_lockProbeWindow);

       int probeReplica[PSIZE];
       return getBandwidth_in(m_aProbeWindow, probeReplica, PSIZE);
   }

   /// Record time information of a packet sending.
   /// @param currtime  timestamp of the packet sending.

   void onPktSent(int currtime)
   {
       int interval = currtime - m_iLastSentTime;

       if ((interval < m_iMinPktSndInt) && (interval > 0))
           m_iMinPktSndInt = interval;

       m_iLastSentTime = currtime;
   }

   /// Record time information of an arrived packet.

   void onPktArrival(int pktsz = 0)
   {
       CGuard cg(m_lockPktWindow);

       m_CurrArrTime = CTimer::getTime();

       // record the packet interval between the current and the last one
       m_aPktWindow[m_iPktWindowPtr] = int(m_CurrArrTime - m_LastArrTime);
       m_aBytesWindow[m_iPktWindowPtr] = pktsz;

       // the window is logically circular
       ++ m_iPktWindowPtr;
       if (m_iPktWindowPtr == ASIZE)
           m_iPktWindowPtr = 0;

       // remember last packet arrival time
       m_LastArrTime = m_CurrArrTime;
   }

   /// Record the arrival time of the first probing packet.

   void probe1Arrival()
   {
       m_ProbeTime = CTimer::getTime();
   }

   /// Record the arrival time of the second probing packet and the interval between packet pairs.

   void probe2Arrival(int pktsz = 0)
   {
       // Lock access to the packet Window
       CGuard cg(m_lockProbeWindow);

       m_CurrArrTime = CTimer::getTime();

       // record the probing packets interval
       // Adjust the time for what a complete packet would have take
       int64_t timediff = m_CurrArrTime - m_ProbeTime;
       int64_t timediff_times_pl_size = timediff * CPacket::SRT_MAX_PAYLOAD_SIZE;

       // Let's take it simpler than it is coded here:
       // (stating that a packet has never zero size)
       //
       // probe_case = (now - previous_packet_time) * SRT_MAX_PAYLOAD_SIZE / pktsz;
       //
       // Meaning: if the packet is fully packed, probe_case = timediff.
       // Otherwise the timediff will be "converted" to a time that a fully packed packet "would take",
       // provided the arrival time is proportional to the payload size and skipping
       // the ETH+IP+UDP+SRT header part elliminates the constant packet delivery time influence.
       //
       m_aProbeWindow[m_iProbeWindowPtr] = pktsz ? timediff_times_pl_size / pktsz : int(timediff);

       // OLD CODE BEFORE BSTATS:
       // record the probing packets interval
       // m_aProbeWindow[m_iProbeWindowPtr] = int(m_CurrArrTime - m_ProbeTime);

       // the window is logically circular
       ++ m_iProbeWindowPtr;
       if (m_iProbeWindowPtr == PSIZE)
           m_iProbeWindowPtr = 0;
   }


private:
   int m_aPktWindow[ASIZE];          // packet information window (inter-packet time)
   int m_aBytesWindow[ASIZE];        // 
   int m_iPktWindowPtr;         // position pointer of the packet info. window.
   mutable pthread_mutex_t m_lockPktWindow; // used to synchronize access to the packet window

   int m_aProbeWindow[PSIZE];        // record inter-packet time for probing packet pairs
   int m_iProbeWindowPtr;       // position pointer to the probing window
   mutable pthread_mutex_t m_lockProbeWindow; // used to synchronize access to the probe window

   int m_iLastSentTime;         // last packet sending time
   int m_iMinPktSndInt;         // Minimum packet sending interval

   uint64_t m_LastArrTime;      // last packet arrival time
   uint64_t m_CurrArrTime;      // current packet arrival time
   uint64_t m_ProbeTime;        // arrival time of the first probing packet

private:
   CPktTimeWindow(const CPktTimeWindow&);
   CPktTimeWindow &operator=(const CPktTimeWindow&);
};


#endif
