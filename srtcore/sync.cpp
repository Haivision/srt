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
#include "srt.h"
#include "srt_compat.h"
#include "logging.h"
#include "common.h"

#if defined(_WIN32)
#define TIMING_USE_QPC
#include "win/wintime.h"
#include <sys/timeb.h>
#elif defined(OSX) || (TARGET_OS_OSX == 1) || (TARGET_OS_IOS == 1) || (TARGET_OS_TV == 1)
#define TIMING_USE_MACH_ABS_TIME
#include <mach/mach_time.h>
#elif defined(ENABLE_MONOTONIC_CLOCK)
#define TIMING_USE_CLOCK_GETTIME
#endif

namespace srt_logging
{
    extern Logger mglog;
}
using namespace srt_logging;

#if !defined(ENABLE_STDCXX_SYNC)
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
#elif defined(TIMING_USE_CLOCK_GETTIME)
    // get_cpu_frequency() returns 1 us accuracy in this case
    timespec tm;
    clock_gettime(CLOCK_MONOTONIC, &tm);
    x = tm.tv_sec * uint64_t(1000000) + (tm.tv_nsec / 1000);
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
#endif // #if !defined(ENABLE_STDCXX_SYNC)

////////////////////////////////////////////////////////////////////////////////
//
// Sync utilities section
//
////////////////////////////////////////////////////////////////////////////////
#ifndef ENABLE_STDCXX_SYNC

static timespec us_to_timespec(const uint64_t time_us)
{
    timespec timeout;
    timeout.tv_sec         = time_us / 1000000;
    timeout.tv_nsec        = (time_us % 1000000) * 1000;
    return timeout;
}
#endif

////////////////////////////////////////////////////////////////////////////////
//
// TimePoint section
//
////////////////////////////////////////////////////////////////////////////////
#ifndef ENABLE_STDCXX_SYNC

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

#endif // !defined(ENABLE_STDCXX_SYNC)

std::string srt::sync::FormatTime(const steady_clock::time_point& timestamp)
{
    if (is_zero(timestamp))
    {
        // Use special string for 0
        return "00:00:00.000000";
    }

    const uint64_t total_us  = count_microseconds(timestamp.time_since_epoch());
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
        floor((static_cast<int64_t>(count_microseconds(now_timestamp.time_since_epoch()) % 1000000) + delta_us) / 1000000.0);
    const time_t tt = now_s + delta_s;
    struct tm    tm = SysLocalTime(tt); // in seconds
    char         tmp_buf[512];
    strftime(tmp_buf, 512, "%X.", &tm);

    ostringstream out;
    out << tmp_buf << setfill('0') << setw(6) << (count_microseconds(timestamp.time_since_epoch()) % 1000000) << " [SYS]";
    return out.str();
}

#if !defined(ENABLE_STDCXX_SYNC)
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
    return (pthread_mutex_trylock(&m_mutex) == 0);
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

#endif // !defined(ENABLE_STDCXX_SYNC)

//
//
//
#if !defined(ENABLE_STDCXX_SYNC)

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

srt::sync::Mutex* srt::sync::UniqueLock::mutex()
{
    return &m_Mutex;
}
#endif // !defined(ENABLE_STDCXX_SYNC)

////////////////////////////////////////////////////////////////////////////////
//
// Condition section (based on pthreads)
//
////////////////////////////////////////////////////////////////////////////////
#ifndef ENABLE_STDCXX_SYNC

namespace srt
{
namespace sync
{

Condition::Condition()
#ifdef _WIN32
    : m_cv(PTHREAD_COND_INITIALIZER)
#endif
{}

Condition::~Condition() {}

void Condition::init()
{
    pthread_condattr_t* attr = NULL;
#if ENABLE_MONOTONIC_CLOCK
    pthread_condattr_t  CondAttribs;
    pthread_condattr_init(&CondAttribs);
    pthread_condattr_setclock(&CondAttribs, CLOCK_MONOTONIC);
    attr = &CondAttribs;
#endif
    const int res = pthread_cond_init(&m_cv, attr);
    if (res != 0)
        throw std::runtime_error("pthread_cond_init monotonic failed");
}

void Condition::destroy()
{
    pthread_cond_destroy(&m_cv);
}

void Condition::wait(UniqueLock& lock)
{
    pthread_cond_wait(&m_cv, &lock.mutex()->ref());
}

bool Condition::wait_for(UniqueLock& lock, const steady_clock::duration& rel_time)
{
    timespec timeout;
#if ENABLE_MONOTONIC_CLOCK
    clock_gettime(CLOCK_MONOTONIC, &timeout);
    const uint64_t now_us = timeout.tv_sec * uint64_t(1000000) + (timeout.tv_nsec / 1000);
#else
    timeval now;
    gettimeofday(&now, 0);
    const uint64_t now_us = now.tv_sec * uint64_t(1000000) + now.tv_usec;
#endif
    timeout = us_to_timespec(now_us + count_microseconds(rel_time));
    return pthread_cond_timedwait(&m_cv, &lock.mutex()->ref(), &timeout) != ETIMEDOUT;
}

bool Condition::wait_until(UniqueLock& lock, const steady_clock::time_point& timeout_time)
{
    // This will work regardless as to which clock is in use. The time
    // should be specified as steady_clock::time_point, so there's no
    // question of the timer base.
    const steady_clock::time_point now = steady_clock::now();
    if (now >= timeout_time)
        return false; // timeout

    // wait_for() is used because it will be converted to pthread-frienly timeout_time inside.
    return wait_for(lock, timeout_time - now);
}

void Condition::notify_one()
{
    pthread_cond_signal(&m_cv);
}

void Condition::notify_all()
{
    pthread_cond_broadcast(&m_cv);
}

}; // namespace sync
}; // namespace srt

#endif // ndef ENABLE_STDCXX_SYNC

////////////////////////////////////////////////////////////////////////////////
//
// CEvent class
//
////////////////////////////////////////////////////////////////////////////////

srt::sync::CEvent::CEvent()
{
#ifndef _WIN32
    m_cond.init();
#endif
}

srt::sync::CEvent::~CEvent()
{
#ifndef _WIN32
    m_cond.destroy();
#endif
}

bool srt::sync::CEvent::lock_wait_until(const TimePoint<steady_clock>& tp)
{
    UniqueLock lock(m_lock);
    return m_cond.wait_until(lock, tp);
}

void srt::sync::CEvent::notify_one()
{
    return m_cond.notify_one();
}

void srt::sync::CEvent::notify_all()
{
    return m_cond.notify_all();
}

bool srt::sync::CEvent::lock_wait_for(const steady_clock::duration& rel_time)
{
    UniqueLock lock(m_lock);
    return m_cond.wait_for(lock, rel_time);
}

bool srt::sync::CEvent::wait_for(UniqueLock& lock, const steady_clock::duration& rel_time)
{
    return m_cond.wait_for(lock, rel_time);
}

void srt::sync::CEvent::lock_wait()
{
    UniqueLock lock(m_lock);
    return wait(lock);
}

void srt::sync::CEvent::wait(UniqueLock& lock)
{
    return m_cond.wait(lock);
}

namespace srt {
namespace sync {

srt::sync::CEvent g_Sync;

} // namespace sync
} // namespace srt

////////////////////////////////////////////////////////////////////////////////
//
// Timer
//
////////////////////////////////////////////////////////////////////////////////

srt::sync::CTimer::CTimer()
{
}


srt::sync::CTimer::~CTimer()
{
}


bool srt::sync::CTimer::sleep_until(TimePoint<steady_clock> tp)
{
    // The class member m_sched_time can be used to interrupt the sleep.
    // Refer to Timer::interrupt().
    enterCS(m_event.mutex());
    m_tsSchedTime = tp;
    leaveCS(m_event.mutex());

#if USE_BUSY_WAITING
#if defined(_WIN32)
    // 10 ms on Windows: bad accuracy of timers
    const steady_clock::duration
        td_threshold = milliseconds_from(10);
#else
    // 1 ms on non-Windows platforms
    const steady_clock::duration
        td_threshold = milliseconds_from(1);
#endif
#endif // USE_BUSY_WAITING

    TimePoint<steady_clock> cur_tp = steady_clock::now();
    
    while (cur_tp < m_tsSchedTime)
    {
#if USE_BUSY_WAITING
        steady_clock::duration td_wait = m_tsSchedTime - cur_tp;
        if (td_wait <= 2 * td_threshold)
            break;

        td_wait -= td_threshold;
        m_event.lock_wait_for(td_wait);
#else
        m_event.lock_wait_until(m_tsSchedTime);
#endif // USE_BUSY_WAITING

        cur_tp = steady_clock::now();
    }

#if USE_BUSY_WAITING
    while (cur_tp < m_tsSchedTime)
    {
#ifdef IA32
        __asm__ volatile ("pause; rep; nop; nop; nop; nop; nop;");
#elif IA64
        __asm__ volatile ("nop 0; nop 0; nop 0; nop 0; nop 0;");
#elif AMD64
        __asm__ volatile ("nop; nop; nop; nop; nop;");
#elif defined(_WIN32) && !defined(__MINGW__)
        __nop();
        __nop();
        __nop();
        __nop();
        __nop();
#endif

        cur_tp = steady_clock::now();
    }
#endif // USE_BUSY_WAITING

    return cur_tp >= m_tsSchedTime;
}


void srt::sync::CTimer::interrupt()
{
    UniqueLock lck(m_event.mutex());
    m_tsSchedTime = steady_clock::now();
    m_event.notify_all();
}


void srt::sync::CTimer::tick()
{
    m_event.notify_one();
}


void srt::sync::CGlobEvent::triggerEvent()
{
    return g_Sync.notify_one();
}

bool srt::sync::CGlobEvent::waitForEvent()
{
    return g_Sync.lock_wait_for(milliseconds_from(10));
}

////////////////////////////////////////////////////////////////////////////////
//
// CThread class
//
////////////////////////////////////////////////////////////////////////////////
#ifndef ENABLE_STDCXX_SYNC

srt::sync::CThread::CThread()
{
    m_thread = pthread_t();
}

srt::sync::CThread::CThread(void *(*start_routine) (void *), void *arg)
{
    create(start_routine, arg);
}

#if HAVE_FULL_CXX11
srt::sync::CThread& srt::sync::CThread::operator=(CThread&& other)
#else
srt::sync::CThread& srt::sync::CThread::operator=(CThread& other)
#endif
{
    if (joinable())
    {
        // If the thread has already terminated, then
        // pthread_join() returns immediately.
        // But we have to check it has terminated before replacing it.
        LOGC(mglog.Error, log << "IPE: Assigning to a thread that is not terminated!");

#ifndef DEBUG
        // In case of production build the hanging thread should be terminated
        // to avoid hang ups and align with C++11 implementation.
        pthread_cancel(m_thread);
#else
        join();
#endif
    }

    // Move thread handler from other
    m_thread = other.m_thread;
    other.m_thread = pthread_t();
    return *this;
}

#if !HAVE_FULL_CXX11
void srt::sync::CThread::create_thread(void *(*start_routine) (void *), void *arg)
{
    SRT_ASSERT(!joinable());
    create(start_routine, arg);
}
#endif

bool srt::sync::CThread::joinable() const
{
    return !pthread_equal(m_thread, pthread_t());
}

void srt::sync::CThread::join()
{
    void *retval;
    const int ret SRT_ATR_UNUSED = pthread_join(m_thread, &retval);
    if (ret != 0)
    {
        LOGC(mglog.Error, log << "pthread_join failed with " << ret);
    }
#ifdef HEAVY_LOGGING
    else
    {
        LOGC(mglog.Debug, log << "pthread_join SUCCEEDED");
    }
#endif
    // After joining, joinable should be false
    m_thread = pthread_t();
    return;
}

void srt::sync::CThread::create(void *(*start_routine) (void *), void *arg)
{
    const int st = pthread_create(&m_thread, NULL, start_routine, arg);
    if (st != 0)
    {
        LOGC(mglog.Error, log << "pthread_create failed with " << st);
        throw CThreadException(MJ_SYSTEMRES, MN_THREAD, 0);
    }
}

#endif // !defined(ENABLE_STDCXX_SYNC)

namespace srt {
namespace sync {

#ifdef ENABLE_STDCXX_SYNC
bool StartThread(CThread& th, ThreadFunc&& f, void* args, const char* name)
#else
bool StartThread(CThread& th, void* (*f) (void*), void* args, const char* name)
#endif
{
    ThreadName tn(name);
    try
    {
#if HAVE_FULL_CXX11 || defined(ENABLE_STDCXX_SYNC)
        th = CThread(f, args);
#else
        // No move semantics in C++03, therefore using a dedicated function
        th.create_thread(f, args);
#endif
    }
    catch (const CThreadException& e)
    {
        HLOGC(mglog.Debug, log << name << ": failed to start thread. " << e.what());
        return false;
    }
    return true;
}

} // namespace sync
} // namespace srt


////////////////////////////////////////////////////////////////////////////////
//
// CThreadError class - thread local storage error wrapper
//
////////////////////////////////////////////////////////////////////////////////
#if !defined(ENABLE_STDCXX_SYNC)
namespace srt {
namespace sync {

class CThreadError
{
public:
    CThreadError()
    {
        pthread_key_create(&m_TLSError, TLSDestroy);
    }

    ~CThreadError()
    {
        delete (CUDTException*)pthread_getspecific(m_TLSError);
        pthread_key_delete(m_TLSError);
    }

public:
    void set(const CUDTException& e)
    {
        CUDTException* cur = get();
        SRT_ASSERT(cur != NULL);
        *cur = e;
    }

    CUDTException* get()
    {
        if (!pthread_getspecific(m_TLSError))
        {
            CUDTException* ne = new CUDTException();
            pthread_setspecific(m_TLSError, ne);
            return ne;
        }
        return (CUDTException*)pthread_getspecific(m_TLSError);
    }

    static void TLSDestroy(void* e)
    {
        delete (CUDTException*)e;
    }

private:
    pthread_key_t m_TLSError;
};

// Threal local error will be used by CUDTUnited
// that has a static scope
static CThreadError s_thErr;

void SetThreadLocalError(const CUDTException& e)
{
    s_thErr.set(e);
}

CUDTException& GetThreadLocalError()
{
    return *s_thErr.get();
}

} // namespace sync
} // namespace srt

#endif // !defined(ENABLE_STDCXX_SYNC)
