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
#include <stdexcept>
#include <cmath>
#include "sync.h"
#include "srt.h"
#include "srt_compat.h"
#include "logging.h"
#include "common.h"

namespace srt_logging
{
    extern Logger mglog;
}
using namespace srt_logging;

namespace srt
{
namespace sync
{

std::string FormatTime(const steady_clock::time_point& timestamp)
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

std::string FormatTimeSys(const steady_clock::time_point& timestamp)
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

