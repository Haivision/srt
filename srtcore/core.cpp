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
   Yunhong Gu, last updated 02/28/2012
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef WIN32
   #include <unistd.h>
   #include <netdb.h>
   #include <arpa/inet.h>
   #include <cerrno>
   #include <cstring>
   #include <cstdlib>
#else
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif
#include <cmath>
#include <sstream>
#include "srt.h"
#include "queue.h"
#include "core.h"
#include "logging.h"
#include "crypto.h"
#include "../common/logsupport.hpp" // Required due to containing extern srt_logger_config

// Again, just in case when some "smart guy" provided such a global macro
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

using namespace std;

struct AllFaOn
{
    set<int> allfa;

    AllFaOn()
    {
        allfa.insert(SRT_LOGFA_BSTATS);
        allfa.insert(SRT_LOGFA_CONTROL);
        allfa.insert(SRT_LOGFA_DATA);
        allfa.insert(SRT_LOGFA_TSBPD);
        allfa.insert(SRT_LOGFA_REXMIT);
    }
} logger_fa_all;

logging::LogConfig srt_logger_config (logger_fa_all.allfa);

logging::Logger glog(SRT_LOGFA_GENERAL, &srt_logger_config, "SRT.g");
logging::Logger blog(SRT_LOGFA_BSTATS, &srt_logger_config, "SRT.b");
logging::Logger mglog(SRT_LOGFA_CONTROL, &srt_logger_config, "SRT.c");
logging::Logger dlog(SRT_LOGFA_DATA, &srt_logger_config, "SRT.d");
logging::Logger tslog(SRT_LOGFA_TSBPD, &srt_logger_config, "SRT.t");
logging::Logger rxlog(SRT_LOGFA_REXMIT, &srt_logger_config, "SRT.r");

CUDTUnited CUDT::s_UDTUnited;

const UDTSOCKET CUDT::INVALID_SOCK = -1;
const int CUDT::ERROR = -1;

const UDTSOCKET UDT::INVALID_SOCK = CUDT::INVALID_SOCK;
const int UDT::ERROR = CUDT::ERROR;

// SRT Version constants
#define SRT_VERSION_UNK     0
#define SRT_VERSION_MAJ1    0x010000            /* Version 1 major */


#define SRT_VERSION_MAJ(v) (0xFF0000 & (v))     /* Major number ensuring backward compatibility */
#define SRT_VERSION_MIN(v) (0x00FF00 & (v))
#define SRT_VERSION_PCH(v) (0x0000FF & (v))

// NOTE: HAISRT_VERSION is primarily defined in the build file.
const int32_t SRT_DEF_VERSION = SrtParseVersion(SRT_VERSION);


//#define SRT_CMD_HSREQ       1           /* SRT Handshake Request (sender) */
#define SRT_CMD_HSREQ_MINSZ 8           /* Minumum Compatible (1.x.x) packet size (bytes) */
#define SRT_CMD_HSREQ_SZ    12          /* Current version packet size */
#if     SRT_CMD_HSREQ_SZ > SRT_CMD_MAXSZ
#error  SRT_CMD_MAXSZ too small
#endif
/*      Handshake Request (Network Order)
        0[31..0]:   SRT version     SRT_DEF_VERSION
        1[31..0]:   Options         0 [ | SRT_OPT_TSBPDSND ][ | SRT_OPT_HAICRYPT ]
        2[31..16]:  TsbPD resv      0
        2[15..0]:   TsbPD delay     [0..60000] msec
*/

//#define SRT_CMD_HSRSP       2           /* SRT Handshake Response (receiver) */
#define SRT_CMD_HSRSP_MINSZ 8           /* Minumum Compatible (1.x.x) packet size (bytes) */
#define SRT_CMD_HSRSP_SZ    12          /* Current version packet size */
#if     SRT_CMD_HSRSP_SZ > SRT_CMD_MAXSZ
#error  SRT_CMD_MAXSZ too small
#endif
/*      Handshake Response (Network Order)
        0[31..0]:   SRT version     SRT_DEF_VERSION
        1[31..0]:   Options         0 [ | SRT_OPT_TSBPDRCV [| SRT_OPT_TLPKTDROP ]][ | SRT_OPT_HAICRYPT]
                                      [ | SRT_OPT_NAKREPORT ] [ | SRT_OPT_REXMITFLG ]
        2[31..16]:  TsbPD resv      0
        2[15..0]:   TsbPD delay     [0..60000] msec
*/


void CUDT::construct()
{
    m_pSndBuffer = NULL;
    m_pRcvBuffer = NULL;
    m_pSndLossList = NULL;
    m_pRcvLossList = NULL;
#if SRT_BELATED_LOSSREPORT
    m_iReorderTolerance = 0;
    m_iMaxReorderTolerance = 0; // Sensible optimal value is 10, 0 preserves old behavior
    m_iConsecEarlyDelivery = 0; // how many times so far the packet considered lost has been received before TTL expires
    m_iConsecOrderedDelivery = 0;
#endif

    m_pSndQueue = NULL;
    m_pRcvQueue = NULL;
    m_pPeerAddr = NULL;
    m_pSNode = NULL;
    m_pRNode = NULL;

    // Congestion Control fields
    m_dCWndSize = 1000;
    m_dMaxCWndSize = 0;
    m_iRcvRate = 0;
    m_iACKPeriod = 0;
    m_iACKInterval = 0;
    m_bUserDefinedRTO = false;
    m_iRTO = -1;
    m_llSndMaxBW = 30000000/8;    // 30Mbps in Bytes/sec
    m_iSndAvgPayloadSize = 7*188; // = 1316 -- shouldn't be configurable?

    m_SndHsLastTime = 0;
    m_SndHsRetryCnt = SRT_MAX_HSRETRY+1; // Will be reset to 0 for HSv5, this value is important for HSv4

    updatePktSndPeriod();

    // Initilize mutex and condition variables
    initSynch();
}

CUDT::CUDT()
{
   construct();

   (void)SRT_DEF_VERSION;

   // Default UDT configurations
   m_iMSS = 1500;
   m_bSynSending = true;
   m_bSynRecving = true;
   m_iFlightFlagSize = 25600;
   m_iSndBufSize = 8192;
   m_iRcvBufSize = 8192; //Rcv buffer MUST NOT be bigger than Flight Flag size
   m_Linger.l_onoff = 1;
   m_Linger.l_linger = 180;
   m_iUDPSndBufSize = 65536;
   m_iUDPRcvBufSize = m_iRcvBufSize * m_iMSS;
   m_iSockType = UDT_STREAM;
   m_iIPversion = AF_INET;
   m_bRendezvous = false;
#ifdef SRT_ENABLE_CONNTIMEO
   m_iConnTimeOut = 3000;
#endif
   m_iSndTimeOut = -1;
   m_iRcvTimeOut = -1;
   m_bReuseAddr = true;
   m_llMaxBW = -1;
#ifdef SRT_ENABLE_IPOPTS
   m_iIpTTL = -1;
   m_iIpToS = -1;
#endif
   m_CryptoSecret.len = 0;
   m_iSndCryptoKeyLen = 0;
   //Cfg
   m_bDataSender = false;       //Sender only if true: does not recv data
   m_bTwoWayData = false;
   m_bOPT_TsbPd = true;        //Enable TsbPd on sender
   m_iOPT_TsbPdDelay = 120;          //Receiver TsbPd delay (mSec)
   m_iOPT_PeerTsbPdDelay = 0;       //Peer's TsbPd delay as receiver (here is its minimum value, if used)
#ifdef SRT_ENABLE_TLPKTDROP
   m_bTLPktDrop = true;         //Too-late Packet Drop
#endif /* SRT_ENABLE_TLPKTDROP */
   //Runtime
   m_bPeerTsbPd = false;
   m_iPeerTsbPdDelay = 0;
   m_bTsbPd = false;
   m_iTsbPdDelay = 0;
   m_iPeerTsbPdDelay = 0;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bPeerTLPktDrop = false;
#endif /* SRT_ENABLE_TLPKTDROP */
#ifdef SRT_ENABLE_NAKREPORT
   m_bRcvNakReport = true;      //Receiver's Periodic NAK Reports
   m_iMinNakInterval = 20000;   //Minimum NAK Report Period (usec)
   m_iNakReportAccel = 2;       //Default NAK Report Period (RTT) accelerator
#endif /* SRT_ENABLE_NAKREPORT */
   m_llInputBW = 0;             // Application provided input bandwidth (internal input rate sampling == 0)
   m_iOverheadBW = 25;          // Percent above input stream rate (applies if m_llMaxBW == 0)
   m_bTwoWayData = false;

   m_pCache = NULL;

   // Initial status
   m_bOpened = false;
   m_bListening = false;
   m_bConnecting = false;
   m_bConnected = false;
   m_bClosing = false;
   m_bShutdown = false;
   m_bBroken = false;
   m_bPeerHealth = true;
   m_ullLingerExpiration = 0;

   m_lSrtVersion = SRT_DEF_VERSION;
   m_lPeerSrtVersion = 0; // not defined until connected.
   m_lMinimumPeerSrtVersion = SRT_VERSION_MAJ1;
}

CUDT::CUDT(const CUDT& ancestor)
{
   construct();

   // Default UDT configurations
   m_iMSS = ancestor.m_iMSS;
   m_bSynSending = ancestor.m_bSynSending;
   m_bSynRecving = ancestor.m_bSynRecving;
   m_iFlightFlagSize = ancestor.m_iFlightFlagSize;
   m_iSndBufSize = ancestor.m_iSndBufSize;
   m_iRcvBufSize = ancestor.m_iRcvBufSize;
   m_Linger = ancestor.m_Linger;
   m_iUDPSndBufSize = ancestor.m_iUDPSndBufSize;
   m_iUDPRcvBufSize = ancestor.m_iUDPRcvBufSize;
   m_iSockType = ancestor.m_iSockType;
   m_iIPversion = ancestor.m_iIPversion;
   m_bRendezvous = ancestor.m_bRendezvous;
#ifdef SRT_ENABLE_CONNTIMEO
   m_iConnTimeOut = ancestor.m_iConnTimeOut;
#endif
   m_iSndTimeOut = ancestor.m_iSndTimeOut;
   m_iRcvTimeOut = ancestor.m_iRcvTimeOut;
   m_bReuseAddr = true;	// this must be true, because all accepted sockets shared the same port with the listener
   m_llMaxBW = ancestor.m_llMaxBW;
#ifdef SRT_ENABLE_IPOPTS
   m_iIpTTL = ancestor.m_iIpTTL;
   m_iIpToS = ancestor.m_iIpToS;
#endif
   m_llInputBW = ancestor.m_llInputBW;
   m_iOverheadBW = ancestor.m_iOverheadBW;
   m_bDataSender = ancestor.m_bDataSender;
   m_bTwoWayData = ancestor.m_bTwoWayData;
   m_bOPT_TsbPd = ancestor.m_bOPT_TsbPd;
   m_iOPT_TsbPdDelay = ancestor.m_iOPT_TsbPdDelay;
   m_iOPT_PeerTsbPdDelay = ancestor.m_iOPT_PeerTsbPdDelay;
   m_iTsbPdDelay = 0;
   m_iPeerTsbPdDelay = 0;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bTLPktDrop = ancestor.m_bTLPktDrop;
#endif /* SRT_ENABLE_TLPKTDROP */
   //Runtime
   m_bPeerTsbPd = false;
   m_iPeerTsbPdDelay = 0;
   m_bTsbPd = false;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bPeerTLPktDrop = false;
#endif /* SRT_ENABLE_TLPKTDROP */
#ifdef SRT_ENABLE_NAKREPORT
   m_bRcvNakReport = ancestor.m_bRcvNakReport;
   m_iMinNakInterval = ancestor.m_iMinNakInterval;
   m_iNakReportAccel = ancestor.m_iNakReportAccel;
#endif /* SRT_ENABLE_NAKREPORT */

   m_CryptoSecret = ancestor.m_CryptoSecret;
   m_iSndCryptoKeyLen = ancestor.m_iSndCryptoKeyLen;

   m_pCache = ancestor.m_pCache;

   // Initial status
   m_bOpened = false;
   m_bListening = false;
   m_bConnecting = false;
   m_bConnected = false;
   m_bClosing = false;
   m_bShutdown = false;
   m_bBroken = false;
   m_bPeerHealth = true;
   m_ullLingerExpiration = 0;

   m_lSrtVersion = SRT_DEF_VERSION;
   m_lPeerSrtVersion = 0; // not defined until connected.
   m_lMinimumPeerSrtVersion = SRT_VERSION_MAJ1;
}

CUDT::~CUDT()
{
   // release mutex/condtion variables
   destroySynch();

   //Wipeout critical data
   memset(&m_CryptoSecret, 0, sizeof(m_CryptoSecret));

   // destroy the data structures
   delete m_pSndBuffer;
   delete m_pRcvBuffer;
   delete m_pSndLossList;
   delete m_pRcvLossList;
   delete m_pPeerAddr;
   delete m_pSNode;
   delete m_pRNode;
}

// This function is to make it possible for both C and C++
// API to accept both bool and int types for boolean options.
// (it's not that C couldn't use <stdbool.h>, it's that people
// often forget to use correct type).
static bool bool_int_value(const void* optval, int optlen)
{
    if ( optlen == sizeof(bool) )
    {
        return *(bool*)optval;
    }

    if ( optlen == sizeof(int) )
    {
        return 0!=  *(int*)optval; // 0!= is a windows warning-killer int-to-bool conversion
    }
    return false;
}

void CUDT::setOpt(UDT_SOCKOPT optName, const void* optval, int optlen)
{
    if (m_bBroken || m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    CGuard cg(m_ConnectionLock);
    CGuard sendguard(m_SendLock);
    CGuard recvguard(m_RecvLock);

    switch (optName)
    {
    case UDT_MSS:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int*)optval < int(CPacket::UDP_HDR_SIZE + CHandShake::m_iContentSize))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_iMSS = *(int*)optval;

        // Packet size cannot be greater than UDP buffer size
        if (m_iMSS > m_iUDPSndBufSize)
            m_iMSS = m_iUDPSndBufSize;
        if (m_iMSS > m_iUDPRcvBufSize)
            m_iMSS = m_iUDPRcvBufSize;

        break;

    case UDT_SNDSYN:
        m_bSynSending = bool_int_value(optval, optlen);
        break;

    case UDT_RCVSYN:
        m_bSynRecving = bool_int_value(optval, optlen);
        break;

    case UDT_FC:
        if (m_bConnecting || m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (*(int*)optval < 1)
            throw CUDTException(MJ_NOTSUP, MN_INVAL);

        // Mimimum recv flight flag size is 32 packets
        if (*(int*)optval > 32)
            m_iFlightFlagSize = *(int*)optval;
        else
            m_iFlightFlagSize = 32;

        break;

    case UDT_SNDBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int*)optval <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_iSndBufSize = *(int*)optval / (m_iMSS - CPacket::UDP_HDR_SIZE);

        break;

    case UDT_RCVBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        if (*(int*)optval <= 0)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        {
            // This weird cast through int is required because
            // API requires 'int', and internals require 'size_t';
            // their size is different on 64-bit systems.
            size_t val = size_t(*(int*)optval);

            // Mimimum recv buffer size is 32 packets
            size_t mssin_size = m_iMSS - CPacket::UDP_HDR_SIZE;

            // XXX This magic 32 deserves some constant
            if (val > mssin_size * 32)
                m_iRcvBufSize = val / mssin_size;
            else
                m_iRcvBufSize = 32;

            // recv buffer MUST not be greater than FC size
            if (m_iRcvBufSize > m_iFlightFlagSize)
                m_iRcvBufSize = m_iFlightFlagSize;
        }

        break;

    case UDT_LINGER:
        m_Linger = *(linger*)optval;
        break;

    case UDP_SNDBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        m_iUDPSndBufSize = *(int*)optval;

        if (m_iUDPSndBufSize < m_iMSS)
            m_iUDPSndBufSize = m_iMSS;

        break;

    case UDP_RCVBUF:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);

        m_iUDPRcvBufSize = *(int*)optval;

        if (m_iUDPRcvBufSize < m_iMSS)
            m_iUDPRcvBufSize = m_iMSS;

        break;

    case UDT_RENDEZVOUS:
        if (m_bConnecting || m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_bRendezvous = bool_int_value(optval, optlen);
        break;

    case UDT_SNDTIMEO:
        m_iSndTimeOut = *(int*)optval;
        break;

    case UDT_RCVTIMEO:
        m_iRcvTimeOut = *(int*)optval;
        break;

    case UDT_REUSEADDR:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_bReuseAddr = bool_int_value(optval, optlen);
        break;

    case UDT_MAXBW:
        m_llMaxBW = *(int64_t*)optval;
        // XXX
        // Note that this below code is effective only
        // when done on an already connected socket. Otherwise
        // the given attached objects don't exist.
        if (m_llMaxBW != 0)
        {  //Absolute MaxBW setting
            setMaxBW(m_llMaxBW); //Bytes/sec
            if (m_pSndBuffer)
            {
                m_pSndBuffer->setInputRateSmpPeriod(0);
            }
        }
        else if (m_llInputBW != 0)
        {  //Application provided input rate  
            setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
            if (m_pSndBuffer)
            {
                m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
            }
        }
        else
        {  //Internal input rate sampling
            if (m_pSndBuffer)
                m_pSndBuffer->setInputRateSmpPeriod(500000);
        }
        break;

#ifdef SRT_ENABLE_IPOPTS
    case SRT_IPTTL:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        if (!(*(int*)optval == -1)
                &&  !((*(int*)optval >= 1) && (*(int*)optval <= 255)))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        m_iIpTTL = *(int*)optval;
        break;

    case SRT_IPTOS:
        if (m_bOpened)
            throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
        m_iIpToS = *(int*)optval;
        break;
#endif

    case SRT_INPUTBW:
        m_llInputBW = *(int64_t*)optval;
        if (m_llMaxBW != 0)
        {  //Keep MaxBW setting
            ;
        }
        else if (m_llInputBW != 0)
        {  //Application provided input rate
            setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
            if (m_pSndBuffer)
            {
                m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
            }
        }
        else
        {  //Internal input rate sampling
            if (m_pSndBuffer)
                m_pSndBuffer->setInputRateSmpPeriod(500000); //Enable input rate sampling
        }
        break;

    case SRT_OHEADBW:
        if ((*(int*)optval < 5)
                ||  (*(int*)optval > 100))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        m_iOverheadBW = *(int*)optval;
        if (m_llMaxBW != 0)
        {  //Keep MaxBW setting
            ;
        }
        else if (m_llInputBW != 0)
        {  //Adjust MaxBW for new overhead
            setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
        }
        //else 
        // Keep input rate sampling setting, next CCupdate will adjust MaxBW
        break;

    case SRT_SENDER:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bDataSender = bool_int_value(optval, optlen);
        break;

    case SRT_TSBPDMODE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bOPT_TsbPd = bool_int_value(optval, optlen);
        break;

    case SRT_TSBPDDELAY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_TsbPdDelay = *(int*)optval;
        m_iOPT_PeerTsbPdDelay = *(int*)optval;
        break;

    case SRT_RCVLATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_TsbPdDelay = *(int*)optval;
        break;

    case SRT_PEERLATENCY:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_iOPT_PeerTsbPdDelay = *(int*)optval;
        break;

#ifdef SRT_ENABLE_TLPKTDROP
    case SRT_TSBPDMAXLAG:
        //Obsolete
        break;

    case SRT_TLPKTDROP:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bOPT_TLPktDrop = bool_int_value(optval, optlen);
        break;
#endif /* SRT_ENABLE_TLPKTDROP */

    case SRT_PASSPHRASE:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        // Password must be 10-80 characters.
        // Or it can be empty to clear the password.
        if ( (optlen != 0) && (optlen < 10 || optlen > HAICRYPT_SECRET_MAX_SZ) )
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        memset(&m_CryptoSecret, 0, sizeof(m_CryptoSecret));
        m_CryptoSecret.typ = HAICRYPT_SECTYP_PASSPHRASE;
        m_CryptoSecret.len = (optlen <= (int)sizeof(m_CryptoSecret.str) ? optlen : (int)sizeof(m_CryptoSecret.str));
        memcpy(m_CryptoSecret.str, optval, m_CryptoSecret.len);
        break;

    case SRT_PBKEYLEN:
    case SRT_SNDPBKEYLEN:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if ((*(int*)optval != 0)     //Encoder: No encryption, Decoder: get key from Keyint Material
                &&  (*(int*)optval != 16)
                &&  (*(int*)optval != 24)
                &&  (*(int*)optval != 32))
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_iSndCryptoKeyLen = *(int*)optval;
        break;

#ifdef SRT_ENABLE_NAKREPORT
    case SRT_RCVNAKREPORT:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_bRcvNakReport = bool_int_value(optval, optlen);
        break;
#endif /* SRT_ENABLE_NAKREPORT */

#ifdef SRT_ENABLE_CONNTIMEO
    case SRT_CONNTIMEO:
        m_iConnTimeOut = *(int*)optval;
        break;
#endif

#if SRT_BELATED_LOSSREPORT
    case SRT_LOSSMAXTTL:
        m_iMaxReorderTolerance = *(int*)optval;
        break;
#endif

    case SRT_AGENTVERSION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_lSrtVersion = *(uint32_t*)optval;
        break;

    case SRT_MINVERSION:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        m_lMinimumPeerSrtVersion = *(uint32_t*)optval;
        break;

    case SRT_STREAMID:
        if (m_bConnected)
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

        if (size_t(optlen) > MAX_SID_LENGTH)
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

        m_sStreamName.assign((const char*)optval, optlen);
        break;

    default:
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }
}

void CUDT::getOpt(UDT_SOCKOPT optName, void* optval, int& optlen)
{
   CGuard cg(m_ConnectionLock);

   switch (optName)
   {
   case UDT_MSS:
      *(int*)optval = m_iMSS;
      optlen = sizeof(int);
      break;

   case UDT_SNDSYN:
      *(bool*)optval = m_bSynSending;
      optlen = sizeof(bool);
      break;

   case UDT_RCVSYN:
      *(bool*)optval = m_bSynRecving;
      optlen = sizeof(bool);
      break;

   case UDT_FC:
      *(int*)optval = m_iFlightFlagSize;
      optlen = sizeof(int);
      break;

   case UDT_SNDBUF:
      *(int*)optval = m_iSndBufSize * (m_iMSS - CPacket::UDP_HDR_SIZE);
      optlen = sizeof(int);
      break;

   case UDT_RCVBUF:
      *(int*)optval = m_iRcvBufSize * (m_iMSS - CPacket::UDP_HDR_SIZE);
      optlen = sizeof(int);
      break;

   case UDT_LINGER:
      if (optlen < (int)(sizeof(linger)))
         throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

      *(linger*)optval = m_Linger;
      optlen = sizeof(linger);
      break;

   case UDP_SNDBUF:
      *(int*)optval = m_iUDPSndBufSize;
      optlen = sizeof(int);
      break;

   case UDP_RCVBUF:
      *(int*)optval = m_iUDPRcvBufSize;
      optlen = sizeof(int);
      break;

   case UDT_RENDEZVOUS:
      *(bool *)optval = m_bRendezvous;
      optlen = sizeof(bool);
      break;

   case UDT_SNDTIMEO:
      *(int*)optval = m_iSndTimeOut;
      optlen = sizeof(int);
      break;

   case UDT_RCVTIMEO:
      *(int*)optval = m_iRcvTimeOut;
      optlen = sizeof(int);
      break;

   case UDT_REUSEADDR:
      *(bool *)optval = m_bReuseAddr;
      optlen = sizeof(bool);
      break;

   case UDT_MAXBW:
      *(int64_t*)optval = m_llMaxBW;
      optlen = sizeof(int64_t);
      break;

   case UDT_STATE:
      *(int32_t*)optval = s_UDTUnited.getStatus(m_SocketID);
      optlen = sizeof(int32_t);
      break;

   case UDT_EVENT:
   {
      int32_t event = 0;
      if (m_bBroken)
         event |= UDT_EPOLL_ERR;
      else
      {
         CGuard::enterCS(m_RecvLock);
         if (m_pRcvBuffer && m_pRcvBuffer->isRcvDataReady())
            event |= UDT_EPOLL_IN;
         CGuard::leaveCS(m_RecvLock);
         if (m_pSndBuffer && (m_iSndBufSize > m_pSndBuffer->getCurrBufSize()))
            event |= UDT_EPOLL_OUT;
      }
      *(int32_t*)optval = event;
      optlen = sizeof(int32_t);
      break;
   }

   case UDT_SNDDATA:
      if (m_pSndBuffer)
         *(int32_t*)optval = m_pSndBuffer->getCurrBufSize();
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

   case UDT_RCVDATA:
      if (m_pRcvBuffer)
      {
         CGuard::enterCS(m_RecvLock);
         *(int32_t*)optval = m_pRcvBuffer->getRcvDataSize();
         CGuard::leaveCS(m_RecvLock);
      }
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

#ifdef SRT_ENABLE_IPOPTS
   case SRT_IPTTL:
      if (m_bOpened)
         *(int32_t*)optval = m_pSndQueue->getIpTTL();
      else
         *(int32_t*)optval = m_iIpTTL;
      break;

   case SRT_IPTOS:
      if (m_bOpened)
         *(int32_t*)optval = m_pSndQueue->getIpToS();
      else
         *(int32_t*)optval = m_iIpToS;
      break;
#endif

   case SRT_SENDER:
      *(int32_t*)optval = m_bDataSender;
      optlen = sizeof(int32_t);
      break;


   case SRT_TSBPDMODE:
      *(int32_t*)optval = m_bOPT_TsbPd;
      optlen = sizeof(int32_t);
      break;

   case SRT_TSBPDDELAY:
   case SRT_RCVLATENCY:
      *(int32_t*)optval = m_iTsbPdDelay;
      optlen = sizeof(int32_t);
      break;

   case SRT_PEERLATENCY:
      *(int32_t*)optval = m_iPeerTsbPdDelay;
      optlen = sizeof(int32_t);
      break;

#ifdef SRT_ENABLE_TLPKTDROP
   case SRT_TSBPDMAXLAG:
      //Obsolete: preserve binary compatibility.
      *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

   case SRT_TLPKTDROP:
      *(int32_t*)optval = m_bTLPktDrop;
      optlen = sizeof(int32_t);
      break;
#endif /* SRT_ENABLE_TLPKTDROP */

   case SRT_PBKEYLEN:
      if (m_pCryptoControl)
         *(int32_t*)optval = m_pCryptoControl->KeyLen(); // Running Key length.
      else
         *(int32_t*)optval = m_iSndCryptoKeyLen; // May be 0.
      optlen = sizeof(int32_t);
      break;

      /*
         XXX This was an experimental bidirectional implementation using HSv4,
         which was using two separate KMX processes per direction. HSv4 bidirectional
         implementation has been completely abandoned for the sake of HSv5, in
         which there's only one KMX process performed as a part of handshake this
         time and therefore there's still one key, one key length and the encryption
         uses the same SEK for both directions (in result, the same password).

   case SRT_SNDPBKEYLEN:
      if (m_pCryptoControl)
         *(int32_t*)optval = m_pCryptoControl->m_iSndKmKeyLen;
      else
         *(int32_t*)optval = m_iSndCryptoKeyLen;
      optlen = sizeof(int32_t);
      break;

   case SRT_RCVPBKEYLEN:
      if (m_pCryptoControl)
         *(int32_t*)optval = m_pCryptoControl->m_iRcvKmKeyLen;
      else
         *(int32_t*)optval = 0; //Defined on sender's side only
      optlen = sizeof(int32_t);
      break;
      */

   case SRT_SNDPEERKMSTATE: /* Sender's peer decryption state */
      /*
      * Was SRT_KMSTATE (receiver's decryption state) before TWOWAY support,
      * where sender reports peer (receiver) state and the receiver reports local state when connected.
      * Maintain binary compatibility and return what SRT_RCVKMSTATE returns for receive-only connected peer.
      */
      if (m_pCryptoControl)
         *(int32_t*)optval = (m_bDataSender || m_bTwoWayData) ? m_pCryptoControl->m_iSndPeerKmState : m_pCryptoControl->m_iRcvKmState;
      else
         *(int32_t*)optval = SRT_KM_S_UNSECURED;
      optlen = sizeof(int32_t);
      break;

   case SRT_RCVKMSTATE: /* Receiver decryption state */
      if (m_pCryptoControl)
         *(int32_t*)optval = (m_bDataSender || m_bTwoWayData) ? m_pCryptoControl->m_iSndPeerKmState : m_pCryptoControl->m_iRcvKmState;
      else
         *(int32_t*)optval = SRT_KM_S_UNSECURED;
      optlen = sizeof(int32_t);
      break;

#ifdef SRT_ENABLE_NAKREPORT
   case SRT_RCVNAKREPORT:
      *(bool*)optval = m_bRcvNakReport;
      optlen = sizeof(bool);
      break;
#endif /* SRT_ENABLE_NAKREPORT */

   case SRT_AGENTVERSION:
      *(int32_t*)optval = m_lSrtVersion;
      optlen = sizeof(int32_t);
      break;

   case SRT_PEERVERSION:
      *(int32_t*)optval = m_lPeerSrtVersion;
      optlen = sizeof(int32_t);
      break;

#ifdef SRT_ENABLE_CONNTIMEO
   case SRT_CONNTIMEO:
      *(int*)optval = m_iConnTimeOut;
      optlen = sizeof(int);
      break;
#endif

   case SRT_MINVERSION:
      *(uint32_t*)optval = m_lMinimumPeerSrtVersion;
      optlen = sizeof(uint32_t);
      break;

   case SRT_STREAMID:
      if (size_t(optlen) < m_sStreamName.size()+1)
          throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

      strcpy((char*)optval, m_sStreamName.c_str());
      optlen = m_sStreamName.size();
      break;

   default:
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);
   }
}

bool CUDT::setstreamid(UDTSOCKET u, const std::string& sid)
{
    CUDT* that = getUDTHandle(u);
    if (!that)
        return false;

    if (sid.size() >= MAX_SID_LENGTH)
        return false;

    if (that->m_bConnected)
        return false;

    that->m_sStreamName = sid;
    return true;
}

std::string CUDT::getstreamid(UDTSOCKET u)
{
    CUDT* that = getUDTHandle(u);
    if (!that)
        return "";

    return that->m_sStreamName;
}

void CUDT::clearData()
{
   // Initial sequence number, loss, acknowledgement, etc.
   m_iPktSize = m_iMSS - CPacket::UDP_HDR_SIZE;
   m_iPayloadSize = m_iPktSize - CPacket::HDR_SIZE;

   LOGC(mglog.Debug) << "clearData: PAYLOAD SIZE: " << m_iPayloadSize;

   m_iEXPCount = 1;
   m_iBandwidth = 1;    //pkts/sec
   // XXX use some constant for this 16
   m_iDeliveryRate = 16 * m_iPayloadSize;
   m_iAckSeqNo = 0;
   m_ullLastAckTime = 0;

   // trace information
   m_StartTime = CTimer::getTime();
   m_llSentTotal = m_llRecvTotal = m_iSndLossTotal = m_iRcvLossTotal = m_iRetransTotal = m_iSentACKTotal = m_iRecvACKTotal = m_iSentNAKTotal = m_iRecvNAKTotal = 0;
   m_LastSampleTime = CTimer::getTime();
   m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
   m_iTraceReorderDistance = 0;
   m_fTraceBelatedTime = 0.0;
   m_iTraceRcvBelated = 0;

#ifdef SRT_ENABLE_TLPKTDROP
   m_iSndDropTotal          = 0;
   m_iTraceSndDrop          = 0;
   m_iRcvDropTotal          = 0;
   m_iTraceRcvDrop          = 0;
#endif /* SRT_ENABLE_TLPKTDROP */

   m_iRcvUndecryptTotal        = 0;
   m_iTraceRcvUndecrypt        = 0;

   m_ullBytesSentTotal      = 0;
   m_ullBytesRecvTotal      = 0;
   m_ullBytesRetransTotal   = 0;
   m_ullTraceBytesSent      = 0;
   m_ullTraceBytesRecv      = 0;
   m_ullTraceBytesRetrans   = 0;
#ifdef SRT_ENABLE_TLPKTDROP
   m_ullSndBytesDropTotal   = 0;
   m_ullRcvBytesDropTotal   = 0;
   m_ullTraceSndBytesDrop   = 0;
   m_ullTraceRcvBytesDrop   = 0;
#endif /* SRT_ENABLE_TLPKTDROP */
   m_ullRcvBytesUndecryptTotal = 0;
   m_ullTraceRcvBytesUndecrypt = 0;

   // Resetting these data because this happens when agent isn't connected.
   m_bPeerTsbPd = false;
   m_iPeerTsbPdDelay = 0;

   m_bTsbPd = m_bOPT_TsbPd; // Take the values from user-configurable options
   m_iTsbPdDelay = m_iOPT_TsbPdDelay;
   m_bTLPktDrop = m_bOPT_TLPktDrop;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bPeerTLPktDrop = false;
#endif /* SRT_ENABLE_TLPKTDROP */

#ifdef SRT_ENABLE_NAKREPORT
   m_bPeerNakReport = false;
#endif /* SRT_ENABLE_NAKREPORT */

   m_bPeerRexmitFlag = false;

   m_llSndDuration = m_llSndDurationTotal = 0;

   m_RdvState = CHandShake::RDV_INVALID;
   m_ullRcvPeerStartTime = 0;
}

void CUDT::open()
{
   CGuard cg(m_ConnectionLock);

   clearData();

   // structures for queue
   if (m_pSNode == NULL)
      m_pSNode = new CSNode;
   m_pSNode->m_pUDT = this;
   m_pSNode->m_llTimeStamp = 1;
   m_pSNode->m_iHeapLoc = -1;

   if (m_pRNode == NULL)
      m_pRNode = new CRNode;
   m_pRNode->m_pUDT = this;
   m_pRNode->m_llTimeStamp = 1;
   m_pRNode->m_pPrev = m_pRNode->m_pNext = NULL;
   m_pRNode->m_bOnList = false;

   m_iRTT = 10 * CPacket::SYN_INTERVAL;
   m_iRTTVar = m_iRTT >> 1;
   m_ullCPUFrequency = CTimer::getCPUFrequency();

   // set up the timers
   m_ullSYNInt = CPacket::SYN_INTERVAL * m_ullCPUFrequency;

   // set minimum NAK and EXP timeout to 300ms
#ifdef SRT_ENABLE_NAKREPORT
   if (m_bRcvNakReport)
      m_ullMinNakInt = m_iMinNakInterval * m_ullCPUFrequency;
   else
#endif
   m_ullMinNakInt = 300000 * m_ullCPUFrequency;
   m_ullMinExpInt = 300000 * m_ullCPUFrequency;

   m_ullACKInt = m_ullSYNInt;
   m_ullNAKInt = m_ullMinNakInt;

   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;
   m_ullNextACKTime = currtime + m_ullSYNInt;
   m_ullNextNAKTime = currtime + m_ullNAKInt;
#ifdef SRT_ENABLE_FASTREXMIT
   m_ullLastRspAckTime = currtime;
   m_iReXmitCount = 1;
#endif /* SRT_ENABLE_FASTREXMIT */
#ifdef SRT_ENABLE_CBRTIMESTAMP
   m_ullSndLastCbrTime = currtime;
#endif
#ifdef SRT_FIX_KEEPALIVE
   m_ullLastSndTime = currtime;
#endif

   m_iPktCount = 0;
   m_iLightACKCount = 1;

   m_ullTargetTime = 0;
   m_ullTimeDiff = 0;

   // Now UDT is opened.
   m_bOpened = true;
}

void CUDT::setListenState()
{
   CGuard cg(m_ConnectionLock);

   if (!m_bOpened)
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

   if (m_bConnecting || m_bConnected)
      throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

   // listen can be called more than once
   if (m_bListening)
      return;

   // if there is already another socket listening on the same port
   if (m_pRcvQueue->setListener(this) < 0)
      throw CUDTException(MJ_NOTSUP, MN_BUSY, 0);

   m_bListening = true;
}

size_t CUDT::fillSrtHandshake(uint32_t* srtdata, size_t srtlen, int msgtype, int hs_version)
{
    if ( srtlen < SRT_HS__SIZE )
    {
        LOGC(mglog.Fatal) << "IPE: fillSrtHandshake: buffer too small: " << srtlen << " (expected: " << SRT_HS__SIZE << ")";
        return 0;
    }

    srtlen = SRT_HS__SIZE; // We use only that much space.

    memset(srtdata, 0, sizeof(uint32_t)*srtlen);
    /* Current version (1.x.x) SRT handshake */
    srtdata[SRT_HS_VERSION] = m_lSrtVersion;  /* Required version */
    srtdata[SRT_HS_FLAGS] |= SRT_OPT_HAICRYPT;

    switch (msgtype)
    {
    case SRT_CMD_HSREQ: return fillSrtHandshake_HSREQ(srtdata, srtlen, hs_version);
    case SRT_CMD_HSRSP: return fillSrtHandshake_HSRSP(srtdata, srtlen, hs_version);
    default: LOGC(mglog.Fatal) << "IPE: createSrtHandshake/sendSrtMsg called with value " << msgtype; return 0;
    }
}

size_t CUDT::fillSrtHandshake_HSREQ(uint32_t* srtdata, size_t /* srtlen - unused */, int hs_version)
{
    // INITIATOR sends HSREQ.

    // The TSBPD(SND|RCV) options are being set only if the TSBPD is set in the current agent.
    // The agent has a decisive power only in the range of RECEIVING the data, however it can
    // also influence the peer's latency. If agent doesn't set TSBPD mode, it doesn't send any
    // latency flags, although the peer might still want to do Rx with TSBPD. When agent sets
    // TsbPd mode, it defines latency values for Rx (itself) and Tx (peer's Rx). If peer does
    // not set TsbPd mode, it will simply ignore the proposed latency (PeerTsbPdDelay), although
    // if it has received the Rx latency as well, it must honor it and respond accordingly
    // (the latter is only in case of HSv5 and bidirectional connection).
    if (m_bOPT_TsbPd)
    {
        m_iTsbPdDelay = m_iOPT_TsbPdDelay;
        m_iPeerTsbPdDelay = m_iOPT_PeerTsbPdDelay;
        /*
         * Sent data is real-time, use Time-based Packet Delivery,
         * set option bit and configured delay
         */
        srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;

        if ( hs_version < CUDT::HS_VERSION_SRT1 )
        {
            // HSv4 - this uses only one value.
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iPeerTsbPdDelay);
        }
        else
        {
            // HSv5 - this will be understood only since this version when this exists.
            srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay);

            m_bTsbPd = true;
            // And in the reverse direction.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;
            srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay);

            // This wasn't there for HSv4, this setting is only for the receiver.
            // HSv5 is bidirectional, so every party is a receiver.

#ifdef SRT_ENABLE_TLPKTDROP
            if (m_bTLPktDrop)
                srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;
#endif
        }
    }

    // I support SRT_OPT_REXMITFLG. Do you?
    srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;

    LOGC(mglog.Debug) << "HSREQ/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY])
        << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]) << "] FLAGS["
        << SrtFlagString(srtdata[SRT_HS_FLAGS]) << "]";

    return 3;
}

size_t CUDT::fillSrtHandshake_HSRSP(uint32_t* srtdata, size_t /* srtlen - unused */, int hs_version)
{
    // Setting m_ullRcvPeerStartTime is done ine processSrtMsg_HSREQ(), so
    // this condition will be skipped only if this function is called without
    // getting first received HSREQ. Doesn't look possible in both HSv4 and HSv5.
    if (m_ullRcvPeerStartTime != 0)
    {
        // If Agent doesn't set TSBPD, it will not set the TSBPD flag back to the Peer.
        // The peer doesn't have be disturbed by it anyway.
        if (m_bTsbPd)
        {
            /* 
             * We got and transposed peer start time (HandShake request timestamp),
             * we can support Timestamp-based Packet Delivery
             */
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;

            if ( hs_version < HS_VERSION_SRT1 )
            {
                // HSv4 - this uses only one value
                srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_LEG::wrap(m_iTsbPdDelay);
            }
            else
            {
                // HSv5 - this puts "agent's" latency into RCV field and "peer's" -
                // into SND field.
                srtdata[SRT_HS_LATENCY] = SRT_HS_LATENCY_RCV::wrap(m_iTsbPdDelay);
            }
        }
        else
        {
            LOGC(mglog.Debug) << "HSRSP/snd: TSBPD off, NOT responding TSBPDRCV flag.";
        }

        // Hsv5, only when peer has declared TSBPD mode.
        // The flag was already set, and the value already "maximized" in processSrtMsg_HSREQ().
        if (m_bPeerTsbPd && hs_version >= HS_VERSION_SRT1 )
        {
            // HSv5 is bidirectional - so send the TSBPDSND flag, and place also the
            // peer's latency into SND field.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;
            srtdata[SRT_HS_LATENCY] |= SRT_HS_LATENCY_SND::wrap(m_iPeerTsbPdDelay);

            LOGC(mglog.Debug) << "HSRSP/snd: HSv5 peer uses TSBPD, responding TSBPDSND latency=" << m_iPeerTsbPdDelay;
        }
        else
        {
            LOGC(mglog.Debug) << "HSRSP/snd: HSv" << (hs_version == CUDT::HS_VERSION_UDT4 ? 4 : 5)
                << " with peer TSBPD=" << (m_bPeerTsbPd ? "on" : "off") << " - NOT responding TSBPDSND";
        }

#ifdef SRT_ENABLE_TLPKTDROP
        if (m_bTLPktDrop)
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;
#endif
    }
    else
    {
        LOGC(mglog.Fatal) << "IPE: fillSrtHandshake_HSRSP: m_ullRcvPeerStartTime NOT SET!";
        return 0;
    }


#ifdef SRT_ENABLE_NAKREPORT
    if (m_bRcvNakReport)
    {
        // HSv5: Note that this setting is independent on the value of
        // m_bPeerNakReport, which represent this setting in the peer.

        srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;
        /*
         * NAK Report is so efficient at controlling bandwidth that sender TLPktDrop
         * is not needed. SRT 1.0.5 to 1.0.7 sender TLPktDrop combined with SRT 1.0
         * Timestamp-Based Packet Delivery was not well implemented and could drop
         * big I-Frame tail before sending once on low latency setups.
         * Disabling TLPktDrop in the receiver SRT Handshake Reply prevents the sender
         * from enabling Too-Late Packet Drop.
         */
        if (m_lPeerSrtVersion <= SrtVersion(1, 0, 7))
            srtdata[SRT_HS_FLAGS] &= ~SRT_OPT_TLPKTDROP;
    }
#endif

    if ( m_lSrtVersion >= SrtVersion(1, 2, 0) )
    {
        if (!m_bPeerRexmitFlag)
        {
            // Peer does not request to use rexmit flag, if so,
            // we won't use as well.
            LOGC(mglog.Debug) << "HSRSP/snd: AGENT understands REXMIT flag, but PEER DOES NOT. NOT setting.";
        }
        else
        {
            // Request that the rexmit bit be used as a part of msgno.
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;
            LOGC(mglog.Debug).form("HSRSP/snd: AGENT UNDERSTANDS REXMIT flag and PEER reported that it does, too." );
        }
    }
    else
    {
        // Since this is now in the code, it can occur only in case when you change the 
        // version specification in the build configuration.
        LOGC(mglog.Debug).form("HSRSP/snd: AGENT DOES NOT UNDERSTAND REXMIT flag" );
    }

    LOGC(mglog.Debug) << "HSRSP/snd: LATENCY[SND:" << SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY])
        << " RCV:" << SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]) << "] FLAGS["
        << SrtFlagString(srtdata[SRT_HS_FLAGS]) << "]";

    return 3;
}

size_t CUDT::prepareSrtHsMsg(int cmd, uint32_t* srtdata, size_t size)
{
    size_t srtlen = fillSrtHandshake(srtdata, size, cmd, handshakeVersion());
    LOGC(mglog.Debug).form("CMD:%s(%d) Len:%d Version: %s Flags: %08X (%s) sdelay:%d",
            MessageTypeStr(UMSG_EXT, cmd).c_str(), cmd, (int)(srtlen * sizeof(int32_t)),
            SrtVersionString(srtdata[SRT_HS_VERSION]).c_str(),
            srtdata[SRT_HS_FLAGS],
            SrtFlagString(srtdata[SRT_HS_FLAGS]).c_str(),
            srtdata[SRT_HS_LATENCY]);

    return srtlen;
}

void CUDT::sendSrtMsg(int cmd, uint32_t *srtdata_in, int srtlen_in)
{
    CPacket srtpkt;
    int32_t srtcmd = (int32_t)cmd;

    static const size_t SRTDATA_MAXSIZE = SRT_CMD_MAXSZ/sizeof(int32_t);

    // This is in order to issue a compile error if the SRT_CMD_MAXSZ is
    // too small to keep all the data. As this is "static const", declaring
    // an array of such specified size in C++ isn't considered VLA.
    static const int SRTDATA_SIZE = SRTDATA_MAXSIZE >= SRT_HS__SIZE ? SRTDATA_MAXSIZE : -1;

    // This will be effectively larger than SRT_HS__SIZE, but it will be also used
    // for incoming data. We have a guarantee that it won't be larger than SRTDATA_MAXSIZE.
    uint32_t srtdata[SRTDATA_SIZE];

    int srtlen = 0;

    if ( cmd == SRT_CMD_REJECT )
    {
        // This is a value returned by processSrtMsg underlying layer, potentially
        // to be reported here. Should this happen, just send a rejection message.
        cmd = SRT_CMD_HSRSP;
        srtdata[SRT_HS_VERSION] = 0;
    }

    switch(cmd){
    case SRT_CMD_HSREQ:
    case SRT_CMD_HSRSP:
        srtlen = prepareSrtHsMsg(cmd, srtdata, SRTDATA_SIZE);
        break;

    case SRT_CMD_KMREQ: //Sender
    case SRT_CMD_KMRSP: //Receiver
        srtlen = srtlen_in;
        /* Msg already in network order
         * But CChannel:sendto will swap again (assuming 32-bit fields)
         * Pre-swap to cancel it.
         */
        HtoNLA(srtdata, srtdata_in, srtlen);
        m_pCryptoControl->updateKmState(cmd, srtlen); // <-- THIS function can't be moved to CUDT

        break;

    default:
        LOGC(mglog.Error).form( "sndSrtMsg: cmd=%d unsupported", cmd);
        break;
    }

    if (srtlen > 0)
    {
        /* srtpkt.pack will set message data in network order */
        srtpkt.pack(UMSG_EXT, &srtcmd, srtdata, srtlen * sizeof(int32_t));
        addressAndSend(srtpkt);
    }
}



// PREREQUISITE:
// pkt must be set the buffer and configured for UMSG_HANDSHAKE.
// Note that this function replaces also serialization for the HSv4.
bool CUDT::createSrtHandshake(ref_t<CPacket> r_pkt, ref_t<CHandShake> r_hs,
        int srths_cmd, int srtkm_cmd,
        const uint32_t* kmdata, size_t kmdata_wordsize /* IN WORDS, NOT BYTES!!! */)
{
    CPacket& pkt = r_pkt;
    CHandShake& hs = r_hs;

    LOGC(mglog.Debug) << "createSrtHandshake: have buffer size=" << pkt.getLength() << " kmdata_wordsize=" << kmdata_wordsize;

    // values > URQ_CONCLUSION include also error types
    // if (hs.m_iVersion == HS_VERSION_UDT4 || hs.m_iReqType > URQ_CONCLUSION) <--- This condition was checked b4 and it's only valid for caller-listener mode
    if (!hs.m_extension)
    {
        // Serialize only the basic handshake, if this is predicted for
        // Hsv4 peer or this is URQ_INDUCTION or URQ_WAVEAHAND.
        if (hs.m_iVersion > HS_VERSION_UDT4)
        {
            // The situation when this function is called without requested extensions
            // is URQ_CONCLUSION in rendezvous mode in some of the transitions.
            // In this case for version 5 just clear the m_iType field, as it has
            // different meaning in HSv5 and contains extension flags.
            hs.m_iType = 0;
        }

        size_t hs_size = pkt.getLength();
        hs.store_to(pkt.m_pcData, Ref(hs_size));
        pkt.setLength(hs_size);
        LOGC(mglog.Debug) << "createSrtHandshake: (no HSREQ/KMREQ ext) data: " << hs.show();
        return true;
    }

    string logext = "HSREQ";

    bool have_kmreq = false;
    bool have_sid = false;

    // Install the SRT extensions
    hs.m_iType = CHandShake::HS_EXT_HSREQ;

    if ( srths_cmd == SRT_CMD_HSREQ && m_sStreamName != "" )
    {
        have_sid = true;
        hs.m_iType |= CHandShake::HS_EXT_SID;
        logext += ",SID";
    }

    if (m_iSndCryptoKeyLen > 0)
    {
        have_kmreq = true;
        hs.m_iType |= CHandShake::HS_EXT_KMREQ;
        logext += ",KMREQ";
    }

    LOGC(mglog.Debug) << "createSrtHandshake: (ext: " << logext << ") data: " << hs.show();

    // NOTE: The HSREQ is practically always required, although may happen
    // in future that CONCLUSION can be sent multiple times for a separate
    // stream encryption support, and this way it won't enclose HSREQ.
    // Also, KMREQ may occur multiple times.

    // So, initially store the UDT legacy handshake.
    size_t hs_size = pkt.getLength(), total_ra_size = (hs_size/sizeof(uint32_t)); // Maximum size of data
    hs.store_to(pkt.m_pcData, Ref(hs_size)); // hs_size is updated

    size_t ra_size = hs_size/sizeof(int32_t);

    // Now attach the SRT handshake for HSREQ
    size_t offset = ra_size;
    uint32_t* p = reinterpret_cast<uint32_t*>(pkt.m_pcData);
    // NOTE: since this point, ra_size has a size in int32_t elements, NOT BYTES.

    // The first 4-byte item is the CMD/LENGTH spec.
    uint32_t* pcmdspec = p+offset; // Remember the location to be filled later, when we know the length
    ++offset;

    // Now use the original function to store the actual SRT_HS data
    // ra_size after that
    // NOTE: so far, ra_size is m_iPayloadSize expressed in number of elements.
    // WILL BE CHANGED HERE.
    ra_size = fillSrtHandshake(p+offset, total_ra_size - offset, srths_cmd, HS_VERSION_SRT1);
    *pcmdspec = HS_CMDSPEC_CMD::wrap(srths_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);

    LOGC(mglog.Debug) << "createSrtHandshake: after HSREQ: offset=" << offset << " HSREQ size=" << ra_size << " space left: " << (total_ra_size - offset);

    if ( have_sid )
    {
        // Use only in REQ phase and only if stream name is set
        offset += ra_size;
        pcmdspec = p+offset;
        ++offset;

        // Now prepare the string with 4-byte alignment. The string size is limited
        // to half the payload size. Just a sanity check to not pack too much into
        // the conclusion packet.
        size_t size_limit = m_iPayloadSize/2;

        if ( m_sStreamName.size() >= size_limit )
        {
            LOGC(mglog.Error) << "createSrtHandshake: stream id too long, limited to " << (size_limit-1) << " bytes";
            return false;
        }

        size_t wordsize = (m_sStreamName.size()+3)/4;
        size_t aligned_bytesize = wordsize*4;

        memset(p+offset, 0, aligned_bytesize);
        memcpy(p+offset, m_sStreamName.data(), m_sStreamName.size());

        ra_size = wordsize;
        *pcmdspec = HS_CMDSPEC_CMD::wrap(SRT_CMD_SID) | HS_CMDSPEC_SIZE::wrap(ra_size);

        LOGC(mglog.Debug) << "createSrtHandshake: after SID [" << m_sStreamName << "] length=" << m_sStreamName.size() << " alignedln=" << aligned_bytesize
            << ": offset=" << offset << " SID size=" << ra_size << " space left: " << (total_ra_size - offset);
    }

    // When encryption turned on
    if (have_kmreq)
    {
        LOGC(mglog.Debug) << "createSrtHandshake: Agent uses ENCRYPTION";
        if ( srtkm_cmd == SRT_CMD_KMREQ )
        {
            bool have_any_keys = false;
            for (size_t ki = 0; ki < 2; ++ki)
            {
                // Skip those that have expired
                if ( !m_pCryptoControl->getKmMsg_needSend(ki) )
                    continue;

                m_pCryptoControl->getKmMsg_markSent(ki);

                offset += ra_size;

                size_t msglen = m_pCryptoControl->getKmMsg_size(ki);
                // Make ra_size back in element unit
                // Add one extra word if the size isn't aligned to 32-bit.
                ra_size = (msglen / sizeof(uint32_t)) + (msglen % sizeof(uint32_t) ? 1 : 0);

                // Store the CMD + SIZE in the next field
                *(p + offset) = HS_CMDSPEC_CMD::wrap(srtkm_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);
                ++offset;

                // Copy the key - do the endian inversion because another endian inversion
                // will be done for every control message before sending, and this KM message
                // is ALREADY in network order.
                const uint32_t* keydata = reinterpret_cast<const uint32_t*>(m_pCryptoControl->getKmMsg_data(ki));

                LOGC(mglog.Debug) << "createSrtHandshake: KMREQ: adding key #" << ki
                    << " length=" << ra_size << " words (KmMsg_size=" << msglen << ")";
                    // XXX INSECURE ": [" << FormatBinaryString((uint8_t*)keydata, msglen) << "]";

                // Yes, I know HtoNLA and NtoHLA do exactly the same operation, but I want
                // to be clear about the true intention.
                NtoHLA(p + offset, keydata, ra_size);
                have_any_keys = true;
            }

            if ( !have_any_keys )
            {
                LOGC(mglog.Error) << "createSrtHandshake: IPE: all keys have expired, no KM to send.";
                return false;
            }
        }
        else if ( srtkm_cmd == SRT_CMD_KMRSP )
        {
            if ( !kmdata || kmdata_wordsize == 0 )
            {
                LOGC(mglog.Fatal) << "createSrtHandshake: IPE: srtkm_cmd=SRT_CMD_KMRSP and no kmdata!";
                return false;
            }

            // Shift the starting point with the value of previously added block,
            // to start with the new one.
            offset += ra_size;

            ra_size = kmdata_wordsize;
            *(p + offset) = HS_CMDSPEC_CMD::wrap(srtkm_cmd) | HS_CMDSPEC_SIZE::wrap(ra_size);
            ++offset;
            LOGC(mglog.Debug) << "createSrtHandshake: KMRSP: applying returned key length="
                << ra_size; // XXX INSECURE << " words: [" << FormatBinaryString((uint8_t*)kmdata, kmdata_wordsize*sizeof(uint32_t)) << "]";

            const uint32_t* keydata = reinterpret_cast<const uint32_t*>(kmdata);
            NtoHLA(p + offset, keydata, ra_size);
        }
        else
        {
            LOGC(mglog.Fatal) << "createSrtHandshake: IPE: wrong value of srtkm_cmd: " << srtkm_cmd;
            return false;
        }
    }

    // ra_size + offset has a value in element unit.
    // Switch it again to byte unit.
    pkt.setLength((ra_size + offset) * sizeof(int32_t));

    LOGC(mglog.Debug) << "createSrtHandshake: filled HSv5 handshake flags: "
        << hs.m_iType << " length: " << pkt.getLength() << " bytes";

    return true;
}

static int FindExtensionBlock(uint32_t* begin, size_t total_length,
        ref_t<size_t> r_out_len, ref_t<uint32_t*> r_next_block)
{
    size_t& out_len = r_out_len;
    uint32_t*& next_block = r_next_block;
    // This function extracts the block command from the block and its length.
    // The command value is returned as a function result.
    // The size of that command block is stored into out_len.
    // The beginning of the prospective next block is stored in next_block.

    // The caller must be aware that:
    // - exactly one element holds the block header (cmd+size), so the actual data are after this one.
    // - the returned size is the number of uint32_t elements since that first data element
    // - the remaining size should be manually calculated as total_length - 1 - out_len, or
    // simply, as next_block - begin.

    // Note that if the total_length is too short to extract the whole block, it will return
    // SRT_CMD_NONE. Note that total_length includes this first CMDSPEC word.
    //
    // When SRT_CMD_NONE is returned, it means that nothing has been extracted and nothing else
    // can be further extracted from this block.

    int cmd = HS_CMDSPEC_CMD::unwrap(*begin);
    size_t size = HS_CMDSPEC_SIZE::unwrap(*begin);

    if ( size + 1 > total_length )
        return SRT_CMD_NONE;

    out_len = size;

    if ( total_length == size + 1 )
        next_block = NULL;
    else
        next_block = begin + 1 + size;

    return cmd;
}

void CUDT::processSrtMsg(const CPacket *ctrlpkt)
{
    uint32_t *srtdata = (uint32_t *)ctrlpkt->m_pcData;
    size_t len = ctrlpkt->getLength();
    int etype = ctrlpkt->getExtendedType();
    uint32_t ts = ctrlpkt->m_iTimeStamp;

    int res = SRT_CMD_NONE;

    LOGC(mglog.Debug) << "Dispatching message type=" << etype << " data length=" << (len/sizeof(int32_t));
    switch (etype)
    {
    case SRT_CMD_HSREQ:
        {
            res = processSrtMsg_HSREQ(srtdata, len, ts, CUDT::HS_VERSION_UDT4);
            break;
        }
    case SRT_CMD_HSRSP:
        {
            res = processSrtMsg_HSRSP(srtdata, len, ts, CUDT::HS_VERSION_UDT4);
            break;
        }
    case SRT_CMD_KMREQ:
        // Special case when the data need to be processed here
        // and the appropriate message must be constructed for sending.
        // No further processing required
        {
            uint32_t srtdata_out[SRTDATA_MAXSIZE];
            size_t len_out = 0;
            res = m_pCryptoControl->processSrtMsg_KMREQ(srtdata, len, srtdata_out, Ref(len_out), CUDT::HS_VERSION_UDT4);
            if ( res == SRT_CMD_KMRSP )
            {
                LOGC(mglog.Debug) << "KMREQ -> requested to send KMRSP length=" << len_out;
                sendSrtMsg(SRT_CMD_KMRSP, srtdata_out, len_out);
            }
            else
            {
                LOGC(mglog.Error) << "KMREQ failed to process the request - ignoring";
            }

            return; // already done what's necessary
        }

    case SRT_CMD_KMRSP:
        {
            // KMRSP doesn't expect any following action
            m_pCryptoControl->processSrtMsg_KMRSP(srtdata, len, CUDT::HS_VERSION_UDT4);
            return; // nothing to do
        }

    default:
        LOGC(mglog.Error).form( "rcvSrtMsg: cmd=%d len=%zu unsupported message", etype, len);
        break;
    }

    if ( res == SRT_CMD_NONE )
        return;

    // Send the message that the message handler requested.
    sendSrtMsg(res);
}

int CUDT::processSrtMsg_HSREQ(const uint32_t* srtdata, size_t len, uint32_t ts, int hsv)
{
    // Set this start time in the beginning, regardless as to whether TSBPD is being
    // used or not. This must be done in the Initiator as well as Responder.

    /*
     * Compute peer StartTime in our time reference
     * This takes time zone, time drift into account.
     * Also includes current packet transit time (rtt/2)
     */
#if 0                   //Debug PeerStartTime if not 1st HS packet
    {
        uint64_t oldPeerStartTime = m_ullRcvPeerStartTime;
        m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
        if (oldPeerStartTime) {
            LOGC(mglog.Note).form( "rcvSrtMsg: 2nd PeerStartTime diff=%lld usec", 
                    (long long)(m_ullRcvPeerStartTime - oldPeerStartTime));
        }
    }
#else
    m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
#endif

    // Prepare the initial runtime values of latency basing on the option values.
    // They are going to get the value fixed HERE.
    m_iTsbPdDelay = m_iOPT_TsbPdDelay;
    m_iPeerTsbPdDelay = m_iOPT_PeerTsbPdDelay;

    if (len < SRT_CMD_HSREQ_MINSZ)
    {
        /* Packet smaller than minimum compatible packet size */
        LOGC(mglog.Error).form( "HSREQ/rcv: cmd=%d(HSREQ) len=%zu invalid", SRT_CMD_HSREQ, len);
        return SRT_CMD_NONE;
    }

    LOGC(mglog.Note).form( "HSREQ/rcv: cmd=%d(HSREQ) len=%zu vers=0x%x opts=0x%x delay=%d", 
            SRT_CMD_HSREQ, len, srtdata[SRT_HS_VERSION], srtdata[SRT_HS_FLAGS],
            SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]));

    m_lPeerSrtVersion = srtdata[SRT_HS_VERSION];
    uint32_t peer_srt_options = srtdata[SRT_HS_FLAGS];

    if ( hsv == CUDT::HS_VERSION_UDT4 )
    {
        if ( m_lPeerSrtVersion >= SRT_VERSION_FEAT_HSv5 )
        {
            LOGC(mglog.Error) << "HSREQ/rcv: With HSv4 version >= "
                << SrtVersionString(SRT_VERSION_FEAT_HSv5) << " is not acceptable.";
            return SRT_CMD_REJECT;
        }
    }
    else
    {
        if ( m_lPeerSrtVersion < SRT_VERSION_FEAT_HSv5 )
        {
            LOGC(mglog.Error) << "HSREQ/rcv: With HSv5 version must be >= "
                << SrtVersionString(SRT_VERSION_FEAT_HSv5) << " .";
            return SRT_CMD_REJECT;
        }
    }

    // Check also if the version satisfies the minimum required version
    if ( m_lPeerSrtVersion < m_lMinimumPeerSrtVersion )
    {
        LOGC(mglog.Error) << "HSREQ/rcv: Peer version: " << SrtVersionString(m_lPeerSrtVersion)
            << " is too old for requested: " << SrtVersionString(m_lMinimumPeerSrtVersion) << " - REJECTING";
        return SRT_CMD_REJECT;
    }

    LOGC(mglog.Debug) << "HSREQ/rcv: PEER Version: "
        << SrtVersionString(m_lPeerSrtVersion)
        << " Flags: " << peer_srt_options
        << "(" << SrtFlagString(peer_srt_options) << ")";

    m_bPeerRexmitFlag = IsSet(peer_srt_options, SRT_OPT_REXMITFLG);
    LOGC(mglog.Debug).form("HSREQ/rcv: peer %s REXMIT flag", m_bPeerRexmitFlag ? "UNDERSTANDS" : "DOES NOT UNDERSTAND" );

    if ( len < SRT_HS_LATENCY+1 )
    {
        // 3 is the size when containing VERSION, FLAGS and LATENCY. Less size
        // makes it contain only the first two. Let's make it acceptable, as long
        // as the latency flags aren't set.
        if ( IsSet(peer_srt_options, SRT_OPT_TSBPDSND) || IsSet(peer_srt_options, SRT_OPT_TSBPDRCV) )
        {
            LOGC(mglog.Error) << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, but TSBPD flags are set. Rejecting.";
            return SRT_CMD_REJECT;
        }

        LOGC(mglog.Warn) << "HSREQ/rcv: Peer sent only VERSION + FLAGS HSREQ, not getting any TSBPD settings.";
        // Don't process any further settings in this case. Turn off TSBPD, just for a case.
        m_bTsbPd = false;
        m_bPeerTsbPd = false;
        return SRT_CMD_HSRSP;
    }

    uint32_t latencystr = srtdata[SRT_HS_LATENCY];

    if ( IsSet(peer_srt_options, SRT_OPT_TSBPDSND) )
    {
        //TimeStamp-based Packet Delivery feature enabled
        if ( !m_bTsbPd )
        {
            LOGC(mglog.Warn) << "HSREQ/rcv: Agent did not set rcv-TSBPD - ignoring proposed latency from peer";

            // Note: also don't set the peer TSBPD flag HERE because
            // - in HSv4 it will be a sender, so it doesn't matter anyway
            // - in HSv5 if it's going to receive, the TSBPDRCV flag will define it.
        }
        else
        {
            int peer_decl_latency;
            if ( hsv < CUDT::HS_VERSION_SRT1 )
            {
                // In HSv4 there is only one value and this is the latency
                // that the sender peer proposes for the agent.
                peer_decl_latency = SRT_HS_LATENCY_LEG::unwrap(latencystr);
            }
            else
            {
                // In HSv5 there are latency declared for sending and receiving separately.

                // SRT_HS_LATENCY_SND is the value that the peer proposes to be the
                // value used by agent when receiving data. We take this as a local latency value.
                peer_decl_latency = SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY]);
            }


            // Use the maximum latency out of latency from our settings and the latency
            // "proposed" by the peer.
            int maxdelay = std::max(m_iTsbPdDelay, peer_decl_latency);
            LOGC(mglog.Debug) << "HSREQ/rcv: LOCAL/RCV LATENCY: Agent:" << m_iTsbPdDelay
                << " Peer:" << peer_decl_latency << "  Selecting:" << maxdelay;
            m_iTsbPdDelay = maxdelay;
        }
    }
    else
    {
        std::string how_about_agent = m_bTsbPd ? "BUT AGENT DOES" : "and nor does Agent";
        LOGC(mglog.Debug) << "HSREQ/rcv: Peer DOES NOT USE latency for sending - " << how_about_agent;
    }

    // This happens when the HSv5 RESPONDER receives the HSREQ message; it declares
    // that the peer INITIATOR will receive the data and informs about its predefined
    // latency. We need to maximize this with our setting of the peer's latency and
    // record as peer's latency, which will be then sent back with HSRSP.
    if ( hsv > CUDT::HS_VERSION_UDT4 && IsSet(peer_srt_options, SRT_OPT_TSBPDRCV) )
    {
        // So, PEER uses TSBPD, set the flag.
        // NOTE: it doesn't matter, if AGENT uses TSBPD.
        m_bPeerTsbPd = true;

        // SRT_HS_LATENCY_RCV is the value that the peer declares as to be
        // used by it when receiving data. We take this as a peer's value,
        // and select the maximum of this one and our proposed latency for the peer.
        int peer_decl_latency = SRT_HS_LATENCY_RCV::unwrap(latencystr);
        int maxdelay = std::max(m_iPeerTsbPdDelay, peer_decl_latency);
        LOGC(mglog.Debug) << "HSREQ/rcv: PEER/RCV LATENCY: Agent:" << m_iPeerTsbPdDelay
            << " Peer:" << peer_decl_latency << " Selecting:" << maxdelay;
        m_iPeerTsbPdDelay = maxdelay;
    }
    else
    {
        std::string how_about_agent = m_bTsbPd ? "BUT AGENT DOES" : "and nor does Agent";
        LOGC(mglog.Debug) << "HSREQ/rcv: Peer DOES NOT USE latency for receiving - " << how_about_agent;
    }

    if ( hsv > CUDT::HS_VERSION_UDT4 )
    {
        // This is HSv5, do the same things as required for the sending party in HSv4,
        // as in HSv5 this can also be a sender.
#ifdef SRT_ENABLE_TLPKTDROP
        if (IsSet(peer_srt_options, SRT_OPT_TLPKTDROP))
        {
            //Too late packets dropping feature supported
            m_bPeerTLPktDrop = true;
        }
#endif /* SRT_ENABLE_TLPKTDROP */
#ifdef SRT_ENABLE_NAKREPORT
        if (IsSet(peer_srt_options, SRT_OPT_NAKREPORT))
        {
            //Peer will send Periodic NAK Reports
            m_bPeerNakReport = true;
        }
#endif /* SRT_ENABLE_NAKREPORT */
    }


    return SRT_CMD_HSRSP;
}

int CUDT::processSrtMsg_HSRSP(const uint32_t* srtdata, size_t len, uint32_t ts, int hsv)
{
    // XXX Check for mis-version
    // With HSv4 we accept only version less than 1.2.0
    if ( hsv == CUDT::HS_VERSION_UDT4 && srtdata[SRT_HS_VERSION] >= SRT_VERSION_FEAT_HSv5 )
    {
        LOGC(mglog.Error) << "HSRSP/rcv: With HSv4 version >= 1.2.0 is not acceptable.";
        return SRT_CMD_NONE;
    }

    if (len < SRT_CMD_HSRSP_MINSZ)
    {
        /* Packet smaller than minimum compatible packet size */
        LOGC(mglog.Error).form( "HSRSP/rcv: cmd=%d(HSRSP) len=%zu invalid", SRT_CMD_HSRSP, len);
        return SRT_CMD_NONE;
    }

    // Set this start time in the beginning, regardless as to whether TSBPD is being
    // used or not. This must be done in the Initiator as well as Responder. In case when
    // agent is sender only (HSv4) this value simply won't be used.

    /*
     * Compute peer StartTime in our time reference
     * This takes time zone, time drift into account.
     * Also includes current packet transit time (rtt/2)
     */
#if 0                   //Debug PeerStartTime if not 1st HS packet
    {
        uint64_t oldPeerStartTime = m_ullRcvPeerStartTime;
        m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
        if (oldPeerStartTime) {
            LOGC(mglog.Note).form( "rcvSrtMsg: 2nd PeerStartTime diff=%lld usec", 
                    (long long)(m_ullRcvPeerStartTime - oldPeerStartTime));
        }
    }
#else
    m_ullRcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ts);
#endif

    m_lPeerSrtVersion = srtdata[SRT_HS_VERSION];
    uint32_t peer_srt_options = srtdata[SRT_HS_FLAGS];

    LOGC(mglog.Debug).form("HSRSP/rcv: Version: %s Flags: SND:%08X (%s)",
            SrtVersionString(m_lPeerSrtVersion).c_str(),
            peer_srt_options,
            SrtFlagString(peer_srt_options).c_str());


    if ( hsv == CUDT::HS_VERSION_UDT4 )
    {
        // The old HSv4 way: extract just one value and put it under peer.
        if (IsSet(peer_srt_options, SRT_OPT_TSBPDRCV))
        {
            //TsbPd feature enabled
            m_bPeerTsbPd = true;
            m_iPeerTsbPdDelay = SRT_HS_LATENCY_LEG::unwrap(srtdata[SRT_HS_LATENCY]);
            LOGC(mglog.Debug) << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay
                << " (Agent: declared:" << m_iTsbPdDelay << " rcv:" << m_iTsbPdDelay << ")";
        }
        // TSBPDSND isn't set in HSv4 by the RESPONDER, because HSv4 RESPONDER is always RECEIVER.
    }
    else
    {
        // HSv5 way: extract the receiver latency and sender latency, if used.

        if (IsSet(peer_srt_options, SRT_OPT_TSBPDRCV))
        {
            //TsbPd feature enabled
            m_bPeerTsbPd = true;
            m_iPeerTsbPdDelay = SRT_HS_LATENCY_RCV::unwrap(srtdata[SRT_HS_LATENCY]);
            LOGC(mglog.Debug) << "HSRSP/rcv: LATENCY: Peer/snd:" << m_iPeerTsbPdDelay;
        }
        else
        {
            LOGC(mglog.Debug) << "HSRSP/rcv: Peer (responder) DOES NOT USE latency";
        }

        if (IsSet(peer_srt_options, SRT_OPT_TSBPDSND))
        {
            if (!m_bTsbPd)
            {
                LOGC(mglog.Warn) << "HSRSP/rcv: BUG? Peer (responder) declares sending latency, but Agent turned off TSBPD.";
            }
            else
            {
                // Take this value as a good deal. In case when the Peer did not "correct" the latency
                // because it has TSBPD turned off, just stay with the present value defined in options.
                m_iTsbPdDelay = SRT_HS_LATENCY_SND::unwrap(srtdata[SRT_HS_LATENCY]);
                LOGC(mglog.Debug) << "HSRSP/rcv: LATENCY Agent/rcv: " << m_iTsbPdDelay;
            }
        }
    }

#ifdef SRT_ENABLE_TLPKTDROP
    if ((m_lSrtVersion >= SrtVersion(1, 0, 5)) && IsSet(peer_srt_options, SRT_OPT_TLPKTDROP))
    {
        //Too late packets dropping feature supported
        m_bPeerTLPktDrop = true;
    }
#endif /* SRT_ENABLE_TLPKTDROP */
#ifdef SRT_ENABLE_NAKREPORT
    if ((m_lSrtVersion >= SrtVersion(1, 1, 0)) && IsSet(peer_srt_options, SRT_OPT_NAKREPORT))
    {
        //Peer will send Periodic NAK Reports
        m_bPeerNakReport = true;
    }
#endif /* SRT_ENABLE_NAKREPORT */

    if ( m_lSrtVersion >= SrtVersion(1, 2, 0) )
    {
        if ( IsSet(peer_srt_options, SRT_OPT_REXMITFLG) )
        {
            //Peer will use REXMIT flag in packet retransmission.
            m_bPeerRexmitFlag = true;
            LOGP(mglog.Debug, "HSRSP/rcv: 1.2.0+ Agent understands REXMIT flag and so does peer.");
        }
        else
        {
            LOGP(mglog.Debug, "HSRSP/rcv: Agent understands REXMIT flag, but PEER DOES NOT");
        }
    }
    else
    {
        LOGC(mglog.Debug).form("HSRSP/rcv: <1.2.0 Agent DOESN'T understand REXMIT flag");
    }

    handshakeDone();

    return SRT_CMD_NONE;
}

// This function is called only when the URQ_CONCLUSION handshake has been received from the peer.
bool CUDT::interpretSrtHandshake(const CHandShake& hs, const CPacket& hspkt, uint32_t* out_data, size_t* out_len)
{
    // Initialize out_len to 0 to handle the unencrypted case
    if ( out_len )
        *out_len = 0;

    // The version=0 statement as rejection is used only since HSv5.
    // The HSv4 sends the AGREEMENT handshake message with version=0, do not misinterpret it.
    if ( m_ConnRes.m_iVersion > HS_VERSION_UDT4 && hs.m_iVersion == 0 )
    {
        LOGC(mglog.Error) << "HS VERSION = 0, meaning the handshake has been rejected.";
        return false;
    }

    if ( hs.m_iVersion < HS_VERSION_SRT1 )
        return true; // do nothing

    // Anyway, check if the handshake contains any extra data.
    if ( hspkt.getLength() <= CHandShake::m_iContentSize )
    {
        // This would mean that the handshake was at least HSv5, but somehow no extras were added.
        // Dismiss it then, however this has to be logged.
        LOGC(mglog.Error) << "HS VERSION=" << hs.m_iVersion << " but no handshake extension found!";
        return false;
    }

    // We still believe it should work, let's check the flags.
    int ext_flags = hs.m_iType;

    if ( ext_flags == 0 )
    {
        LOGC(mglog.Error) << "HS VERSION=" << hs.m_iVersion << " but no handshake extension flags are set!";
        return false;
    }

    LOGC(mglog.Debug) << "HS VERSION=" << hs.m_iVersion << " EXTENSIONS: " << CHandShake::ExtensionFlagStr(ext_flags);

    // Ok, now find the beginning of an int32_t array that follows the UDT handshake.
    uint32_t* p = reinterpret_cast<uint32_t*>(hspkt.m_pcData + CHandShake::m_iContentSize);
    size_t size = hspkt.getLength() - CHandShake::m_iContentSize; // Due to previous cond check we grant it's >0

    if ( IsSet(ext_flags, CHandShake::HS_EXT_HSREQ) )
    {
        LOGC(mglog.Debug) << "interpretSrtHandshake: extracting HSREQ/RSP type extension";
        uint32_t* begin = p;
        uint32_t* next = 0;
        size_t length = size / sizeof(uint32_t);
        size_t blocklen = 0;

        for(;;) // this is ONE SHOT LOOP
        {
            int cmd = FindExtensionBlock(begin, length, Ref(blocklen), Ref(next));

            size_t bytelen = blocklen*sizeof(uint32_t);

            if ( cmd == SRT_CMD_HSREQ )
            {
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if ( blocklen < SRT_HS__SIZE )
                {
                    LOGC(mglog.Error) << "HS-ext HSREQ found but invalid size: " << bytelen
                        << " (expected: " << SRT_HS__SIZE << ")";
                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSREQ(begin+1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_HSRSP.
                if ( rescmd != SRT_CMD_HSRSP )
                {
                    LOGC(mglog.Error) << "interpretSrtHandshake: process HSREQ returned unexpected value " << rescmd;
                    return false;
                }
                handshakeDone();
                updateAfterSrtHandshake(SRT_CMD_HSREQ, HS_VERSION_SRT1);
            }
            else if ( cmd == SRT_CMD_HSRSP )
            {
                // Set is the size as it should, then give it for interpretation for
                // the proper function.
                if ( blocklen < SRT_HS__SIZE )
                {
                    LOGC(mglog.Error) << "HS-ext HSRSP found but invalid size: " << bytelen
                        << " (expected: " << SRT_HS__SIZE << ")";

                    return false; // don't interpret
                }

                int rescmd = processSrtMsg_HSRSP(begin+1, bytelen, hspkt.m_iTimeStamp, HS_VERSION_SRT1);
                // Interpreted? Then it should be responded with SRT_CMD_NONE.
                // (nothing to be responded for HSRSP, unless there was some kinda problem)
                if ( rescmd != SRT_CMD_NONE )
                {
                    LOGC(mglog.Error) << "interpretSrtHandshake: process HSRSP returned unexpected value " << rescmd;
                    return false;
                }
                handshakeDone();
                updateAfterSrtHandshake(SRT_CMD_HSRSP, HS_VERSION_SRT1);
            }
            else if ( cmd == SRT_CMD_NONE )
            {
                LOGC(mglog.Error) << "interpretSrtHandshake: no HSREQ/HSRSP block found in the handshake msg!";
                // This means that there can be no more processing done by FindExtensionBlock().
                // And we haven't found what we need - otherwise one of the above cases would pass
                // and lead to exit this loop immediately.
                return false;
            }
            else
            {
                // Any other kind of message extracted. Search on.
                length -= (next - begin);
                begin = next;
                if (begin)
                    continue;
            }

            break;
        }
    }

    LOGC(mglog.Debug) << "interpretSrtHandshake: HSREQ done, checking KMREQ";

    // Now check the encrypted

    bool encrypted = false;

    if ( IsSet(ext_flags, CHandShake::HS_EXT_KMREQ) )
    {
        LOGC(mglog.Debug) << "interpretSrtHandshake: extracting KMREQ/RSP type extension";

        if (m_iSndCryptoKeyLen <= 0)
        {
            LOGC(mglog.Error) << "HS KMREQ: Peer declares encryption, but agent does not.";
            return false;
        }

        uint32_t* begin = p;
        uint32_t* next = 0;
        size_t length = size / sizeof(uint32_t);
        size_t blocklen = 0;

        for(;;) // This is one shot loop, unless REPEATED by 'continue'.
        {
            int cmd = FindExtensionBlock(begin, length, Ref(blocklen), Ref(next));

            LOGC(mglog.Debug) << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd);

            size_t bytelen = blocklen*sizeof(uint32_t);
            if ( cmd == SRT_CMD_KMREQ )
            {
                if ( !out_data || !out_len )
                {
                    LOGC(mglog.Fatal) << "IPE: HS/KMREQ extracted without passing target buffer!";
                    return false;
                }

                int res = m_pCryptoControl->processSrtMsg_KMREQ(begin+1, bytelen, out_data, Ref(*out_len), HS_VERSION_SRT1);
                if ( res != SRT_CMD_KMRSP )
                {
                    // Something went wrong.
                    LOGC(mglog.Debug) << "interpretSrtHandshake: KMREQ processing failed - returned " << res;
                    return false;
                }
                encrypted = true;
            }
            else if ( cmd == SRT_CMD_KMRSP )
            {
                m_pCryptoControl->processSrtMsg_KMRSP(begin+1, bytelen, HS_VERSION_SRT1);
                // XXX Possible to check status?
                encrypted = true;
            }
            else if ( cmd == SRT_CMD_NONE )
            {
                LOGC(mglog.Error) << "HS KMREQ expected - none found!";
                return false;
            }
            else
            {
                LOGC(mglog.Debug) << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd);
                length -= (next - begin);
                begin = next;
                if (begin)
                    continue;
            }

            break;
        }
    }

    if ( IsSet(ext_flags, CHandShake::HS_EXT_SID) )
    {
        LOGC(mglog.Debug) << "interpretSrtHandshake: extracting SID type extension";

        uint32_t* begin = p;
        uint32_t* next = 0;
        size_t length = size / sizeof(uint32_t);
        size_t blocklen = 0;

        for(;;) // This is one shot loop, unless REPEATED by 'continue'.
        {
            int cmd = FindExtensionBlock(begin, length, Ref(blocklen), Ref(next));

            LOGC(mglog.Debug) << "interpretSrtHandshake: found extension: (" << cmd << ") " << MessageTypeStr(UMSG_EXT, cmd);

            size_t bytelen = blocklen*sizeof(uint32_t);
            if ( cmd == SRT_CMD_SID )
            {
                // Copied through a cleared array. This is because the length is aligned to 4
                // where the padding is filled by zero bytes. For the case when the string is
                // exactly of a 4-divisible length, we make a big array with maximum allowed size
                // filled with zeros. Copying to this array should then copy either only the valid
                // characters of the string (if the lenght is divisible by 4), or the string with
                // padding zeros. In all these cases in the resulting array we should have all
                // subsequent characters of the string plus at least one '\0' at the end. This will
                // make it a perfect NUL-terminated string, to be used to initialize a string.
                char target[MAX_SID_LENGTH+1];
                memset(target, 0, MAX_SID_LENGTH+1);
                memcpy(target, begin+1, bytelen);
                m_sStreamName = target;
                LOGC(mglog.Debug) << "CONNECTOR'S REQUESTED SID [" << m_sStreamName << "] (bytelen=" << bytelen << " blocklen=" << blocklen;
            }
            else if ( cmd == SRT_CMD_NONE )
            {
                LOGC(mglog.Error) << "HS SID expected - none found!";
                return false;
            }
            else
            {
                LOGC(mglog.Debug) << "interpretSrtHandshake: ... skipping " << MessageTypeStr(UMSG_EXT, cmd);
                length -= (next - begin);
                begin = next;
                if (begin)
                    continue;
            }

            break;
        }
    }

    if ( !encrypted && m_iSndCryptoKeyLen > 0 )
    {
        LOGC(mglog.Error) << "HS EXT: Agent declares encryption, but peer does not.";
        return false;
    }

    // Ok, finished, for now.
    return true;
}

void CUDT::startConnect(const sockaddr* serv_addr, int32_t forced_isn)
{
    CGuard cg(m_ConnectionLock);

    LOGC(mglog.Debug) << "startConnect: -> " << SockaddrToString(serv_addr) << "...";

    if (!m_bOpened)
        throw CUDTException(MJ_NOTSUP, MN_NONE, 0);

    if (m_bListening)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    if (m_bConnecting || m_bConnected)
        throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

    // record peer/server address
    delete m_pPeerAddr;
    m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
    memcpy(m_pPeerAddr, serv_addr, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

    // register this socket in the rendezvous queue
    // RendezevousQueue is used to temporarily store incoming handshake, non-rendezvous connections also require this function
#ifdef SRT_ENABLE_CONNTIMEO
    uint64_t ttl = m_iConnTimeOut * 1000ULL;
#else
    uint64_t ttl = 3000000;
#endif
    // XXX DEBUG
    //ttl = 0x1000000000000000;
    // XXX
    if (m_bRendezvous)
        ttl *= 10;
    ttl += CTimer::getTime();
    m_pRcvQueue->registerConnector(m_SocketID, this, m_iIPversion, serv_addr, ttl);

    // This is my current configuration
    if (m_bRendezvous)
    {
        // For rendezvous, use version 5 in the waveahand and the cookie.
        // In case when you get the version 4 waveahand, simply switch to
        // the legacy HSv4 rendezvous and this time send version 4 CONCLUSION.

        // The HSv4 client simply won't check the version nor the cookie and it
        // will be sending its waveahands with version 4. Only when the party
        // has sent version 5 waveahand should the agent continue with HSv5
        // rendezvous.
        m_ConnReq.m_iVersion = HS_VERSION_SRT1;
        //m_ConnReq.m_iVersion = HS_VERSION_UDT4; // <--- Change in order to do regression test.
        m_ConnReq.m_iReqType = URQ_WAVEAHAND;
        m_ConnReq.m_iCookie = bake(serv_addr);
        m_RdvState = CHandShake::RDV_WAVING;
        m_SrtHsSide = HSD_DRAW; // initially not resolved.
    }
    else
    {
        // For caller-listener configuration, set the version 4 for INDUCTION
        // due to a serious problem in UDT code being also in the older SRT versions:
        // the listener peer simply sents the EXACT COPY of the caller's induction
        // handshake, except the cookie, which means that when the caller sents version 5,
        // the listener will respond with version 5, which is a false information. Therefore
        // HSv5 clients MUST send HS_VERSION_UDT4 from the caller, regardless of currently
        // supported handshake version.
        //
        // The HSv5 listener should only respond with INDUCTION with m_iVersion == HS_VERSION_SRT1.
        m_ConnReq.m_iVersion = HS_VERSION_UDT4;
        m_ConnReq.m_iReqType = URQ_INDUCTION;
        m_ConnReq.m_iCookie = 0;
        m_RdvState = CHandShake::RDV_INVALID;
    }

    m_ConnReq.m_iType = m_iSockType;
    m_ConnReq.m_iMSS = m_iMSS;
    m_ConnReq.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;
    m_ConnReq.m_iID = m_SocketID;
    CIPAddress::ntop(serv_addr, m_ConnReq.m_piPeerIP, m_iIPversion);

    if ( forced_isn == 0 )
    {
        // Random Initial Sequence Number (normal mode)
        srand((unsigned int)CTimer::getTime());
        m_iISN = m_ConnReq.m_iISN = (int32_t)(CSeqNo::m_iMaxSeqNo * (double(rand()) / RAND_MAX));
    }
    else
    {
        // Predefined ISN (for debug purposes)
        m_iISN = m_ConnReq.m_iISN = forced_isn;
    }

    m_iLastDecSeq = m_iISN - 1;
    m_iSndLastAck = m_iISN;
    m_iSndLastDataAck = m_iISN;
#ifdef SRT_ENABLE_TLPKTDROP
    m_iSndLastFullAck = m_iISN;
#endif /* SRT_ENABLE_TLPKTDROP */
    m_iSndCurrSeqNo = m_iISN - 1;
    m_iSndLastAck2 = m_iISN;
    m_ullSndLastAck2Time = CTimer::getTime();

    // Inform the server my configurations.
    CPacket reqpkt;
    reqpkt.setControl(UMSG_HANDSHAKE);
    reqpkt.allocate(m_iPayloadSize);
    // XXX NOTE: Now the memory for the payload part is allocated automatically,
    // and such allocated memory is also automatically deallocated in the
    // destructor. If you use CPacket::allocate, remember that you must not:
    // - delete this memory
    // - assign to m_pcData.
    // If you use only manual assignment to m_pCData, this is then manual
    // allocation and so it won't be deallocated in the destructor.
    //
    // (Desired would be to disallow modification of m_pcData outside the
    // control of methods.)

    // ID = 0, connection request
    reqpkt.m_iID = 0;

    size_t hs_size = m_iPayloadSize;
    m_ConnReq.store_to(reqpkt.m_pcData, Ref(hs_size));

    // Note that CPacket::allocate() sets also the size
    // to the size of the allocated buffer, which not
    // necessarily is to be the size of the data.
    reqpkt.setLength(hs_size);

    uint64_t now = CTimer::getTime();
    reqpkt.m_iTimeStamp = int32_t(now - m_StartTime);

    LOGC(mglog.Debug) << CONID() << "CUDT::startConnect: REQ-TIME HIGH. SENDING HS: " << m_ConnReq.show();

    /*
     * Race condition if non-block connect response thread scheduled before we set m_bConnecting to true?
     * Connect response will be ignored and connecting will wait until timeout.
     * Maybe m_ConnectionLock handling problem? Not used in CUDT::connect(const CPacket& response)
     */
    m_llLastReqTime = now;
    m_bConnecting = true;
    m_pSndQueue->sendto(serv_addr, reqpkt);

    //
    ///
    ////  ---> CONTINUE TO: <PEER>.CUDT::processConnectRequest()
    ///        (Take the part under condition: hs.m_iReqType == URQ_INDUCTION)
    ////  <--- RETURN WHEN: m_pSndQueue->sendto() is called.
    ////  .... SKIP UNTIL m_pRcvQueue->recvfrom() HERE....
    ////       (the first "sendto" will not be called due to being too early)
    ///
    //

    // asynchronous connect, return immediately
    if (!m_bSynRecving)
    {
        return;
    }

    // Wait for the negotiated configurations from the peer side.

    // This packet only prepares the storage where we will read the
    // next incoming packet.
    CPacket response;
    response.setControl(UMSG_HANDSHAKE);
    response.allocate(m_iPayloadSize);

    CUDTException e;

    while (!m_bClosing)
    {
        int64_t tdiff = CTimer::getTime() - m_llLastReqTime;
        // avoid sending too many requests, at most 1 request per 250ms

        // SHORT VERSION: 
        // The immediate first run of this loop WILL SKIP THIS PART, so
        // the processing really begins AFTER THIS CONDITION.
        //
        // Note that some procedures inside may set m_llLastReqTime to 0,
        // which will result of this condition to trigger immediately in
        // the next iteration.
        if (tdiff > 250000)
        {
            LOGC(mglog.Debug) << "startConnect: LOOP: time to send (" << tdiff << " > 250000). size=" << reqpkt.getLength();

            if (m_bRendezvous)
                reqpkt.m_iID = m_ConnRes.m_iID;

#if ENABLE_LOGGING
            {
                CHandShake debughs;
                debughs.load_from(reqpkt.m_pcData, reqpkt.getLength());
                LOGC(mglog.Debug) << CONID() << "startConnect: REQ-TIME HIGH. cont/sending HS to peer: " << debughs.show();
            }
#endif

            now = CTimer::getTime();
            m_llLastReqTime = now;
            reqpkt.m_iTimeStamp = int32_t(now - m_StartTime);
            m_pSndQueue->sendto(serv_addr, reqpkt);
        }
        else
        {
            LOGC(mglog.Debug) << "startConnect: LOOP: too early to send - " << tdiff << " < 250000";
        }

        EConnectStatus cst = CONN_CONTINUE;
        response.setLength(m_iPayloadSize);
        if (m_pRcvQueue->recvfrom(m_SocketID, Ref(response)) > 0)
        {
            LOGC(mglog.Debug) << CONID() << "startConnect: got response for connect request";
            cst = processConnectResponse(response, &e, true /*synchro*/);

            LOGC(mglog.Debug) << CONID() << "startConnect: response processing result: "
                << (cst == CONN_CONTINUE
                        ? "INDUCED/CONCLUDING"
                        : cst == CONN_ACCEPT
                            ? "ACCEPTED"
                            : cst == CONN_RENDEZVOUS
                                ? "RENDEZVOUS (HSv5)"
                                : "REJECTED");

            // Expected is that:
            // - the peer responded with URQ_INDUCTION + cookie. This above function
            //   should check that and craft the URQ_CONCLUSION handshake, in which
            //   case this function returns CONN_CONTINUE. As an extra action taken
            //   for that case, we set the SECURING mode if encryption requested,
            //   and serialize again the handshake, possibly together with HS extension
            //   blocks, if HSv5 peer responded. The serialized handshake will be then
            //   sent again, as the loop is repeated.
            // - the peer responded with URQ_CONCLUSION. This handshake was accepted
            //   as a connection, and for >= HSv5 the HS extension blocks have been
            //   also read and interpreted. In this case this function returns:
            //   - CONN_ACCEPT, if everything was correct - break this loop and return normally
            //   - CONN_REJECT in case of any problems with the delivered handshake
            //     (incorrect data or data conflict) - throw error exception
            // - the peer responded with any of URQ_ERROR_*.  - throw error exception
            //
            // The error exception should make the API connect() function fail, if blocking
            // or mark the failure for that socket in epoll, if non-blocking.

            if ( cst == CONN_RENDEZVOUS )
            {
                // When this function returned CONN_RENDEZVOUS, this requires
                // very special processing for the Rendezvous-v5 algorithm. This MAY
                // involve also preparing a new handshake form, also interpreting the
                // SRT handshake extension and crafting SRT handshake extension for the
                // peer, which should be next sent. When this function returns CONN_CONTINUE,
                // it means that it has done all that was required, however none of the below
                // things has to be done (this function will do it by itself if needed).
                // Otherwise the handshake rolling can be interrupted and considered complete.
                cst = processRendezvous(Ref(reqpkt), response, serv_addr, true /*synchro*/);
                if (cst == CONN_CONTINUE)
                    continue;
                break;
            }

            if ( cst != CONN_CONTINUE )
                break;

            // IMPORTANT
            // [[using assert(m_pCryptoControl != nullptr)]];

            // new request/response should be sent out immediately on receving a response
            LOGC(mglog.Debug) << "startConnect: REQ-TIME: LOW, should resend request quickly.";
            m_llLastReqTime = 0;

            // (if security needed, set the SECURING state)
            if (m_iSndCryptoKeyLen > 0)
            {
                m_pCryptoControl->m_iSndKmState = SRT_KM_S_SECURING;
                m_pCryptoControl->m_iSndPeerKmState = SRT_KM_S_SECURING;
                m_pCryptoControl->m_iRcvKmState = SRT_KM_S_SECURING;
                m_pCryptoControl->m_iRcvPeerKmState = SRT_KM_S_SECURING;
            }

            // Now serialize the handshake again to the existing buffer so that it's
            // then sent later in this loop.

            // First, set the size back to the original size, m_iPayloadSize because
            // this is the size of the originally allocated space. It might have been
            // shrunk by serializing the INDUCTION handshake (which was required before
            // sending this packet to the output queue) and therefore be too
            // small to store the CONCLUSION handshake (with HSv5 extensions).
            reqpkt.setLength(m_iPayloadSize);

            // These last 2 parameters designate the buffer, which is in use only for SRT_CMD_KMRSP.
            // If m_ConnReq.m_iVersion == HS_VERSION_UDT4, this function will do nothing,
            // except just serializing the UDT handshake.
            // The trick is that the HS challenge is with version HS_VERSION_UDT4, but the
            // listener should respond with HS_VERSION_SRT1, if it is HSv5 capable.

            LOGC(mglog.Debug) << "startConnect: creating HS CONCLUSION: buffer size=" << reqpkt.getLength();

            // NOTE: BUGFIX: SERIALIZE AGAIN.
            // The original UDT code didn't do it, so it was theoretically
            // turned into conclusion, but was sending still the original
            // induction handshake challenge message. It was working only
            // thanks to that simultaneously there were being sent handshake
            // messages from a separate thread (CSndQueue::worker) from - weird
            // as it's used in this mode - RendezvousQueue, this time
            // serialized properly, which caused that with blocking mode there
            // was a kinda initial "drunk passenger with taxi driver talk"
            // until the RendezvousQueue sends (when "the time comes") the
            // right CONCLUSION handshake challenge.
            //
            // Now that this is fixed, the handshake messages from RendezvousQueue
            // are sent only when there is a rendezvous mode or non-blocking mode.
            createSrtHandshake(Ref(reqpkt), Ref(m_ConnReq), SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0);
        }

        if ( cst == CONN_REJECT )
        {
            e = CUDTException(MJ_SETUP, MN_REJECTED, 0);
            break;
        }

        if (CTimer::getTime() > ttl)
        {
            // timeout
            e = CUDTException(MJ_SETUP, MN_TIMEOUT, 0);
            break;
        }
    }

    if (e.getErrorCode() == 0)
    {
        if (m_bClosing)                                                 // if the socket is closed before connection...
            e = CUDTException(MJ_SETUP); // XXX NO MN ?
        else if (m_ConnRes.m_iReqType == URQ_ERROR_REJECT)                          // connection request rejected
            e = CUDTException(MJ_SETUP, MN_REJECTED, 0);
        else if ((!m_bRendezvous) && (m_ConnRes.m_iISN != m_iISN))      // secuity check
            e = CUDTException(MJ_SETUP, MN_SECURITY, 0);
    }

    if (e.getErrorCode() != 0)
        throw e;

    LOGC(mglog.Debug) << CONID() << "startConnect: handshake exchange succeeded";

    // Parameters at the end.
    LOGC(mglog.Debug) << "startConnect: END. Parameters:"
        " mss=" << m_iMSS <<
        " max-cwnd-size=" << m_dMaxCWndSize <<
        " cwnd-size=" << m_dCWndSize <<
        " rcv-rate=" << m_iRcvRate <<
        " rtt=" << m_iRTT <<
        " bw=" << m_iBandwidth;
}

// Asynchronous connection
EConnectStatus CUDT::processAsyncConnectResponse(const CPacket& pkt) ATR_NOEXCEPT
{
    EConnectStatus cst = CONN_CONTINUE;
    CUDTException e;

    CGuard cg(m_ConnectionLock); // FIX
    LOGC(mglog.Debug) << CONID() << "processAsyncConnectResponse: got response for connect request, processing";
    cst = processConnectResponse(pkt, &e, false);

    LOGC(mglog.Debug) << CONID() << "processAsyncConnectResponse: response processing result: "
        << ConnectStatusStr(cst);

    return cst;
}

bool CUDT::processAsyncConnectRequest(EConnectStatus cst, const CPacket& response, const sockaddr* serv_addr)
{
    // Ok, LISTEN UP!
    //
    // This function is called, still asynchronously, but in the order
    // of call just after the call to the above processAsyncConnectResponse.
    // This should have got the original value returned from
    // processConnectResponse through processAsyncConnectResponse.

    CPacket request;
    request.setControl(UMSG_HANDSHAKE);
    request.allocate(m_iPayloadSize);
    uint64_t now = CTimer::getTime();
    request.m_iTimeStamp = int(now - this->m_StartTime);

    LOGC(mglog.Debug) << "startConnect: REQ-TIME: HIGH. Should prevent too quick responses.";
    m_llLastReqTime = now;
    // ID = 0, connection request
    request.m_iID = !m_bRendezvous ? 0 : m_ConnRes.m_iID;

    if ( cst == CONN_RENDEZVOUS )
    {
        LOGC(mglog.Debug) << "processAsyncConnectRequest: passing to processRendezvous";
        cst = processRendezvous(Ref(request), response, serv_addr, false /*asynchro*/);
        if (cst == CONN_ACCEPT)
        {
            LOGC(mglog.Debug) << "processAsyncConnectRequest: processRendezvous completed the process and responded by itself. Done.";
            return true;
        }

        if (cst != CONN_CONTINUE)
        {
            LOGC(mglog.Error) << "processAsyncConnectRequest: REJECT reported from processRendezvous, not processing further.";
            return false;
        }
    }
    else
    {
        // (this procedure will be also run for HSv4 rendezvous)
        size_t hs_size = m_iPayloadSize;
        LOGC(mglog.Debug) << "processAsyncConnectRequest: serializing HS: buffer size=" << request.getLength();
        if (!createSrtHandshake(Ref(request), Ref(m_ConnReq), SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0))
        {
            LOGC(mglog.Error) << "IPE: processAsyncConnectRequest: createSrtHandshake failed, dismissing.";
            return false;
        }
        hs_size = request.getLength();

        LOGC(mglog.Debug) << "processAsyncConnectRequest: sending HS reqtype=" << RequestTypeStr(m_ConnReq.m_iReqType)
            << " to socket " << request.m_iID << " size=" << hs_size;
    }

    m_pSndQueue->sendto(serv_addr, request);

    return true; // CORRECTLY HANDLED, REMOVE CONNECTOR.
}

void CUDT::cookieContest()
{
    if (m_SrtHsSide != HSD_DRAW)
        return;

    if ( m_ConnReq.m_iCookie == 0 || m_ConnRes.m_iCookie == 0 )
    {
        // Not all cookies are ready, don't start the contest.
        return;
    }

    // INITIATOR/RESPONDER role is resolved by COOKIE CONTEST.
    //
    // The cookie contest must be repeated every time because it
    // may change the state at some point.
    int better_cookie = m_ConnReq.m_iCookie - m_ConnRes.m_iCookie;

    if ( better_cookie > 0 )
    {
        m_SrtHsSide = HSD_INITIATOR;
        return;
    }

    if ( better_cookie < 0 )
    {
        m_SrtHsSide = HSD_RESPONDER;
        return;
    }

    // DRAW! The only way to continue would be to force the
    // cookies to be regenerated and to start over. But it's
    // not worth a shot - this is an extremely rare case.
    // This can simply do reject so that it can be started again.

    // Pretend then that the cookie contest wasn't done so that
    // it's done again. Cookies are baked every time anew, however
    // the successful initial contest remains valid no matter how
    // cookies will change.

    m_SrtHsSide = HSD_DRAW;
}

EConnectStatus CUDT::processRendezvous(ref_t<CPacket> reqpkt, const CPacket& response, const sockaddr* serv_addr, bool synchro)
{
    if ( m_RdvState == CHandShake::RDV_CONNECTED )
    {
        LOGC(mglog.Debug) << "processRendezvous: already in CONNECTED state.";
        return CONN_ACCEPT;
    }

    uint32_t kmdata[SRTDATA_MAXSIZE];
    size_t kmdatasize = SRTDATA_MAXSIZE;
    CPacket& rpkt = reqpkt;

    cookieContest();

    // We know that the other side was contacted and the other side has sent
    // the handshake message - we know then both cookies. If it's a draw, it's
    // a very rare case of creating identical cookies.
    if ( m_SrtHsSide == HSD_DRAW )
        return CONN_REJECT;

    UDTRequestType rsp_type;
    bool needs_extension = m_ConnRes.m_iType != 0; // Initial value: received HS has extensions.
    bool needs_hsrsp = rendezvousSwitchState(Ref(rsp_type), Ref(needs_extension));

    // We have three possibilities here as it comes to HSREQ extensions:

    // 1. The agent is loser in attention state, it sends EMPTY conclusion (without extensions)
    // 2. The agent is loser in initiated state, it interprets incoming HSREQ and creates HSRSP
    // 3. The agent is winner in attention or fine state, it sends HSREQ extension
    m_ConnReq.m_iReqType = rsp_type;
    m_ConnReq.m_extension = needs_extension;

    if (rsp_type > URQ_FAILURE_TYPES)
    {
        LOGC(mglog.Debug) << "processRendezvous: rejecting due to switch-state response: " << RequestTypeStr(rsp_type);
        return CONN_REJECT;
    }

    // This must be done before prepareConnectionObjects().
    applyResponseSettings();

    // This must be done before interpreting and creating HSv5 extensions.
    if ( !prepareConnectionObjects(m_ConnRes, m_SrtHsSide, 0))
    {
        LOGC(mglog.Debug) << "processRendezvous: rejecting due to problems in prepareConnectionObjects.";
        return CONN_REJECT;
    }

    // (if security needed, set the SECURING state)
    // (don't change it if already in FINE or INITIATED state).
    if ((m_RdvState == CHandShake::RDV_WAVING || m_RdvState == CHandShake::RDV_ATTENTION) && m_iSndCryptoKeyLen > 0)
    {
        m_pCryptoControl->m_iSndKmState = SRT_KM_S_SECURING;
        m_pCryptoControl->m_iSndPeerKmState = SRT_KM_S_SECURING;
        m_pCryptoControl->m_iRcvKmState = SRT_KM_S_SECURING;
        m_pCryptoControl->m_iRcvPeerKmState = SRT_KM_S_SECURING;
    }

    // Case 2.
    if ( needs_hsrsp )
    {
        LOGC(mglog.Debug) << "startConnect: REQ-TIME: LOW. Respond immediately.";
        m_llLastReqTime = 0;
        // This means that we have received HSREQ extension with the handshake, so we need to interpret
        // it and craft the response.
        if ( !interpretSrtHandshake(m_ConnRes, response, kmdata, &kmdatasize) )
        {
            LOGC(mglog.Debug) << "processRendezvous: rejecting due to problems in interpretSrtHandshake.";
            return CONN_REJECT;
        }

        // No matter the value of needs_extension, the extension is always needed
        // when HSREQ was interpreted (to store HSRSP extension).
        m_ConnReq.m_extension = true;

        LOGC(mglog.Debug) << "processConnectResponse: HSREQ extension ok, creating HSRSP response. kmdatasize=" << kmdatasize;

        rpkt.setLength(m_iPayloadSize);
        if (!createSrtHandshake(reqpkt, Ref(m_ConnReq), SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize))
        {
            LOGC(mglog.Debug) << "processRendezvous: rejecting due to problems in createSrtHandshake.";
            return CONN_REJECT;
        }

        // This means that it has received URQ_CONCLUSION with HSREQ, agent is then in RDV_FINE
        // state, it sends here URQ_CONCLUSION with HSREQ/KMREQ extensions and it awaits URQ_AGREEMENT.
        return CONN_CONTINUE;
    }

    // Special case: if URQ_AGREEMENT is to be sent, when this side is INITIATOR,
    // then it must have received HSRSP, so it must interpret it. Otherwise it would
    // end up with URQ_DONE, which means that it is the other side to interpret HSRSP.
    if ( m_SrtHsSide == HSD_INITIATOR && m_ConnReq.m_iReqType == URQ_AGREEMENT )
    {
        // The same is done in CUDT::postConnect(), however this section will
        // not be done in case of rendezvous. The section in postConnect() is
        // predicted to run only in regular CALLER handling.
        LOGC(mglog.Debug) << "processRendezvous: INITIATOR, will send AGREEMENT - interpreting HSRSP extension";
        if ( !interpretSrtHandshake(m_ConnRes, response, 0, 0) )
        {
            m_ConnReq.m_iReqType = URQ_ERROR_REJECT;
        }
        // This should be false, make a kinda assert here.
        if ( needs_extension )
        {
            LOGC(mglog.Fatal) << "IPE: INITIATOR responding AGREEMENT should declare no extensions to HS";
            m_ConnReq.m_extension = false;
        }
    }

    LOGC(mglog.Debug) << CONID() << "processRendezvous: COOKIES Agent/Peer: "
        << m_ConnReq.m_iCookie << "/" << m_ConnRes.m_iCookie
        << " HSD:" << (m_SrtHsSide == HSD_INITIATOR ? "initiator" : "responder")
        << " STATE:" << CHandShake::RdvStateStr(m_RdvState) << " ...";

    if ( rsp_type == URQ_DONE )
        LOGC(mglog.Debug) << "... WON'T SEND any response, both sides considered connected";
    else
        LOGC(mglog.Debug) << "... WILL SEND " << RequestTypeStr(rsp_type) << " "
        << (m_ConnReq.m_extension ? "with" : "without") << " SRT HS extensions";

    // This marks the information for the serializer that
    // the SRT handshake extension is required.
    // Rest of the data will be filled together with
    // serialization.
    m_ConnReq.m_extension = needs_extension;

    rpkt.setLength(m_iPayloadSize);
    // needs_extension here distinguishes between cases 1 and 3.
    // NOTE: in case when interpretSrtHandshake was run under the conditions above (to interpret HSRSP),
    // then createSrtHandshake below will create only empty AGREEMENT message.
    createSrtHandshake(reqpkt, Ref(m_ConnReq), SRT_CMD_HSREQ, SRT_CMD_KMREQ, 0, 0);

    if ( m_RdvState == CHandShake::RDV_CONNECTED )
    {
        // When synchro=false, don't lock a mutex for rendezvous queue.
        // This is required when this function is called in the
        // receive queue worker thread - it would lock itself.
        int cst = postConnect(response, true, 0, synchro);
        if ( cst == CONN_REJECT )
        {
            LOGC(mglog.Debug) << "processRendezvous: rejecting due to problems in postConnect.";
            return CONN_REJECT;
        }
    }

    // URQ_DONE or URQ_AGREEMENT can be the result if the state is RDV_CONNECTED.
    // If URQ_DONE, then there's nothing to be done, when URQ_AGREEMENT then return
    // CONN_CONTINUE to make the caller send again the contents if the packet buffer,
    // this time with URQ_AGREEMENT message, but still consider yourself connected.
    if ( rsp_type == URQ_DONE )
    {
        LOGC(mglog.Debug) << "processRendezvous: rsp=DONE, reporting ACCEPT (nothing to respond)";
        return CONN_ACCEPT;
    }

    if ( rsp_type == URQ_AGREEMENT && m_RdvState == CHandShake::RDV_CONNECTED )
    {
        // We are using our own serialization method (not the one called after
        // processConnectResponse, this is skipped in case when this function
        // is called), so we can also send this immediately. Agreement must be
        // sent just once and the party must switch into CONNECTED state - in
        // contrast to CONCLUSION messages, which should be sent in loop repeatedly.
        //
        // Even though in theory the AGREEMENT message sent just once may miss
        // the target (as normal thing in UDP), this is little probable to happen,
        // and this doesn't matter much because even if the other party doesn't
        // get AGREEMENT, but will get payload or KEEPALIVE messages, it will
        // turn into connected state as well. The AGREEMENT is rather kinda
        // catalyzer here and may turn the entity on the right track faster. When
        // AGREEMENT is missed, it may have kinda initial tearing.

        LOGC(mglog.Debug) << "processRendezvous: rsp=AGREEMENT, reporting ACCEPT and sending just this one, REQ-TIME HIGH.";
        uint64_t now = CTimer::getTime();
        m_llLastReqTime = now;
        rpkt.m_iTimeStamp = int32_t(now - m_StartTime);
        m_pSndQueue->sendto(serv_addr, rpkt);

        return CONN_ACCEPT;
    }

    // the request time must be updated so that the next handshake can be sent out immediately.
    LOGC(mglog.Debug) << "startConnect: REQ-TIME: LOW. Respond immediately.";
    m_llLastReqTime = 0;
    LOGC(mglog.Debug) << "processRendezvous: rsp=" << RequestTypeStr(m_ConnReq.m_iReqType) << " SENDING response, but consider yourself conencted";
    return CONN_CONTINUE;
}

EConnectStatus CUDT::processConnectResponse(const CPacket& response, CUDTException* eout, bool synchro) ATR_NOEXCEPT
{
    // NOTE: ASSUMED LOCK ON: m_ConnectionLock.

    // this is the 2nd half of a connection request. If the connection is setup successfully this returns 0.
    // Returned values:
    // - CONN_REJECT: there was some error when processing the response, connection should be rejected
    // - CONN_ACCEPT: the handshake is done and finished correctly
    // - CONN_CONTINUE: the induction handshake has been processed correctly, it's expected CONCLUSION handshake

   if (!m_bConnecting)
      return CONN_REJECT;

   // This is required in HSv5 rendezvous, in which it should send the URQ_AGREEMENT message to
   // the peer, however switch to connected state. 
   LOGC(mglog.Debug) << "processConnectResponse: TYPE:" << MessageTypeStr(response.getType(), response.getExtendedType());
   //ConnectStatus res = CONN_REJECT; // used later for status - must be declared here due to goto POST_CONNECT.

   // For HSv4, the data sender is INITIATOR, and the data receiver is RESPONDER,
   // regardless of the connecting side affiliation. This will be changed for HSv5.
   bool bidirectional = false;
   HandshakeSide hsd = m_bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
   // (defined here due to 'goto' below).

   // SRT peer may send the SRT handshake private message (type 0x7fff) before a keep-alive.

   // This condition is checked when the current agent is trying to do connect() in rendezvous mode,
   // but the peer was faster to send a handshake packet earlier. This makes it continue with connecting
   // process if the peer is already behaving as if the connection was already established.
   
   // This value will check either the initial value, which is less than SRT1, or
   // the value previously loaded to m_ConnReq during the previous handshake response.
   // For the initial form this value should not be checked.
   bool hsv5 = m_ConnRes.m_iVersion >= HS_VERSION_SRT1;

   if (m_bRendezvous
           && (
               m_RdvState == CHandShake::RDV_CONNECTED // somehow Rendezvous-v5 switched it to CONNECTED.
               || !response.isControl()                         // WAS A PAYLOAD PACKET.
               || (response.getType() == UMSG_KEEPALIVE)    // OR WAS A UMSG_KEEPALIVE message.
               || (response.getType() == UMSG_EXT)          // OR WAS a CONTROL packet of some extended type (i.e. any SRT specific)
              )
           // This may happen if this is an initial state in which the socket type was not yet set.
           // If this is a field that holds the response handshake record from the peer, this means that it wasn't received yet.
           // HSv5: added version check because in HSv5 the m_iType field has different meaning
           // and it may be 0 in case when the handshake does not carry SRT extensions.
           && ( hsv5 || m_ConnRes.m_iType != UDT_UNDEFINED))
   {
       //a data packet or a keep-alive packet comes, which means the peer side is already connected
       // in this situation, the previously recorded response will be used
       // In HSv5 this situation is theoretically possible if this party has missed the URQ_AGREEMENT message.
       LOGC(mglog.Debug) << CONID() << "processConnectResponse: already connected - pinning in";
       if (hsv5)
       {
           m_RdvState = CHandShake::RDV_CONNECTED;
       }

       return postConnect(response, hsv5, eout, synchro);
   }

   if ( !response.isControl(UMSG_HANDSHAKE) )
   {
       LOGC(mglog.Error) << CONID() << "processConnectResponse: received non-addresed packet not UMSG_HANDSHAKE: "
           << MessageTypeStr(response.getType(), response.getExtendedType());
       return CONN_REJECT;
   }

   if ( m_ConnRes.load_from(response.m_pcData, response.getLength()) == -1 )
   {
       // Handshake data were too small to reach the Handshake structure. Reject.
       LOGC(mglog.Error) << CONID() << "processConnectResponse: HANDSHAKE data buffer too small - possible blueboxing. Rejecting.";
       return CONN_REJECT;
   }

   LOGC(mglog.Debug) << CONID() << "processConnectResponse: HS RECEIVED: " << m_ConnRes.show();
   if ( m_ConnRes.m_iReqType > URQ_FAILURE_TYPES )
   {
       return CONN_REJECT;
   }

   if ( size_t(m_ConnRes.m_iMSS) > CPacket::ETH_MAX_MTU_SIZE )
   {
       // Yes, we do abort to prevent buffer overrun. Set your MSS correctly
       // and you'll avoid problems.
       LOGC(mglog.Fatal) << "MSS size " << m_iMSS << "exceeds MTU size!";
       return CONN_REJECT;
   }

   // (see createCrypter() call below)
   //
   // The CCryptoControl attached object must be created early
   // because it will be required to create a conclusion handshake in HSv5
   // 
   if (m_bRendezvous)
   {
       // SANITY CHECK: A rendezvous socket should reject any caller requests (it's not a listener)
       if (m_ConnRes.m_iReqType == URQ_INDUCTION)
       {
           LOGC(mglog.Error) << CONID() << "processConnectResponse: Rendezvous-point received INDUCTION handshake (expected WAVEAHAND). Rejecting.";
           return CONN_REJECT;
       }

       // The procedure for version 5 is completely different and changes the states
       // differently, so the old code will still maintain HSv4 the old way.

       if ( m_ConnRes.m_iVersion > HS_VERSION_UDT4 )
       {
           LOGC(mglog.Debug) << CONID() << "processConnectResponse: Rendezvous HSv5 DETECTED.";
           return CONN_RENDEZVOUS; // --> will continue in CUDT::processRendezvous().
       }

       LOGC(mglog.Debug) << CONID() << "processConnectResponse: Rendsezvous HSv4 DETECTED.";
       // So, here it has either received URQ_WAVEAHAND handshake message (while it should be in URQ_WAVEAHAND itself)
       // or it has received URQ_CONCLUSION/URQ_AGREEMENT message while this box has already sent URQ_WAVEAHAND to the peer,
       // and DID NOT send the URQ_CONCLUSION yet.

       if ( m_ConnReq.m_iReqType == URQ_WAVEAHAND
               || m_ConnRes.m_iReqType == URQ_WAVEAHAND )
       {
           LOGC(mglog.Debug) << CONID() << "processConnectResponse: REQ-TIME LOW. got HS RDV. Agent state:" << RequestTypeStr(m_ConnReq.m_iReqType)
               << " Peer HS:" << m_ConnRes.show();

           // Here we could have received WAVEAHAND or CONCLUSION.
           // For HSv4 simply switch to CONCLUSION for the sake of further handshake rolling.
           // For HSv5, make the cookie contest and basing on this decide, which party
           // should provide the HSREQ/KMREQ attachment.

           createCrypter(hsd, false /* unidirectional */);

           m_ConnReq.m_iReqType = URQ_CONCLUSION;
           // the request time must be updated so that the next handshake can be sent out immediately.
           m_llLastReqTime = 0;
           return CONN_CONTINUE;
       }
       else
       {
           LOGC(mglog.Debug) << CONID() << "processConnectResponse: Rendezvous HSv4 PAST waveahand";
       }
   }
   else
   {
      // set cookie
      if (m_ConnRes.m_iReqType == URQ_INDUCTION)
      {
         LOGC(mglog.Debug) << CONID() << "processConnectResponse: REQ-TIME LOW; got INDUCTION HS response (cookie:"
             << hex << m_ConnRes.m_iCookie << " version:" << dec << m_ConnRes.m_iVersion << "), sending CONCLUSION HS with this cookie";

         m_ConnReq.m_iCookie = m_ConnRes.m_iCookie;
         m_ConnReq.m_iReqType = URQ_CONCLUSION;

         // Here test if the LISTENER has responded with version HS_VERSION_SRT1,
         // it means that it is HSv5 capable. It can still accept the HSv4 handshake.
         if ( m_ConnRes.m_iVersion > HS_VERSION_UDT4 )
         {
             // This will catch HS_VERSION_SRT1 and any newer.
             // Set your highest version.
             m_ConnReq.m_iVersion = HS_VERSION_SRT1;
             // CONTROVERSIAL: use 0 as m_iType according to the meaning in HSv5.
             // The HSv4 client might not understand it, which means that agent
             // must switch itself to HSv4 rendezvous, and this time iType sould
             // be set to UDT_DGRAM value.
             m_ConnReq.m_iType = 0;

             // This marks the information for the serializer that
             // the SRT handshake extension is required.
             // Rest of the data will be filled together with
             // serialization.
             m_ConnReq.m_extension = true;

             // For HSv5, the caller is INITIATOR and the listener is RESPONDER.
             // The m_bDataSender value should be completely ignored and the
             // connection is always bidirectional.
             bidirectional = true;
             hsd = HSD_INITIATOR;
         }
         m_llLastReqTime = 0;
         createCrypter(hsd, bidirectional);

         // NOTE: This setup sets URQ_CONCLUSION and appropriate data in the handshake structure.
         // The full handshake to be sent will be filled back in the caller function -- CUDT::startConnect().
         return CONN_CONTINUE;
      }
   }

   return postConnect(response, false, eout, synchro);
}

void CUDT::applyResponseSettings()
{
    // Re-configure according to the negotiated values.
    m_iMSS = m_ConnRes.m_iMSS;
    m_iFlowWindowSize = m_ConnRes.m_iFlightFlagSize;
    m_iPktSize = m_iMSS - CPacket::UDP_HDR_SIZE;
    m_iPayloadSize = m_iPktSize - CPacket::HDR_SIZE;
    m_iPeerISN = m_ConnRes.m_iISN;
    m_iRcvLastAck = m_ConnRes.m_iISN;
#ifdef ENABLE_LOGGING
    m_iDebugPrevLastAck = m_iRcvLastAck;
#endif
#ifdef SRT_ENABLE_TLPKTDROP
    m_iRcvLastSkipAck = m_iRcvLastAck;
#endif /* SRT_ENABLE_TLPKTDROP */
    m_iRcvLastAckAck = m_ConnRes.m_iISN;
    m_iRcvCurrSeqNo = m_ConnRes.m_iISN - 1;
    m_PeerID = m_ConnRes.m_iID;
    memcpy(m_piSelfIP, m_ConnRes.m_piPeerIP, 16);

    LOGC(mglog.Debug) << CONID() << "applyResponseSettings: HANSHAKE CONCLUDED. SETTING: payload-size=" << m_iPayloadSize
        << " mss=" << m_ConnRes.m_iMSS
        << " flw=" << m_ConnRes.m_iFlightFlagSize
        << " isn=" << m_ConnRes.m_iISN
        << " peerID=" << m_ConnRes.m_iID;
}

EConnectStatus CUDT::postConnect(const CPacket& response, bool rendezvous, CUDTException* eout, bool synchro)
{
    if (m_ConnRes.m_iVersion < HS_VERSION_SRT1 )
        m_ullRcvPeerStartTime = 0; // will be set correctly in SRT HS.

    // Remove from rendezvous queue (in this particular case it's
    // actually removing the socket that undergoes asynchronous HS processing).
    m_pRcvQueue->removeConnector(m_SocketID, synchro);

    // This procedure isn't being executed in rendezvous because
    // in rendezvous it's completed before calling this function.
    if ( !rendezvous )
    {
        // NOTE: THIS function must be called before calling prepareConnectionObjects.
        // The reason why it's not part of prepareConnectionObjects is that the activities
        // done there are done SIMILAR way in acceptAndRespond, which also calls this
        // function. In fact, prepareConnectionObjects() represents the code that was
        // done separately in processConnectResponse() and acceptAndRespond(), so this way
        // this code is now common. Now acceptAndRespond() does "manually" something similar
        // to applyResponseSettings(), just a little bit differently. This SHOULD be made
        // common as a part of refactoring job, just needs a bit more time.
        //
        // Currently just this function must be called always BEFORE prepareConnectionObjects
        // everywhere except acceptAndRespond().
        applyResponseSettings();

        // This will actually be done also in rendezvous HSv4,
        // however in this case the HSREQ extension will not be attached,
        // so it will simply go the "old way".
        bool ok = prepareConnectionObjects(m_ConnRes, m_SrtHsSide, eout);
        if ( ok )
        {
            ok = interpretSrtHandshake(m_ConnRes, response, 0, 0);
            if (!ok && eout)
            {
                *eout = CUDTException(MJ_SETUP, MN_REJECTED, 0);
            }
        }
        if ( !ok )
            return CONN_REJECT;
    }

    // XXX Probably redundant - processSrtMsg_HSRSP should do it in both
    // HSv4 and HSv5 modes.
    handshakeDone();

    CInfoBlock ib;
    ib.m_iIPversion = m_iIPversion;
    CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
    if (m_pCache->lookup(&ib) >= 0)
    {
        m_iRTT = ib.m_iRTT;
        m_iBandwidth = ib.m_iBandwidth;
    }

    // And, I am connected too.
    m_bConnecting = false;
    m_bConnected = true;

    // register this socket for receiving data packets
    m_pRNode->m_bOnList = true;
    m_pRcvQueue->setNewEntry(this);

    // acknowledge the management module.
    s_UDTUnited.connect_complete(m_SocketID);

    // acknowledde any waiting epolls to write
    s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

    return CONN_ACCEPT;
}

// Rendezvous
bool CUDT::rendezvousSwitchState(ref_t<UDTRequestType> rsptype, ref_t<bool> needs_extension)
{
    UDTRequestType req = m_ConnRes.m_iReqType;
    bool has_extension = !!m_ConnRes.m_iType; // it holds flags, if no flags, there are no extensions.

    const HandshakeSide& hsd = m_SrtHsSide;
    // Note important possibilities that are considered here:

    // 1. The serial arrangement. This happens when one party has missed the
    // URQ_WAVEAHAND message, it sent its own URQ_WAVEAHAND message, and then the
    // firstmost message it received from the peer is URQ_CONCLUSION, as a response
    // for agent's URQ_WAVEAHAND.
    //
    // In this case, Agent switches to RDV_FINE state and Peer switches to RDV_ATTENTION state.
    //
    // 2. The parallel arrangement. This happens when the URQ_WAVEAHAND message sent
    // by both parties are almost in a perfect synch (a rare, but possible case). In this
    // case, both parties receive one another's URQ_WAVEAHAND message and both switch to
    // RDV_ATTENTION state.
    //
    // It's not possible to predict neither which arrangement will happen, or which
    // party will be RDV_FINE in case when the serial arrangement has happened. What
    // will actually happen will depend on random conditions.
    //
    // No matter this randomity, we have a limited number of possible conditions:
    //
    // Stating that "agent" is the party that has received the URQ_WAVEAHAND in whatever
    // arrangement, we are certain, that "agent" switched to RDV_ATTENTION, and peer:
    //
    // - switched to RDV_ATTENTION state (so, both are in the same state independently)
    // - switched to RDV_FINE state (so, the message interchange is actually more-less sequenced)
    //
    // In particular, there's no possibility of a situation that both are in RDV_FINE state
    // because the agent can switch to RDV_FINE state only if it received URQ_CONCLUSION from
    // the peer, while the peer could not send URQ_CONCLUSION without switching off RDV_WAVING
    // (actually to RDV_ATTENTION). There's also no exit to RDV_FINE from RDV_ATTENTION.

    needs_extension = false;

    string reason;

#if ENABLE_LOGGING

    LOGC(mglog.Debug) << "rendezvousSwitchState: HS: " << m_ConnRes.show();

    struct LogAtTheEnd
    {
        CHandShake::RendezvousState ost;
        UDTRequestType orq;
        const CHandShake::RendezvousState& nst;
        const UDTRequestType& nrq;
        bool& needext;
        string& reason;
        LogAtTheEnd(CHandShake::RendezvousState st, UDTRequestType rq,
                const CHandShake::RendezvousState& rst, const UDTRequestType& rrq, bool& needx, string& rsn):
            ost(st), orq(rq), nst(rst), nrq(rrq), needext(needx), reason(rsn) {}
        ~LogAtTheEnd()
        {
            LOGC(mglog.Debug) << "rendezvousSwitchState: STATE["
                << CHandShake::RdvStateStr(ost) << "->" << CHandShake::RdvStateStr(nst) << "] REQTYPE["
                << RequestTypeStr(orq) << "->" << RequestTypeStr(nrq) << "] "
                << (needext ? "HSREQ-ext" : "") << (reason == "" ? string() : "reason:" + reason);
        }
    } l_logend(m_RdvState, req, m_RdvState, rsptype, needs_extension, reason);

#endif

    switch (m_RdvState)
    {
    case CHandShake::RDV_INVALID: return false;

    case CHandShake::RDV_WAVING:
        {
            if ( req == URQ_WAVEAHAND )
            {
                m_RdvState = CHandShake::RDV_ATTENTION;

                // NOTE: if this->isWinner(), attach HSREQ
                rsptype = URQ_CONCLUSION;
                if ( hsd == HSD_INITIATOR )
                    needs_extension = true;
                return false;
            }

            if ( req == URQ_CONCLUSION )
            {
                m_RdvState = CHandShake::RDV_FINE;
                rsptype = URQ_CONCLUSION;

                needs_extension = true; // (see below - this needs to craft either HSREQ or HSRSP)
                // if this->isWinner(), then craft HSREQ for that response.
                // if this->isLoser(), then this packet should bring HSREQ, so craft HSRSP for the response.
                if ( hsd == HSD_RESPONDER )
                    return true;
                return false;
            }

        }
        reason = "WAVING -> WAVEAHAND or CONCLUSION";
        break;

    case CHandShake::RDV_ATTENTION:
        {
            if ( req == URQ_WAVEAHAND )
            {
                // This is only possible if the URQ_CONCLUSION sent to the peer
                // was lost on track. The peer is then simply unaware that the
                // agent has switched to ATTENTION state and continues sending
                // waveahands. In this case, just remain in ATTENTION state and
                // retry with URQ_CONCLUSION, as normally.
                rsptype = URQ_CONCLUSION;
                if ( hsd == HSD_INITIATOR )
                    needs_extension = true;
                return false;
            }

            if ( req == URQ_CONCLUSION )
            {
                // We have two possibilities here:
                //
                // WINNER (HSD_INITIATOR): send URQ_AGREEMENT
                if ( hsd == HSD_INITIATOR )
                {
                    // WINNER should get a response with HSRSP, otherwise this is kinda empty conclusion.
                    // If no HSRSP attached, stay in this state.
                    if (m_ConnRes.m_iType == 0)
                    {
                        LOGC(mglog.Debug) << "rendezvousSwitchState: {INITIATOR}[ATTENTION] awaits CONCLUSION+HSRSP, got CONCLUSION, remain in [ATTENTION]";
                        rsptype = URQ_CONCLUSION;
                        return false;
                    }
                    m_RdvState = CHandShake::RDV_CONNECTED;
                    rsptype = URQ_AGREEMENT;
                    return false;
                }

                // LOSER (HSD_RESPONDER): send URQ_CONCLUSION and attach HSRSP extension, then expect URQ_AGREEMENT
                if ( hsd == HSD_RESPONDER )
                {
                    m_RdvState = CHandShake::RDV_INITIATED;
                    rsptype = URQ_CONCLUSION;
                    return true;
                }
            }

            if ( req == URQ_AGREEMENT )
            {
                // This means that the peer has received our URQ_CONCLUSION, but
                // the agent missed the peer's URQ_CONCLUSION (received only initial
                // URQ_WAVEAHAND).
                if ( hsd == HSD_INITIATOR )
                {
                    // In this case the missed URQ_CONCLUSION was sent without extensions,
                    // whereas the peer received our URQ_CONCLUSION with HSREQ, and therefore
                    // it sent URQ_AGREEMENT already with HSRSP. This isn't a problem for
                    // us, we can go on with it, especially that the peer is already switched
                    // into CHandShake::RDV_CONNECTED state.
                    m_RdvState = CHandShake::RDV_CONNECTED;

                    // Both sides are connected, no need to send anything anymore.
                    rsptype = URQ_DONE;
                    return false;
                }

                if ( hsd == HSD_RESPONDER )
                {
                    // In this case the missed URQ_CONCLUSION was sent with extensions, so
                    // we have to request this once again. Send URQ_CONCLUSION in order to
                    // inform the other party that we need the conclusion message once again.
                    // The ATTENTION state should be maintained.
                    rsptype = URQ_CONCLUSION;
                    // This is a conclusion message to call for getting HSREQ, so no extensions.
                    return false;
                }
            }

        }
        reason = "ATTENTION -> WAVEAHAND(conclusion), CONCLUSION(agreement/conclusion), AGREEMENT (done/conclusion)";
        break;

    case CHandShake::RDV_FINE:
        {
            // In FINE state we can't receive URQ_WAVEAHAND because if the party has already
            // sent URQ_CONCLUSION, it's already in CHandShake::RDV_ATTENTION, and in this state it can
            // only send URQ_CONCLUSION, whereas when it isn't in CHandShake::RDV_ATTENTION, it couldn't
            // have sent URQ_CONCLUSION, and if it didn't, the agent wouldn't be in CHandShake::RDV_FINE state.

            if ( req == URQ_CONCLUSION )
            {
                // There's only one case when it should receive CONCLUSION in FINE state:
                // When it's the winner. If so, it should then contain HSREQ extension.
                // In case of loser, it shouldn't receive CONCLUSION at all - it should
                // receive AGREEMENT.

                // The winner case, received CONCLUSION + HSRSP - switch to CONNECTED and send AGREEMENT.
                // So, check first if HAS EXTENSION

                bool correct_switch = false;
                if ( hsd == HSD_INITIATOR && !has_extension )
                {
                    // Received REPEATED empty conclusion that has initially switched it into FINE state.
                    // To exit FINE state we need the CONCLUSION message with HSRSP.
                    LOGC(mglog.Debug) << "rendezvousSwitchState: {INITIATOR}[FINE] <CONCLUSION without HSRSP. Stay in [FINE], await CONCLUSION+HSRSP";
                }
                else if ( hsd == HSD_RESPONDER )
                {
                    // In FINE state the RESPONDER expects only to be sent AGREEMENT.
                    // It has previously received CONCLUSION in WAVING state and this has switched
                    // it to FINE state. That CONCLUSION message should have contained extension,
                    // so if this is a repeated CONCLUSION+HSREQ, it should be responded with
                    // CONCLUSION+HSRSP.
                    LOGC(mglog.Debug) << "rendezvousSwitchState: {RESPONDER}[FINE] <CONCLUSION. Stay in [FINE], await AGREEMENT";
                }
                else
                {
                    correct_switch = true;
                }

                if ( !correct_switch )
                {
                    rsptype = URQ_CONCLUSION;
                    needs_extension = true; // initiator should send HSREQ, responder HSRSP, in both cases extension is needed
                    return hsd == HSD_RESPONDER;
                }

                m_RdvState = CHandShake::RDV_CONNECTED;
                rsptype = URQ_AGREEMENT;
                return false;
            }

            if ( req == URQ_AGREEMENT )
            {
                // The loser case, the agreement was sent in response to conclusion that
                // already carried over the HSRSP extension.

                // There's a theoretical case when URQ_AGREEMENT can be received in case of
                // parallel arrangement, while the agent is already in CHandShake::RDV_CONNECTED state.
                // This will be dispatched in the main loop and discarded.

                m_RdvState = CHandShake::RDV_CONNECTED;
                rsptype = URQ_DONE;
                return false;
            }

        }

        reason = "FINE -> CONCLUSION(agreement), AGREEMENT(done)";
        break;
    case CHandShake::RDV_INITIATED:
        {
            // In this state we just wait for URQ_AGREEMENT, which should cause it to
            // switch to CONNECTED. No response required.
            if ( req == URQ_AGREEMENT )
            {
                m_RdvState = CHandShake::RDV_CONNECTED;
                rsptype = URQ_DONE;
                return false;
            }

            if ( req == URQ_CONCLUSION )
            {
                // Receiving conclusion in this state means that the other party
                // didn't get our conclusion, so send it again, the same as when
                // exiting the ATTENTION state.
                rsptype = URQ_CONCLUSION;
                if ( hsd == HSD_RESPONDER )
                {
                    LOGC(mglog.Debug) << "rendezvousSwitchState: {RESPONDER}[INITIATED] awaits AGREEMENT, got CONCLUSION, sending CONCLUSION+HSRSP";
                    needs_extension = true;
                    return true;
                }

                // Loser, initiated? This may only happen in parallel arrangement, where
                // the agent exchanges empty conclusion messages with the peer, simultaneously
                // exchanging HSREQ-HSRSP conclusion messages. Check if THIS message contained
                // HSREQ, and set responding HSRSP in that case.
                if ( m_ConnRes.m_iType == 0 )
                {
                    LOGC(mglog.Debug) << "rendezvousSwitchState: {INITIATOR}[INITIATED] awaits AGREEMENT, got empty CONCLUSION, responding empty CONCLUSION";
                    needs_extension = false;
                    return false;
                }

                LOGC(mglog.Debug) << "rendezvousSwitchState: {INITIATOR}[INITIATED] awaits AGREEMENT, got CONCLUSION+HSREQ, responding CONCLUSION+HSRSP";
                needs_extension = true;
                return true;
            }
        }

        reason = "INITIATED -> AGREEMENT(done)";
        break;

    case CHandShake::RDV_CONNECTED:
        // Do nothing. This theoretically should never happen.
        rsptype = URQ_DONE;
        return false;
    }

    LOGC(mglog.Debug) << "rendezvousSwitchState: INVALID STATE TRANSITION, result: INVALID";
    // All others are treated as errors
    m_RdvState = CHandShake::RDV_WAVING;
    rsptype = URQ_ERROR_INVALID;
    return false;
}

/*
* Timestamp-based Packet Delivery (TsbPd) thread
* This thread runs only if TsbPd mode is enabled
* Hold received packets until its time to 'play' them, at PktTimeStamp + TsbPdDelay.
*/
void* CUDT::tsbpd(void* param)
{
   CUDT* self = (CUDT*)param;

   THREAD_STATE_INIT("SRT Packet Delivery");

   CGuard::enterCS(self->m_RecvLock);
   self->m_bTsbPdAckWakeup = true;
   while (!self->m_bClosing)
   {
      CPacket* rdpkt = 0;
      uint64_t tsbpdtime = 0;
      bool rxready = false;

      CGuard::enterCS(self->m_AckLock);

#ifdef SRT_ENABLE_RCVBUFSZ_MAVG
      self->m_pRcvBuffer->updRcvAvgDataSize(CTimer::getTime());
#endif

#ifdef SRT_ENABLE_TLPKTDROP
      if (self->m_bTLPktDrop)
      {
          int32_t skiptoseqno = -1;
          bool passack = true; //Get next packet to wait for even if not acked

          rxready = self->m_pRcvBuffer->getRcvFirstMsg(tsbpdtime, passack, skiptoseqno, &rdpkt);
          /*
          * rxready:     packet at head of queue ready to play if true
          * tsbpdtime:   timestamp of packet at head of queue, ready or not. 0 if none.
          * passack:     ready head of queue not yet acknowledged if true
          * skiptoseqno: sequence number of packet at head of queue if ready to play but
          *              some preceeding packets are missing (need to be skipped). -1 if none. 
          */
          if (rxready)
          {
             /* Packet ready to play according to time stamp but... */
             int seqlen = CSeqNo::seqoff(self->m_iRcvLastSkipAck, skiptoseqno);

             if (skiptoseqno != -1 && seqlen > 0)
             {
                /* 
                * skiptoseqno != -1,
                * packet ready to play but preceeded by missing packets (hole).
                */

                /* Update drop/skip stats */
                self->m_iRcvDropTotal += seqlen;
                self->m_iTraceRcvDrop += seqlen;
                /* Estimate dropped/skipped bytes from average payload */
                int avgpayloadsz = self->m_pRcvBuffer->getRcvAvgPayloadSize();
                self->m_ullRcvBytesDropTotal += seqlen * avgpayloadsz;
                self->m_ullTraceRcvBytesDrop += seqlen * avgpayloadsz;

                self->unlose(self->m_iRcvLastSkipAck, CSeqNo::decseq(skiptoseqno)); //remove(from,to-inclusive)
                self->m_pRcvBuffer->skipData(seqlen);

                self->m_iRcvLastSkipAck = skiptoseqno;

                uint64_t now = CTimer::getTime();

                int64_t timediff = 0;
                if ( tsbpdtime )
                    timediff = int64_t(now) - int64_t(tsbpdtime);

                LOGC(tslog.Note) << self->CONID() << "tsbpd: DROPSEQ: up to seq=" << CSeqNo::decseq(skiptoseqno)
                    << " (" << seqlen << " packets) playable at " << logging::FormatTime(tsbpdtime) << " delayed "
                    << (timediff/1000) << "." << (timediff%1000) << " ms";

                tsbpdtime = 0; //Next sent ack will unblock
                rxready = false;
             }
             else if (passack)
             {
                /* Packets ready to play but not yet acknowledged (should occurs withing 10ms) */
                rxready = false;
                tsbpdtime = 0; //Next sent ack will unblock
             } /* else packet ready to play */
          } /* else packets not ready to play */
      } else
#endif /* SRT_ENABLE_TLPKTDROP */
      {
          rxready = self->m_pRcvBuffer->isRcvDataReady(tsbpdtime, &rdpkt);
      }
      CGuard::leaveCS(self->m_AckLock);

      if (rxready)
      {
          int seq=0;
          if ( rdpkt )
              seq = rdpkt->getSeqNo();
          LOGC(tslog.Debug) << self->CONID() << "tsbpd: PLAYING PACKET seq=" << seq << " (belated " << ((CTimer::getTime() - tsbpdtime)/1000.0) << "ms)";
         /*
         * There are packets ready to be delivered
         * signal a waiting "recv" call if there is any data available
         */
         if (self->m_bSynRecving)
         {
             pthread_cond_signal(&self->m_RecvDataCond);
         }
         /*
         * Set EPOLL_IN to wakeup any thread waiting on epoll
         */
         self->s_UDTUnited.m_EPoll.update_events(self->m_SocketID, self->m_sPollID, UDT_EPOLL_IN, true);
         tsbpdtime = 0;
      }

      if (tsbpdtime != 0)
      {
         /*
         * Buffer at head of queue is not ready to play.
         * Schedule wakeup when it will be.
         */
          self->m_bTsbPdAckWakeup = false;
          THREAD_PAUSED();
          timespec locktime;
          locktime.tv_sec = tsbpdtime / 1000000;
          locktime.tv_nsec = (tsbpdtime % 1000000) * 1000;
          int seq = 0;
          if ( rdpkt )
              seq = rdpkt->getSeqNo();
          uint64_t now = CTimer::getTime();
          LOGC(tslog.Debug) << self->CONID() << "tsbpd: FUTURE PACKET seq=" << seq << " T=" << logging::FormatTime(tsbpdtime) << " - waiting " << ((tsbpdtime - now)/1000.0) << "ms";
          pthread_cond_timedwait(&self->m_RcvTsbPdCond, &self->m_RecvLock, &locktime);
          THREAD_RESUMED();
      }
      else
      {
         /*
         * We have just signaled epoll; or
         * receive queue is empty; or
         * next buffer to deliver is not in receive queue (missing packet in sequence).
         *
         * Block until woken up by one of the following event:
         * - All ready-to-play packets have been pulled and EPOLL_IN cleared (then loop to block until next pkt time if any)
         * - New buffers ACKed
         * - Closing the connection
         */
         LOGC(tslog.Debug) << self->CONID() << "tsbpd: no data, scheduling wakeup at ack";
         self->m_bTsbPdAckWakeup = true;
         THREAD_PAUSED();
         pthread_cond_wait(&self->m_RcvTsbPdCond, &self->m_RecvLock);
         THREAD_RESUMED();
      }
   }
   CGuard::leaveCS(self->m_RecvLock);
   THREAD_EXIT();
   LOGC(tslog.Debug) << self->CONID() << "tsbpd: EXITING";
   return NULL;
}

bool CUDT::prepareConnectionObjects(const CHandShake& hs, HandshakeSide hsd, CUDTException* eout)
{
    // This will be lazily created due to being the common
    // code with HSv5 rendezvous, in which this will be run
    // in a little bit "randomly selected" moment, but must
    // be run once in the whole connection process.
    if (m_pSndBuffer)
    {
        LOGC(mglog.Debug) << "prepareConnectionObjects: (lazy) already created.";
        return true;
    }

    bool bidirectional = false;
    if ( hs.m_iVersion > HS_VERSION_UDT4 )
    {
        bidirectional = true; // HSv5 is always bidirectional
    }

    // HSD_DRAW is received only if this side is listener.
    // If this side is caller with HSv5, HSD_INITIATOR should be passed.
    // If this is a rendezvous connection with HSv5, the handshake role
    // is taken from m_SrtHsSide field.
    if ( hsd == HSD_DRAW )
    {
        if ( bidirectional )
        {
            hsd = HSD_RESPONDER;   // In HSv5, listener is always acceptor and caller always donor.
        }
        else
        {
            hsd = m_bDataSender ? HSD_INITIATOR : HSD_RESPONDER;
        }
    }

    try
    {
        m_pSndBuffer = new CSndBuffer(32, m_iPayloadSize);
        m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
        // after introducing lite ACK, the sndlosslist may not be cleared in time, so it requires twice space.
        m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
        m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
    }
    catch (...)
    {
        // Simply reject. 
        if ( eout )
        {
            *eout = CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
        }
        return false;
    }

    if (!createCrypter(hsd, bidirectional)) // Make sure CC is created (lazy)
        return false;

    return setupCC();
}

void CUDT::acceptAndRespond(const sockaddr* peer, CHandShake* hs, const CPacket& hspkt)
{
   LOGC(mglog.Debug) << "acceptAndRespond: setting up data according to handshake";

   CGuard cg(m_ConnectionLock);

   m_ullRcvPeerStartTime = 0; // will be set correctly at SRT HS

   // Uses the smaller MSS between the peers
   if (hs->m_iMSS > m_iMSS)
      hs->m_iMSS = m_iMSS;
   else
      m_iMSS = hs->m_iMSS;

   // exchange info for maximum flow window size
   m_iFlowWindowSize = hs->m_iFlightFlagSize;
   hs->m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;

   m_iPeerISN = hs->m_iISN;

   m_iRcvLastAck = hs->m_iISN;
#ifdef ENABLE_LOGGING
   m_iDebugPrevLastAck = m_iRcvLastAck;
#endif
#ifdef SRT_ENABLE_TLPKTDROP
   m_iRcvLastSkipAck = m_iRcvLastAck;
#endif /* SRT_ENABLE_TLPKTDROP */
   m_iRcvLastAckAck = hs->m_iISN;
   m_iRcvCurrSeqNo = hs->m_iISN - 1;

   m_PeerID = hs->m_iID;
   hs->m_iID = m_SocketID;

   // use peer's ISN and send it back for security check
   m_iISN = hs->m_iISN;

   m_iLastDecSeq = m_iISN - 1;
   m_iSndLastAck = m_iISN;
   m_iSndLastDataAck = m_iISN;
#ifdef SRT_ENABLE_TLPKTDROP
   m_iSndLastFullAck = m_iISN;
#endif /* SRT_ENABLE_TLPKTDROP */
   m_iSndCurrSeqNo = m_iISN - 1;
   m_iSndLastAck2 = m_iISN;
   m_ullSndLastAck2Time = CTimer::getTime();

   // this is a reponse handshake
   hs->m_iReqType = URQ_CONCLUSION;

   if ( hs->m_iVersion > HS_VERSION_UDT4 )
   {
       // The version is agreed; this code is executed only in case
       // when AGENT is listener. In this case, conclusion response
       // must always contain HSv5 handshake extensions.
       hs->m_extension = true;
   }

   // get local IP address and send the peer its IP address (because UDP cannot get local IP address)
   memcpy(m_piSelfIP, hs->m_piPeerIP, 16);
   CIPAddress::ntop(peer, hs->m_piPeerIP, m_iIPversion);

   m_iPktSize = m_iMSS - CPacket::UDP_HDR_SIZE;
   m_iPayloadSize = m_iPktSize - CPacket::HDR_SIZE;
   LOGC(mglog.Debug) << "acceptAndRespond: PAYLOAD SIZE: " << m_iPayloadSize;

   // Prepare all structures
   prepareConnectionObjects(*hs, HSD_DRAW, 0);
   // Since now you can use m_pCryptoControl

   CInfoBlock ib;
   ib.m_iIPversion = m_iIPversion;
   CInfoBlock::convert(peer, m_iIPversion, ib.m_piIP);
   if (m_pCache->lookup(&ib) >= 0)
   {
      m_iRTT = ib.m_iRTT;
      m_iBandwidth = ib.m_iBandwidth;
   }

   // This should extract the HSREQ and KMREQ portion in the handshake packet.
   // This could still be a HSv4 packet and contain no such parts, which will leave
   // this entity as "non-SRT-handshaken", and await further HSREQ and KMREQ sent
   // as UMSG_EXT.
   uint32_t kmdata[SRTDATA_MAXSIZE];
   size_t kmdatasize = SRTDATA_MAXSIZE;
   if ( !interpretSrtHandshake(*hs, hspkt, kmdata, &kmdatasize) )
   {
       LOGC(mglog.Debug) << "acceptAndRespond: interpretSrtHandshake failed - responding with REJECT.";
       // If the SRT Handshake extension was provided and wasn't interpreted
       // correctly, the connection should be rejected.
       //
       // Respond with the rejection message and return false from
       // this function so that the caller will know that this new
       // socket should be deleted.
       hs->m_iReqType = URQ_ERROR_REJECT;
       throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
   }

   m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(m_pPeerAddr, peer, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

   // And of course, it is connected.
   m_bConnected = true;

   // register this socket for receiving data packets
   m_pRNode->m_bOnList = true;
   m_pRcvQueue->setNewEntry(this);

   //send the response to the peer, see listen() for more discussions about this
   // XXX Here create CONCLUSION RESPONSE with:
   // - just the UDT handshake, if HS_VERSION_UDT4,
   // - if higher, the UDT handshake, the SRT HSRSP, the SRT KMRSP
   size_t size = m_iPayloadSize;
   // Allocate the maximum possible memory for an SRT payload.
   // This is a maximum you can send once.
   CPacket response;
   response.setControl(UMSG_HANDSHAKE);
   response.allocate(size);

   // This will serialize the handshake according to its current form.
   LOGC(mglog.Debug) << "acceptAndRespond: creating CONCLUSION response (HSv5: with HSRSP/KMRSP) buffer size=" << size;
   if (!createSrtHandshake(Ref(response), Ref(*hs), SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize))
   {
       throw CUDTException(MJ_SETUP, MN_REJECTED, 0);
   }

#if ENABLE_LOGGING
   {
       // To make sure what REALLY is being sent, parse back the handshake
       // data that have been just written into the buffer.
       CHandShake debughs;
       debughs.load_from(response.m_pcData, response.getLength());
       LOGC(mglog.Debug) << CONID() << "acceptAndRespond: sending HS to peer, reqtype="
           << RequestTypeStr(debughs.m_iReqType) << " version=" << debughs.m_iVersion
           << " (connreq:" << RequestTypeStr(m_ConnReq.m_iReqType)
           << "), target_socket=" << response.m_iID << ", my_socket=" << debughs.m_iID;
   }
#endif
   m_pSndQueue->sendto(peer, response);
}


// This function is required to be called when a caller receives an INDUCTION
// response from the listener and would like to create a CONCLUSION that includes
// the SRT handshake extension. This extension requires that the crypter object
// be created, but it's still too early for it to be completely configured.
// This function then precreates the object so that the handshake extension can
// be created, as this happens before the completion of the connection (and
// therefore configuration of the crypter object), which can only take place upon
// reception of CONCLUSION response from the listener.
bool CUDT::createCrypter(HandshakeSide side, bool bidirectional)
{
    // Lazy initialization
    if ( m_pCryptoControl )
        return true;

    m_pCryptoControl.reset(new CCryptoControl(this, m_SocketID));

    // XXX These below are a little bit controversial.
    // These data should probably be filled only upon
    // reception of the conclusion handshake - otherwise
    // they have outdated values.
    if ( bidirectional )
        m_bTwoWayData = true;

    m_pCryptoControl->setCryptoSecret(m_CryptoSecret);

    if ( bidirectional || m_bDataSender )
    {
        m_pCryptoControl->setCryptoKeylen(m_iSndCryptoKeyLen);
    }

    return m_pCryptoControl->init(side, bidirectional);
}

// This function was earlier part of the congestion control class
// intitialization as long as it existed. This functionality is
// now in CUDT class, and this is the initialization part for all
// of the congestion control runtime states.
bool CUDT::setupCC()
{
    // XXX Not sure about that. May happen that AGENT wants
    // tsbpd mode, but PEER doesn't, even in bidirectional mode.
    // This way, the reception side should get precedense.
    //if (bidirectional || m_bDataSender || m_bTwoWayData)
    //    m_bPeerTsbPd = m_bOPT_TsbPd;

#ifdef SRT_ENABLE_NAKREPORT
    /*
     * Enable receiver's Periodic NAK Reports
     */
    m_ullMinNakInt = m_iMinNakInterval * m_ullCPUFrequency;
#endif /* SRT_ENABLE_NAKREPORT */

    //m_pCryptoControl->m_iMSS = m_iMSS;
    m_dMaxCWndSize = m_iFlowWindowSize;
    //m_pCryptoControl->m_iSndCurrSeqNo = m_iSndCurrSeqNo;
    m_iRcvRate = m_iDeliveryRate;
    //m_pCryptoControl->m_iRTT = m_iRTT;
    //m_pCryptoControl->m_iBandwidth = m_iBandwidth;


    LOGC(mglog.Debug) << "setupCC: setting parameters: mss=" << m_iMSS
        << " maxCWNDSize/FlowWindowSize=" << m_iFlowWindowSize
        << " rcvrate=" << m_iDeliveryRate
        << " rtt=" << m_iRTT
        << " bw=" << m_iBandwidth;

    if (m_llMaxBW != 0)
    {
        setMaxBW(m_llMaxBW); //Bytes/sec
        m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
    }
    else if (m_llInputBW != 0)
    {
        setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
        m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
    }
    else
    {
        m_pSndBuffer->setInputRateSmpPeriod(500000); //Enable input rate sampling (fast start)
    }

    m_ullInterval = (uint64_t)(m_dPktSndPeriod * m_ullCPUFrequency);
    m_dCongestionWindow = m_dCWndSize;

    return true;
}

void CUDT::considerLegacySrtHandshake(uint64_t timebase)
{
    // Do a fast pre-check first - this simply declares that agent uses HSv5
    // and the legacy SRT Handshake is not to be done. Second check is whether
    // agent is sender (=initiator in HSv4).
    if ( m_SndHsRetryCnt == 0 || !m_bDataSender )
        return;

    uint64_t now = CTimer::getTime();
    if (timebase != 0)
    {
        // Then this should be done only if it's the right time,
        // the TSBPD mode is on, and when the counter is "still rolling".
        /*
         * SRT Handshake with peer:
         * If...
         * - we want TsbPd mode; and
         * - we have not tried more than CSRTCC_MAXRETRY times (peer may not be SRT); and
         * - and did not get answer back from peer
         * - last sent handshake req should have been replied (RTT*1.5 elapsed); and
         * then (re-)send handshake request.
         */
        if ( !isTsbPd() // tsbpd off = no HSREQ required
                || m_SndHsRetryCnt <= 0 // expired (actually sanity check, theoretically impossible to be <0)
                || timebase > now ) // too early
            return;
    }
    // If 0 timebase, it means that this is the initial sending with the very first
    // payload packet sent. Send only if this is still set to maximum+1 value.
    else if (m_SndHsRetryCnt < SRT_MAX_HSRETRY+1)
    {
        return;
    }

    m_SndHsRetryCnt--;
    m_SndHsLastTime = now;
    sendSrtMsg(SRT_CMD_HSREQ);
}

void CUDT::checkSndTimers(Whether2RegenKm regen)
{
    if (!m_bDataSender)
        return;

    considerLegacySrtHandshake(m_SndHsLastTime + m_iRTT*3/2);
    m_pCryptoControl->sendKeysToPeer(regen);

    /*
     * Readjust the max SndPeriod onACK (and onTimeout)
     */
    updatePktSndPeriod();
}

void CUDT::setMaxBW(int64_t maxbw)
{
    m_llSndMaxBW = maxbw > 0 ? maxbw : BW_INFINITE;
    updatePktSndPeriod();

#ifdef SRT_ENABLE_NOCWND
    /*
     * UDT default flow control should not trigger under normal SRT operation
     * UDT stops sending if the number of packets in transit (not acknowledged)
     * is larger than the congestion window.
     * Up to SRT 1.0.6, this value was set at 1000 pkts, which may be insufficient
     * for satellite links with ~1000 msec RTT and high bit rate.
     */
    // XXX Consider making this a socket option.
    m_dCWndSize = m_dMaxCWndSize;
#else
    m_dCWndSize = 1000;
#endif
}

void CUDT::addressAndSend(CPacket& pkt)
{
    pkt.m_iID = m_PeerID;
    pkt.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
    m_pSndQueue->sendto(m_pPeerAddr, pkt);
}


void CUDT::close()
{
   // NOTE: this function is called from within the garbage collector thread.

   if (!m_bOpened)
   {
      return;
   }

   LOGC(mglog.Debug) << CONID() << " - closing socket:";

   if (m_Linger.l_onoff != 0)
   {
      uint64_t entertime = CTimer::getTime();

      LOGC(mglog.Debug) << CONID() << " ... (linger)";
      while (!m_bBroken && m_bConnected && (m_pSndBuffer->getCurrBufSize() > 0) && (CTimer::getTime() - entertime < m_Linger.l_linger * 1000000ULL))
      {
         // linger has been checked by previous close() call and has expired
         if (m_ullLingerExpiration >= entertime)
            break;

         if (!m_bSynSending)
         {
            // if this socket enables asynchronous sending, return immediately and let GC to close it later
            if (m_ullLingerExpiration == 0)
               m_ullLingerExpiration = entertime + m_Linger.l_linger * 1000000ULL;

            return;
         }

         #ifndef WIN32
            timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;
            nanosleep(&ts, NULL);
         #else
            Sleep(1);
         #endif
      }
   }

   // remove this socket from the snd queue
   if (m_bConnected)
      m_pSndQueue->m_pSndUList->remove(this);

   /*
    * update_events below useless
    * removing usock for EPolls right after (remove_usocks) clears it (in other HAI patch).
    *
    * What is in EPoll shall be the responsibility of the application, if it want local close event,
    * it would remove the socket from the EPoll after close.
    */
   // trigger any pending IO events.
   s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_ERR, true);
   // then remove itself from all epoll monitoring
   try
   {
      for (set<int>::iterator i = m_sPollID.begin(); i != m_sPollID.end(); ++ i)
         s_UDTUnited.m_EPoll.remove_usock(*i, m_SocketID);
   }
   catch (...)
   {
   }

   // XXX What's this, could any of the above actions make it !m_bOpened?
   if (!m_bOpened)
   {
      return;
   }

   // Inform the threads handler to stop.
   m_bClosing = true;

   LOGC(mglog.Debug) << CONID() << "CLOSING STATE. Acquiring connection lock";

   CGuard cg(m_ConnectionLock);

   // Signal the sender and recver if they are waiting for data.
   releaseSynch();

   LOGC(mglog.Debug) << CONID() << "CLOSING, removing from listener/connector";

   if (m_bListening)
   {
      m_bListening = false;
      m_pRcvQueue->removeListener(this);
   }
   else if (m_bConnecting)
   {
       m_pRcvQueue->removeConnector(m_SocketID);
   }

   if (m_bConnected)
   {
      if (!m_bShutdown)
      {
          LOGC(mglog.Debug) << CONID() << "CLOSING - sending SHUTDOWN to the peer";
          sendCtrl(UMSG_SHUTDOWN);
      }

      m_pCryptoControl->close();

      // Store current connection information.
      CInfoBlock ib;
      ib.m_iIPversion = m_iIPversion;
      CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
      ib.m_iRTT = m_iRTT;
      ib.m_iBandwidth = m_iBandwidth;
      m_pCache->update(&ib);

      m_bConnected = false;
   }

   if ( m_bTsbPd  && !pthread_equal(m_RcvTsbPdThread, pthread_t()))
   {
       LOGC(mglog.Debug) << "CLOSING, joining TSBPD thread...";
       void* retval;
       int ret = pthread_join(m_RcvTsbPdThread, &retval);
       LOGC(mglog.Debug) << "... " << (ret == 0 ? "SUCCEEDED" : "FAILED");
   }

   LOGC(mglog.Debug) << "CLOSING, joining send/receive threads";

   // waiting all send and recv calls to stop
   CGuard sendguard(m_SendLock);
   CGuard recvguard(m_RecvLock);

   CGuard::enterCS(m_AckLock);
   /* Release CCryptoControl internals (crypto context) under AckLock in case decrypt is in progress */
   m_pCryptoControl.reset();
   CGuard::leaveCS(m_AckLock);

   m_lSrtVersion = SRT_DEF_VERSION;
   m_lPeerSrtVersion = SRT_VERSION_UNK;
   m_lMinimumPeerSrtVersion = SRT_VERSION_MAJ1;
   m_ullRcvPeerStartTime = 0;

   LOGC(mglog.Debug) << "CLOSING %" << m_SocketID << " - sync signal";
   //pthread_mutex_lock(&m_CloseSynchLock);
   pthread_cond_broadcast(&m_CloseSynchCond);
   //pthread_mutex_unlock(&m_CloseSynchLock);
   // CLOSED.
   m_bOpened = false;
}

int CUDT::send(const char* data, int len)
{
   if (m_iSockType == UDT_DGRAM)
      throw CUDTException(MJ_NOTSUP, MN_ISDGRAM, 0);

   // throw an exception if not connected
   if (m_bBroken || m_bClosing)
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
   else if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (len <= 0)
      return 0;

   CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
#if !defined(SRT_FIX_KEEPALIVE)
      m_ullLastRspTime = currtime;
#endif
#ifdef SRT_ENABLE_FASTREXMIT
      m_ullLastRspAckTime = currtime;
      m_iReXmitCount = 1;
#endif /* SRT_ENABLE_FASTREXMIT */
   }
   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      if (!m_bSynSending)
         throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);
      else
      {
         // wait here during a blocking sending
          pthread_mutex_lock(&m_SendBlockLock);
          if (m_iSndTimeOut < 0)
          {
              while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
                  pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
          }
          else
          {
              uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
              timespec locktime;

              locktime.tv_sec = exptime / 1000000;
              locktime.tv_nsec = (exptime % 1000000) * 1000;

              while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth && (CTimer::getTime() < exptime))
                  pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
          }
          pthread_mutex_unlock(&m_SendBlockLock);

         // check the connection status
         if (m_bBroken || m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
         else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
         else if (!m_bPeerHealth)
         {
            m_bPeerHealth = true;
            throw CUDTException(MJ_PEERERROR);
         }
      }
   }

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      if (m_iSndTimeOut >= 0)
         throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

      return 0;
   }

   int size = (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize;
   if (size > len)
      size = len;

   // record total time used for sending
   if (m_pSndBuffer->getCurrBufSize() == 0)
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sending list
   m_pSndBuffer->addBuffer(data, size);

   // insert this socket to snd list if it is not on the list yet
   m_pSndQueue->m_pSndUList->update(this, false);

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return size;
}

int CUDT::recv(char* data, int len)
{
   if (m_iSockType == UDT_DGRAM)
      throw CUDTException(MJ_NOTSUP, MN_ISDGRAM, 0);

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

   if (len <= 0)
      return 0;

   CGuard recvguard(m_RecvLock);

   if (!m_pRcvBuffer->isRcvDataReady())
   {
      if (!m_bSynRecving)
      {
         throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
      }
      else
      {
          /* Kick TsbPd thread to schedule next wakeup (if running) */
          if (m_iRcvTimeOut < 0)
          {
              while (!m_bBroken && m_bConnected && !m_bClosing && !m_pRcvBuffer->isRcvDataReady())
              {
                  //Do not block forever, check connection status each 1 sec.
                  uint64_t exptime = CTimer::getTime() + 1000000ULL;
                  timespec locktime;

                  locktime.tv_sec = exptime / 1000000;
                  locktime.tv_nsec = (exptime % 1000000) * 1000;
                  pthread_cond_timedwait(&m_RecvDataCond, &m_RecvLock, &locktime);
              }
          }
          else
          {
              uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000;
              timespec locktime;
              locktime.tv_sec = exptime / 1000000;
              locktime.tv_nsec = (exptime % 1000000) * 1000;

              while (!m_bBroken && m_bConnected && !m_bClosing && !m_pRcvBuffer->isRcvDataReady())
              {
                  pthread_cond_timedwait(&m_RecvDataCond, &m_RecvLock, &locktime);
                  if (CTimer::getTime() >= exptime)
                      break;
              }
          }
      }
   }

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

   int res = m_pRcvBuffer->readBuffer(data, len);

   /* Kick TsbPd thread to schedule next wakeup (if running) */
   if (m_bTsbPd)
   {
      LOGP(tslog.Debug, "Ping TSBPD thread to schedule wakeup");
      pthread_cond_signal(&m_RcvTsbPdCond);
   }


   if (!m_pRcvBuffer->isRcvDataReady())
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   if ((res <= 0) && (m_iRcvTimeOut >= 0))
      throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

   return res;
}

#ifdef SRT_ENABLE_SRCTIMESTAMP
int CUDT::sendmsg(const char* data, int len, int msttl, bool inorder, uint64_t srctime)
#else
int CUDT::sendmsg(const char* data, int len, int msttl, bool inorder)
#endif
{
#if defined(SRT_ENABLE_TLPKTDROP) || defined(SRT_ENABLE_ECN)
   bool bCongestion = false;
#endif /* SRT_ENABLE_ECN */

   if (m_iSockType == UDT_STREAM)
      throw CUDTException(MJ_NOTSUP, MN_ISSTREAM, 0);

   // throw an exception if not connected
   if (m_bBroken || m_bClosing)
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
   else if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (len <= 0)
      return 0;

   if (len > m_iSndBufSize * m_iPayloadSize)
      throw CUDTException(MJ_NOTSUP, MN_XSIZE, 0);

   CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
#if !defined(SRT_FIX_KEEPALIVE)
      m_ullLastRspTime = currtime;
#endif
#ifdef SRT_ENABLE_FASTREXMIT
      m_ullLastRspAckTime = currtime;
      m_iReXmitCount = 1;
#endif /* SRT_ENABLE_FASTREXMIT */
   }

#if defined(SRT_ENABLE_TLPKTDROP) || defined(SRT_ENABLE_ECN)
   if (m_bPeerTLPktDrop) 
   {
      int bytes, timespan;
      m_pSndBuffer->getCurrBufSize(bytes, timespan);

#ifdef SRT_ENABLE_TLPKTDROP
      // high threshold (msec) at tsbpd_delay plus sender/receiver reaction time (2 * 10ms)
      // Minimum value must accomodate an I-Frame (~8 x average frame size)
      // >>need picture rate or app to set min treshold
      // >>using 1 sec for worse case 1 frame using all bit budget.
      // picture rate would be useful in auto SRT setting for min latency
#define SRT_TLPKTDROP_MINTHRESHOLD  1000    // (msec)
      // XXX static const uint32_t SRT_TLPKTDROP_MINTHRESHOLD = 1000; // (msec)
      // XXX int msecThreshold = std::max(m_iPeerTsbPdDelay, SRT_TLPKTDROP_MINTHRESHOLD) + (2*CPacket::SYN_INTERVAL/1000);
      int msecThreshold = (m_iPeerTsbPdDelay > SRT_TLPKTDROP_MINTHRESHOLD ? m_iPeerTsbPdDelay : SRT_TLPKTDROP_MINTHRESHOLD)
                        + (2 * CPacket::SYN_INTERVAL / 1000);
      if (timespan > msecThreshold)
      {
         // protect packet retransmission
         CGuard::enterCS(m_AckLock);
         int dbytes;
         int dpkts = m_pSndBuffer->dropLateData(dbytes,  CTimer::getTime() - (msecThreshold * 1000));
         if (dpkts > 0) {
            m_iTraceSndDrop += dpkts;
            m_iSndDropTotal += dpkts;
            m_ullTraceSndBytesDrop += dbytes;
            m_ullSndBytesDropTotal += dbytes;

            int32_t realack = m_iSndLastDataAck;
            int32_t fakeack = CSeqNo::incseq(m_iSndLastDataAck, dpkts);

            m_iSndLastAck = fakeack;
            m_iSndLastDataAck = fakeack;
            m_pSndLossList->remove(CSeqNo::decseq(m_iSndLastDataAck));
            /* If we dropped packets not yet sent, advance current position */
            // THIS MEANS: m_iSndCurrSeqNo = MAX(m_iSndCurrSeqNo, m_iSndLastDataAck-1)
            if (CSeqNo::seqcmp(m_iSndCurrSeqNo, CSeqNo::decseq(m_iSndLastDataAck)) < 0)
            {
               m_iSndCurrSeqNo = CSeqNo::decseq(m_iSndLastDataAck);
            }
            LOGC(dlog.Debug).form("drop,now %llu,%d-%d seqs,%d pkts,%d bytes,%d ms",
                    (unsigned long long)CTimer::getTime(),
                    realack, m_iSndCurrSeqNo,
                    dpkts, dbytes, timespan);
         }
         bCongestion = true;
         CGuard::leaveCS(m_AckLock);
      } else
#endif /* SRT_ENABLE_TLPKTDROP */
      if (timespan > (m_iPeerTsbPdDelay/2))
      {
         LOGC(mglog.Debug).form("cong, NOW: %llu, BYTES %d, TMSPAN %d", (unsigned long long)CTimer::getTime(), bytes, timespan);
         bCongestion = true;
      }
   }
#endif /* SRT_ENABLE_TLPKTDROP || SRT_ENABLE_ECN */


   if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
   {
      //>>We should not get here if SRT_ENABLE_TLPKTDROP
      if (!m_bSynSending)
         throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);
      else
      {
          // wait here during a blocking sending
          pthread_mutex_lock(&m_SendBlockLock);
          if (m_iSndTimeOut < 0)
          {
              while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len))
                  pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
          }
          else
          {
              uint64_t exptime = CTimer::getTime() + m_iSndTimeOut * 1000ULL;
              timespec locktime;

              locktime.tv_sec = exptime / 1000000;
              locktime.tv_nsec = (exptime % 1000000) * 1000;

              while (!m_bBroken && m_bConnected && !m_bClosing && ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len) && (CTimer::getTime() < exptime))
                  pthread_cond_timedwait(&m_SendBlockCond, &m_SendBlockLock, &locktime);
          }
          pthread_mutex_unlock(&m_SendBlockLock);

         // check the connection status
         if (m_bBroken || m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
         else if (!m_bConnected)
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
      }
      /* 
      * The code below is to return ETIMEOUT when blocking mode could not get free buffer in time.
      * If no free buffer available in non-blocking mode, we alredy returned. If buffer availaible,
      * we test twice if this code is outside the else section.
      * This fix move it in the else (blocking-mode) section
      */
      if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
      {
         if (m_iSndTimeOut >= 0)
            throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

         // XXX Not sure if this was intended:
         // The 'len' exceeds the bytes left in the send buffer...
         // ... so we do nothing and return success???
          return 0;
      }
   }

   // record total time used for sending
   if (m_pSndBuffer->getCurrBufSize() == 0)
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sending list
#ifdef SRT_ENABLE_SRCTIMESTAMP
#ifdef SRT_ENABLE_CBRTIMESTAMP
   if (srctime == 0)
   {
      uint64_t currtime;
      CTimer::rdtsc(currtime);

      m_ullSndLastCbrTime = max(currtime, m_ullSndLastCbrTime + m_ullInterval);
      srctime = m_ullSndLastCbrTime / m_ullCPUFrequency;
   }
#endif
   m_pSndBuffer->addBuffer(data, len, msttl, inorder, srctime);
   LOGC(dlog.Debug) << CONID() << "sock:SENDING srctime: " << srctime << " DATA SIZE: " << len;

#else /* SRT_ENABLE_SRCTIMESTAMP */
   m_pSndBuffer->addBuffer(data, len, msttl, inorder);
#endif /* SRT_ENABLE_SRCTIMESTAMP */


   // insert this socket to the snd list if it is not on the list yet
#if defined(SRT_ENABLE_TLPKTDROP) || defined(SRT_ENABLE_ECN)
   m_pSndQueue->m_pSndUList->update(this, bCongestion ? true : false);
#else
   m_pSndQueue->m_pSndUList->update(this, false);
#endif

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

#ifdef SRT_ENABLE_ECN
   if (bCongestion)
      throw CUDTException(MJ_AGAIN, MN_CONGESTION, 0);
#endif /* SRT_ENABLE_ECN */
   return len;
}

int CUDT::recvmsg(char* data, int len)
{
#ifdef SRT_ENABLE_SRCTIMESTAMP
    uint64_t srctime;
    return(CUDT::recvmsg(data, len, srctime));
}

int CUDT::recvmsg(char* data, int len, uint64_t& srctime)
{
#endif /* SRT_ENABLE_SRCTIMESTAMP */
   if (m_iSockType == UDT_STREAM)
      throw CUDTException(MJ_NOTSUP, MN_ISSTREAM, 0);

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (len <= 0)
      return 0;

   CGuard recvguard(m_RecvLock);

   /* XXX DEBUG STUFF - enable when required
   char charbool[2] = {'0', '1'};
   char ptrn [] = "RECVMSG/BEGIN BROKEN 1 CONN 1 CLOSING 1 SYNCR 1 NMSG                                ";
   int pos [] = {21, 28, 38, 46, 53};
   ptrn[pos[0]] = charbool[m_bBroken];
   ptrn[pos[1]] = charbool[m_bConnected];
   ptrn[pos[2]] = charbool[m_bClosing];
   ptrn[pos[3]] = charbool[m_bSynRecving];
   int wrtlen = sprintf(ptrn + pos[4], "%d", m_pRcvBuffer->getRcvMsgNum());
   strcpy(ptrn + pos[4] + wrtlen, "\n");
   fputs(ptrn, stderr);
   // */

   if (m_bBroken || m_bClosing)
   {
      int res = m_pRcvBuffer->readMsg(data, len);

      /* Kick TsbPd thread to schedule next wakeup (if running) */
      if (m_bTsbPd)
         pthread_cond_signal(&m_RcvTsbPdCond);

      if (!m_pRcvBuffer->isRcvDataReady())
      {
         // read is not available any more
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
      }

      if (res == 0)
         throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
      else
         return res;
   }

   if (!m_bSynRecving)
   {

#ifdef SRT_ENABLE_SRCTIMESTAMP
      int res = m_pRcvBuffer->readMsg(data, len, srctime);
#else
      int res = m_pRcvBuffer->readMsg(data, len);
#endif
      if (res == 0)
      {
         // read is not available any more

         // Kick TsbPd thread to schedule next wakeup (if running)
         if (m_bTsbPd)
            pthread_cond_signal(&m_RcvTsbPdCond);

         // Shut up EPoll if no more messages in non-blocking mode
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
         throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
      }
      else
      {
         if (!m_pRcvBuffer->isRcvDataReady())
         {
            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
               pthread_cond_signal(&m_RcvTsbPdCond);

            // Shut up EPoll if no more messages in non-blocking mode
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);

            // After signaling the tsbpd for ready data, report the bandwidth.
            double bw = m_iBandwidth * m_iPayloadSize * 8.0 / 1000000.0;
            LOGC(mglog.Debug) << CONID() << "CURRENT BANDWIDTH: " << bw << "Mbps (" << m_iBandwidth << ")";
         }
         return res;
      }
   }

   int res = 0;
   bool timeout = false;
   //Do not block forever, check connection status each 1 sec.
   uint64_t recvtmo = m_iRcvTimeOut < 0 ? 1000 : m_iRcvTimeOut;

   do
   {
      if (!m_bBroken && m_bConnected && !m_bClosing && !timeout && (!m_pRcvBuffer->isRcvDataReady()))
      {
         /* Kick TsbPd thread to schedule next wakeup (if running) */
         if (m_bTsbPd)
         {
             LOGP(tslog.Debug, "recvmsg: KICK tsbpd()");
             pthread_cond_signal(&m_RcvTsbPdCond);
         }

         do
         {
             uint64_t exptime = CTimer::getTime() + (recvtmo * 1000ULL);
             timespec locktime;

             locktime.tv_sec = exptime / 1000000;
             locktime.tv_nsec = (exptime % 1000000) * 1000;
             if (pthread_cond_timedwait(&m_RecvDataCond, &m_RecvLock, &locktime) == ETIMEDOUT)
             {
                 if (!(m_iRcvTimeOut < 0))
                     timeout = true;
                 LOGP(tslog.Debug, "recvmsg: DATA COND: EXPIRED -- trying to get data anyway");
             }
             else
             {
                 LOGP(tslog.Debug, "recvmsg: DATA COND: KICKED.");
             }
         } while (!m_bBroken && m_bConnected && !m_bClosing && !timeout && (!m_pRcvBuffer->isRcvDataReady()));
      }

      /* XXX DEBUG STUFF - enable when required
      char charbool[2] = {'0', '1'};
      char ptrn [] = "RECVMSG/GO-ON BROKEN 1 CONN 1 CLOSING 1 TMOUT 1 NMSG                                ";
      int pos [] = {21, 28, 38, 46, 53};
      ptrn[pos[0]] = charbool[m_bBroken];
      ptrn[pos[1]] = charbool[m_bConnected];
      ptrn[pos[2]] = charbool[m_bClosing];
      ptrn[pos[3]] = charbool[timeout];
      int wrtlen = sprintf(ptrn + pos[4], "%d", m_pRcvBuffer->getRcvMsgNum());
      strcpy(ptrn + pos[4] + wrtlen, "\n");
      fputs(ptrn, stderr);
      // */

#ifdef SRT_ENABLE_SRCTIMESTAMP
      res = m_pRcvBuffer->readMsg(data, len, srctime);
#else
      res = m_pRcvBuffer->readMsg(data, len);
#endif

      if (m_bBroken || m_bClosing)
         throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
      else if (!m_bConnected)
         throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   } while ((res == 0) && !timeout);

   if (!m_pRcvBuffer->isRcvDataReady())
   {
       // Falling here means usually that res == 0 && timeout == true.
       // res == 0 would repeat the above loop, unless there was also a timeout.
       // timeout has interrupted the above loop, but with res > 0 this condition
       // wouldn't be satisfied.

       // read is not available any more

       // Kick TsbPd thread to schedule next wakeup (if running)
       if (m_bTsbPd)
       {
           LOGP(tslog.Debug, "recvmsg: KICK tsbpd() (buffer empty)");
           pthread_cond_signal(&m_RcvTsbPdCond);
       }

       // Shut up EPoll if no more messages in non-blocking mode
       s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   /* XXX DEBUG STUFF - enable when required
   {
       char ptrn [] = "RECVMSG/EXIT RES ? RCVTIMEOUT                ";
       char chartribool [3] = { '-', '0', '+' };
       int pos [] = { 17, 29 };
       ptrn[pos[0]] = chartribool[int(res >= 0) + int(res > 0)];
       sprintf(ptrn + pos[1], "%d\n", m_iRcvTimeOut);
       fputs(ptrn, stderr);
   }
   // */

   if ((res <= 0) && (m_iRcvTimeOut >= 0))
      throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

   return res;
}

int64_t CUDT::sendfile(fstream& ifs, int64_t& offset, int64_t size, int block)
{
   if (m_iSockType == UDT_DGRAM)
      throw CUDTException(MJ_NOTSUP, MN_ISDGRAM, 0);

   if (m_bBroken || m_bClosing)
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
   else if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (size <= 0)
      return 0;

   CGuard sendguard(m_SendLock);

   if (m_pSndBuffer->getCurrBufSize() == 0)
   {
      // delay the EXP timer to avoid mis-fired timeout
      uint64_t currtime;
      CTimer::rdtsc(currtime);
#if !defined(SRT_FIX_KEEPALIVE)
      m_ullLastRspTime = currtime;
#endif
#ifdef SRT_ENABLE_FASTREXMIT
      m_ullLastRspAckTime = currtime;
      m_iReXmitCount = 1;
#endif /* SRT_ENABLE_FASTREXMIT */
   }

   int64_t tosend = size;
   int unitsize;

   // positioning...
   try
   {
      ifs.seekg((streamoff)offset);
   }
   catch (...)
   {
       // XXX It would be nice to note that this is reported
       // by exception only if explicitly requested by setting
       // the exception flags in the stream.
      throw CUDTException(MJ_FILESYSTEM, MN_SEEKGFAIL);
   }

   // sending block by block
   while (tosend > 0)
   {
      if (ifs.fail())
         throw CUDTException(MJ_FILESYSTEM, MN_WRITEFAIL);

      if (ifs.eof())
         break;

      unitsize = int((tosend >= block) ? block : tosend);

      pthread_mutex_lock(&m_SendBlockLock);
      while (!m_bBroken && m_bConnected && !m_bClosing && (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize()) && m_bPeerHealth)
          pthread_cond_wait(&m_SendBlockCond, &m_SendBlockLock);
      pthread_mutex_unlock(&m_SendBlockLock);

      if (m_bBroken || m_bClosing)
         throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
      else if (!m_bConnected)
         throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
      else if (!m_bPeerHealth)
      {
         // reset peer health status, once this error returns, the app should handle the situation at the peer side
         m_bPeerHealth = true;
         throw CUDTException(MJ_PEERERROR);
      }

      // record total time used for sending
      if (m_pSndBuffer->getCurrBufSize() == 0)
         m_llSndDurationCounter = CTimer::getTime();

      int64_t sentsize = m_pSndBuffer->addBufferFromFile(ifs, unitsize);

      if (sentsize > 0)
      {
         tosend -= sentsize;
         offset += sentsize;
      }

      // insert this socket to snd list if it is not on the list yet
      m_pSndQueue->m_pSndUList->update(this, false);
   }

   if (m_iSndBufSize <= m_pSndBuffer->getCurrBufSize())
   {
      // write is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, false);
   }

   return size - tosend;
}

int64_t CUDT::recvfile(fstream& ofs, int64_t& offset, int64_t size, int block)
{
   if (m_iSockType == UDT_DGRAM)
      throw CUDTException(MJ_NOTSUP, MN_ISDGRAM, 0);

   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

   if (size <= 0)
      return 0;

   CGuard recvguard(m_RecvLock);

   int64_t torecv = size;
   int unitsize = block;
   int recvsize;

   // positioning...
   try
   {
      ofs.seekp((streamoff)offset);
   }
   catch (...)
   {
       // XXX It would be nice to note that this is reported
       // by exception only if explicitly requested by setting
       // the exception flags in the stream.
      throw CUDTException(MJ_FILESYSTEM, MN_SEEKPFAIL);
   }

   // receiving... "recvfile" is always blocking
   while (torecv > 0)
   {
      if (ofs.fail())
      {
         // send the sender a signal so it will not be blocked forever
         int32_t err_code = CUDTException::EFILE;
         sendCtrl(UMSG_PEERERROR, &err_code);

         throw CUDTException(MJ_FILESYSTEM, MN_WRITEFAIL);
      }

      pthread_mutex_lock(&m_RecvDataLock);
      while (!m_bBroken && m_bConnected && !m_bClosing && !m_pRcvBuffer->isRcvDataReady())
          pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
      pthread_mutex_unlock(&m_RecvDataLock);

      if (!m_bConnected)
         throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
      else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
         throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

      unitsize = int((torecv >= block) ? block : torecv);
      recvsize = m_pRcvBuffer->readBufferToFile(ofs, unitsize);

      if (recvsize > 0)
      {
         torecv -= recvsize;
         offset += recvsize;
      }
   }

   if (!m_pRcvBuffer->isRcvDataReady())
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   return size - torecv;
}

void CUDT::sample(CPerfMon* perf, bool clear)
{
   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   if (m_bBroken || m_bClosing)
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

   uint64_t currtime = CTimer::getTime();
   perf->msTimeStamp = (currtime - m_StartTime) / 1000;

   perf->pktSent = m_llTraceSent;
   perf->pktRecv = m_llTraceRecv;
   perf->pktSndLoss = m_iTraceSndLoss;
   perf->pktRcvLoss = m_iTraceRcvLoss;
   perf->pktRetrans = m_iTraceRetrans;
   perf->pktRcvRetrans = m_iTraceRcvRetrans;
   perf->pktSentACK = m_iSentACK;
   perf->pktRecvACK = m_iRecvACK;
   perf->pktSentNAK = m_iSentNAK;
   perf->pktRecvNAK = m_iRecvNAK;
   perf->usSndDuration = m_llSndDuration;
   perf->pktReorderDistance = m_iTraceReorderDistance;
   perf->pktRcvAvgBelatedTime = m_fTraceBelatedTime;
   perf->pktRcvBelated = m_iTraceRcvBelated;

   perf->pktSentTotal = m_llSentTotal;
   perf->pktRecvTotal = m_llRecvTotal;
   perf->pktSndLossTotal = m_iSndLossTotal;
   perf->pktRcvLossTotal = m_iRcvLossTotal;
   perf->pktRetransTotal = m_iRetransTotal;
   perf->pktSentACKTotal = m_iSentACKTotal;
   perf->pktRecvACKTotal = m_iRecvACKTotal;
   perf->pktSentNAKTotal = m_iSentNAKTotal;
   perf->pktRecvNAKTotal = m_iRecvNAKTotal;
   perf->usSndDurationTotal = m_llSndDurationTotal;

   double interval = double(currtime - m_LastSampleTime);

   perf->mbpsSendRate = double(m_llTraceSent) * m_iPayloadSize * 8.0 / interval;
   perf->mbpsRecvRate = double(m_llTraceRecv) * m_iPayloadSize * 8.0 / interval;

   perf->usPktSndPeriod = m_ullInterval / double(m_ullCPUFrequency);
   perf->pktFlowWindow = m_iFlowWindowSize;
   perf->pktCongestionWindow = (int)m_dCongestionWindow;
   perf->pktFlightSize = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) - 1;
   perf->msRTT = m_iRTT/1000.0;
   perf->mbpsBandwidth = m_iBandwidth * m_iPayloadSize * 8.0 / 1000000.0;

   if (pthread_mutex_trylock(&m_ConnectionLock) == 0)
   {
      perf->byteAvailSndBuf = (m_pSndBuffer == NULL) ? 0 
          : (m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iMSS;
      perf->byteAvailRcvBuf = (m_pRcvBuffer == NULL) ? 0 
          : m_pRcvBuffer->getAvailBufSize() * m_iMSS;

      pthread_mutex_unlock(&m_ConnectionLock);
   }
   else
   {
      perf->byteAvailSndBuf = 0;
      perf->byteAvailRcvBuf = 0;
   }

   if (clear)
   {
      m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
      m_llSndDuration = 0;
      m_iTraceRcvRetrans = 0;
      m_LastSampleTime = currtime;
   }
}

void CUDT::bstats(CBytePerfMon* perf, bool clear)
{
   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   if (m_bBroken || m_bClosing)
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

   /* 
   * RecvLock to protect consistency (pkts vs. bytes vs. timespan) of Recv buffer stats.
   * Send buffer stats protected in send buffer class
   */
   CGuard recvguard(m_RecvLock);

   uint64_t currtime = CTimer::getTime();
   perf->msTimeStamp = (currtime - m_StartTime) / 1000;

   perf->pktSent = m_llTraceSent;
   perf->pktRecv = m_llTraceRecv;
   perf->pktSndLoss = m_iTraceSndLoss;
   perf->pktRcvLoss = m_iTraceRcvLoss;
   perf->pktRetrans = m_iTraceRetrans;
   perf->pktSentACK = m_iSentACK;
   perf->pktRecvACK = m_iRecvACK;
   perf->pktSentNAK = m_iSentNAK;
   perf->pktRecvNAK = m_iRecvNAK;
   perf->usSndDuration = m_llSndDuration;
   perf->pktReorderDistance = m_iTraceReorderDistance;
   perf->pktRcvAvgBelatedTime = m_fTraceBelatedTime;
   perf->pktRcvBelated = m_iTraceRcvBelated;
   //>new
   /* perf byte counters include all headers (SRT+UDP+IP) */
   const int pktHdrSize = CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
   perf->byteSent = m_ullTraceBytesSent + (m_llTraceSent * pktHdrSize);
   perf->byteRecv = m_ullTraceBytesRecv + (m_llTraceRecv * pktHdrSize);
   perf->byteRetrans = m_ullTraceBytesRetrans + (m_iTraceRetrans * pktHdrSize);
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
   perf->byteRcvLoss = m_ullTraceRcvBytesLoss + (m_iTraceRcvLoss * pktHdrSize);
#endif

#ifdef SRT_ENABLE_TLPKTDROP
   perf->pktSndDrop = m_iTraceSndDrop;
   perf->pktRcvDrop = m_iTraceRcvDrop + m_iTraceRcvUndecrypt;
   perf->byteSndDrop = m_ullTraceSndBytesDrop + (m_iTraceSndDrop * pktHdrSize);
   perf->byteRcvDrop = m_ullTraceRcvBytesDrop + (m_iTraceRcvDrop * pktHdrSize) + m_ullTraceRcvBytesUndecrypt;
#else
   perf->pktSndDrop = 0;
   perf->pktRcvDrop = 0;
   perf->byteSndDrop = 0;
   perf->byteRcvDrop = 0;
#endif

   perf->pktRcvUndecrypt = m_iTraceRcvUndecrypt;
   perf->byteRcvUndecrypt = m_ullTraceRcvBytesUndecrypt;

   //<

   perf->pktSentTotal = m_llSentTotal;
   perf->pktRecvTotal = m_llRecvTotal;
   perf->pktSndLossTotal = m_iSndLossTotal;
   perf->pktRcvLossTotal = m_iRcvLossTotal;
   perf->pktRetransTotal = m_iRetransTotal;
   perf->pktSentACKTotal = m_iSentACKTotal;
   perf->pktRecvACKTotal = m_iRecvACKTotal;
   perf->pktSentNAKTotal = m_iSentNAKTotal;
   perf->pktRecvNAKTotal = m_iRecvNAKTotal;
   perf->usSndDurationTotal = m_llSndDurationTotal;
   //>new
   perf->byteSentTotal = m_ullBytesSentTotal + (m_llSentTotal * pktHdrSize);
   perf->byteRecvTotal = m_ullBytesRecvTotal + (m_llRecvTotal * pktHdrSize);
   perf->byteRetransTotal = m_ullBytesRetransTotal + (m_iRetransTotal * pktHdrSize);
#ifdef SRT_ENABLE_LOSTBYTESCOUNT
   perf->byteRcvLossTotal = m_ullRcvBytesLossTotal + (m_iRcvLossTotal * pktHdrSize);
#endif
#ifdef SRT_ENABLE_TLPKTDROP
   perf->pktSndDropTotal = m_iSndDropTotal;
   perf->pktRcvDropTotal = m_iRcvDropTotal + m_iRcvUndecryptTotal;
   perf->byteSndDropTotal = m_ullSndBytesDropTotal + (m_iSndDropTotal * pktHdrSize);
   perf->byteRcvDropTotal = m_ullRcvBytesDropTotal + (m_iRcvDropTotal * pktHdrSize) + m_ullRcvBytesUndecryptTotal;
#else
   perf->pktSndDropTotal = 0;
   perf->pktRcvDropTotal = 0;
   perf->byteSndDropTotal = 0;
   perf->byteRcvDropTotal = 0;
#endif
   perf->pktRcvUndecryptTotal = m_iRcvUndecryptTotal;
   perf->byteRcvUndecryptTotal = m_ullRcvBytesUndecryptTotal;
   //<

   double interval = double(currtime - m_LastSampleTime);

   //>mod
   perf->mbpsSendRate = double(perf->byteSent) * 8.0 / interval;
   perf->mbpsRecvRate = double(perf->byteRecv) * 8.0 / interval;
   //<

   perf->usPktSndPeriod = m_ullInterval / double(m_ullCPUFrequency);
   perf->pktFlowWindow = m_iFlowWindowSize;
   perf->pktCongestionWindow = (int)m_dCongestionWindow;
   perf->pktFlightSize = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) - 1;
   perf->msRTT = (double)m_iRTT/1000.0;
   //>new
   perf->msSndTsbPdDelay = m_bPeerTsbPd ? m_iPeerTsbPdDelay : 0;
   perf->msRcvTsbPdDelay = m_bTsbPd ? m_iTsbPdDelay : 0;
   perf->byteMSS = m_iMSS;
   perf->mbpsMaxBW = (double)m_llMaxBW * 8.0/1000000.0;
   /* Maintained by CC if auto maxBW (0) */
   if (m_llMaxBW == 0)
       perf->mbpsMaxBW = (double)m_llSndMaxBW * 8.0/1000000.0;
   //<
   uint32_t availbw = (uint64_t)(m_iBandwidth == 1 ? m_RcvTimeWindow.getBandwidth() : m_iBandwidth);

   perf->mbpsBandwidth = (double)(availbw * (m_iPayloadSize + pktHdrSize) * 8.0 / 1000000.0);

   if (pthread_mutex_trylock(&m_ConnectionLock) == 0)
   {
      if (m_pSndBuffer)
      {
         //new>
         #ifdef SRT_ENABLE_SNDBUFSZ_MAVG
         perf->pktSndBuf = m_pSndBuffer->getAvgBufSize(perf->byteSndBuf, perf->msSndBuf);
         #else
         perf->pktSndBuf = m_pSndBuffer->getCurrBufSize(perf->byteSndBuf, perf->msSndBuf);
         #endif
         perf->byteSndBuf += (perf->pktSndBuf * pktHdrSize);
         //<
         perf->byteAvailSndBuf = (m_iSndBufSize - perf->pktSndBuf) * m_iMSS;
      } else {
         perf->byteAvailSndBuf = 0;
         //new>
         perf->pktSndBuf = 0;
         perf->byteSndBuf = 0;
         perf->msSndBuf = 0;
         //<
      }

      if (m_pRcvBuffer)
      {
         perf->byteAvailRcvBuf = m_pRcvBuffer->getAvailBufSize() * m_iMSS;
         //new>
         #ifdef SRT_ENABLE_RCVBUFSZ_MAVG
         perf->pktRcvBuf = m_pRcvBuffer->getRcvAvgDataSize(perf->byteRcvBuf, perf->msRcvBuf);
         #else
         perf->pktRcvBuf = m_pRcvBuffer->getRcvDataSize(perf->byteRcvBuf, perf->msRcvBuf);
         #endif
         //<
      } else {
         perf->byteAvailRcvBuf = 0;
         //new>
         perf->pktRcvBuf = 0;
         perf->byteRcvBuf = 0;
         perf->msRcvBuf = 0;
         //<
      }

      pthread_mutex_unlock(&m_ConnectionLock);
   }
   else
   {
      perf->byteAvailSndBuf = 0;
      perf->byteAvailRcvBuf = 0;
      //new>
      perf->pktSndBuf = 0;
      perf->byteSndBuf = 0;
      perf->msSndBuf = 0;

      perf->byteRcvBuf = 0;
      perf->msRcvBuf = 0;
      //<
   }

   if (clear)
   {
#ifdef SRT_ENABLE_TLPKTDROP
      m_iTraceSndDrop        = 0;
      m_iTraceRcvDrop        = 0;
      m_ullTraceSndBytesDrop = 0;
      m_ullTraceRcvBytesDrop = 0;
#endif /* SRT_ENABLE_TLPKTDROP */
      m_iTraceRcvUndecrypt        = 0;
      m_ullTraceRcvBytesUndecrypt = 0;
      //new>
      m_ullTraceBytesSent = m_ullTraceBytesRecv = m_ullTraceBytesRetrans = 0;
      //<
      m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
      m_llSndDuration = 0;
      m_LastSampleTime = currtime;
   }
}

void CUDT::CCUpdate()
{
    if ((m_llMaxBW == 0) //Auto MaxBW
            &&  (m_llInputBW == 0) //No application provided input rate
            &&  m_pSndBuffer) //Internal input rate sampling
    {
        int period;
        int payloadsz; //CC will use its own average payload size
        int64_t maxbw = m_pSndBuffer->getInputRate(payloadsz, period); //Auto input rate

        /*
         * On blocked transmitter (tx full) and until connection closes,
         * auto input rate falls to 0 but there may be still lot of packet to retransmit
         * Calling setMaxBW with 0 will set maxBW to default (30Mbps) 
         * and sendrate skyrockets for retransmission.
         * Keep previously set maximum in that case (maxbw == 0).
         */
        if (maxbw != 0)
            setMaxBW((maxbw * (100 + m_iOverheadBW))/100); //Bytes/sec

        if ((m_llSentTotal > 2000) && (period < 5000000))
            m_pSndBuffer->setInputRateSmpPeriod(5000000); //5 sec period after fast start
    }
    m_ullInterval = (uint64_t)(m_dPktSndPeriod * m_ullCPUFrequency);
    m_dCongestionWindow = m_dCWndSize;

#if 0//debug
    static int callcnt = 0;
    if (!(callcnt++ % 250)) fprintf(stderr, "SndPeriod=%llu\n", (unsigned long long)m_ullInterval/m_ullCPUFrequency);
#endif
}

void CUDT::initSynch()
{
      pthread_mutex_init(&m_SendBlockLock, NULL);
      pthread_cond_init(&m_SendBlockCond, NULL);
      pthread_mutex_init(&m_RecvDataLock, NULL);
      pthread_cond_init(&m_RecvDataCond, NULL);
      pthread_mutex_init(&m_SendLock, NULL);
      pthread_mutex_init(&m_RecvLock, NULL);
      pthread_mutex_init(&m_RcvLossLock, NULL);
      pthread_mutex_init(&m_AckLock, NULL);
      pthread_mutex_init(&m_ConnectionLock, NULL);
      memset(&m_RcvTsbPdThread, 0, sizeof m_RcvTsbPdThread);
      pthread_cond_init(&m_RcvTsbPdCond, NULL);

      pthread_mutex_init(&m_CloseSynchLock, NULL);
      pthread_cond_init(&m_CloseSynchCond, NULL);
}

void CUDT::destroySynch()
{
      pthread_mutex_destroy(&m_SendBlockLock);
      pthread_cond_destroy(&m_SendBlockCond);
      pthread_mutex_destroy(&m_RecvDataLock);
      pthread_cond_destroy(&m_RecvDataCond);
      pthread_mutex_destroy(&m_SendLock);
      pthread_mutex_destroy(&m_RecvLock);
      pthread_mutex_destroy(&m_RcvLossLock);
      pthread_mutex_destroy(&m_AckLock);
      pthread_mutex_destroy(&m_ConnectionLock);
      pthread_cond_destroy(&m_RcvTsbPdCond);

      pthread_mutex_destroy(&m_CloseSynchLock);
      pthread_cond_destroy(&m_CloseSynchCond);
}

void CUDT::releaseSynch()
{
    // wake up user calls
    pthread_mutex_lock(&m_SendBlockLock);
    pthread_cond_signal(&m_SendBlockCond);
    pthread_mutex_unlock(&m_SendBlockLock);

    pthread_mutex_lock(&m_SendLock);
    pthread_mutex_unlock(&m_SendLock);

    pthread_mutex_lock(&m_RecvDataLock);
    pthread_cond_signal(&m_RecvDataCond);
    pthread_mutex_unlock(&m_RecvDataLock);

    pthread_mutex_lock(&m_RecvLock);
    pthread_cond_signal(&m_RcvTsbPdCond);
    pthread_mutex_unlock(&m_RecvLock);
    if (!pthread_equal(m_RcvTsbPdThread, pthread_t())) 
    {
        pthread_join(m_RcvTsbPdThread, NULL);
        m_RcvTsbPdThread = pthread_t();
    }
    pthread_mutex_lock(&m_RecvLock);
    pthread_mutex_unlock(&m_RecvLock);
}

#if ENABLE_LOGGING
static void DebugAck(int prev, int ack, string CONID)
{
    if ( !prev )
    {
        LOGC(mglog.Debug).form("ACK %d", ack);
        return;
    }

    prev = CSeqNo::incseq(prev);
    int diff = CSeqNo::seqcmp(ack, prev);
    if ( diff < 0 )
    {
        LOGC(mglog.Error).form("ACK %d-%d (%d)", prev, ack, 1+CSeqNo::seqcmp(ack, prev));
        return;
    }

    bool shorted = diff > 100; // sanity
    if ( shorted )
        ack = CSeqNo::incseq(prev, 100);

    ostringstream ackv;
    for (; prev != ack; prev = CSeqNo::incseq(prev))
        ackv << prev << " ";
    if ( shorted )
        ackv << "...";
    LOGC(mglog.Debug) << CONID << "ACK (" << (diff+1) << "): " << ackv.str() << ack;
}
#else
static inline void DebugAck(int, int, string) {}
#endif

void CUDT::sendCtrl(UDTMessageType pkttype, void* lparam, void* rparam, int size)
{
   CPacket ctrlpkt;
   uint64_t currtime;
   CTimer::rdtsc(currtime);

   ctrlpkt.m_iTimeStamp = int(currtime/m_ullCPUFrequency - m_StartTime);

   int nbsent = 0;
   int local_prevack = 0;

#if ENABLE_LOGGING
   struct SaveBack
   {
       int& target;
       const int& source;

       ~SaveBack()
       {
           target = source;
       }
   } l_saveback = { m_iDebugPrevLastAck, m_iRcvLastAck };
   (void)l_saveback; //kill compiler warning: unused variable `l_saveback` [-Wunused-variable]

   local_prevack = m_iDebugPrevLastAck;
#endif

   switch (pkttype)
   {
   case UMSG_ACK: //010 - Acknowledgement
      {
      int32_t ack;

      // If there is no loss, the ACK is the current largest sequence number plus 1;
      // Otherwise it is the smallest sequence number in the receiver loss list.
      if (m_pRcvLossList->getLossLength() == 0)
         ack = CSeqNo::incseq(m_iRcvCurrSeqNo);
      else
         ack = m_pRcvLossList->getFirstLostSeq();

      if (m_iRcvLastAckAck == ack)
         break;

      // send out a lite ACK
      // to save time on buffer processing and bandwidth/AS measurement, a lite ACK only feeds back an ACK number
      if (size == SEND_LITE_ACK)
      {
         ctrlpkt.pack(pkttype, NULL, &ack, size);
         ctrlpkt.m_iID = m_PeerID;
         nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);
         DebugAck(local_prevack, ack, CONID());
         break;
      }

      uint64_t currtime;
      CTimer::rdtsc(currtime);

      // There are new received packets to acknowledge, update related information.
#ifdef SRT_ENABLE_TLPKTDROP
      /* tsbpd thread may also call ackData when skipping packet so protect code */
      CGuard::enterCS(m_AckLock);
#endif

      // IF ack > m_iRcvLastAck
      if (CSeqNo::seqcmp(ack, m_iRcvLastAck) > 0)
      {
         int acksize = CSeqNo::seqoff(m_iRcvLastSkipAck, ack);

         m_iRcvLastAck = ack;
#ifdef SRT_ENABLE_TLPKTDROP
         m_iRcvLastSkipAck = ack;

         // XXX Unknown as to whether it matters.
         // This if (acksize) causes that ackData() won't be called.
         // With size == 0 it wouldn't do anything except calling CTimer::triggerEvent().
         // This, again, signals the condition, CTimer::m_EventCond.
         // This releases CTimer::waitForEvent() call used in CUDTUnited::selectEx().
         // Preventing to call this on zero size makes sense, if it prevents false alerts.
         if (acksize)
             m_pRcvBuffer->ackData(acksize);
         CGuard::leaveCS(m_AckLock);
#else
         m_pRcvBuffer->ackData(acksize);
#endif

         // If TSBPD is enabled, then INSTEAD OF signaling m_RecvDataCond,
         // signal m_RcvTsbPdCond. This will kick in the tsbpd thread, which
         // will signal m_RecvDataCond when there's time to play particular
         // data packet.

         if (m_bTsbPd)
         {
             /* Newly acknowledged data, signal TsbPD thread */
             pthread_mutex_lock(&m_RecvLock);
             if (m_bTsbPdAckWakeup)
                pthread_cond_signal(&m_RcvTsbPdCond);
             pthread_mutex_unlock(&m_RecvLock);
         }
         else
         {
             if (m_bSynRecving)
             {
                 // signal a waiting "recv" call if there is any data available
                 pthread_mutex_lock(&m_RecvDataLock);
                 pthread_cond_signal(&m_RecvDataCond);
                 pthread_mutex_unlock(&m_RecvDataLock);
             }
             // acknowledge any waiting epolls to read
             s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
         }
#ifdef SRT_ENABLE_TLPKTDROP
         CGuard::enterCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
      }
      else if (ack == m_iRcvLastAck)
      {
         // If the ACK was just sent already AND elapsed time did not exceed RTT, 
         if ((currtime - m_ullLastAckTime) < ((m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency)) 
         {
#ifdef SRT_ENABLE_TLPKTDROP
            CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
            break;
         }
      }
      else 
      {
          // Not possible (m_iRcvCurrSeqNo+1 < m_iRcvLastAck ?)
#ifdef SRT_ENABLE_TLPKTDROP
         CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
         break;
      }

      // [[using assert( ack >= m_iRcvLastAck && is_periodic_ack ) ]]

      // Send out the ACK only if has not been received by the sender before
      if (CSeqNo::seqcmp(m_iRcvLastAck, m_iRcvLastAckAck) > 0)
      {
         // NOTE: The BSTATS feature turns on extra fields above size 6
         // also known as ACKD_TOTAL_SIZE_VER100. 
         int32_t data[ACKD_TOTAL_SIZE];

         // Pay no attention to this stupidity. CAckNo::incack does exactly
         // the same thing as CSeqNo::incseq - and it wouldn't work otherwise.
         m_iAckSeqNo = CAckNo::incack(m_iAckSeqNo);
         data[ACKD_RCVLASTACK] = m_iRcvLastAck;
         data[ACKD_RTT] = m_iRTT;
         data[ACKD_RTTVAR] = m_iRTTVar;
         data[ACKD_BUFFERLEFT] = m_pRcvBuffer->getAvailBufSize();
         // a minimum flow window of 2 is used, even if buffer is full, to break potential deadlock
         if (data[ACKD_BUFFERLEFT] < 2)
            data[ACKD_BUFFERLEFT] = 2;

		 if (currtime - m_ullLastAckTime > m_ullSYNInt)
		 {
			 int rcvRate;
			 uint32_t version = 0;
			 int ctrlsz = ACKD_TOTAL_SIZE_VER100 * ACKD_FIELD_SIZE; // Minimum required size

            data[ACKD_RCVSPEED] = m_RcvTimeWindow.getPktRcvSpeed(rcvRate);
            data[ACKD_BANDWIDTH] = m_RcvTimeWindow.getBandwidth();

            version = m_lPeerSrtVersion;
            //>>Patch while incompatible (1.0.2) receiver floating around
            if ( version == SrtVersion(1, 0, 2) )
            {
                data[ACKD_RCVRATE] = rcvRate; //bytes/sec
                data[ACKD_XMRATE] = data[ACKD_BANDWIDTH] * m_iPayloadSize; //bytes/sec
                ctrlsz = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER102;
            }
            else if (version >= SrtVersion(1, 0, 3))
            {
                data[ACKD_RCVRATE] = rcvRate; //bytes/sec
                ctrlsz = ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_VER101;

            }
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ctrlsz);
            CTimer::rdtsc(m_ullLastAckTime);
         }
         else
         {
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_UDTBASE);
         }

         ctrlpkt.m_iID = m_PeerID;
         ctrlpkt.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
         nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);
         DebugAck(local_prevack, ack, CONID());

         m_ACKWindow.store(m_iAckSeqNo, m_iRcvLastAck);

         ++ m_iSentACK;
         ++ m_iSentACKTotal;
      }
#ifdef SRT_ENABLE_TLPKTDROP
      CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
      break;
      }

   case UMSG_ACKACK: //110 - Acknowledgement of Acknowledgement
      ctrlpkt.pack(pkttype, lparam);
      ctrlpkt.m_iID = m_PeerID;
      nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case UMSG_LOSSREPORT: //011 - Loss Report
      {
          // Explicitly defined lost sequences 
          if (rparam)
          {
              int32_t* lossdata = (int32_t*)rparam;

              size_t bytes = sizeof(*lossdata)*size;
              ctrlpkt.pack(pkttype, NULL, lossdata, bytes);

              ctrlpkt.m_iID = m_PeerID;
              nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

              ++ m_iSentNAK;
              ++ m_iSentNAKTotal;
          }
          // Call with no arguments - get loss list from internal data.
          else if (m_pRcvLossList->getLossLength() > 0)
          {
              // this is periodically NAK report; make sure NAK cannot be sent back too often

              // read loss list from the local receiver loss list
              int32_t* data = new int32_t[m_iPayloadSize / 4];
              int losslen;
              m_pRcvLossList->getLossArray(data, losslen, m_iPayloadSize / 4);

              if (0 < losslen)
              {
                  ctrlpkt.pack(pkttype, NULL, data, losslen * 4);
                  ctrlpkt.m_iID = m_PeerID;
                  nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

                  ++ m_iSentNAK;
                  ++ m_iSentNAKTotal;
              }

              delete [] data;
          }

          // update next NAK time, which should wait enough time for the retansmission, but not too long
          m_ullNAKInt = (m_iRTT + 4 * m_iRTTVar) * m_ullCPUFrequency;
#ifdef SRT_ENABLE_NAKREPORT
          /*
           * duB:
           * The RTT accounts for the time for the last NAK to reach sender and start resending lost pkts.
           * The rcv_speed add the time to resend all the pkts in the loss list.
           * 
           * For realtime Transport Stream content, pkts/sec is not a good indication of time to transmit
           * since packets are not filled to m_iMSS and packet size average is lower than (7*188)
           * for low bit rates.
           * If NAK report is lost, another cycle (RTT) is requred which is bad for low latency so we
           * accelerate the NAK Reports frequency, at the cost of possible duplicate resend.
           * Finally, the UDT4 native minimum NAK interval (m_ullMinNakInt) is 300 ms which is too high
           * (~10 i30 video frames) to maintain low latency.
           */
          m_ullNAKInt /= m_iNakReportAccel;
#else
      int rcv_speed = m_RcvTimeWindow.getPktRcvSpeed();
          if (rcv_speed > 0)
              m_ullNAKInt += (m_pRcvLossList->getLossLength() * 1000000ULL / rcv_speed) * m_ullCPUFrequency;
#endif
          if (m_ullNAKInt < m_ullMinNakInt)
              m_ullNAKInt = m_ullMinNakInt;

          break;
      }

   case UMSG_CGWARNING: //100 - Congestion Warning
      ctrlpkt.pack(pkttype);
      ctrlpkt.m_iID = m_PeerID;
      nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      CTimer::rdtsc(m_ullLastWarningTime);

      break;

   case UMSG_KEEPALIVE: //001 - Keep-alive
      ctrlpkt.pack(pkttype);
      ctrlpkt.m_iID = m_PeerID;
      nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case UMSG_HANDSHAKE: //000 - Handshake
      ctrlpkt.pack(pkttype, NULL, rparam, sizeof(CHandShake));
      ctrlpkt.m_iID = m_PeerID;
      nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case UMSG_SHUTDOWN: //101 - Shutdown
      ctrlpkt.pack(pkttype);
      ctrlpkt.m_iID = m_PeerID;
      nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case UMSG_DROPREQ: //111 - Msg drop request
      ctrlpkt.pack(pkttype, lparam, rparam, 8);
      ctrlpkt.m_iID = m_PeerID;
      nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case UMSG_PEERERROR: //1000 - acknowledge the peer side a special error
      ctrlpkt.pack(pkttype, lparam);
      ctrlpkt.m_iID = m_PeerID;
      nbsent = m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);

      break;

   case UMSG_EXT: //0x7FFF - Resevered for future use
      break;

   default:
      break;
   }
#ifdef SRT_FIX_KEEPALIVE
   if (nbsent)
      m_ullLastSndTime = currtime;
#endif
}

void CUDT::processCtrl(CPacket& ctrlpkt)
{
   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;
   bool using_rexmit_flag = m_bPeerRexmitFlag;

   LOGC(mglog.Debug) << CONID() << "incoming UMSG:" << ctrlpkt.getType() << " ("
       << MessageTypeStr(ctrlpkt.getType(), ctrlpkt.getExtendedType()) << ") socket=%" << ctrlpkt.m_iID;

   switch (ctrlpkt.getType())
   {
   case UMSG_ACK: //010 - Acknowledgement
      {
      int32_t ack;
      int32_t* ackdata = (int32_t*)ctrlpkt.m_pcData;

      // process a lite ACK
      if (ctrlpkt.getLength() == SEND_LITE_ACK)
      {
         ack = *ackdata;
         if (CSeqNo::seqcmp(ack, m_iSndLastAck) >= 0)
         {
            m_iFlowWindowSize -= CSeqNo::seqoff(m_iSndLastAck, ack);
            LOGC(mglog.Debug) << CONID() << "ACK covers: " << m_iSndLastDataAck << " - " << ack << " [ACK=" << m_iSndLastAck << "] (FLW: " << m_iFlowWindowSize << ") [LITE]";

            m_iSndLastAck = ack;
#ifdef SRT_ENABLE_FASTREXMIT
            m_ullLastRspAckTime = currtime;
            m_iReXmitCount = 1;       // Reset re-transmit count since last ACK
#endif /* SRT_ENABLE_FASTREXMIT */
         }

         break;
      }

       // read ACK seq. no.
      ack = ctrlpkt.getAckSeqNo();

      // send ACK acknowledgement
      // number of ACK2 can be much less than number of ACK
      uint64_t now = CTimer::getTime();
      if ((now - m_ullSndLastAck2Time > (uint64_t)CPacket::SYN_INTERVAL) || (ack == m_iSndLastAck2))
      {
         sendCtrl(UMSG_ACKACK, &ack);
         m_iSndLastAck2 = ack;
         m_ullSndLastAck2Time = now;
      }

      // Got data ACK
      ack = ackdata[ACKD_RCVLASTACK];

#ifdef SRT_ENABLE_TLPKTDROP
      // protect packet retransmission
      CGuard::enterCS(m_AckLock);

      // check the validation of the ack
      int seqdiff = CSeqNo::seqcmp(ack, CSeqNo::incseq(m_iSndCurrSeqNo));
      if (seqdiff> 0)
      {
         CGuard::leaveCS(m_AckLock);
         //this should not happen: attack or bug
         LOGC(glog.Error) << CONID() << "ATTACK/ISE: incoming ack seq " << ack << " exceeds current " << m_iSndCurrSeqNo << " by " << seqdiff << "!";
         m_bBroken = true;
         m_iBrokenCounter = 0;
         break;
      }

      if (CSeqNo::seqcmp(ack, m_iSndLastAck) >= 0)
      {
         // Update Flow Window Size, must update before and together with m_iSndLastAck
         m_iFlowWindowSize = ackdata[ACKD_BUFFERLEFT];
         m_iSndLastAck = ack;
#ifdef SRT_ENABLE_FASTREXMIT
         m_ullLastRspAckTime = currtime;
         m_iReXmitCount = 1;       // Reset re-transmit count since last ACK
#endif /* SRT_ENABLE_FASTREXMIT */
      }

      /* 
      * We must not ignore full ack received by peer
      * if data has been artificially acked by late packet drop.
      * Therefore, a distinct ack state is used for received Ack (iSndLastFullAck)
      * and ack position in send buffer (m_iSndLastDataAck).
      * Otherwise, when severe congestion causing packet drops (and m_iSndLastDataAck update)
      * occures, we drop received acks (as duplicates) and do not update stats like RTT,
      * which may go crazy and stay there, preventing proper stream recovery.
      */

      if (CSeqNo::seqoff(m_iSndLastFullAck, ack) <= 0)
      {
         // discard it if it is a repeated ACK
         CGuard::leaveCS(m_AckLock);
         break;
      }
      m_iSndLastFullAck = ack;

      int offset = CSeqNo::seqoff(m_iSndLastDataAck, ack);
      // IF distance between m_iSndLastDataAck and ack is nonempty...
      if (offset > 0) {
          // acknowledge the sending buffer (remove data that predate 'ack')
          m_pSndBuffer->ackData(offset);

          // record total time used for sending
          m_llSndDuration += currtime - m_llSndDurationCounter;
          m_llSndDurationTotal += currtime - m_llSndDurationCounter;
          m_llSndDurationCounter = currtime;

          LOGC(mglog.Debug) << CONID() << "ACK covers: " << m_iSndLastDataAck << " - " << ack << " [ACK=" << m_iSndLastAck << "] BUFr=" << m_iFlowWindowSize
              << " RTT=" << ackdata[ACKD_RTT] << " RTT*=" << ackdata[ACKD_RTTVAR]
              << " BW=" << ackdata[ACKD_BANDWIDTH] << " Vrec=" << ackdata[ACKD_RCVSPEED];
          // update sending variables
          m_iSndLastDataAck = ack;

          // remove any loss that predates 'ack' (not to be considered loss anymore)
          m_pSndLossList->remove(CSeqNo::decseq(m_iSndLastDataAck));
      }

#else /* SRT_ENABLE_TLPKTDROP */

      // check the validation of the ack
      if (CSeqNo::seqcmp(ack, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0)
      {
         //this should not happen: attack or bug
         m_bBroken = true;
         m_iBrokenCounter = 0;
         break;
      }

      if (CSeqNo::seqcmp(ack, m_iSndLastAck) >= 0)
      {
         // Update Flow Window Size, must update before and together with m_iSndLastAck
         m_iFlowWindowSize = ackdata[ACKD_BUFFERLEFT];
         m_iSndLastAck = ack;
#ifdef SRT_ENABLE_FASTREXMIT
         m_ullLastRspAckTime = currtime;
         m_iReXmitCount = 1;       // Reset re-transmit count since last ACK
#endif /* SRT_ENABLE_FASTREXMIT */
      }

      // protect packet retransmission
      CGuard::enterCS(m_AckLock);

      int offset = CSeqNo::seqoff(m_iSndLastDataAck, ack);
      if (offset <= 0)
      {
         // discard it if it is a repeated ACK
         CGuard::leaveCS(m_AckLock);
         break;
      }

      // acknowledge the sending buffer
      m_pSndBuffer->ackData(offset);

      // record total time used for sending
      m_llSndDuration += currtime - m_llSndDurationCounter;
      m_llSndDurationTotal += currtime - m_llSndDurationCounter;
      m_llSndDurationCounter = currtime;

      // update sending variables
      m_iSndLastDataAck = ack;
      m_pSndLossList->remove(CSeqNo::decseq(m_iSndLastDataAck));

#endif /* SRT_ENABLE_TLPKTDROP */

      CGuard::leaveCS(m_AckLock);

      pthread_mutex_lock(&m_SendBlockLock);
      if (m_bSynSending)
          pthread_cond_signal(&m_SendBlockCond);
      pthread_mutex_unlock(&m_SendBlockLock);

      // acknowledde any waiting epolls to write
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);

      // insert this socket to snd list if it is not on the list yet
      m_pSndQueue->m_pSndUList->update(this, false);

      size_t acksize = ctrlpkt.getLength(); // TEMPORARY VALUE FOR CHECKING
      bool wrongsize = 0 != (acksize % ACKD_FIELD_SIZE);
      acksize = acksize / ACKD_FIELD_SIZE;  // ACTUAL VALUE

      if ( wrongsize )
      {
          // Issue a log, but don't do anything but skipping the "odd" bytes from the payload.
          LOGC(mglog.Error) << CONID() << "Received UMSG_ACK payload is not evened up to 4-byte based field size - cutting to " << acksize << " fields";
      }

      // Start with checking the base size.
      if ( acksize < ACKD_TOTAL_SIZE_UDTBASE )
      {
          LOGC(mglog.Error) << CONID() << "Invalid ACK size " << acksize << " fields - less than minimum required!";
          // Ack is already interpreted, just skip further parts.
          break;
      }
      // This check covers fields up to ACKD_BUFFERLEFT.

      // Update RTT
      //m_iRTT = ackdata[ACKD_RTT];
      //m_iRTTVar = ackdata[ACKD_RTTVAR];
      int rtt = ackdata[ACKD_RTT];
      m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
      m_iRTT = (m_iRTT * 7 + rtt) >> 3;


      /* Version-dependent fields:
       * Original UDT (total size: ACKD_TOTAL_SIZE_UDTBASE):
       *   ACKD_RCVLASTACK
       *   ACKD_RTT
       *   ACKD_RTTVAR
       *   ACKD_BUFFERLEFT
       * SRT extension version 1.0.0:
       *   ACKD_RCVSPEED
       *   ACKD_BANDWIDTH
       * SRT extension version 1.0.2 (bstats):
       *   ACKD_RCVRATE
       * SRT extension version 1.0.4:
       *   ACKD_XMRATE
       */

      if (acksize >= ACKD_TOTAL_SIZE_VER101)    //was 32 in SRT v1.0.2
      {
         /* SRT v1.0.2 Bytes-based stats: bandwidth (pcData[ACKD_XMRATE]) and delivery rate (pcData[ACKD_RCVRATE]) in bytes/sec instead of pkts/sec */
         /* SRT v1.0.3 Bytes-based stats: only delivery rate (pcData[ACKD_RCVRATE]) in bytes/sec instead of pkts/sec */
         int bytesps = ackdata[ACKD_RCVRATE];

         if (bytesps > 0)
            m_iDeliveryRate = (m_iDeliveryRate * 7 + bytesps) >> 3;

         if (ackdata[ACKD_BANDWIDTH] > 0)
            m_iBandwidth = (m_iBandwidth * 7 + ackdata[ACKD_BANDWIDTH]) >> 3;

         // Update Estimated Bandwidth and packet delivery rate
         m_iRcvRate = m_iDeliveryRate;
      }
      else if (acksize > ACKD_TOTAL_SIZE_UDTBASE) // This embraces range (...UDTBASE - ...VER100)
      {
         // Peer provides only pkts/sec stats, convert to bits/sec for DeliveryRate
         int pktps;

         if ((pktps = ackdata[ACKD_RCVSPEED]) > 0)
            m_iDeliveryRate = (m_iDeliveryRate * 7 + (pktps * m_iPayloadSize)) >> 3;

         if (ackdata[ACKD_BANDWIDTH] > 0)
            m_iBandwidth = (m_iBandwidth * 7 + ackdata[ACKD_BANDWIDTH]) >> 3;

         // Update Estimated Bandwidth and packet delivery rate
         m_iRcvRate = m_iDeliveryRate; // XXX DUPLICATED FIELD?
      }

      checkSndTimers(REGEN_KM);
      CCUpdate();

      ++ m_iRecvACK;
      ++ m_iRecvACKTotal;

      break;
      }

   case UMSG_ACKACK: //110 - Acknowledgement of Acknowledgement
      {
      int32_t ack;
      int rtt = -1;

      // update RTT
      rtt = m_ACKWindow.acknowledge(ctrlpkt.getAckSeqNo(), ack);
      if (rtt <= 0)
         break;

      //if increasing delay detected...
      //   sendCtrl(4);

      // RTT EWMA
      m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
      m_iRTT = (m_iRTT * 7 + rtt) >> 3;

      CGuard::enterCS(m_RecvLock);
      m_pRcvBuffer->addRcvTsbPdDriftSample(ctrlpkt.getMsgTimeStamp());
      CGuard::leaveCS(m_RecvLock);

      // update last ACK that has been received by the sender
      if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
         m_iRcvLastAckAck = ack;

      break;
      }

   case UMSG_LOSSREPORT: //011 - Loss Report
      {
      int32_t* losslist = (int32_t *)(ctrlpkt.m_pcData);

      CCUpdate();

      bool secure = true;

#ifdef SRT_ENABLE_TLPKTDROP
      // protect packet retransmission
      CGuard::enterCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
    

      // decode loss list message and insert loss into the sender loss list
      for (int i = 0, n = (int)(ctrlpkt.getLength() / 4); i < n; ++ i)
      {
         if (IsSet(losslist[i], LOSSDATA_SEQNO_RANGE_FIRST))
         {
             // Then it's this is a <lo, hi> specification with HI in a consecutive cell.
            int32_t losslist_lo = SEQNO_VALUE::unwrap(losslist[i]);
            int32_t losslist_hi = losslist[i+1];
            // <lo, hi> specification means that the consecutive cell has been already interpreted.
            ++ i;

            LOGC(mglog.Debug).form("received UMSG_LOSSREPORT: %d-%d (%d packets)...", losslist_lo, losslist_hi, CSeqNo::seqcmp(losslist_hi, losslist_lo)+1);

            if ((CSeqNo::seqcmp(losslist_lo, losslist_hi) > 0) || (CSeqNo::seqcmp(losslist_hi, m_iSndCurrSeqNo) > 0))
            {
               // seq_a must not be greater than seq_b; seq_b must not be greater than the most recent sent seq
               secure = false;
               // XXX leaveCS: really necessary? 'break' will break the 'for' loop, not the 'switch' statement.
               // and the leaveCS is done again next to the 'for' loop end.
#ifdef SRT_ENABLE_TLPKTDROP
               CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
               break;
            }

            int num = 0;
            if (CSeqNo::seqcmp(losslist_lo, m_iSndLastAck) >= 0)
               num = m_pSndLossList->insert(losslist_lo, losslist_hi);
            else if (CSeqNo::seqcmp(losslist_hi, m_iSndLastAck) >= 0)
            {
                // This should be theoretically impossible because this would mean
                // that the received packet loss report informs about the loss that predates
                // the ACK sequence.
                // However, this can happen if the packet reordering has caused the earlier sent
                // LOSSREPORT will be delivered after later sent ACK. Whatever, ACK should be
                // more important, so simply drop the part that predates ACK.
               num = m_pSndLossList->insert(m_iSndLastAck, losslist_hi);
            }

            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;

         }
         else if (CSeqNo::seqcmp(losslist[i], m_iSndLastAck) >= 0)
         {
            LOGC(mglog.Debug).form("received UMSG_LOSSREPORT: %d (1 packet)...", losslist[i]);

            if (CSeqNo::seqcmp(losslist[i], m_iSndCurrSeqNo) > 0)
            {
               //seq_a must not be greater than the most recent sent seq
               secure = false;
#ifdef SRT_ENABLE_TLPKTDROP
               CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
               break;
            }

            int num = m_pSndLossList->insert(losslist[i], losslist[i]);

            m_iTraceSndLoss += num;
            m_iSndLossTotal += num;
         }
      }
#ifdef SRT_ENABLE_TLPKTDROP
      CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */

      if (!secure)
      {
          LOGC(mglog.Debug).form("WARNING: out-of-band LOSSREPORT received; considered bug or attack");
         //this should not happen: attack or bug
         m_bBroken = true;
         m_iBrokenCounter = 0;
         break;
      }

      // the lost packet (retransmission) should be sent out immediately
      m_pSndQueue->m_pSndUList->update(this);

      ++ m_iRecvNAK;
      ++ m_iRecvNAKTotal;

      break;
      }

   case UMSG_CGWARNING: //100 - Delay Warning
      // One way packet delay is increasing, so decrease the sending rate
      m_ullInterval = (uint64_t)ceil(m_ullInterval * 1.125);
      m_iLastDecSeq = m_iSndCurrSeqNo;

      break;

   case UMSG_KEEPALIVE: //001 - Keep-alive
      // The only purpose of keep-alive packet is to tell that the peer is still alive
      // nothing needs to be done.

      break;

   case UMSG_HANDSHAKE: //000 - Handshake
      {
      CHandShake req;
      req.load_from(ctrlpkt.m_pcData, ctrlpkt.getLength());

      LOGC(mglog.Debug) << "processCtrl: got HS: " << req.show();

      if ((req.m_iReqType > URQ_INDUCTION_TYPES) // acually it catches URQ_INDUCTION and URQ_ERROR_* symbols...???
              || (m_bRendezvous && (req.m_iReqType != URQ_AGREEMENT))) // rnd sends AGREEMENT in rsp to CONCLUSION
      {
         // The peer side has not received the handshake message, so it keeps querying
         // resend the handshake packet

          // This condition embraces cases when:
          // - this is normal accept() and URQ_INDUCTION was received
          // - this is rendezvous accept() and there's coming any kind of URQ except AGREEMENT (should be RENDEZVOUS or CONCLUSION)
          // - this is any of URQ_ERROR_* - well...
         CHandShake initdata;
         initdata.m_iISN = m_iISN;
         initdata.m_iMSS = m_iMSS;
         initdata.m_iFlightFlagSize = m_iFlightFlagSize;

         // For rendezvous we do URQ_WAVEAHAND/URQ_CONCLUSION --> URQ_AGREEMENT.
         // For client-server we do URQ_INDUCTION --> URQ_CONCLUSION.
         initdata.m_iReqType = (!m_bRendezvous) ? URQ_CONCLUSION : URQ_AGREEMENT;
         initdata.m_iID = m_SocketID;

         uint32_t kmdata[SRTDATA_MAXSIZE];
         size_t kmdatasize = SRTDATA_MAXSIZE;
         bool have_hsreq = false;
         if ( req.m_iVersion > HS_VERSION_UDT4 )
         {
             initdata.m_iVersion = HS_VERSION_SRT1; // if I remember correctly, this is induction/listener...
             if ( req.m_iType != 0 ) // has SRT extensions
             {
                 LOGC(mglog.Debug) << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType) << " WITH SRT ext";
                 have_hsreq = interpretSrtHandshake(req, ctrlpkt, kmdata, &kmdatasize);
                 if ( !have_hsreq )
                 {
                     initdata.m_iVersion = 0;
                     initdata.m_iReqType = URQ_ERROR_INVALID;
                 }
                 else
                 {
                     initdata.m_extension = true;
                 }
             }
             else
             {
                 LOGC(mglog.Debug) << "processCtrl/HS: got HS reqtype=" << RequestTypeStr(req.m_iReqType);
             }
         }
         else
         {
             initdata.m_iVersion = HS_VERSION_UDT4;
         }

         initdata.m_extension = have_hsreq;

         LOGC(mglog.Debug) << CONID() << "processCtrl: responding HS reqtype=" << RequestTypeStr(initdata.m_iReqType) << (have_hsreq ? " WITH SRT HS response extensions" : "");

         // XXX here interpret SRT handshake extension
         CPacket response;
         response.setControl(UMSG_HANDSHAKE);
         response.allocate(m_iPayloadSize);

         // If createSrtHandshake failed, don't send anything. Actually it can only fail on IPE.
         // There is also no possible IPE condition in case of HSv4 - for this version it will always return true.
         if ( createSrtHandshake(Ref(response), Ref(initdata), SRT_CMD_HSRSP, SRT_CMD_KMRSP, kmdata, kmdatasize) )
         {
             response.m_iID = m_PeerID;
             uint64_t currtime;
             CTimer::rdtsc(currtime);
             response.m_iTimeStamp = int(currtime/m_ullCPUFrequency - m_StartTime);
             int nbsent = m_pSndQueue->sendto(m_pPeerAddr, response);
             if (nbsent)
             {
                 uint64_t currtime;
                 CTimer::rdtsc(currtime);
                 m_ullLastSndTime = currtime;
             }
         }

      }
      else
      {
          LOGC(mglog.Debug) << "processCtrl: ... not INDUCTION, not ERROR, not rendezvous - IGNORED.";
      }

      break;
      }

   case UMSG_SHUTDOWN: //101 - Shutdown
      m_bShutdown = true;
      m_bClosing = true;
      m_bBroken = true;
      m_iBrokenCounter = 60;

      // Signal the sender and recver if they are waiting for data.
      releaseSynch();
      // Unblock any call so they learn the connection_broken error
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_ERR, true);

      CTimer::triggerEvent();

      break;

   case UMSG_DROPREQ: //111 - Msg drop request
      CGuard::enterCS(m_RecvLock);
      m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq(using_rexmit_flag), using_rexmit_flag);
      CGuard::leaveCS(m_RecvLock);

      unlose(*(int32_t*)ctrlpkt.m_pcData, *(int32_t*)(ctrlpkt.m_pcData + 4));

      // move forward with current recv seq no.
      if ((CSeqNo::seqcmp(*(int32_t*)ctrlpkt.m_pcData, CSeqNo::incseq(m_iRcvCurrSeqNo)) <= 0)
         && (CSeqNo::seqcmp(*(int32_t*)(ctrlpkt.m_pcData + 4), m_iRcvCurrSeqNo) > 0))
      {
         m_iRcvCurrSeqNo = *(int32_t*)(ctrlpkt.m_pcData + 4);
      }

      break;

   case UMSG_PEERERROR: // 1000 - An error has happened to the peer side
      //int err_type = packet.getAddInfo();

      // currently only this error is signalled from the peer side
      // if recvfile() failes (e.g., due to disk fail), blcoked sendfile/send should return immediately
      // giving the app a chance to fix the issue

      m_bPeerHealth = false;

      break;

   case UMSG_EXT: //0x7FFF - reserved and user defined messages
      LOGC(mglog.Debug).form("CONTROL EXT MSG RECEIVED: %08X\n", ctrlpkt.getExtendedType());
#if SRT_ENABLE_SND2WAYPROTECT
      if (((ctrlpkt.getExtendedType() == SRT_CMD_HSREQ) || (ctrlpkt.getExtendedType() == SRT_CMD_KMREQ))
      &&  (m_bDataSender))
      {
         /*
         * SRT 1.1.2 and earlier sender can assert if accepting HSREQ or KMREQ.
         * Drop connection.
         */
         LOGC(mglog.Error).form("Error: receiving %s control message in SRT sender-only side: %s.",
             ctrlpkt.getExtendedType() == SRT_CMD_HSREQ ? "HSREQ" : "KMREQ", "breaking connection");
         m_bBroken = true;
         m_iBrokenCounter = 0;
         return;
      }
#endif /* SRT_ENABLE_SND2WAYPROTECT */
      processSrtMsg(&ctrlpkt);
      // CAREFUL HERE! This only means that this update comes from the UMSG_EXT
      // message received, REGARDLESS OF WHAT IT IS. This version doesn't mean
      // the handshake version, but the reason of calling this function.
      //
      // Fortunately, the only messages taken into account in this function
      // are HSREQ and HSRSP, which should *never* be interchanged when both
      // parties are HSv5.
      // XXX Would be nice to make some assertion for that - you never know
      // what message you receive.
      updateAfterSrtHandshake(ctrlpkt.getExtendedType(), HS_VERSION_UDT4);
      break;

   default:
      break;
   }
}

void CUDT::updateSrtRcvSettings()
{
    if (m_bTsbPd)
    {
        /* We are TsbPd receiver */
        CGuard::enterCS(m_RecvLock);
        m_pRcvBuffer->setRcvTsbPdMode(m_ullRcvPeerStartTime, m_iTsbPdDelay * 1000);
        CGuard::leaveCS(m_RecvLock);

        LOGC(mglog.Debug).form( "AFTER HS: Set Rcv TsbPd mode: delay=%u.%03u secs",
                m_iTsbPdDelay/1000,
                m_iTsbPdDelay%1000);
    }
    else
    {
        LOGC(mglog.Debug) << "AFTER HS: Rcv TsbPd mode not set";
    }
}

void CUDT::updateSrtSndSettings()
{
    if (m_bPeerTsbPd)
    {
        /* We are TsbPd sender */
        //m_iPeerTsbPdDelay = m_pCryptoControl->getSndPeerTsbPdDelay();// + ((m_iRTT + (4 * m_iRTTVar)) / 1000);
#if defined(SRT_ENABLE_TLPKTDROP)
        /* 
         * For sender to apply Too-Late Packet Drop
         * option (m_bTLPktDrop) must be enabled and receiving peer shall support it
         */
        // XXX HSv5: this is already set from the SRT HS management function.
        // m_bPeerTLPktDrop = m_bTLPktDrop && m_pCryptoControl->getSndPeerTLPktDrop();
        LOGC(mglog.Debug).form( "AFTER HS: Set Snd TsbPd mode %s: delay=%d.%03d secs",
                m_bPeerTLPktDrop ? "with TLPktDrop" : "without TLPktDrop",
                m_iPeerTsbPdDelay/1000, m_iPeerTsbPdDelay%1000);
#else /* SRT_ENABLE_TLPKTDROP */
        LOGC(mglog.Debug).form( "AFTER HS: Set Snd TsbPd mode %s: delay=%d.%03d secs",
                "without TLPktDrop",
                m_iPeerTsbPdDelay/1000, m_iPeerTsbPdDelay%1000);
#endif /* SRT_ENABLE_TLPKTDROP */
    }
    else
    {
        LOGC(mglog.Debug) << "AFTER HS: Snd TsbPd mode not set";
    }
}

void CUDT::updateAfterSrtHandshake(int srt_cmd, int hsv)
{
    CCUpdate();

    switch (srt_cmd)
    {
    case SRT_CMD_HSREQ:
    case SRT_CMD_HSRSP:
        break;
    default:
        return;
    }

    // The only possibility here is one of these two:
    // - Agent is RESPONDER and it receives HSREQ.
    // - Agent is INITIATOR and it receives HSRSP.
    //
    // In HSv4, INITIATOR is sender and RESPONDER is receiver.
    // In HSv5, both are sender AND receiver.
    //
    // This function will be called only ONCE in this
    // instance, through either HSREQ or HSRSP.

    if ( hsv > HS_VERSION_UDT4 )
    {
        updateSrtRcvSettings();
        updateSrtSndSettings();
    }
    else if ( srt_cmd == SRT_CMD_HSRSP )
    {
        // HSv4 INITIATOR is sender
        updateSrtSndSettings();
    }
    else
    {
        // HSv4 RESPONDER is receiver
        updateSrtRcvSettings();
    }
}

int CUDT::packData(CPacket& packet, uint64_t& ts)
{
   int payload = 0;
   bool probe = false;
   uint64_t origintime = 0;

   int kflg = EK_NOENC;

   uint64_t entertime;
   CTimer::rdtsc(entertime);

#if 0//debug: TimeDiff histogram
   static int lldiffhisto[23] = {0};
   static int llnodiff = 0;
   if (m_ullTargetTime != 0)
   {
      int ofs = 11 + ((entertime - m_ullTargetTime)/(int64_t)m_ullCPUFrequency)/1000;
      if (ofs < 0) ofs = 0;
      else if (ofs > 22) ofs = 22;
      lldiffhisto[ofs]++;
   }
   else if(m_ullTargetTime == 0)
   {
      llnodiff++;
   }
   static int callcnt = 0;
   if (!(callcnt++ % 5000)) {
      fprintf(stderr, "%6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d\n",
        lldiffhisto[0],lldiffhisto[1],lldiffhisto[2],lldiffhisto[3],lldiffhisto[4],lldiffhisto[5],
        lldiffhisto[6],lldiffhisto[7],lldiffhisto[8],lldiffhisto[9],lldiffhisto[10],lldiffhisto[11]);
      fprintf(stderr, "%6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d\n",
        lldiffhisto[12],lldiffhisto[13],lldiffhisto[14],lldiffhisto[15],lldiffhisto[16],lldiffhisto[17],
        lldiffhisto[18],lldiffhisto[19],lldiffhisto[20],lldiffhisto[21],lldiffhisto[21],llnodiff);
   }
#endif
   if ((0 != m_ullTargetTime) && (entertime > m_ullTargetTime))
      m_ullTimeDiff += entertime - m_ullTargetTime;

   string reason;

   // Loss retransmission always has higher priority.
   packet.m_iSeqNo = m_pSndLossList->getLostSeq();
   if (packet.m_iSeqNo >= 0)
   {
      // protect m_iSndLastDataAck from updating by ACK processing
      CGuard ackguard(m_AckLock);

      int offset = CSeqNo::seqoff(m_iSndLastDataAck, packet.m_iSeqNo);
      if (offset < 0)
         return 0;

      int msglen;

      payload = m_pSndBuffer->readData(&(packet.m_pcData), offset, packet.m_iMsgNo, origintime, msglen);

      if (-1 == payload)
      {
         int32_t seqpair[2];
         seqpair[0] = packet.m_iSeqNo;
         seqpair[1] = CSeqNo::incseq(seqpair[0], msglen);
         sendCtrl(UMSG_DROPREQ, &packet.m_iMsgNo, seqpair, 8);

         // only one msg drop request is necessary
         m_pSndLossList->remove(seqpair[1]);

         // skip all dropped packets
         if (CSeqNo::seqcmp(m_iSndCurrSeqNo, CSeqNo::incseq(seqpair[1])) < 0)
             m_iSndCurrSeqNo = CSeqNo::incseq(seqpair[1]);

         return 0;
      }
      // NOTE: This is just a sanity check. Returning 0 is impossible to happen
      // in case of retransmission. If the offset was a positive value, then the
      // block must exist in the old blocks because it wasn't yet cut off by ACK
      // and has been already recorded as sent (otherwise the peer wouldn't send
      // back the loss report). May something happen here in case when the send
      // loss record has been updated by the FASTREXMIT.
      else if (payload == 0)
         return 0;


      ++ m_iTraceRetrans;
      ++ m_iRetransTotal;
      m_ullTraceBytesRetrans += payload;
      m_ullBytesRetransTotal += payload;

      //*

      // Alright, gr8. Despite the contextual interpretation of packet.m_iMsgNo around
      // CSndBuffer::readData version 2 (version 1 doesn't return -1), in this particular
      // case we can be sure that this is exactly the value of PH_MSGNO as a bitset.
      // So, set here the rexmit flag if the peer understands it.
      if ( m_bPeerRexmitFlag )
      {
          packet.m_iMsgNo |= PACKET_SND_REXMIT;
      }
      // */
      reason = "reXmit";
   }
   else
   {
      // If no loss, pack a new packet.

      // check congestion/flow window limit
      int cwnd = std::min(int(m_iFlowWindowSize), int(m_dCongestionWindow));
      int seqdiff = CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo));
      if (cwnd >= seqdiff)
      {
         // XXX Here it's needed to set kflg to msgno_bitset in the block stored in the
         // send buffer. This should be somehow avoided, the crypto flags should be set
         // together with encrypting, and the packet should be sent as is, when rexmitting.
         // It would be nice to research as to whether CSndBuffer::Block::m_iMsgNoBitset field
         // isn't a useless redundant state copy. If it is, then taking the flags here can be removed.
         kflg = m_pCryptoControl->getSndCryptoFlags();
         if (0 != (payload = m_pSndBuffer->readData(&(packet.m_pcData), packet.m_iMsgNo, origintime, kflg)))
         {
            m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
            //m_pCryptoControl->m_iSndCurrSeqNo = m_iSndCurrSeqNo;

            packet.m_iSeqNo = m_iSndCurrSeqNo;

            // every 16 (0xF) packets, a packet pair is sent
            if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 0)
               probe = true;
         }
         else
         {
            m_ullTargetTime = 0;
            m_ullTimeDiff = 0;
            ts = 0;
            return 0;
         }
      }
      else
      {
          LOGC(dlog.Debug) << "packData: CONGESTED: cwnd=min(" << m_iFlowWindowSize << "," << m_dCongestionWindow
              << ")=" << cwnd << " seqlen=(" << m_iSndLastAck << "-" << m_iSndCurrSeqNo << ")=" << seqdiff;
         m_ullTargetTime = 0;
         m_ullTimeDiff = 0;
         ts = 0;
         return 0;
      }

      reason = "normal";
   }

   if (m_bPeerTsbPd)
   {
       /*
       * When timestamp is carried over in this sending stream from a received stream,
       * it may be older than the session start time causing a negative packet time
       * that may block the receiver's Timestamp-based Packet Delivery.
       * XXX Isn't it then better to not decrease it by m_StartTime? As long as it
       * doesn't screw up the start time on the other side.
       */
      if (origintime >= m_StartTime)
         packet.m_iTimeStamp = int(origintime - m_StartTime);
      else
         packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
   }
   else
   {
       packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
   }

   packet.m_iID = m_PeerID;
   packet.setLength(payload);

   /* Encrypt if 1st time this packet is sent and crypto is enabled */
   if (kflg)
   {
       // XXX Encryption flags are already set on the packet before calling this.
       // See readData() above.
      if (m_pCryptoControl->encrypt(Ref(packet)))
      //if (packet.encrypt(m_pCryptoControl->getSndCryptoCtx()))
      {
          /* Encryption failed */
          //>>Add stats for crypto failure
          ts = 0;
          return -1; //Encryption failed
      }
      payload = packet.getLength(); /* Cipher may change length */
      reason += " (encrypted)";
   }

#if ENABLE_LOGGING // Required because of referring to MessageFlagStr()
   LOGC(mglog.Debug) << CONID() << "packData: " << reason << " packet seq=" << packet.m_iSeqNo
       << " (ACK=" << m_iSndLastAck << " ACKDATA=" << m_iSndLastDataAck
       << " MSG/FLAGS: " << packet.MessageFlagStr() << ")";
#endif


#ifdef SRT_FIX_KEEPALIVE
   m_ullLastSndTime = entertime;
#endif /* SRT_FIX_KEEPALIVE */

   //onPktSent(&packet);
   // (expanding the call)
   considerLegacySrtHandshake(0);
   m_iSndAvgPayloadSize = ((m_iSndAvgPayloadSize * 127) + packet.getLength()) / 128;
   //m_pSndTimeWindow->onPktSent(packet.m_iTimeStamp);

   m_ullTraceBytesSent += payload;
   m_ullBytesSentTotal += payload;
   ++ m_llTraceSent;
   ++ m_llSentTotal;

   if (probe)
   {
      // sends out probing packet pair
      ts = entertime;
      probe = false;
   }
   else
   {
      #ifndef NO_BUSY_WAITING
         ts = entertime + m_ullInterval;
      #else
         if (m_ullTimeDiff >= m_ullInterval)
         {
            ts = entertime;
            m_ullTimeDiff -= m_ullInterval;
         }
         else
         {
            ts = entertime + m_ullInterval - m_ullTimeDiff;
            m_ullTimeDiff = 0;
         }
      #endif
   }

   m_ullTargetTime = ts;

   return payload;
}

int CUDT::processData(CUnit* unit)
{
   CPacket& packet = unit->m_Packet;

   // XXX This should be called (exclusively) here:
   //m_pRcvBuffer->addLocalTsbPdDriftSample(packet.getMsgTimeStamp());
   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;

   /* We are receiver, start tsbpd thread if TsbPd is enabled */
   if (m_bTsbPd && pthread_equal(m_RcvTsbPdThread, pthread_t()))
   {
       LOGP(mglog.Debug, "Spawning TSBPD thread");
       int st = 0;
       {
           ThreadName tn("SRT:TsbPd");
           st = pthread_create(&m_RcvTsbPdThread, NULL, CUDT::tsbpd, this);
       }
       if ( st != 0 )
           return -1;
   }

   int pktrexmitflag = m_bPeerRexmitFlag ? (int)packet.getRexmitFlag() : 2;
   static const string rexmitstat [] = {"ORIGINAL", "REXMITTED", "RXS-UNKNOWN"};
   string rexmit_reason;


   if ( pktrexmitflag == 1 ) // rexmitted
   {
       m_iTraceRcvRetrans++;

#if ENABLE_LOGGING
       // Check if packet was retransmitted on request or on ack timeout
       // Search the sequence in the loss record.
       rexmit_reason = " by ";
       if ( !m_pRcvLossList->find(packet.m_iSeqNo, packet.m_iSeqNo) )
       //if ( m_DebugLossRecords.find(packet.m_iSeqNo) ) // m_DebugLossRecords not turned on
           rexmit_reason += "REQUEST";
       else
       {
           rexmit_reason += "ACK-TMOUT";
           /*
           if ( !m_DebugLossRecords.exists(packet.m_iSeqNo) )
           {
               rexmit_reason += "(seems/";
               char buf[100] = "empty";
               int32_t base = m_DebugLossRecords.base();
               if ( base != -1 )
                   sprintf(buf, "%d", base);
               rexmit_reason += buf;
               rexmit_reason += ")";
           }
           */
       }
#endif
   }


   LOGC(dlog.Debug) << CONID() << "processData: RECEIVED DATA: size=" << packet.getLength() << " seq=" << packet.getSeqNo();
   //    << "(" << rexmitstat[pktrexmitflag] << rexmit_reason << ")";

   // XXX CCC hook was here!
   // onPktReceived(&packet);
   ++ m_iPktCount;

   int pktsz = packet.getLength();
   // update time information
   m_RcvTimeWindow.onPktArrival(pktsz);

   // check if it is probing packet pair
   if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 0)
      m_RcvTimeWindow.probe1Arrival();
   else if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 1)
      m_RcvTimeWindow.probe2Arrival(pktsz);

   m_ullTraceBytesRecv += pktsz;
   m_ullBytesRecvTotal += pktsz;
   ++ m_llTraceRecv;
   ++ m_llRecvTotal;

   {
      /*
      * Start of offset protected section
      * Prevent TsbPd thread from modifying Ack position while adding data
      * offset from RcvLastAck in RcvBuffer must remain valid between seqoff() and addData()
      */
      CGuard offsetcg(m_AckLock);

#ifdef SRT_ENABLE_TLPKTDROP
      int32_t offset = CSeqNo::seqoff(m_iRcvLastSkipAck, packet.m_iSeqNo);
#else /* SRT_ENABLE_TLPKTDROP */
      int32_t offset = CSeqNo::seqoff(m_iRcvLastAck, packet.m_iSeqNo);
#endif /* SRT_ENABLE_TLPKTDROP */

      bool excessive = false;
      string exc_type = "EXPECTED";
      if ((offset < 0))
      {
          exc_type = "BELATED";
          excessive = true;
          m_iTraceRcvBelated++;
          uint64_t tsbpdtime = m_pRcvBuffer->getPktTsbPdTime(packet.getMsgTimeStamp());
          uint64_t bltime = CountIIR(
                  uint64_t(m_fTraceBelatedTime)*1000,
                  CTimer::getTime() - tsbpdtime, 0.2);
          m_fTraceBelatedTime = double(bltime)/1000.0;
      }
      else
      {

          int avail_bufsize = m_pRcvBuffer->getAvailBufSize();
          if (offset >= avail_bufsize)
          {
              LOGC(mglog.Error) << CONID() << "No room to store incoming packet: offset=" << offset << " avail=" << avail_bufsize;
              return -1;
          }

          if (m_pRcvBuffer->addData(unit, offset) < 0)
          {
              // addData returns -1 if at the m_iLastAckPos+offset position there already is a packet.
              // So this packet is "redundant".
              exc_type = "UNACKED";
              excessive = true;
          }
      }

      LOGC(mglog.Debug) << CONID() << "RECEIVED: seq=" << packet.m_iSeqNo << " offset=" << offset
          << (excessive ? " EXCESSIVE" : " ACCEPTED")
          << " (" << exc_type << "/" << rexmitstat[pktrexmitflag] << rexmit_reason << ") FLAGS: "
          << packet.MessageFlagStr();

      if ( excessive )
      {
          return -1;
      }

      if (packet.getMsgCryptoFlags())
      {
          // Crypto should be already created during connection process,
          // this is rather a kinda sanity check.
          EncryptionStatus rc = m_pCryptoControl ? m_pCryptoControl->decrypt(Ref(packet)) : ENCS_NOTSUP;
          if ( rc != ENCS_CLEAR )
          {
              /*
               * Could not decrypt
               * Keep packet in received buffer
               * Crypto flags are still set
               * It will be acknowledged
               */
              m_iTraceRcvUndecrypt += 1;
              m_ullTraceRcvBytesUndecrypt += pktsz;
              m_iRcvUndecryptTotal += 1;
              m_ullRcvBytesUndecryptTotal += pktsz;
          }
      }

   }  /* End of offsetcg */

   if (m_bClosing) {
      /*
      * RcvQueue worker thread can call processData while closing (or close while processData)
      * This race condition exists in the UDT design but the protection against TsbPd thread
      * (with AckLock) and decryption enlarged the probability window.
      * Application can crash deep in decrypt stack since crypto context is deleted in close.
      * RcvQueue worker thread will not necessarily be deleted with this connection as it can be
      * used by others (socket multiplexer).
      */
      return(-1);
   }

#if SRT_BELATED_LOSSREPORT
   // If the peer doesn't understand REXMIT flag, send rexmit request
   // always immediately.
   int initial_loss_ttl = 0;
   if ( m_bPeerRexmitFlag )
       initial_loss_ttl = m_iReorderTolerance;
#endif

   if  (packet.getMsgCryptoFlags())
   {
       /*
       * Crypto flags not cleared means that decryption failed
       * Do no ask loss packets retransmission
       */
       ;
       LOGC(mglog.Debug) << CONID() << "ERROR: packet not decrypted, dropping data.";
   }
   else
   // Loss detection.
   if (CSeqNo::seqcmp(packet.m_iSeqNo, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0)
   {
       {
           CGuard lg(m_RcvLossLock);
           int32_t seqlo = CSeqNo::incseq(m_iRcvCurrSeqNo);
           int32_t seqhi = CSeqNo::decseq(packet.m_iSeqNo);
           // If loss found, insert them to the receiver loss list
           m_pRcvLossList->insert(seqlo, seqhi);

#if SRT_BELATED_LOSSREPORT
           if ( initial_loss_ttl )
           {
               // pack loss list for (possibly belated) NAK
               // The LOSSREPORT will be sent in a while.
               m_FreshLoss.push_back(CRcvFreshLoss(seqlo, seqhi, initial_loss_ttl));
               LOGC(mglog.Debug).form("added loss sequence %d-%d (%d) with tolerance %d", seqlo, seqhi, 1+CSeqNo::seqcmp(seqhi, seqlo), initial_loss_ttl);
           }
           else
#endif
           {
               // old code; run immediately when tolerance = 0
               // or this feature isn't used because of the peer
               int32_t seq[2] = { seqlo, seqhi };
               if ( seqlo == seqhi )
                   sendCtrl(UMSG_LOSSREPORT, NULL, &seq[1], 1);
               else
               {
                   seq[0] |= LOSSDATA_SEQNO_RANGE_FIRST;
                   sendCtrl(UMSG_LOSSREPORT, NULL, seq, 2);
               }
               LOGC(mglog.Debug).form("lost packets %d-%d (%d packets): sending LOSSREPORT", seqlo, seqhi, 1+CSeqNo::seqcmp(seqhi, seqlo));
           }

           int loss = CSeqNo::seqlen(m_iRcvCurrSeqNo, packet.m_iSeqNo) - 2;
           m_iTraceRcvLoss += loss;
           m_iRcvLossTotal += loss;
           uint64_t lossbytes = loss * m_pRcvBuffer->getRcvAvgPayloadSize();
           m_ullTraceRcvBytesLoss += lossbytes;
           m_ullRcvBytesLossTotal += lossbytes;
       }

       if (m_bTsbPd)
       {
           pthread_mutex_lock(&m_RecvLock);
           pthread_cond_signal(&m_RcvTsbPdCond);
           pthread_mutex_unlock(&m_RecvLock);
       }
   }

#ifdef SRT_BELATED_LOSSREPORT
   // Now review the list of FreshLoss to see if there's any "old enough" to send UMSG_LOSSREPORT to it.

   // PERFORMANCE CONSIDERATIONS:
   // This list is quite inefficient as a data type and finding the candidate to send UMSG_LOSSREPORT
   // is linear time. On the other hand, there are some special cases that are important for performance:
   // - only the first (plus some following) could have had TTL drown to 0
   // - the only (little likely) possibility that the next-to-first record has TTL=0 is when there was
   //   a loss range split (due to unlose() of one sequence)
   // - first found record with TTL>0 means end of "ready to LOSSREPORT" records
   // So:
   // All you have to do is:
   //  - start with first element and continue with next elements, as long as they have TTL=0
   //    If so, send the loss report and remove this element.
   //  - Since the first element that has TTL>0, iterate until the end of container and decrease TTL.
   //
   // This will be efficient becase the loop to increment one field (without any condition check)
   // can be quite well optimized.

   vector<int32_t> lossdata;
   {
       CGuard lg(m_RcvLossLock);

       // XXX There was a mysterious crash around m_FreshLoss. When the initial_loss_ttl is 0
       // (that is, "belated loss report" feature is off), don't even touch m_FreshLoss.
       if ( initial_loss_ttl && !m_FreshLoss.empty() )
       {
           deque<CRcvFreshLoss>::iterator i = m_FreshLoss.begin();

           // Phase 1: take while TTL <= 0.
           // There can be more than one record with the same TTL, if it has happened before
           // that there was an 'unlost' (@c unlose) sequence that has split one detected loss
           // into two records.
           for( ; i != m_FreshLoss.end() && i->ttl <= 0; ++i )
           {
               LOGC(mglog.Debug).form("Packet seq %d-%d (%d packets) considered lost - sending LOSSREPORT",
                                      i->seq[0], i->seq[1], CSeqNo::seqcmp(i->seq[1], i->seq[0])+1);
               addLossRecord(lossdata, i->seq[0], i->seq[1]);
           }

           // Remove elements that have been processed and prepared for lossreport.
           if ( i != m_FreshLoss.begin() )
           {
               m_FreshLoss.erase(m_FreshLoss.begin(), i);
               i = m_FreshLoss.begin();
           }

           if ( m_FreshLoss.empty() )
               LOGC(mglog.Debug).form("NO MORE FRESH LOSS RECORDS.");
           else
               LOGC(mglog.Debug).form("STILL %zu FRESH LOSS RECORDS, FIRST: %d-%d (%d) TTL: %d", m_FreshLoss.size(),
                       i->seq[0], i->seq[1], 1+CSeqNo::seqcmp(i->seq[1], i->seq[0]),
                       i->ttl);

           // Phase 2: rest of the records should have TTL decreased.
           for ( ; i != m_FreshLoss.end(); ++i )
               --i->ttl;
       }

   }
   if ( !lossdata.empty() )
       sendCtrl(UMSG_LOSSREPORT, NULL, lossdata.data(), lossdata.size());
#endif

   // This is not a regular fixed size packet...
   //an irregular sized packet usually indicates the end of a message, so send an ACK immediately
   if (pktsz != m_iPayloadSize)
#ifdef SRT_ENABLE_LOWACKRATE
      if (m_iSockType == UDT_STREAM)
#endif
      CTimer::rdtsc(m_ullNextACKTime);

   // Update the current largest sequence number that has been received.
   // Or it is a retransmitted packet, remove it from receiver loss list.
#if SRT_BELATED_LOSSREPORT
   bool was_orderly_sent = true;
#endif
   if (CSeqNo::seqcmp(packet.m_iSeqNo, m_iRcvCurrSeqNo) > 0)
   {
      m_iRcvCurrSeqNo = packet.m_iSeqNo; // Latest possible received
   }
   else
   {
      unlose(packet); // was BELATED or RETRANSMITTED packet.
#if SRT_BELATED_LOSSREPORT
      was_orderly_sent = 0!=  pktrexmitflag;
#endif
   }

#if SRT_BELATED_LOSSREPORT
   // was_orderly_sent means either of:
   // - packet was sent in order (first if branch above)
   // - packet was sent as old, but was a retransmitted packet

   if ( m_bPeerRexmitFlag && was_orderly_sent )
   {
       ++m_iConsecOrderedDelivery;
       if ( m_iConsecOrderedDelivery >= 50 )
       {
           m_iConsecOrderedDelivery = 0;
           if ( m_iReorderTolerance > 0 )
           {
               m_iReorderTolerance--;
               m_iTraceReorderDistance--;
               LOGC(mglog.Debug).form( "ORDERED DELIVERY of 50 packets in a row - decreasing tolerance to %d", m_iReorderTolerance);
           }
       }
   }

#endif

   return 0;
}

/// This function is called when a packet has arrived, which was behind the current
/// received sequence - that is, belated or retransmitted. Try to remove the packet
/// from both loss records: the general loss record and the fresh loss record.
///
/// Additionally, check - if supported by the peer - whether the "latecoming" packet
/// has been sent due to retransmission or due to reordering, by checking the rexmit
/// support flag and rexmit flag itself. If this packet was surely ORIGINALLY SENT
/// it means that the current network connection suffers of packet reordering. This
/// way try to introduce a dynamic tolerance by calculating the difference between
/// the current packet reception sequence and this packet's sequence. This value
/// will be set to the tolerance value, which means that later packet retransmission
/// will not be required immediately, but only after receiving N next packets that
/// do not include the lacking packet.
/// The tolerance is not increased infinitely - it's bordered by m_iMaxReorderTolerance.
/// This value can be set in options - SRT_LOSSMAXTTL.
void CUDT::unlose(const CPacket& packet)
{
    CGuard lg(m_RcvLossLock);
    int32_t sequence = packet.m_iSeqNo;
    m_pRcvLossList->remove(sequence);

#if SRT_BELATED_LOSSREPORT

    bool has_increased_tolerance = false;
    bool was_reordered = false;

    if ( m_bPeerRexmitFlag )
    {
        // If the peer understands the REXMIT flag, it means that the REXMIT flag is contained
        // in the PH_MSGNO field.

        // The packet is considered coming originally (just possibly out of order), if REXMIT
        // flag is NOT set.
        was_reordered = !packet.getRexmitFlag();
        if ( was_reordered )
        {
            LOGC(mglog.Debug).form("received out-of-band packet seq %d", sequence);

            int seqdiff = abs(CSeqNo::seqcmp(m_iRcvCurrSeqNo, packet.m_iSeqNo));
            m_iTraceReorderDistance = max(seqdiff, m_iTraceReorderDistance);
            if ( seqdiff > m_iReorderTolerance )
            {
                int prev = m_iReorderTolerance;
                m_iReorderTolerance = min(seqdiff, m_iMaxReorderTolerance);
                LOGC(mglog.Debug).form("Belated by %d seqs - Reorder tolerance %s %d", seqdiff,
                        (prev == m_iReorderTolerance) ? "REMAINS with" : "increased to", m_iReorderTolerance);
                has_increased_tolerance = true; // Yes, even if reorder tolerance is already at maximum - this prevents decreasing tolerance.
            }
        }
        else
        {
            LOGC(mglog.Debug) << CONID() << "received reXmitted packet seq=" << sequence;
        }
    }
    else
    {
        LOGC(mglog.Debug).form("received reXmitted or belated packet seq %d (distinction not supported by peer)", sequence);
    }


    int initial_loss_ttl = 0;
    if ( m_bPeerRexmitFlag )
        initial_loss_ttl = m_iReorderTolerance;

    // Don't do anything if "belated loss report" feature is not used.
    // In that case the FreshLoss list isn't being filled in at all, the
    // loss report is sent directly.

    // Note that this condition blocks two things being done in this function:
    // - remove given sequence from the fresh loss record
    //   (in this case it's empty anyway)
    // - decrease current reorder tolerance based on whether packets come in order
    //   (current reorder tolerance is 0 anyway)
    if ( !initial_loss_ttl )
        return;

    size_t i = 0;
    int had_ttl = 0;
    for (i = 0; i < m_FreshLoss.size(); ++i)
    {
        had_ttl = m_FreshLoss[i].ttl;
        switch ( m_FreshLoss[i].revoke(sequence) )
        {
       case CRcvFreshLoss::NONE:
           continue; // Not found. Search again.

       case CRcvFreshLoss::STRIPPED:
           goto breakbreak; // Found and the modification is applied. We're done here.

       case CRcvFreshLoss::DELETE:
           // No more elements. Kill it.
           m_FreshLoss.erase(m_FreshLoss.begin() + i);
           // Every loss is unique. We're done here.
           goto breakbreak;

       case CRcvFreshLoss::SPLIT:
           // Oh, this will be more complicated. This means that it was in between.
           {
               // So create a new element that will hold the upper part of the range,
               // and this one modify to be the lower part of the range.

               // Keep the current end-of-sequence value for the second element
               int32_t next_end = m_FreshLoss[i].seq[1];

               // seq-1 set to the end of this element
               m_FreshLoss[i].seq[1] = CSeqNo::decseq(sequence);
               // seq+1 set to the begin of the next element
               int32_t next_begin = CSeqNo::incseq(sequence);

               // Use position of the NEXT element because insertion happens BEFORE pointed element.
               // Use the same TTL (will stay the same in the other one).
               m_FreshLoss.insert(m_FreshLoss.begin() + i + 1, CRcvFreshLoss(next_begin, next_end, m_FreshLoss[i].ttl));
           }
           goto breakbreak;
       }
    }

    // Could have made the "return" instruction instead of goto, but maybe there will be something
    // to add in future, so keeping that.
breakbreak: ;

    if (i != m_FreshLoss.size())
    {
        LOGC(mglog.Debug).form("sequence %d removed from belated lossreport record", sequence);
    }

    if ( was_reordered )
    {
        m_iConsecOrderedDelivery = 0;
        if ( has_increased_tolerance )
        {
            m_iConsecEarlyDelivery = 0; // reset counter
        }
        else if ( had_ttl > 2 )
        {
            ++m_iConsecEarlyDelivery; // otherwise, and if it arrived quite earlier, increase counter
            LOGC(mglog.Debug).form("... arrived at TTL %d case %d", had_ttl, m_iConsecEarlyDelivery);

            // After 10 consecutive 
            if ( m_iConsecEarlyDelivery >= 10 )
            {
                m_iConsecEarlyDelivery = 0;
                if ( m_iReorderTolerance > 0 )
                {
                    m_iReorderTolerance--;
                    m_iTraceReorderDistance--;
                    LOGC(mglog.Debug).form("... reached %d times - decreasing tolerance to %d", m_iConsecEarlyDelivery, m_iReorderTolerance);
                }
            }

        }
        // If hasn't increased tolerance, but the packet appeared at TTL less than 2, do nothing.
    }

#endif

}

void CUDT::unlose(int32_t from, int32_t to)
{
    CGuard lg(m_RcvLossLock);
    m_pRcvLossList->remove(from, to);

    LOGC(mglog.Debug).form("TLPKTDROP seq %d-%d (%d packets)", from, to, CSeqNo::seqoff(from, to));

#if SRT_BELATED_LOSSREPORT
    int initial_loss_ttl = 0;
    if ( m_bPeerRexmitFlag )
        initial_loss_ttl = m_iReorderTolerance;

    if ( !initial_loss_ttl )
        return;

    // It's highly unlikely that this is waiting to send a belated UMSG_LOSSREPORT,
    // so treat it rather as a sanity check.

    // It's enough to check if the first element of the list starts with a sequence older than 'to'.
    // If not, just do nothing.

    size_t delete_index = 0;
    for (size_t i = 0; i < m_FreshLoss.size(); ++i)
    {
        CRcvFreshLoss::Emod result = m_FreshLoss[i].revoke(from, to);
        switch ( result )
        {
        case CRcvFreshLoss::DELETE:
            delete_index = i+1; // PAST THE END
            continue; // There may be further ranges that are included in this one, so check on.

        case CRcvFreshLoss::NONE:
        case CRcvFreshLoss::STRIPPED:
            break; // THIS BREAKS ONLY 'switch', not 'for'!

        case CRcvFreshLoss::SPLIT: ; // This function never returns it. It's only a compiler shut-up.
        }

        break; // Now this breaks also FOR.
    }

    m_FreshLoss.erase(m_FreshLoss.begin(), m_FreshLoss.begin() + delete_index); // with delete_index == 0 will do nothing
#endif
}

// This function, as the name states, should bake a new cookie.
int32_t CUDT::bake(const sockaddr* addr, int32_t current_cookie, int correction)
{
    static unsigned int distractor = 0;
    unsigned int rollover = distractor+10;

    for(;;)
    {
        // SYN cookie
        char clienthost[NI_MAXHOST];
        char clientport[NI_MAXSERV];
        getnameinfo(addr,
                (m_iIPversion == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
                clienthost, sizeof(clienthost), clientport, sizeof(clientport),
                NI_NUMERICHOST|NI_NUMERICSERV);
        int64_t timestamp = ((CTimer::getTime() - m_StartTime) / 60000000) + distractor - correction; // secret changes every one minute
        stringstream cookiestr;
        cookiestr << clienthost << ":" << clientport << ":" << timestamp;
        union
        {
            unsigned char cookie[16];
            int32_t cookie_val;
        };
        CMD5::compute(cookiestr.str().c_str(), cookie);

        if ( cookie_val != current_cookie )
            return cookie_val;

        ++distractor;

        // This is just to make the loop formally breakable,
        // but this is virtually impossible to happen.
        if ( distractor == rollover )
            return cookie_val;
    }
}

// XXX This is quite a mystery, why this function has a return value
// and what the purpose for it was. There's just one call of this
// function in the whole code and in that call the return value is
// ignored. Actually this call happens in the CRcvQueue::worker thread,
// where it makes a response for incoming UDP packet that might be
// a connection request. Should any error occur in this process, there
// is no way to "report error" that happened here. Basing on that
// these values in original UDT code were quite like the values
// for m_iReqType, they have been changed to URQ_* symbols, which
// may mean that the intent for the return value was to send this
// value back as a control packet back to the connector.
//
// This function is run when the CRcvQueue object is reading packets
// from the multiplexer (@c CRcvQueue::worker_RetrieveUnit) and the
// target socket ID is 0.
//
// XXX Make this function return EConnectStatus enum type (extend if needed),
// and this will be directly passed to the caller.
int CUDT::processConnectRequest(const sockaddr* addr, CPacket& packet)
{
    // XXX ASSUMPTIONS:
    // [[using assert(packet.m_iID == 0)]]

   LOGC(mglog.Debug) << "processConnectRequest: received a connection request";

   if (m_bClosing)
   {
       LOGC(mglog.Debug) << "processConnectRequest: ... NOT. Rejecting because closing.";
       return int(URQ_ERROR_REJECT);
   }

   /*
   * Closing a listening socket only set bBroken
   * If a connect packet is received while closing it gets through
   * processing and crashes later.
   */
   if (m_bBroken)
   {
      LOGC(mglog.Debug) << "processConnectRequest: ... NOT. Rejecting because broken.";
      return int(URQ_ERROR_REJECT);
   }
   size_t exp_len = CHandShake::m_iContentSize; // When CHandShake::m_iContentSize is used in log, the file fails to link!

   // NOTE!!! Old version of SRT code checks if the size of the HS packet
   // is EQUAL to the above CHandShake::m_iContentSize.

   // Changed to < exp_len because we actually need that the packet
   // be at least of a size for handshake, although it may contain
   // more data, depending on what's inside.
   if (packet.getLength() < exp_len)
   {
      LOGC(mglog.Debug) << "processConnectRequest: ... NOT. Wrong size: " << packet.getLength() << " (expected: " << exp_len << ")";
      return int(URQ_ERROR_INVALID);
   }

   // Dunno why the original UDT4 code only MUCH LATER was checking if the packet was UMSG_HANDSHAKE.
   // It doesn't seem to make sense to deserialize it into the handshake structure if we are not
   // sure that the packet contains the handshake at all!
   if ( !packet.isControl(UMSG_HANDSHAKE) )
   {
       LOGC(mglog.Error) << "processConnectRequest: the packet received as handshake is not a handshake message";
       return int(URQ_ERROR_INVALID);
   }

   CHandShake hs;
   hs.load_from(packet.m_pcData, packet.getLength());

   int32_t cookie_val = bake(addr);

   LOGC(mglog.Debug) << "processConnectRequest: new cookie: " << hex << cookie_val;

   // REQUEST:INDUCTION.
   // Set a cookie, a target ID, and send back the same as
   // RESPONSE:INDUCTION.
   if (hs.m_iReqType == URQ_INDUCTION)
   {
       LOGC(mglog.Debug) << "processConnectRequest: received type=induction, sending back with cookie+socket";

       // XXX That looks weird - the calculated md5 sum out of the given host/port/timestamp
       // is 16 bytes long, but CHandShake::m_iCookie has 4 bytes. This then effectively copies
       // only the first 4 bytes. Moreover, it's dangerous on some platforms because the char
       // array need not be aligned to int32_t - changed to union in a hope that using int32_t
       // inside a union will enforce whole union to be aligned to int32_t.
      hs.m_iCookie = cookie_val;
      packet.m_iID = hs.m_iID;

      // Ok, now's the time. The listener sets here the version 5 handshake,
      // even though the request was 4. This is because the old client would
      // simply return THE SAME version, not even looking into it, giving the
      // listener false impression as if it supported version 5.
      //
      // If the caller was really HSv4, it will simply ignore the version 5 in INDUCTION;
      // it will respond with CONCLUSION, but with its own set version, which is version 4.
      //
      // If the caller was really HSv5, it will RECOGNIZE this version 5 in INDUCTION, so
      // it will respond with version 5 when sending CONCLUSION.

      hs.m_iVersion = HS_VERSION_SRT1;

      // Additionally, set this field to a MAGIC value. This field isn't used during INDUCTION
      // by HSv4 client, HSv5 client can use it to additionally verify that this is a HSv5 listener.

      hs.m_iType = SrtHSRequest::SRT_MAGIC_CODE;

      size_t size = packet.getLength();
      hs.store_to(packet.m_pcData, Ref(size));
      packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
      m_pSndQueue->sendto(addr, packet);
      return URQ_INDUCTION;
   }

   // Otherwise this should be REQUEST:CONCLUSION.
   // Should then come with the correct cookie that was
   // set in the above INDUCTION, in the HS_VERSION_SRT1
   // should also contain extra data.

   LOGC(mglog.Debug) << "processConnectRequest: received type=" << RequestTypeStr(hs.m_iReqType) << " - checking cookie...";
   if (hs.m_iCookie != cookie_val)
   {
       cookie_val = bake(addr, cookie_val, -1); // SHOULD generate an earlier, distracted cookie

       if (hs.m_iCookie != cookie_val)
       {
           LOGC(mglog.Debug) << "processConnectRequest: ...wrong cookie " << hex << cookie_val << ". Ignoring.";
           return int(URQ_CONCLUSION); // Don't look at me, I just change integers to symbols!
       }

       LOGC(mglog.Debug) << "processConnectRequest: ... correct (FIXED) cookie. Proceeding.";
   }
   else
   {
       LOGC(mglog.Debug) << "processConnectRequest: ... correct (ORIGINAL) cookie. Proceeding.";
   }

   int32_t id = hs.m_iID;

   // HANDSHAKE: The old client sees the version that does not match HS_VERSION_UDT4 (5).
   // In this case it will respond with URQ_ERROR_REJECT. Rest of the data are the same
   // as in the handshake request. When this message is received, the connector side should
   // switch itself to the version number HS_VERSION_UDT4 and continue the old way (that is,
   // continue sending URQ_INDUCTION, but this time with HS_VERSION_UDT4).

   bool accepted_hs = true;

   if (hs.m_iVersion == HS_VERSION_SRT1)
   {
       // No further check required.
       // The m_iType contains flags informing about attached extensions.
   }
   else if (hs.m_iVersion == HS_VERSION_UDT4)
   {
       // Check additionally if the socktype also matches (OLD UDT compatibility).
       // WARNING. The hs.m_iType has a different meaning in HS_VERSION_SRT1. In SRT version
       // the socket may only be SOCK_DGRAM.
       if (hs.m_iType != m_iSockType)
           accepted_hs = false;
   }
   else
   {
       // Unsupported version
       // (NOTE: This includes "version=0" which is a rejection flag).
       accepted_hs = false;
   }

   if (!accepted_hs)
   {
       LOGC(mglog.Debug) << "processConnectRequest: version/type mismatch. Sending URQ_ERROR_REJECT.";
       // mismatch, reject the request
       hs.m_iReqType = URQ_ERROR_REJECT;
       size_t size = CHandShake::m_iContentSize;
       hs.store_to(packet.m_pcData, Ref(size));
       packet.m_iID = id;
       packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
       m_pSndQueue->sendto(addr, packet);
   }
   else
   {
       int result = s_UDTUnited.newConnection(m_SocketID, addr, &hs, packet);
       // --->
       //        (global.) CUDTUnited::updateListenerMux
       //        (new Socket.) CUDT::acceptAndRespond
       if (result == -1)
       {
           hs.m_iReqType = URQ_ERROR_REJECT;
           LOGC(mglog.Error).form("UU:newConnection: rsp(REJECT): %d", URQ_ERROR_REJECT);
       }

       // XXX developer disorder warning!
       //
       // The newConnection() will call acceptAndRespond() if the processing
       // was successful - IN WHICH CASE THIS PROCEDURE SHOULD DO NOTHING.
       // Ok, almost nothing - see update_events below.
       //
       // If newConnection() failed, acceptAndRespond() will not be called.
       // Ok, more precisely, the thing that acceptAndRespond() is expected to do
       // will not be done (this includes sending any response to the peer).
       //
       // Now read CAREFULLY. The newConnection() will return:
       //
       // - -1: The connection processing failed due to errors like:
       //       - memory alloation error
       //       - listen backlog exceeded
       //       - any error propagated from CUDT::open and CUDT::acceptAndRespond
       // - 0: The connection already exists
       // - 1: Connection accepted.
       //
       // So, update_events is called only if the connection is established.
       // Both 0 (repeated) and -1 (error) require that a response be sent.
       // The CPacket object that has arrived as a connection request is here
       // reused for the connection rejection response (see URQ_ERROR_REJECT set
       // as m_iReqType).

       // send back a response if connection failed or connection already existed
       // new connection response should be sent in acceptAndRespond()
       if (result != 1)
       {
           LOGC(mglog.Debug) << CONID() << "processConnectRequest: sending ABNORMAL handshake info req=" << RequestTypeStr(hs.m_iReqType);
           size_t size = CHandShake::m_iContentSize;
           hs.store_to(packet.m_pcData, Ref(size));
           packet.m_iID = id;
           packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
           m_pSndQueue->sendto(addr, packet);
       }
       else
       {
           // a new connection has been created, enable epoll for write
           s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
       }
   }
   LOGC(mglog.Note) << "listen ret: " << hs.m_iReqType << " - " << RequestTypeStr(hs.m_iReqType);

   return hs.m_iReqType;
}

void CUDT::addLossRecord(std::vector<int32_t>& lr, int32_t lo, int32_t hi)
{
    if ( lo == hi )
        lr.push_back(lo);
    else
    {
        lr.push_back(lo | LOSSDATA_SEQNO_RANGE_FIRST);
        lr.push_back(hi);
    }
}

void CUDT::checkTimers()
{
   // update CC parameters
   CCUpdate();
   //uint64_t minint = (uint64_t)(m_ullCPUFrequency * m_pSndTimeWindow->getMinPktSndInt() * 0.9);
   //if (m_ullInterval < minint)
   //   m_ullInterval = minint;

   uint64_t currtime;
   CTimer::rdtsc(currtime);

   // This is a very heavy log, unblock only for temporary debugging!
#if 0
   LOGC(mglog.Debug) << CONID() << "checkTimers: nextacktime=" << logging::FormatTime(m_ullNextACKTime)
       << " AckInterval=" << m_iACKInterval
       << " pkt-count=" << m_iPktCount << " liteack-count=" << m_iLightACKCount;
#endif

   if ((currtime > m_ullNextACKTime) || ((m_iACKInterval > 0) && (m_iACKInterval <= m_iPktCount)))
   {
      // ACK timer expired or ACK interval is reached

      sendCtrl(UMSG_ACK);
      CTimer::rdtsc(currtime);
      if (m_iACKPeriod > 0)
         m_ullNextACKTime = currtime + m_iACKPeriod * m_ullCPUFrequency;
      else
         m_ullNextACKTime = currtime + m_ullACKInt;

      m_iPktCount = 0;
      m_iLightACKCount = 1;
   }
   else if (m_iSelfClockInterval * m_iLightACKCount <= m_iPktCount)
   {
      //send a "light" ACK
      sendCtrl(UMSG_ACK, NULL, NULL, SEND_LITE_ACK);
      ++ m_iLightACKCount;
   }

#ifdef SRT_ENABLE_NAKREPORT
   /*
   * Enable NAK reports for SRT.
   * Retransmission based on timeout is bandwidth consuming,
   * not knowing what to retransmit when the only NAK sent by receiver is lost,
   * all packets past last ACK are retransmitted (SRT_ENABLE_FASTREXMIT).
   */
   if ((currtime > m_ullNextNAKTime) && m_bRcvNakReport && (m_pRcvLossList->getLossLength() > 0))
   {
      // NAK timer expired, and there is loss to be reported.
      sendCtrl(UMSG_LOSSREPORT);

      CTimer::rdtsc(currtime);
      m_ullNextNAKTime = currtime + m_ullNAKInt;
   }
#else
   // we are not sending back repeated NAK anymore and rely on the sender's EXP for retransmission
   //if ((m_pRcvLossList->getLossLength() > 0) && (currtime > m_ullNextNAKTime))
   //{
   //   // NAK timer expired, and there is loss to be reported.
   //   sendCtrl(UMSG_LOSSREPORT);
   //
   //   CTimer::rdtsc(currtime);
   //   m_ullNextNAKTime = currtime + m_ullNAKInt;
   //}
#endif

   uint64_t next_exp_time;
   if (m_bUserDefinedRTO)
      next_exp_time = m_ullLastRspTime + m_iRTO * m_ullCPUFrequency;
   else
   {
      uint64_t exp_int = (m_iEXPCount * (m_iRTT + 4 * m_iRTTVar) + CPacket::SYN_INTERVAL) * m_ullCPUFrequency;
      if (exp_int < m_iEXPCount * m_ullMinExpInt)
         exp_int = m_iEXPCount * m_ullMinExpInt;
      next_exp_time = m_ullLastRspTime + exp_int;
   }

   if (currtime > next_exp_time)
   {
      // Haven't receive any information from the peer, is it dead?!
      // timeout: at least 16 expirations and must be greater than 5 seconds
      // XXX USE Constants for these 16 exp and 5 seconds
      if ((m_iEXPCount > 16) && (currtime - m_ullLastRspTime > 5000000 * m_ullCPUFrequency))
      {
         //
         // Connection is broken.
         // UDT does not signal any information about this instead of to stop quietly.
         // Application will detect this when it calls any UDT methods next time.
         //
         LOGC(mglog.Debug).form("connection expired after: %llu", (unsigned long long)(currtime - m_ullLastRspTime)/m_ullCPUFrequency);
         m_bClosing = true;
         m_bBroken = true;
         m_iBrokenCounter = 30;

         // update snd U list to remove this socket
         m_pSndQueue->m_pSndUList->update(this);

         releaseSynch();

         // app can call any UDT API to learn the connection_broken error
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR, true);

         CTimer::triggerEvent();

         return;
      }

      // sender: Insert all the packets sent after last received acknowledgement into the sender loss list.
      // recver: Send out a keep-alive packet
      if (m_pSndBuffer->getCurrBufSize() > 0)
      {
#ifdef SRT_ENABLE_FASTREXMIT
         /* 
         * Do nothing here, UDT retransmits unacknowledged packet only when nothing in the loss list.
         * This does not work well for real-time data that is delayed too much.
         * See fast retransmit handling later in function
         */
         ;
#else  /* SRT_ENABLE_FASTREXMIT */
#ifdef SRT_ENABLE_TLPKTDROP
         // protect packet retransmission
         CGuard::enterCS(m_AckLock);
#endif

         // FASTREXMIT works only under the following conditions:
         // - the "ACK window" is nonempty (there are some packets sent, but not ACK-ed)
         // - the sender loss list is empty (the receiver didn't send any LOSSREPORT, or LOSSREPORT was lost on track)
         // Otherwise the rexmit will be done EXCLUSIVELY basing on the received LOSSREPORTs.
         if ((CSeqNo::incseq(m_iSndCurrSeqNo) != m_iSndLastAck) && (m_pSndLossList->getLossLength() == 0))
         {
            // resend all unacknowledged packets on timeout, but only if there is no packet in the loss list
            int32_t csn = m_iSndCurrSeqNo;
            int num = m_pSndLossList->insert(m_iSndLastAck, csn);
            if (num > 0) {
// HAIVISION KULABYTE MODIFIED - MARC
               m_iTraceSndLoss += 1; // num;
               m_iSndLossTotal += 1; // num;
// HAIVISION KULABYTE MODIFIED - MARC

               LOGC(mglog.Debug) << CONID() << "ENFORCED reXmit by ACK-TMOUT (scheduling): " << CSeqNo::incseq(m_iSndLastAck) << "-" << csn
                   << " (" << CSeqNo::seqcmp(csn, m_iSndLastAck) << " packets)";
            }
         }
#ifdef SRT_ENABLE_TLPKTDROP
         // protect packet retransmission
         CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */

         checkSndTimers(DONT_REGEN_KM);
         CCUpdate();

         // immediately restart transmission
         m_pSndQueue->m_pSndUList->update(this);
#endif /* SRT_ENABLE_FASTREXMIT */
      }
      else
      {
#if !defined(SRT_FIX_KEEPALIVE)
         sendCtrl(UMSG_KEEPALIVE);
#endif
         LOGC(mglog.Debug) << CONID() << "(FIX) NOT SENDING KEEPALIVE";
      }
      ++ m_iEXPCount;
#if !defined(SRT_FIX_KEEPALIVE)
      /*
      * duB:
      * It seems there is confusion of the direction of the Response here.
      * LastRspTime is supposed to be when receiving (data/ctrl) from peer
      * as shown in processCtrl and processData,
      * Here we set because we sent something?
      *
      * Disabling this code that prevent quick reconnection when peer disappear
      */
      // Reset last response time since we just sent a heart-beat.
      m_ullLastRspTime = currtime;
#endif
   }
#ifdef SRT_ENABLE_FASTREXMIT
   // sender: Insert some packets sent after last received acknowledgement into the sender loss list.
   //         This handles retransmission on timeout for lost NAK for peer sending only one NAK when loss detected.
   //         Not required if peer send Periodic NAK Reports.
   if ((1)
#ifdef SRT_ENABLE_NAKREPORT
   &&  !m_bPeerNakReport 
#endif
   &&  m_pSndBuffer->getCurrBufSize() > 0)
   {
      uint64_t exp_int = (m_iReXmitCount * (m_iRTT + 4 * m_iRTTVar + 2 * CPacket::SYN_INTERVAL) + CPacket::SYN_INTERVAL) * m_ullCPUFrequency;

      if (currtime > (m_ullLastRspAckTime + exp_int))
      {
#ifdef SRT_ENABLE_TLPKTDROP
         // protect packet retransmission
         CGuard::enterCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */
         if ((CSeqNo::seqoff(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0))
         {
            // resend all unacknowledged packets on timeout
            int32_t csn = m_iSndCurrSeqNo;
            int num = m_pSndLossList->insert(m_iSndLastAck, csn);
#if ENABLE_LOGGING
            LOGC(mglog.Debug) << CONID() << "ENFORCED reXmit by ACK-TMOUT PREPARED: " << CSeqNo::incseq(m_iSndLastAck) << "-" << csn
                << " (" << CSeqNo::seqcmp(csn, m_iSndLastAck) << " packets)";

            LOGC(mglog.Debug).form( "timeout lost: pkts=%d rtt+4*var=%d cnt=%d diff=%llu", num,
                   m_iRTT + 4 * m_iRTTVar, m_iReXmitCount, (unsigned long long)(currtime - (m_ullLastRspAckTime + exp_int)));
#endif
            if (num > 0) {
// HAIVISION KULABYTE MODIFIED - MARC
               m_iTraceSndLoss += 1; // num;
               m_iSndLossTotal += 1; // num;
// HAIVISION KULABYTE MODIFIED - MARC
            }
         }
#ifdef SRT_ENABLE_TLPKTDROP
         // protect packet retransmission
         CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_TLPKTDROP */

         ++m_iReXmitCount;

         checkSndTimers(DONT_REGEN_KM);
         CCUpdate();

         // immediately restart transmission
         m_pSndQueue->m_pSndUList->update(this);
      }
   }
#endif /* SRT_ENABLE_FASTREXMIT */

#ifdef SRT_FIX_KEEPALIVE
//   uint64_t exp_int = (m_iRTT + 4 * m_iRTTVar + CPacket::SYN_INTERVAL) * m_ullCPUFrequency;
   if (currtime > m_ullLastSndTime + (1000000 * m_ullCPUFrequency))
   {
      sendCtrl(UMSG_KEEPALIVE);
      LOGP(mglog.Debug, "KEEPALIVE");
   }
#endif /* SRT_FIX_KEEPALIVE */
}

void CUDT::addEPoll(const int eid)
{
   CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
   m_sPollID.insert(eid);
   CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);

   if (!m_bConnected || m_bBroken || m_bClosing)
      return;

/* new code */
   CGuard::enterCS(m_RecvLock);
   if (m_pRcvBuffer->isRcvDataReady())
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
   }
   CGuard::leaveCS(m_RecvLock);
/* (OLD CODE)
   if (((m_iSockType == UDT_DGRAM) && (m_pRcvBuffer->getRcvMsgNum() > 0))
           ||  ((m_iSockType == UDT_STREAM) &&  m_pRcvBuffer->isRcvDataReady()))
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
   }
*/
   if (m_iSndBufSize > m_pSndBuffer->getCurrBufSize())
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
   }
}

void CUDT::removeEPoll(const int eid)
{
   // clear IO events notifications;
   // since this happens after the epoll ID has been removed, they cannot be set again
   set<int> remove;
   remove.insert(eid);
   s_UDTUnited.m_EPoll.update_events(m_SocketID, remove, UDT_EPOLL_IN | UDT_EPOLL_OUT, false);

   CGuard::enterCS(s_UDTUnited.m_EPoll.m_EPollLock);
   m_sPollID.erase(eid);
   CGuard::leaveCS(s_UDTUnited.m_EPoll.m_EPollLock);
}
