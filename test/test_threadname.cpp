#include <algorithm>
#include <string>

#include "gtest/gtest.h"
#include "threadname.h"

using namespace srt;

TEST(ThreadName, GetSet)
{
    std::string name("getset");
    char        buf[ThreadName::BUFSIZE * 2];

    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(ThreadName::get(buf), true);
    // ensure doesn't write out-of-range
    size_t max = ThreadName::BUFSIZE - 1;
    ASSERT_LE(strlen(buf), max);

    if (ThreadName::DUMMY_IMPL)
        return;

    ASSERT_EQ(ThreadName::set(name), true);
    memset(buf, 'a', sizeof(buf));
    ASSERT_EQ(ThreadName::get(buf), true);
    ASSERT_EQ(buf, name);
}

TEST(ThreadName, AutoReset)
{
    const std::string old_name("old");
    std::string new_name("new-name");
    if (ThreadName::DUMMY_IMPL)
    {
        // just make sure the API is correct
        ThreadName t(std::string("test"));
        return;
    }

    ASSERT_EQ(ThreadName::set(old_name), true);
    std::string name;
    ASSERT_EQ(ThreadName::get(name), true);
    ASSERT_EQ(name, old_name);

    {
        ThreadName threadName(new_name);
        ASSERT_EQ(ThreadName::get(name), true);
        ASSERT_EQ(name, new_name);
    }

    ASSERT_EQ(ThreadName::get(name), true);
    ASSERT_EQ(name, old_name);

    {
        new_name.resize(std::max<size_t>(512, ThreadName::BUFSIZE * 2), 'z');
        ThreadName threadName(new_name);
        ASSERT_EQ(ThreadName::get(name), true);
        ASSERT_EQ(new_name.compare(0, name.size(), name), 0);
    }

    ASSERT_EQ(ThreadName::get(name), true);
    ASSERT_EQ(name, old_name);
}
