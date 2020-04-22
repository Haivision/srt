/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2019 Haivision Systems Inc.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */
#pragma once
#ifndef __SRT_SYNC_H__
#define __SRT_SYNC_H__

//#define USE_STDCXX_CHRONO
//#define ENABLE_CXX17

#include <cstdlib>
#ifdef USE_STDCXX_CHRONO
#include <chrono>
#include <thread>
#else
#include <pthread.h>
#endif
#include "utilities.h"

class CUDTException;    // defined in common.h

namespace srt
{
namespace sync
{
using namespace std;

template <class _Clock>
class Duration
{
public:
    Duration()
        : m_duration(0)
    {
    }

    explicit Duration(int64_t d)
        : m_duration(d)
    {
    }

public:
    inline int64_t count() const { return m_duration; }

    static Duration zero() { return Duration(); }

public: // Relational operators
    inline bool operator>=(const Duration& rhs) const { return m_duration >= rhs.m_duration; }
    inline bool operator>(const Duration& rhs) const { return m_duration > rhs.m_duration; }
    inline bool operator==(const Duration& rhs) const { return m_duration == rhs.m_duration; }
    inline bool operator!=(const Duration& rhs) const { return m_duration != rhs.m_duration; }
    inline bool operator<=(const Duration& rhs) const { return m_duration <= rhs.m_duration; }
    inline bool operator<(const Duration& rhs) const { return m_duration < rhs.m_duration; }

public: // Assignment operators
    inline void operator*=(const double mult) { m_duration = static_cast<int64_t>(m_duration * mult); }
    inline void operator+=(const Duration& rhs) { m_duration += rhs.m_duration; }
    inline void operator-=(const Duration& rhs) { m_duration -= rhs.m_duration; }

    inline Duration operator+(const Duration& rhs) const { return Duration(m_duration + rhs.m_duration); }
    inline Duration operator-(const Duration& rhs) const { return Duration(m_duration - rhs.m_duration); }
    inline Duration operator*(const int& rhs) const { return Duration(m_duration * rhs); }

private:
    // int64_t range is from -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807
    int64_t m_duration;
};

template <class _Clock>
class TimePoint;

class steady_clock
{
public:
    typedef Duration<steady_clock>  duration;
    typedef TimePoint<steady_clock> time_point;

public:
    static time_point now();
    static time_point zero();
};

template <class _Clock>
class TimePoint
{ // represents a point in time
public:
    TimePoint()
        : m_timestamp(0)
    {
    }

    explicit TimePoint(uint64_t tp)
        : m_timestamp(tp)
    {
    }

    TimePoint(const TimePoint<_Clock>& other)
        : m_timestamp(other.m_timestamp)
    {
    }

    ~TimePoint() {}

public: // Relational operators
    inline bool operator<(const TimePoint<_Clock>& rhs) const { return m_timestamp < rhs.m_timestamp; }
    inline bool operator<=(const TimePoint<_Clock>& rhs) const { return m_timestamp <= rhs.m_timestamp; }
    inline bool operator==(const TimePoint<_Clock>& rhs) const { return m_timestamp == rhs.m_timestamp; }
    inline bool operator!=(const TimePoint<_Clock>& rhs) const { return m_timestamp != rhs.m_timestamp; }
    inline bool operator>=(const TimePoint<_Clock>& rhs) const { return m_timestamp >= rhs.m_timestamp; }
    inline bool operator>(const TimePoint<_Clock>& rhs) const { return m_timestamp > rhs.m_timestamp; }

public: // Arithmetic operators
    inline Duration<_Clock> operator-(const TimePoint<_Clock>& rhs) const
    {
        return Duration<_Clock>(m_timestamp - rhs.m_timestamp);
    }
    inline TimePoint operator+(const Duration<_Clock>& rhs) const { return TimePoint(m_timestamp + rhs.count()); }
    inline TimePoint operator-(const Duration<_Clock>& rhs) const { return TimePoint(m_timestamp - rhs.count()); }

public: // Assignment operators
    inline void operator=(const TimePoint<_Clock>& rhs) { m_timestamp = rhs.m_timestamp; }
    inline void operator+=(const Duration<_Clock>& rhs) { m_timestamp += rhs.count(); }
    inline void operator-=(const Duration<_Clock>& rhs) { m_timestamp -= rhs.count(); }

public: //
#if HAVE_FULL_CXX11
    static inline ATR_CONSTEXPR TimePoint min() { return TimePoint(numeric_limits<uint64_t>::min()); }
    static inline ATR_CONSTEXPR TimePoint max() { return TimePoint(numeric_limits<uint64_t>::max()); }
#else
#ifndef UINT64_MAX
#define UNDEF_UINT64_MAX
#define UINT64_MAX 0xffffffffffffffffULL
#endif
    static inline TimePoint min() { return TimePoint(0); }
    static inline TimePoint max() { return TimePoint(UINT64_MAX); }

#ifdef UNDEF_UINT64_MAX
#undef UINT64_MAX
#endif
#endif

public:
    uint64_t         us_since_epoch() const;
    Duration<_Clock> time_since_epoch() const;

public:
    bool is_zero() const { return m_timestamp == 0; }

private:
    uint64_t m_timestamp;
};

inline TimePoint<srt::sync::steady_clock> steady_clock::zero()
{
    return TimePoint<steady_clock>(0);
}


template <>
uint64_t srt::sync::TimePoint<srt::sync::steady_clock>::us_since_epoch() const;

template <>
srt::sync::Duration<srt::sync::steady_clock> srt::sync::TimePoint<srt::sync::steady_clock>::time_since_epoch() const;

inline Duration<steady_clock> operator*(const int& lhs, const Duration<steady_clock>& rhs)
{
    return rhs * lhs;
}

inline int64_t count_microseconds(const TimePoint<steady_clock> tp)
{
    return static_cast<int64_t>(tp.us_since_epoch());
}

int64_t count_microseconds(const steady_clock::duration& t);
int64_t count_milliseconds(const steady_clock::duration& t);
int64_t count_seconds(const steady_clock::duration& t);

Duration<steady_clock> microseconds_from(int64_t t_us);
Duration<steady_clock> milliseconds_from(int64_t t_ms);
Duration<steady_clock> seconds_from(int64_t t_s);

inline bool is_zero(const TimePoint<steady_clock>& t) { return t.is_zero(); }


///////////////////////////////////////////////////////////////////////////////
//
// Mutex section
//
///////////////////////////////////////////////////////////////////////////////

/// Mutex is a class wrapper, that should mimic the std::chrono::mutex class.
/// At the moment the extra function ref() is temporally added to allow calls
/// to pthread_cond_timedwait(). Will be removed by introducing CEvent.
class Mutex
{
    friend class SyncEvent;

public:
    Mutex();
    ~Mutex();

public:
    int lock();
    int unlock();

    /// @return     true if the lock was acquired successfully, otherwise false
    bool try_lock();

    // TODO: To be removed with introduction of the CEvent.
    pthread_mutex_t& ref() { return m_mutex; }

private:
    pthread_mutex_t m_mutex;
};

/// A pthread version of std::chrono::scoped_lock<mutex> (or lock_guard for C++11)
class ScopedLock
{
public:
    ScopedLock(Mutex& m);
    ~ScopedLock();

private:
    Mutex& m_mutex;
};

/// A pthread version of std::chrono::unique_lock<mutex>
class UniqueLock
{
    friend class SyncEvent;

public:
    UniqueLock(Mutex &m);
    ~UniqueLock();

public:
    void unlock();
    Mutex* mutex(); // reflects C++11 unique_lock::mutex()

private:
    int m_iLocked;
    Mutex& m_Mutex;
};

/// The purpose of this typedef is to reduce the number of changes in the code (renamings)
/// and produce less merge conflicts with some other parallel work done.
/// TODO: Replace CGuard with ScopedLock. Use UniqueLock only when required.
typedef UniqueLock CGuard;


inline void enterCS(Mutex& m) { m.lock(); }
inline bool tryEnterCS(Mutex& m) { return m.try_lock(); }
inline void leaveCS(Mutex& m) { m.unlock(); }

class InvertedLock
{
    Mutex *m_pMutex;

  public:
    InvertedLock(Mutex *m)
        : m_pMutex(m)
    {
        if (!m_pMutex)
            return;

        leaveCS(*m_pMutex);
    }

    InvertedLock(Mutex& m)
        : m_pMutex(&m)
    {
        leaveCS(*m_pMutex);
    }

    ~InvertedLock()
    {
        if (!m_pMutex)
            return;
        enterCS(*m_pMutex);
    }
};

inline void setupMutex(Mutex&, const char*) {}
inline void releaseMutex(Mutex&) {}

////////////////////////////////////////////////////////////////////////////////
//
// Condition section
//
////////////////////////////////////////////////////////////////////////////////

class Condition
{
public:
    Condition();
    ~Condition();

public:
    /// These functions do not align with C++11 version. They are here hopefully as a temporal solution
    /// to avoud issues with static initialization of CV on windows.
    void init();
    void destroy();

public:
    /// Causes the current thread to block until the condition variable is notified
    /// or a spurious wakeup occurs.
    ///
    /// @param lock Corresponding mutex locked by UniqueLock
    void wait(UniqueLock& lock);

    /// Atomically releases lock, blocks the current executing thread, 
    /// and adds it to the list of threads waiting on *this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously. When unblocked, regardless of the reason,
    /// lock is reacquired and wait_for() exits.
    ///
    /// @returns false if the relative timeout specified by rel_time expired,
    ///          true otherwise (signal or spurious wake up).
    ///
    /// @note Calling this function if lock.mutex()
    /// is not locked by the current thread is undefined behavior.
    /// Calling this function if lock.mutex() is not the same mutex as the one
    /// used by all other threads that are currently waiting on the same
    /// condition variable is undefined behavior.
    bool wait_for(UniqueLock& lock, const steady_clock::duration& rel_time);

    /// Causes the current thread to block until the condition variable is notified,
    /// a specific time is reached, or a spurious wakeup occurs.
    ///
    /// @param[in] lock  an object of type UniqueLock, which must be locked by the current thread 
    /// @param[in] timeout_time an object of type time_point representing the time when to stop waiting 
    ///
    /// @returns false if the relative timeout specified by timeout_time expired,
    ///          true otherwise (signal or spurious wake up).
    bool wait_until(UniqueLock& lock, const steady_clock::time_point& timeout_time);

    /// Calling notify_one() unblocks one of the waiting threads,
    /// if any threads are waiting on this CV.
    void notify_one();

    /// Unblocks all threads currently waiting for this CV.
    void notify_all();

private:
#ifdef USE_STDCXX_CHRONO
    condition_variable m_cv;
#else
    pthread_cond_t  m_cv;
#endif
};

inline void setupCond(Condition& cv, const char*) { cv.init(); }
inline void releaseCond(Condition& cv) { cv.destroy(); }

///////////////////////////////////////////////////////////////////////////////
//
// Event (CV) section
//
///////////////////////////////////////////////////////////////////////////////

inline void SleepFor(const steady_clock::duration& t)
{
#ifndef _WIN32
    usleep(count_microseconds(t)); // microseconds
#else
    Sleep(count_milliseconds(t));
#endif
}

// This class is used for condition variable combined with mutex by different ways.
// This should provide a cleaner API around locking with debug-logging inside.
class CSync
{
    Condition* m_cond;
    CGuard* m_locker;

public:
    // Locked version: must be declared only after the declaration of CGuard,
    // which has locked the mutex. On this delegate you should call only
    // signal_locked() and pass the CGuard variable that should remain locked.
    // Also wait() and wait_for() can be used only with this socket.
    CSync(Condition& cond, CGuard& g)
        : m_cond(&cond), m_locker(&g)
    {
        // XXX it would be nice to check whether the owner is also current thread
        // but this can't be done portable way.

        // When constructed by this constructor, the user is expected
        // to only call signal_locked() function. You should pass the same guard
        // variable that you have used for construction as its argument.
    }

    // COPY CONSTRUCTOR: DEFAULT!

    // Wait indefinitely, until getting a signal on CV.
    void wait()
    {
        m_cond->wait(*m_locker);
    }

    /// Block the call until either @a timestamp time achieved
    /// or the conditional is signaled.
    /// @param [in] delay Maximum time to wait since the moment of the call
    /// @retval true Resumed due to getting a CV signal
    /// @retval false Resumed due to being past @a timestamp
    bool wait_for(const steady_clock::duration& delay)
    {
        return m_cond->wait_for(*m_locker, delay);
    }

    // Wait until the given time is achieved. This actually
    // refers to wait_for for the time remaining to achieve
    // given time.
    bool wait_until(const steady_clock::time_point& exptime)
    {
        // This will work regardless as to which clock is in use. The time
        // should be specified as steady_clock::time_point, so there's no
        // question of the timer base.
        steady_clock::time_point now = steady_clock::now();
        if (now >= exptime)
            return false; // timeout

        return wait_for(exptime - now);
    }

    // Static ad-hoc version
    static void lock_signal(Condition& cond, Mutex& m)
    {
        CGuard lk(m); // XXX with thread logging, don't use CGuard directly!
        cond.notify_one();
    }

    static void lock_broadcast(Condition& cond, Mutex& m)
    {
        CGuard lk(m); // XXX with thread logging, don't use CGuard directly!
        cond.notify_all();
    }

    void signal_locked(CGuard& lk ATR_UNUSED)
    {
        // EXPECTED: lk.mutex() is LOCKED.
        m_cond->notify_one();
    }

    // The signal_relaxed and broadcast_relaxed functions are to be used in case
    // when you don't care whether the associated mutex is locked or not (you
    // accept the case that a mutex isn't locked and the signal gets effectively
    // missed), or you somehow know that the mutex is locked, but you don't have
    // access to the associated CGuard object. This function, although it does
    // the same thing as signal_locked() and broadcast_locked(), is here for
    // the user to declare explicitly that the signal/broadcast is done without
    // being prematurely certain that the associated mutex is locked.
    //
    // It is then expected that whenever these functions are used, an extra
    // comment is provided to explain, why the use of the relaxed signaling is
    // correctly used.

    void signal_relaxed() { signal_relaxed(*m_cond); }
    static void signal_relaxed(Condition& cond) { cond.notify_one(); }
    static void broadcast_relaxed(Condition& cond) { cond.notify_all(); }
};

////////////////////////////////////////////////////////////////////////////////
//
// CEvent class
//
////////////////////////////////////////////////////////////////////////////////

class CEvent
{
public:
    CEvent();
    ~CEvent();

public:
    Mutex& mutex() { return m_lock; }

public:
    /// Causes the current thread to block until
    /// a specific time is reached.
    ///
    /// @return true  if condition occured or spuriously woken up
    ///         false on timeout
    bool lock_wait_until(const steady_clock::time_point& tp);

    /// Blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// Uses internal mutex to lock.
    ///
    /// @return true  if condition occured or spuriously woken up
    ///         false on timeout
    bool lock_wait_for(const steady_clock::duration& rel_time);

    /// Atomically releases lock, blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// When unblocked, regardless of the reason, lock is reacquiredand wait_for() exits.
    ///
    /// @return true  if condition occured or spuriously woken up
    ///         false on timeout
    bool wait_for(UniqueLock& lk, const steady_clock::duration& rel_time);

    void lock_wait();

    void wait(UniqueLock& lk);

    void notify_one();

    void notify_all();

private:
    Mutex      m_lock;
    Condition  m_cond;
};


class CTimer
{
public:
    CTimer();
    ~CTimer();

public:
    /// Causes the current thread to block until
    /// the specified time is reached.
    /// Sleep can be interrupted by calling interrupt()
    /// or woken up to recheck the scheduled time by tick()
    /// @param tp target time to sleep until
    ///
    /// @return true  if the specified time was reached
    ///         false should never happen
    bool sleep_until(steady_clock::time_point tp);

    /// Resets target wait time and interrupts waiting
    /// in sleep_until(..)
    void interrupt();

    /// Wakes up waiting thread (sleep_until(..)) without
    /// changing the target waiting time to force a recheck
    /// of the current time in comparisson to the target time.
    void tick();

private:
    CEvent m_event;
    steady_clock::time_point m_tsSchedTime;
};


/// Print steady clock timepoint in a human readable way.
/// days HH:MM::SS.us [STD]
/// Example: 1D 02:12:56.123456
///
/// @param [in] steady clock timepoint
/// @returns a string with a formatted time representation
std::string FormatTime(const steady_clock::time_point& time);

/// Print steady clock timepoint relative to the current system time
/// Date HH:MM::SS.us [SYS]
/// @param [in] steady clock timepoint
/// @returns a string with a formatted time representation
std::string FormatTimeSys(const steady_clock::time_point& time);

enum eDurationUnit {DUNIT_S, DUNIT_MS, DUNIT_US};

template <eDurationUnit u>
struct DurationUnitName;

template<>
struct DurationUnitName<DUNIT_US>
{
    static const char* name() { return "us"; }
    static double count(const steady_clock::duration& dur) { return count_microseconds(dur); }
};

template<>
struct DurationUnitName<DUNIT_MS>
{
    static const char* name() { return "ms"; }
    static double count(const steady_clock::duration& dur) { return count_microseconds(dur)/1000.0; }
};

template<>
struct DurationUnitName<DUNIT_S>
{
    static const char* name() { return "s"; }
    static double count(const steady_clock::duration& dur) { return count_microseconds(dur)/1000000.0; }
};

template<eDurationUnit UNIT>
inline std::string FormatDuration(const steady_clock::duration& dur)
{
    return Sprint(DurationUnitName<UNIT>::count(dur)) + DurationUnitName<UNIT>::name();
}

inline std::string FormatDuration(const steady_clock::duration& dur)
{
    return FormatDuration<DUNIT_US>(dur);
}

////////////////////////////////////////////////////////////////////////////////
//
// CGlobEvent class
//
////////////////////////////////////////////////////////////////////////////////

class CGlobEvent
{
public:
    /// Triggers the event and notifies waiting threads.
    /// Simply calls notify_one().
    static void triggerEvent();

    /// Waits for the event to be triggered with 10ms timeout.
    /// Simply calls wait_for().
    static bool waitForEvent();
};

////////////////////////////////////////////////////////////////////////////////
//
// CThread class
//
////////////////////////////////////////////////////////////////////////////////

#ifdef USE_STDCXX_CHRONO
typedef std::system_error CThreadException;
using CThread = std::thread;
#else // pthreads wrapper version
typedef ::CUDTException CThreadException;

class CThread
{
public:
    CThread();
    /// @throws std::system_error if the thread could not be started.
    CThread(void *(*start_routine) (void *), void *arg);

#if HAVE_FULL_CXX11
    CThread& operator=(CThread &other) = delete;
    CThread& operator=(CThread &&other);
#else
    CThread& operator=(CThread &other);
    /// To be used only in StartThread function.
    /// Creates a new stread and assigns to this.
    /// @throw CThreadException
    inline void create_thread(void *(*start_routine) (void *), void *arg);
#endif

public: // Observers
    /// Checks if the CThread object identifies an active thread of execution.
    /// A default constructed thread is not joinable.
    /// A thread that has finished executing code, but has not yet been joined
    /// is still considered an active thread of execution and is therefore joinable.
    bool joinable() const;

public:
    /// Blocks the current thread until the thread identified by *this finishes its execution.
    /// If that thread has already terminated, then join() returns immediately.
    ///
    /// @throws std::system_error if an error occurs
    void join();

public: // Internal
    /// Calls pthread_create, throws exception on failure.
    /// @throw CThreadException
    void create(void *(*start_routine) (void *), void *arg);

private:
    pthread_t m_thread;
};
#endif

/// StartThread function should be used to do CThread assignments:
/// @code
/// CThread a();
/// a = CThread(func, args);
/// @endcode
///
/// @returns true if thread was started successfully,
///          false on failure
///
#ifdef USE_STDCXX_CHRONO
template< class Function >
bool StartThread(CThread& th, Function&& f, void* args, const char* name);
#else
bool StartThread(CThread& th, void* (*f) (void*), void* args, const char* name);
#endif

}; // namespace sync
}; // namespace srt

#endif // __SRT_SYNC_H__
