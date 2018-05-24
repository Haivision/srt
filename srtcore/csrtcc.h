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
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef CSRTCC_H
#define CSRTCC_H

#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstring>
#include <string>

// UDT
#include "udt.h"
#include "core.h"
#include "ccc.h"

#include <hcrypt_msg.h>


#define SRT_VERSION_UNK     0
#define SRT_VERSION_MAJ1    0x010000            /* Version 1 major */
#define SRT_VERSION_1XX     0x010200            /* Version 1 highest supported: 1.2.0 */

#if 0//Test version upgrade
#define SRT_VERSION_MAJ2    0x020000            /* Version 2 major */
#define SRT_VERSION_2XX     0x020000            /* Version 2 highest supported: 2.0.0 */
#define SRT_DEF_VERSION     SRT_VERSION_2XX     /* Current version */
#else
//#define SRT_DEF_VERSION     SRT_VERSION_1XX     /* Current version */
#endif


#define SRT_VERSION_MAJ(v) (0xFF0000 & (v))     /* Major number ensuring backward compatibility */
#define SRT_VERSION_MIN(v) (0x00FF00 & (v))
#define SRT_VERSION_PCH(v) (0x0000FF & (v))

inline int SrtVersion(int major, int minor, int patch)
{
    return patch + minor*0x100 + major*0x10000;
}

inline int32_t SrtParseVersion(const char* v)
{
    int major, minor, patch;
    // On Windows/MSC, ignore C4996 warning here.
    // This warning states that sscanf is unsafe because the %s and %c conversions
    // don't specify the size of the buffer. Just because of this fact, sscanf is
    // considered unsafe - even though I don't use %s or %c here at all.
    int result = sscanf(v, "%d.%d.%d", &major, &minor, &patch);

    if ( result != 3 )
    {
        fprintf(stderr, "Invalid version format for SRT_VERSION: %s - use m.n.p\n", v);
        throw v; // Throwing exception, as this function will be run before main()
    }

    return major*0x10000 + minor*0x100 + patch;
}

const int32_t SRT_DEF_VERSION = SrtParseVersion(SRT_VERSION);

inline std::string SrtVersionString(int version)
{
    int patch = version % 0x100;
    int minor = (version/0x100)%0x100;
    int major = version/0x10000;
    std::ostringstream buf;
    buf << major << "." << minor << "." << patch;
    return buf.str();
}

enum SrtOptions
{
    SRT_OPT_TSBPDSND  = 0x00000001, /* Timestamp-based Packet delivery real-time data sender */
    SRT_OPT_TSBPDRCV  = 0x00000002, /* Timestamp-based Packet delivery real-time data receiver */
    SRT_OPT_HAICRYPT  = 0x00000004, /* HaiCrypt AES-128/192/256-CTR */
    SRT_OPT_TLPKTDROP = 0x00000008, /* Drop real-time data packets too late to be processed in time */
    SRT_OPT_NAKREPORT = 0x00000010, /* Periodic NAK report */
    SRT_OPT_REXMITFLG = 0x00000020, // One bit in payload packet msgno is "retransmitted" flag
};

std::string SrtFlagString(int32_t flags);

const int SRT_CMD_HSREQ = 1,
      SRT_CMD_HSRSP = 2,
      SRT_CMD_KMREQ = 3,
      SRT_CMD_KMRSP = 4;

enum SrtDataStruct
{
    SRT_HS_VERSION = 0,
    SRT_HS_FLAGS,
    SRT_HS_EXTRAS,

    // Keep it always last
    SRT_HS__SIZE
};

typedef Bits<31, 16> SRT_HS_EXTRAS_HI;
typedef Bits<15, 0> SRT_HS_EXTRAS_LO;

// For KMREQ/KMRSP. Only one field is used.
const size_t SRT_KMR_KMSTATE = 0;


class CSRTCC : public CCC
{
public:
    int      m_SrtVersion;          //Local SRT Version (test program can simulate older versions)
    int64_t  m_llSndMaxBW;          //Max bandwidth (bytes/sec)
    int      m_iSndAvgPayloadSize;  //Average Payload Size of packets to xmit

    int      m_iSndKmKeyLen;        //Key length
    int      m_iRcvKmKeyLen;        //Key length from rx KM

    /*
#define SRT_KM_S_UNSECURED  0       //No encryption
#define SRT_KM_S_SECURING   1       //Stream encrypted, exchanging Keying Material
#define SRT_KM_S_SECURED    2       //Stream encrypted, keying Material exchanged, decrypting ok.
#define SRT_KM_S_NOSECRET   3       //Stream encrypted and no secret to decrypt Keying Material
#define SRT_KM_S_BADSECRET  4       //Stream encrypted and wrong secret, cannot decrypt Keying Material
*/
    int      m_iSndKmState;         //Sender Km State
    int      m_iSndPeerKmState;     //Sender's peer (receiver) Km State
    int      m_iRcvKmState;         //Receiver Km State
    int      m_iRcvPeerKmState;     //Receiver's peer (sender) Km State


protected:
    bool     m_bDataSender;         //Sender side (for crypto, TsbPD handshake)
    unsigned m_TsbPdDelay;          //Set TsbPD delay (mSec)

#ifdef SRT_ENABLE_TLPKTDROP
    bool     m_bRcvTLPktDrop;       //Receiver Enabled Too-Late Packet Drop
    bool     m_bSndPeerTLPktDrop;   //Sender's Peer supports Too Late Packets drop
#endif
#ifdef SRT_ENABLE_NAKREPORT
    bool     m_bRcvNakReport;       //Enable Receiver Periodic NAK Reports
    bool     m_bSndPeerNakReport;   //Sender's peer sends Periodic NAK Reports
#endif
    bool     m_bPeerRexmitFlag;  //Peer will receive MSGNO with 26 bits only (bit 26 is used as a rexmit flag)
    int      m_PeerSrtVersion;      //Peer Version received in handshake message

    int      m_SndPeerSrtOptions;   //Sender's Peer Options received in SRT Handshake Response message
    bool     m_bSndTsbPdMode;       //Sender is TsbPD
    unsigned m_SndPeerTsbPdDelay;   //Sender's Peer Exchanged (largest) TsbPD delay (mSec)

    int      m_RcvPeerSrtOptions;   //Receiver's Peer Options received in SRT Handshake Request message
    bool     m_bRcvTsbPdMode;       //Receiver is TsbPD
    unsigned m_RcvTsbPdDelay;       //Receiver Exchanged (largest) TsbPD delay (mSec)
    uint64_t m_RcvPeerStartTime;    //Receiver's Peer StartTime (base of pkt timestamp) in local time reference 

    uint64_t m_SndHsLastTime;	    //Last SRT handshake request time
    int      m_SndHsRetryCnt;       //SRT handshake retries left

    HaiCrypt_Secret m_KmSecret;     //Key material shared secret
    // Sender
    uint64_t        m_SndKmLastTime;
    struct {
        char Msg[HCRYPT_MSG_KM_MAX_SZ];
        size_t MsgLen;
        int iPeerRetry;
    } m_SndKmMsg[2];
    HaiCrypt_Handle m_hSndCrypto;
    // Receiver
    HaiCrypt_Handle m_hRcvCrypto;

    UDTSOCKET m_sock; // for logging

private:
    void sendSrtMsg(int cmd, int32_t *srtdata_in = NULL, int srtlen_in = 0);
    void processSrtMsg(const CPacket *ctrlpkt);
    void checkSndTimers();
    void regenCryptoKm(bool sendit = true);

public:
    CSRTCC();

    std::string CONID() const;

protected:
    virtual void init();
    virtual void close();
    virtual void onACK(int32_t ackno);
    virtual void onPktSent(const CPacket *pkt);
    virtual void onTimeout() { checkSndTimers(); }

    virtual void processCustomMsg(const CPacket *ctrlpkt)
    {
        processSrtMsg(ctrlpkt);
    }

public:

    void setSndTsbPdMode(bool tsbpd)
    {
        m_bDataSender = true;
        m_bSndTsbPdMode = tsbpd;
    }

    void setTsbPdDelay(int delay)
    {
        m_TsbPdDelay = (unsigned)delay;
    }

    int getPeerSrtVersion()
    {
        return(m_PeerSrtVersion);
    }

    unsigned getRcvTsbPdDelay()
    {
        return(m_RcvTsbPdDelay);
    }

    unsigned getSndPeerTsbPdDelay()
    {
        return(m_SndPeerTsbPdDelay);
    }

    bool getSndTsbPdInfo()
    {
        return(m_bSndTsbPdMode);
    }

    bool getRcvTsbPdInfo(uint64_t *starttime)
    {
        if (NULL != starttime)
            *starttime = m_RcvPeerStartTime;
        return(m_bRcvTsbPdMode);
    }

    bool getRcvTsbPdInfo()
    {
        return m_bRcvTsbPdMode;
    }

    uint64_t getRcvPeerStartTime()
    {
        return m_RcvPeerStartTime;
    }

#ifdef SRT_ENABLE_TLPKTDROP
    void setRcvTLPktDrop(bool pktdrop)
    {
        m_bRcvTLPktDrop = pktdrop;
    }

    unsigned getSndPeerTLPktDrop()
    {
        return(m_bSndPeerTLPktDrop);
    }
#endif

    void setMaxBW(int64_t maxbw)
    {
        m_llSndMaxBW = maxbw > 0 ? maxbw : 30000000/8;         //Infinite=> 30Mbps
        m_dPktSndPeriod = ((double)(m_iSndAvgPayloadSize + CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE) / m_llSndMaxBW) * 1000000.0;

#ifdef SRT_ENABLE_NOCWND
        /*
        * UDT default flow control should not trigger under normal SRT operation
        * UDT stops sending if the number of packets in transit (not acknowledged)
        * is larger than the congestion window.
        * Up to SRT 1.0.6, this value was set at 1000 pkts, which may be insufficient
        * for satellite links with ~1000 msec RTT and high bit rate.
        */
        m_dCWndSize = m_dMaxCWndSize;
#else
        m_dCWndSize = 1000;
#endif
    }

    void setCryptoSecret(HaiCrypt_Secret *secret)
    {
        memcpy(&m_KmSecret, secret, sizeof(m_KmSecret));
    }

    void setSndCryptoKeylen(int keylen)
    {
        m_iSndKmKeyLen = keylen;
        m_bDataSender = true;
    }

    HaiCrypt_Handle createCryptoCtx(int keylen, int tx = 0);

    HaiCrypt_Handle getSndCryptoCtx() const
    {
        return(m_hSndCrypto);
    }

    HaiCrypt_Handle getRcvCryptoCtx();

    int getSndCryptoFlags() const
    {
        return(m_hSndCrypto ? HaiCrypt_Tx_GetKeyFlags(m_hSndCrypto) : 0);
    }

    void freeCryptoCtx();

#ifdef SRT_ENABLE_NAKREPORT
    void setRcvNakReport(bool nakreport)
    {
        m_bRcvNakReport = nakreport;
    }

    bool getSndPeerNakReport()
    {
        return(m_bSndPeerNakReport);
    }
#endif

    bool getPeerRexmitFlag()
    {
        return m_bPeerRexmitFlag;
    }

};

#endif // SRT_CONGESTION_CONTROL_H
