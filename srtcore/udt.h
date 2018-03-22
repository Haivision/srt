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
   Yunhong Gu, last updated 01/18/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

/* WARNING!!!
 * Since now this file is a "C and C++ header".
 * It should be then able to be interpreted by C compiler, so
 * all C++-oriented things must be ifdef'd-out by __cplusplus.
 *
 * Mind also comments - to prevent any portability problems,
 * B/C++ comments (// -> EOL) should not be used unless the
 * area is under __cplusplus condition already.
 *
 * NOTE: this file contains _STRUCTURES_ that are common to C and C++,
 * plus some functions and other functionalities ONLY FOR C++. This
 * file doesn't contain _FUNCTIONS_ predicted to be used in C - see udtc.h
 */

#ifndef __UDT_H__
#define __UDT_H__

#include "srt4udt.h"
#include "logging_api.h"

/*
* SRT_ENABLE_THREADCHECK (THIS IS SET IN MAKEFILE NOT HERE)
*/
#if defined(SRT_ENABLE_THREADCHECK)
#include <threadcheck.h>
#else
#define THREAD_STATE_INIT(name)
#define THREAD_EXIT()
#define THREAD_PAUSED()
#define THREAD_RESUMED()
#define INCREMENT_THREAD_ITERATIONS()
#endif

/* Obsolete way to define MINGW */
#ifndef __MINGW__
#if defined(__MINGW32__) || defined(__MINGW64__)
#define __MINGW__ 1
#endif
#endif

// This is a "protected header"; should include all required
// system headers, as required on particular platform.
#include "platform_sys.h"

#ifdef __cplusplus
#include <fstream>
#include <set>
#include <string>
#include <vector>
#endif

////////////////////////////////////////////////////////////////////////////////

//if compiling on VC6.0 or pre-WindowsXP systems
//use -DLEGACY_WIN32

//if compiling with MinGW, it only works on XP or above
//use -D_WIN32_WINNT=0x0501


#ifdef WIN32
   #ifndef __MINGW__
      // Explicitly define 32-bit and 64-bit numbers
      typedef __int32 int32_t;
      typedef __int64 int64_t;
      typedef unsigned __int32 uint32_t;
      #ifndef LEGACY_WIN32
         typedef unsigned __int64 uint64_t;
      #else
         // VC 6.0 does not support unsigned __int64: may cause potential problems.
         typedef __int64 uint64_t;
      #endif

	#ifdef UDT_DYNAMIC
      #ifdef UDT_EXPORTS
         #define UDT_API __declspec(dllexport)
      #else
         #define UDT_API __declspec(dllimport)
      #endif
	#else
		#define UDT_API
	#endif
   #else
      #define UDT_API
   #endif
#else
   #define UDT_API __attribute__ ((visibility("default")))
#endif

#define NO_BUSY_WAITING

#ifdef WIN32
   #ifndef __MINGW__
      typedef SOCKET SYSSOCKET;
   #else
      typedef int SYSSOCKET;
   #endif
#else
   typedef int SYSSOCKET;
#endif

typedef SYSSOCKET UDPSOCKET;
typedef int UDTSOCKET;

////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
// This facility is used only for select() function.
// This is considered obsolete and the epoll() functionality rather should be used.
typedef std::set<UDTSOCKET> ud_set;
#define UD_CLR(u, uset) ((uset)->erase(u))
#define UD_ISSET(u, uset) ((uset)->find(u) != (uset)->end())
#define UD_SET(u, uset) ((uset)->insert(u))
#define UD_ZERO(uset) ((uset)->clear())
#endif

enum SRT_KM_STATE
{
    SRT_KM_S_UNSECURED = 0,      //No encryption
    SRT_KM_S_SECURING  = 1,      //Stream encrypted, exchanging Keying Material
    SRT_KM_S_SECURED   = 2,      //Stream encrypted, keying Material exchanged, decrypting ok.
    SRT_KM_S_NOSECRET  = 3,      //Stream encrypted and no secret to decrypt Keying Material
    SRT_KM_S_BADSECRET = 4       //Stream encrypted and wrong secret, cannot decrypt Keying Material
};

enum UDT_EPOLL_OPT
{
   // this values are defined same as linux epoll.h
   // so that if system values are used by mistake, they should have the same effect
   UDT_EPOLL_IN = 0x1,
   UDT_EPOLL_OUT = 0x4,
   UDT_EPOLL_ERR = 0x8
};

enum UDTSTATUS {
    UDT_INIT = 1,
    UDT_OPENED,
    UDT_LISTENING,
    UDT_CONNECTING,
    UDT_CONNECTED,
    UDT_BROKEN,
    UDT_CLOSING,
    UDT_CLOSED,
    UDT_NONEXIST
};

////////////////////////////////////////////////////////////////////////////////

// XXX DEPRECATED, once the SRT(C) API is in use. All these values
// are duplicated with SRTO_ prefix in srt.h file.
enum UDT_SOCKOPT
{
   UDT_MSS,             // the Maximum Transfer Unit
   UDT_SNDSYN,          // if sending is blocking
   UDT_RCVSYN,          // if receiving is blocking
   UDT_CC,              // custom congestion control algorithm
   UDT_FC,		// Flight flag size (window size)
   UDT_SNDBUF,          // maximum buffer in sending queue
   UDT_RCVBUF,          // UDT receiving buffer size
   UDT_LINGER,          // waiting for unsent data when closing
   UDP_SNDBUF,          // UDP sending buffer size
   UDP_RCVBUF,          // UDP receiving buffer size
   UDT_MAXMSG,          // maximum datagram message size
   UDT_MSGTTL,          // time-to-live of a datagram message
   UDT_RENDEZVOUS,      // rendezvous connection mode
   UDT_SNDTIMEO,        // send() timeout
   UDT_RCVTIMEO,        // recv() timeout
   UDT_REUSEADDR,       // reuse an existing port or create a new one
   UDT_MAXBW,           // maximum bandwidth (bytes per second) that the connection can use
   UDT_STATE,           // current socket state, see UDTSTATUS, read only
   UDT_EVENT,           // current avalable events associated with the socket
   UDT_SNDDATA,         // size of data in the sending buffer
   UDT_RCVDATA,         // size of data available for recv
   SRT_SENDER = 21,     // Set sender mode, independent of connection mode
#ifdef SRT_ENABLE_TSBPD
   SRT_TSBPDMODE = 22,  // Enable/Disable TsbPd. Enable -> Tx set origin timestamp, Rx deliver packet at origin time + delay
   SRT_TSBPDDELAY,      // TsbPd receiver delay (mSec) to absorb burst of missed packet retransmission
#endif
#ifdef SRT_ENABLE_INPUTRATE
   SRT_INPUTBW = 24,
   SRT_OHEADBW,
#endif
   SRT_PASSPHRASE = 26, // PBKDF2 passphrase size[0,10..80] 0:disable crypto
   SRT_PBKEYLEN,        // PBKDF2 generated key len in bytes {16,24,32} Default: 16 (128-bit)
   SRT_KMSTATE,         // Key Material exchange status (SRT_KM_STATE)
#ifdef SRT_ENABLE_IPOPTS
   SRT_IPTTL = 29,
   SRT_IPTOS,
#endif
#ifdef SRT_ENABLE_TLPKTDROP
   SRT_TLPKTDROP = 31,  // Enable/Disable receiver pkt drop
   SRT_TSBPDMAXLAG,     // Decoder's tolerated lag past TspPD delay (decoder's buffer)
#endif
#ifdef SRT_ENABLE_NAKREPORT
   SRT_RCVNAKREPORT = 33,   // Enable/Disable receiver's Periodic NAK Report to sender
#endif
   SRT_AGENTVERSION = 34,
   SRT_PEERVERSION,
#ifdef SRT_ENABLE_CONNTIMEO
   SRT_CONNTIMEO = 36,
#endif
   SRT_TWOWAYDATA = 37,
   SRT_SNDPBKEYLEN = 38,
   SRT_RCVPBKEYLEN,
   SRT_SNDPEERKMSTATE,
   SRT_RCVKMSTATE,
   SRT_LOSSMAXTTL,
};

/* Binary backward compatibility obsolete options */
#define SRT_NAKREPORT   SRT_RCVNAKREPORT

////////////////////////////////////////////////////////////////////////////////

struct CPerfMon
{
   // global measurements
   int64_t msTimeStamp;                 // time since the UDT entity is started, in milliseconds
   int64_t pktSentTotal;                // total number of sent data packets, including retransmissions
   int64_t pktRecvTotal;                // total number of received packets
   int pktSndLossTotal;                 // total number of lost packets (sender side)
   int pktRcvLossTotal;                 // total number of lost packets (receiver side)
   int pktRetransTotal;                 // total number of retransmitted packets
   int pktRcvRetransTotal;              // total number of retransmitted packets received
   int pktSentACKTotal;                 // total number of sent ACK packets
   int pktRecvACKTotal;                 // total number of received ACK packets
   int pktSentNAKTotal;                 // total number of sent NAK packets
   int pktRecvNAKTotal;                 // total number of received NAK packets
   int64_t usSndDurationTotal;		// total time duration when UDT is sending data (idle time exclusive)

   // local measurements
   int64_t pktSent;                     // number of sent data packets, including retransmissions
   int64_t pktRecv;                     // number of received packets
   int pktSndLoss;                      // number of lost packets (sender side)
   int pktRcvLoss;                      // number of lost packets (receiver side)
   int pktRetrans;                      // number of retransmitted packets
   int pktRcvRetrans;                   // number of retransmitted packets received
   int pktSentACK;                      // number of sent ACK packets
   int pktRecvACK;                      // number of received ACK packets
   int pktSentNAK;                      // number of sent NAK packets
   int pktRecvNAK;                      // number of received NAK packets
   double mbpsSendRate;                 // sending rate in Mb/s
   double mbpsRecvRate;                 // receiving rate in Mb/s
   int64_t usSndDuration;		// busy sending time (i.e., idle time exclusive)
   int pktReorderDistance;              // size of order discrepancy in received sequences
   double pktRcvAvgBelatedTime;             // average time of packet delay for belated packets (packets with sequence past the ACK)
   int64_t pktRcvBelated;              // number of received AND IGNORED packets due to having come too late

   // instant measurements
   double usPktSndPeriod;               // packet sending period, in microseconds
   int pktFlowWindow;                   // flow window size, in number of packets
   int pktCongestionWindow;             // congestion window size, in number of packets
   int pktFlightSize;                   // number of packets on flight
   double msRTT;                        // RTT, in milliseconds
   double mbpsBandwidth;                // estimated bandwidth, in Mb/s
   int byteAvailSndBuf;                 // available UDT sender buffer size
   int byteAvailRcvBuf;                 // available UDT receiver buffer size
};

#ifdef SRT_ENABLE_BSTATS
struct CBytePerfMon
{
   // global measurements
   int64_t msTimeStamp;                 // time since the UDT entity is started, in milliseconds
   int64_t pktSentTotal;                // total number of sent data packets, including retransmissions
   int64_t pktRecvTotal;                // total number of received packets
   int pktSndLossTotal;                 // total number of lost packets (sender side)
   int pktRcvLossTotal;                 // total number of lost packets (receiver side)
   int pktRetransTotal;                 // total number of retransmitted packets
   int pktSentACKTotal;                 // total number of sent ACK packets
   int pktRecvACKTotal;                 // total number of received ACK packets
   int pktSentNAKTotal;                 // total number of sent NAK packets
   int pktRecvNAKTotal;                 // total number of received NAK packets
   int64_t usSndDurationTotal;		// total time duration when UDT is sending data (idle time exclusive)
   //>new
   int pktSndDropTotal;                 // number of too-late-to-send dropped packets
   int pktRcvDropTotal;                 // number of too-late-to play missing packets
   int pktRcvUndecryptTotal;            // number of undecrypted packets
   uint64_t byteSentTotal;              // total number of sent data bytes, including retransmissions
   uint64_t byteRecvTotal;              // total number of received bytes
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
   uint64_t byteRcvLossTotal;           // total number of lost bytes
#endif
   uint64_t byteRetransTotal;           // total number of retransmitted bytes
   uint64_t byteSndDropTotal;           // number of too-late-to-send dropped bytes
   uint64_t byteRcvDropTotal;           // number of too-late-to play missing bytes (estimate based on average packet size)
   uint64_t byteRcvUndecryptTotal;      // number of undecrypted bytes
   //<

   // local measurements
   int64_t pktSent;                     // number of sent data packets, including retransmissions
   int64_t pktRecv;                     // number of received packets
   int pktSndLoss;                      // number of lost packets (sender side)
   int pktRcvLoss;                      // number of lost packets (receiver side)
   int pktRetrans;                      // number of retransmitted packets
   int pktRcvRetrans;                   // number of retransmitted packets received
   int pktSentACK;                      // number of sent ACK packets
   int pktRecvACK;                      // number of received ACK packets
   int pktSentNAK;                      // number of sent NAK packets
   int pktRecvNAK;                      // number of received NAK packets
   double mbpsSendRate;                 // sending rate in Mb/s
   double mbpsRecvRate;                 // receiving rate in Mb/s
   int64_t usSndDuration;		// busy sending time (i.e., idle time exclusive)
   int pktReorderDistance;              // size of order discrepancy in received sequences
   double pktRcvAvgBelatedTime;             // average time of packet delay for belated packets (packets with sequence past the ACK)
   int64_t pktRcvBelated;              // number of received AND IGNORED packets due to having come too late
   //>new
   int pktSndDrop;                      // number of too-late-to-send dropped packets
   int pktRcvDrop;                      // number of too-late-to play missing packets
   int pktRcvUndecrypt;                 // number of undecrypted packets
   uint64_t byteSent;                   // number of sent data bytes, including retransmissions
   uint64_t byteRecv;                   // number of received bytes
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
   uint64_t byteRcvLoss;                // number of retransmitted bytes
#endif
   uint64_t byteRetrans;                // number of retransmitted bytes
   uint64_t byteSndDrop;                // number of too-late-to-send dropped bytes
   uint64_t byteRcvDrop;                // number of too-late-to play missing bytes (estimate based on average packet size)
   uint64_t byteRcvUndecrypt;           // number of undecrypted bytes
   //<

   // instant measurements
   double usPktSndPeriod;               // packet sending period, in microseconds
   int pktFlowWindow;                   // flow window size, in number of packets
   int pktCongestionWindow;             // congestion window size, in number of packets
   int pktFlightSize;                   // number of packets on flight
   double msRTT;                        // RTT, in milliseconds
   double mbpsBandwidth;                // estimated bandwidth, in Mb/s
   int byteAvailSndBuf;                 // available UDT sender buffer size
   int byteAvailRcvBuf;                 // available UDT receiver buffer size
   //>new
   double  mbpsMaxBW;                   // Transmit Bandwidth ceiling (Mbps)
   int     byteMSS;                     // MTU

   int     pktSndBuf;                   // UnACKed packets in UDT sender
   int     byteSndBuf;                  // UnACKed bytes in UDT sender
   int     msSndBuf;                    // UnACKed timespan (msec) of UDT sender
   int     msSndTsbPdDelay;             // Timestamp-based Packet Delivery Delay

   int     pktRcvBuf;                   // Undelivered packets in UDT receiver
   int     byteRcvBuf;                  // Undelivered bytes of UDT receiver
   int     msRcvBuf;                    // Undelivered timespan (msec) of UDT receiver
   int     msRcvTsbPdDelay;             // Timestamp-based Packet Delivery Delay
   //<
};
#endif /* SRT_ENABLE_BSTATS */

////////////////////////////////////////////////////////////////////////////////

// Error codes - define outside the CUDTException class
// because otherwise you'd have to use CUDTException::MJ_SUCCESS etc.
// in all throw CUDTException expressions.
enum CodeMajor
{
    MJ_UNKNOWN = -1,
    MJ_SUCCESS = 0,
    MJ_SETUP = 1,
    MJ_CONNECTION = 2,
    MJ_SYSTEMRES = 3,
    MJ_FILESYSTEM = 4,
    MJ_NOTSUP = 5,
    MJ_AGAIN = 6,
    MJ_PEERERROR = 7
};

enum CodeMinor
{
    // These are "minor" error codes from various "major" categories
    // MJ_SETUP
    MN_NONE = 0,
    MN_TIMEOUT = 1,
    MN_REJECTED = 2,
    MN_NORES = 3,
    MN_SECURITY = 4,
    // MJ_CONNECTION
    MN_CONNLOST = 1,
    MN_NOCONN = 2,
    // MJ_SYSTEMRES
    MN_THREAD = 1,
    MN_MEMORY = 2,
    // MJ_FILESYSTEM
    MN_SEEKGFAIL = 1,
    MN_READFAIL = 2,
    MN_SEEKPFAIL = 3,
    MN_WRITEFAIL = 4,
    // MJ_NOTSUP
    MN_ISBOUND = 1,
    MN_ISCONNECTED = 2,
    MN_INVAL = 3,
    MN_SIDINVAL = 4,
    MN_ISUNBOUND = 5,
    MN_NOLISTEN = 6,
    MN_ISRENDEZVOUS = 7,
    MN_ISRENDUNBOUND = 8,
    MN_ISSTREAM = 9,
    MN_ISDGRAM = 10,
    MN_BUSY = 11,
    MN_XSIZE = 12,
    MN_EIDINVAL = 13,
    // MJ_AGAIN
    MN_WRAVAIL = 1,
    MN_RDAVAIL = 2,
    MN_XMTIMEOUT = 3,
    MN_CONGESTION = 4
};

#ifdef __cplusplus

// Class CUDTException exposed for C++ API.
// This is actually useless, unless you'd use a DIRECT C++ API,
// however there's no such API so far. The current C++ API for UDT/SRT
// is predicted to NEVER LET ANY EXCEPTION out of implementation,
// so it's useless to catch this exception anyway.

class UDT_API CUDTException
{
public:

   CUDTException(CodeMajor major = MJ_SUCCESS, CodeMinor minor = MN_NONE, int err = -1);
   CUDTException(const CUDTException& e);

   ~CUDTException();

      /// Get the description of the exception.
      /// @return Text message for the exception description.

   const char* getErrorMessage();

      /// Get the system errno for the exception.
      /// @return errno.

   int getErrorCode() const;

#ifdef HAI_PATCH
      /// Get the system network errno for the exception.
      /// @return errno.

   int getErrno() const;
#endif /* HAI_PATCH */
      /// Clear the error code.

   void clear();

private:
   CodeMajor m_iMajor;        // major exception categories
   CodeMinor m_iMinor;		// for specific error reasons
   int m_iErrno;		// errno returned by the system if there is any
   std::string m_strMsg;	// text error message

   std::string m_strAPI;	// the name of UDT function that returns the error
   std::string m_strDebug;	// debug information, set to the original place that causes the error

public: // Error Code
   static const int SUCCESS;
   static const int ECONNSETUP;
   static const int ENOSERVER;
   static const int ECONNREJ;
   static const int ESOCKFAIL;
   static const int ESECFAIL;
   static const int ECONNFAIL;
   static const int ECONNLOST;
   static const int ENOCONN;
   static const int ERESOURCE;
   static const int ETHREAD;
   static const int ENOBUF;
   static const int EFILE;
   static const int EINVRDOFF;
   static const int ERDPERM;
   static const int EINVWROFF;
   static const int EWRPERM;
   static const int EINVOP;
   static const int EBOUNDSOCK;
   static const int ECONNSOCK;
   static const int EINVPARAM;
   static const int EINVSOCK;
   static const int EUNBOUNDSOCK;
   static const int ENOLISTEN;
   static const int ERDVNOSERV;
   static const int ERDVUNBOUND;
   static const int ESTREAMILL;
   static const int EDGRAMILL;
   static const int EDUPLISTEN;
   static const int ELARGEMSG;
   static const int EINVPOLLID;
   static const int EASYNCFAIL;
   static const int EASYNCSND;
   static const int EASYNCRCV;
   static const int ETIMEOUT;
#ifdef SRT_ENABLE_ECN
   static const int ECONGEST;
#endif /* SRT_ENABLE_ECN */
   static const int EPEERERR;
   static const int EUNKNOWN;
};

#endif // C++, exception class

// Stupid, but effective. This will be #undefined, so don't worry.
#define MJ(major) (1000*MJ_##major)
#define MN(major, minor) (1000*MJ_##major + MN_##minor)

// Some better way to define it, and better for C language.
enum UDT_ERRNO
{
    UDT_EUNKNOWN = -1,
    UDT_SUCCESS = MJ_SUCCESS,

    UDT_ECONNSETUP = MJ(SETUP),
    UDT_ENOSERVER  = MN(SETUP, TIMEOUT),
    UDT_ECONNREJ   = MN(SETUP, REJECTED),
    UDT_ESOCKFAIL  = MN(SETUP, NORES),
    UDT_ESECFAIL   = MN(SETUP, SECURITY),

    UDT_ECONNFAIL =  MJ(CONNECTION),
    UDT_ECONNLOST =  MN(CONNECTION, CONNLOST),
    UDT_ENOCONN =    MN(CONNECTION, NOCONN),

    UDT_ERESOURCE =  MJ(SYSTEMRES),
    UDT_ETHREAD =    MN(SYSTEMRES, THREAD),
    UDT_ENOBUF =     MN(SYSTEMRES, MEMORY),

    UDT_EFILE =      MJ(FILESYSTEM),
    UDT_EINVRDOFF =  MN(FILESYSTEM, SEEKGFAIL),
    UDT_ERDPERM =    MN(FILESYSTEM, READFAIL),
    UDT_EINVWROFF =  MN(FILESYSTEM, SEEKPFAIL),
    UDT_EWRPERM =    MN(FILESYSTEM, WRITEFAIL),

    UDT_EINVOP =       MJ(NOTSUP),
    UDT_EBOUNDSOCK =   MN(NOTSUP, ISBOUND),
    UDT_ECONNSOCK =    MN(NOTSUP, ISCONNECTED),
    UDT_EINVPARAM =    MN(NOTSUP, INVAL),
    UDT_EINVSOCK =     MN(NOTSUP, SIDINVAL),
    UDT_EUNBOUNDSOCK = MN(NOTSUP, ISUNBOUND),
    UDT_ENOLISTEN =    MN(NOTSUP, NOLISTEN),
    UDT_ERDVNOSERV =   MN(NOTSUP, ISRENDEZVOUS),
    UDT_ERDVUNBOUND =  MN(NOTSUP, ISRENDUNBOUND),
    UDT_ESTREAMILL =   MN(NOTSUP, ISSTREAM),
    UDT_EDGRAMILL =    MN(NOTSUP, ISDGRAM),
    UDT_EDUPLISTEN =   MN(NOTSUP, BUSY),
    UDT_ELARGEMSG =    MN(NOTSUP, XSIZE),
    UDT_EINVPOLLID =   MN(NOTSUP, EIDINVAL),

    UDT_EASYNCFAIL = MJ(AGAIN),
    UDT_EASYNCSND =  MN(AGAIN, WRAVAIL),
    UDT_EASYNCRCV =  MN(AGAIN, RDAVAIL),
    UDT_ETIMEOUT =   MN(AGAIN, XMTIMEOUT),
    UDT_ECONGEST =   MN(AGAIN, CONGESTION),

    UDT_EPEERERR = MJ(PEERERROR)
};

#undef MJ
#undef MN

// Logging API - specialization for SRT.

// Define logging functional areas for log selection.
// Use values greater than 0. Value 0 is reserved for LOGFA_GENERAL,
// which is considered always enabled.

// Logger Functional Areas
// Note that 0 is "general".

// Made by #define so that it's available also for C API.
#define SRT_LOGFA_GENERAL 0
#define SRT_LOGFA_BSTATS 1
#define SRT_LOGFA_CONTROL 2
#define SRT_LOGFA_DATA 3
#define SRT_LOGFA_TSBPD 4
#define SRT_LOGFA_REXMIT 5

#define SRT_LOGFA_LASTNONE 99

// Rest of the file is C++ only. It defines functions to be
// called only from C++. For C equivalents, see udtc.h file.

#ifdef __cplusplus


////////////////////////////////////////////////////////////////////////////////

// If you need to export these APIs to be used by a different language,
// declare extern "C" for them, and add a "udt_" prefix to each API.
// The following APIs: sendfile(), recvfile(), epoll_wait(), geterrormsg(),
// include C++ specific feature, please use the corresponding sendfile2(), etc.

namespace UDT
{

typedef CUDTException ERRORINFO;
typedef UDT_SOCKOPT SOCKOPT;
typedef CPerfMon TRACEINFO;
#ifdef SRT_ENABLE_BSTATS
typedef CBytePerfMon TRACEBSTATS;
#endif
typedef ud_set UDSET;

UDT_API extern const UDTSOCKET INVALID_SOCK;
#undef ERROR
UDT_API extern const int ERROR;

UDT_API int startup();
UDT_API int cleanup();
UDT_API UDTSOCKET socket(int af, int type, int protocol);
UDT_API int bind(UDTSOCKET u, const struct sockaddr* name, int namelen);
UDT_API int bind2(UDTSOCKET u, UDPSOCKET udpsock);
UDT_API int listen(UDTSOCKET u, int backlog);
UDT_API UDTSOCKET accept(UDTSOCKET u, struct sockaddr* addr, int* addrlen);
UDT_API int connect(UDTSOCKET u, const struct sockaddr* name, int namelen);
UDT_API int close(UDTSOCKET u);
UDT_API int getpeername(UDTSOCKET u, struct sockaddr* name, int* namelen);
UDT_API int getsockname(UDTSOCKET u, struct sockaddr* name, int* namelen);
UDT_API int getsockopt(UDTSOCKET u, int level, SOCKOPT optname, void* optval, int* optlen);
UDT_API int setsockopt(UDTSOCKET u, int level, SOCKOPT optname, const void* optval, int optlen);
UDT_API int send(UDTSOCKET u, const char* buf, int len, int flags);
UDT_API int recv(UDTSOCKET u, char* buf, int len, int flags);

// If SRT_ENABLE_SRCTIMESTAMP is NOT enabled, 'srctime' argument is ignored.
UDT_API int sendmsg(UDTSOCKET u, const char* buf, int len, int ttl = -1, bool inorder = false, uint64_t srctime = 0LL);
#ifdef SRT_ENABLE_SRCTIMESTAMP
// For non-SRCTIMESTAMP, this version is not available for the application
UDT_API int recvmsg(UDTSOCKET u, char* buf, int len, uint64_t& srctime);
#endif
// For SRCTIMESTAMP this version is still available, it just ignores the received timestamp.
UDT_API int recvmsg(UDTSOCKET u, char* buf, int len);

UDT_API int64_t sendfile(UDTSOCKET u, std::fstream& ifs, int64_t& offset, int64_t size, int block = 364000);
UDT_API int64_t recvfile(UDTSOCKET u, std::fstream& ofs, int64_t& offset, int64_t size, int block = 7280000);
UDT_API int64_t sendfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block = 364000);
UDT_API int64_t recvfile2(UDTSOCKET u, const char* path, int64_t* offset, int64_t size, int block = 7280000);

#ifdef SRT_ENABLE_SRTCC_API
UDT_API int setsrtcc(UDTSOCKET u);
UDT_API int setsrtcc_maxbitrate(UDTSOCKET u, int maxbitrate);
UDT_API int setsrtcc_windowsize(UDTSOCKET u, int windowsize);
#endif /* SRT_ENABLE_SRTCC_API */

// select and selectEX are DEPRECATED; please use epoll. 
UDT_API int select(int nfds, UDSET* readfds, UDSET* writefds, UDSET* exceptfds, const struct timeval* timeout);
UDT_API int selectEx(const std::vector<UDTSOCKET>& fds, std::vector<UDTSOCKET>* readfds,
                     std::vector<UDTSOCKET>* writefds, std::vector<UDTSOCKET>* exceptfds, int64_t msTimeOut);

UDT_API int epoll_create();
UDT_API int epoll_add_usock(int eid, UDTSOCKET u, const int* events = NULL);
UDT_API int epoll_add_ssock(int eid, SYSSOCKET s, const int* events = NULL);
UDT_API int epoll_remove_usock(int eid, UDTSOCKET u);
UDT_API int epoll_remove_ssock(int eid, SYSSOCKET s);
#ifdef HAI_PATCH
UDT_API int epoll_update_usock(int eid, UDTSOCKET u, const int* events = NULL);
UDT_API int epoll_update_ssock(int eid, SYSSOCKET s, const int* events = NULL);
#endif /* HAI_PATCH */
UDT_API int epoll_wait(int eid, std::set<UDTSOCKET>* readfds, std::set<UDTSOCKET>* writefds, int64_t msTimeOut,
                       std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
UDT_API int epoll_wait2(int eid, UDTSOCKET* readfds, int* rnum, UDTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                        SYSSOCKET* lrfds = NULL, int* lrnum = NULL, SYSSOCKET* lwfds = NULL, int* lwnum = NULL);
UDT_API int epoll_release(int eid);
UDT_API ERRORINFO& getlasterror();
UDT_API int getlasterror_code();
UDT_API const char* getlasterror_desc();
UDT_API int perfmon(UDTSOCKET u, TRACEINFO* perf, bool clear = true);
#ifdef SRT_ENABLE_BSTATS
UDT_API int bstats(UDTSOCKET u, TRACEBSTATS* perf, bool clear = true);
#endif
UDT_API UDTSTATUS getsockstate(UDTSOCKET u);

UDT_API void setloglevel(logging::LogLevel::type ll);
UDT_API void addlogfa(logging::LogFA fa);
UDT_API void dellogfa(logging::LogFA fa);
UDT_API void resetlogfa(std::set<logging::LogFA> fas);
UDT_API void setlogstream(std::ostream& stream);
UDT_API void setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
UDT_API void setlogflags(int flags);

}  // namespace UDT

#endif /* __cplusplus */

#endif
