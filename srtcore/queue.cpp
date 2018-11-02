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
   Yunhong Gu, last updated 05/05/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifdef WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif
#include <cstring>

#include "common.h"
#include "api.h"
#include "netinet_any.h"
#include "threadname.h"
#include "logging.h"
#include "queue.h"

using namespace std;


CUnitQueue::CUnitQueue():
m_pQEntry(NULL),
m_pCurrQueue(NULL),
m_pLastQueue(NULL),
m_iSize(0),
m_iCount(0),
m_iMSS(),
m_iIPversion()
{
}

CUnitQueue::~CUnitQueue()
{
   CQEntry* p = m_pQEntry;

   while (p != NULL)
   {
      delete [] p->m_pUnit;
      delete [] p->m_pBuffer;

      CQEntry* q = p;
      if (p == m_pLastQueue)
         p = NULL;
      else
         p = p->m_pNext;
      delete q;
   }
}

int CUnitQueue::init(int size, int mss, int version)
{
   CQEntry* tempq = NULL;
   CUnit* tempu = NULL;
   char* tempb = NULL;

   try
   {
      tempq = new CQEntry;
      tempu = new CUnit [size];
      tempb = new char [size * mss];
   }
   catch (...)
   {
      delete tempq;
      delete [] tempu;
      delete [] tempb;

      return -1;
   }

   for (int i = 0; i < size; ++ i)
   {
      tempu[i].m_iFlag = CUnit::FREE;
      tempu[i].m_Packet.m_pcData = tempb + i * mss;
   }
   tempq->m_pUnit = tempu;
   tempq->m_pBuffer = tempb;
   tempq->m_iSize = size;

   m_pQEntry = m_pCurrQueue = m_pLastQueue = tempq;
   m_pQEntry->m_pNext = m_pQEntry;

   m_pAvailUnit = m_pCurrQueue->m_pUnit;

   m_iSize = size;
   m_iMSS = mss;
   m_iIPversion = version;

   return 0;
}

// XXX High common part detected with CUnitQueue:init.
// Consider merging.
int CUnitQueue::increase()
{
   // adjust/correct m_iCount
   int real_count = 0;
   CQEntry* p = m_pQEntry;
   while (p != NULL)
   {
      CUnit* u = p->m_pUnit;
      for (CUnit* end = u + p->m_iSize; u != end; ++ u)
         if (u->m_iFlag != CUnit::FREE)
            ++ real_count;

      if (p == m_pLastQueue)
         p = NULL;
      else
         p = p->m_pNext;
   }
   m_iCount = real_count;
   if (double(m_iCount) / m_iSize < 0.9)
      return -1;

   CQEntry* tempq = NULL;
   CUnit* tempu = NULL;
   char* tempb = NULL;

   // all queues have the same size
   int size = m_pQEntry->m_iSize;

   try
   {
      tempq = new CQEntry;
      tempu = new CUnit [size];
      tempb = new char [size * m_iMSS];
   }
   catch (...)
   {
      delete tempq;
      delete [] tempu;
      delete [] tempb;

      return -1;
   }

   for (int i = 0; i < size; ++ i)
   {
      tempu[i].m_iFlag = CUnit::FREE;
      tempu[i].m_Packet.m_pcData = tempb + i * m_iMSS;
   }
   tempq->m_pUnit = tempu;
   tempq->m_pBuffer = tempb;
   tempq->m_iSize = size;

   m_pLastQueue->m_pNext = tempq;
   m_pLastQueue = tempq;
   m_pLastQueue->m_pNext = m_pQEntry;

   m_iSize += size;

   return 0;
}

int CUnitQueue::shrink()
{
   // currently queue cannot be shrunk.
   return -1;
}

CUnit* CUnitQueue::getNextAvailUnit()
{
   if (m_iCount * 10 > m_iSize * 9)
      increase();

   if (m_iCount >= m_iSize)
      return NULL;

   CQEntry* entrance = m_pCurrQueue;

   do
   {
      for (CUnit* sentinel = m_pCurrQueue->m_pUnit + m_pCurrQueue->m_iSize - 1; m_pAvailUnit != sentinel; ++ m_pAvailUnit)
         if (m_pAvailUnit->m_iFlag == CUnit::FREE)
            return m_pAvailUnit;

      if (m_pCurrQueue->m_pUnit->m_iFlag == CUnit::FREE)
      {
         m_pAvailUnit = m_pCurrQueue->m_pUnit;
         return m_pAvailUnit;
      }

      m_pCurrQueue = m_pCurrQueue->m_pNext;
      m_pAvailUnit = m_pCurrQueue->m_pUnit;
   } while (m_pCurrQueue != entrance);

   increase();

   return NULL;
}


CSndUList::CSndUList():
    m_pHeap(NULL),
    m_iArrayLength(4096),
    m_iLastEntry(-1),
    m_ListLock(),
    m_pWindowLock(NULL),
    m_pWindowCond(NULL),
    m_pTimer(NULL)
{
    m_pHeap = new CSNode*[m_iArrayLength];
    pthread_mutex_init(&m_ListLock, NULL);
}

CSndUList::~CSndUList()
{
    delete [] m_pHeap;
    pthread_mutex_destroy(&m_ListLock);
}

void CSndUList::insert(int64_t ts, const CUDT* u)
{
   CGuard listguard(m_ListLock, "List");

   // increase the heap array size if necessary
   if (m_iLastEntry == m_iArrayLength - 1)
   {
      CSNode** temp = NULL;

      try
      {
         temp = new CSNode*[m_iArrayLength * 2];
      }
      catch(...)
      {
         return;
      }

      memcpy(temp, m_pHeap, sizeof(CSNode*) * m_iArrayLength);
      m_iArrayLength *= 2;
      delete [] m_pHeap;
      m_pHeap = temp;
   }

   insert_(ts, u);
}

void CSndUList::update(const CUDT* u, EReschedule reschedule)
{
   CGuard listguard(m_ListLock, "List");

   CSNode* n = u->m_pSNode;

   if (n->m_iHeapLoc >= 0)
   {
      if (!reschedule) // EReschedule to bool conversion, predicted.
         return;

      if (n->m_iHeapLoc == 0)
      {
         n->m_llTimeStamp_tk = 1;
         m_pTimer->interrupt();
         return;
      }

      remove_(u);
   }

   insert_(1, u);
}

int CSndUList::pop(ref_t<sockaddr_any> r_addr, ref_t<CPacket> r_pkt, ref_t<sockaddr_any> r_src)
{
   CGuard listguard(m_ListLock, "List");

   if (-1 == m_iLastEntry)
      return -1;

   // no pop until the next schedulled time
   uint64_t ts;
   CTimer::rdtsc(ts);
   if (ts < m_pHeap[0]->m_llTimeStamp_tk)
      return -1;

   CUDT* u = m_pHeap[0]->m_pUDT;
   remove_(u);

   if (!u->m_bConnected || u->m_bBroken)
      return -1;

   // pack a packet from the socket
   if (u->packData(r_pkt, Ref(ts), r_src) <= 0)
      return -1;

   *r_addr = u->m_PeerAddr;

   // insert a new entry, ts is the next processing time
   if (ts > 0)
      insert_(ts, u);

   return 1;
}

void CSndUList::remove(const CUDT* u)
{
   CGuard listguard(m_ListLock, "List");

   remove_(u);
}

uint64_t CSndUList::getNextProcTime()
{
   CGuard listguard(m_ListLock, "List");

   if (-1 == m_iLastEntry)
      return 0;

   return m_pHeap[0]->m_llTimeStamp_tk;
}

void CSndUList::insert_(int64_t ts, const CUDT* u)
{
   CSNode* n = u->m_pSNode;

   // do not insert repeated node
   if (n->m_iHeapLoc >= 0)
      return;

   m_iLastEntry ++;
   m_pHeap[m_iLastEntry] = n;
   n->m_llTimeStamp_tk = ts;

   int q = m_iLastEntry;
   int p = q;
   while (p != 0)
   {
      p = (q - 1) >> 1;
      if (m_pHeap[p]->m_llTimeStamp_tk > m_pHeap[q]->m_llTimeStamp_tk)
      {
         CSNode* t = m_pHeap[p];
         m_pHeap[p] = m_pHeap[q];
         m_pHeap[q] = t;
         t->m_iHeapLoc = q;
         q = p;
      }
      else
         break;
   }

   n->m_iHeapLoc = q;

   // an earlier event has been inserted, wake up sending worker
   if (n->m_iHeapLoc == 0)
      m_pTimer->interrupt();

   // first entry, activate the sending queue
   if (0 == m_iLastEntry)
   {
       pthread_mutex_lock(m_pWindowLock);
       pthread_cond_signal(m_pWindowCond);
       pthread_mutex_unlock(m_pWindowLock);
   }
}

void CSndUList::remove_(const CUDT* u)
{
   CSNode* n = u->m_pSNode;

   if (n->m_iHeapLoc >= 0)
   {
      // remove the node from heap
      m_pHeap[n->m_iHeapLoc] = m_pHeap[m_iLastEntry];
      m_iLastEntry --;
      m_pHeap[n->m_iHeapLoc]->m_iHeapLoc = n->m_iHeapLoc;

      int q = n->m_iHeapLoc;
      int p = q * 2 + 1;
      while (p <= m_iLastEntry)
      {
         if ((p + 1 <= m_iLastEntry) && (m_pHeap[p]->m_llTimeStamp_tk > m_pHeap[p + 1]->m_llTimeStamp_tk))
            p ++;

         if (m_pHeap[q]->m_llTimeStamp_tk > m_pHeap[p]->m_llTimeStamp_tk)
         {
            CSNode* t = m_pHeap[p];
            m_pHeap[p] = m_pHeap[q];
            m_pHeap[p]->m_iHeapLoc = p;
            m_pHeap[q] = t;
            m_pHeap[q]->m_iHeapLoc = q;

            q = p;
            p = q * 2 + 1;
         }
         else
            break;
      }

      n->m_iHeapLoc = -1;
   }

   // the only event has been deleted, wake up immediately
   if (0 == m_iLastEntry)
      m_pTimer->interrupt();
}

//
CSndQueue::CSndQueue():
m_WorkerThread(),
m_pSndUList(NULL),
m_pChannel(NULL),
m_pTimer(NULL),
m_WindowLock(),
m_WindowCond(),
m_bClosing(false),
m_ExitCond()
{
    pthread_cond_init(&m_WindowCond, NULL);
    pthread_mutex_init(&m_WindowLock, NULL);
}

CSndQueue::~CSndQueue()
{
   m_bClosing = true;

   if(m_pTimer != NULL)
   {
        m_pTimer->interrupt();
   }

   pthread_mutex_lock(&m_WindowLock);
   pthread_cond_signal(&m_WindowCond);
   pthread_mutex_unlock(&m_WindowLock);
   if (!pthread_equal(m_WorkerThread, pthread_t()))
       pthread_join(m_WorkerThread, NULL);
   pthread_cond_destroy(&m_WindowCond);
   pthread_mutex_destroy(&m_WindowLock);

   delete m_pSndUList;
}

#if ENABLE_LOGGING
    int CSndQueue::m_counter = 0;
#endif

void CSndQueue::init(CChannel* c, CTimer* t)
{
   m_pChannel = c;
   m_pTimer = t;
   m_pSndUList = new CSndUList;
   m_pSndUList->m_pWindowLock = &m_WindowLock;
   m_pSndUList->m_pWindowCond = &m_WindowCond;
   m_pSndUList->m_pTimer = m_pTimer;

#if ENABLE_LOGGING
    ++m_counter;
    std::string thrname = "SRT:SndQ:w" + Sprint(m_counter);
    ThreadName tn(thrname.c_str());
#endif
   if (0 != pthread_create(&m_WorkerThread, NULL, CSndQueue::worker, this))
   {
	   m_WorkerThread = pthread_t();
       throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
   }
}

#ifdef SRT_ENABLE_IPOPTS
int CSndQueue::getIpTTL() const
{
   return m_pChannel ? m_pChannel->getIpTTL() : -1;
}

int CSndQueue::getIpToS() const
{
   return m_pChannel ? m_pChannel->getIpToS() : -1;
}
#endif

void* CSndQueue::worker(void* param)
{
    CSndQueue* self = (CSndQueue*)param;

    THREAD_STATE_INIT("SRT:SndQ:worker");

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
    CTimer::rdtsc(self->m_ullDbgTime);
    self->m_ullDbgPeriod = uint64_t(5000000) * CTimer::getCPUFrequency();
    self->m_ullDbgTime += self->m_ullDbgPeriod;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

    while (!self->m_bClosing)
    {
        uint64_t ts = self->m_pSndUList->getNextProcTime();

#if   defined(SRT_DEBUG_SNDQ_HIGHRATE)
        self->m_WorkerStats.lIteration++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

        if (ts > 0)
        {
            // wait until next processing time of the first socket on the list
            uint64_t currtime;
            CTimer::rdtsc(currtime);

#if      defined(SRT_DEBUG_SNDQ_HIGHRATE)
            if (self->m_ullDbgTime <= currtime) {
                fprintf(stdout, "SndQueue %lu slt:%lu nrp:%lu snt:%lu nrt:%lu ctw:%lu\n",  
                        self->m_WorkerStats.lIteration,
                        self->m_WorkerStats.lSleepTo,
                        self->m_WorkerStats.lNotReadyPop,
                        self->m_WorkerStats.lSendTo,
                        self->m_WorkerStats.lNotReadyTs,  
                        self->m_WorkerStats.lCondWait);
                memset(&self->m_WorkerStats, 0, sizeof(self->m_WorkerStats));
                self->m_ullDbgTime = currtime + self->m_ullDbgPeriod;
            }
#endif   /* SRT_DEBUG_SNDQ_HIGHRATE */

            THREAD_PAUSED();
            if (currtime < ts) 
            {
                self->m_pTimer->sleepto(ts);

#if         defined(HAI_DEBUG_SNDQ_HIGHRATE)
                self->m_WorkerStats.lSleepTo++;
#endif      /* SRT_DEBUG_SNDQ_HIGHRATE */
            }
            THREAD_RESUMED();

            // it is time to send the next pkt
            sockaddr_any addr;
            CPacket pkt;
            sockaddr_any source_addr;
            if (self->m_pSndUList->pop(Ref(addr), Ref(pkt), Ref(source_addr)) < 0)
            {
                continue;

#if         defined(SRT_DEBUG_SNDQ_HIGHRATE)
                self->m_WorkerStats.lNotReadyPop++;
#endif      /* SRT_DEBUG_SNDQ_HIGHRATE */
            }

            HLOGC(mglog.Debug, log << self->CONID() << "chn:SENDING: " << pkt.Info());
            self->m_pChannel->sendto(addr, pkt, source_addr);

#if      defined(SRT_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lSendTo++;
#endif   /* SRT_DEBUG_SNDQ_HIGHRATE */
        }
        else
        {
#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lNotReadyTs++;
#endif   /* SRT_DEBUG_SNDQ_HIGHRATE */

            // wait here if there is no sockets with data to be sent
            THREAD_PAUSED();
            pthread_mutex_lock(&self->m_WindowLock);
            if (!self->m_bClosing && (self->m_pSndUList->m_iLastEntry < 0)) {
                pthread_cond_wait(&self->m_WindowCond, &self->m_WindowLock);

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
                self->m_WorkerStats.lCondWait++;
#endif         /* SRT_DEBUG_SNDQ_HIGHRATE */
            }
            THREAD_RESUMED();
            pthread_mutex_unlock(&self->m_WindowLock);
        }
    }

    THREAD_EXIT();
    return NULL;
}

int CSndQueue::sendto(const sockaddr_any& addr, CPacket& packet, const sockaddr_any& src)
{
   // send out the packet immediately (high priority), this is a control packet
   m_pChannel->sendto(addr, packet, src);
   return packet.getLength();
}


//
CRcvUList::CRcvUList():
m_pUList(NULL),
m_pLast(NULL)
{
}

CRcvUList::~CRcvUList()
{
}

void CRcvUList::insert(const CUDT* u)
{
   CRNode* n = u->m_pRNode;
   CTimer::rdtsc(n->m_llTimeStamp_tk);

   if (NULL == m_pUList)
   {
      // empty list, insert as the single node
      n->m_pPrev = n->m_pNext = NULL;
      m_pLast = m_pUList = n;

      return;
   }

   // always insert at the end for RcvUList
   n->m_pPrev = m_pLast;
   n->m_pNext = NULL;
   m_pLast->m_pNext = n;
   m_pLast = n;
}

void CRcvUList::remove(const CUDT* u)
{
   CRNode* n = u->m_pRNode;

   if (!n->m_bOnList)
      return;

   if (NULL == n->m_pPrev)
   {
      // n is the first node
      m_pUList = n->m_pNext;
      if (NULL == m_pUList)
         m_pLast = NULL;
      else
         m_pUList->m_pPrev = NULL;
   }
   else
   {
      n->m_pPrev->m_pNext = n->m_pNext;
      if (NULL == n->m_pNext)
      {
         // n is the last node
         m_pLast = n->m_pPrev;
      }
      else
         n->m_pNext->m_pPrev = n->m_pPrev;
   }

   n->m_pNext = n->m_pPrev = NULL;
}

void CRcvUList::update(const CUDT* u)
{
   CRNode* n = u->m_pRNode;

   if (!n->m_bOnList)
      return;

   CTimer::rdtsc(n->m_llTimeStamp_tk);

   // if n is the last node, do not need to change
   if (NULL == n->m_pNext)
      return;

   if (NULL == n->m_pPrev)
   {
      m_pUList = n->m_pNext;
      m_pUList->m_pPrev = NULL;
   }
   else
   {
      n->m_pPrev->m_pNext = n->m_pNext;
      n->m_pNext->m_pPrev = n->m_pPrev;
   }

   n->m_pPrev = m_pLast;
   n->m_pNext = NULL;
   m_pLast->m_pNext = n;
   m_pLast = n;
}

//
CHash::CHash():
m_pBucket(NULL),
m_iHashSize(0)
{
}

CHash::~CHash()
{
   for (int i = 0; i < m_iHashSize; ++ i)
   {
      CBucket* b = m_pBucket[i];
      while (NULL != b)
      {
         CBucket* n = b->m_pNext;
         delete b;
         b = n;
      }
   }

   delete [] m_pBucket;
}

void CHash::init(int size)
{
   m_pBucket = new CBucket* [size];

   for (int i = 0; i < size; ++ i)
      m_pBucket[i] = NULL;

   m_iHashSize = size;
}

CUDT* CHash::lookup(int32_t id)
{
   // simple hash function (% hash table size); suitable for socket descriptors
   CBucket* b = m_pBucket[id % m_iHashSize];

   while (NULL != b)
   {
      if (id == b->m_iID)
         return b->m_pUDT;
      b = b->m_pNext;
   }

   return NULL;
}

void CHash::insert(int32_t id, CUDT* u)
{
   CBucket* b = m_pBucket[id % m_iHashSize];

   CBucket* n = new CBucket;
   n->m_iID = id;
   n->m_pUDT = u;
   n->m_pNext = b;

   m_pBucket[id % m_iHashSize] = n;
}

void CHash::remove(int32_t id)
{
   CBucket* b = m_pBucket[id % m_iHashSize];
   CBucket* p = NULL;

   while (NULL != b)
   {
      if (id == b->m_iID)
      {
         if (NULL == p)
            m_pBucket[id % m_iHashSize] = b->m_pNext;
         else
            p->m_pNext = b->m_pNext;

         delete b;

         return;
      }

      p = b;
      b = b->m_pNext;
   }
}


//
CRendezvousQueue::CRendezvousQueue():
m_lRendezvousID(),
m_RIDVectorLock()
{
    pthread_mutex_init(&m_RIDVectorLock, NULL);
}

CRendezvousQueue::~CRendezvousQueue()
{
   pthread_mutex_destroy(&m_RIDVectorLock);

   m_lRendezvousID.clear();
}

void CRendezvousQueue::insert(const SRTSOCKET& id, CUDT* u, const sockaddr_any& addr, uint64_t ttl)
{
   CGuard vg(m_RIDVectorLock, "RIDVector");

   CRL r;
   r.m_iID = id;
   r.m_pUDT = u;
   r.m_PeerAddr = addr;
   r.m_ullTTL = ttl;

   m_lRendezvousID.push_back(r);
   HLOGC(mglog.Debug, log << "RID: adding socket @" << id << " for address: " << SockaddrToString(addr)
           << " expires: " << logging::FormatTime(ttl)
           << " (total connectors: " << m_lRendezvousID.size() << ")");
}

void CRendezvousQueue::remove(const SRTSOCKET& id, bool should_lock)
{
   CGuard vg(m_RIDVectorLock, "RdvQId", should_lock);
   HLOGC(mglog.Debug, log << "RID: socket @" << id << " removed");

   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (i->m_iID == id)
      {
         m_lRendezvousID.erase(i);
         return;
      }
   }
}

CUDT* CRendezvousQueue::retrieve(const sockaddr_any& addr, ref_t<SRTSOCKET> r_id)
{
   CGuard vg(m_RIDVectorLock, "RIDVector");
   SRTSOCKET& id = *r_id;

   // TODO: optimize search
   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (i->m_PeerAddr == addr && ((id == 0) || (id == i->m_iID)))
      {
          HLOGC(mglog.Debug, log << "RID: found id @" << i->m_iID << " while looking for "
                  << (id ? "THIS ID FROM " : "A NEW CONNECTION FROM ")
                  << SockaddrToString(i->m_PeerAddr));
         id = i->m_iID;
         return i->m_pUDT;
      }
   }

#if ENABLE_HEAVY_LOGGING
   std::ostringstream spec;
   if (id == 0)
       spec << "A NEW CONNECTION REQUEST";
   else
       spec << " AGENT @" << id;
   HLOGC(mglog.Debug, log << "RID: NO CONNECTOR FOR ADR:" << SockaddrToString(addr)
           << " while looking for " << spec.str() << " (" << m_lRendezvousID.size() << " connectors total)");
#endif

   return NULL;
}

void CRendezvousQueue::updateConnStatus(EReadStatus rst, EConnectStatus cst, const CPacket& response)
{
    CGuard vg(m_RIDVectorLock, "RIDVector");

    if (m_lRendezvousID.empty())
        return;

    HLOGC(mglog.Debug, log << "updateConnStatus: updating after getting pkt id=" << response.m_iID << " status: " << ConnectStatusStr(cst));

#if ENABLE_HEAVY_LOGGING
    int debug_nupd = 0;
    int debug_nrun = 0;
    int debug_nfail = 0;
#endif

    for (list<CRL>::iterator i = m_lRendezvousID.begin(), i_next = i; i != m_lRendezvousID.end(); i = i_next)
    {
        ++i_next;
        // NOTE: This is a SAFE LOOP.
        // Incrementation will be done at the end, after the processing did not
        // REMOVE the currently processed element. When the element was removed,
        // the iterator value for the next iteration will be taken from erase()'s result.

        // RST_AGAIN happens in case when the last attempt to read a packet from the UDP
        // socket has read nothing. In this case it would be a repeated update, while
        // still waiting for a response from the peer. When we have any other state here
        // (most expectably CONN_CONTINUE or CONN_RENDEZVOUS, which means that a packet has
        // just arrived in this iteration), do the update immetiately (in SRT this also
        // involves additional incoming data interpretation, which wasn't the case in UDT).
        uint64_t then = i->m_pUDT->m_llLastReqTime;
        uint64_t now = CTimer::getTime();
        bool nowstime = true;

        // Use "slow" cyclic responding in case when
        // - RST_AGAIN (no packet was received for whichever socket)
        // - a packet was received, but not for THIS socket
        if (rst == RST_AGAIN || i->m_iID != response.m_iID)
        {
            // If no packet has been received from the peer,
            // avoid sending too many requests, at most 1 request per 250ms
            nowstime = (now - then) > 250000;
            HLOGC(mglog.Debug, log << "RID:@" << i->m_iID << " then=" << then << " now=" << now << " passed=" << (now-then)
                    <<  " <=> 250000 -- now's " << (nowstime ? "" : "NOT ") << "the time");
        }
        else
        {
            HLOGC(mglog.Debug, log << "RID:@" << i->m_iID << " cst=" << ConnectStatusStr(cst) << " -- sending update NOW.");
        }

#if ENABLE_HEAVY_LOGGING
        ++debug_nrun;
#endif
        if (nowstime)
        {
            // XXX This looks like a loop that rolls in infinity without any sleeps
            // inside and makes it once per about 50 calls send a hs conclusion
            // for a randomly sampled rendezvous ID of a socket out of the list.
            // Ok, probably the rendezvous ID should be just one so not much to
            // sample from, but if so, why the container?
            //
            // This must be somehow fixed!
            //
            // Maybe the time should be simply checked once and the whole loop not
            // done when "it's not the time"?
            if (CTimer::getTime() >= i->m_ullTTL)
            {
                HLOGC(mglog.Debug, log << "RID: socket @" << i->m_iID
                        << " removed - EXPIRED ("
                        // The "enforced on FAILURE" is below when processAsyncConnectRequest failed.
                        << (i->m_ullTTL == 0 ? "enforced on FAILURE" : "passed TTL")
                        << "). ");
                // connection timer expired, acknowledge app via epoll
                i->m_pUDT->m_bConnecting = false;
                CUDT::s_UDTUnited.m_EPoll.update_events(i->m_iID, i->m_pUDT->m_sPollID, UDT_EPOLL_ERR, true);
                /*
                 * Setting m_bConnecting to false but keeping socket in rendezvous queue is not a good idea.
                 * Next CUDT::close will not remove it from rendezvous queue (because !m_bConnecting)
                 * and may crash here on next pass.
                 */
                // i_next was preincremented, but this is guaranteed to point to
                // the element next to erased one.
                i_next = m_lRendezvousID.erase(i);
                continue;
            }
            else
            {
                HLOGC(mglog.Debug, log << "RID: socket @" << i->m_iID << " still active...");
            }

            // This queue is used only in case of Async mode (rendezvous or caller-listener).
            // Synchronous connection requests are handled in startConnect() completely.
            if (!i->m_pUDT->m_bSynRecving)
            {
#if ENABLE_HEAVY_LOGGING
                ++debug_nupd;
#endif
                // IMPORTANT INFORMATION concerning changes towards UDT legacy.
                // In the UDT code there was no attempt to interpret any incoming data.
                // All data from the incoming packet were considered to be already deployed into
                // m_ConnRes field, and m_ConnReq field was considered at this time accordingly updated.
                // Therefore this procedure did only one thing: craft a new handshake packet and send it.
                // In SRT this may also interpret extra data (extensions in case when Agent is Responder)
                // and the `response` packet may sometimes contain no data. Therefore the passed `rst`
                // must be checked to distinguish the call by periodic update (RST_AGAIN) from a call
                // due to have received the packet (RST_OK).
                //
                // In the below call, only the underlying `processRendezvous` function will be attempting
                // to interpret these data (for caller-listener this was already done by `processConnectRequest`
                // before calling this function), and it checks for the data presence.
                if (!i->m_pUDT->processAsyncConnectRequest(rst, cst, response, i->m_PeerAddr))
                {
                    LOGC(mglog.Error, log << "RendezvousQueue: processAsyncConnectRequest FAILED. Setting TTL as EXPIRED.");
                    i->m_ullTTL = 0; // Make it expire right now, will be picked up at the next iteration
#if ENABLE_HEAVY_LOGGING
                    ++debug_nfail;
#endif
                }

                // NOTE: safe loop, the incrementation was done before the loop body,
                // so the `i' node can be safely deleted. Just the body must end here.
                continue;
            }
            else
            {
                HLOGC(mglog.Debug, log << "RID: socket @" << i->m_iID << " deemed SYNCHRONOUS, NOT UPDATING");
            }
        }
    }

    HLOGC(mglog.Debug,
            log << "updateConnStatus: " << debug_nupd << "/" << debug_nrun << " sockets updated ("
            << (debug_nrun-debug_nupd) << " useless). REMOVED " << debug_nfail << " sockets."
         );
}

//
CRcvQueue::CRcvQueue():
    m_WorkerThread(),
    m_UnitQueue(),
    m_pRcvUList(NULL),
    m_pHash(NULL),
    m_pChannel(NULL),
    m_pTimer(NULL),
    m_iPayloadSize(),
    m_bClosing(false),
    m_ExitCond(),
    m_LSLock(),
    m_pListener(NULL),
    m_pRendezvousQueue(NULL),
    m_vNewEntry(),
    m_IDLock(),
    m_mBuffer(),
    m_PassLock(),
    m_PassCond()
{
    pthread_mutex_init(&m_PassLock, NULL);
    pthread_cond_init(&m_PassCond, NULL);
    pthread_mutex_init(&m_LSLock, NULL);
    pthread_mutex_init(&m_IDLock, NULL);
}

CRcvQueue::~CRcvQueue()
{
    m_bClosing = true;
	if (!pthread_equal(m_WorkerThread, pthread_t()))
        pthread_join(m_WorkerThread, NULL);
    pthread_mutex_destroy(&m_PassLock);
    pthread_cond_destroy(&m_PassCond);
    pthread_mutex_destroy(&m_LSLock);
    pthread_mutex_destroy(&m_IDLock);

    delete m_pRcvUList;
    delete m_pHash;
    delete m_pRendezvousQueue;

    // remove all queued messages
    for (map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.begin(); i != m_mBuffer.end(); ++ i)
    {
        while (!i->second.empty())
        {
            CPacket* pkt = i->second.front();
            delete [] pkt->m_pcData;
            delete pkt;
            i->second.pop();
        }
    }
}

#if ENABLE_LOGGING
    int CRcvQueue::m_counter = 0;
#endif


void CRcvQueue::init(int qsize, int payload, int version, int hsize, CChannel* cc, CTimer* t)
{
    m_iPayloadSize = payload;

    m_UnitQueue.init(qsize, payload, version);

    m_pHash = new CHash;
    m_pHash->init(hsize);

    m_pChannel = cc;
    m_pTimer = t;

    m_pRcvUList = new CRcvUList;
    m_pRendezvousQueue = new CRendezvousQueue;

#if ENABLE_LOGGING
    ++m_counter;
    std::string thrname = "SRT:RcvQ:w" + Sprint(m_counter);
    ThreadName tn(thrname.c_str());
#endif

    if (0 != pthread_create(&m_WorkerThread, NULL, CRcvQueue::worker, this))
    {
		m_WorkerThread = pthread_t();
        throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
    }
}

void* CRcvQueue::worker(void* param)
{
   CRcvQueue* self = (CRcvQueue*)param;
   sockaddr_any sa ( self->m_UnitQueue.m_iIPversion );
   int32_t id = 0;

   THREAD_STATE_INIT("SRT:RcvQ:worker");

   CUnit* unit = 0;
   EConnectStatus cst = CONN_AGAIN;
   while (!self->m_bClosing)
   {
       bool have_received = false;
       EReadStatus rst = self->worker_RetrieveUnit(Ref(id), Ref(unit), Ref(sa));
       if (rst == RST_OK)
       {
           if ( id < 0 )
           {
               // User error on peer. May log something, but generally can only ignore it.
               // XXX Think maybe about sending some "connection rejection response".
               HLOGC(mglog.Debug, log << self->CONID() << "RECEIVED negative socket id '" << id << "', rejecting (POSSIBLE ATTACK)");
               continue;
           }

           // NOTE: cst state is being changed here.
           // This state should be maintained through any next failed calls to worker_RetrieveUnit.
           // Any error switches this to rejection, just for a case.

           // Note to rendezvous connection. This can accept:
           // - ID == 0 - take the first waiting rendezvous socket
           // - ID > 0  - find the rendezvous socket that has this ID.
           if (id == 0)
           {
               // ID 0 is for connection request, which should be passed to the listening socket or rendezvous sockets
               cst = self->worker_ProcessConnectionRequest(unit, sa);
           }
           else
           {
               // Otherwise ID is expected to be associated with:
               // - an enqueued rendezvous socket
               // - a socket connected to a peer
               cst = self->worker_ProcessAddressedPacket(id, unit, sa);
           }
           HLOGC(mglog.Debug, log << self->CONID() << "worker: result for the unit: " << ConnectStatusStr(cst));
           if (cst == CONN_AGAIN)
           {
               HLOGC(mglog.Debug, log << self->CONID() << "worker: packet not dispatched, continuing reading.");
               continue;
           }
           have_received = true;
       }
       else if (rst == RST_ERROR)
       {
           // According to the description by CChannel::recvfrom, this can be either of:
           // - IPE: all errors except EBADF
           // - socket was closed in the meantime by another thread: EBADF
           // If EBADF, then it's expected that the "closing" state is also set.
           // Check that just to report possible errors, but interrupt the loop anyway.
           if (self->m_bClosing)
           {
               HLOGC(mglog.Debug, log << self->CONID() << "CChannel reported error, but Queue is closing - INTERRUPTING worker.");
           }
           else
           {
               LOGC(mglog.Fatal, log << self->CONID() << "CChannel reported ERROR DURING TRANSMISSION - IPE. INTERRUPTING worker anyway.");
           }
           cst = CONN_REJECT;
           break;
       }
       // OTHERWISE: this is an "AGAIN" situation. No data was read, but the process should continue.


       // take care of the timing event for all UDT sockets
       uint64_t currtime_tk;
       CTimer::rdtsc(currtime_tk);

       CRNode* ul = self->m_pRcvUList->m_pUList;
       uint64_t ctime_tk = currtime_tk - 100000 * CTimer::getCPUFrequency();
       while ((NULL != ul) && (ul->m_llTimeStamp_tk < ctime_tk))
       {
           CUDT* u = ul->m_pUDT;

           if (u->m_bConnected && !u->m_bBroken && !u->m_bClosing)
           {
               u->checkTimers();
               self->m_pRcvUList->update(u);
           }
           else
           {
               HLOGC(mglog.Debug, log << CUDTUnited::CONID(u->m_SocketID) << " SOCKET broken, REMOVING FROM RCV QUEUE/MAP.");
               // the socket must be removed from Hash table first, then RcvUList
               self->m_pHash->remove(u->m_SocketID);
               self->m_pRcvUList->remove(u);
               u->m_pRNode->m_bOnList = false;
           }

           ul = self->m_pRcvUList->m_pUList;
       }

       if ( have_received )
       {
           HLOGC(mglog.Debug, log << "worker: RECEIVED PACKET --> updateConnStatus. cst=" << ConnectStatusStr(cst)
                   << " id=" << id << " pkt-payload-size=" << unit->m_Packet.getLength());
       }

       // Check connection requests status for all sockets in the RendezvousQueue.
       // Pass the connection status from the last call of:
       // worker_ProcessAddressedPacket --->
       // worker_TryAsyncRend_OrStore --->
       // CUDT::processAsyncConnectResponse --->
       // CUDT::processConnectResponse 
       self->m_pRendezvousQueue->updateConnStatus(rst, cst, unit->m_Packet);

       // XXX updateConnStatus may have removed the connector from the list,
       // however there's still m_mBuffer in CRcvQueue for that socket to care about.
   }

   THREAD_EXIT();
   return NULL;
}

EReadStatus CRcvQueue::worker_RetrieveUnit(ref_t<int32_t> r_id, ref_t<CUnit*> r_unit, ref_t<sockaddr_any> r_addr)
{
#ifdef NO_BUSY_WAITING
    m_pTimer->tick();
#endif

    // check waiting list, if new socket, insert it to the list
    while (ifNewEntry())
    {
        CUDT* ne = getNewEntry();
        if (ne)
        {
            HLOGC(mglog.Debug, log << CUDTUnited::CONID(ne->m_SocketID) << " SOCKET pending for connection - ADDING TO RCV QUEUE/MAP");
            m_pRcvUList->insert(ne);
            m_pHash->insert(ne->m_SocketID, ne);
        }
    }
    // find next available slot for incoming packet
    *r_unit = m_UnitQueue.getNextAvailUnit();
    if (!*r_unit)
    {
        // no space, skip this packet
        CPacket temp;
        temp.m_pcData = new char[m_iPayloadSize];
        temp.setLength(m_iPayloadSize);
        THREAD_PAUSED();
        EReadStatus rst = m_pChannel->recvfrom(r_addr, temp);
        THREAD_RESUMED();
        // Note: this will print nothing about the packet details unless heavy logging is on.
        LOGC(mglog.Error, log << CONID() << "LOCAL STORAGE DEPLETED. Dropping 1 packet: " << temp.Info());
        delete [] temp.m_pcData;

        // Be transparent for RST_ERROR, but ignore the correct
        // data read and fake that the packet was dropped.
        return rst == RST_ERROR ? RST_ERROR : RST_AGAIN;
    }

    r_unit->m_Packet.setLength(m_iPayloadSize);

    // reading next incoming packet, recvfrom returns -1 is nothing has been received
    THREAD_PAUSED();
    EReadStatus rst = m_pChannel->recvfrom(r_addr, r_unit->m_Packet);
    THREAD_RESUMED();

    if (rst == RST_OK)
    {
        *r_id = r_unit->m_Packet.m_iID;
        HLOGC(mglog.Debug, log << "INCOMING PACKET: BOUND=" << SockaddrToString(m_pChannel->bindAddressAny())
                << " DEST=" << SockaddrToString(r_unit->m_Packet.m_DestAddr)
                << " " << r_unit->m_Packet.Info());
    }
    return rst;
}

EConnectStatus CRcvQueue::worker_ProcessConnectionRequest(CUnit* unit, const sockaddr_any& addr)
{
    HLOGC(mglog.Debug, log << "Got sockID=0 from " << SockaddrToString(addr) << " - trying to resolve it as a connection request...");
    // Introduced protection because it may potentially happen
    // that another thread could have closed the socket at
    // the same time and inject a bug between checking the
    // pointer for NULL and using it.
    int listener_ret = 0;
    bool have_listener = false;
    {
        CGuard cg(m_LSLock, "LS");
        if (m_pListener)
        {
            LOGC(mglog.Note, log << "PASSING request from: " << SockaddrToString(addr) << " to agent:" << m_pListener->socketID());
            listener_ret = m_pListener->processConnectRequest(addr, unit->m_Packet);
            // XXX This returns some very significant return value, which
            // is completely ignored here.
            // Actually this is the only place in the code where this
            // function is being called, so it's hard to say what the
            // returned value had to serve for.

            // The tricky part is that this is something done "under the hood";
            // if any problem occurs during this process, then this will simply
            // drop the connection request. The only user process that is connected
            // to it is accept() call (or connect() in case of rendezvous), but
            // the system cannot return an error from accept() just because some
            // user was attempting to connect, but formulated the connection
            // request incorrectly.

            // The only thing that could be done in case when the "listen" call
            // fails, is to probably send a short information packet (once; it's
            // not so important to make it reach the target) that the connection
            // has been rejected due to incorrectly formulated request. However
            // just in order to send anything in response, the actual sender must
            // be properly known, and this isn't the case of incorrectly formulated
            // connection request. So, we can only say sorry to ourselves and
            // do nothing.

            have_listener = true;
        }
    }

    // NOTE: Rendezvous sockets do bind(), but not listen(). It means that the socket is
    // ready to accept connection requests, but they are not being redirected to the listener
    // socket, as this is not a listener socket at all. This goes then HERE.

    if ( have_listener ) // That is, the above block with m_pListener->processConnectRequest was executed
    {
        LOGC(mglog.Note, log << CONID() << "Listener managed the connection request from: " << SockaddrToString(addr)
            << " result:" << RequestTypeStr(UDTRequestType(listener_ret)));
        return (listener_ret >= URQ_FAILURE_TYPES ? CONN_REJECT : CONN_CONTINUE);
    }

    // If there's no listener waiting for the packet, just store it into the queue.
    return worker_TryAsyncRend_OrStore(0, unit, addr); // 0 id because the packet came in with that very ID.
}

EConnectStatus CRcvQueue::worker_ProcessAddressedPacket(int32_t id, CUnit* unit, const sockaddr_any& addr)
{
    CUDT* u = m_pHash->lookup(id);
    if ( !u )
    {
        // Pass this to either async rendezvous connection,
        // or store the packet in the queue.
        HLOGC(mglog.Debug, log << "worker_ProcessAddressedPacket: resending to QUEUED socket @" << id);
        return worker_TryAsyncRend_OrStore(id, unit, addr);
    }

    // Found associated CUDT - process this as control or data packet
    // addressed to an associated socket.
    if (u->m_PeerAddr != addr)
    {
        HLOGC(mglog.Debug, log << CONID() << "Packet for SID=" << id << " asoc with " << SockaddrToString(u->m_PeerAddr)
            << " received from " << SockaddrToString(addr) << " (CONSIDERED ATTACK ATTEMPT)");
        // This came not from the address that is the peer associated
        // with the socket. Ignore it.
        return CONN_AGAIN;
    }

    if (!u->m_bConnected || u->m_bBroken || u->m_bClosing)
    {
        // The socket is currently in the process of being disconnected
        // or destroyed. Ignore.
        // XXX send UMSG_SHUTDOWN in this case?
        // XXX May it require mutex protection?
        return CONN_REJECT;
    }

    if (unit->m_Packet.isControl())
        u->processCtrl(unit->m_Packet);
    else
        u->processData(unit);

    u->checkTimers();
    m_pRcvUList->update(u);

    //return CONN_CONTINUE;
    return CONN_RUNNING;
}

// This function responds to the fact that a packet has come
// for a socket that does not expect to receive a normal connection
// request. This can be then:
// - a normal packet of whatever kind, just to be processed by the message loop
// - a rendezvous connection 
// This function then tries to manage the packet as a rendezvous connection
// request in ASYNC mode; when this is not applicable, it stores the packet
// in the "receiving queue" so that it will be picked up in the "main" thread.
EConnectStatus CRcvQueue::worker_TryAsyncRend_OrStore(int32_t id, CUnit* unit, const sockaddr_any& addr)
{
    // This 'retrieve' requires that 'id' be either one of those
    // stored in the rendezvous queue (see CRcvQueue::registerConnector)
    // or simply 0, but then at least the address must match one of these.
    // If the id was 0, it will be set to the actual socket ID of the returned CUDT.
    CUDT* u = m_pRendezvousQueue->retrieve(addr, Ref(id));
    if ( !u )
    {
        // this socket is then completely unknown to the system.
        // Note that this situation may also happen at a very unfortunate
        // coincidence that the socket is already bound, but the registerConnector()
        // has not yet started. In case of rendezvous this may mean that the other
        // side just started sending its handshake packets, the local side has already
        // run the CRcvQueue::worker thread, and this worker thread is trying to dispatch
        // the handshake packet too early, before the dispatcher has a chance to see
        // this socket registerred in the RendezvousQueue, which causes the packet unable
        // to be dispatched. Therefore simply treat every "out of band" packet (with socket
        // not belonging to the connection and not registered as rendezvous) as "possible
        // attack" and ignore it. This also should better protect the rendezvous socket
        // against a rogue connector.
        if ( id == 0 )
        {
            HLOGC(mglog.Debug, log << CONID() << "AsyncOrRND: no sockets expect connection from "
                << SockaddrToString(addr) << " - POSSIBLE ATTACK, ignore packet");
        }
        else
        {
            HLOGC(mglog.Debug, log << CONID() << "AsyncOrRND: no sockets expect socket " << id << " from "
                << SockaddrToString(addr) << " - POSSIBLE ATTACK, ignore packet");
        }
        return CONN_AGAIN; // This means that the packet should be ignored.
    }

    // asynchronous connect: call connect here
    // otherwise wait for the UDT socket to retrieve this packet
    if (!u->m_bSynRecving)
    {
        HLOGC(mglog.Debug, log << "AsyncOrRND: packet RESOLVED TO @" << id << " -- continuing as ASYNC CONNECT");
        // This is practically same as processConnectResponse, just this applies
        // appropriate mutex lock - which can't be done here because it's intentionally private.
        // OTOH it can't be applied to processConnectResponse because the synchronous
        // call to this method applies the lock by itself, and same-thread-double-locking is nonportable (crashable).
        EConnectStatus cst = u->processAsyncConnectResponse(unit->m_Packet);

        if (cst == CONN_CONFUSED)
        {
            LOGC(mglog.Warn, log << "AsyncOrRND: PACKET NOT HANDSHAKE - re-requesting handshake from peer");
            storePkt(id, unit->m_Packet.clone());
            if (!u->processAsyncConnectRequest(RST_AGAIN, CONN_CONTINUE, unit->m_Packet, u->m_PeerAddr))
            {
                // Reuse previous behavior to reject a packet
                cst = CONN_REJECT;
            }
            else
            {
                cst = CONN_CONTINUE;
            }
        }

        // It might be that this is a data packet, which has turned the connection
        // into "connected" state, removed the connector (so since now every next packet
        // will land directly in the queue), but this data packet shall still be delivered.
        if (cst == CONN_ACCEPT && !unit->m_Packet.isControl())
        {
            // The process as called through processAsyncConnectResponse() should have put the
            // socket into the pending queue for pending connection (don't ask me, this is so).
            // This pending queue is being purged every time in the beginning of this loop, so
            // currently the socket is in the pending queue, but not yet in the connection queue.
            // It will be done at the next iteration of the reading loop, but it will be too late,
            // we have a pending data packet now and we must either dispatch it to an already connected
            // socket or disregard it, and rather prefer the former. So do this transformation now
            // that we KNOW (by the cst == CONN_ACCEPT result) that the socket should be inserted
            // into the pending anteroom.

            CUDT* ne = getNewEntry(); // This function actuall removes the entry and returns it.
            // This **should** now always return a non-null value, but check it first
            // because if this accidentally isn't true, the call to worker_ProcessAddressedPacket will
            // result in redirecting it to here and so on until the call stack overflow. In case of
            // this "accident" simply disregard the packet from any further processing, it will be later
            // loss-recovered.
            // XXX (Probably the old contents of UDT's CRcvQueue::worker should be shaped a little bit
            // differently throughout the functions).
            if (ne)
            {
                HLOGC(mglog.Debug, log << CUDTUnited::CONID(ne->m_SocketID) << " SOCKET pending for connection - ADDING TO RCV QUEUE/MAP");
                m_pRcvUList->insert(ne);
                m_pHash->insert(ne->m_SocketID, ne);

                // The current situation is that this has passed processAsyncConnectResponse, but actually
                // this packet *SHOULD HAVE BEEN* handled by worker_ProcessAddressedPacket, however the
                // connection state wasn't completed at the moment when dispatching this packet. This has
                // been now completed inside the call to processAsyncConnectResponse, but this is still a
                // data packet that should have expected the connection to be already established. Therefore
                // redirect it once again into worker_ProcessAddressedPacket here.

                HLOGC(mglog.Debug, log << "AsyncOrRND: packet SWITCHED TO CONNECTED with ID=" << id
                        << " -- passing to worker_ProcessAddressedPacket");

                // Theoretically we should check if m_pHash->lookup(ne->m_SocketID) returns 'ne', but this
                // has been just added to m_pHash, so the check would be extremely paranoid here.
                cst = worker_ProcessAddressedPacket(id, unit, addr);
                if (cst == CONN_REJECT)
                    return cst;
                return CONN_ACCEPT; // this function usually will return CONN_CONTINUE, which doesn't represent current situation.
            }
            else
            {
                LOGC(mglog.Error, log << "IPE: AsyncOrRND: packet SWITCHED TO CONNECTED, but ID=" << id
                        << " is still not present in the socket ID dispatch hash - DISREGARDING");
            }
        }
        return cst;
    }
    HLOGC(mglog.Debug, log << "AsyncOrRND: packet RESOLVED TO ID=" << id << " -- continuing through CENTRAL PACKET QUEUE");
    // This is where also the packets for rendezvous connection will be landing,
    // in case of a synchronous connection.
    storePkt(id, unit->m_Packet.clone());

    return CONN_CONTINUE;
}

void CRcvQueue::stopWorker()
{
    // We use the decent way, so we say to the thread "please exit".
    m_bClosing = true;

    // Sanity check of the function's affinity.
    if (pthread_self() == m_WorkerThread)
    {
        LOGC(mglog.Error, log << "IPE: RcvQ:WORKER TRIES TO CLOSE ITSELF!");
        return; // do nothing else, this would cause a hangup or crash.
    }

    // And we trust the thread that it does.
    pthread_join(m_WorkerThread, NULL);
}

int CRcvQueue::recvfrom(int32_t id, ref_t<CPacket> r_packet)
{
   CGuard bufferlock(m_PassLock, "Pass");
   CPacket& packet = *r_packet;

   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

   if (i == m_mBuffer.end())
   {
      CTimer::condTimedWaitUS(&m_PassCond, &m_PassLock, 1000000);

      i = m_mBuffer.find(id);
      if (i == m_mBuffer.end())
      {
         packet.setLength(-1);
         return -1;
      }
   }

   // retrieve the earliest packet
   CPacket* newpkt = i->second.front();

   if (packet.getLength() < newpkt->getLength())
   {
      packet.setLength(-1);
      return -1;
   }

   // copy packet content
   // XXX Check if this wouldn't be better done by providing
   // copy constructor for DynamicStruct.
   // XXX Another thing: this looks wasteful. This expects an already
   // allocated memory on the packet, this thing gets the packet,
   // copies it into the passed packet and then the source packet
   // gets deleted. Why not simply return the originally stored packet,
   // without copying, allocation and deallocation?
   memcpy(packet.m_nHeader, newpkt->m_nHeader, CPacket::HDR_SIZE);
   memcpy(packet.m_pcData, newpkt->m_pcData, newpkt->getLength());
   packet.setLength(newpkt->getLength());

   packet.m_DestAddr = newpkt->m_DestAddr;

   delete [] newpkt->m_pcData;
   delete newpkt;

   // remove this message from queue,
   // if no more messages left for this socket, release its data structure
   i->second.pop();
   if (i->second.empty())
      m_mBuffer.erase(i);

   return packet.getLength();
}

int CRcvQueue::setListener(CUDT* u)
{
   CGuard lslock(m_LSLock, "LS");

   if (NULL != m_pListener)
      return -1;

   m_pListener = u;
   return 0;
}

void CRcvQueue::removeListener(const CUDT* u)
{
   CGuard lslock(m_LSLock, "LS");

   if (u == m_pListener)
      m_pListener = NULL;
}

void CRcvQueue::registerConnector(const SRTSOCKET& id, CUDT* u, const sockaddr_any& addr, uint64_t ttl)
{
   HLOGC(mglog.Debug, log << "registerConnector: adding @" << id << " addr=" << SockaddrToString(addr) << " TTL=" << ttl);
   m_pRendezvousQueue->insert(id, u, addr, ttl);
}

void CRcvQueue::removeConnector(const SRTSOCKET& id, bool should_lock)
{
    HLOGC(mglog.Debug, log << "removeConnector: removing @" << id);
    m_pRendezvousQueue->remove(id, should_lock);

    CGuard bufferlock(m_PassLock, "Pass");

    map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);
    if (i != m_mBuffer.end())
    {
        HLOGC(mglog.Debug, log << "removeConnector: ... and its packet queue with " << i->second.size() << " packets collected");
        while (!i->second.empty())
        {
            delete [] i->second.front()->m_pcData;
            delete i->second.front();
            i->second.pop();
        }
        m_mBuffer.erase(i);
    }
}

void CRcvQueue::setNewEntry(CUDT* u)
{
   HLOGC(mglog.Debug, log << CUDTUnited::CONID(u->m_SocketID) << "setting socket PENDING FOR CONNECTION");
   CGuard listguard(m_IDLock, "ID");
   m_vNewEntry.push_back(u);
}

bool CRcvQueue::ifNewEntry()
{
   return !(m_vNewEntry.empty());
}

CUDT* CRcvQueue::getNewEntry()
{
   CGuard listguard(m_IDLock, "ID");

   if (m_vNewEntry.empty())
      return NULL;

   CUDT* u = (CUDT*)*(m_vNewEntry.begin());
   m_vNewEntry.erase(m_vNewEntry.begin());

   return u;
}

void CRcvQueue::storePkt(int32_t id, CPacket* pkt)
{
   CGuard bufferlock(m_PassLock, "Pass");

   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

   if (i == m_mBuffer.end())
   {
      m_mBuffer[id].push(pkt);
      pthread_cond_signal(&m_PassCond);
   }
   else
   {
      //avoid storing too many packets, in case of malfunction or attack
      if (i->second.size() > 16)
         return;

      i->second.push(pkt);
   }
}
