#include <array>
#include <numeric>
#include "gtest/gtest.h"
#include "buffer_tools.h"
#include "sync.h"

using namespace srt;
using namespace std;

#ifdef ENABLE_MAXREXMITBW

class CSndRateEstFixture
    : public ::testing::Test
{
protected:
    CSndRateEstFixture()
        : m_tsStart(sync::steady_clock::now())
        , m_rateEst(m_tsStart)
    {
        // initialization code here
    }

    virtual ~CSndRateEstFixture()
    {
        // cleanup any pending stuff, but no exceptions allowed
    }

protected:
    // SetUp() is run immediately before a test starts.
    void SetUp() override
    {
        // make_unique is unfortunatelly C++14

    }

    void TearDown() override
    {
        // Code here will be called just after the test completes.
        // OK to throw exceptions from here if needed.
    }

    const sync::steady_clock::time_point m_tsStart;
    CSndRateEstimator m_rateEst;
};

// Check the available size of the receiver buffer.
TEST_F(CSndRateEstFixture, Empty)
{
    //EXPECT_EQ(getAvailBufferSize(), m_buff_size_pkts - 1);
    EXPECT_EQ(m_rateEst.getRate(), 0);
}


TEST_F(CSndRateEstFixture, CBRSending)
{
    // Generate CBR sending for 2.1 seconds to wrap the buffer around.
    for (int i = 0; i < 2100; ++i)
    {
        const auto t = m_tsStart + sync::milliseconds_from(i);
        m_rateEst.addSample(t, 1, 1316);
        
        const auto rate = m_rateEst.getRate();
        if (i >= 100)
            EXPECT_EQ(rate, 1316000) << "i=" << i;
        else
            EXPECT_EQ(rate, 0) << "i=" << i;
    }

}

// Make a 1 second long pause and check that the rate is 0 again
// only for one sampling period.
TEST_F(CSndRateEstFixture, CBRSendingAfterPause)
{
    // Send 100 packets with 1000 bytes each
    for (int i = 0; i < 3100; ++i)
    {
        if (i >= 1000 && i < 2000)
            continue;
        const auto t = m_tsStart + sync::milliseconds_from(i);
        m_rateEst.addSample(t, 1, 1316);

        const auto rate = m_rateEst.getRate();
        if (i >= 100 && !(i >= 2000 && i < 2100))
            EXPECT_EQ(rate, 1316000) << "i=" << i;
        else
            EXPECT_EQ(rate, 0) << "i=" << i;
    }
}

// Make a short 0.5 second pause and check the bitrate goes down, but not to 0.
// Those empty samples should be included in bitrate estimation.
TEST_F(CSndRateEstFixture, CBRSendingShortPause)
{
    // Send 100 packets with 1000 bytes each
    for (int i = 0; i < 3100; ++i)
    {
        if (i >= 1000 && i < 1500)
            continue;
        const auto t = m_tsStart + sync::milliseconds_from(i);
        m_rateEst.addSample(t, 1, 1316);

        const auto rate = m_rateEst.getRate();
        if (i >= 1500 && i < 2000)
            EXPECT_EQ(rate, 658000) << "i=" << i;
        else if (i >= 100)
            EXPECT_EQ(rate, 1316000) << "i=" << i;
        else
            EXPECT_EQ(rate, 0) << "i=" << i;
    }
}

#endif // ENABLE_MAXREXMITBW
