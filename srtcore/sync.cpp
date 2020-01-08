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


// Automatically lock in constructor
CGuard::CGuard(pthread_mutex_t& lock, bool shouldwork):
    m_Mutex(lock),
    m_iLocked(-1)
{
    if (shouldwork)
        m_iLocked = pthread_mutex_lock(&m_Mutex);
}

// Automatically unlock in destructor
CGuard::~CGuard()
{
    if (m_iLocked == 0)
        pthread_mutex_unlock(&m_Mutex);
}

// After calling this on a scoped lock wrapper (CGuard),
// the mutex will be unlocked right now, and no longer
// in destructor
void CGuard::forceUnlock()
{
    if (m_iLocked == 0)
    {
        pthread_mutex_unlock(&m_Mutex);
        m_iLocked = -1;
    }
}

int CGuard::enterCS(pthread_mutex_t& lock)
{
    return pthread_mutex_lock(&lock);
}

int CGuard::leaveCS(pthread_mutex_t& lock)
{
    return pthread_mutex_unlock(&lock);
}

void CGuard::createMutex(pthread_mutex_t& lock)
{
    pthread_mutex_init(&lock, NULL);
}

void CGuard::releaseMutex(pthread_mutex_t& lock)
{
    pthread_mutex_destroy(&lock);
}

void CGuard::createCond(pthread_cond_t& cond)
{
    pthread_cond_init(&cond, NULL);
}

void CGuard::releaseCond(pthread_cond_t& cond)
{
    pthread_cond_destroy(&cond);
}



CSync::CSync(pthread_cond_t& cond, CGuard& g)
    : m_cond(&cond), m_mutex(&g.m_Mutex)
{
    // XXX it would be nice to check whether the owner is also current thread
    // but this can't be done portable way.

    // When constructed by this constructor, the user is expected
    // to only call signal_locked() function. You should pass the same guard
    // variable that you have used for construction as its argument.
}

CSync::CSync(pthread_cond_t& cond, pthread_mutex_t& mutex, Nolock)
    : m_cond(&cond)
    , m_mutex(&mutex)
{
    // We expect that the mutex is NOT locked at this moment by the current thread,
    // but it is perfectly ok, if the mutex is locked by another thread. We'll just wait.

    // When constructed by this constructor, the user is expected
    // to only call lock_signal() function.
}

void CSync::wait()
{
    THREAD_PAUSED();
    pthread_cond_wait(&(*m_cond), &(*m_mutex));
    THREAD_RESUMED();
}

bool CSync::wait_until(const steady_clock::time_point& exptime)
{
    // This will work regardless as to which clock is in use. The time
    // should be specified as steady_clock::time_point, so there's no
    // question of the timer base.
    steady_clock::time_point now = steady_clock::now();
    if (now >= exptime)
        return false; // timeout

    THREAD_PAUSED();
    bool signaled = SyncEvent::wait_for(m_cond, m_mutex, exptime - now) != ETIMEDOUT;
    THREAD_RESUMED();

    return signaled;
}

/// Block the call until either @a timestamp time achieved
/// or the conditional is signaled.
/// @param [in] delay Maximum time to wait since the moment of the call
/// @retval true Resumed due to getting a CV signal
/// @retval false Resumed due to being past @a timestamp
bool CSync::wait_for(const steady_clock::duration& delay)
{
    // Note: this is implemented this way because the pthread API
    // does not provide a possibility to wait relative time. When
    // you implement it for different API that does provide relative
    /// time waiting, you may want to implement it better way.

    THREAD_PAUSED();
    bool signaled = SyncEvent::wait_for(m_cond, m_mutex, delay) != ETIMEDOUT;
    THREAD_RESUMED();

    return signaled;
}

void CSync::lock_signal()
{
    // We expect m_nolock == true.
    lock_signal(*m_cond, *m_mutex);
}

void CSync::lock_signal(pthread_cond_t& cond, pthread_mutex_t& mutex)
{
    // Not using CGuard here because it would be logged
    // and this will result in unnecessary excessive logging.
    pthread_mutex_lock(&(mutex));
    pthread_cond_signal(&(cond));
    pthread_mutex_unlock(&(mutex));
}

void CSync::lock_broadcast(pthread_cond_t& cond, pthread_mutex_t& mutex)
{
    // Not using CGuard here because it would be logged
    // and this will result in unnecessary excessive logging.
    pthread_mutex_lock(&(mutex));
    pthread_cond_broadcast(&(cond));
    pthread_mutex_unlock(&(mutex));
}

void CSync::signal_locked(CGuard& lk SRT_ATR_UNUSED)
{
    // We expect m_nolock == false.
    pthread_cond_signal(&(*m_cond));
}

void CSync::signal_relaxed()
{
    signal_relaxed(*m_cond);
}

void CSync::signal_relaxed(pthread_cond_t& cond)
{
    pthread_cond_signal(&(cond));
}

void CSync::broadcast_relaxed(pthread_cond_t& cond)
{
    pthread_cond_broadcast(&(cond));
}

} // namespace sync
} // namespace srt

template <>
uint64_t srt::sync::TimePoint<srt::sync::steady_clock>::us_since_epoch() const
{
    return m_timestamp / s_cpu_frequency;
}

timespec srt::sync::us_to_timespec(const uint64_t time_us)
{
    timespec timeout;
    timeout.tv_sec         = time_us / 1000000;
    timeout.tv_nsec        = (time_us % 1000000) * 1000;
    return timeout;
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
    const uint64_t total_us  = timestamp.us_since_epoch();
    const uint64_t us        = total_us % 1000000;
    const uint64_t total_sec = total_us / 1000000;

    const uint64_t days  = total_sec / (60 * 60 * 24);
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
    const time_t                   now_s         = ::time(NULL); // get current time in seconds
    const steady_clock::time_point now_timestamp = steady_clock::now();
    const int64_t                  delta_us      = count_microseconds(timestamp - now_timestamp);
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

int srt::sync::SyncEvent::wait_for(pthread_cond_t* cond, pthread_mutex_t* mutex, const Duration<steady_clock>& rel_time)
{
    timespec timeout;
    timeval now;
    gettimeofday(&now, 0);
    const uint64_t now_us = now.tv_sec * uint64_t(1000000) + now.tv_usec;
    timeout = us_to_timespec(now_us + count_microseconds(rel_time));

    return pthread_cond_timedwait(cond, mutex, &timeout);
}

#if ENABLE_MONOTONIC_CLOCK
int srt::sync::SyncEvent::wait_for_monotonic(pthread_cond_t* cond, pthread_mutex_t* mutex, const Duration<steady_clock>& rel_time)
{
    timespec timeout;
    clock_gettime(CLOCK_MONOTONIC, &timeout);
    const uint64_t now_us = timeout.tv_sec * uint64_t(1000000) + (timeout.tv_nsec / 1000);
    timeout = us_to_timespec(now_us + count_microseconds(rel_time));

    return pthread_cond_timedwait(cond, mutex, &timeout);
}
#else
int srt::sync::SyncEvent::wait_for_monotonic(pthread_cond_t* cond, pthread_mutex_t* mutex, const Duration<steady_clock>& rel_time)
{
    return wait_for(cond, mutex, rel_time);
}
#endif
