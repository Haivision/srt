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

#define SRT_BELATED_LOSSREPORT 1


#ifndef __UDT_CORE_H__
#define __UDT_CORE_H__

#include <deque>
#include <sstream>

#include "udt.h"
#include "common.h"
#include "list.h"
#include "buffer.h"
#include "window.h"
#include "packet.h"
#include "channel.h"
#include "api.h"
#include "cache.h"
#include "queue.h"
#include "handshake.h"
#include "utilities.h"

#include <haicrypt.h>

extern logging::Logger
    glog,
    blog,
    mglog,
    dlog,
    tslog,
    rxlog;


// XXX Utility function - to be moved to utilities.h?
template <class T>
inline T CountIIR(T base, T newval, double factor)
{
    if ( base == 0.0 )
        return newval;

    T diff = newval - base;
    return base+T(diff*factor);
}

// XXX Probably a better rework for that can be done - this can be
// turned into a serializable structure, just like it's for CHandShake.
enum AckDataItem
{
    ACKD_RCVLASTACK = 0,
    ACKD_RTT = 1,
    ACKD_RTTVAR = 2,
    ACKD_BUFFERLEFT = 3,
    ACKD_TOTAL_SIZE_UDTBASE = 4,

    // Extra stats for SRT
    ACKD_RCVSPEED = 4,   // length would be 16
    ACKD_BANDWIDTH = 5,
    ACKD_TOTAL_SIZE_VER100 = 6, // length = 24
    ACKD_RCVRATE = 6,
    ACKD_TOTAL_SIZE_VER101 = 7, // length = 28
    ACKD_XMRATE = 7, // XXX This is a weird compat stuff. Version 1.1.3 defines it as ACKD_BANDWIDTH*m_iPayloadSize when set. Never got.
                     // XXX NOTE: field number 7 may be used for something in future, need to confirm destruction of all !compat 1.0.2 version

    ACKD_TOTAL_SIZE_VER102 = 8, // 32
// FEATURE BLOCKED. Probably not to be restored.
//  ACKD_ACKBITMAP = 8,
    ACKD_TOTAL_SIZE = ACKD_TOTAL_SIZE_VER102 // length = 32 (or more)
};
const size_t ACKD_FIELD_SIZE = sizeof(int32_t);

// For HSv4 legacy handshake
#define SRT_MAX_HSRETRY     10          /* Maximum SRT handshake retry */

enum SeqPairItems
{
    SEQ_BEGIN = 0, SEQ_END = 1, SEQ_SIZE = 2
};

// Extended SRT Congestion control class - only an incomplete definition required
class CCryptoControl;

class CUDT
{
    friend class CUDTSocket;
    friend class CUDTUnited;
    friend class CCC;
    friend struct CUDTComp;
    friend class CCache<CInfoBlock>;
    friend class CRendezvousQueue;
    friend class CSndQueue;
    friend class CRcvQueue;
    friend class CSndUList;
    friend class CRcvUList;

private: // constructor and desctructor

    void construct();
    void clearData();
    CUDT();
    CUDT(const CUDT& ancestor);
    const CUDT& operator=(const CUDT&) {return *this;}
    ~CUDT();

public: //API
    static int startup();
    static int cleanup();
    static UDTSOCKET socket(int af, int type = SOCK_STREAM, int protocol = 0);
    static int bind(UDTSOCKET u, const sockaddr* name, int namelen);
    static int bind(UDTSOCKET u, UDPSOCKET udpsock);
    static int listen(UDTSOCKET u, int backlog);
    static UDTSOCKET accept(UDTSOCKET u, sockaddr* addr, int* addrlen);
    static int connect(UDTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn);
    static int close(UDTSOCKET u);
    static int getpeername(UDTSOCKET u, sockaddr* name, int* namelen);
    static int getsockname(UDTSOCKET u, sockaddr* name, int* namelen);
    static int getsockopt(UDTSOCKET u, int level, UDT_SOCKOPT optname, void* optval, int* optlen);
    static int setsockopt(UDTSOCKET u, int level, UDT_SOCKOPT optname, const void* optval, int optlen);
    static int send(UDTSOCKET u, const char* buf, int len, int flags);
    static int recv(UDTSOCKET u, char* buf, int len, int flags);
#ifdef SRT_ENABLE_SRCTIMESTAMP
    static int sendmsg(UDTSOCKET u, const char* buf, int len, int ttl = -1, bool inorder = false, uint64_t srctime = 0LL);
    static int recvmsg(UDTSOCKET u, char* buf, int len, uint64_t& srctime);
#else
    static int sendmsg(UDTSOCKET u, const char* buf, int len, int ttl = -1, bool inorder = false);
#endif
    static int recvmsg(UDTSOCKET u, char* buf, int len);
    static int64_t sendfile(UDTSOCKET u, std::fstream& ifs, int64_t& offset, int64_t size, int block = 364000);
    static int64_t recvfile(UDTSOCKET u, std::fstream& ofs, int64_t& offset, int64_t size, int block = 7280000);
    static int select(int nfds, ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout);
    static int selectEx(const std::vector<UDTSOCKET>& fds, std::vector<UDTSOCKET>* readfds, std::vector<UDTSOCKET>* writefds, std::vector<UDTSOCKET>* exceptfds, int64_t msTimeOut);
    static int epoll_create();
    static int epoll_add_usock(const int eid, const UDTSOCKET u, const int* events = NULL);
    static int epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    static int epoll_remove_usock(const int eid, const UDTSOCKET u);
    static int epoll_remove_ssock(const int eid, const SYSSOCKET s);
    static int epoll_update_usock(const int eid, const UDTSOCKET u, const int* events = NULL);
    static int epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    static int epoll_wait(const int eid, std::set<UDTSOCKET>* readfds, std::set<UDTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
    static int epoll_release(const int eid);
    static CUDTException& getlasterror();
    static int perfmon(UDTSOCKET u, CPerfMon* perf, bool clear = true);
    static int bstats(UDTSOCKET u, CBytePerfMon* perf, bool clear = true);
    static UDTSTATUS getsockstate(UDTSOCKET u);
    static bool setstreamid(UDTSOCKET u, const std::string& sid);
    static std::string getstreamid(UDTSOCKET u);

public: // internal API
    static CUDT* getUDTHandle(UDTSOCKET u);
    static std::vector<UDTSOCKET> existingSockets();

    void addressAndSend(CPacket& pkt);
    void sendSrtMsg(int cmd, uint32_t *srtdata_in = NULL, int srtlen_in = 0);

    bool isTsbPd() { return m_bOPT_TsbPd; }
    int RTT() { return m_iRTT; }

private:
    /// initialize a UDT entity and bind to a local address.

    void open();

    /// Start listening to any connection request.

    void setListenState();

    /// Connect to a UDT entity listening at address "peer".
    /// @param peer [in] The address of the listening UDT entity.

    void startConnect(const sockaddr* peer, int32_t forced_isn);

    /// Process the response handshake packet. Failure reasons can be:
    /// * Socket is not in connecting state
    /// * Response @a pkt is not a handshake control message
    /// * Rendezvous socket has once processed a regular handshake
    /// @param pkt [in] handshake packet.
    /// @retval 0 Connection successful
    /// @retval 1 Connection in progress (m_ConnReq turned into RESPONSE)
    /// @retval -1 Connection failed

    EConnectStatus processConnectResponse(const CPacket& pkt, CUDTException* eout, bool synchro) ATR_NOEXCEPT;


    // This function works in case of HSv5 rendezvous. It changes the state
    // according to the present state and received message type, as well as the
    // INITIATOR/RESPONDER side resolved through cookieContest().
    // The resulting data are:
    // - rsptype: handshake message type that should be sent back to the peer (nothing if URQ_DONE)
    // - needs_extension: the HSREQ/KMREQ or HSRSP/KMRSP extensions should be attached to the handshake message.
    // - RETURNED VALUE: if true, it means a URQ_CONCLUSION message was received with HSRSP/KMRSP extensions and needs HSRSP/KMRSP.
    bool rendezvousSwitchState(ref_t<UDTRequestType> rsptype, ref_t<bool> needs_extension);
    void cookieContest();
    EConnectStatus processRendezvous(ref_t<CPacket> reqpkt, const CPacket &response, const sockaddr* serv_addr, bool synchro);
    bool prepareConnectionObjects(const CHandShake &hs, HandshakeSide hsd, CUDTException *eout);
    EConnectStatus postConnect(const CPacket& response, bool rendezvous, CUDTException* eout, bool synchro);
    void applyResponseSettings();
    EConnectStatus processAsyncConnectResponse(const CPacket& pkt) ATR_NOEXCEPT;
    bool processAsyncConnectRequest(EConnectStatus cst, const CPacket& response, const sockaddr* serv_addr);


    size_t fillSrtHandshake_HSREQ(uint32_t* srtdata, size_t srtlen, int hs_version);
    size_t fillSrtHandshake_HSRSP(uint32_t* srtdata, size_t srtlen, int hs_version);
    size_t fillSrtHandshake(uint32_t* srtdata, size_t srtlen, int msgtype, int hs_version);

    bool createSrtHandshake(ref_t<CPacket> reqpkt, ref_t<CHandShake> hs,
            int srths_cmd, int srtkm_cmd, const uint32_t* data, size_t datalen);

    size_t prepareSrtHsMsg(int cmd, uint32_t* srtdata, size_t size);

    void processSrtMsg(const CPacket *ctrlpkt);
    int processSrtMsg_HSREQ(const uint32_t* srtdata, size_t len, uint32_t ts, int hsv);
    int processSrtMsg_HSRSP(const uint32_t* srtdata, size_t len, uint32_t ts, int hsv);
    bool interpretSrtHandshake(const CHandShake& hs, const CPacket& hspkt, uint32_t* out_data, size_t* out_len);

    void updateAfterSrtHandshake(int srt_cmd, int hsv);

    void updateSrtRcvSettings();
    void updateSrtSndSettings();

    /// Connect to a UDT entity listening at address "peer", which has sent "hs" request.
    /// @param peer [in] The address of the listening UDT entity.
    /// @param hs [in/out] The handshake information sent by the peer side (in), negotiated value (out).

    void acceptAndRespond(const sockaddr* peer, CHandShake* hs, const CPacket& hspkt);

    /// Close the opened UDT entity.

    void close();

    /// Request UDT to send out a data block "data" with size of "len".
    /// @param data [in] The address of the application data to be sent.
    /// @param len [in] The size of the data block.
    /// @return Actual size of data sent.

    int send(const char* data, int len);

    /// Request UDT to receive data to a memory block "data" with size of "len".
    /// @param data [out] data received.
    /// @param len [in] The desired size of data to be received.
    /// @return Actual size of data received.

    int recv(char* data, int len);

    /// send a message of a memory block "data" with size of "len".
    /// @param data [out] data received.
    /// @param len [in] The desired size of data to be received.
    /// @param ttl [in] the time-to-live of the message.
    /// @param inorder [in] if the message should be delivered in order.
    /// @param srctime [in] Time when the data were ready to send.
    /// @return Actual size of data sent.

#ifdef SRT_ENABLE_SRCTIMESTAMP
    int sendmsg(const char* data, int len, int ttl, bool inorder, uint64_t srctime);
#else
    int sendmsg(const char* data, int len, int ttl, bool inorder);
#endif
    /// Receive a message to buffer "data".
    /// @param data [out] data received.
    /// @param len [in] size of the buffer.
    /// @return Actual size of data received.

#ifdef SRT_ENABLE_SRCTIMESTAMP
    int recvmsg(char* data, int len, uint64_t& srctime);
#endif
    int recvmsg(char* data, int len);

    /// Request UDT to send out a file described as "fd", starting from "offset", with size of "size".
    /// @param ifs [in] The input file stream.
    /// @param offset [in, out] From where to read and send data; output is the new offset when the call returns.
    /// @param size [in] How many data to be sent.
    /// @param block [in] size of block per read from disk
    /// @return Actual size of data sent.

    int64_t sendfile(std::fstream& ifs, int64_t& offset, int64_t size, int block = 366000);

    /// Request UDT to receive data into a file described as "fd", starting from "offset", with expected size of "size".
    /// @param ofs [out] The output file stream.
    /// @param offset [in, out] From where to write data; output is the new offset when the call returns.
    /// @param size [in] How many data to be received.
    /// @param block [in] size of block per write to disk
    /// @return Actual size of data received.

    int64_t recvfile(std::fstream& ofs, int64_t& offset, int64_t size, int block = 7320000);

    /// Configure UDT options.
    /// @param optName [in] The enum name of a UDT option.
    /// @param optval [in] The value to be set.
    /// @param optlen [in] size of "optval".

    void setOpt(UDT_SOCKOPT optName, const void* optval, int optlen);

    /// Read UDT options.
    /// @param optName [in] The enum name of a UDT option.
    /// @param optval [in] The value to be returned.
    /// @param optlen [out] size of "optval".

    void getOpt(UDT_SOCKOPT optName, void* optval, int& optlen);

    /// read the performance data since last sample() call.
    /// @param perf [in, out] pointer to a CPerfMon structure to record the performance data.
    /// @param clear [in] flag to decide if the local performance trace should be cleared.

    void sample(CPerfMon* perf, bool clear = true);

    // XXX please document
    void bstats(CBytePerfMon* perf, bool clear = true);

    /// Mark sequence contained in the given packet as not lost. This
    /// removes the loss record from both current receiver loss list and
    /// the receiver fresh loss list.
    void unlose(const CPacket& oldpacket);
    void unlose(int32_t from, int32_t to);

private:
    static CUDTUnited s_UDTUnited;               // UDT global management base

public:
    static const UDTSOCKET INVALID_SOCK;         // invalid socket descriptor
    static const int ERROR;                      // socket api error returned value

    int handshakeVersion()
    {
        return m_ConnRes.m_iVersion;
    }

    std::string CONID() const
    {
#if ENABLE_LOGGING
        std::ostringstream os;
        os << "%" << m_SocketID << ":";
        return os.str();
#else
        return "";
#endif
    }

    UDTSOCKET socketID() { return m_SocketID; }

private: // Identification
    UDTSOCKET m_SocketID;                        // UDT socket number
    UDTSockType m_iSockType;                     // Type of the UDT connection (SOCK_STREAM or SOCK_DGRAM)
    UDTSOCKET m_PeerID;                          // peer id, for multiplexer
public:
    static const int HS_VERSION_UDT4 = 4;
    static const int HS_VERSION_SRT1 = 5;

private: // Packet sizes
    int m_iPktSize;                              // Maximum/regular packet size, in bytes
    int m_iPayloadSize;                          // Maximum/regular payload size, in bytes

private: // Options
    int m_iMSS;                                  // Maximum Segment Size, in bytes
    bool m_bSynSending;                          // Sending syncronization mode
    bool m_bSynRecving;                          // Receiving syncronization mode
    int m_iFlightFlagSize;                       // Maximum number of packets in flight from the peer side
    int m_iSndBufSize;                           // Maximum UDT sender buffer size
    int m_iRcvBufSize;                           // Maximum UDT receiver buffer size
    linger m_Linger;                             // Linger information on close
    int m_iUDPSndBufSize;                        // UDP sending buffer size
    int m_iUDPRcvBufSize;                        // UDP receiving buffer size
    int m_iIPversion;                            // IP version
    bool m_bRendezvous;                          // Rendezvous connection mode
#ifdef SRT_ENABLE_CONNTIMEO
    int m_iConnTimeOut;                          // connect timeout in milliseconds
#endif
    int m_iSndTimeOut;                           // sending timeout in milliseconds
    int m_iRcvTimeOut;                           // receiving timeout in milliseconds
    bool m_bReuseAddr;                           // reuse an exiting port or not, for UDP multiplexer
    int64_t m_llMaxBW;                           // maximum data transfer rate (threshold)
#ifdef SRT_ENABLE_IPOPTS
    int m_iIpTTL;
    int m_iIpToS;
#endif
    // These fields keep the options for encryption
    // (SRTO_PASSPHRASE, SRTO_PBKEYLEN). Crypto object is
    // created later and takes values from these.
    HaiCrypt_Secret m_CryptoSecret;
    int m_iSndCryptoKeyLen;

    bool m_bDataSender;
    bool m_bTwoWayData;

    uint64_t m_SndHsLastTime;	    //Last SRT handshake request time
    int      m_SndHsRetryCnt;       //SRT handshake retries left

    // MOVED FROM CSRTCC
    double m_dPktSndPeriod;              // Packet sending period, in microseconds
    double m_dCWndSize;                  // Congestion window size, in packets
    double m_dMaxCWndSize;               // maximum cwnd size, in packets
    int m_iRcvRate;			// packet arrive rate at receiver side, packets per second
    int m_iACKPeriod;                    // Periodical timer to send an ACK, in milliseconds
    int m_iACKInterval;                  // How many packets to send one ACK, in packets
    bool m_bUserDefinedRTO;              // if the RTO value is defined by users
    int m_iRTO;                          // RTO value, microseconds

    int64_t  m_llSndMaxBW;          //Max bandwidth (bytes/sec)
    int      m_iSndAvgPayloadSize;  //Average Payload Size of packets to xmit

    void updatePktSndPeriod()
    {
        double pktsize = m_iSndAvgPayloadSize + CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
        m_dPktSndPeriod = 1000000.0 * (pktsize/m_llSndMaxBW);
    }

    void considerLegacySrtHandshake(uint64_t timebase);

    void setMaxBW(int64_t maxbw);
    void checkSndTimers(Whether2RegenKm regen = DONT_REGEN_KM);

    void handshakeDone()
    {
        m_SndHsRetryCnt = 0;
    }


    bool m_bOPT_TsbPd;               // Whether AGENT will do TSBPD Rx (whether peer does, is not agent's problem)
    int m_iOPT_TsbPdDelay;           // Agent's Rx latency
    int m_iOPT_PeerTsbPdDelay;       // Peer's Rx latency for the traffic made by Agent's Tx.
    bool m_bOPT_TLPktDrop;            // Whether Agent WILL DO TLPKTDROP on Rx.
    std::string m_sStreamName;

    int m_iTsbPdDelay;                           // Rx delay to absorb burst in milliseconds
    int m_iPeerTsbPdDelay;                       // Tx delay that the peer uses to absorb burst in milliseconds
#ifdef SRT_ENABLE_TLPKTDROP
    bool m_bTLPktDrop;                           // Enable Too-late Packet Drop
#endif /* SRT_ENABLE_TLPKTDROP */
    int64_t m_llInputBW;                         // Input stream rate (bytes/sec)
    int m_iOverheadBW;                           // Percent above input stream rate (applies if m_llMaxBW == 0)
#ifdef SRT_ENABLE_NAKREPORT
    bool m_bRcvNakReport;                        // Enable Receiver Periodic NAK Reports
#endif
private: // congestion control
    UniquePtr<CCryptoControl> m_pCryptoControl;                            // congestion control SRT class (small data extension)
    CCache<CInfoBlock>* m_pCache;                // network information cache

private: // Status
    volatile bool m_bListening;                  // If the UDT entit is listening to connection
    volatile bool m_bConnecting;                 // The short phase when connect() is called but not yet completed
    volatile bool m_bConnected;                  // Whether the connection is on or off
    volatile bool m_bClosing;                    // If the UDT entity is closing
    volatile bool m_bShutdown;                   // If the peer side has shutdown the connection
    volatile bool m_bBroken;                     // If the connection has been broken
    volatile bool m_bPeerHealth;                 // If the peer status is normal
    bool m_bOpened;                              // If the UDT entity has been opened
    int m_iBrokenCounter;                        // a counter (number of GC checks) to let the GC tag this socket as disconnected

    int m_iEXPCount;                             // Expiration counter
    int m_iBandwidth;                            // Estimated bandwidth, number of packets per second
    int m_iRTT;                                  // RTT, in microseconds
    int m_iRTTVar;                               // RTT variance
    int m_iDeliveryRate;                         // Packet arrival rate at the receiver side

    uint64_t m_ullLingerExpiration;              // Linger expiration time (for GC to close a socket with data in sending buffer)

    CHandShake m_ConnReq;                        // connection request
    CHandShake m_ConnRes;                        // connection response
    CHandShake::RendezvousState m_RdvState;      // HSv5 rendezvous state
    HandshakeSide m_SrtHsSide;                   // HSv5 rendezvous handshake side resolved from cookie contest (DRAW if not yet resolved)
    int64_t m_llLastReqTime;                     // last time when a connection request is sent

private: // Sending related data
    CSndBuffer* m_pSndBuffer;                    // Sender buffer
    CSndLossList* m_pSndLossList;                // Sender loss list
    CPktTimeWindow<16, 16> m_SndTimeWindow;            // Packet sending time window

    volatile uint64_t m_ullInterval;             // Inter-packet time, in CPU clock cycles
    uint64_t m_ullTimeDiff;                      // aggregate difference in inter-packet time

    volatile int m_iFlowWindowSize;              // Flow control window size
    volatile double m_dCongestionWindow;         // congestion window size

#ifdef SRT_ENABLE_TLPKTDROP
    volatile int32_t m_iSndLastFullAck;          // Last full ACK received
#endif /* SRT_ENABLE_TLPKTDROP */
    volatile int32_t m_iSndLastAck;              // Last ACK received
    volatile int32_t m_iSndLastDataAck;          // The real last ACK that updates the sender buffer and loss list
    volatile int32_t m_iSndCurrSeqNo;            // The largest sequence number that has been sent
    int32_t m_iLastDecSeq;                       // Sequence number sent last decrease occurs
    int32_t m_iSndLastAck2;                      // Last ACK2 sent back
    uint64_t m_ullSndLastAck2Time;               // The time when last ACK2 was sent back
#ifdef SRT_ENABLE_CBRTIMESTAMP
    uint64_t m_ullSndLastCbrTime;                 // Last timestamp set in a data packet to send (usec)
#endif

    int32_t m_iISN;                              // Initial Sequence Number
    bool m_bPeerTsbPd;                            // Peer accept TimeStamp-Based Rx mode
#ifdef SRT_ENABLE_TLPKTDROP
    bool m_bPeerTLPktDrop;                        // Enable sender late packet dropping
#endif /* SRT_ENABLE_TLPKTDROP */
#ifdef SRT_ENABLE_NAKREPORT
    int m_iMinNakInterval;                       // Minimum NAK Report Period (usec)
    int m_iNakReportAccel;                       // NAK Report Period (RTT) accelerator
    bool m_bPeerNakReport;                    // Sender's peer (receiver) issues Periodic NAK Reports
    bool m_bPeerRexmitFlag;                   // Receiver supports rexmit flag in payload packets
#endif /* SRT_ENABLE_NAKREPORT */
#ifdef SRT_ENABLE_FASTREXMIT
    int32_t m_iReXmitCount;                      // Re-Transmit Count since last ACK
#endif /* SRT_ENABLE_FASTREXMIT */

    void CCUpdate();

private: // Receiving related data
    CRcvBuffer* m_pRcvBuffer;               //< Receiver buffer
    CRcvLossList* m_pRcvLossList;           //< Receiver loss list
#if SRT_BELATED_LOSSREPORT
    std::deque<CRcvFreshLoss> m_FreshLoss;  //< Lost sequence already added to m_pRcvLossList, but not yet sent UMSG_LOSSREPORT for.
    int m_iReorderTolerance;                //< Current value of dynamic reorder tolerance
    int m_iMaxReorderTolerance;             //< Maximum allowed value for dynamic reorder tolerance
    int m_iConsecEarlyDelivery;             //< Increases with every OOO packet that came <TTL-2 time, resets with every increased reorder tolerance
    int m_iConsecOrderedDelivery;           //< Increases with every packet coming in order or retransmitted, resets with every out-of-order packet
#endif

    CACKWindow<1024> m_ACKWindow;             //< ACK history window
    CPktTimeWindow<16, 64> m_RcvTimeWindow;   //< Packet arrival time window

    int32_t m_iRcvLastAck;                       //< Last sent ACK
#ifdef ENABLE_LOGGING
    int32_t m_iDebugPrevLastAck;
#endif
#ifdef SRT_ENABLE_TLPKTDROP
    int32_t m_iRcvLastSkipAck;                   // Last dropped sequence ACK
#endif /* SRT_ENABLE_TLPKTDROP */
    uint64_t m_ullLastAckTime;                   // Timestamp of last ACK
    int32_t m_iRcvLastAckAck;                    // Last sent ACK that has been acknowledged
    int32_t m_iAckSeqNo;                         // Last ACK sequence number
    int32_t m_iRcvCurrSeqNo;                     // Largest received sequence number

    uint64_t m_ullLastWarningTime;               // Last time that a warning message is sent

    int32_t m_iPeerISN;                          // Initial Sequence Number of the peer side
    uint64_t m_ullRcvPeerStartTime;

    uint32_t m_lSrtVersion;
    uint32_t m_lMinimumPeerSrtVersion;
    uint32_t m_lPeerSrtVersion;

    bool m_bTsbPd;                            // Peer sends TimeStamp-Based Packet Delivery Packets 
    pthread_t m_RcvTsbPdThread;                  // Rcv TsbPD Thread handle
    pthread_cond_t m_RcvTsbPdCond;
    bool m_bTsbPdAckWakeup;                      // Signal TsbPd thread on Ack sent
    static void* tsbpd(void* param);

private: // synchronization: mutexes and conditions
    pthread_mutex_t m_ConnectionLock;            // used to synchronize connection operation

    pthread_cond_t m_SendBlockCond;              // used to block "send" call
    pthread_mutex_t m_SendBlockLock;             // lock associated to m_SendBlockCond

    pthread_mutex_t m_AckLock;                   // used to protected sender's loss list when processing ACK

    pthread_cond_t m_RecvDataCond;               // used to block "recv" when there is no data
    pthread_mutex_t m_RecvDataLock;              // lock associated to m_RecvDataCond

    pthread_mutex_t m_SendLock;                  // used to synchronize "send" call
    pthread_mutex_t m_RecvLock;                  // used to synchronize "recv" call

    pthread_mutex_t m_RcvLossLock;               // Protects the receiver loss list (access: CRcvQueue::worker, CUDT::tsbpd)

    // This is required to synchronize the background part of the closing socket process
    // with the call of srt_close(). The condition is broadcast at the end regardless of
    // the settings. The srt_close() function is blocked from exiting until this signal
    // is received when the socket is set SRTO_SNDSYN.
    pthread_mutex_t m_CloseSynchLock;
    pthread_cond_t m_CloseSynchCond;

    void initSynch();
    void destroySynch();
    void releaseSynch();

private: // Common connection Congestion Control setup
    bool setupCC();
    bool createCrypter(HandshakeSide side, bool bidi);

private: // Generation and processing of packets
    void sendCtrl(UDTMessageType pkttype, void* lparam = NULL, void* rparam = NULL, int size = 0);
    void processCtrl(CPacket& ctrlpkt);
    int packData(CPacket& packet, uint64_t& ts);
    int processData(CUnit* unit);
    int processConnectRequest(const sockaddr* addr, CPacket& packet);
    static void addLossRecord(std::vector<int32_t>& lossrecord, int32_t lo, int32_t hi);
    int32_t bake(const sockaddr* addr, int32_t previous_cookie = 0, int correction = 0);

private: // Trace
    uint64_t m_StartTime;                        // timestamp when the UDT entity is started
    int64_t m_llSentTotal;                       // total number of sent data packets, including retransmissions
    int64_t m_llRecvTotal;                       // total number of received packets
    int m_iSndLossTotal;                         // total number of lost packets (sender side)
    int m_iRcvLossTotal;                         // total number of lost packets (receiver side)
    int m_iRetransTotal;                         // total number of retransmitted packets
    int m_iSentACKTotal;                         // total number of sent ACK packets
    int m_iRecvACKTotal;                         // total number of received ACK packets
    int m_iSentNAKTotal;                         // total number of sent NAK packets
    int m_iRecvNAKTotal;                         // total number of received NAK packets
#ifdef SRT_ENABLE_TLPKTDROP
    int m_iSndDropTotal;
    int m_iRcvDropTotal;
#endif
    uint64_t m_ullBytesSentTotal;                // total number of bytes sent,  including retransmissions
    uint64_t m_ullBytesRecvTotal;                // total number of received bytes
    uint64_t m_ullRcvBytesLossTotal;             // total number of loss bytes (estimate)
    uint64_t m_ullBytesRetransTotal;             // total number of retransmitted bytes
#ifdef SRT_ENABLE_TLPKTDROP
    uint64_t m_ullSndBytesDropTotal;
    uint64_t m_ullRcvBytesDropTotal;
#endif /* SRT_ENABLE_TLPKTDROP */
    int m_iRcvUndecryptTotal;
    uint64_t m_ullRcvBytesUndecryptTotal;
    int64_t m_llSndDurationTotal;		// total real time for sending

    uint64_t m_LastSampleTime;                   // last performance sample time
    int64_t m_llTraceSent;                       // number of packets sent in the last trace interval
    int64_t m_llTraceRecv;                       // number of packets received in the last trace interval
    int m_iTraceSndLoss;                         // number of lost packets in the last trace interval (sender side)
    int m_iTraceRcvLoss;                         // number of lost packets in the last trace interval (receiver side)
    int m_iTraceRetrans;                         // number of retransmitted packets in the last trace interval
    int m_iSentACK;                              // number of ACKs sent in the last trace interval
    int m_iRecvACK;                              // number of ACKs received in the last trace interval
    int m_iSentNAK;                              // number of NAKs sent in the last trace interval
    int m_iRecvNAK;                              // number of NAKs received in the last trace interval
#ifdef SRT_ENABLE_TLPKTDROP
    int m_iTraceSndDrop;
    int m_iTraceRcvDrop;
#endif /* SRT_ENABLE_TLPKTDROP */
    int m_iTraceRcvRetrans;
    int m_iTraceReorderDistance;
    double m_fTraceBelatedTime;
    int64_t m_iTraceRcvBelated;
    uint64_t m_ullTraceBytesSent;                // number of bytes sent in the last trace interval
    uint64_t m_ullTraceBytesRecv;                // number of bytes sent in the last trace interval
    uint64_t m_ullTraceRcvBytesLoss;             // number of bytes bytes lost in the last trace interval (estimate)
    uint64_t m_ullTraceBytesRetrans;             // number of bytes retransmitted in the last trace interval
#ifdef SRT_ENABLE_TLPKTDROP
    uint64_t m_ullTraceSndBytesDrop;
    uint64_t m_ullTraceRcvBytesDrop;
#endif /* SRT_ENABLE_TLPKTDROP */
    int m_iTraceRcvUndecrypt;
    uint64_t m_ullTraceRcvBytesUndecrypt;
    int64_t m_llSndDuration;			// real time for sending
    int64_t m_llSndDurationCounter;		// timers to record the sending duration

public:

    static const int m_iSelfClockInterval = 64;  // ACK interval for self-clocking
    static const int SEND_LITE_ACK = sizeof(int32_t); // special size for ack containing only ack seq
    static const int PACKETPAIR_MASK = 0xF;

    static const int64_t BW_INFINITE =  30000000/8;         //Infinite=> 30Mbps

    static const size_t MAX_SID_LENGTH = 512;

private: // Timers
    uint64_t m_ullCPUFrequency;                  // CPU clock frequency, used for Timer, ticks per microsecond
    uint64_t m_ullNextACKTime;			// Next ACK time, in CPU clock cycles, same below
    uint64_t m_ullNextNAKTime;			// Next NAK time

    volatile uint64_t m_ullSYNInt;		// SYN interval
    volatile uint64_t m_ullACKInt;		// ACK interval
    volatile uint64_t m_ullNAKInt;		// NAK interval
    volatile uint64_t m_ullLastRspTime;		// time stamp of last response from the peer
#ifdef SRT_ENABLE_FASTREXMIT
    volatile uint64_t m_ullLastRspAckTime;   // time stamp of last ACK from the peer
#endif /* SRT_ENABLE_FASTREXMIT */
#ifdef SRT_FIX_KEEPALIVE
    volatile uint64_t m_ullLastSndTime;		// time stamp of last data/ctrl sent
#endif /* SRT_FIX_KEEPALIVE */
    uint64_t m_ullMinNakInt;			// NAK timeout lower bound; too small value can cause unnecessary retransmission
    uint64_t m_ullMinExpInt;			// timeout lower bound threshold: too small timeout can cause problem

    int m_iPktCount;				// packet counter for ACK
    int m_iLightACKCount;			// light ACK counter

    uint64_t m_ullTargetTime;			// scheduled time of next packet sending

    void checkTimers();

private: // for UDP multiplexer
    CSndQueue* m_pSndQueue;			// packet sending queue
    CRcvQueue* m_pRcvQueue;			// packet receiving queue
    sockaddr* m_pPeerAddr;			// peer address
    uint32_t m_piSelfIP[4];			// local UDP IP address
    CSNode* m_pSNode;				// node information for UDT list used in snd queue
    CRNode* m_pRNode;                            // node information for UDT list used in rcv queue

private: // for epoll
    std::set<int> m_sPollID;                     // set of epoll ID to trigger
    void addEPoll(const int eid);
    void removeEPoll(const int eid);
};


#endif
