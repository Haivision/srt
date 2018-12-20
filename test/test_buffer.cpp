#include "gtest/gtest.h"
#include "buffer.h"

TEST(CRcvBuffer, Create)
{
    const int buffer_size = 65536;
    CUnitQueue unit_queue;
    CRcvBuffer rcv_buffer(&unit_queue, buffer_size);
    
    EXPECT_EQ(rcv_buffer.getAvailBufSize(), buffer_size - 1);   // logic
}
