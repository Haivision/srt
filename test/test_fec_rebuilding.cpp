#include <vector>
#include <algorithm>
#include <future>

#include "gtest/gtest.h"
#include "packet.h"
#include "fec.h"
#include "core.h"
#include "packetfilter.h"
#include "packetfilter_api.h"

// For direct imp access
#include "api.h"

using namespace std;

class TestFECRebuilding: public testing::Test
{
protected:
    FECFilterBuiltin* fec = nullptr;
    vector<SrtPacket> provided;
    vector<unique_ptr<CPacket>> source;
    int sockid = 54321;
    int isn = 123456;
    size_t plsize = 1316;

    TestFECRebuilding()
    {
        // Required to make ParseCorrectorConfig work
        PacketFilter::globalInit();
    }

    void SetUp() override
    {
        int timestamp = 10;

        SrtFilterInitializer init = {
            sockid,
            isn - 1, // It's passed in this form to PacketFilter constructor, it should increase it
            isn - 1, // XXX Probably this better be changed.
            plsize,
            CSrtConfig::DEF_BUFFER_SIZE
        };


        // Make configuration row-only with size 7
        string conf = "fec,rows:1,cols:7";

        provided.clear();

        fec = new FECFilterBuiltin(init, provided, conf);

        int32_t seq = isn;

        for (int i = 0; i < 7; ++i)
        {
            source.emplace_back(new CPacket);
            CPacket& p = *source.back();

            p.allocate(SRT_LIVE_MAX_PLSIZE);

            uint32_t* hdr = p.getHeader();

            // Fill in the values
            hdr[SRT_PH_SEQNO] = seq;
            hdr[SRT_PH_MSGNO] = 1 | MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);
            hdr[SRT_PH_ID] = sockid;
            hdr[SRT_PH_TIMESTAMP] = timestamp;

            // Fill in the contents.
            // Randomly chose the size

            int minsize = 732;
            int divergence = plsize - minsize - 1;
            size_t length = minsize + rand() % divergence;

            p.setLength(length);
            for (size_t b = 0; b < length; ++b)
            {
                p.data()[b] = rand() % 255;
            }

            timestamp += 10;
            seq = CSeqNo::incseq(seq);
        }
    }

    void TearDown() override
    {
        delete fec;
    }
};

class TestMockCUDT
{
public:
    CUDT* core;

    bool checkApplyFilterConfig(const string& s)
    {
        return core->checkApplyFilterConfig(s);
    }
};

// For config exchange we have several possibilities here:
//
// 1. Unknown filter - should be rejected.
//
// 2. Confronting configurations:
//
// - any same parameters with different values are rejected
// - resulting configuiration should have the `cols` value set
//
// We need then the following tests:
//
// 1. Setting the option with unknown filter
// 2. Confrontation with conflicting parameters
// 3. Confrontation with the result not having required `cols`.
// 4. Successful confrontation

bool filterConfigSame(const string& config1, const string& config2)
{
    vector<string> config1_vector;
    Split(config1, ',', back_inserter(config1_vector));
    sort(config1_vector.begin(), config1_vector.end());

    vector<string> config2_vector;
    Split(config2, ',', back_inserter(config2_vector));
    sort(config2_vector.begin(), config2_vector.end());

    return config1_vector == config2_vector;
}

TEST(TestFEC, ConfigExchange)
{
    srt_startup();

    CUDTSocket* s1;

    SRTSOCKET sid1 = CUDT::uglobal()->newSocket(&s1);

    TestMockCUDT m1;
    m1.core = &s1->core();

    // Can't access the configuration storage without
    // accessing the private fields, so let's use the official API

    char fec_config1 [] = "fec,cols:10,rows:10";

    srt_setsockflag(sid1, SRTO_PACKETFILTER, fec_config1, sizeof fec_config1);

    EXPECT_TRUE(m1.checkApplyFilterConfig("fec,cols:10,arq:never"));

    char fec_configback[200];
    int fec_configback_size = 200;
    srt_getsockflag(sid1, SRTO_PACKETFILTER, fec_configback, &fec_configback_size);

    // Order of parameters may differ, so store everything in a vector and sort it.

    string exp_config = "fec,cols:10,rows:10,arq:never,layout:even";

    EXPECT_TRUE(filterConfigSame(fec_configback, exp_config));
    srt_cleanup();
}

TEST(TestFEC, ConfigExchangeFaux)
{
    srt_startup();

    CUDTSocket* s1;

    SRTSOCKET sid1 = CUDT::uglobal()->newSocket(&s1);

    char fec_config_wrong [] = "FEC,Cols:20";
    ASSERT_EQ(srt_setsockflag(sid1, SRTO_PACKETFILTER, fec_config_wrong, sizeof fec_config_wrong), -1);

    TestMockCUDT m1;
    m1.core = &s1->core();

    // Can't access the configuration storage without
    // accessing the private fields, so let's use the official API

    char fec_config1 [] = "fec,cols:20,rows:10";

    srt_setsockflag(sid1, SRTO_PACKETFILTER, fec_config1, sizeof fec_config1);

    cout << "(NOTE: expecting a failure message)\n";
    EXPECT_FALSE(m1.checkApplyFilterConfig("fec,cols:10,arq:never"));

    srt_cleanup();
}

TEST(TestFEC, Connection)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));
    srt_listen(l, 1);

    char fec_config1 [] = "fec,cols:10,rows:10";
    char fec_config2 [] = "fec,cols:10,arq:never";
    char fec_config_final [] = "fec,cols:10,rows:10,arq:never,layout:even";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, sizeof fec_config1);
    srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, sizeof fec_config2);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    sockaddr_in scl;
    int sclen = sizeof scl;
    SRTSOCKET a = srt_accept(l, (sockaddr*)& scl, &sclen);
    EXPECT_NE(a, SRT_ERROR);
    EXPECT_EQ(connect_res.get(), SRT_SUCCESS);

    // Now that the connection is established, check negotiated config

    char result_config1[200];
    int result_config1_size = 200;
    char result_config2[200];
    int result_config2_size = 200;

    srt_getsockflag(s, SRTO_PACKETFILTER, result_config1, &result_config1_size);
    srt_getsockflag(a, SRTO_PACKETFILTER, result_config2, &result_config2_size);

    string caller_config = result_config1;
    string accept_config = result_config2;
    EXPECT_EQ(caller_config, accept_config);

    EXPECT_TRUE(filterConfigSame(caller_config, fec_config_final));
    EXPECT_TRUE(filterConfigSame(accept_config, fec_config_final));

    srt_cleanup();
}

TEST(TestFEC, RejectionConflict)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));
    srt_listen(l, 1);

    char fec_config1 [] = "fec,cols:10,rows:10";
    char fec_config2 [] = "fec,cols:20,arq:never";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, sizeof fec_config1);
    srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, sizeof fec_config2);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    EXPECT_EQ(connect_res.get(), SRT_ERROR);
    EXPECT_EQ(srt_getrejectreason(s), SRT_REJ_FILTER);

    bool no = false;
    // Set non-blocking so that srt_accept can return
    // immediately with failure. Just to make sure that
    // the connection is not about to be established,
    // also on the listener side.
    srt_setsockflag(l, SRTO_RCVSYN, &no, sizeof no);
    sockaddr_in scl;
    int sclen = sizeof scl;
    EXPECT_EQ(srt_accept(l, (sockaddr*)& scl, &sclen), SRT_ERROR);

    srt_cleanup();
}

TEST(TestFEC, RejectionIncompleteEmpty)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));
    srt_listen(l, 1);

    char fec_config1 [] = "fec,rows:10";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, sizeof fec_config1);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    EXPECT_EQ(connect_res.get(), SRT_ERROR);
    EXPECT_EQ(srt_getrejectreason(s), SRT_REJ_FILTER);

    bool no = false;
    // Set non-blocking so that srt_accept can return
    // immediately with failure. Just to make sure that
    // the connection is not about to be established,
    // also on the listener side.
    srt_setsockflag(l, SRTO_RCVSYN, &no, sizeof no);
    sockaddr_in scl;
    int sclen = sizeof scl;
    EXPECT_EQ(srt_accept(l, (sockaddr*)& scl, &sclen), SRT_ERROR);

    srt_cleanup();
}


TEST(TestFEC, RejectionIncomplete)
{
    srt_startup();

    SRTSOCKET s = srt_create_socket();
    SRTSOCKET l = srt_create_socket();

    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(5555);
    ASSERT_EQ(inet_pton(AF_INET, "127.0.0.1", &sa.sin_addr), 1);

    srt_bind(l, (sockaddr*)& sa, sizeof(sa));
    srt_listen(l, 1);

    char fec_config1 [] = "fec,rows:10";
    char fec_config2 [] = "fec,arq:never";

    srt_setsockflag(s, SRTO_PACKETFILTER, fec_config1, sizeof fec_config1);
    srt_setsockflag(l, SRTO_PACKETFILTER, fec_config2, sizeof fec_config2);

    auto connect_res = std::async(std::launch::async, [&s, &sa]() {
        return srt_connect(s, (sockaddr*)& sa, sizeof(sa));
        });

    EXPECT_EQ(connect_res.get(), SRT_ERROR);
    EXPECT_EQ(srt_getrejectreason(s), SRT_REJ_FILTER);

    bool no = false;
    // Set non-blocking so that srt_accept can return
    // immediately with failure. Just to make sure that
    // the connection is not about to be established,
    // also on the listener side.
    srt_setsockflag(l, SRTO_RCVSYN, &no, sizeof no);
    sockaddr_in scl;
    int sclen = sizeof scl;
    EXPECT_EQ(srt_accept(l, (sockaddr*)& scl, &sclen), SRT_ERROR);

    srt_cleanup();
}

TEST_F(TestFECRebuilding, Prepare)
{
    // Stuff in prepared packets into the source fec.
    int32_t seq;
    for (int i = 0; i < 7; ++i)
    {
        CPacket& p = *source[i].get();

        // Feed it simultaneously into the sender FEC
        fec->feedSource(p);
        seq = p.getSeqNo();
    }

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);

    // Use the sequence number of the last packet, as usual.
    bool have_fec_ctl = fec->packControlPacket(fec_ctl, seq);

    EXPECT_EQ(have_fec_ctl, true);
}

TEST_F(TestFECRebuilding, NoRebuild)
{
    // Stuff in prepared packets into the source fec.
    int32_t seq;
    for (int i = 0; i < 7; ++i)
    {
        CPacket& p = *source[i].get();

        // Feed it simultaneously into the sender FEC
        fec->feedSource(p);
        seq = p.getSeqNo();
    }

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);

    // Use the sequence number of the last packet, as usual.
    bool have_fec_ctl = fec->packControlPacket(fec_ctl, seq);

    ASSERT_EQ(have_fec_ctl, true);
    // By having all packets and FEC CTL packet, now stuff in
    // these packets into the receiver

    FECFilterBuiltin::loss_seqs_t loss; // required as return, ignore

    for (int i = 0; i < 7; ++i)
    {
        // SKIP packet 4 to simulate loss
        if (i == 4 || i == 6)
            continue;

        // Stuff in the packet into the FEC filter
        bool want_passthru = fec->receive(*source[i], loss);
        EXPECT_EQ(want_passthru, true);
    }

    // Prepare a real packet basing on the SrtPacket.

    // XXX Consider packing this into a callable function as this
    // is a code directly copied from PacketFilter::packControlPacket.

    unique_ptr<CPacket> fecpkt ( new CPacket );

    uint32_t* chdr = fecpkt->getHeader();
    memcpy(chdr, fec_ctl.hdr, SRT_PH_E_SIZE * sizeof(*chdr));

    // The buffer can be assigned.
    fecpkt->m_pcData = fec_ctl.buffer;
    fecpkt->setLength(fec_ctl.length);

    // This sets only the Packet Boundary flags, while all other things:
    // - Order
    // - Rexmit
    // - Crypto
    // - Message Number
    // will be set to 0/false
    fecpkt->m_iMsgNo = MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);

    // ... and then fix only the Crypto flags
    fecpkt->setMsgCryptoFlags(EncryptionKeySpec(0));

    // And now receive the FEC control packet

    bool want_passthru_fec = fec->receive(*fecpkt, loss);
    EXPECT_EQ(want_passthru_fec, false); // Confirm that it's been eaten up
    EXPECT_EQ(provided.size(), 0); // Confirm that nothing was rebuilt

    /*
    // XXX With such a short sequence, losses will not be reported.
    // You need at least one packet past the row, even in 1-row config.
    // Probably a better way for loss collection should be devised.

    ASSERT_EQ(loss.size(), 2);
    EXPECT_EQ(loss[0].first, isn + 4);
    EXPECT_EQ(loss[1].first, isn + 6);
     */
}

TEST_F(TestFECRebuilding, Rebuild)
{
    // Stuff in prepared packets into the source fec->
    int32_t seq;
    for (int i = 0; i < 7; ++i)
    {
        CPacket& p = *source[i].get();

        // Feed it simultaneously into the sender FEC
        fec->feedSource(p);
        seq = p.getSeqNo();
    }

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);

    // Use the sequence number of the last packet, as usual.
    bool have_fec_ctl = fec->packControlPacket(fec_ctl, seq);

    ASSERT_EQ(have_fec_ctl, true);
    // By having all packets and FEC CTL packet, now stuff in
    // these packets into the receiver

    FECFilterBuiltin::loss_seqs_t loss; // required as return, ignore

    for (int i = 0; i < 7; ++i)
    {
        // SKIP packet 4 to simulate loss
        if (i == 4)
            continue;

        // Stuff in the packet into the FEC filter
        bool want_passthru = fec->receive(*source[i], loss);
        EXPECT_EQ(want_passthru, true);
    }

    // Prepare a real packet basing on the SrtPacket.

    // XXX Consider packing this into a callable function as this
    // is a code directly copied from PacketFilter::packControlPacket.

    unique_ptr<CPacket> fecpkt ( new CPacket );

    uint32_t* chdr = fecpkt->getHeader();
    memcpy(chdr, fec_ctl.hdr, SRT_PH_E_SIZE * sizeof(*chdr));

    // The buffer can be assigned.
    fecpkt->m_pcData = fec_ctl.buffer;
    fecpkt->setLength(fec_ctl.length);

    // This sets only the Packet Boundary flags, while all other things:
    // - Order
    // - Rexmit
    // - Crypto
    // - Message Number
    // will be set to 0/false
    fecpkt->m_iMsgNo = MSGNO_PACKET_BOUNDARY::wrap(PB_SOLO);

    // ... and then fix only the Crypto flags
    fecpkt->setMsgCryptoFlags(EncryptionKeySpec(0));

    // And now receive the FEC control packet

    bool want_passthru_fec = fec->receive(*fecpkt, loss);
    EXPECT_EQ(want_passthru_fec, false); // Confirm that it's been eaten up

    EXPECT_EQ(loss.size(), 0);
    ASSERT_EQ(provided.size(), 1);

    SrtPacket& rebuilt = provided[0];
    CPacket& skipped = *source[4];

    // Set artificially the SN_REXMIT flag in the skipped source packet
    // because the rebuilt packet shall have REXMIT flag set.
    skipped.m_iMsgNo |= MSGNO_REXMIT::wrap(true);

    // Compare the header
    EXPECT_EQ(skipped.getHeader()[SRT_PH_SEQNO], rebuilt.hdr[SRT_PH_SEQNO]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_MSGNO], rebuilt.hdr[SRT_PH_MSGNO]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_ID], rebuilt.hdr[SRT_PH_ID]);
    EXPECT_EQ(skipped.getHeader()[SRT_PH_TIMESTAMP], rebuilt.hdr[SRT_PH_TIMESTAMP]);

    // Compare sizes and contents
    ASSERT_EQ(skipped.size(), rebuilt.size());

    EXPECT_EQ(memcmp(skipped.data(), rebuilt.data(), rebuilt.size()), 0);
}
