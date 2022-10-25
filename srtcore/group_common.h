/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2021 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

/*****************************************************************************
Written by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_GROUP_COMMON_H
#define INC_SRT_GROUP_COMMON_H

#include <deque>
#include <list>

#include "srt.h"
#include "common.h"
#include "core.h"

namespace srt
{
namespace groups
{
    typedef SRT_MEMBERSTATUS GroupState;

    enum SeqType
    {
        /// Freshly first-time to be sent packets.
        SQT_FRESH,

        /// Rexmit requests
        SQT_LOSS,

        /// Packet filter requests
        SQT_PFILTER,

        /// Special value used in case when the request
        /// has been exceptionally cancelled, but removal
        /// of the element would violate the logics.
        SQT_SKIP
    };

    std::string SeqTypeStr(SeqType);

    struct SchedSeq
    {
        int32_t seq;
        SeqType type;

        bool operator == (const SchedSeq& other) const
        {
            return seq == other.seq && type == other.type;
        }
    };

    struct SocketData
    {
        SRTSOCKET      id; // same as ps->m_SocketID
        CUDTSocket*    ps;
        int            token;
        SRT_SOCKSTATUS laststatus;
        GroupState     sndstate;
        GroupState     rcvstate;
        int            sndresult;
        int            rcvresult;
        sockaddr_any   agent;
        sockaddr_any   peer;
        bool           ready_read;
        bool           ready_write;
        bool           ready_error;

        // Balancing data
        bool          use_send_schedule;
        double load_factor;
        double unit_load;

        // Configuration
        uint16_t       weight;

        // Measurement
        int64_t        pktSndDropTotal;  //< copy of socket's max drop stat value
        int            rcvSeqDistance;   //< distance to the latest received sequence in the group

        size_t updateCounter; //< counter used to damper measurement pickup for longest sequence span

        // This is used only in balancing mode and it defines
        // sequence numbers of packets to be sent at the next request
        // from packData() for a socket that belongs to a balancing group.
        std::deque<SchedSeq> send_schedule;
    };

    SocketData prepareSocketData(CUDTSocket* s, SRT_GROUP_TYPE type);

    typedef std::list<SocketData> group_t;
    typedef group_t::iterator     gli_t;

} // namespace groups
} // namespace srt

#endif // INC_SRT_GROUP_COMMON_H
