#include "gtest/gtest.h"
#include "common.h"
#include "core.h"

using namespace srt;


const int32_t CSeqNo::m_iSeqNoTH;
const int32_t CSeqNo::m_iMaxSeqNo;


TEST(CSeqNo, constants)
{
    // Compare two seq#, considering the wraping.
    EXPECT_EQ(CSeqNo::m_iMaxSeqNo, 0x7FFFFFFF);
    EXPECT_EQ(CSeqNo::m_iSeqNoTH,  0x3FFFFFFF);
}


TEST(CSeqNo, seqcmp)
{
    // Compare two seq#, considering the wraping.
    EXPECT_EQ(CSeqNo::seqcmp(0x7FFFFFFF, 0x7FFFFFFF), 0);

    // abs(seq1 - seq2) < 0x3FFFFFFF : seq1 - seq2
    EXPECT_EQ(CSeqNo::seqcmp(128, 1),  127);
    EXPECT_EQ(CSeqNo::seqcmp(1, 128), -127);

    // abs(seq1 - seq2) >= 0x3FFFFFFF : seq2 - seq1
    EXPECT_EQ(CSeqNo::seqcmp(0x7FFFFFFF, 1), int(0x80000002));   // -2147483646
    EXPECT_EQ(CSeqNo::seqcmp(1, 0x7FFFFFFF), 0x7FFFFFFE);   //  2147483646
}


TEST(CSeqNo, seqoff)
{
    // seqoff: offset from the 2nd to the 1st seq#
    EXPECT_EQ(CSeqNo::seqoff(0x7FFFFFFF, 0x7FFFFFFF), 0);

    // distance(seq2 - seq1)
    EXPECT_EQ(CSeqNo::seqoff(125,        1), -124);

    EXPECT_EQ(CSeqNo::seqoff(1, 0x7FFFFFFF),   -2);
    EXPECT_EQ(CSeqNo::seqoff(0x7FFFFFFF, 1),    2);
}

TEST(CSeqNo, seqlen)
{
    EXPECT_EQ(CSeqNo::seqlen(125, 125), 1);
    EXPECT_EQ(CSeqNo::seqlen(125, 126), 2);

    EXPECT_EQ(CSeqNo::seqlen(2147483647, 0), 2);
    EXPECT_EQ(CSeqNo::seqlen(0, 2147483647), 2147483648);

    // seq2 %> seq1, but the distance exceeds m_iSeqNoTH = 0x3FFFFFFF.
    //EXPECT_TRUE(CSeqNo::seqlen(0, 0x3FFFFFFF + 10) > 0x3FFFFFFF);
    //EXPECT_TRUE(CSeqNo::seqlen(0x7FFFFFFF, 0x3FFFFFFF + 10) > 0x3FFFFFFF);

    // seq2 <% seq1, but the distance exceeds m_iSeqNoTH = 0x3FFFFFFF.
    //EXPECT_EQ(CSeqNo::seqlen(0x3FFFFFFF + 10, 0), 2);
    //EXPECT_EQ(CSeqNo::seqlen(0x7FFFFFFF, 0x3FFFFFFF + 10), 2);
}

enum class pkt_validity
{
    descrepancy = -1,
    ok = 0,
    behind = 1,
    ahead = 2
};

const char* to_cstr(pkt_validity val)
{
    switch (val)
    {
    case pkt_validity::descrepancy:
        return "DESCREPANCY";
    case pkt_validity::ok:
        return "OK";
    case pkt_validity::behind:
        return "BEHIND";
    case pkt_validity::ahead:
        return "AHEAD";
    default:
        break;
    }

    return "INVAL";
}


pkt_validity ValidateSeqno(int base_seqno, int pkt_seqno)
{
    const uint32_t seqlen_ahead  = CSeqNo::seqlen(base_seqno, pkt_seqno);
    std::cout << "SeqLen ahead: " << seqlen_ahead << std::endl;
    if (seqlen_ahead == 2)
        return pkt_validity::ok;

    if (seqlen_ahead <= 1)
        return pkt_validity::behind;

    if (seqlen_ahead < CSeqNo::m_iSeqNoTH) {
        return pkt_validity::ahead;
    }

    const uint32_t seqlen_behind = CSeqNo::seqlen(pkt_seqno, base_seqno);
    std::cout << "SeqLen behind: " << seqlen_behind << std::endl;

    if (seqlen_behind < CSeqNo::m_iSeqNoTH / 2)
        return pkt_validity::behind;

    return pkt_validity::descrepancy;
}

TEST(CSeqNo, Descrepancy)
{
    EXPECT_EQ(ValidateSeqno(       125,  124), pkt_validity::behind);
    EXPECT_EQ(ValidateSeqno(       125,  125), pkt_validity::behind);
    EXPECT_EQ(ValidateSeqno(       125,  126), pkt_validity::ok);
    EXPECT_EQ(ValidateSeqno(0x7FFFFFFF,    0), pkt_validity::ok);
    EXPECT_EQ(ValidateSeqno(0x7FFFFFFF,    1), pkt_validity::ahead);
    EXPECT_EQ(ValidateSeqno(         0, 0x7FFFFFFF), pkt_validity::behind);

    // pkt_seqno is ahead and descrepancy
    EXPECT_EQ(ValidateSeqno(0, 0x3FFFFFFF + 10), pkt_validity::descrepancy);
    EXPECT_EQ(ValidateSeqno(0x3FFFFFFF - 10, 0x7FFFFFFF), pkt_validity::descrepancy);
    
}

TEST(CUDT, getFlightSpan)
{
    const int test_values[3][3] = {
        // lastack  curseq  span
        {      125,    124,   0 }, // all sent packets are acknowledged
        {      125,    125,   1 },
        {      125,    130,   6 }
    };

    for (auto val : test_values)
    {
        EXPECT_EQ(CUDT::getFlightSpan(val[0], val[1]), val[2]) << "Span(" << val[0] << ", " << val[1] << ")";
    }
}

TEST(CSeqNo, incseq)
{
    // incseq: increase the seq# by 1
    EXPECT_EQ(CSeqNo::incseq(1),                   2);
    EXPECT_EQ(CSeqNo::incseq(125),               126);
    EXPECT_EQ(CSeqNo::incseq(0x7FFFFFFF),          0);
    EXPECT_EQ(CSeqNo::incseq(0x3FFFFFFF), 0x40000000);
}


TEST(CSeqNo, decseq)
{
    // decseq: decrease the seq# by 1
    EXPECT_EQ(CSeqNo::decseq(1),                   0);
    EXPECT_EQ(CSeqNo::decseq(125),               124);
    EXPECT_EQ(CSeqNo::decseq(0),          0x7FFFFFFF);
    EXPECT_EQ(CSeqNo::decseq(0x40000000), 0x3FFFFFFF);
}


TEST(CSeqNo, incseqint)
{
    // incseq: increase the seq# by 1
    EXPECT_EQ(CSeqNo::incseq(1, 1),                   2);
    EXPECT_EQ(CSeqNo::incseq(125, 1),               126);
    EXPECT_EQ(CSeqNo::incseq(0x7FFFFFFF, 1),          0);
    EXPECT_EQ(CSeqNo::incseq(0x3FFFFFFF, 1), 0x40000000);

    EXPECT_EQ(CSeqNo::incseq(0x3FFFFFFF, 0x3FFFFFFF), 0x7FFFFFFE);
    EXPECT_EQ(CSeqNo::incseq(0x3FFFFFFF, 0x40000000), 0x7FFFFFFF);
    EXPECT_EQ(CSeqNo::incseq(0x3FFFFFFF, 0x40000001), 0x00000000);
}


TEST(CSeqNo, decseqint)
{
    // decseq: decrease the seq# by 1
    EXPECT_EQ(CSeqNo::decseq(1, 1),                   0);
    EXPECT_EQ(CSeqNo::decseq(125, 1),               124);
    EXPECT_EQ(CSeqNo::decseq(0, 1),          0x7FFFFFFF);
    EXPECT_EQ(CSeqNo::decseq(0x40000000, 1), 0x3FFFFFFF);
}

