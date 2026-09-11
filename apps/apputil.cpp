/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

#include <cstring>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <utility>
#include <memory>

#include "srt.h" // Required for SRT_SYNC_CLOCK_* definitions.
#include "common.h"
#include "apputil.hpp"
#include "netinet_any.h"
#include "hvu_compat.h"
#include "ofmt.h"

using namespace std;
using namespace srt;


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
#if defined(_WIN32) && !defined(HAVE_INET_PTON)
namespace // Prevent conflict in case when still defined
{
int inet_pton(int af, const char * src, void * dst)
{
   struct sockaddr_storage ss;
   int ssSize = sizeof(ss);
   char srcCopy[INET6_ADDRSTRLEN + 1];

   ZeroMemory(&ss, sizeof(ss));

   // work around non-const API
#ifdef _MSC_VER
   strncpy_s(srcCopy, INET6_ADDRSTRLEN + 1, src, _TRUNCATE);
#else
   strncpy(srcCopy, src, INET6_ADDRSTRLEN);
   srcCopy[INET6_ADDRSTRLEN] = '\0';
#endif
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
}
#endif // _WIN32 && !HAVE_INET_PTON

sockaddr_any CreateAddr(const string& name, unsigned short port, int pref_family)
{
    // Handle empty name.
    // If family is specified, empty string resolves to ANY of that family.
    // If not, it resolves to IPv4 ANY (to specify IPv6 any, use [::]).
    if (name == "")
    {
        sockaddr_any result(pref_family == AF_INET6 ? pref_family : AF_INET);
        result.hport(port);
        return result;
    }

    bool first6 = pref_family != AF_INET;
    int families[2] = {AF_INET6, AF_INET};
    if (!first6)
    {
        families[0] = AF_INET;
        families[1] = AF_INET6;
    }

    for (int i = 0; i < 2; ++i)
    {
        int family = families[i];
        sockaddr_any result (family);

        // Try to resolve the name by pton first
        if (inet_pton(family, name.c_str(), result.get_addr()) == 1)
        {
            result.hport(port); // same addr location in ipv4 and ipv6
            return result;
        }
    }

    // If not, try to resolve by getaddrinfo
    // This time, use the exact value of pref_family

    sockaddr_any result;
    addrinfo fo = {
        0,
        pref_family,
        0, 0,
        0, 0,
        NULL, NULL
    };

    addrinfo* val = nullptr;
    int erc = getaddrinfo(name.c_str(), nullptr, &fo, &val);
    if (erc == 0)
    {
        result.set(val->ai_addr);
        result.len = result.size();
        result.hport(port); // same addr location in ipv4 and ipv6
    }
    freeaddrinfo(val);

    return result;
}

string Join(const vector<string>& in, string sep)
{
    if (in.empty())
        return "";

    hvu::ofmt_bufs os;

    os << in[0];
    for (auto i = in.begin()+1; i != in.end(); ++i)
        os << sep << *i;
    return os.str();
}

const char* SRTClockTypeStr()
{
    const int clock_type = srt_clock_type();

    switch (clock_type)
    {
    case SRT_SYNC_CLOCK_STDCXX_STEADY:
        return "CXX11_STEADY";
    case SRT_SYNC_CLOCK_GETTIME_MONOTONIC:
        return "GETTIME_MONOTONIC";
    case SRT_SYNC_CLOCK_WINQPC:
        return "WIN_QPC";
    case SRT_SYNC_CLOCK_MACH_ABSTIME:
        return "MACH_ABSTIME";
    case SRT_SYNC_CLOCK_POSIX_GETTIMEOFDAY:
        return "POSIX_GETTIMEOFDAY";
    default:
        break;
    }
    
    return "UNKNOWN VALUE";
}

void PrintLibVersion()
{
    cerr << "Built with SRT Library version: " << SRT_VERSION  << endl;
    const uint32_t srtver = srt_getversion();
    const int major = srtver / 0x10000;
    const int minor = (srtver / 0x100) % 0x100;
    const int patch = srtver % 0x100;
    cerr << "SRT Library version: " << major << "." << minor << "." << patch << ", clock type: " << SRTClockTypeStr() << endl;
}

bool IsTargetAddrSelf(const sockaddr* boundaddr, const sockaddr* targetaddr)
{
    sockaddr_any bound = boundaddr;
    sockaddr_any target = targetaddr;

    if (!bound.isany())
    {
        // Bound to a specific local address, so only check if
        // this isn't the same address as 'target'.
        if (target.equal_address(bound))
        {
            return true;
        }
    }
    else
    {
        // Bound to INADDR_ANY, so check matching with any local IP address
        const vector<srt::LocalInterface>& locals = srt::GetLocalInterfaces();

        // If any of the above function fails, it will collect
        // no local interfaces, so it's impossible to check anything.
        // OTOH it should also mean that the network isn't working,
        // so it's unlikely, as well as no address should match the
        // local address anyway.
        for (size_t i = 0; i < locals.size(); ++i)
        {
            if (locals[i].addr.equal_address(target))
            {
                return true;
            }
        }
    }

    return false;
}

