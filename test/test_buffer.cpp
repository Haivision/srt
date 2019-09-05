#include <array>
#include "gtest/gtest.h"
#include "buffer.h"


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
        EXPECT_EQ(res, payload_size);
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
    EXPECT_EQ(res, payload_size);
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
        EXPECT_EQ(res, payload_size);
        EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size_pkts - ack_pkts + i);
    }

    // Add packet to the same position
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_Packet.setLength(payload_size);
    EXPECT_EQ(rcv_buffer.addData(unit, 1), -1);
}


