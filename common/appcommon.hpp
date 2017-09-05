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


#ifndef INC__APPCOMMON_H
#define INC__APPCOMMON_H
 
#if WIN32

// Keep this below commented out.
// This is for a case when you need cpp debugging on Windows.
//#ifdef _WINSOCKAPI_
//#error "You include <winsock.h> somewhere, remove it. It causes conflicts"
//#endif

#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
// WIN32 API does not have sleep() and usleep(), Although MINGW does.
#ifdef __MINGW32__
#include <unistd.h>
#else
extern "C" inline int sleep(int seconds) { Sleep(seconds * 1000); return 0; }
#endif

inline bool SysInitializeNetwork()
{
    WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData;
    return WSAStartup(wVersionRequested, &wsaData) == 0;
}

inline void SysCleanupNetwork()
{
    WSACleanup();
}

#else
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// Nothing needs to be done on POSIX; this is a Windows problem.
inline bool SysInitializeNetwork() {return true;}
inline void SysCleanupNetwork() {}

#endif

#include <string>
#include <cstring>
// For Options
#include <iostream>
#include <sstream>
#include <vector>
#include <map>

// NOTE: MINGW currently does not include support for inet_pton(). See
//    http://mingw.5.n7.nabble.com/Win32API-request-for-new-functions-td22029.html
//    Even if it did support inet_pton(), it is only available on Windows Vista
//    and later. Since we need to support WindowsXP and later in ORTHRUS. Many
//    customers still use it, we will need to implement using something like
//    WSAStringToAddress() which is available on Windows95 and later.
//    Support for IPv6 was added on WindowsXP SP1.
// Header: winsock2.h
// Implementation: ws2_32.dll
// See:
//    https://msdn.microsoft.com/en-us/library/windows/desktop/ms742214(v=vs.85).aspx
//    http://www.winsocketdotnetworkprogramming.com/winsock2programming/winsock2advancedInternet3b.html
#ifdef __MINGW32__
static inline int inet_pton(int af, const char * src, void * dst)
{
   struct sockaddr_storage ss;
   int ssSize = sizeof(ss);
   char srcCopy[INET6_ADDRSTRLEN + 1];

   ZeroMemory(&ss, sizeof(ss));

   // work around stupid non-const API
   strncpy(srcCopy, src, INET6_ADDRSTRLEN + 1);
   srcCopy[INET6_ADDRSTRLEN] = '\0';

   if (WSAStringToAddress(
      srcCopy, af, NULL, (struct sockaddr *)&ss, &ssSize) != 0)
   {
      return 0;
   }

   switch (af)
   {
      case AF_INET :
      {
         *(struct in_addr *)dst = ((struct sockaddr_in *)&ss)->sin_addr;
         return 1;
      }
      case AF_INET6 :
      {
         *(struct in6_addr *)dst = ((struct sockaddr_in6 *)&ss)->sin6_addr;
         return 1;
      }
      default :
      {
         // No-Op
      }
   }

   return 0;
}
#endif // __MINGW__

inline sockaddr_in CreateAddrInet(const std::string& name, unsigned short port)
{
    sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if ( name != "" )
    {
        if ( inet_pton(AF_INET, name.c_str(), &sa.sin_addr) == 1 )
            return sa;

        // XXX RACY!!! Use getaddrinfo() instead. Check portability.
        // Windows/Linux declare it.
        // See:
        //  http://www.winsocketdotnetworkprogramming.com/winsock2programming/winsock2advancedInternet3b.html
        hostent* he = gethostbyname(name.c_str());
        if ( !he || he->h_addrtype != AF_INET )
            throw std::invalid_argument("SrtSource: host not found: " + name);

        sa.sin_addr = *(in_addr*)he->h_addr_list[0];
    }

    return sa;
}

inline std::string Join(const std::vector<std::string>& in, std::string sep)
{
    if ( in.empty() )
        return "";

    std::ostringstream os;

    os << in[0];
    for (auto i = in.begin()+1; i != in.end(); ++i)
        os << sep << *i;
    return os.str();
}

typedef std::map<std::string, std::vector<std::string>> options_t;

struct OutList
{
    typedef std::vector<std::string> type;
    static type process(const options_t::mapped_type& i) { return i; }
};

struct OutString
{
    typedef std::string type;
    static type process(const options_t::mapped_type& i) { return Join(i, " "); }
};


template <class OutType, class OutValue> inline
typename OutType::type Option(const options_t&, OutValue deflt=OutValue()) { return deflt; }

template <class OutType, class OutValue, class... Args> inline
typename OutType::type Option(const options_t& options, OutValue deflt, std::string key, Args... further_keys)
{
    auto i = options.find(key);
    if ( i == options.end() )
        return Option<OutType>(options, deflt, further_keys...);
    return OutType::process(i->second);
}

inline options_t ProcessOptions(char* const* argv, int argc)
{
    using namespace std;

    string current_key = "";
    map<string, vector<string>> params;

    for (char* const* p = argv+1; p != argv+argc; ++p)
    {
        const char* a = *p;
        if ( a[0] == '-' )
        {
            current_key = a+1;
            params[current_key].clear();
            continue;
        }

        params[current_key].push_back(a);
    }

    return params;
}


#endif // INC__APPCOMMON_H
