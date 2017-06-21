#ifndef INC__WIN_WINTIME
#define INC__WIN_WINTIME

#include <winsock2.h>
#include <windows.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 1
#endif

int clock_gettime(int X, struct timespec *ts);

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
    #define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else
    #define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif
 

#ifndef _TIMEZONE_DEFINED /* also in sys/time.h */
#define _TIMEZONE_DEFINED
struct timezone 
{
    int tz_minuteswest; /* minutes W of Greenwich */
    int tz_dsttime;     /* type of dst correction */
};
#endif

void timeradd(struct timeval *a, struct timeval *b, struct timeval *result);

int gettimeofday(struct timeval* tp, struct timezone* tz);

#ifdef __cplusplus
}
#endif

#endif
