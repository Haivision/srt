/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 */


/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef HAISRT_COMPAT_H__
#define HAISRT_COMPAT_H__

#include <stddef.h>
#include <time.h>

#ifdef WIN32
   #ifndef __MINGW__
      #ifdef SRT_DYNAMIC
         #ifdef SRT_EXPORTS
            #define SRT_API __declspec(dllexport)
         #else
            #define SRT_API __declspec(dllimport)
         #endif
      #else
         #define SRT_API
      #endif
   #else
      #define SRT_API
   #endif
#else
   #define SRT_API __attribute__ ((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif


/* Ensures that we store the error in the buffer and return the bufer. */
SRT_API const char * SysStrError(int errnum, char * buf, size_t buflen);

#ifdef __cplusplus
} // extern C


// Extra C++ stuff. Included only in C++ mode.


#include <string>
inline std::string SysStrError(int errnum)
{
    char buf[1024];
    return SysStrError(errnum, buf, 1024);
}

inline struct tm SysLocalTime(time_t tt)
{
    struct tm tms;
    memset(&tms, 0, sizeof tms);
#ifdef WIN32
	errno_t rr = localtime_s(&tms, &tt);
	if (rr == 0)
		return tms;
#else
	tms = *localtime_r(&tt, &tms);
#endif

    return tms;
}


#endif // defined C++

#endif // HAISRT_COMPAT_H__
