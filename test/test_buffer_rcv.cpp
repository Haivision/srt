#include <array>
#include <numeric>
#include "gtest/gtest.h"
#include "buffer_rcv.h"

using namespace srt;
using namespace std;

class CRcvBufferReadMsg
    : public ::testing::Test
{
protected:
    CRcvBufferReadMsg(bool message_api = true)
        : m_use_message_api(message_api)
    {
        // initialization code here
    }

    virtual ~CRcvBufferReadMsg()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        // make_unique is unfortunatelly C++14
        m_unit_queue.reset(new CUnitQueue(m_buff_size_pkts, 1500));
        ASSERT_NE(m_unit_queue.get(), nullptr);

        const bool enable_msg_api = m_use_message_api;
        const bool enable_peer_rexmit = true;
        m_rcv_buffer.reset(new CRcvBuffer(m_init_seqno, m_buff_size_pkts, m_unit_queue.get(), enable_msg_api));
        m_rcv_buffer->setPeerRexmitFlag(enable_peer_rexmit);
        ASSERT_NE(m_rcv_buffer.get(), nullptr);
    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_rcv_buffer.reset();
        m_unit_queue.reset();
    }

public:
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

        auto info = m_rcv_buffer->insert(unit);
        // XXX extra checks?

        return int(info.result);
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

    int ackPackets(int num_pkts)
    {
        m_first_unack_seqno = CSeqNo::incseq(m_first_unack_seqno, num_pkts);
        return 0;
    }

    int getAvailBufferSize()
    {
        return m_rcv_buffer->getAvailSize(m_first_unack_seqno);
    }

    int readMessage(char* data, size_t len)
    {
        return m_rcv_buffer->readMessage(data, len);
    }

    bool hasAvailablePackets()
    {
        return m_rcv_buffer->hasAvailablePackets();
    }

protected:
    unique_ptr<CUnitQueue> m_unit_queue;
    unique_ptr<CRcvBuffer> m_rcv_buffer;
    const int m_buff_size_pkts = 16;
    const int m_init_seqno = 1000;
    int m_first_unack_seqno = m_init_seqno;
    static const size_t m_payload_sz = 1456;
    const bool m_use_message_api;

    const sync::steady_clock::time_point m_tsbpd_base = sync::steady_clock::now(); // now() - HS.timestamp, microseconds
    const sync::steady_clock::duration m_delay = sync::milliseconds_from(200);
};

// Check the available size of the receiver buffer.
TEST_F(CRcvBufferReadMsg, Create)
{
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
}

// Check that destroying the buffer also frees memory units.
TEST_F(CRcvBufferReadMsg, Destroy)
{
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < getAvailBufferSize(); ++i)
        EXPECT_EQ(addMessage(1, CSeqNo::incseq(m_init_seqno, i)), 0);

    m_rcv_buffer.reset();
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// Fill the buffer full, and check adding more data results in an error.
TEST_F(CRcvBufferReadMsg, FullBuffer)
{
    auto& rcv_buffer = *m_rcv_buffer.get();
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < getAvailBufferSize(); ++i)
    {
        EXPECT_EQ(addMessage(1, CSeqNo::incseq(m_init_seqno, i)), 0);
    }

    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);   // logic

    ackPackets(m_buff_size_pkts - 1);
    EXPECT_EQ(getAvailBufferSize(), 0);

    // Try to add more data than the available size of the buffer
    EXPECT_EQ(addPacket(CSeqNo::incseq(m_init_seqno, getAvailBufferSize())), -1);

    array<char, m_payload_sz> buff;
    for (int i = 0; i < m_buff_size_pkts - 1; ++i)
    {
        const int res = rcv_buffer.readBuffer(buff.data(), buff.size());
        EXPECT_TRUE(size_t(res) == m_payload_sz);
        EXPECT_TRUE(verifyPayload(buff.data(), res, CSeqNo::incseq(m_init_seqno, i)));
    }

    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// BUG in the old RCV buffer!!!
// In this test case a packet is added to receiver buffer with offset 1,
// thus leaving offset 0 with an empty pointer.
// The buffer says it is not empty, and the data is available
// to be read, but reading is not possible.
TEST_F(CRcvBufferReadMsg, OnePacketGap)
{
    // Add one packet message to to the buffer
    // with a gap of one packet.
    EXPECT_EQ(addMessage(1, CSeqNo::incseq(m_init_seqno)), 0);

    auto& rcv_buffer = *m_rcv_buffer.get();
    // Before ACK the available buffer size stays the same.
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    // Not available for reading as not yet acknowledged.
    EXPECT_FALSE(hasAvailablePackets());
    // Confirm reading zero bytes.
    array<char, m_payload_sz> buff;
    int res = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // BUG. Acknowledging an empty position must not result in a read-readiness.
    // TODO: Actually we should not acknowledge, but must drop instead.
    ackPackets(1);
    EXPECT_FALSE(hasAvailablePackets());
    EXPECT_FALSE(rcv_buffer.isRcvDataReady());

    const auto next_packet = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(next_packet.seqno, m_init_seqno + 1);

    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 2);
    // The buffer will return 0 as reading is not available.
    res = rcv_buffer.readBuffer(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    res = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // Add a missing packet (can't add before an acknowledged position in the old buffer).
    EXPECT_EQ(addMessage(1, m_init_seqno), 0);

    for (int pktno = 0; pktno < 2; ++pktno)
    {
        const size_t msg_bytelen = m_payload_sz;
        EXPECT_TRUE(rcv_buffer.isRcvDataReady());
        EXPECT_EQ(readMessage(buff.data(), buff.size()), msg_bytelen);
        EXPECT_TRUE(verifyPayload(buff.data(), msg_bytelen, CSeqNo::incseq(m_init_seqno, pktno)));
    }
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());

    // Further read is not possible
    EXPECT_FALSE(rcv_buffer.isRcvDataReady());
}

/// One packet is added to the buffer after 1-packet gap. Should be read only after ACK.
/// 1. insert (1)
///       |
/// +---+---+  ---+---+---+---+   +---+
/// | 0 | 1 |   0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+---+  ---+---+---+---+   +---+
/// 2. drop (0)
/// 2. read (1)
///
TEST_F(CRcvBufferReadMsg, OnePacketGapDrop)
{
    // Add one packet message to to the buffer
    // with a gap of one packet.
    EXPECT_EQ(addMessage(1, CSeqNo::incseq(m_init_seqno)), 0);
    auto& rcv_buffer = *m_rcv_buffer.get();
    EXPECT_FALSE(hasAvailablePackets());
    EXPECT_FALSE(rcv_buffer.isRcvDataReady());
    rcv_buffer.dropUpTo(CSeqNo::incseq(m_init_seqno));

    EXPECT_TRUE(hasAvailablePackets());
    EXPECT_TRUE(rcv_buffer.isRcvDataReady());
    array<char, m_payload_sz> buff;
    EXPECT_TRUE(readMessage(buff.data(), buff.size()) == m_payload_sz);
    EXPECT_TRUE(verifyPayload(buff.data(), m_payload_sz, CSeqNo::incseq(m_init_seqno)));
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// Add one packet to the buffer and read it once it is acknowledged.
// Confirm the data read is valid.
// Don't allow to add packet with the same sequence number.
TEST_F(CRcvBufferReadMsg, OnePacket)
{
    const size_t msg_pkts = 1;
    // Adding one message  without acknowledging
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), 0);
    // Adding a packet into the same position must return an error.
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), -1);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;

    // The receiver buffer allows reading without ACK.
    EXPECT_TRUE(hasAvailablePackets());

    const int res2 = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res2, msg_bytelen);
    EXPECT_TRUE(verifyPayload(buff.data(), res2, m_init_seqno));
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// Add ten packets to the buffer, acknowledge and read some of them.
// Then try to add packets to the position of existing packets.
// We can't check adding to the position of those packets already read,
// because a negative offset is not checked by the receiver buffer,
// but must be handled by the CUDT socket.
TEST_F(CRcvBufferReadMsg, AddData)
{
    const int num_pkts = 10;
    ASSERT_LT(num_pkts, m_buff_size_pkts);
    for (int i = 0; i < num_pkts; ++i)
    {
        EXPECT_EQ(addMessage(1, CSeqNo::incseq(m_init_seqno, i)), 0);
    }

    // The available buffer size remains the same
    // The value is reported by SRT receiver like this:
    // data[ACKD_BUFFERLEFT] = m_pRcvBuffer->getAvailBufSize();
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    // The receiver buffer does not need ACK to allow reading.
    EXPECT_TRUE(hasAvailablePackets());

    // Now acknowledge two packets
    const int ack_pkts = 2;
    ackPackets(2);
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1 - ack_pkts);
    EXPECT_TRUE(hasAvailablePackets());

    std::array<char, m_payload_sz> buff;
    for (int i = 0; i < ack_pkts; ++i)
    {
        const int res = readMessage(buff.data(), buff.size());
        EXPECT_TRUE(size_t(res) == m_payload_sz);
        EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - ack_pkts + i);
        EXPECT_TRUE(verifyPayload(buff.data(), res, CSeqNo::incseq(m_init_seqno, i)));
    }

    // Add packet to the position of oackets already read.
    EXPECT_EQ(addPacket(m_init_seqno), -2);

    // Add packet to a non-empty position.
    EXPECT_EQ(addPacket(CSeqNo::incseq(m_init_seqno, ack_pkts)), -1);

    const int num_pkts_left = num_pkts - ack_pkts;
    ackPackets(num_pkts_left);
    for (int i = 0; i < num_pkts_left; ++i)
    {
        const int res = readMessage(buff.data(), buff.size());
        EXPECT_TRUE(size_t(res) == m_payload_sz);
        EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - num_pkts_left + i);
        EXPECT_TRUE(verifyPayload(buff.data(), res, CSeqNo::incseq(m_init_seqno, ack_pkts + i)));
    }
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// Check reading the whole message (consisting of several packets) from the buffer.
TEST_F(CRcvBufferReadMsg, MsgAcked)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;

    // Acknowledge all packets of the message.
    ackPackets(msg_pkts);
    // Now the whole message can be read.
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());

    const int res = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        const ptrdiff_t offset = i * m_payload_sz;
        EXPECT_TRUE(verifyPayload(buff.data() + offset, m_payload_sz, CSeqNo::incseq(m_init_seqno, i)));
    }
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// Check reading the whole message (consisting of several packets) into
// a buffer of an insufficient size.
TEST_F(CRcvBufferReadMsg, SmallReadBuffer)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;

    // Acknowledge all packets of the message.
    ackPackets(msg_pkts);
    // Now the whole message can be read.
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());

    // Check reading into an insufficient size buffer.
    // The old buffer extracts the whole message, but copies only
    // the number of bytes provided in the 'len' argument.
    const int res = readMessage(buff.data(), m_payload_sz);
    EXPECT_EQ(res, 1456);

    // No more messages to read
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_FALSE(hasAvailablePackets());
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// BUG!!!
// Checks signaling of read-readiness of a half-acknowledged message.
// The RCV buffer implementation has an issue here: when only half of the message is
// acknowledged, the RCV buffer signals read-readiness, even though
// the message can't be read, and reading returns 0.
TEST_F(CRcvBufferReadMsg, MsgHalfAck)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);
    
    // Nothing to read (0 for zero bytes read).
    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;

    // The receiver buffer does not care about ACK.
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());

    const int res = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        const ptrdiff_t offset = i * m_payload_sz;
        EXPECT_TRUE(verifyPayload(buff.data() + offset, m_payload_sz, CSeqNo::incseq(m_init_seqno, i)));
    }
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// BUG!!!
// Adding a message with the out-of-order flag set.
// RCV buffer does not signal read-readiness, but actually the packet can be read.
TEST_F(CRcvBufferReadMsg, OutOfOrderMsgNoACK)
{
    const size_t msg_pkts = 4;
    // Adding one message with the Out-Of-Order flag set, but without acknowledging.
    addMessage(msg_pkts, m_init_seqno, true);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;
    const int res = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        const ptrdiff_t offset = i * m_payload_sz;
        EXPECT_TRUE(verifyPayload(buff.data() + offset, m_payload_sz, CSeqNo::incseq(m_init_seqno, i)));
    }

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_FALSE(hasAvailablePackets());

    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// Adding a message with the out-of-order flag set.
// The message can be read.
TEST_F(CRcvBufferReadMsg, OutOfOrderMsgGap)
{
    const size_t msg_pkts = 4;
    // Adding one message with the Out-Of-Order flag set, but without acknowledging.
    addMessage(msg_pkts, CSeqNo::incseq(m_init_seqno, 1), true);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;
    const int res = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        const ptrdiff_t offset = i * m_payload_sz;
        EXPECT_TRUE(verifyPayload(buff.data() + offset, m_payload_sz, CSeqNo::incseq(m_init_seqno, 1 + i)));
    }

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_FALSE(hasAvailablePackets());
    // Adding one message with the Out-Of-Order flag set, but without acknowledging.
    //int seqno, bool pb_first = true, bool pb_last = true, bool out_of_order = false, int ts = 0)
    const int res2 = addPacket(CSeqNo::incseq(m_init_seqno, 1));
    EXPECT_EQ(res2, -1); // already exists

    EXPECT_EQ(addPacket(m_init_seqno), 0);
    ackPackets(msg_pkts + 1);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());

    const int res3 = readMessage(buff.data(), buff.size());
    EXPECT_TRUE(res3 == m_payload_sz);
    EXPECT_TRUE(verifyPayload(buff.data(), m_payload_sz, m_init_seqno));

    // Only "passack" or EntryState_Read packets remain in the buffer.
    // They are falsely signalled as read-ready.
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_FALSE(hasAvailablePackets());

    // Adding a packet right after the EntryState_Read packets.
    const int seqno = CSeqNo::incseq(m_init_seqno, msg_pkts + 1);
    EXPECT_EQ(addPacket(seqno), 0);
    ackPackets(1);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());
    EXPECT_TRUE(readMessage(buff.data(), buff.size()) == m_payload_sz);
    EXPECT_TRUE(verifyPayload(buff.data(), m_payload_sz, seqno));
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_FALSE(hasAvailablePackets());
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// One message (4 packets) are added to the buffer.
// Check if reading is only possible once the whole message is present in the buffer.
TEST_F(CRcvBufferReadMsg, LongMsgReadReady)
{
    const size_t msg_pkts = 4;
    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        // int addPacket(int seqno, bool pb_first = true, bool pb_last = true, bool out_of_order = false, int ts = 0)
        const bool pb_first = (i == 0);
        const bool pb_last  = (i == (msg_pkts - 1));
        EXPECT_EQ(addPacket(CSeqNo::incseq(m_init_seqno, i), pb_first, pb_last), 0);
        ackPackets(1);
        if (!pb_last)
        {
            EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
            EXPECT_FALSE(hasAvailablePackets());
            EXPECT_EQ(readMessage(buff.data(), buff.size()), 0);
        }
    }

    // Read the whole message.
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(hasAvailablePackets());

    const int res = readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        const ptrdiff_t offset = i * m_payload_sz;
        EXPECT_TRUE(verifyPayload(buff.data() + offset, m_payload_sz, CSeqNo::incseq(m_init_seqno, i)));
    }
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// One message (4 packets) is added to the buffer. Can be read out of order.
// Reading should be possible even before the missing packet is dropped.
TEST_F(CRcvBufferReadMsg, MsgOutOfOrderDrop)
{
    const size_t msg_pkts = 4;
    // 1. Add one message (4 packets) without acknowledging
    const int msg_seqno = m_init_seqno + 1; // seqno of the first packet in the message
    EXPECT_EQ(addMessage(msg_pkts, msg_seqno, true), 0);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());

    // 2. Read full message after gap.
    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;
    int res = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    for (size_t i = 0; i < msg_pkts; ++i)
    {
        EXPECT_TRUE(verifyPayload(buff.data() + i * m_payload_sz, m_payload_sz, msg_seqno + i));
    }

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());

    // Can't add to the same message
    EXPECT_EQ(addMessage(msg_pkts, msg_seqno, true), -1);

    const auto pkt_info = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(pkt_info.seqno, -1); // Nothing to read
    EXPECT_TRUE(srt::sync::is_zero(pkt_info.tsbpd_time));

    // Drop missing packet
    m_rcv_buffer->dropUpTo(msg_seqno);
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    // All memory units are expected to be freed.
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}

// One message (4 packets) is added to the buffer after a message with "in order" flag.
// Read in order
TEST_F(CRcvBufferReadMsg, MsgOutOfOrderAfterInOrder)
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

/// One packet is added to the buffer. Can be read on TSBPD-readiness.
///
/// 1. insert
///   | 
/// +---+  ---+---+---+---+---+   +---+
/// | 1 |   0 | 0 | 0 | 0 | 0 |...| 0 | m_pUnit[]
/// +---+  ---+---+---+---+---+   +---+
///   |
/// 2. read
///
TEST_F(CRcvBufferReadMsg, OnePacketTSBPD)
{
    const size_t msg_pkts = 1;

    m_rcv_buffer->setTsbPdMode(m_tsbpd_base, false, m_delay);

    const int packet_ts = 0;
    // Adding one message. Note that all packets have the out of order flag
    // set to false by default in TSBPD mode, but this flag is ignored.
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, true, packet_ts), 0);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;

    // Confirm adding to the same location returns an error.
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, true, packet_ts), -1);

    // There is one packet in the buffer, but not ready to read after delay/2
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + (m_delay / 2)));
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay - sync::microseconds_from(1)));
    // There is one packet in the buffer ready to read after delay
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay));
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay + sync::microseconds_from(1)));

    // Read out the first message
    const int read_len = m_rcv_buffer->readMessage(buff.data(), buff.size());
    EXPECT_EQ(read_len, msg_bytelen);
    EXPECT_TRUE(verifyPayload(buff.data(), read_len, m_init_seqno));

    // Check the state after a packet was read
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay));
    EXPECT_EQ(addMessage(msg_pkts, m_init_seqno, false), -2);

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(m_tsbpd_base + m_delay));
}

/// TSBPD = ON, a ready to play packet is preceeded by a missing packet.
/// The read-rediness must be signalled, and a packet must be read after the missing
/// one is dropped.
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
TEST_F(CRcvBufferReadMsg, TSBPDGapBeforeValid)
{
    m_rcv_buffer->setTsbPdMode(m_tsbpd_base, false, m_delay);
    // Add a solo packet to position m_init_seqno + 1 with timestamp 200 us
    const int seqno = m_init_seqno + 1;
    const int32_t pkt_ts = 200;
    EXPECT_EQ(addMessage(1, seqno, false, pkt_ts), 0);

    const auto readready_timestamp = m_tsbpd_base + sync::microseconds_from(pkt_ts) + m_delay;
    // Check that getFirstValidPacketInfo() returns first valid packet.
    const auto pkt_info = m_rcv_buffer->getFirstValidPacketInfo();
    EXPECT_EQ(pkt_info.tsbpd_time, readready_timestamp);
    EXPECT_EQ(pkt_info.seqno, seqno);
    EXPECT_TRUE(pkt_info.seq_gap);

    // The packet can't be read because there is a missing packet preceeding.
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady(readready_timestamp));

    const int seq_gap_len = CSeqNo::seqoff(m_rcv_buffer->getStartSeqNo(), pkt_info.seqno);
    EXPECT_GT(seq_gap_len, 0);
    if (seq_gap_len > 0)
    {
        m_rcv_buffer->dropUpTo(pkt_info.seqno);
    }

    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady(readready_timestamp));

    const size_t msg_bytelen = m_payload_sz;
    array<char, 2 * msg_bytelen> buff;
    EXPECT_EQ(readMessage(buff.data(), buff.size()), msg_bytelen);
    EXPECT_TRUE(verifyPayload(buff.data(), m_payload_sz, seqno));
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}


class CRcvBufferReadStream
    : public CRcvBufferReadMsg
{
protected:
    CRcvBufferReadStream()
        : CRcvBufferReadMsg(false)
    {}

    virtual ~CRcvBufferReadStream() { }
};


// Add ten packets to the buffer in stream mode, read some of them.
// Try to add packets to occupied positions.
TEST_F(CRcvBufferReadStream, ReadSinglePackets)
{
    const int num_pkts = 10;
    ASSERT_LT(num_pkts, m_buff_size_pkts);
    for (int i = 0; i < num_pkts; ++i)
    {
        EXPECT_EQ(addPacket(CSeqNo::incseq(m_init_seqno, i), false, false), 0);
    }

    // The available buffer size remains the same
    // The value is reported by SRT receiver like this:
    // data[ACKD_BUFFERLEFT] = m_pRcvBuffer->getAvailBufSize();
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    EXPECT_TRUE(hasAvailablePackets());

    // Now acknowledge two packets
    const int ack_pkts = 2;
    ackPackets(2);
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1 - ack_pkts);
    EXPECT_TRUE(hasAvailablePackets());

    std::array<char, m_payload_sz> buff;
    for (int i = 0; i < ack_pkts; ++i)
    {
        const size_t res = m_rcv_buffer->readBuffer(buff.data(), buff.size());
        EXPECT_TRUE(size_t(res) == m_payload_sz);
        EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - ack_pkts + i);
        EXPECT_TRUE(verifyPayload(buff.data(), res, CSeqNo::incseq(m_init_seqno, i)));
    }

    // Add packet to the position of oackets already read.
    // Can't check the old buffer, as it does not handle a negative offset.
    EXPECT_EQ(addPacket(m_init_seqno), -2);

    // Add packet to a non-empty position.
    EXPECT_EQ(addPacket(CSeqNo::incseq(m_init_seqno, ack_pkts)), -1);

    const int num_pkts_left = num_pkts - ack_pkts;
    ackPackets(num_pkts_left);
    for (int i = 0; i < num_pkts_left; ++i)
    {
        const int res = m_rcv_buffer->readBuffer(buff.data(), buff.size());
        EXPECT_TRUE(size_t(res) == m_payload_sz);
        EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - num_pkts_left + i);
        EXPECT_TRUE(verifyPayload(buff.data(), res, CSeqNo::incseq(m_init_seqno, ack_pkts + i)));
    }
    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}


// Add packets to the buffer in stream mode. Read fractional number of packets
// to confirm a partially read packet stays in the buffer and is read properly afterwards.
TEST_F(CRcvBufferReadStream, ReadFractional)
{
    const int num_pkts = 10;
    ASSERT_LT(num_pkts, m_buff_size_pkts);
    for (int i = 0; i < num_pkts; ++i)
    {
        EXPECT_EQ(addPacket(CSeqNo::incseq(m_init_seqno, i), false, false), 0);
    }

    // The available buffer size remains the same
    // The value is reported by SRT receiver like this:
    // data[ACKD_BUFFERLEFT] = m_pRcvBuffer->getAvailBufSize();
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    EXPECT_TRUE(hasAvailablePackets());

    array<char, m_payload_sz * num_pkts> buff;

    const size_t nfull_pkts = 2;
    const size_t num_bytes1 = nfull_pkts * m_payload_sz + m_payload_sz / 2;
    const int res1 = m_rcv_buffer->readBuffer(buff.data(), num_bytes1);
    EXPECT_TRUE(size_t(res1) == num_bytes1);
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    EXPECT_TRUE(hasAvailablePackets());

    const size_t num_bytes2 = m_payload_sz * (num_pkts - nfull_pkts - 1) + m_payload_sz / 2;

    const int res2 = m_rcv_buffer->readBuffer(buff.data() + num_bytes1, buff.size() - num_bytes1);
    EXPECT_TRUE(size_t(res2) == num_bytes2);
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    EXPECT_FALSE(hasAvailablePackets());
    ackPackets(num_pkts); // Move the reference ACK position.
    EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);

    for (int i = 0; i < num_pkts; ++i)
    {
        EXPECT_TRUE(verifyPayload(buff.data() + i * m_payload_sz, m_payload_sz, CSeqNo::incseq(m_init_seqno, i))) << "i = " << i;
    }

    EXPECT_EQ(m_unit_queue->size(), m_unit_queue->capacity());
}
