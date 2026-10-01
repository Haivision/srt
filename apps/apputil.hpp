/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


#ifndef INC_SRT_APPCOMMON_H
#define INC_SRT_APPCOMMON_H

#include <string>
#include <map>
#include <set>
#include <vector>
#include <memory>

#include "netinet_any.h"
#include "utilities.h"
#include "srt.h"
#include "options.hpp"

namespace srt
{
    using namespace hvu;
}

#if _WIN32

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

// Fixes Android build on NDK r16b and earlier.
#if defined(__ANDROID__) && (__ANDROID__ == 1)
   #include <android/ndk-version.h>
   #if !defined(__NDK_MAJOR__) || (__NDK_MAJOR__ <= 16)
      struct ip_mreq_sourceFIXED {
        struct in_addr imr_multiaddr;
        struct in_addr imr_interface;
        struct in_addr imr_sourceaddr;
      };
      #define ip_mreq_source ip_mreq_sourceFIXED
   #endif
#endif

// Nothing needs to be done on POSIX; this is a Windows problem.
inline bool SysInitializeNetwork() {return true;}
inline void SysCleanupNetwork() {}

#endif

#ifdef _WIN32
inline int SysError() { return ::GetLastError(); }
const int SysAGAIN = WSAEWOULDBLOCK;
#else
inline int SysError() { return errno; }
const int SysAGAIN = EAGAIN;
#endif

srt::sockaddr_any CreateAddr(const std::string& name, unsigned short port = 0, int pref_family = AF_UNSPEC);
std::string Join(const std::vector<std::string>& in, std::string sep);

template <class VarType, class ValType>
struct OnReturnSetter
{
    VarType& var;
    ValType value;

    OnReturnSetter(VarType& target, ValType v): var(target), value(v) {}
    ~OnReturnSetter() { var = value; }
};

template <class VarType, class ValType>
OnReturnSetter<VarType, ValType> OnReturnSet(VarType& target, ValType v)
{ return OnReturnSetter<VarType, ValType>(target, v); }

const char* SRTClockTypeStr();
void PrintLibVersion();
bool IsTargetAddrSelf(const sockaddr* boundaddr, const sockaddr* targetaddr);


namespace srt
{

struct OptionSetterProxy
{
    SRTSOCKET s;
    SRTSTATUS result = SRT_ERROR;

    OptionSetterProxy(SRTSOCKET ss): s(ss) {}

    struct OptionProxy
    {
        OptionSetterProxy& parent;
        SRT_SOCKOPT opt;

#define SPEC(type) \
        OptionProxy& operator=(const type& val)\
        {\
            parent.result = srt_setsockflag(parent.s, opt, &val, sizeof val);\
            return *this;\
        }

        SPEC(int32_t);
        SPEC(int64_t);
        SPEC(bool);
#undef SPEC

        template<size_t N>
        OptionProxy& operator=(const char (&val)[N])
        {
            parent.result = srt_setsockflag(parent.s, opt, val, N-1);
            return *this;
        }

        OptionProxy& operator=(const std::string& val)
        {
            parent.result = srt_setsockflag(parent.s, opt, val.c_str(), val.size());
            return *this;
        }
    };

    OptionProxy operator[](SRT_SOCKOPT opt)
    {
        return OptionProxy {*this, opt};
    }

    operator SRTSTATUS() { return result; }
};

inline OptionSetterProxy setopt(SRTSOCKET socket)
{
    return OptionSetterProxy(socket);
}

}
#endif // INC_SRT_APPCOMMON_H
