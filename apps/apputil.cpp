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
#include <sstream>
#include <utility>
#include <memory>

#include <ifaddrs.h>
#include "srt.h" // Required for SRT_SYNC_CLOCK_* definitions.
#include "apputil.hpp"
#include "netinet_any.h"
#include "srt_compat.h"

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
    if ( in.empty() )
        return "";

    ostringstream os;

    os << in[0];
    for (auto i = in.begin()+1; i != in.end(); ++i)
        os << sep << *i;
    return os.str();
}

// OPTION LIBRARY

OptionScheme::Args OptionName::DetermineTypeFromHelpText(const std::string& helptext)
{
    if (helptext.empty())
        return OptionScheme::ARG_NONE;

    if (helptext[0] == '<')
    {
        // If the argument is <one-argument>, then it's ARG_NONE.
        // If it's <multiple-arguments...>, then it's ARG_VAR.
        // When closing angle bracket isn't found, fallback to ARG_ONE.
        size_t pos = helptext.find('>');
        if (pos == std::string::npos)
            return OptionScheme::ARG_ONE; // mistake, but acceptable

        if (pos >= 4 && helptext.substr(pos-3, 4) == "...>")
            return OptionScheme::ARG_VAR;

        // We have < and > without ..., simply one argument
        return OptionScheme::ARG_ONE;
    }

    if (helptext[0] == '[')
    {
        // Argument in [] means it is optional; in this case
        // you should state that the argument can be given or not.
        return OptionScheme::ARG_VAR;
    }

    // Also as fallback
    return OptionScheme::ARG_NONE;
}

options_t ProcessOptions(char* const* argv, int argc, std::vector<OptionScheme> scheme)
{
    using namespace std;

    string current_key;
    string extra_arg;
    size_t vals = 0;
    OptionScheme::Args type = OptionScheme::ARG_VAR; // This is for no-option-yet or consumed
    map<string, vector<string>> params;
    bool moreoptions = true;

    for (char* const* p = argv+1; p != argv+argc; ++p)
    {
        const char* a = *p;
        // cout << "*D ARG: '" << a << "'\n";
        bool isoption = false;
        if (a[0] == '-')
        {
            isoption = true;
            // If a[0] isn't NUL - because it is dash - then
            // we can safely check a[1].
            // An expression starting with a dash is not
            // an option marker if it is a single dash or
            // a negative number.
            if (!a[1] || isdigit(a[1]))
                isoption = false;
        }

        if (moreoptions && isoption)
        {
            bool arg_specified = false;
            size_t seppos; // (see goto, it would jump over initialization)
            current_key = a+1;
            if ( current_key == "-" )
            {
                // The -- argument terminates the options.
                // The default key is restored to empty so that
                // it collects now all arguments under the empty key
                // (not-option-assigned argument).
                moreoptions = false;
                goto EndOfArgs;
            }

            // Maintain the backward compatibility with argument specified after :
            // or with one string separated by space inside.
            seppos = current_key.find(':');
            if (seppos == string::npos)
                seppos = current_key.find(' ');
            if (seppos != string::npos)
            {
                // Old option specification.
                extra_arg = current_key.substr(seppos + 1);
                current_key = current_key.substr(0, 0 + seppos);
                arg_specified = true; // Prevent eating args from option list
            }

            params[current_key].clear();
            vals = 0;

            if (extra_arg != "")
            {
                params[current_key].push_back(extra_arg);
                ++vals;
                extra_arg.clear();
            }

            // Find the key in the scheme. If not found, treat it as ARG_NONE.
            for (const auto& s: scheme)
            {
                if (s.names().count(current_key))
                {
                    // cout << "*D found '" << current_key << "' in scheme type=" << int(s.type) << endl;
                    // If argument was specified using the old way, like
                    // -v:0 or "-v 0", then consider the argument specified and
                    // treat further arguments as either no-option arguments or
                    // new options.
                    if (s.type == OptionScheme::ARG_NONE || arg_specified)
                    {
                        // Anyway, consider it already processed.
                        break;
                    }
                    type = s.type;

                    if ( vals == 1 && type == OptionScheme::ARG_ONE )
                    {
                        // Argument for one-arg option already consumed,
                        // so set to free args.
                        goto EndOfArgs;
                    }
                    goto Found;
                }

            }
            // Not found: set ARG_NONE.
            // cout << "*D KEY '" << current_key << "' assumed type NONE\n";
EndOfArgs:
            type = OptionScheme::ARG_VAR;
            current_key = "";
Found:
            continue;
        }

        // Collected a value - check if full
        // cout << "*D COLLECTING '" << a << "' for key '" << current_key << "' (" << vals << " so far)\n";
        params[current_key].push_back(a);
        ++vals;
        if ( vals == 1 && type == OptionScheme::ARG_ONE )
        {
            // cout << "*D KEY TYPE ONE - resetting to empty key\n";
            // Reset the key to "default one".
            current_key = "";
            vals = 0;
            type = OptionScheme::ARG_VAR;
        }
        else
        {
            // cout << "*D KEY type VAR - still collecting until the end of options or next option.\n";
        }
    }

    return params;
}

string OptionHelpItem(const OptionName& o)
{
    string out = "\t-" + o.main_name;
    string hlp = o.helptext;
    string prefix;

    if (hlp == "")
    {
        hlp = " (Undocumented)";
    }
    else if (hlp[0] != ' ')
    {
        size_t end = string::npos;
        if (hlp[0] == '<')
        {
            end = hlp.find('>');
        }
        else if (hlp[0] == '[')
        {
            end = hlp.find(']');
        }

        if (end != string::npos)
        {
            ++end;
        }
        else
        {
            end = hlp.find(' ');
        }

        if (end != string::npos)
        {
            prefix = hlp.substr(0, end);
            //while (hlp[end] == ' ')
            //    ++end;
            hlp = hlp.substr(end);
            out += " " + prefix;
        }
    }

    out += " -" + hlp;
    return out;
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

#ifdef _WIN32
    #if SRT_ENABLE_CONSELF_CHECK_WIN32
        #define ENABLE_CONSELF_CHECK 1
    #endif
#else

// For non-Windows platofm, enable always.
#define ENABLE_CONSELF_CHECK 1
#endif

#if ENABLE_CONSELF_CHECK

static vector<sockaddr_any> GetLocalInterfaces()
{
    vector<sockaddr_any> locals;
#ifdef _WIN32
	ULONG flags = GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_ALL_INTERFACES;
	ULONG outBufLen4 = 0, outBufLen6 = 0, outBufLen = 0;

    // This function doesn't allocate memory by itself, you have to do it
    // yourself, worst case when it's too small, the size will be corrected
    // and the function will do nothing. So, simply, call the function with
    // always too little 0 size and make it show the correct one.
    GetAdaptersAddresses(AF_INET, flags, NULL, NULL, &outBufLen4);
	GetAdaptersAddresses(AF_INET, flags, NULL, NULL, &outBufLen6);
    // Ignore errors. Check errors on the real call.
	// (Have doubts about this "max" here, as VC reports errors when
	// using std::max, so it will likely resolve to a macro - hope this
	// won't cause portability problems, this code is Windows only.
	outBufLen = max(outBufLen4, outBufLen6);

    // Good, now we can allocate memory
    PIP_ADAPTER_ADDRESSES pAddresses = (PIP_ADAPTER_ADDRESSES)::operator new(outBufLen);
    ULONG st = GetAdaptersAddresses(AF_INET, flags, NULL, pAddresses, &outBufLen);
    if (st == ERROR_SUCCESS)
    {
        PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pAddresses->FirstUnicastAddress;
        while (pUnicast)
        {
            locals.push_back(pUnicast->Address.lpSockaddr);
            pUnicast = pUnicast->Next;
        }
    }
	st = GetAdaptersAddresses(AF_INET6, flags, NULL, pAddresses, &outBufLen);
	if (st == ERROR_SUCCESS)
	{
		PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pAddresses->FirstUnicastAddress;
		while (pUnicast)
		{
			locals.push_back(pUnicast->Address.lpSockaddr);
			pUnicast = pUnicast->Next;
		}
	}

    ::operator delete(pAddresses);

#else
    // Use POSIX method: getifaddrs
    struct ifaddrs* pif, * pifa;
    int st = getifaddrs(&pifa);
    if (st == 0)
    {
        for (pif = pifa; pif; pif = pif->ifa_next)
        {
            locals.push_back(pif->ifa_addr);
        }
    }

    freeifaddrs(pifa);
#endif
    return locals;
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
        vector<sockaddr_any> locals = GetLocalInterfaces();

        // If any of the above function fails, it will collect
        // no local interfaces, so it's impossible to check anything.
        // OTOH it should also mean that the network isn't working,
        // so it's unlikely, as well as no address should match the
        // local address anyway.
        for (size_t i = 0; i < locals.size(); ++i)
        {
            if (locals[i].equal_address(target))
            {
                return true;
            }
        }
    }

    return false;
}

#else
bool IsTargetAddrSelf(const sockaddr* , const sockaddr* )
{
    // State that the given address is never "self", so
    // prevention from connecting to self will not be in force.
    return false;
}
#endif
