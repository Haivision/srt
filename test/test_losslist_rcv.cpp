#include <iostream>
#include "gtest/gtest.h"
#include "common.h"
#include "list.h"

using namespace std;
using namespace srt;

class CRcvLossListTest
    : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_lossList = new CRcvLossList(CRcvLossListTest::SIZE);
    }

    void TearDown() override
    {
        delete m_lossList;
    }

    void CheckEmptyArray()
    {
        EXPECT_EQ(m_lossList->getLossLength(), 0);
        EXPECT_EQ(m_lossList->getFirstLostSeq(), SRT_SEQNO_NONE);
    }

    void CleanUpList()
    {
        //while (m_lossList->popLostSeq() != -1);
    }

    CRcvLossList* m_lossList;

public:
    const int SIZE = 256;
};

/// Check the state of the freshly created list.
/// Capacity, loss length and pop().
TEST_F(CRcvLossListTest, Create)
{
    CheckEmptyArray();
}

///////////////////////////////////////////////////////////////////////////////
///
/// The first group of tests checks insert and pop()
///
///////////////////////////////////////////////////////////////////////////////

/// Insert and remove one element from the list.
TEST_F(CRcvLossListTest, InsertRemoveOneElem)
{
    EXPECT_EQ(m_lossList->insert(1, 1), 1);

    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_TRUE(m_lossList->remove(1, 1));
    CheckEmptyArray();
}


/// Insert and pop one element from the list.
TEST_F(CRcvLossListTest, InsertTwoElemsEdge)
{
    EXPECT_EQ(m_lossList->insert(CSeqNo::m_iMaxSeqNo, 1), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_TRUE(m_lossList->remove(CSeqNo::m_iMaxSeqNo, 1));
    CheckEmptyArray();
}

TEST(CRcvFreshLossListTest, CheckFreshLossList)
{
    std::deque<CRcvFreshLoss> floss {
        CRcvFreshLoss (10, 15, 5),
        CRcvFreshLoss (25, 29, 10),
        CRcvFreshLoss (30, 30, 3),
        CRcvFreshLoss (45, 80, 100)
    };

    EXPECT_EQ(floss.size(), 4);

    // Ok, now let's do element removal

    int had_ttl = 0;
    bool rm = CRcvFreshLoss::removeOne((floss), 26, &had_ttl);

    EXPECT_EQ(rm, true);
    EXPECT_EQ(had_ttl, 10);
    EXPECT_EQ(floss.size(), 5);

    // Now we expect to have [10-15] [25-25] [27-35]...
    // After revoking 25 it should have removed it.

    // SPLIT
    rm = CRcvFreshLoss::removeOne((floss), 27, &had_ttl);
    EXPECT_EQ(rm, true);
    EXPECT_EQ(had_ttl, 10);
    EXPECT_EQ(floss.size(), 5);

    // STRIP
    rm = CRcvFreshLoss::removeOne((floss), 28, &had_ttl);
    EXPECT_EQ(rm, true);
    EXPECT_EQ(had_ttl, 10);
    EXPECT_EQ(floss.size(), 5);

    // DELETE
    rm = CRcvFreshLoss::removeOne((floss), 25, &had_ttl);
    EXPECT_EQ(rm, true);
    EXPECT_EQ(had_ttl, 10);
    EXPECT_EQ(floss.size(), 4);

    // SPLIT
    rm = CRcvFreshLoss::removeOne((floss), 50, &had_ttl);
    EXPECT_EQ(rm, true);
    EXPECT_EQ(had_ttl, 100);
    EXPECT_EQ(floss.size(), 5);

    // DELETE
    rm = CRcvFreshLoss::removeOne((floss), 30, &had_ttl);
    EXPECT_EQ(rm, true);
    EXPECT_EQ(had_ttl, 3);
    EXPECT_EQ(floss.size(), 4);

    // Remove nonexistent sequence, but existing before.
    rm = CRcvFreshLoss::removeOne((floss), 25, NULL);
    EXPECT_EQ(rm, false);
    EXPECT_EQ(floss.size(), 4);

    // Remove nonexistent sequence that didn't exist before.
    rm = CRcvFreshLoss::removeOne((floss), 31, &had_ttl);
    EXPECT_EQ(rm, false);
    EXPECT_EQ(had_ttl, 0);
    EXPECT_EQ(floss.size(), 4);

}
