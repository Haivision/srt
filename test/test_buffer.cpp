#include <array>
#include "gtest/gtest.h"
#include "buffer.h"
#include <array>

using namespace srt;

#ifndef ENABLE_NEW_RCVBUFFER

TEST(CRcvBuffer, Create)
{
    const int buffer_size_pkts = 128;
    CUnitQueue unit_queue;
    CRcvBuffer rcv_buffer(&unit_queue, buffer_size_pkts);

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);   // logic
}


TEST(CRcvBuffer, FullBuffer)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    CRcvBuffer rcv_buffer(&unit_queue, buffer_size_pkts);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < rcv_buffer.getAvailBufSize(); ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(payload_size);
        EXPECT_EQ(rcv_buffer.addData(unit, i), 0);
    }

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);   // logic

    rcv_buffer.ackData(buffer_size_pkts - 1);
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), 0);

    // Try to add more data than the available size of the buffer
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    EXPECT_EQ(rcv_buffer.addData(unit, 1), -1);

    std::array<char, payload_size> buff;
    for (int i = 0; i < buffer_size_pkts - 1; ++i)
    {
        const int res = rcv_buffer.readBuffer(buff.data(), buff.size());
        EXPECT_EQ(size_t(res), payload_size);
    }
}


// In this test case a packet is added to receiver buffer with offset 1,
// thus leaving offset 0 with an empty pointer.
// The buffer sais it is not empty, and the data is available
// to be read, but reading should cause error.
TEST(CRcvBuffer, ReadDataIPE)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    CRcvBuffer rcv_buffer(&unit_queue, buffer_size_pkts);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_Packet.setLength(payload_size);
    EXPECT_EQ(rcv_buffer.addData(unit, 1), 0);
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);

    EXPECT_FALSE(rcv_buffer.isRcvDataAvailable());
    rcv_buffer.ackData(1);
    EXPECT_TRUE(rcv_buffer.isRcvDataAvailable());

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 2);

    std::cerr << "Expecting IPE message: \n";
    std::array<char, payload_size> buff;
    const int res = rcv_buffer.readBuffer(buff.data(), buff.size());
    EXPECT_EQ(res, -1);
}


TEST(CRcvBuffer, ReadData)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    CRcvBuffer rcv_buffer(&unit_queue, buffer_size_pkts);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_Packet.setLength(payload_size);
    EXPECT_EQ(rcv_buffer.addData(unit, 0), 0);
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);

    EXPECT_FALSE(rcv_buffer.isRcvDataAvailable());
    rcv_buffer.ackData(1);
    EXPECT_TRUE(rcv_buffer.isRcvDataAvailable());

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 2);

    std::array<char, payload_size> buff;
    const int res = rcv_buffer.readBuffer(buff.data(), buff.size());
    EXPECT_EQ(size_t(res), payload_size);
}


TEST(CRcvBuffer, AddData)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    CRcvBuffer rcv_buffer(&unit_queue, buffer_size_pkts);

    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);   // logic

    const size_t payload_size = 1456;
    // Add 10 units (packets) to the buffer
    for (int i = 0; i < 10; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(payload_size);
        EXPECT_EQ(rcv_buffer.addData(unit, i), 0);
    }

    // The available buffer size remains the same
    // The value is reported by SRT receiver like this:
    // data[ACKD_BUFFERLEFT] = m_pRcvBuffer->getAvailBufSize();
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1);
    EXPECT_FALSE(rcv_buffer.isRcvDataAvailable());

    // Now acknowledge two packets
    const int ack_pkts = 2;
    rcv_buffer.ackData(2);
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - 1 - ack_pkts);
    EXPECT_TRUE(rcv_buffer.isRcvDataAvailable());

    std::array<char, payload_size> buff;
    for (int i = 0; i < ack_pkts; ++i)
    {
        const int res = rcv_buffer.readBuffer(buff.data(), buff.size());
        EXPECT_EQ(size_t(res), payload_size);
        EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - ack_pkts + i);
    }

    // Add packet to the same position
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_Packet.setLength(payload_size);
    EXPECT_EQ(rcv_buffer.addData(unit, 1), -1);
}



using namespace std;
TEST(CRcvBuffer, OneMessageInSeveralPackets)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    CRcvBuffer rcv_buffer(&unit_queue, buffer_size_pkts);

    const int initial_seqno = 1000;
    const int message_len_in_pkts = 4;
    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < message_len_in_pkts; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);

        CPacket& packet = unit->m_Packet;
        packet.setLength(payload_size);
        packet.m_iSeqNo = initial_seqno + i;
        packet.m_iMsgNo = PacketBoundaryBits(PB_SUBSEQUENT);
        if (i == 0)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_FIRST);
        const bool is_last_packet = (i == message_len_in_pkts - 1);
        if (is_last_packet)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_LAST);

        EXPECT_EQ(rcv_buffer.addData(unit, i), 0);
    }

    rcv_buffer.ackData(message_len_in_pkts);

    cout << "Buffer size before reading: " << rcv_buffer.getAvailBufSize() << endl;
    std::array<char, payload_size> buff;
    cout << "Reading one packet of the 4-packet message" << endl;
    const int res = rcv_buffer.readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, payload_size);
    cout << "Buffer size after reading: " << rcv_buffer.getAvailBufSize() << endl;
}



class TestRcvBufferRead
    : public ::testing::Test
{
protected:
    TestRcvBufferRead()
    {
        // initialization code here
    }

    ~TestRcvBufferRead()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:

    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        // make_unique is unfortunatelly C++14
        m_unit_queue = unique_ptr<CUnitQueue>(new CUnitQueue);
        m_unit_queue->init(m_buff_size_pkts, 1500, AF_INET);
        m_rcv_buffer = unique_ptr<CRcvBuffer>(new CRcvBuffer(m_unit_queue.get(), m_buff_size_pkts));
    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_unit_queue.reset();
        m_rcv_buffer.reset();
    }

public:

    void addMessage(size_t msg_len_pkts, int start_seqno, bool out_of_order = false)
    {
        const int msg_offset = start_seqno - m_init_seqno;

        for (size_t i = 0; i < msg_len_pkts; ++i)
        {
            CUnit* unit = m_unit_queue->getNextAvailUnit();
            EXPECT_NE(unit, nullptr);

            CPacket& packet = unit->m_Packet;
            packet.setLength(m_payload_sz);
            packet.m_iSeqNo = start_seqno + i;
            packet.m_iMsgNo = PacketBoundaryBits(PB_SUBSEQUENT);
            if (i == 0)
                packet.m_iMsgNo |= PacketBoundaryBits(PB_FIRST);
            const bool is_last_packet = (i == (msg_len_pkts - 1));
            if (is_last_packet)
                packet.m_iMsgNo |= PacketBoundaryBits(PB_LAST);

            if (!out_of_order)
            {
                packet.m_iMsgNo |= MSGNO_PACKET_INORDER::wrap(1);
                EXPECT_TRUE(packet.getMsgOrderFlag());
            }

            EXPECT_EQ(m_rcv_buffer->addData(unit, msg_offset + i), 0);
        }
    }

protected:

    unique_ptr<CUnitQueue> m_unit_queue;
    unique_ptr<CRcvBuffer> m_rcv_buffer;
    const int m_buff_size_pkts = 16;
    const int m_init_seqno = 1000;
    static const size_t m_payload_sz = 1456;
};


TEST_F(TestRcvBufferRead, OnePacket)
{
    const size_t msg_pkts = 1;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());

    int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // Full ACK
    m_rcv_buffer->ackData(msg_pkts);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
}


TEST_F(TestRcvBufferRead, OnePacketWithGap)
{
    const size_t msg_pkts = 1;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno + 1, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());

    int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // ACK first missing packet
    m_rcv_buffer->ackData(msg_pkts);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    m_rcv_buffer->ackData(msg_pkts);
    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
}


TEST_F(TestRcvBufferRead, MsgAcked)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());

    int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // Half ACK
    m_rcv_buffer->ackData(2);

    EXPECT_TRUE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);

    // Full ACK
    m_rcv_buffer->ackData(2);

    EXPECT_FALSE(m_rcv_buffer->isRcvDataAvailable());

    res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);
}


TEST_F(TestRcvBufferRead, MsgHalfAck)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    m_rcv_buffer->ackData(2);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    const int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, 0);
}


TEST_F(TestRcvBufferRead, OutOfOrderMsgNoACK)
{
    const size_t msg_pkts = 4;
    // Adding one message  without acknowledging
    addMessage(msg_pkts, m_init_seqno, false);

    const size_t msg_bytelen = msg_pkts * m_payload_sz;
    std::array<char, 2 * msg_bytelen> buff;

    const int res = m_rcv_buffer->readMsg(buff.data(), buff.size());
    EXPECT_EQ(res, msg_bytelen);
}

#endif // !ENABLE_NEW_RCVBUFFER
