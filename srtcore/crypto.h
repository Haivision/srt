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

#ifndef INC__CRYPTO_H
#define INC__CRYPTO_H

#include <cstring>
#include <string>

// UDT
#include "udt.h"
#include "packet.h"
#include "utilities.h"

#include <haicrypt.h>
#include <hcrypt_msg.h>


// For KMREQ/KMRSP. Only one field is used.
const size_t SRT_KMR_KMSTATE = 0;

#define SRT_CMD_MAXSZ       HCRYPT_MSG_KM_MAX_SZ  /* Maximum SRT custom messages payload size (bytes) */
const size_t SRTDATA_MAXSIZE = SRT_CMD_MAXSZ/sizeof(int32_t);

enum Whether2RegenKm {DONT_REGEN_KM = 0, REGEN_KM = 1};

class CCryptoControl
{
//public:
    class CUDT* m_parent;
    SRTSOCKET   m_SocketID;

    size_t      m_iSndKmKeyLen;        //Key length
    size_t      m_iRcvKmKeyLen;        //Key length from rx KM

    // Temporarily allow these to be accessed.
public:
    SRT_KM_STATE m_iSndKmState;         //Sender Km State
    SRT_KM_STATE m_iSndPeerKmState;     //Sender's peer (receiver) Km State
    SRT_KM_STATE m_iRcvKmState;         //Receiver Km State
    SRT_KM_STATE m_iRcvPeerKmState;     //Receiver's peer (sender) Km State

private:
    bool     m_bDataSender;         //Sender side (for crypto, TsbPD handshake)

    HaiCrypt_Secret m_KmSecret;     //Key material shared secret
    // Sender
    uint64_t        m_SndKmLastTime;
    struct {
        unsigned char Msg[HCRYPT_MSG_KM_MAX_SZ];
        size_t MsgLen;
        int iPeerRetry;
    } m_SndKmMsg[2];
    HaiCrypt_Handle m_hSndCrypto;
    // Receiver
    HaiCrypt_Handle m_hRcvCrypto;

public:

    bool sendingAllowed()
    {
        // This function is called to state as to whether the
        // crypter allows the packet to be sent over the link.
        // This is possible in two cases:
        // - when Agent didn't set a password, no matter the crypto state
        if (m_KmSecret.len == 0)
            return true;
        // - when Agent did set a password and the crypto state is SECURED.
        if (m_KmSecret.len > 0 && m_iSndKmState == SRT_KM_S_SECURED
                // && m_iRcvPeerKmState == SRT_KM_S_SECURED ?
           )
            return true;

        return false;
    }

private:

    void regenCryptoKm(bool sendit, bool bidirectional);

public:

    size_t KeyLen() { return m_iSndKmKeyLen; }

    // Needed for CUDT
    void updateKmState(int cmd, size_t srtlen);

    // Detailed processing
    int processSrtMsg_KMREQ(const uint32_t* srtdata, size_t len, uint32_t* srtdata_out, ref_t<size_t> r_srtlen, int hsv);
    int processSrtMsg_KMRSP(const uint32_t* srtdata, size_t len, int hsv);

    const unsigned char* getKmMsg_data(size_t ki) const { return m_SndKmMsg[ki].Msg; }
    size_t getKmMsg_size(size_t ki) const { return m_SndKmMsg[ki].MsgLen; }
    bool getKmMsg_needSend(size_t ki) const
    {
        return (m_SndKmMsg[ki].iPeerRetry > 0 && m_SndKmMsg[ki].MsgLen > 0);
    }

    void getKmMsg_markSent(size_t ki)
    {
        m_SndKmMsg[ki].iPeerRetry--;
        m_SndKmLastTime = CTimer::getTime();
    }

    bool getKmMsg_acceptResponse(size_t ki, const uint32_t* srtmsg, size_t bytesize)
    {
        if ( m_SndKmMsg[ki].MsgLen == bytesize
                && 0 == memcmp(m_SndKmMsg[ki].Msg, srtmsg, m_SndKmMsg[ki].MsgLen))
        {
            m_SndKmMsg[ki].iPeerRetry = 0;
            return true;
        }
        return false;
    }

    CCryptoControl(CUDT* parent, SRTSOCKET id);

    std::string CONID() const;

    bool init(HandshakeSide, bool);
    void close();

    // This function is used in:
    // - HSv4 (initial key material exchange - in HSv5 it's attached to handshake)
    // - case of key regeneration, which should be then exchanged again
    void sendKeysToPeer(Whether2RegenKm regen);


    void setCryptoSecret(const HaiCrypt_Secret& secret)
    {
        m_KmSecret = secret;
        //memcpy(&m_KmSecret, &secret, sizeof(m_KmSecret));
    }

    void setSndCryptoKeylen(size_t keylen)
    {
        m_iSndKmKeyLen = keylen;
        m_bDataSender = true;
    }

    void setCryptoKeylen(size_t keylen)
    {
        m_iSndKmKeyLen = keylen;
        m_iRcvKmKeyLen = keylen;
    }

    bool createCryptoCtx(ref_t<HaiCrypt_Handle> rh, size_t keylen, HaiCrypt_CryptoDir tx);

    HaiCrypt_Handle getSndCryptoCtx() const
    {
        return(m_hSndCrypto);
    }

    HaiCrypt_Handle getRcvCryptoCtx();

    int getSndCryptoFlags() const
    {
        return(m_hSndCrypto ? HaiCrypt_Tx_GetKeyFlags(m_hSndCrypto) : 0);
    }

    /// Encrypts the packet. If encryption is not turned on, it
    /// does nothing. If the encryption is not correctly configured,
    /// the encryption will fail.
    /// XXX Encryption flags in the PH_MSGNO
    /// field in the header must be correctly set before calling.
    EncryptionStatus encrypt(ref_t<CPacket> r_packet);

    /// Decrypts the packet. If the packet has ENCKEYSPEC part
    /// in PH_MSGNO set to EK_NOENC, it does nothing. It decrypts
    /// only if the encryption correctly configured, otherwise it
    /// fails. After successful decryption, the ENCKEYSPEC part
    // in PH_MSGNO is set to EK_NOENC.
    EncryptionStatus decrypt(ref_t<CPacket> r_packet);

    ~CCryptoControl();
};

#endif // SRT_CONGESTION_CONTROL_H
