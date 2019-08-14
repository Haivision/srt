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
#include <iostream>
#include <sstream>
#include <utility>

#include "apputil.hpp"
#include "netinet_any.h"

#ifdef WIN32
   #include <iphlpapi.h> // getting local interfaces
#else
   #include <ifaddrs.h> // getting local interfaces
#endif

using namespace std;


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
#if defined(__MINGW32__) && !defined(InetPton)
namespace // Prevent conflict in case when still defined
{
int inet_pton(int af, const char * src, void * dst)
{
   struct sockaddr_storage ss;
   int ssSize = sizeof(ss);
   char srcCopy[INET6_ADDRSTRLEN + 1];

   ZeroMemory(&ss, sizeof(ss));

   // work around non-const API
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
}
#endif // __MINGW__

sockaddr_in CreateAddrInet(const string& name, unsigned short port)
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
            throw invalid_argument("SrtSource: host not found: " + name);

        sa.sin_addr = *(in_addr*)he->h_addr_list[0];
    }

    return sa;
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
        if (moreoptions && a[0] == '-')
        {
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
            for (auto s: scheme)
            {
                if (s.names.count(current_key))
                {
                    // cout << "*D found '" << current_key << "' in scheme type=" << int(s.type) << endl;
                    if (s.type == OptionScheme::ARG_NONE)
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

