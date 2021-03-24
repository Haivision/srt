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

#include "platform_sys.h"

#include <cmath>
#include <cstring>
#include "common.h"
#include "window.h"
#include <algorithm>

using namespace std;
using namespace srt::sync;

namespace ACKWindow
{

void store(AckNode* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t jrn, int32_t ackseq)
{
   r_aSeq[r_iHead].tsTimeStamp = steady_clock::now();
   r_aSeq[r_iHead].iJournal = jrn;
   r_aSeq[r_iHead].iAckSeq = ackseq;

   r_iHead = (r_iHead + 1) % size;

   // Overwrite the oldest ACK since it is not likely to be acknowledged.
   // Eat your own tail.
   if (r_iHead == r_iTail)
      r_iTail = (r_iTail + 1) % size;
}

struct Range
{
    int begin, end;
};

struct FIsJournal
{
    int32_t jrn;
    FIsJournal(int32_t v): jrn(v) {}

    bool operator()(const AckNode& node) const
    {
        return node.iJournal == jrn;
    }
};

Status acknowledge(AckNode* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t jrn, int32_t& w_ack, int32_t& w_timediff)
{
    steady_clock::time_point now = steady_clock::now();

    Range range1, range2;
    range1.begin = r_iTail;
    range2.begin = 0;
    if (r_iHead < r_iTail)
    {
        // range2: [0 ... r_iHead] ... range1:[r_iTail ... end]
        range1.end = size;
        range2.end = r_iHead;
    }
    else
    {
        // [0 ... r_iTail-1] range1:[r_iTail ... r_iHead] [... end], range2: [0-0] (empty)
        range1.end = r_iHead;
        range2.end = 0;
    }

    // Here we are certain that the range1 is contiguous and nonempty
    // Continuous is by extracting two contiguous ranges in case when the
    // original range was non-contiguous.
    // Emptiness is checked here:
    if (range1.begin == range1.end)
    {
        // This can be as well rogue, but with empty
        // container it would cost a lot of checks to
        // confirm that it was the case, not worth a shot.
        return OLD;
    }

    // Check the first range.
    // The first range is always "older" than the second range, if the second one exists.
    if (CSeqNo::seqcmp(jrn, r_aSeq[range1.begin].iJournal) < 0)
    {
        return OLD;
    }

    int found = -1;

    if (CSeqNo::seqcmp(jrn, r_aSeq[range1.end-1].iJournal) <= 0)
    {
        // We have the value within this range, check if exists.
        AckNode* pos = std::find_if(r_aSeq + range1.begin, r_aSeq + range1.end, FIsJournal(jrn));
        if (pos == r_aSeq + range1.end)
            return WIPED;

        found = pos - r_aSeq;
    }
    else
    {
        // Not within the first range, check the second range.
        // If second range is empty, report this as ROGUE.
        if (range2.begin == range2.end)
        {
            return ROGUE;
        }

        if (CSeqNo::seqcmp(jrn, r_aSeq[range2.begin].iJournal < 0))
        {
            // The value is above range1, but below range2. Hence, not found.
            return WIPED;
        }

        if (CSeqNo::seqcmp(jrn, r_aSeq[range2.end-1].iJournal) <= 0)
        {
            // We have the value within this range, check if exists.
            AckNode* pos = std::find_if(r_aSeq + range2.begin, r_aSeq + range2.end, FIsJournal(jrn));
            if (pos == r_aSeq + range1.end)
                return WIPED;
            found = pos - r_aSeq;
        }
        else
        {
            // ABOVE range2 - ROGUE
            return ROGUE;
        }
    }

    // As long as none of the above did abnormal termination by early return,
    // pos contains our required node.
    w_ack = r_aSeq[found].iAckSeq;
    w_timediff = count_microseconds(now - r_aSeq[found].tsTimeStamp);

    int inext = found + 1;
    if (inext == r_iHead)
    {
        // Clear the container completely.
        r_iHead = 0;
        r_iTail = 0;
        r_aSeq[0].iJournal = SRT_SEQNO_NONE;
        r_aSeq[0].iAckSeq = SRT_SEQNO_NONE;
        r_aSeq[0].tsTimeStamp = steady_clock::time_point();
    }
    else
    {
        // Just cut the tail.
        if (inext == int(size))
        {
            inext = 0;
        }
        r_iTail = inext;
        // Keep r_iHead in existing position.
    }

    return OK;
}

/* Updated old version remains for historical reasons
Status old_acknowledge(AckNode* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t jrn, int32_t& w_ack, int32_t& w_timediff)
{
   if (r_iHead >= r_iTail)
   {
      // Head has not exceeded the physical boundary of the window

      for (int i = r_iTail, n = r_iHead; i < n; ++ i)
      {
         // looking for identical ACK AckNode. No.
         if (jrn == r_aSeq[i].iJournal)
         {
            // return the Data ACK it carried
            w_ack = r_aSeq[i].iAckSeq;

            // calculate RTT
            w_timediff = count_microseconds(steady_clock::now() - r_aSeq[i].tsTimeStamp);

            if (i + 1 == r_iHead)
            {
               r_iTail = r_iHead = 0;
               r_aSeq[0].iJournal = SRT_SEQNO_NONE;
            }
            else
               r_iTail = (i + 1) % size;

            return OK;
         }
      }

      // Bad input, the ACK node has been overwritten
      return ROGUE;
   }

   // Head has exceeded the physical window boundary, so it is behind tail
   for (int j = r_iTail, n = r_iHead + size; j < n; ++ j)
   {
      // looking for indentical ACK jrn. no.
      if (jrn == r_aSeq[j % size].iJournal)
      {
         // return Data ACK
         j %= size;
         w_ack = r_aSeq[j].iAckSeq;

         // calculate RTT
         w_timediff = count_microseconds(steady_clock::now() - r_aSeq[j].tsTimeStamp);

         if (j == r_iHead)
         {
            r_iTail = r_iHead = 0;
            r_aSeq[0].iJournal = -1;
         }
         else
            r_iTail = (j + 1) % size;

         return OK;
      }
   }

   // bad input, the ACK node has been overwritten
   return ROGUE;
}
*/
}

////////////////////////////////////////////////////////////////////////////////

void CPktTimeWindowTools::initializeWindowArrays(int* r_pktWindow, int* r_probeWindow, int* r_bytesWindow, size_t asize, size_t psize)
{
   for (size_t i = 0; i < asize; ++ i)
      r_pktWindow[i] = 1000000;   //1 sec -> 1 pkt/sec

   for (size_t k = 0; k < psize; ++ k)
      r_probeWindow[k] = 1000;    //1 msec -> 1000 pkts/sec

   for (size_t i = 0; i < asize; ++ i)
      r_bytesWindow[i] = CPacket::SRT_MAX_PAYLOAD_SIZE; //based on 1 pkt/sec set in r_pktWindow[i]
}


int CPktTimeWindowTools::getPktRcvSpeed_in(const int* window, int* replica, const int* abytes, size_t asize, int& bytesps)
{
   // get median value, but cannot change the original value order in the window
   std::copy(window, window + asize, replica);
   std::nth_element(replica, replica + (asize / 2), replica + asize);
   //std::sort(replica, replica + asize);
   int median = replica[asize / 2];

   unsigned count = 0;
   int sum = 0;
   int upper = median << 3;
   int lower = median >> 3;

   bytesps = 0;
   unsigned long bytes = 0;
   const int* bp = abytes;
   // median filtering
   const int* p = window;
   for (int i = 0, n = asize; i < n; ++ i)
   {
      if ((*p < upper) && (*p > lower))
      {
         ++ count;  //packet counter
         sum += *p; //usec counter
         bytes += (unsigned long)*bp;   //byte counter
      }
      ++ p;     //advance packet pointer
      ++ bp;    //advance bytes pointer
   }

   // claculate speed, or return 0 if not enough valid value
   if (count > (asize >> 1))
   {
      bytes += (CPacket::SRT_DATA_HDR_SIZE * count); //Add protocol headers to bytes received
      bytesps = (unsigned long)ceil(1000000.0 / (double(sum) / double(bytes)));
      return (int)ceil(1000000.0 / (sum / count));
   }
   else
   {
      bytesps = 0;
      return 0;
   }
}

int CPktTimeWindowTools::getBandwidth_in(const int* window, int* replica, size_t psize)
{
    // This calculation does more-less the following:
    //
    // 1. Having example window:
    //  - 50, 51, 100, 55, 80, 1000, 600, 1500, 1200, 10, 90
    // 2. This window is now sorted, but we only know the value in the middle:
    //  - 10, 50, 51, 55, 80, [[90]], 100, 600, 1000, 1200, 1500
    // 3. Now calculate:
    //   - lower: 90/8 = 11.25
    //   - upper: 90*8 = 720
    // 4. Now calculate the arithmetic median from all these values,
    //    but drop those from outside the <lower, upper> range:
    //  - 10, (11<) [ 50, 51, 55, 80, 90, 100, 600, ] (>720) 1000, 1200, 1500
    // 5. Calculate the median from the extracted range,
    //    NOTE: the median is actually repeated once, so size is +1.
    //
    //    values = { 50, 51, 55, 80, 90, 100, 600 };
    //    sum = 90 + accumulate(values); ==> 1026
    //    median = sum/(1 + values.size()); ==> 147
    //
    // For comparison: the overall arithmetic median from this window == 430
    //
    // 6. Returned value = 1M/median

   // get median value, but cannot change the original value order in the window
   std::copy(window, window + psize - 1, replica);
   std::nth_element(replica, replica + (psize / 2), replica + psize - 1);
   //std::sort(replica, replica + psize); <--- was used for debug, just leave it as a mark
   int median = replica[psize / 2];

   int count = 1;
   int sum = median;
   int upper = median << 3; // median*8
   int lower = median >> 3; // median/8

   // median filtering
   const int* p = window;
   for (int i = 0, n = psize; i < n; ++ i)
   {
      if ((*p < upper) && (*p > lower))
      {
         ++ count;
         sum += *p;
      }
      ++ p;
   }

   return (int)ceil(1000000.0 / (double(sum) / double(count)));
}


