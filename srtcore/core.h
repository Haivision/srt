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


#ifndef __UDT_CORE_H__
#define __UDT_CORE_H__

#include <deque>
#include <sstream>

#include "srt.h"
#include "common.h"
#include "list.h"
#include "buffer.h"
#include "window.h"
#include "packet.h"
#include "channel.h"
//#include "api.h"
#include "cache.h"
#include "queue.h"
#include "handshake.h"
#include "smoother.h"
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
    ACKD_TOTAL_SIZE_SMALL = 4,

    // Extra fields existing in UDT (not always sent)

    ACKD_RCVSPEED = 4,   // length would be 16
    ACKD_BANDWIDTH = 5,
    ACKD_TOTAL_SIZE_UDTBASE = 6, // length = 24
    // Extra stats for SRT

    ACKD_RCVRATE = 6,
    ACKD_TOTAL_SIZE_VER101 = 7, // length = 28
    ACKD_XMRATE = 7, // XXX This is a weird compat stuff. Version 1.1.3 defines it as ACKD_BANDWIDTH*m_iMaxSRTPayloadSize when set. Never got.
                     // XXX NOTE: field number 7 may be used for something in future, need to confirm destruction of all !compat 1.0.2 version

    ACKD_TOTAL_SIZE_VER102 = 8, // 32
// FEATURE BLOCKED. Probably not to be restored.
//  ACKD_ACKBITMAP = 8,
    ACKD_TOTAL_SIZE = ACKD_TOTAL_SIZE_VER102 // length = 32 (or more)
};
const size_t ACKD_FIELD_SIZE = sizeof(int32_t);

enum GroupDataItem
{
    GRPD_GROUPID,
    GRPD_GROUPTYPE,

    /* That was an early concept, not to be used.
    GRPD_MASTERID,
    GRPD_MASTERTDIFF,
    */
    /// end
    GRPD__SIZE
};

const size_t GRPD_FIELD_SIZE = sizeof(int32_t);

// For HSv4 legacy handshake
#define SRT_MAX_HSRETRY     10          /* Maximum SRT handshake retry */

enum SeqPairItems
{
    SEQ_BEGIN = 0, SEQ_END = 1, SEQ_SIZE = 2
};

// Extended SRT Congestion control class - only an incomplete definition required
class CCryptoControl;
class CUDTUnited;
class CUDTSocket;

class CUDTSocket;

template <class ExecutorType>
void SRT_tsbpdLoop(
        ExecutorType* self,
        SRTSOCKET sid,
        CGuard& lock);

class CUDTGroup
{
    friend class CUDTUnited;

public:
    enum GroupState
    {
        GST_PENDING,  // The socket is created correctly, but not yet ready for getting data.
        GST_IDLE,     // The socket should be activated at the next operation immediately.
        GST_RUNNING,  // The socket was already activated and is in use
        GST_BROKEN    // The last operation broke the socket, it should be closed.
    };

    struct SocketData
    {
        SRTSOCKET id;
        CUDTSocket* ps;
        SRT_SOCKSTATUS laststatus;
        GroupState sndstate;
        GroupState rcvstate;
        sockaddr_any agent;
        sockaddr_any peer;
        bool ready_read;
        bool ready_write;
        bool ready_error;
    };

    // This object will be placed at the position in the
    // buffer assigned to packet's sequence number. When extracting
    // the data to be delivered to the output, this defines, from
    // which socket the data should be read to get the packet of that
    // sequence. The data are read until the packet with this sequence
    // is found. The play time defines when exactly the packet should be
    // given up to the application.
    struct Provider
    {
        uint64_t playtime;
        std::vector<CUDT*> provider; // XXX may be not the most optimal

        Provider(): playtime(0) {}

        struct FUpdate
        {
            uint64_t playtime;
            CUDT* provider;
            FUpdate(CUDT* prov, uint64_t pt): playtime(pt), provider(prov) {}
            void operator()(Provider& accessed, bool isnew)
            {
                if (isnew)
                {
                    accessed.playtime = playtime;
                    accessed.provider.clear();
                }

                accessed.provider.push_back(provider);
            }
        };

        struct FValid
        {
            bool operator()(const Provider& p) { return !p.provider.empty(); }
        };
    };

    // This object keeps the data extracted from the packet and waiting
    // to be delivered to the application when the time comes. The CPacket
    // object is used here because it already provides the automatic allocation
    // facility. It's not the best that can be used here, but there's no better
    // alternative, provided we need to keep the receiver buffer untouched.
    struct Pending
    {
        uint64_t playtime;
        CPacket packet;
        SRT_MSGCTRL msgctrl;
    };

    struct ConfigItem
    {
        SRT_SOCKOPT so;
        std::vector<unsigned char> value;

        template<class T> bool get(T& refr)
        {
            if (sizeof(T) > value.size())
                return false;
            refr = *(T*)&value[0];
        }

        ConfigItem(SRT_SOCKOPT o, const void* val, int size): so(o)
        {
            value.resize(size);
            unsigned char* begin = (unsigned char*)val;
            std::copy(begin, begin+size, value.begin());
        }
    };

    typedef std::list<SocketData> group_t;
    typedef group_t::iterator gli_t;
    CUDTGroup();
    ~CUDTGroup();

    static SocketData prepareData(CUDTSocket* s);

    gli_t add(SocketData data);

    struct HaveID
    {
        SRTSOCKET id;
        HaveID(SRTSOCKET sid): id(sid) {}
        bool operator()(const SocketData& s) { return s.id == id; }
    };

    gli_t find(SRTSOCKET id)
    {
        CGuard g(m_GroupLock);
        gli_t f = std::find_if(m_Group.begin(), m_Group.end(), HaveID(id));
        if (f == m_Group.end())
        {
            return gli_NULL();
        }
        return f;
    }

    // NEED LOCKING
    gli_t begin() { return m_Group.begin(); }
    gli_t end() { return m_Group.end(); }

    // REMEMBER: the group spec should be taken from the socket
    // (set m_IncludedGroup to NULL and m_IncludedIter to grp->gli_NULL())
    // PRIOR TO calling this function.
    bool remove(SRTSOCKET id)
    {
        CGuard g(m_GroupLock);
        gli_t f = std::find_if(m_Group.begin(), m_Group.end(), HaveID(id));
        if (f != m_Group.end())
        {
            m_Group.erase(f);

            // Reset sequence numbers on a dead group so that they are
            // initialized anew with the new alive connection within
            // the group.
            // XXX The problem is that this should be done after the
            // socket is considered DISCONNECTED, not when it's being
            // closed. After being disconnected, the sequence numbers
            // are no longer valid, and will be reinitialized when the
            // socket is connected again. This may stay as is for now
            // as in SRT it's not predicted to do anything with the socket
            // that was disconnected other than immediately closing it.
            if (m_Group.empty())
            {
                m_iLastSchedSeqNo = 0;
                setInitialRxSequence(1);
            }
            return true;
        }

        return false;
    }

    bool empty()
    {
        CGuard g(m_GroupLock);
        return m_Group.empty();
    }

    void resetStateOn(CUDTSocket* sock);

    static gli_t gli_NULL() { return s_NoGroup.end(); }

    int send(const char* buf, int len, ref_t<SRT_MSGCTRL> mc);
    int recv(char* buf, int len, ref_t<SRT_MSGCTRL> mc);

    void close();

    void setOpt(SRT_SOCKOPT optname, const void* optval, int optlen);
    void getOpt(SRT_SOCKOPT optName, void* optval, ref_t<int> optlen);

    SRT_SOCKSTATUS getStatus();

    bool getMasterData(SRTSOCKET slave, ref_t<SRTSOCKET> mpeer, ref_t<uint64_t> start_time);

    bool isGroupReceiver()
    {
        // XXX add here also other group types, which
        // predict group receiving.
        return m_type == SRT_GTYPE_REDUNDANT;
    }

    pthread_mutex_t* exp_groupLock() { return &m_GroupLock; }
    void addEPoll(int eid);
    void removeEPoll(int eid);

    std::vector<bool> providePacket(int32_t exp_sequence, int32_t sequence, CUDT *provider, uint64_t time);


#if ENABLE_HEAVY_LOGGING
    void debugGroup();
#else
    void debugGroup() {}
#endif
private:
    // Check if there's at least one connected socket.
    // If so, grab the status of all member sockets.
    void getGroupCount(ref_t<size_t> r_size, ref_t<bool> r_still_alive);
    void getMemberStatus(ref_t< std::vector<SRT_SOCKGROUPDATA> > r_gd, SRTSOCKET wasread, int result, bool again);
    void readInterceptorThread();
    static void* readInterceptorThread_FWD(void* vself)
    {
        CUDTGroup* self = (CUDTGroup*)vself;
        self->readInterceptorThread();
        return 0;
    }

    class CUDTUnited* m_pGlobal;
    pthread_mutex_t m_GroupLock;

    SRTSOCKET m_GroupID;
    SRTSOCKET m_PeerGroupID;
    std::list<SocketData> m_Group;
    static std::list<SocketData> s_NoGroup; // This is to have a predictable "null iterator".
    bool m_selfManaged;
    SRT_GROUP_TYPE m_type;
    CUDTSocket* m_listener; // A "group" can only have one listener.
    std::set<int> m_sPollID;                     // set of epoll ID to trigger
    int m_iMaxPayloadSize;
    bool m_bSynRecving;
    bool m_bTsbPd;
    bool m_bTLPktDrop;
    int m_iRcvTimeOut;                           // receiving timeout in milliseconds
    pthread_t m_RcvInterceptorThread;

    // This buffer gets updated when a packet with some seq number has come.
    // This gives the TSBPD thread clue that a packet is ready for extraction
    // for given sequence number. This is also a central packet information for
    // other sockets in the group to know whether the packet recovery for jumped-over
    // sequences shall be undertaken or not.
    CircularBuffer<Provider> m_Providers;
    std::deque<Pending> m_Pending;

    // This condition shall be signaled when a packet arrives
    // at the position earlier than the current earliest sequence.
    // Including when the provider buffer is currently empty.
    pthread_cond_t m_RcvPacketAhead;

    // This is the sequence number of the position [0] in m_Providers buffer.
    volatile int32_t m_RcvBaseSeqNo;

    // This is the sequence number that is next available for extraction.
    // May be equal to m_RcvBaseSeqNo if you have a packet at head ready.
    // This sequence should be updated in case when the buffer was empty
    // and when the newly coming packet has a sequence less than this.
    volatile int32_t m_RcvReadySeqNo;

    // Incremented with a new packet arriving
    volatile int32_t m_RcvLatestSeqNo;

    bool m_bOpened;                    // Set to true on a first use
    bool m_bClosing;

    // There's no simple way of transforming config
    // items that are predicted to be used on socket.
    // Use some options for yourself, store the others
    // for setting later on a socket.
    std::vector<ConfigItem> m_config;

    // Signal for the blocking user thread that the packet
    // is ready to deliver.
    pthread_cond_t m_RcvDataCond;
    pthread_mutex_t m_RcvDataLock;
    volatile int32_t m_iLastSchedSeqNo; // represetnts the value of CUDT::m_iSndNextSeqNo for each running socket
public:

    std::string CONID() const
    {
#if ENABLE_LOGGING
        std::ostringstream os;
        os << "%" << m_GroupID << ":";
        return os.str();
#else
        return "";
#endif
    }

    void setInitialRxSequence(int32_t seq)
    {
        m_RcvBaseSeqNo = m_RcvReadySeqNo = m_RcvLatestSeqNo = CSeqNo::decseq(seq);
    }


    // Property accessors
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, SRTSOCKET, id, m_GroupID);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, SRTSOCKET, peerid, m_PeerGroupID);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, bool, managed, m_selfManaged);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, SRT_GROUP_TYPE, type, m_type);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, int32_t, currentSchedSequence, m_iLastSchedSeqNo);
    SRTU_PROPERTY_RW_CHAIN(CUDTGroup, std::set<int>&, epollset, m_sPollID);

    // Required for SRT_tsbpdLoop
    SRTU_PROPERTY_RO(bool, closing, m_bClosing);
    SRTU_PROPERTY_RO(bool, isTLPktDrop, m_bTLPktDrop);
    SRTU_PROPERTY_RO(bool, isSynReceiving, m_bSynRecving);
    SRTU_PROPERTY_RO(CUDTUnited*, uglobal, m_pGlobal);
    SRTU_PROPERTY_RO(std::set<int>&, pollset, m_sPollID);
};

// XXX REFACTOR: The 'CUDT' class is to be merged with 'CUDTSocket'.
// There's no reason for separating them, there's no case of having them
// anyhow managed separately. After this is done, with a small help with
// separating the internal abnormal path management (exceptions) from the
// API (return values), through CUDTUnited, this class may become in future
// an officially exposed C++ API.
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
    friend class CUDTGroup;

private: // constructor and desctructor

    void construct();
    void clearData();
    CUDT(CUDTSocket* parent);
    CUDT(CUDTSocket* parent, const CUDT& ancestor);
    const CUDT& operator=(const CUDT&) {return *this;} // = delete ?
    ~CUDT();

public: //API
    static int startup();
    static int cleanup();
    static SRTSOCKET socket();
    static SRTSOCKET createGroup(SRT_GROUP_TYPE);
    static int addSocketToGroup(SRTSOCKET socket, SRTSOCKET group);
    static int removeSocketFromGroup(SRTSOCKET socket);
    static SRTSOCKET getGroupOfSocket(SRTSOCKET socket);
    static bool isgroup(SRTSOCKET sock) { return (sock & SRTGROUP_MASK) != 0; }
    static int bind(SRTSOCKET u, const sockaddr* name, int namelen);
    static int bind(SRTSOCKET u, int udpsock);
    static int listen(SRTSOCKET u, int backlog);
    static SRTSOCKET accept(SRTSOCKET u, sockaddr* addr, int* addrlen);
    static int connect(SRTSOCKET u, const sockaddr* name, int namelen, int32_t forced_isn);
    static int connect(SRTSOCKET u, const sockaddr* name, int namelen, const sockaddr* tname, int tnamelen);
    static int close(SRTSOCKET u);
    static int getpeername(SRTSOCKET u, sockaddr* name, int* namelen);
    static int getsockname(SRTSOCKET u, sockaddr* name, int* namelen);
    static int getsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, void* optval, int* optlen);
    static int setsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, const void* optval, int optlen);
    static int send(SRTSOCKET u, const char* buf, int len, int flags);
    static int recv(SRTSOCKET u, char* buf, int len, int flags);
    static int sendmsg(SRTSOCKET u, const char* buf, int len, int ttl = -1, bool inorder = false, uint64_t srctime = 0);
    static int recvmsg(SRTSOCKET u, char* buf, int len, uint64_t& srctime);
    static int sendmsg2(SRTSOCKET u, const char* buf, int len, ref_t<SRT_MSGCTRL> mctrl);
    static int recvmsg2(SRTSOCKET u, char* buf, int len, ref_t<SRT_MSGCTRL> mctrl);
    static int64_t sendfile(SRTSOCKET u, std::fstream& ifs, int64_t& offset, int64_t size, int block = SRT_DEFAULT_SENDFILE_BLOCK);
    static int64_t recvfile(SRTSOCKET u, std::fstream& ofs, int64_t& offset, int64_t size, int block = SRT_DEFAULT_RECVFILE_BLOCK);
    static int select(int nfds, ud_set* readfds, ud_set* writefds, ud_set* exceptfds, const timeval* timeout);
    static int selectEx(const std::vector<SRTSOCKET>& fds, std::vector<SRTSOCKET>* readfds, std::vector<SRTSOCKET>* writefds, std::vector<SRTSOCKET>* exceptfds, int64_t msTimeOut);
    static int epoll_create();
    static int epoll_add_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
    static int epoll_add_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    static int epoll_remove_usock(const int eid, const SRTSOCKET u);
    static int epoll_remove_ssock(const int eid, const SYSSOCKET s);
    static int epoll_update_usock(const int eid, const SRTSOCKET u, const int* events = NULL);
    static int epoll_update_ssock(const int eid, const SYSSOCKET s, const int* events = NULL);
    static int epoll_wait(const int eid, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, int64_t msTimeOut, std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
    static int epoll_release(const int eid);
    static CUDTException& getlasterror();
    static int perfmon(SRTSOCKET u, CPerfMon* perf, bool clear = true);
    static int bstats(SRTSOCKET u, CBytePerfMon* perf, bool clear = true, bool instantaneous = false);
    static SRT_SOCKSTATUS getsockstate(SRTSOCKET u);
    static bool setstreamid(SRTSOCKET u, const std::string& sid);
    static std::string getstreamid(SRTSOCKET u);
    static int getsndbuffer(SRTSOCKET u, size_t* blocks, size_t* bytes);
    static int setError(const CUDTException& e);
    static int setError(CodeMajor mj, CodeMinor mn, int syserr);


public: // internal API
    static const SRTSOCKET INVALID_SOCK = -1;         // invalid socket descriptor
    static const int ERROR = -1;                      // socket api error returned value

    static const int HS_VERSION_UDT4 = 4;
    static const int HS_VERSION_SRT1 = 5;

    // Parameters
    //
    // Note: use notation with X*1000*1000* ... instead of million zeros in a row.
    // In C++17 there is a possible notation of 5'000'000 for convenience, but that's
    // something only for a far future.
    static const int COMM_RESPONSE_TIMEOUT_US = 5*1000*1000; // 5 seconds
    static const int COMM_RESPONSE_MAX_EXP = 16;
    static const int SRT_TLPKTDROP_MINTHRESHOLD_MS = 1000;
    static const uint64_t COMM_KEEPALIVE_PERIOD_US = 1*1000*1000;
    static const int32_t COMM_SYN_INTERVAL_US = 10*1000;

    // Input rate constants
    static const uint64_t
        SND_INPUTRATE_FAST_START_US = 500*1000,
        SND_INPUTRATE_RUNNING_US = 1*1000*1000;
    static const int64_t SND_INPUTRATE_MAX_PACKETS = 2000;
    static const int SND_INPUTRATE_INITIAL_BPS = 10000000/8;  // 10 Mbps (1.25 MBps)

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

    SRTSOCKET socketID() { return m_SocketID; }

    static CUDT* getUDTHandle(SRTSOCKET u);
    static std::vector<SRTSOCKET> existingSockets();

    void addressAndSend(CPacket& pkt);
    void sendSrtMsg(int cmd, uint32_t *srtdata_in = NULL, int srtlen_in = 0);

    bool isOPT_TsbPd() { return m_bOPT_TsbPd; }
    int RTT() { return m_iRTT; }
    int32_t sndSeqNo() { return m_iSndCurrSeqNo; }
    int32_t schedSeqNo() { return m_iSndNextSeqNo; }
    bool overrideSndSeqNo(int32_t seq);

    int32_t rcvSeqNo() { return m_iRcvCurrSeqNo; }
    int flowWindowSize() { return m_iFlowWindowSize; }
    int32_t deliveryRate() { return m_iDeliveryRate; }
    int bandwidth() { return m_iBandwidth; }
    int64_t maxBandwidth() { return m_llMaxBW; }
    int MSS() { return m_iMSS; }

    uint32_t latency_us() {return m_iTsbPdDelay_ms*1000; }

    size_t maxPayloadSize() { return m_iMaxSRTPayloadSize; }
    size_t OPT_PayloadSize() { return m_zOPT_ExpPayloadSize; }
    uint64_t minNAKInterval() { return m_ullMinNakInt_tk; }
    int32_t ISN() { return m_iISN; }
    int32_t peerISN() { return m_iPeerISN; }
    sockaddr_any peerAddr() { return m_PeerAddr; }

    int minSndSize(int len = 0)
    {
        if (len == 0) // wierd, can't use non-static data member as default argument!
            len = m_iMaxSRTPayloadSize;
        return m_bMessageAPI ? (len+m_iMaxSRTPayloadSize-1)/m_iMaxSRTPayloadSize : 1;
    }

    int makeTS(uint64_t from_time)
    {
        // NOTE:
        // - This calculates first the time difference towards start time.
        // - This difference value is also CUT OFF THE SEGMENT information
        //   (a multiple of MAX_TIMESTAMP+1)
        // So, this can be simply defined as: TS = (RTS - STS) % (MAX_TIMESTAMP+1)
        // XXX Would be nice to check if local_time > m_StartTime,
        // otherwise it may go unnoticed with clock skew.
        return int(from_time - m_StartTime);
    }

    void setPacketTS(CPacket& p, uint64_t local_time)
    {
        p.m_iTimeStamp = makeTS(local_time);
    }

    // Utility used for closing a listening socket
    // immediately to free the socket
    void notListening()
    {
        CGuard cg(m_ConnectionLock);
        m_bListening = false;
        m_pRcvQueue->removeListener(this);
    }

    // XXX See CUDT::tsbpd() to see how to implement it. This should
    // do the same as TLPKTDROP feature when skipping packets that are agreed
    // to be lost. Note that this is predicted to be called with TSBPD off.
    // This is to be exposed for the application so that it can require this
    // sequence to be skipped, if that packet has been otherwise arrived through
    // a different channel.
    void skipIncoming(int32_t seq);

    // For SRT_tsbpdLoop
    CUDTUnited* uglobal() { return &s_UDTUnited; } // needed by tsbpdLoop
    std::set<int>& pollset() { return m_sPollID; }

    SRTU_PROPERTY_RO(bool, closing, m_bClosing);
    SRTU_PROPERTY_RO(CRcvBuffer*, rcvBuffer, m_pRcvBuffer);
    SRTU_PROPERTY_RO(bool, isTLPktDrop, m_bTLPktDrop);
    SRTU_PROPERTY_RO(bool, isSynReceiving, m_bSynRecving);
    SRTU_PROPERTY_RO(pthread_cond_t*, recvDataCond, &m_RecvDataCond);
    SRTU_PROPERTY_RO(pthread_cond_t*, recvTsbPdCond, &m_RcvTsbPdCond);

    void ConnectSignal(ETransmissionEvent tev, EventSlot sl);
    void DisconnectSignal(ETransmissionEvent tev);

    // This is in public section so prospective overriding it can be
    // done by directly assigning to a field.

    Callback<std::vector<int32_t>, CPacket> m_cbPacketArrival;

private:
    /// initialize a UDT entity and bind to a local address.

    void open();

    /// Start listening to any connection request.

    void setListenState();

    /// Connect to a UDT entity listening at address "peer".
    /// @param peer [in] The address of the listening UDT entity.

    void startConnect(const sockaddr_any& peer, int32_t forced_isn);

    /// Process the response handshake packet. Failure reasons can be:
    /// * Socket is not in connecting state
    /// * Response @a pkt is not a handshake control message
    /// * Rendezvous socket has once processed a regular handshake
    /// @param pkt [in] handshake packet.
    /// @retval 0 Connection successful
    /// @retval 1 Connection in progress (m_ConnReq turned into RESPONSE)
    /// @retval -1 Connection failed

    SRT_ATR_NODISCARD EConnectStatus processConnectResponse(const CPacket& pkt, CUDTException* eout, EConnectMethod synchro) ATR_NOEXCEPT;

    // This function works in case of HSv5 rendezvous. It changes the state
    // according to the present state and received message type, as well as the
    // INITIATOR/RESPONDER side resolved through cookieContest().
    // The resulting data are:
    // - rsptype: handshake message type that should be sent back to the peer (nothing if URQ_DONE)
    // - needs_extension: the HSREQ/KMREQ or HSRSP/KMRSP extensions should be attached to the handshake message.
    // - RETURNED VALUE: if true, it means a URQ_CONCLUSION message was received with HSRSP/KMRSP extensions and needs HSRSP/KMRSP.
    void rendezvousSwitchState(ref_t<UDTRequestType> rsptype, ref_t<bool> needs_extension, ref_t<bool> needs_hsrsp);
    void cookieContest();

    /// Interpret the incoming handshake packet in order to perform appropriate
    /// rendezvous FSM state transition if needed, and craft the response, serialized
    /// into the packet to be next sent.
    /// @param reqpkt Packet to be written with handshake data
    /// @param response incoming handshake response packet to be interpreted
    /// @param serv_addr incoming packet's address
    /// @param synchro True when this function was called in blocking mode
    /// @param rst Current read status to know if the HS packet was freshly received from the peer, or this is only a periodic update (RST_AGAIN)
    SRT_ATR_NODISCARD EConnectStatus processRendezvous(ref_t<CPacket> reqpkt, const CPacket &response, const sockaddr_any& serv_addr, bool synchro, EReadStatus);
    SRT_ATR_NODISCARD bool prepareConnectionObjects(const CHandShake &hs, HandshakeSide hsd, CUDTException *eout);
    SRT_ATR_NODISCARD EConnectStatus postConnect(const CPacket& response, bool rendezvous, CUDTException* eout, bool synchro);
    void applyResponseSettings(const CPacket& hspkt);
    SRT_ATR_NODISCARD EConnectStatus processAsyncConnectResponse(const CPacket& pkt) ATR_NOEXCEPT;
    SRT_ATR_NODISCARD bool processAsyncConnectRequest(EReadStatus rst, EConnectStatus cst, const CPacket& response, const sockaddr_any& serv_addr);

    void checkUpdateCryptoKeyLen(const char* loghdr, int32_t typefield);

    SRT_ATR_NODISCARD size_t fillSrtHandshake_HSREQ(uint32_t* srtdata, size_t srtlen, int hs_version);
    SRT_ATR_NODISCARD size_t fillSrtHandshake_HSRSP(uint32_t* srtdata, size_t srtlen, int hs_version);
    SRT_ATR_NODISCARD size_t fillSrtHandshake(uint32_t* srtdata, size_t srtlen, int msgtype, int hs_version);

    SRT_ATR_NODISCARD bool createSrtHandshake(ref_t<CPacket> reqpkt, ref_t<CHandShake> hs,
            int srths_cmd, int srtkm_cmd, const uint32_t* data, size_t datalen);

    SRT_ATR_NODISCARD size_t prepareSrtHsMsg(int cmd, uint32_t* srtdata, size_t size);

    SRT_ATR_NODISCARD bool processSrtMsg(const CPacket *ctrlpkt);
    SRT_ATR_NODISCARD int processSrtMsg_HSREQ(const uint32_t* srtdata, size_t len, uint32_t ts, int hsv);
    SRT_ATR_NODISCARD int processSrtMsg_HSRSP(const uint32_t* srtdata, size_t len, uint32_t ts, int hsv);
    SRT_ATR_NODISCARD bool interpretSrtHandshake(const CHandShake& hs, const CPacket& hspkt, uint32_t* out_data, size_t* out_len);

    static CUDTGroup& newGroup(int); // defined EXCEPTIONALLY in api.cpp for convenience reasons
    // Note: This is an "interpret" function, which should treat the tp as
    // "possibly group type" that might be out of the existing values.
    SRT_ATR_NODISCARD bool interpretGroup(const int32_t grpdata[], int hsreq_type_cmd);
    SRT_ATR_NODISCARD SRTSOCKET makeMePeerOf(SRTSOCKET peergroup, SRT_GROUP_TYPE tp);
    void synchronizeGroupTime(CUDTGroup* grp);

    void updateAfterSrtHandshake(int hsv);

    void updateSrtRcvSettings();
    void updateSrtSndSettings();

    void checkNeedDrop(ref_t<bool> bCongestion);

    /// Connect to a UDT entity listening at address "peer", which has sent "hs" request.
    /// @param peer [in] The address of the listening UDT entity.
    /// @param hs [in/out] The handshake information sent by the peer side (in), negotiated value (out).

    void acceptAndRespond(const sockaddr_any& peer, CHandShake* hs, const CPacket& hspkt);

    /// Close the opened UDT entity.

    void close();

    /// Request UDT to send out a data block "data" with size of "len".
    /// @param data [in] The address of the application data to be sent.
    /// @param len [in] The size of the data block.
    /// @return Actual size of data sent.

    SRT_ATR_NODISCARD int send(const char* data, int len)
    {
        return sendmsg(data, len, -1, false, 0);
    }

    /// Request UDT to receive data to a memory block "data" with size of "len".
    /// @param data [out] data received.
    /// @param len [in] The desired size of data to be received.
    /// @return Actual size of data received.

    SRT_ATR_NODISCARD int recv(char* data, int len);

    /// send a message of a memory block "data" with size of "len".
    /// @param data [out] data received.
    /// @param len [in] The desired size of data to be received.
    /// @param ttl [in] the time-to-live of the message.
    /// @param inorder [in] if the message should be delivered in order.
    /// @param srctime [in] Time when the data were ready to send.
    /// @return Actual size of data sent.

    SRT_ATR_NODISCARD int sendmsg(const char* data, int len, int ttl, bool inorder, uint64_t srctime);
    /// Receive a message to buffer "data".
    /// @param data [out] data received.
    /// @param len [in] size of the buffer.
    /// @return Actual size of data received.

    SRT_ATR_NODISCARD int sendmsg2(const char* data, int len, ref_t<SRT_MSGCTRL> m);

    SRT_ATR_NODISCARD int recvmsg(char* data, int len, uint64_t& srctime);
    SRT_ATR_NODISCARD int recvmsg2(char* data, int len, ref_t<SRT_MSGCTRL> m);
    SRT_ATR_NODISCARD int receiveMessage(char* data, int len, ref_t<SRT_MSGCTRL> m, int32_t uptoseq = CSeqNo::m_iMaxSeqNo);
    SRT_ATR_NODISCARD int receiveBuffer(char* data, int len);

    size_t dropMessage(int32_t seqtoskip);

    /// Request UDT to send out a file described as "fd", starting from "offset", with size of "size".
    /// @param ifs [in] The input file stream.
    /// @param offset [in, out] From where to read and send data; output is the new offset when the call returns.
    /// @param size [in] How many data to be sent.
    /// @param block [in] size of block per read from disk
    /// @return Actual size of data sent.

    SRT_ATR_NODISCARD int64_t sendfile(std::fstream& ifs, int64_t& offset, int64_t size, int block = 366000);

    /// Request UDT to receive data into a file described as "fd", starting from "offset", with expected size of "size".
    /// @param ofs [out] The output file stream.
    /// @param offset [in, out] From where to write data; output is the new offset when the call returns.
    /// @param size [in] How many data to be received.
    /// @param block [in] size of block per write to disk
    /// @return Actual size of data received.

    SRT_ATR_NODISCARD int64_t recvfile(std::fstream& ofs, int64_t& offset, int64_t size, int block = 7320000);

    /// Configure UDT options.
    /// @param optName [in] The enum name of a UDT option.
    /// @param optval [in] The value to be set.
    /// @param optlen [in] size of "optval".

    void setOpt(SRT_SOCKOPT optName, const void* optval, int optlen);

    /// Read UDT options.
    /// @param optName [in] The enum name of a UDT option.
    /// @param optval [in] The value to be returned.
    /// @param optlen [out] size of "optval".

    void getOpt(SRT_SOCKOPT optName, void* optval, ref_t<int> optlen);

    /// read the performance data since last sample() call.
    /// @param perf [in, out] pointer to a CPerfMon structure to record the performance data.
    /// @param clear [in] flag to decide if the local performance trace should be cleared.

    void sample(CPerfMon* perf, bool clear = true);

    /// read the performance data with bytes counters since bstats() 
    ///  
    /// @param perf [in, out] pointer to a CPerfMon structure to record the performance data.
    /// @param clear [in] flag to decide if the local performance trace should be cleared. 
    /// @param instantaneous [in] flag to request instantaneous data 
    /// instead of moving averages. 
    void bstats(CBytePerfMon* perf, bool clear = true, bool instantaneous = false);

    /// Mark sequence contained in the given packet as not lost. This
    /// removes the loss record from both current receiver loss list and
    /// the receiver fresh loss list.
    void unlose(const CPacket& oldpacket);
    void unlose(int32_t from, int32_t to);

    void considerLegacySrtHandshake(uint64_t timebase);
    void checkSndTimers(Whether2RegenKm regen = DONT_REGEN_KM);
    void handshakeDone()
    {
        m_iSndHsRetryCnt = 0;
    }

    int64_t withOverhead(int64_t basebw)
    {
        return (basebw * (100 + m_iOverheadBW))/100;
    }

    static double Bps2Mbps(int64_t basebw)
    {
        return double(basebw) * 8.0/1000000.0;
    }

    bool stillConnected()
    {
        // Still connected is when:
        // - no "broken" condition appeared (security, protocol error, response timeout)
        return !m_bBroken
            // - still connected (no one called srt_close())
            && m_bConnected
            // - isn't currently closing (srt_close() called, response timeout, shutdown)
            && !m_bClosing;
    }

    int sndSpaceLeft()
    {
        return sndBuffersLeft() * m_iMaxSRTPayloadSize;
    }

    int sndBuffersLeft()
    {
        return m_iSndBufSize - m_pSndBuffer->getCurrBufSize();
    }

    uint64_t socketStartTime()
    {
        return m_StartTime;
    }

    // TSBPD thread main function.
    static void* tsbpd(void* param);

    void updateForgotten(int seqlen, int32_t lastack, int32_t skiptoseqno);

    static std::vector<int32_t> defaultPacketArrival(void* vself, CPacket& pkt);
    static std::vector<int32_t> groupPacketArrival(void* vself, CPacket& pkt);

    static CUDTUnited s_UDTUnited;               // UDT global management base

private: // Identification
    CUDTSocket* const m_parent; // temporary, until the CUDTSocket class is merged with CUDT
    SRTSOCKET m_SocketID;                        // UDT socket number
    SRTSOCKET m_PeerID;                          // peer id, for multiplexer

    int m_iMaxSRTPayloadSize;                 // Maximum/regular payload size, in bytes
    size_t m_zOPT_ExpPayloadSize;                    // Expected average payload size (user option)

    // Options
    int m_iMSS;                                  // Maximum Segment Size, in bytes
    bool m_bSynSending;                          // Sending syncronization mode
    bool m_bSynRecving;                          // Receiving syncronization mode
    int m_iFlightFlagSize;                       // Maximum number of packets in flight from the peer side
    int m_iSndBufSize;                           // Maximum UDT sender buffer size
    int m_iRcvBufSize;                           // Maximum UDT receiver buffer size
    linger m_Linger;                             // Linger information on close
    int m_iUDPSndBufSize;                        // UDP sending buffer size
    int m_iUDPRcvBufSize;                        // UDP receiving buffer size
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

    // XXX Consider removing them. The m_bDataSender may stay here
    // in order to maintain the HS side selection in HSv4.
    // m_bTwoWayData is unused.
    bool m_bDataSender;
    bool m_bTwoWayData;

    // HSv4 (legacy handshake) support)
    uint64_t m_ullSndHsLastTime_us;	    //Last SRT handshake request time
    int      m_iSndHsRetryCnt;       //SRT handshake retries left

    bool m_bMessageAPI;
    bool m_bOPT_TsbPd;               // Whether AGENT will do TSBPD Rx (whether peer does, is not agent's problem)
    int m_iOPT_TsbPdDelay;           // Agent's Rx latency
    int m_iOPT_PeerTsbPdDelay;       // Peer's Rx latency for the traffic made by Agent's Tx.
    bool m_bOPT_TLPktDrop;           // Whether Agent WILL DO TLPKTDROP on Rx.
    int m_iOPT_SndDropDelay;         // Extra delay when deciding to snd-drop for TLPKTDROP, -1 to off
    bool m_bOPT_GroupConnect;
    std::string m_sStreamName;

    int m_iTsbPdDelay_ms;                           // Rx delay to absorb burst in milliseconds
    int m_iPeerTsbPdDelay_ms;                       // Tx delay that the peer uses to absorb burst in milliseconds
    bool m_bTLPktDrop;                           // Enable Too-late Packet Drop
    int64_t m_llInputBW;                         // Input stream rate (bytes/sec)
    int m_iOverheadBW;                           // Percent above input stream rate (applies if m_llMaxBW == 0)
    bool m_bRcvNakReport;                        // Enable Receiver Periodic NAK Reports
private:
    UniquePtr<CCryptoControl> m_pCryptoControl;                            // congestion control SRT class (small data extension)
    CCache<CInfoBlock>* m_pCache;                // network information cache

    // Congestion control
    std::vector<EventSlot> m_Slots[TEV__SIZE];
    Smoother m_Smoother;

    // Attached tool function
    void EmitSignal(ETransmissionEvent tev, EventVariant var);

    // Internal state
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
    int m_iByteDeliveryRate;                     // Byte arrival rate at the receiver side

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

    volatile uint64_t m_ullInterval_tk;             // Inter-packet time, in CPU clock cycles
    uint64_t m_ullTimeDiff_tk;                      // aggregate difference in inter-packet time

    volatile int m_iFlowWindowSize;              // Flow control window size
    volatile double m_dCongestionWindow;         // congestion window size

    volatile int32_t m_iSndLastFullAck;          // Last full ACK received
    volatile int32_t m_iSndLastAck;              // Last ACK received

    // NOTE: m_iSndLastDataAck is the value strictly bound to the CSndBufer object (m_pSndBuffer)
    // and this is the sequence number that refers to the block at position [0]. Upon acknowledgement,
    // this value is shifted to the acknowledged position, and the blocks are removed from the
    // m_pSndBuffer buffer up to excluding this sequence number.
    // XXX CONSIDER removing this field and give up the maintenance of this sequence number
    // to the sending buffer. This way, extraction of an old packet for retransmission should
    // require only the lost sequence number, and how to find the packet with this sequence
    // will be up to the sending buffer.
    volatile int32_t m_iSndLastDataAck;          // The real last ACK that updates the sender buffer and loss list
    volatile int32_t m_iSndCurrSeqNo;            // The largest sequence number that HAS BEEN SENT
    volatile int32_t m_iSndNextSeqNo;            // The sequence number predicted to be placed at the currently scheduled packet

    // Note important differences between Curr and Next fields:
    // - m_iSndCurrSeqNo: this is used by SRT:SndQ:worker thread and it's operated from CUDT::packData
    //   function only. This value represents the sequence number that has been stamped on a packet directly
    //   before it is sent over the network.
    // - m_iSndNextSeqNo: this is used by the user's thread and it's operated from CUDT::sendmsg2
    //   function only. This value represents the sequence number that is PREDICTED to be stamped on the
    //   first block out of the block series that will be scheduled for later sending over the network
    //   out of the data passed in this function. For a special case when the length of the data is
    //   short enough to be passed in one UDP packet (always the case for live mode), this value is
    //   always increased by one in this call, otherwise it will be increased by the number of blocks
    //   scheduled for sending.

    //int32_t m_iLastDecSeq;                       // Sequence number sent last decrease occurs (actually part of FileSmoother, formerly CUDTCC)
    int32_t m_iSndLastAck2;                      // Last ACK2 sent back

    void setInitialSndSeq(int32_t isn, bool initial = true)
    {
        // m_iLastDecSeq = isn - 1; <-- purpose unknown; duplicate from FileSmoother?
        m_iSndLastAck = isn;
        m_iSndLastDataAck = isn;
        m_iSndLastFullAck = isn;
        m_iSndCurrSeqNo = isn - 1;

        // This should NOT be done at the "in the flight" situation
        // because after the initial stage there are more threads using
        // these fields, and this field has a different affinity than
        // the others, and is practically a source of this value, just
        // pushed through a queue barrier.
        if (initial)
            m_iSndNextSeqNo = isn;
        m_iSndLastAck2 = isn;
    }

    void setInitialRcvSeq(int32_t isn)
    {
        m_iRcvLastAck = isn;
#ifdef ENABLE_LOGGING
        m_iDebugPrevLastAck = m_iRcvLastAck;
#endif
        m_iRcvLastSkipAck = m_iRcvLastAck;
        m_iRcvLastAckAck = isn;
        m_iRcvCurrSeqNo = isn - 1;
    }

    uint64_t m_ullSndLastAck2Time;               // The time when last ACK2 was sent back
    int32_t m_iISN;                              // Initial Sequence Number
    bool m_bPeerTsbPd;                            // Peer accept TimeStamp-Based Rx mode
    bool m_bPeerTLPktDrop;                        // Enable sender late packet dropping
    bool m_bPeerNakReport;                    // Sender's peer (receiver) issues Periodic NAK Reports
    bool m_bPeerRexmitFlag;                   // Receiver supports rexmit flag in payload packets
    int32_t m_iReXmitCount;                      // Re-Transmit Count since last ACK

private: // Receiving related data
    CRcvBuffer* m_pRcvBuffer;               //< Receiver buffer
    CRcvLossList* m_pRcvLossList;           //< Receiver loss list
    std::deque<CRcvFreshLoss> m_FreshLoss;  //< Lost sequence already added to m_pRcvLossList, but not yet sent UMSG_LOSSREPORT for.
    int m_iReorderTolerance;                //< Current value of dynamic reorder tolerance
    int m_iMaxReorderTolerance;             //< Maximum allowed value for dynamic reorder tolerance
    int m_iConsecEarlyDelivery;             //< Increases with every OOO packet that came <TTL-2 time, resets with every increased reorder tolerance
    int m_iConsecOrderedDelivery;           //< Increases with every packet coming in order or retransmitted, resets with every out-of-order packet

    CACKWindow<1024> m_ACKWindow;             //< ACK history window
    CPktTimeWindow<16, 64> m_RcvTimeWindow;   //< Packet arrival time window

    int32_t m_iRcvLastAck;                       //< Last sent ACK
#ifdef ENABLE_LOGGING
    int32_t m_iDebugPrevLastAck;
#endif
    int32_t m_iRcvLastSkipAck;                   // Last dropped sequence ACK
    uint64_t m_ullLastAckTime_tk;                   // Timestamp of last ACK
    int32_t m_iRcvLastAckAck;                    // Last sent ACK that has been acknowledged
    int32_t m_iAckSeqNo;                         // Last ACK sequence number
    int32_t m_iRcvCurrSeqNo;                     // Largest received sequence number

    uint64_t m_ullLastWarningTime;               // Last time that a warning message is sent

    int32_t m_iPeerISN;                          // Initial Sequence Number of the peer side
    uint64_t m_ullRcvPeerStartTime;

    uint32_t m_lSrtVersion;
    uint32_t m_lMinimumPeerSrtVersion;
    uint32_t m_lPeerSrtVersion;

    bool m_bTsbPd;                               // Peer sends TimeStamp-Based Packet Delivery Packets 
    bool m_bGroupTsbPd;                          // TSBPD should be used for GROUP RECEIVER instead.

    pthread_t m_RcvTsbPdThread;                  // Rcv TsbPD Thread handle
    pthread_cond_t m_RcvTsbPdCond;
    bool m_bTsbPdAckWakeup;                      // Signal TsbPd thread on Ack sent

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

    void initSynch();
    void destroySynch();
    void releaseSynch();

private: // Common connection Congestion Control setup

    // XXX This can fail only when it failed to create a smoother
    // which only may happen when the smoother list is extended 
    // with user-supplied smoothers, not a case so far.
    // SRT_ATR_NODISCARD
    bool setupCC();
    // for updateCC it's ok to discard the value. This returns false only if
    // the Smoother isn't created, and this can be prevented from.
    bool updateCC(ETransmissionEvent, EventVariant arg);

    // XXX Unsure as to this return value is meaningful.
    // May happen that this failure is acceptable slongs
    // the other party will be sending unencrypted stream.
    // SRT_ATR_NODISCARD
    bool createCrypter(HandshakeSide side, bool bidi);

private: // Generation and processing of packets
    void sendCtrl(UDTMessageType pkttype, void* lparam = NULL, void* rparam = NULL, int size = 0);
    void processCtrl(CPacket& ctrlpkt);
    int packData(ref_t<CPacket> packet, ref_t<uint64_t> ts, ref_t<sockaddr_any> src_adr);
    int processData(CUnit* unit);
    void processClose();
    int processConnectRequest(const sockaddr_any& addr, CPacket& packet);
    static void addLossRecord(std::vector<int32_t>& lossrecord, int32_t lo, int32_t hi);
    int32_t bake(const sockaddr_any& addr, int32_t previous_cookie = 0, int correction = 0);

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
    int m_iSndDropTotal;
    int m_iRcvDropTotal;
    uint64_t m_ullBytesSentTotal;                // total number of bytes sent,  including retransmissions
    uint64_t m_ullBytesRecvTotal;                // total number of received bytes
    uint64_t m_ullRcvBytesLossTotal;             // total number of loss bytes (estimate)
    uint64_t m_ullBytesRetransTotal;             // total number of retransmitted bytes
    uint64_t m_ullSndBytesDropTotal;
    uint64_t m_ullRcvBytesDropTotal;
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
    int m_iTraceSndDrop;
    int m_iTraceRcvDrop;
    int m_iTraceRcvRetrans;
    int m_iTraceReorderDistance;
    double m_fTraceBelatedTime;
    int64_t m_iTraceRcvBelated;
    uint64_t m_ullTraceBytesSent;                // number of bytes sent in the last trace interval
    uint64_t m_ullTraceBytesRecv;                // number of bytes sent in the last trace interval
    uint64_t m_ullTraceRcvBytesLoss;             // number of bytes bytes lost in the last trace interval (estimate)
    uint64_t m_ullTraceBytesRetrans;             // number of bytes retransmitted in the last trace interval
    uint64_t m_ullTraceSndBytesDrop;
    uint64_t m_ullTraceRcvBytesDrop;
    int m_iTraceRcvUndecrypt;
    uint64_t m_ullTraceRcvBytesUndecrypt;
    int64_t m_llSndDuration;			// real time for sending
    int64_t m_llSndDurationCounter;		// timers to record the sending duration

public:

    static const int SELF_CLOCK_INTERVAL = 64;  // ACK interval for self-clocking
    static const int SEND_LITE_ACK = sizeof(int32_t); // special size for ack containing only ack seq
    static const int PACKETPAIR_MASK = 0xF;

    static const size_t MAX_SID_LENGTH = 512;

private: // Timers
    uint64_t m_ullCPUFrequency;               // CPU clock frequency, used for Timer, ticks per microsecond
    uint64_t m_ullNextACKTime_tk;			  // Next ACK time, in CPU clock cycles, same below
    uint64_t m_ullNextNAKTime_tk;			  // Next NAK time

    volatile uint64_t m_ullSYNInt_tk;		  // SYN interval
    volatile uint64_t m_ullACKInt_tk;         // ACK interval
    volatile uint64_t m_ullNAKInt_tk;         // NAK interval
    volatile uint64_t m_ullLastRspTime_tk;    // time stamp of last response from the peer
    volatile uint64_t m_ullLastRspAckTime_tk; // time stamp of last ACK from the peer
    volatile uint64_t m_ullLastSndTime_tk;    // time stamp of last data/ctrl sent (in system ticks)
    uint64_t m_ullMinNakInt_tk;               // NAK timeout lower bound; too small value can cause unnecessary retransmission
    uint64_t m_ullMinExpInt_tk;               // timeout lower bound threshold: too small timeout can cause problem

    int m_iPktCount;				// packet counter for ACK
    int m_iLightACKCount;			// light ACK counter

    uint64_t m_ullTargetTime_tk;			// scheduled time of next packet sending

    void checkTimers();

public: // For the use of CCryptoControl
    // HaiCrypt configuration
    unsigned int m_uKmRefreshRatePkt;
    unsigned int m_uKmPreAnnouncePkt;


private: // for UDP multiplexer
    CSndQueue* m_pSndQueue;			// packet sending queue
    CRcvQueue* m_pRcvQueue;			// packet receiving queue
    sockaddr_any m_PeerAddr;		// peer address
    sockaddr_any m_SourceAddr;      // override UDP source address with this one when sending
    uint32_t m_piSelfIP[4];			// local UDP IP address
    CSNode* m_pSNode;				// node information for UDT list used in snd queue
    CRNode* m_pRNode;               // node information for UDT list used in rcv queue

public: // For smoother
    const CSndQueue* sndQueue() { return m_pSndQueue; }
    const CRcvQueue* rcvQueue() { return m_pRcvQueue; }

private: // for epoll
    std::set<int> m_sPollID;                     // set of epoll ID to trigger
    void addEPoll(const int eid);
    void removeEPoll(const int eid);
};


#endif
