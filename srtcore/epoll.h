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
Copyright (c) 2001 - 2010, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 08/20/2010
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef __UDT_EPOLL_H__
#define __UDT_EPOLL_H__


#include <map>
#include <set>
#include "udt.h"

struct CEPollDesc: public SrtPollState
{
   int m_iID;                                // epoll ID
   std::set<SRTSOCKET> m_sUDTSocksOut;       // set of UDT sockets waiting for write events
   std::set<SRTSOCKET> m_sUDTSocksIn;        // set of UDT sockets waiting for read events
   std::set<SRTSOCKET> m_sUDTSocksEx;        // set of UDT sockets waiting for exceptions
   std::set<SRTSOCKET> m_sUDTSocksSpc;       // internal-only events on UDT sockets

   int m_iLocalID;                           // local system epoll ID
   std::set<SYSSOCKET> m_sLocals;            // set of local (non-UDT) descriptors

   bool empty() const
   {
       return m_sUDTSocksIn.empty() && m_sUDTSocksOut.empty() && m_sUDTSocksEx.empty() && m_sUDTSocksSpc.empty();
   }

   void remove(const SRTSOCKET u)
   {
       m_sUDTSocksIn.erase(u);
       m_sUDTSocksOut.erase(u);
       m_sUDTSocksEx.erase(u);
       m_sUDTSocksSpc.erase(u);

       /*
        * We are no longer interested in signals from this socket
        * If some are up, they will unblock EPoll forever.
        * Clear them.
        */
       m_sUDTReads.erase(u);
       m_sUDTWrites.erase(u);
       m_sUDTExcepts.erase(u);
       m_sUDTSpecial.erase(u);
   }
};

// Type-to-constant binder
template <int event_type>
class CEPollET;

#define CEPOLL_BIND(event_type, subscriber, eventsink, descname) \
template<> \
class CEPollET<event_type> \
{ \
public: \
    static std::set<SRTSOCKET> CEPollDesc::*subscribers() { return &CEPollDesc:: subscriber; } \
    static std::set<SRTSOCKET> CEPollDesc::*eventsinks() { return &CEPollDesc:: eventsink; } \
    static const char* name() { return descname; } \
}

CEPOLL_BIND(SRT_EPOLL_IN, m_sUDTSocksIn, m_sUDTReads, "IN");
CEPOLL_BIND(SRT_EPOLL_OUT, m_sUDTSocksOut, m_sUDTWrites, "OUT");
CEPOLL_BIND(SRT_EPOLL_ERR, m_sUDTSocksEx, m_sUDTExcepts, "ERR");
CEPOLL_BIND(SRT_EPOLL_SPECIAL, m_sUDTSocksSpc, m_sUDTSpecial, "SPECIAL");

#undef CEPOLL_BIND


class CEPoll
{
friend class CUDT;
friend class CUDTGroup;
friend class CRendezvousQueue;

public:
   CEPoll();
   ~CEPoll();

public: // for CUDTUnited API

      /// create a new EPoll.
      /// @return new EPoll ID if success, otherwise an error number.

   int create(CEPollDesc** ppd = 0);


   /// delete all user sockets (SRT sockets) from an EPoll
   /// @param [in] eid EPoll ID.
   /// @return 0 
   int clear_usocks(int eid);

      /// add a UDT socket to an EPoll.
      /// @param [in] eid EPoll ID.
      /// @param [in] u UDT Socket ID.
      /// @param [in] events events to watch.
      /// @return 0 if success, otherwise an error number.

   int add_usock(const int eid, const SRTSOCKET& u, const int* events = NULL);

      /// add a system socket to an EPoll.
      /// @param [in] eid EPoll ID.
      /// @param [in] s system Socket ID.
      /// @param [in] events events to watch.
      /// @return 0 if success, otherwise an error number.

   int add_ssock(const int eid, const SYSSOCKET& s, const int* events = NULL);

      /// remove a UDT socket event from an EPoll; socket will be removed if no events to watch.
      /// @param [in] eid EPoll ID.
      /// @param [in] u UDT socket ID.
      /// @return 0 if success, otherwise an error number.

   int remove_usock(const int eid, const SRTSOCKET& u);

      /// remove a system socket event from an EPoll; socket will be removed if no events to watch.
      /// @param [in] eid EPoll ID.
      /// @param [in] s system socket ID.
      /// @return 0 if success, otherwise an error number.

   int remove_ssock(const int eid, const SYSSOCKET& s);
      /// update a UDT socket events from an EPoll.
      /// @param [in] eid EPoll ID.
      /// @param [in] u UDT socket ID.
      /// @param [in] events events to watch.
      /// @return 0 if success, otherwise an error number.

   int update_usock(const int eid, const SRTSOCKET& u, const int* events = NULL);

      /// update a system socket events from an EPoll.
      /// @param [in] eid EPoll ID.
      /// @param [in] u UDT socket ID.
      /// @param [in] events events to watch.
      /// @return 0 if success, otherwise an error number.

   int update_ssock(const int eid, const SYSSOCKET& s, const int* events = NULL);

      /// wait for EPoll events or timeout.
      /// @param [in] eid EPoll ID.
      /// @param [out] readfds UDT sockets available for reading.
      /// @param [out] writefds UDT sockets available for writing.
      /// @param [in] msTimeOut timeout threshold, in milliseconds.
      /// @param [out] lrfds system file descriptors for reading.
      /// @param [out] lwfds system file descriptors for writing.
      /// @return number of sockets available for IO.

   int wait(const int eid, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds, std::set<SYSSOCKET>* lwfds);

   int swait(CEPollDesc& d, SrtPollState& st, int64_t msTimeOut, bool report_by_exception = true);

   // Could be a template directly, but it's now hidden in the imp file.
   void clear_ready_usocks(CEPollDesc& d, int direction);

      /// close and release an EPoll.
      /// @param [in] eid EPoll ID.
      /// @return 0 if success, otherwise an error number.

   int release(const int eid);

public: // for CUDT to acknowledge IO status

      /// Update events available for a UDT socket.
      /// @param [in] uid UDT socket ID.
      /// @param [in] eids EPoll IDs to be set
      /// @param [in] events Combination of events to update
      /// @param [in] enable true -> enable, otherwise disable
      /// @return 0 if success, otherwise an error number

   int update_events(const SRTSOCKET& uid, std::set<int>& eids, int events, bool enable);

   CEPollDesc& access(int eid);

private:
   int m_iIDSeed;                            // seed to generate a new ID
   pthread_mutex_t m_SeedLock;

   std::map<int, CEPollDesc> m_mPolls;       // all epolls
   pthread_mutex_t m_EPollLock;
};


#endif
