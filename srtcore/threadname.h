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
 *****************************************************************************/

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC__THREADNAME_H
#define INC__THREADNAME_H

#ifdef __linux__

#include <sys/prctl.h>

class ThreadName
{
    char old_name[128];
    char new_name[128];
    bool good;

public:
    static const size_t BUFSIZE = 128;

    static bool get(char* namebuf)
    {
        return prctl(PR_GET_NAME, (unsigned long)namebuf, 0, 0) != -1;
    }

    static bool set(const char* name)
    {
        return prctl(PR_SET_NAME, (unsigned long)name, 0, 0) != -1;
    }


    ThreadName(const char* name)
    {
        if ( get(old_name) )
        {
            snprintf(new_name, 127, "%s", name);
            new_name[127] = 0;
            prctl(PR_SET_NAME, (unsigned long)new_name, 0, 0);
        }
    }

    ~ThreadName()
    {
        if ( good )
            prctl(PR_SET_NAME, (unsigned long)old_name, 0, 0);
    }
};

#elif defined WIN32

// https://docs.microsoft.com/en-us/visualstudio/debugger/how-to-set-a-thread-name-in-native-code
//  
// Usage: SetThreadName ((DWORD)-1, "MainThread");  
//  
#include <windows.h>  
const DWORD MS_VC_EXCEPTION = 0x406D1388;  
#pragma pack(push,8)  
typedef struct tagTHREADNAME_INFO  
{  
	DWORD dwType; // Must be 0x1000.  
	LPCSTR szName; // Pointer to name (in user addr space).  
	DWORD dwThreadID; // Thread ID (-1=caller thread).  
	DWORD dwFlags; // Reserved for future use, must be zero.  
} THREADNAME_INFO;  
#pragma pack(pop)  
inline void SetThreadName(DWORD dwThreadID, const char* threadName) {  
	THREADNAME_INFO info;  
	info.dwType = 0x1000;  
	info.szName = threadName;  
	info.dwThreadID = dwThreadID;  
	info.dwFlags = 0;  
#pragma warning(push)  
#pragma warning(disable: 6320 6322)  
	__try{  
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);  
	}  
	__except (EXCEPTION_EXECUTE_HANDLER){  
	}
#pragma warning(pop)  
}

class ThreadName
{
public:

	// this isn't exposed, see https://stackoverflow.com/questions/9366722/how-to-get-the-name-of-a-win32-thread
	static bool get(char*) { return false; }

	ThreadName(const char* name)
	{
		SetThreadName(GetCurrentThreadId(), name);
	}

	~ThreadName()
	{
	}
};


#else

// Fake class, which does nothing. You can also take a look how
// this works in other systems that are not supported here and add
// the support. This is a fallback for systems that do not support
// thread names.

class ThreadName
{
public:

    static bool get(char*) { return false; }

    ThreadName(const char*)
    {
    }

    ~ThreadName() // just to make it "non-trivially-destructible" for compatibility with normal version
    {
    }
};



#endif
#endif
