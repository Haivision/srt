#include "gtest/gtest.h"
#include <array>
#include <numeric>

#if 1 //ENABLE_NEW_RCVBUFFER

#include "buffer_rcv.h"
using namespace srt;

TEST(CRcvBufferNew, Create)
{
    const int buffer_size = 65536;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size, 1500, AF_INET);
    const int init_seqno = 0;
    CRcvBufferNew rcv_buffer(init_seqno, buffer_size, &unit_queue, true);

    EXPECT_EQ(rcv_buffer.getAvailSize(init_seqno), buffer_size - 1);   // logic
}

TEST(CRcvBufferNew, FullBuffer)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1234;
    CRcvBufferNew rcv_buffer(initial_seqno, buffer_size_pkts, &unit_queue, true);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < (int) rcv_buffer.capacity(); ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(payload_size);
        unit->m_Packet.m_iSeqNo = initial_seqno + i;
        unit->m_Packet.m_iMsgNo = PacketBoundaryBits(PB_SOLO);
        unit->m_Packet.m_iMsgNo |= MSGNO_PACKET_INORDER::wrap(1);
        EXPECT_TRUE(unit->m_Packet.getMsgOrderFlag());
        EXPECT_EQ(rcv_buffer.insert(unit), 0);
    }

    EXPECT_EQ(rcv_buffer.getAvailSize(initial_seqno), buffer_size_pkts - 1);

    // Try to add a unit with sequence number already existing in the buffer.
    CUnit* unit = unit_queue.getNextAvailUnit();
    EXPECT_NE(unit, nullptr);
    unit->m_Packet.setLength(payload_size);
    unit->m_Packet.m_iSeqNo = initial_seqno;

    EXPECT_EQ(rcv_buffer.insert(unit), -1);

    // Check available size if all packets were acknowledged.
    EXPECT_EQ(rcv_buffer.getAvailSize(initial_seqno + buffer_size_pkts - 1), 0);

    // Try to add a unit with a sequence number that was already acknowledged.
    unit->m_Packet.setLength(payload_size);
    unit->m_Packet.m_iSeqNo = initial_seqno;
    EXPECT_EQ(rcv_buffer.insert(unit), -1);

    // Try to add data past the available size of the buffer
    unit->m_Packet.m_iSeqNo = initial_seqno + buffer_size_pkts;
    EXPECT_EQ(rcv_buffer.insert(unit), -3);

    std::array<char, 2 * payload_size> buff;
    for (int i = 0; i < buffer_size_pkts - 1; ++i)
    {
        EXPECT_TRUE(rcv_buffer.isRcvDataReady());
    	const int res = rcv_buffer.readMessage(buff.data(), buff.size());
    	EXPECT_EQ(res, payload_size);
    }
}

/// In this test case two packets are inserted in the CRcvBufferNew,
/// but with a gap in sequence numbers. The test checks that this situation
/// is handled properly.
TEST(CRcvBufferNew, HandleSeqGap)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1234;
    CRcvBufferNew rcv_buffer(initial_seqno, buffer_size_pkts, &unit_queue, true);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < 2; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(payload_size);
        unit->m_Packet.m_iSeqNo = initial_seqno + i;
        EXPECT_EQ(rcv_buffer.insert(unit), 0);
    }

    EXPECT_EQ(rcv_buffer.getAvailSize(initial_seqno), buffer_size_pkts - 1);
}

/// In this test case several units are inserted in the CRcvBuffer
/// but all composing a one message. In details, each packet [  ]
/// [PB_FIRST] [PB_SUBSEQUENT] [PB_SUBSEQUENsT] [PB_LAST]
///
TEST(CRcvBufferNew, OneMessageInSeveralPackets)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1000;
    CRcvBufferNew rcv_buffer(initial_seqno, buffer_size_pkts, &unit_queue, true);

    const size_t payload_size = 1456;
    const int message_len_in_pkts = 4;
    const size_t buf_length = payload_size * message_len_in_pkts;
    std::array<char, buf_length> src_buffer;
    std::iota(src_buffer.begin(), src_buffer.end(), (char)0);

    for (int i = 0; i < message_len_in_pkts; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_iFlag = CUnit::GOOD;
        CPacket& packet = unit->m_Packet;
        packet.setLength(payload_size);
        packet.m_iSeqNo = initial_seqno + i;

        packet.m_iMsgNo = PacketBoundaryBits(PB_SUBSEQUENT);
        if (i == 0)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_FIRST);
        const bool is_last_packet = (i == message_len_in_pkts - 1);
        if (is_last_packet)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_LAST);

        memcpy(packet.m_pcData, src_buffer.data() + i * payload_size, payload_size);

        EXPECT_EQ(rcv_buffer.insert(unit), 0);
        EXPECT_EQ(rcv_buffer.isRcvDataReady(), is_last_packet);
        //EXPECT_EQ(rcv_buffer.countReadable(), is_last_packet ? message_len_in_pkts : 0);
    }

    // Read the whole message from the buffer
    std::array<char, buf_length> read_buffer;
    const int read_len = rcv_buffer.readMessage(read_buffer.data(), buf_length);
    EXPECT_EQ(read_len, payload_size * message_len_in_pkts);
    EXPECT_TRUE(read_buffer == src_buffer);
    EXPECT_FALSE(rcv_buffer.isRcvDataReady());
}


/// In this test case several units are inserted in the CRcvBuffer.
/// The first message is not full, but the second message is ready to be extracted.
///
TEST(CRcvBufferNew, MessageOutOfOrder)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1000;
    CRcvBufferNew rcv_buffer(initial_seqno, buffer_size_pkts, &unit_queue, true);

    const size_t payload_size = 1456;
    const int message_len_in_pkts = 4;
    const size_t buf_length = payload_size * message_len_in_pkts;
    std::array<char, buf_length> src_buffer;
    std::iota(src_buffer.begin(), src_buffer.end(), (char)0);

    for (int i = 0; i < message_len_in_pkts; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_iFlag = CUnit::GOOD;
        CPacket& packet = unit->m_Packet;
        packet.setLength(payload_size);
        packet.m_iSeqNo = initial_seqno + message_len_in_pkts + i;
        
        packet.m_iMsgNo = PacketBoundaryBits(PB_SUBSEQUENT);
        if (i == 0)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_FIRST);
        const bool is_last_packet = (i == message_len_in_pkts - 1);
        if (is_last_packet)
            packet.m_iMsgNo |= PacketBoundaryBits(PB_LAST);
        EXPECT_FALSE(packet.getMsgOrderFlag());

        memcpy(packet.m_pcData, src_buffer.data() + i * payload_size, payload_size);

        EXPECT_EQ(rcv_buffer.insert(unit), 0);

        // Due to out of order flag we should be able to read the message even with a missing preceeding message.
        EXPECT_EQ(rcv_buffer.isRcvDataReady(), is_last_packet);
        //EXPECT_EQ(rcv_buffer.countReadable(), is_last_packet ? message_len_in_pkts : 0);
    }

    // Read the whole message from the buffer
    std::array<char, buf_length> read_buffer;
    const int read_len = rcv_buffer.readMessage(read_buffer.data(), buf_length);
    EXPECT_EQ(read_len, payload_size * message_len_in_pkts);
    EXPECT_TRUE(read_buffer == src_buffer);
    EXPECT_FALSE(rcv_buffer.isRcvDataReady());
}


TEST(CRcvBufferNew, GetFirstValidPacket)
{
    const int buffer_size_pkts = 16;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);
    const int initial_seqno = 1234;
    CRcvBufferNew rcv_buffer(initial_seqno, buffer_size_pkts, &unit_queue, true);

    const size_t payload_size = 1456;
    // Add a number of units (packets) to the buffer
    // equal to the buffer size in packets
    for (int i = 0; i < 2; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        EXPECT_NE(unit, nullptr);
        unit->m_Packet.setLength(payload_size);
        unit->m_Packet.m_iSeqNo = initial_seqno + i;
        EXPECT_EQ(rcv_buffer.insert(unit), 0);
    }

    EXPECT_EQ(rcv_buffer.getAvailSize(initial_seqno), buffer_size_pkts - 1);
}

#endif // ENABLE_NEW_RCVBUFFER
