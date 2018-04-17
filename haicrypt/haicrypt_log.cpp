/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#if ENABLE_HAICRYPT_LOGGING

#include "hcrypt.h"
#include "../srtcore/srt.h"
#include "../srtcore/logging.h"

extern logging::LogConfig srt_logger_config;

// LOGFA symbol defined in srt.h
logging::Logger hclog(SRT_LOGFA_HAICRYPT, srt_logger_config, "SRT.k");

extern "C" {

int HaiCrypt_SetLogLevel(int level, int logfa)
{
    srt_setloglevel(level);
    if (logfa != SRT_LOGFA_GENERAL) // General can't be turned on or off
    {
        srt_addlogfa(logfa);
    }
    return 0;
}

// HaiCrypt will be using its own FA, which will be turned off by default.

// Templates made C way.
// It's tempting to use the HAICRYPT_DEFINE_LOG_DISPATCHER macro here because it would provide the
// exact signature that is needed here, the problem is though that this would expand the LOGLEVEL
// parameter, which is also a macro, into the value that the macro designates, which would generate
// the HaiCrypt_LogF_0 instead of HaiCrypt_LogF_LOG_DEBUG, for example.
#define HAICRYPT_DEFINE_LOG_DISPATCHER(LOGLEVEL) \
    int HaiCrypt_LogF_##LOGLEVEL ( const char* file, int line, const char* function, const char* format, ...) \
{ \
    va_list ap; \
    va_start(ap, format); \
    logging::LogDispatcher& lg = hclog.get<LOGLEVEL>(); \
    if (!lg.CheckEnabled()) return -1; \
    lg().setloc(file, line, function).vform(format, ap); \
    va_end(ap); \
    return 0; \
}


HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_DEBUG);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_NOTICE);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_INFO);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_WARNING);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_ERR);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_CRIT);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_ALERT);
HAICRYPT_DEFINE_LOG_DISPATCHER(LOG_EMERG);


} // extern "C"

#endif // Block for the whole file
