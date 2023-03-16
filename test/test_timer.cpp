#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <array>
#include <numeric>   // std::accumulate
#include "common.h"
#include "sync.h"




TEST(CTimer, DISABLED_SleeptoAccuracy)
{
    using namespace std;
    using namespace srt::sync;

    const int num_samples = 1000;
    array<uint64_t, num_samples> sleeps_us;

    const uint64_t sleep_intervals_us[] = { 1, 5, 10, 50, 100, 250, 500, 1000, 5000, 10000 };

    CTimer timer;
    bool dummy_forced = false;

    for (uint64_t interval_us : sleep_intervals_us)
    {
        for (int i = 0; i < num_samples; i++)
        {
            steady_clock::time_point currtime = steady_clock::now();
            timer.sleep_until(currtime + microseconds_from(interval_us), dummy_forced);

            steady_clock::time_point new_time = steady_clock::now();
            sleeps_us[i] = count_microseconds(new_time - currtime);
        }

        cerr << "Target sleep duration: " << interval_us << " us\n";
        cerr << "avg sleep duration: " << accumulate(sleeps_us.begin(), sleeps_us.end(), (uint64_t) 0) / num_samples << " us\n";
        cerr << "min sleep duration: " << *min_element(sleeps_us.begin(), sleeps_us.end()) << " us\n";
        cerr << "max sleep duration: " << *max_element(sleeps_us.begin(), sleeps_us.end()) << " us\n";
        cerr << "\n";
    }
}


