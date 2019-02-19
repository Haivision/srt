/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


// This is a controversial thing, so temporarily blocking
//#define SRT_ENABLE_SYSTEMBUFFER_TRACE




#ifdef SRT_ENABLE_SYSTEMBUFFER_TRACE
#if defined(unix)
// XXX will be nonportable
#include <sys/ioctl.h>
#endif
#endif

#include <string>
#include <cmath>


#include "common.h"
#include "core.h"
#include "queue.h"
#include "packet.h"
#include "smoother.h"
#include "logging.h"

using namespace std;
using namespace srt_logging;

SmootherBase::SmootherBase(CUDT* parent)
{
    m_parent = parent;
    m_dMaxCWndSize = m_parent->flowWindowSize();
    // RcvRate (deliveryRate()), RTT and Bandwidth can be read directly from CUDT when needed.
    m_dCWndSize = 1000;
    m_dPktSndPeriod = 1;
}

void Smoother::Check()
{
    if (!smoother)
        throw CUDTException(MJ_CONNECTION, MN_NOCONN, 0);
}

// Useful macro to shorthand passing a method as argument
// Requires "Me" name by which a class refers to itself
#define SSLOT(method) EventSlot(this, &Me:: method)

class LiveSmoother: public SmootherBase
{
    int64_t  m_llSndMaxBW;          //Max bandwidth (bytes/sec)
    int      m_iSndAvgPayloadSize;  //Average Payload Size of packets to xmit
    size_t   m_zMaxPayloadSize;

    // NAKREPORT stuff.
    int m_iMinNakInterval_us;                       // Minimum NAK Report Period (usec)
    int m_iNakReportAccel;                       // NAK Report Period (RTT) accelerator

    typedef LiveSmoother Me; // required for SSLOT macro

public:

    LiveSmoother(CUDT* parent): SmootherBase(parent)
    {
        m_llSndMaxBW = BW_INFINITE;    // 30Mbps in Bytes/sec BW_INFINITE
        m_zMaxPayloadSize = parent->OPT_PayloadSize();
        if ( m_zMaxPayloadSize == 0 )
            m_zMaxPayloadSize = parent->maxPayloadSize();
        m_iSndAvgPayloadSize = m_zMaxPayloadSize;

        m_iMinNakInterval_us = 20000;   //Minimum NAK Report Period (usec)
        m_iNakReportAccel = 2;       //Default NAK Report Period (RTT) accelerator

        HLOGC(mglog.Debug, log << "Creating LiveSmoother: bw=" << m_llSndMaxBW << " avgplsize=" << m_iSndAvgPayloadSize);

        updatePktSndPeriod();


        // NOTE: TEV_SEND gets dispatched from Sending thread, all others
        // from receiving thread.
        parent->ConnectSignal(TEV_SEND, SSLOT(updatePayloadSize));

        /*
         * Readjust the max SndPeriod onACK (and onTimeout)
         */
        parent->ConnectSignal(TEV_CHECKTIMER, SSLOT(updatePktSndPeriod_onTimer));
        parent->ConnectSignal(TEV_ACK, SSLOT(updatePktSndPeriod_onAck));
    }

    bool checkTransArgs(Smoother::TransAPI api, Smoother::TransDir dir, const char* , size_t size, int , bool ) ATR_OVERRIDE
    {
        if (api != Smoother::STA_MESSAGE)
        {
            LOGC(mglog.Error, log << "LiveSmoother: invalid API use. Only sendmsg/recvmsg allowed.");
            return false;
        }

        if (dir == Smoother::STAD_SEND)
        {
            // For sending, check if the size of data doesn't exceed the maximum live packet size.
            if (size > m_zMaxPayloadSize)
            {
                LOGC(mglog.Error, log << "LiveSmoother: payload size: " << size << " exceeds maximum allowed " << m_zMaxPayloadSize);
                return false;
            }
        }
        else
        {
            // For receiving, check if the buffer has enough space to keep the payload.
            if (size < m_zMaxPayloadSize)
            {
                LOGC(mglog.Error, log << "LiveSmoother: buffer size: " << size << " is too small for the maximum possible " << m_zMaxPayloadSize);
                return false;
            }
        }

        return true;
    }

    // XXX You can decide here if the not-fully-packed packet should require immediate ACK or not.
    // bool needsQuickACK(const CPacket& pkt) ATR_OVERRIDE

    virtual int64_t sndBandwidth() ATR_OVERRIDE { return m_llSndMaxBW; }

private:
    // SLOTS:

    // TEV_SEND -> CPacket*.
    void updatePayloadSize(ETransmissionEvent, EventVariant var)
    {
        const CPacket& packet = *var.get<EventVariant::PACKET>();

        // XXX NOTE: TEV_SEND is sent from CSndQueue::worker thread, which is
        // different to threads running any other events (TEV_CHECKTIMER and TEV_ACK).
        // The m_iSndAvgPayloadSize field is however left unguarded because as
        // 'int' type it's considered atomic, as well as there's no other modifier
        // of this field. Worst case scenario, the procedure running in CRcvQueue::worker
        // thread will pick up a "slightly outdated" average value from this
        // field - this is insignificant.
        m_iSndAvgPayloadSize = avg_iir<128, int>(m_iSndAvgPayloadSize, packet.getLength());
        HLOGC(mglog.Debug, log << "LiveSmoother: avg payload size updated: " << m_iSndAvgPayloadSize);
    }

    void updatePktSndPeriod_onTimer(ETransmissionEvent , EventVariant var)
    {
        if ( var.get<EventVariant::STAGE>() != TEV_CHT_INIT )
            updatePktSndPeriod();
    }

    void updatePktSndPeriod_onAck(ETransmissionEvent , EventVariant )
    {
        updatePktSndPeriod();
    }

    void updatePktSndPeriod()
    {
        // packet = payload + header
        double pktsize = m_iSndAvgPayloadSize + CPacket::SRT_DATA_HDR_SIZE;
        m_dPktSndPeriod = 1000*1000.0 * (pktsize/m_llSndMaxBW);
        HLOGC(mglog.Debug, log << "LiveSmoother: sending period updated: " << m_iSndAvgPayloadSize);
    }

    void setMaxBW(int64_t maxbw)
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

    void updateBandwidth(int64_t maxbw, int64_t bw) ATR_OVERRIDE
    {
        // bw is the bandwidth calculated with regard to the
        // SRTO_INPUTBW and SRTO_OHEADBW parameters. The maxbw
        // value simply represents the SRTO_MAXBW setting.
        if (maxbw)
        {
            setMaxBW(maxbw);
            return;
        }

        if (bw == 0)
        {
            return;
        }

        setMaxBW(bw);
    }

    Smoother::RexmitMethod rexmitMethod() ATR_OVERRIDE
    {
        return Smoother::SRM_FASTREXMIT;
    }

    uint64_t updateNAKInterval(uint64_t nakint_tk, int /*rcv_speed*/, size_t /*loss_length*/) ATR_OVERRIDE
    {
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
         * Finally, the UDT4 native minimum NAK interval (m_ullMinNakInt_tk) is 300 ms which is too high
         * (~10 i30 video frames) to maintain low latency.
         */

        // Note: this value will still be reshaped to defined minimum,
        // as per minNAKInterval.
        return nakint_tk / m_iNakReportAccel;
    }

    uint64_t minNAKInterval() ATR_OVERRIDE
    {
        return m_iMinNakInterval_us * CTimer::getCPUFrequency();
    }

};


class FileSmoother: public SmootherBase
{
    typedef FileSmoother Me; // Required by SSLOT macro

    // Fields from CCC not used by LiveSmoother
    int m_iACKPeriod;

    // Fields from CUDTCC
    int m_iRCInterval;			// UDT Rate control interval
    uint64_t m_LastRCTime;		// last rate increase time
    bool m_bSlowStart;			// if in slow start phase
    int32_t m_iLastAck;			// last ACKed seq no
    bool m_bLoss;			// if loss happened since last rate increase
    int32_t m_iLastDecSeq;		// max pkt seq no sent out when last decrease happened
    double m_dLastDecPeriod;		// value of pktsndperiod when last decrease happened
    int m_iNAKCount;                     // NAK counter
    int m_iDecRandom;                    // random threshold on decrease by number of loss events
    int m_iAvgNAKNum;                    // average number of NAKs per congestion
    int m_iDecCount;			// number of decreases in a congestion epoch

    int64_t m_maxSR;

public:
    FileSmoother(CUDT* parent): SmootherBase(parent)
    {
        // Note that this function is called at the moment of
        // calling m_Smoother.configure(this). It is placed more less
        // at the same position as the series-of-parameter-setting-then-init
        // in the original UDT code. So, old CUDTCC::init() can be moved
        // to constructor.

        m_iRCInterval = CUDT::COMM_SYN_INTERVAL_US;
        m_LastRCTime = CTimer::getTime();
        m_iACKPeriod = CUDT::COMM_SYN_INTERVAL_US;

        m_bSlowStart = true;
        m_iLastAck = parent->sndSeqNo();
        m_bLoss = false;
        m_iLastDecSeq = CSeqNo::decseq(m_iLastAck);
        m_dLastDecPeriod = 1;
        m_iAvgNAKNum = 0;
        m_iNAKCount = 0;
        m_iDecRandom = 1;

        // SmotherBase
        m_dCWndSize = 16;
        m_dPktSndPeriod = 1;

        m_maxSR = 0;

        parent->ConnectSignal(TEV_ACK, SSLOT(updateSndPeriod));
        parent->ConnectSignal(TEV_LOSSREPORT, SSLOT(slowdownSndPeriod));
        parent->ConnectSignal(TEV_CHECKTIMER, SSLOT(speedupToWindowSize));

        HLOGC(mglog.Debug, log << "Creating FileSmoother");
    }

    bool checkTransArgs(Smoother::TransAPI, Smoother::TransDir, const char* , size_t , int , bool ) ATR_OVERRIDE
    {
        // XXX
        // The FileSmoother has currently no restrictions, although it should be
        // rather required that the "message" mode or "buffer" mode be used on both sides the same.
        // This must be somehow checked separately.
        return true;
    }

    bool needsQuickACK(const CPacket& pkt) ATR_OVERRIDE
    {
        // For FileSmoother, treat non-full-buffer situation as an end-of-message situation;
        // request ACK to be sent immediately.
        if (pkt.getLength() < m_parent->maxPayloadSize())
            return true;

        return false;
    }

    void updateBandwidth(int64_t maxbw, int64_t) ATR_OVERRIDE
    {
        if (maxbw != 0)
        {
            m_maxSR = maxbw;
            HLOGC(mglog.Debug, log << "FileSmoother: updated BW: " << m_maxSR);
        }
    }

private:

    // SLOTS
    void updateSndPeriod(ETransmissionEvent, EventVariant arg)
    {
        int ack = arg.get<EventVariant::ACK>();

        int64_t B = 0;
        double inc = 0;

        uint64_t currtime = CTimer::getTime();
        if (currtime - m_LastRCTime < (uint64_t)m_iRCInterval)
            return;

        m_LastRCTime = currtime;

        if (m_bSlowStart)
        {
            m_dCWndSize += CSeqNo::seqlen(m_iLastAck, ack);
            m_iLastAck = ack;

            if (m_dCWndSize > m_dMaxCWndSize)
            {
                m_bSlowStart = false;
                if (m_parent->deliveryRate() > 0)
                {
                    m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                    HLOGC(mglog.Debug, log << "FileSmoother: UPD (slowstart:ENDED) wndsize="
                        << m_dCWndSize << "/" << m_dMaxCWndSize
                        << " sndperiod=" << m_dPktSndPeriod << "us = mega/("
                        << m_parent->deliveryRate() << "B/s)");
                }
                else
                {
                    m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                    HLOGC(mglog.Debug, log << "FileSmoother: UPD (slowstart:ENDED) wndsize="
                        << m_dCWndSize << "/" << m_dMaxCWndSize
                        << " sndperiod=" << m_dPktSndPeriod << "us = wndsize/(RTT+RCIV) RTT="
                        << m_parent->RTT() << " RCIV=" << m_iRCInterval);
                }
            }
            else
            {
                HLOGC(mglog.Debug, log << "FileSmoother: UPD (slowstart:KEPT) wndsize="
                    << m_dCWndSize << "/" << m_dMaxCWndSize
                    << " sndperiod=" << m_dPktSndPeriod << "us");
            }
        }
        else
        {
            m_dCWndSize = m_parent->deliveryRate() / 1000000.0 * (m_parent->RTT() + m_iRCInterval) + 16;
        }

        // During Slow Start, no rate increase
        if (m_bSlowStart)
        {
            goto RATE_LIMIT;
        }

        if (m_bLoss)
        {
            m_bLoss = false;
            goto RATE_LIMIT;
        }

        B = (int64_t)(m_parent->bandwidth() - 1000000.0 / m_dPktSndPeriod);
        if ((m_dPktSndPeriod > m_dLastDecPeriod) && ((m_parent->bandwidth() / 9) < B))
            B = m_parent->bandwidth() / 9;
        if (B <= 0)
            inc = 1.0 / m_parent->MSS();
        else
        {
            // inc = max(10 ^ ceil(log10( B * MSS * 8 ) * Beta / MSS, 1/MSS)
            // Beta = 1.5 * 10^(-6)

            inc = pow(10.0, ceil(log10(B * m_parent->MSS() * 8.0))) * 0.0000015 / m_parent->MSS();

            if (inc < 1.0/m_parent->MSS())
                inc = 1.0/m_parent->MSS();
        }

        m_dPktSndPeriod = (m_dPktSndPeriod * m_iRCInterval) / (m_dPktSndPeriod * inc + m_iRCInterval);

RATE_LIMIT:

#if ENABLE_HEAVY_LOGGING
        // Try to do reverse-calculation for m_dPktSndPeriod, as per minSP below
        // sndperiod = mega / (maxbw / MSS)
        // 1/sndperiod = (maxbw/MSS) / mega
        // mega/sndperiod = maxbw/MSS
        // maxbw = (MSS*mega)/sndperiod
        uint64_t usedbw = (m_parent->MSS() * 1000000.0) / m_dPktSndPeriod;

#if defined(unix) && defined (SRT_ENABLE_SYSTEMBUFFER_TRACE)
        // Check the outgoing system queue level
        int udp_buffer_size = m_parent->sndQueue()->sockoptQuery(SOL_SOCKET, SO_SNDBUF);
        int udp_buffer_level = m_parent->sndQueue()->ioctlQuery(TIOCOUTQ);
        int udp_buffer_free = udp_buffer_size - udp_buffer_level;
#else
        int udp_buffer_free = -1;
#endif

        HLOGC(mglog.Debug, log << "FileSmoother: UPD (slowstart:"
            << (m_bSlowStart ? "ON" : "OFF") << ") wndsize=" << m_dCWndSize
            << " sndperiod=" << m_dPktSndPeriod << "us BANDWIDTH USED:" << usedbw << " (limit: " << m_maxSR << ")"
            " SYSTEM BUFFER LEFT: " << udp_buffer_free);
#endif

        //set maximum transfer rate
        if (m_maxSR)
        {
            double minSP = 1000000.0 / (double(m_maxSR) / m_parent->MSS());
            if (m_dPktSndPeriod < minSP)
            {
                m_dPktSndPeriod = minSP;
                HLOGC(mglog.Debug, log << "FileSmoother: BW limited to " << m_maxSR
                    << " - SLOWDOWN sndperiod=" << m_dPktSndPeriod << "us");
            }
        }

    }

    // When a lossreport has been received, it might be due to having
    // reached the available bandwidth limit. Slowdown to avoid further losses.
    void slowdownSndPeriod(ETransmissionEvent, EventVariant arg)
    {
        const int32_t* losslist = arg.get_ptr();
        size_t losslist_size = arg.get_len();

        // Sanity check. Should be impossible that TEV_LOSSREPORT event
        // is called with a nonempty loss list.
        if ( losslist_size == 0 )
        {
            LOGC(mglog.Error, log << "IPE: FileSmoother: empty loss list!");
            return;
        }

        //Slow Start stopped, if it hasn't yet
        if (m_bSlowStart)
        {
            m_bSlowStart = false;
            if (m_parent->deliveryRate() > 0)
            {
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                HLOGC(mglog.Debug, log << "FileSmoother: LOSS, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS mega/rate (rate="
                    << m_parent->deliveryRate() << ")");
            }
            else
            {
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                HLOGC(mglog.Debug, log << "FileSmoother: LOSS, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS wndsize/(RTT+RCIV) (RTT="
                    << m_parent->RTT() << " RCIV=" << m_iRCInterval << ")");
            }

        }

        m_bLoss = true;

        // In contradiction to UDT, TEV_LOSSREPORT will be reported also when
        // the lossreport is being sent again, periodically, as a result of
        // NAKREPORT feature. You should make sure that NAKREPORT is off when
        // using FileSmoother, so relying on SRTO_TRANSTYPE rather than
        // just SRTO_SMOOTHER is recommended.
        int32_t lossbegin = SEQNO_VALUE::unwrap(losslist[0]);

        if (CSeqNo::seqcmp(lossbegin, m_iLastDecSeq) > 0)
        {
            m_dLastDecPeriod = m_dPktSndPeriod;
            m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);

            m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * 0.875 + m_iNAKCount * 0.125);
            m_iNAKCount = 1;
            m_iDecCount = 1;

            m_iLastDecSeq = m_parent->sndSeqNo();

            // remove global synchronization using randomization
            srand(m_iLastDecSeq);
            m_iDecRandom = (int)ceil(m_iAvgNAKNum * (double(rand()) / RAND_MAX));
            if (m_iDecRandom < 1)
                m_iDecRandom = 1;
            HLOGC(mglog.Debug, log << "FileSmoother: LOSS:NEW lastseq=" << m_iLastDecSeq
                << ", rand=" << m_iDecRandom
                << " avg NAK:" << m_iAvgNAKNum
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }
        else if ((m_iDecCount ++ < 5) && (0 == (++ m_iNAKCount % m_iDecRandom)))
        {
            // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
            m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
            m_iLastDecSeq = m_parent->sndSeqNo();
            HLOGC(mglog.Debug, log << "FileSmoother: LOSS:PERIOD lseq=" << lossbegin
                << ", dseq=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", deccnt=" << m_iDecCount
                << ", decrnd=" << m_iDecRandom
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }
        else
        {
            HLOGC(mglog.Debug, log << "FileSmoother: LOSS:STILL lseq=" << lossbegin
                << ", dseq=" << m_iLastDecSeq
                << ", seqdiff=" << CSeqNo::seqoff(m_iLastDecSeq, lossbegin)
                << ", deccnt=" << m_iDecCount
                << ", decrnd=" << m_iDecRandom
                << ", sndperiod=" << m_dPktSndPeriod << "us");
        }

    }

    void speedupToWindowSize(ETransmissionEvent, EventVariant arg)
    {
        ECheckTimerStage stg = arg.get<EventVariant::STAGE>();

        // TEV_INIT is in the beginning of checkTimers(), used
        // only to synchronize back the values (which is done in updateCC
        // after emitting the signal).
        if (stg == TEV_CHT_INIT)
            return;

        if (m_bSlowStart)
        {
            m_bSlowStart = false;
            if (m_parent->deliveryRate() > 0)
            {
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                HLOGC(mglog.Debug, log << "FileSmoother: CHKTIMER, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS mega/rate (rate="
                    << m_parent->deliveryRate() << ")");
            }
            else
            {
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
                HLOGC(mglog.Debug, log << "FileSmoother: CHKTIMER, SLOWSTART:OFF, sndperiod=" << m_dPktSndPeriod << "us AS wndsize/(RTT+RCIV) (wndsize="
                    << setprecision(6) << m_dCWndSize << " RTT=" << m_parent->RTT() << " RCIV=" << m_iRCInterval << ")");
            }
        }
        else
        {
            // XXX This code is a copy of legacy CUDTCC::onTimeout() body.
            // This part was commented out there already.
            /*
               m_dLastDecPeriod = m_dPktSndPeriod;
               m_dPktSndPeriod = ceil(m_dPktSndPeriod * 2);
               m_iLastDecSeq = m_iLastAck;
             */
        }
    }

    Smoother::RexmitMethod rexmitMethod() ATR_OVERRIDE
    {
        return Smoother::SRM_LATEREXMIT;
    }
};


#undef SSLOT

template <class Target>
struct Creator
{
    static SmootherBase* Create(CUDT* parent) { return new Target(parent); }
};

Smoother::NamePtr Smoother::smoothers[N_SMOOTHERS] =
{
    {"live", Creator<LiveSmoother>::Create },
    {"file", Creator<FileSmoother>::Create }
};


bool Smoother::configure(CUDT* parent)
{
    if (selector == N_SMOOTHERS)
        return false;

    // Found a smoother, so call the creation function
    smoother = (*smoothers[selector].second)(parent);

    // The smoother should have pinned in all events
    // that are of its interest. It's stated that
    // it's ready after creation.
    return !!smoother;
}

Smoother::~Smoother()
{
    delete smoother;
    smoother = 0;
}
