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

#ifndef INC_SRT_THREADNAME_H
#define INC_SRT_THREADNAME_H

#if defined(__APPLE__) || defined(__linux__)
#if defined(__linux__)
#include <sys/prctl.h>
#endif

#include <pthread.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>

#include "sync.h"

class ThreadName
{

#if defined(__APPLE__) || defined(__linux__)

    class ThreadNameImpl
    {
    public:
        static const size_t BUFSIZE    = 64;
        static const bool   DUMMY_IMPL = false;

        static bool get(char* namebuf)
        {
#if defined(__linux__)
            // since Linux 2.6.11. The buffer should allow space for up to 16
            // bytes; the returned string will be null-terminated.
            return prctl(PR_GET_NAME, (unsigned long)namebuf, 0, 0) != -1;
#elif defined(__APPLE__)
            // since macos(10.6), ios(3.2)
            return pthread_getname_np(pthread_self(), namebuf, BUFSIZE) == 0;
#else
#error "unsupported platform"
#endif
        }

        static bool set(const char* name)
        {
#if defined(__linux__)
            // The name can be up to 16 bytes long, including the terminating
            // null byte. (If the length of the string, including the terminating
            // null byte, exceeds 16 bytes, the string is silently truncated.)
            return prctl(PR_SET_NAME, (unsigned long)name, 0, 0) != -1;
#elif defined(__APPLE__)
            // since macos(10.6), ios(3.2)
            return pthread_setname_np(name) == 0;
#else
#error "unsupported platform"
#endif
        }

        ThreadNameImpl(const char* name)
        {
            reset = false;
            tid   = pthread_self();

            if (!get(old_name))
                return;

            reset = set(name);
            if (reset)
                return;

            // Try with a shorter name. 15 is the upper limit supported by Linux,
            // other platforms should support a larger value. So 15 should works
            // on all platforms.
            size_t max_len = 15;
            if (std::strlen(name) > max_len)
                reset = set(std::string(name, max_len).c_str());
        }

        ~ThreadNameImpl()
        {
            if (!reset)
                return;

            // ensure it's called on the right thread
            if (tid == pthread_self())
                set(old_name);
        }

    private:
        bool      reset;
        pthread_t tid;
        char      old_name[BUFSIZE];
    };

#else

    class ThreadNameImpl
    {
    public:
        static const bool   DUMMY_IMPL = true;
        static const size_t BUFSIZE    = 64;

        static bool get(char* output)
        {
            // The default implementation will simply try to get the thread ID
            std::ostringstream bs;
            bs << "T" << srt::sync::this_thread::get_id();
            size_t s  = bs.str().copy(output, BUFSIZE - 1);
            output[s] = '\0';
            return true;
        }

        static bool set(const char*) { return false; }

        ThreadNameImpl(const char*) {}

        ~ThreadNameImpl() // just to make it "non-trivially-destructible" for compatibility with normal version
        {
        }
    };

#endif // platform dependent impl

    // Why delegate to impl:
    // 1. to make sure implementation on different platforms have the same interface.
    // 2. it's simple to add some wrappers like get(const std::string &).
    ThreadNameImpl impl;

public:
    static const bool   DUMMY_IMPL = ThreadNameImpl::DUMMY_IMPL;
    static const size_t BUFSIZE    = ThreadNameImpl::BUFSIZE;

    // len should >= BUFSIZE
    static bool get(char* output) {
        return ThreadNameImpl::get(output);
    }

    static bool get(std::string& name)
    {
        char buf[BUFSIZE];
        bool ret = get(buf);
        if (ret)
            name = buf;
        return ret;
    }

    // note: set can fail if name is too long. The upper limit is platform
    // dependent. strlen(name) <= 15 should work on most of the platform.
    static bool set(const char* name) { return ThreadNameImpl::set(name); }

    static bool set(const std::string& name) { return set(name.c_str()); }

    ThreadName(const char* name)
        : impl(name)
    {
    }

    ThreadName(const std::string& name)
        : impl(name.c_str())
    {
    }
};

#endif
