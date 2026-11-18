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

// IMPORTANT!!!
//
// This is normally not a part of SRT source files. This is a developer utility
// that allows developers to perform a compile test on a version instrumented
// with type checks. To do that, you can do one of two things:
//
// - configure the compiling process with extra -DSRT_TEST_FORCED_CONSTANT=1 flag
// - unblock the commented out #define in srt.h file for that constant
//
// Note that there's no use of such a compiled code. This is done only so that
// the compiler can detect any misuses of the SRT symbolic type names and
// constants.


#include <ostream>

template <typename T, typename OS>
concept Streamable = requires(OS& os, T value) {
    { os << value };
};

template<class INT, int ambg>
struct IntWrapper
{
    INT v;

    IntWrapper() {}
    explicit IntWrapper(INT val): v(val) {}

    bool operator==(const IntWrapper& x) const
    {
        return v == x.v;
    }

    bool operator!=(const IntWrapper& x) const
    {
        return !(*this == x);
    }

    explicit operator INT() const
    {
        return v;
    }

    bool operator<(const IntWrapper& w) const
    {
        return v < w.v;
    }

    template<class Str>
    requires Streamable<Str, INT>
    friend Str& operator<<(Str& out, const IntWrapper<INT, ambg>& x)
    {
        out << x.v;
        return out;
    }

    friend std::ostream& operator<<(std::ostream& out, const IntWrapper<INT, ambg>& x)
    {
        out << x.v;
        return out;
    }
};

template<class INT, int ambg>
struct IntWrapperLoose: IntWrapper<INT, ambg>
{
    typedef IntWrapper<INT, ambg> base_t;
    explicit IntWrapperLoose(INT val): base_t(val) {}

    bool operator==(const IntWrapper<INT, ambg>& x) const
    {
        return this->v == x.v;
    }

    friend bool operator==(const IntWrapper<INT, ambg>& x, const IntWrapperLoose& y)
    {
        return x.v == y.v;
    }

    bool operator==(INT val) const
    {
        return this->v == val;
    }

    friend bool operator==(INT val, const IntWrapperLoose<INT, ambg>& x)
    {
        return val == x.v;
    }

    operator INT() const
    {
        return this->v;
    }
};


typedef IntWrapper<int32_t, 0> SRTSOCKET;
typedef IntWrapper<int, 1> SRTSTATUS;
typedef IntWrapper<int, 2> SRTRUNSTATUS;
typedef IntWrapperLoose<int, 1> SRTSTATUS_LOOSE;


