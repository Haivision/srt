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

#include "list.h"
#include "packet.h"
#include "logging.h"

// Use "inline namespace" in C++11
namespace srt_logging
{
    extern Logger dlog, mglog;
}

using srt_logging::mglog;


using namespace srt::sync;


CSndLossList::CSndLossList(int size)
    : m_caSeq()
    , m_iHead(-1)
    , m_iLength(0)
    , m_iSize(size)
    , m_iLastInsertPos(-1)
    , m_ListLock()
{
    m_caSeq = new Seq[size];

   // -1 means there is no data in the node
   for (int i = 0; i < size; ++ i)
   {
      m_caSeq[i].data1 = -1;
      m_caSeq[i].data2 = -1;
   }
}

CSndLossList::~CSndLossList()
{
    delete [] m_caSeq;
}

int CSndLossList::insert(int32_t seqno1, int32_t seqno2)
{
   CGuard listguard(m_ListLock);

   if (0 == m_iLength)
   {
      // insert data into an empty list

      m_iHead = 0;
      m_caSeq[m_iHead].data1 = seqno1;
      if (seqno2 != seqno1)
         m_caSeq[m_iHead].data2 = seqno2;

      m_caSeq[m_iHead].next = -1;
      m_iLastInsertPos = m_iHead;

      m_iLength += CSeqNo::seqlen(seqno1, seqno2);

      return m_iLength;
   }

   // otherwise find the position where the data can be inserted
   const int origlen = m_iLength;
   const int offset = CSeqNo::seqoff(m_caSeq[m_iHead].data1, seqno1);
   int loc = (m_iHead + offset + m_iSize) % m_iSize;

   if (offset < 0)
   {
      // Insert data prior to the head pointer
      loc = (m_iHead + 1) % m_iSize;
      while (loc != m_iHead && (-1 != m_caSeq[loc].data1 || -1 != m_caSeq[loc].data2)) // Improve search of first free element
         loc = (loc + 1) % m_iSize;
      if (loc != m_iHead) // Got one free element
      {
         m_caSeq[loc].data1 = seqno1;
         if (seqno2 != seqno1)
            m_caSeq[loc].data2 = seqno2;

         // new node becomes head
         m_caSeq[loc].next = m_iHead;
         m_iHead = loc;
         m_iLastInsertPos = loc;

         m_iLength += CSeqNo::seqlen(seqno1, seqno2);
      } // List is full ... does nothing
   }
   else if (offset > 0)
   {
      if (seqno1 == m_caSeq[loc].data1)
      {
         m_iLastInsertPos = loc;

         // first seqno is equivlent, compare the second
         if (-1 == m_caSeq[loc].data2)
         {
            if (seqno2 != seqno1)
            {
               m_iLength += CSeqNo::seqlen(seqno1, seqno2) - 1;
               m_caSeq[loc].data2 = seqno2;
            }
         }
         else if (CSeqNo::seqcmp(seqno2, m_caSeq[loc].data2) > 0)
         {
            // new seq pair is longer than old pair, e.g., insert [3, 7] to [3, 5], becomes [3, 7]
            m_iLength += CSeqNo::seqlen(m_caSeq[loc].data2, seqno2) - 1;
            m_caSeq[loc].data2 = seqno2;
         }
         else
            // Do nothing if it is already there
            return 0;
      }
      else
      {
         // searching the prior node
         int i;
         if ((-1 != m_iLastInsertPos) && (CSeqNo::seqcmp(m_caSeq[m_iLastInsertPos].data1, seqno1) < 0))
            i = m_iLastInsertPos;
         else
            i = m_iHead;

         while ((-1 != m_caSeq[i].next) && (CSeqNo::seqcmp(m_caSeq[m_caSeq[i].next].data1, seqno1) < 0))
         {
            if (i == m_caSeq[i].next)
               m_caSeq[i].next = -1;
            else
               i = m_caSeq[i].next;
         }

         if ((-1 == m_caSeq[i].data2) || (CSeqNo::seqcmp(m_caSeq[i].data2, seqno1) < 0))
         {
            // no overlap, create new node

            // Search a free node
            int h = loc;
            loc = (h + 1) % m_iSize;
            while (loc != h && (-1 != m_caSeq[loc].data1 || -1 != m_caSeq[loc].data2)) // Improve search of first free element
               loc = (loc + 1) % m_iSize;

            if (loc != h)
            {
               m_iLastInsertPos = loc;

               m_caSeq[loc].data1 = seqno1;
               if (seqno2 != seqno1)
                  m_caSeq[loc].data2 = seqno2;

               m_caSeq[loc].next = m_caSeq[i].next;
               m_caSeq[i].next = loc;

               m_iLength += CSeqNo::seqlen(seqno1, seqno2);
            } // List full ... nothing to do
         }
         else
         {
            m_iLastInsertPos = i;

            // overlap, coalesce with prior node, insert(3, 7) to [2, 5], ... becomes [2, 7]
            if (CSeqNo::seqcmp(m_caSeq[i].data2, seqno2) < 0)
            {
               m_iLength += CSeqNo::seqlen(m_caSeq[i].data2, seqno2) - 1;
               m_caSeq[i].data2 = seqno2;

               loc = i;
            }
            else
               return 0;
         }
      }
   }
   else
   {
      m_iLastInsertPos = m_iHead;

      // insert to head node
      if (seqno2 != seqno1)
      {
         if (-1 == m_caSeq[loc].data2)
         {
            m_iLength += CSeqNo::seqlen(seqno1, seqno2) - 1;
            m_caSeq[loc].data2 = seqno2;
         }
         else if (CSeqNo::seqcmp(seqno2, m_caSeq[loc].data2) > 0)
         {
            m_iLength += CSeqNo::seqlen(m_caSeq[loc].data2, seqno2) - 1;
            m_caSeq[loc].data2 = seqno2;
         }
         else
            return 0;
      }
      else
         return 0;
   }

   // coalesce with next node. E.g., [3, 7], ..., [6, 9] becomes [3, 9]
   while ((-1 != m_caSeq[loc].next) && (-1 != m_caSeq[loc].data2))
   {
      const int i = m_caSeq[loc].next;

      if (CSeqNo::seqcmp(m_caSeq[i].data1, CSeqNo::incseq(m_caSeq[loc].data2)) > 0)
         break;

      // coalesce if there is overlap
      if (-1 != m_caSeq[i].data2)
      {
         if (CSeqNo::seqcmp(m_caSeq[i].data2, m_caSeq[loc].data2) > 0)
         {
            if (CSeqNo::seqcmp(m_caSeq[loc].data2, m_caSeq[i].data1) >= 0)
               m_iLength -= CSeqNo::seqlen(m_caSeq[i].data1, m_caSeq[loc].data2);

            m_caSeq[loc].data2 = m_caSeq[i].data2;
         }
         else
            m_iLength -= CSeqNo::seqlen(m_caSeq[i].data1, m_caSeq[i].data2);
      }
      else
      {
         if (m_caSeq[i].data1 == CSeqNo::incseq(m_caSeq[loc].data2))
            m_caSeq[loc].data2 = m_caSeq[i].data1;
         else
            m_iLength--;
      }

      m_caSeq[i].data1  = -1;
      m_caSeq[i].data2  = -1;
      m_caSeq[loc].next = m_caSeq[i].next;
   }

   return m_iLength - origlen;
}

void CSndLossList::remove(int32_t seqno)
{
   CGuard listguard(m_ListLock);
   removeNoLock(seqno);
}

void CSndLossList::removeNoLock(int32_t seqno)
{
   if (0 == m_iLength)
      return;

   // Remove all from the head pointer to a node with a larger seq. no. or the list is empty
   int offset = CSeqNo::seqoff(m_caSeq[m_iHead].data1, seqno);
   int loc = (m_iHead + offset + m_iSize) % m_iSize;

   if (0 == offset)
   {
      // It is the head. Remove the head and point to the next node if any ?
      if (-1 == m_caSeq[m_iHead].data2)
      {
         m_caSeq[m_iHead].data1 = -1;
         if (m_caSeq[m_iHead].next != -1) // Next node available ?
         {
            loc = m_caSeq[m_iHead].next; // Yes it will be the next head
            m_caSeq[m_iHead].next = -1;
         }
         else
            loc = m_iHead; // No, reset the current head content
      }
      else // data2 available, at least equals to (data1 + 1)
      {
         m_caSeq[m_iHead].data1 = CSeqNo::incseq(seqno);
         m_caSeq[m_iHead].data2 = (m_caSeq[m_iHead].data2 == m_caSeq[m_iHead].data1) ? -1 : m_caSeq[m_iHead].data2;
         loc = m_iHead;
      }
      if (m_iLastInsertPos == m_iHead)
         m_iLastInsertPos = -1;

      m_iHead = loc;
      m_iLength--;
   }
   else if (offset > 0)
   {
      int h = m_iHead;

      if (seqno == m_caSeq[loc].data1)
      {
         m_iHead = removeInNode(loc, seqno);
      }
      else
      {
         // target node is empty, check prior node
         int i = m_iHead;
         while ((-1 != m_caSeq[i].next) && (CSeqNo::seqcmp(m_caSeq[m_caSeq[i].next].data1, seqno) <= 0))
         {
            if (i == m_caSeq[i].next)
               m_caSeq[i].next = -1;
            else
               i = m_caSeq[i].next;
         }

         if (seqno == m_caSeq[i].data1)
            m_iHead = removeInNode(i, seqno);
         else if (-1 == m_caSeq[i].data2)
         {
            if (m_caSeq[i].next != -1)
               m_iHead = m_caSeq[i].next;
            else
            {
               m_iLength--;
               m_caSeq[i].data1 = -1;
               m_caSeq[i].data2 = -1;
               m_iHead = i;
            }
         }
         else if (CSeqNo::seqcmp(m_caSeq[i].data2, seqno) > 0)
         {
            m_iLength -= CSeqNo::seqlen(m_caSeq[i].data1, seqno);
            m_caSeq[i].data1 = CSeqNo::incseq(seqno);
            m_caSeq[i].data2 = (m_caSeq[i].data2 == m_caSeq[i].data1) ? -1 : m_caSeq[i].data2;
            m_iHead = i;
         }
         else if (m_caSeq[i].next != -1)
         {
            m_iHead = m_caSeq[i].next;
         }
         else
         {
            m_iLength -= CSeqNo::seqlen(m_caSeq[i].data1, m_caSeq[i].data2);
            m_caSeq[i].data1 = -1;
            m_caSeq[i].data2 = -1;
            m_caSeq[i].next = -1;
            m_iHead = i;
         }
      }

      // Remove all nodes prior to the new head
      while (h != m_iHead)
      {
         if (m_caSeq[h].data2 != -1)
         {
            m_iLength -= CSeqNo::seqlen(m_caSeq[h].data1, m_caSeq[h].data2);
            m_caSeq[h].data2 = -1;
         }
         else
            m_iLength--;

         m_caSeq[h].data1 = -1;

         if (m_iLastInsertPos == h)
            m_iLastInsertPos = -1;

         const int o = h;
         h = m_caSeq[h].next;
         m_caSeq[o].next = -1;
      }
   } // A negative offset means that nothing has to be done
}

int CSndLossList::getLossLength() const
{
   CGuard listguard(m_ListLock);

   return m_iLength;
}

int32_t CSndLossList::popLostSeq()
{
   CGuard listguard(m_ListLock);

   if (0 == m_iLength)
      return -1;

   if (m_iLastInsertPos == m_iHead)
      m_iLastInsertPos = -1;

   // return the first loss seq. no.
   int32_t seqno = m_caSeq[m_iHead].data1;
   removeNoLock(seqno);
   return seqno;
}

int CSndLossList::removeInNode(int loc, int32_t seqno)
{
   int ret = loc;
   if (-1 == m_caSeq[loc].data2)
   {
      m_caSeq[loc].data1 = -1;
      if (m_caSeq[loc].next != -1) // Next node available ?
      {
         loc = m_caSeq[loc].next; // Yes it will be the next head,
                                  // will be removed by the loop at the end of the function
      }
      else
         m_iLength--;
      ret = loc; // Whatever the use case is, the new head is loc
   }
   else // data2 available, at least equals to (data1 + 1)
   {
      m_iLength--;
      m_caSeq[loc].data1 = CSeqNo::incseq(seqno);
      m_caSeq[loc].data2 = (m_caSeq[loc].data2 == m_caSeq[loc].data1) ? -1 : m_caSeq[loc].data2;
      ret = loc;
   }
   return ret;
}

////////////////////////////////////////////////////////////////////////////////

CRcvLossList::CRcvLossList(int size):
m_caSeq(),
m_iHead(-1),
m_iTail(-1),
m_iLength(0),
m_iSize(size)
{
   m_caSeq = new Seq[m_iSize];

   // -1 means there is no data in the node
   for (int i = 0; i < size; ++ i)
   {
      m_caSeq[i].data1 = -1;
      m_caSeq[i].data2 = -1;
   }
}

CRcvLossList::~CRcvLossList()
{
    delete [] m_caSeq;
}

void CRcvLossList::insert(int32_t seqno1, int32_t seqno2)
{
   // Data to be inserted must be larger than all those in the list
   // guaranteed by the UDT receiver

   if (0 == m_iLength)
   {
      // insert data into an empty list
      m_iHead = 0;
      m_iTail = 0;
      m_caSeq[m_iHead].data1 = seqno1;
      if (seqno2 != seqno1)
         m_caSeq[m_iHead].data2 = seqno2;

      m_caSeq[m_iHead].next = -1;
      m_caSeq[m_iHead].prior = -1;
      m_iLength += CSeqNo::seqlen(seqno1, seqno2);

      return;
   }

   // otherwise searching for the position where the node should be
   int offset = CSeqNo::seqoff(m_caSeq[m_iHead].data1, seqno1);
   if (offset < 0)
   {
       LOGC(mglog.Error, log << "RCV-LOSS/insert: IPE: new LOSS %(" << seqno1 << "-" << seqno2
               << ") PREDATES HEAD %" << m_caSeq[m_iHead].data1 << " -- REJECTING");
       return;
   }

   int loc = (m_iHead + offset) % m_iSize;

   if ((-1 != m_caSeq[m_iTail].data2) && (CSeqNo::incseq(m_caSeq[m_iTail].data2) == seqno1))
   {
      // coalesce with prior node, e.g., [2, 5], [6, 7] becomes [2, 7]
      loc = m_iTail;
      m_caSeq[loc].data2 = seqno2;
   }
   else
   {
      // create new node
      m_caSeq[loc].data1 = seqno1;

      if (seqno2 != seqno1)
         m_caSeq[loc].data2 = seqno2;

      m_caSeq[m_iTail].next = loc;
      m_caSeq[loc].prior = m_iTail;
      m_caSeq[loc].next = -1;
      m_iTail = loc;
   }

   m_iLength += CSeqNo::seqlen(seqno1, seqno2);
}

bool CRcvLossList::remove(int32_t seqno)
{
   if (0 == m_iLength)
      return false;

   // locate the position of "seqno" in the list
   int offset = CSeqNo::seqoff(m_caSeq[m_iHead].data1, seqno);
   if (offset < 0)
      return false;

   int loc = (m_iHead + offset) % m_iSize;

   if (seqno == m_caSeq[loc].data1)
   {
      // This is a seq. no. that starts the loss sequence

      if (-1 == m_caSeq[loc].data2)
      {
         // there is only 1 loss in the sequence, delete it from the node
         if (m_iHead == loc)
         {
            m_iHead = m_caSeq[m_iHead].next;
            if (-1 != m_iHead)
               m_caSeq[m_iHead].prior = -1;
         }
         else
         {
            m_caSeq[m_caSeq[loc].prior].next = m_caSeq[loc].next;
            if (-1 != m_caSeq[loc].next)
               m_caSeq[m_caSeq[loc].next].prior = m_caSeq[loc].prior;
            else
               m_iTail = m_caSeq[loc].prior;
         }

         m_caSeq[loc].data1 = -1;
      }
      else
      {
         // there are more than 1 loss in the sequence
         // move the node to the next and update the starter as the next loss inSeqNo(seqno)

         // find next node
         int i = (loc + 1) % m_iSize;

         // remove the "seqno" and change the starter as next seq. no.
         m_caSeq[i].data1 = CSeqNo::incseq(m_caSeq[loc].data1);

         // process the sequence end
         if (CSeqNo::seqcmp(m_caSeq[loc].data2, CSeqNo::incseq(m_caSeq[loc].data1)) > 0)
            m_caSeq[i].data2 = m_caSeq[loc].data2;

         // remove the current node
         m_caSeq[loc].data1 = -1;
         m_caSeq[loc].data2 = -1;
         // update list pointer
         m_caSeq[i].next = m_caSeq[loc].next;
         m_caSeq[i].prior = m_caSeq[loc].prior;

         if (m_iHead == loc)
            m_iHead = i;
         else
            m_caSeq[m_caSeq[i].prior].next = i;

         if (m_iTail == loc)
            m_iTail = i;
         else
            m_caSeq[m_caSeq[i].next].prior = i;
      }

      m_iLength --;

      return true;
   }

   // There is no loss sequence in the current position
   // the "seqno" may be contained in a previous node

   // searching previous node
   int i = (loc - 1 + m_iSize) % m_iSize;
   while (-1 == m_caSeq[i].data1)
      i = (i - 1 + m_iSize) % m_iSize;

   // not contained in this node, return
   if ((-1 == m_caSeq[i].data2) || (CSeqNo::seqcmp(seqno, m_caSeq[i].data2) > 0))
       return false;

   if (seqno == m_caSeq[i].data2)
   {
      // it is the sequence end

      if (seqno == CSeqNo::incseq(m_caSeq[i].data1))
         m_caSeq[i].data2 = -1;
      else
         m_caSeq[i].data2 = CSeqNo::decseq(seqno);
   }
   else
   {
      // split the sequence

      // construct the second sequence from CSeqNo::incseq(seqno) to the original sequence end
      // located at "loc + 1"
      loc = (loc + 1) % m_iSize;

      m_caSeq[loc].data1 = CSeqNo::incseq(seqno);
      if (CSeqNo::seqcmp(m_caSeq[i].data2, m_caSeq[loc].data1) > 0)
         m_caSeq[loc].data2 = m_caSeq[i].data2;

      // the first (original) sequence is between the original sequence start to CSeqNo::decseq(seqno)
      if (seqno == CSeqNo::incseq(m_caSeq[i].data1))
         m_caSeq[i].data2 = -1;
      else
         m_caSeq[i].data2 = CSeqNo::decseq(seqno);

      // update the list pointer
      m_caSeq[loc].next = m_caSeq[i].next;
      m_caSeq[i].next = loc;
      m_caSeq[loc].prior = i;

      if (m_iTail == i)
         m_iTail = loc;
      else
         m_caSeq[m_caSeq[loc].next].prior = loc;
   }

   m_iLength --;

   return true;
}

bool CRcvLossList::remove(int32_t seqno1, int32_t seqno2)
{
   if (seqno1 <= seqno2)
   {
      for (int32_t i = seqno1; i <= seqno2; ++ i)
         remove(i);
   }
   else
   {
      for (int32_t j = seqno1; j < CSeqNo::m_iMaxSeqNo; ++ j)
         remove(j);
      for (int32_t k = 0; k <= seqno2; ++ k)
         remove(k);
   }

   return true;
}

bool CRcvLossList::find(int32_t seqno1, int32_t seqno2) const
{
   if (0 == m_iLength)
      return false;

   int p = m_iHead;

   while (-1 != p)
   {
      if ((CSeqNo::seqcmp(m_caSeq[p].data1, seqno1) == 0) ||
          ((CSeqNo::seqcmp(m_caSeq[p].data1, seqno1) > 0) && (CSeqNo::seqcmp(m_caSeq[p].data1, seqno2) <= 0)) ||
          ((CSeqNo::seqcmp(m_caSeq[p].data1, seqno1) < 0) && (m_caSeq[p].data2 != -1) && CSeqNo::seqcmp(m_caSeq[p].data2, seqno1) >= 0))
          return true;

      p = m_caSeq[p].next;
   }

   return false;
}

int CRcvLossList::getLossLength() const
{
   return m_iLength;
}

int CRcvLossList::getFirstLostSeq() const
{
   if (0 == m_iLength)
      return -1;

   return m_caSeq[m_iHead].data1;
}

void CRcvLossList::getLossArray(int32_t* array, int& len, int limit)
{
   len = 0;

   int i = m_iHead;

   while ((len < limit - 1) && (-1 != i))
   {
      array[len] = m_caSeq[i].data1;
      if (-1 != m_caSeq[i].data2)
      {
         // there are more than 1 loss in the sequence
         array[len] |= LOSSDATA_SEQNO_RANGE_FIRST;
         ++ len;
         array[len] = m_caSeq[i].data2;
      }

      ++ len;

      i = m_caSeq[i].next;
   }
}


CRcvFreshLoss::CRcvFreshLoss(int32_t seqlo, int32_t seqhi, int initial_age)
    : ttl(initial_age)
    , timestamp(steady_clock::now())
{
    seq[0] = seqlo;
    seq[1] = seqhi;
}


CRcvFreshLoss::Emod CRcvFreshLoss::revoke(int32_t sequence)
{
    int32_t diffbegin = CSeqNo::seqcmp(sequence, seq[0]);
    int32_t diffend = CSeqNo::seqcmp(sequence, seq[1]);

    if ( diffbegin < 0 || diffend > 0 )
    {
        return NONE; // not within the range at all.
    }

    if ( diffbegin == 0 )
    {
        if ( diffend == 0 ) // exactly at begin and end
        {
            return DELETE;
        }

        // only exactly at begin. Shrink the range
        seq[0] = CSeqNo::incseq(seq[0]);
        return STRIPPED;
    }

    if ( diffend == 0 ) // exactly at end
    {
        seq[1] = CSeqNo::decseq(seq[1]);
        return STRIPPED;
    }

    return SPLIT;
}

CRcvFreshLoss::Emod CRcvFreshLoss::revoke(int32_t lo, int32_t hi)
{
    // This should only if the range lo-hi is anyhow covered by seq[0]-seq[1].

    // Note: if the checked item contains sequences that are OLDER
    // than the oldest sequence in this range, they should be deleted,
    // even though this wasn't explicitly requested.

    // LOHI:               <lo, hi>
    // ITEM:  <lo, hi>                      <--- delete
    // If the sequence range is older than the range to be revoked,
    // delete it anyway.
    if ( CSeqNo::seqcmp(lo, seq[1]) > 0 )
        return DELETE;

    // LOHI:  <lo, hi>
    // ITEM:             <lo, hi>  <-- NOTFOUND
    // This element is newer than the given sequence, so match failed.
    if ( CSeqNo::seqcmp(hi, seq[0]) < 0 )
        return NONE;

    // LOHI:     <lo,     hi>
    // ITEM:       <lo,    !     hi>
    // RESULT:            <lo,   hi>
    // 2. If the 'hi' is in the middle (less than seq[1]), delete partially.
    // That is, take care of this range for itself and return STRIPPED.
    if ( CSeqNo::seqcmp(hi, seq[1]) < 0 )
    {
        seq[0] = CSeqNo::incseq(hi);
        return STRIPPED;
    }

    // LOHI:            <lo,         hi>
    // ITEM:       <lo,    !     hi>
    // RESULT: DELETE.
    // 3. Otherwise delete the record, even if this was covering only part of this range.
    // This is not possible that the sequences OLDER THAN THIS are not required to be
    // revoken together with this one.

    return DELETE;
}
