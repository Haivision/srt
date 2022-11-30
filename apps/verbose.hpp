/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC_SRT_VERBOSE_HPP
#define INC_SRT_VERBOSE_HPP

#include <iostream>
#if SRT_ENABLE_VERBOSE_LOCK
#include <mutex>
#endif

namespace Verbose
{

extern bool on;
extern std::ostream* cverb;

struct LogNoEol { LogNoEol() {} };
#if SRT_ENABLE_VERBOSE_LOCK
struct LogLock { LogLock() {} };
#endif

class Log
{
    bool noeol = false;
#if SRT_ENABLE_VERBOSE_LOCK
    bool lockline = false;
#endif

    // Disallow creating dynamic objects
    void* operator new(size_t);

public:

    template <class V>
    Log& operator<<(const V& arg)
    {
        // Template - must be here; extern template requires
        // predefined specializations.
        if (on)
            (*cverb) << arg;
        return *this;
    }

    Log& operator<<(LogNoEol);
#if SRT_ENABLE_VERBOSE_LOCK
    Log& operator<<(LogLock);
#endif
    ~Log();
};


class ErrLog: public Log
{
public:

    template <class V>
    ErrLog& operator<<(const V& arg)
    {
        // Template - must be here; extern template requires
        // predefined specializations.
        if (on)
            (*cverb) << arg;
        else
            std::cerr << arg;
        return *this;
    }
};

// terminal
inline void Print(Log& ) {}

template <typename Arg1, typename... Args>
inline void Print(Log& out, Arg1&& arg1, Args&&... args)
{
    out << arg1;
    Print(out, args...);
}

}

inline Verbose::Log Verb() { return Verbose::Log(); }
inline Verbose::ErrLog Verror() { return Verbose::ErrLog(); }

template <typename... Args>
inline void Verb(Args&&... args)
{
    Verbose::Log log;
    Verbose::Print(log, args...);
}


// Manipulator tags
static const Verbose::LogNoEol VerbNoEOL;
#if SRT_ENABLE_VERBOSE_LOCK
static const Verbose::LogLock VerbLock;
#endif

#endif
