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
#ifndef INC_SRT_SYNC_H
#define INC_SRT_SYNC_H

#include "platform_sys.h"
#include "srt_attr_defs.h"

#include <cstdlib>
#include <limits>
#ifdef SRT_ENABLE_STDCXX_SYNC
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#if HAVE_CXX17
#include <shared_mutex>
#endif
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_STDCXX_STEADY
#define SRT_SYNC_CLOCK_STR "STDCXX_STEADY"
#else
#include <pthread.h>

// Defile clock type to use
#ifdef IA32
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_IA32_RDTSC
#define SRT_SYNC_CLOCK_STR "IA32_RDTSC"
#elif defined(IA64)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_IA64_ITC
#define SRT_SYNC_CLOCK_STR "IA64_ITC"
#elif defined(AMD64)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_AMD64_RDTSC
#define SRT_SYNC_CLOCK_STR "AMD64_RDTSC"
#elif defined(_WIN32)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_WINQPC
#define SRT_SYNC_CLOCK_STR "WINQPC"
#elif TARGET_OS_MAC
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_MACH_ABSTIME
#define SRT_SYNC_CLOCK_STR "MACH_ABSTIME"
#elif defined(SRT_ENABLE_MONOTONIC_CLOCK)
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_GETTIME_MONOTONIC
#define SRT_SYNC_CLOCK_STR "GETTIME_MONOTONIC"
#else
#define SRT_SYNC_CLOCK SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY
#define SRT_SYNC_CLOCK_STR "POSIX_GETTIMEOFDAY"
#endif

#endif // SRT_ENABLE_STDCXX_SYNC

// Force defined
#ifndef SRT_BUSY_WAITING
#define SRT_BUSY_WAITING 0
#endif

#include "srt.h"
#include "utilities.h"
#include "atomic_clock.h"
#include "ofmt.h"
#ifdef SRT_ENABLE_THREAD_DEBUG
#include <set>
#endif

namespace srt
{

class CUDTException;    // defined in common.h

namespace sync
{

///////////////////////////////////////////////////////////////////////////////
//
// Duration class
//
///////////////////////////////////////////////////////////////////////////////

#if SRT_ENABLE_STDCXX_SYNC

template <class Clock>
using Duration = std::chrono::duration<Clock>;

#else

/// Class template srt::sync::Duration represents a time interval.
/// It consists of a count of ticks of _Clock.
/// It is a wrapper of system timers in case of non-C++11 chrono build.
template <class Clock>
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
    inline void operator*=(const int64_t mult) { m_duration = static_cast<int64_t>(m_duration * mult); }
    inline void operator+=(const Duration& rhs) { m_duration += rhs.m_duration; }
    inline void operator-=(const Duration& rhs) { m_duration -= rhs.m_duration; }

    inline Duration operator+(const Duration& rhs) const { return Duration(m_duration + rhs.m_duration); }
    inline Duration operator-(const Duration& rhs) const { return Duration(m_duration - rhs.m_duration); }
    inline Duration operator*(const int64_t& rhs) const { return Duration(m_duration * rhs); }
    inline Duration operator/(const int64_t& rhs) const { return Duration(m_duration / rhs); }

private:
    // int64_t range is from -9,223,372,036,854,775,808 to 9,223,372,036,854,775,807
    int64_t m_duration;
};

#endif // SRT_ENABLE_STDCXX_SYNC

///////////////////////////////////////////////////////////////////////////////
//
// TimePoint and steadt_clock classes
//
///////////////////////////////////////////////////////////////////////////////

#if SRT_ENABLE_STDCXX_SYNC

using steady_clock = std::chrono::steady_clock;

template <class Clock, class Duration = typename Clock::duration>
using time_point = std::chrono::time_point<Clock, Duration>;

template <class Clock>
using TimePoint = std::chrono::time_point<Clock>;

template <class Clock, class Duration = typename Clock::duration>
inline bool is_zero(const time_point<Clock, Duration> &tp)
{
    return tp.time_since_epoch() == Clock::duration::zero();
}

inline bool is_zero(const steady_clock::time_point& t)
{
    return t == steady_clock::time_point();
}

#else
template <class Clock>
class TimePoint;

class steady_clock
{
public:
    typedef Duration<steady_clock>  duration;
    typedef TimePoint<steady_clock> time_point;

public:
    static time_point now();
};

/// Represents a point in time
template <class Clock>
class TimePoint
{
public:
    TimePoint()
        : m_timestamp(0)
    {
    }

    explicit TimePoint(uint64_t tp)
        : m_timestamp(tp)
    {
    }

    TimePoint(const TimePoint<Clock>& other)
        : m_timestamp(other.m_timestamp)
    {
    }

    TimePoint(const Duration<Clock>& duration_since_epoch)
        : m_timestamp(duration_since_epoch.count())
    {
    }

    ~TimePoint() {}

public: // Relational operators
    inline bool operator<(const TimePoint<Clock>& rhs) const { return m_timestamp < rhs.m_timestamp; }
    inline bool operator<=(const TimePoint<Clock>& rhs) const { return m_timestamp <= rhs.m_timestamp; }
    inline bool operator==(const TimePoint<Clock>& rhs) const { return m_timestamp == rhs.m_timestamp; }
    inline bool operator!=(const TimePoint<Clock>& rhs) const { return m_timestamp != rhs.m_timestamp; }
    inline bool operator>=(const TimePoint<Clock>& rhs) const { return m_timestamp >= rhs.m_timestamp; }
    inline bool operator>(const TimePoint<Clock>& rhs) const { return m_timestamp > rhs.m_timestamp; }

public: // Arithmetic operators
    inline Duration<Clock> operator-(const TimePoint<Clock>& rhs) const
    {
        return Duration<Clock>(m_timestamp - rhs.m_timestamp);
    }
    inline TimePoint operator+(const Duration<Clock>& rhs) const { return TimePoint(m_timestamp + rhs.count()); }
    inline TimePoint operator-(const Duration<Clock>& rhs) const { return TimePoint(m_timestamp - rhs.count()); }

public: // Assignment operators
    inline void operator=(const TimePoint<Clock>& rhs) { m_timestamp = rhs.m_timestamp; }
    inline void operator+=(const Duration<Clock>& rhs) { m_timestamp += rhs.count(); }
    inline void operator-=(const Duration<Clock>& rhs) { m_timestamp -= rhs.count(); }

public: //
    static inline ATR_CONSTEXPR TimePoint min() { return TimePoint(std::numeric_limits<uint64_t>::min()); }
    static inline ATR_CONSTEXPR TimePoint max() { return TimePoint(std::numeric_limits<uint64_t>::max()); }

public:
    Duration<Clock> time_since_epoch() const;

private:
    uint64_t m_timestamp;
};

template <>
Duration<steady_clock> TimePoint<steady_clock>::time_since_epoch() const;

inline Duration<steady_clock> operator*(const int& lhs, const Duration<steady_clock>& rhs)
{
    return rhs * lhs;
}

#endif // SRT_ENABLE_STDCXX_SYNC

// NOTE: Moved the following class definitions to "atomic_clock.h"
//   template <class Clock>
//      class AtomicDuration;
//   template <class Clock>
//      class AtomicClock;

///////////////////////////////////////////////////////////////////////////////
//
// Duration and timepoint conversions
//
///////////////////////////////////////////////////////////////////////////////

/// Function return number of decimals in a subsecond precision.
/// E.g. for a microsecond accuracy of steady_clock the return would be 6.
/// For a nanosecond accuracy of the steady_clock the return value would be 9.
int clockSubsecondPrecision();

#if SRT_ENABLE_STDCXX_SYNC

inline long long count_microseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(t).count();
}

inline long long count_microseconds(const steady_clock::time_point tp)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

inline long long count_milliseconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(t).count();
}

inline long long count_seconds(const steady_clock::duration &t)
{
    return std::chrono::duration_cast<std::chrono::seconds>(t).count();
}

inline steady_clock::duration microseconds_from(int64_t t_us)
{
    return std::chrono::microseconds(t_us);
}

inline steady_clock::duration milliseconds_from(int64_t t_ms)
{
    return std::chrono::milliseconds(t_ms);
}

inline steady_clock::duration seconds_from(int64_t t_s)
{
    return std::chrono::seconds(t_s);
}

#else

int64_t count_microseconds(const steady_clock::duration& t);
int64_t count_milliseconds(const steady_clock::duration& t);
int64_t count_seconds(const steady_clock::duration& t);

Duration<steady_clock> microseconds_from(int64_t t_us);
Duration<steady_clock> milliseconds_from(int64_t t_ms);
Duration<steady_clock> seconds_from(int64_t t_s);

inline bool is_zero(const TimePoint<steady_clock>& t)
{
    return t == TimePoint<steady_clock>();
}

#endif // SRT_ENABLE_STDCXX_SYNC

////////////////////////////////////////////////////////////////////////////////
//
// CThread class
//
////////////////////////////////////////////////////////////////////////////////

#ifdef SRT_ENABLE_STDCXX_SYNC
typedef std::system_error CThreadException;
using CThread = std::thread;
namespace this_thread = std::this_thread;
#else // pthreads wrapper version
typedef CUDTException CThreadException;

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
    void create_thread(void *(*start_routine) (void *), void *arg);
#endif

public: // Observers
    /// Checks if the CThread object identifies an active thread of execution.
    /// A default constructed thread is not joinable.
    /// A thread that has finished executing code, but has not yet been joined
    /// is still considered an active thread of execution and is therefore joinable.
    bool joinable() const;

    struct id
    {
        explicit id(const pthread_t t)
            : value(t)
        {}

        // XXX IMPORTANT!!!
        // This has been verified empirically that it works this way on Linux.
        // This is, however, __NOT PORTABLE__.
        // According to the POSIX specification, there's no trap representation
        // for pthread_t type and the integer 0 value is as good as any other.
        // However, the C++11 thread implementation with POSIX does use the pthead_t
        // type as an integer type where 0 is a trap representation that does not
        // represent any thread.
        //
        // Note that the C++11 threads for `thread::id` type there is defined a trap
        // representation; it's a value after creating a thread without spawning
        // and it's the value after join(). It is also granted that a.joinable() == false
        // implies a.get_id() == thread::id().
        id(): value(pthread_t())
        {
        }

        pthread_t value;
        bool operator==(const id& second) const
        {
            return pthread_equal(value, second.value) != 0;
        }

        bool operator!=(const id& second) const { return !(*this == second); }

        // According to the std::thread::id type specification, this type should
        // be also orderable.

        bool operator<(const id& second) const
        {
            // NOTE: this ain't portable and it is only known
            // to work with "primary platforms" for gcc. If this doesn't
            // compile, resolve to C++11 threads instead (see SRT_ENABLE_STDCXX_SYNC).
            uint64_t left = uint64_t(value);
            uint64_t right = uint64_t(second.value);
            return left < right;
        }
    };

    /// Returns the id of the current thread.
    /// In this implementation the ID is the pthread_t.
    const id get_id() const { return id(m_thread); }

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
    pid_t     m_pid;
};

template <class Stream>
inline Stream& operator<<(Stream& str, const CThread::id& cid)
{
#if defined(_WIN32) && (defined(PTW32_VERSION) || defined (__PTW32_VERSION))
    // This is a version specific for pthread-win32 implementation
    // Here pthread_t type is a structure that is not convertible
    // to a number at all.
    return str << pthread_getw32threadid_np(cid.value);
#else
    return str << cid.value;
#endif
}

namespace this_thread
{
    const inline CThread::id get_id() { return CThread::id (pthread_self()); }

    inline void sleep_for(const steady_clock::duration& t)
    {
#if !defined(_WIN32)
        usleep(count_microseconds(t)); // microseconds
#else
        Sleep((DWORD) count_milliseconds(t));
#endif
    }
}

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
#ifdef SRT_ENABLE_STDCXX_SYNC
typedef void* (&ThreadFunc) (void*);
bool StartThread(CThread& th, ThreadFunc&& f, void* args, const std::string& name);
#else
bool StartThread(CThread& th, void* (*f) (void*), void* args, const std::string& name);
#endif

// Some functions are defined to be run exclusively in a specific thread
// of known id. This function checks if this is true.
inline bool CheckAffinity(CThread::id id)
{
    return this_thread::get_id() == id;
}

inline bool this_thread_is(const CThread& th)
{
    return this_thread::get_id() == th.get_id();
}

////////////////////////////////////////////////////////////////////////////////
//
// CThreadError class - thread local storage wrapper
//
////////////////////////////////////////////////////////////////////////////////

/// Set thread local error
/// @param e new CUDTException
void SetThreadLocalError(const CUDTException& e);

/// Get thread local error
/// @returns CUDTException pointer
CUDTException& GetThreadLocalError();


///////////////////////////////////////////////////////////////////////////////
//
// Mutex section
//
///////////////////////////////////////////////////////////////////////////////

#if SRT_ENABLE_STDCXX_SYNC
using Mutex SRT_TSA_CAPABILITY("mutex") = std::mutex;
using UniqueLock = std::unique_lock<std::mutex>;
using ScopedLock = std::lock_guard<std::mutex>;
#else
/// Mutex is a class wrapper, that should mimic the std::chrono::mutex class.
/// At the moment the extra function ref() is temporally added to allow calls
/// to pthread_cond_timedwait(). Will be removed by introducing CEvent.
class SRT_TSA_CAPABILITY("mutex") Mutex
{
    friend class SyncEvent;

public:
    explicit Mutex();
    ~Mutex();

public:
    int lock() SRT_TSA_WILL_LOCK();
    int unlock() SRT_TSA_WILL_UNLOCK();

    /// @return     true if the lock was acquired successfully, otherwise false
    bool try_lock() SRT_TSA_WILL_TRY_LOCK(true);

    // TODO: To be removed with introduction of the CEvent.
    pthread_mutex_t& ref() { return m_mutex; }

private:
    pthread_mutex_t m_mutex;
};

/// A pthread version of std::scoped_lock (or lock_guard for C++11).
class SRT_TSA_SCOPED_CAPABILITY ScopedLock
{
public:
    SRT_TSA_WILL_LOCK(m)
    explicit ScopedLock(Mutex& m)
        : m_mutex(m)
    {
        m_mutex.lock();
    }

    SRT_TSA_WILL_UNLOCK()
    ~ScopedLock() { m_mutex.unlock(); }

private:
    Mutex& m_mutex;
};

/// A pthread version of std::chrono::unique_lock<mutex>
class SRT_TSA_SCOPED_CAPABILITY UniqueLock
{
    friend class SyncEvent;
    int m_iLocked;
    Mutex& m_Mutex;

public:
    SRT_TSA_WILL_LOCK(m)
    explicit UniqueLock(Mutex &m);

    SRT_TSA_WILL_UNLOCK()
    ~UniqueLock();

public:
    SRT_TSA_WILL_LOCK()
    void lock();

    SRT_TSA_WILL_UNLOCK()
    void unlock();

    SRT_TSA_RETURN_CAPABILITY(m_Mutex)
    Mutex* mutex(); // reflects C++11 unique_lock::mutex()
};
#endif // SRT_ENABLE_STDCXX_SYNC

inline void enterCS(Mutex& m)
SRT_TSA_NEEDS_NONLOCKED(m)
SRT_TSA_WILL_LOCK(m)
{ m.lock(); }

inline bool tryEnterCS(Mutex& m)
SRT_TSA_NEEDS_NONLOCKED(m)
SRT_TSA_WILL_TRY_LOCK(true, m)
{ return m.try_lock(); }

inline void leaveCS(Mutex& m)
SRT_TSA_NEEDS_LOCKED(m)
SRT_TSA_WILL_UNLOCK(m)
{ m.unlock(); }

class InvertedLock
{
    Mutex& m_mtx;

public:
    SRT_TSA_NEEDS_LOCKED(m)
    SRT_TSA_WILL_UNLOCK(m)
    InvertedLock(Mutex& m)
        : m_mtx(m)
    {
        m_mtx.unlock();
    }

    SRT_TSA_WILL_LOCK(m_mtx)
    ~InvertedLock()
    {
        m_mtx.lock();
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
    void reset();
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

    // Debug

#if SRT_ENABLE_THREAD_DEBUG
private:

#define SRT_SYNC_THREAD_DEBUG_MAX 16

    atomic<CThread::id> m_waitmap[SRT_SYNC_THREAD_DEBUG_MAX];
    atomic<CThread::id> m_notifymap[SRT_SYNC_THREAD_DEBUG_MAX];
    atomic<bool> m_sanitize_enabled;

public:

    bool sanitize() const { return m_sanitize_enabled.load(); }
    void sanitize(bool enabled)
    {
        m_sanitize_enabled = enabled;
    }

    void add_as_waiter()
    {
        if (!sanitize())
            return;
        CThread::id id = this_thread::get_id();
        assert_have_notifiers(id);
        // Occupy the first free place.
        // Give up if all are occupied.
        for (int i = 0; i < SRT_SYNC_THREAD_DEBUG_MAX; ++i)
        {
            if (m_waitmap[i].compare_exchange(CThread::id(), id))
            {
                break;
            }
        }
    }

    void remove_as_waiter()
    {
        if (!sanitize())
            return;
        CThread::id id = this_thread::get_id();
        for (int i = 0; i < SRT_SYNC_THREAD_DEBUG_MAX; ++i)
        {
            if (m_waitmap[i].compare_exchange(id, CThread::id()))
            {
                break;
            }
        }
    }

    void add_as_notifier()
    {
        if (!sanitize())
            return;
        CThread::id id = this_thread::get_id();
        // Occupy the first free place.
        // Give up if all are occupied.
        for (int i = 0; i < SRT_SYNC_THREAD_DEBUG_MAX; ++i)
        {
            if (m_notifymap[i].compare_exchange(CThread::id(), id))
            {
                break;
            }
        }
    }

    void remove_as_notifier()
    {
        if (!sanitize())
            return;
        CThread::id id = this_thread::get_id();
        for (int i = 0; i < SRT_SYNC_THREAD_DEBUG_MAX; ++i)
        {
            if (m_notifymap[i].compare_exchange(id, CThread::id()))
            {
                assert_no_orphan_waiters(id);
                break;
            }
        }
    }

    // This checks if an unregistering notifier isn't the
    // last one, or if so, there are no waiters on this cv.
    void assert_no_orphan_waiters(CThread::id);

    // This checks if for a waiter thread trying to enter
    // the wait state there is already registered notifier.
    void assert_have_notifiers(CThread::id);

    // This means "you" (thread) are not in wait mode.
    // Should be impossible (as it can't enter another wait
    // mode without exiting from current waiting), but things happen.
    void assert_thisthread_not_waiting();

#else

    void sanitize(bool) {}

    // Leave stubs for simpliciation
    void add_as_waiter() {}
    void remove_as_waiter() {}
    void add_as_notifier() {}
    void remove_as_notifier() {}

    void assert_thisthread_not_waiting() {}

#endif

    struct ScopedNotifier
    {
        Condition* cv;
        ScopedNotifier(Condition& c): cv(&c)
        {
            cv->add_as_notifier();
        }

        ~ScopedNotifier()
        {
            cv->remove_as_notifier();
        }
    };

    struct ScopedWaiter
    {
        Condition* cv;
        ScopedWaiter(Condition& c): cv(&c)
        {
            cv->add_as_waiter();
        }

        ~ScopedWaiter()
        {
            cv->remove_as_waiter();
        }
    };

private:
#if SRT_ENABLE_STDCXX_SYNC
    std::condition_variable m_cv;
#else
    pthread_cond_t  m_cv;
#endif
};

inline void setupCond(Condition& cv, const char*, bool sanitize = false) { cv.init(); cv.sanitize(sanitize); }
inline void resetCond(Condition& cv) { cv.reset(); }
inline void releaseCond(Condition& cv) { cv.destroy(); }

///////////////////////////////////////////////////////////////////////////////
//
// Shared Mutex section
//
///////////////////////////////////////////////////////////////////////////////

#if defined(SRT_ENABLE_STDCXX_SYNC) && HAVE_CXX17
using SharedMutex SRT_TSA_CAPABILITY("mutex") = std::shared_mutex;
#else

/// Implementation of a read-write mutex. 
/// This allows multiple readers at a time, or a single writer.
/// TODO: The class can be improved if needed to give writer a preference
/// by adding additional m_iWritersWaiting member variable (counter).
/// TODO: The m_iCountRead could be made atomic to make unlock_shared() faster and lock-free.
class SRT_TSA_CAPABILITY("mutex") SharedMutex
{
public:
    SharedMutex();
    ~SharedMutex();

public:
    /// Acquire the lock for writting purposes. Only one thread can acquire this lock at a time
    /// Once it is locked, no reader can acquire it
    void lock() SRT_TSA_WILL_LOCK();
    bool try_lock() SRT_TSA_WILL_TRY_LOCK(true);
    void unlock() SRT_TSA_WILL_UNLOCK();

    /// Acquire the lock if no writter already has it. For read purpose only
    /// Several readers can lock this at the same time.
    void lock_shared() SRT_TSA_WILL_LOCK_SHARED();
    bool try_lock_shared() SRT_TSA_WILL_TRY_LOCK_SHARED(true);
    void unlock_shared() SRT_TSA_WILL_UNLOCK_SHARED();

    int getReaderCount() const;
#ifdef SRT_ENABLE_THREAD_DEBUG
    CThread::id exclusive_owner() const { return m_ExclusiveOwner; }
    bool shared_owner(CThread::id i) const { return m_SharedOwners.count(i); }
#else

    // XXX NOT IMPLEMENTED. This returns true if ANY THREAD has
    // made a shared lock in order to fire assertion only if the
    // lock was NOT applied at all (whether by this thread or any other)
    bool shared_owner(CThread::id) const { return m_iCountRead; }
#endif

protected:
    Condition m_LockWriteCond;
    Condition m_LockReadCond;

    mutable Mutex m_Mutex;

    int  m_iCountRead;
    bool m_bWriterLocked;
#ifdef SRT_ENABLE_THREAD_DEBUG
    CThread::id m_ExclusiveOwner; // For debug support
    std::set<CThread::id> m_SharedOwners;
#endif
};
#endif

inline void enterCS(SharedMutex& m) SRT_TSA_WILL_LOCK(m) { m.lock(); }

inline bool tryEnterCS(SharedMutex& m) SRT_TSA_WILL_TRY_LOCK(true, m) { return m.try_lock(); }

inline void leaveCS(SharedMutex& m) SRT_TSA_WILL_UNLOCK(m) { m.unlock(); }

inline void setupMutex(SharedMutex&, const char*) {}
inline void releaseMutex(SharedMutex&) {}

/// A version of std::scoped_lock<std::shared_mutex> (or lock_guard for C++11).
/// We could have used the srt::sync::ScopedLock making it a template-based class.
/// But in that case all usages would have to be specificed like ScopedLock<Mutex> in C++03.
class SRT_TSA_SCOPED_CAPABILITY ExclusiveLock
{
public:
    SRT_TSA_WILL_LOCK(m)
    explicit ExclusiveLock(SharedMutex& m)
        : m_mutex(m)
    {
        m_mutex.lock();
    }

    SRT_TSA_WILL_UNLOCK()
    ~ExclusiveLock() { m_mutex.unlock(); }

private:
    SharedMutex& m_mutex;
};

/// A reduced implementation of the std::shared_lock functionality (available in C++14).
class SRT_TSA_SCOPED_CAPABILITY SharedLock
{
public:
    explicit SharedLock(SharedMutex& m)
    SRT_TSA_WILL_LOCK_SHARED(m)
        : m_mtx(m)
    {
        m_mtx.lock_shared();
    }

    ~SharedLock()
    SRT_TSA_WILL_UNLOCK_GENERIC() // Using generic because TSA somehow doesn't understand it was locked shared
    { m_mtx.unlock_shared(); }

private:
    SharedMutex& m_mtx;
};

/// A class template for a shared object. It is a wrapper around a pointer to an object
/// and a shared mutex. It allows multiple readers to access the object at the same time,
/// but only one writer can access the object at a time.
template <class T>
class CSharedObjectPtr : public SharedMutex
{
public:
    CSharedObjectPtr()
        : m_pObj(NULL)
    {
    }

    bool compare_exchange(T* expected, T* newobj)
    {
        ExclusiveLock lock(*this);
        if (m_pObj != expected)
            return false;
        m_pObj = newobj;
        return true;
    }

    T* get_locked(SharedLock& /*wholocked*/)
    {
        // XXX Here you can assert that `wholocked` locked *this.
        return m_pObj;
    }

private:
    T* m_pObj;
};

///////////////////////////////////////////////////////////////////////////////
//
// Event (CV) section
//
///////////////////////////////////////////////////////////////////////////////

// This class is used for condition variable combined with mutex by different ways.
// This should provide a cleaner API around locking with debug-logging inside.
class CSync
{
protected:
    Condition* m_cond;
    UniqueLock* m_locker;

public:
    // Locked version: must be declared only after the declaration of UniqueLock,
    // which has locked the mutex. On this delegate you should call only
    // signal_locked() and pass the UniqueLock variable that should remain locked.
    // Also wait() and wait_for() can be used only with this socket.
    CSync(Condition& cond, UniqueLock& g)
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
    /// @retval false if the relative timeout specified by rel_time expired,
    /// @retval true if condition is signaled or spurious wake up.
    bool wait_for(const steady_clock::duration& delay)
    {
        return m_cond->wait_for(*m_locker, delay);
    }

    // Wait until the given time is achieved.
    /// @param [in] exptime The target time to wait until.
    /// @retval false if the target wait time is reached.
    /// @retval true if condition is signal or spurious wake up.
    bool wait_until(const steady_clock::time_point& exptime)
    {
        return m_cond->wait_until(*m_locker, exptime);
    }

    // Static ad-hoc version
    static void lock_notify_one(Condition& cond, Mutex& m)
    {
        ScopedLock lk(m); // XXX with thread logging, don't use ScopedLock directly!
        cond.notify_one();
    }

    static void lock_notify_all(Condition& cond, Mutex& m)
    {
        ScopedLock lk(m); // XXX with thread logging, don't use ScopedLock directly!
        cond.notify_all();
    }

    void notify_one_locked(UniqueLock& lk SRT_ATR_UNUSED)
    {
        // EXPECTED: lk.mutex() is LOCKED.
        m_cond->notify_one();
    }

    void notify_all_locked(UniqueLock& lk SRT_ATR_UNUSED)
    {
        // EXPECTED: lk.mutex() is LOCKED.
        m_cond->notify_all();
    }

    // The *_relaxed functions are to be used in case when you don't care
    // whether the associated mutex is locked or not (you accept the case that
    // a mutex isn't locked and the condition notification gets effectively
    // missed), or you somehow know that the mutex is locked, but you don't
    // have access to the associated UniqueLock object. This function, although
    // it does the same thing as CSync::notify_one_locked etc. here for the
    // user to declare explicitly that notifying is done without being
    // prematurely certain that the associated mutex is locked.
    //
    // It is then expected that whenever these functions are used, an extra
    // comment is provided to explain, why the use of the relaxed notification
    // is correctly used.

    void notify_one_relaxed() { notify_one_relaxed(*m_cond); }
    static void notify_one_relaxed(Condition& cond) { cond.notify_one(); }
    static void notify_all_relaxed(Condition& cond) { cond.notify_all(); }
};

////////////////////////////////////////////////////////////////////////////////
//
// CEvent class
//
////////////////////////////////////////////////////////////////////////////////

// XXX Do not use this class now, there's an unknown issue
// connected to object management with the use of release* functions.
// Until this is solved, stay with separate *Cond and *Lock fields.
class CEvent
{
public:
    CEvent();
    ~CEvent();

public:
    Mutex& mutex() { return m_lock; }
    Condition& cond() { return m_cond; }

public:
    /// Causes the current thread to block until
    /// a specific time is reached.
    ///
    /// @return true  if condition occurred or spuriously woken up
    ///         false on timeout
    bool lock_wait_until(const steady_clock::time_point& tp);

    /// Blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// Uses internal mutex to lock.
    ///
    /// @return true  if condition occurred or spuriously woken up
    ///         false on timeout
    bool lock_wait_for(const steady_clock::duration& rel_time);

    /// Atomically releases lock, blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// When unblocked, regardless of the reason, lock is reacquiredand wait_for() exits.
    ///
    /// @return true  if condition occurred or spuriously woken up
    ///         false on timeout
    bool wait_for(UniqueLock& lk, const steady_clock::duration& rel_time);

    bool wait_until(UniqueLock& lk, const steady_clock::time_point& tp);

    void lock_wait();

    void wait(UniqueLock& lk);

    void notify_one();

    void notify_all();

    void lock_notify_one()
    {
        ScopedLock lk(m_lock); // XXX with thread logging, don't use ScopedLock directly!
        m_cond.notify_one();
    }

    void lock_notify_all()
    {
        ScopedLock lk(m_lock); // XXX with thread logging, don't use ScopedLock directly!
        m_cond.notify_all();
    }

private:
    Mutex      m_lock;
    Condition  m_cond;
};


// This class binds together the functionality of
// UniqueLock and CSync. It provides a simple interface of CSync
// while having already the UniqueLock applied in the scope,
// so a safe statement can be made about the mutex being locked
// when signalling or waiting.
class SRT_TSA_SCOPED_CAPABILITY CUniqueSync: public CSync
{
    UniqueLock m_ulock;

public:

    UniqueLock& locker() { return m_ulock; }

    CUniqueSync(Mutex& mut, Condition& cnd)
    SRT_TSA_WILL_LOCK(*m_ulock.mutex())
        : CSync(cnd, m_ulock)
        , m_ulock(mut)
    {
    }

    CUniqueSync(CEvent& event)
    SRT_TSA_WILL_LOCK(*m_ulock.mutex())
        : CSync(event.cond(), m_ulock)
        , m_ulock(event.mutex())
    {
    }

    ~CUniqueSync()
    SRT_TSA_WILL_UNLOCK(*m_ulock.mutex())
    {}

    // These functions can be used safely because
    // this whole class guarantees that whatever happens
    // while its object exists is that the mutex is locked.

    void notify_one()
    {
        m_cond->notify_one();
    }

    void notify_all()
    {
        m_cond->notify_all();
    }
};

// XXX THIS CLASS IS UNUSED.
// Delete if turns out to be useless for the future code.
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
    sync::AtomicClock<steady_clock> m_tsSchedTime;

    void wait_busy();
    void wait_stalled();
};


/// Print steady clock timepoint in a human readable way.
/// days HH:MM:SS.us [STD]
/// Example: 1D 02:12:56.123456
///
/// @param [in] steady clock timepoint
/// @returns a string with a formatted time representation
std::string FormatTime(const steady_clock::time_point& time);

/// Print steady clock timepoint relative to the current system time
/// Date HH:MM:SS.us [SYS]
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
    static double count(const steady_clock::duration& dur) { return static_cast<double>(count_microseconds(dur)); }
};

template<>
struct DurationUnitName<DUNIT_MS>
{
    static const char* name() { return "ms"; }
    static double count(const steady_clock::duration& dur) { return static_cast<double>(count_microseconds(dur))/1000.0; }
};

template<>
struct DurationUnitName<DUNIT_S>
{
    static const char* name() { return "s"; }
    static double count(const steady_clock::duration& dur) { return static_cast<double>(count_microseconds(dur))/1000000.0; }
};

template<eDurationUnit UNIT>
inline std::string FormatDuration(const steady_clock::duration& dur)
{
    using namespace hvu;
    return fmtcat(fmt(DurationUnitName<UNIT>::count(dur), std::fixed), DurationUnitName<UNIT>::name());
}

inline std::string FormatDuration(const steady_clock::duration& dur)
{
    return FormatDuration<DUNIT_US>(dur);
}

std::string FormatDurationAuto(const steady_clock::duration& dur);

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

inline void resetThread(CThread* th) { (void)new (th) CThread; }

////////////////////////////////////////////////////////////////////////////////
//
// Random distribution functions.
//
////////////////////////////////////////////////////////////////////////////////

/// Generate a uniform-distributed random integer from [minVal; maxVal].
/// If HAVE_CXX11, uses std::uniform_distribution(std::random_device).
/// @param[in] minVal minimum allowed value of the resulting random number.
/// @param[in] maxVal maximum allowed value of the resulting random number.
int genRandomInt(int minVal, int maxVal);

} // namespace sync
} // namespace srt

#include "atomic_clock.h"

#endif // INC_SRT_SYNC_H
