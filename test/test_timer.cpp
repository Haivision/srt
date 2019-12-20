#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <array>
#include <numeric>   // std::accumulate
#include "sync.h"




TEST(CTimer, DISABLED_SleeptoAccuracy)
{
    using namespace std;
    using namespace srt::sync;

    const int num_samples = 1000;
    array<uint64_t, num_samples> sleeps_us;

    const long sleep_intervals_us[] = { 1, 5, 10, 50, 100, 250, 500, 1000, 5000, 10000 };

    srt::sync::SyncEvent timer;

    for (long interval_us : sleep_intervals_us)
    {
        for (int i = 0; i < num_samples; i++)
        {
            const auto currtime = steady_clock::now();

            timer.wait_until(currtime + microseconds_from(interval_us));

            const auto newtime = steady_clock::now();
            sleeps_us[i] = count_microseconds(newtime - currtime);
        }

        cerr << "Target sleep duration: " << interval_us << " us\n";
        cerr << "avg sleep duration: " << accumulate(sleeps_us.begin(), sleeps_us.end(), (uint64_t) 0) / num_samples << " us\n";
        cerr << "min sleep duration: " << *min_element(sleeps_us.begin(), sleeps_us.end()) << " us\n";
        cerr << "max sleep duration: " << *max_element(sleeps_us.begin(), sleeps_us.end()) << " us\n";
        cerr << "\n";
    }
}

