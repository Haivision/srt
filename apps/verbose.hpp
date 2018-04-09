/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#ifndef INC__VERBOSE_HPP
#define INC__VERBOSE_HPP

#include <iostream>

namespace Verbose
{

extern bool on;
extern std::ostream* cverb;

struct LogNoEol { LogNoEol() {} };

class Log
{
    bool noeol;
public:

    Log(): noeol(false) {}

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
    ~Log();
};

}

inline Verbose::Log Verb() { return Verbose::Log(); }
static const Verbose::LogNoEol VerbNoEOL;

#endif
