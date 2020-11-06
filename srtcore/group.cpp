#include <iterator>

#include "api.h"
#include "group.h"

using namespace std;
using namespace srt::sync;
using namespace srt_logging;

// The SRT_DEF_VERSION is defined in core.cpp.
extern const int32_t SRT_DEF_VERSION;

int32_t CUDTGroup::s_tokenGen = 0;

// [[using locked(this->m_GroupLock)]];
bool CUDTGroup::getBufferTimeBase(CUDT*                     forthesakeof,
                                  steady_clock::time_point& w_tb,
                                  bool&                     w_wp,
                                  steady_clock::duration&   w_dr)
{
    CUDT* master = 0;
    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        CUDT* u = &gi->ps->core();
        if (gi->laststatus != SRTS_CONNECTED)
        {
            HLOGC(gmlog.Debug,
                  log << "getBufferTimeBase: skipping @" << u->m_SocketID
                      << ": not connected, state=" << SockStatusStr(gi->laststatus));
            continue;
        }

        if (u == forthesakeof)
            continue; // skip the member if it's the target itself

        if (!u->m_pRcvBuffer)
            continue; // Not initialized yet

        master = u;
        break; // found
    }

    // We don't have any sockets in the group, so can't get
    // the buffer timebase. This should be then initialized
    // the usual way.
    if (!master)
        return false;

    w_wp = master->m_pRcvBuffer->getInternalTimeBase((w_tb), (w_dr));

    // Sanity check
    if (is_zero(w_tb))
    {
        LOGC(gmlog.Error, log << "IPE: existing previously socket has no time base set yet!");
        return false; // this will enforce initializing the time base normal way
    }
    return true;
}

// [[using locked(this->m_GroupLock)]];
bool CUDTGroup::applyGroupSequences(SRTSOCKET target, int32_t& w_snd_isn, int32_t& w_rcv_isn)
{
    if (m_bConnected) // You are the first one, no need to change.
    {
        IF_HEAVY_LOGGING(string update_reason = "what?");
        // Find a socket that is declared connected and is not
        // the socket that caused the call.
        for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
        {
            if (gi->id == target)
                continue;

            CUDT& se = gi->ps->core();
            if (!se.m_bConnected)
                continue;

            // Found it. Get the following sequences:
            // For sending, the sequence that is about to be sent next.
            // For receiving, the sequence of the latest received packet.

            // SndCurrSeqNo is initially set to ISN-1, this next one is
            // the sequence that is about to be stamped on the next sent packet
            // over that socket. Using this field is safer because it is volatile
            // and its affinity is to the same thread as the sending function.

            // NOTE: the groupwise scheduling sequence might have been set
            // already. If so, it means that it was set by either:
            // - the call of this function on the very first conencted socket (see below)
            // - the call to `sendBroadcast` or `sendBackup`
            // In both cases, we want THIS EXACTLY value to be reported
            if (m_iLastSchedSeqNo != -1)
            {
                w_snd_isn = m_iLastSchedSeqNo;
                IF_HEAVY_LOGGING(update_reason = "GROUPWISE snd-seq");
            }
            else
            {
                w_snd_isn = se.m_iSndNextSeqNo;

                // Write it back to the groupwise scheduling sequence so that
                // any next connected socket will take this value as well.
                m_iLastSchedSeqNo = w_snd_isn;
                IF_HEAVY_LOGGING(update_reason = "existing socket not yet sending");
            }

            // RcvCurrSeqNo is increased by one because it happens that at the
            // synchronization moment it's already past reading and delivery.
            // This is redundancy, so the redundant socket is connected at the moment
            // when the other one is already transmitting, so skipping one packet
            // even if later transmitted is less troublesome than requesting a
            // "mistakenly seen as lost" packet.
            w_rcv_isn = CSeqNo::incseq(se.m_iRcvCurrSeqNo);

            HLOGC(gmlog.Debug,
                  log << "applyGroupSequences: @" << target << " gets seq from @" << gi->id << " rcv %" << (w_rcv_isn)
                      << " snd %" << (w_rcv_isn) << " as " << update_reason);
            return false;
        }
    }

    // If the GROUP (!) is not connected, or no running/pending socket has been found.
    // // That is, given socket is the first one.
    // The group data should be set up with its own data. They should already be passed here
    // in the variables.
    //
    // Override the schedule sequence of the group in this case because whatever is set now,
    // it's not valid.

    HLOGC(gmlog.Debug,
          log << "applyGroupSequences: no socket found connected and transmitting, @" << target
              << " not changing sequences, storing snd-seq %" << (w_snd_isn));

    set_currentSchedSequence(w_snd_isn);

    return true;
}

// NOTE: This function is now for DEBUG PURPOSES ONLY.
// Except for presenting the extracted data in the logs, there's no use of it now.
void CUDTGroup::debugMasterData(SRTSOCKET slave)
{
    // Find at least one connection, which is running. Note that this function is called
    // from within a handshake process, so the socket that undergoes this process is at best
    // currently in SRT_GST_PENDING state and it's going to be in SRT_GST_IDLE state at the
    // time when the connection process is done, until the first reading/writing happens.
    ScopedLock cg(m_GroupLock);

    SRTSOCKET mpeer;
    steady_clock::time_point start_time;

    bool found = false;

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->sndstate == SRT_GST_RUNNING)
        {
            // Found it. Get the socket's peer's ID and this socket's
            // Start Time. Once it's delivered, this can be used to calculate
            // the Master-to-Slave start time difference.
            mpeer = gi->ps->m_PeerID;
            start_time    = gi->ps->core().socketStartTime();
            HLOGC(gmlog.Debug,
                  log << "getMasterData: found RUNNING master @" << gi->id << " - reporting master's peer $" << mpeer
                      << " starting at " << FormatTime(start_time));
            found = true;
            break;
        }
    }

    if (!found)
    {
        // If no running one found, then take the first socket in any other
        // state than broken, except the slave. This is for a case when a user
        // has prepared one link already, but hasn't sent anything through it yet.
        for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
        {
            if (gi->sndstate == SRT_GST_BROKEN)
                continue;

            if (gi->id == slave)
                continue;

            // Found it. Get the socket's peer's ID and this socket's
            // Start Time. Once it's delivered, this can be used to calculate
            // the Master-to-Slave start time difference.
            mpeer = gi->ps->core().m_PeerID;
            start_time    = gi->ps->core().socketStartTime();
            HLOGC(gmlog.Debug,
                    log << "getMasterData: found IDLE/PENDING master @" << gi->id << " - reporting master's peer $" << mpeer
                    << " starting at " << FormatTime(start_time));
            found = true;
            break;
        }
    }

    if (!found)
    {
        LOGC(cnlog.Debug, log << CONID() << "NO GROUP MASTER LINK found for group: $" << id());
    }
    else
    {
        // The returned master_st is the master's start time. Calculate the
        // differene time.
        steady_clock::duration master_tdiff = m_tsStartTime - start_time;
        LOGC(cnlog.Debug, log << CONID() << "FOUND GROUP MASTER LINK: peer=$" << mpeer
                << " - start time diff: " << FormatDuration<DUNIT_S>(master_tdiff));
    }
}

// GROUP

std::list<CUDTGroup::SocketData> CUDTGroup::GroupContainer::s_NoList;

CUDTGroup::gli_t CUDTGroup::add(SocketData data)
{
    ScopedLock g(m_GroupLock);

    // Change the snd/rcv state of the group member to PENDING.
    // Default for SocketData after creation is BROKEN, which just
    // after releasing the m_GroupLock could be read and interpreted
    // as broken connection and removed before the handshake process
    // is done.
    data.sndstate = SRT_GST_PENDING;
    data.rcvstate = SRT_GST_PENDING;

    HLOGC(gmlog.Debug, log << "CUDTGroup::add: adding new member @" << data.id);
    m_Group.push_back(data);
    gli_t end = m_Group.end();
    if (m_iMaxPayloadSize == -1)
    {
        int plsize = data.ps->m_pUDT->OPT_PayloadSize();
        HLOGC(gmlog.Debug,
              log << "CUDTGroup::add: taking MAX payload size from socket @" << data.ps->m_SocketID << ": " << plsize
                  << " " << (plsize ? "(explicit)" : "(unspecified = fallback to 1456)"));
        if (plsize == 0)
            plsize = SRT_LIVE_MAX_PLSIZE;
        // It is stated that the payload size
        // is taken from first, and every next one
        // will get the same.
        m_iMaxPayloadSize = plsize;
    }

    return --end;
}

CUDTGroup::SocketData CUDTGroup::prepareData(CUDTSocket* s)
{
    // This uses default SRT_GST_BROKEN because when the group operation is done,
    // then the SRT_GST_IDLE state automatically turns into SRT_GST_RUNNING. This is
    // recognized as an initial state of the fresh added socket to the group,
    // so some "initial configuration" must be done on it, after which it's
    // turned into SRT_GST_RUNNING, that is, it's treated as all others. When
    // set to SRT_GST_BROKEN, this socket is disregarded. This socket isn't cleaned
    // up, however, unless the status is simultaneously SRTS_BROKEN.

    // The order of operations is then:
    // - add the socket to the group in this "broken" initial state
    // - connect the socket (or get it extracted from accept)
    // - update the socket state (should be SRTS_CONNECTED)
    // - once the connection is established (may take time with connect), set SRT_GST_IDLE
    // - the next operation of send/recv will automatically turn it into SRT_GST_RUNNING
    SocketData sd = {
        s->m_SocketID,
        s,
        -1,
        SRTS_INIT,
        SRT_GST_BROKEN,
        SRT_GST_BROKEN,
        -1,
        -1,
        sockaddr_any(),
        sockaddr_any(),
        false,
        false,
        false,
        0 // weight
    };
    return sd;
}

CUDTGroup::CUDTGroup(SRT_GROUP_TYPE gtype)
    : m_pGlobal(&CUDT::s_UDTUnited)
    , m_GroupID(-1)
    , m_PeerGroupID(-1)
    , m_selfManaged(true)
    , m_bSyncOnMsgNo(false)
    , m_type(gtype)
    , m_listener()
    , m_iBusy()
    , m_iSndOldestMsgNo(SRT_MSGNO_NONE)
    , m_iSndAckedMsgNo(SRT_MSGNO_NONE)
    , m_uOPT_StabilityTimeout(CUDT::COMM_DEF_STABILITY_TIMEOUT_US)
    // -1 = "undefined"; will become defined with first added socket
    , m_iMaxPayloadSize(-1)
    , m_bSynRecving(true)
    , m_bSynSending(true)
    , m_bTsbPd(true)
    , m_bTLPktDrop(true)
    , m_iTsbPdDelay_us(0)
    // m_*EID and m_*Epolld fields will be initialized
    // in the constructor body.
    , m_iSndTimeOut(-1)
    , m_iRcvTimeOut(-1)
    , m_tsStartTime()
    , m_tsRcvPeerStartTime()
    , m_RcvBaseSeqNo(SRT_SEQNO_NONE)
    , m_bOpened(false)
    , m_bConnected(false)
    , m_bClosing(false)
    , m_iLastSchedSeqNo(SRT_SEQNO_NONE)
    , m_iLastSchedMsgNo(SRT_MSGNO_NONE)
{
    setupMutex(m_GroupLock, "Group");
    setupMutex(m_RcvDataLock, "RcvData");
    setupCond(m_RcvDataCond, "RcvData");
    m_RcvEID = m_pGlobal->m_EPoll.create(&m_RcvEpolld);
    m_SndEID = m_pGlobal->m_EPoll.create(&m_SndEpolld);

    // Set this data immediately during creation before
    // two or more sockets start arguing about it.
    m_iLastSchedSeqNo = CUDT::generateISN();

    // Configure according to type
    switch (gtype)
    {
    case SRT_GTYPE_BROADCAST:
        m_selfManaged = true;
        break;

    case SRT_GTYPE_BACKUP:
        m_selfManaged = true;
        break;

    case SRT_GTYPE_BALANCING:
        m_selfManaged  = true;
        m_bSyncOnMsgNo = true;
        break;

    case SRT_GTYPE_MULTICAST:
        m_selfManaged = false;
        break;

    default:
        break;
    }
}

CUDTGroup::~CUDTGroup()
{
    srt_epoll_release(m_RcvEID);
    srt_epoll_release(m_SndEID);
    releaseMutex(m_GroupLock);
    releaseMutex(m_RcvDataLock);
    releaseCond(m_RcvDataCond);
}

void CUDTGroup::GroupContainer::erase(CUDTGroup::gli_t it)
{
    if (it == m_LastActiveLink)
    {
        if (m_List.empty())
        {
            LOGC(gmlog.Error, log << "IPE: GroupContainer is empty and 'erase' is called on it.");
            return; // this avoids any misunderstandings in iterator checks
        }

        gli_t bb = m_List.begin();
        ++bb;
        if (bb == m_List.end()) // means: m_List.size() == 1
        {
            // One element, this one being deleted, nothing to point to.
            m_LastActiveLink = null();
        }
        else
        {
            // Set the link to the previous element IN THE RING.
            // We have the position pointer.
            // Reverse iterator is automatically decremented.
            std::reverse_iterator<gli_t> rt(m_LastActiveLink);
            if (rt == m_List.rend())
                rt = m_List.rbegin();

            m_LastActiveLink = rt.base();

            // This operation is safe because we know that:
            // - the size of the container is at least 2 (0 and 1 cases are handled above)
            // - if m_LastActiveLink == m_List.begin(), `rt` is shifted to the opposite end.
            --m_LastActiveLink;
        }
    }
    m_List.erase(it);
}

void CUDTGroup::setOpt(SRT_SOCKOPT optName, const void* optval, int optlen)
{
    HLOGC(gmlog.Debug,
          log << "GROUP $" << id() << " OPTION: #" << optName
              << " value:" << FormatBinaryString((uint8_t*)optval, optlen));

    switch (optName)
    {
    case SRTO_RCVSYN:
        m_bSynRecving = cast_optval<bool>(optval, optlen);
        return;

    case SRTO_SNDSYN:
        m_bSynSending = cast_optval<bool>(optval, optlen);
        return;

    case SRTO_SNDTIMEO:
        m_iSndTimeOut = cast_optval<int>(optval, optlen);
        break;

    case SRTO_RCVTIMEO:
        m_iRcvTimeOut = cast_optval<int>(optval, optlen);
        break;

    case SRTO_GROUPSTABTIMEO:
    {
        const int val = cast_optval<int>(optval, optlen);

        // Search if you already have SRTO_PEERIDLETIMEO set
        int                          idletmo = CUDT::COMM_RESPONSE_TIMEOUT_MS;
        vector<ConfigItem>::iterator f =
            find_if(m_config.begin(), m_config.end(), ConfigItem::OfType(SRTO_PEERIDLETIMEO));
        if (f != m_config.end())
        {
            f->get(idletmo); // worst case, it will leave it unchanged.
        }

        if (val >= idletmo)
        {
            LOGC(qmlog.Error,
                 log << "group option: SRTO_GROUPSTABTIMEO(" << val << ") exceeds SRTO_PEERIDLETIMEO(" << idletmo
                     << ")");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        m_uOPT_StabilityTimeout = val * 1000;
    }

    break;

        // XXX Currently no socket groups allow any other
        // congestion control mode other than live.
    case SRTO_CONGESTION:
    {
        LOGP(gmlog.Error, "group option: SRTO_CONGESTION is only allowed as 'live' and cannot be changed");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

        // Other options to be specifically interpreted by group may follow.

    default:
        break;
    }

    // All others must be simply stored for setting on a socket.
    // If the group is already open and any post-option is about
    // to be modified, it must be allowed and applied on all sockets.

    if (m_bOpened)
    {
        // There's at least one socket in the group, so only
        // post-options are allowed.
        if (!std::binary_search(srt_post_opt_list, srt_post_opt_list + SRT_SOCKOPT_NPOST, optName))
        {
            LOGC(gmlog.Error, log << "setsockopt(group): Group is connected, this option can't be altered");
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        }

        HLOGC(gmlog.Debug, log << "... SPREADING to existing sockets.");
        // This means that there are sockets already, so apply
        // this option on them.
        ScopedLock gg(m_GroupLock);
        for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
        {
            gi->ps->core().setOpt(optName, optval, optlen);
        }
    }

    // Store the option regardless if pre or post. This will apply
    m_config.push_back(ConfigItem(optName, optval, optlen));
}

static bool getOptDefault(SRT_SOCKOPT optname, void* optval, int& w_optlen);

// unfortunately this is required to properly handle th 'default_opt != opt'
// operation in the below importOption. Not required simultaneously operator==.
static bool operator!=(const struct linger& l1, const struct linger& l2)
{
    return l1.l_onoff != l2.l_onoff || l1.l_linger != l2.l_linger;
}

template <class ValueType>
static void importOption(vector<CUDTGroup::ConfigItem>& storage, SRT_SOCKOPT optname, const ValueType& field)
{
    ValueType default_opt      = ValueType();
    int       default_opt_size = sizeof(ValueType);
    ValueType opt              = field;
    if (!getOptDefault(optname, (&default_opt), (default_opt_size)) || default_opt != opt)
    {
        // Store the option when:
        // - no default for this option is found
        // - the option value retrieved from the field is different than default
        storage.push_back(CUDTGroup::ConfigItem(optname, &opt, default_opt_size));
    }
}

// This function is called by the same premises as the CUDT::CUDT(const CUDT&) (copy constructor).
// The intention is to rewrite the part that comprises settings from the socket
// into the group. Note that some of the settings concern group, some others concern
// only target socket, and there are also options that can't be set on a socket.
void CUDTGroup::deriveSettings(CUDT* u)
{
    // !!! IMPORTANT !!!
    //
    // This function shall ONLY be called on a newly created group
    // for the sake of the newly accepted socket from the group-enabled listener,
    // which is lazy-created for the first ever accepted socket.
    // Once the group is created, it should stay with the options
    // state as initialized here, and be changeable only in case when
    // the option is altered on the group.

    // SRTO_RCVSYN
    m_bSynRecving = u->m_bSynRecving;

    // SRTO_SNDSYN
    m_bSynSending = u->m_bSynSending;

    // SRTO_RCVTIMEO
    m_iRcvTimeOut = u->m_iRcvTimeOut;

    // SRTO_SNDTIMEO
    m_iSndTimeOut = u->m_iSndTimeOut;

    // Ok, this really is disgusting, but there's only one way
    // to properly do it. Would be nice to have some more universal
    // connection between an option symbolic name and the internals
    // in CUDT class, but until this is done, since now every new
    // option will have to be handled both in the CUDT::setOpt/getOpt
    // functions, and here as well.

    // This is about moving options from listener to the group,
    // to be potentially replicated on the socket. So both pre
    // and post options apply.

#define IM(option, field) importOption(m_config, option, u->field)

    IM(SRTO_MSS, m_iMSS);
    IM(SRTO_FC, m_iFlightFlagSize);

    // Nonstandard
    importOption(m_config, SRTO_SNDBUF, u->m_iSndBufSize * (u->m_iMSS - CPacket::UDP_HDR_SIZE));
    importOption(m_config, SRTO_RCVBUF, u->m_iRcvBufSize * (u->m_iMSS - CPacket::UDP_HDR_SIZE));

    IM(SRTO_LINGER, m_Linger);
    IM(SRTO_UDP_SNDBUF, m_iUDPSndBufSize);
    IM(SRTO_UDP_RCVBUF, m_iUDPRcvBufSize);
    // SRTO_RENDEZVOUS: impossible to have it set on a listener socket.
    // SRTO_SNDTIMEO/RCVTIMEO: groupwise setting
    IM(SRTO_CONNTIMEO, m_tdConnTimeOut);
    IM(SRTO_DRIFTTRACER, m_bDriftTracer);
    // Reuseaddr: true by default and should only be true.
    IM(SRTO_MAXBW, m_llMaxBW);
    IM(SRTO_INPUTBW, m_llInputBW);
    IM(SRTO_OHEADBW, m_iOverheadBW);
    IM(SRTO_IPTOS, m_iIpToS);
    IM(SRTO_IPTTL, m_iIpTTL);
    IM(SRTO_TSBPDMODE, m_bOPT_TsbPd);
    IM(SRTO_RCVLATENCY, m_iOPT_TsbPdDelay);
    IM(SRTO_PEERLATENCY, m_iOPT_PeerTsbPdDelay);
    IM(SRTO_SNDDROPDELAY, m_iOPT_SndDropDelay);
    IM(SRTO_PAYLOADSIZE, m_zOPT_ExpPayloadSize);
    IM(SRTO_TLPKTDROP, m_bTLPktDrop);
    IM(SRTO_STREAMID, m_sStreamName);
    IM(SRTO_MESSAGEAPI, m_bMessageAPI);
    IM(SRTO_NAKREPORT, m_bRcvNakReport);
    IM(SRTO_MINVERSION, m_lMinimumPeerSrtVersion);
    IM(SRTO_ENFORCEDENCRYPTION, m_bOPT_StrictEncryption);
    IM(SRTO_IPV6ONLY, m_iIpV6Only);
    IM(SRTO_PEERIDLETIMEO, m_iOPT_PeerIdleTimeout);
    IM(SRTO_GROUPSTABTIMEO, m_uOPT_StabilityTimeout);
    IM(SRTO_PACKETFILTER, m_OPT_PktFilterConfigString);

    importOption(m_config, SRTO_PBKEYLEN, u->m_pCryptoControl->KeyLen());

    // Passphrase is empty by default. Decipher the passphrase and
    // store as passphrase option
    if (u->m_CryptoSecret.len)
    {
        string password((const char*)u->m_CryptoSecret.str, u->m_CryptoSecret.len);
        m_config.push_back(ConfigItem(SRTO_PASSPHRASE, password.c_str(), password.size()));
    }

    IM(SRTO_KMREFRESHRATE, m_uKmRefreshRatePkt);
    IM(SRTO_KMPREANNOUNCE, m_uKmPreAnnouncePkt);

    string cc = u->m_CongCtl.selected_name();
    if (cc != "live")
    {
        m_config.push_back(ConfigItem(SRTO_CONGESTION, cc.c_str(), cc.size()));
    }

    // NOTE: This is based on information extracted from the "semi-copy-constructor" of CUDT class.
    // Here should be handled all things that are options that modify the socket, but not all options
    // are assigned to configurable items.

#undef IM
}

bool CUDTGroup::applyFlags(uint32_t flags, HandshakeSide hsd)
{
    bool synconmsg = IsSet(flags, SRT_GFLAG_SYNCONMSG);

    if (m_type == SRT_GTYPE_BALANCING)
    {
        // We support only TRUE for this flag
        if (!synconmsg)
        {
            HLOGP(gmlog.Debug, "GROUP: Balancing mode implemented only with sync on msgno - overridden request");
            return true; // accept, but override
        }

        // We have this flag set; change it in yourself, if needed.
        if (hsd == HSD_INITIATOR && !m_bSyncOnMsgNo)
        {
            // With this you can change in future the default value to false.
            HLOGP(gmlog.Debug, "GROUP: Balancing requrested msgno-sync, OVERRIDING original setting");
            m_bSyncOnMsgNo = true;
            return true;
        }
    }
    else
    {
        if (synconmsg)
        {
            LOGP(gmlog.Error, "GROUP: non-balancing type requested sync on msgno - IPE/EPE?");
            return false;
        }
    }

    // Ignore the flag anyway. This can change in future versions though.
    return true;
}

template <class Type>
struct Value
{
    static int fill(void* optval, int, Type value)
    {
        // XXX assert size >= sizeof(Type) ?
        *(Type*)optval = value;
        return sizeof(Type);
    }
};

template <>
inline int Value<std::string>::fill(void* optval, int len, std::string value)
{
    if (size_t(len) < value.size())
        return 0;
    memcpy(optval, value.c_str(), value.size());
    return value.size();
}

template <class V>
inline int fillValue(void* optval, int len, V value)
{
    return Value<V>::fill(optval, len, value);
}

static bool getOptDefault(SRT_SOCKOPT optname, void* pw_optval, int& w_optlen)
{
    static const linger def_linger = {1, CUDT::DEF_LINGER_S};
    switch (optname)
    {
    default:
        return false;

#define RD(value)                                                                                                      \
    w_optlen = fillValue((pw_optval), w_optlen, value);                                                                \
    break

    case SRTO_KMSTATE:
    case SRTO_SNDKMSTATE:
    case SRTO_RCVKMSTATE:
        RD(SRT_KM_S_UNSECURED);
    case SRTO_PBKEYLEN:
        RD(16);

    case SRTO_MSS:
        RD(CUDT::DEF_MSS);

    case SRTO_SNDSYN:
        RD(true);
    case SRTO_RCVSYN:
        RD(true);
    case SRTO_ISN:
        RD(SRT_SEQNO_NONE);
    case SRTO_FC:
        RD(CUDT::DEF_FLIGHT_SIZE);

    case SRTO_SNDBUF:
    case SRTO_RCVBUF:
        w_optlen = fillValue((pw_optval), w_optlen, CUDT::DEF_BUFFER_SIZE * (CUDT::DEF_MSS - CPacket::UDP_HDR_SIZE));
        break;

    case SRTO_LINGER:
        RD(def_linger);
    case SRTO_UDP_SNDBUF:
    case SRTO_UDP_RCVBUF:
        RD(CUDT::DEF_UDP_BUFFER_SIZE);
    case SRTO_RENDEZVOUS:
        RD(false);
    case SRTO_SNDTIMEO:
        RD(-1);
    case SRTO_RCVTIMEO:
        RD(-1);
    case SRTO_REUSEADDR:
        RD(true);
    case SRTO_MAXBW:
        RD(int64_t(-1));
    case SRTO_INPUTBW:
        RD(int64_t(-1));
    case SRTO_OHEADBW:
        RD(0);
    case SRTO_STATE:
        RD(SRTS_INIT);
    case SRTO_EVENT:
        RD(0);
    case SRTO_SNDDATA:
        RD(0);
    case SRTO_RCVDATA:
        RD(0);

    case SRTO_IPTTL:
        RD(0);
    case SRTO_IPTOS:
        RD(0);

    case SRTO_SENDER:
        RD(false);
    case SRTO_TSBPDMODE:
        RD(false);
    case SRTO_LATENCY:
    case SRTO_RCVLATENCY:
    case SRTO_PEERLATENCY:
        RD(SRT_LIVE_DEF_LATENCY_MS);
    case SRTO_TLPKTDROP:
        RD(true);
    case SRTO_SNDDROPDELAY:
        RD(-1);
    case SRTO_NAKREPORT:
        RD(true);
    case SRTO_VERSION:
        RD(SRT_DEF_VERSION);
    case SRTO_PEERVERSION:
        RD(0);

    case SRTO_CONNTIMEO:
        RD(-1);
    case SRTO_DRIFTTRACER:
        RD(true);

    case SRTO_MINVERSION:
        RD(0);
    case SRTO_STREAMID:
        RD(std::string());
    case SRTO_CONGESTION:
        RD(std::string());
    case SRTO_MESSAGEAPI:
        RD(true);
    case SRTO_PAYLOADSIZE:
        RD(0);
    }

#undef RD
    return true;
}

void CUDTGroup::getOpt(SRT_SOCKOPT optname, void* pw_optval, int& w_optlen)
{
    // Options handled in group
    switch (optname)
    {
    case SRTO_RCVSYN:
        *(bool*)pw_optval = m_bSynRecving;
        w_optlen          = sizeof(bool);
        return;

    case SRTO_SNDSYN:
        *(bool*)pw_optval = m_bSynSending;
        w_optlen          = sizeof(bool);
        return;

    default:; // pass on
    }

    // XXX Suspicous: may require locking of GlobControlLock
    // to prevent from deleting a socket in the meantime.
    // Deleting a socket requires removing from the group first,
    // so after GroupLock this will be either already NULL or
    // a valid socket that will only be closed after time in
    // the GC, so this is likely safe like all other API functions.
    CUDTSocket* ps = 0;

    {
        // In sockets. All sockets should have all options
        // set the same and should represent the group state
        // well enough. If there are no sockets, just use default.

        // Group lock to protect the container itself.
        // Once a socket is extracted, we state it cannot be
        // closed without the group send/recv function or closing
        // being involved.
        ScopedLock lg(m_GroupLock);
        if (m_Group.empty())
        {
            if (!getOptDefault(optname, (pw_optval), (w_optlen)))
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

            return;
        }

        ps = m_Group.begin()->ps;

        // Release the lock on the group, as it's not necessary,
        // as well as it might cause a deadlock when combined
        // with the others.
    }

    if (!ps)
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    return ps->core().getOpt(optname, (pw_optval), (w_optlen));
}

struct HaveState : public unary_function<pair<SRTSOCKET, SRT_SOCKSTATUS>, bool>
{
    SRT_SOCKSTATUS s;
    HaveState(SRT_SOCKSTATUS ss)
        : s(ss)
    {
    }
    bool operator()(pair<SRTSOCKET, SRT_SOCKSTATUS> i) const { return i.second == s; }
};

SRT_SOCKSTATUS CUDTGroup::getStatus()
{
    typedef vector<pair<SRTSOCKET, SRT_SOCKSTATUS> > states_t;
    states_t                                         states;

    {
        ScopedLock cg(m_GroupLock);
        for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
        {
            switch (gi->sndstate)
            {
                // Check only sndstate. If this machine is ONLY receiving,
                // then rcvstate will turn into SRT_GST_RUNNING, while
                // sndstate will remain SRT_GST_IDLE, but still this may only
                // happen if the socket is connected.
            case SRT_GST_IDLE:
            case SRT_GST_RUNNING:
                states.push_back(make_pair(gi->id, SRTS_CONNECTED));
                break;

            case SRT_GST_BROKEN:
                states.push_back(make_pair(gi->id, SRTS_BROKEN));
                break;

            default: // (pending, or whatever will be added in future)
            {
                // TEMPORARY make a node to note a socket to be checked afterwards
                states.push_back(make_pair(gi->id, SRTS_NONEXIST));
            }
            }
        }
    }

    SRT_SOCKSTATUS pending_state = SRTS_NONEXIST;

    for (states_t::iterator i = states.begin(); i != states.end(); ++i)
    {
        // If at least one socket is connected, the state is connected.
        if (i->second == SRTS_CONNECTED)
            return SRTS_CONNECTED;

        // Second level - pick up the state
        if (i->second == SRTS_NONEXIST)
        {
            // Otherwise find at least one socket, which's state isn't broken.
            i->second = m_pGlobal->getStatus(i->first);
            if (pending_state == SRTS_NONEXIST)
                pending_state = i->second;
        }
    }

        // Return that state as group state
    if (pending_state != SRTS_NONEXIST) // did call getStatus at least once and it didn't return NOEXIST
        return pending_state;

    // If none found, return SRTS_BROKEN.
    return SRTS_BROKEN;
}

// [[using locked(m_GroupLock)]];
void CUDTGroup::syncWithSocket(const CUDT& core, const HandshakeSide side)
{
    if (side == HSD_RESPONDER)
    {
        // On the listener side you should synchronize ISN with the incoming
        // socket, which is done immediately after creating the socket and
        // adding it to the group. On the caller side the ISN is defined in
        // the group directly, before any member socket is created.
        set_currentSchedSequence(core.ISN());
    }

    // XXX
    // Might need further investigation as to whether this isn't
    // wrong for some cases. By having this -1 here the value will be
    // laziliy set from the first reading one. It is believed that
    // it covers all possible scenarios, that is:
    //
    // - no readers - no problem!
    // - have some readers and a new is attached - this is set already
    // - connect multiple links, but none has read yet - you'll be the first.
    //
    // Previous implementation used setting to: core.m_iPeerISN
    resetInitialRxSequence();

    // Get the latency (possibly fixed against the opposite side)
    // from the first socket (core.m_iTsbPdDelay_ms),
    // and set it on the current socket.
    set_latency(core.m_iTsbPdDelay_ms * int64_t(1000));
}

void CUDTGroup::close()
{
    // Close all descriptors, then delete the group.
    vector<SRTSOCKET> ids;

    {
        ScopedLock g(m_GroupLock);

        // A non-managed group may only be closed if there are no
        // sockets in the group.

        // XXX Fortunately there are currently no non-self-managed
        // groups, so this error cannot ever happen, but this error
        // has the overall code suggesting that it's about the listener,
        // so either the name should be changed here, or a different code used.
        if (!m_selfManaged && !m_Group.empty())
            throw CUDTException(MJ_NOTSUP, MN_BUSY, 0);

        m_bClosing = true;

        // Copy the list of IDs into the array.
        for (gli_t ig = m_Group.begin(); ig != m_Group.end(); ++ig)
            ids.push_back(ig->id);
    }

    HLOGC(gmlog.Debug, log << "grp/close: closing $" << m_GroupID << ", closing first " << ids.size() << " sockets:");
    // Close all sockets with unlocked GroupLock
    for (vector<SRTSOCKET>::iterator i = ids.begin(); i != ids.end(); ++i)
        m_pGlobal->close(*i);
    HLOGC(gmlog.Debug, log << "grp/close: closing $" << m_GroupID << ": sockets closed, clearing the group:");

    // Lock the group again to clear the group data
    {
        ScopedLock g(m_GroupLock);

        if (!m_Group.empty())
        {
            LOGC(gmlog.Error, log << "grp/close: IPE - after requesting to close all members, still " << m_Group.size()
                    << " lingering members!");
            m_Group.clear();
        }

        m_PeerGroupID = -1;
        // This takes care of the internal part.
        // The external part will be done in Global (CUDTUnited)
    }

    // Release blocked clients
    // XXX This looks like a dead code. Group receiver functions
    // do not use any lock on m_RcvDataLock, it is likely a remainder
    // of the old, internal impementation. 
    // CSync::lock_signal(m_RcvDataCond, m_RcvDataLock);
}

// [[using locked(m_pGlobal->m_GlobControlLock)]]
// [[using locked(m_GroupLock)]]
void CUDTGroup::send_CheckValidSockets()
{
    vector<gli_t> toremove;

    for (gli_t d = m_Group.begin(), d_next = d; d != m_Group.end(); d = d_next)
    {
        ++d_next; // it's now safe to erase d
        CUDTSocket* revps = m_pGlobal->locateSocket_LOCKED(d->id);
        if (revps != d->ps)
        {
            m_Group.erase(d);
        }
    }
}

int CUDTGroup::send(const char* buf, int len, SRT_MSGCTRL& w_mc)
{
    switch (m_type)
    {
    default:
        LOGC(gslog.Error, log << "CUDTGroup::send: not implemented for type #" << m_type);
        throw CUDTException(MJ_SETUP, MN_INVAL, 0);

    case SRT_GTYPE_BROADCAST:
        return sendBroadcast(buf, len, (w_mc));

    case SRT_GTYPE_BACKUP:
        return sendBackup(buf, len, (w_mc));

        /* to be implemented

    case SRT_GTYPE_BALANCING:
        return sendBalancing(buf, len, (w_mc));

    case SRT_GTYPE_MULTICAST:
        return sendMulticast(buf, len, (w_mc));
        */
    }
}

int CUDTGroup::sendBroadcast(const char* buf, int len, SRT_MSGCTRL& w_mc)
{
    // Avoid stupid errors in the beginning.
    if (len <= 0)
    {
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    // NOTE: This is a "vector of list iterators". Every element here
    // is an iterator to another container.
    // Note that "list" is THE ONLY container in standard C++ library,
    // for which NO ITERATORS ARE INVALIDATED after a node at particular
    // iterator has been removed, except for that iterator itself.
    vector<SRTSOCKET> wipeme;
    vector<gli_t> idlers;
    vector<SRTSOCKET> pending; // need sock ids as it will be checked out of lock

    int32_t curseq = SRT_SEQNO_NONE;

    int rstat = -1;

    int                          stat = 0;
    SRT_ATR_UNUSED CUDTException cx(MJ_SUCCESS, MN_NONE, 0);

    vector<gli_t> sendable;

    // First, acquire GlobControlLock to make sure all member sockets still exist
    enterCS(m_pGlobal->m_GlobControlLock);
    ScopedLock guard(m_GroupLock);

    if (m_bClosing)
    {
        leaveCS(m_pGlobal->m_GlobControlLock);
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Now, still under lock, check if all sockets still can be dispatched

    // LOCKED: GlobControlLock, GroupLock (RIGHT ORDER!)
    send_CheckValidSockets();
    leaveCS(m_pGlobal->m_GlobControlLock);
    // LOCKED: GroupLock (only)
    // Since this moment GlobControlLock may only be locked if GroupLock is unlocked first.

    // This simply requires the payload to be sent through every socket in the group
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
    {
        // Check the socket state prematurely in order not to uselessly
        // send over a socket that is broken.
        CUDT* pu = 0;
        if (d->ps)
            pu = &d->ps->core();

        if (!pu || pu->m_bBroken)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBroadcast: socket @" << d->id << " detected +Broken - transit to BROKEN");
            d->sndstate = SRT_GST_BROKEN;
            d->rcvstate = SRT_GST_BROKEN;
        }

        // Check socket sndstate before sending
        if (d->sndstate == SRT_GST_BROKEN)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBroadcast: socket in BROKEN state: @" << d->id
                      << ", sockstatus=" << SockStatusStr(d->ps ? d->ps->getStatus() : SRTS_NONEXIST));
            wipeme.push_back(d->id);
            continue;
        }

        if (d->sndstate == SRT_GST_IDLE)
        {
            SRT_SOCKSTATUS st = SRTS_NONEXIST;
            if (d->ps)
                st = d->ps->getStatus();
            // If the socket is already broken, move it to broken.
            if (int(st) >= int(SRTS_BROKEN))
            {
                HLOGC(gslog.Debug,
                      log << "CUDTGroup::send.$" << id() << ": @" << d->id << " became " << SockStatusStr(st)
                          << ", WILL BE CLOSED.");
                wipeme.push_back(d->id);
                continue;
            }

            if (st != SRTS_CONNECTED)
            {
                HLOGC(gslog.Debug,
                      log << "CUDTGroup::send. @" << d->id << " is still " << SockStatusStr(st) << ", skipping.");
                pending.push_back(d->id);
                continue;
            }

            HLOGC(gslog.Debug, log << "grp/sendBroadcast: socket in IDLE state: @" << d->id << " - will activate it");
            // This is idle, we'll take care of them next time
            // Might be that:
            // - this socket is idle, while some NEXT socket is running
            // - we need at least one running socket to work BEFORE activating the idle one.
            // - if ALL SOCKETS ARE IDLE, then we simply activate the first from the list,
            //   and all others will be activated using the ISN from the first one.
            idlers.push_back(d);
            continue;
        }

        if (d->sndstate == SRT_GST_RUNNING)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBroadcast: socket in RUNNING state: @" << d->id << " - will send a payload");
            sendable.push_back(d);
            continue;
        }

        HLOGC(gslog.Debug,
              log << "grp/sendBroadcast: socket @" << d->id << " not ready, state: " << StateStr(d->sndstate) << "("
                  << int(d->sndstate) << ") - NOT sending, SET AS PENDING");

        pending.push_back(d->id);
    }

    vector<Sendstate> sendstates;

    for (vector<gli_t>::iterator snd = sendable.begin(); snd != sendable.end(); ++snd)
    {
        gli_t d   = *snd;
        int   erc = 0; // success
        // Remaining sndstate is SRT_GST_RUNNING. Send a payload through it.
        try
        {
            // This must be wrapped in try-catch because on error it throws an exception.
            // Possible return values are only 0, in case when len was passed 0, or a positive
            // >0 value that defines the size of the data that it has sent, that is, in case
            // of Live mode, equal to 'len'.
            stat = d->ps->core().sendmsg2(buf, len, (w_mc));
        }
        catch (CUDTException& e)
        {
            cx   = e;
            stat = -1;
            erc  = e.getErrorCode();
        }

        if (stat != -1)
        {
            curseq = w_mc.pktseq;
        }

        const Sendstate cstate = {d->id, d, stat, erc};
        sendstates.push_back(cstate);
        d->sndresult  = stat;
        d->laststatus = d->ps->getStatus();
    }

    // Ok, we have attempted to send a payload over all links
    // that are currently in the RUNNING state. We know that at
    // least one is successful if we have non-default curseq value.

    // Here we need to activate all links that are found as IDLE.
    // Some portion of logical exclusions:
    //
    // - sockets that were broken in the beginning are already wiped out
    // - broken sockets are checked first, so they can't be simultaneously idle
    // - idle sockets can't get broken because there's no operation done on them
    // - running sockets are the only one that could change sndstate here
    // - running sockets can either remain running or turn to broken
    // In short: Running and Broken sockets can't become idle,
    // although Running sockets can become Broken.

    // There's no certainty here as to whether at least one link was
    // running and it has successfully performed the operation.
    // Might have even happened that we had 2 running links that
    // got broken and 3 other links so far in idle sndstate that just connected
    // at that very moment. In this case we have 3 idle links to activate,
    // but there is no sequence base to overwrite their ISN with. If this
    // happens, then the first link that should be activated goes with
    // whatever ISN it has, whereas every next idle link should use that
    // exactly ISN.
    //
    // If it has additionally happened that the first link got broken at
    // that very moment of sending, the second one has a chance to succeed
    // and therefore take over the leading role in setting the ISN. If the
    // second one fails, too, then the only remaining idle link will simply
    // go with its own original sequence.
    //
    // On the opposite side the reader should know that the link is inactive
    // so the first received payload activates it. Activation of an idle link
    // means that the very first packet arriving is TAKEN AS A GOOD DEAL, that is,
    // no LOSSREPORT is sent even if the sequence looks like a "jumped over".
    // Only for activated links is the LOSSREPORT sent upon seqhole detection.

    // Now we can go to the idle links and attempt to send the payload
    // also over them.

    // { sendBroadcast_ActivateIdlers
    for (vector<gli_t>::iterator i = idlers.begin(); i != idlers.end(); ++i)
    {
        gli_t d       = *i;
        if (!d->ps->m_IncludedGroup)
            continue;

        int   erc     = 0;
        int   lastseq = d->ps->core().schedSeqNo();
        if (curseq != SRT_SEQNO_NONE && curseq != lastseq)
        {
            HLOGC(gslog.Debug,
                    log << "grp/sendBroadcast: socket @" << d->id << ": override snd sequence %" << lastseq << " with %"
                    << curseq << " (diff by " << CSeqNo::seqcmp(curseq, lastseq)
                    << "); SENDING PAYLOAD: " << BufferStamp(buf, len));
            d->ps->core().overrideSndSeqNo(curseq);
        }
        else
        {
            HLOGC(gslog.Debug,
                    log << "grp/sendBroadcast: socket @" << d->id << ": sequence remains with original value: %"
                    << lastseq << "; SENDING PAYLOAD " << BufferStamp(buf, len));
        }

        // Now send and check the status
        // The link could have got broken

        try
        {
            stat = d->ps->core().sendmsg2(buf, len, (w_mc));
        }
        catch (CUDTException& e)
        {
            cx   = e;
            stat = -1;
            erc  = e.getErrorCode();
        }

        if (stat != -1)
        {
            d->sndstate = SRT_GST_RUNNING;

            // Note: this will override the sequence number
            // for all next iterations in this loop.
            curseq = w_mc.pktseq;
            HLOGC(gslog.Debug,
                    log << "@" << d->id << ":... sending SUCCESSFUL %" << curseq << " MEMBER STATUS: RUNNING");
        }

        d->sndresult  = stat;
        d->laststatus = d->ps->getStatus();

        const Sendstate cstate = {d->id, d, stat, erc};
        sendstates.push_back(cstate);
    }

    if (curseq != SRT_SEQNO_NONE)
    {
        HLOGC(gslog.Debug, log << "grp/sendBroadcast: updating current scheduling sequence %" << curseq);
        m_iLastSchedSeqNo = curseq;
    }

    // }

    // { send_CheckBrokenSockets()

    if (!pending.empty())
    {
        HLOGC(gslog.Debug, log << "grp/sendBroadcast: found pending sockets, polling them.");

        // These sockets if they are in pending state, they should be added to m_SndEID
        // at the connecting stage.
        CEPoll::fmap_t sready;

        if (m_SndEpolld->watch_empty())
        {
            // Sanity check - weird pending reported.
            LOGC(gslog.Error,
                 log << "grp/sendBroadcast: IPE: reported pending sockets, but EID is empty - wiping pending!");
            copy(pending.begin(), pending.end(), back_inserter(wipeme));
        }
        else
        {
            {
                InvertedLock ug(m_GroupLock);

                THREAD_PAUSED();
                m_pGlobal->m_EPoll.swait(
                    *m_SndEpolld, sready, 0, false /*report by retval*/); // Just check if anything happened
                THREAD_RESUMED();
            }

            HLOGC(gslog.Debug, log << "grp/sendBroadcast: RDY: " << DisplayEpollResults(sready));

            // sockets in EX: should be moved to wipeme.
            for (vector<SRTSOCKET>::iterator i = pending.begin(); i != pending.end(); ++i)
            {
                if (CEPoll::isready(sready, *i, SRT_EPOLL_ERR))
                {
                    HLOGC(gslog.Debug,
                          log << "grp/sendBroadcast: Socket @" << (*i) << " reported FAILURE - moved to wiped.");
                    // Failed socket. Move d to wipeme. Remove from eid.
                    wipeme.push_back(*i);
                    int no_events = 0;
                    m_pGlobal->m_EPoll.update_usock(m_SndEID, *i, &no_events);
                }
            }

            // After that, all sockets that have been reported
            // as ready to write should be removed from EID. This
            // will also remove those sockets that have been added
            // as redundant links at the connecting stage and became
            // writable (connected) before this function had a chance
            // to check them.
            m_pGlobal->m_EPoll.clear_ready_usocks(*m_SndEpolld, SRT_EPOLL_CONNECT);
        }
    }

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    if (!wipeme.empty())
    {
        InvertedLock ug(m_GroupLock);

        // With unlocked GroupLock, we can now lock GlobControlLock.
        // This is a requirement for makeClosed.
        ScopedLock globlock(CUDT::s_UDTUnited.m_GlobControlLock);

        for (vector<SRTSOCKET>::iterator p = wipeme.begin(); p != wipeme.end(); ++p)
        {
            CUDTSocket* s = CUDT::s_UDTUnited.locateSocket_LOCKED(*p);

            // If the socket has been just moved to ClosedSockets, it means that
            // the object still exists, but it will be no longer findable.
            if (!s)
                continue;

            HLOGC(gslog.Debug,
                  log << "grp/sendBroadcast: BROKEN SOCKET @" << (*p) << " - CLOSING AND REMOVING.");

            s->makeClosed(); // <-- will use GroupLock to delete the socket from the group container
        }
    }

    HLOGC(gslog.Debug, log << "grp/sendBroadcast: - wiped " << wipeme.size() << " broken sockets");

    // We'll need you again.
    wipeme.clear();

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    // }

    // { sendBroadcast_CheckBlockedLinks()

    // Alright, we've made an attempt to send a packet over every link.
    // Every operation was done through a non-blocking attempt, so
    // links where sending was blocked have SRT_EASYNCSND error.
    // Links that were successful, have the len value in state.

    // First thing then, find out if at least one link was successful.
    // This might even be one of the idlers only, this doesn't matter.
    // If there were any running links successful, they have set the sequence.
    // If there were only some reactivated idlers successful, the first
    // idler has defined the sequence.

    vector<gli_t> successful, blocked;

    // This iteration of the state will simply
    // qualify the remaining sockets into three categories:
    //
    // - successful (we only need to know if at least one did)
    // - blocked - if none succeeded, but some blocked, POLL & RETRY.
    // - wipeme - sending failed by any other reason than blocking, remove.

    // Now - sendstates contain directly sockets.
    // In order to update members, you need to have locked:
    // - GlobControlLock to prevent sockets from disappearing or being closed
    // - then GroupLock to latch the validity of m_IncludedIter field.

    {
        {
            InvertedLock ung (m_GroupLock);
            enterCS(CUDT::s_UDTUnited.m_GlobControlLock);
        }

        // Under this condition, as an unlock-lock cycle was done on m_GroupLock,
        // the Sendstate::it field shall not be used here!
        for (vector<Sendstate>::iterator is = sendstates.begin(); is != sendstates.end(); ++is)
        {
            CUDTSocket* ps = CUDT::s_UDTUnited.locateSocket_LOCKED(is->id);

            // Is the socket valid? If not, simply SKIP IT. Nothing to be done with it,
            // it's already deleted.
            if (!ps)
                continue;

            // Is the socket still group member? If not, SKIP IT. It could only be taken ownership
            // by being explicitly closed and so it's deleted from the container.
            if (!ps->m_IncludedGroup)
                continue;

            // Now we are certain that m_IncludedIter is valid.
            gli_t d = ps->m_IncludedIter;

            if (is->stat == len)
            {
                HLOGC(gslog.Debug,
                        log << "SEND STATE link [" << (is - sendstates.begin()) << "]: SUCCESSFULLY sent " << len
                        << " bytes");
                // Successful.
                successful.push_back(d);
                rstat = is->stat;
                continue;
            }

            // Remaining are only failed. Check if again.
            if (is->code == SRT_EASYNCSND)
            {
                blocked.push_back(d);
                continue;
            }

#if ENABLE_HEAVY_LOGGING
            string errmsg = cx.getErrorString();
            LOGC(gslog.Debug,
                    log << "SEND STATE link [" << (is - sendstates.begin()) << "]: FAILURE (result:" << is->stat
                    << "): " << errmsg << ". Setting this socket broken status.");
#endif
            // Turn this link broken
            d->sndstate = SRT_GST_BROKEN;
        }

        // Now you can leave GlobControlLock, while GroupLock is still locked.
        leaveCS(CUDT::s_UDTUnited.m_GlobControlLock);
        HLOGC(gslog.Debug, log << "grp/sendBroadcast: locking back GroupLock");
    }

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
    {
        HLOGC(gslog.Debug, log << "grp/sendBroadcast: GROUP CLOSED, ABANDONING");
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Good, now let's realize the situation.
    // First, check the most optimistic scenario: at least one link succeeded.

    bool was_blocked    = false;
    bool none_succeeded = false;

    if (!successful.empty())
    {
        // Good. All blocked links are now qualified as broken.
        // You had your chance, but I can't leave you here,
        // there will be no further chance to reattempt sending.
        for (vector<gli_t>::iterator b = blocked.begin(); b != blocked.end(); ++b)
        {
            (*b)->sndstate = SRT_GST_BROKEN;
        }
        blocked.clear();
    }
    else
    {
        none_succeeded = true;
        was_blocked    = !blocked.empty();
    }

    int ercode = 0;

    if (was_blocked)
    {
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        if (!m_bSynSending)
        {
            throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);
        }

        HLOGC(gslog.Debug, log << "grp/sendBroadcast: all blocked, trying to common-block on epoll...");

        // XXX TO BE REMOVED. Sockets should be subscribed in m_SndEID at connecting time
        // (both srt_connect and srt_accept).

        // None was successful, but some were blocked. It means that we
        // haven't sent the payload over any link so far, so we still have
        // a chance to retry.
        int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
        for (vector<gli_t>::iterator b = blocked.begin(); b != blocked.end(); ++b)
        {
            HLOGC(gslog.Debug,
                  log << "Will block on blocked socket @" << (*b)->id << " as only blocked socket remained");
            CUDT::s_UDTUnited.epoll_add_usock_INTERNAL(m_SndEID, (*b)->ps, &modes);
        }

        const int blocklen = blocked.size();

        int            blst = 0;
        CEPoll::fmap_t sready;

        {
            // Lift the group lock for a while, to avoid possible deadlocks.
            InvertedLock ug(m_GroupLock);
            HLOGC(gslog.Debug, log << "grp/sendBroadcast: blocking on any of blocked sockets to allow sending");

            // m_iSndTimeOut is -1 by default, which matches the meaning of waiting forever
            THREAD_PAUSED();
            blst = m_pGlobal->m_EPoll.swait(*m_SndEpolld, sready, m_iSndTimeOut);
            THREAD_RESUMED();

            // NOTE EXCEPTIONS:
            // - EEMPTY: won't happen, we have explicitly added sockets to EID here.
            // - XTIMEOUT: will be propagated as this what should be reported to API
            // This is the only reason why here the errors are allowed to be handled
            // by exceptions.
        }

        // Re-check after the waiting lock has been reacquired
        if (m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

        if (blst == -1)
        {
            int rno;
            ercode = srt_getlasterror(&rno);
        }
        else
        {
            sendable.clear();
            sendstates.clear();
            // Extract gli's from the whole group that have id found in the array.

            // LOCKING INFO:
            // For the moment of lifting m_GroupLock, some sockets could have been closed.
            // But then, we believe they have been also removed from the group container,
            // and this requires locking on GroupLock. We can then stafely state that the
            // group container contains only existing sockets, at worst broken.

            for (gli_t dd = m_Group.begin(); dd != m_Group.end(); ++dd)
            {
                int rdev = CEPoll::ready(sready, dd->id);
                if (rdev & SRT_EPOLL_ERR)
                {
                    dd->sndstate = SRT_GST_BROKEN;
                }
                else if (rdev & SRT_EPOLL_OUT)
                    sendable.push_back(dd);
            }

            for (vector<gli_t>::iterator snd = sendable.begin(); snd != sendable.end(); ++snd)
            {
                gli_t d   = *snd;

                int   erc = 0; // success
                // Remaining sndstate is SRT_GST_RUNNING. Send a payload through it.
                try
                {
                    // This must be wrapped in try-catch because on error it throws an exception.
                    // Possible return values are only 0, in case when len was passed 0, or a positive
                    // >0 value that defines the size of the data that it has sent, that is, in case
                    // of Live mode, equal to 'blocklen'.
                    stat = d->ps->core().sendmsg2(buf, blocklen, (w_mc));
                }
                catch (CUDTException& e)
                {
                    cx   = e;
                    stat = -1;
                    erc  = e.getErrorCode();
                }
                if (stat != -1)
                    curseq = w_mc.pktseq;

                const Sendstate cstate = {d->id, d, stat, erc};
                sendstates.push_back(cstate);
                d->sndresult  = stat;
                d->laststatus = d->ps->getStatus();
            }

            // This time only check if any were successful.
            // All others are wipeme.
            // NOTE: m_GroupLock is continuously locked - you can safely use Sendstate::it field.
            for (vector<Sendstate>::iterator is = sendstates.begin(); is != sendstates.end(); ++is)
            {
                if (is->stat == blocklen)
                {
                    // Successful.
                    successful.push_back(is->it);
                    rstat          = is->stat;
                    was_blocked    = false;
                    none_succeeded = false;
                    continue;
                }
#if ENABLE_HEAVY_LOGGING
                string errmsg = cx.getErrorString();
                HLOGC(gslog.Debug,
                      log << "... (repeat-waited) sending FAILED (" << errmsg
                          << "). Setting this socket broken status.");
#endif
                // Turn this link broken
                is->it->sndstate = SRT_GST_BROKEN;
            }
        }
    }

    // }

    if (none_succeeded)
    {
        HLOGC(gslog.Debug, log << "grp/sendBroadcast: all links broken (none succeeded to send a payload)");
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
        // Reparse error code, if set.
        // It might be set, if the last operation was failed.
        // If any operation succeeded, this will not be executed anyway.
        CodeMajor major = CodeMajor(ercode ? ercode / 1000 : MJ_CONNECTION);
        CodeMinor minor = CodeMinor(ercode ? ercode % 1000 : MN_CONNLOST);

        throw CUDTException(major, minor, 0);
    }

    // Now that at least one link has succeeded, update sending stats.
    m_stats.sent.Update(len);

    // Pity that the blocking mode only determines as to whether this function should
    // block or not, but the epoll flags must be updated regardless of the mode.

    // Now fill in the socket table. Check if the size is enough, if not,
    // then set the pointer to NULL and set the correct size.

    // Note that list::size() is linear time, however this shouldn't matter,
    // as with the increased number of links in the redundancy group the
    // impossibility of using that many of them grows exponentally.
    size_t grpsize = m_Group.size();

    if (w_mc.grpdata_size < grpsize)
    {
        w_mc.grpdata = NULL;
    }

    size_t i = 0;

    bool ready_again = false;
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d, ++i)
    {
        if (w_mc.grpdata)
        {
            // Enough space to fill
            copyGroupData(*d, (w_mc.grpdata[i]));
        }

        // We perform this loop anyway because we still need to check if any
        // socket is writable. Note that the group lock will hold any write ready
        // updates that are performed just after a single socket update for the
        // group, so if any socket is actually ready at the moment when this
        // is performed, and this one will result in none-write-ready, this will
        // be fixed just after returning from this function.

        ready_again = ready_again | d->ps->writeReady();
    }
    w_mc.grpdata_size = i;

    if (!ready_again)
    {
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
    }

    return rstat;
}

int CUDTGroup::getGroupData(SRT_SOCKGROUPDATA* pdata, size_t* psize)
{
    if (!psize)
        return CUDT::APIError(MJ_NOTSUP, MN_INVAL);

    ScopedLock gl(m_GroupLock);

    return getGroupData_LOCKED(pdata, psize);
}

// [[using locked(this->m_GroupLock)]]
int CUDTGroup::getGroupData_LOCKED(SRT_SOCKGROUPDATA* pdata, size_t* psize)
{
    SRT_ASSERT(psize != NULL);
    const size_t size = *psize;
    // Rewrite correct size
    *psize = m_Group.size();

    if (!pdata)
    {
        return 0;
    }

    if (m_Group.size() > size)
    {
        // Not enough space to retrieve the data.
        return CUDT::APIError(MJ_NOTSUP, MN_XSIZE);
    }

    size_t i = 0;
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d, ++i)
    {
        copyGroupData(*d, (pdata[i]));
    }

    return m_Group.size();
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::copyGroupData(const CUDTGroup::SocketData& source, SRT_SOCKGROUPDATA& w_target)
{
    w_target.id = source.id;
    memcpy((&w_target.peeraddr), &source.peer, source.peer.size());

    w_target.sockstate = source.laststatus;
    w_target.token = source.token;

    // In the internal structure the member state
    // is one per direction. From the user perspective
    // however it is used either in one direction only,
    // in which case the one direction that is active
    // matters, or in both directions, in which case
    // it will be always either both active or both idle.

    if (source.sndstate == SRT_GST_RUNNING || source.rcvstate == SRT_GST_RUNNING)
    {
        w_target.result      = 0;
        w_target.memberstate = SRT_GST_RUNNING;
    }
    // Stats can differ per direction only
    // when at least in one direction it's ACTIVE.
    else if (source.sndstate == SRT_GST_BROKEN || source.rcvstate == SRT_GST_BROKEN)
    {
        w_target.result      = -1;
        w_target.memberstate = SRT_GST_BROKEN;
    }
    else
    {
        // IDLE or PENDING
        w_target.result      = 0;
        w_target.memberstate = source.sndstate;
    }

    w_target.weight = source.weight;
}

void CUDTGroup::getGroupCount(size_t& w_size, bool& w_still_alive)
{
    ScopedLock gg(m_GroupLock);

    // Note: linear time, but no way to avoid it.
    // Fortunately the size of the redundancy group is even
    // in the craziest possible implementation at worst 4 members long.
    size_t group_list_size = 0;

    // In managed group, if all sockets made a failure, all
    // were removed, so the loop won't even run once. In
    // non-managed, simply no socket found here would have a
    // connected status.
    bool still_alive = false;

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->laststatus == SRTS_CONNECTED)
        {
            still_alive = true;
        }
        ++group_list_size;
    }

    // If no socket is found connected, don't update any status.
    w_size        = group_list_size;
    w_still_alive = still_alive;
}

// [[using locked(m_GroupLock)]]
void CUDTGroup::fillGroupData(SRT_MSGCTRL&       w_out, // MSGCTRL to be written
                              const SRT_MSGCTRL& in     // MSGCTRL read from the data-providing socket
)
{
    // Preserve the data that will be overwritten by assignment
    SRT_SOCKGROUPDATA* grpdata      = w_out.grpdata;
    size_t             grpdata_size = w_out.grpdata_size;

    w_out = in; // NOTE: This will write NULL to grpdata and 0 to grpdata_size!

    w_out.grpdata      = NULL; // Make sure it's done, for any case
    w_out.grpdata_size = 0;

    // User did not wish to read the group data at all.
    if (!grpdata)
    {
        return;
    }

    int st = getGroupData_LOCKED((grpdata), (&grpdata_size));

    // Always write back the size, no matter if the data were filled.
    w_out.grpdata_size = grpdata_size;

    if (st == SRT_ERROR)
    {
        // Keep NULL in grpdata
        return;
    }

    // Write back original data
    w_out.grpdata = grpdata;
}

struct FLookupSocketWithEvent_LOCKED
{
    CUDTUnited* glob;
    int         evtype;
    FLookupSocketWithEvent_LOCKED(CUDTUnited* g, int event_type)
        : glob(g)
        , evtype(event_type)
    {
    }

    typedef CUDTSocket* result_type;

    pair<CUDTSocket*, bool> operator()(const pair<SRTSOCKET, int>& es)
    {
        CUDTSocket* so = NULL;
        if ((es.second & evtype) == 0)
            return make_pair(so, false);

        so = glob->locateSocket_LOCKED(es.first);
        return make_pair(so, !!so);
    }
};

void CUDTGroup::updateReadState(SRTSOCKET /* not sure if needed */, int32_t sequence)
{
    bool       ready = false;
    ScopedLock lg(m_GroupLock);
    int        seqdiff = 0;

    if (m_RcvBaseSeqNo == SRT_SEQNO_NONE)
    {
        // One socket reported readiness, while no reading operation
        // has ever been done. Whatever the sequence number is, it will
        // be taken as a good deal and reading will be accepted.
        ready = true;
    }
    else if ((seqdiff = CSeqNo::seqcmp(sequence, m_RcvBaseSeqNo)) > 0)
    {
        // Case diff == 1: The very next. Surely read-ready.

        // Case diff > 1:
        // We have an ahead packet. There's one strict condition in which
        // we may believe it needs to be delivered - when KANGAROO->HORSE
        // transition is allowed. Stating that the time calculation is done
        // exactly the same way on every link in the redundancy group, when
        // it came to a situation that a packet from one link is ready for
        // extraction while it has jumped over some packet, it has surely
        // happened due to TLPKTDROP, and if it happened on at least one link,
        // we surely don't have this packet ready on any other link.

        // This might prove not exactly true, especially when at the moment
        // when this happens another link may surprisinly receive this lacking
        // packet, so the situation gets suddenly repaired after this function
        // is called, the only result of it would be that it will really get
        // the very next sequence, even though this function doesn't know it
        // yet, but surely in both cases the situation is the same: the medium
        // is ready for reading, no matter what packet will turn out to be
        // returned when reading is done.

        ready = true;
    }

    // When the sequence number is behind the current one,
    // stating that the readines wasn't checked otherwise, the reading
    // function will not retrieve anything ready to read just by this premise.
    // Even though this packet would have to be eventually extracted (and discarded).

    if (ready)
    {
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, true);
    }
}

void CUDTGroup::updateWriteState()
{
    ScopedLock lg(m_GroupLock);
    m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, true);
}

// The "app reader" version of the reading function.
// This reads the packets from every socket treating them as independent
// and prepared to work with the application. Then packets are sorted out
// by getting the sequence number.
int CUDTGroup::recv(char* buf, int len, SRT_MSGCTRL& w_mc)
{
    typedef map<SRTSOCKET, ReadPos>::iterator pit_t;
    // Later iteration over it might be less efficient than
    // by vector, but we'll also often try to check a single id
    // if it was ever seen broken, so that it's skipped.
    set<CUDTSocket*> broken;
    size_t output_size = 0;

    // First, acquire GlobControlLock to make sure all member sockets still exist
    enterCS(m_pGlobal->m_GlobControlLock);
    ScopedLock guard(m_GroupLock);

    if (m_bClosing)
    {
        // The group could be set closing in the meantime, but if
        // this is only about to be set by another thread, this thread
        // must fist wait for being able to acquire this lock.
        // The group will not be deleted now because it is added usage counter
        // by this call, but will be released once it exits.
        leaveCS(m_pGlobal->m_GlobControlLock);
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Now, still under lock, check if all sockets still can be dispatched
    send_CheckValidSockets();
    leaveCS(m_pGlobal->m_GlobControlLock);

    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    for (;;)
    {
        if (!m_bOpened || !m_bConnected)
        {
            LOGC(grlog.Error,
                 log << boolalpha << "group/recv: ERROR opened=" << m_bOpened << " connected=" << m_bConnected);
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        }

        // Check first the ahead packets if you have any to deliver.
        if (m_RcvBaseSeqNo != SRT_SEQNO_NONE && !m_Positions.empty())
        {
            // This function also updates the group sequence pointer.
            ReadPos* pos = checkPacketAhead();
            if (pos)
            {
                if (size_t(len) < pos->packet.size())
                    throw CUDTException(MJ_NOTSUP, MN_XSIZE, 0);

                HLOGC(grlog.Debug,
                      log << "group/recv: delivering AHEAD packet %" << pos->mctrl.pktseq << " #" << pos->mctrl.msgno
                          << ": " << BufferStamp(&pos->packet[0], pos->packet.size()));
                memcpy(buf, &pos->packet[0], pos->packet.size());
                fillGroupData((w_mc), pos->mctrl);
                len = pos->packet.size();
                pos->packet.clear();

                // Update stats as per delivery
                m_stats.recv.Update(len);
                updateAvgPayloadSize(len);

                // We predict to have only one packet ahead, others are pending to be reported by tsbpd.
                // This will be "re-enabled" if the later check puts any new packet into ahead.
                m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);

                return len;
            }
        }

        // LINK QUALIFICATION NAMES:
        //
        // HORSE: Correct link, which delivers the very next sequence.
        // Not necessarily this link is currently active.
        //
        // KANGAROO: Got some packets dropped and the sequence number
        // of the packet jumps over the very next sequence and delivers
        // an ahead packet.
        //
        // ELEPHANT: Is not ready to read, while others are, or reading
        // up to the current latest delivery sequence number does not
        // reach this sequence and the link becomes non-readable earlier.

        // The above condition has ruled out one kangaroo and turned it
        // into a horse.

        // Below there's a loop that will try to extract packets. Kangaroos
        // will be among the polled ones because skipping them risks that
        // the elephants will take over the reading. Links already known as
        // elephants will be also polled in an attempt to revitalize the
        // connection that experienced just a short living choking.
        //
        // After polling we attempt to read from every link that reported
        // read-readiness and read at most up to the sequence equal to the
        // current delivery sequence.

        // Links that deliver a packet below that sequence will be retried
        // until they deliver no more packets or deliver the packet of
        // expected sequence. Links that don't have a record in m_Positions
        // and report readiness will be always read, at least to know what
        // sequence they currently stand on.
        //
        // Links that are already known as kangaroos will be polled, but
        // no reading attempt will be done. If after the reading series
        // it will turn out that we have no more horses, the slowest kangaroo
        // will be "upgraded to a horse" (the ahead link with a sequence
        // closest to the current delivery sequence will get its sequence
        // set as current delivered and its recorded ahead packet returned
        // as the read packet).

        // If we find at least one horse, the packet read from that link
        // will be delivered. All other link will be just ensured update
        // up to this sequence number, or at worst all available packets
        // will be read. In this case all kangaroos remain kangaroos,
        // until the current delivery sequence m_RcvBaseSeqNo will be lifted
        // to the sequence recorded for these links in m_Positions,
        // during the next time ahead check, after which they will become
        // horses.

#if ENABLE_HEAVY_LOGGING
        std::ostringstream ds;
        ds << "E(" << m_RcvEID << ") ";
#define HCLOG(expr) expr
#else
#define HCLOG(x)                                                                                                       \
    if (false)                                                                                                         \
    {                                                                                                                  \
    }
#endif

        bool   still_alive = false;
        size_t size        = 0;

        // You can't lock the whole group for that
        // action because this will result in a deadlock.
        // Prepare first the list of sockets to be added as connect-pending
        // and as read-ready, then unlock the group, and then add them to epoll.
        vector<CUDTSocket*> read_ready;
        vector<SRTSOCKET> connect_pending;

        {
            HLOGC(grlog.Debug, log << "group/recv: Reviewing member sockets to epoll-add");
            for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
            {
                ++size; // list::size loops over all elements anyway, so this hits two birds with one stone
                if (gi->laststatus == SRTS_CONNECTING)
                {
                    HCLOG(ds << "@" << gi->id << "<pending> ");
                    /*
                       connect_pending.push_back(gi->id);
                     */

                    continue; // don't read over a failed or pending socket
                }

                if (gi->laststatus >= SRTS_BROKEN)
                {
                    broken.insert(gi->ps);
                }

                if (broken.count(gi->ps))
                {
                    HCLOG(ds << "@" << gi->id << "<broken> ");
                    continue;
                }

                if (gi->laststatus != SRTS_CONNECTED)
                {
                    HCLOG(ds << "@" << gi->id << "<unstable:" << SockStatusStr(gi->laststatus) << "> ");
                    // Sockets in this state are ignored. We are waiting until it
                    // achieves CONNECTING state, then it's added to write.
                    // Or gets broken and closed in the next step.
                    continue;
                }

                still_alive = true;

                // Don't skip packets that are ahead because if we have a situation
                // that all links are either "elephants" (do not report read readiness)
                // and "kangaroos" (have already delivered an ahead packet) then
                // omiting kangaroos will result in only elephants to be polled for
                // reading. Due to the strict timing requirements and ensurance that
                // TSBPD on every link will result in exactly the same delivery time
                // for a packet of given sequence, having an elephant and kangaroo in
                // one cage means that the elephant is simply a broken or half-broken
                // link (the data are not delivered, but it will get repaired soon,
                // enough for SRT to maintain the connection, but it will still drop
                // packets that didn't arrive in time), in both cases it may
                // potentially block the reading for an indefinite time, while
                // simultaneously a kangaroo might be a link that got some packets
                // dropped, but then it's still capable to deliver packets on time.

                // Note that gi->id might be a socket that was previously being polled
                // on write, when it's attempting to connect, but now it's connected.
                // This will update the socket with the new event set.

                read_ready.push_back(gi->ps);
                HCLOG(ds << "@" << gi->id << "[READ] ");
            }
        }

        int read_modes = SRT_EPOLL_IN | SRT_EPOLL_ERR;

        /* Done at the connecting stage so that it won't be missed.

           int connect_modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
           for (vector<SRTSOCKET>::iterator i = connect_pending.begin(); i != connect_pending.end(); ++i)
           {
           XXX This is wrong code; this should use the internal function and pass CUDTSocket*
           epoll_add_usock_INTERNAL(m_RcvEID, i->second, &connect_modes);
           }

           AND this below additionally for sockets that were so far pending connection,
           will be now "upgraded" to readable sockets. The epoll adding function for a
           socket that already is in the eid container will only change the poll flags,
           but will not re-add it, that is, event reports that are both in old and new
           flags will survive the operation.

         */

        for (vector<CUDTSocket*>::iterator i = read_ready.begin(); i != read_ready.end(); ++i)
        {
            // NOT using the official srt_epoll_add_usock because this will do socket dispatching,
            // which requires lock on m_GlobControlLock, while this lock cannot be applied without
            // first unlocking m_GroupLock.
            CUDT::s_UDTUnited.epoll_add_usock_INTERNAL(m_RcvEID, *i, &read_modes);
        }

        HLOGC(grlog.Debug, log << "group/recv: " << ds.str() << " --> EPOLL/SWAIT");
#undef HCLOG

        if (!still_alive)
        {
            LOGC(grlog.Error, log << "group/recv: all links broken");
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        }

        // Here we need to make an additional check.
        // There might be a possibility that all sockets that
        // were added to the reader group, are ahead. At least
        // surely we don't have a situation that any link contains
        // an ahead-read subsequent packet, because GroupCheckPacketAhead
        // already handled that case.
        //
        // What we can have is that every link has:
        // - no known seq position yet (is not registered in the position map yet)
        // - the position equal to the latest delivered sequence
        // - the ahead position

        // Now the situation is that we don't have any packets
        // waiting for delivery so we need to wait for any to report one.

        // XXX We support blocking mode only at the moment.
        // The non-blocking mode would need to simply check the readiness
        // with only immediate report, and read-readiness would have to
        // be done in background.

        // Poll on this descriptor until reading is available, indefinitely.
        CEPoll::fmap_t sready;

        // In blocking mode, use m_iRcvTimeOut, which's default value -1
        // means to block indefinitely, also in swait().
        // In non-blocking mode use 0, which means to always return immediately.
        int timeout = m_bSynRecving ? m_iRcvTimeOut : 0;
        int nready = 0;

        // GlobControlLock is required for dispatching the sockets.
        // Therefore it must be applied only when GroupLock is off.
        {
            // This call may wait indefinite time, so GroupLock must be unlocked.
            InvertedLock ung (m_GroupLock);
            THREAD_PAUSED();
            nready  = m_pGlobal->m_EPoll.swait(*m_RcvEpolld, sready, timeout, false /*report by retval*/);
            THREAD_RESUMED();

            // HERE GlobControlLock is locked first, then GroupLock is applied back
            enterCS(CUDT::s_UDTUnited.m_GlobControlLock);
        }
        // BOTH m_GlobControlLock AND m_GroupLock are locked here.

        HLOGC(grlog.Debug, log << "group/recv: RDY: " << DisplayEpollResults(sready));

        if (nready == 0)
        {
            // GlobControlLock is applied manually, so unlock manually.
            // GroupLock will be unlocked as per scope.
            leaveCS(CUDT::s_UDTUnited.m_GlobControlLock);
            // This can only happen when 0 is passed as timeout and none is ready.
            // And 0 is passed only in non-blocking mode. So this is none ready in
            // non-blocking mode.
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }

        // Handle sockets of pending connection and with errors.

        // Nice to have something like:

        // broken = FilterIf(sready, [] (auto s)
        //                   { return s.second == SRT_EPOLL_ERR && (auto cs = g->locateSocket(s.first, ERH_RETURN))
        //                          ? {cs, true}
        //                          : {nullptr, false}
        //                   });

        FilterIf(
            /*FROM*/ sready.begin(),
            sready.end(),
            /*TO*/ std::inserter(broken, broken.begin()),
            /*VIA*/ FLookupSocketWithEvent_LOCKED(m_pGlobal, SRT_EPOLL_ERR));

        // Ok, now we need to have some extra qualifications:
        // 1. If a socket has no registry yet, we read anyway, just
        // to notify the current position. We read ONLY ONE PACKET this time,
        // we'll worry later about adjusting it to the current group sequence
        // position.
        // 2. If a socket is already position ahead, DO NOT read from it, even
        // if it is ready.

        // The state of things whether we were able to extract the very next
        // sequence will be simply defined by the fact that `output` is nonempty.

        int32_t next_seq = m_RcvBaseSeqNo;

        // If this set is empty, it won't roll even once, therefore output
        // will be surely empty. This will be checked then same way as when
        // reading from every socket resulted in error.

        vector<CUDTSocket*> ready_sockets;

        for (CEPoll::fmap_t::const_iterator i = sready.begin(); i != sready.end(); ++i)
        {
            if (i->second & SRT_EPOLL_ERR)
                continue; // broken already

            if ((i->second & SRT_EPOLL_IN) == 0)
                continue; // not ready for reading

            // Check if this socket is in aheads
            // If so, don't read from it, wait until the ahead is flushed.
            SRTSOCKET   id = i->first;
            CUDTSocket* ps = m_pGlobal->locateSocket_LOCKED(id);
            if (ps)
                ready_sockets.push_back(ps);
        }
        leaveCS(CUDT::s_UDTUnited.m_GlobControlLock);

        // m_GlobControlLock lifted, m_GroupLock still locked.
        //
        // NOTE: Although m_GlobControlLock is lifted here so potentially sockets
        // colected in ready_sockets could be closed at any time, all of them are member
        // sockets of this group. Therefore the first socket attempted to be closed will
        // have to remove the socket from the group, and this will require lock on GroupLock,
        // which is still applied here. So this will have to wait for this function to finish
        // (or block on swait, in which case the lock is lifted) anyway.

        for (vector<CUDTSocket*>::iterator si = ready_sockets.begin(); si != ready_sockets.end(); ++si)
        {
            CUDTSocket* ps = *si;
            SRTSOCKET id = ps->m_SocketID;
            ReadPos*    p  = NULL;
            pit_t       pe = m_Positions.find(id);
            if (pe != m_Positions.end())
            {
                p = &pe->second;

                // Possible results of comparison:
                // x < 0: the sequence is in the past, the socket should be adjusted FIRST
                // x = 0: the socket should be ready to get the exactly next packet
                // x = 1: the case is already handled by GroupCheckPacketAhead.
                // x > 1: AHEAD. DO NOT READ.
                int seqdiff = CSeqNo::seqcmp(p->mctrl.pktseq, m_RcvBaseSeqNo);
                if (seqdiff > 1)
                {
                    HLOGC(grlog.Debug,
                          log << "group/recv: EPOLL: @" << id << " %" << p->mctrl.pktseq << " AHEAD %" << m_RcvBaseSeqNo
                              << ", not reading.");
                    continue;
                }
            }
            else
            {
                // The position is not known, so get the position on which
                // the socket is currently standing.
                pair<pit_t, bool> ee = m_Positions.insert(make_pair(id, ReadPos(ps->core().m_iRcvLastSkipAck)));
                p                    = &(ee.first->second);
                HLOGC(grlog.Debug,
                      log << "group/recv: EPOLL: @" << id << " %" << p->mctrl.pktseq << " NEW SOCKET INSERTED");
            }

            // Read from this socket stubbornly, until:
            // - reading is no longer possible (AGAIN)
            // - the sequence difference is >= 1

            for (;;)
            {
                SRT_MSGCTRL mctrl = srt_msgctrl_default;

                // Read the data into the user's buffer. This is an optimistic
                // prediction that we'll read the right data. This will be overwritten
                // by "more correct data" if found more appropriate later. But we have to
                // copy these data anyway anywhere, even if they need to fall on the floor later.
                int stat;
                if (output_size)
                {
                    // We have already the data, so this must fall on the floor
                    char lostbuf[SRT_LIVE_MAX_PLSIZE];
                    stat = ps->core().receiveMessage((lostbuf), SRT_LIVE_MAX_PLSIZE, (mctrl), CUDTUnited::ERH_RETURN);
                    HLOGC(grlog.Debug,
                          log << "group/recv: @" << id << " IGNORED data with %" << mctrl.pktseq << " #" << mctrl.msgno
                              << ": " << (stat <= 0 ? "(NOTHING)" : BufferStamp(lostbuf, stat)));
                    if (stat > 0)
                    {
                        m_stats.recvDiscard.Update(stat);
                    }
                }
                else
                {
                    stat = ps->core().receiveMessage((buf), len, (mctrl), CUDTUnited::ERH_RETURN);
                    HLOGC(grlog.Debug,
                          log << "group/recv: @" << id << " EXTRACTED data with %" << mctrl.pktseq << " #"
                              << mctrl.msgno << ": " << (stat <= 0 ? "(NOTHING)" : BufferStamp(buf, stat)));
                }
                if (stat == 0)
                {
                    HLOGC(grlog.Debug, log << "group/recv: SPURIOUS epoll, ignoring");
                    // This is returned in case of "again". In case of errors, we have SRT_ERROR.
                    // Do not treat this as spurious, just stop reading.
                    break;
                }

                if (stat == SRT_ERROR)
                {
                    HLOGC(grlog.Debug, log << "group/recv: @" << id << ": " << srt_getlasterror_str());
                    broken.insert(ps);
                    break;
                }

                // NOTE: checks against m_RcvBaseSeqNo and decisions based on it
                // must NOT be done if m_RcvBaseSeqNo is SRT_SEQNO_NONE, which
                // means that we are about to deliver the very first packet and we
                // take its sequence number as a good deal.

                // The order must be:
                // - check discrepancy
                // - record the sequence
                // - check ordering.
                // The second one must be done always, but failed discrepancy
                // check should exclude the socket from any further checks.
                // That's why the common check for m_RcvBaseSeqNo != SRT_SEQNO_NONE can't
                // embrace everything below.

                // We need to first qualify the sequence, just for a case
                if (m_RcvBaseSeqNo != SRT_SEQNO_NONE && abs(m_RcvBaseSeqNo - mctrl.pktseq) > CSeqNo::m_iSeqNoTH)
                {
                    // This error should be returned if the link turns out
                    // to be the only one, or set to the group data.
                    // err = SRT_ESECFAIL;
                    LOGC(grlog.Error,
                         log << "group/recv: @" << id << ": SEQUENCE DISCREPANCY: base=%" << m_RcvBaseSeqNo
                             << " vs pkt=%" << mctrl.pktseq << ", setting ESECFAIL");
                    broken.insert(ps);
                    break;
                }

                // Rewrite it to the state for a case when next reading
                // would not succeed. Do not insert the buffer here because
                // this is only required when the sequence is ahead; for that
                // it will be fixed later.
                p->mctrl.pktseq = mctrl.pktseq;

                if (m_RcvBaseSeqNo != SRT_SEQNO_NONE)
                {
                    // Now we can safely check it.
                    int seqdiff = CSeqNo::seqcmp(mctrl.pktseq, m_RcvBaseSeqNo);

                    if (seqdiff <= 0)
                    {
                        HLOGC(grlog.Debug,
                              log << "group/recv: @" << id << " %" << mctrl.pktseq << " #" << mctrl.msgno
                                  << " BEHIND base=%" << m_RcvBaseSeqNo << " - discarding");
                        // The sequence is recorded, the packet has to be discarded.
                        // That's all.
                        continue;
                    }

                    // Now we have only two possibilities:
                    // seqdiff == 1: The very next sequence, we want to read and return the packet.
                    // seqdiff > 1: The packet is ahead - record the ahead packet, but continue with the others.

                    if (seqdiff > 1)
                    {
                        HLOGC(grlog.Debug,
                              log << "@" << id << " %" << mctrl.pktseq << " #" << mctrl.msgno << " AHEAD base=%"
                                  << m_RcvBaseSeqNo);
                        p->packet.assign(buf, buf + stat);
                        p->mctrl = mctrl;
                        break; // Don't read from that socket anymore.
                    }
                }

                // We have seqdiff = 1, or we simply have the very first packet
                // which's sequence is taken as a good deal. Update the sequence
                // and record output.

                if (output_size)
                {
                    HLOGC(grlog.Debug,
                          log << "group/recv: @" << id << " %" << mctrl.pktseq << " #" << mctrl.msgno << " REDUNDANT");
                    break;
                }

                HLOGC(grlog.Debug,
                      log << "group/recv: @" << id << " %" << mctrl.pktseq << " #" << mctrl.msgno << " DELIVERING");
                output_size = stat;
                fillGroupData((w_mc), mctrl);

                // Update stats as per delivery
                m_stats.recv.Update(output_size);
                updateAvgPayloadSize(output_size);

                // Record, but do not update yet, until all sockets are handled.
                next_seq = mctrl.pktseq;
                break;
            }
        }

#if ENABLE_HEAVY_LOGGING
        if (!broken.empty())
        {
            std::ostringstream brks;
            for (set<CUDTSocket*>::iterator b = broken.begin(); b != broken.end(); ++b)
                brks << "@" << (*b)->m_SocketID << " ";
            LOGC(grlog.Debug, log << "group/recv: REMOVING BROKEN: " << brks.str());
        }
#endif

        // Now remove all broken sockets from aheads, if any.
        // Even if they have already delivered a packet.
        for (set<CUDTSocket*>::iterator di = broken.begin(); di != broken.end(); ++di)
        {
            CUDTSocket* ps = *di;
            m_Positions.erase(ps->m_SocketID);
            ps->makeMemberClosed();
        }

        if (broken.size() >= size) // This > is for sanity check
        {
            // All broken
            HLOGC(grlog.Debug, log << "group/recv: All sockets broken");
            m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);

            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        // May be required to be re-read.
        broken.clear();

        if (output_size)
        {
            // We have extracted something, meaning that we have the sequence shift.
            // Update it now and don't do anything else with the sockets.

            // Sanity check
            if (next_seq == SRT_SEQNO_NONE)
            {
                LOGP(grlog.Error, "IPE: next_seq not set after output extracted!");

                // This should never happen, but the only way to keep the code
                // safe an recoverable is to use the incremented sequence. By
                // leaving the sequence as is there's a risk of hangup.
                // Not doing it in case of SRT_SEQNO_NONE as it would make a valid %0.
                if (m_RcvBaseSeqNo != SRT_SEQNO_NONE)
                    m_RcvBaseSeqNo = CSeqNo::incseq(m_RcvBaseSeqNo);
            }
            else
            {
                m_RcvBaseSeqNo = next_seq;
            }

            ReadPos* pos = checkPacketAhead();
            if (!pos)
            {
                // Don't clear the read-readinsess state if you have a packet ahead because
                // if you have, the next read call will return it.
                m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
            }

            HLOGC(grlog.Debug,
                  log << "group/recv: successfully extracted packet size=" << output_size << " - returning");
            return output_size;
        }

        HLOGC(grlog.Debug, log << "group/recv: NOT extracted anything - checking for a need to kick kangaroos");

        // Check if we have any sockets left :D

        // Here we surely don't have any more HORSES,
        // only ELEPHANTS and KANGAROOS. Qualify them and
        // attempt to at least take advantage of KANGAROOS.

        // In this position all links are either:
        // - updated to the current position
        // - updated to the newest possible possition available
        // - not yet ready for extraction (not present in the group)

        // If we haven't extracted the very next sequence position,
        // it means that we might only have the ahead packets read,
        // that is, the next sequence has been dropped by all links.

        if (!m_Positions.empty())
        {
            // This might notify both lingering links, which didn't
            // deliver the required sequence yet, and links that have
            // the sequence ahead. Review them, and if you find at
            // least one packet behind, just wait for it to be ready.
            // Use again the waiting function because we don't want
            // the general waiting procedure to skip others.
            set<SRTSOCKET> elephants;

            // const because it's `typename decltype(m_Positions)::value_type`
            pair<const SRTSOCKET, ReadPos>* slowest_kangaroo = 0;

            for (pit_t rp = m_Positions.begin(); rp != m_Positions.end(); ++rp)
            {
                // NOTE that m_RcvBaseSeqNo in this place wasn't updated
                // because we haven't successfully extracted anything.
                int seqdiff = CSeqNo::seqcmp(rp->second.mctrl.pktseq, m_RcvBaseSeqNo);
                if (seqdiff < 0)
                {
                    elephants.insert(rp->first);
                }
                // If seqdiff == 0, we have a socket ON TRACK.
                else if (seqdiff > 0)
                {
                    // If there's already a slowest_kangaroo, seqdiff decides if this one is slower.
                    // Otherwise it is always slower by having no competition.
                    seqdiff = slowest_kangaroo
                                  ? CSeqNo::seqcmp(slowest_kangaroo->second.mctrl.pktseq, rp->second.mctrl.pktseq)
                                  : 1;
                    if (seqdiff > 0)
                    {
                        slowest_kangaroo = &*rp;
                    }
                }
            }

            // Note that if no "slowest_kangaroo" was found, it means
            // that we don't have kangaroos.
            if (slowest_kangaroo)
            {
                // We have a slowest kangaroo. Elephants must be ignored.
                // Best case, they will get revived, worst case they will be
                // soon broken.
                //
                // As we already have the packet delivered by the slowest
                // kangaroo, we can simply return it.

                // Check how many were skipped and add them to the stats
                const int32_t jump = (CSeqNo(slowest_kangaroo->second.mctrl.pktseq) - CSeqNo(m_RcvBaseSeqNo)) - 1;
                if (jump > 0)
                {
                    m_stats.recvDrop.UpdateTimes(jump, avgRcvPacketSize());
                }

                m_RcvBaseSeqNo    = slowest_kangaroo->second.mctrl.pktseq;
                vector<char>& pkt = slowest_kangaroo->second.packet;
                if (size_t(len) < pkt.size())
                    throw CUDTException(MJ_NOTSUP, MN_XSIZE, 0);

                HLOGC(grlog.Debug,
                      log << "@" << slowest_kangaroo->first << " KANGAROO->HORSE %"
                          << slowest_kangaroo->second.mctrl.pktseq << " #" << slowest_kangaroo->second.mctrl.msgno
                          << ": " << BufferStamp(&pkt[0], pkt.size()));

                memcpy(buf, &pkt[0], pkt.size());
                fillGroupData((w_mc), slowest_kangaroo->second.mctrl);
                len = pkt.size();
                pkt.clear();

                // Update stats as per delivery
                m_stats.recv.Update(len);
                updateAvgPayloadSize(len);

                // It is unlikely to have a packet ahead because usually having one packet jumped-ahead
                // clears the possibility of having aheads at all.
                // XXX Research if this is possible at all; if it isn't, then don't waste time on
                // looking for it.
                ReadPos* pos = checkPacketAhead();
                if (!pos)
                {
                    // Don't clear the read-readinsess state if you have a packet ahead because
                    // if you have, the next read call will return it.
                    m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
                }
                return len;
            }

            HLOGC(grlog.Debug,
                  log << "group/recv: "
                      << (elephants.empty() ? "NO LINKS REPORTED ANY FRESHER PACKET." : "ALL LINKS ELEPHANTS.")
                      << " Re-polling.");
        }
        else
        {
            HLOGC(grlog.Debug, log << "group/recv: POSITIONS EMPTY - Re-polling.");
        }
    }
}

// [[using locked(m_GroupLock)]]
CUDTGroup::ReadPos* CUDTGroup::checkPacketAhead()
{
    typedef map<SRTSOCKET, ReadPos>::iterator pit_t;
    ReadPos*                                  out = 0;

    // This map no longer maps only ahead links.
    // Here are all links, and whether ahead, it's defined by the sequence.
    for (pit_t i = m_Positions.begin(); i != m_Positions.end(); ++i)
    {
        // i->first: socket ID
        // i->second: ReadPos { sequence, packet }
        // We are not interested with the socket ID because we
        // aren't going to read from it - we have the packet already.
        ReadPos& a = i->second;

        const int seqdiff = CSeqNo::seqcmp(a.mctrl.pktseq, m_RcvBaseSeqNo);
        if (seqdiff == 1)
        {
            // The very next packet. Return it.
            // XXX SETTING THIS ONE IS PROBABLY A BUG.
            m_RcvBaseSeqNo = a.mctrl.pktseq;
            HLOGC(grlog.Debug,
                  log << "group/recv: Base %" << m_RcvBaseSeqNo << " ahead delivery POSSIBLE %" << a.mctrl.pktseq << "#"
                      << a.mctrl.msgno << " from @" << i->first << ")");
            out = &a;
        }
        else if (seqdiff < 1 && !a.packet.empty())
        {
            HLOGC(grlog.Debug,
                  log << "group/recv: @" << i->first << " dropping collected ahead %" << a.mctrl.pktseq << "#"
                      << a.mctrl.msgno << " with base %" << m_RcvBaseSeqNo);
            a.packet.clear();
        }
        // In case when it's >1, keep it in ahead
    }

    return out;
}

const char* CUDTGroup::StateStr(CUDTGroup::GroupState st)
{
    static const char* const states[] = {"PENDING", "IDLE", "RUNNING", "BROKEN"};
    static const size_t      size     = Size(states);
    static const char* const unknown  = "UNKNOWN";
    if (size_t(st) < size)
        return states[st];
    return unknown;
}

void CUDTGroup::synchronizeDrift(CUDT* cu, steady_clock::duration udrift, steady_clock::time_point newtimebase)
{
    ScopedLock glock(m_GroupLock);

    bool wrap_period = false;

    bool anycheck = false;

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        // Skip non-connected; these will be synchronized when ready
        if (gi->laststatus != SRTS_CONNECTED)
            continue;

        // Skip the entity that has reported this
        if (cu == gi->ps->m_pUDT)
            continue;

        steady_clock::time_point this_timebase;
        steady_clock::duration   this_udrift(0);
        bool wrp = gi->ps->m_pUDT->m_pRcvBuffer->getInternalTimeBase((this_timebase), (this_udrift));

        udrift                                   = std::min(udrift, this_udrift);
        steady_clock::time_point new_newtimebase = std::min(newtimebase, this_timebase);
        if (new_newtimebase != newtimebase)
        {
            wrap_period = wrp;
        }
        newtimebase = new_newtimebase;
        anycheck    = true;
    }

    if (!anycheck)
    {
        HLOGC(grlog.Debug, log << "GROUP: synch uDRIFT NOT DONE, no other links");
        return;
    }

    HLOGC(grlog.Debug,
          log << "GROUP: synch uDRIFT=" << FormatDuration(udrift) << " TB=" << FormatTime(newtimebase) << "("
              << (wrap_period ? "" : "NO ") << "wrap period)");

    // Now that we have the minimum timebase and drift calculated, apply this to every link,
    // INCLUDING THE REPORTER.

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        // Skip non-connected; these will be synchronized when ready
        if (gi->laststatus != SRTS_CONNECTED)
            continue;

        gi->ps->m_pUDT->m_pRcvBuffer->applyGroupDrift(newtimebase, wrap_period, udrift);
    }
}

void CUDTGroup::bstatsSocket(CBytePerfMon* perf, bool clear)
{
    if (!m_bConnected)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    const steady_clock::time_point currtime = steady_clock::now();

    memset(perf, 0, sizeof *perf);

    ScopedLock gg(m_GroupLock);

    perf->msTimeStamp = count_milliseconds(currtime - m_tsStartTime);

    perf->pktSentUnique = m_stats.sent.local.pkts;
    perf->pktRecvUnique = m_stats.recv.local.pkts;
    perf->pktRcvDrop    = m_stats.recvDrop.local.pkts;

    perf->byteSentUnique = m_stats.sent.local.fullBytes();
    perf->byteRecvUnique = m_stats.recv.local.fullBytes();
    perf->byteRcvDrop    = m_stats.recvDrop.local.fullBytes();

    perf->pktSentUniqueTotal = m_stats.sent.total.pkts;
    perf->pktRecvUniqueTotal = m_stats.recv.total.pkts;
    perf->pktRcvDropTotal    = m_stats.recvDrop.total.pkts;

    perf->byteSentUniqueTotal = m_stats.sent.total.fullBytes();
    perf->byteRecvUniqueTotal = m_stats.recv.total.fullBytes();
    perf->byteRcvDropTotal    = m_stats.recvDrop.total.fullBytes();

    const double interval = static_cast<double>(count_microseconds(currtime - m_stats.tsLastSampleTime));
    perf->mbpsSendRate    = double(perf->byteSent) * 8.0 / interval;
    perf->mbpsRecvRate    = double(perf->byteRecv) * 8.0 / interval;

    if (clear)
    {
        m_stats.reset();
    }
}

// For sorting group members by priority

struct FPriorityOrder
{
    // returned true = "elements are in the right order"
    static bool check(uint16_t preceding, uint16_t succeeding)
    {
        return preceding > succeeding;
    }

    typedef CUDTGroup::gli_t gli_t;

    bool operator()(gli_t preceding, gli_t succeeding)
    {
        return check(preceding->weight, succeeding->weight);
    }
};

// [[using maybe_locked(this->m_GroupLock)]]
bool CUDTGroup::send_CheckIdle(const gli_t d, vector<SRTSOCKET>& w_wipeme, vector<SRTSOCKET>& w_pending)
{
    SRT_SOCKSTATUS st = SRTS_NONEXIST;
    if (d->ps)
        st = d->ps->getStatus();
    // If the socket is already broken, move it to broken.
    if (int(st) >= int(SRTS_BROKEN))
    {
        HLOGC(gslog.Debug,
              log << "CUDTGroup::send.$" << id() << ": @" << d->id << " became " << SockStatusStr(st)
                  << ", WILL BE CLOSED.");
        w_wipeme.push_back(d->id);
        return false;
    }

    if (st != SRTS_CONNECTED)
    {
        HLOGC(gslog.Debug, log << "CUDTGroup::send. @" << d->id << " is still " << SockStatusStr(st) << ", skipping.");
        w_pending.push_back(d->id);
        return false;
    }

    return true;
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_CheckIdleTime(gli_t w_d)
{
    // Check if it was fresh set as idle, we had to wait until its sender
    // buffer gets empty so that we can make sure that KEEPALIVE will be the
    // really last sent for longer time.
    CUDT& u = w_d->ps->core();
    if (!is_zero(u.m_tsTmpActiveTime))
    {
        CSndBuffer* b = u.m_pSndBuffer;
        if (b && b->getCurrBufSize() == 0)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBackup: FRESH IDLE LINK reached empty buffer - setting permanent and KEEPALIVE");
            u.m_tsTmpActiveTime = steady_clock::time_point();

            // Send first immediate keepalive. The link is to be turn to IDLE
            // now so nothing will be sent to it over time and it will start
            // getting KEEPALIVES since now. Send the first one now to increase
            // probability that the link will be recognized as IDLE on the
            // reception side ASAP.
            int32_t arg = 1;
            w_d->ps->m_pUDT->sendCtrl(UMSG_KEEPALIVE, &arg);
        }
    }
}

// [[using locked(this->m_GroupLock)]]
bool CUDTGroup::sendBackup_CheckRunningStability(const gli_t d, const time_point currtime)
{
    CUDT& u = d->ps->core();
    // This link might be unstable, check its responsiveness status
    // NOTE: currtime - last_rsp_time: we believe this value will be always positive as
    // the Tk clock is believed to be monotonic. The resulting value

    // IMPORTANT: the socket could be potentially updated IN THE MEANTIME in another
    // thread AFTER (!!!) currtime has been read, but BEFORE (!!!) this value us used
    // for calculation - which could make the difference negative.
    // There's no way to avoid it because it would require making a mutex-locking for
    // updating the m_tsLastRspTime field. This is useless because avoiding the
    // negative value is relatively easy, while introducing a mutex would only add a
    // deadlock risk and performance degradation.

    bool is_unstable = false;

    HLOGC(gslog.Debug,
          log << "grp/sendBackup: CHECK STABLE: @" << d->id
              << ": TIMEDIFF {response= " << FormatDuration<DUNIT_MS>(currtime - u.m_tsLastRspTime)
              << " ACK=" << FormatDuration<DUNIT_MS>(currtime - u.m_tsLastRspAckTime) << " activation="
              << (!is_zero(u.m_tsTmpActiveTime) ? FormatDuration<DUNIT_MS>(currtime - u.m_tsTmpActiveTime) : "PAST")
              << " unstable="
              << (!is_zero(u.m_tsUnstableSince) ? FormatDuration<DUNIT_MS>(currtime - u.m_tsUnstableSince) : "NEVER")
              << "}");

    if (currtime > u.m_tsLastRspTime)
    {
        // The last response predates the start of this function, look at the difference
        steady_clock::duration td_responsive = currtime - u.m_tsLastRspTime;

        IF_HEAVY_LOGGING(string source = "heard");

        bool check_stability = true;

        if (!is_zero(u.m_tsTmpActiveTime) && u.m_tsTmpActiveTime < currtime)
        {
            // The link is temporary-activated. Calculate then since the activation time.

            // Check the last received ACK time first. This time is initialized with 'now'
            // at the CUDT::open call, so you can't count on the trap zero time here, but
            // it's still possible to check if activation time predates the ACK time. Things
            // are here in the following possible order:
            //
            // - ACK time (old because defined at open)
            // - Response time (old because the time of received handshake or keepalive counts)
            // ... long time nothing ...
            // - Activation time.
            //
            // If we have this situation, we have to wait for at least one ACK that is
            // newer than activation time. However, if in this situation we have a fresh
            // response, that is:
            //
            // - ACK time
            // ...
            // - Activation time
            // - Response time (because a Keepalive had a caprice to come accidentally after sending)
            //
            // We still wait for a situation that there's at least one ACK that is newer than activation.

            // As we DO have activation time, we need to check if there's at least
            // one ACK newer than activation, that is, td_acked < td_active
            if (u.m_tsLastRspAckTime < u.m_tsTmpActiveTime)
            {
                check_stability = false;
                HLOGC(gslog.Debug,
                      log << "grp/sendBackup: link @" << d->id
                          << " activated after ACK, "
                             "not checking for stability");
            }
            else
            {
                u.m_tsTmpActiveTime = steady_clock::time_point();
            }
        }

        if (check_stability && count_microseconds(td_responsive) > m_uOPT_StabilityTimeout)
        {
            if (is_zero(u.m_tsUnstableSince))
            {
                HLOGC(gslog.Debug,
                      log << "grp/sendBackup: socket NEW UNSTABLE: @" << d->id << " last " << source << " "
                          << FormatDuration(td_responsive) << " > " << m_uOPT_StabilityTimeout
                          << " (stability timeout)");
                // The link seems to have missed two ACKs already.
                // Qualify this link as unstable
                // Notify that it has been seen so since now
                u.m_tsUnstableSince = currtime;
            }

            is_unstable = true;
        }
    }

    if (!is_unstable)
    {
        // If stability is ok, but unstable-since was set before, reset it.
        HLOGC(gslog.Debug,
              log << "grp/sendBackup: link STABLE: @" << d->id
                  << (!is_zero(u.m_tsUnstableSince) ? " - RESTORED" : " - CONTINUED"));

        u.m_tsUnstableSince = steady_clock::time_point();
        is_unstable         = false;
    }

#if ENABLE_HEAVY_LOGGING
    // Could be set above
    if (is_unstable)
    {
        HLOGC(gslog.Debug,
              log << "grp/sendBackup: link UNSTABLE for " << FormatDuration(currtime - u.m_tsUnstableSince) << " : @"
                  << d->id << " - will send a payload");
    }
    else
    {
        HLOGC(gslog.Debug, log << "grp/sendBackup: socket in RUNNING state: @" << d->id << " - will send a payload");
    }
#endif

    return !is_unstable;
}

// [[using locked(this->m_GroupLock)]]
bool CUDTGroup::sendBackup_CheckSendStatus(gli_t                                    d,
                                           const steady_clock::time_point& currtime ATR_UNUSED,
                                           const int                                stat,
                                           const int                                erc,
                                           const int32_t                            lastseq,
                                           const int32_t                            pktseq,
                                           CUDT&                                    w_u,
                                           int32_t&                                 w_curseq,
                                           vector<gli_t>&                           w_parallel,
                                           int&                                     w_final_stat,
                                           set<uint16_t>&                           w_sendable_pri,
                                           size_t&                                  w_nsuccessful,
                                           bool&                                    w_is_nunstable)
{
    bool none_succeeded = true;

    if (stat != -1)
    {
        if (w_curseq == SRT_SEQNO_NONE)
        {
            w_curseq = pktseq;
        }
        else if (w_curseq != lastseq)
        {
            // We believe that all running links use the same seq.
            // But we can do some sanity check.
            LOGC(gslog.Error,
                 log << "grp/sendBackup: @" << w_u.m_SocketID << ": IPE: another running link seq discrepancy: %"
                     << lastseq << " vs. previous %" << w_curseq << " - fixing");

            // Override must be done with a sequence number greater by one.

            // Example:
            //
            // Link 1 before sending: curr=1114, next=1115
            // After sending it reports pktseq=1115
            //
            // Link 2 before sending: curr=1110, next=1111 (->lastseq before sending)
            // THIS CHECK done after sending:
            //  -- w_curseq(1115) != lastseq(1111)
            //
            // NOW: Link 1 after sending is:
            // curr=1115, next=1116
            //
            // The value of w_curseq here = 1115, while overrideSndSeqNo
            // calls setInitialSndSeq(seq), which sets:
            // - curr = seq - 1
            // - next = seq
            //
            // So, in order to set curr=1115, next=1116
            // this must set to 1115+1.

            w_u.overrideSndSeqNo(CSeqNo::incseq(w_curseq));
        }

        // If this link is already found as unstable,
        // do not add it to the "w_parallel", as all links out
        // of these "w_parallels" will be later tried to be
        // shrunk to 1. Out of all links currently running we need
        // only 1 link STABLE, and we allow any nymber of unstable
        // links.

        if (is_zero(w_u.m_tsUnstableSince))
        {
            w_parallel.push_back(d);
        }
        else
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBackup: Link @" << w_u.m_SocketID << " still UNSTABLE for "
                      << FormatDuration(currtime - w_u.m_tsUnstableSince) << ", not counting as w_parallel");
        }

        // State it as succeeded, though. We don't know if the link
        // is broken until we get the connection broken confirmation,
        // and the instability state may wear off next time.
        none_succeeded = false;
        w_final_stat   = stat;
        ++w_nsuccessful;
        w_sendable_pri.insert(d->weight);
    }
    else if (erc == SRT_EASYNCSND)
    {
        HLOGC(gslog.Debug, log << "grp/sendBackup: Link @" << w_u.m_SocketID << " DEEMED UNSTABLE (not ready to send)");
        w_is_nunstable = true;
    }

    return none_succeeded;
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_Buffering(const char* buf, const int len, int32_t& w_curseq, SRT_MSGCTRL& w_mc)
{
    // This is required to rewrite into currentSchedSequence() property
    // as this value will be used as ISN when a new link is connected.
    int32_t oldest_buffer_seq = SRT_SEQNO_NONE;

    if (w_curseq != SRT_SEQNO_NONE)
    {
        HLOGC(gslog.Debug, log << "grp/sendBackup: successfully sent over running link, ADDING TO BUFFER.");

        // Note: the sequence number that was used to send this packet should be
        // recorded here.
        oldest_buffer_seq = addMessageToBuffer(buf, len, (w_mc));
    }
    else
    {
        // We have to predict, which sequence number would have
        // to be placed on the packet about to be sent now. To
        // maintain consistency:

        // 1. If there are any packets in the sender buffer,
        //    get the sequence of the last packet, increase it.
        //    This must be done even if this contradicts the ISN
        //    of all idle links because otherwise packets will get
        //    discrepancy.
        if (!m_SenderBuffer.empty())
        {
            BufferedMessage& m = m_SenderBuffer.back();
            w_curseq           = CSeqNo::incseq(m.mc.pktseq);

            // Set also this sequence to the current w_mc
            w_mc.pktseq = w_curseq;

            // XXX may need tighter revision when message mode is allowed
            w_mc.msgno        = ++MsgNo(m.mc.msgno);
            oldest_buffer_seq = addMessageToBuffer(buf, len, (w_mc));
        }

        // Note that if buffer is empty and w_curseq is (still) SRT_SEQNO_NONE,
        // it will have to try to send first in order to extract the data.

        // Note that if w_curseq is still SRT_SEQNO_NONE at this point, it means
        // that we have the case of the very first packet sending.
        // Otherwise there would be something in the buffer already.
    }

    if (oldest_buffer_seq != SRT_SEQNO_NONE)
        m_iLastSchedSeqNo = oldest_buffer_seq;
}

// [[using locked(this->m_GroupLock)]]
size_t CUDTGroup::sendBackup_CheckNeedActivate(const vector<gli_t>&        idlers,
                                             const char*                   buf,
                                             const int                     len,
                                             bool&                         w_none_succeeded,
                                             SRT_MSGCTRL&                  w_mc,
                                             int32_t&                      w_curseq,
                                             int32_t&                      w_final_stat,
                                             CUDTException&                w_cx,
                                             vector<Sendstate>&            w_sendstates,
                                             vector<gli_t>&                w_parallel,
                                             vector<SRTSOCKET>&            w_wipeme,
                                             const string& activate_reason ATR_UNUSED)
{
    int stat = -1;

    // If we have no stable links, activate one of idle links.

    HLOGC(gslog.Debug,
          log << "grp/sendBackup: " << activate_reason << ", trying to activate an idle link (" << idlers.size()
              << " available)");

    size_t nactive = 0;

    for (vector<gli_t>::const_iterator i = idlers.begin(); i != idlers.end(); ++i)
    {
        int   erc = 0;
        gli_t d   = *i;
        // Now send and check the status
        // The link could have got broken

        try
        {
            if (w_curseq == SRT_SEQNO_NONE)
            {
                // This marks the fact that the given here packet
                // could not be sent over any link. This includes the
                // situation of sending the very first packet after connection.

                HLOGC(gslog.Debug,
                      log << "grp/sendBackup: ... trying @" << d->id << " - sending the VERY FIRST message");

                stat = d->ps->core().sendmsg2(buf, len, (w_mc));
                if (stat != -1)
                {
                    // This will be no longer used, but let it stay here.
                    // It's because if this is successful, no other links
                    // will be tried.
                    w_curseq = w_mc.pktseq;
                    addMessageToBuffer(buf, len, (w_mc));
                }
            }
            else
            {
                HLOGC(gslog.Debug,
                      log << "grp/sendBackup: ... trying @" << d->id << " - resending " << m_SenderBuffer.size()
                          << " collected messages...");
                // Note: this will set the currently required packet
                // because it has been just freshly added to the sender buffer
                stat = sendBackupRexmit(d->ps->core(), (w_mc));
            }
            ++nactive;
        }
        catch (CUDTException& e)
        {
            // This will be propagated from internal sendmsg2 call,
            // but that's ok - we want this sending interrupted even in half.
            w_cx = e;
            stat = -1;
            erc  = e.getErrorCode();
        }

        d->sndresult  = stat;
        d->laststatus = d->ps->getStatus();

        const Sendstate cstate = {d->id, d, stat, erc};
        w_sendstates.push_back(cstate);

        if (stat != -1)
        {
            if (d->sndstate != SRT_GST_RUNNING)
            {
                steady_clock::time_point currtime = steady_clock::now();
                d->ps->core().m_tsTmpActiveTime   = currtime;
                HLOGC(gslog.Debug,
                      log << "@" << d->id << ":... sending SUCCESSFUL #" << w_mc.msgno
                          << " LINK ACTIVATED (pri: " << d->weight << ").");
            }
            else
            {
                LOGC(gslog.Warn,
                     log << "@" << d->id << ":... sending SUCCESSFUL #" << w_mc.msgno
                         << " LINK ACTIVATED (pri: " << d->weight << ").");
            }
            // Note: this will override the sequence number
            // for all next iterations in this loop.
            d->sndstate = SRT_GST_RUNNING;

            if (is_zero(d->ps->core().m_tsUnstableSince))
            {
                w_parallel.push_back(d);
            }
            else
            {
                HLOGC(gslog.Debug,
                      log << "grp/sendBackup: Link @" << d->id << " (idle) UNSTABLE, not counting as parallel");
            }
            w_none_succeeded = false;
            w_final_stat     = stat;

            // We've activated the link, so that's enough.
            break;
        }

        // Failure - move to broken those that could not be activated
        bool isblocked SRT_ATR_UNUSED = true;
        if (erc != SRT_EASYNCSND)
        {
            isblocked = false;
            w_wipeme.push_back(d->id);
        }

        // If we found a blocked link, leave it alone, however
        // still try to send something over another link

        HLOGC(gslog.Debug,
              log << "@" << d->id << " FAILED (" << (isblocked ? "blocked" : "ERROR")
                  << "), trying to activate another link.");
    }

    return nactive;
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::send_CheckPendingSockets(const vector<SRTSOCKET>& pending, vector<SRTSOCKET>& w_wipeme)
{
    // If we have at least one stable link, then select a link that have the
    // highest priority and silence the rest.

    // Note: If we have one stable link, this is the situation we need.
    // If we have no stable links at all, there's nothing we can do anyway.
    // The freshly activated previously idle links don't count because we
    // just started them and we can't determine their stability. At least if
    // we have one link that is stable and the freshly activated link is actually
    // stable too, we'll check this next time.
    //
    if (!pending.empty())
    {
        HLOGC(gslog.Debug, log << "grp/send*: found pending sockets, polling them.");

        // These sockets if they are in pending state, they should be added to m_SndEID
        // at the connecting stage.
        CEPoll::fmap_t sready;

        if (m_SndEpolld->watch_empty())
        {
            // Sanity check - weird pending reported.
            LOGC(gslog.Error, log << "grp/send*: IPE: reported pending sockets, but EID is empty - wiping pending!");
            copy(pending.begin(), pending.end(), back_inserter(w_wipeme));
        }
        else
        {
            // Some sockets could have been closed in the meantime.
            if (m_SndEpolld->watch_empty())
                throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

            {
                InvertedLock ug(m_GroupLock);
                m_pGlobal->m_EPoll.swait(
                    *m_SndEpolld, sready, 0, false /*report by retval*/); // Just check if anything happened
            }

            HLOGC(gslog.Debug, log << "grp/send*: RDY: " << DisplayEpollResults(sready));

            // sockets in EX: should be moved to w_wipeme.
            for (vector<SRTSOCKET>::const_iterator i = pending.begin(); i != pending.end(); ++i)
            {
                if (CEPoll::isready(sready, *i, SRT_EPOLL_ERR))
                {
                    HLOGC(gslog.Debug, log << "grp/send*: Socket @" << (*i) << " reported FAILURE - moved to wiped.");
                    // Failed socket. Move d to w_wipeme. Remove from eid.
                    w_wipeme.push_back(*i);
                    int no_events = 0;
                    m_pGlobal->m_EPoll.update_usock(m_SndEID, *i, &no_events);
                }
            }

            // After that, all sockets that have been reported
            // as ready to write should be removed from EID. This
            // will also remove those sockets that have been added
            // as redundant links at the connecting stage and became
            // writable (connected) before this function had a chance
            // to check them.
            m_pGlobal->m_EPoll.clear_ready_usocks(*m_SndEpolld, SRT_EPOLL_OUT);
        }
    }
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::send_CloseBrokenSockets(vector<SRTSOCKET>& w_wipeme)
{
    if (!w_wipeme.empty())
    {
        InvertedLock ug(m_GroupLock);

        // With unlocked GroupLock, we can now lock GlobControlLock.
        // This is a requirement for makeClosed.
        ScopedLock globlock(CUDT::s_UDTUnited.m_GlobControlLock);

        for (vector<SRTSOCKET>::iterator p = w_wipeme.begin(); p != w_wipeme.end(); ++p)
        {
            CUDTSocket* s = CUDT::s_UDTUnited.locateSocket_LOCKED(*p);

            // If the socket has been just moved to ClosedSockets, it means that
            // the object still exists, but it will be no longer findable.
            if (!s)
                continue;

            HLOGC(gslog.Debug,
                  log << "grp/sendBroadcast: BROKEN SOCKET @" << (*p) << " - CLOSING AND REMOVING.");

            s->makeClosed(); // <-- will use GroupLock to delete the socket from the group container
        }
    }

    HLOGC(gslog.Debug, log << "grp/sendBackup: - wiped " << w_wipeme.size() << " broken sockets");

    // We'll need you again.
    w_wipeme.clear();
}

struct FByOldestActive
{
    typedef CUDTGroup::gli_t gli_t;
    bool operator()(gli_t a, gli_t b)
    {
        CUDT& x = a->ps->core();
        CUDT& y = b->ps->core();

        return x.m_tsTmpActiveTime < y.m_tsTmpActiveTime;
    }
};

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_CheckParallelLinks(const vector<gli_t>& unstable,
                                              vector<gli_t>&       w_parallel,
                                              int&                 w_final_stat,
                                              bool&                w_none_succeeded,
                                              SRT_MSGCTRL&         w_mc,
                                              CUDTException&       w_cx)
{
    // In contradiction to broadcast sending, backup sending must check
    // the blocking state in total first. We need this information through
    // epoll because we didn't use all sockets to send the data hence the
    // blocked socket information would not be complete.

    // Don't do this check if sending has succeeded over at least one
    // stable link. This procedure is to wait for at least one write-ready
    // link.
    //
    // If sending succeeded also over at least one unstable link (you only have
    // unstable links and none other or others just got broken), continue sending
    // anyway.

#if ENABLE_HEAVY_LOGGING
    // Potential problem to be checked in developer mode
    for (vector<gli_t>::iterator p = w_parallel.begin(); p != w_parallel.end(); ++p)
    {
        if (std::find(unstable.begin(), unstable.end(), *p) != unstable.end())
        {
            LOGC(gslog.Debug,
                    log << "grp/sendBackup: IPE: parallel links enclose unstable link @" << (*p)->ps->m_SocketID);
        }
    }
#endif

    // This procedure is for a case when the packet could not be sent
    // over any link (hence "none succeeded"), but there are some unstable
    // links and no parallel links. We need to WAIT for any of the links
    // to become available for sending.
    if (w_parallel.empty() && !unstable.empty() && w_none_succeeded)
    {
        HLOGC(gslog.Debug, log << "grp/sendBackup: no parallel links and "
                << unstable.size() << " unstable links - checking...");

        // Note: GroupLock is set already, skip locks and checks
        getGroupData_LOCKED((w_mc.grpdata), (&w_mc.grpdata_size));
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);

        if (m_SndEpolld->watch_empty())
        {
            // wipeme wiped, pending sockets checked, it can only mean that
            // all sockets are broken.
            HLOGC(gslog.Debug, log << "grp/sendBackup: epolld empty - all sockets broken?");
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        if (!m_bSynSending)
        {
            HLOGC(gslog.Debug, log << "grp/sendBackup: non-blocking mode - exit with no-write-ready");
            throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);
        }
        // Here is the situation that the only links left here are:
        // - those that failed to send (already closed and wiped out)
        // - those that got blockade on sending

        // At least, there was so far no socket through which we could
        // successfully send anything.

        // As a last resort in this situation, try to wait for any links
        // remaining in the group to become ready to write.

        CEPoll::fmap_t sready;
        int            brdy;

        // This keeps the number of links that existed at the entry.
        // Simply notify all dead links, regardless as to whether the number
        // of group members decreases below. If the number of corpses reaches
        // this number, consider the group connection broken.
        size_t nlinks = m_Group.size();
        size_t ndead  = 0;

RetryWaitBlocked:
        {
            // Some sockets could have been closed in the meantime.
            if (m_SndEpolld->watch_empty())
            {
                HLOGC(gslog.Debug, log << "grp/sendBackup: no more sendable sockets - group broken");
                throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
            }

            InvertedLock ug(m_GroupLock);
            HLOGC(gslog.Debug,
                    log << "grp/sendBackup: swait call to get at least one link alive up to " << m_iSndTimeOut << "us");
            THREAD_PAUSED();
            brdy = m_pGlobal->m_EPoll.swait(*m_SndEpolld, (sready), m_iSndTimeOut);
            THREAD_RESUMED();

            if (brdy == 0) // SND timeout exceeded
            {
                throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);
            }

            HLOGC(gslog.Debug, log << "grp/sendBackup: swait exited with " << brdy << " ready sockets:");

            // Check if there's anything in the "error" section.
            // This must be cleared here before the lock on group is set again.
            // (This loop will not fire neither once if no failed sockets found).
            for (CEPoll::fmap_t::const_iterator i = sready.begin(); i != sready.end(); ++i)
            {
                if (i->second & SRT_EPOLL_ERR)
                {
                    SRTSOCKET   id = i->first;
                    CUDTSocket* s  = m_pGlobal->locateSocket(id, CUDTUnited::ERH_RETURN); // << LOCKS m_GlobControlLock!
                    if (s)
                    {
                        HLOGC(gslog.Debug,
                                log << "grp/sendBackup: swait/ex on @" << (id)
                                << " while waiting for any writable socket - CLOSING");
                        CUDT::s_UDTUnited.close(s); // << LOCKS m_GlobControlLock, then GroupLock!
                    }
                    else
                    {
                        HLOGC(gslog.Debug, log << "grp/sendBackup: swait/ex on @" << (id) << " - WAS DELETED IN THE MEANTIME");
                    }

                    ++ndead;
                }
            }
            HLOGC(gslog.Debug, log << "grp/sendBackup: swait/?close done, re-acquiring GroupLock");
        }

        // GroupLock is locked back

        // Re-check after the waiting lock has been reacquired
        if (m_bClosing)
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

        if (brdy == -1 || ndead >= nlinks)
        {
            LOGC(gslog.Error,
                    log << "grp/sendBackup: swait=>" << brdy << " nlinks=" << nlinks << " ndead=" << ndead
                    << " - looxlike all links broken");
            m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
            m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
            // You can safely throw here - nothing to fill in when all sockets down.
            // (timeout was reported by exception in the swait call).
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        // Ok, now check if we have at least one write-ready.
        // Note that the procedure of activation of a new link in case of
        // no stable links found embraces also rexmit-sending and status
        // check as well, including blocked status.

        // Find which one it was. This is so rare case that we can
        // suffer linear search.

        int nwaiting = 0;
        int nactivated ATR_UNUSED = 0;
        int stat     = -1;
        for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
        {
            // int erc = 0;

            // Skip if not writable in this run
            if (!CEPoll::isready(sready, d->id, SRT_EPOLL_OUT))
            {
                ++nwaiting;
                HLOGC(gslog.Debug, log << "grp/sendBackup: @" << d->id << " NOT ready:OUT, added as waiting");
                continue;
            }

            if (d->sndstate == SRT_GST_RUNNING)
            {
                HLOGC(gslog.Debug,
                        log << "grp/sendBackup: link @" << d->id << " RUNNING - SKIPPING from activate and resend");
                continue;
            }

            try
            {
                // Note: this will set the currently required packet
                // because it has been just freshly added to the sender buffer
                stat = sendBackupRexmit(d->ps->core(), (w_mc));
                ++nactivated;
            }
            catch (CUDTException& e)
            {
                // This will be propagated from internal sendmsg2 call,
                // but that's ok - we want this sending interrupted even in half.
                w_cx = e;
                stat = -1;
                // erc = e.getErrorCode();
            }

            d->sndresult  = stat;
            d->laststatus = d->ps->getStatus();

            if (stat == -1)
            {
                // This link is no longer waiting.
                continue;
            }

            w_parallel.push_back(d);
            w_final_stat                      = stat;
            steady_clock::time_point currtime = steady_clock::now();
            d->ps->core().m_tsTmpActiveTime   = currtime;
            d->sndstate                       = SRT_GST_RUNNING;
            w_none_succeeded                  = false;
            HLOGC(gslog.Debug, log << "grp/sendBackup: after waiting, ACTIVATED link @" << d->id);

            break;
        }

        // If we have no links successfully activated, but at least
        // one link "not ready for writing", continue waiting for at
        // least one link ready.
        if (stat == -1 && nwaiting > 0)
        {
            HLOGC(gslog.Debug, log << "grp/sendBackup: still have " << nwaiting << " waiting and none succeeded, REPEAT");
            goto RetryWaitBlocked;
        }

        HLOGC(gslog.Debug, log << "grp/sendBackup: " << nactivated << " links activated with "
                << unstable.size() << " unstable");
    }

    // The most important principle is to keep the data being sent constantly,
    // even if it means temporarily full redundancy. However, if you are certain
    // that you have multiple stable links running at the moment, SILENCE all but
    // the one with highest priority.
    if (w_parallel.size() > 1)
    {
        sort(w_parallel.begin(), w_parallel.end(), FPriorityOrder());
        steady_clock::time_point currtime = steady_clock::now();

        vector<gli_t>::iterator b = w_parallel.begin();

        // Additional criterion: if you have multiple links with the same weight,
        // check if you have at least one with m_tsTmpActiveTime == 0. If not,
        // sort them additionally by this time.

        vector<gli_t>::iterator b1 = b, e = ++b1;

        // Both e and b1 stand on b+1 position.
        // We have a guarantee that b+1 still points to a valid element.
        while (e != w_parallel.end())
        {
            if ((*e)->weight != (*b)->weight)
                break;
            ++e;
        }

        if (b1 != e)
        {
            // More than 1 link with the same weight. Sorting them according
            // to a different criterion will not change the previous sorting order
            // because the elements in this range are equal according to the previous
            // criterion.
            // Here find the link with least time. The "trap" zero time matches this
            // requirement, occasionally.
            sort(b, e, FByOldestActive());
        }

        // After finding the link to leave active, leave it behind.
        HLOGC(gslog.Debug, log << "grp/sendBackup: keeping parallel link @" << (*b)->id << " and silencing others:");
        ++b;
        for (; b != w_parallel.end(); ++b)
        {
            gli_t& d = *b;
            if (d->sndstate != SRT_GST_RUNNING)
            {
                LOGC(gslog.Error,
                        log << "grp/sendBackup: IPE: parallel link container contains non-running link @" << d->id);
                continue;
            }
            CUDT&                  ce = d->ps->core();
            steady_clock::duration td(0);
            if (!is_zero(ce.m_tsTmpActiveTime) &&
                    count_microseconds(td = currtime - ce.m_tsTmpActiveTime) < ce.m_uOPT_StabilityTimeout)
            {
                HLOGC(gslog.Debug,
                        log << "... not silencing @" << d->id << ": too early: " << FormatDuration(td) << " < "
                        << ce.m_uOPT_StabilityTimeout << "(stability timeout)");
                continue;
            }

            // Clear activation time because the link is no longer active!
            d->sndstate = SRT_GST_IDLE;
            HLOGC(gslog.Debug, log << " ... @" << d->id << " ACTIVATED: " << FormatTime(ce.m_tsTmpActiveTime));
            ce.m_tsTmpActiveTime = steady_clock::time_point();
        }
    }
}

int CUDTGroup::sendBackup(const char* buf, int len, SRT_MSGCTRL& w_mc)
{
    // Avoid stupid errors in the beginning.
    if (len <= 0)
    {
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    // Live only - sorry.
    if (len > SRT_LIVE_MAX_PLSIZE)
    {
        LOGC(gslog.Error, log << "grp/send(backup): buffer size=" << len << " exceeds maximum allowed in live mode");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    // [[using assert(this->m_pSndBuffer != nullptr)]];

    // NOTE: This is a "vector of list iterators". Every element here
    // is an iterator to another container.
    // Note that "list" is THE ONLY container in standard C++ library,
    // for which NO ITERATORS ARE INVALIDATED after a node at particular
    // iterator has been removed, except for that iterator itself.
    vector<SRTSOCKET> wipeme;
    vector<gli_t> idlers;
    vector<SRTSOCKET> pending;
    vector<gli_t> unstable;
    vector<gli_t> sendable;

    int                          stat       = 0;
    int                          final_stat = -1;
    SRT_ATR_UNUSED CUDTException cx(MJ_SUCCESS, MN_NONE, 0);

    // First, acquire GlobControlLock to make sure all member sockets still exist
    enterCS(m_pGlobal->m_GlobControlLock);
    ScopedLock guard(m_GroupLock);

    if (m_bClosing)
    {
        leaveCS(m_pGlobal->m_GlobControlLock);
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Now, still under lock, check if all sockets still can be dispatched
    send_CheckValidSockets();
    leaveCS(m_pGlobal->m_GlobControlLock);

    steady_clock::time_point currtime = steady_clock::now();

    sendable.reserve(m_Group.size());

    // First, check status of every link - no matter if idle or active.
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
    {
        // Check the socket state prematurely in order not to uselessly
        // send over a socket that is broken.
        CUDT* pu = 0;
        if (d->ps)
            pu = &d->ps->core();

        if (!pu || pu->m_bBroken)
        {
            HLOGC(gslog.Debug, log << "grp/sendBackup: socket @" << d->id << " detected +Broken - transit to BROKEN");
            d->sndstate = SRT_GST_BROKEN;
            d->rcvstate = SRT_GST_BROKEN;
        }

        // Check socket sndstate before sending
        if (d->sndstate == SRT_GST_BROKEN)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBackup: socket in BROKEN state: @" << d->id
                      << ", sockstatus=" << SockStatusStr(d->ps ? d->ps->getStatus() : SRTS_NONEXIST));
            wipeme.push_back(d->id);
            continue;
        }

        if (d->sndstate == SRT_GST_IDLE)
        {
            if (!send_CheckIdle(d, (wipeme), (pending)))
                continue;

            HLOGC(gslog.Debug,
                  log << "grp/sendBackup: socket in IDLE state: @" << d->id << " - will activate it IF NEEDED");
            // This is idle, we'll take care of them next time
            // Might be that:
            // - this socket is idle, while some NEXT socket is running
            // - we need at least one running socket to work BEFORE activating the idle one.
            // - if ALL SOCKETS ARE IDLE, then we simply activate the first from the list,
            //   and all others will be activated using the ISN from the first one.
            idlers.push_back(d);
            sendBackup_CheckIdleTime(d);
            continue;
        }

        if (d->sndstate == SRT_GST_RUNNING)
        {
            if (!sendBackup_CheckRunningStability(d, (currtime)))
            {
                insert_uniq((unstable), d);
            }
            // Unstable links should still be used for sending.
            sendable.push_back(d);
            continue;
        }

        HLOGC(gslog.Debug,
              log << "grp/sendBackup: socket @" << d->id << " not ready, state: " << StateStr(d->sndstate) << "("
                  << int(d->sndstate) << ") - NOT sending, SET AS PENDING");

        pending.push_back(d->id);
    }

    // Sort the idle sockets by priority so the highest priority idle links are checked first.
    sort(idlers.begin(), idlers.end(), FPriorityOrder());

    vector<Sendstate> sendstates;

    // Ok, we've separated the unstable from sendable just to know if:
    // - we have any STABLE sendable (if not, we must activate a backup link)
    // - we have multiple stable sendable and we need to stop all but one

    // Normally there should be only one link with state == SRT_GST_RUNNING, but there might
    // be multiple links set as running when a "breaking suspection" is set on a link.

    bool none_succeeded = true; // be pessimistic

    // This should be added all sockets that are currently stable
    // and sending was successful. Later, all but the one with highest
    // priority should remain active.
    vector<gli_t> parallel;

#if ENABLE_HEAVY_LOGGING
    {
        vector<SRTSOCKET> show_running, show_idle;
        for (vector<gli_t>::iterator i = sendable.begin(); i != sendable.end(); ++i)
            show_running.push_back((*i)->id);

        for (vector<gli_t>::iterator i = idlers.begin(); i != idlers.end(); ++i)
            show_idle.push_back((*i)->id);

        LOGC(gslog.Debug,
             log << "grp/sendBackup: RUNNING: " << PrintableMod(show_running, "@")
                 << " IDLE: " << PrintableMod(show_idle, "@"));
    }
#endif

    int32_t curseq      = SRT_SEQNO_NONE;
    size_t  nsuccessful = 0;

    // Collect priorities from sendable links, added only after sending is successful.
    // This will be used to check if any of the idlers have higher priority
    // and therefore need to be activated.
    set<uint16_t> sendable_pri;

    // We believe that we need to send the payload over every sendable link anyway.
    for (vector<gli_t>::iterator snd = sendable.begin(); snd != sendable.end(); ++snd)
    {
        gli_t d   = *snd;
        int   erc = 0; // success
        // Remaining sndstate is SRT_GST_RUNNING. Send a payload through it.
        CUDT&   u       = d->ps->core();
        int32_t lastseq = u.schedSeqNo();
        try
        {
            // This must be wrapped in try-catch because on error it throws an exception.
            // Possible return values are only 0, in case when len was passed 0, or a positive
            // >0 value that defines the size of the data that it has sent, that is, in case
            // of Live mode, equal to 'len'.
            stat = u.sendmsg2(buf, len, (w_mc));
        }
        catch (CUDTException& e)
        {
            cx   = e;
            stat = -1;
            erc  = e.getErrorCode();
        }

        bool is_unstable = false;
        none_succeeded &= sendBackup_CheckSendStatus(d,
                                                     currtime,
                                                     stat,
                                                     erc,
                                                     lastseq,
                                                     w_mc.pktseq,
                                                     (u),
                                                     (curseq),
                                                     (parallel),
                                                     (final_stat),
                                                     (sendable_pri),
                                                     (nsuccessful),
                                                     (is_unstable));

        if (is_unstable && is_zero(u.m_tsUnstableSince)) // Add to unstable only if it wasn't unstable already
            insert_uniq((unstable), d);

        const Sendstate cstate = {d->id, d, stat, erc};
        sendstates.push_back(cstate);
        d->sndresult  = stat;
        d->laststatus = d->ps->getStatus();
    }

    // Ok, we have attempted to send a payload over all active links
    // We know that at least one is successful if we have non-default curmsgno
    // value.

    // Now we need to check the link that is currently defined as
    // main active because we can have:
    // - one active link only - we want to check its status
    // - two active links - one "main active" and one "temporarily
    //   activated"

    // Here the only thing we need to decide about is:
    // 1. if we have at least one active and STABLE link
    //   - if there are no stable links, activate one idle link
    // 2. if we have more than one active and stable link
    //   - select those with highest priority out of them
    //   - select the first in order from those
    //   - silence the rest (turn them idle)

    // In Backup group, we have the following possibilities
    // - only one link active and stable (normal)
    // - multiple links active (and possibly one of them stable)
    //
    // We might have had a situation that sending was not possible
    // due to have been blocked.
    //
    // If you have any link blocked, treat it as unstable, which
    // means that one link out of the waiting idle must be activated.
    //
    // HOWEVER:
    //
    // Collect blocked links in order to make an additional check:
    //
    // If all links out of the unstable-running links are blocked,
    // perform epoll wait on them. In this situation we know that
    // there are no idle blocked links because IDLE LINK CAN'T BE BLOCKED,
    // no matter what. It's because the link may only be blocked if
    // the sender buffer of this socket is full, and it can't be
    // full if it wasn't used so far.
    //
    // This means that in case when we have no stable links, we
    // need to try out any link that can accept the rexmit-load.
    // We'll check link stability at the next sending attempt.

    // Here we need to activate one IDLE link, if we have
    // no stable links.
    // Some portion of logical exclusions:
    //
    // - sockets that were broken in the beginning are already wiped out
    // - broken sockets are checked first, so they can't be simultaneously idle
    // - idle sockets can't get broken because there's no operation done on them
    // - running sockets are the only one that could change sndstate here
    // - running sockets can either remain running or turn to broken
    // In short: Running and Broken sockets can't become idle,
    // although Running sockets can become Broken.

    // There's no certainty here as to whether at least one link was
    // running and it has successfully performed the operation.
    // Might have even happened that we had 2 running links that
    // got broken and 3 other links so far in idle sndstate that just connected
    // at that very moment (in Backup group: 1 running stable, 1 running
    // unstable, 3 links keeping connetion being idle).

    // In this case we have 3 idle links to activate one of,
    // but there is no message number base. If so, take the number for
    // the first activated link as a good deal.
    //
    // If it has additionally happened that the first link got broken at
    // that very moment of sending, the second one has a chance to succeed
    // and therefore take over the leading role in setting the leading message
    // number. If the second one fails, too, then the only remaining idle link
    // will simply go with its own original message number.
    //
    // Now we can go to the idle links and attempt to send the payload
    // also over them.

    sendBackup_Buffering(buf, len, (curseq), (w_mc));

    // CHECK: no sendable that exceeds unstable
    // This embraces the case when there are no sendable at all.
    // Note that unstable links still count as sendable; they
    // are simply links that were qualified for sending, but:
    // - have exceeded response timeout
    // - have hit EASYNCSND error during sending
    bool   need_activate = sendable.size() <= unstable.size();
    string activate_reason;
    IF_HEAVY_LOGGING(activate_reason = "BY NO REASON???");
    if (need_activate)
    {
        HLOGC(gslog.Debug,
              log << "grp/sendBackup: all " << sendable.size() << " links unstable - will activate an idle link");
        IF_HEAVY_LOGGING(activate_reason = "no stable links");
    }
    else
    {
        // Another reason to activate might be if the link with highest priority
        // among the idlers has a higher priority than any link currently active
        // (those are collected in 'sendable_pri'). Check if there are any (if
        // no sendable, a new link needs to be activated anyway), and if the
        // priority has a lower number.
        if (sendable_pri.empty() || (!idlers.empty() && FPriorityOrder::check(idlers[0]->weight, *sendable_pri.begin())))
        {
            need_activate = true;
#if ENABLE_HEAVY_LOGGING
            if (sendable_pri.empty())
            {
                activate_reason = "no successful links found";
                LOGC(gslog.Debug,
                     log << "grp/sendBackup: no active links were successful - will activate an idle link");
            }
            else if (idlers.empty())
            {
                // This should be impossible.
                activate_reason = "WEIRD (no idle links!)";
                LOGC(gslog.Debug,
                     log << "grp/sendBackup: BY WEIRD AND IMPOSSIBLE REASON (IPE?) - will activate an idle link");
            }
            else
            {
                // Only now we are granted that both sendable_pri and idlers are nonempty
                LOGC(gslog.Debug,
                     log << "grp/sendBackup: found link pri " << idlers[0]->weight << " PREF OVER " << (*sendable_pri.begin())
                         << " (highest from sendable) - will activate an idle link");
                activate_reason = "found higher pri link";
            }
#endif
        }
        else
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBackup: sendable_pri (" << sendable_pri.size() << "): " << Printable(sendable_pri)
                      << " first idle pri: " << (idlers.size() > 0 ? int(idlers[0]->weight) : -1)
                      << " - will NOT activate an idle link");
        }
    }

    if (need_activate)
    {
        if (idlers.empty())
        {
            HLOGP(gslog.Debug, "grp/sendBackup: no idlers to activate, keeping only unstable links");
        }
        else
        {
            size_t n ATR_UNUSED = sendBackup_CheckNeedActivate(idlers,
                                     buf,
                                     len,
                                     (none_succeeded),
                                     (w_mc),
                                     (curseq),
                                     (final_stat),
                                     (cx),
                                     (sendstates),
                                     (parallel),
                                     (wipeme),
                                     activate_reason);

            HLOGC(gslog.Debug, log << "grp/sendBackup: activated " << n << " idle links tried");
        }
    }
    else
    {
        HLOGC(gslog.Debug,
              log << "grp/sendBackup: have sendable links, stable=" << (sendable.size() - unstable.size())
                  << " unstable=" << unstable.size());
    }

    send_CheckPendingSockets(pending, (wipeme));

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    send_CloseBrokenSockets((wipeme));

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    sendBackup_CheckParallelLinks(unstable, (parallel), (final_stat), (none_succeeded), (w_mc), (cx));
    // (closing condition checked inside this call)

    if (none_succeeded)
    {
        HLOGC(gslog.Debug, log << "grp/sendBackup: all links broken (none succeeded to send a payload)");
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
        // Reparse error code, if set.
        // It might be set, if the last operation was failed.
        // If any operation succeeded, this will not be executed anyway.

        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Now fill in the socket table. Check if the size is enough, if not,
    // then set the pointer to NULL and set the correct size.

    // Note that list::size() is linear time, however this shouldn't matter,
    // as with the increased number of links in the redundancy group the
    // impossibility of using that many of them grows exponentally.
    size_t grpsize = m_Group.size();

    if (w_mc.grpdata_size < grpsize)
    {
        w_mc.grpdata = NULL;
    }

    size_t i = 0;

    bool ready_again = false;

    HLOGC(gslog.Debug, log << "grp/sendBackup: copying group data");
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d, ++i)
    {
        if (w_mc.grpdata)
        {
            // Enough space to fill
            copyGroupData(*d, (w_mc.grpdata[i]));
        }

        // We perform this loop anyway because we still need to check if any
        // socket is writable. Note that the group lock will hold any write ready
        // updates that are performed just after a single socket update for the
        // group, so if any socket is actually ready at the moment when this
        // is performed, and this one will result in none-write-ready, this will
        // be fixed just after returning from this function.

        ready_again = ready_again | d->ps->writeReady();
    }
    w_mc.grpdata_size = i;

    if (!ready_again)
    {
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
    }

    HLOGC(gslog.Debug,
          log << "grp/sendBackup: successfully sent " << final_stat << " bytes, "
              << (ready_again ? "READY for next" : "NOT READY to send next"));
    return final_stat;
}

// [[using locked(this->m_GroupLock)]]
int32_t CUDTGroup::addMessageToBuffer(const char* buf, size_t len, SRT_MSGCTRL& w_mc)
{
    if (m_iSndAckedMsgNo == SRT_MSGNO_NONE)
    {
        // Very first packet, just set the msgno.
        m_iSndAckedMsgNo  = w_mc.msgno;
        m_iSndOldestMsgNo = w_mc.msgno;
    }
    else if (m_iSndOldestMsgNo != m_iSndAckedMsgNo)
    {
        int offset = MsgNo(m_iSndAckedMsgNo) - MsgNo(m_iSndOldestMsgNo);
        HLOGC(gslog.Debug,
              log << "addMessageToBuffer: new ACK-ed messages: #(" << m_iSndOldestMsgNo << "-" << m_iSndAckedMsgNo
                  << ") - going to remove");

        if (offset > int(m_SenderBuffer.size()))
        {
            LOGC(gslog.Error,
                 log << "addMessageToBuffer: IPE: offset=" << offset << " exceeds buffer size=" << m_SenderBuffer.size()
                     << " - CLEARING");
            m_SenderBuffer.clear();
        }
        else
        {
            HLOGC(gslog.Debug,
                  log << "addMessageToBuffer: erasing " << offset << "/" << m_SenderBuffer.size()
                      << " group-senderbuffer ACKED messages for #" << m_iSndOldestMsgNo << " - #" << m_iSndAckedMsgNo);
            m_SenderBuffer.erase(m_SenderBuffer.begin(), m_SenderBuffer.begin() + offset);
        }

        // Position at offset is not included
        m_iSndOldestMsgNo = m_iSndAckedMsgNo;
    }

    m_SenderBuffer.resize(m_SenderBuffer.size() + 1);
    BufferedMessage& bm = m_SenderBuffer.back();
    bm.mc               = w_mc;
    bm.copy(buf, len);

    HLOGC(gslog.Debug,
          log << "addMessageToBuffer: #" << w_mc.msgno << " size=" << len << " !" << BufferStamp(buf, len));

    return m_SenderBuffer.front().mc.pktseq;
}

// [[using locked(this->m_GroupLock)]]
int CUDTGroup::sendBackupRexmit(CUDT& core, SRT_MSGCTRL& w_mc)
{
    // This should resend all packets
    if (m_SenderBuffer.empty())
    {
        LOGC(gslog.Fatal, log << "IPE: sendBackupRexmit: sender buffer empty");

        // Although act as if it was successful, otherwise you'll get connection break
        return 0;
    }

    // using [[assert !m_SenderBuffer.empty()]];

    // Send everything you currently have in the sender buffer.
    // The receiver will reject packets that it currently has.
    // Start from the oldest.

    CPacket packet;

    set<int> results;
    int      stat = -1;

    // Make sure that the link has correctly synchronized sequence numbers.
    // Note that sequence numbers should be recorded in mc.
    int32_t curseq       = m_SenderBuffer[0].mc.pktseq;
    size_t  skip_initial = 0;
    if (curseq != core.schedSeqNo())
    {
        int distance = CSeqNo::seqoff(core.schedSeqNo(), curseq);
        if (distance < 0)
        {
            // This may happen in case when the link to be activated is already running.
            // Getting sequences backwards is not allowed, as sending them makes no
            // sense - they are already ACK-ed or are behind the ISN. Instead, skip all
            // packets that are in the past towards the scheduling sequence.
            skip_initial = -distance;
            LOGC(gslog.Warn,
                 log << "sendBackupRexmit: OVERRIDE attempt to %" << core.schedSeqNo() << " from BACKWARD %" << curseq
                     << " - DENIED; skip " << skip_initial << " packets");
        }
        else
        {
            // In case when the next planned sequence on this link is behind
            // the firstmost sequence in the backup buffer, synchronize the
            // sequence with it first so that they go hand-in-hand with
            // sequences already used by the link from which packets were
            // copied to the backup buffer.
            IF_HEAVY_LOGGING(int32_t old = core.schedSeqNo());
            const bool su ATR_UNUSED = core.overrideSndSeqNo(curseq);
            HLOGC(gslog.Debug,
                  log << "sendBackupRexmit: OVERRIDING seq %" << old << " with %" << curseq
                      << (su ? " - succeeded" : " - FAILED!"));
        }
    }

    senderBuffer_t::iterator i = m_SenderBuffer.begin();

    if (skip_initial >= m_SenderBuffer.size())
        return 0; // can't return any other state, nothing was sent
    else if (skip_initial)
        i += skip_initial;

    // Send everything - including the packet freshly added to the buffer
    for (; i != m_SenderBuffer.end(); ++i)
    {
        // NOTE: an exception from here will interrupt the loop
        // and will be caught in the upper level.
        stat = core.sendmsg2(i->data, i->size, (i->mc));
        if (stat == -1)
        {
            // Stop sending if one sending ended up with error
            LOGC(gslog.Warn,
                 log << "sendBackupRexmit: sending from buffer stopped at %" << core.schedSeqNo() << " and FAILED");
            return -1;
        }
    }

    // Copy the contents of the last item being updated.
    w_mc = m_SenderBuffer.back().mc;
    HLOGC(gslog.Debug, log << "sendBackupRexmit: pre-sent collected %" << curseq << " - %" << w_mc.pktseq);
    return stat;
}

void CUDTGroup::ackMessage(int32_t msgno)
{
    // The message id could not be identified, skip.
    if (msgno == SRT_MSGNO_CONTROL)
    {
        HLOGC(gslog.Debug, log << "ackMessage: msgno not found in ACK-ed sequence");
        return;
    }

    // It's impossible to get the exact message position as the
    // message is allowed also to span for multiple packets.
    // Search since the oldest packet until you hit the first
    // packet with this message number.

    // First, you need to decrease the message number by 1. It's
    // because the sequence number being ACK-ed can be in the middle
    // of the message, while it doesn't acknowledge that the whole
    // message has been received. Decrease the message number so that
    // partial-message-acknowledgement does not swipe the whole message,
    // part of which may need to be retransmitted over a backup link.

    int offset = MsgNo(msgno) - MsgNo(m_iSndAckedMsgNo);
    if (offset <= 0)
    {
        HLOGC(gslog.Debug, log << "ackMessage: already acked up to msgno=" << msgno);
        return;
    }

    HLOGC(gslog.Debug, log << "ackMessage: updated to #" << msgno);

    // Update last acked. Will be picked up when adding next message.
    m_iSndAckedMsgNo = msgno;
}

void CUDTGroup::handleKeepalive(gli_t gli)
{
    // received keepalive for that group member
    // In backup group it means that the link went IDLE.
    if (m_type == SRT_GTYPE_BACKUP)
    {
        if (gli->rcvstate == SRT_GST_RUNNING)
        {
            gli->rcvstate = SRT_GST_IDLE;
            HLOGC(gslog.Debug, log << "GROUP: received KEEPALIVE in @" << gli->id << " - link turning rcv=IDLE");
        }

        // When received KEEPALIVE, the sending state should be also
        // turned IDLE, if the link isn't temporarily activated. The
        // temporarily activated link will not be measured stability anyway,
        // while this should clear out the problem when the transmission is
        // stopped and restarted after a while. This will simply set the current
        // link as IDLE on the sender when the peer sends a keepalive because the
        // data stopped coming in and it can't send ACKs therefore.
        //
        // This also shouldn't be done for the temporary activated links because
        // stability timeout could be exceeded for them by a reason that, for example,
        // the packets come with the past sequences (as they are being synchronized
        // the sequence per being IDLE and empty buffer), so a large portion of initial
        // series of packets may come with past sequence, delaying this way with ACK,
        // which may result not only with exceeded stability timeout (which fortunately
        // isn't being measured in this case), but also with receiveing keepalive
        // (therefore we also don't reset the link to IDLE in the temporary activation period).
        if (gli->sndstate == SRT_GST_RUNNING && is_zero(gli->ps->core().m_tsTmpActiveTime))
        {
            gli->sndstate = SRT_GST_IDLE;
            HLOGC(gslog.Debug,
                  log << "GROUP: received KEEPALIVE in @" << gli->id << " active=PAST - link turning snd=IDLE");
        }
    }
}

void CUDTGroup::internalKeepalive(gli_t gli)
{
    // This is in response to AGENT SENDING keepalive. This means that there's
    // no transmission in either direction, but the KEEPALIVE packet from the
    // other party could have been missed. This is to ensure that the IDLE state
    // is recognized early enough, before any sequence discrepancy can happen.

    if (m_type == SRT_GTYPE_BACKUP && gli->rcvstate == SRT_GST_RUNNING)
    {
        gli->rcvstate = SRT_GST_IDLE;
        // Prevent sending KEEPALIVE again in group-sending
        gli->ps->core().m_tsTmpActiveTime = steady_clock::time_point();
        HLOGC(gslog.Debug, log << "GROUP: EXP-requested KEEPALIVE in @" << gli->id << " - link turning IDLE");
    }
}

CUDTGroup::BufferedMessageStorage CUDTGroup::BufferedMessage::storage(SRT_LIVE_MAX_PLSIZE /*, 1000*/);

int CUDTGroup::configure(const char* str)
{
    string config = str;
    switch (type())
    {
        /* TMP review stub case SRT_GTYPE_BALANCING:
            // config contains the algorithm name
            if (config == "" || config == "auto")
            {
                m_cbSelectLink.set(this, &CUDTGroup::linkSelect_window_fw);
                HLOGC(gmlog.Debug, log << "group(balancing): WINDOW algorithm selected");
            }
            else if (config == "fixed")
            {
                m_cbSelectLink.set(this, &CUDTGroup::linkSelect_fixed_fw);
                HLOGC(gmlog.Debug, log << "group(balancing): FIXED algorithm selected");
            }
            else
            {
                LOGC(gmlog.Error, log << "group(balancing): unknown selection algorithm '"
                        << config << "'");
                return CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0);
            }

            break;*/

    default:
        if (config == "")
        {
            // You can always call the config with empty string,
            // it should set defaults or do nothing, if not supported.
            return 0;
        }
        LOGC(gmlog.Error, log << "this group type doesn't support any configuration");
        return CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0);
    }

    return 0;
}

// Forwarder needed due to class definition order
int32_t CUDTGroup::generateISN()
{
    return CUDT::generateISN();
}

void CUDTGroup::setGroupConnected()
{
    if (!m_bConnected)
    {
        // Switch to connected state and give appropriate signal
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_CONNECT, true);
        m_bConnected = true;
    }
}

void CUDTGroup::updateLatestRcv(CUDTSocket* s)
{
    // Currently only Backup groups use connected idle links.
    if (m_type != SRT_GTYPE_BACKUP)
        return;

    HLOGC(grlog.Debug,
          log << "updateLatestRcv: BACKUP group, updating from active link @" << s->m_SocketID << " with %"
              << s->m_pUDT->m_iRcvLastSkipAck);

    CUDT*         source = s->m_pUDT;
    vector<CUDT*> targets;

    UniqueLock lg(m_GroupLock);
    // Sanity check for a case when getting a deleted socket
    if (!s->m_IncludedGroup)
        return;

    // Under a group lock, we block execution of removal of the socket
    // from the group, so if m_IncludedGroup is not NULL, we are granted
    // that m_IncludedIter is valid.
    gli_t current = s->m_IncludedIter;

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        // Skip the socket that has reported packet reception
        if (gi == current)
        {
            HLOGC(grlog.Debug, log << "grp: NOT updating rcv-seq on self @" << gi->id);
            continue;
        }

        // Don't update the state if the link is:
        // - PENDING - because it's not in the connected state, wait for it.
        // - RUNNING - because in this case it should have its own line of sequences
        // - BROKEN - because it doesn't make sense anymore, about to be removed
        if (gi->rcvstate != SRT_GST_IDLE)
        {
            HLOGC(grlog.Debug,
                  log << "grp: NOT updating rcv-seq on @" << gi->id
                      << " - link state:" << srt_log_grp_state[gi->rcvstate]);
            continue;
        }

        // Sanity check
        if (!gi->ps->m_pUDT->m_bConnected)
        {
            HLOGC(grlog.Debug, log << "grp: IPE: NOT updating rcv-seq on @" << gi->id << " - IDLE BUT NOT CONNECTED");
            continue;
        }

        targets.push_back(gi->ps->m_pUDT);
    }

    lg.unlock();

    // Do this on the unlocked group because this
    // operation will need receiver lock, so it might
    // risk a deadlock.

    for (size_t i = 0; i < targets.size(); ++i)
    {
        targets[i]->updateIdleLinkFrom(source);
    }
}

void CUDTGroup::activateUpdateEvent(bool still_have_items)
{
    // This function actually reacts on the fact that a socket
    // was deleted from the group. This might make the group empty.
    if (!still_have_items) // empty, or removal of unknown socket attempted - set error on group
    {
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR, true);
    }
    else
    {
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_UPDATE, true);
    }
}

void CUDTGroup::addEPoll(int eid)
{
    enterCS(m_pGlobal->m_EPoll.m_EPollLock);
    m_sPollID.insert(eid);
    leaveCS(m_pGlobal->m_EPoll.m_EPollLock);

    bool any_read    = false;
    bool any_write   = false;
    bool any_broken  = false;
    bool any_pending = false;

    {
        // Check all member sockets
        ScopedLock gl(m_GroupLock);

        // We only need to know if there is any socket that is
        // ready to get a payload and ready to receive from.

        for (gli_t i = m_Group.begin(); i != m_Group.end(); ++i)
        {
            if (i->sndstate == SRT_GST_IDLE || i->sndstate == SRT_GST_RUNNING)
            {
                any_write |= i->ps->writeReady();
            }

            if (i->rcvstate == SRT_GST_IDLE || i->rcvstate == SRT_GST_RUNNING)
            {
                any_read |= i->ps->readReady();
            }

            if (i->ps->broken())
                any_broken |= true;
            else
                any_pending |= true;
        }
    }

    // This is stupid, but we don't have any other interface to epoll
    // internals. Actually we don't have to check if id() is in m_sPollID
    // because we know it is, as we just added it. But it's not performance
    // critical, sockets are not being often added during transmission.
    if (any_read)
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, true);

    if (any_write)
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, true);

    // Set broken if none is non-broken (pending, read-ready or write-ready)
    if (any_broken && !any_pending)
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
}

void CUDTGroup::removeEPollEvents(const int eid)
{
    // clear IO events notifications;
    // since this happens after the epoll ID has been removed, they cannot be set again
    set<int> remove;
    remove.insert(eid);
    m_pGlobal->m_EPoll.update_events(id(), remove, SRT_EPOLL_IN | SRT_EPOLL_OUT, false);
}

void CUDTGroup::removeEPollID(const int eid)
{
    enterCS(m_pGlobal->m_EPoll.m_EPollLock);
    m_sPollID.erase(eid);
    leaveCS(m_pGlobal->m_EPoll.m_EPollLock);
}

void CUDTGroup::updateFailedLink()
{
    ScopedLock lg(m_GroupLock);

    // Check all members if they are in the pending
    // or connected state.

    int nhealthy = 0;

    for (gli_t i = m_Group.begin(); i != m_Group.end(); ++i)
    {
        if (i->sndstate < SRT_GST_BROKEN)
            nhealthy++;
    }

    if (!nhealthy)
    {
        // No healthy links, set ERR on epoll.
        HLOGC(gmlog.Debug, log << "group/updateFailedLink: All sockets broken");
        m_pGlobal->m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
    }
    else
    {
        HLOGC(gmlog.Debug, log << "group/updateFailedLink: Still " << nhealthy << " links in the group");
    }
}

#if ENABLE_HEAVY_LOGGING
void CUDTGroup::debugGroup()
{
    ScopedLock gg(m_GroupLock);

    HLOGC(gmlog.Debug, log << "GROUP MEMBER STATUS - $" << id());

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        HLOGC(gmlog.Debug,
              log << " ... id { agent=@" << gi->id << " peer=@" << gi->ps->m_PeerID
                  << " } address { agent=" << gi->agent.str() << " peer=" << gi->peer.str() << "} "
                  << " state {snd=" << StateStr(gi->sndstate) << " rcv=" << StateStr(gi->rcvstate) << "}");
    }
}
#endif
