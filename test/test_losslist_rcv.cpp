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
