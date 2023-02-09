#include "gtest/gtest.h"
#include <array>
#include <chrono>
#include <thread>
#include <future>
#include <numeric> // std::accumulate
#include <regex>   // Used in FormatTime test
#include "sync.h"
#include "common.h"

// This test set requires support for C++14
// * Uses "'" as a separator: 100'000
// * Uses operator"ms" at al from chrono

using namespace std;
using namespace srt::sync;


TEST(SyncDuration, BasicChecks)
{
    const steady_clock::duration d = steady_clock::duration();

    EXPECT_EQ(d.count(), 0);
    EXPECT_TRUE(d == d);  // operator==
    EXPECT_FALSE(d != d); // operator!=
    EXPECT_EQ(d, steady_clock::duration::zero());
    EXPECT_EQ(d, microseconds_from(0));
    EXPECT_EQ(d, milliseconds_from(0));
    EXPECT_EQ(d, seconds_from(0));
    EXPECT_EQ(count_milliseconds(d), 0);
    EXPECT_EQ(count_microseconds(d), 0);
    EXPECT_EQ(count_seconds(d), 0);

    const steady_clock::duration a = d + milliseconds_from(120);
    EXPECT_EQ(a, milliseconds_from(120));
    EXPECT_EQ(count_milliseconds(a), 120);
    EXPECT_EQ(count_microseconds(a), 120000);
    EXPECT_EQ(count_seconds(a), 0);
}

/// Check operations on (uint32_t + 1)
TEST(SyncDuration, DurationFrom)
{
    const int64_t val = int64_t(numeric_limits<uint32_t>::max()) + 1;

    const steady_clock::duration us_from = microseconds_from(val);
    EXPECT_EQ(count_microseconds(us_from), val);

    const steady_clock::duration ms_from = milliseconds_from(val);
    EXPECT_EQ(count_milliseconds(ms_from), val);

    const steady_clock::duration s_from = seconds_from(val);
    EXPECT_EQ(count_seconds(s_from), val);
}

TEST(SyncDuration, RelOperators)
{
    const steady_clock::duration a = steady_clock::duration();

    EXPECT_EQ(a.count(), 0);
    EXPECT_TRUE(a == a);  // operator==
    EXPECT_FALSE(a != a); // operator!=
    EXPECT_FALSE(a > a);  // operator>
    EXPECT_FALSE(a < a);  // operator<
    EXPECT_TRUE(a <= a);  // operator<=
    EXPECT_TRUE(a >= a);  // operator>=

    const steady_clock::duration b = a + milliseconds_from(120);
    EXPECT_FALSE(b == a); // operator==
    EXPECT_TRUE(b != a);  // operator!=
    EXPECT_TRUE(b > a);   // operator>
    EXPECT_FALSE(a > b);  // operator>
    EXPECT_FALSE(b < a);  // operator<
    EXPECT_TRUE(a < b);   // operator<
    EXPECT_FALSE(b <= a); // operator<=
    EXPECT_TRUE(a <= b);  // operator<=
    EXPECT_TRUE(b >= a);  // operator>=
    EXPECT_FALSE(a >= b); // operator>=

    const steady_clock::duration c = steady_clock::duration(numeric_limits<int64_t>::max());
    EXPECT_EQ(c.count(), numeric_limits<int64_t>::max());
    const steady_clock::duration d = steady_clock::duration(numeric_limits<int64_t>::min());
    EXPECT_EQ(d.count(), numeric_limits<int64_t>::min());
}

TEST(SyncDuration, OperatorMinus)
{
    const steady_clock::duration a = seconds_from(5);
    const steady_clock::duration b = milliseconds_from(3500);

    EXPECT_EQ(count_milliseconds(a - b), 1500);
    EXPECT_EQ(count_milliseconds(b - a), -1500);
    EXPECT_EQ((a - a).count(), 0);
}

TEST(SyncDuration, OperatorMinusEq)
{
    const steady_clock::duration a = seconds_from(5);
    const steady_clock::duration b = milliseconds_from(3500);

    steady_clock::duration c = a;
    EXPECT_EQ(c, a);
    c -= b;
    EXPECT_EQ(count_milliseconds(c), 1500);
    c = b;
    EXPECT_EQ(c, b);
    c -= a;
    EXPECT_EQ(count_milliseconds(c), -1500);
}

TEST(SyncDuration, OperatorPlus)
{
    const steady_clock::duration a = seconds_from(5);
    const steady_clock::duration b = milliseconds_from(3500);

    EXPECT_EQ(count_milliseconds(a + b), 8500);
    EXPECT_EQ(count_milliseconds(b + a), 8500);
}

TEST(SyncDuration, OperatorPlusEq)
{
    const steady_clock::duration a = seconds_from(5);
    const steady_clock::duration b = milliseconds_from(3500);

    steady_clock::duration c = a;
    EXPECT_EQ(c, a);
    c += b;
    EXPECT_EQ(count_milliseconds(c), 8500);
    c = b;
    EXPECT_EQ(c, b);
    c += a;
    EXPECT_EQ(count_milliseconds(c), 8500);
}

TEST(SyncDuration, OperatorMultInt)
{
    const steady_clock::duration a = milliseconds_from(3500);

    EXPECT_EQ(count_milliseconds(a), 3500);
    EXPECT_EQ(count_milliseconds(a * 2), 7000);
}

TEST(SyncDuration, OperatorMultIntEq)
{
    steady_clock::duration a = milliseconds_from(3500);

    EXPECT_EQ(count_milliseconds(a), 3500);
    a *= 2;
    EXPECT_EQ(count_milliseconds(a), 7000);
}

TEST(SyncRandom, GenRandomInt)
{
    array<size_t, 64> mn = {};

    // Check generated values are in the specified range.
    const size_t n = 2048;
    for (size_t i = 0; i < n; ++i)
    {
        const int rand_val = genRandomInt(0, int(mn.size()) - 1);
        ASSERT_GE(rand_val, 0);
        ASSERT_LT(rand_val, mn.size());
        ++mn[rand_val];
    }

    // Check the distribution is more or less uniform.
    // 100% uniform if each value is generated (n / (2 * mn.size())) times.
    // We expect at least half of that value for a random uniform distribution.
    ASSERT_GT(n / (2 * mn.size()), 4u);
    const size_t min_value = n / (2 * mn.size()) - 4u; // Subtracting 4 to tolerate possible deviations.
    for (size_t i = 0; i < mn.size(); ++i)
    {
        EXPECT_GE(mn[i], min_value) << "i=" << i << ". Ok-ish if the count is non-zero.";
    }

    // Uncomment to see the distribution.
    //cout << "min value: " << min_value << endl;
    //for (size_t i = 0; i < mn.size(); ++i)
    //{
    //    cout << i << '\t';
    //    for (int j=0; j<mn[i]; ++j) cout << '*';
    //    cout << '\n';
    //}

    // Check INT32_MAX
    for (size_t i = 0; i < n; ++i)
    {
        const int rand_val = genRandomInt(INT32_MAX - 1, INT32_MAX);

        EXPECT_GE(rand_val, INT32_MAX - 1);
        EXPECT_LE(rand_val, INT32_MAX);
    }
}

/*****************************************************************************/
/*
 * TimePoint tests
 */
/*****************************************************************************/

TEST(SyncTimePoint, DefaultConstructorZero)
{
    steady_clock::time_point a;
    EXPECT_TRUE(is_zero(a));
}

TEST(SyncTimePoint, RelOperators)
{
    const steady_clock::time_point a(steady_clock::time_point::max());
    const steady_clock::time_point b(steady_clock::time_point::min());
    EXPECT_TRUE(a == a);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(a != b);

    EXPECT_TRUE(a >= a);
    EXPECT_FALSE(b >= a);
    EXPECT_TRUE(a > b);
    EXPECT_FALSE(a > a);
    EXPECT_TRUE(a <= a);
    EXPECT_TRUE(b <= a);
    EXPECT_FALSE(a <= b);
    EXPECT_FALSE(a < a);
    EXPECT_TRUE(b < a);
    EXPECT_FALSE(a < b);
}

#ifndef ENABLE_STDCXX_SYNC
TEST(SyncTimePoint, OperatorMinus)
{
    const int64_t                  delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);
    EXPECT_EQ((a - b).count(), delta);
    EXPECT_EQ((b - a).count(), -delta);
}

TEST(SyncTimePoint, OperatorEq)
{
    const int64_t                  delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max() - delta);
    const steady_clock::time_point b = a;
    EXPECT_EQ(a, b);
}

TEST(SyncTimePoint, OperatorMinusPlusDuration)
{
    const int64_t                  delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);

    EXPECT_EQ((a + steady_clock::duration(-delta)), b);
    EXPECT_EQ((b + steady_clock::duration(+delta)), a);

    EXPECT_EQ((a - steady_clock::duration(+delta)), b);
    EXPECT_EQ((b - steady_clock::duration(-delta)), a);
}

TEST(SyncTimePoint, OperatorPlusEqDuration)
{
    const int64_t                  delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);
    steady_clock::time_point       r = a;
    EXPECT_EQ(r, a);
    r += steady_clock::duration(-delta);
    EXPECT_EQ(r, b);
    r = b;
    EXPECT_EQ(r, b);
    r += steady_clock::duration(+delta);
    EXPECT_EQ(r, a);
    r = a;
    EXPECT_EQ(r, a);
    r -= steady_clock::duration(+delta);
    EXPECT_EQ((a - steady_clock::duration(+delta)), b);
    EXPECT_EQ((b - steady_clock::duration(-delta)), a);
}

TEST(SyncTimePoint, OperatorMinusEqDuration)
{
    const int64_t                  delta = 1024;
    const steady_clock::time_point a(numeric_limits<uint64_t>::max());
    const steady_clock::time_point b(numeric_limits<uint64_t>::max() - delta);
    steady_clock::time_point       r = a;
    EXPECT_EQ(r, a);
    r -= steady_clock::duration(+delta);
    EXPECT_EQ(r, b);
    r = b;
    EXPECT_EQ(r, b);
    r -= steady_clock::duration(-delta);
    EXPECT_EQ(r, a);
}
#endif

/*****************************************************************************/
/*
 * UniqueLock tests
 */
/*****************************************************************************/
TEST(SyncUniqueLock, LockUnlock)
{
    Mutex mtx;
    UniqueLock lock(mtx);
    EXPECT_FALSE(mtx.try_lock());
    
    lock.unlock();
    EXPECT_TRUE(mtx.try_lock());
    
    mtx.unlock();
    lock.lock();
    EXPECT_FALSE(mtx.try_lock());
}

TEST(SyncUniqueLock, Scope)
{
    Mutex mtx;

    {
        UniqueLock lock(mtx);
        EXPECT_FALSE(mtx.try_lock());
    }
    
    EXPECT_TRUE(mtx.try_lock());
    mtx.unlock();
}

/*****************************************************************************/
/*
 * SyncEvent tests
 */
/*****************************************************************************/
TEST(SyncEvent, WaitFor)
{
    Mutex mutex;
    Condition  cond;
    cond.init();

    for (int timeout_us : {50, 100, 500, 1000, 101000, 1001000})
    {
        const steady_clock::duration   timeout = microseconds_from(timeout_us);
        UniqueLock lock(mutex);
        const steady_clock::time_point start = steady_clock::now();
        const bool on_timeout = !cond.wait_for(lock, timeout);
        const steady_clock::time_point stop = steady_clock::now();
        const steady_clock::duration waittime = stop - start;
        const int64_t waittime_us = count_microseconds(waittime);
#if defined(ENABLE_STDCXX_SYNC) || !defined(_WIN32)
        // This check somehow fails on AppVeyor Windows VM with VS 2015 and pthreads.
        // - SyncEvent::wait_for( 50us) took 6us
        // - SyncEvent::wait_for(100us) took 4us
        if (on_timeout) {
            const int tolerance = timeout_us/1000;
            EXPECT_GE(waittime_us, timeout_us - tolerance);
        }
#endif
        if (on_timeout) {
            // Give it 100 times the timeout, as this is
            // considered more than "crazy long", whereas we only
            // want to check if it has waited a finite amount of time.
            EXPECT_LE(waittime_us, 10 * 1001000); // biggest wait value
        }

        string spurious = on_timeout ? "" : " (SPURIOUS)";

        if (timeout_us < 1000)
        {
            cerr << "SyncEvent::wait_for(" << timeout_us << "us) took "
                << waittime_us << "us" << spurious << endl;
        }
        else
        {
            cerr << "SyncEvent::wait_for(" << count_milliseconds(timeout) << " ms) took "
                << (waittime_us / 1000.0) << " ms" << spurious << endl;
        }
    }

    cond.destroy();
}

TEST(SyncEvent, WaitForNotifyOne)
{
    Mutex mutex;
    Condition cond;
    cond.init();

    const steady_clock::duration timeout = seconds_from(5);

    auto wait_async = [](Condition* cond, Mutex* mutex, const steady_clock::duration& timeout) {
        CUniqueSync cc (*mutex, *cond);
        return cc.wait_for(timeout);
    };
    auto wait_async_res = async(launch::async, wait_async, &cond, &mutex, timeout);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    cond.notify_one();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::ready);
    const bool wait_for_res = wait_async_res.get();
    EXPECT_TRUE(wait_for_res) << "Woken up by a notification";

    cond.destroy();
}

TEST(SyncEvent, WaitNotifyOne)
{
    Mutex mutex;
    Condition cond;
    cond.init();

    auto wait_async = [](Condition* cond, Mutex* mutex) {
        UniqueLock lock(*mutex);
        return cond->wait(lock);
    };
    auto wait_async_res = async(launch::async, wait_async, &cond, &mutex);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    cond.notify_one();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(100)), future_status::ready);
    wait_async_res.get();

    cond.destroy();
}

TEST(SyncEvent, WaitForTwoNotifyOne)
{
    Mutex mutex;
    Condition cond;
    vector<int> notified_clients;
    cond.init();
    const steady_clock::duration timeout = seconds_from(3);
    const int VAL_SIGNAL = 42;
    const int VAL_NO_SIGNAL = 0;

    srt::sync::atomic<bool> resource_ready(true);

    auto wait_async = [&](Condition* cond, Mutex* mutex, const steady_clock::duration& timeout, int id) {
        UniqueLock lock(*mutex);
        if (cond->wait_for(lock, timeout) && resource_ready)
        {
            notified_clients.push_back(id);
            resource_ready = false;
            return VAL_SIGNAL;
        }
        return VAL_NO_SIGNAL;
    };

    using future_t = decltype(async(launch::async, wait_async, &cond, &mutex, timeout, 0));

    future_t future_result[2] = {
        async(launch::async, wait_async, &cond, &mutex, timeout, 0),
        async(launch::async, wait_async, &cond, &mutex, timeout, 1)
    };

    for (auto& wr: future_result)
    {
        ASSERT_EQ(wr.wait_for(chrono::milliseconds(100)), future_status::timeout);
    }

    {
        ScopedLock lk(mutex);
        cond.notify_one();
    }

    using wait_t = decltype(future_t().wait_for(chrono::microseconds(0)));

    wait_t wait_state[2] = {
        move(future_result[0].wait_for(chrono::microseconds(500))),
        move(future_result[1].wait_for(chrono::microseconds(500)))
    };

    cerr << "SyncEvent::WaitForTwoNotifyOne: NOTIFICATION came from " << notified_clients.size()
        << " clients:";
    for (auto& nof: notified_clients)
        cerr << " " << nof;
    cerr << endl;

    // Now exactly one waiting thread should become ready
    // Error if: 0 (none ready) or 2 (both ready, while notify_one was used)
    ASSERT_EQ(notified_clients.size(), 1U);

    const int ready = notified_clients[0];
    const int not_ready = (ready + 1) % 2;

    int future_val[2];

    // The READY client must have a valid value.
    ASSERT_TRUE(future_result[ready].valid());
    future_val[ready] = future_result[ready].get();

    // The NOT READY client MIGHT have a valid value, in which case we take expected 0,
    // or maybe not, in which case we set -1 value. Either of both must be the
    // result for the test to be valid.
    if (future_result[not_ready].valid())
    {
        future_val[not_ready] = future_result[not_ready].get();
    }
    else
    {
        future_val[not_ready] = VAL_NO_SIGNAL-1; // to match LE comparison
    }

    string disp_future[16];
    disp_future[int(future_status::timeout)] = "timeout";
    disp_future[int(future_status::ready)] = "ready";

    // Informational text
    cerr << "SyncEvent::WaitForTwoNotifyOne: READY THREAD: " << ready
        << " STATUS " << disp_future[int(wait_state[ready])]
        //<< " RESULT " << disp_state[0+future_val[ready]] << endl;
        << " RESULT " << future_val[ready] << endl;

    cerr << "SyncEvent::WaitForTwoNotifyOne: TMOUT THREAD: " << not_ready
        << " STATUS " << disp_future[int(wait_state[not_ready])]
        //<< " RESULT " << disp_state[0+future_val[not_ready]] << endl;
        << " RESULT " << future_val[not_ready] << endl;

    // The one that got the signal, should exit ready.
    // The one that didn't get the signal, should exit timeout.
    EXPECT_EQ(wait_state[ready], future_status::ready);
    EXPECT_EQ(wait_state[not_ready], future_status::timeout);

    // Same, expect these future to return the value
    // TURNED OFF for Windows, as there happens to be a
    // "spurious" signal causing this condition to fail,
    // even though it is declared valid and timed out.
    EXPECT_EQ(future_val[ready], VAL_SIGNAL);

    EXPECT_LE(future_val[not_ready], VAL_NO_SIGNAL);

    cond.destroy();
}

TEST(SyncEvent, WaitForTwoNotifyAll)
{
    Mutex mutex;
    Condition cond;
    cond.init();
    const steady_clock::duration timeout = seconds_from(3);

    auto wait_async = [](Condition* cond, Mutex* mutex, const steady_clock::duration& timeout) {
        UniqueLock lock(*mutex);
        return cond->wait_for(lock, timeout);
    };
    auto wait_async1_res = async(launch::async, wait_async, &cond, &mutex, timeout);
    auto wait_async2_res = async(launch::async, wait_async, &cond, &mutex, timeout);

    EXPECT_EQ(wait_async1_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    EXPECT_EQ(wait_async2_res.wait_for(chrono::milliseconds(100)), future_status::timeout);
    cond.notify_all();
    // Now only one waiting thread should become ready
    const future_status status1 = wait_async1_res.wait_for(chrono::milliseconds(100));
    const future_status status2 = wait_async2_res.wait_for(chrono::milliseconds(100));
    EXPECT_EQ(status1, future_status::ready);
    EXPECT_EQ(status2, future_status::ready);
    // Expect both threads to wake up by condition
    EXPECT_TRUE(wait_async1_res.get());
    EXPECT_TRUE(wait_async2_res.get());

    cond.destroy();
}

TEST(SyncEvent, WaitForNotifyAll)
{
    Mutex mutex;
    Condition cond;
    cond.init();
    const steady_clock::duration timeout = seconds_from(5);

    auto wait_async = [](Condition* cond, Mutex* mutex, const steady_clock::duration& timeout) {
        UniqueLock lock(*mutex);
        return cond->wait_for(lock, timeout);
    };
    auto wait_async_res = async(launch::async, wait_async, &cond, &mutex, timeout);

    EXPECT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::timeout);
    cond.notify_all();
    ASSERT_EQ(wait_async_res.wait_for(chrono::milliseconds(500)), future_status::ready);
    const bool wait_for_res = wait_async_res.get();
    EXPECT_TRUE(wait_for_res) << "Woken up by condition";

    cond.destroy();
}

/*****************************************************************************/
/*
 * CThread
 */
 /*****************************************************************************/
void* dummythread(void* param)
{
    *(bool*)(param) = true;
    return nullptr;
}

TEST(SyncThread, Joinable)
{
    CThread foo;
    srt::sync::atomic<bool> thread_finished;

    StartThread(foo, dummythread, (void*)&thread_finished, "DumyThread");

    EXPECT_TRUE(foo.joinable());
    while (!thread_finished)
    {
        std::this_thread::sleep_for(chrono::milliseconds(50));
    }
    EXPECT_TRUE(foo.joinable());
    foo.join();
    EXPECT_FALSE(foo.joinable());
}

/*****************************************************************************/
/*
 * FormatTime
 */
/*****************************************************************************/
#if !defined(__GNUC__) || defined(__clang__) || (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 9))
//#if !defined(__GNUC__) || (__GNUC__ > 4)
//#if !defined(__GNUC__) || (__GNUC__ >= 5)
// g++ before 4.9 (?) does not support regex and crashes on execution.

TEST(Sync, FormatTime)
{
    auto parse_time = [](const string& timestr) -> long long {
        // Example string: 1D 02:10:55.972651 [STD]
        const regex rex("([[:digit:]]+D )?([[:digit:]]{2}):([[:digit:]]{2}):([[:digit:]]{2}).([[:digit:]]{6,}) \\[STDY\\]");
        std::smatch sm;
        EXPECT_TRUE(regex_match(timestr, sm, rex));
        EXPECT_LE(sm.size(), 6U);
        if (sm.size() != 6 && sm.size() != 5)
            return 0;

        // Day may be missing if zero
        const long long d = sm[1].matched ? std::stoi(sm[1]) : 0;
        const long long h = std::stoll(sm[2]);
        const long long m = std::stoll(sm[3]);
        const long long s = std::stoll(sm[4]);
        const long long u = std::stoll(sm[5]);

        return u + s * 1000000 + m * 60000000 + h * 60 * 60 * 1000000 + d * 24 * 60 * 60 * 1000000;
    };

    auto print_timediff = [&parse_time](const string& desc, const string& time, const string& time_base) {
        const long long diff = parse_time(time) - parse_time(time_base);
        cerr << desc << time << " (" << diff << " us)" << endl;
    };

    const auto   a = steady_clock::now();
    const string time1 = FormatTime(a);
    const string time2 = FormatTime(a);
    const string time3 = FormatTime(a + milliseconds_from(500));
    const string time4 = FormatTime(a + seconds_from(1));
    const string time5 = FormatTime(a + seconds_from(5));
    const string time6 = FormatTime(a + milliseconds_from(-4350));
    cerr << "Current time formated:    " << time1 << endl;
    const long long diff_2_1 = parse_time(time2) - parse_time(time1);
    cerr << "Same time formated again: " << time2 << " (" << diff_2_1 << " us)" << endl;
    print_timediff("Same time formated again: ", time2, time1);
    print_timediff("Time +500 ms formated:    ", time3, time1);
    print_timediff("Time +1  sec formated:    ", time4, time1);
    print_timediff("Time +5  sec formated:    ", time5, time1);
    print_timediff("Time -4350 ms formated:   ", time6, time1);

    EXPECT_TRUE(time1 == time2);
}

TEST(Sync, FormatTimeSys)
{
    auto parse_time = [](const string& timestr) -> long long {
        const regex rex("([[:digit:]]{2}):([[:digit:]]{2}):([[:digit:]]{2}).([[:digit:]]{6}) \\[SYST\\]");
        std::smatch sm;
        EXPECT_TRUE(regex_match(timestr, sm, rex));
        EXPECT_EQ(sm.size(), 5U);
        if (sm.size() != 5)
            return 0;

        const long long h = std::stoi(sm[1]);
        const long long m = std::stoi(sm[2]);
        const long long s = std::stoi(sm[3]);
        const long long u = std::stoi(sm[4]);

        return u + s * 1000000 + m * 60000000 + h * 60 * 60 * 1000000;
    };

    auto print_timediff = [&parse_time](const string& desc, const string& time, const string& time_base) {
        const long long diff = parse_time(time) - parse_time(time_base);
        cerr << desc << time << " (" << diff << " us)" << endl;
    };

    const steady_clock::time_point a     = steady_clock::now();
    const string                   time1 = FormatTimeSys(a);
    const string                   time2 = FormatTimeSys(a);
    const string                   time3 = FormatTimeSys(a + milliseconds_from(500));
    const string                   time4 = FormatTimeSys(a + seconds_from(1));
    const string                   time5 = FormatTimeSys(a + seconds_from(5));
    const string                   time6 = FormatTimeSys(a + milliseconds_from(-4350));
    cerr << "Current time formated:    " << time1 << endl;
    const long long diff_2_1 = parse_time(time2) - parse_time(time1);
    cerr << "Same time formated again: " << time2 << " (" << diff_2_1 << " us)" << endl;
    print_timediff("Same time formated again: ", time2, time1);
    print_timediff("Time +500 ms formated:    ", time3, time1);
    print_timediff("Time +1  sec formated:    ", time4, time1);
    print_timediff("Time +5  sec formated:    ", time5, time1);
    print_timediff("Time -4350 ms formated:   ", time6, time1);

    EXPECT_TRUE(time1 == time2);
}
#endif
