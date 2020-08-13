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
#include "utilities.h"
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

////////////////////////////////////////////////////////////////////////////////
//
// Sync utilities section
//
////////////////////////////////////////////////////////////////////////////////

static timespec us_to_timespec(const uint64_t time_us)
{
    timespec timeout;
    timeout.tv_sec         = time_us / 1000000;
    timeout.tv_nsec        = (time_us % 1000000) * 1000;
    return timeout;
}

////////////////////////////////////////////////////////////////////////////////
//
// TimePoint section
//
////////////////////////////////////////////////////////////////////////////////

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

srt::sync::Mutex::Mutex()
{
    const int err = pthread_mutex_init(&m_mutex, 0);
    if (err)
    {
        throw CUDTException(MJ_SYSTEMRES, MN_MEMORY, 0);
    }
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

////////////////////////////////////////////////////////////////////////////////
//
// Condition section (based on pthreads)
//
////////////////////////////////////////////////////////////////////////////////

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


////////////////////////////////////////////////////////////////////////////////
//
// CThread class
//
////////////////////////////////////////////////////////////////////////////////

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


////////////////////////////////////////////////////////////////////////////////
//
// CThreadError class - thread local storage error wrapper
//
////////////////////////////////////////////////////////////////////////////////
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

