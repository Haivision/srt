
#include "gtest/gtest.h"
#include "test_env.h"
#include "buffer_snd.h"
#include <iostream>
#include <thread>
#include "ofmt.h"
#include "sync.h"


using namespace std;
using namespace srt;
using namespace srt::sync;

class TestSndBuffer: public srt::Test
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

    size_t readUniqueForget()
    {
        int pktskipseqno = 0;
        int kflg = 0;
        time_point tsOrigin;

        CSndPacket sndpkt;
        int size = m_buffer->extractUniquePacket((sndpkt), (tsOrigin), kflg, (pktskipseqno));
        // Ignore intermediate data

        return size_t(size);
    }

    size_t readUniqueKeep(CSndPacket& sndpkt)
    {
        int pktskipseqno = 0;
        int kflg = 0;
        time_point tsOrigin;

        int size = m_buffer->extractUniquePacket((sndpkt), (tsOrigin), kflg, (pktskipseqno));
        // Ignore intermediate data

        return size_t(size);
    }

    size_t readOld(int32_t seqno, CSndPacket& w_pkt)
    {
        std::list<std::pair<int32_t, int32_t>> dropseq;
        return readOld(seqno, (w_pkt), (dropseq));
    }

    size_t readOld(int32_t seqno, CSndPacket& w_pkt, std::list<std::pair<int32_t, int32_t>>& w_dropseq)
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
    void setup() override
    {
        // make_unique is unfortunatelly C++14
        m_buffer.reset(new CSndBuffer(32*1024, 1024, 1500, CPacket::udpHeaderSize(AF_INET), 0, 8192));
        last_seqno = 12345;
    }

    void teardown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
        m_buffer.reset();
    }

};


class TestSndLoss: public srt::Test
{
    using time_point = steady_clock::time_point;

public:

    hvu::ofmtrefstream sout {cout};

    srt::SndPktArray packets { 1024, 20 };

protected:

    // SetUp() is run immediately before a test starts.
    void setup() override
    {
        for (int i = 0; i < 20; ++i)
        {
            packets.push();
        }
    }


    void teardown() override
    {
        packets.clearAllLoss();
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
    EXPECT_NE(readUniqueForget(), 0);
    EXPECT_NE(readUniqueForget(), 0);

    {
        CSndPacket spkt;
        EXPECT_NE(readUniqueKeep((spkt)), 0);
    }

    // And let's see
    sout.puts("AFTER extracting 3 packets:");
    sout.puts(m_buffer->show());

    // Now let's schedule 12346 and 12347 for rexmit
    scheduleRexmit(12346, 12347);

    sout.puts("AFTER scheduling #1 and #2 for rexmit:");
    sout.puts(m_buffer->show());

    {
        // Now read one packet as old at seq 12346, and while keeping it, ACK up to 12347.
        CSndPacket snd;
        EXPECT_NE(readOld(12346, (snd)), 0);
        revokeSeq(12347);

        // SHOULD ACK only up to 12346.
        EXPECT_EQ(m_buffer->firstSeqNo(), 12346);

        sout.puts("READ 12346 and ack up to 12347:");
        sout.puts(m_buffer->show());
    }

    sout.puts("RELEASED send packet 12346:");
    sout.puts(m_buffer->show());

    // Now remove up to the second one
    revokeSeq(12347); // this is the first seq that should stay

    sout.puts("AFTER ACK #0 and #1:");
    sout.puts(m_buffer->show());

    // Now read 4 more packets
    EXPECT_NE(readUniqueForget(), 0);
    EXPECT_NE(readUniqueForget(), 0);
    EXPECT_NE(readUniqueForget(), 0);
    {
        CSndPacket spkt;
        EXPECT_NE(readUniqueKeep(spkt), 0);
    }

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

static size_t generateRandomPayload(char* pw_out, size_t minsize, size_t maxsize)
{
    // Generate the size
    size_t size = genRandomInt(minsize, maxsize);

    for (size_t i = 0; i < size; ++i)
        pw_out[i] = genRandomInt(32, 127);

    return size;
}

// Second test for sender buffer should use multiple threads for
// scheduling packets, scheduling losses, and picking up packets for sending.

TEST_F(TestSndBuffer, Threaded)
{
    // We create 2 threads:
    // 1. Sender Thread: will get packets from the buffer and "send" them.
    //    The thread is controlled by the timer that gives it 0.2s between
    //    each reading request. We try first to get a loss, and if this isn't
    //    delivered, a new unique packet.

    auto sender_thread_fn = [this] ()
    {
        for (;;)
        {
            // XXX try to fuzzy this value a bit
            std::this_thread::sleep_for(200ms);

            sout.puts("[S] Checking on LOSS seq");

            // Check if a lost sequence is available
            CSndBuffer::DropRange buffer_drop;
            int32_t seq = m_buffer->popLostSeq((buffer_drop));
            if (seq != SRT_SEQNO_NONE)
            {
                // Pick up the loss and "send" it.
                CSndPacket snd;
                int payload = readOld(seq, (snd));
                EXPECT_GT(payload, 0);

                // "send" it.
                char buf[1024];
                memcpy(buf, snd.pkt.data(), snd.pkt.size());
                sout.puts("[S] Lost packet %", seq, " !", BufferStamp(buf, snd.pkt.size()));

                continue;
            }

            CSndPacket snd;
            int pld_size = readUniqueKeep((snd));
            if (pld_size == 0) // no more packets
            {
                sout.puts("[S] NO MORE PACKETS, exitting");
                return;
            }
            EXPECT_NE(pld_size, -1);

            // "send" it.
            char buf[1024];
            memcpy(buf, snd.pkt.data(), snd.pkt.size());
            sout.puts("[S] Unique packet %", snd.seqno, " !", BufferStamp(buf, snd.pkt.size()));
        }
    };

    // 2. Update Thread: will simulate ACK or LOSS reception and update the
    //    sender buffer accordingly.

    auto update_thread_fn = [this] ()
    {
        // This should be already after sending 4 packets.
        std::this_thread::sleep_for(1s);
        //
        // So now declare packet 3 as lost

        int32_t lostseq = 12345 + 3;
        sout.puts("[U] Adding loss info: %", lostseq);
        scheduleRexmit(lostseq, lostseq);

        std::this_thread::sleep_for(200ms);
        // After that you should expect the lost packet retransmitted,
        // so fake having received ACK

        sout.puts("[U] ACK %", 12348);
        revokeSeq(12349);

        // Just in case
        std::this_thread::sleep_for(200ms);

        sout.puts("[U] ACK %", 12355);
        revokeSeq(12355);
    };

    std::thread sender_thread (sender_thread_fn);

    std::thread update_thread (update_thread_fn);

    // Ok; main thread is going to submit packets,
    // then wait until all other threads are finished.

    // (secondary threads are starting with some slip, so
    // we have a guarantee to get at least one packet send-ready)

    // 32 is the total capacity
    for (int i = 0; i < 24; ++i)
    {
        char buf[1024];
        size_t size = generateRandomPayload((buf), 384, 1001);

        sout.puts("[A] Sending payload size=", size, " !", BufferStamp(buf, size));

        addBuffer(buf, size, i+1);

        std::this_thread::sleep_for(100ms); //2* faster than reading
    }

    sout.puts("[A] DONE, waiting for others to finish");
    sender_thread.join();
    update_thread.join();
}


// Tests for SndPktArray and loss management. Based on the test case description generated by Grok.

TEST_F(TestSndLoss, Insert_into_empty_structure)
{
    // Initial: Empty (first=-1, last=-1, no nodes).
    // (nothing to do)

    // Operation: Insert [5,7].  

    packets.insert_loss(5, 7);

    // Expected: Nodes = {5: len=3, next=0}; first=5, last=5.
    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets.first_loss(), 5);
    EXPECT_EQ(packets.last_loss(), 5);

    EXPECT_EQ(packets.loss_length(), 3);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_disjoint_before_existing_AKA_becomes_new_first)
{
    // Initial: Nodes = {5: len=3, next=0}; first=5, last=5.
    packets.insert_loss(5, 7);

    // Operation: Insert [1,2].  
    packets.insert_loss(1, 2);

    // Expected: Nodes = {1: len=2, next=4}, {5: len=3, next=0}; first=1, last=5.
    EXPECT_EQ(packets[1].m_iLossLength, 2);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 4);
    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 5);

    EXPECT_EQ(packets.loss_length(), 5);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_disjoint_after_existing_AKA_becomes_new_last)
{

    // Initial: Nodes = {1: len=2, next=4}, {5: len=3, next=0}; first=1, last=5.  
    packets.insert_loss(5, 7);
    packets.insert_loss(1, 2);

    // Operation: Insert [10,12].  
    packets.insert_loss(10, 12);

    // Expected: Nodes = {1: len=2, next=4}, {5: len=3, next=5}, {10: len=3, next=0}; first=1, last=10.
    EXPECT_EQ(packets[1].m_iLossLength, 2);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 4);
    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 5);
    EXPECT_EQ(packets[10].m_iLossLength, 3);
    EXPECT_EQ(packets[10].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 10);

    EXPECT_EQ(packets.loss_length(), 8);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_disjoint_in_middle_gap)
{

    // Initial: Nodes = {1: len=2, next=4}, {5: len=3, next=5}, {10: len=3, next=0}; first=1, last=10.  
    packets.insert_loss(10, 12);
    packets.insert_loss(1, 2);
    packets.insert_loss(5, 6);

    // Operation: Insert [8,8].  
    packets.insert_loss(8, 8);

    // Expected: Nodes = {1: len=2, next=4}, {5: len=2, next=3}, {8: len=1, next=2}, {10: len=3, next=0}; first=1, last=10.
    EXPECT_EQ(packets[1].m_iLossLength, 2);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 4);

    EXPECT_EQ(packets[5].m_iLossLength, 2);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 3);

    EXPECT_EQ(packets[8].m_iLossLength, 1);
    EXPECT_EQ(packets[8].m_iNextLossGroupOffset, 2);

    EXPECT_EQ(packets[10].m_iLossLength, 3);
    EXPECT_EQ(packets[10].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 10);

    EXPECT_EQ(packets.loss_length(), 8);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_adjacent_left_of_existing_AKA_merge)
{
    // Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);

    // Operation: Insert [4,4].  
    packets.insert_loss(4, 4);

    // Expected: Nodes = {4: len=4, next=0}; first=4, last=4.
    EXPECT_EQ(packets[4].m_iLossLength, 4);

    EXPECT_EQ(packets.loss_length(), 4);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_adjacent_right_of_existing_AKA_merge)
{
    // Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);

    // Operation: Insert [8,8].  
    packets.insert_loss(8, 8);

    // Expected: Nodes = {5: len=4, next=0}; first=5, last=5.
    EXPECT_EQ(packets[5].m_iLossLength, 4);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets[8].m_iLossLength, 0);
    EXPECT_EQ(packets.first_loss(), 5);
    EXPECT_EQ(packets.last_loss(), 5);

    EXPECT_EQ(packets.loss_length(), 4);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_overlapping_left_of_existing_AKA_extend_left)
{
    // Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);

    // Operation: Insert [3,6].  
    packets.insert_loss(3, 6);

    // Expected: Nodes = {3: len=5, next=0}; first=3, last=3.
    EXPECT_EQ(packets[3].m_iLossLength, 5);
    EXPECT_EQ(packets[3].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets.first_loss(), 3);
    EXPECT_EQ(packets.last_loss(), 3);

    EXPECT_EQ(packets.loss_length(), 5);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_overlapping_right_of_existing_AKA_extend_right)
{
    // Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);

    // Operation: Insert [6,9].  
    packets.insert_loss(6, 9);

    // Expected: Nodes = {5: len=5, next=0}; first=5, last=5.
    EXPECT_EQ(packets[5].m_iLossLength, 5);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets[6].m_iLossLength, 0);
    EXPECT_EQ(packets.first_loss(), 5);
    EXPECT_EQ(packets.last_loss(), 5);

    EXPECT_EQ(packets.loss_length(), 5);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_completely_covering_existing_AKA_swallow)
{
    // Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);

    // Operation: Insert [4,8].  
    packets.insert_loss(4, 8);

    // Expected: Nodes = {4: len=5, next=0}; first=4, last=4.
    EXPECT_EQ(packets[4].m_iLossLength, 5);
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets.first_loss(), 4);
    EXPECT_EQ(packets.last_loss(), 4);

    EXPECT_EQ(packets.loss_length(), 5);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_inside_existing_AKA_no_change)
{
    //  Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);

    //  Operation: Insert [6,6].  
    packets.insert_loss(6, 6);

    //  Expected: No change; first=5, last=5.
    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets[6].m_iLossLength, 0);

    EXPECT_EQ(packets.first_loss(), 5);
    EXPECT_EQ(packets.last_loss(), 5);

    EXPECT_EQ(packets.loss_length(), 3);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_inside_existing_AKA_no_change_2)
{
    //  Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(1, 11);
    EXPECT_EQ(packets[1].m_iLossLength, 11);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 0);

    //  Operation: Insert [6,6].  
    packets.insert_loss(6, 6);
    packets.insert_loss(9, 10);
    packets.insert_loss(3, 9);

    //  Expected: No change; first=5, last=5.
    EXPECT_EQ(packets[1].m_iLossLength, 11);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets[3].m_iLossLength, 0);
    EXPECT_EQ(packets[6].m_iLossLength, 0);
    EXPECT_EQ(packets[9].m_iLossLength, 0);

    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 1);

    EXPECT_EQ(packets.loss_length(), 11);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_bridging_two_disjoint_ranges_AKA_merge)
{
    //  Initial: Nodes = {1: len=2, next=4}, {5: len=3, next=0}; first=1, last=5.  
    packets.insert_loss(1, 2);
    packets.insert_loss(5, 7);
    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 5);

    //  Operation: Insert [3,4].  
    packets.insert_loss(3, 4);

    //  Expected: Nodes = {1: len=7, next=0}; first=1, last=1.

    // (first: expect removed nodes at 3 and 5
    EXPECT_EQ(packets[3].m_iLossLength, 0);
    EXPECT_EQ(packets[5].m_iLossLength, 0);

    // Now valid node
    EXPECT_EQ(packets[1].m_iLossLength, 7);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 1);

    EXPECT_EQ(packets.loss_length(), 7);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_overlapping_and_bridging_multiple_ranges)
{
    //  Initial: Nodes = {1: len=2, next=4}, {5: len=3, next=5}, {10: len=3, next=0}; first=1, last=10.  
    packets.insert_loss(10, 12);
    packets.insert_loss(5, 7);
    packets.insert_loss(1, 2);

    EXPECT_EQ(packets[1].m_iLossLength, 2);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 4);

    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 5);

    EXPECT_EQ(packets[10].m_iLossLength, 3);
    EXPECT_EQ(packets[10].m_iNextLossGroupOffset, 0);

    //  Operation: Insert [4,11].  
    packets.insert_loss(4, 11);

    //  Expected: Nodes = {1: len=2, next=3}, {4: len=9, next=0}; first=1, last=4. (Merges second and third, overlaps first's adjacent.)
    EXPECT_EQ(packets[1].m_iLossLength, 2);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 3);
    EXPECT_EQ(packets[4].m_iLossLength, 9);
    EXPECT_EQ(packets[4].m_iNextLossGroupOffset, 0);
    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 4);

    EXPECT_EQ(packets.loss_length(), 11);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_swallowing_multiple_ranges)
{
    //  Initial: Nodes = {1: len=2, next=4}, {5: len=3, next=5}, {10: len=3, next=0}; first=1, last=10.  
    packets.insert_loss(10, 12);
    packets.insert_loss(5, 7);
    packets.insert_loss(1, 2);

    EXPECT_EQ(packets[1].m_iLossLength, 2);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 4);

    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 5);

    EXPECT_EQ(packets[10].m_iLossLength, 3);
    EXPECT_EQ(packets[10].m_iNextLossGroupOffset, 0);

    //  Operation: Insert [0,15].  
    packets.insert_loss(0, 15);

    // Expect first that none of the old nodes exists anymore.
    EXPECT_EQ(packets[1].m_iLossLength, 0);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets[10].m_iLossLength, 0);
    EXPECT_EQ(packets[10].m_iNextLossGroupOffset, 0);

    //  Expected: Nodes = {0: len=16, next=0}; first=0, last=0.
    EXPECT_EQ(packets[0].m_iLossLength, 16);
    EXPECT_EQ(packets[0].m_iNextLossGroupOffset, 0);

    // The only node
    EXPECT_EQ(packets.first_loss(), 0);
    EXPECT_EQ(packets.last_loss(), 0);

    EXPECT_EQ(packets.loss_length(), 16);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_at_absolute_front_AKA_index_0_disjoint)
{
    //  Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);
    EXPECT_EQ(packets[5].m_iLossLength, 3);

    //  Operation: Insert [0,0].  
    packets.insert_loss(0, 0);

    //  Expected: Nodes = {0: len=1, next=5}, {5: len=3, next=0}; first=0, last=5.
    EXPECT_EQ(packets[0].m_iLossLength, 1);
    EXPECT_EQ(packets[0].m_iNextLossGroupOffset, 5);

    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets.first_loss(), 0);
    EXPECT_EQ(packets.last_loss(), 5);

    EXPECT_EQ(packets.loss_length(), 4);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_at_absolute_end_AKA_last_index_disjoint)
{
    //  Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);
    EXPECT_EQ(packets[5].m_iLossLength, 3);

    //  Operation: Insert [19,19].  
    packets.insert_loss(19, 19);

    //  Expected: Nodes = {5: len=3, next=14}, {19: len=1, next=0}; first=5, last=19.
    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[5].m_iNextLossGroupOffset, 14);

    EXPECT_EQ(packets[19].m_iLossLength, 1);
    EXPECT_EQ(packets[19].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets.first_loss(), 5);
    EXPECT_EQ(packets.last_loss(), 19);

    EXPECT_EQ(packets.loss_length(), 4);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_full_deque_range_over_empty)
{
    //  Initial: Empty.  
    //  Operation: Insert [0,19].  
    packets.insert_loss(0, 19);

    //  Expected: Nodes = {0: len=20, next=0}; first=0, last=0.
    EXPECT_EQ(packets[0].m_iLossLength, 20);
    EXPECT_EQ(packets[0].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets.first_loss(), 0);
    EXPECT_EQ(packets.last_loss(), 0);

    EXPECT_EQ(packets.loss_length(), 20);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Insert_adjacent_or_overlapping_when_updating_last_only)
{
    //  Initial: Nodes = {1: len=2, next=0}; first=1, last=1.  
    packets.insert_loss(1, 2);

    //  Operation: Insert [3,4]. (Adjacent, merge if policy allows; assume merge for contiguous).  
    packets.insert_loss(3, 4);

    //  Expected: Nodes = {1: len=4, next=0}; first=1, last=1.
    EXPECT_EQ(packets[1].m_iLossLength, 4);
    EXPECT_EQ(packets[1].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets[3].m_iLossLength, 0);

    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 1);

    EXPECT_EQ(packets.loss_length(), 4);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}


// POP single item removal tests

TEST_F(TestSndLoss, Remove_from_single_range_with_len_1_AKA_empties_structure)
{
    // Initial: Nodes = {5: len=1, next=0}; first=5, last=5.  
    packets.insert_loss(5, 5);

    // Operation: Remove single first.
    int first_loss = packets.extractFirstLoss();
    EXPECT_EQ(first_loss, 5);

    // Expected: Empty; first=-1, last=-1.
    EXPECT_EQ(packets.loss_length(), 0);
    EXPECT_EQ(packets.first_loss(), -1);
    EXPECT_EQ(packets.last_loss(), -1);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Remove_from_single_range_with_len_gt_1_AKA_shrink_left)
{
    // Initial: Nodes = {5: len=3, next=0}; first=5, last=5.  
    packets.insert_loss(5, 7);

    // Operation: Remove single first.  
    int first_loss = packets.extractFirstLoss();
    EXPECT_EQ(first_loss, 5);

    // Expected: Nodes = {6: len=2, next=0}; first=6, last=6. (Implicit len=0 at old 5 position.)
    EXPECT_EQ(packets.loss_length(), 2);
    EXPECT_EQ(packets.first_loss(), 6);
    EXPECT_EQ(packets.last_loss(), 6);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Remove_from_first_range_len_1_multiple_ranges_AKA_update_first)
{
    // Initial: Nodes = {1: len=1, next=4}, {5: len=3, next=0}; first=1, last=5.  
    packets.insert_loss(5, 7);
    packets.insert_loss(1, 1);

    // Operation: Remove single first.  
    int first_loss = packets.extractFirstLoss();
    EXPECT_EQ(first_loss, 1);

    // EXPECT: removed nodes
    EXPECT_EQ(packets[1].m_iLossLength, 0);

    // Expected: Nodes = {5: len=3, next=0}; first=5, last=5.
    EXPECT_EQ(packets[5].m_iLossLength, 3);

    EXPECT_EQ(packets.loss_length(), 3);
    EXPECT_EQ(packets.first_loss(), 5);
    EXPECT_EQ(packets.last_loss(), 5);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Remove_from_first_range_len_gt_1_multiple_ranges_AKA_shrink_last_unchanged)
{
    // Initial: Nodes = {1: len=3, next=5}, {8: len=2, next=0}; first=1, last=8.  
    packets.insert_loss(1, 3);
    packets.insert_loss(8, 9);

    // Operation: Remove single first.  
    int first_loss = packets.extractFirstLoss();
    EXPECT_EQ(first_loss, 1);

    // EXPECT: removed nodes
    EXPECT_EQ(packets[1].m_iLossLength, 0);

    // Expected: Nodes = {2: len=2, next=6}, {8: len=2, next=0}; first=2, last=8. (Next updated: 2 to 8 offset=6 > 2.)
    EXPECT_EQ(packets[2].m_iLossLength, 2);
    EXPECT_EQ(packets[2].m_iNextLossGroupOffset, 6);

    EXPECT_EQ(packets[8].m_iLossLength, 2);
    EXPECT_EQ(packets[8].m_iNextLossGroupOffset, 0);

    EXPECT_EQ(packets.first_loss(), 2);
    EXPECT_EQ(packets.last_loss(), 8);

    EXPECT_EQ(packets.loss_length(), 4);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Remove_when_only_one_marked_element_overall)
{
    // Initial: Nodes = {0: len=1, next=0}; first=0, last=0.  

    packets.insert_loss(0, 0);

    // Operation: Remove single first.  
    int first_loss = packets.extractFirstLoss();
    EXPECT_EQ(first_loss, 0);

    // Expected: Empty; first=-1, last=-1.
    EXPECT_EQ(packets.loss_length(), 0);
    EXPECT_EQ(packets.first_loss(), -1);
    EXPECT_EQ(packets.last_loss(), -1);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Remove_when_removal_affects_last_AKA_single_range_len_2)
{
    // Initial: Nodes = {5: len=2, next=0}; first=5, last=5.  
    packets.insert_loss(5, 6);

    // Operation: Remove single first.  
    int first_loss = packets.extractFirstLoss();
    EXPECT_EQ(first_loss, 5);

    // Expected: Nodes = {6: len=1, next=0}; first=6, last=6.
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets[6].m_iLossLength, 1);

    EXPECT_EQ(packets.loss_length(), 1);
    EXPECT_EQ(packets.first_loss(), 6);
    EXPECT_EQ(packets.last_loss(), 6);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

// Remove multiple tests

TEST_F(TestSndLoss, Remove_M_lt_first_range_len_AKA_partial_shrink_left)
{
    // Initial: Nodes = {5: len=5_ next=0}; first=5_ last=5. M=2.  
    packets.insert_loss(5, 9);

    // Operation: Remove first 2 marked.  

    // NOTE: AI Bot didn't understand that removal is per packet
    // index, not loss number - remove loss from as many packets
    // as needed so that 5, 6 sequences are hooked up.
    packets.remove_loss(6);

    // Expected: Nodes = {7: len=3_ next=0}; first=7_ last=7.
    EXPECT_EQ(packets.first_loss(), 7);
    EXPECT_EQ(packets.last_loss(), 7);
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets[7].m_iLossLength, 3);
    EXPECT_EQ(packets.loss_length(), 3);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}
TEST_F(TestSndLoss, Remove_M_equal_first_range_len_AKA_remove_entire_first_range)
{
    // Initial: Nodes = {5: len=3_ next=5}_ {10: len=2_ next=0}; first=5_ last=10. M=3.  
    packets.insert_loss(5, 7);
    packets.insert_loss(10, 12);

    // Operation: Remove first 3 marked.  
    // AI-FIX: intended is that removed are 3 subsequent losses,
    // so remove all up to 9
    packets.remove_loss(9);

    // AI GENERATION:
    // Expected: Nodes = {10: len=2_ next=0}; first=10_ last=10.
    // Expected: Nodes = {7: len=3_ next=0}; first=7_ last=7.
    //
    // Ok, this is bullshit; when removed up to 9, it should
    // clear the first record and leave untouchted the second one,
    // so it's 10-12 the only remaining loss.

    EXPECT_EQ(packets.first_loss(), 10);
    EXPECT_EQ(packets.last_loss(), 10);
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets[10].m_iLossLength, 3);
    EXPECT_EQ(packets.loss_length(), 3);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}
TEST_F(TestSndLoss, Remove_M_gt_first_range_len_AKA_remove_first_AND_partial_next)
{
    // Initial: Nodes = {1: len=2_ next=4}_ {5: len=3_ next=0}; first=1_ last=5. M=4.  
    packets.insert_loss(1, 2);
    packets.insert_loss(5, 7);

    // AI:
    // Operation: Remove first 4 marked. AKA_Remove [1-2] + first 2 of [5-7]  
    // Expected: Nodes = {7: len=1_ next=0}; first=7_ last=7.
    // REAL:
    // Operation: remove up to 5. should remain 6-7.
    packets.remove_loss(5);
    // So, expected clear node 1 and 5, activated 6 with len=2
    EXPECT_EQ(packets[1].m_iLossLength, 0);
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets[6].m_iLossLength, 2);
    EXPECT_EQ(packets.first_loss(), 6);
    EXPECT_EQ(packets.last_loss(), 6);

    EXPECT_EQ(packets.loss_length(), 2);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}
TEST_F(TestSndLoss, Remove_across_multiple_full_ranges)
{
    // Initial: Nodes = {1: len=2_ next=4}_ {5: len=3_ next=5}_ {10: len=2_ next=0}; first=1_ last=10. M=5.  
    packets.insert_loss(1, 2);
    packets.insert_loss(5, 7);
    packets.insert_loss(10, 12);

    // AI:
    // Operation: Remove first 5 marked. AKA_Full first + full second  
    // Expected: Nodes = {10: len=2_ next=0}; first=10_ last=10.
    // REAL:
    // Ok, so let's remove up to 8 so that node 10 is left untouched.
    packets.remove_loss(8);

    EXPECT_EQ(packets.first_loss(), 10);
    EXPECT_EQ(packets.last_loss(), 10);
    EXPECT_EQ(packets[1].m_iLossLength, 0);
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets[10].m_iLossLength, 3);
    EXPECT_EQ(packets.loss_length(), 3);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}
TEST_F(TestSndLoss, Remove_all_marked_elements_AKA_empties_structure)
{
    // Initial: Nodes = {1: len=2_ next=4}_ {5: len=3_ next=0}; first=1_ last=5. M=5.  
    packets.insert_loss(1, 2);
    packets.insert_loss(5, 7);

    // Operation: Remove first 5 marked.  
    // Expected: Empty; first=-1_ last=-1.
    // REAL: Ok, this time let's remove up to the exact element.
    // Should be empty afterwards.
    packets.remove_loss(7);

    EXPECT_EQ(packets.first_loss(), -1);
    EXPECT_EQ(packets.last_loss(), -1);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}

TEST_F(TestSndLoss, Build_complex_then_remove_single_and_prefix)
{
    // Start empty. Insert [1,2], [5,7], [10,10]. (Disjoint).
    packets.insert_loss(1, 2);
    packets.insert_loss(5, 7);
    packets.insert_loss(10, 10);

    // Expected after inserts: {1: len=2, next=4}, {5: len=3, next=5}, {10: len=1, next=0}; first=1, last=10. (Gaps ensure next > len.)
    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 10);
    EXPECT_EQ(packets[1].m_iLossLength, 2);
    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[10].m_iLossLength, 1);
    EXPECT_EQ(packets.loss_length(), 6);

    std::string validmsg;
    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;

    // Insert [3,6]. (Bridge + overlap + adjacent). Expected: {1: len=7, next=9}, {10: len=1, next=0}; first=1, last=10. (Merge to [1-7]; 9 > 7.)
    packets.insert_loss(3, 6);

    EXPECT_EQ(packets.first_loss(), 1);
    EXPECT_EQ(packets.last_loss(), 10);
    EXPECT_EQ(packets[1].m_iLossLength, 7);
    EXPECT_EQ(packets[5].m_iLossLength, 0);
    EXPECT_EQ(packets[10].m_iLossLength, 1);
    EXPECT_EQ(packets.loss_length(), 8);

    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;

    // Remove single first. Expected: {2: len=6, next=8}, {10: len=1, next=0}; first=2, last=10. (8 > 6.)
    int first_loss = packets.extractFirstLoss();
    EXPECT_EQ(first_loss, 1);
    EXPECT_EQ(packets.first_loss(), 2);
    EXPECT_EQ(packets.last_loss(), 10);
    EXPECT_EQ(packets[1].m_iLossLength, 0);
    EXPECT_EQ(packets[2].m_iLossLength, 6);
    EXPECT_EQ(packets[2].m_iNextLossGroupOffset, 8);
    EXPECT_EQ(packets[10].m_iLossLength, 1);
    EXPECT_EQ(packets.loss_length(), 7);

    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;

    // Remove first 3 marked. Expected: {5: len=3, next=5}, {10: len=1, next=0}; first=5, last=10. (5 > 3.)
    packets.remove_loss(4);
    EXPECT_EQ(packets.first_loss(), 5);
    EXPECT_EQ(packets.last_loss(), 10);
    EXPECT_EQ(packets[1].m_iLossLength, 0);
    EXPECT_EQ(packets[2].m_iLossLength, 0);
    EXPECT_EQ(packets[5].m_iLossLength, 3);
    EXPECT_EQ(packets[10].m_iLossLength, 1);
    EXPECT_EQ(packets.loss_length(), 4);

    EXPECT_TRUE(packets.validateLossIntegrity((validmsg))) << ">>> " << validmsg;
}




