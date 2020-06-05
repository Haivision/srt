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

#ifdef __linux__

#include <sys/prctl.h>
#include <cstdio>

class ThreadName
{
    char old_name[128];
    char new_name[128];
    bool good;

public:
    static const size_t BUFSIZE = 128;

    static bool get(char* namebuf)
    {
        return prctl(PR_GET_NAME, (unsigned long)namebuf, 0, 0) != -1;
    }

    static bool set(const char* name)
    {
        return prctl(PR_SET_NAME, (unsigned long)name, 0, 0) != -1;
    }


    ThreadName(const char* name)
    {
        if ( (good = get(old_name)) )
        {
            snprintf(new_name, 127, "%s", name);
            new_name[127] = 0;
            prctl(PR_SET_NAME, (unsigned long)new_name, 0, 0);
        }
    }

    ~ThreadName()
    {
        if ( good )
            prctl(PR_SET_NAME, (unsigned long)old_name, 0, 0);
    }
};

#else

#include "sync.h"

// Fallback version, which simply reports the thread name as
// T<numeric-id>, and custom names used with `set` are ignored.
// If you know how to implement this for other systems than
// Linux, you can make another conditional. This one is now
// the "ultimate fallback".

class ThreadName
{
public:
    static const size_t BUFSIZE = 128;

    static bool get(char* output)
    {
        // The default implementation will simply try to get the thread ID
        std::ostringstream bs;
        bs << "T" << srt::sync::this_thread::get_id();
        size_t s = bs.str().copy(output, BUFSIZE-1);
        output[s] = '\0';
        return true;
    }
    static bool set(const char*) { return false; }

    ThreadName(const char*)
    {
    }

    ~ThreadName() // just to make it "non-trivially-destructible" for compatibility with normal version
    {
    }

};



#endif
#endif
