#include "gtest/gtest.h"
#include <chrono>
#include <thread>
#include <array>
#include <numeric>   // std::accumulate
#include "common.h"




TEST(CTimer, DISABLED_SleeptoAccuracy)
{
    using namespace std;

    const int num_samples = 1000;
    array<uint64_t, num_samples> sleeps_us;

    const uint64_t freq = CTimer::getCPUFrequency();
    std::cerr << "CPU Frequency: " << freq << "\n";

    const uint64_t sleep_intervals_us[] = { 1, 5, 10, 50, 100, 250, 500, 1000, 5000, 10000 };

    CTimer timer;

    for (uint64_t interval_us : sleep_intervals_us)
    {
        for (int i = 0; i < num_samples; i++)
        {
            uint64_t currtime;
            CTimer::rdtsc(currtime);

            timer.sleepto(currtime + interval_us * freq);

            uint64_t new_time;
            CTimer::rdtsc(new_time);
            sleeps_us[i] = (new_time - currtime) / freq;
        }

        cerr << "Target sleep duration: " << interval_us << " us\n";
        cerr << "avg sleep duration: " << accumulate(sleeps_us.begin(), sleeps_us.end(), (uint64_t) 0) / num_samples << " us\n";
        cerr << "min sleep duration: " << *min_element(sleeps_us.begin(), sleeps_us.end()) << " us\n";
        cerr << "max sleep duration: " << *max_element(sleeps_us.begin(), sleeps_us.end()) << " us\n";
        cerr << "\n";
    }
}


