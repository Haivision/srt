/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

// This file should provide the standard way of sync facilities (mutex, scoped lock, atomic, threads)
// This requires, however, at least C++11. For C++98 and C++03 you need to provide some external
// facility.

#ifndef INC_HVU_SYNC_H
#define INC_HVU_SYNC_H

#include <mutex>
#include <atomic>
#include <thread>
#define HVU_EXT_MUTEX std::mutex
#define HVU_EXT_LOCKGUARD std::lock_guard<std::mutex>
#define HVU_EXT_ATOMIC std::atomic
#define HVU_EXT_THIS_THREAD std::this_thread

#endif
