
// Copied from: https://gist.github.com/panzi/6856583
// License: Public Domain.

#ifndef INC_HVU_BYTE_ORDER_H
#define INC_HVU_BYTE_ORDER_H

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__)

#	define __WINDOWS__

#endif

#if defined(__linux__) || defined(__CYGWIN__) || defined(__GNU__) || defined(__GLIBC__)

#	include <endian.h>

// GLIBC-2.8 and earlier does not provide these macros.
// See http://linux.die.net/man/3/endian
// From https://gist.github.com/panzi/6856583
#   if defined(__GLIBC__) \
      && ( !defined(__GLIBC_MINOR__) \
         || ((__GLIBC__ < 2) \
         || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ < 9))) )
#       include <arpa/inet.h>
#       if defined(__BYTE_ORDER) && (__BYTE_ORDER == __LITTLE_ENDIAN)

#           define htole32(x) (x)
#           define le32toh(x) (x)

#       elif defined(__BYTE_ORDER) && (__BYTE_ORDER == __BIG_ENDIAN)

#           define htole16(x) ((((((uint16_t)(x)) >> 8))|((((uint16_t)(x)) << 8)))
#           define le16toh(x) ((((((uint16_t)(x)) >> 8))|((((uint16_t)(x)) << 8)))

#           define htole32(x) (((uint32_t)htole16(((uint16_t)(((uint32_t)(x)) >> 16)))) | (((uint32_t)htole16(((uint16_t)(x)))) << 16))
#           define le32toh(x) (((uint32_t)le16toh(((uint16_t)(((uint32_t)(x)) >> 16)))) | (((uint32_t)le16toh(((uint16_t)(x)))) << 16))

#       else
#           error Byte Order not supported or not defined.
#       endif
#   endif

#elif defined(__APPLE__)

#	include <libkern/OSByteOrder.h>

#	define htobe16(x) OSSwapHostToBigInt16(x)
#	define htole16(x) OSSwapHostToLittleInt16(x)
#	define be16toh(x) OSSwapBigToHostInt16(x)
#	define le16toh(x) OSSwapLittleToHostInt16(x)
 
#	define htobe32(x) OSSwapHostToBigInt32(x)
#	define htole32(x) OSSwapHostToLittleInt32(x)
#	define be32toh(x) OSSwapBigToHostInt32(x)
#	define le32toh(x) OSSwapLittleToHostInt32(x)
 
#	define htobe64(x) OSSwapHostToBigInt64(x)
#	define htole64(x) OSSwapHostToLittleInt64(x)
#	define be64toh(x) OSSwapBigToHostInt64(x)
#	define le64toh(x) OSSwapLittleToHostInt64(x)

#	define __BYTE_ORDER    BYTE_ORDER
#	define __BIG_ENDIAN    BIG_ENDIAN
#	define __LITTLE_ENDIAN LITTLE_ENDIAN
#	define __PDP_ENDIAN    PDP_ENDIAN

#elif defined(__OpenBSD__)

#	include <sys/endian.h>

#elif defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__) || defined(__FreeBSD_kernel__)

#	include <sys/endian.h>

#ifndef be16toh
#	define be16toh(x) betoh16(x)
#endif
#ifndef le16toh
#	define le16toh(x) letoh16(x)
#endif

#ifndef be32toh
#	define be32toh(x) betoh32(x)
#endif
#ifndef le32toh
#	define le32toh(x) letoh32(x)
#endif

#ifndef be64toh
#	define be64toh(x) betoh64(x)
#endif
#ifndef le64toh
#	define le64toh(x) letoh64(x)
#endif

#elif defined(SUNOS)

   // SunOS/Solaris

   #include <sys/byteorder.h>
   #include <sys/isa_defs.h>

   #define __LITTLE_ENDIAN 1234
   #define __BIG_ENDIAN 4321

   # if defined(_BIG_ENDIAN)
   #define __BYTE_ORDER __BIG_ENDIAN
   #define be64toh(x) (x)
   #define be32toh(x) (x)
   #define be16toh(x) (x)
   #define le16toh(x) ((uint16_t)BSWAP_16(x))
   #define le32toh(x) BSWAP_32(x)
   #define le64toh(x) BSWAP_64(x)
   #define htobe16(x) (x)
   #define htole16(x) ((uint16_t)BSWAP_16(x))
   #define htobe32(x) (x)
   #define htole32(x) BSWAP_32(x)
   #define htobe64(x) (x)
   #define htole64(x) BSWAP_64(x)
   # else
   #define __BYTE_ORDER __LITTLE_ENDIAN
   #define be64toh(x) BSWAP_64(x)
   #define be32toh(x) ntohl(x)
   #define be16toh(x) ntohs(x)
   #define le16toh(x) (x)
   #define le32toh(x) (x)
   #define le64toh(x) (x)
   #define htobe16(x) htons(x)
   #define htole16(x) (x)
   #define htobe32(x) htonl(x)
   #define htole32(x) (x)
   #define htobe64(x) BSWAP_64(x)
   #define htole64(x) (x)
   # endif

#elif defined(__WINDOWS__)

#	include <winsock2.h>

#	if BYTE_ORDER == LITTLE_ENDIAN

#		define htobe16(x) htons(x)
#		define htole16(x) (x)
#		define be16toh(x) ntohs(x)
#		define le16toh(x) (x)
 
#		define htobe32(x) htonl(x)
#		define htole32(x) (x)
#		define be32toh(x) ntohl(x)
#		define le32toh(x) (x)
 
#		define htobe64(x) htonll(x)
#		define htole64(x) (x)
#		define be64toh(x) ntohll(x)
#		define le64toh(x) (x)

#	elif BYTE_ORDER == BIG_ENDIAN

		/* that would be xbox 360 */
#		define htobe16(x) (x)
#		define htole16(x) __builtin_bswap16(x)
#		define be16toh(x) (x)
#		define le16toh(x) __builtin_bswap16(x)
 
#		define htobe32(x) (x)
#		define htole32(x) __builtin_bswap32(x)
#		define be32toh(x) (x)
#		define le32toh(x) __builtin_bswap32(x)
 
#		define htobe64(x) (x)
#		define htole64(x) __builtin_bswap64(x)
#		define be64toh(x) (x)
#		define le64toh(x) __builtin_bswap64(x)

#	else

#		error byte order not supported

#	endif // BYTE_ORDER

#	define __BYTE_ORDER    BYTE_ORDER
#	define __BIG_ENDIAN    BIG_ENDIAN
#	define __LITTLE_ENDIAN LITTLE_ENDIAN
#	define __PDP_ENDIAN    PDP_ENDIAN

#else

#	error Endian: platform not supported

#endif // Platform-dependent macro

#endif // INC_HVU_BYTE_ORDER_H
