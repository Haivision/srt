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
   Yunhong Gu, last updated 01/01/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#define SRT_IMPORT_EVENT
#include "platform_sys.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <iterator>

#include "common.h"
#include "epoll.h"
#include "logging.h"
#include "udt.h"
#include "logging.h"

using namespace std;

// Use "inline namespace" in C++11
namespace srt_logging
{
    extern Logger dlog, mglog;
}

using srt_logging::dlog;
using srt_logging::mglog;

CEPoll::CEPoll():
m_iIDSeed(0)
{
   CGuard::createMutex(m_EPollLock);
}

CEPoll::~CEPoll()
{
   CGuard::releaseMutex(m_EPollLock);
}

int CEPoll::create(CEPollDesc** pout)
{
   CGuard pg(m_EPollLock, "EPoll");

   int localid = 0;

   #ifdef LINUX
   localid = epoll_create(1024);
   /* Possible reasons of -1 error:
EMFILE: The per-user limit on the number of epoll instances imposed by /proc/sys/fs/epoll/max_user_instances was encountered.
ENFILE: The system limit on the total number of open files has been reached.
ENOMEM: There was insufficient memory to create the kernel object.
       */
   if (localid < 0)
      throw CUDTException(MJ_SETUP, MN_NONE, errno);
   #elif defined(BSD) || defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
   localid = kqueue();
   if (localid < 0)
      throw CUDTException(MJ_SETUP, MN_NONE, errno);
   #else
   // on Solaris, use /dev/poll
   // on Windows, select
   #endif

   if (++ m_iIDSeed >= 0x7FFFFFFF)
      m_iIDSeed = 0;

   CEPollDesc desc;
   desc.m_iID = m_iIDSeed;
   desc.m_iLocalID = localid;
   CEPollDesc* pp = &(m_mPolls[desc.m_iID] = desc);
   if (pout)
       *pout = pp;

   return desc.m_iID;
}

int CEPoll::clear_usocks(int eid)
{
    // This should remove all SRT sockets from given eid.
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

   CEPollDesc& d= p->second;

   d.m_sUDTSocksIn.clear();
   d.m_sUDTSocksOut.clear();
   d.m_sUDTSocksEx.clear();
   d.m_sUDTSocksSpc.clear();

   d.clear_state();

   return 0;
}

namespace
{
template<int event_type>
inline void prv_update_usock(CEPollDesc& d, SRTSOCKET u, int flags)
{
    set<SRTSOCKET>& subscribers = d.*(CEPollET<event_type>::subscribers());
    set<SRTSOCKET>& eventsinks = d.*(CEPollET<event_type>::eventsinks());

    if (IsSet(flags, event_type))
    {
        // Add the socket to the subscribers (m_sUDTSocksIn etc.)
        subscribers.insert(u);
    }
    else
    {
        // Remove the socket from the subscribers, AND
        // also remove it from eventsink, as when the socket
        // was subscribed already, but is not present in this
        // call's flags, the user is no longer interested in
        // the events represented by event flags that are not to be set.
        subscribers.erase(u);
        eventsinks.erase(u);
    }
}

template<int event_type>
inline void prv_clear_ready_usocks(CEPollDesc& d, int event_type_match)
{
    if (event_type_match != event_type)
        return;

    set<SRTSOCKET>& subscribers = d.*(CEPollET<event_type>::subscribers());
    set<SRTSOCKET>& eventsinks = d.*(CEPollET<event_type>::eventsinks());

    // Remove from subscribers all sockets that were found in eventsinks
    set<SRTSOCKET> without;

    // WITHOUT = SUBSCRIBERS \ EVENTSINKS
    std::set_difference(subscribers.begin(), subscribers.end(),
            eventsinks.begin(), eventsinks.end(), std::inserter(without, without.begin()));

    HLOGC(mglog.Debug, log << "EID " << d.m_iID << ": removing " << CEPollET<event_type>::name() << "-ready socekts: "
            << Printable(eventsinks));

    eventsinks.clear();
    swap(subscribers, without);
}

}

void CEPoll::clear_ready_usocks(CEPollDesc& d, int direction)
{
   CGuard pg(m_EPollLock, "EPoll");


   // The call encloses both checking if direction == SRT_EPOLL_*
   // and the actual activity to perform. The function is inline so
   // this series should expand into a condition check and execution
   // only when the direction matches given symbol.
   //
   // This can be also further optimized by making an array where only
   // index of 1, 4 and 8 are filled, others are zero.

   prv_clear_ready_usocks<SRT_EPOLL_IN>(d, direction);
   prv_clear_ready_usocks<SRT_EPOLL_OUT>(d, direction);
   prv_clear_ready_usocks<SRT_EPOLL_ERR>(d, direction);
   prv_clear_ready_usocks<SRT_EPOLL_SPECIAL>(d, direction);
}

int CEPoll::add_usock(const int eid, const SRTSOCKET& u, const int* events)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);
#if ENABLE_HEAVY_LOGGING
   string modes;
   if (!events)
       modes = "all ";
   else
   {
       int mx[4] = {SRT_EPOLL_IN, SRT_EPOLL_OUT, SRT_EPOLL_ERR, SRT_EPOLL_SPECIAL};
       string nam[4] = { "in", "out", "err", "spec" };
       for (int i = 0; i < 4; ++i)
           if (*events & mx[i])
           {
               modes += nam[i];
               modes += " ";
           }
   }

   LOGC(mglog.Debug, log << "srt_epoll_add_usock(" << eid << ") @" << u << " modes: " << modes);
#endif

   int ef = events ? *events : (SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR);
   CEPollDesc& d = p->second;

   // Changes done here:
   // 1. Connecting timeout not signalled without EPOLL_ERR 
   // 2. You can use 'add_usock' also in order to CHANGE event spec
   //    on a socket that is already added to the epoll container.
   //    If particular even type is no longer to be tracked on that
   //    socket, it will be removed from subscribers and from event
   //    information stored so far in the event sink, if any.

   prv_update_usock<SRT_EPOLL_IN>(d, u, ef);
   prv_update_usock<SRT_EPOLL_OUT>(d, u, ef);
   prv_update_usock<SRT_EPOLL_ERR>(d, u, ef);
   prv_update_usock<SRT_EPOLL_SPECIAL>(d, u, ef);

   return 0;
}

int CEPoll::add_ssock(const int eid, const SYSSOCKET& s, const int* events)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

#ifdef LINUX
   epoll_event ev;
   memset(&ev, 0, sizeof(epoll_event));

   if (NULL == events)
      ev.events = EPOLLIN | EPOLLOUT | EPOLLERR;
   else
   {
      ev.events = 0;
      if (*events & SRT_EPOLL_IN)
         ev.events |= EPOLLIN;
      if (*events & SRT_EPOLL_OUT)
         ev.events |= EPOLLOUT;
      if (*events & SRT_EPOLL_ERR)
         ev.events |= EPOLLERR;
   }

   ev.data.fd = s;
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_ADD, s, &ev) < 0)
      throw CUDTException();
#elif defined(BSD) || defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
   struct kevent ke[2];
   int num = 0;

   if (NULL == events)
   {
      EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
   }
   else
   {
      if (*events & SRT_EPOLL_IN)
      {
         EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      }
      if (*events & SRT_EPOLL_OUT)
      {
         EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
      }
   }
   if (kevent(p->second.m_iLocalID, ke, num, NULL, 0, NULL) < 0)
      throw CUDTException();
#else

#ifdef _MSC_VER
// Microsoft Visual Studio doesn't support the #warning directive - nonstandard anyway.
// Use #pragma message with the same text.
// All other compilers should be ok :)
#pragma message("WARNING: Unsupported system for epoll. The epoll_add_ssock() API call won't work on this platform.")
#else
#warning "Unsupported system for epoll. The epoll_add_ssock() API call won't work on this platform."
#endif

#endif

   p->second.m_sLocals.insert(s);

   return 0;
}

int CEPoll::remove_usock(const int eid, const SRTSOCKET& u)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

   HLOGC(mglog.Debug, log << "srt_epoll_remove_usock(" << eid << "): removed @" << u);

   p->second.remove(u);
   return 0;
}

int CEPoll::remove_ssock(const int eid, const SYSSOCKET& s)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

#ifdef LINUX
   epoll_event ev;  // ev is ignored, for compatibility with old Linux kernel only.
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_DEL, s, &ev) < 0)
      throw CUDTException();
#elif defined(BSD) || defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
   struct kevent ke;

   //
   // Since I don't know what was set before
   // Just clear out both read and write
   //
   EV_SET(&ke, s, EVFILT_READ, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, &ke, 1, NULL, 0, NULL);
   EV_SET(&ke, s, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, &ke, 1, NULL, 0, NULL);
#endif

   p->second.m_sLocals.erase(s);

   return 0;
}

// Need this to atomically modify polled events (ex: remove write/keep read)
int CEPoll::update_usock(const int eid, const SRTSOCKET& u, const int* events)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

   int ef = events ? *events : SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;

   prv_update_usock<SRT_EPOLL_IN>(p->second, u, ef);
   prv_update_usock<SRT_EPOLL_OUT>(p->second, u, ef);
   prv_update_usock<SRT_EPOLL_ERR>(p->second, u, ef);
   prv_update_usock<SRT_EPOLL_SPECIAL>(p->second, u, ef);

   return 0;
}

int CEPoll::update_ssock(const int eid, const SYSSOCKET& s, const int* events)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
   if (p == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

#ifdef LINUX
   epoll_event ev;
   memset(&ev, 0, sizeof(epoll_event));

   if (NULL == events)
      ev.events = EPOLLIN | EPOLLOUT | EPOLLERR;
   else
   {
      ev.events = 0;
      if (*events & SRT_EPOLL_IN)
         ev.events |= EPOLLIN;
      if (*events & SRT_EPOLL_OUT)
         ev.events |= EPOLLOUT;
      if (*events & SRT_EPOLL_ERR)
         ev.events |= EPOLLERR;
   }

   ev.data.fd = s;
   if (::epoll_ctl(p->second.m_iLocalID, EPOLL_CTL_MOD, s, &ev) < 0)
      throw CUDTException();
#elif defined(BSD) || defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
   struct kevent ke[2];
   int num = 0;

   //
   // Since I don't know what was set before
   // Just clear out both read and write
   //
   EV_SET(&ke[0], s, EVFILT_READ, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, ke, 1, NULL, 0, NULL);
   EV_SET(&ke[0], s, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
   kevent(p->second.m_iLocalID, ke, 1, NULL, 0, NULL);
   if (NULL == events)
   {
      EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
   }
   else
   {
      if (*events & SRT_EPOLL_IN)
      {
         EV_SET(&ke[num++], s, EVFILT_READ, EV_ADD, 0, 0, NULL);
      }
      if (*events & SRT_EPOLL_OUT)
      {
         EV_SET(&ke[num++], s, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
      }
   }
   if (kevent(p->second.m_iLocalID, ke, num, NULL, 0, NULL) < 0)
      throw CUDTException();
#endif
// Assuming add is used if not inserted
//   p->second.m_sLocals.insert(s);

   return 0;
}

int CEPoll::wait(const int eid, set<SRTSOCKET>* readfds, set<SRTSOCKET>* writefds, int64_t msTimeOut, set<SYSSOCKET>* lrfds, set<SYSSOCKET>* lwfds)
{
   // if all fields is NULL and waiting time is infinite, then this would be a deadlock
   if (!readfds && !writefds && !lrfds && !lwfds && (msTimeOut < 0))
      throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

   // Clear these sets in case the app forget to do it.
   if (readfds) readfds->clear();
   if (writefds) writefds->clear();
   if (lrfds) lrfds->clear();
   if (lwfds) lwfds->clear();

   int total = 0;

   int64_t entertime = CTimer::getTime();
   while (true)
   {
      CGuard::enterCS(m_EPollLock, "EPoll");

      map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
      if (p == m_mPolls.end())
      {
         CGuard::leaveCS(m_EPollLock, "EPoll");
         LOGC(mglog.Error, log << "EID:" << eid << " INVALID.");
         throw CUDTException(MJ_NOTSUP, MN_EIDINVAL, 0);
      }

      if (p->second.m_sUDTSocksIn.empty() && p->second.m_sUDTSocksOut.empty() && p->second.m_sLocals.empty() && (msTimeOut < 0))
      {
         // no socket is being monitored, this may be a deadlock
         CGuard::leaveCS(m_EPollLock, "EPoll");
         LOGC(mglog.Error, log << "EID:" << eid << " no sockets to check, this would deadlock");
         throw CUDTException(MJ_NOTSUP, MN_EEMPTY, 0);
      }

      // Sockets with exceptions are returned to both read and write sets.
      if ((NULL != readfds) && (!p->second.m_sUDTReads.empty() || !p->second.m_sUDTExcepts.empty()))
      {
         *readfds = p->second.m_sUDTReads;
         for (set<SRTSOCKET>::const_iterator i = p->second.m_sUDTExcepts.begin(); i != p->second.m_sUDTExcepts.end(); ++ i)
            readfds->insert(*i);
         total += p->second.m_sUDTReads.size() + p->second.m_sUDTExcepts.size();
      }
      if ((NULL != writefds) && (!p->second.m_sUDTWrites.empty() || !p->second.m_sUDTExcepts.empty()))
      {
         *writefds = p->second.m_sUDTWrites;
         for (set<SRTSOCKET>::const_iterator i = p->second.m_sUDTExcepts.begin(); i != p->second.m_sUDTExcepts.end(); ++ i)
            writefds->insert(*i);
         total += p->second.m_sUDTWrites.size() + p->second.m_sUDTExcepts.size();
      }

      if (lrfds || lwfds)
      {
         #ifdef LINUX
         const int max_events = p->second.m_sLocals.size();
         epoll_event ev[max_events];
         int nfds = ::epoll_wait(p->second.m_iLocalID, ev, max_events, 0);

         for (int i = 0; i < nfds; ++ i)
         {
            if ((NULL != lrfds) && (ev[i].events & EPOLLIN))
            {
               lrfds->insert(ev[i].data.fd);
               ++ total;
            }
            if ((NULL != lwfds) && (ev[i].events & EPOLLOUT))
            {
               lwfds->insert(ev[i].data.fd);
               ++ total;
            }
         }
         #elif defined(BSD) || defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
         struct timespec tmout = {0, 0};
         const int max_events = p->second.m_sLocals.size();
         struct kevent ke[max_events];

         int nfds = kevent(p->second.m_iLocalID, NULL, 0, ke, max_events, &tmout);

         for (int i = 0; i < nfds; ++ i)
         {
            if ((NULL != lrfds) && (ke[i].filter == EVFILT_READ))
            {
               lrfds->insert(ke[i].ident);
               ++ total;
            }
            if ((NULL != lwfds) && (ke[i].filter == EVFILT_WRITE))
            {
               lwfds->insert(ke[i].ident);
               ++ total;
            }
         }
         #else
         //currently "select" is used for all non-Linux platforms.
         //faster approaches can be applied for specific systems in the future.

         //"select" has a limitation on the number of sockets
         int max_fd = 0;

         fd_set readfds;
         fd_set writefds;
         FD_ZERO(&readfds);
         FD_ZERO(&writefds);

         for (set<SYSSOCKET>::const_iterator i = p->second.m_sLocals.begin(); i != p->second.m_sLocals.end(); ++ i)
         {
            if (lrfds)
               FD_SET(*i, &readfds);
            if (lwfds)
               FD_SET(*i, &writefds);
            if (*i > max_fd)
              max_fd = *i;
        }

         timeval tv;
         tv.tv_sec = 0;
         tv.tv_usec = 0;
         if (::select(max_fd + 1, &readfds, &writefds, NULL, &tv) > 0)
         {
            for (set<SYSSOCKET>::const_iterator i = p->second.m_sLocals.begin(); i != p->second.m_sLocals.end(); ++ i)
            {
               if (lrfds && FD_ISSET(*i, &readfds))
               {
                  lrfds->insert(*i);
                  ++ total;
               }
               if (lwfds && FD_ISSET(*i, &writefds))
               {
                  lwfds->insert(*i);
                  ++ total;
               }
            }
         }
         #endif
      }

      CGuard::leaveCS(m_EPollLock, "EPoll");

      if (total > 0)
         return total;

      if ((msTimeOut >= 0) && (int64_t(CTimer::getTime() - entertime) >= msTimeOut * int64_t(1000)))
      {
          HLOGC(mglog.Debug, log << "EID:" << eid << ": TIMEOUT.");
          throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);
      }

      CTimer::waitForEvent();
   }

   return 0;
}

CEPollDesc& CEPoll::access(int eid)
{
    CGuard lg(m_EPollLock, "EPoll");

    map<int, CEPollDesc>::iterator p = m_mPolls.find(eid);
    if (p == m_mPolls.end())
    {
        LOGC(mglog.Error, log << "EID:" << eid << " INVALID.");
        throw CUDTException(MJ_NOTSUP, MN_EIDINVAL, 0);
    }

    return p->second;
}

#if ENABLE_HEAVY_LOGGING
namespace {

void PrintReady(std::ostringstream& os, const char* header, const std::set<SRTSOCKET>& subscribers, const std::set<SRTSOCKET>& states)
{
    os << header << " ";
    for (std::set<SRTSOCKET>::const_iterator i = subscribers.begin(); i != subscribers.end(); ++i)
    {
        os << "(";
        if (states.count(*i))
            os << "*";
        else
            os << " ";
        os << ") " << *i << " ";
    }
}

string ShowReadySockets(const CEPollDesc& d)
{
    std::ostringstream os;

    os << "EID:" << d.m_iID
        << " TOTAL:" << (d.rd().size() + d.wr().size() + d.ex().size() + d.sp().size())
        << "  STATES: ";
    PrintReady(os, "[R]", d.m_sUDTSocksIn, d.rd());
    PrintReady(os, "[W]", d.m_sUDTSocksOut, d.wr());
    PrintReady(os, "[E]", d.m_sUDTSocksEx, d.ex());
    PrintReady(os, "[S]", d.m_sUDTSocksSpc, d.sp());

    return os.str();
}
}
#endif

int CEPoll::swait(CEPollDesc& d, SrtPollState& st, int64_t msTimeOut, bool report_by_exception)
{
    {
        CGuard lg(m_EPollLock, "EPoll");
        if (d.empty() && msTimeOut < 0)
        {
            // no socket is being monitored, this may be a deadlock
            LOGC(mglog.Error, log << "EID:" << d.m_iID << " no sockets to check, this would deadlock");
            if (report_by_exception)
                throw CUDTException(MJ_NOTSUP, MN_EEMPTY, 0);
            return -1;
        }
    }

    st.clear_state();

    int total = 0;

    int64_t entertime = CTimer::getTime();
    while (true)
    {
        {
            // Not extracting separately because this function is
            // for internal use only and we state that the eid could
            // not be deleted or changed the target CEPollDesc in the
            // meantime.

            // Here we only prevent the pollset be updated simultaneously
            // with unstable reading. 
            CGuard lg(m_EPollLock, "EPoll");
            total = d.rd().size() + d.wr().size() + d.ex().size() + d.sp().size();
            if (total > 0 || msTimeOut == 0)
            {
                // If msTimeOut == 0, it means that we need the information
                // immediately, we don't want to wait. Therefore in this case
                // report also when none is ready.
                st = d;

                HLOGC(dlog.Debug, log << ShowReadySockets(d));

                // IMPORTANT: SPECIAL is reported only ONCE and cleared after
                // calling 'swait'. Next 'swait' call shouldn't pick it up.
                d.clear_special();

                return total;
            }
            // Don't report any updates because this check happens
            // extremely often.
        }

        if ((msTimeOut >= 0) && (int64_t(CTimer::getTime() - entertime) >= msTimeOut * int64_t(1000)))
        {
            HLOGC(mglog.Debug, log << "EID:" << d.m_iID << ": TIMEOUT.");
            if (report_by_exception)
                throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);
            return 0; // meaning "none is ready"
        }

#if (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
#else
        CTimer::waitForEvent();
#endif
    }

    return 0;
}

int CEPoll::release(const int eid)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator i = m_mPolls.find(eid);
   if (i == m_mPolls.end())
      throw CUDTException(MJ_NOTSUP, MN_EIDINVAL);

   #ifdef LINUX
   // release local/system epoll descriptor
   ::close(i->second.m_iLocalID);
   #elif defined(BSD) || defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
   ::close(i->second.m_iLocalID);
   #endif

   m_mPolls.erase(i);

   return 0;
}

namespace
{
// For debug purposes
template<int event_type> inline
string epoll_event_name(int events, bool enable)
{
    if (!IsSet(events, event_type))
        return string();

    string output = enable ? "+" : "-";
    return output + CEPollET<event_type>::name() + " ";
}

template <int event_type> inline
bool update_epoll_sets(int eid SRT_ATR_UNUSED, SRTSOCKET uid, CEPollDesc& d, int flags, bool enable)
{
    if (!IsSet(flags, event_type))
        return false;

    set<SRTSOCKET>& watch = d.*(CEPollET<event_type>::subscribers());
    set<SRTSOCKET>& result = d.*(CEPollET<event_type>::eventsinks());

    // Required here because of goto
#if ENABLE_HEAVY_LOGGING
    string evs = epoll_event_name<event_type>(flags, enable);
#endif


    int nerased ATR_UNUSED = 0;
    if (enable && watch.count(uid))
    {
        result.insert(uid);
        goto Updated;
    }

    if (!enable)
    {
        nerased = result.erase(uid);
        goto Updated;
    }

#if ENABLE_HEAVY_LOGGING
    HLOGC(dlog.Debug, log << "epoll/update: NOT updated EID " << eid
            << " for @" << uid << "[" << evs << "]"
            << " TRACKED: " << Printable(watch));
    return false;

    if (false)
    {
Updated: ;
        LOGC(dlog.Debug, log << "epoll/update: EID " << eid << " @" << uid
                << (!enable ? (nerased ? " (cleared)" : " (UNCHANGED)") : "")
                << " [" << evs << "] TRACKED:"
                << Printable(watch));
    }
    return true;
#else
    return false;
Updated:
    return true;
#endif
}

}  // namespace

int CEPoll::update_events(const SRTSOCKET& uid, std::set<int>& eids, int events, bool enable)
{
   CGuard pg(m_EPollLock, "EPoll");

   map<int, CEPollDesc>::iterator p;

#if ENABLE_HEAVY_LOGGING
   string evs =
       epoll_event_name<SRT_EPOLL_IN>(events, enable)
       + epoll_event_name<SRT_EPOLL_OUT>(events, enable)
       + epoll_event_name<SRT_EPOLL_ERR>(events, enable)
       + epoll_event_name<SRT_EPOLL_SPECIAL>(events, enable);

   if (eids.empty())
   {
       LOGC(dlog.Debug, log << "epoll/update: @" << uid << " [" << evs << "]: NO SUBSCRIBERS");
   }
#endif

   vector<int> lost;
   bool updated ATR_UNUSED = false;
   for (set<int>::iterator i = eids.begin(); i != eids.end(); ++ i)
   {
      p = m_mPolls.find(*i);
      if (p == m_mPolls.end())
      {
         LOGC(dlog.Error, log << "epoll/update: EID " << *i << " was deleted in the meantime");
         lost.push_back(*i);
      }
      else
      {
          updated |= update_epoll_sets<SRT_EPOLL_IN >(*i, uid, p->second, events, enable);
          updated |= update_epoll_sets<SRT_EPOLL_OUT>(*i, uid, p->second, events, enable);
          updated |= update_epoll_sets<SRT_EPOLL_ERR>(*i, uid, p->second, events, enable);
          updated |= update_epoll_sets<SRT_EPOLL_SPECIAL>(*i, uid, p->second, events, enable);
      }
   }

   for (vector<int>::iterator i = lost.begin(); i != lost.end(); ++ i)
      eids.erase(*i);

#if ENABLE_HEAVY_LOGGING
   if (!updated)
   {
       LOGC(dlog.Debug, log << "epoll/update: @" << uid << " [" << evs << "]: NOTHING UPDATED");
   }
#endif

   return 0;
}
