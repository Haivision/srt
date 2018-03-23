/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

// Implementation file for srt_compat.h
/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

// Prevents from misconfiguration through preprocessor.

#include <srt_compat.h>

#include <string.h>
#include <errno.h>
#if !defined(_WIN32) \
   && !defined(__MACH__)
#include <features.h>
#endif

#if defined(_WIN32) || defined(WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#if defined(__MACH__)

#include <assert.h>
#include <sys/time.h>
#ifdef __APPLE__
#include "TargetConditionals.h"
#endif
#if TARGET_OS_IOS || TARGET_OS_WATCH || TARGET_OS_TV
#include <MobileCoreServices/MobileCoreServices.h>
#else
#include <CoreServices/CoreServices.h>
#endif
#include <pthread.h>
#include <mach/mach.h>
#include <mach/clock.h>
#include <mach/mach_time.h>
#include <AvailabilityMacros.h>


int OSX_clock_gettime(clockid_t clock_id, struct timespec * ts)
{
   int result = -1;

   if (ts == NULL)
   {
      errno = EFAULT;
      return -1;
   }

   switch (clock_id)
   {
      case CLOCK_REALTIME :
      {
         struct timeval tv;

         result = gettimeofday(&tv, NULL);
         if (result == 0)
         {
            ts->tv_sec = tv.tv_sec;
            ts->tv_nsec = tv.tv_usec * 1000;
         }
      }
      break;

      case CLOCK_MONOTONIC :
      {
         mach_timebase_info_data_t timebase_info;
         memset(&timebase_info, 0, sizeof(timebase_info));
         static const uint64_t BILLION = UINT64_C(1000000000);

         (void)mach_timebase_info(&timebase_info);
         if (timebase_info.numer <= 0
            || timebase_info.denom <= 0)
         {
            result = -1;
            errno = EINVAL;
         }
         else
         {
            uint64_t monotonic_nanoseconds = mach_absolute_time()
                  * timebase_info.numer / timebase_info.denom;

            ts->tv_sec = monotonic_nanoseconds / BILLION;
            ts->tv_nsec = monotonic_nanoseconds % BILLION;
            if (ts->tv_sec < 0
               || ts->tv_nsec < 0)
            {
               result = -1;
               errno = EINVAL;
            }
            else
            {
               result = 0;
            }
         }
      }
      break;

      #if 0
      // Warning This is probably slow!!
      case CLOCK_CALENDAR :
      {
         clock_serv_t cclock;
         mach_timespec_t mts;

         host_get_clock_service(mach_host_self(), CALENDAR_CLOCK, &cclock);
         clock_get_time(cclock, &mts);
         mach_port_deallocate(mach_task_self(), cclock);
         ts->tv_sec = mts.tv_sec;
         ts->tv_nsec = mts.tv_nsec;

         result = 0;
      }
      break;
      #endif

      default :
      {
         result = -1;
         errno = EINVAL;
      }
   }

   return result;
}

#endif // (__MACH__)


extern const char * SysStrError(int errnum, char * buf, size_t buflen)
{
   if (buf == NULL || buflen <= 0)
   {
      errno = EFAULT;
      return buf;
   }

   buf[0] = '\0';

#if defined(_WIN32) || defined(WIN32)
   const char* lpMsgBuf;

   // Note: Intentionally the "fixed char size" types are used despite using
   // character size dependent FormatMessage (instead of FormatMessageA) so that
   // your compilation fails when you use wide characters.
   // The problem is that when TCHAR != char, then the buffer written this way
   // would have to be converted to ASCII, not just copied by strncpy.
   FormatMessage(0
            | FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
         NULL, // no lpSource
         errnum, // dwMessageId (as controlled by FORMAT_MESSAGE_FROM_SYSTEM)
         MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	   // This below parameter normally should contain a pointer to an allocated buffer,
	   // and this way it's LPTSTR. But when FORMAT_MESSAGE_ALLOCATE_BUFFER, then it is
	   // expected to be a the value of LPTSTR* type, converted to LPTSTR, that designates
	   // a pointer to a variable of type LPTSTR, to which the newly allocated buffer is
	   // assigned. This buffer should be freed afterwards using LocalFree().
         (LPSTR)&lpMsgBuf, 
	   0, NULL);
   char * result = strncpy(buf, lpMsgBuf, buflen-1);
   LocalFree(lpMsgBuf);
   return result;
#elif (!defined(__GNU_LIBRARY__) && !defined(__GLIBC__) )  \
   || (( (_POSIX_C_SOURCE >= 200112L) || (_XOPEN_SOURCE >= 600)) && ! _GNU_SOURCE )
   // POSIX/XSI-compliant version.
   // Overall general POSIX version: returns status.
   // 0 for success, otherwise it's errno_value or -1 and errno_value is in '::errno'.
   if (strerror_r(errnum, buf, buflen) != 0)
   {
      buf[0] = '\0';
   }
   return buf;
#else
   // GLIBC is non-standard under these conditions.
   // GNU version: returns the pointer to the message.
   // This is either equal to the local buffer (errmsg)
   // or some system-wide storage, depending on kernel's caprice.
   char * tBuffer = strerror_r(errnum, buf, buflen);
   if (tBuffer != NULL
      && tBuffer != buf)
   {
      return strncpy(buf, tBuffer, buflen);
   }
   else
   {
      return buf;
   }
#endif
}
