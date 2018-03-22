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

#include <cstring>
#include <string>

#include <haicrypt.h>
#include "csrtcc.h"
#include "logging.h"

extern logging::Logger mglog, dlog;

#define SRT_CMD_MAXSZ       HCRYPT_MSG_KM_MAX_SZ  /* Maximum SRT custom messages payload size (bytes) */
#define SRT_MAX_HSRETRY     10          /* Maximum SRT handshake retry */

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

#define SRT_MAX_KMRETRY     10
 
//#define SRT_CMD_KMREQ       3           /* HaiCryptTP SRT Keying Material */
//#define SRT_CMD_KMRSP       4           /* HaiCryptTP SRT Keying Material ACK */
#define SRT_CMD_KMREQ_SZ    HCRYPT_MSG_KM_MAX_SZ          /* */
#if     SRT_CMD_KMREQ_SZ > SRT_CMD_MAXSZ
#error  SRT_CMD_MAXSZ too small
#endif
/*      Key Material Request (Network Order)
        See HaiCryptTP SRT (hcrypt_xpt_srt.c)
*/



void CSRTCC::sendSrtMsg(int cmd, int32_t *srtdata_in, int srtlen_in)
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
    int32_t srtdata[SRTDATA_SIZE];

    int srtlen = 0;

    switch(cmd){
    case SRT_CMD_HSREQ:
        memset(srtdata, 0, sizeof(srtdata));

#ifdef SRT_VERSION_MAJ2
		if (SRT_VERSION_MAJ(m_PeerSrtVersion) == SRT_VERSION_MAJ1)
		{
			//>>duB: fix that to not set m_SrtVersion under normal operations (version downgrade)
			//>>only set to test version handshake
			//m_SrtVersion = SRT_VERSION_1XX; //highest compatible version 1
			// move 1.x.x handshake code here when default becomes 2.x.x
			//break; //>>fall through until 2.x.x implemented
		}
		else
#endif
        /*
           XXX Do some version renegotiation if needed;
           The "unknown version" may be used for something else,
           currently not predicted to happen.
		if (SRT_VERSION_MAJ(m_PeerSrtVersion) == SRT_VERSION_UNK)
		{
			// Some fallback...
			srtdata[SRT_HS_VERSION] = 0; // Rejection
		}
		else
        */
		{
            /* Current version (1.x.x) SRT handshake */
            srtdata[SRT_HS_VERSION] = m_SrtVersion;  /* Required version */
            if (m_bSndTsbPdMode)
            {
                /*
                * Sent data is real-time, use Time-based Packet Delivery,
                * set option bit and configured delay
                */
                srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDSND;
                srtdata[SRT_HS_EXTRAS] = SRT_HS_EXTRAS_LO::wrap(m_TsbPdDelay);
            }

            srtdata[SRT_HS_FLAGS] |= SRT_OPT_HAICRYPT;
            srtlen = SRT_HS__SIZE;

            // I support SRT_OPT_REXMITFLG. Do you?
            srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;

            LOGC(mglog.Note).form( "sndSrtMsg: cmd=%d(HSREQ) len=%d vers=0x%x opts=0x%x delay=%d\n", 
                cmd, (int)(srtlen * sizeof(int32_t)),
                srtdata[SRT_HS_VERSION],
                srtdata[SRT_HS_FLAGS],
                SRT_HS_EXTRAS_LO::unwrap(srtdata[SRT_HS_EXTRAS]));
        }
        break;

    case SRT_CMD_HSRSP:
        memset(srtdata, 0, sizeof(srtdata));


#ifdef SRT_VERSION_MAJ2
        if (SRT_VERSION_MAJ(m_PeerSrtVersion) == SRT_VERSION_MAJ1)
        {
            //>>duB: fix that to not set m_SrtVersion under normal operations (version downgrade)
            //>>only set to test version handshake
            //m_SrtVersion = SRT_VERSION_1XX; //highest compatible version 1
            // move 1.x.x handshake code here when default becomes 2.x.x
            //break; //>>fall through until 2.x.x implemented
        }
        else
#endif
        /*
        if (SRT_VERSION_MAJ(m_PeerSrtVersion) == SRT_VERSION_UNK)
        {
            // Some fallback...
            srtdata[SRT_HS_VERSION] = 0; // Rejection
        }
        else
        */
        {
            /* Current version (1.x.x) SRT handshake */
            srtdata[SRT_HS_VERSION] = m_SrtVersion; /* Required version */
            if (0 != m_RcvPeerStartTime)
            {
                /* 
                 * We got and transposed peer start time (HandShake request timestamp),
                * we can support Timestamp-based Packet Delivery
                */
                srtdata[SRT_HS_FLAGS] |= SRT_OPT_TSBPDRCV;
#ifdef SRT_ENABLE_TLPKTDROP
                if ((m_SrtVersion >= SrtVersion(1, 0, 5)) && m_bRcvTLPktDrop)
                    srtdata[SRT_HS_FLAGS] |= SRT_OPT_TLPKTDROP;
#endif
                srtdata[SRT_HS_EXTRAS] = SRT_HS_EXTRAS_LO::wrap(m_RcvTsbPdDelay);
            }

            srtdata[SRT_HS_FLAGS] |= SRT_OPT_HAICRYPT;

#ifdef SRT_ENABLE_NAKREPORT
            if ((m_SrtVersion >= SrtVersion(1, 1, 0)) && m_bRcvNakReport)
            {
                srtdata[SRT_HS_FLAGS] |= SRT_OPT_NAKREPORT;
                /*
                * NAK Report is so efficient at controlling bandwidth that sender TLPktDrop
                * is not needed. SRT 1.0.5 to 1.0.7 sender TLPktDrop combined with SRT 1.0
                * Timestamp-Based Packet Delivery was not well implemented and could drop
                * big I-Frame tail before sending once on low latency setups.
                * Disabling TLPktDrop in the receiver SRT Handshake Reply prevents the sender
                * from enabling Too-Late Packet Drop.
                */
                if (m_PeerSrtVersion <= SrtVersion(1, 0, 7))
                    srtdata[SRT_HS_FLAGS] &= ~SRT_OPT_TLPKTDROP;
            }
#endif

            if ( m_SrtVersion >= SrtVersion(1, 2, 0) )
            {
                // Request that the rexmit bit be used as a part of msgno.
                srtdata[SRT_HS_FLAGS] |= SRT_OPT_REXMITFLG;
                LOGC(mglog.Debug).form("HS RP1: I UNDERSTAND REXMIT flag" );
            }
            else
            {
                // Since this is now in the code, it can occur only in case when you change the 
                // version specification in the build configuration.
                LOGC(mglog.Debug).form("HS RP1: I DO NOT UNDERSTAND REXMIT flag" );
            }
            srtlen = SRT_HS__SIZE;

            LOGC(mglog.Note).form( "sndSrtMsg: cmd=%d(HSRSP) len=%d vers=0x%x opts=0x%x delay=%d\n", 
                cmd, (int)(srtlen * sizeof(int32_t)), srtdata[SRT_HS_VERSION], srtdata[SRT_HS_FLAGS], srtdata[SRT_HS_EXTRAS]);
        }
        break;

#ifdef SRT_ENABLE_HAICRYPT
    case SRT_CMD_KMREQ: //Sender
        srtlen = srtlen_in;
        /* Msg already in network order
         * But CChannel:sendto will swap again (assuming 32-bit fields)
         * Pre-swap to cancel it.
         */
        for (int i = 0; i < srtlen; ++ i) srtdata[i] = htonl(srtdata_in[i]);

        if (SRT_KM_S_UNSECURED == m_iSndKmState)
        {
            m_iSndKmState = SRT_KM_S_SECURING;
            m_iSndPeerKmState = SRT_KM_S_SECURING;
        }
        LOGC(mglog.Note).form( "sndSrtMsg: cmd=%d(KMREQ) len=%d Snd/PeerKmState=%s/%s\n", 
            cmd, (int)(srtlen * sizeof(int32_t)),
            SRT_KM_S_SECURED == m_iSndKmState ? "secured"
            : SRT_KM_S_SECURING == m_iSndKmState ? "securing" : "unsecured",
            SRT_KM_S_SECURED == m_iSndPeerKmState ? "secured"
            : SRT_KM_S_NOSECRET == m_iSndPeerKmState ? "no-secret"
            : SRT_KM_S_BADSECRET == m_iSndPeerKmState ? "bad-secret"
            : SRT_KM_S_SECURING == m_iSndPeerKmState ? "securing" : "unsecured");
        break;

    case SRT_CMD_KMRSP: //Receiver
        srtlen = srtlen_in;
        /* Msg already in network order
         * But CChannel:sendto will swap again (assuming 32-bit fields)
         * Pre-swap to cancel it.
         */
        for (int i = 0; i < srtlen; ++ i) srtdata[i] = htonl(srtdata_in[i]);

        LOGC(mglog.Note).form( "sndSrtMsg: cmd=%d(KMRSP) len=%d Peer/RcvKmState=%s/%s\n", 
            cmd, (int)(srtlen * sizeof(int32_t)),
            SRT_KM_S_SECURED == m_iRcvPeerKmState ? "secured"
            : SRT_KM_S_SECURING == m_iRcvPeerKmState ? "securing" : "unsecured",
            SRT_KM_S_SECURED == m_iRcvKmState ? "secured"
            : SRT_KM_S_NOSECRET == m_iRcvKmState ? "no-secret"
            : SRT_KM_S_BADSECRET == m_iRcvKmState ? "bad-secret"
            : SRT_KM_S_SECURING == m_iRcvKmState ? "securing" : "unsecured");
        break;
#endif /* SRT_ENABLE_HAICRYPT */

    default:
        LOGC(mglog.Error).form( "sndSrtMsg: cmd=%d unsupported\n", cmd);
        break;
    }
    if (srtlen > 0)
    {
        LOGC(mglog.Debug).form("CMD:%s Version: %s Flags: %08X (%s)\n",
                MessageTypeStr(UMSG_EXT, srtcmd).c_str(),
                SrtVersionString(srtdata[SRT_HS_VERSION]).c_str(),
                srtdata[SRT_HS_FLAGS],
                SrtFlagString(srtdata[SRT_HS_FLAGS]).c_str());
        /* srtpkt.pack will set message data in network order */
        srtpkt.pack(UMSG_EXT, &srtcmd, srtdata, srtlen * sizeof(int32_t));
        sendCustomMsg(srtpkt);
    }
}


void CSRTCC::processSrtMsg(const CPacket *ctrlpkt)
{
    int32_t *srtdata = (int32_t *)ctrlpkt->m_pcData;

    switch(ctrlpkt->getExtendedType()) 
    {
    case SRT_CMD_HSREQ:
        if (ctrlpkt->getLength() < SRT_CMD_HSREQ_MINSZ)
        { 
            /* Packet smaller than minimum compatible packet size */
            LOGC(mglog.Error).form( "rcvSrtMsg: cmd=%d(HSREQ) len=%d invalid\n", ctrlpkt->getExtendedType(), ctrlpkt->getLength());
        }
        else switch(SRT_VERSION_MAJ(srtdata[SRT_HS_VERSION]))
        {
#ifdef SRT_VERSION_2XX
        case SRT_VERSION_MAJ2: /* Peer SRT version == 2.x.x */
            LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(HSREQ) len=%d vers=0x%x\n", 
                    ctrlpkt->getExtendedType(), ctrlpkt->getLength(), srtdata[SRT_HS_VERSION]);

            m_PeerSrtVersion = srtdata[SRT_HS_VERSION];
            m_RcvPeerSrtOptions = srtdata[SRT_HS_FLAGS];
            sendSrtMsg(SRT_CMD_HSRSP);
            break;
#endif
        case SRT_VERSION_MAJ1: /* Peer SRT version == 1.x.x */
            LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(HSREQ) len=%d vers=0x%x opts=0x%x delay=%d\n", 
                    ctrlpkt->getExtendedType(), ctrlpkt->getLength(), srtdata[SRT_HS_VERSION], srtdata[SRT_HS_FLAGS], srtdata[SRT_HS_EXTRAS]);

            m_PeerSrtVersion = srtdata[SRT_HS_VERSION];
            m_RcvPeerSrtOptions = srtdata[SRT_HS_FLAGS];

            LOGC(mglog.Debug).form("HS RQ: Version: %s Flags: %08X (%s)\n",
                    SrtVersionString(m_PeerSrtVersion).c_str(),
                    m_RcvPeerSrtOptions,
                    SrtFlagString(m_RcvPeerSrtOptions).c_str());

            if ( IsSet(m_RcvPeerSrtOptions, SRT_OPT_TSBPDSND) )
            {
                //TimeStamp-based Packet Delivery feature enabled
                m_bRcvTsbPdMode = true;  //Sender use TsbPd, enable TsbPd rx.

                /*
                 * Take max of sender/receiver TsbPdDelay
                 */
                m_RcvTsbPdDelay = SRT_HS_EXTRAS_LO::unwrap(srtdata[SRT_HS_EXTRAS]);
                if (m_TsbPdDelay > m_RcvTsbPdDelay)
                {
                    m_RcvTsbPdDelay = m_TsbPdDelay;
                }

                /*
                 * Compute peer StartTime in our time reference
                 * This takes time zone, time drift into account.
                 * Also includes current packet transit time (rtt/2)
                 */
#if 0                   //Debug PeerStartTime if not 1st HS packet
                {
                    uint64_t oldPeerStartTime = m_RcvPeerStartTime;
                    m_RcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ctrlpkt->m_iTimeStamp);
                    if (oldPeerStartTime) {
                        LOGC(mglog.Note).form( "rcvSrtMsg: 2nd PeerStartTime diff=%lld usec\n", 
                                (long long)(m_RcvPeerStartTime - oldPeerStartTime));
                    }
                }
#else
                m_RcvPeerStartTime = CTimer::getTime() - (uint64_t)((uint32_t)ctrlpkt->m_iTimeStamp);
#endif
            }

            m_bPeerRexmitFlag = IsSet(m_RcvPeerSrtOptions, SRT_OPT_REXMITFLG);
            LOGC(mglog.Debug).form("HS RQ: peer %s REXMIT flag\n", m_bPeerRexmitFlag ? "UNDERSTANDS" : "DOES NOT UNDERSTAND" );
            sendSrtMsg(SRT_CMD_HSRSP);
            break;

        default:
            /* Peer tries SRT version handshake we don't support */

            LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(HSREQ) vers=0x%x unsupported: try downgrade\n", 
                    ctrlpkt->getExtendedType(), srtdata[SRT_HS_VERSION]);

            /* Respond with our max supported version, peer may still support it */
            m_RcvPeerSrtOptions = SRT_VERSION_UNK;
            sendSrtMsg(SRT_CMD_HSRSP);
            break;
        }
        break;

    case SRT_CMD_HSRSP:
        if (ctrlpkt->getLength() < SRT_CMD_HSRSP_MINSZ)
        { 
            /* Packet smaller than minimum compatible packet size */
            LOGC(mglog.Error).form( "rcvSrtMsg: cmd=%d(HSRSP) len=%d invalid\n", ctrlpkt->getExtendedType(), ctrlpkt->getLength());
        }
        else switch(SRT_VERSION_MAJ(srtdata[SRT_HS_VERSION]))
        {
#ifdef SRT_VERSION_2XX
        case SRT_VERSION_MAJ2: /* Peer SRT version == 2.x.x */
            LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(HSRSP) len=%d vers=0x%x\n", 
                    ctrlpkt->getExtendedType(), ctrlpkt->getLength(), srtdata[SRT_HS_VERSION]);

            m_PeerSrtVersion = srtdata[SRT_HS_VERSION];
            m_SndPeerSrtOptions = srtdata[SRT_HS_FLAGS];
            // add 2.x.x handshake code here 
            m_SndHsRetryCnt = 0;  /* Handshake done */
            break;

        case SRT_VERSION_MAJ1: /* Peer SRT version == 1.x.x */
            if (m_PeerSrtVersion == 0)
            {
                /* 
                 * Peer does not support our current version,
                 * restart handshake using 1.x.x method
                 */
                LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(HSRSP) len=%d vers=0x%x downgrading handshake\n", 
                        ctrlpkt->getExtendedType(), ctrlpkt->getLength(), srtdata[SRT_HS_VERSION]);

                m_PeerSrtVersion = srtdata[SRT_HS_VERSION];
                m_SndPeerSrtOptions = srtdata[SRT_HS_FLAGS];
                m_SndHsRetryCnt = SRT_MAX_HSRETRY;  /* Reset handshake retry counter */
                m_SndHsLastTime = CTimer::getTime();
                sendSrtMsg(SRT_CMD_HSREQ);
            }
            else
#else
        case SRT_VERSION_MAJ1: /* Peer SRT version == 1.x.x */
#endif
            {        
                /* Response from peer to SRT 1.x.x handshake request */
                LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(HSRSP) len=%d vers=0x%x opts=0x%x delay=%d\n", 
                        ctrlpkt->getExtendedType(), ctrlpkt->getLength(), srtdata[SRT_HS_VERSION], srtdata[SRT_HS_FLAGS], srtdata[SRT_HS_EXTRAS]);

                m_PeerSrtVersion = srtdata[SRT_HS_VERSION];
                m_SndPeerSrtOptions = srtdata[SRT_HS_FLAGS];

                LOGC(mglog.Debug).form("HS RP: Version: %s Flags: SND:%08X (%s) RCV:%08X (%s)\n",
                        SrtVersionString(m_PeerSrtVersion).c_str(),
                        m_SndPeerSrtOptions,
                        SrtFlagString(m_SndPeerSrtOptions).c_str(),
                        m_RcvPeerSrtOptions,
                        SrtFlagString(m_RcvPeerSrtOptions).c_str());

                if (IsSet(m_SndPeerSrtOptions, SRT_OPT_TSBPDRCV))
                {
                    //TsbPd feature enabled
                    m_SndPeerTsbPdDelay = SRT_HS_EXTRAS_LO::unwrap(srtdata[SRT_HS_EXTRAS]);
                }
#ifdef SRT_ENABLE_TLPKTDROP
                if ((m_SrtVersion >= SrtVersion(1, 0, 5)) && IsSet(m_SndPeerSrtOptions, SRT_OPT_TLPKTDROP))
                {
                    //Too late packets dropping feature supported
                    m_bSndPeerTLPktDrop = true;
                }
#endif /* SRT_ENABLE_TLPKTDROP */
#ifdef SRT_ENABLE_NAKREPORT
                if ((m_SrtVersion >= SrtVersion(1, 1, 0)) && IsSet(m_SndPeerSrtOptions, SRT_OPT_NAKREPORT))
                {
                    //Peer will send Periodic NAK Reports
                    m_bSndPeerNakReport = true;
                }
#endif /* SRT_ENABLE_NAKREPORT */

                if ( m_SrtVersion >= SrtVersion(1, 2, 0) )
                {
                    if ( IsSet(m_SndPeerSrtOptions, SRT_OPT_REXMITFLG) )
                    {
                        //Peer will use REXMIT flag in packet retransmission.
                        m_bPeerRexmitFlag = true;
                        LOGC(mglog.Debug).form("HS RP2: I UNDERSTAND REXMIT flag and SO DOES PEER\n");
                    }
                    else
                    {
                        LOGC(mglog.Debug).form("HS RP: I UNDERSTAND REXMIT flag, but PEER DOES NOT\n");
                    }
                }
                else
                {
                    LOGC(mglog.Debug).form("HS RP: I DO NOT UNDERSTAND REXMIT flag\n" );
                }

                m_SndHsRetryCnt = 0;  /* Handshake done */
            }
            break;

        default:
            /* Peer responded with obsolete unsupported version */
            LOGC(mglog.Error).form( "rcvSrtMsg: cmd=%d(HSRSP) vers=0x%x unsuppported version\n", 
                    ctrlpkt->getExtendedType(), srtdata[SRT_HS_VERSION]);
            m_SndHsRetryCnt = 0;  /* Handshake failed, stop trying */
            break;
        }
        break;

    case SRT_CMD_KMREQ: //Receiver
        /* All 32-bit msg fields swapped on reception
         * But HaiCrypt expect network order message
         * Re-swap to cancel it.
         */
        {
            int srtlen = ctrlpkt->getLength()/sizeof(srtdata[SRT_KMR_KMSTATE]);
            for (int i = 0; i < srtlen; i++) 
                srtdata[i] = htonl(srtdata[i]);

            if ((NULL == m_hRcvCrypto) //No crypto context (we are receiver)
                    &&  (0 < m_KmSecret.len)  //We have a shared secret
                    &&  ((srtlen * sizeof(srtdata[SRT_KMR_KMSTATE])) > HCRYPT_MSG_KM_OFS_SALT)) //Sanity on message
            {
                m_iRcvKmKeyLen = (int)hcryptMsg_KM_GetSekLen((unsigned char *)srtdata);
                if (0 < m_iRcvKmKeyLen) m_hRcvCrypto = createCryptoCtx(m_iRcvKmKeyLen, 0);
            }

            if (SRT_KM_S_UNSECURED == m_iRcvPeerKmState)
            {
                m_iRcvPeerKmState = SRT_KM_S_SECURING;
                if (0 == m_KmSecret.len)
                    m_iRcvKmState = SRT_KM_S_NOSECRET;
                else
                    m_iRcvKmState = SRT_KM_S_SECURING;
            }

            /* Maybe we have it now */
            if (NULL != m_hRcvCrypto)
            {
                int rc = HaiCrypt_Rx_Process(m_hRcvCrypto, (unsigned char *)srtdata, ctrlpkt->getLength(), NULL, NULL, 0);
                switch(rc >= 0 ? 0 : rc)
                {
                case 0: //Success
                    m_iRcvPeerKmState = SRT_KM_S_SECURED;
                    m_iRcvKmState = SRT_KM_S_SECURED;
                    //Send back the whole message to confirm
                    break;
                case -2: //Unmatched shared secret to decrypt wrapped key
                    m_iRcvKmState = SRT_KM_S_BADSECRET;
                    //Send status KMRSP message to tel error
                    srtlen = 1;
                    break;
                case -1: //Other errors
                default:
                    m_iRcvKmState = SRT_KM_S_SECURING;
                    //Send status KMRSP message to tel error
                    srtlen = 1;
                    break;
                }
            }
            else
            {
                //Send status KMRSP message to tel error
                srtlen = 1;
            }

            LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(KMREQ) len=%d Peer/RcvKmState=%s/%s\n", 
                    ctrlpkt->getExtendedType(), ctrlpkt->getLength(),
                    SRT_KM_S_SECURED == m_iRcvPeerKmState ? "secured"
                    : SRT_KM_S_SECURING == m_iRcvPeerKmState ? "securing" : "unsecured",
                    SRT_KM_S_SECURED == m_iRcvKmState ? "secured"
                    : SRT_KM_S_NOSECRET == m_iRcvKmState ? "no-secret"
                    : SRT_KM_S_BADSECRET == m_iRcvKmState ? "bad-secret"
                    : SRT_KM_S_SECURING == m_iRcvKmState ? "securing" : "unsecured");

            if (srtlen == 1)
                srtdata[SRT_KMR_KMSTATE] = m_iRcvKmState;
            sendSrtMsg(SRT_CMD_KMRSP, srtdata, srtlen);
        }
        break;

    case SRT_CMD_KMRSP:
        {
            /* All 32-bit msg fields (if present) swapped on reception
             * But HaiCrypt expect network order message
             * Re-swap to cancel it.
             */
            int srtlen = ctrlpkt->getLength()/sizeof(int32_t);
            for (int i = 0; i < srtlen; ++ i)
                srtdata[i] = htonl(srtdata[i]);

            if (srtlen == 1)
            {
                m_iSndPeerKmState = srtdata[SRT_KMR_KMSTATE]; /* Bad or no passphrase */
                m_SndKmMsg[0].iPeerRetry = 0;
                m_SndKmMsg[1].iPeerRetry = 0;
            }
            else if ((m_SndKmMsg[0].MsgLen == (srtlen * sizeof(int32_t)))
                    &&  (0 == memcmp(m_SndKmMsg[0].Msg, srtdata, m_SndKmMsg[0].MsgLen)))
            {
                m_SndKmMsg[0].iPeerRetry = 0;  /* Handshake ctx 0 done */
                m_iSndKmState = SRT_KM_S_SECURED;
                m_iSndPeerKmState = SRT_KM_S_SECURED;

            }
            else if ((m_SndKmMsg[1].MsgLen == (srtlen * sizeof(int32_t)))
                    &&  (0 == memcmp(m_SndKmMsg[1].Msg, srtdata, m_SndKmMsg[1].MsgLen)))
            {
                m_SndKmMsg[1].iPeerRetry = 0;  /* Handshake ctx 1 done */
                m_iSndKmState = SRT_KM_S_SECURED;
                m_iSndPeerKmState = SRT_KM_S_SECURED;
            }
            LOGC(mglog.Note).form( "rcvSrtMsg: cmd=%d(KMRSP) len=%d Snd/PeerKmState=%s/%s\n", 
                    ctrlpkt->getExtendedType(), ctrlpkt->getLength(), 
                    SRT_KM_S_SECURED == m_iSndKmState ? "secured"
                    : SRT_KM_S_SECURING == m_iSndKmState ? "securing" : "unsecured",
                    SRT_KM_S_SECURED == m_iSndPeerKmState ? "secured"
                    : SRT_KM_S_NOSECRET == m_iSndPeerKmState ? "no-secret"
                    : SRT_KM_S_BADSECRET == m_iSndPeerKmState ? "bad-secret"
                    : SRT_KM_S_SECURING == m_iSndPeerKmState ? "securing" : "unsecured");
        }
        break;

    default:
        LOGC(mglog.Error).form( "rcvSrtMsg: cmd=%d len=%d unsupported message\n", ctrlpkt->getExtendedType(), ctrlpkt->getLength());
        break;
    }
}


void CSRTCC::checkSndTimers()
{
    uint64_t now;

    if (!m_bDataSender) return;

    /*
     * SRT Handshake with peer:
     * If...
     * - we want TsbPd mode; and
     * - we have not tried more than CSRTCC_MAXRETRY times (peer may not be SRT); and
     * - and did not get answer back from peer
     * - last sent handshake req should have been replied (RTT*1.5 elapsed); and
     * then (re-)send handshake request.
     */
    if ((m_bSndTsbPdMode)
            &&  (m_SndHsRetryCnt > 0)
            &&  ((m_SndHsLastTime + ((m_iRTT * 3)/2)) <= (now = CTimer::getTime()))) {
        m_SndHsRetryCnt--;
        m_SndHsLastTime = now;
        sendSrtMsg(SRT_CMD_HSREQ);
    }

    /*
     * Crypto Key Distribution to peer:
     * If...
     * - we want encryption; and
     * - we have not tried more than CSRTCC_MAXRETRY times (peer may not be SRT); and
     * - and did not get answer back from peer; and
     * - last sent Keying Material req should have been replied (RTT*1.5 elapsed);
     * then (re-)send handshake request.
     */
    if ((m_hSndCrypto)
            &&  ((m_SndKmMsg[0].iPeerRetry > 0) || (m_SndKmMsg[1].iPeerRetry > 0))
            &&  ((m_SndKmLastTime + ((m_iRTT * 3)/2)) <= (now = CTimer::getTime())))
    {
        for (int ki = 0; ki < 2; ki++)
        {
            if (m_SndKmMsg[ki].iPeerRetry > 0 && m_SndKmMsg[ki].MsgLen > 0)
            {
                m_SndKmMsg[ki].iPeerRetry--;
                m_SndKmLastTime = now;
                sendSrtMsg(SRT_CMD_KMREQ, (int32_t *)m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen/sizeof(int32_t));
            }
        }
    }
    /*
     * Readjust the max SndPeriod onACK (and onTimeout)
     */
    m_dPktSndPeriod = 1000000.0 / (double(m_llSndMaxBW) / (m_iSndAvgPayloadSize + CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE));
#if 0//debug
    static int callcnt = 0;
    if (!(callcnt++ % 100)) fprintf(stderr, "onAck: SndPeriod=%f AvgPkt=%d\n", m_dPktSndPeriod, m_iSndAvgPayloadSize);
#endif
}

void CSRTCC::regenCryptoKm(bool sendit)
{
    if (m_hSndCrypto == NULL) return;

    void *out_p[2];
    size_t out_len_p[2];
    int nbo = HaiCrypt_Tx_ManageKeys(m_hSndCrypto, out_p, out_len_p, 2);
    int sent = 0;

    for (int i = 0; i < nbo && i < 2; i++)
    {
        /*
         * New connection keying material
         * or regenerated after crypto_cfg.km_refresh_rate_pkt packets .
         * Send to peer
         */
        int ki = hcryptMsg_KM_GetKeyIndex((unsigned char *)(out_p[i])) & 0x1;
        if ((out_len_p[i] != m_SndKmMsg[ki].MsgLen)
                ||  (0 != memcmp(out_p[i], m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen))) 
        {
#ifdef DEBUG_KM
            fprintf(stderr, "new key[%d] len=%zd,%zd msg=%0x,%0x\n", 
                    ki, out_len_p[i], m_SndKmMsg[ki].MsgLen,
                    *(int32_t *)out_p[i], *(int32_t *)(&m_SndKmMsg[ki].Msg[0]));
#endif
            /* New Keying material, send to peer */
            memcpy(m_SndKmMsg[ki].Msg, out_p[i], out_len_p[i]);
            m_SndKmMsg[ki].MsgLen = out_len_p[i];
            m_SndKmMsg[ki].iPeerRetry = SRT_MAX_KMRETRY;  

            if (sendit)
            {
                sendSrtMsg(SRT_CMD_KMREQ, (int32_t *)m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen/sizeof(int32_t));
                sent++;
            }
        }
    }
    if (sent)
        m_SndKmLastTime = CTimer::getTime();
}

CSRTCC::CSRTCC()
{
    //Settings
    m_SrtVersion    = SRT_DEF_VERSION;
    m_bDataSender   = false;
    m_bSndTsbPdMode = false;
    m_TsbPdDelay    = 120;  //msec

    m_dCWndSize     = 1000;

    //Data
    m_bRcvTsbPdMode = true;
#ifdef SRT_ENABLE_TLPKTDROP
    m_bRcvTLPktDrop     = false;   //Settings
    m_bSndPeerTLPktDrop = false;   //Data
#endif
#ifdef SRT_ENABLE_NAKREPORT
    m_bRcvNakReport     = false;   //Settings
    m_bSndPeerNakReport = false;   //Data
#endif
    m_bPeerRexmitFlag = false;

    m_PeerSrtVersion = SRT_VERSION_UNK;
    m_SndPeerSrtOptions = 0;
    m_RcvPeerSrtOptions = 0;
    m_RcvPeerStartTime = 0;

    m_SndHsLastTime = 0;
    m_SndHsRetryCnt = SRT_MAX_HSRETRY;

    m_iSndAvgPayloadSize = (7*188);
    m_llSndMaxBW = 30000000/8;    // 30Mbps in Bytes/sec
    m_dPktSndPeriod = 1000000.0 / (double(m_llSndMaxBW) / (m_iSndAvgPayloadSize + CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE));

    m_KmSecret.len = 0;
    //send
    m_iSndKmKeyLen = 0;
    m_iSndKmState = SRT_KM_S_UNSECURED;
    m_iSndPeerKmState = SRT_KM_S_UNSECURED;
    m_SndKmLastTime = 0;
    m_SndKmMsg[0].MsgLen = 0;
    m_SndKmMsg[0].iPeerRetry = 0;
    m_SndKmMsg[1].MsgLen = 0;
    m_SndKmMsg[1].iPeerRetry = 0;
    m_hSndCrypto = NULL;
    //recv
    m_iRcvKmKeyLen = 0;
    m_iRcvKmState = SRT_KM_S_UNSECURED;
    m_iRcvPeerKmState = SRT_KM_S_UNSECURED;
    m_hRcvCrypto = NULL;

    m_sock = 0; // as uninitialized
}

void CSRTCC::init() 
{
    if (m_bDataSender) 
    {
        m_SndHsRetryCnt = SRT_MAX_HSRETRY+1;
        //sendSrtMsg(SRT_CMD_HSREQ);
        //m_SndHsLastTime = CTimer::getTime();
        if ((m_iSndKmKeyLen > 0) && (m_hSndCrypto == NULL))
            m_hSndCrypto = createCryptoCtx(m_iSndKmKeyLen, true);
        if (m_hSndCrypto)
            regenCryptoKm(false);
    }
}

void CSRTCC::close() 
{
    m_sock = 0;

    /* Wipeout secrets */
    memset(&m_KmSecret, 0, sizeof(m_KmSecret));
    m_SrtVersion = SRT_DEF_VERSION;
    m_bDataSender = false;
    m_bSndTsbPdMode = false;
    m_bSndTsbPdMode = false;
#ifdef SRT_ENABLE_TLPKTDROP
    m_bSndPeerTLPktDrop = false;
#endif
#ifdef SRT_ENABLE_NAKREPORT
    m_bSndPeerNakReport = false;
#endif
    m_PeerSrtVersion = SRT_VERSION_UNK;
    m_RcvPeerStartTime = 0;

    m_SndHsLastTime = 0;
    m_SndHsRetryCnt = SRT_MAX_HSRETRY;
}


void CSRTCC::onACK(int32_t ackno) 
{
    (void)ackno; //unused

    /*
     * We are receiving an ACK so we are sender.
     * SRT handshake with peer (receiver) initiated on sender connection (init())
     * Initial Crypto Keying Material too.
     */
    checkSndTimers();
    if (m_hSndCrypto)
        regenCryptoKm();
}

void CSRTCC::onPktSent(const CPacket *pkt)
{
    if ((m_SndHsRetryCnt == SRT_MAX_HSRETRY+1) && m_bDataSender) 
    {
        m_SndHsRetryCnt--;
        m_SndHsLastTime = CTimer::getTime();
        sendSrtMsg(SRT_CMD_HSREQ);
    }
    m_iSndAvgPayloadSize = ((m_iSndAvgPayloadSize * 127) + pkt->getLength()) / 128;
    m_sock = pkt->m_iID;
}

std::string CSRTCC::CONID() const
{
    if ( m_sock == 0 )
        return "";

    std::ostringstream os;
    os << "%" << m_sock << ":";

    return os.str();
}

HaiCrypt_Handle CSRTCC::createCryptoCtx(int keylen, int tx)
{
    HaiCrypt_Handle hCrypto = NULL;

    if ((m_KmSecret.len > 0) && (keylen > 0))
    {
        HaiCrypt_Cfg crypto_cfg;
        memset(&crypto_cfg, 0, sizeof(crypto_cfg));

        crypto_cfg.flags = HAICRYPT_CFG_F_CRYPTO | (tx ? HAICRYPT_CFG_F_TX : 0);
        crypto_cfg.xport = HAICRYPT_XPT_SRT;
        crypto_cfg.cipher = HaiCryptCipher_Get_Instance();
        crypto_cfg.key_len = (size_t)keylen;
        crypto_cfg.data_max_len = HAICRYPT_DEF_DATA_MAX_LENGTH;    //MTU
        crypto_cfg.km_tx_period_ms = 0;//No HaiCrypt KM inject period, handled in SRT;
        crypto_cfg.km_refresh_rate_pkt = HAICRYPT_DEF_KM_REFRESH_RATE;
        crypto_cfg.km_pre_announce_pkt = 0x10000; //HAICRYPT_DEF_KM_PRE_ANNOUNCE;

        memcpy(&crypto_cfg.secret, &m_KmSecret, sizeof(crypto_cfg.secret));

        if (HaiCrypt_Create(&crypto_cfg, &hCrypto))
        {
            LOGC(dlog.Error) << CONID() << "cryptoCtx: could not create " << (tx ? "tx" : "rx") << " crypto ctx";
            hCrypto = NULL;
        }
    }
    else
    {
        LOGC(dlog.Error) << CONID() << "cryptoCtx: missing secret (" << m_KmSecret.len << ") or key length (" << keylen << ")";
    }
    return(hCrypto);
}


HaiCrypt_Handle CSRTCC::getRcvCryptoCtx()
{
    /* 
     * We are receiver and
     * have detected that incoming packets are encrypted
     */
    if (SRT_KM_S_SECURED == m_iRcvKmState) 
    {
        return(m_hRcvCrypto); //Return working crypto only
    }
    if (SRT_KM_S_UNSECURED == m_iRcvPeerKmState) 
    {
        m_iRcvPeerKmState = SRT_KM_S_SECURING;
        if (0 != m_KmSecret.len) 
        {   // We have a passphrase, wait for keying material
            m_iRcvKmState = SRT_KM_S_SECURING;
        }
        else
        {   // We don't have a passphrase, will never decrypt
            m_iRcvKmState = SRT_KM_S_NOSECRET;
        }
    }
    return(NULL);
}


void CSRTCC::freeCryptoCtx()
{
    if (NULL != m_hSndCrypto)
    {
        HaiCrypt_Close(m_hSndCrypto);
        m_hSndCrypto = NULL;
    }
    if (NULL != m_hRcvCrypto)
    {
        HaiCrypt_Close(m_hRcvCrypto);
        m_hRcvCrypto = NULL;
    }
}



std::string SrtFlagString(int32_t flags)
{
#define LEN(arr) (sizeof (arr)/(sizeof ((arr)[0])))

    std::string output;
    static std::string namera[] = { "TSBPD-snd", "TSBPD-rcv", "haicrypt", "TLPktDrop", "NAKReport", "ReXmitFlag" };

    size_t i = 0;
    for ( ; i < LEN(namera); ++i )
    {
        if ( (flags & 1) == 1 )
        {
            output += "+" + namera[i] + " ";
        }
        else
        {
            output += "-" + namera[i] + " ";
        }

        flags >>= 1;
       //if ( flags == 0 )
       //    break;
    }

#undef LEN

    if ( flags != 0 )
    {
        output += "+unknown";
    }

    return output;
}
