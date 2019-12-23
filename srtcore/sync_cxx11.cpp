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


#ifdef USE_STL_CHRONO


 ////////////////////////////////////////////////////////////////////////////////
 //
 // SyncCond (based on stl chrono C++11)
 //
 ////////////////////////////////////////////////////////////////////////////////

srt::sync::SyncCond::SyncCond() {}

srt::sync::SyncCond::~SyncCond() {}


bool srt::sync::SyncCond::wait_for(UniqueLock& lk, steady_clock::duration timeout)
{
    // Another possible implementation is wait_until(steady_clock::now() + timeout);
    return m_tick_cond.wait_for(lk, timeout) != cv_status::timeout;
}


void srt::sync::SyncCond::wait(UniqueLock& lk)
{
    return m_tick_cond.wait(lk);
}


void srt::sync::SyncCond::notify_one()
{
    m_tick_cond.notify_one();
}

void srt::sync::SyncCond::notify_all()
{
    m_tick_cond.notify_all();
}


////////////////////////////////////////////////////////////////////////////////
//
// SyncEvent (based on stl chrono C++11)
//
////////////////////////////////////////////////////////////////////////////////

srt::sync::SyncEvent::SyncEvent() {}

srt::sync::SyncEvent::~SyncEvent() {}

bool srt::sync::SyncEvent::wait_until(steady_clock::time_point tp)
{
    // TODO: Add busy waiting

    // using namespace srt_logging;
    // LOGC(dlog.Note, log << "SyncEvent::wait_until delta="
    //    << std::chrono::duration_cast<std::chrono::microseconds>(tp - steady_clock::now()).count() << " us");
    std::unique_lock<std::mutex> lk(m_tick_lock);
    return m_tick_cond.wait_until(lk, tp) != cv_status::timeout;
}

bool srt::sync::SyncEvent::wait_for(const steady_clock::duration& rel_time)
{
    std::unique_lock<std::mutex> lk(m_tick_lock);
    return m_tick_cond.wait_for(lk, rel_time) != cv_status::timeout;

    // wait_until(steady_clock::now() + timeout);
}

bool srt::sync::SyncEvent::wait_for(UniqueLock& lk, const steady_clock::duration& rel_time)
{
    return m_tick_cond.wait_for(lk, rel_time) != cv_status::timeout;

    // wait_until(steady_clock::now() + timeout);
}


void srt::sync::SyncEvent::wait(UniqueLock& lk)
{
    return m_tick_cond.wait(lk);
}

void srt::sync::SyncEvent::wait()
{
    std::unique_lock<std::mutex> lk(m_tick_lock);
    return m_tick_cond.wait(lk);
}

void srt::sync::SyncEvent::interrupt()
{
    {
        ScopedLock lock(m_tick_lock);
        m_sched_time = steady_clock::now();
    }

    notify_one();
}

void srt::sync::SyncEvent::notify_one()
{
    m_tick_cond.notify_one();
}

void srt::sync::SyncEvent::notify_all()
{
    m_tick_cond.notify_all();
}


////////////////////////////////////////////////////////////////////////////////
//
// Timer (based on stl chrono C++11)
//
////////////////////////////////////////////////////////////////////////////////

srt::sync::Timer::Timer()
{
}


srt::sync::Timer::~Timer()
{
}


bool srt::sync::Timer::sleep_until(TimePoint<steady_clock> tp)
{
    // TODO: Add busy waiting

    std::unique_lock<std::mutex> lk(m_tick_lock);
    m_sched_time = tp;
    return m_tick_cond.wait_until(lk, tp, [this]() { return m_sched_time <= steady_clock::now(); });
}


void srt::sync::Timer::interrupt()
{
    {
        ScopedLock lock(m_tick_lock);
        m_sched_time = steady_clock::now();
    }

    notify_all();
}


void srt::sync::Timer::notify_one()
{
    m_tick_cond.notify_one();
}

void srt::sync::Timer::notify_all()
{
    m_tick_cond.notify_all();
}

////////////////////////////////////////////////////////////////////////////////
//
// FormatTime (stl chrono C++11)
//
////////////////////////////////////////////////////////////////////////////////

std::string srt::sync::FormatTime(const steady_clock::time_point& timestamp)
{
    const uint64_t total_us = count_microseconds(timestamp.time_since_epoch());
    const uint64_t us = total_us % 1000000;
    const uint64_t total_sec = total_us / 1000000;

    const uint64_t days = total_sec / (60 * 60 * 24);
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
    const time_t                   now_s = ::time(NULL); // get current time in seconds
    const steady_clock::time_point now_timestamp = steady_clock::now();
    const int64_t                  delta_us = count_microseconds(timestamp - now_timestamp);
    const uint64_t now_us = count_microseconds(now_timestamp.time_since_epoch());
    const int64_t                  delta_s =
        floor((static_cast<int64_t>(now_us % 1000000) + delta_us) / 1000000.0);
    const time_t tt = now_s + delta_s;
    struct tm    tm = SysLocalTime(tt); // in seconds
    char         tmp_buf[512];
    strftime(tmp_buf, 512, "%X.", &tm);

    ostringstream out;
    const uint64_t timestamp_us = count_microseconds(timestamp.time_since_epoch());
    out << tmp_buf << setfill('0') << setw(6) << (timestamp_us % 1000000) << " [SYS]";
    return out.str();
}


#endif // USE_STL_CHRONO