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
#include "logging.h"
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

namespace srt_logging
{
extern Logger mglog; // For ThreadCheckAffinity
}

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

#ifdef ENABLE_THREAD_LOGGING
struct CGuardLogMutex
{
    pthread_mutex_t mx;
    CGuardLogMutex()
    {
        pthread_mutex_init(&mx, NULL);
    }

    ~CGuardLogMutex()
    {
        pthread_mutex_destroy(&mx);
    }

    void lock() { pthread_mutex_lock(&mx); }
    void unlock() { pthread_mutex_unlock(&mx); }
};
static CGuardLogMutex g_gmtx;
#endif


// Automatically lock in constructor
CGuard::CGuard(CMutex& lock, explicit_t<bool> shouldwork):
    m_Mutex(lock),
    m_iLocked(-1)
{
#if ENABLE_THREAD_LOGGING
    char errbuf[256];
#endif
    if (shouldwork)
    {
        LOGS(std::cerr, log << "CGuard: { LOCK:" << lock.lockname << " ...");
        Lock();

#if ENABLE_THREAD_ASSERT
        if (m_iLocked != 0)
            abort();
#endif
        LOGS(std::cerr, log << "... " << lock.lockname << " lock state:" <<
                (m_iLocked == 0 ? "locked successfully" : SysStrError(m_iLocked, errbuf, 256)));
    }
    else
    {
        LOGS(std::cerr, log << "CGuard: LOCK NOT DONE (not required):" << lock.lockname);
    }
}

// Automatically unlock in destructor
CGuard::~CGuard()
{
    if (m_iLocked == 0)
    {
        LOGS(std::cerr, log << "CGuard: } UNLOCK:" << m_Mutex.lockname);
        Unlock();
    }
    else
    {
        LOGS(std::cerr, log << "CGuard: UNLOCK NOT DONE (not locked):" << m_Mutex.lockname);
    }
}

int CGuard::enterCS(CMutex& lock, explicit_t<bool> block)
{
    int retval;
    if (block)
    {
        LOGS(std::cerr, log << "enterCS(block) {  LOCK: " << lock.lockname << " ...");
        retval = pthread_mutex_lock(RawAddr(lock));
        LOGS(std::cerr, log << "... " << lock.lockname << " locked.");
    }
    else
    {
        retval = pthread_mutex_trylock(RawAddr(lock));
        LOGS(std::cerr, log << "enterCS(try) {  LOCK: " << lock.lockname << " "
                << (retval == 0 ? " LOCKED." : " FAILED }"));
    }
    return retval;
}

int CGuard::leaveCS(CMutex& lock)
{
    LOGS(std::cerr, log << "leaveCS: } UNLOCK: " << lock.lockname);
    return pthread_mutex_unlock(RawAddr(lock));
}

/// This function checks if the given thread id
/// is a thread id, stating that a thread id variable
/// that doesn't hold a running thread, is equal to
/// a null thread (pthread_t()).
bool isthread(const pthread_t& thr)
{
    return pthread_equal(thr, pthread_t()) == 0; // NOT equal to a null thread
}

bool jointhread(pthread_t& thr)
{
    LOGS(std::cerr, log << "JOIN: " << thr << " ---> " << pthread_self());
    int ret = pthread_join(thr, NULL);
    thr = pthread_t(); // prevent dangling
    return ret == 0;
}

bool jointhread(pthread_t& thr, void*& result)
{
    LOGS(std::cerr, log << "JOIN: " << thr << " ---> " << pthread_self());
    int ret = pthread_join(thr, &result);
    thr = pthread_t();
    return ret == 0;
}

void createMutex(CMutex& lock, const char* name SRT_ATR_UNUSED)
{
    pthread_mutexattr_t* pattr = NULL;
#if ENABLE_THREAD_LOGGING
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    pattr = &attr;
    std::ostringstream cv;
    cv << &lock.in_sysobj;
    if (name)
    {
        cv << "(" << name << ")";
    }
    lock.lockname = cv.str();
#endif
    pthread_mutex_init(RawAddr(lock), pattr);
}

void releaseMutex(CMutex& lock)
{
    pthread_mutex_destroy(RawAddr(lock));
}

void createCond(CCondition& cond, const char* name SRT_ATR_UNUSED)
{
#if ENABLE_THREAD_LOGGING
    std::ostringstream cv;
    cv << &cond.in_sysobj;
    if (name)
    {
        cv << "(" << name << ")";
    }
    cond.cvname = cv.str();
#endif
    pthread_condattr_t* pattr = NULL;
    pthread_cond_init(RawAddr(cond), pattr);
}

void createCond_monotonic(CCondition& cond, const char* name SRT_ATR_UNUSED)
{
#if ENABLE_THREAD_LOGGING
    std::ostringstream cv;
    cv << &cond.in_sysobj;
    if (name)
    {
        cv << "(" << name << ")";
    }
    cond.cvname = cv.str();
#endif

    pthread_condattr_t* pattr = NULL;
#if ENABLE_MONOTONIC_CLOCK
    pthread_condattr_t  CondAttribs;
    pthread_condattr_init(&CondAttribs);
    pthread_condattr_setclock(&CondAttribs, CLOCK_MONOTONIC);
    pattr = &CondAttribs;
#endif
    pthread_cond_init(RawAddr(cond), pattr);
}


void releaseCond(CCondition& cond)
{
    pthread_cond_destroy(RawAddr(cond));
}

CSync::CSync(CCondition& cond, CGuard& g)
    : m_cond(&cond), m_mutex(&g.m_Mutex)
#if ENABLE_THREAD_LOGGING
, m_nolock(false)
#endif
{
#if ENABLE_THREAD_LOGGING
    // This constructor expects that the mutex is locked, and 'g' should designate
    // the CGuard variable that holds the mutex. Test in debug mode whether the
    // mutex is locked
    int lockst = pthread_mutex_trylock(&m_mutex->in_sysobj);
    if (lockst == 0)
    {
        pthread_mutex_unlock(&m_mutex->in_sysobj);
        LOGS(std::cerr, log << "CCond: IPE: Mutex " << m_mutex->lockname << " in CGuard IS NOT LOCKED.");
        return;
    }
#endif
    // XXX it would be nice to check whether the owner is also current thread
    // but this can't be done portable way.

    // When constructed by this constructor, the user is expected
    // to only call signal_locked() function. You should pass the same guard
    // variable that you have used for construction as its argument.
}

CSync::CSync(CCondition& cond, CMutex& mutex, Nolock)
    : m_cond(&cond)
    , m_mutex(&mutex)
#if ENABLE_THREAD_LOGGING
, m_nolock(false)
#endif
{
    // We expect that the mutex is NOT locked at this moment by the current thread,
    // but it is perfectly ok, if the mutex is locked by another thread. We'll just wait.

    // When constructed by this constructor, the user is expected
    // to only call lock_signal() function.
}

void CSync::wait()
{
    LOGS(std::cerr, log << "Cond: WAIT:" << m_cond->cvname << " UNLOCK:" << m_mutex->lockname);
    THREAD_PAUSED();
    pthread_cond_wait(RawAddr(*m_cond), RawAddr(*m_mutex));
    THREAD_RESUMED();
    LOGS(std::cerr, log << "Cond: CAUGHT:" << m_cond->cvname << " LOCKED:" << m_mutex->lockname);
}

bool CSync::wait_until(const steady_clock::time_point& exptime)
{
    // This will work regardless as to which clock is in use. The time
    // should be specified as steady_clock::time_point, so there's no
    // question of the timer base.
    steady_clock::time_point now = steady_clock::now();
    if (now >= exptime)
        return false; // timeout

    LOGS(std::cerr, log << "Cond: WAIT:" << m_cond->cvname << " UNLOCK:" << m_mutex->lockname << " - until " << FormatTime(exptime) << "...");
    THREAD_PAUSED();
    bool signaled = CondWaitFor(m_cond, m_mutex, exptime - now) != ETIMEDOUT;
    THREAD_RESUMED();
    LOGS(std::cerr, log << "Cond: CAUGHT:" << m_cond->cvname << " LOCKED:" << m_mutex->lockname << " REASON:" << (signaled ? "SIGNAL" : "TIMEOUT"));

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

    LOGS(std::cerr, log << "Cond: WAIT:" << m_cond->cvname << " UNLOCK:" << m_mutex->lockname << " - for "
            << count_microseconds(delay) << "us...");
    THREAD_PAUSED();
    bool signaled = CondWaitFor(m_cond, m_mutex, delay) != ETIMEDOUT;
    THREAD_RESUMED();
    LOGS(std::cerr, log << "Cond: CAUGHT:" << m_cond->cvname << " LOCKED:" << m_mutex->lockname << " REASON:" << (signaled ? "SIGNAL" : "TIMEOUT"));

    return signaled;
}

/// Block the call until either @a timestamp time achieved
/// or the conditional is signaled.
/// @param [in] delay Maximum time to wait since the moment of the call
/// @retval true Resumed due to getting a CV signal
/// @retval false Resumed due to being past @a timestamp
bool CSync::wait_for_monotonic(const steady_clock::duration& delay)
{
    // Note: this is implemented this way because the pthread API
    // does not provide a possibility to wait relative time. When
    // you implement it for different API that does provide relative
    /// time waiting, you may want to implement it better way.

    LOGS(std::cerr, log << "Cond: WAIT:" << m_cond->cvname << " UNLOCK:" << m_mutex->lockname << " - for "
            << count_microseconds(delay) << "us...");
    THREAD_PAUSED();
    bool signaled = CondWaitFor_monotonic(m_cond, m_mutex, delay) != ETIMEDOUT;
    THREAD_RESUMED();
    LOGS(std::cerr, log << "Cond: CAUGHT:" << m_cond->cvname << " LOCKED:" << m_mutex->lockname << " REASON:" << (signaled ? "SIGNAL" : "TIMEOUT"));

    return signaled;
}


void CSync::lock_signal()
{
    // We expect m_nolock == true.
#if ENABLE_THREAD_LOGGING
    if (!m_nolock)
    {
        LOGS(std::cerr, log << "Cond: IPE: lock_signal done on LOCKED Cond.");
    }
#endif

    lock_signal(*m_cond, *m_mutex);
}

void CSync::lock_signal(CCondition& cond, CMutex& mutex)
{
    LOGS(std::cerr, log << "Cond: SIGNAL:" << cond.cvname << " { LOCKING: " << mutex.lockname << "...");

    // Not using CGuard here because it would be logged
    // and this will result in unnecessary excessive logging.
    pthread_mutex_lock(RawAddr(mutex));
    LOGS(std::cerr, log << "Cond: ... locked: " << mutex.lockname << " - SIGNAL!");
    pthread_cond_signal(RawAddr(cond));
    pthread_mutex_unlock(RawAddr(mutex));

    LOGS(std::cerr, log << "Cond: } UNLOCK:" << mutex.lockname);
}

void CSync::lock_broadcast(CCondition& cond, CMutex& mutex)
{
    LOGS(std::cerr, log << "Cond: BROADCAST:" << cond.cvname << " { LOCKING: " << mutex.lockname << "...");

    // Not using CGuard here because it would be logged
    // and this will result in unnecessary excessive logging.
    pthread_mutex_lock(RawAddr(mutex));
    LOGS(std::cerr, log << "Cond: ... locked: " << mutex.lockname << " - BROADCAST!");
    pthread_cond_broadcast(RawAddr(cond));
    pthread_mutex_unlock(RawAddr(mutex));

    LOGS(std::cerr, log << "Cond: } UNLOCK:" << mutex.lockname);
}

void CSync::signal_locked(CGuard& lk SRT_ATR_UNUSED)
{
    // We expect m_nolock == false.
#if ENABLE_THREAD_LOGGING
    if (m_nolock)
    {
        LOGS(std::cerr, log << "Cond: IPE: signal done on no-lock-checked Cond.");
    }

    if (&lk.m_Mutex != m_mutex)
    {
        LOGS(std::cerr, log << "Cond: IPE: signal declares CGuard.mutex=" << lk.m_Mutex.lockname << " but Cond.mutex=" << m_mutex->lockname);
    }
    LOGS(std::cerr, log << "Cond: SIGNAL:" << m_cond->cvname << " (with locked:" << m_mutex->lockname << ")");
#endif

    pthread_cond_signal(RawAddr(*m_cond));
}

void CSync::signal_relaxed()
{
    signal_relaxed(*m_cond);
}

void CSync::signal_relaxed(CCondition& cond)
{
    LOGS(std::cerr, log << "Cond: SIGNAL:" << cond.cvname << " (NOT locking anything)");
    pthread_cond_signal(RawAddr(cond));
}

void CSync::broadcast_relaxed(CCondition& cond)
{
    LOGS(std::cerr, log << "Cond: BROADCAST:" << cond.cvname << " (NOT locking anything)");
    pthread_cond_broadcast(RawAddr(cond));
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

int srt::sync::CondWaitFor(pthread_cond_t* cond, pthread_mutex_t* mutex, const Duration<steady_clock>& rel_time)
{
    timespec timeout;
    timeval now;
    gettimeofday(&now, 0);
    const uint64_t now_us = now.tv_sec * uint64_t(1000000) + now.tv_usec;
    timeout = us_to_timespec(now_us + count_microseconds(rel_time));

    return pthread_cond_timedwait(cond, mutex, &timeout);
}

#if ENABLE_MONOTONIC_CLOCK
int srt::sync::CondWaitFor_monotonic(pthread_cond_t* cond, pthread_mutex_t* mutex, const Duration<steady_clock>& rel_time)
{
    timespec timeout;
    clock_gettime(CLOCK_MONOTONIC, &timeout);
    const uint64_t now_us = timeout.tv_sec * uint64_t(1000000) + (timeout.tv_nsec / 1000);
    timeout = us_to_timespec(now_us + count_microseconds(rel_time));

    return pthread_cond_timedwait(cond, mutex, &timeout);
}
#else
int srt::sync::CondWaitFor_monotonic(pthread_cond_t* cond, pthread_mutex_t* mutex, const Duration<steady_clock>& rel_time)
{
    return CondWaitFor(cond, mutex, rel_time);
}
#endif

#if ENABLE_THREAD_LOGGING
void srt::sync::ThreadCheckAffinity(const char* function, pthread_t thr)
{
    using namespace srt_logging;

    if (thr == pthread_self())
        return;

    LOGC(mglog.Fatal, log << "IPE: '" << function << "' should not be executed in this thread!");
    throw std::runtime_error("INTERNAL ERROR: incorrect function affinity");
}
#endif
