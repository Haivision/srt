#include <array>
#include <vector>
#include "gtest/gtest.h"
#include "queue.h"

using namespace std;
using namespace srt;

/// Create CUnitQueue with queue size of 4 units.
/// The size of 4 is chosen on purpose, because 
/// CUnitQueue::getNextAvailUnit(..) has the following
/// condition `if (m_iCount * 10 > m_iSize * 9)`. With m_iSize = 4
/// it will be false up until m_iCount becomes 4.
/// And there was an issue in getNextAvailUnit(..) in taking
/// the very last element of the queue (it was skipped).
TEST(CUnitQueue, Increase)
{
    const int buffer_size_pkts = 4;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);

    vector<CUnit*> taken_units;
    for (int i = 0; i < 5 * buffer_size_pkts; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        ASSERT_NE(unit, nullptr);
        unit_queue.makeUnitGood(unit);
        taken_units.push_back(unit);
    }
}

/// Create CUnitQueue with queue size of 4 units.
/// Then after requesting the 5th unit, free the previous
/// four units. This makes the previous queue completely free.
/// Requesting the 5th unit, there would be 3 units available in the
/// beginning of the same queue.
TEST(CUnitQueue, IncreaseAndFree)
{
    const int buffer_size_pkts = 4;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);

    CUnit* taken_unit = nullptr;
    for (int i = 0; i < 5 * buffer_size_pkts; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        ASSERT_NE(unit, nullptr);
        unit_queue.makeUnitGood(unit);

        if (taken_unit)
            unit_queue.makeUnitFree(taken_unit);

        taken_unit = unit;
    }
}

/// Create CUnitQueue with queue size of 4 units.
/// Then after requesting the 5th unit, free the previous
/// four units. This makes the previous queue completely free.
/// Requesting the 9th unit, there would be 4 units available in the
/// Thus the test checks if 
TEST(CUnitQueue, IncreaseAndFreeGrouped)
{
    const int buffer_size_pkts = 4;
    CUnitQueue unit_queue;
    unit_queue.init(buffer_size_pkts, 1500, AF_INET);

    vector<CUnit*> taken_units;
    for (int i = 0; i < 5 * buffer_size_pkts; ++i)
    {
        CUnit* unit = unit_queue.getNextAvailUnit();
        ASSERT_NE(unit, nullptr);
        unit_queue.makeUnitGood(unit);

        if (taken_units.size() >= buffer_size_pkts)
        {
            for_each(taken_units.begin(), taken_units.end(),
                [&unit_queue](CUnit* u) { unit_queue.makeUnitFree(u); });

            taken_units.clear();
        }

        taken_units.push_back(unit);
        EXPECT_LE(unit_queue.capacity(), 2 * buffer_size_pkts)
            << "Buffer capacity should not exceed two queues of 4 units";
    }
}
