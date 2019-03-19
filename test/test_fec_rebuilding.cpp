#include <vector>

#include "gtest/gtest.h"
#include "packet.h"
#include "fec.h"
#include "packetfilter.h"
#include "packetfilter_api.h"

using namespace std;


TEST(FECRebuilding, Create)
{
    // Required to make ParseCorrectorConfig work
    PacketFilter::globalInit();

    int sockid = 54321;
    int isn = 123456;
    size_t plsize = 1316;
    int timestamp = 10;

    SrtFilterInitializer init = {
        sockid,
        isn - 1, // It's passed in this form to PacketFilter constructor, it should increase it
        isn - 1, // XXX Probably this better be changed.
        plsize
    };

    vector<SrtPacket> provided;

    // Make configuration row-only with size 7
    string conf = "fec,rows:1,cols:7";

    FECFilterBuiltin fec (init, provided, conf);

    // Create 7 packets with random contents

    vector<unique_ptr<CPacket>> source;

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

        // Feed it simultaneously into the sender FEC
        fec.feedSource(p);

        timestamp += 10;
        seq = CSeqNo::incseq(seq);
    }

    // Last step here has moved seq by 7, to the
    // next row. Go back one number because we need
    // to specify the sequence number of the row 0.
    seq = CSeqNo::decseq(seq);

    SrtPacket fec_ctl(SRT_LIVE_MAX_PLSIZE);

    // Use the sequence number of the last packet, as usual.
    bool have_fec_ctl = fec.packControlPacket(fec_ctl, seq);

    EXPECT_EQ(have_fec_ctl, true);

    if (!have_fec_ctl)
        return;

    // By having all packets and FEC CTL packet, now stuff in
    // these packets into the receiver

    FECFilterBuiltin::loss_seqs_t loss; // required as return, ignore

    for (int i = 0; i < 7; ++i)
    {
        // SKIP packet 4 to simulate loss
        if (i == 4)
            continue;

        // Stuff in the packet into the FEC filter
        bool want_passthru = fec.receive(*source[i], loss);
        EXPECT_EQ(want_passthru, true);
    }

    // Prepare a real packet basing on the SrtPacket.

    // XXX Consider packing this into a callable function as this
    // is a code directly copied from PacketFilter::packControlPacket.

    unique_ptr<CPacket> fecpkt ( new CPacket );

    uint32_t* chdr = fecpkt->getHeader();
    memcpy(chdr, fec_ctl.hdr, SRT_PH__SIZE * sizeof(*chdr));

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

    bool want_passthru_fec = fec.receive(*fecpkt, loss);
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
