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

#include "platform_sys.h"

#include <iterator>
#include <fstream>
#include "srt.h"
#include "common.h"
#include "packet.h"
#include "core.h"
#include "utilities.h"

using namespace std;
using namespace srt;


extern "C" {

SRTRUNSTATUS srt_startup() { return CUDT::startup(); }
SRTSTATUS srt_cleanup() { return CUDT::cleanup(); }

// Socket creation.
SRTSOCKET srt_socket(int , int , int ) { return CUDT::socket(); }
SRTSOCKET srt_create_socket() { return CUDT::socket(); }

#if ENABLE_BONDING
// Group management.
SRTSOCKET srt_create_group(SRT_GROUP_TYPE gt) { return CUDT::createGroup(gt); }
SRTSOCKET srt_groupof(SRTSOCKET socket) { return CUDT::getGroupOfSocket(socket); }
SRTSTATUS srt_group_data(SRTSOCKET socketgroup, SRT_SOCKGROUPDATA* output, size_t* inoutlen)
{
    return CUDT::getGroupData(socketgroup, output, inoutlen);
}

SRT_SOCKOPT_CONFIG* srt_create_config()
{
    return new SRT_SocketOptionObject;
}

SRTSTATUS srt_config_add(SRT_SOCKOPT_CONFIG* config, SRT_SOCKOPT option, const void* contents, int len)
{
    if (!config)
        return SRT_ERROR;

    if (!config->add(option, contents, len))
        return SRT_ERROR;

    return SRT_STATUS_OK;
}

SRTSOCKET srt_connect_group(SRTSOCKET group,
    SRT_SOCKGROUPCONFIG name[], int arraysize)
{
    return CUDT::connectLinks(group, name, arraysize);
}

#else

SRTSOCKET srt_create_group(SRT_GROUP_TYPE) { return SRT_INVALID_SOCK; }
SRTSOCKET srt_groupof(SRTSOCKET) { return SRT_INVALID_SOCK; }
SRTSTATUS srt_group_data(SRTSOCKET, SRT_SOCKGROUPDATA*, size_t*) { return srt::CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0); }
SRT_SOCKOPT_CONFIG* srt_create_config() { return NULL; }
SRTSTATUS srt_config_add(SRT_SOCKOPT_CONFIG*, SRT_SOCKOPT, const void*, int) { return srt::CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0); }

SRTSOCKET srt_connect_group(SRTSOCKET, SRT_SOCKGROUPCONFIG[], int) { return srt::CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0), SRT_INVALID_SOCK; }

#endif

SRT_SOCKGROUPCONFIG srt_prepare_endpoint(const struct sockaddr* src, const struct sockaddr* dst, int namelen)
{
    SRT_SOCKGROUPCONFIG data;
#if ENABLE_BONDING
    data.errorcode = SRT_SUCCESS;
#else
    data.errorcode = SRT_EINVOP;
#endif
    data.id = SRT_INVALID_SOCK;
    data.token = -1;
    data.weight = 0;
    data.config = NULL;
    if (src)
        memcpy(&data.srcaddr, src, namelen);
    else
    {
        memset(&data.srcaddr, 0, sizeof data.srcaddr);
        // Still set the family according to the target address
        data.srcaddr.ss_family = dst->sa_family;
    }
    memcpy(&data.peeraddr, dst, namelen);
    return data;
}

void srt_delete_config(SRT_SOCKOPT_CONFIG* in)
{
    delete in;
}

// Binding and connection management
SRTSTATUS srt_bind(SRTSOCKET u, const struct sockaddr * name, int namelen) { return CUDT::bind(u, name, namelen); }
SRTSTATUS srt_bind_acquire(SRTSOCKET u, UDPSOCKET udpsock) { return CUDT::bind(u, udpsock); }
SRTSTATUS srt_listen(SRTSOCKET u, int backlog) { return CUDT::listen(u, backlog); }
SRTSOCKET srt_accept(SRTSOCKET u, struct sockaddr * addr, int * addrlen) { return CUDT::accept(u, addr, addrlen); }
SRTSOCKET srt_accept_bond(const SRTSOCKET lsns[], int lsize, int64_t msTimeOut) { return CUDT::accept_bond(lsns, lsize, msTimeOut); }
SRTSOCKET srt_connect(SRTSOCKET u, const struct sockaddr * name, int namelen) { return CUDT::connect(u, name, namelen, SRT_SEQNO_NONE); }
SRTSOCKET srt_connect_debug(SRTSOCKET u, const struct sockaddr * name, int namelen, int forced_isn) { return CUDT::connect(u, name, namelen, forced_isn); }
SRTSOCKET srt_connect_bind(SRTSOCKET u,
        const struct sockaddr* source,
        const struct sockaddr* target, int target_len)
{
    return CUDT::connect(u, source, target, target_len);
}

SRTSTATUS srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen)
{
#if ENABLE_BONDING
    if (CUDT::isgroup(u))
        return CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0);
#endif

    bool yes = 1;
    CUDT::setsockopt(u, 0, SRTO_RENDEZVOUS, &yes, sizeof yes);

    // Note: PORT is 16-bit and at the same location in both sockaddr_in and sockaddr_in6.
    // Just as a safety precaution, check the structs.
    if ( (local_name->sa_family != AF_INET && local_name->sa_family != AF_INET6)
            || local_name->sa_family != remote_name->sa_family)
        return CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0);

    const SRTSTATUS st = srt_bind(u, local_name, local_namelen);
    if (st != SRT_STATUS_OK)
        return st;

    // Note: srt_connect may potentially return a socket value if it is used
    // to connect a group. But rendezvous is not supported for groups.
    const SRTSOCKET sst = srt_connect(u, remote_name, remote_namelen);
    if (sst == SRT_INVALID_SOCK)
        return SRT_ERROR;

    return SRT_STATUS_OK;
}

SRTSTATUS srt_close(SRTSOCKET u)
{
    SRT_SOCKSTATUS st = srt_getsockstate(u);

    if ((st == SRTS_NONEXIST) ||
        (st == SRTS_CLOSED)   ||
        (st == SRTS_CLOSING) )
    {
        // It's closed already. Do nothing.
        return SRT_STATUS_OK;
    }

    return CUDT::close(u);
}

SRTSTATUS srt_getpeername(SRTSOCKET u, struct sockaddr * name, int * namelen) { return CUDT::getpeername(u, name, namelen); }
SRTSTATUS srt_getsockname(SRTSOCKET u, struct sockaddr * name, int * namelen) { return CUDT::getsockname(u, name, namelen); }
SRTSTATUS srt_getsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, void * optval, int * optlen)
{ return CUDT::getsockopt(u, level, optname, optval, optlen); }
SRTSTATUS srt_setsockopt(SRTSOCKET u, int level, SRT_SOCKOPT optname, const void * optval, int optlen)
{ return CUDT::setsockopt(u, level, optname, optval, optlen); }
SRTSTATUS srt_getsockflag(SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen)
{ return CUDT::getsockopt(u, 0, opt, optval, optlen); }
SRTSTATUS srt_setsockflag(SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen)
{ return CUDT::setsockopt(u, 0, opt, optval, optlen); }

int srt_send(SRTSOCKET u, const char * buf, int len) { return CUDT::send(u, buf, len, 0); }
int srt_recv(SRTSOCKET u, char * buf, int len) { return CUDT::recv(u, buf, len, 0); }
int srt_sendmsg(SRTSOCKET u, const char * buf, int len, int ttl, int inorder) { return CUDT::sendmsg(u, buf, len, ttl, 0!=  inorder); }
int srt_recvmsg(SRTSOCKET u, char * buf, int len) { int64_t ign_srctime; return CUDT::recvmsg(u, buf, len, ign_srctime); }
int64_t srt_sendfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
    if (!path || !offset )
    {
        return CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0).as<int>();
    }
    fstream ifs(path, ios::binary | ios::in);
    if (!ifs)
    {
        return CUDT::APIError(MJ_FILESYSTEM, MN_READFAIL, 0).as<int>();
    }
    int64_t ret = CUDT::sendfile(u, ifs, *offset, size, block);
    ifs.close();
    return ret;
}

int64_t srt_recvfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block)
{
    if (!path || !offset )
    {
        return CUDT::APIError(MJ_NOTSUP, MN_INVAL, 0).as<int>();
    }
    fstream ofs(path, ios::binary | ios::out);
    if (!ofs)
    {
        return CUDT::APIError(MJ_FILESYSTEM, MN_WRAVAIL, 0).as<int>();
    }
    int64_t ret = CUDT::recvfile(u, ofs, *offset, size, block);
    ofs.close();
    return ret;
}

extern const SRT_MSGCTRL srt_msgctrl_default = {
    0,     // no flags set
    SRT_MSGTTL_INF,
    false, // not in order (matters for msg mode only)
    PB_SUBSEQUENT,
    0,     // srctime: take "now" time
    SRT_SEQNO_NONE,
    SRT_MSGNO_NONE,
    NULL,  // grpdata not supplied
    0      // idem
};

void srt_msgctrl_init(SRT_MSGCTRL* mctrl)
{
    *mctrl = srt_msgctrl_default;
}

int srt_sendmsg2(SRTSOCKET u, const char * buf, int len, SRT_MSGCTRL *mctrl)
{
    // Allow NULL mctrl in the API, but not internally.
    if (mctrl)
        return CUDT::sendmsg2(u, buf, len, (*mctrl));
    SRT_MSGCTRL mignore = srt_msgctrl_default;
    return CUDT::sendmsg2(u, buf, len, (mignore));
}

int srt_recvmsg2(SRTSOCKET u, char * buf, int len, SRT_MSGCTRL *mctrl)
{
    if (mctrl)
        return CUDT::recvmsg2(u, buf, len, (*mctrl));
    SRT_MSGCTRL mignore = srt_msgctrl_default;
    return CUDT::recvmsg2(u, buf, len, (mignore));
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
    static srt::CUDTException e;
    e = srt::CUDTException(CodeMajor(code/1000), CodeMinor(code%1000), err);
    return(e.getErrorMessage());
}


void srt_clearlasterror()
{
    UDT::getlasterror().clear();
}

SRTSTATUS srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear) { return CUDT::bstats(u, perf, 0!=  clear); }
SRTSTATUS srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous) { return CUDT::bstats(u, perf, 0!=  clear, 0!= instantaneous); }

SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u) { return SRT_SOCKSTATUS((int)CUDT::getsockstate(u)); }

// event mechanism
int srt_epoll_create() { return CUDT::epoll_create(); }

SRTSTATUS srt_epoll_clear_usocks(int eit) { return CUDT::epoll_clear_usocks(eit); }

// You can use either SRT_EPOLL_* flags or EPOLL* flags from <sys/epoll.h>, both are the same. IN/OUT/ERR only.
// events == NULL accepted, in which case all flags are set.
SRTSTATUS srt_epoll_add_usock(int eid, SRTSOCKET u, const int * events) { return CUDT::epoll_add_usock(eid, u, events); }

SRTSTATUS srt_epoll_add_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

    if (events) {
        flag = *events;
    } else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }

    // call UDT native function
    return CUDT::epoll_add_ssock(eid, s, &flag);
}

SRTSTATUS srt_epoll_remove_usock(int eid, SRTSOCKET u) { return CUDT::epoll_remove_usock(eid, u); }
SRTSTATUS srt_epoll_remove_ssock(int eid, SYSSOCKET s) { return CUDT::epoll_remove_ssock(eid, s); }

SRTSTATUS srt_epoll_update_usock(int eid, SRTSOCKET u, const int * events)
{
    return CUDT::epoll_update_usock(eid, u, events);
}

SRTSTATUS srt_epoll_update_ssock(int eid, SYSSOCKET s, const int * events)
{
    int flag = 0;

    if (events) {
        flag = *events;
    } else {
        flag = SRT_EPOLL_IN | SRT_EPOLL_OUT | SRT_EPOLL_ERR;
    }

    // call UDT native function
    return CUDT::epoll_update_ssock(eid, s, &flag);
}

int srt_epoll_wait(
      int eid,
      SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum,
      int64_t msTimeOut,
      SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum)
  {
    return UDT::epoll_wait2(
        eid,
        readfds, rnum, writefds, wnum,
        msTimeOut,
        lrfds, lrnum, lwfds, lwnum);
}

int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut)
{
    return UDT::epoll_uwait(
        eid,
        fdsSet,
        fdsSize,
        msTimeOut);
}

// use this function to set flags. Default flags are always "everything unset".
// Pass 0 here to clear everything, or nonzero to set a desired flag.
// Pass -1 to not change anything (but still get the current flag value).
int32_t srt_epoll_set(int eid, int32_t flags) { return CUDT::epoll_set(eid, flags); }

SRTSTATUS srt_epoll_release(int eid) { return CUDT::epoll_release(eid); }

void srt_setloglevel(int ll)
{
    UDT::setloglevel(srt_logging::LogLevel::type(ll));
}

void srt_addlogfa(int fa)
{
    UDT::addlogfa(srt_logging::LogFA(fa));
}

void srt_dellogfa(int fa)
{
    UDT::dellogfa(srt_logging::LogFA(fa));
}

void srt_resetlogfa(const int* fara, size_t fara_size)
{
    UDT::resetlogfa(fara, fara_size);
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

int srt_getrejectreason(SRTSOCKET sock)
{
    return CUDT::rejectReason(sock);
}

SRTSTATUS srt_setrejectreason(SRTSOCKET sock, int value)
{
    return CUDT::rejectReason(sock, value);
}

SRTSTATUS srt_listen_callback(SRTSOCKET lsn, srt_listen_callback_fn* hook, void* opaq)
{
    return CUDT::installAcceptHook(lsn, hook, opaq);
}

SRTSTATUS srt_connect_callback(SRTSOCKET lsn, srt_connect_callback_fn* hook, void* opaq)
{
    return CUDT::installConnectHook(lsn, hook, opaq);
}

uint32_t srt_getversion()
{
    return SrtVersion(SRT_VERSION_MAJOR, SRT_VERSION_MINOR, SRT_VERSION_PATCH);
}

int64_t srt_time_now()
{
    return srt::sync::count_microseconds(srt::sync::steady_clock::now().time_since_epoch());
}

int64_t srt_connection_time(SRTSOCKET sock)
{
    return CUDT::socketStartTime(sock);
}

int srt_clock_type()
{
    return SRT_SYNC_CLOCK;
}

// NOTE: crypto mode is defined regardless of the setting of
// ENABLE_AEAD_API_PREVIEW symbol. This can only block the symbol,
// but it doesn't change the symbol layout.
const char* const srt_rejection_reason_msg [] = {
    "Unknown or erroneous",
    "Error in system calls",
    "Peer rejected connection",
    "Resource allocation failure",
    "Rogue peer or incorrect parameters",
    "Listener's backlog exceeded",
    "Internal Program Error",
    "Socket is being closed",
    "Peer version too old",
    "Rendezvous-mode cookie collision",
    "Incorrect passphrase",
    "Password required or unexpected",
    "MessageAPI/StreamAPI collision",
    "Congestion controller type collision",
    "Packet Filter settings error",
    "Group settings collision",
    "Connection timeout",
    "Crypto mode",
    "Invalid configuration"
};

const char* srt_rejectreason_str(int id)
{
    if (id >= SRT_REJC_PREDEFINED)
    {
        return "Application-defined rejection reason";
    }

    static const size_t ra_size = Size(srt_rejection_reason_msg);
    if (size_t(id) >= ra_size)
        return srt_rejection_reason_msg[0];
    return srt_rejection_reason_msg[id];
}

}
