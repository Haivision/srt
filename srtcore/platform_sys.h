/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */
#ifndef INC__PLATFORM_SYS_H
#define INC__PLATFORM_SYS_H

#ifdef WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <ws2ipdef.h>
   #include <windows.h>
   #if defined(_MSC_VER)
      #include <win/stdint.h>
      #include <win/inttypes.h>
      #pragma warning(disable:4251)
   #elif defined(__MINGW32__)
      #include <inttypes.h>
      #include <stdint.h>
   #endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

#endif
