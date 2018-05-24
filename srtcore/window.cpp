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

#include <cmath>
#include <cstring>
#include "common.h"
#include "window.h"
#include <algorithm>

using namespace std;

namespace ACKWindowTools
{

void store(Seq* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t seq, int32_t ack)
{
   r_aSeq[r_iHead].iACKSeqNo = seq;
   r_aSeq[r_iHead].iACK = ack;
   r_aSeq[r_iHead].TimeStamp = CTimer::getTime();

   r_iHead = (r_iHead + 1) % size;

   // overwrite the oldest ACK since it is not likely to be acknowledged
   if (r_iHead == r_iTail)
      r_iTail = (r_iTail + 1) % size;
}

int acknowledge(Seq* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t seq, int32_t& r_ack)
{
   if (r_iHead >= r_iTail)
   {
      // Head has not exceeded the physical boundary of the window

      for (int i = r_iTail, n = r_iHead; i < n; ++ i)
      {
         // looking for indentical ACK Seq. No.
         if (seq == r_aSeq[i].iACKSeqNo)
         {
            // return the Data ACK it carried
            r_ack = r_aSeq[i].iACK;

            // calculate RTT
            int rtt = int(CTimer::getTime() - r_aSeq[i].TimeStamp);

            if (i + 1 == r_iHead)
            {
               r_iTail = r_iHead = 0;
               r_aSeq[0].iACKSeqNo = -1;
            }
            else
               r_iTail = (i + 1) % size;

            return rtt;
         }
      }

      // Bad input, the ACK node has been overwritten
      return -1;
   }

   // Head has exceeded the physical window boundary, so it is behind tail
   for (int j = r_iTail, n = r_iHead + size; j < n; ++ j)
   {
      // looking for indentical ACK seq. no.
      if (seq == r_aSeq[j % size].iACKSeqNo)
      {
         // return Data ACK
         j %= size;
         r_ack = r_aSeq[j].iACK;

         // calculate RTT
         int rtt = int(CTimer::getTime() - r_aSeq[j].TimeStamp);

         if (j == r_iHead)
         {
            r_iTail = r_iHead = 0;
            r_aSeq[0].iACKSeqNo = -1;
         }
         else
            r_iTail = (j + 1) % size;

         return rtt;
      }
   }

   // bad input, the ACK node has been overwritten
   return -1;
}
}

////////////////////////////////////////////////////////////////////////////////

void CPktTimeWindowTools::initializeWindowArrays(int* r_pktWindow, int* r_probeWindow, int* r_bytesWindow, size_t asize, size_t psize)
{
   for (size_t i = 0; i < asize; ++ i)
      r_pktWindow[i] = 1000000;   //1 sec -> 1 pkt/sec

   for (size_t k = 0; k < psize; ++ k)
      r_probeWindow[k] = 1000;    //1 msec -> 1000 pkts/sec

   for (size_t i = 0; i < asize; ++ i)
      r_bytesWindow[i] = (1500 - SRT_DATA_PKTHDR_SIZE); //based on 1 pkt/sec set in r_pktWindow[i]
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
////#ifdef SRT_ENABLE_BSTATS
         bytes += (unsigned long)*bp;   //byte counter
      }
      ++ p;     //advance packet pointer
      ++ bp;    //advance bytes pointer
   }

   // claculate speed, or return 0 if not enough valid value
   if (count > (asize >> 1))
   {
      bytes += (SRT_DATA_PKTHDR_SIZE * count); //Add protocol headers to bytes received
      bytesps = (unsigned long)ceil(1000000.0 / (double(sum) / double(bytes)));
      return (int)ceil(1000000.0 / (sum / count));
   }
   else
   {
      bytesps = 0;
      return 0;
   }
/* #else
      }
      ++ p;
   }

   // claculate speed, or return 0 if not enough valid value
   if (count > (ASIZE >> 1))
      return (int)ceil(1000000.0 / (sum / count));
   else
      return 0;
#endif
*/
}

int CPktTimeWindowTools::getBandwidth_in(const int* window, int* replica, size_t psize)
{
   // get median value, but cannot change the original value order in the window
   std::copy(window, window + psize - 1, replica);
   std::nth_element(replica, replica + (psize / 2), replica + psize - 1);
   //std::sort(replica, replica + psize);
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


