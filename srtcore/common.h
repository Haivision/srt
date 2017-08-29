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


const int64_t BW_INFINITE =  30000000/8;         //Infinite=> 30Mbps
const int DEFAULT_LIVE_LATENCY = 120; // (mSec)


enum ETransmissionEvent
{
    TEV_INIT,
    TEV_HANDSHAKE,
    TEV_ACK,        // --> CCC:onAck()
    TEV_ACKACK,     // --> UDT does only RTT sync, can be read from CUDT::RTT().
    TEV_LOSSREPORT, // --> CCC::onLoss()
    TEV_CHECKTIMER, // --> See TEV_CHT_REXMIT
    TEV_SEND,       // --> CCC::onPktSent
    TEV_RECEIVE,    // --> CCC::onPktReceived
    TEV_CUSTOM,      // --> CCC::processCustomMsg, but probably dead call

    TEV__SIZE
};

// Special parameter for TEV_CHECKTIMER
enum ECheckTimerStage
{
    TEV_CHT_INIT,       // --> UDT: just update parameters, don't call any CCC::*
    TEV_CHT_FASTREXMIT, // --> not available on UDT
    TEV_CHT_REXMIT      // --> CCC::onTimeout() in UDT
};

enum EInitEvent
{
    TEV_INIT_RESET = 0,
    TEV_INIT_INPUTBW,
    TEV_INIT_OHEADBW
};

class CPacket;

// XXX Use some more standard less hand-crafted solution, if possible
// XXX Consider creating a mapping between TEV_* values and associated types,
// so that the type is compiler-enforced when calling updateCC() and when
// connecting signals to slots.
struct EventVariant
{
    enum Type {UNDEFINED, PACKET, ARRAY, ACK, STAGE, INIT} type;
    union U
    {
        CPacket* packet;
        uint32_t ack;
        struct
        {
            int32_t* ptr;
            size_t len;
        } array;
        ECheckTimerStage stage;
        EInitEvent init;
    } u;

    EventVariant()
    {
        type = UNDEFINED;
        memset(&u, 0, sizeof u);
    }

    template<Type t>
    struct VariantFor;

    template <Type tp, typename Arg>
    void Assign(Arg arg)
    {
        type = tp;
        (u.*(VariantFor<tp>::field())) = arg;
        //(u.*field) = arg;
    }

    void operator=(CPacket* arg) { Assign<PACKET>(arg); };
    void operator=(uint32_t arg) { Assign<ACK>(arg); };
    void operator=(ECheckTimerStage arg) { Assign<STAGE>(arg); };
    void operator=(EInitEvent arg) { Assign<INIT>(arg); };

    // Note: UNDEFINED and ARRAY don't have assignment operator.
    // For ARRAY you'll use 'set' function. For UNDEFINED there's nothing.


    template <class T>
    EventVariant(T arg)
    {
        *this = arg;
    }

    int32_t* get_ptr()
    {
        return u.array.ptr;
    }

    size_t get_len()
    {
        return u.array.len;
    }

    void set(int32_t* ptr, size_t len)
    {
        type = ARRAY;
        u.array.ptr = ptr;
        u.array.len = len;
    }

    EventVariant(int32_t* ptr, size_t len)
    {
        set(ptr, len);
    }

    template<Type T>
    typename VariantFor<T>::type get()
    {
        return u.*(VariantFor<T>::field());
    }
};

/*
    Maybe later.
    This had to be a solution for automatic extraction of the
    type hidden in particular EventArg for particular event so
    that it's not runtime-mistaken.

    In order that this make sense there would be required an array
    indexed by event id (just like a slot array m_Slots in CUDT),
    where the "type distiller" function would be extracted and then
    combined with the user-connected slot function this would call
    it already with correct type. Note that also the ConnectSignal
    function would have to get the signal id by template parameter,
    not function parameter. For example:

    m_parent->ConnectSignal<TEV_ACK>(SSLOT(updateOnSent));

    in which updateOnSent would have to receive an appropriate type.
    This has a disadvantage that you can't connect multiple signals
    with different argument types to the same slot, you'd have to
    make slot wrappers to translate arguments.

    It seems that a better idea would be to create binders that would
    translate the argument from EventArg to the correct type according
    to the rules imposed by particular event id. But I'd not make it
    until there's a green light on C++11 for SRT, so maybe in a far future.

template <ETransmissionEvent type>
class EventArgType;
#define MAP_EVENT_TYPE(tev, tp) template<> class EventArgType<tev> { typedef tp type; }
*/


// The 'type' field wouldn't be even necessary if we
// could use any method of extracting 'type' from 'type U::*' expression.
template<> struct EventVariant::VariantFor<EventVariant::PACKET>
{
    typedef CPacket* type;
    static type U::*field() {return &U::packet;}
};

template<> struct EventVariant::VariantFor<EventVariant::ACK>
{
    typedef uint32_t type;
    static type U::*field() { return &U::ack; }
};

template<> struct EventVariant::VariantFor<EventVariant::STAGE>
{
    typedef ECheckTimerStage type;
    static type U::*field() { return &U::stage; }
};

template<> struct EventVariant::VariantFor<EventVariant::INIT>
{
    typedef EInitEvent type;
    static type U::*field() { return &U::init; }
};

// Sigh. The code must be C++03, C++11 and C++17 compliant.
// There's std::mem_fun in C++03, but it's deprecated in C++11 and removed in
// C++17.  There's std::function and std::bind, but only since C++11. No way to
// define it any compatible way.  There were already problems with ref_t and
// unique_ptr/auto_ptr, for which custom classes were needed.

// I'd stay with custom class, completely. This can be changed in future, when
// all compilers that don't support C++11 are finally abandoned. Not sure when
// this will happen - I remember that "ARM C++" pre-standard version has been
// abandoned maybe around 2008 year, or maybe it's even still rolling around
// somewhere. Hopes of abandoning C language are even more a wishful thinking.

class EventSlotBase
{
public:
    virtual void emit(ETransmissionEvent tev, EventVariant var) = 0;
    typedef void dispatcher_t(void* opaque, ETransmissionEvent tev, EventVariant var);

    virtual ~EventSlotBase() {}
};

class SimpleEventSlot: public EventSlotBase
{
public:
    void* opaque;
    dispatcher_t* dispatcher;

    SimpleEventSlot(void* op, dispatcher_t* disp): opaque(op), dispatcher(disp) {}

    void emit(ETransmissionEvent tev, EventVariant var) ATR_OVERRIDE
    {
        (*dispatcher)(opaque, tev, var);
    }
};

template <class Class>
class ObjectEventSlot: public EventSlotBase
{
public:
    typedef void (Class::*method_ptr_t)(ETransmissionEvent tev, EventVariant var);

    method_ptr_t pm;
    Class* po;

    ObjectEventSlot(Class* o, method_ptr_t m): pm(m), po(o) {}

    void emit(ETransmissionEvent tev, EventVariant var) ATR_OVERRIDE
    {
        (po->*pm)(tev, var);
    }
};


struct EventSlot
{
    EventSlotBase* slot;
    // Create empty slot. Calls are ignored.
    EventSlot(): slot(0) {}

    EventSlot(void* op, EventSlotBase::dispatcher_t* disp)
    {
        slot = new SimpleEventSlot(op, disp);
    }

    template <class ObjectClass>
    EventSlot(ObjectClass* obj, typename ObjectEventSlot<ObjectClass>::method_ptr_t method)
    {
        slot = new ObjectEventSlot<ObjectClass>(obj, method);
    }

    void emit(ETransmissionEvent tev, EventVariant var)
    {
        if (!slot)
            return;
        slot->emit(tev, var);
    }

    ~EventSlot()
    {
        delete slot;
    }
};



#endif
