
#include "gtest/gtest.h"
#include "buffer_snd.h"
#include <iostream>
#include <thread>
#include "ofmt.h"
#include "sync.h"


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

