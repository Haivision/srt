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

//#define USE_STL_CHRONO
//#define ENABLE_CXX17

#include <cstdlib>
#include <pthread.h>
#include "utilities.h"

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

timespec us_to_timespec(const uint64_t time_us);


///////////////////////////////////////////////////////////////////////////////
//
// Common pthread/chrono section
//
///////////////////////////////////////////////////////////////////////////////

class SyncEvent
{
public:
};

#if ENABLE_THREAD_LOGGING
struct CMutexWrapper
{
    typedef pthread_mutex_t sysobj_t;
    pthread_mutex_t in_sysobj;
    std::string lockname;

    //pthread_mutex_t* operator& () { return &in_sysobj; }

   // Turned explicitly to string because this is exposed only for logging purposes.
   std::string show()
   {
       std::ostringstream os;
       os << (&in_sysobj);
       return os.str();
   }

};

typedef CMutexWrapper CMutex;

struct CConditionWrapper
{
    typedef pthread_cond_t sysobj_t;
    pthread_cond_t in_sysobj;
    std::string cvname;

};

typedef CConditionWrapper CCondition;

template<class SysObj>
inline typename SysObj::sysobj_t* RawAddr(SysObj& obj)
{
    return &obj.in_sysobj;
}

#else
typedef ::pthread_mutex_t CMutex;
typedef ::pthread_cond_t CCondition;

// Note: This cannot be defined as overloaded for
// two different types because on some platforms
// the pthread_cond_t and pthread_mutex_t are distinct
// types, while on others they resolve to the same type.
template <class SysObj>
inline SysObj* RawAddr(SysObj& m) { return &m; }
#endif


class CGuard
{
public:
   /// Constructs CGuard, which locks the given mutex for
   /// the scope where this object exists.
   /// @param lock Mutex to lock
   /// @param if_condition If this is false, CGuard will do completely nothing
   CGuard(CMutex& lock, explicit_t<bool> if_condition = true);
   ~CGuard();

public:

   // The force-Lock/Unlock mechanism can be used to forcefully
   // change the lock on the CGuard object. This is in order to
   // temporarily change the lock status on the given mutex, but
   // still do the right job in the destructor. For example, if
   // a lock has been forcefully unlocked by forceUnlock, then
   // the CGuard object will not try to unlock it in the destructor,
   // but again, if the forceLock() was done again, the destructor
   // will still unlock the mutex.
   void forceLock()
   {
       if (m_iLocked == 0)
           return;
       Lock();
   }

   // After calling this on a scoped lock wrapper (CGuard),
   // the mutex will be unlocked right now, and no longer
   // in destructor
   void forceUnlock()
   {
       if (m_iLocked == 0)
       {
           m_iLocked = -1;
           Unlock();
       }
   }

   static int enterCS(CMutex& lock, explicit_t<bool> block = true);
   static int leaveCS(CMutex& lock);

   // This is for a special case when one class keeps a pointer
   // to another mutex/cond in another object. Required because
   // the operator& has been defined to return the internal pointer
   // so that the most used syntax matches directly the raw mutex/cond types.

private:
   friend class CSync;

   void Lock()
   {
       m_iLocked = pthread_mutex_lock(RawAddr(m_Mutex));
   }

   void Unlock()
   {
        pthread_mutex_unlock(RawAddr(m_Mutex));
   }

   CMutex& m_Mutex;            // Alias name of the mutex to be protected
   int m_iLocked;                       // Locking status

   CGuard& operator=(const CGuard&);
};

bool isthread(const pthread_t& thrval);

bool jointhread(pthread_t& thr, void*& result);
bool jointhread(pthread_t& thr);

void createMutex(CMutex& lock, const char* name);
void releaseMutex(CMutex& lock);

void createCond(CCondition& cond, const char* name);
void createCond_monotonic(CCondition& cond, const char* name);
void releaseCond(CCondition& cond);


class InvertedGuard
{
    CMutex* m_pMutex;
public:

    InvertedGuard(CMutex& smutex, bool shouldlock = true): m_pMutex()
    {
        if ( !shouldlock)
            return;
        m_pMutex = AddressOf(smutex);
        CGuard::leaveCS(smutex);
    }

    ~InvertedGuard()
    {
        if ( !m_pMutex )
            return;

        CGuard::enterCS(*m_pMutex);
    }
};

////////////////////////////////////////////////////////////////////////////////

/// Atomically releases lock, blocks the current executing thread,
/// and adds it to the list of threads waiting on* this.
/// The thread will be unblocked when notify_all() or notify_one() is executed,
/// or when the relative timeout rel_time expires.
/// It may also be unblocked spuriously.
/// When unblocked, regardless of the reason, lock is reacquiredand wait_for() exits.
///
/// @return result of pthread_cond_wait(...) function call
///
int CondWaitFor(pthread_cond_t* cond, pthread_mutex_t* mutex, const steady_clock::duration& rel_time);
int CondWaitFor_monotonic(pthread_cond_t* cond, pthread_mutex_t* mutex, const steady_clock::duration& rel_time);

#if ENABLE_THREAD_LOGGING
inline int CondWaitFor(CConditionWrapper* cond, CMutexWrapper* mutex, const steady_clock::duration& rel_time)
{
    return CondWaitFor(&cond->in_sysobj, &mutex->in_sysobj, rel_time);
}
inline int CondWaitFor_monotonic(CConditionWrapper* cond, CMutexWrapper* mutex, const steady_clock::duration& rel_time)
{
    return CondWaitFor_monotonic(&cond->in_sysobj, &mutex->in_sysobj, rel_time);
}
#endif
// This class is used for condition variable combined with mutex by different ways.
// This should provide a cleaner API around locking with debug-logging inside.
class CSync
{
    CCondition* m_cond;
    CMutex* m_mutex;
#if ENABLE_THREAD_LOGGING
    bool m_nolock;
#endif


public:
    enum Nolock { NOLOCK };

    // Locked version: must be declared only after the declaration of CGuard,
    // which has locked the mutex. On this delegate you should call only
    // signal_locked() and pass the CGuard variable that should remain locked.
    // Also wait() and wait_for() can be used only with this socket.
    CSync(CCondition& cond, CGuard& g);

    // This is only for one-shot signaling. This doesn't need a CGuard
    // variable, only the mutex itself. Only lock_signal() can be used.
    CSync(CCondition& cond, CMutex& mutex, Nolock);

    // An alternative method
    static CSync nolock(CCondition& cond, CMutex& m)
    {
        return CSync(cond, m, NOLOCK);
    }

    // COPY CONSTRUCTOR: DEFAULT!

    // Wait indefinitely, until getting a signal on CV.
    void wait();

    // Wait only for a given time delay (in microseconds). This function
    // extracts first current time using steady_clock::now().
    bool wait_for(const steady_clock::duration& delay);
    bool wait_for_monotonic(const steady_clock::duration& delay);

    // Wait until the given time is achieved. This actually
    // refers to wait_for for the time remaining to achieve
    // given time.
    bool wait_until(const steady_clock::time_point& exptime);

    // You can signal using two methods:
    // - lock_signal: expect the mutex NOT locked, lock it, signal, then unlock.
    // - signal: expect the mutex locked, so only issue a signal, but you must pass the CGuard that keeps the lock.
    void lock_signal();

    // Static ad-hoc version
    static void lock_signal(CCondition& cond, CMutex& m);
    static void lock_broadcast(CCondition& cond, CMutex& m);

    void signal_locked(CGuard& lk);
    void signal_relaxed();
    static void signal_relaxed(CCondition& cond);
    static void broadcast_relaxed(CCondition& cond);
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

// Debug purposes
#if ENABLE_THREAD_LOGGING
void ThreadCheckAffinity(const char* function, pthread_t thr);
#define THREAD_CHECK_AFFINITY(thr) srt::sync::ThreadCheckAffinity(__FUNCTION__, thr)
#else
#define THREAD_CHECK_AFFINITY(thr)
#endif

} // namespace sync
} // namespace srt

#endif // __SRT_SYNC_H__
