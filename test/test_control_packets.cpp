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

        void processCtrlDropReq(const CPacket& pkt) { core->processCtrlDropReq(pkt); }
        int32_t rcvCurrSeqNo() const { return core->m_iRcvCurrSeqNo; }
        void setRcvCurrSeqNo(int32_t v) { core->m_iRcvCurrSeqNo = v; }
    };
}

// processCtrlDropReq must reject DROPREQs whose payload is smaller than two
// seqno words; otherwise dropdata[1] reads past the wire payload.
TEST(ControlPackets, DropReqRejectsShortPayload)
{
    srt::TestInit srtinit;

    CUDTSocket* s1 = NULL;
    SRTSOCKET sid1 = CUDT::uglobal().newSocket(&s1);
    ASSERT_NE(sid1, SRT_INVALID_SOCK);

    TestMockControlPackets m1;
    m1.core = &s1->core();

    const int32_t sentinel = 100;
    m1.setRcvCurrSeqNo(sentinel);

    CPacket pkt;
    pkt.allocate(1500);

    // Each of these is shorter than the 8-byte minimum and must be rejected
    // by the guard at the top of processCtrlDropReq.
    const size_t short_lens[] = { 0, 1, 4, 7 };
    for (size_t i = 0; i < sizeof(short_lens) / sizeof(short_lens[0]); ++i)
    {
        pkt.setLength(short_lens[i]);
        m1.processCtrlDropReq(pkt);
        EXPECT_EQ(m1.rcvCurrSeqNo(), sentinel)
            << "DROPREQ with payload " << short_lens[i] << " bytes must not be processed";
    }

    pkt.deallocate();
    srt_close(sid1);
}
