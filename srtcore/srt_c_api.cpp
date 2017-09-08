/*****************************************************************************
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
 * 
 *****************************************************************************/

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
#include "core.h"

extern "C" {

int srt_startup() { return CUDT::startup(); }
int srt_cleanup() { return CUDT::cleanup(); }
UDTSOCKET srt_socket(int af, int type, int protocol) { return CUDT::socket(af, type, protocol); }
int srt_bind(UDTSOCKET u, const struct sockaddr * name, int namelen) { return CUDT::bind(u, name, namelen); }
int srt_bind_peerof(UDTSOCKET u, UDPSOCKET udpsock) { return CUDT::bind(u, udpsock); }
int srt_listen(UDTSOCKET u, int backlog) { return CUDT::listen(u, backlog); }
UDTSOCKET srt_accept(UDTSOCKET u, struct sockaddr * addr, int * addrlen) { return CUDT::accept(u, addr, addrlen); }
int srt_connect(UDTSOCKET u, const struct sockaddr * name, int namelen) { return CUDT::connect(u, name, namelen, 0); }
int srt_connect_debug(UDTSOCKET u, const struct sockaddr * name, int namelen, int32_t forced_isn) { return CUDT::connect(u, name, namelen, forced_isn); }

int srt_rendezvous(UDTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen)
{
    bool yes = 1;
    CUDT::setsockopt(u, 0, UDT_RENDEZVOUS, &yes, sizeof yes);

    // Note: PORT is 16-bit and at the same location in both sockaddr_in and sockaddr_in6.
    // Just as a safety precaution, check the structs.
    if ( (local_name->sa_family != AF_INET && local_name->sa_family != AF_INET6)
            || local_name->sa_family != remote_name->sa_family)
        return SRT_EINVPARAM;

    sockaddr_in* local_sin = (sockaddr_in*)local_name;
    sockaddr_in* remote_sin = (sockaddr_in*)remote_name;

    if (local_sin->sin_port != remote_sin->sin_port)
        return SRT_EINVPARAM;

    int st = srt_bind(u, local_name, local_namelen);
    if ( st != 0 )
        return st;

    return srt_connect(u, remote_name, remote_namelen);
}

int srt_rendezvous(UDTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen)
{
    bool yes = 1;
    UDT::setsockopt(u, 0, UDT_RENDEZVOUS, &yes, sizeof yes);

    // Note: PORT is 16-bit and at the same location in both sockaddr_in and sockaddr_in6.
    // Just as a safety precaution, check the structs.
    if ( (local_name->sa_family != AF_INET && local_name->sa_family != AF_INET6)
            || local_name->sa_family != remote_name->sa_family)
        return SRT_EINVPARAM;

    sockaddr_in* local_sin = (sockaddr_in*)local_name;
    sockaddr_in* remote_sin = (sockaddr_in*)remote_name;

    if (local_sin->sin_port != remote_sin->sin_port)
        return SRT_EINVPARAM;

    int st = srt_bind(u, local_name, local_namelen);
    if ( st != 0 )
        return st;

    return srt_connect(u, remote_name, remote_namelen);
}

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

    return CUDT::close(u);
}

int srt_getpeername(UDTSOCKET u, struct sockaddr * name, int * namelen) { return CUDT::getpeername(u, name, namelen); }
int srt_getsockname(UDTSOCKET u, struct sockaddr * name, int * namelen) { return CUDT::getsockname(u, name, namelen); }
int srt_getsockopt(UDTSOCKET u, int level, SRT_SOCKOPT optname, void * optval, int * optlen)
{ return CUDT::getsockopt(u, level, optname, optval, optlen); }
int srt_setsockopt(UDTSOCKET u, int level, SRT_SOCKOPT optname, const void * optval, int optlen)
{ return CUDT::setsockopt(u, level, optname, optval, optlen); }

int srt_getsockflag(UDTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen)
{ return CUDT::getsockopt(u, 0, opt, optval, optlen); }
int srt_setsockflag(UDTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen)
{ return CUDT::setsockopt(u, 0, opt, optval, optlen); }

int srt_send(UDTSOCKET u, const char * buf, int len, int flags) { return CUDT::send(u, buf, len, flags); }
int srt_recv(UDTSOCKET u, char * buf, int len, int flags) { return CUDT::recv(u, buf, len, flags); }
int srt_sendmsg(UDTSOCKET u, const char * buf, int len, int ttl, int inorder) { return CUDT::sendmsg(u, buf, len, ttl, 0!=  inorder); }
int srt_recvmsg(UDTSOCKET u, char * buf, int len) { return CUDT::recvmsg(u, buf, len); }

int srt_sendmsg2(UDTSOCKET u, const char * buf, int len, SRT_MSGCTRL *mctrl)
{
    if (mctrl)
        return CUDT::sendmsg(u, buf, len, -1, true, mctrl->srctime);
    else
        return CUDT::sendmsg(u, buf, len);
}

int srt_recvmsg2(UDTSOCKET u, char * buf, int len, SRT_MSGCTRL *mctrl)
{
    uint64_t srctime = 0;
    int rc = CUDT::recvmsg(u, buf, len, srctime);
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
    return CUDT::getlasterror().getErrorCode();
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

int srt_perfmon(UDTSOCKET u, SRT_TRACEINFO * perf, int clear) { return CUDT::perfmon(u, perf, 0!=  clear); }
int srt_bstats(UDTSOCKET u, SRT_TRACEBSTATS * perf, int clear) { return CUDT::bstats(u, perf, 0!=  clear); }

SRT_SOCKSTATUS srt_getsockstate(UDTSOCKET u) { return SRT_SOCKSTATUS((int)CUDT::getsockstate(u)); }

// event mechanism
int srt_epoll_create() { return CUDT::epoll_create(); }

// You can use either SRT_EPOLL_* flags or EPOLL* flags from <sys/epoll.h>, both are the same. IN/OUT/ERR only.
// events == NULL accepted, in which case all flags are set.
int srt_epoll_add_usock(int eid, UDTSOCKET u, const int * events) { return CUDT::epoll_add_usock(eid, u, events); }

int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

#ifdef LINUX
    if (events) {
        flag = *events;
	} else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }
#elif defined(OSX) || defined(TARGET_OS_IOS) || defined(TARGET_OS_TV)
    if (events) {
        flag = *events;
	} else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }
#else
    flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
#endif

    // call UDT native function
    return CUDT::epoll_add_ssock(eid, s, &flag);
}

int srt_epoll_remove_usock(int eid, UDTSOCKET u) { return CUDT::epoll_remove_usock(eid, u); }
int srt_epoll_remove_ssock(int eid, SYSSOCKET s) { return CUDT::epoll_remove_ssock(eid, s); }

int srt_epoll_update_usock(int eid, UDTSOCKET u, const int * events)
{
	int srt_ev = 0;

	if (events) {
        srt_ev = *events;
	} else {
		srt_ev = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
	}

	return CUDT::epoll_update_usock(eid, u, &srt_ev);
}

int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

#ifdef LINUX
    if (events) {
        flag = *events;
	} else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }
#elif defined(OSX) || defined(TARGET_OS_IOS) || defined(TARGET_OS_TV)
    if (events) {
        flag = *events;
	} else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }
#else
    flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
#endif

    // call UDT native function
    return CUDT::epoll_update_ssock(eid, s, &flag);
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

int srt_epoll_release(int eid) { return CUDT::epoll_release(eid); }

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

int srt_getsndbuffer(SRTSOCKET sock, size_t* blocks, size_t* bytes)
{
    return CUDT::getsndbuffer(sock, blocks, bytes);
}


}
