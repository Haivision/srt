#if ENABLE_NEW_RCVBUFFER

#include "gtest/gtest.h"
#include <array>
#include <numeric>

#include "buffer_rcv.h"
#include "sync.h"

using namespace std;
using namespace srt;
using namespace srt::sync;

/*!
 * This set of tests has the following CRcvBufferNew common configuration:
 * - TSBPD ode = OFF
 *
 */

class TestRcvBuffer2Read : public ::testing::Test
{
protected:
    TestRcvBuffer2Read()
    {
        // initialization code here
    }

    ~TestRcvBuffer2Read()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:
    // Constructs CRcvBufferNew and CUnitQueue
    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        // make_unique is unfortunatelly C++14
        m_unit_queue = unique_ptr<CUnitQueue>(new CUnitQueue);
        m_unit_queue->init(m_buff_size_pkts, 1500, AF_INET);
        m_rcv_buffer = unique_ptr<CRcvBufferNew>(new CRcvBufferNew(m_init_seqno, m_buff_size_pkts, m_unit_queue.get(), true));
    }

    // Destructs CRcvBufferNew and CUnitQueue
    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
        m_unit_queue.reset();
        m_rcv_buffer.reset();
    }

public:
    using steady_clock = srt::sync::steady_clock;

    /// Generate and add one packet to the receiver buffer.
    ///
    /// @returns the result of rcv_buffer::insert(..)
    int addPacket(int seqno, bool pb_first = true, bool pb_last = true, bool out_of_order = false, int ts = 0)
    {
        CUnit* unit = m_unit_queue->getNextAvailUnit();
        EXPECT_NE(unit, nullptr);

        CPacket& packet = unit->m_Packet;
        packet.m_iSeqNo = seqno;
        packet.m_iTimeStamp = ts;

        packet.setLength(m_payload_sz);
        generatePayload(packet.data(), packet.getLength(), packet.m_iSeqNo);

        packet.m_iMsgNo = PacketBoundaryBits(PB_SUBSEQUENT);
        if (pb_first)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_FIRST);
        if (pb_last)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_LAST);

        if (!out_of_order)
        {
            packet.m_iMsgNo |= MSGNO_PACKET_INORDER::wrap(1);
            EXPECT_TRUE(packet.getMsgOrderFlag());
        }

        return m_rcv_buffer->insert(unit);
    }

    /// @returns 0 on success, the result of rcv_buffer::insert(..) once it failed
    int addMessage(size_t msg_len_pkts, int start_seqno, bool out_of_order = false, int ts = 0)
    {
        for (size_t i = 0; i < msg_len_pkts; ++i)
        {
            const bool pb_first = (i == 0);
            const bool pb_last = (i == (msg_len_pkts - 1));
            const int res = addPacket(start_seqno + i, pb_first, pb_last, out_of_order, ts);

            if (res != 0)
                return res;
        }
        return 0;
    }

    void generatePayload(char* dst, size_t len, int seqno) { std::iota(dst, dst + len, (char)seqno); }

    bool verifyPayload(char* dst, size_t len, int seqno)
    {
        // Note. A more consistent way would be to use generatePayload function,
        // but I don't want to create another buffer for the data.
        for (size_t i = 0; i < len; ++i)
        {
            if (dst[i] != static_cast<char>(seqno + i))
                return false;
        }
        return true;
    }

    void checkPacketPos(CUnit* unit)
    {
        // TODO: check that a certain packet was placed into the right
        // position with right offset.

        // m_rcv_buffer->peek(offset) == unit;
    }

protected:
    unique_ptr<CUnitQueue>  m_unit_queue;
    unique_ptr<CRcvBufferNew> m_rcv_buffer;
    const int               m_buff_size_pkts = 16;
    const int               m_init_seqno     = 1000;
    static const size_t     m_payload_sz     = 1456;

    const steady_clock::time_point m_tsbpd_base = steady_clock::now(); // now() - HS.timestamp, microseconds
    const steady_clock::duration m_delay = srt::sync::milliseconds_from(200);
};

/// One packet is added to the buffer. Is allowed to be read.
/// Don't allow to add packet with the same sequence number.
///
/// 1. insert
///   /
/// +---+  ---+---+---+---+---+   +---+
/// | 1 |   0 | 0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+  ---+---+---+---+---+   +---+
///   \
/// 2. read
///
TEST_F(TestRcvBuffer2Read, OnePacket)
{
    const size_t msg_pkts = 1;
    // Adding one message  without acknowledging
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), 0);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), -1) << "Adding a packet into the same position must return -1.";

    const size_t  msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;
    int read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, msg_bytelen);

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), -1);
    EXPECT_EQ(read_len, msg_bytelen);
}

/// One packet is added to the buffer. Is allowed to be read on TSBPD condition.
///
/// 1. insert
///   /
/// +---+  ---+---+---+---+---+   +---+
/// | 1 |   0 | 0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+  ---+---+---+---+---+   +---+
///   \
/// 2. read
///
TEST_F(TestRcvBuffer2Read, OnePacketTSBPD)
{
    const size_t msg_pkts = 1;
    
    m_rcv_buffer->setTsbPdMode(m_tsbpd_base, false, m_delay);

    const int packet_ts = 0;
    // Adding one message. Note that all packets are out of order in TSBPD mode.
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, true, packet_ts), 0);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    // There is one packet in the buffer, but not ready to read after delay/2
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + (m_delay / 2)));

    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, true, packet_ts), -1);

    // There is one packet in the buffer, but not ready to read after delay/2
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay));

    // Read out the first message
    const int read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, msg_bytelen);
    EXPECT_TRUE(verifyPayload(buff.data(), read_len, m_init_seqno));

    // Check the state after a packet was read
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay));
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), -2);

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay));
}

/// One packet is added to the buffer after 1-packet gap. Should be read only after ACK.
/// 1. insert
///       |
/// +---+---+  ---+---+---+---+   +---+
/// | 0 | 1 |   0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+---+  ---+---+---+---+   +---+
///   \    \
/// 2. read
///
TEST_F(TestRcvBuffer2Read, OnePacketAfterGapDrop)
{
    const size_t msg_pkts = 1;
    // 1. Add one message (1 packet) without acknowledging
    // with a gap of one packet.
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno + 1, false), 0);
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady()) << "No packet to read at this point";

    // 2. Try to read message. Expect to get an error due to the missing first packet.
    const size_t                      msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;
    EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), -1) << "No packet to read due to the gap";

    const auto next_packet = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(next_packet.seqno, m_init_seqno + 1);

    m_rcv_buffer->dropUpTo(next_packet.seqno);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), msg_bytelen);
    EXPECT_TRUE(verifyPayload(buff.data(), msg_bytelen, m_init_seqno + 1));

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
}

// The same as above, but a missing packet is added to the buffer.
TEST_F(TestRcvBuffer2Read, OnePacketAfterGapAdd)
{
    const size_t msg_pkts = 1;
    // 1. Add one message (1 packet) without acknowledging
    // with a gap of one packet.
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno + 1, false), 0);
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady()) << "No packet to read at this point";

    // 2. Try to read message. Expect to get an error due to the missing first packet.
    const size_t                      msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;
    EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), -1) << "No packet to read due to the gap";

    const auto next_packet = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(next_packet.seqno, m_init_seqno + 1);

    // Add a missing packet
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), 0);

    for (int pktno = 0; pktno < 2; ++pktno)
    {
        EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
        EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), msg_bytelen);
        EXPECT_TRUE(verifyPayload(buff.data(), msg_bytelen, m_init_seqno + pktno));
    }

    // 5. Further read is not possible
    //EXPECT_FALSE(m_rcv_buffer->canAck());
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
}

// One message (4 packets) are added to the buffer.
// Check if reading is only possible once the whole message is present in the buffer.
TEST_F(TestRcvBuffer2Read, LongMessage)
{
    const size_t msg_pkts = 4;
    // 1. Add one message (4 packets) without acknowledging
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), 0);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    const size_t                      msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    // TODO: Check reading if insufficient space
    //EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), m_payload_sz), -1);
    //EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        EXPECT_TRUE(verifyPayload(buff.data() + i * m_payload_sz, m_payload_sz, m_init_seqno + i));
    }

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
}

// One message (4 packets) are added to the buffer. Can be read out of order.
// Reading should be possible even before the missing packet arrives.
// After a missing packet arrived and read, memory units of the previously read message are freed.
TEST_F(TestRcvBuffer2Read, MsgOutOfOrderAdd)
{
    const size_t msg_pkts = 4;
    // 1. Add one message (4 packets) without acknowledging
    const int msg_seqno = m_init_seqno + 1; // seqno of the first packet in the message
    EXPECT_EQ(addMessage(msg_pkts, msg_seqno, true), 0);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    // 2. Read full message after gap.
    const size_t                      msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;
    int                               res = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        EXPECT_TRUE(verifyPayload(buff.data() + i * m_payload_sz, m_payload_sz, msg_seqno + i));
    }

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());

    // Can't add to the same message
    EXPECT_EQ(addMessage(msg_pkts, msg_seqno, true), -1);

    // Add missing packet
    EXPECT_EQ(addMessage(1, m_init_seqno, true), 0);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    const size_t  one_msg_bytelen = m_payload_sz; // Assertion of a static variable m_payload_sz results in linking error
    EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), one_msg_bytelen);
    EXPECT_TRUE(verifyPayload(buff.data(), one_msg_bytelen, m_init_seqno));
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());

    // All memory usits are expected to be freed.
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// One message (4 packets) are added to the buffer. Can be read out of order.
// Reading should be possible even before the missing packet is dropped.
// After a missing packet is dropped, memory units of the previously read message are freed.
TEST_F(TestRcvBuffer2Read, MsgOutOfOrderDrop)
{
    const size_t msg_pkts = 4;
    // 1. Add one message (4 packets) without acknowledging
    const int msg_seqno = m_init_seqno + 1; // seqno of the first packet in the message
    EXPECT_EQ(addMessage(msg_pkts, msg_seqno, true), 0);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    // 2. Read full message after gap.
    const size_t                      msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;
    int                               res = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        EXPECT_TRUE(verifyPayload(buff.data() + i * m_payload_sz, m_payload_sz, msg_seqno + i));
    }

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());

    // Can't add to the same message
    EXPECT_EQ(addMessage(msg_pkts, msg_seqno, true), -1);

    const auto pkt_info = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(pkt_info.seqno, msg_seqno);

    // Drop missing packet
    m_rcv_buffer->dropUpTo(msg_seqno);
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    // All memory units are expected to be freed.
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// One message (4 packets) is added to the buffer after a message with "in order" flag.
// Read in order
TEST_F(TestRcvBuffer2Read, MsgOutOfOrderAfterInOrder)
{
    const size_t msg_pkts = 4;
    // 1. Add one packet with inOrder=true and one message (4 packets) with inOrder=false
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno + 2 * msg_pkts, true), 0);
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), 0);
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno + msg_pkts, true), 0);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    // 2. Read messages in order

    const size_t                      msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;
    for (int msg_i = 0; msg_i < 3; ++msg_i)
    {
        EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
        EXPECT_EQ(m_rcv_buffer->readMessage(buff.data(), buff.size()), msg_bytelen);
        for (size_t i = 0; i < msg_pkts; ++i)
        {
            EXPECT_TRUE(verifyPayload(buff.data() + i * m_payload_sz, m_payload_sz, m_init_seqno + msg_i * msg_pkts + i));
        }    
    }

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
}

#endif // ENABLE_NEW_RCVBUFFER
