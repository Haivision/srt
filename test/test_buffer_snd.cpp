#include <climits>
#include <vector>
#include "gtest/gtest.h"
#include "buffer_snd.h"
#include "packet.h"

using namespace srt;

// Tests for the robustness of the sender buffer against rogue
// ACK offsets and sequence numbers (see issue #2836).
class CSndBufferRogue : public ::testing::Test
{
protected:
    enum { ISN = 1000, PAYLOAD = 1456 };

    CSndBufferRogue()
        : m_buf(AF_INET, 32, PAYLOAD + 16, 0)
    {
    }

    void add(int npackets)
    {
        std::vector<char> data(PAYLOAD, 'x');
        for (int i = 0; i < npackets; ++i)
        {
            SRT_MSGCTRL mc = srt_msgctrl_default;
            mc.pktseq      = m_next_seq;
            m_buf.addBuffer(data.data(), (int)data.size(), mc);
            m_next_seq = mc.pktseq;
        }
    }

    // Reads the next not yet sent packet. Returns its length.
    int readNext(int32_t& w_seq)
    {
        CPacket pkt;
        std::vector<char> storage(PAYLOAD + 16);
        pkt.m_pcData = storage.data();
        sync::steady_clock::time_point t;
        int seqinc = 1;
        const int len = m_buf.readData((pkt), (t), 0, (seqinc));
        w_seq = pkt.seqno();
        return len;
    }

    CSndBuffer m_buf;
    int32_t m_next_seq = ISN;
};

TEST_F(CSndBufferRogue, AckBeyondContent)
{
    add(5);
    EXPECT_EQ(m_buf.getCurrBufSize(), 5);

    EXPECT_EQ(m_buf.ackData(1000000), 5);
    EXPECT_EQ(m_buf.getCurrBufSize(), 0);

    // The buffer must still be usable afterwards.
    add(2);
    int32_t seq = 0;
    EXPECT_EQ(readNext((seq)), PAYLOAD);
    EXPECT_EQ(seq, m_next_seq - 2);
    EXPECT_EQ(readNext((seq)), PAYLOAD);
}

TEST_F(CSndBufferRogue, AckNegativeOffset)
{
    add(3);
    EXPECT_EQ(m_buf.ackData(-5), 0);
    EXPECT_EQ(m_buf.ackData(INT_MIN), 0);
    EXPECT_EQ(m_buf.getCurrBufSize(), 3);
}

TEST_F(CSndBufferRogue, AckReturnsRemoved)
{
    add(5);
    EXPECT_EQ(m_buf.ackData(2), 2);
    EXPECT_EQ(m_buf.getCurrBufSize(), 3);
    EXPECT_EQ(m_buf.ackData(0), 0);
}

TEST_F(CSndBufferRogue, TrimBefore)
{
    add(5); // ISN .. ISN+4
    EXPECT_FALSE(m_buf.trimBefore(ISN + 100));
    EXPECT_EQ(m_buf.getCurrBufSize(), 5);

    EXPECT_TRUE(m_buf.trimBefore(ISN + 2));
    EXPECT_EQ(m_buf.getCurrBufSize(), 3);

    EXPECT_TRUE(m_buf.trimBefore(ISN + 2)); // already first: no-op
    EXPECT_EQ(m_buf.getCurrBufSize(), 3);
}

TEST_F(CSndBufferRogue, RexmitTimeOutOfRange)
{
    add(3);
    EXPECT_EQ(m_buf.getPacketRexmitTime(-1), sync::steady_clock::time_point());
    EXPECT_EQ(m_buf.getPacketRexmitTime(3), sync::steady_clock::time_point());
    EXPECT_EQ(m_buf.getPacketRexmitTime(1000000), sync::steady_clock::time_point());
}

TEST_F(CSndBufferRogue, ReadByOffsetRejectsBadOffsets)
{
    add(3);
    CPacket pkt;
    std::vector<char> storage(PAYLOAD + 16);
    pkt.m_pcData = storage.data();
    pkt.set_seqno(ISN);
    sync::steady_clock::time_point t;
    CSndBuffer::DropRange drop;

    const int none = CSndBuffer::READ_NONE;
    EXPECT_EQ(m_buf.readData(-1, (pkt), (t), (drop)), none);
    EXPECT_EQ(m_buf.readData(100, (pkt), (t), (drop)), none);
}
