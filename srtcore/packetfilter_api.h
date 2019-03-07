/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC__PACKETFILTER_API_H
#define INC__PACKETFILTER_API_H

enum SrtPktHeaderFields
{
    SRT_PH_SEQNO = 0, //< sequence number
    SRT_PH_MSGNO = 1, //< message number
    SRT_PH_TIMESTAMP = 2, //< time stamp
    SRT_PH_ID = 3  //< socket ID
};


#endif
