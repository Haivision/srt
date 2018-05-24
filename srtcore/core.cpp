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
#include "queue.h"
#include "core.h"
#include "logging.h"

// Again, just in case when some "smart guy" provided such a global macro
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifdef SRT_ENABLE_SRTCC_EMB
#include "csrtcc.h"
//#define CSRTCC CSRTCC
#endif /* SRT_ENABLE_SRTCC_EMB */

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

logging::LogConfig logger_config (logger_fa_all.allfa);

logging::Logger glog(SRT_LOGFA_GENERAL, &logger_config, "SRT.g");
logging::Logger blog(SRT_LOGFA_BSTATS, &logger_config, "SRT.b");
logging::Logger mglog(SRT_LOGFA_CONTROL, &logger_config, "SRT.c");
logging::Logger dlog(SRT_LOGFA_DATA, &logger_config, "SRT.d");
logging::Logger tslog(SRT_LOGFA_TSBPD, &logger_config, "SRT.t");
logging::Logger rxlog(SRT_LOGFA_REXMIT, &logger_config, "SRT.r");

CUDTUnited CUDT::s_UDTUnited;

const UDTSOCKET CUDT::INVALID_SOCK = -1;
const int CUDT::ERROR = -1;

const UDTSOCKET UDT::INVALID_SOCK = CUDT::INVALID_SOCK;
const int UDT::ERROR = CUDT::ERROR;

const int32_t CSeqNo::m_iSeqNoTH = 0x3FFFFFFF;
const int32_t CSeqNo::m_iMaxSeqNo = 0x7FFFFFFF;
const int32_t CAckNo::m_iMaxAckSeqNo = 0x7FFFFFFF;

//const int32_t CMsgNo::m_iMsgNoTH = 0x03FFFFFF;
//const int32_t CMsgNo::m_iMaxMsgNo = 0x07FFFFFF;

// XXX This is moved to packet.h in-class definition
#ifdef SRT_ENABLE_TSBPD
#ifdef SRT_DEBUG_TSBPD_WRAP //Receiver
//const uint32_t CPacket::MAX_TIMESTAMP = 0x07FFFFFF; //27 bit fast wraparound for tests (~2m15s)
#else
//const uint32_t CPacket::MAX_TIMESTAMP = 0xFFFFFFFF; //Full 32 bit (01h11m35s)
#endif
#endif /* SRT_ENABLE_TSBPD */

const int CUDT::m_iVersion = 4;
const int CUDT::m_iSYNInterval = 10000;
const int CUDT::m_iSelfClockInterval = 64;

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
#ifdef SRT_ENABLE_TSBPD
   m_bTsbPdMode = true;        //Enable TsbPd on sender
   m_iTsbPdDelay = 120;          //Receiver TsbPd delay (mSec)
#ifdef SRT_ENABLE_TLPKTDROP
   m_bTLPktDrop = true;         //Too-late Packet Drop
#endif /* SRT_ENABLE_TLPKTDROP */
   //Runtime
   m_bTsbPdSnd = false;
   m_SndTsbPdDelay = 0;
   m_bTsbPdRcv = false;
   m_RcvTsbPdDelay = 0;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bTLPktDropSnd = false;
#endif /* SRT_ENABLE_TLPKTDROP */
#endif /* SRT_ENABLE_TSBPD */
#ifdef SRT_ENABLE_NAKREPORT
   m_bRcvNakReport = true;      //Receiver's Periodic NAK Reports
   m_iMinNakInterval = 20000;   //Minimum NAK Report Period (usec)
   m_iNakReportAccel = 2;       //Default NAK Report Period (RTT) accelerator
#endif /* SRT_ENABLE_NAKREPORT */
#ifdef SRT_ENABLE_INPUTRATE
   m_llInputBW = 0;             // Application provided input bandwidth (internal input rate sampling == 0)
   m_iOverheadBW = 25;          // Percent above input stream rate (applies if m_llMaxBW == 0)
#endif
   m_bTwoWayData = false;

#ifdef SRT_ENABLE_SRTCC_EMB
   m_pCCFactory = new CCCFactory<CSRTCC>;
#else /* SRT_ENABLE_SRTCC_EMB */
   m_pCCFactory = new CCCFactory<CUDTCC>;
#endif /* SRT_ENABLE_SRTCC_EMB */
   m_pCC = NULL;
   m_pSRTCC = NULL;
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
#ifdef SRT_ENABLE_INPUTRATE
   m_llInputBW = ancestor.m_llInputBW;
   m_iOverheadBW = ancestor.m_iOverheadBW;
#endif
   m_bDataSender = ancestor.m_bDataSender;
   m_bTwoWayData = ancestor.m_bTwoWayData;
#ifdef SRT_ENABLE_TSBPD
   m_bTsbPdMode = ancestor.m_bTsbPdMode;
   m_iTsbPdDelay = ancestor.m_iTsbPdDelay;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bTLPktDrop = ancestor.m_bTLPktDrop;
#endif /* SRT_ENABLE_TLPKTDROP */
   //Runtime
   m_bTsbPdSnd = false;
   m_SndTsbPdDelay = 0;
   m_bTsbPdRcv = false;
   m_RcvTsbPdDelay = 0;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bTLPktDropSnd = false;
#endif /* SRT_ENABLE_TLPKTDROP */
#endif /* SRT_ENABLE_TSBPD */
#ifdef SRT_ENABLE_NAKREPORT
   m_bRcvNakReport = ancestor.m_bRcvNakReport;
   m_iMinNakInterval = ancestor.m_iMinNakInterval;
   m_iNakReportAccel = ancestor.m_iNakReportAccel;
#endif /* SRT_ENABLE_NAKREPORT */

   m_CryptoSecret = ancestor.m_CryptoSecret;
   m_iSndCryptoKeyLen = ancestor.m_iSndCryptoKeyLen;

   m_pCCFactory = ancestor.m_pCCFactory->clone();
   m_pCC = NULL;
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
   delete m_pCCFactory;
   delete m_pCC;
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

   case UDT_CC:
      if (m_bConnecting || m_bConnected)
         throw CUDTException(MJ_NOTSUP, MN_ISBOUND, 0);
      if (m_pCCFactory != NULL)
         delete m_pCCFactory;
      m_pCCFactory = ((CCCVirtualFactory *)optval)->clone();

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
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_llMaxBW != 0)
      {  //Absolute MaxBW setting
         if (m_pSRTCC != NULL) m_pSRTCC->setMaxBW(m_llMaxBW); //Bytes/sec
#ifdef SRT_ENABLE_INPUTRATE
         if (m_pSndBuffer != NULL) m_pSndBuffer->setInputRateSmpPeriod(0);
      }
      else if (m_llInputBW != 0)
      {  //Application provided input rate  
         if (m_pSRTCC)
             m_pSRTCC->setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
         if (m_pSndBuffer != NULL)
             m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
      }
      else
      {  //Internal input rate sampling
         if (m_pSndBuffer != NULL) m_pSndBuffer->setInputRateSmpPeriod(500000);
#endif /* SRT_ENABLE_INPUTRATE */
      }
#endif /* SRT_ENABLE_SRTCC_EMB */
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

#ifdef SRT_ENABLE_INPUTRATE
   case SRT_INPUTBW:
      m_llInputBW = *(int64_t*)optval;
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_llMaxBW != 0)
      {  //Keep MaxBW setting
         ;
      }
      else if (m_llInputBW != 0)
      {  //Application provided input rate
         if (m_pSRTCC)
             m_pSRTCC->setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
         if (m_pSndBuffer != NULL)
             m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
      }
      else
      {  //Internal input rate sampling
         if (m_pSndBuffer != NULL)
             m_pSndBuffer->setInputRateSmpPeriod(500000); //Enable input rate sampling
      }
#endif /* SRT_ENABLE_SRTCC_EMB */
      break;

   case SRT_OHEADBW:
      if ((*(int*)optval < 5)
      ||  (*(int*)optval > 100))
         throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
      m_iOverheadBW = *(int*)optval;
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_llMaxBW != 0)
      {  //Keep MaxBW setting
         ;
      }
      else if (m_llInputBW != 0)
      {  //Adjust MaxBW for new overhead
         if (m_pSRTCC)
             m_pSRTCC->setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
      }
      //else 
         // Keep input rate sampling setting, next CCupdate will adjust MaxBW
#endif /* SRT_ENABLE_SRTCC_EMB */
      break;
#endif /* SRT_ENABLE_INPUTRATE */

   case SRT_SENDER:
      if (m_bConnected)
         throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
      m_bDataSender = bool_int_value(optval, optlen);
      break;

   case SRT_TWOWAYDATA:
      if (m_bConnected)
         throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
      m_bTwoWayData = bool_int_value(optval, optlen);
      break;

#ifdef SRT_ENABLE_TSBPD
   case SRT_TSBPDMODE:
      if (m_bConnected)
         throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
      m_bTsbPdMode = bool_int_value(optval, optlen);
      break;

   case SRT_TSBPDDELAY:
      if (m_bConnected)
         throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
      m_iTsbPdDelay = *(int*)optval;
      break;

#ifdef SRT_ENABLE_TLPKTDROP
   case SRT_TSBPDMAXLAG:
      //Obsolete
      break;

   case SRT_TLPKTDROP:
      if (m_bConnected)
         throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
      m_bTLPktDrop = bool_int_value(optval, optlen);
      break;
#endif /* SRT_ENABLE_TLPKTDROP */
#endif /* SRT_ENABLE_TSBPD */

   case SRT_PASSPHRASE:
      if (m_bConnected)
         throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

      if ((optlen != 0)
      &&  (10 > optlen)
      &&  (HAICRYPT_SECRET_MAX_SZ < optlen))
         throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);

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

   default:
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);
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

   case UDT_CC:
      if (!m_bOpened)
         throw CUDTException(MJ_NOTSUP, MN_ISUNBOUND, 0);
      *(CCC**)optval = m_pCC;
      optlen = sizeof(CCC*);

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
#ifdef   SRT_ENABLE_TSBPD
         CGuard::enterCS(m_RecvLock);
         if (m_pRcvBuffer && m_pRcvBuffer->isRcvDataReady())
            event |= UDT_EPOLL_IN;
         CGuard::leaveCS(m_RecvLock);
#else    /* SRT_ENABLE_TSBPD */
         if (m_pRcvBuffer && m_pRcvBuffer->isRcvDataReady())
            event |= UDT_EPOLL_IN;
#endif   /* SRT_ENABLE_TSBPD */
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
#ifdef SRT_ENABLE_TSBPD
      if (m_pRcvBuffer)
      {
         CGuard::enterCS(m_RecvLock);
         *(int32_t*)optval = m_pRcvBuffer->getRcvDataSize();
         CGuard::leaveCS(m_RecvLock);
      }
#else /* SRT_ENABLE_TSBPD */
      if (m_pRcvBuffer)
         *(int32_t*)optval = m_pRcvBuffer->getRcvDataSize();
#endif/* SRT_ENABLE_TSBPD */
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

   case SRT_TWOWAYDATA:
      *(int32_t*)optval = m_bTwoWayData;
      optlen = sizeof(int32_t);
      break;

#ifdef SRT_ENABLE_TSBPD
   case SRT_TSBPDMODE:
      *(int32_t*)optval = m_bTsbPdMode;
      optlen = sizeof(int32_t);
      break;

   case SRT_TSBPDDELAY:
      *(int32_t*)optval = m_iTsbPdDelay;
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
#endif /* SRT_ENABLE_TSBPD */

   case SRT_PBKEYLEN:
      /*
      * Before TWOWAY support this was returning the sender's keylen from both side of the connection when connected.
      * Maintain binary compatibility for sender-only and receiver-only peers.
      */
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_pSRTCC)
         *(int32_t*)optval = (m_bDataSender || m_bTwoWayData) ? m_pSRTCC->m_iSndKmKeyLen : m_pSRTCC->m_iRcvKmKeyLen;
      else
#endif
         *(int32_t*)optval = (m_bDataSender || m_bTwoWayData) ? m_iSndCryptoKeyLen : 0;
      optlen = sizeof(int32_t);
      break;

   case SRT_SNDPBKEYLEN:
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_pSRTCC)
         *(int32_t*)optval = m_pSRTCC->m_iSndKmKeyLen;
      else
#endif
         *(int32_t*)optval = m_iSndCryptoKeyLen;
      optlen = sizeof(int32_t);
      break;

   case SRT_RCVPBKEYLEN:
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_pSRTCC)
         *(int32_t*)optval = m_pSRTCC->m_iRcvKmKeyLen;
      else
#endif
         *(int32_t*)optval = 0; //Defined on sender's side only
      optlen = sizeof(int32_t);
      break;

   case SRT_SNDPEERKMSTATE: /* Sender's peer decryption state */
      /*
      * Was SRT_KMSTATE (receiver's decryption state) before TWOWAY support,
      * where sender reports peer (receiver) state and the receiver reports local state when connected.
      * Maintain binary compatibility and return what SRT_RCVKMSTATE returns for receive-only connected peer.
      */
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_pSRTCC)
         *(int32_t*)optval = (m_bDataSender || m_bTwoWayData) ? m_pSRTCC->m_iSndPeerKmState : m_pSRTCC->m_iRcvKmState;
      else
#endif
         *(int32_t*)optval = SRT_KM_S_UNSECURED;
      optlen = sizeof(int32_t);
      break;

   case SRT_RCVKMSTATE: /* Receiver decryption state */
#ifdef SRT_ENABLE_SRTCC_EMB
      if (m_pSRTCC)
         *(int32_t*)optval = (m_bDataSender || m_bTwoWayData) ? m_pSRTCC->m_iSndPeerKmState : m_pSRTCC->m_iRcvKmState;
      else
#endif
         *(int32_t*)optval = SRT_KM_S_UNSECURED;
      optlen = sizeof(int32_t);
      break;

#ifdef SRT_ENABLE_NAKREPORT
   case SRT_RCVNAKREPORT:
      *(bool*)optval = m_bRcvNakReport;
      optlen = sizeof(bool);
      break;
#endif /* SRT_ENABLE_NAKREPORT */

#ifdef SRT_ENABLE_SRTCC_EMB
   case SRT_AGENTVERSION:
      if (m_pSRTCC)
         *(int32_t*)optval = m_pSRTCC->m_SrtVersion;
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;

   case SRT_PEERVERSION:
      if (m_pSRTCC)
         *(int32_t*)optval = m_pSRTCC->getPeerSrtVersion();
      else
         *(int32_t*)optval = 0;
      optlen = sizeof(int32_t);
      break;
#endif

#ifdef SRT_ENABLE_CONNTIMEO
   case SRT_CONNTIMEO:
      *(int*)optval = m_iConnTimeOut;
      optlen = sizeof(int);
      break;
#endif

   default:
      throw CUDTException(MJ_NOTSUP, MN_NONE, 0);
   }
}

void CUDT::clearData()
{
   // Initial sequence number, loss, acknowledgement, etc.
   m_iPktSize = m_iMSS - CPacket::UDP_HDR_SIZE;
   m_iPayloadSize = m_iPktSize - CPacket::HDR_SIZE;

   m_iEXPCount = 1;
   m_iBandwidth = 1;    //pkts/sec
#ifdef SRT_ENABLE_BSTATS
   // XXX use some constant for this 16
   m_iDeliveryRate = 16 * m_iPayloadSize;
#else
   m_iDeliveryRate = 16;
#endif
   m_iAckSeqNo = 0;
   m_ullLastAckTime = 0;

   // trace information
   m_StartTime = CTimer::getTime();
   m_llSentTotal = m_llRecvTotal = m_iSndLossTotal = m_iRcvLossTotal = m_iRetransTotal = m_iSentACKTotal = m_iRecvACKTotal = m_iSentNAKTotal = m_iRecvNAKTotal = 0;
   m_LastSampleTime = CTimer::getTime();
   m_llTraceSent = m_llTraceRecv = m_iTraceSndLoss = m_iTraceRcvLoss = m_iTraceRetrans = m_iSentACK = m_iRecvACK = m_iSentNAK = m_iRecvNAK = 0;
   m_iTraceRcvRetrans = 0;
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

#ifdef SRT_ENABLE_BSTATS
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
#endif /* SRT_ENABLE_BSTATS */

#ifdef SRT_ENABLE_TSBPD
   m_bTsbPdSnd = false;
   m_SndTsbPdDelay = 0;
   m_bTsbPdRcv = false;
   m_RcvTsbPdDelay = 0;
#ifdef SRT_ENABLE_TLPKTDROP
   m_bTLPktDropSnd = false;
#endif /* SRT_ENABLE_TLPKTDROP */
#endif /* SRT_ENABLE_TSBPD */

#ifdef SRT_ENABLE_NAKREPORT
   m_bSndPeerNakReport = false;
#endif /* SRT_ENABLE_NAKREPORT */

   m_bPeerRexmitFlag = false;

   m_llSndDuration = m_llSndDurationTotal = 0;

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

   m_iRTT = 10 * m_iSYNInterval;
   m_iRTTVar = m_iRTT >> 1;
   m_ullCPUFrequency = CTimer::getCPUFrequency();

   // set up the timers
   m_ullSYNInt = m_iSYNInterval * m_ullCPUFrequency;

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

void CUDT::connect(const sockaddr* serv_addr, int32_t forced_isn)
{
   CGuard cg(m_ConnectionLock);

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
   if (m_bRendezvous)
      ttl *= 10;
   ttl += CTimer::getTime();
   m_pRcvQueue->registerConnector(m_SocketID, this, m_iIPversion, serv_addr, ttl);

   // This is my current configurations
   m_ConnReq.m_iVersion = m_iVersion;
   m_ConnReq.m_iType = m_iSockType;
   m_ConnReq.m_iMSS = m_iMSS;
   m_ConnReq.m_iFlightFlagSize = (m_iRcvBufSize < m_iFlightFlagSize)? m_iRcvBufSize : m_iFlightFlagSize;
   m_ConnReq.m_iReqType = (!m_bRendezvous) ? URQ_INDUCTION : URQ_RENDEZVOUS;
   m_ConnReq.m_iID = m_SocketID;
   CIPAddress::ntop(serv_addr, m_ConnReq.m_piPeerIP, m_iIPversion);

   if ( forced_isn == 0 )
   {
       // Random Initial Sequence Number
       srand((unsigned int)CTimer::getTime());
       m_iISN = m_ConnReq.m_iISN = (int32_t)(CSeqNo::m_iMaxSeqNo * (double(rand()) / RAND_MAX));
   }
   else
   {
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
   CPacket request;
   char* reqdata = new char [m_iPayloadSize];
   request.pack(UMSG_HANDSHAKE, NULL, reqdata, m_iPayloadSize);
   // ID = 0, connection request
   request.m_iID = 0;

   int hs_size = m_iPayloadSize;
   m_ConnReq.serialize(reqdata, hs_size);
   request.setLength(hs_size);

#ifdef SRT_ENABLE_CTRLTSTAMP
   uint64_t now = CTimer::getTime();
   request.m_iTimeStamp = int(now - m_StartTime);
#elif defined(HAI_PATCH)
   uint64_t now = CTimer::getTime();
#endif

   LOGC(mglog.Debug) << CONID() << "CUDT::connect: sending UDT handshake for socket=" << m_ConnReq.m_iID;

#ifdef HAI_PATCH
   /*
   * Race condition if non-block connect response thread scheduled before we set m_bConnecting to true?
   * Connect response will be ignored and connecting will wait until timeout.
   * Maybe m_ConnectionLock handling problem? Not used in CUDT::connect(const CPacket& response)
   */
   m_llLastReqTime = now;
   m_bConnecting = true;
   m_pSndQueue->sendto(serv_addr, request);
#else  /* HAI_PATCH */
   m_pSndQueue->sendto(serv_addr, request);
   m_llLastReqTime = CTimer::getTime();

   m_bConnecting = true;
#endif /* HAI_PATCH */

   // asynchronous connect, return immediately
   if (!m_bSynRecving)
   {
      delete [] reqdata;
      return;
   }

   // Wait for the negotiated configurations from the peer side.
   CPacket response;
   char* resdata = new char [m_iPayloadSize];
   response.pack(UMSG_HANDSHAKE, NULL, resdata, m_iPayloadSize);

   CUDTException e;

   while (!m_bClosing)
   {
      // avoid sending too many requests, at most 1 request per 250ms
      if (CTimer::getTime() - m_llLastReqTime > 250000)
      {
         m_ConnReq.serialize(reqdata, hs_size);
         request.setLength(hs_size);
         if (m_bRendezvous)
            request.m_iID = m_ConnRes.m_iID;
#ifdef SRT_ENABLE_CTRLTSTAMP
         now = CTimer::getTime();
         m_llLastReqTime = now;
         request.m_iTimeStamp = int(now - m_StartTime);
         m_pSndQueue->sendto(serv_addr, request);
#else /* SRT_ENABLE_CTRLTSTAMP */
         m_pSndQueue->sendto(serv_addr, request);
         m_llLastReqTime = CTimer::getTime();
#endif /* SRT_ENABLE_CTRLTSTAMP */
      }

      response.setLength(m_iPayloadSize);
      if (m_pRcvQueue->recvfrom(m_SocketID, response) > 0)
      {
         if (processConnectResponse(response) <= 0)
            break;

         // new request/response should be sent out immediately on receving a response
         m_llLastReqTime = 0;
      }

      if (CTimer::getTime() > ttl)
      {
         // timeout
         e = CUDTException(MJ_SETUP, MN_TIMEOUT, 0);
         break;
      }
   }

   delete [] reqdata;
   delete [] resdata;

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

   LOGC(mglog.Debug) << CONID() << "CUDT::connect: handshake exchange succeeded";
}

int CUDT::processConnectResponse(const CPacket& response) ATR_NOEXCEPT
{
    // NOTE: ASSUMED LOCK ON: m_ConnectionLock.

   // this is the 2nd half of a connection request. If the connection is setup successfully this returns 0.
   // returning -1 means there is an error.
   // returning 1 or 2 means the connection is in process and needs more handshake

   if (!m_bConnecting)
      return -1;

   /* SRT peer may send the SRT handshake private message (type 0x7fff) before a keep-alive */
   // This condition is checked when the current agent is trying to do connect() in rendezvous mode,
   // but the peer was faster to send a handshake packet earlier. This makes it continue with connecting
   // process if the peer is already behaving as if the connection was already established.
   if (m_bRendezvous
           && (
               !response.isControl()                         // WAS A PAYLOAD PACKET.
                || (response.getType() == UMSG_KEEPALIVE)    // OR WAS A UMSG_KEEPALIVE message.
                || (response.getType() == UMSG_EXT)          // OR WAS a CONTROL packet of some extended type (i.e. any SRT specific)
           )
             // This may happen if this is an initial state in which the socket type was not yet set.
             // If this is a field that holds the response handshake record from the peer, this means that it wasn't received yet.
           && (m_ConnRes.m_iType != UDT_UNDEFINED))
   {
      //a data packet or a keep-alive packet comes, which means the peer side is already connected
      // in this situation, the previously recorded response will be used
      goto POST_CONNECT;
   }

   if ( !response.isControl(UMSG_HANDSHAKE) )
       return -1;

   m_ConnRes.deserialize(response.m_pcData, response.getLength());

   if (m_bRendezvous)
   {
      // regular connect should NOT communicate with rendezvous connect
      // rendezvous connect require 3-way handshake
      if (m_ConnRes.m_iReqType == URQ_INDUCTION)
         return -1;

      if ( m_ConnReq.m_iReqType == URQ_RENDEZVOUS
        || m_ConnRes.m_iReqType == URQ_RENDEZVOUS )
      {
         m_ConnReq.m_iReqType = URQ_CONCLUSION;
         // the request time must be updated so that the next handshake can be sent out immediately.
         m_llLastReqTime = 0;
         return 1;
      }
   }
   else
   {
      // set cookie
      if (m_ConnRes.m_iReqType == URQ_INDUCTION)
      {
         m_ConnReq.m_iReqType = URQ_CONCLUSION;
         m_ConnReq.m_iCookie = m_ConnRes.m_iCookie;
         m_llLastReqTime = 0;
         return 1;
      }
   }

POST_CONNECT:
   // Remove from rendezvous queue
   m_pRcvQueue->removeConnector(m_SocketID);

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

   // Prepare all data structures
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
       // XXX Will cause error in C++11; the NOEXCEPT declaration
       // is false in this case. This is probably wrong - the function
       // should return -1 and set appropriate "errno".
       // The original UDT code contained throw() declaration; this would
       // make this instruction resolved to std::unexpected().
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
   }

   CInfoBlock ib;
   ib.m_iIPversion = m_iIPversion;
   CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
   if (m_pCache->lookup(&ib) >= 0)
   {
      m_iRTT = ib.m_iRTT;
      m_iBandwidth = ib.m_iBandwidth;
   }

   setupCC();

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

   return 0;
}

#ifdef SRT_ENABLE_TSBPD
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

                LOGC(tslog.Note) << self->CONID() << "TSBPD:DROPSEQ: up to seq=" << CSeqNo::decseq(skiptoseqno)
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
          LOGC(tslog.Debug) << self->CONID() << "PLAYING PACKET seq=" << seq << " (belated " << ((CTimer::getTime() - tsbpdtime)/1000.0) << "ms)";
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
          LOGC(tslog.Debug) << self->CONID() << "FUTURE PACKET seq=" << seq << " T=" << logging::FormatTime(tsbpdtime) << " - waiting " << ((tsbpdtime - now)/1000.0) << "ms";
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
         self->m_bTsbPdAckWakeup = true;
         THREAD_PAUSED();
         pthread_cond_wait(&self->m_RcvTsbPdCond, &self->m_RecvLock);
         THREAD_RESUMED();
      }
   }
   CGuard::leaveCS(self->m_RecvLock);
   THREAD_EXIT();
   return NULL;
}
#endif /* SRT_ENABLE_TSBPD */

void CUDT::acceptAndRespond(const sockaddr* peer, CHandShake* hs)
{
   CGuard cg(m_ConnectionLock);

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

   // get local IP address and send the peer its IP address (because UDP cannot get local IP address)
   memcpy(m_piSelfIP, hs->m_piPeerIP, 16);
   CIPAddress::ntop(peer, hs->m_piPeerIP, m_iIPversion);

   m_iPktSize = m_iMSS - CPacket::UDP_HDR_SIZE;
   m_iPayloadSize = m_iPktSize - CPacket::HDR_SIZE;

   // Prepare all structures
   try
   {
      m_pSndBuffer = new CSndBuffer(32, m_iPayloadSize);
      m_pRcvBuffer = new CRcvBuffer(&(m_pRcvQueue->m_UnitQueue), m_iRcvBufSize);
      m_pSndLossList = new CSndLossList(m_iFlowWindowSize * 2);
      m_pRcvLossList = new CRcvLossList(m_iFlightFlagSize);
   }
   catch (...)
   {
      throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
   }

   CInfoBlock ib;
   ib.m_iIPversion = m_iIPversion;
   CInfoBlock::convert(peer, m_iIPversion, ib.m_piIP);
   if (m_pCache->lookup(&ib) >= 0)
   {
      m_iRTT = ib.m_iRTT;
      m_iBandwidth = ib.m_iBandwidth;
   }

   setupCC();

   m_pPeerAddr = (AF_INET == m_iIPversion) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(m_pPeerAddr, peer, (AF_INET == m_iIPversion) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

   // And of course, it is connected.
   m_bConnected = true;

   // register this socket for receiving data packets
   m_pRNode->m_bOnList = true;
   m_pRcvQueue->setNewEntry(this);

   //send the response to the peer, see listen() for more discussions about this
   CPacket response;
   int size = CHandShake::m_iContentSize;
   char* buffer = new char[size];
   hs->serialize(buffer, size);
   response.pack(UMSG_HANDSHAKE, NULL, buffer, size);
   response.m_iID = m_PeerID;
#ifdef SRT_ENABLE_CTRLTSTAMP
   response.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
#endif
   m_pSndQueue->sendto(peer, response);

   delete [] buffer;
}

void CUDT::setupCC(void)
{
   m_pCC = m_pCCFactory->create();
   m_pSRTCC = dynamic_cast<CSRTCC*>(m_pCC); // will become NULL if CCC class is not CSRTCC.
   if ( !m_pSRTCC )
       throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
   m_pCC->m_UDT = m_SocketID;
   m_pCC->setMSS(m_iMSS);
   m_pCC->setMaxCWndSize(m_iFlowWindowSize);
   m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);
   m_pCC->setRcvRate(m_iDeliveryRate);
   m_pCC->setRTT(m_iRTT);
   m_pCC->setBandwidth(m_iBandwidth);
#ifdef SRT_ENABLE_SRTCC_EMB
   if (m_llMaxBW != 0)
   {
       m_pSRTCC->setMaxBW(m_llMaxBW); //Bytes/sec
#ifdef SRT_ENABLE_INPUTRATE
       m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
   }
   else if (m_llInputBW != 0)
   {
       m_pSRTCC->setMaxBW((m_llInputBW * (100 + m_iOverheadBW))/100); //Bytes/sec
       m_pSndBuffer->setInputRateSmpPeriod(0); //Disable input rate sampling
   }
   else
   {
       m_pSndBuffer->setInputRateSmpPeriod(500000); //Enable input rate sampling (fast start)
#endif /* SRT_ENABLE_INPUTRATE */
   }

   m_pSRTCC->setCryptoSecret(&m_CryptoSecret);
   if (m_bDataSender || m_bTwoWayData)
       m_pSRTCC->setSndCryptoKeylen(m_iSndCryptoKeyLen);

#ifdef SRT_ENABLE_TSBPD
   if (m_bDataSender || m_bTwoWayData)
       m_pSRTCC->setSndTsbPdMode(m_bTsbPdMode);
   m_pSRTCC->setTsbPdDelay(m_iTsbPdDelay);
#ifdef SRT_ENABLE_TLPKTDROP
   /*
   * Set SRT handshake receiver packet drop flag
   */
//   if (!m_bDataSender) 
      m_pSRTCC->setRcvTLPktDrop(m_bTLPktDrop);
#endif /* SRT_ENABLE_TLPKTDROP */
#endif /* SRT_ENABLE_TSBPD */
#ifdef SRT_ENABLE_NAKREPORT
   /*
   * Enable receiver's Periodic NAK Reports
   */
   m_pSRTCC->setRcvNakReport(m_bRcvNakReport);
   m_ullMinNakInt = m_iMinNakInterval * m_ullCPUFrequency;
#endif /* SRT_ENABLE_NAKREPORT */
#endif /* SRT_ENABLE_SRTCC_EMB */

   m_pCC->init();

   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;
   return;
}

void CUDT::close()
{
   if (!m_bOpened)
   {
      return;
   }

   if (m_Linger.l_onoff != 0)
   {
      uint64_t entertime = CTimer::getTime();

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

#ifdef HAI_PATCH
   /*
    * update_events below useless
    * removing usock for EPolls right after (remove_usocks) clears it (in other HAI patch).
    *
    * What is in EPoll shall be the responsibility of the application, if it want local close event,
    * it would remove the socket from the EPoll after close.
    */
#endif /* HAI_PATCH */
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

   if (!m_bOpened)
   {
      return;
   }

   // Inform the threads handler to stop.
   m_bClosing = true;

   CGuard cg(m_ConnectionLock);

   // Signal the sender and recver if they are waiting for data.
   releaseSynch();

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
         sendCtrl(UMSG_SHUTDOWN);

      m_pCC->close();

      // Store current connection information.
      CInfoBlock ib;
      ib.m_iIPversion = m_iIPversion;
      CInfoBlock::convert(m_pPeerAddr, m_iIPversion, ib.m_piIP);
      ib.m_iRTT = m_iRTT;
      ib.m_iBandwidth = m_iBandwidth;
      m_pCache->update(&ib);

      m_bConnected = false;
   }

   // waiting all send and recv calls to stop
   CGuard sendguard(m_SendLock);
   CGuard recvguard(m_RecvLock);

#ifdef SRT_ENABLE_SRTCC_EMB
   CGuard::enterCS(m_AckLock);
   /* Release crypto context under AckLock in cast decrypt is in progress */
   if (m_pSRTCC)
       m_pSRTCC->freeCryptoCtx();
   CGuard::leaveCS(m_AckLock);
#endif /* SRT_ENABLE_SRTCC_EMB */

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

#ifdef SRT_ENABLE_TSBPD
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

#else /* SRT_ENABLE_TSBPD */
   if (!m_pRcvBuffer->isRcvDataReady())
   {
      if (!m_bSynRecving)
      {
         throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
      }
      else
      {
          pthread_mutex_lock(&m_RecvDataLock);
          if (m_iRcvTimeOut < 0)
          {
              while (!m_bBroken && m_bConnected && !m_bClosing && !m_pRcvBuffer->isRcvDataReady())
              {
                  pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
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
                  pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime);
                  if (CTimer::getTime() >= exptime)
                      break;
              }
          }
          pthread_mutex_unlock(&m_RecvDataLock);
      }
   }
#endif /* SRT_ENABLE_TSBPD */

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   else if ((m_bBroken || m_bClosing) && !m_pRcvBuffer->isRcvDataReady())
      throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

   int res = m_pRcvBuffer->readBuffer(data, len);

#ifdef SRT_ENABLE_TSBPD
   /* Kick TsbPd thread to schedule next wakeup (if running) */
   if (m_bTsbPdRcv)
   {
      LOGP(tslog.Debug, "Ping TSBPD thread to schedule wakeup");
      pthread_cond_signal(&m_RcvTsbPdCond);
   }
#endif /* SRT_ENABLE_TSBPD */


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
   if (m_bTLPktDropSnd) 
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
      // XXX int msecThreshold = std::max(m_SndTsbPdDelay, SRT_TLPKTDROP_MINTHRESHOLD) + (2*m_iSYNInterval/1000);
      int msecThreshold = (m_SndTsbPdDelay > SRT_TLPKTDROP_MINTHRESHOLD ? m_SndTsbPdDelay : SRT_TLPKTDROP_MINTHRESHOLD)
                        + (2 * m_iSYNInterval / 1000);
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
      if ((uint32_t)timespan > (m_SndTsbPdDelay/2))
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
#ifdef HAI_PATCH
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

          return 0;
      }
#else
   }

   if ((m_iSndBufSize - m_pSndBuffer->getCurrBufSize()) * m_iPayloadSize < len)
   {
      if (m_iSndTimeOut >= 0)
         throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

      // XXX Not sure if this was intended:
      // The 'len' exceeds the bytes left in the send buffer...
      // ... so we do nothing and return success???
      return 0;
#endif
   }

   // record total time used for sending
   if (m_pSndBuffer->getCurrBufSize() == 0)
      m_llSndDurationCounter = CTimer::getTime();

   // insert the user buffer into the sending list
#ifdef SRT_ENABLE_SRCTIMESTAMP
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

#ifdef SRT_ENABLE_TSBPD

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
      if (m_bTsbPdRcv)
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
         if (m_bTsbPdRcv)
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
            if (m_bTsbPdRcv)
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
         if (m_bTsbPdRcv)
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
                 LOGP(tslog.Debug, "recvmsg: DATA COND: expired -- trying to get data anyway");
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
      // read is not available any more

      // Kick TsbPd thread to schedule next wakeup (if running)
      if (m_bTsbPdRcv)
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

#else /* SRT_ENABLE_TSBPD */

int CUDT::recvmsg(char* data, int len)
{
   if (m_iSockType == UDT_STREAM)
      throw CUDTException(MJ_NOTSUP, MN_ISSTREAM, 0);

   // throw an exception if not connected
   if (!m_bConnected)
      throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);

   if (len <= 0)
      return 0;

   CGuard recvguard(m_RecvLock);

   if (m_bBroken || m_bClosing)
   {
      int res = m_pRcvBuffer->readMsg(data, len);

      if (m_pRcvBuffer->getRcvMsgNum() <= 0)
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
      int res = m_pRcvBuffer->readMsg(data, len);
#ifdef HAI_PATCH // Shut up EPoll if no more messages in non-blocking mode
      if (res == 0)
      {
         // read is not available any more
         s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
         throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
      }
      else
      {
         if (m_pRcvBuffer->getRcvMsgNum() <= 0)
         {
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
         }
         return res;
      }
#else /* HAI_PATCH */
      if (res == 0)
         throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
      else
         return res;
#endif /* HAI_PATCH */
   }

   int res = 0;
   bool timeout = false;

   do
   {
       pthread_mutex_lock(&m_RecvDataLock);

       if (m_iRcvTimeOut < 0)
       {
           while (!m_bBroken && m_bConnected && !m_bClosing && ((res = m_pRcvBuffer->readMsg(data, len == 0))))
               pthread_cond_wait(&m_RecvDataCond, &m_RecvDataLock);
       }
       else
       {
           uint64_t exptime = CTimer::getTime() + m_iRcvTimeOut * 1000ULL;
           timespec locktime;

           locktime.tv_sec = exptime / 1000000;
           locktime.tv_nsec = (exptime % 1000000) * 1000;

           if (pthread_cond_timedwait(&m_RecvDataCond, &m_RecvDataLock, &locktime) == ETIMEDOUT)
               timeout = true;
           res = m_pRcvBuffer->readMsg(data, len);
       }
       pthread_mutex_unlock(&m_RecvDataLock);

      if (m_bBroken || m_bClosing)
         throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
      else if (!m_bConnected)
         throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
   } while ((res == 0) && !timeout);

   if (m_pRcvBuffer->getRcvMsgNum() <= 0)
   {
      // read is not available any more
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, false);
   }

   if ((res <= 0) && (m_iRcvTimeOut >= 0))
      throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);

   return res;
}
#endif /* SRT_ENABLE_TSBPD */

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

#ifdef SRT_ENABLE_BSTATS
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
#ifdef SRT_ENABLE_TSBPD
   perf->msSndTsbPdDelay = m_bTsbPdSnd ? m_SndTsbPdDelay : 0;
   perf->msRcvTsbPdDelay = m_bTsbPdRcv ? m_RcvTsbPdDelay : 0;
#endif
   perf->byteMSS = m_iMSS;
   perf->mbpsMaxBW = (double)m_llMaxBW * 8.0/1000000.0;
#ifdef SRT_ENABLE_SRTCC_EMB
   /* Maintained by CC if auto maxBW (0) */
   if (m_llMaxBW == 0)
       perf->mbpsMaxBW = (double)m_pSRTCC->m_llSndMaxBW * 8.0/1000000.0;
#endif
   //<
   uint32_t availbw = (uint64_t)(m_iBandwidth == 1 ? m_RcvTimeWindow.getBandwidth() : m_iBandwidth);

   perf->mbpsBandwidth = (double)(availbw * (m_iPayloadSize + pktHdrSize) * 8.0 / 1000000.0);

   if (pthread_mutex_trylock(&m_ConnectionLock) == 0)
   {
      if (m_pSndBuffer != NULL) {
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

      if (m_pRcvBuffer != NULL) {
         perf->byteAvailRcvBuf = m_pRcvBuffer->getAvailBufSize() * m_iMSS;
         //new>
         #ifdef SRT_ENABLE_TSBPD
         #ifdef SRT_ENABLE_RCVBUFSZ_MAVG
         perf->pktRcvBuf = m_pRcvBuffer->getRcvAvgDataSize(perf->byteRcvBuf, perf->msRcvBuf);
         #else
         perf->pktRcvBuf = m_pRcvBuffer->getRcvDataSize(perf->byteRcvBuf, perf->msRcvBuf);
         #endif
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
#endif /* SRT_ENABLE_BSTATS */

void CUDT::CCUpdate()
{
#if defined(SRT_ENABLE_SRTCC_EMB) && defined(SRT_ENABLE_INPUTRATE)
   if ((m_llMaxBW == 0) //Auto MaxBW
   &&  (m_llInputBW == 0) //No application provided input rate
   &&  (m_pSndBuffer != NULL)) //Internal input rate sampling
   {
      int period;
      int payloadsz; //CC will use its own average payload size
      int64_t maxbw = m_pSndBuffer->getInputRate(payloadsz, period); //Auto input rate

      /*
      * On blocked transmitter (tx full) and until connection closes,
      * auto input rate falls to 0 but there may be still lot of packet to retransmit
      * Calling pCC->setMaxBW with 0 will set maxBW to default (30Mbps) 
      * and sendrate skyrockets for retransmission.
      * Keep previously set maximum in that case (maxbw == 0).
      */
      if (maxbw != 0)
          m_pSRTCC->setMaxBW((maxbw * (100 + m_iOverheadBW))/100); //Bytes/sec

      if ((m_llSentTotal > 2000) && (period < 1000000))
         m_pSndBuffer->setInputRateSmpPeriod(1000000); //1 sec period after fast start
   }
   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

#else /* SRT_ENABLE_SRTCC_EMB && SRT_ENABLE_INPUTRATE */
   m_ullInterval = (uint64_t)(m_pCC->m_dPktSndPeriod * m_ullCPUFrequency);
   m_dCongestionWindow = m_pCC->m_dCWndSize;

   if (m_llMaxBW <= 0)
      return;
   const double minSP = 1000000.0 / (double(m_llMaxBW) / m_iMSS) * m_ullCPUFrequency;

   if (m_ullInterval < (uint64_t)minSP)
       m_ullInterval = (uint64_t)minSP;
#endif /* SRT_ENABLE_SRTCC_EMB && SRT_ENABLE_INPUTRATE */

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
#ifdef SRT_ENABLE_TSBPD
      memset(&m_RcvTsbPdThread, 0, sizeof m_RcvTsbPdThread);
      pthread_cond_init(&m_RcvTsbPdCond, NULL);
#endif /* SRT_ENABLE_TSBPD */
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
#ifdef SRT_ENABLE_TSBPD
      pthread_cond_destroy(&m_RcvTsbPdCond);
#endif /* SRT_ENABLE_TSBPD */
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

#ifdef SRT_ENABLE_TSBPD
    pthread_mutex_lock(&m_RecvLock);
    pthread_cond_signal(&m_RcvTsbPdCond);
    pthread_mutex_unlock(&m_RecvLock);
    if (!pthread_equal(m_RcvTsbPdThread, pthread_t())) 
    {
        pthread_join(m_RcvTsbPdThread, NULL);
        m_RcvTsbPdThread = pthread_t();
    }
#endif
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

#ifdef SRT_ENABLE_CTRLTSTAMP
   ctrlpkt.m_iTimeStamp = int(currtime/m_ullCPUFrequency - m_StartTime);
#endif

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

#ifdef SRT_ENABLE_TSBPD
         if (m_bTsbPdRcv)
         {
             /* Newly acknowledged data, signal TsbPD thread */
             pthread_mutex_lock(&m_RecvLock);
             if (m_bTsbPdAckWakeup)
                pthread_cond_signal(&m_RcvTsbPdCond);
             pthread_mutex_unlock(&m_RecvLock);
         }
         else
#endif /* SRT_ENABLE_TSBPD */
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
         // NOTE: SRT_ENABLE_BSTATS turns on extra fields above size 6
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
#ifdef SRT_ENABLE_BSTATS
			 int rcvRate;
			 int version = 0;
			 int ctrlsz = ACKD_TOTAL_SIZE_VER100 * ACKD_FIELD_SIZE; // Minimum required size

            data[ACKD_RCVSPEED] = m_RcvTimeWindow.getPktRcvSpeed(rcvRate);
            data[ACKD_BANDWIDTH] = m_RcvTimeWindow.getBandwidth();

#ifdef SRT_ENABLE_SRTCC_EMB
            if (m_pSRTCC)
                version = m_pSRTCC->getPeerSrtVersion();
#endif /* SRT_ENABLE_SRTCC_EMB */
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
#else /* SRT_ENABLE_BSTATS */
            data[ACKD_RCVSPEED] = m_RcvTimeWindow.getPktRcvSpeed();
            data[ACKD_BANDWIDTH] = m_RcvTimeWindow.getBandwidth();
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE); // total size is bandwidth + 1 if !SRT_ENABLE_BSTATS
#endif /* SRT_ENABLE_BSTATS */
            CTimer::rdtsc(m_ullLastAckTime);
         }
         else
         {
            ctrlpkt.pack(pkttype, &m_iAckSeqNo, data, ACKD_FIELD_SIZE * ACKD_TOTAL_SIZE_UDTBASE);
         }

         ctrlpkt.m_iID = m_PeerID;
#ifdef SRT_ENABLE_CTRLTSTAMP
         ctrlpkt.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
#endif /* SRT_ENABLE_CTRLTSTAMP */
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
       << MessageTypeStr(ctrlpkt.getType(), ctrlpkt.getExtendedType()) << ") SID=" << ctrlpkt.m_iID;

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
      if ((now - m_ullSndLastAck2Time > (uint64_t)m_iSYNInterval) || (ack == m_iSndLastAck2))
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

      m_pCC->setRTT(m_iRTT);

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

#ifdef SRT_ENABLE_BSTATS
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
         m_pCC->setRcvRate(m_iDeliveryRate);
         m_pCC->setBandwidth(m_iBandwidth);
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
         m_pCC->setRcvRate(m_iDeliveryRate);
         m_pCC->setBandwidth(m_iBandwidth);
      }
#else /* SRT_ENABLE_BSTATS */
      if (ctrlpkt.getLength() > 16)
      {
         // Update Estimated Bandwidth and packet delivery rate
         if (ackdata[ACKD_RCVSPEED] > 0)
            m_iDeliveryRate = (m_iDeliveryRate * 7 + ackdata[ACKD_RCVSPEED]) >> 3;

         if (ackdata[ACKD_BANDWIDTH] > 0)
            m_iBandwidth = (m_iBandwidth * 7 + ackdata[ACKD_BANDWIDTH]) >> 3;

         m_pCC->setRcvRate(m_iDeliveryRate);
         m_pCC->setBandwidth(m_iBandwidth);
      }
#endif /* SRT_ENABLE_BSTATS */

      m_pCC->onACK(ack);
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

      m_pCC->setRTT(m_iRTT);

#ifdef SRT_ENABLE_TSBPD
      CGuard::enterCS(m_RecvLock);
      m_pRcvBuffer->addRcvTsbPdDriftSample(ctrlpkt.getMsgTimeStamp());
      CGuard::leaveCS(m_RecvLock);
#endif /* SRT_ENABLE_TSBPD */

      // update last ACK that has been received by the sender
      if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
         m_iRcvLastAckAck = ack;

      break;
      }

   case UMSG_LOSSREPORT: //011 - Loss Report
      {
      int32_t* losslist = (int32_t *)(ctrlpkt.m_pcData);

      m_pCC->onLoss(losslist, ctrlpkt.getLength() / 4);
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
      req.deserialize(ctrlpkt.m_pcData, ctrlpkt.getLength());
      if ((req.m_iReqType > URQ_INDUCTION_TYPES) // acually it catches URQ_INDUCTION and URQ_ERROR_* symbols...???
              || (m_bRendezvous && (req.m_iReqType != URQ_AGREEMENT))) // rnd sends AGREEMENT in rsp to CONCLUSION
      {
         // The peer side has not received the handshake message, so it keeps querying
         // resend the handshake packet

         CHandShake initdata;
         initdata.m_iISN = m_iISN;
         initdata.m_iMSS = m_iMSS;
         initdata.m_iFlightFlagSize = m_iFlightFlagSize;
         initdata.m_iReqType = (!m_bRendezvous) ? URQ_CONCLUSION : URQ_AGREEMENT;
         initdata.m_iID = m_SocketID;

         char* hs = new char [m_iPayloadSize];
         int hs_size = m_iPayloadSize;
         initdata.serialize(hs, hs_size);
         sendCtrl(UMSG_HANDSHAKE, NULL, hs, hs_size);
         delete [] hs;
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
#ifdef SRT_ENABLE_TSBPD
      CGuard::enterCS(m_RecvLock);
      m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq(using_rexmit_flag), using_rexmit_flag);
      CGuard::leaveCS(m_RecvLock);
#else /* SRT_ENABLE_TSBPD */
      m_pRcvBuffer->dropMsg(ctrlpkt.getMsgSeq(using_rexmit_flag), using_rexmit_flag);
#endif /* SRT_ENABLE_TSBPD */

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
      m_pCC->processCustomMsg(&ctrlpkt); // --> m_pSRTCC->processSrtMessage(&ctrlpkt)
      CCUpdate();
#if defined(SRT_ENABLE_TSBPD) && defined(SRT_ENABLE_SRTCC_EMB)
      switch(ctrlpkt.getExtendedType())
      {
      case SRT_CMD_HSREQ:
         {
             m_bTsbPdRcv = m_pSRTCC->getRcvTsbPdInfo();

             if (m_bTsbPdRcv)
             {
                /* We are TsbPd receiver */
                // m_bTsbPdRcv = true; // XXX redundant?
                m_RcvTsbPdDelay = m_pSRTCC->getRcvTsbPdDelay();
                CGuard::enterCS(m_RecvLock);
                m_pRcvBuffer->setRcvTsbPdMode(m_pSRTCC->getRcvPeerStartTime(), m_RcvTsbPdDelay * 1000);
                CGuard::leaveCS(m_RecvLock);

                LOGC(mglog.Debug).form( "Set Rcv TsbPd mode: delay=%u.%03u secs",
                           m_RcvTsbPdDelay/1000,
                           m_RcvTsbPdDelay%1000);
             }
             // FIX: The agent that is being handshaken by the peer
             // only now knows the flags that have been updated through
             // the call of processCustomMsg().
             m_bSndPeerNakReport = m_pSRTCC->getSndPeerNakReport();
             m_bPeerRexmitFlag = m_pSRTCC->getPeerRexmitFlag();
             LOGC(mglog.Debug).form("REXMIT FLAG IS: %d", m_bPeerRexmitFlag);
         }
         break;
      case SRT_CMD_HSRSP:
         {
             m_bTsbPdSnd = m_pSRTCC->getSndTsbPdInfo();

             if (m_bTsbPdSnd)
             {
                /* We are TsbPd sender */
                m_SndTsbPdDelay = m_pSRTCC->getSndPeerTsbPdDelay();// + ((m_iRTT + (4 * m_iRTTVar)) / 1000);
#if defined(SRT_ENABLE_TLPKTDROP)
                /* 
                * For sender to apply Too-Late Packet Drop
                * option (m_bTLPktDrop) must be enabled and receiving peer shall support it
                */
                m_bTLPktDropSnd = m_bTLPktDrop && m_pSRTCC->getSndPeerTLPktDrop();
                LOGC(mglog.Debug).form( "Set Snd TsbPd mode %s: delay=%d.%03d secs",
                           m_bTLPktDropSnd ? "with TLPktDrop" : "without TLPktDrop",
                           m_SndTsbPdDelay/1000, m_SndTsbPdDelay%1000);
#else /* SRT_ENABLE_TLPKTDROP */
                LOGC(mglog.Debug).form( "Set Snd TsbPd mode %s: delay=%d.%03d secs",
                           "without TLPktDrop",
                           m_SndTsbPdDelay/1000, m_SndTsbPdDelay%1000);
#endif /* SRT_ENABLE_TLPKTDROP */
             }
             m_bSndPeerNakReport = m_pSRTCC->getSndPeerNakReport();
             m_bPeerRexmitFlag = m_pSRTCC->getPeerRexmitFlag();
             LOGC(mglog.Debug).form("REXMIT FLAG IS: %d", m_bPeerRexmitFlag);
         }
         break;
      default:
         break;
      }
#endif /* SRT_ENABLE_TSBPD */

      break;

   default:
      break;
   }
}

int CUDT::packData(CPacket& packet, uint64_t& ts)
{
   int payload = 0;
   bool probe = false;
#ifdef SRT_ENABLE_TSBPD
   uint64_t origintime = 0;
#endif /* SRT_ENABLE_TSBPD */

   int kflg = 0;

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

#ifdef SRT_ENABLE_TSBPD
      payload = m_pSndBuffer->readData(&(packet.m_pcData), offset, packet.m_iMsgNo, origintime, msglen);
#else  /* SRT_ENABLE_TSBPD */
      payload = m_pSndBuffer->readData(&(packet.m_pcData), offset, packet.m_iMsgNo, msglen);
#endif /* SRT_ENABLE_TSBPD */

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
#ifdef SRT_ENABLE_BSTATS
      m_ullTraceBytesRetrans += payload;
      m_ullBytesRetransTotal += payload;
#endif

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
      if (cwnd >= CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)))
      {
         kflg = m_pSRTCC->getSndCryptoFlags();
#ifdef   SRT_ENABLE_TSBPD
         if (0 != (payload = m_pSndBuffer->readData(&(packet.m_pcData), packet.m_iMsgNo, origintime, kflg)))
#else
         if (0 != (payload = m_pSndBuffer->readData(&(packet.m_pcData), packet.m_iMsgNo, kflg)))
#endif
         {
            m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
            m_pCC->setSndCurrSeqNo(m_iSndCurrSeqNo);

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
          LOGC(dlog.Debug).form( "congested maxbw=%lld cwnd=%d seqlen=%d\n",
                  (unsigned long long)m_pSRTCC->m_llSndMaxBW, cwnd, CSeqNo::seqlen(m_iSndLastAck, CSeqNo::incseq(m_iSndCurrSeqNo)));
         m_ullTargetTime = 0;
         m_ullTimeDiff = 0;
         ts = 0;
         return 0;
      }

      reason = "normal";
   }

#ifdef SRT_ENABLE_TSBPD
   if (m_bTsbPdSnd)
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
#endif /* SRT_ENABLE_TSBPD */

   packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
   packet.m_iID = m_PeerID;
   packet.setLength(payload);

#ifdef SRT_ENABLE_SRTCC_EMB
   /* Encrypt if 1st time this packet is sent and crypto is enabled */
   if (kflg)
   {
      if (packet.encrypt(m_pSRTCC->getSndCryptoCtx()))
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

#endif

#ifdef SRT_FIX_KEEPALIVE
   m_ullLastSndTime = entertime;
#endif /* SRT_FIX_KEEPALIVE */

   m_pCC->onPktSent(&packet);
   //m_pSndTimeWindow->onPktSent(packet.m_iTimeStamp);

#ifdef SRT_ENABLE_BSTATS
   m_ullTraceBytesSent += payload;
   m_ullBytesSentTotal += payload;
#endif
   ++ m_llTraceSent;
   ++ m_llSentTotal;

   if (probe)
   {
      // sends out probing packet pair
      ts = entertime;
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
   //m_pRcvBuffer->addRcvTsbPdDriftSample(packet.getMsgTimeStamp());
#if SRT_ENABLE_SND2WAYPROTECT
   if (m_bDataSender)
   {
      /*
      * SRT 1.1.2 and earlier sender can assert if accepting data that will not be read.
      * Ignoring received data.
      */
      LOGP(mglog.Error, "Error: receiving data in SRT sender-only side: breaking connection.");
      m_bBroken = true;
      m_iBrokenCounter = 0;
      return -1;
   }
#endif /* SRT_ENABLE_SND2WAYPROTECT */
   // Just heard from the peer, reset the expiration count.
   m_iEXPCount = 1;
   uint64_t currtime;
   CTimer::rdtsc(currtime);
   m_ullLastRspTime = currtime;

#ifdef SRT_ENABLE_TSBPD
   /* We are receiver, start tsbpd thread if TsbPd is enabled */
   if (m_bTsbPdRcv && pthread_equal(m_RcvTsbPdThread, pthread_t()))
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
#endif /* SRT_ENABLE_TSBPD */

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

   m_pCC->onPktReceived(&packet);
   ++ m_iPktCount;

   int pktsz = packet.getLength();
#ifdef SRT_ENABLE_BSTATS
   // update time information
   m_RcvTimeWindow.onPktArrival(pktsz);

   // check if it is probing packet pair
   if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 0)
      m_RcvTimeWindow.probe1Arrival();
   else if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 1)
      m_RcvTimeWindow.probe2Arrival(pktsz);

   m_ullTraceBytesRecv += pktsz;
   m_ullBytesRecvTotal += pktsz;
#else
   m_RcvTimeWindow.onPktArrival();

   // check if it is probing packet pair
   if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 0)
      m_RcvTimeWindow.probe1Arrival();
   else if ((packet.m_iSeqNo & PUMASK_SEQNO_PROBE) == 1)
      m_RcvTimeWindow.probe2Arrival();
#endif
   ++ m_llTraceRecv;
   ++ m_llRecvTotal;

#ifdef SRT_ENABLE_TSBPD
   {
      /*
      * Start of offset protected section
      * Prevent TsbPd thread from modifying Ack position while adding data
      * offset from RcvLastAck in RcvBuffer must remain valid between seqoff() and addData()
      */
      CGuard offsetcg(m_AckLock);
#endif /* SRT_ENABLE_TSBPD */

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
          << " (" << exc_type << "/" << rexmitstat[pktrexmitflag] << rexmit_reason << ")";

      if ( excessive )
      {
          return -1;
      }

      if (packet.getMsgCryptoFlags())
      {
#ifdef   SRT_ENABLE_SRTCC_EMB
          EncryptionStatus rc = m_pSRTCC ? packet.decrypt(m_pSRTCC->getRcvCryptoCtx()) : ENCS_NOTSUP;
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
#endif   /* SRT_ENABLE_SRTCC_EMB */
      }

#ifdef SRT_ENABLE_TSBPD
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
#endif /* SRT_ENABLE_TSBPD */

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
#ifdef SRT_ENABLE_BSTATS
         uint64_t lossbytes = loss * m_pRcvBuffer->getRcvAvgPayloadSize();
         m_ullTraceRcvBytesLoss += lossbytes;
         m_ullRcvBytesLossTotal += lossbytes;
#endif /* SRT_ENABLE_BSTATS */
      }

#ifdef SRT_ENABLE_TSBPD
      if (m_bTsbPdRcv)
      {
         pthread_mutex_lock(&m_RecvLock);
         pthread_cond_signal(&m_RcvTsbPdCond);
         pthread_mutex_unlock(&m_RecvLock);
      }
#endif /* SRT_ENABLE_TSBPD */
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
int CUDT::processConnectRequest(const sockaddr* addr, CPacket& packet)
{
   LOGC(mglog.Note).form("listen");
   if (m_bClosing){
       LOGC(mglog.Error).form("listen reject: closing");
       return int(URQ_ERROR_REJECT);
   }
   /*
   * Closing a listening socket only set bBroken
   * If a connect packet is received while closing it gets through
   * processing and crashes later.
   */
   if (m_bBroken){
      LOGC(mglog.Error).form("listen reject: broken");
      return int(URQ_ERROR_REJECT);
   }

   if (packet.getLength() != CHandShake::m_iContentSize){
      LOGC(mglog.Error).form("listen invalid: invalif lengh %d!= %d",packet.getLength(),CHandShake::m_iContentSize);
      return int(URQ_ERROR_INVALID);
   }
   CHandShake hs;
   hs.deserialize(packet.m_pcData, packet.getLength());

   // SYN cookie
   char clienthost[NI_MAXHOST];
   char clientport[NI_MAXSERV];
   getnameinfo(addr,
           (m_iIPversion == AF_INET) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6),
           clienthost, sizeof(clienthost), clientport, sizeof(clientport),
           NI_NUMERICHOST|NI_NUMERICSERV);
   int64_t timestamp = (CTimer::getTime() - m_StartTime) / 60000000; // secret changes every one minute
   stringstream cookiestr;
   cookiestr << clienthost << ":" << clientport << ":" << timestamp;
   union
   {
        unsigned char cookie[16];
        int32_t cookie_val;
   };
   CMD5::compute(cookiestr.str().c_str(), cookie);

   if (hs.m_iReqType == URQ_INDUCTION)
   {
       // XXX That looks weird - the calculated md5 sum out of the given host/port/timestamp
       // is 16 bytes long, but CHandShake::m_iCookie has 4 bytes. This then effectively copies
       // only the first 4 bytes. Moreover, it's dangerous on some platforms because the char
       // array need not be aligned to int32_t - changed to union in a hope that using int32_t
       // inside a union will enforce whole union to be aligned to int32_t.
      hs.m_iCookie = cookie_val;
      packet.m_iID = hs.m_iID;
      int size = packet.getLength();
      hs.serialize(packet.m_pcData, size);
#ifdef SRT_ENABLE_CTRLTSTAMP
      packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
#endif
      m_pSndQueue->sendto(addr, packet);
      return 0; // XXX URQ_RENDEZVOUS, oh really?
   }
   else
   {
      if (hs.m_iCookie != cookie_val)
      {
         timestamp --;
         cookiestr << clienthost << ":" << clientport << ":" << timestamp;
         CMD5::compute(cookiestr.str().c_str(), cookie);

         if (hs.m_iCookie != cookie_val)
         {
            LOGC(mglog.Note).form("listen rsp: %d", URQ_CONCLUSION);
            return int(URQ_CONCLUSION); // Don't look at me, I just change integers to symbols!
         }
      }
   }

   int32_t id = hs.m_iID;

   // When a peer side connects in...
   if ( packet.isControl(UMSG_HANDSHAKE) )
   {
      if ((hs.m_iVersion != m_iVersion) || (hs.m_iType != m_iSockType))
      {
         // mismatch, reject the request
         hs.m_iReqType = URQ_ERROR_REJECT;
         int size = CHandShake::m_iContentSize;
         hs.serialize(packet.m_pcData, size);
         packet.m_iID = id;
#ifdef SRT_ENABLE_CTRLTSTAMP
         packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
#endif
         m_pSndQueue->sendto(addr, packet);
      }
      else
      {
         int result = s_UDTUnited.newConnection(m_SocketID, addr, &hs);
         if (result == -1)
         {
            hs.m_iReqType = URQ_ERROR_REJECT;
            LOGC(mglog.Error).form("listen rsp(REJECT): %d", URQ_ERROR_REJECT);
         }

         // XXX developer disorder warning!
         //
         // The newConnection() will call acceptAndRespond() if the processing
         // was successful - IN WHICH CASE THIS PROCEDURE SHOULD DO NOTHING.
         // Ok, almost nothing - see update_events below.
         //
         // If newConnection() failed, acceptAndRespond() will not be called.
         // Ok, more precisely, the thing that acceptAndRespond() is expected to do
         // will not be done.
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
         // reused for the connection rejection response (see URQ_REJECT set
         // as m_iReqType).

         // send back a response if connection failed or connection already existed
         // new connection response should be sent in acceptAndRespond()
         if (result != 1)
         {
            int size = CHandShake::m_iContentSize;
            hs.serialize(packet.m_pcData, size);
            packet.m_iID = id;
#ifdef SRT_ENABLE_CTRLTSTAMP
            packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);
#endif
            m_pSndQueue->sendto(addr, packet);
         }
         else
         {
            // a new connection has been created, enable epoll for write
            s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_OUT, true);
         }
      }
   }
   LOGC(mglog.Note).form("listen ret: %d", hs.m_iReqType);

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
       << " AckInterval=" << m_pCC->m_iACKInterval
       << " pkt-count=" << m_iPktCount << " liteack-count=" << m_iLightACKCount;
#endif

   if ((currtime > m_ullNextACKTime) || ((m_pCC->m_iACKInterval > 0) && (m_pCC->m_iACKInterval <= m_iPktCount)))
   {
      // ACK timer expired or ACK interval is reached

      sendCtrl(UMSG_ACK);
      CTimer::rdtsc(currtime);
      if (m_pCC->m_iACKPeriod > 0)
         m_ullNextACKTime = currtime + m_pCC->m_iACKPeriod * m_ullCPUFrequency;
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
   if (m_pCC->m_bUserDefinedRTO)
      next_exp_time = m_ullLastRspTime + m_pCC->m_iRTO * m_ullCPUFrequency;
   else
   {
      uint64_t exp_int = (m_iEXPCount * (m_iRTT + 4 * m_iRTTVar) + m_iSYNInterval) * m_ullCPUFrequency;
      if (exp_int < m_iEXPCount * m_ullMinExpInt)
         exp_int = m_iEXPCount * m_ullMinExpInt;
      next_exp_time = m_ullLastRspTime + exp_int;
   }

   if (currtime > next_exp_time)
   {
      // Haven't receive any information from the peer, is it dead?!
#ifdef HAI_PATCH //Comment says 10 second, code says 5
      // timeout: at least 16 expirations and must be greater than 5 seconds
#else
      // timeout: at least 16 expirations and must be greater than 10 seconds
#endif
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

         m_pCC->onTimeout();
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
   &&  !m_bSndPeerNakReport 
#endif
   &&  m_pSndBuffer->getCurrBufSize() > 0)
   {
      uint64_t exp_int = (m_iReXmitCount * (m_iRTT + 4 * m_iRTTVar + 2 * m_iSYNInterval) + m_iSYNInterval) * m_ullCPUFrequency;

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

         m_pCC->onTimeout();
         CCUpdate();

         // immediately restart transmission
         m_pSndQueue->m_pSndUList->update(this);
      }
   }
#endif /* SRT_ENABLE_FASTREXMIT */

#ifdef SRT_FIX_KEEPALIVE
//   uint64_t exp_int = (m_iRTT + 4 * m_iRTTVar + m_iSYNInterval) * m_ullCPUFrequency;
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

#ifdef SRT_ENABLE_TSBPD
   CGuard::enterCS(m_RecvLock);
   if (m_pRcvBuffer->isRcvDataReady())
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
   }
   CGuard::leaveCS(m_RecvLock);
#else /* SRT_ENABLE_TSBPD */
   if (((m_iSockType == UDT_DGRAM) && (m_pRcvBuffer->getRcvMsgNum() > 0))
           ||  ((m_iSockType == UDT_STREAM) &&  m_pRcvBuffer->isRcvDataReady()))
   {
      s_UDTUnited.m_EPoll.update_events(m_SocketID, m_sPollID, UDT_EPOLL_IN, true);
   }
#endif /* SRT_ENABLE_TSBPD */
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
