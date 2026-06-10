#include <thread>

#include "gtest/gtest.h"
#include "test_env.h"

#include "srt.h"
#include "api.h"
#include "core.h"
#include "packet.h"

using namespace srt;

namespace srt {
    // Friend wrapper for unit tests that drive private CUDT control-packet
    // entry points. Declared as a friend in core.h.
    class TestMockControlPackets
    {
    public:
        CUDT* core;

        bool processCtrl(const CPacket& pkt) { return core->processCtrl(pkt); }
        void processCtrlLossReport(const CPacket& pkt) { core->processCtrlLossReport(pkt); }
        int32_t rcvCurrSeqNo() const { return core->m_iRcvCurrSeqNo; }
        void setRcvCurrSeqNo(int32_t v) { core->m_iRcvCurrSeqNo = v; }
        bool isBroken() const { return core->m_bBroken; }
    };
}

class ControlPackets: public srt::Test
{
public:
    SRTSOCKET caller = SRT_INVALID_SOCK;
    SRTSOCKET listener = SRT_INVALID_SOCK;
    SRTSOCKET accepted = SRT_INVALID_SOCK;
    CUDTSocket* pcaller = NULL;
    TestMockControlPackets cmock;

    static void swipe(SRTSOCKET& sockid)
    {
        if (sockid == SRT_INVALID_SOCK)
            return;

        EXPECT_NE(srt_close(sockid), SRT_ERROR);
        sockid = SRT_INVALID_SOCK;
    }

    void setup() override
    {
        caller = CUDT::uglobal().newSocket(&pcaller);
        ASSERT_NE(caller, SRT_INVALID_SOCK);
        cmock.core = &pcaller->core();

        ASSERT_NE(listener = srt_create_socket(), SRT_INVALID_SOCK);

        srt::sockaddr_any sa = srt::CreateAddr("localhost", 5555, AF_INET);
        ASSERT_NE(srt_bind(listener, sa.get(), sa.size()), SRT_ERROR);
        ASSERT_NE(srt_listen(listener, 1), SRT_ERROR);

        std::thread spawned_connect( [this, &sa] { EXPECT_NE(srt_connect(caller, sa.get(), sa.size()), SRT_ERROR); });

        accepted = srt_accept(listener, NULL, 0);
        spawned_connect.join();
        ASSERT_NE(accepted, SRT_ERROR);
    }

    void stop()
    {
        swipe(caller);
    }

    void teardown() override
    {
        swipe(caller);
        swipe(accepted);
        swipe(listener);
    }
};

// processCtrlDropReq must reject DROPREQs whose payload is smaller than two
// seqno words; otherwise dropdata[1] reads past the wire payload.
TEST_F(ControlPackets, DropReqRejectsShortPayload)
{
    const int32_t sentinel = 100;
    cmock.setRcvCurrSeqNo(sentinel);

    CPacket pkt;
    pkt.allocate(1500);
    pkt.setControl(UMSG_DROPREQ);

    // Each of these is shorter than the 8-byte minimum and must be rejected
    // by the guard at the top of processCtrlDropReq.
    const size_t short_lens[] = { 0, 1, 4, 7 };
    for (size_t i = 0; i < sizeof(short_lens) / sizeof(short_lens[0]); ++i)
    {
        pkt.setLength(short_lens[i]);
        EXPECT_FALSE(cmock.processCtrl(pkt));
        EXPECT_EQ(cmock.rcvCurrSeqNo(), sentinel)
            << "DROPREQ with payload " << short_lens[i] << " bytes must not be processed";
    }
}

// processCtrlDropReq must reject DROPREQs whose (lo, hi) seqno range is
// reversed. Otherwise CRcvBuffer::dropMessage walks the circular buffer from
// offset(lo) past offset(hi)+1 via incPos() and wipes nearly every entry --
// a DoS primitive triggerable by a single malicious DROPREQ.
TEST_F(ControlPackets, DropReqRejectsReversedRange)
{
    const int32_t sentinel = 1000;
    cmock.setRcvCurrSeqNo(sentinel);

    CPacket pkt;
    pkt.allocate(8);
    int32_t* data = (int32_t*) pkt.m_pcData;
    data[0] = 2000;  // lo
    data[1] = 1500;  // hi  (seqcmp(lo, hi) > 0)
    pkt.setLength(8);
    pkt.setControl(UMSG_DROPREQ);

    // With the guard, this returns before touching m_pRcvBuffer (NULL on
    // an unconnected socket -- would crash if the guard were missing).
    EXPECT_FALSE(cmock.processCtrl(pkt));

    EXPECT_EQ(cmock.rcvCurrSeqNo(), sentinel);
}

// processCtrlLossReport must reject a LOSSREPORT whose final cell carries
// the LOSSDATA_SEQNO_RANGE_FIRST marker but has no HI cell behind it;
// otherwise losslist[i+1] reads past the wire payload (4-byte OOB read of
// adjacent heap). The handler should mark the connection broken via the
// `secure = false` path.
TEST_F(ControlPackets, LossReportRejectsTrailingRangeFirst)
{
    // Single 4-byte payload, high bit set => LOSSDATA_SEQNO_RANGE_FIRST.
    // Without the guard, the handler would dereference losslist[1] (the
    // missing HI cell), reading 4 bytes past the packet payload.
    CPacket pkt;
    pkt.allocate(64);
    int32_t* data = (int32_t*) pkt.m_pcData;
    data[0] = SEQNO_VALUE::wrap(100) | LOSSDATA_SEQNO_RANGE_FIRST;
    pkt.setLength(sizeof(int32_t));
    pkt.setControl(UMSG_LOSSREPORT);

    EXPECT_FALSE(cmock.processCtrl(pkt));

    EXPECT_TRUE(cmock.isBroken())
        << "LOSSREPORT with trailing range-first marker must take the "
           "secure=false bail path and mark the connection broken";
}

