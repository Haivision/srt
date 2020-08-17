#include <iostream>
#include "gtest/gtest.h"
#include "common.h"

using namespace std;
#include "list.h"

class CSndLossListTest
    : public ::testing::Test
{
protected:
    void SetUp() override
    {
        m_lossList = new CSndLossList(CSndLossListTest::SIZE);
    }

    void TearDown() override
    {
        delete m_lossList;
    }

    void CheckEmptyArray()
    {
        EXPECT_EQ(m_lossList->getLossLength(), 0);
        EXPECT_EQ(m_lossList->popLostSeq(), -1);
    }

    void CleanUpList()
    {
        while (m_lossList->popLostSeq() != -1);
    }

    CSndLossList* m_lossList;

public:
    const int SIZE = 256;
};

/// Check the state of the freshly created list.
/// Capacity, loss length and pop().
TEST_F(CSndLossListTest, Create)
{
    CheckEmptyArray();
}

///////////////////////////////////////////////////////////////////////////////
///
/// The first group of tests checks insert and pop()
///
///////////////////////////////////////////////////////////////////////////////

/// Insert and pop one element from the list.
TEST_F(CSndLossListTest, InsertPopOneElem)
{
    EXPECT_EQ(m_lossList->insert(1, 1), 1);

    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    CheckEmptyArray();
}

/// Insert two elements at once and pop one by one
TEST_F(CSndLossListTest, InsertPopTwoElemsRange)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);

    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    CheckEmptyArray();
}

/// Insert 1 and 4 and pop() one by one
TEST_F(CSndLossListTest, InsertPopTwoElems)
{
    EXPECT_EQ(m_lossList->insert(1, 1), 1);
    EXPECT_EQ(m_lossList->insert(4, 4), 1);

    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 4);
    CheckEmptyArray();
}

/// Insert 1 and 2 and pop() one by one
TEST_F(CSndLossListTest, InsertPopTwoSerialElems)
{
    EXPECT_EQ(m_lossList->insert(1, 1), 1);
    EXPECT_EQ(m_lossList->insert(2, 2), 1);

    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    CheckEmptyArray();
}

/// Insert (1,2) and 4, then pop one by one
TEST_F(CSndLossListTest, InsertPopRangeAndSingle)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 4), 1);

    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 4);
    CheckEmptyArray();
}

/// Insert 1, 4, 2, 0, then pop
TEST_F(CSndLossListTest, InsertPopFourElems)
{
    EXPECT_EQ(m_lossList->insert(1, 1), 1);
    EXPECT_EQ(m_lossList->insert(4, 4), 1);
    EXPECT_EQ(m_lossList->insert(0, 0), 1);
    EXPECT_EQ(m_lossList->insert(2, 2), 1);

    EXPECT_EQ(m_lossList->getLossLength(), 4);
    EXPECT_EQ(m_lossList->popLostSeq(), 0);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 4);
    CheckEmptyArray();
}

/// Insert (1,2) and 4, then pop one by one
TEST_F(CSndLossListTest, InsertCoalesce)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 4), 1);
    EXPECT_EQ(m_lossList->insert(3, 3), 1);

    EXPECT_EQ(m_lossList->getLossLength(), 4);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 4);
    CheckEmptyArray();
}

///////////////////////////////////////////////////////////////////////////////
///
/// The group of tests checks remove() from different positions in the list,
///
///////////////////////////////////////////////////////////////////////////////

///
///
///
TEST_F(CSndLossListTest, BasicRemoveInListNodeHead01)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 4), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    // Remove up to element 4
    m_lossList->removeUpTo(4);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead02)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 5), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->removeUpTo(4);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 5);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead03)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 4), 1);
    EXPECT_EQ(m_lossList->insert(8, 8), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->removeUpTo(4);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead04)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 6), 3);
    EXPECT_EQ(m_lossList->insert(8, 8), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 6);
    m_lossList->removeUpTo(4);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_EQ(m_lossList->popLostSeq(), 5);
    EXPECT_EQ(m_lossList->popLostSeq(), 6);
    EXPECT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead01)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 5), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead02)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 5), 2);
    EXPECT_EQ(m_lossList->insert(8, 8), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead03)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 8), 5);
    EXPECT_EQ(m_lossList->getLossLength(), 7);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_EQ(m_lossList->popLostSeq(), 6);
    EXPECT_EQ(m_lossList->popLostSeq(), 7);
    EXPECT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead04)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 8), 5);
    EXPECT_EQ(m_lossList->insert(10, 12), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 6);
    EXPECT_EQ(m_lossList->popLostSeq(), 6);
    EXPECT_EQ(m_lossList->popLostSeq(), 7);
    EXPECT_EQ(m_lossList->popLostSeq(), 8);
    EXPECT_EQ(m_lossList->popLostSeq(), 10);
    EXPECT_EQ(m_lossList->popLostSeq(), 11);
    EXPECT_EQ(m_lossList->popLostSeq(), 12);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead05)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 8), 5);
    EXPECT_EQ(m_lossList->insert(10, 12), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->removeUpTo(9);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_EQ(m_lossList->popLostSeq(), 10);
    EXPECT_EQ(m_lossList->popLostSeq(), 11);
    EXPECT_EQ(m_lossList->popLostSeq(), 12);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead06)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 8), 5);
    EXPECT_EQ(m_lossList->insert(10, 12), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->removeUpTo(50);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead07)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(4, 8), 5);
    EXPECT_EQ(m_lossList->insert(10, 12), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->removeUpTo(-50);
    EXPECT_EQ(m_lossList->getLossLength(), 10);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 4);
    EXPECT_EQ(m_lossList->popLostSeq(), 5);
    EXPECT_EQ(m_lossList->popLostSeq(), 6);
    EXPECT_EQ(m_lossList->popLostSeq(), 7);
    EXPECT_EQ(m_lossList->popLostSeq(), 8);
    EXPECT_EQ(m_lossList->popLostSeq(), 10);
    EXPECT_EQ(m_lossList->popLostSeq(), 11);
    EXPECT_EQ(m_lossList->popLostSeq(), 12);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead08)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(5, 6), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    m_lossList->removeUpTo(6);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead09)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(5, 6), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    m_lossList->removeUpTo(6);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead10)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(5, 6), 2);
    EXPECT_EQ(m_lossList->insert(10, 10), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    m_lossList->removeUpTo(7);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 10);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead11)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(5, 6), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    m_lossList->removeUpTo(7);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertRemoveInsert01)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->insert(5, 6), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->removeUpTo(5);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    m_lossList->removeUpTo(6);
    EXPECT_EQ(m_lossList->getLossLength(), 0);
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertHead01)
{
    EXPECT_EQ(m_lossList->insert(1, 2), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHead02)
{
    EXPECT_EQ(m_lossList->insert(1, 1), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadIncrease01)
{
    EXPECT_EQ(m_lossList->insert(1, 1), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->insert(2, 2), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 2);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadOverlap01)
{
    EXPECT_EQ(m_lossList->insert(1, 5), 5);
    EXPECT_EQ(m_lossList->getLossLength(), 5);
    EXPECT_EQ(m_lossList->insert(6, 8), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 8);
    EXPECT_EQ(m_lossList->insert(2, 10), 2);
    EXPECT_EQ(m_lossList->getLossLength(), 10);
    for (int i = 1; i < 11; i++)
    {
        EXPECT_EQ(m_lossList->popLostSeq(), i);
        EXPECT_EQ(m_lossList->getLossLength(), 10 - i);
    }

    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadOverlap02)
{
    EXPECT_EQ(m_lossList->insert(1, 5), 5);
    EXPECT_EQ(m_lossList->getLossLength(), 5);
    EXPECT_EQ(m_lossList->insert(6, 8), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 8);
    EXPECT_EQ(m_lossList->insert(2, 7), 0);
    EXPECT_EQ(m_lossList->getLossLength(), 8);
    EXPECT_EQ(m_lossList->insert(5, 5), 0);
    EXPECT_EQ(m_lossList->getLossLength(), 8);

    for (int i = 1; i < 9; i++)
    {
        EXPECT_EQ(m_lossList->popLostSeq(), i);
        EXPECT_EQ(m_lossList->getLossLength(), 8 - i);
    }

    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadNegativeOffset01)
{
    EXPECT_EQ(m_lossList->insert(10000000, 10000000), 1);
    EXPECT_EQ(m_lossList->insert(10000001, 10000001), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 2);

    // The offset of the sequence number being added does not fit
    // into the size of the loss list, it must be ignored.
    // Normally this situation should not happen.
    cerr << "Expecting IPE message:" << endl;
    EXPECT_EQ(m_lossList->insert(1, 1), 0);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 10000000);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 10000001);

    CheckEmptyArray();
}

// Check the part of the loss report the can fit into the list
// goes into the list.
TEST_F(CSndLossListTest, InsertHeadNegativeOffset02)
{
    const int32_t head_seqno = 10000000;
    EXPECT_EQ(m_lossList->insert(head_seqno,     head_seqno), 1);
    EXPECT_EQ(m_lossList->insert(head_seqno + 1, head_seqno + 1), 1);
    EXPECT_EQ(m_lossList->getLossLength(), 2);

    // The offset of the sequence number being added does not fit
    // into the size of the loss list, it must be ignored.
    // Normally this situation should not happen.

    const int32_t outofbound_seqno = head_seqno - CSndLossListTest::SIZE;
    EXPECT_EQ(m_lossList->insert(outofbound_seqno - 1, outofbound_seqno + 1), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 5);
    EXPECT_EQ(m_lossList->popLostSeq(), outofbound_seqno - 1);
    EXPECT_EQ(m_lossList->getLossLength(), 4);
    EXPECT_EQ(m_lossList->popLostSeq(), outofbound_seqno);
    EXPECT_EQ(m_lossList->getLossLength(), 3);
    EXPECT_EQ(m_lossList->popLostSeq(), outofbound_seqno + 1);
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 10000000);
    EXPECT_EQ(m_lossList->getLossLength(), 1);
    EXPECT_EQ(m_lossList->popLostSeq(), 10000001);

    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertFullListCoalesce)
{
    for (int i = 1; i <= CSndLossListTest::SIZE; i++)
        EXPECT_EQ(m_lossList->insert(i, i), 1);
    EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE);
    // Inserting additional element: 1 item more than list size.
    // But given all elements coalesce into one entry, list size should still increase.
    EXPECT_EQ(m_lossList->insert(CSndLossListTest::SIZE + 1, CSndLossListTest::SIZE + 1), 1);
    EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE + 1);
    for (int i = 1; i <= CSndLossListTest::SIZE + 1; i++)
    {
        EXPECT_EQ(m_lossList->popLostSeq(), i);
        EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE + 1 - i);
    }
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    EXPECT_EQ(m_lossList->getLossLength(), 0);

    CheckEmptyArray();
}

TEST_F(CSndLossListTest, DISABLED_InsertFullListNoCoalesce)
{
    // We will insert each element with a gap of one elements.
    // This should lead to having space for only [i; SIZE] sequence numbers.
    for (int i = 1; i <= CSndLossListTest::SIZE / 2; i++)
        EXPECT_EQ(m_lossList->insert(2 * i, 2 * i), 1);

    // At this point the list has every second element empty
    // [0]:taken, [1]: empty, [2]: taken, [3]: empty, ...
    EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE / 2);

    // Inserting additional element: 1 item more than list size.
    // There should be one free place for it at list[SIZE-1]
    // right after previously inserted element.
    const int seqno1 = CSndLossListTest::SIZE + 2;
    EXPECT_EQ(m_lossList->insert(seqno1, seqno1), 1);

    // Inserting one more element into a full list.
    // There should be no place for it.
    const int seqno2 = CSndLossListTest::SIZE + 4;
    EXPECT_EQ(m_lossList->insert(seqno2, seqno2), 0);

    EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE + 1);
    for (int i = 1; i <= CSndLossListTest::SIZE + 1; i++)
    {
        EXPECT_EQ(m_lossList->popLostSeq(), 2 * i);
        EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE  - i);
    }
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    EXPECT_EQ(m_lossList->getLossLength(), 0);

    CheckEmptyArray();
}

TEST_F(CSndLossListTest, DISABLED_InsertFullListNegativeOffset)
{
    for (int i = 10000000; i < 10000000 + CSndLossListTest::SIZE; i++)
        m_lossList->insert(i, i);
    EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE);
    m_lossList->insert(1, CSndLossListTest::SIZE + 1);
    EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE);
    for (int i = 10000000; i < 10000000 + CSndLossListTest::SIZE; i++)
    {
        EXPECT_EQ(m_lossList->popLostSeq(), i);
        EXPECT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE - (i - 10000000 + 1));
    }
    EXPECT_EQ(m_lossList->popLostSeq(), -1);
    EXPECT_EQ(m_lossList->getLossLength(), 0);

    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertNoUpdateElement01)
{
    EXPECT_EQ(m_lossList->insert(0, 1), 2);
    EXPECT_EQ(m_lossList->insert(3, 5), 3);
    m_lossList->removeUpTo(3); // Remove all to seq no 3
    EXPECT_EQ(m_lossList->insert(4, 5), 0); // Element not updated
    EXPECT_EQ(m_lossList->getLossLength(), 2);
    EXPECT_EQ(m_lossList->popLostSeq(), 4);
    EXPECT_EQ(m_lossList->popLostSeq(), 5);
}

TEST_F(CSndLossListTest, InsertNoUpdateElement03)
{
    EXPECT_EQ(m_lossList->insert(1, 5), 5);
    EXPECT_EQ(m_lossList->getLossLength(), 5);
    EXPECT_EQ(m_lossList->insert(6, 8), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 8);
    EXPECT_EQ(m_lossList->insert(2, 5), 0);
    EXPECT_EQ(m_lossList->getLossLength(), 8);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertUpdateElement01)
{
    EXPECT_EQ(m_lossList->insert(1, 5), 5);
    EXPECT_EQ(m_lossList->getLossLength(), 5);
    EXPECT_EQ(m_lossList->insert(1, 8), 3);
    EXPECT_EQ(m_lossList->getLossLength(), 8);
    EXPECT_EQ(m_lossList->insert(2, 5), 0);
    EXPECT_EQ(m_lossList->getLossLength(), 8);
}
