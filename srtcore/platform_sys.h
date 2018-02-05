#ifndef INC__PLATFORM_SYS_H
#define INC__PLATFORM_SYS_H

#ifdef WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <ws2ipdef.h>
   #include <windows.h>
   #include <inttypes.h>
   #include <stdint.h>
   #include "win/wintime.h"
   #if defined(_MSC_VER)
      #pragma warning(disable:4251)
   #endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

#endif
