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
