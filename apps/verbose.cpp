/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include "verbose.hpp"
#include "sync.h" // srt::sync

namespace Verbose
{
    bool on = false;
    std::ostream* cverb = &std::cerr;
    srt::sync::Mutex vlock;

    Log& Log::operator<<(LogNoEol)
    {
        noeol = true;
        if (on)
        {
            (*cverb) << std::flush;
        }
        return *this;
    }

    Log& Log::operator<<(LogLock)
    {
        lockline = true;
        return *this;
    }

    Log::~Log()
    {
        if (on && !noeol)
        {
            if (lockline)
            {
                // Lock explicitly, as requested, and wait for the opportunity.
                vlock.lock();
            }
            else if (vlock.try_lock())
            {
                // Successfully locked, so unlock immediately, locking wasn't requested.
                vlock.unlock();
            }
            else
            {
                // Failed to lock, which means that some other thread has locked it first.
                // This means that some other thread wants to print the whole line and doesn't
                // want to be disturbed during this process. Lock the thread then as this is
                // the only way to wait until it's unlocked. However, do not block your printing
                // with locking, because you were not requested to lock (treat this mutex as
                // an entry semaphore, which may only occasionally block the whole line).
                vlock.lock();
                vlock.unlock();
            }
            (*cverb) << std::endl;

            // If lockline is set, the lock was requested and WAS DONE, so unlock.
            // Otherwise locking WAS NOT DONE.
            if (lockline)
                vlock.unlock();
        }
    }
}
