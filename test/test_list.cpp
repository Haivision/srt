#include <iostream>
#include "gtest/gtest.h"
#include "common.h"

using namespace std;

// Ugly hack to access private member ... but non code intrusive.
//#define protected public
//#define private public
#include "list.h"
//#undef protected
//#undef private

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
        ASSERT_EQ(m_lossList->getLossLength(), 0);
        ASSERT_EQ(m_lossList->popLostSeq(), -1);
    }

    void CleanUpList()
    {
        while (m_lossList->popLostSeq() != -1);
    }

    CSndLossList *m_lossList;

public:
    static const int SIZE = 256;
};

/// Check the state of the freshly created list.
/// Capacity, loss length and pop().
TEST_F(CSndLossListTest, Create)
{
    EXPECT_EQ(m_lossList->capacity(), CSndLossListTest::SIZE);
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
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 4);
    ASSERT_EQ(m_lossList->getLossLength(), 3);
    m_lossList->remove(4);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead02)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 5);
    ASSERT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->remove(4);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 5);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead03)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 4);
    m_lossList->insert(8, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->remove(4);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead04)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 6);
    m_lossList->insert(8, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 6);
    m_lossList->remove(4);
    ASSERT_EQ(m_lossList->getLossLength(), 3);
    ASSERT_EQ(m_lossList->popLostSeq(), 5);
    ASSERT_EQ(m_lossList->popLostSeq(), 6);
    ASSERT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead01)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 5);
    ASSERT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead02)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 5);
    m_lossList->insert(8, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead03)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 7);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 3);
    ASSERT_EQ(m_lossList->popLostSeq(), 6);
    ASSERT_EQ(m_lossList->popLostSeq(), 7);
    ASSERT_EQ(m_lossList->popLostSeq(), 8);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead04)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 8);
    m_lossList->insert(10, 12);
    ASSERT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 6);
    ASSERT_EQ(m_lossList->popLostSeq(), 6);
    ASSERT_EQ(m_lossList->popLostSeq(), 7);
    ASSERT_EQ(m_lossList->popLostSeq(), 8);
    ASSERT_EQ(m_lossList->popLostSeq(), 10);
    ASSERT_EQ(m_lossList->popLostSeq(), 11);
    ASSERT_EQ(m_lossList->popLostSeq(), 12);
    CheckEmptyArray();
}


TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead05)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 8);
    m_lossList->insert(10, 12);
    ASSERT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->remove(9);
    ASSERT_EQ(m_lossList->getLossLength(), 3);
    ASSERT_EQ(m_lossList->popLostSeq(), 10);
    ASSERT_EQ(m_lossList->popLostSeq(), 11);
    ASSERT_EQ(m_lossList->popLostSeq(), 12);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead06)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 8);
    m_lossList->insert(10, 12);
    ASSERT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->remove(50);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead07)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(4, 8);
    m_lossList->insert(10, 12);
    ASSERT_EQ(m_lossList->getLossLength(), 10);
    m_lossList->remove(-50);
    ASSERT_EQ(m_lossList->getLossLength(), 10);
    ASSERT_EQ(m_lossList->popLostSeq(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 2);
    ASSERT_EQ(m_lossList->popLostSeq(), 4);
    ASSERT_EQ(m_lossList->popLostSeq(), 5);
    ASSERT_EQ(m_lossList->popLostSeq(), 6);
    ASSERT_EQ(m_lossList->popLostSeq(), 7);
    ASSERT_EQ(m_lossList->popLostSeq(), 8);
    ASSERT_EQ(m_lossList->popLostSeq(), 10);
    ASSERT_EQ(m_lossList->popLostSeq(), 11);
    ASSERT_EQ(m_lossList->popLostSeq(), 12);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead08)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(5, 6);
    ASSERT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    m_lossList->remove(6);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead09)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(5, 6);
    ASSERT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    m_lossList->insert(1, 2);
    m_lossList->remove(6);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead10)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(5, 6);
    m_lossList->insert(10, 10);
    ASSERT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 2);
    m_lossList->insert(1, 2);
    m_lossList->remove(7);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 10);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead11)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(5, 6);
    ASSERT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    m_lossList->insert(1, 2);
    m_lossList->remove(7);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertRemoveInsert01)
{
    m_lossList->insert(1, 2);
    m_lossList->insert(5, 6);
    ASSERT_EQ(m_lossList->getLossLength(), 4);
    m_lossList->remove(5);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    m_lossList->insert(1, 2);
    m_lossList->remove(6);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertHead01)
{
    m_lossList->insert(1, 2);
    ASSERT_EQ(m_lossList->getLossLength(), 2);
    ASSERT_EQ(m_lossList->popLostSeq(), 1);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 2);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHead02)
{
    m_lossList->insert(1, 1);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 1);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadIncrease01)
{
    m_lossList->insert(1, 1);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    m_lossList->insert(2, 2);
    ASSERT_EQ(m_lossList->getLossLength(), 2);
    ASSERT_EQ(m_lossList->popLostSeq(), 1);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 2);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadOverlap01)
{
    m_lossList->insert(1, 5);
    ASSERT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->insert(6, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 8);
    m_lossList->insert(2, 10);
    ASSERT_EQ(m_lossList->getLossLength(), 10);
    for (int i=1; i<11; i++)
    {
        ASSERT_EQ(m_lossList->popLostSeq(), i);
        ASSERT_EQ(m_lossList->getLossLength(), 10-i);
    }
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    ASSERT_EQ(m_lossList->getLossLength(), 0);

    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadOverlap02)
{
    m_lossList->insert(1, 5);
    ASSERT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->insert(6, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 8);
    m_lossList->insert(2, 7);

    ASSERT_EQ(m_lossList->getLossLength(), 8);
    for (int i=1; i<9; i++)
    {
        ASSERT_EQ(m_lossList->popLostSeq(), i);
        ASSERT_EQ(m_lossList->getLossLength(), 8-i);
    }
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    ASSERT_EQ(m_lossList->getLossLength(), 0);

    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertHeadNegativeOffset01)
{
    m_lossList->insert(10000000, 10000000);
    m_lossList->insert(10000001, 10000001);
    ASSERT_EQ(m_lossList->getLossLength(), 2);
    m_lossList->insert(1, 1);
    ASSERT_EQ(m_lossList->getLossLength(), 3);
    ASSERT_EQ(m_lossList->popLostSeq(), 1);
    ASSERT_EQ(m_lossList->getLossLength(), 2);
    ASSERT_EQ(m_lossList->popLostSeq(), 10000000);
    ASSERT_EQ(m_lossList->getLossLength(), 1);
    ASSERT_EQ(m_lossList->popLostSeq(), 10000001);
    ASSERT_EQ(m_lossList->getLossLength(), 0);
    ASSERT_EQ(m_lossList->popLostSeq(), -1);

    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertFullList)
{
    for (int i=1; i<= CSndLossListTest::SIZE; i++)
        m_lossList->insert(i,i);
    ASSERT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE);
    m_lossList->insert(CSndLossListTest::SIZE+1, CSndLossListTest::SIZE+1);
    ASSERT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE);
    for (int i=1; i<=CSndLossListTest::SIZE; i++)
    {
        ASSERT_EQ(m_lossList->popLostSeq(), i);
        ASSERT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE - i);
    }
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    ASSERT_EQ(m_lossList->getLossLength(), 0);

    CheckEmptyArray();
}

TEST_F(CSndLossListTest, InsertFullListNegativeOffset)
{
    for (int i=10000000; i< 10000000+CSndLossListTest::SIZE; i++)
        m_lossList->insert(i,i);
    ASSERT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE);
    m_lossList->insert(1, CSndLossListTest::SIZE+1);
    ASSERT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE);
    for (int i=10000000; i< 10000000+CSndLossListTest::SIZE; i++)
    {
        ASSERT_EQ(m_lossList->popLostSeq(), i);
        ASSERT_EQ(m_lossList->getLossLength(), CSndLossListTest::SIZE - (i-10000000+1));
    }
    ASSERT_EQ(m_lossList->popLostSeq(), -1);
    ASSERT_EQ(m_lossList->getLossLength(), 0);

    CheckEmptyArray();
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertNoUpdateElement01)
{
    m_lossList->insert(0, 1);
    m_lossList->insert(3, 5);
    m_lossList->remove(3); // Remove all to seq no 3
    ASSERT_EQ(m_lossList->insert(4, 5), 0); // Element not updated
    ASSERT_EQ(m_lossList->getLossLength(), 2);
    ASSERT_EQ(m_lossList->popLostSeq(), 4);
    ASSERT_EQ(m_lossList->popLostSeq(), 5);
}

//TEST_F(CSndLossListTest, InsertNoUpdateElement02)
//{
//    m_lossList->insert(0, 0);
//    m_lossList->insert(2, 2);
//    m_lossList->insert(4, 5);
//    m_lossList->remove(2);
//
//    // This use case was created to hit uncovered code. The code comes from legacy version.
//    // It's not easy to change the structure so that the test hits the code
//    // Thus we change the structure manually
//
//    // Standard insert will place the value at idx (seqno1 + 1)
//    // Insert data manually so that idx == seqno1
//    // m_lossList->insert(2, 3);
//    m_lossList->m_caSeq[0].next = 2;
//    m_lossList->m_caSeq[2].data1 = 2;
//    m_lossList->m_caSeq[2].data2 = 3;
//    m_lossList->m_iLength += 2;
//
//    ASSERT_EQ(m_lossList->insert(2, 3), 0); // Element not updated
//
//    ASSERT_EQ(m_lossList->getLossLength(), 3);
//    ASSERT_EQ(m_lossList->popLostSeq(), 0);
//    ASSERT_EQ(m_lossList->popLostSeq(), 2);
//    ASSERT_EQ(m_lossList->popLostSeq(), 3);
//}

TEST_F(CSndLossListTest, InsertNoUpdateElement03)
{
    m_lossList->insert(1, 5);
    ASSERT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->insert(6, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 8);
    ASSERT_EQ(m_lossList->insert(2, 5), 0);
    ASSERT_EQ(m_lossList->getLossLength(), 8);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertUpdateElement01)
{
    m_lossList->insert(1, 5);
    ASSERT_EQ(m_lossList->getLossLength(), 5);
    m_lossList->insert(1, 8);
    ASSERT_EQ(m_lossList->getLossLength(), 8);
    ASSERT_EQ(m_lossList->insert(2, 5), 0);
    ASSERT_EQ(m_lossList->getLossLength(), 8);
}

//TEST_F(CSndLossListTest, InsertUpdateElement02)
//{
//    m_lossList->insert(0, 0);
//
//    // see InsertNoUpdateElement02 for details
//    m_lossList->m_caSeq[0].next = 2;
//    m_lossList->m_caSeq[2].data1 = 2;
//    m_lossList->m_caSeq[2].data2 = 3;
//    m_lossList->m_iLength += 2;
//
//    ASSERT_EQ(m_lossList->insert(2, 4), 1); // Element should be updated
//
//    ASSERT_EQ(m_lossList->getLossLength(), 4);
//    ASSERT_EQ(m_lossList->popLostSeq(), 0);
//    ASSERT_EQ(m_lossList->popLostSeq(), 2);
//    ASSERT_EQ(m_lossList->popLostSeq(), 3);
//    ASSERT_EQ(m_lossList->popLostSeq(), 4);
//}

//TEST_F(CSndLossListTest, InsertUpdateLowLevel)
//{
//    ASSERT_EQ(m_lossList->update_element(0, -1, -1), false);
//}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
//TEST_F(CSndLossListTest, InsertCorruptionUseCase)
//{
//    // Fill the structure
//    for (int i=0; i < CSndLossListTest::SIZE; i++)
//        m_lossList->insert(i,i);
//
//    // Corrupt structure by introducing an infinite loop ...
//    m_lossList->m_caSeq[CSndLossListTest::SIZE-1].next = CSndLossListTest::SIZE-1;
//
//    // next insert shoud not loop forever ...
//    m_lossList->insert(CSndLossListTest::SIZE-1,CSndLossListTest::SIZE-1);
//}
