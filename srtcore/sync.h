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

//
//#define ENABLE_CXX17

#include <cstdlib>

#ifdef USE_STL_CHRONO
#include <chrono>
#include <condition_variable>
#include <mutex>
#else
#include <pthread.h>
#endif // USE_STL_CHRONO

#include "utilities.h"

namespace srt
{
namespace sync
{
using namespace std;

#ifdef USE_STL_CHRONO

template <class Clock, class Duration = typename Clock::duration>
using time_point = chrono::time_point<Clock, Duration>;

using system_clock   = chrono::system_clock;
using high_res_clock = chrono::high_resolution_clock;
using steady_clock   = chrono::steady_clock;

uint64_t get_timestamp_us();

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

inline long long count_seconds(const steady_clock::duration& t)
{
    return std::chrono::duration_cast<std::chrono::seconds>(t).count();
}

inline steady_clock::duration microseconds_from(long t_us) { return std::chrono::microseconds(t_us); }

inline steady_clock::duration milliseconds_from(long t_ms) { return std::chrono::milliseconds(t_ms); }

inline steady_clock::duration seconds_from(long t_s) { return std::chrono::seconds(t_s); }

template <class Clock, class Duration = typename Clock::duration>
inline bool is_zero(const time_point<Clock, Duration> &tp)
{
    return tp.time_since_epoch() == Clock::duration::zero();
}

#else // !defined(USE_STL_CHRONO)

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

template <class _Clock> class TimePoint;

class steady_clock
{
public:
    typedef Duration<steady_clock>  duration;
    typedef TimePoint<steady_clock> time_point;

 public:
    static time_point now();
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

#endif  // USE_STL_CHRONO

///////////////////////////////////////////////////////////////////////////////
//
// Common pthread/chrono section
//
///////////////////////////////////////////////////////////////////////////////

// Mutex section
#ifdef USE_STL_CHRONO
// Mutex for C++03 should call pthread init and destroy
using Mutex      = mutex;
using UniqueLock = unique_lock<mutex>;
#if ENABLE_CXX17
using ScopedLock = scoped_lock<mutex>;
#else
using ScopedLock = lock_guard<mutex>;
#endif

using Thread     = thread;

inline void SleepFor(const steady_clock::duration &t) { this_thread::sleep_for(t); }

#else

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

private:

    pthread_mutex_t m_mutex;
};


class ScopedLock
{
public:
    ScopedLock(Mutex& m);
    ~ScopedLock();

private:
    Mutex& m_mutex;
};



class UniqueLock
{
    friend class SyncEvent;

public:
    UniqueLock(Mutex &m);
    ~UniqueLock();

public:
    void unlock();

private:
    int m_iLocked;
    Mutex& m_Mutex;
};


inline void SleepFor(const steady_clock::duration& t)
{
#ifndef _WIN32
    usleep(count_microseconds(t));
#else
    Sleep((DWORD)count_milliseconds(t));
#endif
}

#endif  // USE_STL_CHRONO


struct CriticalSection
{
    static void enter(Mutex &m) { m.lock(); }
    static void leave(Mutex &m) { m.unlock(); }
};


class InvertedLock
{
    Mutex *m_pMutex;

  public:
    InvertedLock(Mutex *m)
        : m_pMutex(m)
    {
        if (!m_pMutex)
            return;

        CriticalSection::leave(*m_pMutex);
    }

    ~InvertedLock()
    {
        if (!m_pMutex)
            return;
        CriticalSection::enter(*m_pMutex);
    }
};


class SyncCond
{

public:
    SyncCond();

    ~SyncCond();

public:
    bool wait_for(UniqueLock& lk, steady_clock::duration timeout);
    void wait(UniqueLock& lk);

    void notify_one();
    void notify_all();

private:
#ifdef USE_STL_CHRONO
    condition_variable m_tick_cond;
#else
    pthread_cond_t  m_tick_cond;
#endif
};



class SyncEvent
{

public:
    SyncEvent(bool is_static = false);

    ~SyncEvent();

public:

    Mutex &mutex() { return m_tick_lock; }

public:

    /// wait_until causes the current thread to block until
    /// a specific time is reached.
    ///
    /// @return true  if condition occured or spuriously woken up
    ///         false on timeout
    bool wait_until(const steady_clock::time_point& tp);

    /// Blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// Uses internal mutex to lock.
    ///
    /// @return true  if condition occured or spuriously woken up
    ///         false on timeout
    bool wait_for(const steady_clock::duration& rel_time);

    /// Atomically releases lock, blocks the current executing thread,
    /// and adds it to the list of threads waiting on* this.
    /// The thread will be unblocked when notify_all() or notify_one() is executed,
    /// or when the relative timeout rel_time expires.
    /// It may also be unblocked spuriously.
    /// When unblocked, regardless of the reason, lock is reacquiredand wait_for() exits.
    ///
    /// @return true  if condition occured or spuriously woken up
    ///         false on timeout
    bool wait_for(UniqueLock &lk, const steady_clock::duration& rel_time);

    void wait();

    void wait(UniqueLock& lk);

    void notify_one();

    void notify_all();

  private:
#ifdef USE_STL_CHRONO
    Mutex              m_tick_lock;
    condition_variable m_tick_cond;
#else
    Mutex              m_tick_lock;
    pthread_cond_t     m_tick_cond;
#endif
};


class Timer
{
public:
    Timer();

    ~Timer();

public:

    /// sleep_until causes the current thread to block until
    /// a specific time is reached.
    /// Sleep can be interrupted by calling interrupt()
    ///
    /// @return true  if the specified time was reached
    ///         false should never happen
    bool sleep_until(steady_clock::time_point tp);


    /// Resets target wait time and interrupts all sleeps
    void interrupt();


    void notify_one();

    void notify_all();

private:

    SyncEvent m_event;
    steady_clock::time_point m_sched_time;

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

}; // namespace sync
}; // namespace srt

extern srt::sync::SyncEvent s_SyncEvent;

#endif // __SRT_SYNC_H__
