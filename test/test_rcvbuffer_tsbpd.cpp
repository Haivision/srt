#include "gtest/gtest.h"
#include <array>
#include <memory>
#include <numeric>

#if ENABLE_NEW_RCVBUFFER

#include "buffer_rcv.h"
#include "sync.h"

using namespace std;
using namespace srt;
#if 0

class TestRcvBufferTSBPD
    : public ::testing::Test
{
    using steady_clock = srt::sync::steady_clock;
protected:
    TestRcvBufferTSBPD()
    {
        // initialization code here
    }

    ~TestRcvBufferTSBPD()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:

    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        //m_unit_queue = make_unique<CUnitQueue>();
        m_unit_queue = unique_ptr<CUnitQueue>(new CUnitQueue);
        m_unit_queue->init(m_buff_size_pkts, 1500, AF_INET);
        //m_rcv_buffer = make_unique<CRcvBufferNew>(m_init_seqno, m_buff_size_pkts);
        m_rcv_buffer = unique_ptr<CRcvBufferNew>(new CRcvBufferNew(m_init_seqno, m_buff_size_pkts, m_unit_queue.get()));
        //m_rcv_buffer->setTsbPdMode(m_tsbpd_base, false, m_delay, steady_clock::duration(0));
    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_unit_queue.reset();
        m_rcv_buffer.reset();
    }

protected:

    unique_ptr<CUnitQueue>  m_unit_queue;
    unique_ptr<CRcvBufferNew> m_rcv_buffer;
    const int m_buff_size_pkts = 16;
    const int m_init_seqno = 1000;

    const steady_clock::time_point m_tsbpd_base = steady_clock::now(); // now() - HS.timestamp, microseconds
    const steady_clock::duration m_delay = srt::sync::milliseconds_from(200);
};




/// TSBPD = ON, not acknowledged ready to play packet is preceeded by a missing packet.
/// So the CRcvBufferNew::updateState() function should drop the missing packet.
/// The packet has a timestamp of 200 us.
/// The TSBPD delay is set to 200 ms. This means, that the packet can be played
/// not earlier than after 200200 microseconds from the peer start time.
/// The peer start time is set to 100000 us.
///
/// 
/// |<m_iMaxPosInc>|
/// |          /
/// |        /
/// |       |
/// +---+---+---+---+---+---+   +---+
/// | 0 | 1 | 0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+---+---+---+---+---+   +---+
/// |       |
/// |       \__last pkt received
/// |
/// \___ m_iStartPos: first message to read
///  \___ m_iLastAckPos: last ack sent
///
/// m_pUnit[i]->m_iFlag: 0:free, 1:good, 2:passack, 3:dropped
///
TEST_F(TestRcvBufferTSBPD, UnackPreceedsMissing)
{
    // Add a solo packet to position m_init_seqno + 1 with timestamp 200 us
    const int seqno = m_init_seqno + 1;
    const size_t pld_size = 1456;
    CUnit* unit = m_unit_queue->getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_iFlag = CUnit::GOOD;
    CPacket& pkt = unit->m_Packet;
    pkt.setLength(pld_size);
    pkt.m_iSeqNo = seqno;
    pkt.m_iMsgNo |= PacketBoundaryBits(PB_SOLO);
    pkt.m_iTimeStamp = static_cast<int32_t>(200);
    EXPECT_EQ(m_rcv_buffer->insert(unit), 0);

    const uint64_t readready_timestamp = m_peer_start_time_us + pkt.m_iTimeStamp + m_delay_us;
    // Check that getFirstValidPacketInfo() returns first valid packet.
    const auto pkt_info = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(pkt_info.tsbpd_time, readready_timestamp);
    EXPECT_EQ(pkt_info.seqno, seqno);
    EXPECT_TRUE(pkt_info.seq_gap);

    // The packet is not yet acknowledges, so we can't read it
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(readready_timestamp));

    // The packet is preceeded by a gap, so we can't acknowledge it
    //EXPECT_FALSE(m_rcv_buffer->canAck());

    // Update at time before read readyness should not change anything.
    //m_rcv_buffer->updateState(readready_timestamp - 1);
    //EXPECT_FALSE(m_rcv_buffer->canAck());

    // updateState() should drop the missing packet
    //m_rcv_buffer->updateState(readready_timestamp);

    // Now the missing packet is droped, so we can acknowledge the existing packet.
    //EXPECT_TRUE(m_rcv_buffer->canAck());

}



/// In this test case one packet is inserted into the CRcvBufferNew.
/// TSBPD mode is ON. The packet has a timestamp of 200 us.
/// The TSBPD delay is set to 200 ms. This means, that the packet can be played
/// not earlier than after 200200 microseconds from the peer start time.
/// The peer start time is set to 100000 us.
TEST_F(TestRcvBufferTSBPD, ReadMessage)
{
    int seqno = m_init_seqno;
    const size_t payload_size = 1456;
    CUnit* unit = m_unit_queue->getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_iFlag = CUnit::GOOD;

    std::array<char, payload_size> src_buffer;
    std::iota(src_buffer.begin(), src_buffer.end(), (char)0);

    CPacket& pkt = unit->m_Packet;
    pkt.setLength(payload_size);
    pkt.m_iSeqNo = seqno;
    pkt.m_iMsgNo |= PacketBoundaryBits(PB_SOLO);
    pkt.m_iTimeStamp = static_cast<int32_t>(200);
    memcpy(pkt.m_pcData, src_buffer.data(), src_buffer.size());
    EXPECT_EQ(m_rcv_buffer->insert(unit), 0);

    const auto pkt_info = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(pkt_info.tsbpd_time, m_peer_start_time_us + pkt.m_iTimeStamp + m_delay_us);

    // The packet is not yet acknowledges, so we can't read it
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(pkt_info.tsbpd_time - 1));

    //m_rcv_buffer->ack(CSeqNo::incseq(seqno));
    // Expect it is not time to read the next packet
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(pkt_info.tsbpd_time - 1));
    EXPECT_TRUE (m_rcv_buffer->isRcvDataReady(pkt_info.tsbpd_time));
    EXPECT_TRUE (m_rcv_buffer->isRcvDataReady(pkt_info.tsbpd_time + 1));

    // Read the message from the buffer
    std::array<char, payload_size> read_buffer;
    const int read_len = m_rcv_buffer->readMessage(read_buffer.data(), read_buffer.size());
    EXPECT_EQ(read_len, read_buffer.size());
    EXPECT_TRUE(read_buffer == src_buffer);
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
}



/// TSBPD mode = ON.
/// A packet is acknowledged and is ready to be read.


/// TSBPD mode = ON.
/// A packet is acknowledged, but not ready to be read
/// In case of blocking RCV call, we can wait directly on the buffer. So no TSBPD thread
/// is needed. However, the SRTO_RCVSYN mode can be turned on in runtime (no protection in setOpt()).
/// In case of non-bocking RCV call, epoll has to be signalled at a certain time.
/// For blocking 



/// TSBPD mode = ON.
/// A packet is not acknowledged, but ready to be read, and has some preceeding missing packet.
/// In that case all missing packets have to be dropped up to the first ready packet. And wait for ACK of that packet.
/// So those missing packets should be removed from the receiver's loss list, and the receiver's buffer
/// has to skip m_iStartPos and m_iLastAckPos up to that packet.

#endif

#endif // ENABLE_NEW_RCVBUFFER
