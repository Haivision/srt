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

namespace srt
{
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

Status acknowledge(AckNode* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t jrn, const steady_clock::time_point& currtime, int32_t& w_ack, int32_t& w_timediff)
{
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
    // Contiguous is by extracting two contiguous ranges in case when the
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
    w_timediff = count_microseconds(currtime - r_aSeq[found].tsTimeStamp);

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
Status old_acknowledge(AckNode* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t jrn, int32_t& w_ack, const steady_clock::time_point& currtime, int32_t& w_timediff)
{
   // Head has not exceeded the physical boundary of the window
   if (r_iHead >= r_iTail)
   {
      for (int i = r_iTail, n = r_iHead; i < n; ++ i)
      {
         // looking for an identical ACK AckNode. No.
         if (jrn == r_aSeq[i].iJournal)
         {
            // Return the Data ACK it carried
            w_ack = r_aSeq[i].iAckSeq;

            // Calculate RTT estimate
            w_timediff = (int32_t)count_microseconds(currtime - r_aSeq[i].tsTimeStamp);

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

      // The record about ACK is not found in the buffer, RTT can not be calculated
      return ROGUE;
   }

   // Head has exceeded the physical window boundary, so it is behind tail
   for (int j = r_iTail, n = r_iHead + (int)size; j < n; ++ j)
   {
      // Looking for an identical ACK Seq. No.
      if (jrn == r_aSeq[j % size].iJournal)
      {
         // Return the Data ACK it carried
         j %= size;
         w_ack = r_aSeq[j].iAckSeq;

         // Calculate RTT estimate
         w_timediff = (int32_t)count_microseconds(currtime - r_aSeq[j].tsTimeStamp);

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

   // The record about ACK is not found in the buffer, RTT can not be calculated
   return ROGUE;
}
*/
} // namespace AckTools
} // namespace srt

////////////////////////////////////////////////////////////////////////////////

void srt::CPktTimeWindowTools::initializeWindowArrays(int* r_pktWindow, int* r_probeWindow, int* r_bytesWindow, size_t asize, size_t psize, size_t max_payload_size)
{
   for (size_t i = 0; i < asize; ++ i)
      r_pktWindow[i] = 1000000;   //1 sec -> 1 pkt/sec

   for (size_t k = 0; k < psize; ++ k)
      r_probeWindow[k] = 1000;    //1 msec -> 1000 pkts/sec

   for (size_t i = 0; i < asize; ++ i)
      r_bytesWindow[i] = int(max_payload_size); //based on 1 pkt/sec set in r_pktWindow[i]
}

int srt::CPktTimeWindowTools::ceilPerMega(double value, double count)
{
    static const double MEGA = 1000.0 * 1000.0;
    return int(::ceil(MEGA / (value / count)));
}

int srt::CPktTimeWindowTools::getPktRcvSpeed_in(const int* window, int* replica, const int* abytes, size_t asize, size_t hdr_size, int& w_bytesps)
{
    PassFilter<int> filter = GetPeakRange(window, replica, asize);

    unsigned count = 0;
    int sum = 0;

    w_bytesps = 0;
    unsigned long bytes = 0;
    // // (explicit specialization due to problems on MSVC 2013 and 2015)
    AccumulatePassFilterParallel<unsigned, unsigned long>(window, asize, filter, abytes,
            (sum), (count), (bytes));

    // calculate speed, or return 0 if not enough valid value
    if (count <= (asize/2))
    {
        w_bytesps = 0;
        return 0;
    }

    bytes += (unsigned long)(hdr_size * count); //Add protocol headers to bytes received
    w_bytesps = ceilPerMega(sum, bytes);
    return ceilPerMega(sum, count);
}

int srt::CPktTimeWindowTools::getBandwidth_in(const int* window, int* replica, size_t psize)
{
    PassFilter<int> filter = GetPeakRange(window, replica, psize);

    int sum, count;
    Tie2(sum, count) = AccumulatePassFilter(window, psize, filter);
    sum   += filter.median;
    count += 1;

    return ceilPerMega(sum, count);
}


