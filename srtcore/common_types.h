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
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_COMMON_TYPES_H
#define INC_SRT_COMMON_TYPES_H

namespace srt
{

enum ErrorHandling
{
    ERH_RETURN,
    ERH_THROW,
    ERH_ABORT
};

// This enum is deprecated, only used to recognize the old version
// of the handshake pre 1.3.0 version.
enum UDTSockType
{
    UDT_UNDEFINED = 0, // initial trap representation
    UDT_STREAM = 1,
    UDT_DGRAM = 2
};


/// The message types used by UDT protocol. This is a part of UDT
/// protocol and should never be changed.
enum UDTMessageType
{
    UMSG_HANDSHAKE = 0, //< Connection Handshake. Control: see @a CHandShake.
    UMSG_KEEPALIVE = 1, //< Keep-alive.
    UMSG_ACK = 2, //< Acknowledgement. Control: past-the-end sequence number up to which packets have been received.
    UMSG_LOSSREPORT = 3, //< Negative Acknowledgement (NAK). Control: Loss list.
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
// to receive it. It's important for the encryption: the INITIATOR side generates
// the KM, and sends it to RESPONDER. RESPONDER awaits KM received from the
// INITIATOR. The INITIATOR creates both sending and receiving contexts (each one
// with its dedicated key), then sends the key to RESPONDER, which creates both
// sending and receiving contexts, using the key received from INITIATOR.
//
// The method of selection:
//
// 1. For caller-listener configuration: caller is INITIATOR, listener is
// RESPONDER.
//
// 2. For rendezvous the parties are equivalent, so the role is resolved by
// "cookie contest". Rendezvous sockets both know each other's cookie generated
// during the URQ_WAVEAHAND handshake phase. The cookies are simply compared
// as integer numbers; the party which's cookie is a greater number becomes an
// INITIATOR, and the other party becomes a RESPONDER. 
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

// Commonly used by various reading facilities
enum EReadStatus
{
    RST_OK = 0,      //< A new portion of data has been received
    RST_AGAIN,       //< Nothing has been received, try again
    RST_ERROR = -1   //< Irrecoverable error, please close descriptor and stop reading.
};

enum EConnectStatus
{
    CONN_ACCEPT = 0,     //< Received final handshake that confirms connection established
    CONN_REJECT = -1,    //< Error during processing handshake.
    CONN_CONTINUE = 1,   //< induction->conclusion phase
    CONN_RENDEZVOUS = 2, //< pass to a separate rendezvous processing (HSv5 only)
    CONN_CONFUSED = 3,   //< listener thinks it's connected, but caller missed conclusion
    CONN_RUNNING = 10,   //< no connection in progress, already connected
    CONN_AGAIN = -2      //< No data was read, don't change any state.
};

enum EConnectMethod
{
    COM_ASYNCHRO,
    COM_SYNCHRO
};

enum ETransmissionEvent
{
    TEV_INIT,       // --> After creation, and after any parameters were updated.
    TEV_ACK,        // --> When handling UMSG_ACK - older CCC:onAck()
    TEV_ACKACK,     // --> UDT does only RTT sync, can be read from CUDT::avgRTT().
    TEV_LOSSREPORT, // --> When handling UMSG_LOSSREPORT - older CCC::onLoss()
    TEV_CHECKTIMER, // --> See TEV_CHT_REXMIT
    TEV_SEND,       // --> When the packet is scheduled for sending - older CCC::onPktSent
    TEV_RECEIVE,    // --> When a data packet was received - older CCC::onPktReceived
    TEV_CUSTOM,     // --> probably dead call - older CCC::processCustomMsg
    TEV_SYNC,       // --> Backup group. When rate estimation is derived from an active member, and update is needed.

    TEV_E_SIZE
};

}
#endif
