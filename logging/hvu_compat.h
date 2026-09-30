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

#ifndef INC_HVU_COMPAT_H
#define INC_HVU_COMPAT_H

// This contains portable versions of `strerror` and `localtime`
// functions:
// * sys_strerror: uses system-dependent reentrant version:
//    - Windows: FormatMessageA with allocated buffer
//    - POSIX/XSI: strerror_r stating it copied the string to the buffer
//    - GNU: strerror_r stating that it might have not copied, but allocated itself
// * sys_localtime:
//    - Windows: localtime_s
//    - POSIX: localtime_r

#include <string>
#include <cstring>
#include <ctime>

namespace hvu
{

// Ensures that we store the error in the buffer and return the buffer.
const char* sys_strerror(int errnum, char* buf, size_t buflen);

inline std::string sys_strerror(int errnum)
{
    char buf[1024];
    return sys_strerror(errnum, buf, 1024);
}

inline struct tm sys_localtime(time_t tt)
{
    using namespace std;

    struct tm tms;
    memset(&tms, 0, sizeof tms);
#ifdef _WIN32
	errno_t rr = localtime_s(&tms, &tt);
	if (rr == 0)
		return tms;
#else

    // Ignore the error, state that if something
    // happened, you simply have a pre-cleared tms.
    localtime_r(&tt, &tms);
#endif

    return tms;
}

}


#endif // macroguard
