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
// to receive it. It's important for the encryption: the INITIATOR side generates
// the KM, and sends it to RESPONDER. RESPONDER awaits KM received from the
// INITIATOR. Note that in bidirectional mode - that is always with HSv5 - the
// INITIATOR creates both sending and receiving contexts, then sends the key to
// RESPONDER, which creates both sending and receiving contexts, using the same
// key received from INITIATOR.
//
// The method of selection:
//
// In HSv4, it's always data sender (the party that sets SRTO_SENDER flag on the
// socket) INITIATOR, and receiver - RESPONDER. The HSREQ and KMREQ are done
// AFTER the UDT connection is done using UMSG_EXT extension messages. As this
// is unidirectional, the INITIATOR prepares the sending context only, the
// RESPONDER - receiving context only.
//
// In HSv5, for caller-listener configuration, it's simple: caller is INITIATOR,
// listener is RESPONDER. In case of rendezvous the parties are equivalent,
// so the role is resolved by "cookie contest". Rendezvous sockets both know
// each other's cookie generated during the URQ_WAVEAHAND handshake phase.
// The cookies are simply compared as integer numbers; the party which's cookie
// is a greater number becomes an INITIATOR, and the other party becomes a
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
    CONN_AGAIN = -2      //< No data was read, don't change any state.
};

std::string ConnectStatusStr(EConnectStatus est);


const int64_t BW_INFINITE =  30000000/8;         //Infinite=> 30Mbps

//////////////////////////////////////////////////
//
// The Smoother Event System
//
// The events are reported using: updateCC< Event_ID >( argument ).
// There's only one arity supported: 1. Multiple arguments must be packed.


/// Event identifiers
enum ETransmissionEvent
{
    TEV_INIT,       // --> After creation, and after any parameters were updated.
    TEV_ACK,        // --> When handling UMSG_ACK - older CCC:onAck()
    TEV_ACKACK,     // --> UDT does only RTT sync, can be read from CUDT::RTT().
    TEV_LOSSREPORT, // --> When handling UMSG_LOSSREPORT - older CCC::onLoss()
    TEV_CHECKTIMER, // --> See TEV_CHT_REXMIT
    TEV_SEND,       // --> When the packet is scheduled for sending - older CCC::onPktSent
    TEV_RECEIVE,    // --> When a data packet was received - older CCC::onPktReceived
    TEV_CUSTOM,     // --> When receiving UMSG_EXT with unknown command (outside SRT_CMD_* values)

    TEV__SIZE
};

std::string TransmissionEventStr(ETransmissionEvent ev);

// Extra type definitions for events' parameters

/// Parameter for TEV_CHECKTIMER event
enum ECheckTimerStage
{
    TEV_CHT_INIT,       /// At the beginning of checkTimers, UDT: just update parameters, don't call any CCC::*
    TEV_CHT_FASTREXMIT, /// When FASTREXMIT activated - only Live mode without NAKREPORT
    TEV_CHT_REXMIT      /// When LATEREXMIT activated - old CCC::onTimeout() in UDT
};

/// Parameter for TEV_INIT event
enum EInitEvent
{
    TEV_INIT_RESET = 0, //< When creating the socket
    TEV_INIT_INPUTBW,   //< When SRTO_INPUTBW was changed
    TEV_INIT_OHEADBW    //< When SRTO_OHEADBW was changed
};

// Used by TEV_RECEIVE and TEV_CUSTOM
class CPacket;

// Used by TEV_LOSSREPORT.
// This typedef is required because without this there is a comma
// in the type definition, which doesn't compose well with macros :)
typedef std::pair<int32_t*, size_t> TevSeqArray;

// Defines parameter types for particular event
template <ETransmissionEvent Ev> struct EventMapping;

#define EVENT_ARG_TYPE(EvType, ArgType) \
template<> struct EventMapping<EvType> { typedef ArgType type; }

EVENT_ARG_TYPE(TEV_INIT, EInitEvent);              // --> Init stage, see definition
EVENT_ARG_TYPE(TEV_ACK, uint32_t);                 // --> Sequence number being acked
EVENT_ARG_TYPE(TEV_ACKACK, uint32_t);              // --> ACK journal
EVENT_ARG_TYPE(TEV_LOSSREPORT, TevSeqArray);       // --> Array with loss sequence numbers
EVENT_ARG_TYPE(TEV_CHECKTIMER, ECheckTimerStage);  // --> Where in checkTimers() it is called
EVENT_ARG_TYPE(TEV_SEND, CPacket*);                // --> Packet that has been sent
EVENT_ARG_TYPE(TEV_RECEIVE, CPacket*);             // --> Packet that has been received
EVENT_ARG_TYPE(TEV_CUSTOM, CPacket*);              // --> Packet received as UMSG_EXT and not handled

#undef EVENT_ARG_TYPE

template <class ArgType>
class EventSlotBase
{
public:
    virtual void emit(ETransmissionEvent tev, ArgType var) = 0;
    virtual ~EventSlotBase() {}
};

template <class ArgType>
class SimpleEventSlot: public EventSlotBase<ArgType>
{
public:
    typedef void dispatcher_t(void* opaque, ETransmissionEvent tev, ArgType var);
    void* opaque;
    dispatcher_t* dispatcher;

    SimpleEventSlot(void* op, dispatcher_t* disp): opaque(op), dispatcher(disp) {}

    void emit(ETransmissionEvent tev, ArgType var) ATR_OVERRIDE
    {
        (*dispatcher)(opaque, tev, var);
    }
};

template <class Class, class ArgType>
class ObjectEventSlot: public EventSlotBase<ArgType>
{
public:
    typedef void (Class::*method_ptr_t)(ETransmissionEvent tev, ArgType var);

    method_ptr_t pm;
    Class* po;

    ObjectEventSlot(Class* o, method_ptr_t m): pm(m), po(o) {}

    void emit(ETransmissionEvent tev, ArgType var) ATR_OVERRIDE
    {
        (po->*pm)(tev, var);
    }
};


template <class ArgType>
struct EventSlot
{
    mutable EventSlotBase<ArgType>* slot;
    // Create empty slot. Calls are ignored.
    EventSlot(): slot(0) {}

    // "Stealing" copy constructor, following the auto_ptr method.
    // This isn't very nice, but no other way to do it in C++03
    // without rvalue-reference and move.
    EventSlot(const EventSlot& victim)
    {
        slot = victim.slot; // Should MOVE.
        victim.slot = 0;
    }

    EventSlot(void* op, typename SimpleEventSlot<ArgType>::dispatcher_t* disp)
    {
        slot = new SimpleEventSlot<ArgType>(op, disp);
    }

    template <class ObjectClass>
    EventSlot(ObjectClass* obj, typename ObjectEventSlot<ObjectClass, ArgType>::method_ptr_t method)
    {
        slot = new ObjectEventSlot<ObjectClass, ArgType>(obj, method);
    }

    void emit(ETransmissionEvent tev, ArgType var)
    {
        if (!slot)
            return;
        slot->emit(tev, var);
    }

    ~EventSlot()
    {
        if (slot)
            delete slot;
    }
};

// Using an ugly and clumsy explicit definition of 'disp' and 'method'
// parameters below because otherwise the compiler can't match the type at the
// call.  In C++11 they at least would be able to use global type alias:
// template <class ArgType> using slot_dispatcher_t = void ()(void*...)
template <class ArgType> inline
EventSlot<ArgType> MakeEventSlot(void* op,
        void (*disp)(void* opaque, ETransmissionEvent tev, ArgType var))
{
    return EventSlot<ArgType>(op, disp);
}

template <class ArgType, class ObjectClass> inline
EventSlot<ArgType> MakeEventSlot(ObjectClass* obj,
        void (ObjectClass::*method)(ETransmissionEvent tev, ArgType var))
{
    return EventSlot<ArgType>(obj, method);
}

// Single array containing event handlers (slots). This wrapper
// is necessary for easy translation from a value of ETransmissionEvent
// to EventSlot< ArgType > for ArgType assigned to this event.
template <ETransmissionEvent Ev>
struct SlotVector
{
    typedef std::vector<EventSlot<typename EventMapping<Ev>::type> > svector_t;
    svector_t slots;
};

// This replaces an array of vectors containing event slots; there's one
// element created per event, which is an array containing handlers. This
// general definition shall create its vector cell v_, and also the "next_"
// field containing the same array with size - 1. When this reaches 1 ...
template <size_t Size>
struct SlotPack
{
    static const size_t Ix = Size-1;
    SlotVector<ETransmissionEvent(Size-1)> v_;
    SlotPack<Size-1> next_;
};

// ... then this specialization is used, which contains only the vector cell,
// but no "next_" field.
template <>
struct SlotPack<1>
{
    static const size_t Ix = 0;
    SlotVector<ETransmissionEvent(0)> v_;
};

// The *Tools classes are workaround for a lacking feature in C++03
// for providing partial specializations for function templates.
// The general definition effectively handles only the "false"
// case because it's a complement for "true", and for "true"
// there's a specialization provided.
template<bool Equal=false> // this =false is only for clarity
struct SlotTools
{
    template <ETransmissionEvent Ev, class SlotPackT>
    static typename SlotVector<Ev>::svector_t& Get(SlotPackT& sp)
    {
        return SlotTools<Ev == SlotPackT::Ix-1>::template Get<Ev>(sp.next_);
    }
};

template <>
struct SlotTools<true>
{
    template <ETransmissionEvent Ev, class SlotPackT>
    static typename SlotVector<Ev>::svector_t& Get(SlotPackT& sp)
    {
        return sp.v_.slots;
    }
};

// Helper function. If you treat the `SlotPack<N> slotpack` as an array of N
// elements, the call `slot_access<Event>(slotpack)` is equivalent to
// `slotpack[int(Event)]`. Might be a good idea to make a method of it, but it
// would have to be called like `slotpack.template at<Event>()`, so it doesn't
// look any better.
template <ETransmissionEvent Ev, class SlotPackT>
typename SlotVector<Ev>::svector_t& slot_access(SlotPackT& sp)
{
    // Resolves to:
    //  - SlotTools<true>::Get(sp) --> returns the vector at this slot pack's top
    //  - SlotTools<false>::Get(sp) --> extracts sp.next and calls itself with Ix-1
    return SlotTools<Ev == SlotPackT::Ix>::template Get<Ev>(sp);
}



// Old UDT library specific classes, moved from utilities as utilities
// should now be general-purpose.

class CTimer
{
public:
   CTimer();
   ~CTimer();

public:

      /// Sleep for "interval" CCs.
      /// @param [in] interval CCs to sleep.

   void sleep(uint64_t interval);

      /// Seelp until CC "nexttime".
      /// @param [in] nexttime next time the caller is waken up.

   void sleepto(uint64_t nexttime);

      /// Stop the sleep() or sleepto() methods.

   void interrupt();

      /// trigger the clock for a tick, for better granuality in no_busy_waiting timer.

   void tick();

public:

      /// Read the CPU clock cycle into x.
      /// @param [out] x to record cpu clock cycles.

   static void rdtsc(uint64_t &x);

      /// return the CPU frequency.
      /// @return CPU frequency.

   static uint64_t getCPUFrequency();

      /// check the current time, 64bit, in microseconds.
      /// @return current time in microseconds.

   static uint64_t getTime();

      /// trigger an event such as new connection, close, new data, etc. for "select" call.

   static void triggerEvent();

   enum EWait {WT_EVENT, WT_ERROR, WT_TIMEOUT};

      /// wait for an event to br triggered by "triggerEvent".
      /// @retval WT_EVENT The event has happened
      /// @retval WT_TIMEOUT The event hasn't happened, the function exited due to timeout
      /// @retval WT_ERROR The function has exit due to an error

   static EWait waitForEvent();

      /// sleep for a short interval. exact sleep time does not matter

   static void sleep();
   
      /// Wait for condition with timeout 
      /// @param [in] cond Condition variable to wait for
      /// @param [in] mutex locked mutex associated with the condition variable
      /// @param [in] delay timeout in microseconds
      /// @retval 0 Wait was successfull
      /// @retval ETIMEDOUT The wait timed out

   static int condTimedWaitUS(pthread_cond_t* cond, pthread_mutex_t* mutex, uint64_t delay);

private:
   uint64_t getTimeInMicroSec();

private:
   uint64_t m_ullSchedTime;             // next schedulled time

   pthread_cond_t m_TickCond;
   pthread_mutex_t m_TickLock;

   static pthread_cond_t m_EventCond;
   static pthread_mutex_t m_EventLock;

private:
   static uint64_t s_ullCPUFrequency;	// CPU frequency : clock cycles per microsecond
   static uint64_t readCPUFrequency();
   static bool m_bUseMicroSecond;       // No higher resolution timer available, use gettimeofday().
};

////////////////////////////////////////////////////////////////////////////////

class CGuard
{
public:
   /// Constructs CGuard, which locks the given mutex for
   /// the scope where this object exists.
   /// @param lock Mutex to lock
   /// @param if_condition If this is false, CGuard will do completely nothing
   CGuard(pthread_mutex_t& lock, bool if_condition = true);
   ~CGuard();

public:
   static int enterCS(pthread_mutex_t& lock);
   static int leaveCS(pthread_mutex_t& lock);

   static void createMutex(pthread_mutex_t& lock);
   static void releaseMutex(pthread_mutex_t& lock);

   static void createCond(pthread_cond_t& cond);
   static void releaseCond(pthread_cond_t& cond);

   void forceUnlock();

private:
   pthread_mutex_t& m_Mutex;            // Alias name of the mutex to be protected
   int m_iLocked;                       // Locking status

   CGuard& operator=(const CGuard&);
};

class InvertedGuard
{
    pthread_mutex_t* m_pMutex;
public:

    InvertedGuard(pthread_mutex_t* smutex): m_pMutex(smutex)
    {
        if ( !smutex )
            return;

        CGuard::leaveCS(*smutex);
    }

    ~InvertedGuard()
    {
        if ( !m_pMutex )
            return;

        CGuard::enterCS(*m_pMutex);
    }
};

////////////////////////////////////////////////////////////////////////////////

// UDT Sequence Number 0 - (2^31 - 1)

// seqcmp: compare two seq#, considering the wraping
// seqlen: length from the 1st to the 2nd seq#, including both
// seqoff: offset from the 2nd to the 1st seq#
// incseq: increase the seq# by 1
// decseq: decrease the seq# by 1
// incseq: increase the seq# by a given offset

class CSeqNo
{
public:
   inline static int seqcmp(int32_t seq1, int32_t seq2)
   {return (abs(seq1 - seq2) < m_iSeqNoTH) ? (seq1 - seq2) : (seq2 - seq1);}

   inline static int seqlen(int32_t seq1, int32_t seq2)
   {return (seq1 <= seq2) ? (seq2 - seq1 + 1) : (seq2 - seq1 + m_iMaxSeqNo + 2);}

   inline static int seqoff(int32_t seq1, int32_t seq2)
   {
      if (abs(seq1 - seq2) < m_iSeqNoTH)
         return seq2 - seq1;

      if (seq1 < seq2)
         return seq2 - seq1 - m_iMaxSeqNo - 1;

      return seq2 - seq1 + m_iMaxSeqNo + 1;
   }

   inline static int32_t incseq(int32_t seq)
   {return (seq == m_iMaxSeqNo) ? 0 : seq + 1;}

   inline static int32_t decseq(int32_t seq)
   {return (seq == 0) ? m_iMaxSeqNo : seq - 1;}

   inline static int32_t incseq(int32_t seq, int32_t inc)
   {return (m_iMaxSeqNo - seq >= inc) ? seq + inc : seq - m_iMaxSeqNo + inc - 1;}
   // m_iMaxSeqNo >= inc + sec  --- inc + sec <= m_iMaxSeqNo
   // if inc + sec > m_iMaxSeqNo then return seq + inc - (m_iMaxSeqNo+1)

   inline static int32_t decseq(int32_t seq, int32_t dec)
   {
       // Check if seq - dec < 0, but before it would have happened
       if ( seq < dec )
       {
           int32_t left = dec - seq; // This is so many that is left after dragging dec to 0
           // So now decrement the (m_iMaxSeqNo+1) by "left"
           return m_iMaxSeqNo - left + 1;
       }
       return seq - dec;
   }

public:
   static const int32_t m_iSeqNoTH = 0x3FFFFFFF;             // threshold for comparing seq. no.
   static const int32_t m_iMaxSeqNo = 0x7FFFFFFF;            // maximum sequence number used in UDT
};

////////////////////////////////////////////////////////////////////////////////

// UDT ACK Sub-sequence Number: 0 - (2^31 - 1)

class CAckNo
{
public:
   inline static int32_t incack(int32_t ackno)
   {return (ackno == m_iMaxAckSeqNo) ? 0 : ackno + 1;}

public:
   static const int32_t m_iMaxAckSeqNo = 0x7FFFFFFF;         // maximum ACK sub-sequence number used in UDT
};



////////////////////////////////////////////////////////////////////////////////

struct CIPAddress
{
   static bool ipcmp(const struct sockaddr* addr1, const struct sockaddr* addr2, int ver = AF_INET);
   static void ntop(const struct sockaddr* addr, uint32_t ip[4], int ver = AF_INET);
   static void pton(struct sockaddr* addr, const uint32_t ip[4], int ver = AF_INET);
   static std::string show(const struct sockaddr* adr);
};

////////////////////////////////////////////////////////////////////////////////

struct CMD5
{
   static void compute(const char* input, unsigned char result[16]);
};

// Debug stats
template <size_t SIZE>
class StatsLossRecords
{
    int32_t initseq;
    std::bitset<SIZE> array;

public:

    StatsLossRecords(): initseq(-1) {}

    // To check if this structure still keeps record of that sequence.
    // This is to check if the information about this not being found
    // is still reliable.
    bool exists(int32_t seq)
    {
        return initseq != -1 && CSeqNo::seqcmp(seq, initseq) >= 0;
    }

    int32_t base() { return initseq; }

    void clear()
    {
        initseq = -1;
        array.reset();
    }

    void add(int32_t lo, int32_t hi)
    {
        int32_t end = lo + CSeqNo::seqcmp(hi, lo);
        for (int32_t i = lo; i != end; i = CSeqNo::incseq(i))
            add(i);
    }

    void add(int32_t seq)
    {
        if ( array.none() )
        {
            // May happen it wasn't initialized. Set it as initial loss sequence.
            initseq = seq;
            array[0] = true;
            return;
        }

        // Calculate the distance between this seq and the oldest one.
        int seqdiff = CSeqNo::seqcmp(seq, initseq);
        if ( seqdiff > int(SIZE) )
        {
            // Size exceeded. Drop the oldest sequences.
            // First calculate how many must be removed.
            size_t toremove = seqdiff - SIZE;
            // Now, since that position, find the nearest 1
            while ( !array[toremove] && toremove <= SIZE )
                ++toremove;

            // All have to be dropped, so simply reset the array
            if ( toremove == SIZE )
            {
                initseq = seq;
                array[0] = true;
                return;
            }

            // Now do the shift of the first found 1 to position 0
            // and its index add to initseq
            initseq += toremove;
            seqdiff -= toremove;
            array >>= toremove;
        }

        // Now set appropriate bit that represents this seq
        array[seqdiff] = true;
    }

    StatsLossRecords& operator << (int32_t seq)
    {
        add(seq);
        return *this;
    }

    void remove(int32_t seq)
    {
        // Check if is in range. If not, ignore.
        int seqdiff = CSeqNo::seqcmp(seq, initseq);
        if ( seqdiff < 0 )
            return; // already out of array
        if ( seqdiff > SIZE )
            return; // never was added!

        array[seqdiff] = true;
    }

    bool find(int32_t seq) const
    {
        int seqdiff = CSeqNo::seqcmp(seq, initseq);
        if ( seqdiff < 0 )
            return false; // already out of array
        if ( size_t(seqdiff) > SIZE )
            return false; // never was added!

        return array[seqdiff];
    }

#if HAVE_CXX11

    std::string to_string() const
    {
        std::string out;
        for (size_t i = 0; i < SIZE; ++i)
        {
            if ( array[i] )
                out += std::to_string(initseq+i) + " ";
        }

        return out;
    }
#endif
};


// Version parsing
inline ATR_CONSTEXPR uint32_t SrtVersion(int major, int minor, int patch)
{
    return patch + minor*0x100 + major*0x10000;
}

inline int32_t SrtParseVersion(const char* v)
{
    int major, minor, patch;
    int result = sscanf(v, "%d.%d.%d", &major, &minor, &patch);

    if ( result != 3 )
    {
        return 0;
        fprintf(stderr, "Invalid version format for HAISRT_VERSION: %s - use m.n.p\n", v);
        throw v; // Throwing exception, as this function will be run before main()
    }

    return major*0x10000 + minor*0x100 + patch;
}

inline std::string SrtVersionString(int version)
{
    int patch = version % 0x100;
    int minor = (version/0x100)%0x100;
    int major = version/0x10000;

    char buf[20];
    sprintf(buf, "%d.%d.%d", major, minor, patch);
    return buf;
}

#endif
