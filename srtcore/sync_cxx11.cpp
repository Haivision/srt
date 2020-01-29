/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
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
#include "srt_compat.h"

#ifdef USE_STDCXX_CHRONO

 ////////////////////////////////////////////////////////////////////////////////
 //
 // SyncCond (based on stl chrono C++11)
 //
 ////////////////////////////////////////////////////////////////////////////////

srt::sync::CCondVar::CCondVar() {}

srt::sync::CCondVar::~CCondVar() {}

void srt::sync::CCondVar::wait(UniqueLock& lock)
{
    m_cv.wait(lock);
}

bool srt::sync::CCondVar::wait_for(UniqueLock& lock, const steady_clock::duration& rel_time)
{
    // Another possible implementation is wait_until(steady_clock::now() + timeout);
    return m_cv.wait_for(lock, rel_time) != cv_status::timeout;
}

bool srt::sync::CCondVar::wait_until(UniqueLock& lock, const steady_clock::time_point& timeout_time)
{
    return m_cv.wait_until(lock, timeout_time) != cv_status::timeout;
}

void srt::sync::CCondVar::notify_one()
{
    m_cv.notify_one();
}

void srt::sync::CCondVar::notify_all()
{
    m_cv.notify_all();
}


#endif // USE_STDCXX_CHRONO
