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

namespace Verbose
{
    bool on = false;
    std::ostream* cverb = &std::cout;

    Log& Log::operator<<(LogNoEol)
    {
        noeol = true;
        return *this;
    }

    Log::~Log()
    {
        if (on && !noeol)
        {
            (*cverb) << std::endl;
        }
    }
}
