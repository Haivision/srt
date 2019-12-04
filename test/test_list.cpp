#include <iostream>
#include "gtest/gtest.h"
#include "common.h"

using namespace std;

// Ugly hack to access private member ... but non code intrusive.
#define protected public
#define private public
#include "list.h"
#undef protected
#undef private

class CSndLossListTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        pt = new CSndLossList(CSndLossListTest::SIZE);
    }

    void TearDown() override
    {
        delete pt;
    }

    void CheckEmptyArray(CSndLossList *pt, int size)
    {
        ASSERT_EQ(pt->m_iLength, 0);
        ASSERT_EQ(pt->getLossLength(), 0);
        for (int i = 0; i < size; i++)
        {
            ASSERT_EQ(pt->m_caSeq[i].data1, -1) << " i is " << i << " ; m_iHead is " << pt->m_iHead << endl;
            ASSERT_EQ(pt->m_caSeq[i].data2, -1) << " i is " << i << " ; m_iHead is " << pt->m_iHead << endl;
        }
    }

    void CleanUpList(CSndLossList *pt)
    {
        while (pt->popLostSeq() != -1);
    }

    CSndLossList *pt;

public:
    static const int SIZE;
};
const int CSndLossListTest::SIZE = 256;

TEST_F(CSndLossListTest, Create)
{
    ASSERT_EQ(pt->m_iHead, -1);
    ASSERT_EQ(pt->m_iSize, CSndLossListTest::SIZE);
    ASSERT_EQ(pt->m_iLastInsertPos, -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
}

TEST_F(CSndLossListTest, BasicInsertRemove)
{
    pt->insert(1, 1);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->remove(1);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveHead01)
{
    pt->insert(1, 1);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->remove(1);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveHead02)
{
    pt->insert(1, 2);
    ASSERT_EQ(pt->getLossLength(), 2);
    pt->remove(1);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->remove(2);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveHead03)
{
    pt->insert(1, 1);
    pt->insert(4, 4);
    ASSERT_EQ(pt->getLossLength(), 2);
    pt->remove(1);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 4);
    ASSERT_EQ(pt->getLossLength(), 0);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveHead04)
{
    pt->insert(1, 2);
    pt->insert(4, 4);
    ASSERT_EQ(pt->getLossLength(), 3);
    pt->remove(1);
    ASSERT_EQ(pt->getLossLength(), 2);
    pt->remove(2);
    ASSERT_EQ(pt->popLostSeq(), 4);
    ASSERT_EQ(pt->getLossLength(), 0);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead01)
{
    pt->insert(1, 2);
    pt->insert(4, 4);
    ASSERT_EQ(pt->getLossLength(), 3);
    pt->remove(4);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead02)
{
    pt->insert(1, 2);
    pt->insert(4, 5);
    ASSERT_EQ(pt->getLossLength(), 4);
    pt->remove(4);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 5);
    ASSERT_EQ(pt->getLossLength(), 0);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead03)
{
    pt->insert(1, 2);
    pt->insert(4, 4);
    pt->insert(8, 8);
    ASSERT_EQ(pt->getLossLength(), 4);
    pt->remove(4);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 8);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNodeHead04)
{
    pt->insert(1, 2);
    pt->insert(4, 6);
    pt->insert(8, 8);
    ASSERT_EQ(pt->getLossLength(), 6);
    pt->remove(4);
    ASSERT_EQ(pt->getLossLength(), 3);
    ASSERT_EQ(pt->popLostSeq(), 5);
    ASSERT_EQ(pt->popLostSeq(), 6);
    ASSERT_EQ(pt->popLostSeq(), 8);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead01)
{
    pt->insert(1, 2);
    pt->insert(4, 5);
    ASSERT_EQ(pt->getLossLength(), 4);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead02)
{
    pt->insert(1, 2);
    pt->insert(4, 5);
    pt->insert(8, 8);
    ASSERT_EQ(pt->getLossLength(), 5);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 8);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead03)
{
    pt->insert(1, 2);
    pt->insert(4, 8);
    ASSERT_EQ(pt->getLossLength(), 7);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 3);
    ASSERT_EQ(pt->popLostSeq(), 6);
    ASSERT_EQ(pt->popLostSeq(), 7);
    ASSERT_EQ(pt->popLostSeq(), 8);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead04)
{
    pt->insert(1, 2);
    pt->insert(4, 8);
    pt->insert(10, 12);
    ASSERT_EQ(pt->getLossLength(), 10);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 6);
    ASSERT_EQ(pt->popLostSeq(), 6);
    ASSERT_EQ(pt->popLostSeq(), 7);
    ASSERT_EQ(pt->popLostSeq(), 8);
    ASSERT_EQ(pt->popLostSeq(), 10);
    ASSERT_EQ(pt->popLostSeq(), 11);
    ASSERT_EQ(pt->popLostSeq(), 12);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}


TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead05)
{
    pt->insert(1, 2);
    pt->insert(4, 8);
    pt->insert(10, 12);
    ASSERT_EQ(pt->getLossLength(), 10);
    pt->remove(9);
    ASSERT_EQ(pt->getLossLength(), 3);
    ASSERT_EQ(pt->popLostSeq(), 10);
    ASSERT_EQ(pt->popLostSeq(), 11);
    ASSERT_EQ(pt->popLostSeq(), 12);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead06)
{
    pt->insert(1, 2);
    pt->insert(4, 8);
    pt->insert(10, 12);
    ASSERT_EQ(pt->getLossLength(), 10);
    pt->remove(50);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead07)
{
    pt->insert(1, 2);
    pt->insert(4, 8);
    pt->insert(10, 12);
    ASSERT_EQ(pt->getLossLength(), 10);
    pt->remove(-50);
    ASSERT_EQ(pt->getLossLength(), 10);
    ASSERT_EQ(pt->popLostSeq(), 1);
    ASSERT_EQ(pt->popLostSeq(), 2);
    ASSERT_EQ(pt->popLostSeq(), 4);
    ASSERT_EQ(pt->popLostSeq(), 5);
    ASSERT_EQ(pt->popLostSeq(), 6);
    ASSERT_EQ(pt->popLostSeq(), 7);
    ASSERT_EQ(pt->popLostSeq(), 8);
    ASSERT_EQ(pt->popLostSeq(), 10);
    ASSERT_EQ(pt->popLostSeq(), 11);
    ASSERT_EQ(pt->popLostSeq(), 12);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead08)
{
    pt->insert(1, 2);
    pt->insert(5, 6);
    ASSERT_EQ(pt->getLossLength(), 4);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->remove(6);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead09)
{
    pt->insert(1, 2);
    pt->insert(5, 6);
    ASSERT_EQ(pt->getLossLength(), 4);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->insert(1, 2);
    pt->remove(6);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead10)
{
    pt->insert(1, 2);
    pt->insert(5, 6);
    pt->insert(10, 10);
    ASSERT_EQ(pt->getLossLength(), 5);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 2);
    pt->insert(1, 2);
    pt->remove(7);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 10);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, BasicRemoveInListNotInNodeHead11)
{
    pt->insert(1, 2);
    pt->insert(5, 6);
    ASSERT_EQ(pt->getLossLength(), 4);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->insert(1, 2);
    pt->remove(7);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertRemoveInsert01)
{
    pt->insert(1, 2);
    pt->insert(5, 6);
    ASSERT_EQ(pt->getLossLength(), 4);
    pt->remove(5);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->insert(1, 2);
    pt->remove(6);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertHead01)
{
    pt->insert(1, 2);
    ASSERT_EQ(pt->getLossLength(), 2);
    ASSERT_EQ(pt->popLostSeq(), 1);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 2);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, InsertHead02)
{
    pt->insert(1, 1);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 1);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, InsertHeadIncrease01)
{
    pt->insert(1, 1);
    ASSERT_EQ(pt->getLossLength(), 1);
    pt->insert(2, 2);
    ASSERT_EQ(pt->getLossLength(), 2);
    ASSERT_EQ(pt->popLostSeq(), 1);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 2);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);
    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, InsertHeadOverlap01)
{
    pt->insert(1, 5);
    ASSERT_EQ(pt->getLossLength(), 5);
    pt->insert(6, 8);
    ASSERT_EQ(pt->getLossLength(), 8);
    pt->insert(2, 10);
    ASSERT_EQ(pt->getLossLength(), 10);
    for (int i=1; i<11; i++)
    {
        ASSERT_EQ(pt->popLostSeq(), i);
        ASSERT_EQ(pt->getLossLength(), 10-i);
    }
    ASSERT_EQ(pt->popLostSeq(), -1);
    ASSERT_EQ(pt->getLossLength(), 0);

    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, InsertHeadOverlap02)
{
    pt->insert(1, 5);
    ASSERT_EQ(pt->getLossLength(), 5);
    pt->insert(6, 8);
    ASSERT_EQ(pt->getLossLength(), 8);
    pt->insert(2, 7);

    ASSERT_EQ(pt->getLossLength(), 8);
    for (int i=1; i<9; i++)
    {
        ASSERT_EQ(pt->popLostSeq(), i);
        ASSERT_EQ(pt->getLossLength(), 8-i);
    }
    ASSERT_EQ(pt->popLostSeq(), -1);
    ASSERT_EQ(pt->getLossLength(), 0);

    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, InsertHeadNegativeOffset01)
{
    pt->insert(10000000, 10000000);
    pt->insert(10000001, 10000001);
    ASSERT_EQ(pt->getLossLength(), 2);
    pt->insert(1, 1);
    ASSERT_EQ(pt->getLossLength(), 3);
    ASSERT_EQ(pt->popLostSeq(), 1);
    ASSERT_EQ(pt->getLossLength(), 2);
    ASSERT_EQ(pt->popLostSeq(), 10000000);
    ASSERT_EQ(pt->getLossLength(), 1);
    ASSERT_EQ(pt->popLostSeq(), 10000001);
    ASSERT_EQ(pt->getLossLength(), 0);
    ASSERT_EQ(pt->popLostSeq(), -1);

    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertFullList)
{
    for (int i=1; i<= CSndLossListTest::SIZE; i++)
        pt->insert(i,i);
    ASSERT_EQ(pt->getLossLength(), CSndLossListTest::SIZE);
    pt->insert(CSndLossListTest::SIZE+1, CSndLossListTest::SIZE+1);
    ASSERT_EQ(pt->getLossLength(), CSndLossListTest::SIZE);
    for (int i=1; i<=CSndLossListTest::SIZE; i++)
    {
        ASSERT_EQ(pt->popLostSeq(), i);
        ASSERT_EQ(pt->getLossLength(), CSndLossListTest::SIZE - i);
    }
    ASSERT_EQ(pt->popLostSeq(), -1);
    ASSERT_EQ(pt->getLossLength(), 0);

    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

TEST_F(CSndLossListTest, InsertFullListNegativeOffset)
{
    for (int i=10000000; i< 10000000+CSndLossListTest::SIZE; i++)
        pt->insert(i,i);
    ASSERT_EQ(pt->getLossLength(), CSndLossListTest::SIZE);
    pt->insert(1, CSndLossListTest::SIZE+1);
    ASSERT_EQ(pt->getLossLength(), CSndLossListTest::SIZE);
    for (int i=10000000; i< 10000000+CSndLossListTest::SIZE; i++)
    {
        ASSERT_EQ(pt->popLostSeq(), i);
        ASSERT_EQ(pt->getLossLength(), CSndLossListTest::SIZE - (i-10000000+1));
    }
    ASSERT_EQ(pt->popLostSeq(), -1);
    ASSERT_EQ(pt->getLossLength(), 0);

    CheckEmptyArray(pt, CSndLossListTest::SIZE);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertNoUpdateElement01)
{
    pt->insert(0, 1);
    pt->insert(3, 5);
    pt->remove(3); // Remove all to seq no 3
    ASSERT_EQ(pt->insert(4, 5), 0); // Element not updated
    ASSERT_EQ(pt->getLossLength(), 2);
    ASSERT_EQ(pt->popLostSeq(), 4);
    ASSERT_EQ(pt->popLostSeq(), 5);
}

TEST_F(CSndLossListTest, InsertNoUpdateElement02)
{
    pt->insert(0, 0);

    // This use case was created to hit uncovered code. The code comes from legacy version.
    // It's not easy to change the structure so that the test hits the code
    // Thus we change the structure manually

    // Standard insert will place the value at idx (seqno1 + 1)
    // Insert data manually so that idx == seqno1
    // pt->insert(2, 3);
    pt->m_caSeq[0].next = 2;
    pt->m_caSeq[2].data1 = 2;
    pt->m_caSeq[2].data2 = 3;
    pt->m_iLength += 2;

    ASSERT_EQ(pt->insert(2, 3), 0); // Element not updated

    ASSERT_EQ(pt->getLossLength(), 3);
    ASSERT_EQ(pt->popLostSeq(), 0);
    ASSERT_EQ(pt->popLostSeq(), 2);
    ASSERT_EQ(pt->popLostSeq(), 3);
}

TEST_F(CSndLossListTest, InsertNoUpdateElement03)
{
    pt->insert(1, 5);
    ASSERT_EQ(pt->getLossLength(), 5);
    pt->insert(6, 8);
    ASSERT_EQ(pt->getLossLength(), 8);
    ASSERT_EQ(pt->insert(2, 5), 0);
    ASSERT_EQ(pt->getLossLength(), 8);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertUpdateElement01)
{
    pt->insert(1, 5);
    ASSERT_EQ(pt->getLossLength(), 5);
    pt->insert(1, 8);
    ASSERT_EQ(pt->getLossLength(), 8);
    ASSERT_EQ(pt->insert(2, 5), 0);
    ASSERT_EQ(pt->getLossLength(), 8);
}

TEST_F(CSndLossListTest, InsertUpdateElement02)
{
    pt->insert(0, 0);

    // see InsertNoUpdateElement02 for details
    pt->m_caSeq[0].next = 2;
    pt->m_caSeq[2].data1 = 2;
    pt->m_caSeq[2].data2 = 3;
    pt->m_iLength += 2;

    ASSERT_EQ(pt->insert(2, 4), 1); // Element should be updated

    ASSERT_EQ(pt->getLossLength(), 4);
    ASSERT_EQ(pt->popLostSeq(), 0);
    ASSERT_EQ(pt->popLostSeq(), 2);
    ASSERT_EQ(pt->popLostSeq(), 3);
    ASSERT_EQ(pt->popLostSeq(), 4);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
TEST_F(CSndLossListTest, InsertCorruptionUseCase)
{
    // Fill the structure
    for (int i=0; i < CSndLossListTest::SIZE; i++)
        pt->insert(i,i);

    // Corrupt structure by introducing an infinite loop ...
    pt->m_caSeq[CSndLossListTest::SIZE-1].next = CSndLossListTest::SIZE-1;

    // next insert shoud not loop forever ...
    pt->insert(CSndLossListTest::SIZE-1,CSndLossListTest::SIZE-1);
}
