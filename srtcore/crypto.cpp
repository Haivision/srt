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
 *****************************************************************************/

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#include <cstring>
#include <string>
#include <sstream>
#include <iterator>

#include "udt.h"
#include "utilities.h"
#include <haicrypt.h>
#include "crypto.h"
#include "logging.h"
#include "core.h"

extern logging::Logger mglog, dlog;

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

// 10* HAICRYPT_DEF_KM_PRE_ANNOUNCE
const int SRT_CRYPT_KM_PRE_ANNOUNCE = 0x10000;

static std::string KmStateStr(SRT_KM_STATE state)
{
    switch (state)
    {
#define TAKE(val) case SRT_KM_S_##val : return #val
        TAKE(UNSECURED);
        TAKE(SECURED);
        TAKE(SECURING);
        TAKE(NOSECRET);
        TAKE(BADSECRET);
#undef TAKE
    default: return "???";
    }
}

static std::string FormatKmMessage(std::string hdr, bool isrcv, int cmd, size_t srtlen, SRT_KM_STATE agt_state, SRT_KM_STATE peer_state)
{
    std::ostringstream os;
    os << hdr << ": cmd=" << cmd << "(" << (cmd == SRT_CMD_KMREQ ? "KMREQ":"KMRSP") <<") len="
        << size_t(srtlen*sizeof(int32_t)) << " ";
    if ( !isrcv )
    {
        os << "Snd/PeerKmState=" << KmStateStr(agt_state) << "/" << KmStateStr(peer_state);
    }
    else
    {
        os << "Peer/RcvKmState=" << KmStateStr(peer_state) << "/" << KmStateStr(agt_state);
    }

    return os.str();
}

void CCryptoControl::updateKmState(int cmd, size_t srtlen)
{
    if (cmd == SRT_CMD_KMREQ)
    {
        if ( SRT_KM_S_UNSECURED == m_iSndKmState)
        {
            m_iSndKmState = SRT_KM_S_SECURING;
            m_iSndPeerKmState = SRT_KM_S_SECURING;
        }
        LOGP(mglog.Note, FormatKmMessage("sndSrtMsg", false, cmd, srtlen, m_iSndKmState, m_iSndPeerKmState));
    }
    else
    {
        LOGP(mglog.Note, FormatKmMessage("sndSrtMsg", true, cmd, srtlen, m_iRcvKmState, m_iRcvPeerKmState));
    }
}


int CCryptoControl::processSrtMsg_KMREQ(const uint32_t* srtdata, size_t bytelen, uint32_t* srtdata_out, ref_t<size_t> r_srtlen, int hsv)
{
    size_t& srtlen = *r_srtlen;
    //Receiver
    /* All 32-bit msg fields swapped on reception
     * But HaiCrypt expect network order message
     * Re-swap to cancel it.
     */
    srtlen = bytelen/sizeof(srtdata[SRT_KMR_KMSTATE]);
    HtoNLA(srtdata_out, srtdata, srtlen);
    unsigned char* kmdata = reinterpret_cast<unsigned char*>(srtdata_out);

    std::vector<unsigned char> kmcopy(kmdata, kmdata + bytelen);

    // The side that has received KMREQ is always an HSD_RESPONDER, regardless of
    // what has called this function. The HSv5 handshake only enforces bidirectional
    // connection.

    bool bidirectional = hsv > CUDT::HS_VERSION_UDT4;

    // Local macro to return rejection appropriately.
    // For HSv5 this function is part of the general handshake, so this should
    // reject the connection.
    // For HSv4 this is received by a custom message after the connection is
    // established, so in this case the rejection response must be sent as KMRSP.
#define KMREQ_RESULT_REJECTION() if (bidirectional) { return SRT_CMD_NONE; } else { srtlen = 1; goto HSv4_ErrorReport; }

    int rc = HAICRYPT_OK; // needed before 'goto' run from KMREQ_RESULT_REJECTION macro
    size_t sek_len = 0;

    // What we have to do:
    // If encryption is on (we know that by having m_KmSecret nonempty), create
    // the crypto context (if bidirectional, create for both sending and receiving).
    // Both crypto contexts should be set with the same length of the key.
    // The problem with interpretinting this should be reported as SRT_CMD_NONE,
    // should be appropriately handled by the caller, as it expects that this
    // function normally return SRT_CMD_KMRSP.
    if ( bytelen <= HCRYPT_MSG_KM_OFS_SALT )  //Sanity on message
    {
        LOGC(mglog.Error) << "processSrtMsg_KMREQ: size of the KM (" << bytelen << ") is too small, must be >" << HCRYPT_MSG_KM_OFS_SALT;
        KMREQ_RESULT_REJECTION();
    }

    // This below probably shouldn't happen because KMREQ isn't expected to be
    // received when Agent does not declare encryption.
    if (m_KmSecret.len == 0)  //We have a shared secret <==> encryption is on
    {
        LOGC(mglog.Error) << "processSrtMsg_KMREQ: Agent does not declare encryption - REJECTING!";
        KMREQ_RESULT_REJECTION();
    }

    LOGC(mglog.Debug) << "KMREQ: getting SEK and creating receiver crypto";
    sek_len = hcryptMsg_KM_GetSekLen(kmdata);
    if ( sek_len == 0 )
    {
        LOGC(mglog.Error) << "processSrtMsg_KMREQ: Received SEK is empty - REJECTING!";
        KMREQ_RESULT_REJECTION();
    }

    m_iRcvKmKeyLen = sek_len;
    if (!createCryptoCtx(Ref(m_hRcvCrypto), m_iRcvKmKeyLen, HAICRYPT_CRYPTO_DIR_RX))
    {
        LOGC(mglog.Error) << "processSrtMsg_KMREQ: Can't create RCV CRYPTO CTX - must reject...";
        KMREQ_RESULT_REJECTION();
    }

    if (bidirectional)
    {
        m_iSndKmKeyLen = m_iRcvKmKeyLen;
        if (!createCryptoCtx(Ref(m_hSndCrypto), m_iSndKmKeyLen, HAICRYPT_CRYPTO_DIR_TX))
        {
            LOGC(mglog.Error) << "processSrtMsg_KMREQ: Can't create SND CRYPTO CTX - must reject...";
            KMREQ_RESULT_REJECTION();
        }
    }

    if (m_iRcvPeerKmState == SRT_KM_S_UNSECURED)
    {
        m_iRcvPeerKmState = SRT_KM_S_SECURING;
        if (0 == m_KmSecret.len)
            m_iRcvKmState = SRT_KM_S_NOSECRET;
        else
            m_iRcvKmState = SRT_KM_S_SECURING;
        LOGC(mglog.Debug) << "processSrtMsg_KMREQ: RCV unsecured - changing state to "
            << (m_iRcvKmState == SRT_KM_S_SECURING ? "SECURING" : "NOSECRET");
    }

    rc = HaiCrypt_Rx_Process(m_hRcvCrypto, kmdata, bytelen, NULL, NULL, 0);
    switch(rc >= 0 ? HAICRYPT_OK : rc)
    {
    case HAICRYPT_OK:
        m_iRcvPeerKmState = SRT_KM_S_SECURED;
        m_iRcvKmState = SRT_KM_S_SECURED;
        LOGC(mglog.Debug) << "KMREQ/rcv: (snd) Rx process successful - SECURED";
        //Send back the whole message to confirm
        break;
    case HAICRYPT_ERROR_WRONG_SECRET: //Unmatched shared secret to decrypt wrapped key
        m_iRcvKmState = SRT_KM_S_BADSECRET;
        //Send status KMRSP message to tel error
        srtlen = 1;
        LOGC(mglog.Error) << "KMREQ/rcv: (snd) Rx process failure - BADSECRET";
        break;
    case HAICRYPT_ERROR: //Other errors
    default:
        m_iRcvKmState = SRT_KM_S_SECURING;
        //Send status KMRSP message to tel error
        srtlen = 1;
        LOGC(mglog.Error) << "KMREQ/rcv: (snd) Rx process failure - SECURING";
        break;
    }

    LOGP(mglog.Note, FormatKmMessage("processSrtMsg_KMREQ", true, SRT_CMD_KMREQ, bytelen, m_iRcvKmState, m_iRcvPeerKmState));

    if (m_iRcvKmState == SRT_KM_S_SECURED && bidirectional )
    {
        // For HSv5, the above error indication should turn into rejection reaction.
        if ( srtlen == 1 )
            return SRT_CMD_NONE;

        m_iSndKmKeyLen = m_iRcvKmKeyLen;
        if (HaiCrypt_Clone(m_hRcvCrypto, HAICRYPT_CRYPTO_DIR_TX, &m_hSndCrypto))
        {
            LOGC(mglog.Error) << "processSrtMsg_KMREQ: Can't create SND CRYPTO CTX - must reject...";
            KMREQ_RESULT_REJECTION();
        }
        if (m_iSndPeerKmState == SRT_KM_S_UNSECURED)
        {
            m_iSndPeerKmState = SRT_KM_S_SECURING;
            if (0 == m_KmSecret.len)
                m_iSndKmState = SRT_KM_S_NOSECRET;
            else
                m_iSndKmState = SRT_KM_S_SECURING;
            LOGC(mglog.Debug) << "processSrtMsg_KMREQ: SND unsecured - changing state to "
                << (m_iSndKmState == SRT_KM_S_SECURING ? "SECURING" : "NOSECRET");
        }

        LOGP(mglog.Note, FormatKmMessage("processSrtMsg_KMREQ", false, SRT_CMD_KMREQ, bytelen, m_iSndKmState, m_iSndPeerKmState));

        if ( srtlen == 1 )
            return SRT_CMD_NONE;
    }

    return SRT_CMD_KMRSP;

HSv4_ErrorReport:
    srtdata_out[SRT_KMR_KMSTATE] = m_iRcvKmState;
    return SRT_CMD_KMRSP;
#undef KMREQ_RESULT_REJECTION
}

int CCryptoControl::processSrtMsg_KMRSP(const uint32_t* srtdata, size_t len, int hsv)
{
    /* All 32-bit msg fields (if present) swapped on reception
     * But HaiCrypt expect network order message
     * Re-swap to cancel it.
     */
    uint32_t srtd[SRTDATA_MAXSIZE];
    size_t srtlen = len/sizeof(uint32_t);
    HtoNLA(srtd, srtdata, srtlen);

    bool bidirectional = hsv > CUDT::HS_VERSION_UDT4;

    if (srtlen == 1) // Error report. Set accordingly.
    {
        m_iSndPeerKmState = SRT_KM_STATE(srtd[SRT_KMR_KMSTATE]); /* Bad or no passphrase */
        m_SndKmMsg[0].iPeerRetry = 0;
        m_SndKmMsg[1].iPeerRetry = 0;
        LOGC(mglog.Error) << "processSrtMsg_KMRSP: received failure report. STATE: " << KmStateStr(m_iSndPeerKmState);
    }
    else
    {
        LOGC(mglog.Debug) << "processSrtMsg_KMRSP: received key response len=" << len;
        // XXX INSECURE << ": [" << FormatBinaryString((uint8_t*)srtd, len) << "]";
        bool key1 = getKmMsg_acceptResponse(0, srtd, len);
        bool key2 = true;
        if ( !key1 )
            key2 = getKmMsg_acceptResponse(1, srtd, len); // <--- NOTE SEQUENCING!

        if (key1 || key2)
        {
            m_iSndKmState = SRT_KM_S_SECURED;
            m_iSndPeerKmState = SRT_KM_S_SECURED;
            LOGC(mglog.Debug) << "processSrtMsg_KMRSP: KM response matches key " << (key1 ? 1 : 2);
        }
        else
        {
            LOGC(mglog.Error) << "processSrtMsg_KMRSP: KM response key matches no key";
            /* XXX INSECURE
            LOGC(mglog.Error) << "processSrtMsg_KMRSP: KM response: [" << FormatBinaryString((uint8_t*)srtd, len)
                << "] matches no key 0=[" << FormatBinaryString((uint8_t*)m_SndKmMsg[0].Msg, m_SndKmMsg[0].MsgLen)
                << "] 1=[" << FormatBinaryString((uint8_t*)m_SndKmMsg[1].Msg, m_SndKmMsg[1].MsgLen) << "]";
                */
        }
    }

    if ( bidirectional )
    {
        m_iRcvKmState = m_iSndKmState;
        m_iRcvPeerKmState = m_iSndPeerKmState;
    }

    LOGP(mglog.Note, FormatKmMessage("processSrtMsg_KMRSP", false, SRT_CMD_KMRSP, len, m_iSndKmState, m_iSndPeerKmState));

    return SRT_CMD_NONE;
}

void CCryptoControl::sendKeysToPeer(Whether2RegenKm regen)
{
    // XXX This must be done somehow differently for bidi
    if ( !m_hSndCrypto )
        return;
    uint64_t now;
    /*
     * Crypto Key Distribution to peer:
     * If...
     * - we want encryption; and
     * - we have not tried more than CSRTCC_MAXRETRY times (peer may not be SRT); and
     * - and did not get answer back from peer; and
     * - last sent Keying Material req should have been replied (RTT*1.5 elapsed);
     * then (re-)send handshake request.
     */
    if (  ((m_SndKmMsg[0].iPeerRetry > 0) || (m_SndKmMsg[1].iPeerRetry > 0))
      &&  ((m_SndKmLastTime + ((m_parent->RTT() * 3)/2)) <= (now = CTimer::getTime())))
    {
        for (int ki = 0; ki < 2; ki++)
        {
            if (m_SndKmMsg[ki].iPeerRetry > 0 && m_SndKmMsg[ki].MsgLen > 0)
            {
                m_SndKmMsg[ki].iPeerRetry--;
                m_SndKmLastTime = now;
                m_parent->sendSrtMsg(SRT_CMD_KMREQ, (uint32_t *)m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen/sizeof(uint32_t));
            }
        }
    }

    if (regen)
        regenCryptoKm(true, m_parent->handshakeVersion() > CUDT::HS_VERSION_UDT4); // regenerate and send
}

void CCryptoControl::regenCryptoKm(bool sendit, bool bidirectional)
{
    if (!m_hSndCrypto)
        return;

    void *out_p[2];
    size_t out_len_p[2];
    int nbo = HaiCrypt_Tx_ManageKeys(m_hSndCrypto, out_p, out_len_p, 2);
    int sent = 0;

    LOGC(mglog.Debug) << "regenCryptoKm: regenerating crypto keys nbo=" << nbo;

    for (int i = 0; i < nbo && i < 2; i++)
    {
        /*
         * New connection keying material
         * or regenerated after crypto_cfg.km_refresh_rate_pkt packets .
         * Send to peer
         */
        // XXX Need to make it clearer and less hardcoded values
        int ki = hcryptMsg_KM_GetKeyIndex((unsigned char *)(out_p[i])) & 0x1;
        if ((out_len_p[i] != m_SndKmMsg[ki].MsgLen)
                ||  (0 != memcmp(out_p[i], m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen))) 
        {

            uint8_t* oldkey = m_SndKmMsg[ki].Msg;
            LOGC(mglog.Debug).form("new key[%d] len=%zd,%zd msg=%0x,%0x\n", 
                    ki, out_len_p[i], m_SndKmMsg[ki].MsgLen,
                    *(int32_t *)out_p[i],
                    *(int32_t *)oldkey);
            /* New Keying material, send to peer */
            memcpy(m_SndKmMsg[ki].Msg, out_p[i], out_len_p[i]);
            m_SndKmMsg[ki].MsgLen = out_len_p[i];
            m_SndKmMsg[ki].iPeerRetry = SRT_MAX_KMRETRY;  

            if (bidirectional)
            {
                // "Send" this key also to myself, just to be applied to the receiver crypto,
                // exactly the same way how this key is interpreted on the peer side into its receiver crypto
                int rc = HaiCrypt_Rx_Process(m_hRcvCrypto, m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen, NULL, NULL, 0);
                if ( rc < 0 )
                {
                    LOGC(mglog.Fatal) << "regenCryptoKm: IPE: applying key generated in snd crypto into rcv crypto: failed code=" << rc;
                    // The party won't be able to decrypt incoming data!
                    // Not sure if anything has to be reported.
                }
            }

            if (sendit)
            {
                m_parent->sendSrtMsg(SRT_CMD_KMREQ, (uint32_t *)m_SndKmMsg[ki].Msg, m_SndKmMsg[ki].MsgLen/sizeof(uint32_t));
                sent++;
            }
        }
    }
    if (sent)
        m_SndKmLastTime = CTimer::getTime();
}

CCryptoControl::CCryptoControl(CUDT* parent, SRTSOCKET id):
m_parent(parent), // should be initialized in createCC()
m_SocketID(id),
m_iSndKmKeyLen(0),
m_iRcvKmKeyLen(0),
m_iSndKmState(SRT_KM_S_UNSECURED),
m_iSndPeerKmState(SRT_KM_S_UNSECURED),
m_iRcvKmState(SRT_KM_S_UNSECURED),
m_iRcvPeerKmState(SRT_KM_S_UNSECURED),
m_bDataSender(false)
{

    m_KmSecret.len = 0;
    //send
    m_SndKmLastTime = 0;
    m_SndKmMsg[0].MsgLen = 0;
    m_SndKmMsg[0].iPeerRetry = 0;
    m_SndKmMsg[1].MsgLen = 0;
    m_SndKmMsg[1].iPeerRetry = 0;
    m_hSndCrypto = NULL;
    //recv
    m_hRcvCrypto = NULL;
}

bool CCryptoControl::init(HandshakeSide side, bool bidirectional)
{
    // NOTE: initiator creates m_hSndCrypto. When bidirectional,
    // it creates also m_hRcvCrypto with the same key length.
    // Acceptor creates nothing - it will create appropriate
    // contexts when receiving KMREQ from the initiator.

    LOGC(mglog.Debug) << "CCryptoControl::init: HS SIDE:"
        << (side == HSD_INITIATOR ? "INITIATOR" : "RESPONDER")
        << " DIRECTION:" << (bidirectional ? "BOTH" : (side == HSD_INITIATOR) ? "SENDER" : "RECEIVER");

    if (bidirectional)
        m_bDataSender = true; // both directions on, so you are always a sender

    if ( side == HSD_INITIATOR )
    {
        if (m_iSndKmKeyLen > 0)
        {
            bool ok = createCryptoCtx(Ref(m_hSndCrypto), m_iSndKmKeyLen, HAICRYPT_CRYPTO_DIR_TX);
            LOGC(mglog.Debug) << "CCryptoControl::init: creating SND crypto context: " << ok;

            if (ok && bidirectional)
            {
                m_iRcvKmKeyLen = m_iSndKmKeyLen;
                int st = HaiCrypt_Clone(m_hSndCrypto, HAICRYPT_CRYPTO_DIR_RX, &m_hRcvCrypto);
                LOGC(mglog.Debug) << "CCryptoControl::init: creating CLONED RCV crypto context: status=" << st;
                ok = st == 0;
            }

            if (!ok)
                return false;

            regenCryptoKm(false, bidirectional); // regen, but don't send.
        }
        else
        {
            LOGC(mglog.Debug) << "CCryptoControl::init: CAN'T CREATE crypto: key length for SND = " << m_iSndKmKeyLen;
        }
    }
    else
    {
        LOGC(mglog.Debug) << "CCryptoControl::init: NOT creating crypto contexts - will be created upon reception of KMREQ";
    }

    return true;
}

void CCryptoControl::close() 
{
    /* Wipeout secrets */
    memset(&m_KmSecret, 0, sizeof(m_KmSecret));
}

std::string CCryptoControl::CONID() const
{
    if ( m_SocketID == 0 )
        return "";

    std::ostringstream os;
    os << "%" << m_SocketID << ":";

    return os.str();
}

static std::string CryptoFlags(int flg)
{
    using namespace std;

    vector<string> f;
    if (flg & HAICRYPT_CFG_F_CRYPTO)
        f.push_back("crypto");
    if (flg & HAICRYPT_CFG_F_TX)
        f.push_back("TX");
    if (flg & HAICRYPT_CFG_F_FEC)
        f.push_back("fec");

    ostringstream os;
    copy(f.begin(), f.end(), ostream_iterator<string>(os, "|"));
    return os.str();
}

bool CCryptoControl::createCryptoCtx(ref_t<HaiCrypt_Handle> hCrypto, size_t keylen, HaiCrypt_CryptoDir cdir)
{
    //HaiCrypt_Handle& hCrypto (rh);

    if (*hCrypto)
    {
        // XXX You can check here if the existing handle represents
        // a correctly defined crypto. But this doesn't seem to be
        // necessary - the whole CCryptoControl facility seems to be valid only
        // within the frames of one connection.
        return true;
    }

    if ((m_KmSecret.len <= 0) || (keylen <= 0))
    {
        LOGC(mglog.Error) << CONID() << "cryptoCtx: missing secret (" << m_KmSecret.len << ") or key length (" << keylen << ")";
        return false;
    }

    HaiCrypt_Cfg crypto_cfg;
    memset(&crypto_cfg, 0, sizeof(crypto_cfg));

    crypto_cfg.flags = HAICRYPT_CFG_F_CRYPTO | (cdir == HAICRYPT_CRYPTO_DIR_TX ? HAICRYPT_CFG_F_TX : 0);
    crypto_cfg.xport = HAICRYPT_XPT_SRT;
    crypto_cfg.cipher = HaiCryptCipher_OpenSSL_EVP();
    crypto_cfg.key_len = (size_t)keylen;
    crypto_cfg.data_max_len = HAICRYPT_DEF_DATA_MAX_LENGTH;    //MTU
    crypto_cfg.km_tx_period_ms = 0;//No HaiCrypt KM inject period, handled in SRT;
    crypto_cfg.km_refresh_rate_pkt = HAICRYPT_DEF_KM_REFRESH_RATE;
    crypto_cfg.km_pre_announce_pkt = SRT_CRYPT_KM_PRE_ANNOUNCE;
    crypto_cfg.secret = m_KmSecret;
    //memcpy(&crypto_cfg.secret, &m_KmSecret, sizeof(crypto_cfg.secret));

    LOGC(mglog.Debug) << "CRYPTO CFG: flags=" << CryptoFlags(crypto_cfg.flags) << " xport=" << crypto_cfg.xport << " cipher=" << crypto_cfg.cipher
        << " keylen=" << crypto_cfg.key_len << " passphrase_length=" << crypto_cfg.secret.len;

    if (HaiCrypt_Create(&crypto_cfg, &hCrypto.get()) != HAICRYPT_OK)
    {
        LOGC(mglog.Error) << CONID() << "cryptoCtx: could not create " << (cdir == HAICRYPT_CRYPTO_DIR_TX ? "tx" : "rx") << " crypto ctx";
        return false;
    }

    LOGC(mglog.Debug) << CONID() << "cryptoCtx: CREATED crypto for dir=" << (cdir == HAICRYPT_CRYPTO_DIR_TX ? "tx" : "rx") << " keylen=" << keylen;

    return true;
}


HaiCrypt_Handle CCryptoControl::getRcvCryptoCtx()
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

    LOGC(mglog.Warn) << "getRcvCryptoCtx: NOT RCV SECURE. States agent=" << KmStateStr(m_iRcvKmState) << " peer=" << KmStateStr(m_iRcvPeerKmState);
    return(NULL);
}

EncryptionStatus CCryptoControl::encrypt(ref_t<CPacket> r_packet)
{
    // Encryption not enabled - do nothing.
    if ( getSndCryptoFlags() == EK_NOENC )
        return ENCS_CLEAR;

    CPacket& packet = *r_packet;
    int rc = HaiCrypt_Tx_Data(m_hSndCrypto, (uint8_t*)packet.getHeader(), (uint8_t*)packet.m_pcData, packet.getLength());
    if (rc < 0)
    {
        return ENCS_FAILED;
    }
    else if ( rc > 0 )
    {
        // XXX what happens if the encryption is said to be "succeeded",
        // but the length is 0? Shouldn't this be treated as unwanted?
        packet.setLength(rc);
    }

    return ENCS_CLEAR;
}

EncryptionStatus CCryptoControl::decrypt(ref_t<CPacket> r_packet)
{
    CPacket& packet = *r_packet;

    if (packet.getMsgCryptoFlags() == EK_NOENC)
    {
        LOGC(mglog.Debug) << "CPacket::decrypt: packet not encrypted";
        return ENCS_CLEAR; // not encrypted, no need do decrypt, no flags to be modified
    }

    // If not secured, preted for securing, but make
    // decryption failed.
    if (m_iRcvKmState != SRT_KM_S_SECURED) 
    {
        if (m_iRcvPeerKmState == SRT_KM_S_UNSECURED) 
        {
            m_iRcvPeerKmState = SRT_KM_S_SECURING;
            if (m_KmSecret.len != 0)
            {   // We have a passphrase, wait for keying material
                m_iRcvKmState = SRT_KM_S_SECURING;
            }
            else
            {   // We don't have a passphrase, will never decrypt
                m_iRcvKmState = SRT_KM_S_NOSECRET;
            }

            LOGC(mglog.Error) << "DECRYPTION FAILED: KM not configured or not yet received";
            return ENCS_FAILED;
        }
    }

    int rc = HaiCrypt_Rx_Data(m_hRcvCrypto, (uint8_t *)packet.getHeader(), (uint8_t *)packet.m_pcData, packet.getLength());
    if ( rc <= 0 )
    {
        LOGC(mglog.Debug) << "decrypt ERROR: HaiCrypt_Rx_Data failure=" << rc << " - returning failed decryption";
        // -1: decryption failure
        // 0: key not received yet
        return ENCS_FAILED;
    }
    // Otherwise: rc == decrypted text length.
    packet.setLength(rc); /* In case clr txt size is different from cipher txt */

    // Decryption succeeded. Update flags.
    packet.setMsgCryptoFlags(EK_NOENC);

    LOGC(mglog.Debug) << "decrypt: successfully decrypted, resulting length=" << rc;
    return ENCS_CLEAR;
}


CCryptoControl::~CCryptoControl()
{
    if (m_hSndCrypto)
    {
        HaiCrypt_Close(m_hSndCrypto);
    }

    if (m_hRcvCrypto)
    {
        HaiCrypt_Close(m_hRcvCrypto);
    }
}


std::string SrtFlagString(int32_t flags)
{
#define LEN(arr) (sizeof (arr)/(sizeof ((arr)[0])))

    std::string output;
    static std::string namera[] = { "TSBPD-snd", "TSBPD-rcv", "haicrypt", "TLPktDrop", "NAKReport", "ReXmitFlag", "StreamAPI" };

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
