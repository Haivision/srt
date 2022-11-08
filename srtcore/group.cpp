#include "platform_sys.h"

#include <iterator>

#include "api.h"
#include "group.h"
#include "socketconfig.h"

using namespace std;
using namespace srt::sync;
using namespace srt::groups;
using namespace srt_logging;

// The SRT_DEF_VERSION is defined in core.cpp.
extern const int32_t SRT_DEF_VERSION;

namespace srt {

int32_t CUDTGroup::s_tokenGen = 0;

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
            // over that socket. Using this field is safer because it is atomic
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
                      << " snd %" << (w_snd_isn) << " as " << update_reason);
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

    IF_LOGGING(SRTSOCKET mpeer = SRT_INVALID_SOCK);
    IF_LOGGING(steady_clock::time_point start_time);

    bool found = false;

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->sndstate == SRT_GST_RUNNING)
        {
            // Found it. Get the socket's peer's ID and this socket's
            // Start Time. Once it's delivered, this can be used to calculate
            // the Master-to-Slave start time difference.
            IF_LOGGING(mpeer = gi->ps->m_PeerID);
            IF_LOGGING(start_time = gi->ps->core().socketStartTime());
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
            IF_LOGGING(mpeer = gi->ps->core().m_PeerID);
            IF_LOGGING(start_time    = gi->ps->core().socketStartTime());
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
        IF_LOGGING(steady_clock::duration master_tdiff = m_tsStartTime - start_time);
        LOGC(cnlog.Debug, log << CONID() << "FOUND GROUP MASTER LINK: peer=$" << mpeer
                << " - start time diff: " << FormatDuration<DUNIT_S>(master_tdiff));
    }
}

// GROUP

CUDTGroup::SocketData* CUDTGroup::add(SocketData data)
{
    ScopedLock g(m_GroupLock);

    // Change the snd/rcv state of the group member to PENDING.
    // Default for SocketData after creation is BROKEN, which just
    // after releasing the m_GroupLock could be read and interpreted
    // as broken connection and removed before the handshake process
    // is done.
    data.sndstate = SRT_GST_PENDING;
    data.rcvstate = SRT_GST_PENDING;

    LOGC(gmlog.Note, log << "group/add: adding member @" << data.id << " into group $" << id());
    m_Group.push_back(data);
    gli_t end = m_Group.end();
    if (m_iMaxPayloadSize == -1)
    {
        int plsize = data.ps->core().OPT_PayloadSize();
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

    --end;
    return &*end;
}

CUDTGroup::CUDTGroup(SRT_GROUP_TYPE gtype)
    : m_Global(CUDT::uglobal())
    , m_GroupID(-1)
    , m_PeerGroupID(-1)
    , m_zLongestDistance(0)
    , m_type(gtype)
    , m_iBusy()
    , m_iRcvPossibleLossSeq(SRT_SEQNO_NONE)
    , m_iSndOldestMsgNo(SRT_MSGNO_NONE)
    , m_iSndAckedMsgNo(SRT_MSGNO_NONE)
    , m_uOPT_MinStabilityTimeout_us(1000 * CSrtConfig::COMM_DEF_MIN_STABILITY_TIMEOUT_MS)
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
    , m_bOPT_MessageAPI(true) // XXX currently not settable
    , m_iOPT_RcvBufSize(CSrtConfig::DEF_BUFFER_SIZE)
    , m_bOPT_DriftTracer(true)
    , m_tsStartTime()
    , m_tsRcvPeerStartTime()
    , m_bOpened(false)
    , m_bConnected(false)
    , m_bClosing(false)
    , m_iLastSchedSeqNo(SRT_SEQNO_NONE)
    , m_iLastSchedMsgNo(SRT_MSGNO_NONE)
    , m_uBalancingRoll(0)
    , m_RandomCredit(16)
{
    setupMutex(m_GroupLock, "Group");
    setupMutex(m_RcvDataLock, "G/RcvData");
    setupCond(m_RcvDataCond, "G/RcvData");
    setupCond(m_RcvTsbPdCond, "G/TSBPD");
    setupMutex(m_RcvBufferLock, "G/Buffer");

    m_SndEID = m_Global.m_EPoll.create(&m_SndEpolld);

    m_stats.init();

    // Set this data immediately during creation before
    // two or more sockets start arguing about it.
    m_iLastSchedSeqNo = CUDT::generateISN();

    m_cbSelectLink.set(this, &CUDTGroup::linkSelect_plain_fw);

    m_RcvFurthestPacketTime = steady_clock::now();
}

void CUDTGroup::createBuffers(int32_t isn, const time_point& tsbpd_start_time, int flow_winsize)
{
    // XXX NOT YET, but will be in use.
    m_pSndBuffer.reset();

    m_pRcvBuffer.reset(new srt::CRcvBuffer(isn, m_iOPT_RcvBufSize, /*m_pRcvQueue->m_pUnitQueue, */ m_bOPT_MessageAPI));
    if (tsbpd_start_time != time_point())
    {
        HLOGC(gmlog.Debug, log << "grp/createBuffers: setting rcv buf start time=" << FormatTime(tsbpd_start_time) << " lat=" << latency_us() << "us");
        m_pRcvBuffer->setTsbPdMode(tsbpd_start_time, false, microseconds_from(latency_us()));
    }

    m_pSndLossList.reset(new CSndLossList(flow_winsize * 2));
}

/// Update the internal state after a single link has been switched to RUNNING state.
void CUDTGroup::updateRcvRunningState()
{
    ScopedLock lk (m_GroupLock);

    size_t nrunning;
    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->rcvstate == SRT_GST_RUNNING)
            ++nrunning;
    }

    m_Group.set_number_running(nrunning);
}

void CUDTGroup::updateErasedLink()
{
    // When a link has been erased, reset the tracing data
    // to enforce a situation that some new links have been
    // added
    if (m_Group.size() > 1)
    {
        updateRcvRunningState();
    }

    m_zLongestDistance = 0;
    m_tdLongestDistance = duration::zero();
}

void CUDTGroup::updateInterlinkDistance()
{
    // Before locking anything, check if you have good enough conditions
    // to update the distance information. If not all links are idle, resolve
    // to the distance equal to the number of links. That is, that many packets
    // may be received after the gap so that the gap can be qualified as loss.

    if (m_Group.number_running() < m_Group.size())
    {
        size_t max_size = max(m_zLongestDistance.load(), m_Group.size());
        m_zLongestDistance = max_size;

        // Reset the duration so that it's not being traced
        m_tdLongestDistance = duration::zero();

        // Can't do anything more.
        return;
    }

    ScopedLock lk (m_GroupLock);


}

CUDTGroup::~CUDTGroup()
{
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
            m_LastActiveLink = m_List.end();
            return; // this avoids any misunderstandings in iterator checks
        }

        gli_t bb = m_List.begin();
        ++bb;
        if (bb == m_List.end()) // means: m_List.size() == 1
        {
            // One element, this one being deleted, nothing to point to.
            m_LastActiveLink = m_List.end();
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
    --m_SizeCache;
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
        break; // passthrough to socket option

    case SRTO_RCVTIMEO:
        m_iRcvTimeOut = cast_optval<int>(optval, optlen);
        break; // passthrough to socket option

    case SRTO_RCVBUF:
        {
            // This requires to obtain the possibly set MSS and FC options.
            // XXX Find some more sensible way to do it. Would be nice to
            // systematize the search method and default values.
            int val = cast_optval<int>(optval, optlen);
            if (val <= 0)
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

            // Search if you already have SRTO_MSS set
            int mss = CSrtConfig::DEF_MSS;
            vector<ConfigItem>::iterator f =
                find_if(m_config.begin(), m_config.end(), ConfigItem::OfType(SRTO_MSS));
            if (f != m_config.end())
            {
                f->get(mss); // worst case, it will leave it unchanged.
            }

            // Search if you already have SRTO_FC set
            int fc = CSrtConfig::DEF_FLIGHT_SIZE;
            f = find_if(m_config.begin(), m_config.end(), ConfigItem::OfType(SRTO_FC));
            if (f != m_config.end())
            {
                f->get(fc); // worst case, it will leave it unchanged.
            }

            if (mss <= 0 || fc <= 0)
                throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

            m_iOPT_RcvBufSize = srt::RcvBufferSizeOptionToValue(val, fc, mss);
        }
        break; // Keep passthru. This is also required for Unit queue initial size.

    case SRTO_DRIFTTRACER:
        {
            m_bOPT_DriftTracer = cast_optval<bool>(optval, optlen);
            return; // no passthru.
        }

    case SRTO_GROUPMINSTABLETIMEO:
    {
        const int val_ms = cast_optval<int>(optval, optlen);
        const int min_timeo_ms = (int) CSrtConfig::COMM_DEF_MIN_STABILITY_TIMEOUT_MS;
        if (val_ms < min_timeo_ms)
        {
            LOGC(qmlog.Error,
                 log << "group option: SRTO_GROUPMINSTABLETIMEO min allowed value is " << min_timeo_ms << " ms.");
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        // Search if you already have SRTO_PEERIDLETIMEO set
        int idletmo = CSrtConfig::COMM_RESPONSE_TIMEOUT_MS;
        vector<ConfigItem>::iterator f =
            find_if(m_config.begin(), m_config.end(), ConfigItem::OfType(SRTO_PEERIDLETIMEO));
        if (f != m_config.end())
        {
            f->get(idletmo); // worst case, it will leave it unchanged.
        }

        if (val_ms > idletmo)
        {
            LOGC(qmlog.Error,
                 log << "group option: SRTO_GROUPMINSTABLETIMEO=" << val_ms << " exceeds SRTO_PEERIDLETIMEO=" << idletmo);
            throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
        }

        m_uOPT_MinStabilityTimeout_us = 1000 * val_ms;
    }

    break;

    case SRTO_CONGESTION:
        // Currently no socket groups allow any other
        // congestion control mode other than live.
        LOGP(gmlog.Error, "group option: SRTO_CONGESTION is only allowed as 'live' and cannot be changed");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);

    case SRTO_GROUPCONFIG:
        configure((const char*)optval);
        return;

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
        if (!binary_search(srt_post_opt_list, srt_post_opt_list + SRT_SOCKOPT_NPOST, optName))
        {
            LOGC(gmlog.Error, log << "setsockopt(group): Group is connected, this option can't be altered");
            throw CUDTException(MJ_NOTSUP, MN_ISCONNECTED, 0);
        }

        HLOGC(gmlog.Debug, log << "... SPREADING to existing sockets.");
        // This means that there are sockets already, so apply
        // this option on them.
        std::vector<CUDTSocket*> ps_vec;
        {
            // Do copy to avoid deadlock. CUDT::setOpt() cannot be called directly inside this loop, because
            // CUDT::setOpt() will lock m_ConnectionLock, which should be locked before m_GroupLock.
            ScopedLock gg(m_GroupLock);
            for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
            {
                ps_vec.push_back(gi->ps);
            }
        }
        for (std::vector<CUDTSocket*>::iterator it = ps_vec.begin(); it != ps_vec.end(); ++it)
        {
            (*it)->core().setOpt(optName, optval, optlen);
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
    m_bSynRecving = u->m_config.bSynRecving;

    // SRTO_SNDSYN
    m_bSynSending = u->m_config.bSynSending;

    // SRTO_RCVTIMEO
    m_iRcvTimeOut = u->m_config.iRcvTimeOut;

    // SRTO_SNDTIMEO
    m_iSndTimeOut = u->m_config.iSndTimeOut;

    // SRTO_GROUPMINSTABLETIMEO
    m_uOPT_MinStabilityTimeout_us = 1000 * u->m_config.uMinStabilityTimeout_ms;

    // Ok, this really is disgusting, but there's only one way
    // to properly do it. Would be nice to have some more universal
    // connection between an option symbolic name and the internals
    // in CUDT class, but until this is done, since now every new
    // option will have to be handled both in the CUDT::setOpt/getOpt
    // functions, and here as well.

    // This is about moving options from listener to the group,
    // to be potentially replicated on the socket. So both pre
    // and post options apply.

#define IM(option, field) importOption(m_config, option, u->m_config.field)
#define IMF(option, field) importOption(m_config, option, u->field)

    IM(SRTO_MSS, iMSS);
    IM(SRTO_FC, iFlightFlagSize);

    // Nonstandard
    importOption(m_config, SRTO_SNDBUF, u->m_config.iSndBufSize * (u->m_config.iMSS - CPacket::UDP_HDR_SIZE));
    importOption(m_config, SRTO_RCVBUF, u->m_config.iRcvBufSize * (u->m_config.iMSS - CPacket::UDP_HDR_SIZE));

    IM(SRTO_LINGER, Linger);
    IM(SRTO_UDP_SNDBUF, iUDPSndBufSize);
    IM(SRTO_UDP_RCVBUF, iUDPRcvBufSize);
    // SRTO_RENDEZVOUS: impossible to have it set on a listener socket.
    // SRTO_SNDTIMEO/RCVTIMEO: groupwise setting
    IM(SRTO_CONNTIMEO, tdConnTimeOut);
    IM(SRTO_DRIFTTRACER, bDriftTracer);
    // Reuseaddr: true by default and should only be true.
    IM(SRTO_MAXBW, llMaxBW);
    IM(SRTO_INPUTBW, llInputBW);
    IM(SRTO_MININPUTBW, llMinInputBW);
    IM(SRTO_OHEADBW, iOverheadBW);
    IM(SRTO_IPTOS, iIpToS);
    IM(SRTO_IPTTL, iIpTTL);

    // XXX CONTROVERSIAL: there must be either the whole group TSBPD or not.
    // Single sockets should not have a say there. And also currently the
    // groups are not prepared to handle non-tsbpd mode.
    IM(SRTO_TSBPDMODE, bTSBPD);
    IM(SRTO_RCVLATENCY, iRcvLatency);
    IM(SRTO_PEERLATENCY, iPeerLatency);
    IM(SRTO_SNDDROPDELAY, iSndDropDelay);
    IM(SRTO_PAYLOADSIZE, zExpPayloadSize);
    IMF(SRTO_TLPKTDROP, m_bTLPktDrop);

    importOption(m_config, SRTO_STREAMID, u->m_config.sStreamName.str());

    IM(SRTO_MESSAGEAPI, bMessageAPI);
    IM(SRTO_NAKREPORT, bRcvNakReport);
    IM(SRTO_MINVERSION, uMinimumPeerSrtVersion);
    IM(SRTO_ENFORCEDENCRYPTION, bEnforcedEnc);
    IM(SRTO_IPV6ONLY, iIpV6Only);
    IM(SRTO_PEERIDLETIMEO, iPeerIdleTimeout_ms);

    importOption(m_config, SRTO_PACKETFILTER, u->m_config.sPacketFilterConfig.str());

    importOption(m_config, SRTO_PBKEYLEN, u->m_pCryptoControl->KeyLen());

    // Passphrase is empty by default. Decipher the passphrase and
    // store as passphrase option
    if (u->m_config.CryptoSecret.len)
    {
        string password((const char*)u->m_config.CryptoSecret.str, u->m_config.CryptoSecret.len);
        m_config.push_back(ConfigItem(SRTO_PASSPHRASE, password.c_str(), password.size()));
    }

    IM(SRTO_KMREFRESHRATE, uKmRefreshRatePkt);
    IM(SRTO_KMPREANNOUNCE, uKmPreAnnouncePkt);

    string cc = u->m_CongCtl.selected_name();
    if (cc != "live")
    {
        m_config.push_back(ConfigItem(SRTO_CONGESTION, cc.c_str(), cc.size()));
    }

    // NOTE: This is based on information extracted from the "semi-copy-constructor" of CUDT class.
    // Here should be handled all things that are options that modify the socket, but not all options
    // are assigned to configurable items.

#undef IM
#undef IMF
}

bool CUDTGroup::applyFlags(uint32_t flags, HandshakeSide)
{
    const bool synconmsg = IsSet(flags, SRT_GFLAG_SYNCONMSG);
    if (synconmsg)
    {
        LOGP(gmlog.Error, "GROUP: requested sync on msgno - not supported.");
        return false;
    }

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
    return (int) value.size();
}

template <class V>
inline int fillValue(void* optval, int len, V value)
{
    return Value<V>::fill(optval, len, value);
}

static bool getOptDefault(SRT_SOCKOPT optname, void* pw_optval, int& w_optlen)
{
    static const linger def_linger = {1, CSrtConfig::DEF_LINGER_S};
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
        RD(CSrtConfig::DEF_MSS);

    case SRTO_SNDSYN:
        RD(true);
    case SRTO_RCVSYN:
        RD(true);
    case SRTO_ISN:
        RD(SRT_SEQNO_NONE);
    case SRTO_FC:
        RD(CSrtConfig::DEF_FLIGHT_SIZE);

    case SRTO_SNDBUF:
    case SRTO_RCVBUF:
        w_optlen = fillValue((pw_optval), w_optlen, CSrtConfig::DEF_BUFFER_SIZE * (CSrtConfig::DEF_MSS - CPacket::UDP_HDR_SIZE));
        break;

    case SRTO_LINGER:
        RD(def_linger);
    case SRTO_UDP_SNDBUF:
    case SRTO_UDP_RCVBUF:
        RD(CSrtConfig::DEF_UDP_BUFFER_SIZE);
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
    case SRTO_GROUPMINSTABLETIMEO:
        RD(CSrtConfig::COMM_DEF_MIN_STABILITY_TIMEOUT_MS);
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
            i->second = m_Global.getStatus(i->first);
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
void CUDTGroup::syncWithFirstSocket(const CUDT& core, const HandshakeSide side)
{
    if (side == HSD_RESPONDER)
    {
        // On the listener side you should synchronize ISN with the incoming
        // socket, which is done immediately after creating the socket and
        // adding it to the group. On the caller side the ISN is defined in
        // the group directly, before any member socket is created.
        set_currentSchedSequence(core.ISN());
    }

    // Must be done here before createBuffers because the latency value
    // will be used to set it to the buffer after creation.
    HLOGC(gmlog.Debug, log << "grp/syncWithFirstSocket: setting group latency: " << core.m_iTsbPdDelay_ms << "ms");
    // Get the latency (possibly fixed against the opposite side)
    // from the first socket (core.m_iTsbPdDelay_ms),
    // and set it on the group.
    set_latency_us(core.m_iTsbPdDelay_ms * int64_t(1000));

    /*
    FIX: In this implementation we need to initialize the receiver buffer.
    This function is called when the first socket is added to the group,
    both as the first connection on the caller side and the socket connection
    that spawned this group as a mirror group on the listener side.
    The receiver buffer, which will be common for the group, needs ISN,
    in order to be able to recover any initially lost packets. Also,
    with the newly created fresh socket and very first socket in the group,
    it should be completely safe to set the ISN from the first socket,
    which is the same for sending and receiving. Next sockets added to
    the group may have these values derived from the group, and they can
    differ in sender and receiver.
    */

    int32_t butlast_seqno = CSeqNo::decseq(core.ISN());
    m_RcvLastSeqNo = butlast_seqno;

    // This should be the sequence of the latest packet in flight,
    // after being send over whichever member connection.
    m_SndLastSeqNo = butlast_seqno;
    m_SndLastDataAck = core.ISN();

    if (core.m_bGroupTsbPd)
    {
        m_tsRcvPeerStartTime = core.m_tsRcvPeerStartTime;
    }

    HLOGC(gmlog.Debug, log << "grp/syncWithFirstSocket: creating receiver buffer for ISN=%" << core.ISN()
            << " TSBPD start: " << (core.m_bGroupTsbPd ? FormatTime(m_tsRcvPeerStartTime) : "not enabled"));

    createBuffers(core.ISN(), m_tsRcvPeerStartTime, core.m_iFlowWindowSize);
}

CRcvBuffer::InsertInfo CUDTGroup::addDataUnit(groups::SocketData* member, CUnit* u, CUDT::loss_seqs_t& w_losses, bool& w_have_loss)
{
    // If this returns false, the adding has failed and 

    CRcvBuffer::InsertInfo info;
    const CPacket& rpkt = u->m_Packet;
    w_have_loss = false;

    {
        ScopedLock lk (m_RcvBufferLock);
        info = m_pRcvBuffer->insert(u);

        if (info.result == CRcvBuffer::InsertInfo::INSERTED)
        {
            w_have_loss = checkPacketArrivalLoss(member, u->m_Packet, (w_losses));
        }
    }

    if (info.result == CRcvBuffer::InsertInfo::INSERTED)
    {
        // If m_bTsbpdWaitForNewPacket, then notify anyway.
        // Otherwise notify only if a "fresher" packet was added,
        // so TSBPD should interrupt its sleep earlier and re-check.
        if (m_bTsbPd && (m_bTsbpdWaitForNewPacket || info.first_time != time_point()))
        {
            HLOGC(gmlog.Debug, log << CONID() << "grp/addDataUnit: got a packet [live], reason:"
                   << (m_bTsbpdWaitForNewPacket ? "expected" : "sealing") << " - SIGNAL TSBPD");
            // Make a lock on data reception first, to protect the buffer.
            // Then notify TSBPD if required.
            CUniqueSync tsbpd_cc(m_RcvDataLock, m_RcvTsbPdCond);
            tsbpd_cc.notify_all();
        }
    }
    else if (info.result == CRcvBuffer::InsertInfo::DISCREPANCY)
    {
        LOGC(qrlog.Error, log << CONID() << "grp/addDataUnit: "
                << "SEQUENCE DISCREPANCY. DISCARDING."
                << " seq=" << rpkt.m_iSeqNo
                << " buffer=(" << m_pRcvBuffer->getStartSeqNo()
                << ":" << m_RcvLastSeqNo                   // -1 = size to last index
                << "+" << CSeqNo::incseq(m_pRcvBuffer->getStartSeqNo(), int(m_pRcvBuffer->capacity()) - 1)
                << ")");
    }
    else
    {
#if ENABLE_HEAVY_LOGGING
        static const char* const ival [] = { "inserted", "redundant", "belated", "discrepancy" };
        if (int(info.result) > -4 && int(info.result) <= 0)
        {
            LOGC(qrlog.Debug, log << CONID() << "grp/addDataUnit: insert status: " << ival[-info.result]);
        }
        else
        {
            LOGC(qrlog.Debug, log << CONID() << "grp/addDataUnit: IPE: invalid insert status");
        }
#endif
    }

    return info;
}

// [[using locked(m_RcvBufferLock)]]
int CUDTGroup::rcvDropTooLateUpTo(int seqno)
{
    int iDropCnt = 0;

    // Nothing to drop from an empty buffer.
    // Required to check first to secure size()-1 expression.
    if (!m_pRcvBuffer->empty())
    {
        // Make sure that it would not drop over m_iRcvCurrSeqNo, which may break senders.
        int32_t last_seq = CSeqNo::incseq(m_pRcvBuffer->getStartSeqNo(), m_pRcvBuffer->size() - 1);
        if (CSeqNo::seqcmp(seqno, last_seq) > 0)
            seqno = last_seq;

        // Skipping the sequence number of the new contiguous region
        iDropCnt = m_pRcvBuffer->dropUpTo(seqno);

        /* not sure how to stats.
           if (iDropCnt > 0)
           {
           enterCS(m_StatsLock);
        // Estimate dropped bytes from average payload size.
        const uint64_t avgpayloadsz = m_pRcvBuffer->getRcvAvgPayloadSize();
        m_stats.rcvr.dropped.count(stats::BytesPackets(iDropCnt * avgpayloadsz, (uint32_t) iDropCnt));
        leaveCS(m_StatsLock);
        }
         */
    }

    return iDropCnt;
}

void CUDTGroup::synchronizeLoss(int32_t seqno)
{
    ScopedLock lk (m_GroupLock);

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        CUDT& u = gi->ps->core();
        u.skipMemberLoss(seqno);
    }
}

// [[using locked(m_RcvBufferLock)]]
bool CUDTGroup::checkPacketArrivalLoss(SocketData* member, const CPacket& rpkt, CUDT::loss_seqs_t& w_losses)
{
    // This is called when the packet was added to the buffer and this
    // adding was successful. Here we need to:

    // - check contiguity of the range between the last read and this packet
    // - update the m_RcvLastSeqNo to the last packet's sequence, if this was the newest packet

    // Note that we don't need to keep the latest contiguous packet sequence
    // because whatever non-contiguous range has been detected, it was notified
    // in the losses.

    bool have = false;

    // m_RcvLastSeqNo is atomic, so no need to protect it,
    // but it's also being modified using R-M-W method, and
    // this can potentially be interleft.
    //
    // Also, if this packet is going to be sealed from another
    // socket in the group, then this check should be done again
    // from the beginning, regarding the already recorded loss candidate.

    int32_t expected_seqno = m_RcvLastSeqNo;
    expected_seqno = CSeqNo::incseq(expected_seqno);

    // For balancing groups, use some more complicated mechanism.
    if (type() == SRT_GTYPE_BALANCING || type() == SRT_GTYPE_BROADCAST)
    {
        have = checkBalancingLoss(rpkt, (w_losses));
    }
    else if (CSeqNo::seqcmp(rpkt.m_iSeqNo, expected_seqno) > 0)
    {
        int32_t seqlo = expected_seqno;
        int32_t seqhi = CSeqNo::decseq(rpkt.m_iSeqNo);

        w_losses.push_back(make_pair(seqlo, seqhi));
        have = true;
        HLOGC(grlog.Debug, log << "grp:checkPacketArrivalLoss: loss detected: %("
                << seqlo << " - " << seqhi << ")");
    }

    if (CSeqNo::seqcmp(rpkt.m_iSeqNo, m_RcvLastSeqNo) > 0)
    {
        HLOGC(grlog.Debug, log << "grp:checkPacketArrivalLoss: latest updated: %" << m_RcvLastSeqNo << " -> %" << rpkt.m_iSeqNo);
        m_RcvLastSeqNo = rpkt.m_iSeqNo;

        // This should theoretically set it up with the very first packet received over whichever link
        // but this time is initialized upon creation of the group, just in case.
        m_RcvFurthestPacketTime = steady_clock::now();
        m_zLongestDistance = 0; // this member is at top
        member->updateCounter = 0;
    }
    else
    {
        bool updated SRT_ATR_UNUSED = false;
        if (++member->updateCounter == 10 && m_zLongestDistance > 1)
        {
            // Decrease by 1 once per 10 events so that if a link
            // happens to deliver packets faster, it is at some point detected
            // and taken into account.
            --m_zLongestDistance;
            m_tdLongestDistance = duration::zero();
            member->updateCounter = 0;
            updated = true;
        }

        int dist = CSeqNo::seqoff(rpkt.m_iSeqNo, m_RcvLastSeqNo);
        dist = max<int>(m_zLongestDistance, dist);
        m_zLongestDistance = dist;

        duration td = steady_clock::now() - m_RcvFurthestPacketTime;
        td = max(m_tdLongestDistance.load(), td);
        m_tdLongestDistance = td;

        HLOGC(grlog.Debug, log << "grp:checkPacketArrivalLoss: latest = %" << m_RcvLastSeqNo << ": pkt %" << rpkt.m_iSeqNo
                << " dist={" << dist << "pkt " << FormatDuration(m_tdLongestDistance) << (updated ? "} (reflected)" : "} (continued)"));
    }

    return have;
}

struct FFringeGreaterThan
{
    size_t baseval;
    FFringeGreaterThan(size_t b): baseval(b) {}

    template <class Value>
    bool operator()(const pair<Value, size_t>& val)
    {
        return val.second > baseval;
    }
};

// [[using locked(m_RcvBufferLock)]]
bool CUDTGroup::checkBalancingLoss(const CPacket& pkt, CUDT::loss_seqs_t& w_losses)
{
    // This is done in case of every incoming packet.

    if (pkt.getSeqNo() == m_iRcvPossibleLossSeq)
    {
        // XXX WARNING: it's unknown so far as to whether this "first loss"
        // hasn't been reported already.

        // This seals the exact loss position.
        // The returned value can be also NONE, which clears out the loss information.
        m_iRcvPossibleLossSeq = m_pRcvBuffer->getFirstLossSeq(m_iRcvPossibleLossSeq);

        HLOGC(gmlog.Debug, log << "grp:checkBalancingLoss: %" << pkt.getSeqNo() << " SEALS A LOSS, shift to %" << m_iRcvPossibleLossSeq);
        return false;
    }

    // We state that this is the oldest possible loss sequence; just formally check
    int cmp = CSeqNo::seqcmp(pkt.m_iSeqNo, m_RcvLastSeqNo);
    if (cmp < 0)
    {
        HLOGC(gmlog.Debug, log << "grp:checkBalancingLoss: %" << pkt.getSeqNo() << " IN THE PAST");
        return false;
    }

    // We need to check first, if we ALREADY have some older loss candidate,
    // and if so, if the condition for having it "eclipsed" is satisfied.

    bool found_reportable_losses = false, more_losses = false;

    while (m_iRcvPossibleLossSeq != SRT_SEQNO_NONE)
    {
        // We do have a recorded loss before. Get unit information.
        vector<SRTSOCKET> followers;
        m_pRcvBuffer->getUnitSeriesInfo(m_iRcvPossibleLossSeq, m_Group.size(), (followers));

        // The "eclipse" condition is one of two:
        //
        // When the loss (even if divided by other losses) is followed by some
        // number of packets, among which:
        //
        // 1. There is at least one packet from every link.
        // 2. There are at least two packets coming from one of the links.

        HLOGC(gmlog.Debug, log << "grp:checkBalancingLoss: existng %" << m_iRcvPossibleLossSeq << " followed by: "
                << Printable(followers));

        map<SRTSOCKET, size_t> nums;
        FringeValues(followers, (nums));

        IF_HEAVY_LOGGING(const char* which_condition[3] = {"fullcover", "longtail", "both???"});

        bool longtail = false;
        bool fullcover = nums.size() >= m_Group.number_running();
        if (!fullcover)
        {
            int actual_distance = CSeqNo::seqoff(m_iRcvPossibleLossSeq, m_RcvLastSeqNo);

            // The minimum distance is the number of links.
            // This is used always, regardless of other conditions
            longtail = (actual_distance > int(m_Group.size() + 1));

            if (longtail && m_zLongestDistance > m_Group.size())
            {
                // This is a complicated condition. We need to state that
                // the long tail has been exceeded if:
                // 1. We have a long distance measured.
                //    a. If not, fall back to the number of member links.
                //
                // 2. To this value we add 0.2 of the value (minimum 1) to make it
                //    a base value for test if this is exceeded.
                //
                // 3. We check the distance between the packet tested for
                // being a loss (m_iRcvPossibleLossSeq) and the latest received
                // (m_RcvLastSeqNo).

                int32_t basefax = m_zLongestDistance;
                double extrafax = max(basefax * 0.2, 1.0);
                basefax += int(extrafax);

                // Previously it was tested this way to find providers that are longer
                // than given value (here 1). As we currently collect the measurement values
                // as they appear, we don't need to check it now.
                //find_if(nums.begin(), nums.end(), FFringeGreaterThan(1)) != nums.end();

                longtail = (actual_distance > basefax);

                HLOGC(grlog.Debug, log << "grp:checkBalancingLoss: loss-distance=" << actual_distance
                        << (longtail ? " EXCEEDS" : " UNDER") << " the longest tail " << m_zLongestDistance
                        << " stretched to " << basefax);
            }
            else
            {
                HLOGC(grlog.Debug, log << "grp:checkBalancingLoss: loss-distance=" << actual_distance
                        << (longtail ? " EXCEEDS" : " BELOW") << " the group size=" << m_Group.size()
                        << (longtail ? " but not" : " and") << " the tail=" << m_zLongestDistance);
            }
        }
        else
        {
            HLOGC(grlog.Debug, log << "grp:checkBalancingLoss: loss confirmed by " << nums.size() << " sources out of " << m_Group.number_running() << " running");
        }

        if (longtail || fullcover)
        {
            // Extract the whole first loss
            typename CUDT::loss_seqs_t::value_type loss;
            loss.first = m_pRcvBuffer->getFirstLossSeq(m_iRcvPossibleLossSeq, (&loss.second));
            if (loss.first == SRT_SEQNO_NONE)
            {
                HLOGC(gmlog.Debug, log << "... LOSS SEALED (IPE) ???");
                m_iRcvPossibleLossSeq = SRT_SEQNO_NONE;
                break;
            }
            w_losses.push_back(loss);

            found_reportable_losses = true;

            // Save the next found loss
            m_iRcvPossibleLossSeq = m_pRcvBuffer->getFirstLossSeq(CSeqNo::incseq(loss.second));

            HLOGC(gmlog.Debug, log << "... qualified as loss (" << which_condition[(int(fullcover) + 2*int(longtail))-1] << "): %(" << loss.first << " - " << loss.second
                    << "), next loss: %" << m_iRcvPossibleLossSeq);

            if (m_iRcvPossibleLossSeq == SRT_SEQNO_NONE)
            {
                // We extracted all losses
                more_losses = false;
                break;
            }

            // Found at least one reportable loss
            more_losses = true;
            continue;
        }
        else
        {
            HLOGC(gmlog.Debug, log << "... not yet a loss - waiting for possible sealing");
        }

        break;
    }

    // found_reportable_losses = at least one of the so far POTENTIAL loss was confirmed as ACTUAL loss and we report it.
    // more_losses = not all seen losses have been extracted (so don't try to register a new POTENTIAL loss)

    // In case when the above procedure didn't set m_iRcvPossibleLossSeq,
    // check now the CURRENT arrival if it doesn't create a new loss.

    // HERE: if !more_losses, then m_iRcvPossibleLossSeq == SRT_SEQNO_NONE.
    // This condition may change it or leave as is.

    int32_t next_seqno = CSeqNo::incseq(m_RcvLastSeqNo);
    if (!more_losses && CSeqNo::seqcmp(pkt.m_iSeqNo, next_seqno) > 0)
    {
        // NOTE: in case when you have (at least temporarily) only one link,
        // then you have to do the same as with a general case. The above loop
        // had to be performed anyway, but this only touches upon any earlier losses.
        // In this case if we have one link only, do not notify it for the next time,
        // but report it directly instead.
        if (m_Group.size() == 1)
        {
            typename CUDT::loss_seqs_t::value_type loss = make_pair(next_seqno, CSeqNo::decseq(pkt.m_iSeqNo));
            w_losses.push_back(loss);
            HLOGC(gmlog.Debug, log << "grp:checkBalancingLoss: incom %" << pkt.m_iSeqNo << " jumps over expected %" << next_seqno
                    << " - with 1 link only, just reporting");
            return true;
        }

        HLOGC(gmlog.Debug, log << "grp:checkBalancingLoss: incom %" << pkt.m_iSeqNo << " jumps over expected %" << next_seqno
                << " - setting up as loss candidate");
        m_iRcvPossibleLossSeq = next_seqno;
    }

    return found_reportable_losses;
}

bool CUDTGroup::getFirstNoncontSequence(int32_t& w_seq, string& w_log_reason)
{
    ScopedLock buflock (m_RcvBufferLock);
    bool has_followers = m_pRcvBuffer->getContiguousEnd((w_seq));
    if (has_followers)
        w_log_reason = "first lost";
    else
        w_log_reason = "last received";

    return true;
}


void CUDTGroup::close()
{
    // Close all descriptors, then delete the group.
    vector<SRTSOCKET> ids;

    {
        ScopedLock glob(CUDT::uglobal().m_GlobControlLock);
        ScopedLock g(m_GroupLock);

        m_bClosing = true;

        // Copy the list of IDs into the array.
        for (gli_t ig = m_Group.begin(); ig != m_Group.end(); ++ig)
        {
            ids.push_back(ig->id);
            // Immediately cut ties to this group.
            // Just for a case, redispatch the socket, to stay safe.
            CUDTSocket* s = CUDT::uglobal().locateSocket_LOCKED(ig->id);
            if (!s)
            {
                HLOGC(smlog.Debug, log << "group/close: IPE(NF): group member @" << ig->id << " already deleted");
                continue;
            }

            // Make the socket closing BEFORE withdrawing its group membership
            // because a socket created as a group member cannot be valid
            // without the group.
            // This is not true in case of non-managed groups, which
            // only collect sockets, but also non-managed groups should not
            // use common group buffering and tsbpd. Also currently there are
            // no other groups than managed one.
            s->setClosing();

            s->m_GroupOf = NULL;
            s->m_GroupMemberData = NULL;
            HLOGC(smlog.Debug, log << "group/close: CUTTING OFF @" << ig->id << " (found as @" << s->m_SocketID << ") from the group");
        }

        // After all sockets that were group members have their ties cut,
        // the container can be cleared. Note that sockets won't be now
        // removing themselves from the group when closing because they
        // are unaware of being group members.
        m_Group.clear();
        m_PeerGroupID = -1;

        set<int> epollid;
        {
            // Global EPOLL lock must be applied to access any socket's epoll set.
            // This is a set of all epoll ids subscribed to it.
            ScopedLock elock (CUDT::uglobal().m_EPoll.m_EPollLock);
            epollid = m_sPollID; // use move() in C++11
            m_sPollID.clear();
        }

        int no_events = 0;
        for (set<int>::iterator i = epollid.begin(); i != epollid.end(); ++i)
        {
            HLOGC(smlog.Debug, log << "close: CLEARING subscription on E" << (*i) << " of $" << id());
            try
            {
                CUDT::uglobal().m_EPoll.update_usock(*i, id(), &no_events);
            }
            catch (...)
            {
                // May catch an API exception, but this isn't an API call to be interrupted.
            }
            HLOGC(smlog.Debug, log << "close: removing E" << (*i) << " from back-subscribers of $" << id());
        }

        // NOW, the m_GroupLock is released, then m_GlobControlLock.
        // The below code should work with no locks and execute socket
        // closing.
    }

    HLOGC(gmlog.Debug, log << "grp/close: closing $" << m_GroupID << ", closing first " << ids.size() << " sockets:");
    // Close all sockets with unlocked GroupLock
    for (vector<SRTSOCKET>::iterator i = ids.begin(); i != ids.end(); ++i)
    {
        try
        {
            CUDT::uglobal().close(*i);
        }
        catch (CUDTException&)
        {
            HLOGC(gmlog.Debug, log << "grp/close: socket @" << *i << " is likely closed already, ignoring");
        }
    }

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

        // This takes care of the internal part.
        // The external part will be done in Global (CUDTUnited)
    }

    // Release blocked clients
    // XXX This looks like a dead code. Group receiver functions
    // do not use any lock on m_RcvDataLock, it is likely a remainder
    // of the old, internal impementation. 
    // CSync::lock_notify_one(m_RcvDataCond, m_RcvDataLock);
}

// [[using locked(m_Global->m_GlobControlLock)]]
// [[using locked(m_GroupLock)]]
void CUDTGroup::send_CheckValidSockets()
{
    vector<gli_t> toremove;

    for (gli_t d = m_Group.begin(), d_next = d; d != m_Group.end(); d = d_next)
    {
        ++d_next; // it's now safe to erase d
        CUDTSocket* revps = m_Global.locateSocket_LOCKED(d->id);
        if (revps != d->ps)
        {
            // Note: the socket might STILL EXIST, just in the trash, so
            // it can't be found by locateSocket. But it can still be bound
            // to the group. Just mark it broken from upside so that the
            // internal sending procedures will skip it. Removal from the
            // group will happen in GC, which will both remove from
            // group container and cut backward links to the group.

            HLOGC(gmlog.Debug, log << "group/send_CheckValidSockets: socket @" << d->id << " is no longer valid, setting BROKEN in $" << id());
            d->sndstate = SRT_GST_BROKEN;
            d->rcvstate = SRT_GST_BROKEN;
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

    case SRT_GTYPE_BALANCING:
        return sendBalancing(buf, len, (w_mc));

        /* to be implemented
    case SRT_GTYPE_MULTICAST:
        return sendMulticast(buf, len, (w_mc));
        */
    }
}

int CUDTGroup::sendBroadcast(const char* buf, int len, SRT_MSGCTRL& w_mc)
{
    return sendSelectable(buf, len, (w_mc), false);
}

int CUDTGroup::sendBalancing(const char* buf, int len, SRT_MSGCTRL& w_mc)
{
    return sendSelectable(buf, len, (w_mc), true);
}

int CUDTGroup::sendSelectable(const char* buf, int len, SRT_MSGCTRL& w_mc, bool use_select SRT_ATR_UNUSED)
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
    vector<gli_t> idleLinks;
    vector<SRTSOCKET> pendingSockets; // need sock ids as it will be checked out of lock

    int32_t curseq = SRT_SEQNO_NONE;  // The seqno of the first packet of this message.
    int32_t nextseq = SRT_SEQNO_NONE;  // The seqno of the first packet of next message.

    int rstat = -1;

    int                          stat = 0;
    SRT_ATR_UNUSED CUDTException cx(MJ_SUCCESS, MN_NONE, 0);

    vector<gli_t> activeLinks;

    // First, acquire GlobControlLock to make sure all member sockets still exist
    enterCS(m_Global.m_GlobControlLock);
    ScopedLock guard(m_GroupLock);

    if (m_bClosing)
    {
        leaveCS(m_Global.m_GlobControlLock);
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Now, still under lock, check if all sockets still can be dispatched

    // LOCKED: GlobControlLock, GroupLock (RIGHT ORDER!)
    send_CheckValidSockets();
    leaveCS(m_Global.m_GlobControlLock);
    // LOCKED: GroupLock (only)
    // Since this moment GlobControlLock may only be locked if GroupLock is unlocked first.

    if (m_bClosing)
    {
        // No temporary locks here. The group lock is scoped.
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // This simply requires the payload to be sent through every socket in the group
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
    {
        if (d->sndstate != SRT_GST_BROKEN)
        {
            // Check the socket state prematurely in order not to uselessly
            // send over a socket that is broken.
            CUDT* const pu = (d->ps)
                ?  &d->ps->core()
                :  NULL;

            if (!pu || pu->m_bBroken)
            {
                HLOGC(gslog.Debug,
                        log << "grp/sendSelectable: socket @" << d->id << " detected +Broken - transit to BROKEN");
                d->sndstate = SRT_GST_BROKEN;
                d->rcvstate = SRT_GST_BROKEN;
            }
        }

        // Check socket sndstate before sending
        if (d->sndstate == SRT_GST_BROKEN)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendSelectable: socket in BROKEN state: @" << d->id
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
                pendingSockets.push_back(d->id);
                continue;
            }

            HLOGC(gslog.Debug, log << "grp/sendSelectable: socket in IDLE state: @" << d->id << " - will activate it");
            // This is idle, we'll take care of them next time
            // Might be that:
            // - this socket is idle, while some NEXT socket is running
            // - we need at least one running socket to work BEFORE activating the idle one.
            // - if ALL SOCKETS ARE IDLE, then we simply activate the first from the list,
            //   and all others will be activated using the ISN from the first one.
            idleLinks.push_back(d);
            continue;
        }

        if (d->sndstate == SRT_GST_RUNNING)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendSelectable: socket in RUNNING state: @" << d->id << " - will send a payload");
            activeLinks.push_back(d);
            continue;
        }

        HLOGC(gslog.Debug,
              log << "grp/sendSelectable: socket @" << d->id << " not ready, state: " << StateStr(d->sndstate) << "("
                  << int(d->sndstate) << ") - NOT sending, SET AS PENDING");

        pendingSockets.push_back(d->id);
    }

    vector<Sendstate> sendstates;
    if (w_mc.srctime == 0)
        w_mc.srctime = count_microseconds(steady_clock::now().time_since_epoch());

    for (vector<gli_t>::iterator snd = activeLinks.begin(); snd != activeLinks.end(); ++snd)
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
            nextseq = d->ps->core().schedSeqNo();
        }

        const Sendstate cstate = {d->id, &*d, stat, erc};
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

    // TODO: { sendSelectable_ActivateIdleLinks
    for (vector<gli_t>::iterator i = idleLinks.begin(); i != idleLinks.end(); ++i)
    {
        gli_t d       = *i;
        if (!d->ps->m_GroupOf)
            continue;

        int   erc     = 0;
        int   lastseq = d->ps->core().schedSeqNo();
        if (curseq != SRT_SEQNO_NONE && curseq != lastseq)
        {
            HLOGC(gslog.Debug,
                    log << "grp/sendSelectable: socket @" << d->id << ": override snd sequence %" << lastseq << " with %"
                    << curseq << " (diff by " << CSeqNo::seqcmp(curseq, lastseq)
                    << "); SENDING PAYLOAD: " << BufferStamp(buf, len));
            d->ps->core().overrideSndSeqNo(curseq);
        }
        else
        {
            HLOGC(gslog.Debug,
                    log << "grp/sendSelectable: socket @" << d->id << ": sequence remains with original value: %"
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
            nextseq = d->ps->core().schedSeqNo();
            HLOGC(gslog.Debug,
                    log << "@" << d->id << ":... sending SUCCESSFUL %" << curseq << " MEMBER STATUS: RUNNING");
        }

        d->sndresult  = stat;
        d->laststatus = d->ps->getStatus();

        const Sendstate cstate = {d->id, &*d, stat, erc};
        sendstates.push_back(cstate);
    }

    if (nextseq != SRT_SEQNO_NONE)
    {
        HLOGC(gslog.Debug,
              log << "grp/sendSelectable: $" << id() << ": updating current scheduling sequence %" << nextseq);
        m_iLastSchedSeqNo = nextseq;
    }

    // }

    // { send_CheckBrokenSockets()

    if (!pendingSockets.empty())
    {
        HLOGC(gslog.Debug, log << "grp/sendSelectable: found pending sockets, polling them.");

        // These sockets if they are in pending state, they should be added to m_SndEID
        // at the connecting stage.
        CEPoll::fmap_t sready;

        if (m_Global.m_EPoll.empty(*m_SndEpolld))
        {
            // Sanity check - weird pending reported.
            LOGC(gslog.Error,
                 log << "grp/sendSelectable: IPE: reported pending sockets, but EID is empty - wiping pending!");
            copy(pendingSockets.begin(), pendingSockets.end(), back_inserter(wipeme));
        }
        else
        {
            {
                InvertedLock ug(m_GroupLock);

                THREAD_PAUSED();
                m_Global.m_EPoll.swait(
                    *m_SndEpolld, sready, 0, false /*report by retval*/); // Just check if anything happened
                THREAD_RESUMED();
            }

            if (m_bClosing)
            {
                // No temporary locks here. The group lock is scoped.
                throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
            }

            HLOGC(gslog.Debug, log << "grp/sendSelectable: RDY: " << DisplayEpollResults(sready));

            // sockets in EX: should be moved to wipeme.
            for (vector<SRTSOCKET>::iterator i = pendingSockets.begin(); i != pendingSockets.end(); ++i)
            {
                if (CEPoll::isready(sready, *i, SRT_EPOLL_ERR))
                {
                    HLOGC(gslog.Debug,
                          log << "grp/sendSelectable: Socket @" << (*i) << " reported FAILURE - moved to wiped.");
                    // Failed socket. Move d to wipeme. Remove from eid.
                    wipeme.push_back(*i);
                    int no_events = 0;
                    m_Global.m_EPoll.update_usock(m_SndEID, *i, &no_events);
                }
            }

            // After that, all sockets that have been reported
            // as ready to write should be removed from EID. This
            // will also remove those sockets that have been added
            // as redundant links at the connecting stage and became
            // writable (connected) before this function had a chance
            // to check them.
            m_Global.m_EPoll.clear_ready_usocks(*m_SndEpolld, SRT_EPOLL_CONNECT);
        }
    }

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    send_CloseBrokenSockets(wipeme);

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    // }

    // { sendSelectable_CheckBlockedLinks()

    // Alright, we've made an attempt to send a packet over every link.
    // Every operation was done through a non-blocking attempt, so
    // links where sending was blocked have SRT_EASYNCSND error.
    // Links that were successful, have the len value in state.

    // First thing then, find out if at least one link was successful.
    // The first successful link sets the sequence value,
    // the following links derive it. This might be also the first idle
    // link with its random-generated ISN, if there were no active links.

    vector<SocketData*> successful, blocked;

    // This iteration of the state will simply
    // qualify the remaining sockets into three categories:
    //
    // - successful (we only need to know if at least one did)
    // - blocked - if none succeeded, but some blocked, POLL & RETRY.
    // - wipeme - sending failed by any other reason than blocking, remove.

    // Now - sendstates contain directly sockets.
    // In order to update members, you need to have locked:
    // - GlobControlLock to prevent sockets from disappearing or being closed
    // - then GroupLock to latch the validity of m_GroupMemberData field.

    {
        {
            InvertedLock ung (m_GroupLock);
            enterCS(CUDT::uglobal().m_GlobControlLock);
            HLOGC(gslog.Debug, log << "grp/sendSelectable: Locked GlobControlLock, locking back GroupLock");
        }

        // Under this condition, as an unlock-lock cycle was done on m_GroupLock,
        // the Sendstate::it field shall not be used here!
        for (vector<Sendstate>::iterator is = sendstates.begin(); is != sendstates.end(); ++is)
        {
            CUDTSocket* ps = CUDT::uglobal().locateSocket_LOCKED(is->id);

            // Is the socket valid? If not, simply SKIP IT. Nothing to be done with it,
            // it's already deleted.
            if (!ps)
                continue;

            // Is the socket still group member? If not, SKIP IT. It could only be taken ownership
            // by being explicitly closed and so it's deleted from the container.
            if (!ps->m_GroupOf)
                continue;

            // Now we are certain that m_GroupMemberData is valid.
            SocketData* d = ps->m_GroupMemberData;

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
        leaveCS(CUDT::uglobal().m_GlobControlLock);
    }

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
    {
        HLOGC(gslog.Debug, log << "grp/sendSelectable: GROUP CLOSED, ABANDONING");
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
        for (vector<SocketData*>::iterator b = blocked.begin(); b != blocked.end(); ++b)
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
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        if (!m_bSynSending)
        {
            throw CUDTException(MJ_AGAIN, MN_WRAVAIL, 0);
        }

        HLOGC(gslog.Debug, log << "grp/sendSelectable: all blocked, trying to common-block on epoll...");

        // XXX TO BE REMOVED. Sockets should be subscribed in m_SndEID at connecting time
        // (both srt_connect and srt_accept).

        // None was successful, but some were blocked. It means that we
        // haven't sent the payload over any link so far, so we still have
        // a chance to retry.
        int modes = SRT_EPOLL_OUT | SRT_EPOLL_ERR;
        for (vector<SocketData*>::iterator b = blocked.begin(); b != blocked.end(); ++b)
        {
            HLOGC(gslog.Debug,
                  log << "Will block on blocked socket @" << (*b)->id << " as only blocked socket remained");
            CUDT::uglobal().epoll_add_usock_INTERNAL(m_SndEID, (*b)->ps, &modes);
        }

        int            blst = 0;
        CEPoll::fmap_t sready;

        {
            // Lift the group lock for a while, to avoid possible deadlocks.
            InvertedLock ug(m_GroupLock);
            HLOGC(gslog.Debug, log << "grp/sendSelectable: blocking on any of blocked sockets to allow sending");

            // m_iSndTimeOut is -1 by default, which matches the meaning of waiting forever
            THREAD_PAUSED();
            blst = m_Global.m_EPoll.swait(*m_SndEpolld, sready, m_iSndTimeOut);
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
            activeLinks.clear();
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
                    activeLinks.push_back(dd);
            }

            for (vector<gli_t>::iterator snd = activeLinks.begin(); snd != activeLinks.end(); ++snd)
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
                    curseq = w_mc.pktseq;

                const Sendstate cstate = {d->id, &*d, stat, erc};
                sendstates.push_back(cstate);
                d->sndresult  = stat;
                d->laststatus = d->ps->getStatus();
            }

            // This time only check if any were successful.
            // All others are wipeme.
            // NOTE: m_GroupLock is continuously locked - you can safely use Sendstate::it field.
            for (vector<Sendstate>::iterator is = sendstates.begin(); is != sendstates.end(); ++is)
            {
                if (is->stat == len)
                {
                    // Successful.
                    successful.push_back(is->mb);
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
                is->mb->sndstate = SRT_GST_BROKEN;
            }
        }
    }

    // }

    if (none_succeeded)
    {
        HLOGC(gslog.Debug, log << "grp/sendSelectable: all links broken (none succeeded to send a payload)");
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
        // Reparse error code, if set.
        // It might be set, if the last operation was failed.
        // If any operation succeeded, this will not be executed anyway.
        CodeMajor major = CodeMajor(ercode ? ercode / 1000 : MJ_CONNECTION);
        CodeMinor minor = CodeMinor(ercode ? ercode % 1000 : MN_CONNLOST);

        throw CUDTException(major, minor, 0);
    }

    // Now that at least one link has succeeded, update sending stats.
    m_stats.sent.count(len);

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

        ready_again = ready_again || d->ps->writeReady();
    }
    w_mc.grpdata_size = i;

    if (!ready_again)
    {
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
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

// [[using locked(CUDT::uglobal()->m_GlobControLock)]]
// [[using locked(m_GroupLock)]]
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


// Old unused procedure.
// Leaving here for historical reasons.
#if 0
void CUDTGroup::recv_CollectAliveAndBroken(vector<CUDTSocket*>& alive, set<CUDTSocket*>& broken)
{
#if ENABLE_HEAVY_LOGGING
    std::ostringstream ds;
    ds << "E(" << m_RcvEID << ") ";
#define HCLOG(expr) expr
#else
#define HCLOG(x) if (false) {}
#endif

    alive.reserve(m_Group.size());

    HLOGC(grlog.Debug, log << "group/recv: Reviewing member sockets for polling");
    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        if (gi->laststatus == SRTS_CONNECTING)
        {
            HCLOG(ds << "@" << gi->id << "<pending> ");
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

        alive.push_back(gi->ps);
        HCLOG(ds << "@" << gi->id << "[READ] ");
    }

    HLOGC(grlog.Debug, log << "group/recv: " << ds.str() << " --> EPOLL/SWAIT");
#undef HCLOG
}

vector<CUDTSocket*> CUDTGroup::recv_WaitForReadReady(const vector<CUDTSocket*>& aliveMembers, set<CUDTSocket*>& w_broken)
{
    if (aliveMembers.empty())
    {
        LOGC(grlog.Error, log << "group/recv: all links broken");
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
    }

    for (vector<CUDTSocket*>::const_iterator i = aliveMembers.begin(); i != aliveMembers.end(); ++i)
    {
        // NOT using the official srt_epoll_add_usock because this will do socket dispatching,
        // which requires lock on m_GlobControlLock, while this lock cannot be applied without
        // first unlocking m_GroupLock.
        const int read_modes = SRT_EPOLL_IN | SRT_EPOLL_ERR;
        CUDT::uglobal().epoll_add_usock_INTERNAL(m_RcvEID, *i, &read_modes);
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

    // The non-blocking mode would need to simply check the readiness
    // with only immediate report, and read-readiness would have to
    // be done in background.

    // In blocking mode, use m_iRcvTimeOut, which's default value -1
    // means to block indefinitely, also in swait().
    // In non-blocking mode use 0, which means to always return immediately.
    int timeout = m_bSynRecving ? m_iRcvTimeOut : 0;
    int nready = 0;
    // Poll on this descriptor until reading is available, indefinitely.
    CEPoll::fmap_t sready;

    // GlobControlLock is required for dispatching the sockets.
    // Therefore it must be applied only when GroupLock is off.
    {
        // This call may wait indefinite time, so GroupLock must be unlocked.
        InvertedLock ung (m_GroupLock);
        THREAD_PAUSED();
        nready  = m_Global.m_EPoll.swait(*m_RcvEpolld, sready, timeout, false /*report by retval*/);
        THREAD_RESUMED();

        // HERE GlobControlLock is locked first, then GroupLock is applied back
        enterCS(CUDT::uglobal().m_GlobControlLock);
    }
    // BOTH m_GlobControlLock AND m_GroupLock are locked here.

    HLOGC(grlog.Debug, log << "group/recv: " << nready << " RDY: " << DisplayEpollResults(sready));

    if (nready == 0)
    {
        // GlobControlLock is applied manually, so unlock manually.
        // GroupLock will be unlocked as per scope.
        leaveCS(CUDT::uglobal().m_GlobControlLock);
        // This can only happen when 0 is passed as timeout and none is ready.
        // And 0 is passed only in non-blocking mode. So this is none ready in
        // non-blocking mode.
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
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
        /*TO*/ std::inserter(w_broken, w_broken.begin()),
        /*VIA*/ FLookupSocketWithEvent_LOCKED(&m_Global, SRT_EPOLL_ERR));

    
    // If this set is empty, it won't roll even once, therefore output
    // will be surely empty. This will be checked then same way as when
    // reading from every socket resulted in error.
    vector<CUDTSocket*> readReady;
    readReady.reserve(aliveMembers.size());
    for (vector<CUDTSocket*>::const_iterator sockiter = aliveMembers.begin(); sockiter != aliveMembers.end(); ++sockiter)
    {
        CUDTSocket* sock = *sockiter;
        const CEPoll::fmap_t::const_iterator ready_iter = sready.find(sock->m_SocketID);
        if (ready_iter != sready.end())
        {
            if (ready_iter->second & SRT_EPOLL_ERR)
                continue; // broken already

            if ((ready_iter->second & SRT_EPOLL_IN) == 0)
                continue; // not ready for reading

            readReady.push_back(*sockiter);
        }
        else
        {
            // No read-readiness reported by epoll, but probably missed or not yet handled
            // as the receiver buffer is read-ready.
            ScopedLock lg(sock->core().m_RcvBufferLock);
            if (sock->core().m_pRcvBuffer && sock->core().m_pRcvBuffer->isRcvDataReady())
                readReady.push_back(sock);
        }
    }
    
    leaveCS(CUDT::uglobal().m_GlobControlLock);

    return readReady;
}

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
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, true);
    }
}

int32_t CUDTGroup::getRcvBaseSeqNo()
{
    ScopedLock lg(m_GroupLock);
    return m_RcvBaseSeqNo;
}
#endif

void CUDTGroup::updateWriteState()
{
    ScopedLock lg(m_GroupLock);
    m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, true);
}

#if 0
/// Validate iPktSeqno is in range
/// (iBaseSeqno - m_iSeqNoTH/2; iBaseSeqno + m_iSeqNoTH).
///
/// EXPECT_EQ(isValidSeqno(125, 124), true); // behind
/// EXPECT_EQ(isValidSeqno(125, 125), true); // behind
/// EXPECT_EQ(isValidSeqno(125, 126), true); // the next in order
///
/// EXPECT_EQ(isValidSeqno(0, 0x3FFFFFFF - 2), true);  // ahead, but ok.
/// EXPECT_EQ(isValidSeqno(0, 0x3FFFFFFF - 1), false); // too far ahead.
/// EXPECT_EQ(isValidSeqno(0x3FFFFFFF + 2, 0x7FFFFFFF), false); // too far ahead.
/// EXPECT_EQ(isValidSeqno(0x3FFFFFFF + 3, 0x7FFFFFFF), true); // ahead, but ok.
/// EXPECT_EQ(isValidSeqno(0x3FFFFFFF, 0x1FFFFFFF + 2), false); // too far (behind)
/// EXPECT_EQ(isValidSeqno(0x3FFFFFFF, 0x1FFFFFFF + 3), true); // behind, but ok
/// EXPECT_EQ(isValidSeqno(0x70000000, 0x0FFFFFFF), true); // ahead, but ok
/// EXPECT_EQ(isValidSeqno(0x70000000, 0x30000000 - 2), false); // too far ahead.
/// EXPECT_EQ(isValidSeqno(0x70000000, 0x30000000 - 3), true); // ahead, but ok
/// EXPECT_EQ(isValidSeqno(0x0FFFFFFF, 0), true);
/// EXPECT_EQ(isValidSeqno(0x0FFFFFFF, 0x7FFFFFFF), true);
/// EXPECT_EQ(isValidSeqno(0x0FFFFFFF, 0x70000000), false);
/// EXPECT_EQ(isValidSeqno(0x0FFFFFFF, 0x70000001), false);
/// EXPECT_EQ(isValidSeqno(0x0FFFFFFF, 0x70000002), true);  // behind by 536870910
/// EXPECT_EQ(isValidSeqno(0x0FFFFFFF, 0x70000003), true);
///
/// @return false if @a iPktSeqno is not inside the valid range; otherwise true.
static bool isValidSeqno(int32_t iBaseSeqno, int32_t iPktSeqno)
{
    const int32_t iLenAhead = CSeqNo::seqlen(iBaseSeqno, iPktSeqno);
    if (iLenAhead >= 0 && iLenAhead < CSeqNo::m_iSeqNoTH)
        return true;

    const int32_t iLenBehind = CSeqNo::seqlen(iPktSeqno, iBaseSeqno);
    if (iLenBehind >= 0 && iLenBehind < CSeqNo::m_iSeqNoTH / 2)
        return true;

    return false;
}

int CUDTGroup::recv_old(char* buf, int len, SRT_MSGCTRL& w_mc)
{
    // First, acquire GlobControlLock to make sure all member sockets still exist
    enterCS(m_Global.m_GlobControlLock);
    ScopedLock guard(m_GroupLock);

    if (m_bClosing)
    {
        // The group could be set closing in the meantime, but if
        // this is only about to be set by another thread, this thread
        // must fist wait for being able to acquire this lock.
        // The group will not be deleted now because it is added usage counter
        // by this call, but will be released once it exits.
        leaveCS(m_Global.m_GlobControlLock);
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Now, still under lock, check if all sockets still can be dispatched
    send_CheckValidSockets();
    leaveCS(m_Global.m_GlobControlLock);

    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    // Later iteration over it might be less efficient than
    // by vector, but we'll also often try to check a single id
    // if it was ever seen broken, so that it's skipped.
    set<CUDTSocket*> broken;

    for (;;)
    {
        if (!m_bOpened || !m_bConnected)
        {
            LOGC(grlog.Error,
                 log << boolalpha << "grp/recv: $" << id() << ": ABANDONING: opened=" << m_bOpened
                     << " connected=" << m_bConnected);
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        }

        vector<CUDTSocket*> aliveMembers;
        recv_CollectAliveAndBroken(aliveMembers, broken);
        if (aliveMembers.empty())
        {
            LOGC(grlog.Error, log << "grp/recv: ALL LINKS BROKEN, ABANDONING.");
            m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        }

        vector<CUDTSocket*> readySockets;
        if (m_bSynRecving)
            readySockets = recv_WaitForReadReady(aliveMembers, broken);
        else
            readySockets = aliveMembers;

        if (m_bClosing)
        {
            HLOGC(grlog.Debug, log << "grp/recv: $" << id() << ": GROUP CLOSED, ABANDONING.");
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        // Find the first readable packet among all member sockets.
        CUDTSocket*               socketToRead = NULL;
        CRcvBuffer::PacketInfo infoToRead   = {-1, false, time_point()};
        for (vector<CUDTSocket*>::const_iterator si = readySockets.begin(); si != readySockets.end(); ++si)
        {
            CUDTSocket* ps = *si;

            ScopedLock lg(ps->core().m_RcvBufferLock);
            if (m_RcvBaseSeqNo != SRT_SEQNO_NONE)
            {
                // Drop here to make sure the getFirstReadablePacketInfo() below return fresher packet.
                int cnt = ps->core().rcvDropTooLateUpTo(CSeqNo::incseq(m_RcvBaseSeqNo));
                if (cnt > 0)
                {
                    HLOGC(grlog.Debug,
                          log << "grp/recv: $" << id() << ": @" << ps->m_SocketID << ": dropped " << cnt
                              << " packets before reading: m_RcvBaseSeqNo=" << m_RcvBaseSeqNo);
                }
            }

            const CRcvBuffer::PacketInfo info =
                ps->core().m_pRcvBuffer->getFirstReadablePacketInfo(steady_clock::now());
            if (info.seqno == SRT_SEQNO_NONE)
            {
                HLOGC(grlog.Debug, log << "grp/recv: $" << id() << ": @" << ps->m_SocketID << ": Nothing to read.");
                continue;
            }
            // We need to qualify the sequence, just for a case.
            if (m_RcvBaseSeqNo != SRT_SEQNO_NONE && !isValidSeqno(m_RcvBaseSeqNo, info.seqno))
            {
                LOGC(grlog.Error,
                     log << "grp/recv: $" << id() << ": @" << ps->m_SocketID << ": SEQUENCE DISCREPANCY: base=%"
                         << m_RcvBaseSeqNo << " vs pkt=%" << info.seqno << ", setting ESECFAIL");
                ps->core().m_bBroken = true;
                broken.insert(ps);
                continue;
            }
            if (socketToRead == NULL || CSeqNo::seqcmp(info.seqno, infoToRead.seqno) < 0)
            {
                socketToRead = ps;
                infoToRead   = info;
            }
        }

        if (socketToRead == NULL)
        {
            if (m_bSynRecving)
            {
                HLOGC(grlog.Debug,
                      log << "grp/recv: $" << id() << ": No links reported any fresher packet, re-polling.");
                continue;
            }
            else
            {
                HLOGC(grlog.Debug,
                      log << "grp/recv: $" << id() << ": No links reported any fresher packet, clearing readiness.");
                m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
                throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
            }
        }
        else
        {
            HLOGC(grlog.Debug,
                  log << "grp/recv: $" << id() << ": Found first readable packet from @" << socketToRead->m_SocketID
                      << ": seq=" << infoToRead.seqno << " gap=" << infoToRead.seq_gap
                      << " time=" << FormatTime(infoToRead.tsbpd_time));
        }

        const int res = socketToRead->core().receiveMessage((buf), len, (w_mc), CUDTUnited::ERH_RETURN);
        HLOGC(grlog.Debug,
              log << "grp/recv: $" << id() << ": @" << socketToRead->m_SocketID << ": Extracted data with %"
                  << w_mc.pktseq << " #" << w_mc.msgno << ": " << (res <= 0 ? "(NOTHING)" : BufferStamp(buf, res)));
        if (res == 0)
        {
            LOGC(grlog.Warn,
                 log << "grp/recv: $" << id() << ": @" << socketToRead->m_SocketID << ": Retrying next socket...");
            // This socket will not be socketToRead in the next turn because receiveMessage() return 0 here.
            continue;
        }
        if (res == SRT_ERROR)
        {
            LOGC(grlog.Warn,
                 log << "grp/recv: $" << id() << ": @" << socketToRead->m_SocketID << ": " << srt_getlasterror_str()
                     << ". Retrying next socket...");
            broken.insert(socketToRead);
            continue;
        }
        fillGroupData((w_mc), w_mc);

        HLOGC(grlog.Debug,
              log << "grp/recv: $" << id() << ": Update m_RcvBaseSeqNo: %" << m_RcvBaseSeqNo << " -> %" << w_mc.pktseq);
        m_RcvBaseSeqNo = w_mc.pktseq;

        // Update stats as per delivery
        m_stats.recv.count(res);
        updateAvgPayloadSize(res);

        for (vector<CUDTSocket*>::const_iterator si = aliveMembers.begin(); si != aliveMembers.end(); ++si)
        {
            CUDTSocket* ps = *si;
            ScopedLock  lg(ps->core().m_RcvBufferLock);
            if (m_RcvBaseSeqNo != SRT_SEQNO_NONE)
            {
                int cnt = ps->core().rcvDropTooLateUpTo(CSeqNo::incseq(m_RcvBaseSeqNo));
                if (cnt > 0)
                {
                    HLOGC(grlog.Debug,
                          log << "grp/recv: $" << id() << ": @" << ps->m_SocketID << ": dropped " << cnt
                              << " packets after reading: m_RcvBaseSeqNo=" << m_RcvBaseSeqNo);
                }
            }
        }
        for (vector<CUDTSocket*>::const_iterator si = aliveMembers.begin(); si != aliveMembers.end(); ++si)
        {
            CUDTSocket* ps = *si;
            if (!ps->core().isRcvBufferReady())
                m_Global.m_EPoll.update_events(ps->m_SocketID, ps->core().m_sPollID, SRT_EPOLL_IN, false);
        }

        return res;
    }
    LOGC(grlog.Error, log << "grp/recv: UNEXPECTED RUN PATH, ABANDONING.");
    m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
    throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
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
            HLOGC(grlog.Debug,
                  log << "group/recv: Base %" << m_RcvBaseSeqNo << " ahead delivery POSSIBLE %" << a.mctrl.pktseq
                      << " #" << a.mctrl.msgno << " from @" << i->first << ")");
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

#endif // block by if 0

const char* CUDTGroup::StateStr(CUDTGroup::GroupState st)
{
    static const char* const states[] = {"PENDING", "IDLE", "RUNNING", "BROKEN"};
    static const size_t      size     = Size(states);
    static const char* const unknown  = "UNKNOWN";
    if (size_t(st) < size)
        return states[st];
    return unknown;
}


// The REAL version for the new group receiver.
// 
int CUDTGroup::recv(char* data, int len, SRT_MSGCTRL& w_mctrl)
{
    CUniqueSync tscond (m_RcvDataLock, m_RcvTsbPdCond);

    /* XXX DEBUG STUFF - enable when required
       char charbool[2] = {'0', '1'};
       char ptrn [] = "RECVMSG/BEGIN BROKEN 1 CONN 1 CLOSING 1 SYNCR 1 NMSG                                ";
       int pos [] = {21, 28, 38, 46, 53};
       ptrn[pos[0]] = charbool[m_bBroken];
       ptrn[pos[1]] = charbool[m_bConnected];
       ptrn[pos[2]] = charbool[m_bClosing];
       ptrn[pos[3]] = charbool[m_config.m_bSynRecving];
       int wrtlen = sprintf(ptrn + pos[4], "%d", m_pRcvBuffer->getRcvMsgNum());
       strcpy(ptrn + pos[4] + wrtlen, "\n");
       fputs(ptrn, stderr);
    // */

    if (m_bClosing)
    {
        HLOGC(arlog.Debug, log << CONID() << "grp:recv: CONNECTION BROKEN - reading from recv buffer just for formality");

        int as_result = 0;
        {
            ScopedLock lk (m_RcvBufferLock);
            bool ready = m_pRcvBuffer->isRcvDataReady(steady_clock::now());

            if (ready)
            {
                as_result = m_pRcvBuffer->readMessage(data, len, (&w_mctrl));
            }
        }

        {
            ScopedLock lk (m_GroupLock);
            fillGroupData((w_mctrl), w_mctrl);
        }

        const int res = as_result;

        w_mctrl.srctime = 0;

        // Kick TsbPd thread to schedule next wakeup (if running)
        if (m_bTsbPd)
        {
            HLOGP(tslog.Debug, "SIGNAL TSBPD thread to schedule wakeup FOR EXIT");
            tscond.notify_all();
        }
        else
        {
            HLOGP(tslog.Debug, "NOT pinging TSBPD - not set");
        }

        if (!isRcvBufferReady())
        {
            // read is not available any more
            m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
        }

        if (res == 0)
        {
            if (!m_bOPT_MessageAPI && !m_bOpened)
                return 0;
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else
            return res;
    }

    pair<int32_t, int32_t> seqrange;

    if (!m_bSynRecving)
    {
        HLOGC(arlog.Debug, log << CONID() << "grp:recv: BEGIN ASYNC MODE. Going to extract payload size=" << len);

        int as_result = 0;
        {
            ScopedLock lk (m_RcvBufferLock);
            bool ready = m_pRcvBuffer->isRcvDataReady(steady_clock::now());

            if (ready)
            {
                as_result = m_pRcvBuffer->readMessage(data, len, (&w_mctrl), (&seqrange));
            }
        }

        {
            ScopedLock lk (m_GroupLock);
            fillGroupData((w_mctrl), w_mctrl);
        }

        const int res = as_result;

        HLOGC(arlog.Debug, log << CONID() << "AFTER readMsg: (NON-BLOCKING) result=" << res);

        if (res == 0)
        {
            // read is not available any more
            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
            {
                HLOGC(arlog.Debug, log << "grp:recv: nothing to read, SIGNAL TSBPD (" << (m_bTsbpdWaitForExtraction ? "" : "un") << "expected), return AGAIN");
                tscond.notify_all();
            }
            else
            {
                HLOGP(arlog.Debug, "grp:recv: nothing to read, return AGAIN");
            }

            // Shut up EPoll if no more messages in non-blocking mode
            CUDT::uglobal().m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
            // Forced to return 0 instead of throwing exception, in case of AGAIN/READ
            throw CUDTException(MJ_AGAIN, MN_RDAVAIL, 0);
        }

        if (!m_pRcvBuffer->isRcvDataReady(steady_clock::now()))
        {
            // Kick TsbPd thread to schedule next wakeup (if running)
            if (m_bTsbPd)
            {
                HLOGC(arlog.Debug, log << "grp:recv: ONE PACKET READ, but no more avail, SUGNAL TSBPD (" << (m_bTsbpdWaitForExtraction ? "" : "un") << "expected), return AGAIN");
                tscond.notify_all();
            }
            else
            {
                HLOGP(arlog.Debug, "grp:recv: DATA READ, but nothing more");
            }

            // Shut up EPoll if no more messages in non-blocking mode
            m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);

        }
        return res;
    }

    HLOGC(arlog.Debug, log << CONID() << "grp:recv: BEGIN SYNC MODE. Going to extract payload size max=" << len);

    int  res     = 0;
    bool timeout = false;
    // Do not block forever, check connection status each 1 sec.
    const steady_clock::duration recv_timeout = m_iRcvTimeOut < 0 ? seconds_from(1) : milliseconds_from(m_iRcvTimeOut);

    CSync recv_cond (m_RcvDataCond, tscond.locker());

    do
    {
        if (stillConnected() && !timeout && !isRcvBufferReady())
        {
            /* Kick TsbPd thread to schedule next wakeup (if running) */
            if (m_bTsbPd)
            {
                // XXX Experimental, so just inform:
                // Check if the last check of isRcvDataReady has returned any "next time for a packet".
                // If so, then it means that TSBPD has fallen asleep only up to this time, so waking it up
                // would be "spurious". If a new packet comes ahead of the packet which's time is returned
                // in tstime (as TSBPD sleeps up to then), the procedure that receives it is responsible
                // of kicking TSBPD.
                HLOGC(tslog.Debug, log << CONID() << "grp:recv: SIGNAL TSBPD" << (m_bTsbpdWaitForNewPacket ? " (spurious)" : ""));
                tscond.notify_one();
            }

            THREAD_PAUSED();
            do
            {
                // `wait_for(recv_timeout)` wouldn't be correct here. Waiting should be
                // only until the time that is now + timeout since the first moment
                // when this started, or sliced-waiting for 1 second, if timtout is
                // higher than this.
                const steady_clock::time_point exptime = steady_clock::now() + recv_timeout;

                HLOGC(tslog.Debug,
                      log << CONID() << "grp:recv: fall asleep up to TS=" << FormatTime(exptime)
                          << " lock=" << (&m_RcvDataLock) << " cond=" << (&m_RcvDataCond));

                if (!recv_cond.wait_until(exptime))
                {
                    if (m_iRcvTimeOut >= 0) // otherwise it's "no timeout set"
                        timeout = true;
                    HLOGP(tslog.Debug,
                          "grp:recv: DATA COND: EXPIRED -- checking connection conditions and rolling again");
                }
                else
                {
                    HLOGP(tslog.Debug, "grp:recv: DATA COND: KICKED.");
                }
            } while (stillConnected() && !timeout && (!isRcvBufferReady()));
            THREAD_RESUMED();

            HLOGC(tslog.Debug,
                  log << CONID() << "grp:recv: lock-waiting loop exited: stillConntected=" << stillConnected()
                      << " timeout=" << timeout << " data-ready=" << isRcvBufferReady());
        }

        /* XXX DEBUG STUFF - enable when required
        LOGC(arlog.Debug, "RECVMSG/GO-ON BROKEN " << m_bBroken << " CONN " << m_bConnected
                << " CLOSING " << m_bClosing << " TMOUT " << timeout
                << " NMSG " << m_pRcvBuffer->getRcvMsgNum());
                */

        enterCS(m_RcvBufferLock);
        res = m_pRcvBuffer->readMessage((data), len, &w_mctrl);
        leaveCS(m_RcvBufferLock);
        HLOGC(arlog.Debug, log << CONID() << "AFTER readMsg: (BLOCKING) result=" << res);

        {
            ScopedLock lk (m_GroupLock);
            fillGroupData((w_mctrl), w_mctrl);
        }


        if (m_bClosing)
        {
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }
        else if (!m_bConnected)
        {
            throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
        }
    } while ((res == 0) && !timeout);

    if (!isRcvBufferReady())
    {
        // Falling here means usually that res == 0 && timeout == true.
        // res == 0 would repeat the above loop, unless there was also a timeout.
        // timeout has interrupted the above loop, but with res > 0 this condition
        // wouldn't be satisfied.

        // read is not available any more

        // Kick TsbPd thread to schedule next wakeup (if running)
        if (m_bTsbPd)
        {
            HLOGP(tslog.Debug, "recvmsg: SIGNAL TSBPD (buffer empty)");
            tscond.notify_all();
        }

        // Shut up EPoll if no more messages in non-blocking mode
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, false);
    }

    // Unblock when required
    // LOGC(tslog.Debug, "RECVMSG/EXIT RES " << res << " RCVTIMEOUT");

    if ((res <= 0) && (m_iRcvTimeOut >= 0))
    {
        throw CUDTException(MJ_AGAIN, MN_XMTIMEOUT, 0);
    }

    return res;
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

    perf->pktSentUnique = m_stats.sent.trace.count();
    perf->pktRecvUnique = m_stats.recv.trace.count();
    perf->pktRcvDrop    = m_stats.recvDrop.trace.count();

    perf->byteSentUnique = m_stats.sent.trace.bytesWithHdr();
    perf->byteRecvUnique = m_stats.recv.trace.bytesWithHdr();
    perf->byteRcvDrop    = m_stats.recvDrop.trace.bytesWithHdr();

    perf->pktSentUniqueTotal = m_stats.sent.total.count();
    perf->pktRecvUniqueTotal = m_stats.recv.total.count();
    perf->pktRcvDropTotal    = m_stats.recvDrop.total.count();

    perf->byteSentUniqueTotal = m_stats.sent.total.bytesWithHdr();
    perf->byteRecvUniqueTotal = m_stats.recv.total.bytesWithHdr();
    perf->byteRcvDropTotal    = m_stats.recvDrop.total.bytesWithHdr();

    const double interval = static_cast<double>(count_microseconds(currtime - m_stats.tsLastSampleTime));
    perf->mbpsSendRate    = double(perf->byteSent) * 8.0 / interval;
    perf->mbpsRecvRate    = double(perf->byteRecv) * 8.0 / interval;

    if (clear)
    {
        m_stats.reset();
    }
}

/// @brief Compares group members by their weight (higher weight comes first).
struct FCompareByWeight
{
    typedef CUDTGroup::gli_t gli_t;

    /// @returns true if the first argument is less than (i.e. is ordered before) the second.
    bool operator()(const gli_t preceding, const gli_t succeeding)
    {
        return preceding->weight > succeeding->weight;
    }
};

// [[using maybe_locked(this->m_GroupLock)]]
BackupMemberState CUDTGroup::sendBackup_QualifyIfStandBy(const gli_t d)
{
    if (!d->ps)
        return BKUPST_BROKEN;

    const SRT_SOCKSTATUS st = d->ps->getStatus();
    // If the socket is already broken, move it to broken.
    if (int(st) >= int(SRTS_BROKEN))
    {
        HLOGC(gslog.Debug,
            log << "CUDTGroup::send.$" << id() << ": @" << d->id << " became " << SockStatusStr(st)
            << ", WILL BE CLOSED.");
        return BKUPST_BROKEN;
    }

    if (st != SRTS_CONNECTED)
    {
        HLOGC(gslog.Debug, log << "CUDTGroup::send. @" << d->id << " is still " << SockStatusStr(st) << ", skipping.");
        return BKUPST_PENDING;
    }

    return BKUPST_STANDBY;
}

// [[using maybe_locked(this->m_GroupLock)]]
bool CUDTGroup::send_CheckIdle(const gli_t d, vector<SRTSOCKET>& w_wipeme, vector<SRTSOCKET>& w_pendingSockets)
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
        w_pendingSockets.push_back(d->id);
        return false;
    }

    return true;
}


#if SRT_DEBUG_BONDING_STATES
class StabilityTracer
{
public:
    StabilityTracer()
    {
    }

    ~StabilityTracer()
    {
        srt::sync::ScopedLock lck(m_mtx);
        m_fout.close();
    }

    void trace(const CUDT& u, const srt::sync::steady_clock::time_point& currtime, uint32_t activation_period_us,
        int64_t stability_tmo_us, const std::string& state, uint16_t weight)
    {
        srt::sync::ScopedLock lck(m_mtx);
        create_file();

        m_fout << srt::sync::FormatTime(currtime) << ",";
        m_fout << u.id() << ",";
        m_fout << weight << ",";
        m_fout << u.peerLatency_us() << ",";
        m_fout << u.SRTT() << ",";
        m_fout << u.RTTVar() << ",";
        m_fout << stability_tmo_us << ",";
        m_fout << count_microseconds(currtime - u.lastRspTime()) << ",";
        m_fout << state << ",";
        m_fout << (srt::sync::is_zero(u.freshActivationStart()) ? -1 : (count_microseconds(currtime - u.freshActivationStart()))) << ",";
        m_fout << activation_period_us << "\n";
        m_fout.flush();
    }

private:
    void print_header()
    {
        //srt::sync::ScopedLock lck(m_mtx);
        m_fout << "Timepoint,SocketID,weight,usLatency,usRTT,usRTTVar,usStabilityTimeout,usSinceLastResp,State,usSinceActivation,usActivationPeriod\n";
    }

    void create_file()
    {
        if (m_fout.is_open())
            return;

        std::string str_tnow = srt::sync::FormatTimeSys(srt::sync::steady_clock::now());
        str_tnow.resize(str_tnow.size() - 7); // remove trailing ' [SYST]' part
        while (str_tnow.find(':') != std::string::npos) {
            str_tnow.replace(str_tnow.find(':'), 1, 1, '_');
        }
        const std::string fname = "stability_trace_" + str_tnow + ".csv";
        m_fout.open(fname, std::ofstream::out);
        if (!m_fout)
            std::cerr << "IPE: Failed to open " << fname << "!!!\n";

        print_header();
    }

private:
    srt::sync::Mutex m_mtx;
    std::ofstream m_fout;
};

StabilityTracer s_stab_trace;
#endif

void CUDTGroup::sendBackup_QualifyMemberStates(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime)
{
    // First, check status of every link - no matter if idle or active.
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
    {
        if (d->sndstate != SRT_GST_BROKEN)
        {
            // Check the socket state prematurely in order not to uselessly
            // send over a socket that is broken.
            CUDT* const pu = (d->ps)
                ?  &d->ps->core()
                :  NULL;

            if (!pu || pu->m_bBroken)
            {
                HLOGC(gslog.Debug, log << "grp/sendBackup: socket @" << d->id << " detected +Broken - transit to BROKEN");
                d->sndstate = SRT_GST_BROKEN;
                d->rcvstate = SRT_GST_BROKEN;
            }
        }

        // Check socket sndstate before sending
        if (d->sndstate == SRT_GST_BROKEN)
        {
            HLOGC(gslog.Debug,
                  log << "grp/sendBackup: socket in BROKEN state: @" << d->id
                      << ", sockstatus=" << SockStatusStr(d->ps ? d->ps->getStatus() : SRTS_NONEXIST));
            sendBackup_AssignBackupState(d->ps->core(), BKUPST_BROKEN, currtime);
            w_sendBackupCtx.recordMemberState(&(*d), BKUPST_BROKEN);
#if SRT_DEBUG_BONDING_STATES
            s_stab_trace.trace(d->ps->core(), currtime, 0, 0, stateToStr(BKUPST_BROKEN), d->weight);
#endif
            continue;
        }

        if (d->sndstate == SRT_GST_IDLE)
        {
            const BackupMemberState idle_state = sendBackup_QualifyIfStandBy(d);
            sendBackup_AssignBackupState(d->ps->core(), idle_state, currtime);
            w_sendBackupCtx.recordMemberState(&(*d), idle_state);

            if (idle_state == BKUPST_STANDBY)
            {
                // TODO: Check if this is some abandoned logic.
                sendBackup_CheckIdleTime(d);
            }
#if SRT_DEBUG_BONDING_STATES
            s_stab_trace.trace(d->ps->core(), currtime, 0, 0, stateToStr(idle_state), d->weight);
#endif
            continue;
        }

        if (d->sndstate == SRT_GST_RUNNING)
        {
            const BackupMemberState active_state = sendBackup_QualifyActiveState(d, currtime);
            sendBackup_AssignBackupState(d->ps->core(), active_state, currtime);
            w_sendBackupCtx.recordMemberState(&(*d), active_state);
#if SRT_DEBUG_BONDING_STATES
            s_stab_trace.trace(d->ps->core(), currtime, 0, 0, stateToStr(active_state), d->weight);
#endif
            continue;
        }

        HLOGC(gslog.Debug,
              log << "grp/sendBackup: socket @" << d->id << " not ready, state: " << StateStr(d->sndstate) << "("
                  << int(d->sndstate) << ") - NOT sending, SET AS PENDING");

        // Otherwise connection pending
        sendBackup_AssignBackupState(d->ps->core(), BKUPST_PENDING, currtime);
        w_sendBackupCtx.recordMemberState(&(*d), BKUPST_PENDING);
#if SRT_DEBUG_BONDING_STATES
        s_stab_trace.trace(d->ps->core(), currtime, 0, 0, stateToStr(BKUPST_PENDING), d->weight);
#endif
    }
}


void CUDTGroup::sendBackup_AssignBackupState(CUDT& sock, BackupMemberState state, const steady_clock::time_point& currtime)
{
    switch (state)
    {
    case BKUPST_PENDING:
    case BKUPST_STANDBY:
    case BKUPST_BROKEN:
        sock.m_tsFreshActivation = steady_clock::time_point();
        sock.m_tsUnstableSince = steady_clock::time_point();
        sock.m_tsWarySince = steady_clock::time_point();
        break;
    case BKUPST_ACTIVE_FRESH:
        if (is_zero(sock.freshActivationStart()))
        {
            sock.m_tsFreshActivation = currtime;
        }
        sock.m_tsUnstableSince = steady_clock::time_point();
        sock.m_tsWarySince     = steady_clock::time_point();;
        break;
    case BKUPST_ACTIVE_STABLE:
        sock.m_tsFreshActivation = steady_clock::time_point();
        sock.m_tsUnstableSince = steady_clock::time_point();
        sock.m_tsWarySince = steady_clock::time_point();
        break;
    case BKUPST_ACTIVE_UNSTABLE:
        if (is_zero(sock.m_tsUnstableSince))
        {
            sock.m_tsUnstableSince = currtime;
        }
        sock.m_tsFreshActivation = steady_clock::time_point();
        sock.m_tsWarySince = steady_clock::time_point();
        break;
    case BKUPST_ACTIVE_UNSTABLE_WARY:
        if (is_zero(sock.m_tsWarySince))
        {
            sock.m_tsWarySince = currtime;
        }
        break;
    default:
        break;
    }
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_CheckIdleTime(gli_t w_d)
{
    // Check if it was fresh set as idle, we had to wait until its sender
    // buffer gets empty so that we can make sure that KEEPALIVE will be the
    // really last sent for longer time.
    CUDT& u = w_d->ps->core();
    if (is_zero(u.m_tsFreshActivation)) // TODO: Check if this condition is ever false
        return;

    CSndBuffer* b = u.m_pSndBuffer;
    if (b && b->getCurrBufSize() == 0)
    {
        HLOGC(gslog.Debug,
                log << "grp/sendBackup: FRESH IDLE LINK reached empty buffer - setting permanent and KEEPALIVE");
        u.m_tsFreshActivation = steady_clock::time_point();

        // Send first immediate keepalive. The link is to be turn to IDLE
        // now so nothing will be sent to it over time and it will start
        // getting KEEPALIVES since now. Send the first one now to increase
        // probability that the link will be recognized as IDLE on the
        // reception side ASAP.
        int32_t arg = 1;
        w_d->ps->core().sendCtrl(UMSG_KEEPALIVE, &arg);
    }
}

// [[using locked(this->m_GroupLock)]]
CUDTGroup::BackupMemberState CUDTGroup::sendBackup_QualifyActiveState(const gli_t d, const time_point currtime)
{
    const CUDT& u = d->ps->core();

    const uint32_t latency_us = u.peerLatency_us();

    const int32_t min_stability_us = m_uOPT_MinStabilityTimeout_us;
    const int64_t initial_stabtout_us = max<int64_t>(min_stability_us, latency_us);
    const int64_t probing_period_us = initial_stabtout_us + 5 * CUDT::COMM_SYN_INTERVAL_US;

    // RTT and RTTVar values are still being refined during the probing period,
    // therefore the dymanic timeout should not be used during the probing period.
    const bool is_activation_phase = !is_zero(u.freshActivationStart())
        && (count_microseconds(currtime - u.freshActivationStart()) <= probing_period_us);

    // Initial stability timeout is used only in activation phase.
    // Otherwise runtime stability is used, including the WARY state.
    const int64_t stability_tout_us = is_activation_phase
        ? initial_stabtout_us // activation phase
        : min<int64_t>(max<int64_t>(min_stability_us, 2 * u.SRTT() + 4 * u.RTTVar()), latency_us);

    const steady_clock::time_point last_rsp = max(u.freshActivationStart(), u.lastRspTime());
    const steady_clock::duration td_response = currtime - last_rsp;

    // No response for a long time
    if (count_microseconds(td_response) > stability_tout_us)
    {
        return BKUPST_ACTIVE_UNSTABLE;
    }

    enterCS(u.m_StatsLock);
    const int64_t drop_total = u.m_stats.sndr.dropped.total.count();
    leaveCS(u.m_StatsLock);

    const bool have_new_drops = d->pktSndDropTotal != drop_total;
    if (have_new_drops)
    {
        d->pktSndDropTotal = drop_total;
        if (!is_activation_phase)
            return BKUPST_ACTIVE_UNSTABLE;
    }

    // Responsive: either stable, wary or still fresh activated.
    if (is_activation_phase)
        return BKUPST_ACTIVE_FRESH;

    const bool is_wary = !is_zero(u.m_tsWarySince);
    const bool is_wary_probing = is_wary
        && (count_microseconds(currtime - u.m_tsWarySince) <= 4 * u.peerLatency_us());

    const bool is_unstable = !is_zero(u.m_tsUnstableSince);

    // If unstable and not in wary, become wary.
    if (is_unstable && !is_wary)
        return BKUPST_ACTIVE_UNSTABLE_WARY;

    // Still probing for stability.
    if (is_wary_probing)
        return BKUPST_ACTIVE_UNSTABLE_WARY;

    if (is_wary)
    {
        LOGC(gslog.Debug,
            log << "grp/sendBackup: @" << u.id() << " wary->stable after " << count_milliseconds(currtime - u.m_tsWarySince) << " ms");
    }

    return BKUPST_ACTIVE_STABLE;
}

// [[using locked(this->m_GroupLock)]]
bool CUDTGroup::sendBackup_CheckSendStatus(const steady_clock::time_point& currtime SRT_ATR_UNUSED,
                                           const int                       send_status,
                                           const int32_t                   lastseq,
                                           const int32_t                   pktseq,
                                           CUDT&                           w_u,
                                           int32_t&                        w_curseq,
                                           int&                            w_final_stat)
{
    if (send_status == -1)
        return false; // Sending failed.


    bool send_succeeded = false;
    if (w_curseq == SRT_SEQNO_NONE)
    {
        w_curseq = pktseq;
    }
    else if (w_curseq != lastseq)
    {
        // We believe that all active links use the same seq.
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

    // State it as succeeded, though. We don't know if the link
    // is broken until we get the connection broken confirmation,
    // and the instability state may wear off next time.
    send_succeeded = true;
    w_final_stat   = send_status;

    return send_succeeded;
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

size_t CUDTGroup::sendBackup_TryActivateStandbyIfNeeded(
    const char* buf,
    const int   len,
    bool& w_none_succeeded,
    SRT_MSGCTRL& w_mc,
    int32_t& w_curseq,
    int32_t& w_final_stat,
    SendBackupCtx& w_sendBackupCtx,
    CUDTException& w_cx,
    const steady_clock::time_point& currtime)
{
    const unsigned num_standby = w_sendBackupCtx.countMembersByState(BKUPST_STANDBY);
    if (num_standby == 0)
    {
        return 0;
    }

    const unsigned num_stable = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_STABLE);
    const unsigned num_fresh  = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_FRESH);

    if (num_stable + num_fresh == 0)
    {
        LOGC(gslog.Warn,
            log << "grp/sendBackup: trying to activate a stand-by link (" << num_standby << " available). "
            << "Reason: no stable links"
        );
    }
    else if (w_sendBackupCtx.maxActiveWeight() < w_sendBackupCtx.maxStandbyWeight())
    {
        LOGC(gslog.Warn,
            log << "grp/sendBackup: trying to activate a stand-by link (" << num_standby << " available). "
                << "Reason: max active weight " << w_sendBackupCtx.maxActiveWeight()
                << ", max stand by weight " << w_sendBackupCtx.maxStandbyWeight()
        );
    }
    else
    {
        /*LOGC(gslog.Warn,
            log << "grp/sendBackup: no need to activate (" << num_standby << " available). "
            << "Max active weight " << w_sendBackupCtx.maxActiveWeight()
            << ", max stand by weight " << w_sendBackupCtx.maxStandbyWeight()
        );*/
        return 0;
    }

    int stat = -1;

    size_t num_activated = 0;

    w_sendBackupCtx.sortByWeightAndState();
    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t member = w_sendBackupCtx.memberStates().begin(); member != w_sendBackupCtx.memberStates().end(); ++member)
    {
        if (member->state != BKUPST_STANDBY)
            continue;

        int   erc = 0;
        SocketData* d = member->pSocketData;
        // Now send and check the status
        // The link could have got broken

        try
        {
            CUDT& cudt = d->ps->core();
            // Take source rate estimation from an active member (needed for the input rate estimation mode).
            cudt.setRateEstimator(w_sendBackupCtx.getRateEstimate());

            // TODO: At this point all packets that could be sent
            // are located in m_SenderBuffer. So maybe just use sendBackupRexmit()?
            if (w_curseq == SRT_SEQNO_NONE)
            {
                // This marks the fact that the given here packet
                // could not be sent over any link. This includes the
                // situation of sending the very first packet after connection.

                HLOGC(gslog.Debug,
                    log << "grp/sendBackup: ... trying @" << d->id << " - sending the VERY FIRST message");

                stat = cudt.sendmsg2(buf, len, (w_mc));
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
                stat = sendBackupRexmit(cudt, (w_mc));
            }
            ++num_activated;
        }
        catch (CUDTException& e)
        {
            // This will be propagated from internal sendmsg2 call,
            // but that's ok - we want this sending interrupted even in half.
            w_cx = e;
            stat = -1;
            erc = e.getErrorCode();
        }

        d->sndresult = stat;
        d->laststatus = d->ps->getStatus();

        if (stat != -1)
        {
            d->sndstate = SRT_GST_RUNNING;
            sendBackup_AssignBackupState(d->ps->core(), BKUPST_ACTIVE_FRESH, currtime);
            w_sendBackupCtx.updateMemberState(d, BKUPST_ACTIVE_FRESH);
            // Note: this will override the sequence number
            // for all next iterations in this loop.
            w_none_succeeded = false;
            w_final_stat = stat;

            LOGC(gslog.Warn,
                log << "@" << d->id << " FRESH-ACTIVATED");

            // We've activated the link, so that's enough.
            break;
        }

        // Failure - move to broken those that could not be activated
        bool isblocked SRT_ATR_UNUSED = true;
        if (erc != SRT_EASYNCSND)
        {
            isblocked = false;
            sendBackup_AssignBackupState(d->ps->core(), BKUPST_BROKEN, currtime);
            w_sendBackupCtx.updateMemberState(d, BKUPST_BROKEN);
        }

        // If we found a blocked link, leave it alone, however
        // still try to send something over another link

        LOGC(gslog.Warn,
            log << "@" << d->id << " FAILED (" << (isblocked ? "blocked" : "ERROR")
            << "), trying to activate another link.");
    }

    return num_activated;
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_CheckPendingSockets(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime)
{
    if (w_sendBackupCtx.countMembersByState(BKUPST_PENDING) == 0)
        return;

    HLOGC(gslog.Debug, log << "grp/send*: checking pending sockets.");

    // These sockets if they are in pending state, should be added to m_SndEID
    // at the connecting stage.
    CEPoll::fmap_t sready;

    if (m_Global.m_EPoll.empty(*m_SndEpolld))
    {
        // Sanity check - weird pending reported.
        LOGC(gslog.Error, log << "grp/send*: IPE: reported pending sockets, but EID is empty - wiping pending!");
        return;
    }

    {
        InvertedLock ug(m_GroupLock);
        m_Global.m_EPoll.swait(
            *m_SndEpolld, sready, 0, false /*report by retval*/); // Just check if anything has happened
    }

    if (m_bClosing)
    {
        HLOGC(gslog.Debug, log << "grp/send...: GROUP CLOSED, ABANDONING");
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Some sockets could have been closed in the meantime.
    if (m_Global.m_EPoll.empty(*m_SndEpolld))
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    HLOGC(gslog.Debug, log << "grp/send*: RDY: " << DisplayEpollResults(sready));

    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t member = w_sendBackupCtx.memberStates().begin(); member != w_sendBackupCtx.memberStates().end(); ++member)
    {
        if (member->state != BKUPST_PENDING)
            continue;

        const SRTSOCKET sockid = member->pSocketData->id;
        if (!CEPoll::isready(sready, sockid, SRT_EPOLL_ERR))
            continue;

        HLOGC(gslog.Debug, log << "grp/send*: Socket @" << sockid << " reported FAILURE - qualifying as broken.");
        w_sendBackupCtx.updateMemberState(member->pSocketData, BKUPST_BROKEN);
        if (member->pSocketData->ps)
            sendBackup_AssignBackupState(member->pSocketData->ps->core(), BKUPST_BROKEN, currtime);

        const int no_events = 0;
        m_Global.m_EPoll.update_usock(m_SndEID, sockid, &no_events);
    }

    // After that, all sockets that have been reported
    // as ready to write should be removed from EID. This
    // will also remove those sockets that have been added
    // as redundant links at the connecting stage and became
    // writable (connected) before this function had a chance
    // to check them.
    m_Global.m_EPoll.clear_ready_usocks(*m_SndEpolld, SRT_EPOLL_OUT);
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_CheckUnstableSockets(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime)
{
    const unsigned num_stable = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_STABLE);
    if (num_stable == 0)
        return;

    const unsigned num_unstable = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_UNSTABLE);
    const unsigned num_wary     = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_UNSTABLE_WARY);
    if (num_unstable + num_wary == 0)
        return;

    HLOGC(gslog.Debug, log << "grp/send*: checking unstable sockets.");

    
    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t member = w_sendBackupCtx.memberStates().begin(); member != w_sendBackupCtx.memberStates().end(); ++member)
    {
        if (member->state != BKUPST_ACTIVE_UNSTABLE && member->state != BKUPST_ACTIVE_UNSTABLE_WARY)
            continue;

        CUDT& sock = member->pSocketData->ps->core();

        if (is_zero(sock.m_tsUnstableSince))
        {
            LOGC(gslog.Error, log << "grp/send* IPE: Socket @" << member->socketID
                << " is qualified as unstable, but does not have the 'unstable since' timestamp. Still marking for closure.");
        }

        const int unstable_for_ms = count_milliseconds(currtime - sock.m_tsUnstableSince);
        if (unstable_for_ms < sock.peerIdleTimeout_ms())
            continue;

        // Requesting this socket to be broken with the next CUDT::checkExpTimer() call.
        sock.breakAsUnstable();

        LOGC(gslog.Warn, log << "grp/send*: Socket @" << member->socketID << " is unstable for " << unstable_for_ms 
            << "ms - requesting breakage.");

        //w_sendBackupCtx.updateMemberState(member->pSocketData, BKUPST_BROKEN);
        //if (member->pSocketData->ps)
        //    sendBackup_AssignBackupState(member->pSocketData->ps->core(), BKUPST_BROKEN, currtime);
    }
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::send_CloseBrokenSockets(vector<SRTSOCKET>& w_wipeme)
{
    if (!w_wipeme.empty())
    {
        InvertedLock ug(m_GroupLock);

        // With unlocked GroupLock, we can now lock GlobControlLock.
        // This is needed prevent any of them be deleted from the container
        // at the same time.
        ScopedLock globlock(CUDT::uglobal().m_GlobControlLock);

        for (vector<SRTSOCKET>::iterator p = w_wipeme.begin(); p != w_wipeme.end(); ++p)
        {
            CUDTSocket* s = CUDT::uglobal().locateSocket_LOCKED(*p);

            // If the socket has been just moved to ClosedSockets, it means that
            // the object still exists, but it will be no longer findable.
            if (!s)
                continue;

            HLOGC(gslog.Debug,
                  log << "grp/send...: BROKEN SOCKET @" << (*p) << " - CLOSING, to be removed from group.");

            // As per sending, make it also broken so that scheduled
            // packets will be also abandoned.
            s->setClosed();
        }
    }

    HLOGC(gslog.Debug, log << "grp/send...: - wiped " << w_wipeme.size() << " broken sockets");

    // We'll need you again.
    w_wipeme.clear();
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_CloseBrokenSockets(SendBackupCtx& w_sendBackupCtx)
{
    if (w_sendBackupCtx.countMembersByState(BKUPST_BROKEN) == 0)
        return;

    InvertedLock ug(m_GroupLock);

    // With unlocked GroupLock, we can now lock GlobControlLock.
    // This is needed prevent any of them be deleted from the container
    // at the same time.
    ScopedLock globlock(CUDT::uglobal().m_GlobControlLock);

    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t member = w_sendBackupCtx.memberStates().begin(); member != w_sendBackupCtx.memberStates().end(); ++member)
    {
        if (member->state != BKUPST_BROKEN)
            continue;

        // m_GroupLock is unlocked, therefore member->pSocketData can't be used.
        const SRTSOCKET sockid = member->socketID;
        CUDTSocket* s = CUDT::uglobal().locateSocket_LOCKED(sockid);

        // If the socket has been just moved to ClosedSockets, it means that
        // the object still exists, but it will be no longer findable.
        if (!s)
            continue;

        LOGC(gslog.Debug,
                log << "grp/send...: BROKEN SOCKET @" << sockid << " - CLOSING, to be removed from group.");

        // As per sending, make it also broken so that scheduled
        // packets will be also abandoned.
        s->setBrokenClosed();
    }

    // TODO: all broken members are to be removed from the context now???
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_RetryWaitBlocked(SendBackupCtx&       w_sendBackupCtx,
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


    // This procedure is for a case when the packet could not be sent
    // over any link (hence "none succeeded"), but there are some unstable
    // links and no parallel links. We need to WAIT for any of the links
    // to become available for sending.

    // Note: A link is added in unstableLinks if sending has failed with SRT_ESYNCSND.
    const unsigned num_unstable = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_UNSTABLE);
    const unsigned num_wary     = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_UNSTABLE_WARY);
    if ((num_unstable + num_wary == 0) || !w_none_succeeded)
        return;

    HLOGC(gslog.Debug, log << "grp/sendBackup: no successfull sending: "
        << (num_unstable + num_wary) << " unstable links - waiting to retry sending...");

    // Note: GroupLock is set already, skip locks and checks
    getGroupData_LOCKED((w_mc.grpdata), (&w_mc.grpdata_size));
    m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
    m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);

    if (m_Global.m_EPoll.empty(*m_SndEpolld))
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
    const size_t nlinks = m_Group.size();
    size_t ndead = 0;

RetryWaitBlocked:
    {
        // Some sockets could have been closed in the meantime.
        if (m_Global.m_EPoll.empty(*m_SndEpolld))
        {
            HLOGC(gslog.Debug, log << "grp/sendBackup: no more sockets available for sending - group broken");
            throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
        }

        InvertedLock ug(m_GroupLock);
        HLOGC(gslog.Debug,
            log << "grp/sendBackup: swait call to get at least one link alive up to " << m_iSndTimeOut << "us");
        THREAD_PAUSED();
        brdy = m_Global.m_EPoll.swait(*m_SndEpolld, (sready), m_iSndTimeOut);
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
                CUDTSocket* s = m_Global.locateSocket(id, CUDTUnited::ERH_RETURN); // << LOCKS m_GlobControlLock!
                if (s)
                {
                    HLOGC(gslog.Debug,
                        log << "grp/sendBackup: swait/ex on @" << (id)
                        << " while waiting for any writable socket - CLOSING");
                    CUDT::uglobal().close(s); // << LOCKS m_GlobControlLock, then GroupLock!
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
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
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
    int nactivated SRT_ATR_UNUSED = 0;
    int stat = -1;
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
    {
        // We are waiting only for active members
        if (d->sndstate != SRT_GST_RUNNING)
        {
            HLOGC(gslog.Debug,
                log << "grp/sendBackup: member @" << d->id << " state is not RUNNING - SKIPPING from retry/waiting");
            continue;
        }
        // Skip if not writable in this run
        if (!CEPoll::isready(sready, d->id, SRT_EPOLL_OUT))
        {
            ++nwaiting;
            HLOGC(gslog.Debug, log << "grp/sendBackup: @" << d->id << " NOT ready:OUT, added as waiting");
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
        }

        d->sndresult = stat;
        d->laststatus = d->ps->getStatus();

        if (stat == -1)
        {
            // This link is no longer waiting.
            continue;
        }

        w_final_stat = stat;
        d->sndstate = SRT_GST_RUNNING;
        w_none_succeeded = false;
        const steady_clock::time_point currtime = steady_clock::now();
        sendBackup_AssignBackupState(d->ps->core(), BKUPST_ACTIVE_UNSTABLE_WARY, currtime);
        w_sendBackupCtx.updateMemberState(&(*d), BKUPST_ACTIVE_UNSTABLE_WARY);
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
}

// [[using locked(this->m_GroupLock)]]
void CUDTGroup::sendBackup_SilenceRedundantLinks(SendBackupCtx& w_sendBackupCtx, const steady_clock::time_point& currtime)
{
    // The most important principle is to keep the data being sent constantly,
    // even if it means temporarily full redundancy.
    // A member can be silenced only if there is at least one stable memebr.
    const unsigned num_stable = w_sendBackupCtx.countMembersByState(BKUPST_ACTIVE_STABLE);
    if (num_stable == 0)
        return;

    // INPUT NEEDED:
    // - stable member with maximum weight

    uint16_t max_weight_stable = 0;
    SRTSOCKET stableSocketId = SRT_INVALID_SOCK; // SocketID of a stable link with higher weight
    
    w_sendBackupCtx.sortByWeightAndState();
    //LOGC(gslog.Debug, log << "grp/silenceRedundant: links after sort: " << w_sendBackupCtx.printMembers());
    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t member = w_sendBackupCtx.memberStates().begin(); member != w_sendBackupCtx.memberStates().end(); ++member)
    {
        if (!isStateActive(member->state))
            continue;

        const bool haveHigherWeightStable = stableSocketId != SRT_INVALID_SOCK;
        const uint16_t weight = member->pSocketData->weight;

        if (member->state == BKUPST_ACTIVE_STABLE)
        {
            // silence stable link if it is not the first stable
            if (!haveHigherWeightStable)
            {
                max_weight_stable = (int) weight;
                stableSocketId = member->socketID;
                continue;
            }
            else
            {
                LOGC(gslog.Note, log << "grp/sendBackup: silencing stable member @" << member->socketID  << " (weight " << weight
                    << ") in favor of @" << stableSocketId << " (weight " << max_weight_stable << ")");
            }
        }
        else if (haveHigherWeightStable && weight <= max_weight_stable)
        {
            LOGC(gslog.Note, log << "grp/sendBackup: silencing member @" << member->socketID << " (weight " << weight
                << " " << stateToStr(member->state)
                << ") in favor of @" << stableSocketId << " (weight " << max_weight_stable << ")");
        }
        else
        {
            continue;
        }

        // TODO: Move to a separate function sendBackup_SilenceMember
        SocketData* d = member->pSocketData;
        CUDT& u = d->ps->core();

        sendBackup_AssignBackupState(u, BKUPST_STANDBY, currtime);
        w_sendBackupCtx.updateMemberState(d, BKUPST_STANDBY);

        if (d->sndstate != SRT_GST_RUNNING)
        {
            LOGC(gslog.Error,
                log << "grp/sendBackup: IPE: misidentified a non-running link @" << d->id << " as active");
            continue;
        }

        d->sndstate = SRT_GST_IDLE;
    }
}

int CUDTGroup::sendBackup(const char* buf, int len, SRT_MSGCTRL& w_mc)
{
    if (len <= 0)
    {
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    // Only live streaming is supported
    if (len > SRT_LIVE_MAX_PLSIZE)
    {
        LOGC(gslog.Error, log << "grp/send(backup): buffer size=" << len << " exceeds maximum allowed in live mode");
        throw CUDTException(MJ_NOTSUP, MN_INVAL, 0);
    }

    // [[using assert(this->m_pSndBuffer != nullptr)]];

    // First, acquire GlobControlLock to make sure all member sockets still exist
    enterCS(m_Global.m_GlobControlLock);
    ScopedLock guard(m_GroupLock);

    if (m_bClosing)
    {
        leaveCS(m_Global.m_GlobControlLock);
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // Now, still under lock, check if all sockets still can be dispatched
    send_CheckValidSockets();
    leaveCS(m_Global.m_GlobControlLock);

    steady_clock::time_point currtime = steady_clock::now();

    SendBackupCtx sendBackupCtx; // default initialized as empty
    // TODO: reserve? sendBackupCtx.memberStates.reserve(m_Group.size());

    sendBackup_QualifyMemberStates((sendBackupCtx), currtime);

    int32_t curseq      = SRT_SEQNO_NONE;
    size_t  nsuccessful = 0;

    SRT_ATR_UNUSED CUDTException cx(MJ_SUCCESS, MN_NONE, 0); // TODO: Delete then?
    uint16_t maxActiveWeight = 0; // Maximum weight of active links.
    // The number of bytes sent or -1 for error will be stored in group_send_result
    int group_send_result = sendBackup_SendOverActive(buf, len, w_mc, currtime, (curseq), (nsuccessful), (maxActiveWeight), (sendBackupCtx), (cx));
    bool none_succeeded = (nsuccessful == 0);

    // Save current payload in group's sender buffer.
    sendBackup_Buffering(buf, len, (curseq), (w_mc));

    sendBackup_TryActivateStandbyIfNeeded(buf, len, (none_succeeded),
        (w_mc),
        (curseq),
        (group_send_result),
        (sendBackupCtx),
        (cx), currtime);

    sendBackup_CheckPendingSockets((sendBackupCtx), currtime);
    sendBackup_CheckUnstableSockets((sendBackupCtx), currtime);

    //LOGC(gslog.Debug, log << "grp/sendBackup: links after all checks: " << sendBackupCtx.printMembers());

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    sendBackup_CloseBrokenSockets((sendBackupCtx));

    // Re-check after the waiting lock has been reacquired
    if (m_bClosing)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    // If all links out of the unstable-running links are blocked (SRT_EASYNCSND),
    // perform epoll wait on them. In this situation we know that
    // there are no idle blocked links because IDLE LINK CAN'T BE BLOCKED,
    // no matter what. It's because the link may only be blocked if
    // the sender buffer of this socket is full, and it can't be
    // full if it wasn't used so far.
    //
    // This means that in case when we have no stable links, we
    // need to try out any link that can accept the rexmit-load.
    // We'll check link stability at the next sending attempt.
    sendBackup_RetryWaitBlocked((sendBackupCtx), (group_send_result), (none_succeeded), (w_mc), (cx));

    sendBackup_SilenceRedundantLinks((sendBackupCtx), currtime);
    // (closing condition checked inside this call)

    if (none_succeeded)
    {
        HLOGC(gslog.Debug, log << "grp/sendBackup: all links broken (none succeeded to send a payload)");
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
        // Reparse error code, if set.
        // It might be set, if the last operation was failed.
        // If any operation succeeded, this will not be executed anyway.

        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);
    }

    // At least one link has succeeded, update sending stats.
    m_stats.sent.count(len);

    // Now fill in the socket table. Check if the size is enough, if not,
    // then set the pointer to NULL and set the correct size.

    // Note that list::size() is linear time, however this shouldn't matter,
    // as with the increased number of links in the redundancy group the
    // impossibility of using that many of them grows exponentally.
    const size_t grpsize = m_Group.size();

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

        ready_again = ready_again || d->ps->writeReady();
    }
    w_mc.grpdata_size = i;

    if (!ready_again)
    {
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
    }

    HLOGC(gslog.Debug,
          log << "grp/sendBackup: successfully sent " << group_send_result << " bytes, "
              << (ready_again ? "READY for next" : "NOT READY to send next"));
    return group_send_result;
}

// [[using locked(this->m_GroupLock)]]
int32_t CUDTGroup::addMessageToBuffer(const char* buf, size_t len, SRT_MSGCTRL& w_mc)
{
    if (m_iSndAckedMsgNo == SRT_MSGNO_NONE)
    {
        // Very first packet, just set the msgno.
        m_iSndAckedMsgNo  = w_mc.msgno;
        m_iSndOldestMsgNo = w_mc.msgno;
        HLOGC(gslog.Debug, log << "addMessageToBuffer: initial message no #" << w_mc.msgno);
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
        HLOGC(gslog.Debug,
              log << "addMessageToBuffer: ... after: oldest #" << m_iSndOldestMsgNo);
    }

    m_SenderBuffer.resize(m_SenderBuffer.size() + 1);
    BufferedMessage& bm = m_SenderBuffer.back();
    bm.mc               = w_mc;
    bm.copy(buf, len);

    HLOGC(gslog.Debug,
          log << "addMessageToBuffer: #" << w_mc.msgno << " size=" << len << " !" << BufferStamp(buf, len));

    return m_SenderBuffer.front().mc.pktseq;
}

int CUDTGroup::sendBackup_SendOverActive(const char* buf, int len, SRT_MSGCTRL& w_mc, const steady_clock::time_point& currtime, int32_t& w_curseq,
    size_t& w_nsuccessful, uint16_t& w_maxActiveWeight, SendBackupCtx& w_sendBackupCtx, CUDTException& w_cx)
{
    if (w_mc.srctime == 0)
        w_mc.srctime = count_microseconds(currtime.time_since_epoch());

    SRT_ASSERT(w_nsuccessful == 0);
    SRT_ASSERT(w_maxActiveWeight == 0);

    int group_send_result = SRT_ERROR;

    // TODO: implement iterator over active links
    typedef vector<BackupMemberStateEntry>::const_iterator const_iter_t;
    for (const_iter_t member = w_sendBackupCtx.memberStates().begin(); member != w_sendBackupCtx.memberStates().end(); ++member)
    {
        if (!isStateActive(member->state))
            continue;

        SocketData* d = member->pSocketData;
        int   erc = SRT_SUCCESS;
        // Remaining sndstate is SRT_GST_RUNNING. Send a payload through it.
        CUDT& u = d->ps->core();
        const int32_t lastseq = u.schedSeqNo();
        int sndresult = SRT_ERROR;
        try
        {
            // This must be wrapped in try-catch because on error it throws an exception.
            // Possible return values are only 0, in case when len was passed 0, or a positive
            // >0 value that defines the size of the data that it has sent, that is, in case
            // of Live mode, equal to 'len'.
            sndresult = u.sendmsg2(buf, len, (w_mc));
        }
        catch (CUDTException& e)
        {
            w_cx = e;
            erc  = e.getErrorCode();
            sndresult = SRT_ERROR;
        }

        const bool send_succeeded = sendBackup_CheckSendStatus(
            currtime,
            sndresult,
            lastseq,
            w_mc.pktseq,
            (u),
            (w_curseq),
            (group_send_result));

        if (send_succeeded)
        {
            ++w_nsuccessful;
            w_maxActiveWeight = max(w_maxActiveWeight, d->weight);

            if (u.m_pSndBuffer)
                w_sendBackupCtx.setRateEstimate(u.m_pSndBuffer->getRateEstimator());
        }
        else if (erc == SRT_EASYNCSND)
        {
            sendBackup_AssignBackupState(u, BKUPST_ACTIVE_UNSTABLE, currtime);
            w_sendBackupCtx.updateMemberState(d, BKUPST_ACTIVE_UNSTABLE);
        }

        d->sndresult  = sndresult;
        d->laststatus = d->ps->getStatus();
    }

    return group_send_result;
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
        const int distance = CSeqNo::seqoff(core.schedSeqNo(), curseq);
        if (distance < 0)
        {
            // This may happen in case when the link to be activated is already running.
            // Getting sequences backwards is not allowed, as sending them makes no
            // sense - they are already ACK-ed or are behind the ISN. Instead, skip all
            // packets that are in the past towards the scheduling sequence.
            skip_initial = -distance;
            LOGC(gslog.Warn,
                 log << "sendBackupRexmit: OVERRIDE attempt. Link seqno %" << core.schedSeqNo() << ", trying to send from seqno %" << curseq
                     << " - DENIED; skip " << skip_initial << " pkts, " << m_SenderBuffer.size() << " pkts in buffer");
        }
        else
        {
            // In case when the next planned sequence on this link is behind
            // the firstmost sequence in the backup buffer, synchronize the
            // sequence with it first so that they go hand-in-hand with
            // sequences already used by the link from which packets were
            // copied to the backup buffer.
            IF_HEAVY_LOGGING(int32_t old = core.schedSeqNo());
            const bool su SRT_ATR_UNUSED = core.overrideSndSeqNo(curseq);
            HLOGC(gslog.Debug,
                  log << "sendBackupRexmit: OVERRIDING seq %" << old << " with %" << curseq
                      << (su ? " - succeeded" : " - FAILED!"));
        }
    }


    if (skip_initial >= m_SenderBuffer.size())
    {
        LOGC(gslog.Warn,
            log << "sendBackupRexmit: All packets were skipped. Nothing to send %" << core.schedSeqNo() << ", trying to send from seqno %" << curseq
            << " - DENIED; skip " << skip_initial << " packets");
        return 0; // can't return any other state, nothing was sent
    }

    senderBuffer_t::iterator i = m_SenderBuffer.begin() + skip_initial;

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

#if 0 // Old balancing sending, leaving for historical reasons
int CUDTGroup::sendBalancing_orig(const char* buf, int len, SRT_MSGCTRL& w_mc)
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
    vector<gli_t> wipeme;
    vector<gli_t> pending;

    w_mc.msgno = -1;

    ScopedLock guard (m_GroupLock);

    // Always set the same exactly message number for the payload
    // sent over all links.Regardless whether it will be used to synchronize
    // the streams or not.
    if (m_iLastSchedMsgNo != -1)
    {
        HLOGC(gslog.Debug, log << "grp/sendBalancing: setting message number: " << m_iLastSchedMsgNo);
        w_mc.msgno = m_iLastSchedMsgNo;
    }
    else
    {
        HLOGP(gslog.Debug, "grp/sendBalancing: NOT setting message number - waiting for the first successful sending");
    }


    // Overview loop
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
    {
        d->sndresult = 0; // set as default

        // Check socket sndstate before sending
        if (d->sndstate == SRT_GST_BROKEN)
        {
            HLOGC(gslog.Debug, log << "grp/sendBalancing: socket in BROKEN state: @" << d->id << ", sockstatus=" << SockStatusStr(d->ps ? d->ps->getStatus() : SRTS_NONEXIST));
            wipeme.push_back(d);
            d->sndresult = -1;

            /*
               This distinction is now blocked - it has led to blocking removal of
               authentically broken sockets that just got only incorrect state update.
               (XXX This problem has to be fixed either, but when epoll is rewritten it
                will be fixed from the start anyway).

            // Check if broken permanently
            if (!d->ps || d->ps->getStatus() == SRTS_BROKEN)
            {
                HLOGC(gslog.Debug, log << "... permanently. Will delete it from group $" << id());
                wipeme.push_back(d);
            }
            else
            {
                HLOGC(gslog.Debug, log << "... socket still " << SockStatusStr(d->ps ? d->ps->getStatus() : SRTS_NONEXIST));
            }
            */
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
                HLOGC(gslog.Debug, log << "CUDTGroup::send.$" << id() << ": @" << d->id << " became "
                        << SockStatusStr(st) << ", WILL BE CLOSED.");
                wipeme.push_back(d);
                d->sndstate = SRT_GST_BROKEN;
                d->sndresult = -1;
                continue;
            }

            if (st != SRTS_CONNECTED)
            {
                HLOGC(gslog.Debug, log << "CUDTGroup::send. @" << d->id << " is still " << SockStatusStr(st) << ", skipping.");
                pending.push_back(d);
                continue;
            }

            HLOGC(gslog.Debug, log << "grp/sendBalancing: socket in IDLE state: @" << d->id << " - ACTIVATING it");
            d->sndstate = SRT_GST_RUNNING;
            continue;
        }

        if (d->sndstate == SRT_GST_RUNNING)
        {
            HLOGC(gslog.Debug, log << "grp/sendBalancing: socket in RUNNING state: @" << d->id << " - will send a payload");
            continue;
        }

        HLOGC(gslog.Debug, log << "grp/sendBalancing: socket @" << d->id << " not ready, state: "
                << StateStr(d->sndstate) << "(" << int(d->sndstate) << ") - NOT sending, SET AS PENDING");

        pending.push_back(d);
    }

    SRT_ATR_UNUSED CUDTException cx (MJ_SUCCESS, MN_NONE, 0);
    BalancingLinkState lstate = { m_Group.active(), 0, 0 };
    int stat = -1;
    gli_t selink; // will be initialized first in the below loop

    for (;;)
    {
        // Repeatable block.
        // The algorithm is more-less:
        //
        // 1. Select a link to use for sending
        // 2. Perform the operation
        // 3. If the operation succeeded, record this link and exit with success
        // 4. If the operation failed, call selector again, this time with error info
        // 5. The selector can return a link to use again, or gli_NULL() if the operation should fail
        // 6. If the selector returned a valid link, go to p. 2.

        // Call selection. Default: defaultSelectLink
        selink = CALLBACK_CALL(m_cbSelectLink, lstate);

        if (selink == m_Group.end())
        {
            stat = -1; // likely not possible, but make sure.
            break;
        }

        // Sanity check
        if (selink->sndstate != SRT_GST_RUNNING)
        {
            LOGC(gslog.Error, log << "IPE: sendBalancing: selectLink returned an iactive link! - trying blindly anyway");
        }

        // Perform the operation
        int erc = SRT_SUCCESS;
        try
        {
            // This must be wrapped in try-catch because on error it throws an exception.
            // Possible return values are only 0, in case when len was passed 0, or a positive
            // >0 value that defines the size of the data that it has sent, that is, in case
            // of Live mode, equal to 'len'.
            CUDTSocket* ps = selink->ps;
            InvertedLock ug (m_GroupLock);

            HLOGC(gslog.Debug, log << "grp/sendBalancing: SENDING #" << w_mc.msgno << " through link [" << m_uBalancingRoll << "]");

            // NOTE: EXCEPTION PASSTHROUGH.
            stat = ps->core().sendmsg2(buf, len, (w_mc));
        }
        catch (CUDTException& e)
        {
            cx = e;
            stat = -1;
            erc = e.getErrorCode();
        }

        selink->sndresult = stat;

        if (stat != -1)
        {
            if (m_iLastSchedMsgNo == -1)
            {
                // Initialize this number
                HLOGC(gslog.Debug, log << "grp/sendBalancing: INITIALIZING message number: " << w_mc.msgno);
                m_iLastSchedMsgNo = w_mc.msgno;
            }

            m_Group.set_active(selink);

            // Sending succeeded. Complete the rest of the activities.
            break;
        }

        // Handle the error. If a link got the blocking error, set
        // this link PENDING state. This will cause that this link be
        // activated at the next sending call and retried, but in this
        // session it will be skipped.
        if (erc == SRT_EASYNCSND)
        {
            selink->sndstate = SRT_GST_PENDING;
        }
        else
        {
            selink->sndstate = SRT_GST_BROKEN;
            if (std::find(wipeme.begin(), wipeme.end(), selink) == wipeme.end())
                wipeme.push_back(selink); // unique add
        }

        lstate.ilink = selink;
        lstate.status = stat;
        lstate.errorcode = erc;

        // Now repeat selection.
        // Note that every selection either gets a link that
        // succeeds (and this loop is broken) or the link becomes
        // broken, and then it should be skipped by the selector.
        // Eventually with all links broken the selector will return
        // no link to be used, and therefore this operation is interrupted
        // and error-reported.
    }

    if (!pending.empty())
    {
        HLOGC(gslog.Debug, log << "grp/sendBalancing: found pending sockets, polling them.");

        // These sockets if they are in pending state, they should be added to m_SndEID
        // at the connecting stage.
        CEPoll::fmap_t sready;

        if (m_Global.m_EPoll.empty(*m_SndEpolld))
        {
            // Sanity check - weird pending reported.
            LOGC(gslog.Error, log << "grp/sendBalancing: IPE: reported pending sockets, but EID is empty - wiping pending!");
            copy(pending.begin(), pending.end(), back_inserter(wipeme));
        }
        else
        {
            {
                InvertedLock ug (m_GroupLock);
                m_Global.m_EPoll.swait(*m_SndEpolld, sready, 0, false /*report by retval*/); // Just check if anything happened
            }

            HLOGC(gslog.Debug, log << "grp/sendBalancing: RDY: " << DisplayEpollResults(sready));

            // sockets in EX: should be moved to wipeme.
            for (vector<gli_t>::iterator i = pending.begin(); i != pending.end(); ++i)
            {
                gli_t d = *i;
                int rdev = CEPoll::ready(sready, d->id);
                if (rdev & SRT_EPOLL_ERR)
                {
                    HLOGC(gslog.Debug, log << "grp/sendBalancing: Socket @" << d->id << " reported FAILURE - moved to wiped.");
                    // Failed socket. Move d to wipeme. Remove from eid.
                    wipeme.push_back(d);
                    m_Global.epoll_remove_usock(m_SndEID, d->id);
                }
                else if (rdev & SRT_EPOLL_OUT)
                {
                    d->sndstate = SRT_GST_IDLE;
                }
            }

            // After that, all sockets that have been reported
            // as ready to write should be removed from EID. This
            // will also remove those sockets that have been added
            // as redundant links at the connecting stage and became
            // writable (connected) before this function had a chance
            // to check them.
            m_Global.m_EPoll.clear_ready_usocks(*m_SndEpolld, SRT_EPOLL_CONNECT);
        }
    }


    // Do final checkups.

    // Now complete the status data in the function and return.
    // This is the case for both successful and failed return.

    size_t grpsize = m_Group.size();

    if (w_mc.grpdata_size < grpsize)
    {
        w_mc.grpdata = NULL;
    }

    size_t i = 0;

    // Fill the array first before removal.

    bool ready_again = false;
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d, ++i)
    {
        if (w_mc.grpdata)
        {
            // Enough space to fill
            w_mc.grpdata[i].id = d->id;
            w_mc.grpdata[i].sockstate = d->laststatus;

            if (d->sndstate == SRT_GST_RUNNING)
                w_mc.grpdata[i].result = d->sndresult;
            else if (d->sndstate == SRT_GST_IDLE)
                w_mc.grpdata[i].result = 0;
            else
                w_mc.grpdata[i].result = -1;

            memcpy(&w_mc.grpdata[i].peeraddr, &d->peer, d->peer.size());
        }

        // We perform this loop anyway because we still need to check if any
        // socket is writable. Note that the group lock will hold any write ready
        // updates that are performed just after a single socket update for the
        // group, so if any socket is actually ready at the moment when this
        // is performed, and this one will result in none-write-ready, this will
        // be fixed just after returning from this function.

        ready_again = ready_again | d->ps->writeReady();
    }

    // Review the wipeme sockets.
    // The reason why 'wipeme' is kept separately to 'broken_sockets' is that
    // it might theoretically happen that ps becomes NULL while the item still exists.
    vector<CUDTSocket*> broken_sockets;

    // delete all sockets that were broken at the entrance
    for (vector<gli_t>::iterator i = wipeme.begin(); i != wipeme.end(); ++i)
    {
        gli_t d = *i;
        CUDTSocket* ps = d->ps;
        if (!ps)
        {
            LOGC(gslog.Error, log << "grp/sendBalancing: IPE: socket NULL at id=" << d->id << " - removing from group list");
            // Closing such socket is useless, it simply won't be found in the map and
            // the internal facilities won't know what to do with it anyway.
            // Simply delete the entry.
            m_Group.erase(d);
            updateErasedLink();
            continue;
        }
        broken_sockets.push_back(ps);
    }

    if (!broken_sockets.empty()) // Prevent unlock-lock cycle if no broken sockets found
    {
        // Lift the group lock for a while, to avoid possible deadlocks.
        InvertedLock ug (m_GroupLock);

        for (vector<CUDTSocket*>::iterator x = broken_sockets.begin(); x != broken_sockets.end(); ++x)
        {
            CUDTSocket* ps = *x;
            HLOGC(gslog.Debug, log << "grp/sendBalancing: BROKEN SOCKET @" << ps->m_SocketID << " - CLOSING AND REMOVING.");

            // NOTE: This does inside: ps->removeFromGroup().
            // After this call, 'd' is no longer valid and *i is singular.
            CUDT::uglobal().close(ps->m_SocketID);
        }
    }

    HLOGC(gslog.Debug, log << "grp/sendBalancing: - wiped " << wipeme.size() << " broken sockets");

    w_mc.grpdata_size = i;

    if (!ready_again)
    {
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, false);
    }

    // If m_iLastSchedSeqNo wasn't initialized above, don't touch it.
    if (m_iLastSchedMsgNo != -1)
    {
        m_iLastSchedMsgNo = ++MsgNo(m_iLastSchedMsgNo);
        HLOGC(gslog.Debug, log << "grp/sendBalancing: updated msgno: " << m_iLastSchedMsgNo);
    }

    if (stat == -1)
        throw CUDTException(MJ_CONNECTION, MN_CONNLOST, 0);

    return stat;
}
#endif


// XXX DEAD CODE.
// [[using locked(CUDTGroup::m_GroupLock)]];
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

void CUDTGroup::processKeepalive(CUDTGroup::SocketData* gli, const CPacket& ctrlpkt SRT_ATR_UNUSED, const time_point& tsArrival SRT_ATR_UNUSED)
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
        if (gli->sndstate == SRT_GST_RUNNING && is_zero(gli->ps->core().m_tsFreshActivation))
        {
            gli->sndstate = SRT_GST_IDLE;
            HLOGC(gslog.Debug,
                  log << "GROUP: received KEEPALIVE in @" << gli->id << " active=PAST - link turning snd=IDLE");
        }
    }

    ScopedLock lck(m_RcvBufferLock);
    m_pRcvBuffer->updateTsbPdTimeBase(ctrlpkt.getMsgTimeStamp());
    if (m_bOPT_DriftTracer)
        m_pRcvBuffer->addRcvTsbPdDriftSample(ctrlpkt.getMsgTimeStamp(), tsArrival, -1);

}

void CUDTGroup::addGroupDriftSample(uint32_t timestamp, const time_point& tsArrival, int rtt)
{
    if (!m_bOPT_DriftTracer)
        return;

    ScopedLock lck(m_RcvBufferLock);
    m_pRcvBuffer->addRcvTsbPdDriftSample(timestamp, tsArrival, rtt);
}

void CUDTGroup::internalKeepalive(SocketData* gli)
{
    // This is in response to AGENT SENDING keepalive. This means that there's
    // no transmission in either direction, but the KEEPALIVE packet from the
    // other party could have been missed. This is to ensure that the IDLE state
    // is recognized early enough, before any sequence discrepancy can happen.

    if (m_type == SRT_GTYPE_BACKUP && gli->rcvstate == SRT_GST_RUNNING)
    {
        gli->rcvstate = SRT_GST_IDLE;
        // Prevent sending KEEPALIVE again in group-sending
        gli->ps->core().m_tsFreshActivation = steady_clock::time_point();
        HLOGC(gslog.Debug, log << "GROUP: EXP-requested KEEPALIVE in @" << gli->id << " - link turning IDLE");
    }
}

CUDTGroup::BufferedMessageStorage CUDTGroup::BufferedMessage::storage(SRT_LIVE_MAX_PLSIZE /*, 1000*/);

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
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_CONNECT, true);
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
              << s->core().m_iRcvLastSkipAck);

    CUDT*         source = &s->core();
    vector<CUDT*> targets;

    UniqueLock lg(m_GroupLock);
    // Sanity check for a case when getting a deleted socket
    if (!s->m_GroupOf)
        return;

    // Under a group lock, we block execution of removal of the socket
    // from the group, so if m_GroupOf is not NULL, we are granted
    // that m_GroupMemberData is valid.
    SocketData* current = s->m_GroupMemberData;

    for (gli_t gi = m_Group.begin(); gi != m_Group.end(); ++gi)
    {
        // Skip the socket that has reported packet reception
        if (&*gi == current)
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
        if (!gi->ps->core().m_bConnected)
        {
            HLOGC(grlog.Debug, log << "grp: IPE: NOT updating rcv-seq on @" << gi->id << " - IDLE BUT NOT CONNECTED");
            continue;
        }

        targets.push_back(&gi->ps->core());
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
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR, true);
    }
    else
    {
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_UPDATE, true);
    }
}

void CUDTGroup::addEPoll(int eid)
{
    enterCS(m_Global.m_EPoll.m_EPollLock);
    m_sPollID.insert(eid);
    leaveCS(m_Global.m_EPoll.m_EPollLock);

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
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN, true);

    if (any_write)
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_OUT, true);

    // Set broken if none is non-broken (pending, read-ready or write-ready)
    if (any_broken && !any_pending)
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_ERR, true);
}

void CUDTGroup::removeEPollEvents(const int eid)
{
    // clear IO events notifications;
    // since this happens after the epoll ID has been removed, they cannot be set again
    set<int> remove;
    remove.insert(eid);
    m_Global.m_EPoll.update_events(id(), remove, SRT_EPOLL_IN | SRT_EPOLL_OUT, false);
}

void CUDTGroup::removeEPollID(const int eid)
{
    enterCS(m_Global.m_EPoll.m_EPollLock);
    m_sPollID.erase(eid);
    leaveCS(m_Global.m_EPoll.m_EPollLock);
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
        m_Global.m_EPoll.update_events(id(), m_sPollID, SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR, true);
    }
    else
    {
        HLOGC(gmlog.Debug, log << "group/updateFailedLink: Still " << nhealthy << " links in the group");
    }
}


int CUDTGroup::configure(const char* str)
{
    string config = str;
    switch (type())
    {
    case SRT_GTYPE_BALANCING:
        // config contains the algorithm name
        if (config == "" || config == "plain")
        {
            m_cbSelectLink.set(this, &CUDTGroup::linkSelect_plain_fw);
            HLOGC(gmlog.Debug, log << "group(balancing): PLAIN algorithm selected");
        }
        else if (config == "window")
        {
            m_cbSelectLink.set(this, &CUDTGroup::linkSelect_window_fw);
            HLOGC(gmlog.Debug, log << "group(balancing): WINDOW algorithm selected");
        }
        else
        {
            LOGC(gmlog.Error, log << "group(balancing): unknown selection algorithm '"
                    << config << "'");
            return CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0);
        }

        break;

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


CUDTGroup::gli_t CUDTGroup::linkSelect_plain(const CUDTGroup::BalancingLinkState& state)
{
    if (m_Group.empty())
    {
        // Should be impossible, but fallback just in case.
        return m_Group.end();
    }

    if (state.ilink == m_Group.end())
    {
        // Very first sending operation. Pick up the first link
        return m_Group.begin();
    }

    gli_t this_link = state.ilink;

    for (;;)
    {
        // Roll to the next link
        ++this_link;
        if (this_link == m_Group.end())
            this_link = m_Group.begin(); // roll around

        // Check the status. If the link is PENDING or BROKEN,
        // skip it. If the link is IDLE, turn it to ACTIVE.
        // If the rolling reached back to the original link,
        // and this one isn't usable either, return m_Group.end().

        if (this_link->sndstate == SRT_GST_IDLE)
        {
            HLOGC(gmlog.Debug, log << "linkSelect_plain: activating link [" << distance(m_Group.begin(), this_link) << "] @" << this_link->id);
            this_link->sndstate = SRT_GST_RUNNING;
        }

        if (this_link->sndstate == SRT_GST_RUNNING)
        {
            // Found you, buddy. Go on.
            HLOGC(gmlog.Debug, log << "linkSelect_plain: SELECTING link [" << distance(m_Group.begin(), this_link) << "] @" << this_link->id);
            return this_link;
        }

        if (this_link == state.ilink)
        {
            // No more links. Sorry.
            HLOGC(gmlog.Debug, log << "linkSelect_plain: rolled back to first link not running - bailing out");
            return m_Group.end();
        }

        // Check maybe next link...
    }

    return this_link;
}

struct LinkCapableData
{
    CUDTGroup::gli_t link;
    int flight;
};

CUDTGroup::gli_t CUDTGroup::linkSelect_window(const CUDTGroup::BalancingLinkState& state)
{
    if (state.ilink == m_Group.end())
    {
        // Very first sending operation. Pick up the first link
        return m_Group.begin();
    }


    gli_t this_link = m_Group.end();

    if (m_RandomCredit <= 0)
    {
        vector<LinkCapableData> linkdata;
        int total_flight = 0;
        int number_links = 0;

        // First, collect data required for selection
        vector<gli_t> linkorder;

        gli_t last = state.ilink;
        ++last;
        // NOTE: ++last could make it == m_Group.end() in which
        // case the first loop will get 0 passes and the second
        // one will be from begin() to end().
        for (gli_t li = last; li != m_Group.end(); ++li)
            linkorder.push_back(li);
        for (gli_t li = m_Group.begin(); li != last; ++li)
            linkorder.push_back(li);

        // Sanity check
        if (linkorder.empty())
        {
            LOGC(gslog.Error, log << "linkSelect_window: IPE: no links???");
            return m_Group.end();
        }

        // Fallback
        this_link = *linkorder.begin();

        // This does the following:
        // We have links: [ 1 2 3 4 5 ]
        // Last used link was 4
        // linkorder: [ (5) (1) (2) (3) (4) ]
        for (vector<gli_t>::iterator i = linkorder.begin(); i != linkorder.end(); ++i)
        {
            gli_t li = *i;
            int flight = li->ps->core().m_iSndMinFlightSpan;

            HLOGC(gslog.Debug, log << "linkSelect_window: previous link was #" << distance(m_Group.begin(), state.ilink)
                    << " Checking link #" << distance(m_Group.begin(), li)
                    << "@" << li->id << " TO " << li->peer.str()
                    << " flight=" << flight);

            // Upgrade idle to running
            if (li->sndstate == SRT_GST_IDLE)
                li->sndstate = SRT_GST_RUNNING;

            if (li->sndstate != SRT_GST_RUNNING)
            {
                HLOGC(gslog.Debug, log << "linkSelect_window: ... state=" << StateStr(li->sndstate) << " - skipping");
                // Skip pending/broken links
                continue;
            }

            // Check if this link was used at least once so far.
            // If not, select it immediately.
            if (li->load_factor == 0)
            {
                HLOGC(gslog.Debug, log << "linkSelect_window: ... load factor empty: SELECTING.");
                this_link = li;
                goto ReportLink;
            }

            ++number_links;
            if (flight == -1)
            {
                HLOGC(gslog.Debug, log << "linkSelect_window: link #" << distance(m_Group.begin(), this_link)
                        << " HAS NO FLIGHT COUNTED - selecting, deferring to next 18 * numberlinks=" << number_links << " packets.");
                // Not measureable flight. Use this link.
                this_link = li;

                // Also defer next measurement point by 16 per link.
                // Of course, number_links doesn't contain the exact
                // number of active links (the loop is underway), but
                // it doesn't matter much. The probability is on the
                // side of later links, so it's unlikely that earlier
                // links could enforce more often update (worst case
                // scenario, the probing will happen again in 16 packets).
                m_RandomCredit = 16 * number_links;

                goto ReportLink;
            }
            flight += 2; // prevent having 0 used for equations

            total_flight += flight;

            linkdata.push_back( (LinkCapableData){li, flight} );
        }

        if (linkdata.empty())
        {
            HLOGC(gslog.Debug, log << "linkSelect_window: no capable links found - requesting transmission interrupt!");
            return m_Group.end();
        }

        this_link = linkdata.begin()->link;
        double least_load = linkdata.begin()->link->load_factor;
        double biggest_unit_load = 0;

        HLOGC(gslog.Debug, log << "linkSelect_window: total_flight (with fix): " << total_flight
                << " - updating link load factors:");
        // Now that linkdata list is ready, update the link span values
        // If at least one link has the span value not yet measureable
        for (vector<LinkCapableData>::iterator i = linkdata.begin();
                i != linkdata.end(); ++i)
        {
            // Here update the unit load basing on the percentage
            // of the link flight size.
            //
            // The sum of all flight window sizes from all links is
            // the total number. The value of the flight size for
            // each link shows how much of a percentage this link
            // has as share.
            //
            // Example: in case when all links go totally equally,
            // and there is 5 links, each having 10 packets in flight:
            //
            // total_flitht = 50
            // share_load = link_flight / total_flight = 10/50 = 1/5
            // link_load = share_load * number_links = 1/5 * 5 = 1.0
            //
            // If the links are not perfectly equivalent, some deviation
            // towards 1.0 will result.
            double share_load = double(i->flight) / total_flight;
            double link_load = share_load * number_links;
            i->link->unit_load = link_load;

            HLOGC(gslog.Debug, log << "linkSelect_window: ... #" << distance(m_Group.begin(), i->link)
                    << " flight=" << i->flight << " share_load=" << (100*share_load) << "% unit-load="
                    << link_load << " current-load:" << i->link->load_factor);

            if (link_load > biggest_unit_load)
                biggest_unit_load = link_load;

            if (i->link->load_factor < least_load)
            {
                HLOGC(gslog.Debug, log << "linkSelect_window: ... this link has currently smallest load");
                this_link = i->link;
                least_load = i->link->load_factor;
            }
        }

        HLOGC(gslog.Debug, log << "linkSelect_window: selecting link #" << distance(m_Group.begin(), this_link));
        // Now that a link is selected and all load factors updated,
        // do a CUTOFF by the value of at least one size of unit load.


        // This comparison can be used to recognize if all values of
        // the load factor have already exceeded the value that should
        // result in a cutoff.
        if (biggest_unit_load > 0 && least_load > 2 * biggest_unit_load)
        {
            for (vector<LinkCapableData>::iterator i = linkdata.begin();
                    i != linkdata.end(); ++i)
            {
                i->link->load_factor -= biggest_unit_load;
            }
            HLOGC(gslog.Debug, log << "linkSelect_window: cutting off value of " << biggest_unit_load
                    << " from all load factors");
        }

        // The above loop certainly found something.
        goto ReportLink;
    }

    HLOGC(gslog.Debug, log << "linkSelect_window: remaining credit: " << m_RandomCredit
            << " - staying with equal balancing");

    // This starts from 16, decreases here. As long as
    // there is a credit given, simply roll over all links
    // equally.
    --m_RandomCredit;

    this_link = state.ilink;
    for (;;)
    {
        // Roll to the next link
        ++this_link;
        if (this_link == m_Group.end())
            this_link = m_Group.begin(); // roll around

        // Check the status. If the link is PENDING or BROKEN,
        // skip it. If the link is IDLE, turn it to ACTIVE.
        // If the rolling reached back to the original link,
        // and this one isn't usable either, return m_Group.end().

        if (this_link->sndstate == SRT_GST_IDLE)
            this_link->sndstate = SRT_GST_RUNNING;

        if (this_link->sndstate == SRT_GST_RUNNING)
        {
            // Found you, buddy. Go on.
            break;
        }

        if (this_link == state.ilink)
        {
            // No more links. Sorry.
            return m_Group.end();
        }

        // Check maybe next link...
    }

ReportLink:

    // When a link is used for sending, the load factor is
    // increased by this link's unit load, which is calculated
    // basing on how big share among all flight sizes this link has.
    // The larger the flight window, the bigger the unit load.
    // This unit load then defines how much "it costs" to send
    // a packet over that link. The bigger this value is then,
    // the less often will this link be selected among others.

    this_link->load_factor += this_link->unit_load;

    HLOGC(gslog.Debug, log << "linkSelect_window: link #" << distance(m_Group.begin(), this_link)
            << " selected, upd load_factor=" << this_link->load_factor);
    return this_link;
}

// Update on adding a new fresh packet to the sender buffer.
// [[using locked(m_GroupLock)]]
bool CUDTGroup::updateSendPacketUnique_LOCKED(int32_t single_seq)
{
    // Check first if the packet wasn't already scheduled
    // If so, do nothing and return success.
    for (gli_t d = m_Group.begin(); d != m_Group.end(); ++d)
    {
        if (find(d->send_schedule.begin(), d->send_schedule.end(), (SchedSeq){single_seq, groups::SQT_FRESH}) != d->send_schedule.end())
        {
            HLOGC(gmlog.Debug, log << "grp/schedule(fresh): already scheduled to %" << d->id << " - skipping");
            return true; // because this should be considered successful, even though didn't schedule.
        }
    }

    BalancingLinkState lstate = { m_Group.active(), 0, 0 };
    gli_t selink =  CALLBACK_CALL(m_cbSelectLink, lstate);
    if (selink == m_Group.end())
    {
        HLOGC(gmlog.Debug, log << "grp/schedule(fresh): no link selected!");
        // If this returns the "trap" link index, it means
        // that no link is qualified for sending.
        return false;
    }

    HLOGC(gmlog.Debug, log << "grp/schedule(fresh): scheduling %" << single_seq << " to @" << selink->id);

    selink->send_schedule.push_back((groups::SchedSeq){single_seq, groups::SQT_FRESH});
    m_Group.set_active(selink);

    // XXX
    // This function is called when the newly scheduled packet by
    // the user is called. Therefore here must be also a procedure
    // to extract RIGHT NOW and schedule (possibly to a side container)
    // packet-filter control packet(s). The original function that
    // possibly creates such a packet should be called here, but
    // there should be also a separate container for them, as they
    // simply can't be referred as sequence to the sender buffer.

    return true;
}

// Update on received loss report or request to retransmit on NAKREPORT.
bool CUDTGroup::updateSendPacketLoss(bool use_send_sched, const std::vector< std::pair<int32_t, int32_t> >& seqlist)
{
    ScopedLock guard(m_LossAckLock);

    typedef std::vector< std::pair<int32_t, int32_t> > seqlist_t;

    int num = 0; // for stats

    HLOGC(gslog.Debug, log << "INITIAL:");
    HLOGC(gslog.Debug, m_pSndLossList->traceState(log));

    // Add the loss list to the groups loss list
    for (seqlist_t::const_iterator seqpair = seqlist.begin(); seqpair != seqlist.end(); ++seqpair)
    {
        int len = m_pSndLossList->insert(seqpair->first, seqpair->second);
        num += len;
        HLOGC(gslog.Debug, log << "LOSS Added: " << Printable(seqlist) << " length: " << len);
        HLOGC(gslog.Debug, m_pSndLossList->traceState(log));
    }

    if (use_send_sched)
    {
        ScopedLock guard(m_GroupLock);

        BalancingLinkState lstate = { m_Group.active(), 0, 0 };

        for (seqlist_t::const_iterator seqpair = seqlist.begin(); seqpair != seqlist.end(); ++seqpair)
        {
            // These are loss ranges, so believe that they are in order.
            pair<int32_t, int32_t> begin_end = *seqpair;
            // The seqpair in the loss list is the first and last, both including,
            // except when there's only one, in which case it's twice the same value.
            // Increase the end seq by one to make it the "past the end seq".
            begin_end.second = CSeqNo::incseq(begin_end.second);

            for (int32_t seq = begin_end.first; seq != begin_end.second; seq = CSeqNo::incseq(seq))
            {
                // Select a link to use for every sequence.
                gli_t selink = CALLBACK_CALL(m_cbSelectLink, lstate);
                if (selink == m_Group.end())
                {
                    // Interrupt all - we have no link candidates to send.
                    HLOGC(gmlog.Debug, log << "grp/schedule(loss): no link selected!");
                    return false;
                }

                HLOGC(gmlog.Debug, log << "grp/schedule(loss): schedule REXMIT %" << seq << " to @" << selink->id);
                selink->send_schedule.push_back((SchedSeq){seq, groups::SQT_LOSS});
                lstate.ilink = selink;
            }
        }

        m_Group.set_active(lstate.ilink);
    }
    return true;
}

bool CUDTGroup::updateOnACK(int32_t ackdata_seqno, int32_t& w_last_sent_seqno)
{
    w_last_sent_seqno = getSentSeq();
    /*
    if (CSeqNo::seqcmp(ackdata_seqno, w_last_sent_seqno) > 0)
        return false;
        */

    ScopedLock guard(m_LossAckLock);
    if (CSeqNo::seqcmp(m_SndLastDataAck, ackdata_seqno) < 0)
    {
        // remove any loss that predates 'ack' (not to be considered loss anymore)
        m_pSndLossList->removeUpTo(CSeqNo::decseq(ackdata_seqno));
        m_SndLastDataAck = ackdata_seqno;
    }

    return true;
}

// This is almost a copy of the CUDT::packLostData except that:
// - it uses a separate mechanism to extract the selected sequence number
// (which is known from the schedule, while the schedule is filled upon incoming loss request)
// - it doesn't check if the loss was received too early (it's more complicated this time)
int CUDTGroup::packLostData(CUDT* core, CPacket& w_packet, int32_t exp_seq)
{
    // protect m_iSndLastDataAck from updating by ACK processing
    UniqueLock ackguard(m_LossAckLock);
    //const steady_clock::time_point time_now = steady_clock::now();
    //const steady_clock::time_point time_nak = time_now - microseconds_from(core->m_iSRTT - 4 * core->m_iRTTVar);

    // XXX This is temporarily used for broadcast with common loss list.
    bool have_extracted = false;
    const char* as = "FIRST FOUND";
    if (exp_seq == SRT_SEQNO_NONE)
    {
        exp_seq = m_pSndLossList->popLostSeq();
        have_extracted = (exp_seq != SRT_SEQNO_NONE);
    }
    else
    {
        as = "EXPECTED";
        have_extracted = m_pSndLossList->popLostSeq(exp_seq);
    }

    HLOGC(gslog.Debug, log << "CUDTGroup::packLostData: " << (have_extracted ? "" : "NOT") << " extracted "
            << as << " %" << exp_seq);

    if (have_extracted)
    {
        steady_clock::time_point origintime;
        w_packet.m_iSeqNo = exp_seq;

        // XXX See the note above the m_iSndLastDataAck declaration in core.h
        // This is the place where the important sequence numbers for
        // sender buffer are actually managed by this field here.
        const int offset = CSeqNo::seqoff(core->m_iSndLastDataAck, w_packet.m_iSeqNo);
        if (offset < 0)
        {
            // XXX Likely that this will never be executed because if the upper
            // sequence is not in the sender buffer, then most likely the loss 
            // was completely ignored.
            LOGC(gslog.Error, log << "IPE/EPE: packLostData: LOST packet negative offset: seqoff(m_iSeqNo "
                << w_packet.m_iSeqNo << ", m_iSndLastDataAck " << core->m_iSndLastDataAck
                << ")=" << offset << ". Continue");

            // No matter whether this is right or not (maybe the attack case should be
            // considered, and some LOSSREPORT flood prevention), send the drop request
            // to the peer.
            int32_t seqpair[2] = {
                w_packet.m_iSeqNo,
                CSeqNo::decseq(core->m_iSndLastDataAck)
            };
            w_packet.m_iMsgNo = 0; // Message number is not known, setting all 32 bits to 0.

            HLOGC(gslog.Debug, log << "PEER reported LOSS not from the sending buffer - requesting DROP: "
                    << "msg=" << MSGNO_SEQ::unwrap(w_packet.m_iMsgNo) << " SEQ:"
                    << seqpair[0] << " - " << seqpair[1] << "(" << (-offset) << " packets)");

            core->sendCtrl(UMSG_DROPREQ, &w_packet.m_iMsgNo, seqpair, sizeof(seqpair));
            return 0;
        }

        int msglen;
        const int payload = core->m_pSndBuffer->readData(offset, (w_packet), (origintime), (msglen));
        if (payload == -1)
        {
            int32_t seqpair[2];
            seqpair[0] = w_packet.m_iSeqNo;
            SRT_ASSERT(msglen >= 1);
            seqpair[1] = CSeqNo::incseq(seqpair[0], msglen - 1);

            HLOGC(gslog.Debug,
                  log << "loss-reported packets expired in SndBuf - requesting DROP: "
                      << "msgno=" << MSGNO_SEQ::unwrap(w_packet.m_iMsgNo) << " msglen=" << msglen
                      << " SEQ:" << seqpair[0] << " - " << seqpair[1]);
            core->sendCtrl(UMSG_DROPREQ, &w_packet.m_iMsgNo, seqpair, sizeof(seqpair));

            // skip all dropped packets
            m_pSndLossList->removeUpTo(seqpair[1]);
            core->m_iSndCurrSeqNo = CSeqNo::maxseq(core->m_iSndCurrSeqNo, seqpair[1]);
            return 0;
        }
        else if (payload == 0)
            return 0;

        // At this point we no longer need the ACK lock,
        // because we are going to return from the function.
        // Therefore unlocking in order not to block other threads.
        ackguard.unlock();

        enterCS(core->m_StatsLock);
        core->m_stats.sndr.sentRetrans.count(payload);
        leaveCS(core->m_StatsLock);

        // Despite the contextual interpretation of packet.m_iMsgNo around
        // CSndBuffer::readData version 2 (version 1 doesn't return -1), in this particular
        // case we can be sure that this is exactly the value of PH_MSGNO as a bitset.
        // So, set here the rexmit flag if the peer understands it.
        if (core->m_bPeerRexmitFlag)
        {
            w_packet.m_iMsgNo |= PACKET_SND_REXMIT;
        }

        // XXX we don't predict any other use of groups than live,
        // so tsbpdmode is always on. Unblock this code otherwise:
        // if (!m_bTsbpdMode)
        // {
        //     origintime = steady_clock::now();
        // }

        // Only assert here. Any user-supplied origin time that is earlier
        // than start time should be rejected with API error.
        SRT_ASSERT(origintime > m_tsStartTime);

        CUDT::setPacketTS(w_packet, m_tsStartTime, origintime);

        return payload;
    }
    else
    {
        // This is not the sequence we are looking for.
        HLOGC(gslog.Debug, log << "packLostData: expected %" << exp_seq << " not found in the group's loss list");
    }

    return 0;
}

SRT_ATR_NODISCARD bool CUDTGroup::getSendSchedule(SocketData* d, vector<groups::SchedSeq>& w_seqs)
{
    // This is going to provide a packet from the packet filter control buffer
    // or sender buffer.

    ScopedLock glock (m_GroupLock);

    if (d->send_schedule.empty())
        return false;

    copy(d->send_schedule.begin(), d->send_schedule.end(),
            back_inserter(w_seqs));

    return true;
}

void CUDTGroup::discardSendSchedule(SocketData* d, int ndiscard)
{
    ScopedLock glock (m_GroupLock);
    if (ndiscard > int(d->send_schedule.size()))
    {
        LOGC(gmlog.Error, log << "grp/discardSendSchedule: IPE: size " << ndiscard << " is out of range of " << d->send_schedule.size() << " (fallback: clear all)");
        d->send_schedule.clear();
    }
    else if (ndiscard == int(d->send_schedule.size()))
    {
        HLOGC(gmlog.Debug, log << "grp/discardSendSchedule: clear all");
        d->send_schedule.clear();
    }
    else
    {
        d->send_schedule.erase(d->send_schedule.begin(), d->send_schedule.begin() + ndiscard);
        HLOGC(gmlog.Debug, log << "grp/discardSendSchedule: drop " << ndiscard << " and keep " << d->send_schedule.size() << " events");
    }
}

// Receiver part

int CUDTGroup::checkLazySpawnLatencyThread()
{
    // It is confirmed that the TSBPD thread is required,
    // so just check if it's running already.

    if (!m_RcvTsbPdThread.joinable())
    {
        ScopedLock lock(m_GroupLock);

        if (m_bClosing) // Check again to protect join() in CUDT::releaseSync()
            return -1;

        HLOGP(qrlog.Debug, "Spawning Group TSBPD thread");
#if ENABLE_HEAVY_LOGGING
        std::ostringstream tns1, tns2;
        // Take the last 2 ciphers from the socket ID.
        tns1 << id();
        std::string s = tns1.str();
        tns2 << "SRT:GLat:$" << s.substr(s.size()-2, 2);

        const string& tn = tns2.str();

        ThreadName tnkeep(tn);
        const string& thname = tn;
#else
        const string thname = "SRT:GLat";
#endif
        if (!StartThread(m_RcvTsbPdThread, CUDTGroup::tsbpd, this, thname))
            return -1;
    }

    return 0;
}

void* CUDTGroup::tsbpd(void* param)
{
    CUDTGroup* self = (CUDTGroup*)param;

    THREAD_STATE_INIT("SRT:GLat");

    // Make the TSBPD thread a "client" of the group,
    // which will ensure that the group will not be physically
    // deleted until this thread exits.
    // NOTE: DO NOT LEAD TO EVER CANCEL THE THREAD!!!
    ScopedGroupKeeper gkeeper(self);

    CUniqueSync recvdata_lcc(self->m_RcvDataLock, self->m_RcvDataCond);
    CSync       tsbpd_cc(self->m_RcvTsbPdCond, recvdata_lcc.locker());

    self->m_bTsbpdWaitForNewPacket = true;
    HLOGC(gmlog.Debug, log << "grp/TSBPD: START");
    while (!self->m_bClosing)
    {
        enterCS(self->m_RcvBufferLock);
        const steady_clock::time_point tnow = steady_clock::now();

        self->m_pRcvBuffer->updRcvAvgDataSize(tnow);
        const srt::CRcvBuffer::PacketInfo info = self->m_pRcvBuffer->getFirstValidPacketInfo();

        const bool is_time_to_deliver = !is_zero(info.tsbpd_time) && (tnow >= info.tsbpd_time);
        steady_clock::time_point tsNextDelivery = info.tsbpd_time;
        bool                             rxready = false;

        HLOGC(tslog.Debug, log << self->CONID() << "grp/tsbpd: packet check: %"
                << info.seqno << " T=" << FormatTime(tsNextDelivery)
                << " diff-now-playtime=" << FormatDuration(tnow - tsNextDelivery)
                << " ready=" << is_time_to_deliver
                << " ondrop=" << info.seq_gap);

        bool synch_loss_after_drop = false;

        if (!self->m_bTLPktDrop)
        {
            rxready = !info.seq_gap && is_time_to_deliver;
        }
        else if (is_time_to_deliver)
        {
            rxready = true;
            if (info.seq_gap)
            {
                const int iDropCnt SRT_ATR_UNUSED = self->rcvDropTooLateUpTo(info.seqno);

                // Part required for synchronizing loss state in all group members should
                // follow the drop, but this must be done outside the lock on the buffer.
                synch_loss_after_drop = iDropCnt;

                const int64_t timediff_us = count_microseconds(tnow - info.tsbpd_time);

#if ENABLE_HEAVY_LOGGING
                HLOGC(tslog.Debug,
                      log << self->CONID() << "grp/tsbpd: DROPSEQ: up to seqno %" << CSeqNo::decseq(info.seqno) << " ("
                          << iDropCnt << " packets) playable at " << FormatTime(info.tsbpd_time) << " delayed "
                          << (timediff_us / 1000) << "." << std::setw(3) << std::setfill('0') << (timediff_us % 1000)
                          << " ms");
#endif
                LOGC(brlog.Warn,
                     log << self->CONID() << "RCV-DROPPED " << iDropCnt << " packet(s). Packet seqno %" << info.seqno
                         << " delayed for " << (timediff_us / 1000) << "." << std::setw(3) << std::setfill('0')
                         << (timediff_us % 1000) << " ms");

                tsNextDelivery = steady_clock::time_point(); // Ready to read, nothing to wait for.
            }
        }
        leaveCS(self->m_RcvBufferLock);

        if (synch_loss_after_drop)
            self->synchronizeLoss(info.seqno);

        if (rxready)
        {
            HLOGC(tslog.Debug,
                  log << self->CONID() << "grp/tsbpd: PLAYING PACKET seq=" << info.seqno << " (belated "
                      << (count_milliseconds(steady_clock::now() - info.tsbpd_time)) << "ms)");
            /*
             * There are packets ready to be delivered
             * signal a waiting "recv" call if there is any data available
             */
            if (self->m_bSynRecving)
            {
                HLOGC(tslog.Debug, log << self->CONID() << "grp/tsbpd: SIGNAL blocking recv()");
                recvdata_lcc.notify_one();
            }
            /*
             * Set EPOLL_IN to wakeup any thread waiting on epoll
             */
            CUDT::uglobal().m_EPoll.update_events(self->id(), self->m_sPollID, SRT_EPOLL_IN, true);
            CGlobEvent::triggerEvent();
            tsNextDelivery = steady_clock::time_point(); // Ready to read, nothing to wait for.
        }
        else
        {
            HLOGC(tslog.Debug, log << self->CONID() << "grp/tsbpd: NEXT PACKET: "
                    << (info.tsbpd_time == time_point() ? "NOT AVAILABLE" : FormatTime(info.tsbpd_time))
                    << " vs. now=" << FormatTime(tnow));
        }

        SRT_ATR_UNUSED bool got_signal = true;

        // None should be true in case when waiting for the next time.
        // If there is a ready packet, but only to be extracted in some time,
        // then sleep until this time and then retry triggering.
        self->m_bTsbpdWaitForNewPacket = false;
        self->m_bTsbpdWaitForExtraction = false;

        // NOTE: if (rxready) then tsNextDelivery == 0. So this branch is for a situation
        // when:
        // - no packet is currently READY for delivery
        // - but there is a packet candidate ready soon.
        // So you have to sleep until it's ready and then trigger read-readiness.
        if (!is_zero(tsNextDelivery))
        {
            IF_HEAVY_LOGGING(const steady_clock::duration timediff = tsNextDelivery - tnow);
            /*
             * Buffer at head of queue is not ready to play.
             * Schedule wakeup when it will be.
             */
            HLOGC(tslog.Debug,
                  log << self->CONID() << "grp/tsbpd: FUTURE PACKET seq=" << info.seqno
                      << " T=" << FormatTime(tsNextDelivery) << " - waiting " << count_milliseconds(timediff) << "ms up to " << FormatTime(tsNextDelivery));
            THREAD_PAUSED();
            got_signal = tsbpd_cc.wait_until(tsNextDelivery);
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
             * - All ready-to-play packets have been pulled and EPOLL_IN cleared (then loop to block until next pkt time
             * if any)
             * - New packet arrived
             * - Closing the connection
             */
            HLOGC(tslog.Debug, log << self->CONID() << "grp/tsbpd: " << (rxready ? "expecting user's packet retrieval" : "no data to deliver") << ", scheduling wakeup on reception");

            // If there was rxready, then epoll readiness was set, and recvdata_lcc was triggered
            // - so it should remain sleeping until the user's thread has extracted EVERY ready packet and turned epoll back to not-ready.
            // Otherwise the situation was that there's no ready packet at all.
            // - so it should remain sleeping until a new packet arrives and it is potentially extractable.
            if (rxready)
            {
                self->m_bTsbpdWaitForExtraction = true;
            }
            else
            {
                self->m_bTsbpdWaitForNewPacket = true;
            }
            THREAD_PAUSED();
            tsbpd_cc.wait();
            THREAD_RESUMED();
        }

        HLOGC(tslog.Debug, log << self->CONID() << "grp/tsbpd: WAKE UP on " << (got_signal ? "signal" : "timeout")
                << "; now=" << FormatTime(steady_clock::now()));
    }
    THREAD_EXIT();
    HLOGC(tslog.Debug, log << self->CONID() << "grp/tsbpd: EXITING");
    return NULL;
}


#if ENABLE_HEAVY_LOGGING
// [[using maybe_locked(CUDT::uglobal()->m_GlobControlLock)]]
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

} // namespace srt
