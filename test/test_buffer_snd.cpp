
#include "gtest/gtest.h"
#include "buffer_snd.h"
#include <iostream>
#include <thread>
#include "ofmt.h"


using namespace std;
using namespace srt;
using namespace srt::sync;

class TestSndBuffer: public testing::Test
{
    using time_point = steady_clock::time_point;

public:

    hvu::ofmtrefstream sout {cout};

    unique_ptr<CSndBuffer> m_buffer;

    int32_t last_seqno;

    TestSndBuffer()
    {
        // initialization code here
    }

    virtual ~TestSndBuffer()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

    void addBuffer(const char* data, int len, int msgno, int ttl = -1)
    {
        SRT_MSGCTRL c = srt_msgctrl_default;
        c.pktseq = last_seqno;
        c.msgno = msgno;
        c.msgttl = ttl;

        m_buffer->addBuffer(data, len, (c));
        last_seqno = c.pktseq;
    }

    void revokeSeq(int32_t seqno)
    {
        m_buffer->revoke(seqno);
    }

    void scheduleRexmit(int32_t seqlo, int32_t seqhi, steady_clock::duration uptime = steady_clock::duration())
    {
        //time_point sched_at = steady_clock::now() + uptime;
        m_buffer->insertLoss(seqlo, seqhi, steady_clock::now() + uptime);
    }

    void cancelRexmit(int32_t seq)
    {
        m_buffer->cancelLostSeq(seq);
    }

    size_t readUnique(CPacket& pkt, int kflg)
    {
        int pktskipseqno = 0;
        time_point tsOrigin;

        int size = m_buffer->readData((pkt), (tsOrigin), kflg, (pktskipseqno));
        // Ignore intermediate data

        return size_t(size);
    }

    size_t readOld(int32_t seqno, CPacket& w_pkt, std::list<std::pair<int32_t, int32_t>>& w_dropseq)
    {
        for (;;)
        {
            time_point tsOrigin;

            CSndBuffer::DropRange drop;
            int size = m_buffer->readOldPacket(seqno, (w_pkt), (tsOrigin), (drop));

            if (size == CSndBuffer::READ_DROP)
            {
                w_dropseq.emplace_back(drop.seqno[0], drop.seqno[1]);
                continue;
            }

            // 0 and >0 are handled common way
            return size;
        }
    }

    int32_t popLoss()
    {
        CSndBuffer::DropRange drop;
        return m_buffer->popLostSeq((drop));
    }

    int lossLength()
    {
        return m_buffer->getLossLength();
    }

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        // make_unique is unfortunatelly C++14
        m_buffer.reset(new CSndBuffer(32*1024, 1024, 1500, CPacket::udpHeaderSize(AF_INET), 0, 8192));
        last_seqno = 12345;
    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_buffer.reset();
    }

};

TEST_F(TestSndBuffer, Basic)
{
    for (int i = 1; i < 11; ++i)
    {
        addBuffer("BUFFERDATA", 10, i);
    }

    sout.puts("BUFFER STATUS:");
    sout.puts(m_buffer->show());

    // Now let's read 3 packets from it

    CPacket rpkt;
    EXPECT_NE(readUnique((rpkt), 0), 0);
    EXPECT_NE(readUnique((rpkt), 0), 0);
    EXPECT_NE(readUnique((rpkt), 0), 0);

    // AFTER READING you need to declare you no longer need them.
    m_buffer->releaseSeqno(12345); // we haven't ACKed anything yet.

    // And let's see

    sout.puts("AFTER extracting 3 packets:");
    sout.puts(m_buffer->show());

    // Now let's schedule 12346 and 12347 for rexmit
    scheduleRexmit(12346, 12347);

    sout.puts("AFTER scheduling #1 and #2 for rexmit:");
    sout.puts(m_buffer->show());

    // Now remove up to the second one
    revokeSeq(12347); // this is the first seq that should stay

    sout.puts("AFTER ACK #0 and #1:");
    sout.puts(m_buffer->show());

    // Now read 4 more packets
    EXPECT_NE(readUnique((rpkt), 0), 0);
    EXPECT_NE(readUnique((rpkt), 0), 0);
    EXPECT_NE(readUnique((rpkt), 0), 0);
    EXPECT_NE(readUnique((rpkt), 0), 0);

    m_buffer->releaseSeqno(12345); // we haven't ACKed anything yet.

    // Then add two rexmit requests
    scheduleRexmit(12348, 12349);
    scheduleRexmit(12351, 12352);

    sout.puts("AFTER read 4, and loss-report: 12348-12349 and 12351-12352");
    sout.puts(m_buffer->show());

    // Ok, you should have now losses in order:
    // 12347 - 12349, 12351 - 12352

    EXPECT_EQ(popLoss(), 12347);
    EXPECT_EQ(popLoss(), 12348);
    EXPECT_EQ(popLoss(), 12349);
    EXPECT_EQ(popLoss(), 12351);

    EXPECT_EQ(lossLength(), 1);

    sout.puts("AFTER 4 times loss was popped:");
    sout.puts(m_buffer->show());

    sout.puts("Scheduled rexmit: 12348-12350 (3)");
    scheduleRexmit(12348, 12350);
    EXPECT_EQ(lossLength(), 4);
    sout.puts(m_buffer->show());

    sout.puts("Scheduled rexmit: and 12351-12353 (3)");
    scheduleRexmit(12351, 12353);

    EXPECT_EQ(lossLength(), 6);

    // NEXT TESTS:
    // 
    // 1. Add losses that cover existing losses pre- and post, with multiple records
    // 2. Add gluing-in losses
    // 3. Clear a single loss with 0-time and test how itś skipped.
    // 4. Set future loss time, followed by 0-time and see skipping with pop().

    sout.puts(m_buffer->show());

    // Ok so let's cancel now 12350 and lift the time of 12351 in the future
    cancelRexmit(12350);
    cancelRexmit(12351);
    scheduleRexmit(12351, 12351, milliseconds_from(500)); // 0.5s in the future

    sout.puts("Cleared 12350 and set 12351 0.5s in the future");
    sout.puts(m_buffer->show());

    // Now extract a loss 3 times. 50 should be wiped and 51 skipped.
    EXPECT_EQ(popLoss(), 12348);
    EXPECT_EQ(popLoss(), 12349);

    EXPECT_EQ(popLoss(), 12352);

    sout.puts("After extracting 12348, 12349 and 12352");
    sout.puts(m_buffer->show());

    sout.puts("Sleep for 0.5s to make 12351 future-expire");
    std::this_thread::sleep_for(500ms);

    sout.puts("Now 12351 should be extracted, then 12353");
    EXPECT_EQ(popLoss(), 12351);
    EXPECT_EQ(popLoss(), 12353);

    // And all loss reports should be gone
    EXPECT_EQ(lossLength(), 0);

    sout.puts(m_buffer->show());
}

// Second test for sender buffer should use multiple threads for
// scheduling packets, scheduling losses, and picking up packets for sending.

