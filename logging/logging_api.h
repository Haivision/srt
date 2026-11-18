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

// This file contains definitions that can be provided for the application that
// would like to control the logging of a library. Part if this can be also used
// in a C application, while you only need C++ code to directly deal with the
// logging system.

#ifndef INC_HVU_LOGGING_API_H
#define INC_HVU_LOGGING_API_H

// These are required for access functions:
// - adding FA (requires set)
// - setting a log stream (requires iostream)
#ifdef __cplusplus
#include <set>
#include <iostream>
#include <string>
#endif

#ifdef _WIN32
#include "windows_syslog.h"
#else
#include <syslog.h>
#endif

// Syslog is included so that it provides log level names.
// Haivision log standard requires the same names plus extra one:
#ifndef LOG_DEBUG_TRACE
#define LOG_DEBUG_TRACE 8
#endif
// It's unused anyway, just for the record.
#define HVU_LOG_LEVEL_MIN LOG_CRIT
#define HVU_LOG_LEVEL_MAX LOG_DEBUG

// Flags
#define HVU_LOGF_DISABLE_TIME 1
#define HVU_LOGF_DISABLE_THREADNAME 2
#define HVU_LOGF_DISABLE_SEVERITY 4
#define HVU_LOGF_DISABLE_EOL 8

// Handler type - provided for C API.
typedef void HVU_LOG_HANDLER_FN(void* opaque, int level, const char* file, int line, const char* area, const char* message);

#ifdef __cplusplus
namespace hvu
{
namespace logging
{

class LogConfig;

// same as HVU_LOG_HANDLER_FN 
typedef void loghandler_fn_t(void* opaque, int level, const char* file, int line, const char* area, const char* message);

// Same as C-API flags
const int LOGF_DISABLE_TIME = 1,
      LOGF_DISABLE_THREADNAME = 2,
      LOGF_DISABLE_SEVERITY = 4,
      LOGF_DISABLE_EOL = 8;

namespace LogLevel
{
    // There are 3 general levels:

    // A. fatal - this means the application WILL crash.
    // B. unexpected:
    //    - error: this was unexpected for the library
    //    - warning: this was expected by the library, but may be harmful for the application
    // C. expected:
    //    - note: a significant, but rarely occurring event
    //    - debug: may occur even very often and enabling it can harm performance

    enum type
    {
        invalid = -1,

        fatal = LOG_CRIT,
        // Fatal vs. Error: with Error, you can still continue.
        error = LOG_ERR,
        // Error vs. Warning: Warning isn't considered a problem for the library.
        warning = LOG_WARNING,
        // Warning vs. Note: Note means something unusual, but completely correct behavior.
        note = LOG_NOTICE,
        // Note vs. Debug: Debug may occur even multiple times in a millisecond.
        // (Well, worth noting that Error and Warning potentially also can).
        debug = LOG_DEBUG
    };
}

extern LogLevel::type parse_level(const std::string&);
extern std::set<int> parse_fa(const hvu::logging::LogConfig& config, std::string fa, std::set<std::string>* punknown = NULL);

class Logger;

} // /logging
} // /hvu
#endif

#endif
