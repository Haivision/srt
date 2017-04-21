/*****************************************************************************
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
 * 
 * Based on UDT4 SDK version 4.11
 *****************************************************************************/

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#include <win/wintime.h>
#include <sys/timeb.h>

#if 0
// Temporarily blocked. Needs to be fixed.
// Currently unused, but may be useful in future.
int clock_gettime(int X, struct timespec *ts)
{
    LARGE_INTEGER           t;
    FILETIME            f;
    double                  microseconds;
    static LARGE_INTEGER    offset;
    static double           frequencyToMicroseconds;
    static int              initialized = 0;
    static BOOL             usePerformanceCounter = 0;

    if (!initialized) {
        LARGE_INTEGER performanceFrequency;
        initialized = 1;
        usePerformanceCounter = QueryPerformanceFrequency(&performanceFrequency);
        if (usePerformanceCounter) {
            QueryPerformanceCounter(&offset);
            frequencyToMicroseconds = (double)performanceFrequency.QuadPart / 1000000.;
        } else {
            offset = getFILETIMEoffset();
            frequencyToMicroseconds = 10.;
        }
    }
    if (usePerformanceCounter) QueryPerformanceCounter(&t);
    else {
        GetSystemTimeAsFileTime(&f);
        t.QuadPart = f.dwHighDateTime;
        t.QuadPart <<= 32;
        t.QuadPart |= f.dwLowDateTime;
    }

    t.QuadPart -= offset.QuadPart;
    microseconds = (double)t.QuadPart / frequencyToMicroseconds;
    t.QuadPart = microseconds;
    tv->tv_sec = t.QuadPart / 1000000;
    tv->tv_usec = t.QuadPart % 1000000;
    return (0);
}
#endif

void timeradd(struct timeval *a, struct timeval *b, struct timeval *result)
{
    result->tv_sec  = a->tv_sec + b->tv_sec;
    result->tv_usec = a->tv_usec + b->tv_usec;
    if (result->tv_usec >= 1000000)
    {
        result->tv_sec++;
        result->tv_usec -= 1000000;
    }
}


/*
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    FILETIME ft;
    unsigned __int64 tmpres = 0;
    static int tzflag;
    long timezone = 0;
    int daylight = 0;

    if (NULL != tv)
    {
        GetSystemTimeAsFileTime(&ft);
 
        tmpres |= ft.dwHighDateTime;
        tmpres <<= 32;
        tmpres |= ft.dwLowDateTime;
 
        // converting file time to unix epoch
        tmpres -= DELTA_EPOCH_IN_MICROSECS; 
        tmpres /= 10;  //convert into microseconds
        tv->tv_sec  = (long)(tmpres / 1000000UL);
        tv->tv_usec = (long)(tmpres % 1000000UL);
    }
 
    if (NULL != tz)
    {
        if (!tzflag)
        {
            _tzset();
            tzflag++;
        }

        _get_timezone(&timezone);
        _get_daylight(&daylight);

        tz->tz_minuteswest = timezone / 60;
        tz->tz_dsttime     = daylight;
    }
 
    return 0;
}
*/


int gettimeofday(struct timeval* tp, struct timezone* tz)
{
    static LARGE_INTEGER tickFrequency, epochOffset;

    // For our first call, use "ftime()", so that we get a time with a proper epoch.
    // For subsequent calls, use "QueryPerformanceCount()", because it's more fine-grain.
    static int isFirstCall = 1;

    LARGE_INTEGER tickNow;
    QueryPerformanceCounter(&tickNow);

    if (isFirstCall)
    {
        struct timeb tb;
        ftime(&tb);
        tp->tv_sec  = (long)tb.time;
        tp->tv_usec = 1000*tb.millitm;

        // Also get our counter frequency:
        QueryPerformanceFrequency(&tickFrequency);

        // And compute an offset to add to subsequent counter times, so we get a proper epoch:
        epochOffset.QuadPart = tb.time*tickFrequency.QuadPart + (tb.millitm*tickFrequency.QuadPart)/1000 - tickNow.QuadPart;

        isFirstCall = 0; // for next time
    }
    else
    {
        // Adjust our counter time so that we get a proper epoch:
        tickNow.QuadPart += epochOffset.QuadPart;

        tp->tv_sec = (long) (tickNow.QuadPart / tickFrequency.QuadPart);
        tp->tv_usec = (long) (((tickNow.QuadPart % tickFrequency.QuadPart) * 1000000L) / tickFrequency.QuadPart);
    }
    return 0;
}

