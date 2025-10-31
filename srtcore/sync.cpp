/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#include "platform_sys.h"

#include <iomanip>
#include <stdexcept>
#include <cmath>
#include "sync.h"
#include "srt.h"
#include "hvu_compat.h"
#include "hvu_threadname.h"
#include "logging.h"
#include "logger_fas.h"
#include "common.h"

// HAVE_CXX11 is defined in utilities.h, included with common.h. 
// The following conditional inclusion must go after common.h.
#if HAVE_CXX11 
#include <random>
#endif

// IMPORTANT NOTE on the thread-safe initialization of static local variables:
// 1. C++11 guarantees thread-safe deadlock-free initialization
// 2. C++03 STANDARD doesn't give such a guarantee, however compilers do:
//   - gcc, clang, Intel: Implemented and turned on by default, unless
//     overridden by -fno-threadsafe-statics; gcc supports it since version 4.0
//     released in 2005
//   - Microsoft Visual Studio: this is supported since VS 2019
//
// Therefore this code relies everywhere on that the static locals are initialized
// safely in the multithreaded environment and pthread_once() is not in use.

using namespace srt::logging;
using namespace std;

namespace srt
{
namespace sync
{

std::string FormatTime(const steady_clock::time_point& timestamp)
{
    using namespace hvu;
    if (is_zero(timestamp))
    {
        // Use special string for 0
        return "00:00:00.000000 [STDY]";
    }

    const int decimals = clockSubsecondPrecision();
    const uint64_t total_sec = count_seconds(timestamp.time_since_epoch());
    const uint64_t days = total_sec / (60 * 60 * 24);
    const uint64_t hours = total_sec / (60 * 60) - days * 24;
    const uint64_t minutes = total_sec / 60 - (days * 24 * 60) - hours * 60;
    const uint64_t seconds = total_sec - (days * 24 * 60 * 60) - hours * 60 * 60 - minutes * 60;
    steady_clock::time_point frac = timestamp - seconds_from(total_sec);
    ofmtbufstream out;
    if (days)
        out << days << OFMT_RAWSTR("D ");

    fmtc d02 = fmtc().dec().fillzero().width(2),
         dec0 = fmtc().dec().fillzero().width(decimals);

    out << fmt(hours, d02) << OFMT_RAWSTR(":")
        << fmt(minutes, d02) << OFMT_RAWSTR(":")
        << fmt(seconds, d02) << OFMT_RAWSTR(".")
        << fmt(frac.time_since_epoch().count(), dec0)
        << OFMT_RAWSTR(" [STDY]");
    return out.str();
}

std::string FormatTimeSys(const steady_clock::time_point& timestamp)
{
    using namespace hvu;

    const time_t                   now_s         = ::time(NULL); // get current time in seconds
    const steady_clock::time_point now_timestamp = steady_clock::now();
    const int64_t                  delta_us      = count_microseconds(timestamp - now_timestamp);
    const int64_t                  delta_s =
        static_cast<int64_t>(floor((static_cast<double>(count_microseconds(now_timestamp.time_since_epoch()) % 1000000) + delta_us) / 1000000.0));
    const time_t tt = now_s + delta_s;
    struct tm    tm = SysLocalTime(tt); // in seconds
    char         tmp_buf[512];
    size_t tmp_size = strftime(tmp_buf, 512, "%X.", &tm);
    // Mind the theoretically possible error case
    if (!tmp_size)
        return "<TIME FORMAT ERROR>";

    ofmtbufstream out;
    out << fmt_rawstr(tmp_buf, tmp_size)
        << fmt(count_microseconds(timestamp.time_since_epoch()) % 1000000, fmtc().fillzero().width(6))
        << OFMT_RAWSTR(" [SYST]");
    return out.str();
}

std::string FormatDurationAuto(const steady_clock::duration& dur)
{
    int64_t value = count_microseconds(dur);

    if (value < 1000)
        return FormatDuration<DUNIT_US>(dur);

    if (value < 1000000)
        return FormatDuration<DUNIT_MS>(dur);

    return FormatDuration<DUNIT_S>(dur);
}


#ifdef SRT_ENABLE_STDCXX_SYNC
bool StartThread(CThread& th, ThreadFunc&& f, void* args, const string& name)
#else
bool StartThread(CThread& th, void* (*f) (void*), void* args, const string& name)
#endif
{
    hvu::ThreadName tn(name);
    try
    {
#if HAVE_FULL_CXX11 || defined(SRT_ENABLE_STDCXX_SYNC)
        th = CThread(f, args);
#else
        // No move semantics in C++03, therefore using a dedicated function
        th.create_thread(f, args);
#endif
    }
#if HVU_ENABLE_HEAVY_LOGGING
    catch (const CThreadException& e)
#else
    catch (const CThreadException&)
#endif
    {
        HLOGC(inlog.Debug, log << name << ": failed to start thread. " << e.what());
        return false;
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//
// CEvent class
//
////////////////////////////////////////////////////////////////////////////////

CEvent::CEvent()
{
#ifndef _WIN32
    m_cond.init();
#endif
}

CEvent::~CEvent()
{
#ifndef _WIN32
    m_cond.destroy();
#endif
}

bool CEvent::lock_wait_until(const TimePoint<steady_clock>& tp)
{
    UniqueLock lock(m_lock);
    return m_cond.wait_until(lock, tp);
}

void CEvent::notify_one()
{
    return m_cond.notify_one();
}

void CEvent::notify_all()
{
    return m_cond.notify_all();
}

bool CEvent::lock_wait_for(const steady_clock::duration& rel_time)
{
    UniqueLock lock(m_lock);
    return m_cond.wait_for(lock, rel_time);
}

bool CEvent::wait_for(UniqueLock& lock, const steady_clock::duration& rel_time)
{
    return m_cond.wait_for(lock, rel_time);
}

bool CEvent::wait_until(UniqueLock& lock, const TimePoint<steady_clock>& tp)
{
    return m_cond.wait_until(lock, tp);
}

void CEvent::lock_wait()
{
    UniqueLock lock(m_lock);
    return wait(lock);
}

void CEvent::wait(UniqueLock& lock)
{
    return m_cond.wait(lock);
}


CEvent g_Sync;

////////////////////////////////////////////////////////////////////////////////
//
// Timer
//
////////////////////////////////////////////////////////////////////////////////

CTimer::CTimer()
{
}


CTimer::~CTimer()
{
}

// This function sleeps up to the given time, then exits.
// Meanwhile it can be influenced from another thread by calling:
// - tick(): exit waiting, but re-check the end time and fall back to sleep if not reached
// - interrupt(): exit waiting with setting wait time to now() so that it exits immediately
//
// This function returns true if it has exit on the originally set time.
// If the time was changed due to being interrupted and it did really exit before
// that time, false is returned.
bool CTimer::sleep_until(TimePoint<steady_clock> tp)
{
    // The class member m_sched_time can be used to interrupt the sleep.
    // Refer to Timer::interrupt().
    enterCS(m_event.mutex());
    m_tsSchedTime = tp;
    leaveCS(m_event.mutex());

#if SRT_BUSY_WAITING
    wait_busy();
#else
    wait_stalled();
#endif

    // Returning false means that sleep was early interrupted
    return m_tsSchedTime.load() >= tp;
}

void CTimer::wait_stalled()
{
    TimePoint<steady_clock> cur_tp = steady_clock::now();
    {
        UniqueLock elk (m_event.mutex());
        while (cur_tp < m_tsSchedTime.load())
        {
            m_event.wait_until(elk, m_tsSchedTime);
            cur_tp = steady_clock::now();
        }
    }
}

void srt::sync::CTimer::wait_busy()
{
#if defined(_WIN32)
    // 10 ms on Windows: bad accuracy of timers
    const steady_clock::duration
        td_threshold = milliseconds_from(10);
#else
    // 1 ms on non-Windows platforms
    const steady_clock::duration
        td_threshold = milliseconds_from(1);
#endif

    TimePoint<steady_clock> cur_tp = steady_clock::now();
    {
        UniqueLock elk (m_event.mutex());
        while (cur_tp < m_tsSchedTime.load())
        {
            steady_clock::duration td_wait = m_tsSchedTime.load() - cur_tp;
            if (td_wait <= 2 * td_threshold)
                break;

            td_wait -= td_threshold;
            m_event.wait_for(elk, td_wait);

            cur_tp = steady_clock::now();
        }

        while (cur_tp < m_tsSchedTime.load())
        {
            InvertedLock ulk (m_event.mutex());
#ifdef IA32
            __asm__ volatile ("pause; rep; nop; nop; nop; nop; nop;");
#elif IA64
            __asm__ volatile ("nop 0; nop 0; nop 0; nop 0; nop 0;");
#elif AMD64
            __asm__ volatile ("nop; nop; nop; nop; nop;");
#elif defined(_WIN32) && !defined(__MINGW32__)
            __nop();
            __nop();
            __nop();
            __nop();
            __nop();
#endif
            cur_tp = steady_clock::now();
        }
    }
}


void CTimer::interrupt()
{
    UniqueLock lck(m_event.mutex());
    m_tsSchedTime = steady_clock::now();
    m_event.notify_all();
}


void CTimer::tick()
{
    m_event.notify_one();
}


void CGlobEvent::triggerEvent()
{
    return g_Sync.notify_one();
}

bool CGlobEvent::waitForEvent()
{
    return g_Sync.lock_wait_for(milliseconds_from(10));
}

////////////////////////////////////////////////////////////////////////////////
//
// Random
//
////////////////////////////////////////////////////////////////////////////////

#if HAVE_CXX11
int genRandomInt(int minval, int maxval)
{
    // Random state need not be shared between threads, so a better thread safety
    // is ensured with thread_local.
    thread_local std::random_device s_RandomDevice;
    thread_local std::mt19937 s_GenMT19937(s_RandomDevice());
    uniform_int_distribution<> dis(minval, maxval);
    return dis(s_GenMT19937);
}

#else

// The use of rand/srand is racy if the user app is also using it. Therefore we
// prefer rand_r(), but it's not 100% portable.
#if SRT_HAVE_RAND_R
static int randWithSeed()
{
    static unsigned int s_uRandSeed = sync::steady_clock::now().time_since_epoch().count();
    return rand_r(&s_uRandSeed);
}
#else

// Wrapper because srand() can't be used in the initialization expression.
static inline unsigned int srandWrapper(unsigned int seed)
{
    srand(seed);
    return seed;
}
static int randWithSeed()
{
    static unsigned int s_copyseed = srandWrapper(sync::steady_clock::now().time_since_epoch().count());
    (void)s_copyseed; // fake it is used
    return rand();
}
#endif // rand_r or rand

int genRandomInt(int minVal, int maxVal)
{
    // This uses mutex protection with Meyer's singleton; thread-local storage
    // could be better, but requires a complicated solution using
    // pthread_getspecific(3P) and it's not possible with rand().
    // The generator is not used often (Initial Socket ID, Initial sequence
    // number, FileCC), so sharing a single seed among threads should not
    // impact the performance.
    static sync::Mutex s_mtxRandomDevice;
    sync::ScopedLock lck(s_mtxRandomDevice);

    double rand_0_1 = double(randWithSeed()) / RAND_MAX; // range [0.0, 1.0].
    const int64_t stretch = int64_t(maxVal) - minVal;
    return minVal + (stretch * rand_0_1);
}
#endif

#if defined(SRT_ENABLE_STDCXX_SYNC) && HAVE_CXX17

// Shared mutex imp not required - aliased from C++17

#else

////////////////////////////////////////////////////////////////////////////////
//
// Shared Mutex 
//
////////////////////////////////////////////////////////////////////////////////

SharedMutex::SharedMutex()
    : m_LockWriteCond()
    , m_LockReadCond()
    , m_Mutex()
    , m_iCountRead(0)
    , m_bWriterLocked(false)
{
    setupCond(m_LockReadCond, "SharedMutex::m_pLockReadCond");
    setupCond(m_LockWriteCond, "SharedMutex::m_pLockWriteCond");
    setupMutex(m_Mutex, "SharedMutex::m_pMutex");
}

SharedMutex::~SharedMutex()
{
    releaseMutex(m_Mutex);
    releaseCond(m_LockWriteCond);
    releaseCond(m_LockReadCond);
}

void SharedMutex::lock()
{
    UniqueLock l1(m_Mutex);
    while (m_bWriterLocked)
        m_LockWriteCond.wait(l1);

    m_bWriterLocked = true;
    
    while (m_iCountRead)
        m_LockReadCond.wait(l1);
#ifdef SRT_ENABLE_THREAD_DEBUG
    SRT_ASSERT(m_ExclusiveOwner == CThread::id());
    m_ExclusiveOwner = this_thread::get_id();
#endif
}

bool SharedMutex::try_lock()
{
    UniqueLock l1(m_Mutex);
    if (m_bWriterLocked || m_iCountRead > 0)
        return false;
    
    m_bWriterLocked = true;
#ifdef SRT_ENABLE_THREAD_DEBUG
    SRT_ASSERT(m_ExclusiveOwner == CThread::id());
    m_ExclusiveOwner = this_thread::get_id();
#endif
    return true;
}

void SharedMutex::unlock()
{
    ScopedLock lk(m_Mutex);
    m_bWriterLocked = false;
#ifdef SRT_ENABLE_THREAD_DEBUG
    SRT_ASSERT(m_ExclusiveOwner == this_thread::get_id());
    m_ExclusiveOwner = CThread::id();
#endif

    m_LockWriteCond.notify_all();
}

void SharedMutex::lock_shared()
{
    UniqueLock lk(m_Mutex);
    while (m_bWriterLocked)
        m_LockWriteCond.wait(lk);

    m_iCountRead++;
#ifdef SRT_ENABLE_THREAD_DEBUG
    SRT_ASSERT(m_ExclusiveOwner == CThread::id());
    m_SharedOwners.insert(this_thread::get_id());
#endif
}

bool SharedMutex::try_lock_shared()
{
    UniqueLock lk(m_Mutex);
    if (m_bWriterLocked)
        return false;

    m_iCountRead++;
#ifdef SRT_ENABLE_THREAD_DEBUG
    m_SharedOwners.insert(this_thread::get_id());
#endif
    return true;
}

void SharedMutex::unlock_shared()
{
    ScopedLock lk(m_Mutex);

    m_iCountRead--;

    SRT_ASSERT(m_iCountRead >= 0);
    if (m_iCountRead < 0)
        m_iCountRead = 0;

#ifdef SRT_ENABLE_THREAD_DEBUG
    CThread::id me = this_thread::get_id();

    // DO NOT. This is debug-only, while this may happen
    // if you have made a shared lock multiple times in
    // a single thread. While this should not happen in the
    // application, tests may rely on this possibility, so
    // making an assert here is an overkill. A warning might
    // be in order, but there's no mechanism for that.
    // SRT_ASSERT(m_SharedOwners.count(me));
    m_SharedOwners.erase(me);
#endif
    if (m_bWriterLocked && m_iCountRead == 0)
        m_LockReadCond.notify_one();
    
}

int SharedMutex::getReaderCount() const
{
    ScopedLock lk(m_Mutex);
    return m_iCountRead;
}
#endif // C++17 for shared_mutex

}  // END namespace sync
}  // END namespace srt
