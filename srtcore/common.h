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

const size_t DEFAULT_MPEG_UNIT_SIZE = 188;
const size_t DEFAULT_LIVE_PAYLOAD_SIZE = 7*DEFAULT_MPEG_UNIT_SIZE; // 1316

// Old UDT library specific classes, moved from utilities as utilities
// should now be general-purpose.

class CTimer
{
public:
   CTimer();
   ~CTimer();

public:

      /// Sleep for "interval" CCs.
      /// @param interval [in] CCs to sleep.

   void sleep(uint64_t interval);

      /// Seelp until CC "nexttime".
      /// @param nexttime [in] next time the caller is waken up.

   void sleepto(uint64_t nexttime);

      /// Stop the sleep() or sleepto() methods.

   void interrupt();

      /// trigger the clock for a tick, for better granuality in no_busy_waiting timer.

   void tick();

public:

      /// Read the CPU clock cycle into x.
      /// @param x [out] to record cpu clock cycles.

   static void rdtsc(uint64_t &x);

      /// return the CPU frequency.
      /// @return CPU frequency.

   static uint64_t getCPUFrequency();

      /// check the current time, 64bit, in microseconds.
      /// @return current time in microseconds.

   static uint64_t getTime();

      /// trigger an event such as new connection, close, new data, etc. for "select" call.

   static void triggerEvent();

      /// wait for an event to br triggered by "triggerEvent".

   static void waitForEvent();

      /// sleep for a short interval. exact sleep time does not matter

   static void sleep();

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
