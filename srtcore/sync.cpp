/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <iomanip>
#include <math.h>
#include <stdexcept>
#include "sync.h"
#include "udt.h"
#include "srt_compat.h"


#ifndef USE_STL_CHRONO

#if defined(_WIN32)
#define TIMING_USE_QPC
#include "win/wintime.h"
#include <sys/timeb.h>
#elif defined(OSX) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
#define TIMING_USE_MACH_ABS_TIME
#include <mach/mach_time.h>
//#elif defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_TIMERS > 0
#elif defined(ENABLE_MONOTONIC_CLOCK)
#define TIMING_USE_CLOCK_GETTIME
#endif

namespace srt
{
namespace sync
{

void rdtsc(uint64_t& x)
{
#ifdef IA32
    uint32_t lval, hval;
    // asm volatile ("push %eax; push %ebx; push %ecx; push %edx");
    // asm volatile ("xor %eax, %eax; cpuid");
    asm volatile("rdtsc" : "=a"(lval), "=d"(hval));
    // asm volatile ("pop %edx; pop %ecx; pop %ebx; pop %eax");
    x = hval;
    x = (x << 32) | lval;
#elif defined(IA64)
    asm("mov %0=ar.itc" : "=r"(x)::"memory");
#elif defined(AMD64)
    uint32_t lval, hval;
    asm("rdtsc" : "=a"(lval), "=d"(hval));
    x = hval;
    x = (x << 32) | lval;
#elif defined(TIMING_USE_QPC)
    // This function should not fail, because we checked the QPC
    // when calling to QueryPerformanceFrequency. If it failed,
    // the m_bUseMicroSecond was set to true.
    QueryPerformanceCounter((LARGE_INTEGER*)&x);
#elif defined(TIMING_USE_CLOCK_GETTIME)
    // get_cpu_frequency() returns 1 us accuracy in this case
    timespec tm;
    clock_gettime(CLOCK_MONOTONIC, &tm);
    x = tm.tv_sec * uint64_t(1000000) + t.tv_usec;
#elif defined(TIMING_USE_MACH_ABS_TIME)
    x = mach_absolute_time();
#else
    // use system call to read time clock for other archs
    timeval t;
    gettimeofday(&t, 0);
    x = t.tv_sec * uint64_t(1000000) + t.tv_usec;
#endif
}

 int64_t get_cpu_frequency()
 {
     int64_t frequency = 1; // 1 tick per microsecond.

#if defined(TIMING_USE_QPC)
     LARGE_INTEGER ccf; // in counts per second
     if (QueryPerformanceFrequency(&ccf))
         frequency = ccf.QuadPart / 1000000; // counts per microsecond

#elif defined(TIMING_USE_CLOCK_GETTIME)
     frequency = 1;

#elif defined(TIMING_USE_MACH_ABS_TIME)

     mach_timebase_info_data_t info;
     mach_timebase_info(&info);
     frequency = info.denom * int64_t(1000) / info.numer;

#elif defined(IA32) || defined(IA64) || defined(AMD64)
     uint64_t t1, t2;

     rdtsc(t1);
     timespec ts;
     ts.tv_sec  = 0;
     ts.tv_nsec = 100000000;
     nanosleep(&ts, NULL);
     rdtsc(t2);

     // CPU clocks per microsecond
     frequency = int64_t(t2 - t1) / 100000;
#endif

     return frequency;
 }

 const int64_t s_cpu_frequency = get_cpu_frequency();

    } // namespace sync
} // namespace srt

template <>
uint64_t srt::sync::TimePoint<srt::sync::steady_clock>::us_since_epoch() const
{
    return m_timestamp / s_cpu_frequency;
}

template <>
srt::sync::Duration<srt::sync::steady_clock> srt::sync::TimePoint<srt::sync::steady_clock>::time_since_epoch() const
{
    return srt::sync::Duration<srt::sync::steady_clock>(m_timestamp);
}

srt::sync::TimePoint<srt::sync::steady_clock> srt::sync::steady_clock::now()
{
    uint64_t x = 0;
    rdtsc(x);
    return TimePoint<steady_clock>(x);
}

int64_t srt::sync::count_microseconds(const steady_clock::duration& t)
{
    return t.count() / s_cpu_frequency;
}

int64_t srt::sync::count_milliseconds(const steady_clock::duration& t)
{
    return t.count() / s_cpu_frequency / 1000;
}

int64_t srt::sync::count_seconds(const steady_clock::duration& t)
{
    return t.count() / s_cpu_frequency / 1000000;
}

srt::sync::steady_clock::duration srt::sync::microseconds_from(int64_t t_us)
{
    return steady_clock::duration(t_us * s_cpu_frequency);
}

srt::sync::steady_clock::duration srt::sync::milliseconds_from(int64_t t_ms)
{
    return steady_clock::duration((1000 * t_ms) * s_cpu_frequency);
}

srt::sync::steady_clock::duration srt::sync::seconds_from(int64_t t_s)
{
    return steady_clock::duration((1000000 * t_s) * s_cpu_frequency);
}


std::string srt::sync::FormatTime(const steady_clock::time_point& timestamp)
{
    const uint64_t total_us = timestamp.us_since_epoch();
    const uint64_t us = total_us % 1000000;
    const uint64_t total_sec = total_us / 1000000;

    const uint64_t days = total_sec / (60 * 60 * 24);
    const uint64_t hours = total_sec / (60 * 60) - days * 24;

    const uint64_t minutes = total_sec / 60 - (days * 24 * 60) - hours * 60;
    const uint64_t seconds = total_sec - (days * 24 * 60 * 60) - hours * 60 * 60 - minutes * 60;

    ostringstream out;
    if (days)
        out << days << "D ";
    out << setfill('0') << setw(2) << hours << ":"
        << setfill('0') << setw(2) << minutes << ":"
        << setfill('0') << setw(2) << seconds << "."
        << setfill('0') << setw(6) << us << " [STD]";
    return out.str();
}

std::string srt::sync::FormatTimeSys(const steady_clock::time_point& timestamp)
{
    const time_t                   now_s = ::time(NULL); // get current time in seconds
    const steady_clock::time_point now_timestamp = steady_clock::now();
    const int64_t                  delta_us = count_microseconds(timestamp - now_timestamp);
    const int64_t                  delta_s =
        floor((static_cast<int64_t>(now_timestamp.us_since_epoch() % 1000000) + delta_us) / 1000000.0);
    const time_t tt = now_s + delta_s;
    struct tm    tm = SysLocalTime(tt); // in seconds
    char         tmp_buf[512];
    strftime(tmp_buf, 512, "%X.", &tm);

    ostringstream out;
    out << tmp_buf << setfill('0') << setw(6) << (timestamp.us_since_epoch() % 1000000) << " [SYS]";
    return out.str();
}


srt::sync::Mutex::Mutex()
{
    pthread_mutex_init(&m_mutex, NULL);
}


srt::sync::Mutex::~Mutex()
{
    pthread_mutex_destroy(&m_mutex);
}


int srt::sync::Mutex::lock()
{
    return pthread_mutex_lock(&m_mutex);
}


int srt::sync::Mutex::unlock()
{
    return pthread_mutex_unlock(&m_mutex);
}


bool srt::sync::Mutex::try_lock()
{
    return (pthread_mutex_lock(&m_mutex) == 0);
}


srt::sync::ScopedLock::ScopedLock(Mutex& m)
    : m_mutex(m)
{
    m_mutex.lock();
}


srt::sync::ScopedLock::~ScopedLock()
{
    m_mutex.unlock();
}


//
//
//

srt::sync::UniqueLock::UniqueLock(Mutex& m)
    : m_Mutex(m)
{
    m_iLocked = m_Mutex.lock();
}


srt::sync::UniqueLock::~UniqueLock()
{
    unlock();
}


void srt::sync::UniqueLock::unlock()
{
    if (m_iLocked == 0)
    {
        m_Mutex.unlock();
        m_iLocked = -1;
    }
}



srt::sync::SyncEvent::SyncEvent(bool is_static)
    : m_tick_cond()
{
    if (is_static)
    {
        m_tick_cond = PTHREAD_COND_INITIALIZER;
        return;
    }

    const int res = pthread_cond_init(&m_tick_cond, NULL);
    if (res != 0)
        throw std::runtime_error("pthread_cond_init failed");
}


srt::sync::SyncEvent::~SyncEvent()
{
    pthread_cond_destroy(&m_tick_cond);
}


bool srt::sync::SyncEvent::wait_until(const TimePoint<steady_clock>& tp)
{
    UniqueLock lck(m_tick_lock);

    TimePoint<steady_clock> cur_tp = steady_clock::now();

    if (cur_tp >= tp)
        return true;

    const uint64_t wait_us = count_microseconds(tp - cur_tp);
    // Conversion to microseconds may lose precision, therefore check for 0.
    if (wait_us == 0)
        return true;

    timeval now;
    gettimeofday(&now, 0);
    const uint64_t time_us = now.tv_sec * uint64_t(1000000) + now.tv_usec + wait_us;
    timespec       timeout;
    timeout.tv_sec = time_us / 1000000;
    timeout.tv_nsec = (time_us % 1000000) * 1000;

    pthread_cond_timedwait(&m_tick_cond, &lck.m_Mutex.m_mutex, &timeout);

    cur_tp = steady_clock::now();

    return cur_tp >= tp;
}

void srt::sync::SyncEvent::notify_one()
{
    pthread_cond_signal(&m_tick_cond);
}

void srt::sync::SyncEvent::notify_all()
{
    pthread_cond_broadcast(&m_tick_cond);
}

bool srt::sync::SyncEvent::wait_for(const Duration<steady_clock>& rel_time)
{
    UniqueLock lock(m_tick_lock);
    return wait_for(lock, rel_time);
}

bool srt::sync::SyncEvent::wait_for(UniqueLock& lock, const Duration<steady_clock>& rel_time)
{
    timeval now;
    gettimeofday(&now, 0);
    const uint64_t time_us = now.tv_sec * uint64_t(1000000) + now.tv_usec + count_microseconds(rel_time);
    timespec targettime;
    targettime.tv_sec = time_us / 1000000;
    targettime.tv_nsec = (time_us % 1000000) * 1000;

    return (pthread_cond_timedwait(&m_tick_cond, &lock.m_Mutex.m_mutex, &targettime) == 0);
}

void srt::sync::SyncEvent::wait()
{
    UniqueLock lock(m_tick_lock);
    wait(lock);
}

void srt::sync::SyncEvent::wait(UniqueLock& lk)
{
    pthread_cond_wait(&m_tick_cond, &lk.m_Mutex.m_mutex);
}


////////////////////////////////////////////////////////////////////////////////
//
// Timer
//
////////////////////////////////////////////////////////////////////////////////

srt::sync::Timer::Timer()
{
}


srt::sync::Timer::~Timer()
{
}


bool srt::sync::Timer::sleep_until(TimePoint<steady_clock> tp)
{
    UniqueLock lck(m_event.mutex());
    // Use class member such that the method can be interrupted by others
    m_sched_time = tp;

    TimePoint<steady_clock> cur_tp = steady_clock::now();

    while (cur_tp < m_sched_time)
    {
#if USE_BUSY_WAITING
#ifdef IA32
        __asm__ volatile("pause; rep; nop; nop; nop; nop; nop;");
#elif IA64
        __asm__ volatile("nop 0; nop 0; nop 0; nop 0; nop 0;");
#elif AMD64
        __asm__ volatile("nop; nop; nop; nop; nop;");
#endif
#else
        m_event.wait_until(m_sched_time);
#endif

        cur_tp = steady_clock::now();
    }

    return cur_tp >= m_sched_time;
}


void srt::sync::Timer::interrupt()
{
    UniqueLock lck(m_event.mutex());
    m_sched_time = steady_clock::now();
    m_event.notify_all();
}


void srt::sync::Timer::notify_one()
{
    m_event.notify_one();
}

void srt::sync::Timer::notify_all()
{
    m_event.notify_all();
}

#endif


srt::sync::SyncEvent s_SyncEvent(true);
