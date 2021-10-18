#include <array>
#include <numeric>
#include "gtest/gtest.h"
#include "buffer.h"

using namespace srt;
using namespace std;

class CRcvBufferReadMsg
    : public ::testing::Test
{
protected:
    CRcvBufferReadMsg()
    {
        // initialization code here
    }

    ~CRcvBufferReadMsg()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        // make_unique is unfortunatelly C++14
        m_unit_queue = unique_ptr<CUnitQueue>(new CUnitQueue);
        ASSERT_NE(m_unit_queue.get(), nullptr);
        m_unit_queue->init(m_buff_size_pkts, 1500, AF_INET);
        m_rcv_buffer = unique_ptr<CRcvBuffer>(new CRcvBuffer(m_unit_queue.get(), m_buff_size_pkts));
        ASSERT_NE(m_rcv_buffer.get(), nullptr);
    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_unit_queue.reset();
        m_rcv_buffer.reset();
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

        const int offset = CSeqNo::seqoff(m_first_unack_seqno, seqno);
        return m_rcv_buffer->addData(unit, offset);
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
        return m_rcv_buffer->ackData(num_pkts);
    }

protected:
    unique_ptr<CUnitQueue> m_unit_queue;
    unique_ptr<CRcvBuffer> m_rcv_buffer;
    const int m_buff_size_pkts = 16;
    const int m_init_seqno = 1000;
    int m_first_unack_seqno = m_init_seqno;
    static const size_t m_payload_sz = 1456;
};

// Check the available size of the receiver buffer.
TEST_F(CRcvBufferReadMsg, Create)
{
    EXPECT_EQ(m_rcv_buffer->getAvailBufSize(), m_buff_size_pkts - 1);
}

// Fill the buffer full, and check adding more data results in an error.
TEST_F(CRcvBufferReadMsg, FullBuffer)
{
    CRcvBuffer& rcv_buffer = *m_rcv_buffer.get();
    CUnitQueue& unit_queue = *m_unit_queue.get();
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < rcv_buffer.getAvailBufSize(); ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(m_payload_sz);
        EXPECT_EQ(rcv_buffer.addData(unit, i), 0);
    }

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), m_buff_size_pkts - 1);   // logic

    ackPackets(m_buff_size_pkts - 1);
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), 0);

    // Try to add more data than the available size of the buffer
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    EXPECT_EQ(rcv_buffer.addData(unit, 1), -1);

    array<char, m_payload_sz> buff;
    for (int i = 0; i < m_buff_size_pkts - 1; ++i)
    {
        const int res = rcv_buffer.readBuffer(buff.data(), buff.size());
        EXPECT_TRUE(size_t(res) == m_payload_sz);
    }
}

// BUG!!!
// In this test case a packet is added to receiver buffer with offset 1,
// thus leaving offset 0 with an empty pointer.
// The buffer says it is not empty, and the data is available
// to be read, but reading is not possible.
TEST_F(CRcvBufferReadMsg, OnePacketGap)
{
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    EXPECT_EQ(addMessage(1, CSeqNo::incseq(m_init_seqno)), 0);

    CRcvBuffer& rcv_buffer = *m_rcv_buffer.get();
    // Before ACK the available buffer size stays the same.
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), m_buff_size_pkts - 1);
    // Not available for reading as not yet acknowledged.
    EXPECT_FALSE(rcv_buffer.isRcvDataAvailable());
    // Confirm reading zero bytes.
    array<char, m_payload_sz> buff;
    int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // BUG. Acknowledging an empty position must not result in a read-readiness.
    ackPackets(1);
    EXPECT_TRUE(rcv_buffer.isRcvDataAvailable());
    EXPECT_TRUE(rcv_buffer.isRcvDataReady());

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), m_buff_size_pkts - 2);
    cerr << "Expecting IPE from readBuffer(..): \n";
    res = rcv_buffer.readBuffer(buff.data(), buff.size());
    EXPECT_EQ(res, -1);

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);
}

// Add one packet to the buffer and read it once it is acknowledged.
// Confirm the data read is valid.
TEST_F(CRcvBufferReadMsg, OnePacket)
{
    const size_t msg_pkts = 1;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;

    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());

    int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // Full ACK
    ackPackets(msg_pkts);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
    EXPECT_TRUE(verifyPayload(buff.data(), res, m_init_seqno));
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
    CRcvBuffer& rcv_buffer = *m_rcv_buffer.get();
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), m_buff_size_pkts - 1);
    EXPECT_FALSE(rcv_buffer.isRcvDataAvailable());

    // Now acknowledge two packets
    const int ack_pkts = 2;
    ackPackets(2);
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), m_buff_size_pkts - 1 - ack_pkts);
    EXPECT_TRUE(rcv_buffer.isRcvDataAvailable());

    std::array<char, m_payload_sz> buff;
    for (int i = 0; i < ack_pkts; ++i)
    {
        const int res = rcv_buffer.readMsg(buff.data(), buff.size());
        EXPECT_TRUE(size_t(res) == m_payload_sz);
        EXPECT_EQ(rcv_buffer.getAvailBufSize(), m_buff_size_pkts - ack_pkts + i);
        EXPECT_TRUE(verifyPayload(buff.data(), res, CSeqNo::incseq(m_init_seqno, i)));
    }

    // Add packet to the position of oackets already read.
    // Can't check, as negative offset is an error not handled by the receiver buffer.
    //EXPECT_EQ(addPacket(m_init_seqno), -1);

    // Add packet to a non-empty position.
    EXPECT_EQ(addPacket(CSeqNo::incseq(m_init_seqno, ack_pkts)), -1);
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
    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());
    const int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
}

// BUG!!!
// Checks signalling of read-readiness of a half-acknowledged message.
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
    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());
    int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // ACK half of the message and check read-readiness.
    m_rcv_buffer->ackData(2);
    // FIXME: Sadly RCV buffer says the data is ready to be read.
    // EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    // EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());
    EXPECT_TRUE(m_rcv_buffer->isRcvDataReady());
    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());

    // Actually must be nothing to read (can't read half a message).
    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);
}

// BUG!!!
// Adding a message with the out-of-order flag set.
// RCV buffer does not signal read-readiness, but actually the packet can be read.
TEST_F(CRcvBufferReadMsg, OutOfOrderMsgNoACK)
{
    const size_t msg_pkts = 4;
    // Adding one message with the Out-Of-Order flag set, but without acknowledging.
    addMessage(msg_pkts, m_init_seqno, true);

    EXPECT_FALSE(m_rcv_buffer->isRcvDataReady());
    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());
    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    array<char, 2 * msg_bytelen> buff;
    const int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
}
