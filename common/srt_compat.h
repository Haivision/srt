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

#ifndef HAISRT_COMPAT_H__
#define HAISRT_COMPAT_H__

#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif


// MacOS X has a monotonic clock, but it's not POSIX.
#if defined(__MACH__)

#include <AvailabilityMacros.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>

// NOTE: clock_gettime() and clock_res() were added in OSX-10.12, IOS-10.0,
//    TVOS-10.0, and WATCHOS-3.0. But XCode8 makes this very difficult to
//    conditionally check for availability of the function. It will create
//    a weak binding to clock_gettime() and clock_res() which will not be
//    resolved at runtime unless actually run under OSX-10.12 even if
//    -mmacosx-version-min= or MACOSX_DEPLOYMENT_TARGET= are less than 10.12.
//    This is particularly problematic because the weak symbol will be NULL
//    and cause an application crash when calling either of these functions.
//    Attempting to work around this issue.
// See:
//    https://bugs.erlang.org/browse/ERL-256
//    https://curl.haxx.se/mail/lib-2016-09/0043.html

#if defined(CLOCK_REALTIME)
#define __SRT_OSX_CLOCK_GETTIME_AVAILABILITY 1
#else
typedef enum
{
   CLOCK_REALTIME  = 0,
   CLOCK_MONOTONIC = 6,
   // CONSIDER: CLOCK_PROCESS_CPUTIME_ID, CLOCK_THREAD_CPUTIME_ID.
   // Mentioned in Linux manpage, but not described as Linux-specific.
} clockid_t;
#endif

// OS-X does not have clock_gettime(2). This implements work arounds.
// https://developer.apple.com/library/mac/qa/qa1398/_index.html
// http://stackoverflow.com/questions/5167269/clock-gettime-alternative-in-mac-os-x

int OSX_clock_gettime(clockid_t clock_id, struct timespec * ts);

static inline int OSX_clock_getres(clockid_t clock_id, struct timespec * ts)
{
   if (0)
   {
      (void)clock_id;
   }

   ts->tv_sec = 0;
   ts->tv_nsec = 1;

   return 0;
}

static inline int OSXCall_clock_gettime(clockid_t clock_id, struct timespec * ts)
{
#if defined(__SRT_OSX_CLOCK_GETTIME_AVAILABILITY) \
   && (__SRT_OSX_CLOCK_GETTIME_AVAILABILITY == 1)
   if (&clock_gettime != NULL)
   {
      return clock_gettime(clock_id, ts);
   }
#endif

   return OSX_clock_gettime(clock_id, ts);
}

static inline int OSXCall_clock_getres(clockid_t clock_id, struct timespec * ts)
{
#if defined(__SRT_OSX_CLOCK_GETTIME_AVAILABILITY) \
   && (__SRT_OSX_CLOCK_GETTIME_AVAILABILITY == 1)
   if (&clock_getres != NULL)
   {
      return clock_getres(clock_id, ts);
   }
#endif

   return OSX_clock_getres(clock_id, ts);
}

#define SysClockGetTime OSXCall_clock_gettime
#define SysClockGetRes OSXCall_clock_getres

static inline int pthread_condattr_setclock(
      pthread_condattr_t * attr, clockid_t clock_id)
{
   if (0)
   {
      (void)attr;
      (void)clock_id;
   }

   errno = ENOSYS;

   return -1;
}

static inline size_t SysStrnlen(const char * s, size_t maxlen)
{
    const char* pos = (const char*) memchr(s, 0, maxlen);
    return pos ? pos - s : maxlen;
}

#if !defined(MAC_OS_X_VERSION_MAX_ALLOWED)
   #error "FIXME!!"
#endif
#if (MAC_OS_X_VERSION_MAX_ALLOWED <= 101200)
   #if defined(strnlen)
   #undef strnlen
   #endif
   #define strnlen(s, maxlen) SysStrnlen(s, maxlen)
#endif

#endif // (__MACH__)


#ifndef SysClockGetTime
#define SysClockGetTime clock_gettime
#endif

#ifndef SysClockGetRes
#define SysClockGetRes clock_res
#endif

/* Ensures that we store the error in the buffer and return the bufer. */
const char * SysStrError(int errnum, char * buf, size_t buflen);

#ifdef __cplusplus
} // extern C


// Extra C++ stuff. Included only in C++ mode.


#include <string>
inline std::string SysStrError(int errnum)
{
    char buf[1024];
    return SysStrError(errnum, buf, 1024);
}

inline struct tm LocalTime(time_t tt)
{
	struct tm tm;
#ifdef WIN32
#if defined(_MSC_VER) && (_MSC_VER>=1500)
	errno_t rr = localtime_s(&tm, &tt);
	if (rr)
		return tm;

#else
	tm = *localtime(&tt);
#endif // _MSC_VER

#else // WIN32
	tm = *localtime_r(&tt, &tm);
#endif

    return tm;
}



#endif

#endif // HAISRT_COMPAT_H__
