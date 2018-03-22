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

#include <iterator>
#if __APPLE__
   #include "TargetConditionals.h"
#endif
#include "srt.h"
#include "common.h"


extern "C" {

int srt_startup() { return UDT::startup(); }
int srt_cleanup() { return UDT::cleanup(); }
UDTSOCKET srt_socket(int af, int type, int protocol) { return UDT::socket(af, type, protocol); }
int srt_bind(UDTSOCKET u, const struct sockaddr * name, int namelen) { return UDT::bind(u, name, namelen); }
int srt_bind_peerof(UDTSOCKET u, UDPSOCKET udpsock) { return UDT::bind2(u, udpsock); }
int srt_listen(UDTSOCKET u, int backlog) { return UDT::listen(u, backlog); }
UDTSOCKET srt_accept(UDTSOCKET u, struct sockaddr * addr, int * addrlen) { return UDT::accept(u, addr, addrlen); }
int srt_connect(UDTSOCKET u, const struct sockaddr * name, int namelen) { return UDT::connect(u, name, namelen); }

int srt_close(UDTSOCKET u)
{
    SRT_SOCKSTATUS st = srt_getsockstate(u);

    if ((st == SRTS_NONEXIST) ||
        (st == SRTS_CLOSED)   ||
        (st == SRTS_CLOSING) )
    {
        // It's closed already. Do nothing.
        return 0;
    }

    return UDT::close(u);
}

int srt_getpeername(UDTSOCKET u, struct sockaddr * name, int * namelen) { return UDT::getpeername(u, name, namelen); }
int srt_getsockname(UDTSOCKET u, struct sockaddr * name, int * namelen) { return UDT::getsockname(u, name, namelen); }
int srt_getsockopt(UDTSOCKET u, int level, SRT_SOCKOPT optname, void * optval, int * optlen)
{ return UDT::getsockopt(u, level, UDT::SOCKOPT(optname), optval, optlen); }
int srt_setsockopt(UDTSOCKET u, int level, SRT_SOCKOPT optname, const void * optval, int optlen)
{ return UDT::setsockopt(u, level, UDT::SOCKOPT(optname), optval, optlen); }

int srt_getsockflag(UDTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen)
{ return UDT::getsockopt(u, 0, UDT::SOCKOPT(opt), optval, optlen); }
int srt_setsockflag(UDTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen)
{ return UDT::setsockopt(u, 0, UDT::SOCKOPT(opt), optval, optlen); }

int srt_send(UDTSOCKET u, const char * buf, int len, int flags) { return UDT::send(u, buf, len, flags); }
int srt_recv(UDTSOCKET u, char * buf, int len, int flags) { return UDT::recv(u, buf, len, flags); }
int srt_sendmsg(UDTSOCKET u, const char * buf, int len, int ttl, int inorder) { return UDT::sendmsg(u, buf, len, ttl, 0!=  inorder); }
int srt_recvmsg(UDTSOCKET u, char * buf, int len) { return UDT::recvmsg(u, buf, len); }

int srt_sendmsg2(UDTSOCKET u, const char * buf, int len, SRT_MSGCTRL *mctrl)
{
    if (mctrl)
        return UDT::sendmsg(u, buf, len, -1, true, mctrl->srctime);
    else
        return UDT::sendmsg(u, buf, len);
}

int srt_recvmsg2(UDTSOCKET u, char * buf, int len, SRT_MSGCTRL *mctrl)
{
    uint64_t srctime = 0;
    int rc = UDT::recvmsg(u, buf, len, srctime);
    if (rc == UDT::ERROR) {
        // error happen
        return -1;
    }

    if (mctrl)
        mctrl->srctime = srctime;
    return rc;
}

const char* srt_getlasterror_str() { return UDT::getlasterror().getErrorMessage(); }

int srt_getlasterror(int* loc_errno)
{
    if ( loc_errno )
        *loc_errno = UDT::getlasterror().getErrno();
    return UDT::getlasterror().getErrorCode();
}

const char* srt_strerror(int code, int err)
{
    static CUDTException e;
    e = CUDTException(CodeMajor(code/1000), CodeMinor(code%1000), err);
    return(e.getErrorMessage());
}


void srt_clearlasterror()
{
    UDT::getlasterror().clear();
}

int srt_perfmon(UDTSOCKET u, SRT_TRACEINFO * perf, int clear) { return UDT::perfmon(u, perf, 0!=  clear); }
int srt_bstats(UDTSOCKET u, SRT_TRACEBSTATS * perf, int clear) { return UDT::bstats(u, perf, 0!=  clear); }

SRT_SOCKSTATUS srt_getsockstate(UDTSOCKET u) { return SRT_SOCKSTATUS((int)UDT::getsockstate(u)); }

// event mechanism
int srt_epoll_create() { return UDT::epoll_create(); }

// You can use either SRT_EPOLL_* flags or EPOLL* flags from <sys/epoll.h>, both are the same. IN/OUT/ERR only.
// events == NULL accepted, in which case all flags are set.
int srt_epoll_add_usock(int eid, UDTSOCKET u, const int * events) { return UDT::epoll_add_usock(eid, u, events); }

int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

#ifdef LINUX
    if (events) {
        flag = *events;
	} else {
        flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
    }
#elif defined(OSX) || defined(TARGET_OS_IOS) || defined(TARGET_OS_TV)
    if (events) {
        flag = *events;
	} else {
        flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
    }
#else
    flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
#endif

    // call UDT native function
    return UDT::epoll_add_ssock(eid, s, &flag);
}

int srt_epoll_remove_usock(int eid, UDTSOCKET u) { return UDT::epoll_remove_usock(eid, u); }
int srt_epoll_remove_ssock(int eid, SYSSOCKET s) { return UDT::epoll_remove_ssock(eid, s); }

int srt_epoll_update_usock(int eid, UDTSOCKET u, const int * events)
{
	int srt_ev = 0;

	if (events) {
        srt_ev = *events;
	} else {
		srt_ev = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
	}

	return UDT::epoll_update_usock(eid, u, &srt_ev);
}

int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

#ifdef LINUX
    if (events) {
        flag = *events;
	} else {
        flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
    }
#elif defined(OSX) || defined(TARGET_OS_IOS) || defined(TARGET_OS_TV)
    if (events) {
        flag = *events;
	} else {
        flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
    }
#else
    flag = UDT_EPOLL_IN | UDT_EPOLL_OUT | UDT_EPOLL_ERR;
#endif

    // call UDT native function
    return UDT::epoll_update_ssock(eid, s, &flag);
}

int srt_epoll_wait(
		int eid,
		UDTSOCKET* readfds, int* rnum, UDTSOCKET* writefds, int* wnum,
		int64_t msTimeOut,
        SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum)
{
    return UDT::epoll_wait2(
    		eid,
    		readfds, rnum, writefds, wnum,
    		msTimeOut,
    		lrfds, lrnum, lwfds, lwnum);
}

int srt_epoll_release(int eid) { return UDT::epoll_release(eid); }

void srt_setloglevel(int ll)
{
    UDT::setloglevel(logging::LogLevel::type(ll));
}

void srt_addlogfa(int fa)
{
    UDT::addlogfa(logging::LogFA(fa));
}

void srt_dellogfa(int fa)
{
    UDT::dellogfa(logging::LogFA(fa));
}

void srt_resetlogfa(const int* fara, size_t fara_size)
{
    std::set<logging::LogFA> fas;
    std::copy(fara, fara + fara_size, std::inserter(fas, fas.begin()));
    UDT::resetlogfa(fas);
}

void srt_setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler)
{
    UDT::setloghandler(opaque, handler);
}

void srt_setlogflags(int flags)
{
    UDT::setlogflags(flags);
}



}
