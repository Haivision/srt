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

#ifndef INC__SRTC_H
#define INC__SRTC_H

#include "udt.h"
#include <string.h>

#define SRT_API UDT_API

#ifdef __cplusplus
extern "C" {
#endif

typedef UDTSOCKET SRTSOCKET; // UDTSOCKET is a typedef to int anyway, and it's not even in UDT namespace :)


// Values returned by srt_getsockstate()
typedef enum SRT_SOCKSTATUS {
	SRTS_INIT = 1,
	SRTS_OPENED,
	SRTS_LISTENING,
	SRTS_CONNECTING,
	SRTS_CONNECTED,
	SRTS_BROKEN,
	SRTS_CLOSING,
	SRTS_CLOSED,
	SRTS_NONEXIST
} SRT_SOCKSTATUS;

// This is a duplicate enum. Must be kept in sync with the original UDT enum for
// backward compatibility until all compat is destroyed.
typedef enum SRT_SOCKOPT {

	SRTO_MSS,             // the Maximum Transfer Unit
	SRTO_SNDSYN,          // if sending is blocking
	SRTO_RCVSYN,          // if receiving is blocking
	SRTO_CC,              // custom congestion control algorithm
	SRTO_FC,              // Flight flag size (window size)
	SRTO_SNDBUF,          // maximum buffer in sending queue
	SRTO_RCVBUF,          // UDT receiving buffer size
	SRTO_LINGER,          // waiting for unsent data when closing
	SRTO_UDP_SNDBUF,      // UDP sending buffer size
	SRTO_UDP_RCVBUF,      // UDP receiving buffer size
	SRTO_MAXMSG,          // maximum datagram message size
	SRTO_MSGTTL,          // time-to-live of a datagram message
	SRTO_RENDEZVOUS,      // rendezvous connection mode
	SRTO_SNDTIMEO,        // send() timeout
	SRTO_RCVTIMEO,        // recv() timeout
	SRTO_REUSEADDR,       // reuse an existing port or create a new one
	SRTO_MAXBW,           // maximum bandwidth (bytes per second) that the connection can use
	SRTO_STATE,           // current socket state, see UDTSTATUS, read only
	SRTO_EVENT,           // current available events associated with the socket
	SRTO_SNDDATA,         // size of data in the sending buffer
	SRTO_RCVDATA,         // size of data available for recv
	SRTO_SENDER = 21,     // Sender mode (independent of conn mode), for encryption, tsbpd handshake.
	SRTO_TSBPDMODE = 22,  // Enable/Disable TsbPd. Enable -> Tx set origin timestamp, Rx deliver packet at origin time + delay
	SRTO_TSBPDDELAY = 23, // TsbPd receiver delay (mSec) to absorb burst of missed packet retransmission
	SRTO_LATENCY = 23,    // ALIAS: SRTO_TSBPDDELAY
	SRTO_INPUTBW = 24,    // Estimated input stream rate.
	SRTO_OHEADBW,         // MaxBW ceiling based on % over input stream rate. Applies when UDT_MAXBW=0 (auto).
	SRTO_PASSPHRASE = 26, // Crypto PBKDF2 Passphrase size[0,10..64] 0:disable crypto
	SRTO_PBKEYLEN,        // Crypto key len in bytes {16,24,32} Default: 16 (128-bit)
	SRTO_KMSTATE,         // Key Material exchange status (UDT_SRTKmState)
	SRTO_IPTTL = 29,      // IP Time To Live
	SRTO_IPTOS,           // IP Type of Service
	SRTO_TLPKTDROP = 31,  // Enable receiver pkt drop
	SRTO_TSBPDMAXLAG,     // !!!IMPORTANT NOTE: obsolete parameter. Has no effect !!!
	SRTO_NAKREPORT = 33,  // Enable receiver to send periodic NAK reports
	SRTO_VERSION = 34,    // Local SRT Version
	SRTO_PEERVERSION,     // Peer SRT Version (from SRT Handshake)
	SRTO_CONNTIMEO = 36,   // Connect timeout in msec. Ccaller default: 3000, rendezvous (x 10)
	SRTO_TWOWAYDATA = 37,
	SRTO_SNDPBKEYLEN = 38,
	SRTO_RCVPBKEYLEN,
	SRTO_SNDPEERKMSTATE,
	SRTO_RCVKMSTATE,
	SRTO_LOSSMAXTTL,
} SRT_SOCKOPT;

// UDT error code
// Using duplicated wrapper until backward-compatible apps using UDT
// enum are destroyed.
typedef enum SRT_ERRNO {
    SRT_ESUCCESS = 0,
    SRT_ECONNSETUP =  (int)UDT_ECONNSETUP,
    SRT_ENOSERVER =  (int)UDT_ENOSERVER,
    SRT_ECONNREJ =  (int)UDT_ECONNREJ,
    SRT_ESOCKFAIL =  (int)UDT_ESOCKFAIL,
    SRT_ESECFAIL =  (int)UDT_ESECFAIL,
    SRT_ECONNFAIL =  (int)UDT_ECONNFAIL,
    SRT_ECONNLOST =  (int)UDT_ECONNLOST,
    SRT_ENOCONN =  (int)UDT_ENOCONN,
    SRT_ERESOURCE =  (int)UDT_ERESOURCE,
    SRT_ETHREAD =  (int)UDT_ETHREAD,
    SRT_ENOBUF =  (int)UDT_ENOBUF,
    SRT_EFILE =  (int)UDT_EFILE,
    SRT_EINVRDOFF =  (int)UDT_EINVRDOFF,
    SRT_ERDPERM =  (int)UDT_ERDPERM,
    SRT_EINVWROFF =  (int)UDT_EINVWROFF,
    SRT_EWRPERM =  (int)UDT_EWRPERM,
    SRT_EINVOP =  (int)UDT_EINVOP,
    SRT_EBOUNDSOCK =  (int)UDT_EBOUNDSOCK,
    SRT_ECONNSOCK =  (int)UDT_ECONNSOCK,
    SRT_EINVPARAM =  (int)UDT_EINVPARAM,
    SRT_EINVSOCK =  (int)UDT_EINVSOCK,
    SRT_EUNBOUNDSOCK =  (int)UDT_EUNBOUNDSOCK,
    SRT_ENOLISTEN =  (int)UDT_ENOLISTEN,
    SRT_ERDVNOSERV =  (int)UDT_ERDVNOSERV,
    SRT_ERDVUNBOUND =  (int)UDT_ERDVUNBOUND,
    SRT_ESTREAMILL =  (int)UDT_ESTREAMILL,
    SRT_EDGRAMILL =  (int)UDT_EDGRAMILL,
    SRT_EDUPLISTEN =  (int)UDT_EDUPLISTEN,
    SRT_ELARGEMSG =  (int)UDT_ELARGEMSG,
    SRT_EINVPOLLID =  (int)UDT_EINVPOLLID,
    SRT_EASYNCFAIL =  (int)UDT_EASYNCFAIL,
    SRT_EASYNCSND =  (int)UDT_EASYNCSND,
    SRT_EASYNCRCV =  (int)UDT_EASYNCRCV,
    SRT_ETIMEOUT =  (int)UDT_ETIMEOUT,
    SRT_ECONGEST =  (int)UDT_ECONGEST,
    SRT_EPEERERR =  (int)UDT_EPEERERR,
    SRT_EUNKNOWN = -1
} SRT_ERRNO;

typedef struct CPerfMon SRT_TRACEINFO;
typedef struct CBytePerfMon SRT_TRACEBSTATS;

// This structure is only a kind-of wannabe. The only use of it is currently
// the 'srctime', however the functionality of application-supplied timestamps
// also doesn't work properly. Left for future until the problems are solved.
// This may prove useful as currently there's no way to tell the application
// that TLPKTDROP facility has dropped some data in favor of timely delivery.
typedef struct SRT_MsgCtrl_ {
   int flags;
   int boundary;                        //0:mid pkt, 1(01b):end of frame, 2(11b):complete frame, 3(10b): start of frame
   uint64_t srctime;                    //source timestamp (usec), 0LL: use internal time     
} SRT_MSGCTRL;

static const SRTSOCKET SRT_INVALID_SOCK = -1;
static const int SRT_ERROR = -1;

// library initialization
SRT_API extern int srt_startup(void);
SRT_API extern int srt_cleanup(void);

// socket operations
SRT_API extern SRTSOCKET srt_socket(int af, int type, int protocol);
SRT_API extern int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
SRT_API extern int srt_bind_peerof(SRTSOCKET u, UDPSOCKET udpsock);
SRT_API extern int srt_listen(SRTSOCKET u, int backlog);
SRT_API extern SRTSOCKET srt_accept(SRTSOCKET u, struct sockaddr* addr, int* addrlen);
SRT_API extern int srt_connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
SRT_API extern int srt_close(SRTSOCKET u);
SRT_API extern int srt_getpeername(SRTSOCKET u, struct sockaddr* name, int* namelen);
SRT_API extern int srt_getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen);
SRT_API extern int srt_getsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT optname, void* optval, int* optlen);
SRT_API extern int srt_setsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT optname, const void* optval, int optlen);
SRT_API extern int srt_getsockflag(UDTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen);
SRT_API extern int srt_setsockflag(UDTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);
/* Don't use it, not proven to work
SRT_API extern int srt_send(SRTSOCKET u, const char* buf, int len, int flags);
SRT_API extern int srt_recv(SRTSOCKET u, char* buf, int len, int flags);
*/

// The sendmsg/recvmsg and their 2 counterpart require MAXIMUM the size of SRT payload size (1316).
// Any data over that size will be ignored.
SRT_API extern int srt_sendmsg(SRTSOCKET u, const char* buf, int len, int ttl/* = -1*/, int inorder/* = false*/);
SRT_API extern int srt_recvmsg(SRTSOCKET u, char* buf, int len);
SRT_API extern int srt_sendmsg2(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL *mctrl);
SRT_API extern int srt_recvmsg2(SRTSOCKET u, char *buf, int len, SRT_MSGCTRL *mctrl);

// last error detection
SRT_API extern const char* srt_getlasterror_str(void);
SRT_API extern int srt_getlasterror(int* errno_loc);
SRT_API extern const char* srt_strerror(int code, int errnoval);
SRT_API extern void srt_clearlasterror(void);

// performance track
SRT_API extern int srt_perfmon(SRTSOCKET u, SRT_TRACEINFO * perf, int clear);
SRT_API extern int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear);

// Socket Status (for problem tracking)
SRT_API extern SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u);

// event mechanism
// select and selectEX are DEPRECATED; please use epoll.
typedef enum SRT_EPOLL_OPT
{
	// this values are defined same as linux epoll.h
	// so that if system values are used by mistake, they should have the same effect
	SRT_EPOLL_IN  = 0x1,
	SRT_EPOLL_OUT = 0x4,
	SRT_EPOLL_ERR = 0x8
} SRT_EPOLL_OPT;

#ifdef __cplusplus
// In C++ these enums cannot be treated as int and glued by operator |.
// Unless this operator is defined.
inline SRT_EPOLL_OPT operator|(SRT_EPOLL_OPT a1, SRT_EPOLL_OPT a2)
{
    return SRT_EPOLL_OPT( (int)a1 | (int)a2 );
}
#endif

SRT_API extern int srt_epoll_create(void);
SRT_API extern int srt_epoll_add_usock(int eid, SRTSOCKET u, const int* events);
SRT_API extern int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int* events);
SRT_API extern int srt_epoll_remove_usock(int eid, SRTSOCKET u);
SRT_API extern int srt_epoll_remove_ssock(int eid, SYSSOCKET s);
SRT_API extern int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events);
SRT_API extern int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events);
///SRT_API extern int srt_epoll_wait(int eid, std::set<SRTSOCKET>* readfds, std::set<SRTSOCKET>* writefds, int64_t msTimeOut,
///                       std::set<SYSSOCKET>* lrfds = NULL, std::set<SYSSOCKET>* wrfds = NULL);
SRT_API extern int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                        SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
SRT_API extern int srt_epoll_release(int eid);

// Logging control

SRT_API void srt_setloglevel(int ll);
SRT_API void srt_addlogfa(int fa);
SRT_API void srt_dellogfa(int fa);
SRT_API void srt_resetlogfa(const int* fara, size_t fara_size);
// This isn't predicted, will be only available in SRT C++ API.
// For the time being, until this API is ready, use UDT::setlogstream.
// SRT_API void srt_setlogstream(std::ostream& stream);
SRT_API void srt_setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
SRT_API void srt_setlogflags(int flags);

#ifdef __cplusplus
}
#endif

#endif
