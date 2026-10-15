#include <chrono>
#include <cstring>
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
        void storeAck(int32_t ackno, int32_t seq) { core->m_ACKWindow.store(ackno, seq); }
        bool isFirstRTTReceived() const { return core->m_bIsFirstRTTReceived; }
        bool isPeerHealthy() const { return core->m_bPeerHealth; }
        sync::steady_clock::duration sendInterval() const { return core->m_tdSendInterval; }
        void setSendInterval(const sync::steady_clock::duration& d) { core->m_tdSendInterval = d; }

        // Simulate the state left by closeInternal(), which resets
        // m_pCryptoControl (destroying the HaiCrypt contexts) under
        // m_ConnectionLock.
        void releaseCrypto() { core->m_pCryptoControl.reset(); }
        bool hasCrypto() const { return core->m_pCryptoControl.get() != NULL; }
        void setClosing(bool v) { core->m_bClosing = v; }
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


// Issue #3403: KEEPALIVE, CGWARNING, SHUTDOWN, ACKACK and PEERERROR have no
// Control Information Field. SRT pads them with 4 bytes, but other
// implementations may send them with an empty payload, which must be accepted.
TEST_F(ControlPackets, AckAckAcceptsEmptyPayload)
{
    const int32_t ackno = 12345;
    cmock.storeAck(ackno, 1000);
    // Make sure the RTT computed from the ACK/ACKACK pair is positive.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    CPacket pkt;
    pkt.pack(UMSG_ACKACK, &ackno);
    pkt.setLength(0);

    EXPECT_TRUE(cmock.processCtrl(pkt));
    EXPECT_TRUE(cmock.isFirstRTTReceived()) << "empty ACKACK must produce an RTT sample";
}

TEST_F(ControlPackets, KeepaliveAcceptsEmptyPayload)
{
    CPacket pkt;
    pkt.pack(UMSG_KEEPALIVE);
    pkt.setLength(0);

    EXPECT_TRUE(cmock.processCtrl(pkt));
    EXPECT_FALSE(cmock.isBroken());
}

TEST_F(ControlPackets, CgWarningAcceptsEmptyPayload)
{
    CPacket pkt;
    pkt.pack(UMSG_CGWARNING);
    pkt.setLength(0);

    // The CGWARNING handler always returns false, so check its effect instead:
    // the sending interval is increased by 12.5%.
    const sync::steady_clock::duration before = sync::microseconds_from(1000);
    cmock.setSendInterval(before);
    cmock.processCtrl(pkt);
    EXPECT_EQ(cmock.sendInterval(), sync::microseconds_from(1125));
    EXPECT_FALSE(cmock.isBroken());
}

TEST_F(ControlPackets, PeerErrorAcceptsEmptyPayload)
{
    const int32_t err_code = 4000;
    CPacket pkt;
    pkt.pack(UMSG_PEERERROR, &err_code);
    pkt.setLength(0);

    EXPECT_TRUE(cmock.processCtrl(pkt));
    EXPECT_FALSE(cmock.isPeerHealthy());
}

TEST_F(ControlPackets, ShutdownAcceptsEmptyPayload)
{
    CPacket pkt;
    pkt.pack(UMSG_SHUTDOWN);
    pkt.setLength(0);

    EXPECT_TRUE(cmock.processCtrl(pkt));
    EXPECT_TRUE(cmock.isBroken());
}

TEST_F(ControlPackets, RejectsInvalidPayloadSize)
{
    CPacket pkt;
    pkt.allocate(64);
    memset(pkt.m_pcData, 0, 64);

    // Messages carrying a Control Information Field still require a payload.
    pkt.setLength(0);
    const UDTMessageType with_cif[] = { UMSG_HANDSHAKE, UMSG_ACK, UMSG_LOSSREPORT, UMSG_DROPREQ};
    for (size_t i = 0; i < Size(with_cif); ++i)
    {
        pkt.setControl(with_cif[i]);
        EXPECT_FALSE(cmock.processCtrl(pkt)) << "empty payload must be rejected for type " << srt::MessageTypeStr(with_cif[i]);
    }

    // A payload not aligned to 4 bytes is rejected for every type.
    pkt.setLength(2);
    const UDTMessageType without_cif[] = { UMSG_KEEPALIVE, UMSG_CGWARNING, UMSG_SHUTDOWN, UMSG_ACKACK, UMSG_PEERERROR };
    for (size_t i = 0; i < sizeof(without_cif) / sizeof(without_cif[0]); ++i)
    {
        pkt.setControl(without_cif[i]);
        EXPECT_FALSE(cmock.processCtrl(pkt)) << "unaligned payload must be rejected for type " << without_cif[i];
    }

    // Additionally UMSG_EXT + SRT_CMD_NONE - this is the only passthrough handled.
    pkt.setExtendedType(SRT_CMD_NONE);
    EXPECT_FALSE(cmock.processCtrl(pkt)) << "empty payload must be rejected for type " << srt::MessageTypeStr(UMSG_EXT, SRT_CMD_NONE);

    EXPECT_FALSE(cmock.isBroken());
}

// A KMREQ / KMRSP (UMSG_EXT, subtype SRT_CMD_KMREQ/KMRSP) may be delivered to
// the receive-queue worker at the same moment the owning thread runs
// srt_close(), which resets m_pCryptoControl and destroys the HaiCrypt
// contexts. processSrtMsg() must not dereference a freed/null crypto control:
// it must observe the closing state under m_ConnectionLock and bail out. Without
// the guard this reproduces the heap-use-after-free / NULL deref in
// hcryptCtx_GenSecret reported for libsrt 1.5.7.
TEST_F(ControlPackets, KmReqDuringCloseIsIgnored)
{
    // Model the window after closeInternal() has reset m_pCryptoControl.
    cmock.releaseCrypto();
    cmock.setClosing(true);
    ASSERT_FALSE(cmock.hasCrypto());

    CPacket pkt;
    pkt.allocate(64);
    memset(pkt.m_pcData, 0, 64);
    pkt.setLength(16); // aligned, non-empty KM payload

    // KMREQ: the worker path that reaches processSrtMsg_KMREQ -> HaiCrypt.
    pkt.setExtendedType(SRT_CMD_KMREQ);
    EXPECT_FALSE(cmock.processCtrl(pkt)) << "KMREQ during close must be ignored, not crash";

    // KMRSP: the companion path.
    pkt.setExtendedType(SRT_CMD_KMRSP);
    EXPECT_FALSE(cmock.processCtrl(pkt)) << "KMRSP during close must be ignored, not crash";

    EXPECT_FALSE(cmock.isBroken());
}
