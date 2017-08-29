
#include <string>
#include <cmath>
#include "common.h"
#include "core.h"
#include "packet.h"
#include "smoother.h"

using namespace std;


void SmootherBase::Initialize(CUDT* parent)
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

    typedef LiveSmoother Me; // required for SSLOT macro

public:
    LiveSmoother(CUDT* parent)
    {
        Initialize(parent);

        m_llSndMaxBW = BW_INFINITE;    // 30Mbps in Bytes/sec BW_INFINITE
        m_iSndAvgPayloadSize = 7*188; // XXX = 1316 -- shouldn't be configurable?
        updatePktSndPeriod();

        parent->ConnectSignal(TEV_SEND, SSLOT(updatePayloadSize));

        /*
         * Readjust the max SndPeriod onACK (and onTimeout)
         */
        parent->ConnectSignal(TEV_CHECKTIMER, SSLOT(updatePktSndPeriod_onTimer));
        parent->ConnectSignal(TEV_ACK, SSLOT(updatePktSndPeriod_onAck));
    }


    virtual int64_t sndBandwidth() ATR_OVERRIDE { return m_llSndMaxBW; }

    // SLOTS:

    // TEV_SEND -> CPacket*.
    void updatePayloadSize(ETransmissionEvent, EventVariant var)
    {
        const CPacket& packet = *var.get<EventVariant::PACKET>();
        m_iSndAvgPayloadSize = ((m_iSndAvgPayloadSize * 127) + packet.getLength()) / 128;
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
        double pktsize = m_iSndAvgPayloadSize + CPacket::HDR_SIZE + CPacket::UDP_HDR_SIZE;
        m_dPktSndPeriod = 1000000.0 * (pktsize/m_llSndMaxBW);
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
    FileSmoother(CUDT* parent)
    {
        Initialize(parent);
        // Note that this function is called at the moment of
        // calling m_Smoother.configure(this). It is placed more less
        // at the same position as the series-of-parameter-setting-then-init
        // in the original UDT code. So, old CUDTCC::init() can be moved
        // to constructor.

        m_iRCInterval = CPacket::SYN_INTERVAL;
        m_LastRCTime = CTimer::getTime();
        m_iACKPeriod = CPacket::SYN_INTERVAL;

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
    }

    void updateBandwidth(int64_t maxbw, int64_t) ATR_OVERRIDE
    {
        if (maxbw != 0)
            m_maxSR = maxbw;
    }

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
                    m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
                else
                    m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
            }
        }
        else
            m_dCWndSize = m_parent->deliveryRate() / 1000000.0 * (m_parent->RTT() + m_iRCInterval) + 16;

        // During Slow Start, no rate increase
        if (m_bSlowStart)
            goto RATE_LIMIT;

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
        //set maximum transfer rate
        if (m_maxSR)
        {
            double minSP = 1000000.0 / (double(m_maxSR) / m_parent->MSS());
            if (m_dPktSndPeriod < minSP)
                m_dPktSndPeriod = minSP;
        }

    }

    void slowdownSndPeriod(ETransmissionEvent, EventVariant arg)
    {
        const int32_t* losslist = arg.get_ptr();
        size_t losslist_size = arg.get_len();

        // Sanity check. Should be impossible that TEV_LOSSREPORT event
        // is called with a nonempty loss list.
        if ( losslist_size == 0 )
            return;

        //Slow Start stopped, if it hasn't yet
        if (m_bSlowStart)
        {
            m_bSlowStart = false;
            if (m_parent->deliveryRate() > 0)
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
            else
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
        }

        m_bLoss = true;

        // In contradiction to UDT, this will effectively fire not only
        // when the first-time loss was detected, but also when the loss
        // report is to be sent due to NAKREPORT feature. Hopefully the
        // "repeated loss report" case is to be cut off by this condition.
        if (CSeqNo::seqcmp(SEQNO_VALUE::unwrap(losslist[0]), m_iLastDecSeq) > 0)
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
        }
        else if ((m_iDecCount ++ < 5) && (0 == (++ m_iNAKCount % m_iDecRandom)))
        {
            // 0.875^5 = 0.51, rate should not be decreased by more than half within a congestion period
            m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
            m_iLastDecSeq = m_parent->sndSeqNo();
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
                m_dPktSndPeriod = 1000000.0 / m_parent->deliveryRate();
            else
                m_dPktSndPeriod = m_dCWndSize / (m_parent->RTT() + m_iRCInterval);
        }
        else
        {
            /*
               m_dLastDecPeriod = m_dPktSndPeriod;
               m_dPktSndPeriod = ceil(m_dPktSndPeriod * 2);
               m_iLastDecSeq = m_iLastAck;
             */
        }
    }

};


#undef SSLOT

template <class Target>
struct Creator
{
    static SmootherBase* Create(CUDT* parent) { return new Target(parent); }
};

typedef map<string, smoother_create_t*> smoother_base_t;
smoother_base_t Smoother::registerred_smoothers;

class SmootherBaseInitializer
{
public:
    SmootherBaseInitializer()
    {
        Smoother::registerred_smoothers["live"] = Creator<LiveSmoother>::Create;
        Smoother::registerred_smoothers["file"] = Creator<FileSmoother>::Create;
    }

} g_smoother_base_initializer;


bool Smoother::configure(CUDT* parent)
{
    if (selector == registerred_smoothers.end())
        return false;

    // Found a smoother, so call the creation function
    smoother = (*selector->second)(parent);

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
