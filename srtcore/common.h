/*****************************************************************************
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 * 
 * Based on UDT4 SDK version 4.11
 *****************************************************************************/

/*****************************************************************************
Copyright (c) 2001 - 2009, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 08/01/2009
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef __UDT_COMMON_H__
#define __UDT_COMMON_H__


#include <cstdlib>
#ifndef WIN32
   #include <sys/time.h>
   #include <sys/uio.h>
#else
   // #include <winsock2.h>
   //#include <windows.h>
#endif
#include <pthread.h>
#include "udt.h"
#include "utilities.h"

enum UDTSockType
{
    UDT_UNDEFINED = 0, // initial trap representation
    UDT_STREAM = 1,
    UDT_DGRAM
};


/// The message types used by UDT protocol. This is a part of UDT
/// protocol and should never be changed.
enum UDTMessageType
{
    UMSG_HANDSHAKE = 0, //< Connection Handshake. Control: see @a CHandShake.
    UMSG_KEEPALIVE = 1, //< Keep-alive.
    UMSG_ACK = 2, //< Acknowledgement. Control: past-the-end sequence number up to which packets have been received.
    UMSG_LOSSREPORT = 3, //< Negative Acknowledgement (NACK). Control: Loss list.
    UMSG_CGWARNING = 4, //< Congestion warning.
    UMSG_SHUTDOWN = 5, //< Shutdown.
    UMSG_ACKACK = 6, //< Acknowledgement of Acknowledgement. Add info: The ACK sequence number
    UMSG_DROPREQ = 7, //< Message Drop Request. Add info: Message ID. Control Info: (first, last) number of the message.
    UMSG_PEERERROR = 8, //< Signal from the Peer side. Add info: Error code.
    // ... add extra code types here
    UMSG_END_OF_TYPES,
    UMSG_EXT = 0x7FFF //< For the use of user-defined control packets.
};

// This side's role is: INITIATOR prepares the environment first, and sends
// appropriate information to the peer. The peer must be RESPONDER and be ready
// to receive it.  It's important for the encryption: the INITIATOR side generates
// the KM, and sends it to RESPONDER. RESPONDER awaits KM received from the
// INITIATOR. Note that in bidirectional mode - that is always with HSv5 - the
// INITIATOR creates both sending and receiving contexts, then sends the key to
// RESPONDER, which creates both sending and receiving contexts, using the same
// key received from INITIATOR.
//
// The method of selection:
//
// In HSv4, it's always data sender INITIATOR, and receiver - RESPONDER. The HSREQ
// and KMREQ are done AFTER the UDT connection is done using UMSG_EXT extension
// messages. As this is unidirectional, the INITIATOR prepares the sending context
// only, the RESPONDER - receiving context only.
//
// In HSv5, for caller-listener configuration, it's simple: caller is INITIATOR,
// listener is RESPONDER. In case of rendezvous the parties are equivalent,
// so the role is resolved by "cookie contest". Rendezvous sockets both know
// each other's cookie generated during the URQ_WAVEAHAND handshake phase.
// The cookies are simply compared as integer numbers; the party that has baked
// a bigger cookie wins, and becomes a INITIATOR. The other loses and becomes an
// RESPONDER.
//
// The case of a draw - that both occasionally have baked identical cookies -
// is treated as an extremely rare and virtually impossible case, so this
// results in connection rejected.
enum HandshakeSide
{
    HSD_DRAW,
    HSD_INITIATOR,    //< Side that initiates HSREQ/KMREQ. HSv4: data sender, HSv5: connecting socket or winner rendezvous socket
    HSD_RESPONDER  //< Side that expects HSREQ/KMREQ from the peer. HSv4: data receiver, HSv5: accepted socket or loser rendezvous socket
};

// For debug
std::string MessageTypeStr(UDTMessageType mt, uint32_t extt = 0);

////////////////////////////////////////////////////////////////////////////////

// Commonly used by various reading facilities
enum EReadStatus
{
    RST_OK = 0,      // A new portion of data has been received
    RST_AGAIN,       // Nothing has been received, try again
    RST_ERROR = -1   // Irrecoverable error, please close descriptor and stop reading.
};

enum EConnectStatus
{
    CONN_ACCEPT = 0,     // Received final handshake that confirms connection established
    CONN_REJECT = -1,    // Error during processing handshake.
    CONN_CONTINUE = 1,   // induction->conclusion phase
    CONN_RENDEZVOUS = 2, // pass to a separate rendezvous processing (HSv5 only)
    CONN_AGAIN = -2      // No data was read, don't change any state.
};

std::string ConnectStatusStr(EConnectStatus est);

#endif
