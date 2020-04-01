SRT API Functions
=================

- [**Library Initialization**](#Library-Initialization)
  * [srt_startup](#srt_startup)
  * [srt_cleanup](#srt_cleanup)
- [**Creating and configuring sockets**](#Creating-and-configuring-sockets)
  * [srt_socket](#srt_socket)
  * [srt_create_socket](#srt_create_socket)
  * [srt_bind](#srt_bind)
  * [srt_create_socket](#srt_create_socket)
  * [srt_bind_acquire](#srt_bind_acquire)
  * [srt_getsockstate](#srt_getsockstate)
  * [srt_getsndbuffer](#srt_getsndbuffer)
  * [srt_close](#srt_close)
- [**Connecting**](#Connecting)
  * [srt_listen](#srt_listen)
  * [srt_accept](#srt_accept)
  * [srt_listen_callback](#srt_listen_callback)
  * [srt_connect](#srt_connect)
  * [srt_connect_bind](#srt_connect_bind)
  * [srt_connect_debug](#srt_connect_debug)
  * [srt_rendezvous](#srt_rendezvous)
- [**Socket group management**](#Socket-group-management)
  * [SRT_GROUP_TYPE](#SRT_GROUP_TYPE)
  * [SRT_SOCKGROUPDATA](#SRT_SOCKGROUPDATA)
  * [srt_create_group](#srt_create_group)
  * [srt_include](#srt_include)
  * [srt_exclude](#srt_exclude)
  * [srt_groupof](#srt_groupof)
  * [srt_group_data](#srt_group_data)
  * [srt_connect_group](#srt_connect_group)
  * [srt_prepare_endpoint](#srt_prepare_endpoint)
- [**Options and properties**](#Options-and-properties)
  * [srt_getpeername](#srt_getpeername)
  * [srt_getsockname](#srt_getsockname)
  * [srt_getsockopt, srt_getsockflag](#srt_getsockopt-srt_getsockflag)
  * [srt_setsockopt, srt_setsockflag](#srt_setsockopt-srt_setsockflag)
  * [srt_getversion](#srt_getversion)
- [**Helper data types for transmission**](#Helper-data-types-for-transmission)
  * [SRT_MSGCTRL](#SRT_MSGCTRL)
- [**Transmission**](#Transmission)
  * [srt_send, srt_sendmsg, srt_sendmsg2](#srt_send-srt_sendmsg-srt_sendmsg2)
  * [srt_recv, srt_recvmsg, srt_recvmsg2](#srt_recv-srt_recvmsg-srt_recvmsg2)
  * [srt_sendfile, srt_recvfile](#srt_sendfile-srt_recvfile)
- [**Diagnostics**](#Diagnostics)
  * [srt_getlasterror_str](#srt_getlasterror_str)
  * [srt_getlasterror](#srt_getlasterror)
  * [srt_strerror](#srt_strerror)
  * [srt_clearlasterror](#srt_clearlasterror)
- [**Performance tracking**](#Performance-tracking)
  * [srt_bstats, srt_bistats](#srt_bstats-srt_bistats)
- [**Asynchronous operations (epoll)**](#Asynchronous-operations-epoll)
  * [srt_epoll_create](#srt_epoll_create)
  * [srt_epoll_add_usock, srt_epoll_add_ssock, srt_epoll_update_usock, srt_epoll_update_ssock](#srt_epoll_add_usock-srt_epoll_add_ssock-srt_epoll_update_usock-srt_epoll_update_ssock)
  * [srt_epoll_remove_usock, srt_epoll_remove_ssock](#srt_epoll_remove_usock-srt_epoll_remove_ssock)
  * [srt_epoll_wait](#srt_epoll_wait)
  * [srt_epoll_uwait](#srt_epoll_uwait)
  * [srt_epoll_set](#srt_epoll_set)
  * [srt_epoll_release](#srt_epoll_release)
- [**Logging control**](#Logging-control)
  * [srt_setloglevel](#srt_setloglevel)
  * [srt_addlogfa, srt_dellogfa, srt_resetlogfa](#srt_addlogfa-srt_dellogfa-srt_resetlogfa)
  * [srt_setloghandler](#srt_setloghandler)
  * [srt_setlogflags](#srt_setlogflags)


Library initialization
----------------------

### srt_startup
```
int srt_startup(void);
```

This function shall be called at the start of an application that uses the SRT
library. It provides all necessary platform-specific initializations, sets up
global data, and starts the SRT GC thread. If this function isn't explicitly 
called, it will be called automatically when creating the first socket. However, 
relying on this behavior is strongly discouraged.

- Returns:

  *  0 = successfully run, or already started
  *  1 = this is the first startup, but the GC thread is already running
  * -1 = failed

- Errors:

  * `SRT_ECONNSETUP` (with error code set): Reported when required system
resource(s) failed to initialize. This is currently used only on Windows to 
report a failure from `WSAStartup`.

### srt_cleanup
```
int srt_cleanup(void);
```

This function cleans up all global SRT resources and shall be called just before 
exiting the application that uses the SRT library. This cleanup function will still
be called from the C++ global destructor, if not called by the application, although
relying on this behavior is stronly discouraged.

- Returns:

  * 0 (A possibility to return other values is reserved for future use)

**IMPORTANT**: Note that the startup/cleanup calls have an instance counter.
This means that if you call `srt_startup` multiple times, you need to call the 
`srt_cleanup` function exactly the same number of times.

Creating and configuring sockets
--------------------------------

### srt_socket
```
SRTSOCKET srt_socket(int af, int type, int protocol);
```

Old and deprecated version of `srt_create_socket`. All arguments are ignored.

**NOTE** changes with respect to UDT version:

* In UDT (and SRT versions before 1.5.0) the `af` parameter was specifying the
socket family (`AF_INET` or `AF_INET6`). This is now not required; this parameter
is decided at the call of `srt_conenct` or `srt_bind`.

* In UDT the `type` parameter was used to specify the file or message mode
using `SOCK_STREAM` or `SOCK_DGRAM` symbols (with the latter being misleading,
as the message mode has nothing to do with UDP datagrams and it's rather
similar to the SCTP protocol). In SRT these two modes are available by setting
`SRTO_TRANSTYPE`. The default is `SRTT_LIVE`. If, however, you set
`SRTO_TRANSTYPE` to `SRTT_FILE` for file mode, you can then leave the
`SRTO_MESSAGEAPI` option as false (default), which corresponds to "stream" mode
(TCP-like), or set it to true, which corresponds to "message" mode (SCTP-like).


### srt_create_socket
```
SRTSOCKET srt_create_socket();
```

Creates an SRT socket.

Note that socket IDs always have the `SRTGROUP_MASK` bit clear.

- Returns:

  * a valid socket ID on success
  * `INVALID_SOCKET` (-1) on error

- Errors:

  * `SRT_ENOTBUF`: not enough memory to allocate required resources

**NOTE:** This is probably a design flaw (**BUG?**). Usually underlying system 
errors are reported by `SRT_ECONNSETUP`.


### srt_bind
```
int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
```

Binds a socket to a local address and port. Binding specifies the local network
interface and the UDP port number to be used for the socket. When the local 
address is a form of `INADDR_ANY`, then it's bound to all interfaces. When the 
port number is 0, then the port number will be system-allocated if necessary.

This call is obligatory for a listening socket before calling `srt_listen`
and for rendezvous mode before calling `srt_connect`, otherwise it's optional.
For a listening socket it defines the network interface and the port where the
listener should expect a call request. In case of rendezvous mode (when the
socket has set `SRTO_RENDEZVOUS` to true, in this mode both parties connect
to one another) it defines the network interface and port from which packets
will be sent to the peer and to which the peer is expected to send packets.

For a connecting socket this call can set up the outgoing port to be used in
the communication. It is allowed that multiple SRT sockets share one local
outgoing port, as long as `SRTO_REUSEADDR` is set to *true* (default). Without
this call the port will be automatically selected by the system.

NOTE: This function cannot be called on socket group. If you need to
have the group-member socket bound to the specified source address before
connecting, use `srt_connect_bind` for that purpose.

- Returns:

  * `SRT_ERROR` (-1) on error, otherwise 0

- Errors:

  * `SRT_EINVSOCK`: Socket passed as `u` designates no valid socket
  * `SRT_EINVOP`: Socket already bound
  * `SRT_ECONNSETUP`: Internal creation of a UDP socket failed 
  * `SRT_ESOCKFAIL`: Internal configuration of a UDP socket (`bind`, `setsockopt`) failed

### srt_bind_acquire

```
int srt_bind_acquire(SRTSOCKET u, UDPSOCKET udpsock);
```

A version of `srt_bind` that acquires a given UDP socket instead of creating one.

### srt_getsockstate

```
SRT_SOCKSTATUS srt_getsockstate(SRTSOCKET u);
```

Gets the current status of the socket. Possible states are:

* `SRTS_INIT`: Created, but not bound
* `SRTS_OPENED`: Created and bound, but not in use yet.
* `SRTS_LISTENING`: Socket is in listening state
* `SRTS_CONNECTING`: The connect operation was initiated, but not yet 
finished. This may also mean that it has timed out; you can only know
that after getting a socket error report from `srt_epoll_wait`. In blocking
mode it's not possible because `srt_connect` does not return until the
socket is connected or failed due to timeout or interrupted call.
* `SRTS_CONNECTED`: The socket is connected and ready for transmission.
* `SRTS_BROKEN`: The socket was connected, but the connection was broken
* `SRTS_CLOSING`: The socket may still be open and active, but closing
is requested, so no further operations will be accepted (active operations will 
be completed before closing)
* `SRTS_CLOSED`: The socket has been closed, but not yet removed by the GC
thread
* `SRTS_NONEXIST`: The specified number does not correspond to a valid socket.

### srt_getsndbuffer

```
int srt_getsndbuffer(SRTSOCKET sock, size_t* blocks, size_t* bytes);
```

Retrieves information about the sender buffer.

* `sock`: Socket to test
* `blocks`: Written information about buffer blocks in use
* `bytes`: Written information about bytes in use

This function can be used for diagnostics. It is especially useful when the 
socket needs to be closed asynchronously.

### srt_close

```
int srt_close(SRTSOCKET u);
```

Closes the socket or group and frees all used resources. Note that underlying
UDP sockets may be shared between sockets, so these are freed only with the
last user closed.

- Returns:

  * `SRT_ERROR` (-1) in case of error, otherwise 0

- Errors:

  * `SRT_EINVSOCK`: Socket `u` indicates no valid socket ID

Connecting
----------

### srt_listen
```
int srt_listen(SRTSOCKET u, int backlog);
```

This sets up the listening state on a socket with a backlog setting that 
defines how many sockets may be allowed to wait until they are accepted 
(excessive connection requests are rejected in advance).

The following important options may change the behavior of the listener
socket and the `srt_accept` function:

* `srt_listen_callback` installs a user function that will be called
before `srt_accept` can happen
* `SRTO_GROUPCONNECT` option allows the listener socket to accept group
connections

- Returns:

  * `SRT_ERROR` (-1) in case of error, otherwise 0.

- Errors:

  * `SRT_EINVPARAM`: Value of `backlog` is 0 or negative.
  * `SRT_EINVSOCK`: Socket `u` indicates no valid SRT socket.
  * `SRT_EUNBOUNDSOCK`: `srt_bind` has not yet been called on that socket.
  * `SRT_ERDVNOSERV`: `SRTO_RENDEZVOUS` flag is set to true on specified socket.
  * `SRT_EINVOP`: Internal error (should not happen when `SRT_EUNBOUNDSOCK` is reported).
  * `SRT_ECONNSOCK`: The socket is already connected.
  * `SRT_EDUPLISTEN`: The address used in `srt_bind` by this socket is already
occupied by another listening socket. Binding multiple sockets to one IP address 
and port is allowed, as long as `SRTO_REUSEADDR` is set to true, but only one of 
these sockets can be set up as a listener.

### srt_accept

```
SRTSOCKET srt_accept(SRTSOCKET lsn, struct sockaddr* addr, int* addrlen);
```

Accepts a pending connection, then creates and returns a new socket or
group ID that handles this connection. The group and socket can be
distinguished by checking the `SRTGROUP_MASK` bit on the returned ID.

* `lsn`: the listener socket previously configured by `srt_listen`
* `addr`: the IP address and port specification for the remote party
* `addrlen`: INPUT: size of `addr` pointed object. OUTPUT: real size of the
returned object

**NOTE:** `addr` is allowed to be NULL, in which case it's understood that the
application is not interested in the address from which the connection originated.
Otherwise `addr` should specify an object into which the address will be written, 
and `addrlen` must also specify a variable to contain the object size. Note also
that in the case of group connection only the initial connection that
establishes the group connection is returned, together with its address. As
member connections are added or broken within the group, you can obtain this
information through `srt_group_data` or the data filled by `srt_sendmsg2` and
`srt_recvmsg2`.

If the pending connection is a group connection (initiated on the peer side
by calling the connection function using a group ID, and permitted on the
listener socket by `SRTO_GROUPCONNECT` flag), then the value returned is a
group ID. This function then creates a new group, as well as a new socket for
this very connection, that will be added to the group. Once the group is
created this way, further connections within the same group, as well as sockets
for them, will be created in the background. The `SRT_EPOLL_UPDATE` event is
raised on the `lsn` socket when a new background connection is attached to the
group, although it's usually for internal use only.

- Returns:

  * On success, a valid SRT socket or group ID to be used for transmission
  * `SRT_ERROR` (-1) on failure 

- Errors:

  * `SRT_EINVPARAM`: NULL specified as `addrlen`, when `addr` is not NULL
  * `SRT_EINVSOCK`: `lsn` designates no valid socket ID. Can also mean Internal
Error when an error occurred while creating an accepted socket (**BUG?**)
  * `SRT_ENOLISTEN`: `lsn` is not set up as a listener (`srt_listen` not called,
or the listener socket has already been closed)
  * `SRT_ERDVNOSERV`: Internal error (if no `SRT_ENOLISTEN` reported, it means
that the socket could not be set up as rendezvous because `srt_listen` does
not allow it)
  * `SRT_EASYNCRCV`: No connection reported so far. This error is reported only
when the `lsn` listener socket was configured as non-blocking for reading
(`SRTO_RCVSYN` set to false); otherwise the call blocks until a connection
is reported or an error occurs

### srt_accept_bond

```
SRTSOCKET srt_accept_bond(const SRTSOCKET listeners[], int nlisteners, int msTimeOut);
```

Accepts a pending connection, like `srt_accept`, but pending on any of the
listener sockets passed in the `listeners` array of `nlisteners` size.

* `listeners`: array of listener sockets (all must be setup by `srt_listen`)
* `nlisteners`: size of the `listeners` array
* `msTimeOut`: timeout in [ms] or -1 to block forever

This function is for blocking mode only - for non-blocking mode you should
simply call `srt_accept` on the first listener socket that reports readiness,
and this function is actually a friendly shortcut that uses waiting on epoll
and `srt_accept` internally. This function supports an important use case for
accepting a group connection, for which every member connection is expected to
be established over a different listener socket.

Note that there's no special set of settings required or rejected for this
function. The group-member connections for the same group can be established
over various different listener sockets always when all those listeners are
hosted by the same application, as the group management is global for the
application, so a connection reporting in for an already connected group
gets discovered and the connection will be handled in the background,
regardless to which listener socket the call was done - as long as the
connection is accepted according to any additional conditions.

This function has still nothing to do with the groups - you can use it in
any case when you have one service that accepts connections to multiple
endpoints. Note also that the settings as to whether listeners should
accept or reject socket or group connections, should be applied to the
listener sockets appropriately prior to calling this function.

- Returns:

  * On success, a valid SRT socket or group ID to be used for transmission
  * `SRT_ERROR` (-1) on failure 

- Errors:

  * `SRT_EINVPARAM`: NULL specified as `listeners` or `nlisteners` < 1

  * `SRT_EINVSOCK`: any socket in `listeners` designates no valid socket ID.
Can also mean Internal Error when an error occurred while creating an
accepted socket (**BUG?**)

  * `SRT_ENOLISTEN`: any socket in `listeners` is not set up as a listener
(`srt_listen` not called, or the listener socket has already been closed)

  * `SRT_EASYNCRCV`: No connection reported on any listener socket as the
timeout has been reached. This error is only reported when msTimeOut is
not -1


### srt_listen_callback

```
int srt_listen_callback(SRTSOCKET lsn, srt_listen_callback_fn* hook_fn, void* hook_opaque);
```

This call installs a callback hook, which will be executed on a socket that is
automatically created to handle the incoming connection on the listeneing
socket (and is about to be returned by `srt_accept`), but before the connection
has been accepted.

* `lsn`: Listening socket where you want to install the callback hook
* `hook_fn`: The callback hook function pointer
* `hook_opaque`: The pointer value that will be passed to the callback function

- Returns:

   * 0, if successful
   * -1, on error

- Errors:

   * `SRT_EINVPARAM` reported when `hook_fn` is a null pointer

The callback function has the signature as per this type definition:
```
typedef int srt_listen_callback_fn(void* opaque, SRTSOCKET ns, int hs_version
             const struct sockaddr* peeraddr, const char* streamid);
```

The callback function gets the following parameters passed:

* `opaque`: The pointer passed as `hook_opaque` when registering
* `ns`: The freshly created socket to handle the incoming connection
* `hs_version`: The handshake version (usually 5, pre-1.3 versions use 4)
* `peeraddr`: The address of the incoming connection
* `streamid`: The value set to `SRTO_STREAMID` option set on the peer side

(Note that versions that use handshake version 4 are incapable of using
any extensions, such as streamid, however they do support encryption.
Note also that the SRT version isn't yet extracted, however you can
prevent too old version connections using `SRTO_MINVERSION` option).

The callback function is given an opportunity to:

* use the passed information (streamid and peer address) to decide
  what to do with this connection
* alter any options on the socket, which could not be set properly
  before on the listening socket to be derived by the accepted socket,
  and won't be allowed to be altered after the socket is returned by
  `srt_accept`

Note that the returned socket has already set all derived options from the
listener socket, as it happens normally, and the moment when this callback is
called is when the conclusion handshake has been already received from the
caller party, but not yet interpreted (the streamid field is extracted from it
prematurely). When you, for example, set a passphrase on the socket at this
very moment, the Key Material processing will happen against this already set
passphrase, after the callback function is finished.

The callback function shall return 0, if the connection is to be accepted.
If you return -1, **or** if the function throws an exception, this will be
understood as a request to reject the incoming connection - in which case the
about-to-be-accepted socket will be silently deleted and `srt_accept` will not
report it. Note that in case of non-blocking mode the epoll bits for read-ready
on the listener socket will not be set if the connection is rejected, including
when rejected from this user function.

**IMPORTANT**: This function is called in the receiver worker thread, which
means that it must do its checks and operations as quickly as possible and keep
the minimum possible time, as every delay you do in this function will burden
the processing of the incoming data on the associated UDP socket, which in case
of a listener socket means the listener socket itself and every socket accepted
off this listener socket. Avoid any extensive search operations, best cache in
memory whatever database you have to check against the data received in
streamid or peeraddr.


### srt_connect

```
int srt_connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
```

Connects a socket or a group to a remote party with a specified address and port.

* `u`: can be an SRT socket or SRT group, both freshly created and not yet
  used for any connection, except possibly `srt_bind` on the socket
* `name`: specification of the remote address and port
* `namelen`: size of the object passed by `name`

**NOTES:**
1. The socket used here may be bound from upside (or binding and connection can
be done in one function, `srt_connect_bind`) so that it uses a predefined
network interface or local outgoing port. If not, it behaves as if it was
bound to `INADDR_ANY` (which binds on all interfaces) and port 0 (which
makes the system assign the port automatically).
2. When `u` is a group, then this call can be done multiple times, each time
for another member connection, and a new member SRT socket will be created
automatically for every call of this function.
3. If you want to connect a group to multiple links at once and use blocking
mode, you might want to use `srt_connect_group` instead.

- Returns:

  * `SRT_ERROR` (-1) in case of error
  * 0 in case when used for `u` socket
  * Socket ID created for connection for `u` group

- Errors:

  * `SRT_EINVSOCK`: Socket `u` indicates no valid socket ID
  * `SRT_ERDVUNBOUND`: Socket `u` has set `SRTO_RENDEZVOUS` to true, but `srt_bind`
hasn't yet been called on it. The `srt_connect` function is also used to connect a
rendezvous socket, but rendezvous sockets must be explicitly bound to a local
interface prior to connecting. Non-rendezvous sockets (caller sockets) can be
left without binding - the call to `srt_connect` will bind them automatically.
  * `SRT_ECONNSOCK`: Socket `u` is already connected
  * `SRT_ECONNREJ`: Connection has been rejected

In case when `SRT_ECONNREJ` error was reported, you can get the reason for
a rejected connection from `srt_getrejectreason`.

### srt_connect_bind

```
int srt_connect_bind(SRTSOCKET u, const struct sockaddr* source,
                     const struct sockaddr* target, int len);
```

This function does the same as first `srt_bind` then `srt_connect`, if called
with `u` being a socket. If `u` is a group, then it will execute `srt_bind`
first on the automatically created socket for the connection.

* `u`: Socket or group to connect
* `source`: Address to bind `u` to
* `target`: Address to connect
* `len`: size of the original structure of `source` and `target`

- Returns:

  * `SRT_ERROR` (-1) in case of error
  * 0 in case when used for `u` socket
  * Socket ID created for connection for `u` group

- Errors:

  * `SRT_EINVSOCK`: Socket passed as `u` designates no valid socket
  * `SRT_EINVOP`: Socket already bound
  * `SRT_ECONNSETUP`: Internal creation of a UDP socket failed
  * `SRT_ESOCKFAIL`: Internal configuration of a UDP socket (`bind`, `setsockopt`) failed
  * `SRT_ERDVUNBOUND`: Internal error (`srt_connect` should not report it after `srt_bind` was called)
  * `SRT_ECONNSOCK`: Socket `u` is already connected
  * `SRT_ECONNREJ`: Connection has been rejected

IMPORTANT: It's not allowed to bind and connect the same socket to two
different families (that is, both `source` and `target` must be `AF_INET` or
`AF_INET6`), although you may mix links over IPv4 and IPv6 in one group.

### srt_connect_debug

```
int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);
```

This function is for developers only and can be used for testing. It does the 
same thing as [`srt_connect`](#srt_connect), with the exception that it allows 
specifying the Initial Sequence Number for data transmission. Normally this value 
is generated randomly.

### srt_rendezvous
```
int srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen);
```
Performs a rendezvous connection. This is a shortcut for doing bind locally,
setting the `SRTO_RENDEZVOUS` option to true, and doing `srt_connect`. 

* `u`: socket to connect
* `local_name`: specifies the local network interface and port to bind
* `remote_name`: specifies the remote party's IP address and port

- Returns:

  * `SRT_ERROR` (-1) in case of error, otherwise 0

- Errors:

  * `SRT_EINVSOCK`: Socket passed as `u` designates no valid socket
  * `SRT_EINVOP`: Socket already bound
  * `SRT_ECONNSETUP`: Internal creation of a UDP socket failed
  * `SRT_ESOCKFAIL`: Internal configuration of a UDP socket (`bind`, `setsockopt`) failed
  * `SRT_ERDVUNBOUND`: Internal error (`srt_connect` should not report it after `srt_bind` was called)
  * `SRT_ECONNSOCK`: Socket `u` is already connected
  * `SRT_ECONNREJ`: Connection has been rejected

IMPORTANT: It's not allowed to perform a rendezvous connection to two
different families (that is, both `local_name` and `remote_name` must be `AF_INET` or
`AF_INET6`).

Socket group management
-----------------------

### SRT_GROUP_TYPE

The following group types are collected in an `SRT_GROUP_TYPE` enum:

* `SRT_GTYPE_BROADCAST`: broadcast type, all links are actively used at once
* `SRT_GTYPE_BACKUP`: backup type, idle links take over connection on disturbance
* `SRT_GTYPE_BALANCING`: balancing type, share bandwidth usage between links

### SRT_SOCKGROUPDATA

The most important structure for the group members is `SRT_SOCKGROUPDATA`:

```
typedef struct SRT_SocketGroupData_
{
    SRTSOCKET id;
    SRT_SOCKSTATUS status;
    int result;
    struct sockaddr_storage srcaddr;
    struct sockaddr_storage peeraddr; // Don't want to expose sockaddr_any to public API
    int priority;
} SRT_SOCKGROUPDATA;
```

where:

* `id`: member socket ID
* `status`: current connection status (see `srt_getsockstate`)
* `result`: result of the operation (if this operation recently updated this structure)
* `srcaddr`: address to which `id` should be bound
* `peeraddr`: address to which `id` should be connected
* `priority`: priority for backup group

The priority is set to 0 by default by `srt_prepare_endpoint()` - you can set
it to a different value afterwards. The default 0 value is the highest priority
and greater values declare lower priorities. The priority for the backup
groups determines which link is activated first when the currently active link is
unstable, and which should keep transmitting when multiple active links are
currently stable. This is not used by any other group types.

Functions to be used on groups:

### srt_create_group

```
SRTSOCKET srt_create_group(SRT_GROUP_TYPE type);
```

Creates a new group of type `type`. This is typically called on the
caller side to be next used for connecting to the listener peer side.
The group ID is of the same domain as socket ID, with the exception that
the `SRTGROUP_MASK` bit is set on it, unlike for socket ID.

### srt_include

```
int srt_include(SRTSOCKET socket, SRTSOCKET group);
```

This function adds a socket to a group. This is only allowed for unmanaged
groups. No such group type is currently implemented.

### srt_exclude

```
int srt_exclude(SRTSOCKET socket);
```
This function removes a socket from a group to which it currently belongs.
This is only allowed for unmanaged groups. No such group type is currently
implemented.

### srt_groupof

```
SRTSOCKET srt_groupof(SRTSOCKET socket);
```

Returns the group ID of the socket, or `SRT_INVALID_SOCK` if the socket
doesn't exist or it's not a member of any group.

### srt_group_data 

```
int srt_group_data(SRTSOCKET socketgroup, SRT_SOCKGROUPDATA output[], size_t* inoutlen);
```

* `socketgroup` an existing socket group ID
* `output` points to an output array
* `inoutlen` points to a variable set to array's size

This function obtains the current member state of the group specified in
`socketgroup`. The `output` should point to an array large enough to hold
all the elements, and `inoutlen` to a variable preset to the size of this array.
The current number of members will be written back to `inoutlen`. If the size
is enough for the current number of members, the `output` array will be
filled with group data and the function will return 0. Otherwise the array
will not be filled and `SRT_ERROR` will be returned.

- Returns:

   * 0, if successful
   * -1, on error

- Errors:

   * `SRT_EINVPARAM` reported if `socketgroup` is not an existing group ID
   * `SRT_SUCCESS` if the array was too small



### srt_connect_group

```
int srt_connect_group(SRTSOCKET group,
                      SRT_SOCKGROUPDATA name [], int arraysize);
```

This function does almost the same as calling `srt_connect` or `srt_connect_bind`
(when the source was specified for `srt_prepare_endpoint`) in a loop for every
item specified in `name` array. However if you did this in blocking mode, the
first call to `srt_connect` would block until the connection is established,
whereas this function blocks until any of the specified connections is
established. 

If you set the group nonblocking mode (`SRTO_RCVSYN` option), there's no
difference. Note, however, that this function accepts only groups, not
sockets.

The elements of the `name` array need to be prepared with the use of the
`srt_prepare_endpoint` function. Note that it is **NOT** required that every
address you specify for it is of the same family.

Return value and errors in this function are the same as in `srt_connect`,
although this function reports success when at least one connection has
succeeded. Which one and how many of them succeeded, can be checked with the
`srt_group_data` function.


### srt_prepare_endpoint

```
SRT_SOCKGROUPDATA srt_prepare_endpoint(const struct sockaddr* src /*nullable*/,
                                       const struct sockaddr* adr, int namelen);
```

This function turns the given addresses into the `SRT_SOCKGROUPDATA` structure
needed by the `srt_connect_group` function.

* `src`: address to which the newly created socket should be bound
* `adr`: address to which the newly created socket should connect
* `namelen`: size of both `src` and `adr`

The `src` parameter is optional and can be NULL, in which case the
`srt_bind` will not be called on the newly created socket for that endpoint.
The `adr` parameter is obligatory. Note though that this function has no
possibility of reporting errors - these would be reported only by
`srt_connect_group`.


Options and properties
----------------------

### srt_getpeername
```
int srt_getpeername(SRTSOCKET u, struct sockaddr* name, int* namelen);
```

Retrieves the remote address to which the socket is connected.

- Returns:

  * `SRT_ERROR` (-1) in case of error, otherwise 0

- Errors:

  * `SRT_EINVSOCK`: Socket `u` indicates no valid socket ID
  * `SRT_ENOCONN`: Socket `u` isn't connected, so there's no remote address to return

### srt_getsockname
```
int srt_getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen);
```

Extracts the address to which the socket was bound. Although you  should know 
the address(es) that you have used for binding yourself, this function can be 
useful for extracting the local outgoing port number when it was specified as 0 
with binding for system autoselection. With this function you can extract the 
port number after it has been autoselected.

- Returns:

  * `SRT_ERROR` (-1) in case of error, otherwise 0

- Errors:

  * `SRT_EINVSOCK`: Socket `u` indicates no valid socket ID
  * `SRT_ENOCONN`: Socket `u` isn't bound, so there's no local
address to return (**BUG?** It should rather be `SRT_EUNBOUNDSOCK`)

### srt_getsockopt, srt_getsockflag
```
int srt_getsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT opt, void* optval, int* optlen);
int srt_getsockflag(SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen);
```

Gets the value of the given socket option (from a socket or a group). The first
version (`srt_getsockopt`) respects the BSD socket API convention, although the
"level" parameter is ignored.  The second version (`srt_getsockflag`) omits the
"level" parameter completely.

Options correspond to various data types, so you need to know what data type is 
assigned to a particular option, and to pass a variable of the appropriate data 
type. Specifications are provided in the `apps/socketoptions.hpp` file at the 
`srt_options` object declaration.

- Returns:

  * `SRT_ERROR` (-1) in case of error, otherwise 0

- Errors:

  * `SRT_EINVSOCK`: Socket `u` indicates no valid socket ID
  * `SRT_EINVOP`: Option `opt` indicates no valid option

### srt_setsockopt, srt_setsockflag

```
int srt_setsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT opt, const void* optval, int optlen);
int srt_setsockflag(SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);
```

Sets a value for a socket option in the socket or group. The first version
(`srt_setsockopt`) respects the BSD socket API convention, although the "level"
parameter is ignored. The second version (`srt_setsockflag`) omits the "level"
parameter completely.

Options correspond to various data types, so you need to know what data type is 
assigned to a particular option, and to pass a variable of the appropriate data 
type with the option value to be set.

Please note that some of the options can only be set on sockets or only on
groups, although most of the options can be set on the groups so that they
are then derived by the member sockets.

- Returns:

  * `SRT_ERROR` (-1) in case of error, otherwise 0

-Errors:

  * `SRT_EINVSOCK`: Socket `u` indicates no valid socket ID
  * `SRT_EINVOP`: Option `opt` indicates no valid option
  * Various other errors that may result from problems when setting a specific 
    option (see option description for details).

### srt_getversion

```
uint32_t srt_getversion();
```

Get SRT version value. The version format in hex is 0xXXYYZZ for x.y.z in human readable form, 
where x = ("%d", (version>>16) & 0xff), etc.

- Returns:

  * srt version as an unsigned 32-bit integer


Helper data types for transmission
----------------------------------

### SRT_MSGCTRL

The `SRT_MSGCTRL` structure:

```
typedef struct SRT_MsgCtrl_
{
   int flags;            // Left for future
   int msgttl;           // TTL for a message, default -1 (no TTL limitation)
   int inorder;          // Whether a message is allowed to supersede partially lost one. Unused in stream and live mode.
   int boundary;         //0:mid pkt, 1(01b):end of frame, 2(11b):complete frame, 3(10b): start of frame
   uint64_t srctime;     // source timestamp (usec), 0: use internal time     
   int32_t pktseq;       // sequence number of the first packet in received message (unused for sending)
   int32_t msgno;        // message number (output value for both sending and receiving)
} SRT_MSGCTRL;
```

The `SRT_MSGCTRL` structure is used in `srt_sendmsg2` and `srt_recvmsg2` calls 
and specifies some special extra parameters:

* `flags`: [IN, OUT]. RESERVED FOR FUTURE USE (should be 0). This is
intended to specify some special options controlling the details of how the
called function should work.

* `msgttl`: [IN]. In **message** and **live mode** only, specifies the TTL for 
sending messages (in `[ms]`). Not used for receiving messages. If this value
is not negative, it defines the maximum time up to which this message should
stay scheduled for sending for the sake of later retransmission. A message
is always sent for the first time, but the UDP packet carrying it may be
(also partially) lost, and if so, lacking packets will be retransmitted. If
the message is not successfully resent before TTL expires, further retransmission
is given up and the message is discarded.

* `inorder`: [IN]. In **message mode** only, specifies that sent messages should 
be extracted by the receiver in the order of sending. This can be meaningful if 
a packet loss has happened, and a particular message must wait for retransmission 
so that it can be reassembled and then delivered. When this flag is false, the 
message can be delivered even if there are any previous messages still waiting 
for completion.

* `boundary`: RESERVED FOR FUTURE USE. Intended to be used in a special mode 
when you are allowed to send or retrieve a part of the message.

* `srctime`:
   * [IN] Sender only. Specifies the application-provided timestamp. If not used
(specified as 0), the current system time (absolute microseconds since epoch) is 
used.
   * [OUT] Receiver only. Specifies the time when the packet was intended to be
delivered to the receiver.

* `pktseq`: Receiver only. Reports the sequence number for the packet carrying
out the payload being returned. If the payload is carried out by more than one
UDP packet, only the sequence of the first one is reported. Note that in
**live mode** there's always one UDP packet per message.

* `msgno`: Message number that can be sent by both sender and receiver,
although it is required that this value remain monotonic in subsequent send calls. 
Normally message numbers start with 1 and increase with every message sent.

**Helpers for `SRT_MSGCTRL`:**

```
void srt_msgctrl_init(SRT_MSGCTRL* mctrl);
const SRT_MSGCTRL srt_msgctrl_default;
```

Helpers for getting an object of `SRT_MSGCTRL` type ready to use. The first is
a function that fills the object with default values. The second is a constant
object and can be used as a source for assignment. Note that you cannot pass 
this constant object into any of the API functions because they require it to be 
mutable, as they use some fields to output values.

Transmission
------------
### srt_send, srt_sendmsg, srt_sendmsg2
```
int srt_send(SRTSOCKET u, const char* buf, int len);
int srt_sendmsg(SRTSOCKET u, const char* buf, int len, int ttl/* = -1*/, int inorder/* = false*/);
int srt_sendmsg2(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL *mctrl);
```

Sends a payload to a remote party over a given socket.

* `u`: Socket used to send. The socket must be connected for this operation.
* `buf`: Points to the buffer containing the payload to send.
* `len`: Size of the payload specified in `buf`.
* `ttl`: Time (in `[ms]`) to wait for a successful delivery. See description of 
the [`SRT_MSGCTRL::msgttl`](#SRT_MSGCTRL) field.
* `inorder`: Required to be received in the order of sending. See 
[`SRT_MSGCTRL::inorder`](#SRT_MSGCTRL).
* `mctrl`: An object of [`SRT_MSGCTRL`](#SRT_MSGCTRL) type that contains extra 
parameters, including `ttl` and `inorder`.

The way this function works is determined by the mode set in options, and it has
specific requirements:

1. In **file/stream mode**, the payload is byte-based. You are not required to
know the size of the data, although they are only guaranteed to be received
in the same byte order.

2. In **file/message mode**, the payload that you send using this function is 
a single message that you intend to be received as a whole. In other words, a 
single call to this function determines a message's boundaries.

3. In **live mode**, you are only allowed to send up to the length of
`SRTO_PAYLOADSIZE`, which can't be larger than 1456 bytes (1316 default).

- Returns:

  * Size of the data sent, if successful. Note that in **file/stream mode** the
returned size may be less than `len`, which means that it didn't send the
whole contents of the buffer. You would need to call this function again
with the rest of the buffer next time to send it completely. In both
**file/message** and **live mode** the successful return is always equal to `len`
  * In case of error, `SRT_ERROR` (-1)

- Errors:

  * `SRT_ENOCONN`: Socket `u` used when the operation is not connected.
  * `SRT_ECONNLOST`: Socket `u` used for the operation has lost its connection.
  * `SRT_EINVALMSGAPI`: Incorrect API usage in **message mode**:
    * **live mode**: trying to send more bytes at once than `SRTO_PAYLOADSIZE`
  * `SRT_EINVALBUFFERAPI`: Incorrect API usage in **stream mode**:
    * Reserved for future use. The congestion controller object
      used for this mode doesn't use any restrictions on this call for now,
      but this may change in future.
  * `SRT_ELARGEMSG`: Message to be sent can't fit in the sending buffer (that is,
it exceeds the current total space in the sending buffer in bytes). This means
that the sender buffer is too small, or the application is trying to send
a larger message than initially predicted.
  * `SRT_EASYNCSND`: There's no free space currently in the buffer to schedule
the payload. This is only reported in non-blocking mode (`SRTO_SNDSYN` set
to false); in blocking mode the call is blocked until enough free space in
the sending buffer becomes available.
  * `SRT_ETIMEOUT`: The condition described above still persists and the timeout
has passed. This is only reported in blocking mode when `SRTO_SNDTIMEO` is
set to a value other than -1.
  * `SRT_EPEERERR`: This is reported only in the case where, as a stream is being 
  received by a peer, the `srt_recvfile` function encounters an error during a 
  write operation on a file. This is reported by a `UMSG_PEERERROR` message from 
  the peer, and the agent sets the appropriate flag internally. This flag 
  persists up to the moment when the connection is broken or closed.

### srt_recv, srt_recvmsg, srt_recvmsg2

```
int srt_recv(SRTSOCKET u, char* buf, int len);
int srt_recvmsg(SRTSOCKET u, char* buf, int len);
int srt_recvmsg2(SRTSOCKET u, char *buf, int len, SRT_MSGCTRL *mctrl);
```

Extracts the payload waiting to be received. Note that `srt_recv` and `srt_recvmsg`
are identical functions, two different names being kept for historical reasons.
In the UDT predecessor the application was required to use either the `UDT::recv`
version for **stream mode** and `UDT::recvmsg` for **message mode**. In SRT this
distinction is resolved internally by the `SRTO_MESSAGEAPI` flag.

* `u`: Socket used to send. The socket must be connected for this operation.
* `buf`: Points to the buffer to which the payload is copied
* `len`: Size of the payload specified in `buf`
* `mctrl`: An object of [`SRT_MSGCTRL`](#SRT_MSGCTRL) type that contains extra 
parameters

The way this function works is determined by the mode set in options, and it has 
specific requirements:

1. In **file/stream mode**, as many bytes as possible are retrieved, that is,
only so many bytes that fit in the buffer and are currently available. Any
data that is available but not extracted this time will be available next time.

2. In **file/message mode**, exactly one message is retrieved, with the 
boundaries defined at the moment of sending. If some parts of the messages are 
already retrieved, but not the whole message, nothing will be received (the 
function blocks or returns `SRT_EASYNCRCV`). If the message to be returned does 
not fit in the buffer, nothing will be received and the error is reported.

3. In **live mode**, the function behaves as in **file/message mode**, although 
the number of bytes retrieved will be at most the size of `SRTO_PAYLOADSIZE`. In 
this mode, however, with default settings of `SRTO_TSBPDMODE` and `SRTO_TLPKTDROP`, 
the message will be received only when its time to play has come, and until then 
it will be kept in the receiver buffer; also, when the time to play has come
for a message that is next to the currently lost one, it will be delivered
and the lost one dropped.

- Returns:

  * Size (\>0) of the data received, if successful.
  * 0, if the connection has been closed
  * `SRT_ERROR` (-1) when an error occurs 

- Errors:

  * `SRT_ENOCONN`: Socket `u` used for the operation is not connected.
  * `SRT_ECONNLOST`: Socket `u` used for the operation has lost connection
(this is reported only if the connection was unexpectedly broken, not
when it was closed by the foreign host).
  * `SRT_EINVALMSGAPI`: Incorrect API usage in **message mode**:
    * **live mode**: size of the buffer is less than `SRTO_PAYLOADSIZE`
  * `SRT_EINVALBUFFERAPI`: Incorrect API usage in **stream mode**:
    * Currently not in use. File congestion control used for **stream mode** 
     does not restrict the parameters. **???**
  * `SRT_ELARGEMSG`: Message to be sent can't fit in the sending buffer (that is,
it exceeds the current total space in the sending buffer in bytes). This means
that the sender buffer is too small, or the application is trying to send
a larger message than initially intended.
  * `SRT_EASYNCRCV`: There are no data currently waiting for delivery. This
happens only in non-blocking mode (when `SRTO_RCVSYN` is set to false). In
blocking mode the call is blocked until the data are ready. How this is defined,
depends on the mode:
   * In **live mode** (with `SRTO_TSBPDMODE` on), at least one packet must
   be present in the receiver buffer and its time to play be in the past
   * In **file/message mode**, one full message must be available,
     * the next one waiting if there are no messages with `inorder` = false, or 
     possibly the first message ready with `inorder` = false
   * In **file/stream mode**, it is expected to have at least one byte of data 
   still not extracted
   * `SRT_ETIMEOUT`: The readiness condition described above is still not achieved 
and the timeout has passed. This is only reported in blocking mode when
`SRTO_RCVTIMEO` is set to a value other than -1.

### srt_sendfile, srt_recvfile

```
int64_t srt_sendfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
int64_t srt_recvfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
```

These are functions dedicated to sending and receiving a file. You need to call
this function just once for the whole file, although you need to know the size of
the file prior to sending and also define the size of a single block that should
be internally retrieved and written into a file in a single step. This influences
only the performance of the internal operations; from the application perspective
you just have one call that exits only when the transmission is complete.

* `u`: Socket used for transmission. The socket must be connected.
* `path`: Path to the file that should be read or written.
* `offset`: Needed to pass or retrieve the offset used to read or write to a file
* `size`: Size of transfer (file size, if offset is at 0)
* `block`: Size of the single block to read at once before writing it to a file

The following values are recommended for the `block` parameter:

```
#define SRT_DEFAULT_SENDFILE_BLOCK 364000
#define SRT_DEFAULT_RECVFILE_BLOCK 7280000
```

You need to pass them to the `srt_sendfile` or `srt_recvfile` function if you 
don't know what value to chose.

- Returns:

  * Size (\>0) of the transmitted data of a file. It may be less than `size`, if 
  the size was greater than the free space in the buffer, in which case you have 
  to send rest of the file next time.
  * -1 in case of error.

- Errors:

  * `SRT_ENOCONN`: Socket `u` used for the operation is not connected.
  * `SRT_ECONNLOST`: Socket `u` used for the operation has lost its connection.
  * `SRT_EINVALBUFFERAPI`: When socket has `SRTO_MESSAGEAPI` = true or 
  `SRTO_TSBPDMODE` = true.
(**BUG?**: Looxlike MESSAGEAPI isn't checked)
  * `SRT_EINVRDOFF`: There is a mistake in `offset` or `size` parameters, which 
  should match the index availability and size of the bytes available since 
  `offset` index. This is actually reported for `srt_sendfile` when the `seekg` 
  or `tellg` operations resulted in error.
  * `SRT_EINVWROFF`: Like above, reported for `srt_recvfile` and `seekp`/`tellp`.
  * `SRT_ERDPERM`: The read from file operation has failed (`srt_sendfile`).
  * `SRT_EWRPERM`: The write to file operation has failed (`srt_recvfile`).

Diagnostics
-----------

General notes concerning the "getlasterror" diagnostic functions: when an API
function ends up with error, this error information is stored in a thread-local
storage. This means that you'll get the error of the operation that was last
performed as long as you call this diagnostic function just after the failed
function has returned. In any other situation the information provided by the
diagnostic function is undefined.

### srt_getlasterror

```
int srt_getlasterror(int* errno_loc);
```

Get the numeric code of the last error. Additionally, in the variable passed as
`errno_loc` the system error value is returned, or 0 if there was no system error
associated with the last error. The system error is:

  * On POSIX systems, the value from `errno`
  * On Windows, the result from `GetLastError()` call


### srt_strerror
```
const char* srt_strerror(int code, int errnoval);
```

Returns a string message that represents a given SRT error code and possibly the
`errno` value, if not 0.

**NOTE:** *This function isn't thread safe. It uses a static variable to hold the
error description. There's no problem with using it in a multithreaded environment,
as long as only one thread in the whole application calls this function at the
moment*

### srt_getlasterror_str
```
const char* srt_getlasterror_str(void);
```

Get the text message for the last error. It's a shortcut to calling first
`srt_getlasterror` and then passing the returned value into `srt_strerror`.
Note that, in contradiction to `srt_strerror`, this function is thread safe.


### srt_clearlasterror

```
void srt_clearlasterror(void);
```

This function clears the last error. After this call, the `srt_getlasterror` will
report a "successful" code.

### srt_getrejectreason

```
enum SRT_REJECT_REASON srt_getrejectreason(SRTSOCKET sock);
```

This function shall be called after a connecting function (such as `srt_connect`)
has returned an error, which's code was `SRT_ECONNREJ`. It allows to get a more
detailed rejection reason. This function returns a numeric code, which can be
translated into a message by `srt_rejectreason_str`. The following codes are
currently reported:

#### SRT_REJ_UNKNOWN

A fallback value for cases when there was no connection rejected.

#### SRT_REJ_SYSTEM

One of system function reported a failure. Usually this means some system
error or lack of system resources to complete the task.

#### SRT_REJ_PEER

The connection has been rejected by peer, but no further details are available.
This usually means that the peer doesn't support rejection reason reporting.

#### SRT_REJ_RESOURCE

A problem with resource allocation (usually memory).

#### SRT_REJ_ROGUE

The data sent by one party to another cannot be properly interpreted. This
should not happen during normal usage, unless it's a bug, or some weird
events are happening on the network.

#### SRT_REJ_BACKLOG

The listener's backlog has exceeded (there are many other callers waiting for
the opportunity of being connected and wait in the queue, which has reached
its limit).

#### SRT_REJ_IPE

Internal Program Error. This should not happen during normal usage and it
usually means a bug in the software (although this can be reported by both
local and foreign host).

#### SRT_REJ_CLOSE

The listener socket was able to receive your request, but at this moment it
is being closed. It's likely that your next attempt will result with timeout.

#### SRT_REJ_VERSION

Any party of the connection has set up minimum version that is required for
that connection, and the other party didn't satisfy this requirement.

#### SRT_REJ_RDVCOOKIE

Rendezvous cookie collision. This normally should never happen, or the
probability that this will really happen is negligible. However this can
be also a result of a misconfiguration that you are trying to make a
rendezvous connection where both parties try to bind to the same IP
address, or both are local addresses of the same host - in which case
the sent handshake packets are returning to the same host as if they
were sent by the peer, who is this party itself. When this happens,
this reject reason will be reported by every attempt.

#### SRT_REJ_BADSECRET

Both parties have defined a passprhase for connection and they differ.

#### SRT_REJ_UNSECURE

Only one connection party has set up a password. See also
`SRTO_ENFORCEDENCRYPTION` flag in API.md.

#### SRT_REJ_MESSAGEAPI

The value for `SRTO_MESSAGEAPI` flag is different on both connection
parties.

#### SRT_REJ_CONGESTION

The `SRTO_CONGESTION` option has been set up differently on both
connection parties.

#### SRT_REJ_FILTER

The `SRTO_PACKETFILTER` option has been set differently on both connection
parties.


### srt_rejectreason_str

```
const char* srt_rejectreason_str(enum SRT_REJECT_REASON id);
```

Returns a constant string for the reason of the connection rejected,
as per given code id. Alternatively you can use the `srt_rejectreason_msg`
array. This function additionally handles the case for unknown id by
reporting `SRT_REJ_UNKNOWN` in such case.


Performance tracking
--------------------

General note concerning sequence numbers used in SRT: they are 32-bit "circular
numbers" with the most significant bit not included. For example 0x7FFFFFFF
shifted forward by 3 becomes 2. As far as any comparison is concerned, it can
be thought of as a "distance" which is an integer
value expressing an offset to be added to one sequence in order to get the
second one. This distance is only valid as long as the threshold value isn't
exceeded, so it's stated that all sequence numbers that are anywhere taken into
account were systematically updated and they are kept in the range between 0
and half of the maximum 0x7FFFFFFF. Hence the distance counting procedure
always assumes that the sequence number are in the required range already, so
for a numbers like 0x7FFFFFF0 and 0x10, for which the "numeric difference"
would be 0x7FFFFFE0, the "distance" is 0x20.

### srt_bstats, srt_bistats
```
// Performance monitor with Byte counters for better bitrate estimation.
int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear);

// Performance monitor with Byte counters and instantaneous stats instead of moving averages for Snd/Rcvbuffer sizes.
int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous);
```

Reports the current statistics

* `u`: Socket from which to get statistics
* `perf`: Pointer to an object to be written with the statistics
* `clear`: 1 if the statistics should be cleared after retrieval
* `instantaneous`: 1 if the statistics should use instant data, not moving averages

`SRT_TRACEBSTATS` is an alias to `struct CBytePerfMon`. For a complete description
of the fields please refer to the document [statistics.md](statistics.md).

Asynchronous operations (epoll)
-------------------------------

The epoll system is currently the only method for using multiple sockets in one
thread with having the blocking operation moved to epoll waiting so that it can
block on multiple sockets at once. That is, instead of blocking a single reading
or writing operation, as it's in blocking mode, it blocks until at least one of
the sockets subscribed for a single waiting call in given operation mode is ready
to do this operation without blocking. It's usually combined with setting the
nonblocking mode on a socket, which in SRT is set separately for reading and
writing (`SRTO_RCVSYN` and `SRTO_SNDSYN` respectively) in order to ensure that
in case of some internal error in the application (or even possibly a bug in SRT
that has reported a spurious readiness report) the operation will end up with
error rather than cause blocking, which would be more dangerous for the application
in this case (`SRT_EASYNCRCV` and `SRT_EASYNCRCV` respectively).

The epoll system, similar to the one on Linux, relies on `eid` objects managed
internally in SRT, which can be subscribed to particular sockets and the 
readiness status of particular operations. The `srt_epoll_wait` function can 
then be used to block until any readiness status in the whole `eid` is set.

### srt_epoll_create
```
int srt_epoll_create(void);
```

Creates a new epoll container.

- Returns:

  * valid EID on success
  * -1 on failure

- Errors:

  * `SRT_ECONNSETUP`: System operation failed or not enough space to create a new epoll.
System error might happen on systems that use a 
special method for the system part of epoll (`epoll_create()`, `kqueue()`), and therefore associated resources,
like epoll on Linux.

### srt_epoll_add_usock, srt_epoll_add_ssock, srt_epoll_update_usock, srt_epoll_update_ssock

```
int srt_epoll_add_usock(int eid, SRTSOCKET u, const int* events);
int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int* events);
int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events);
int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events);
```

Adds a socket to a container, or updates an existing socket subscription.

The `_usock` suffix refers to a user socket (SRT socket). 
The `_ssock` suffix refers to a system socket.

The `_add_` functions add new sockets. The `_update_` functions act on a socket 
that is in the container already and just allow changes in the subscription 
details. For example, if you have already subscribed a socket with `SRT_EPOLL_OUT` 
to wait until it's connected, to change it into poll for read-readiness, you use 
this function on that same socket with a variable set to `SRT_EPOLL_IN`. This 
will not only change the event type which is polled on the socket, but also 
remove any readiness status for flags that are no longer set. It is discouraged
to perform socket removal and adding back (instead of using `_update_`) because
this way you may miss an event that could happen in a short moment between
these two calls.

* `eid`: epoll container id
* `u`: SRT socket
* `s`: system socket
* `events`: points to
  * a variable set to epoll flags (see below) to use only selected events
  * NULL if you want to subscribe a socket for all events in level-triggered mode

Possible epoll flags are the following:

   * `SRT_EPOLL_IN`: report readiness for reading or incoming connection on a listener socket
   * `SRT_EPOLL_OUT`: report readiness for writing or a successful connection
   * `SRT_EPOLL_ERR`: report errors on the socket
   * `SRT_EPOLL_UPDATE`: group-listening socket gets a new connection established
   * `SRT_EPOLL_ET`: the event will be edge-triggered

All flags except `SRT_EPOLL_ET` are event type flags (important for functions
that expect only event types and not other flags).

The `SRT_EPOLL_IN`, `SRT_EPOLL_OUT` and `SRT_EPOLL_ERR` events are by
default **level-triggered**. With `SRT_EPOLL_ET` flag they become
**edge-triggered**. The `SRT_EPOLL_UPDATE` flag is always edge-triggered
and it designates a special event that happens only for a listening
socket that has the `SRTO_GROUPCONNECT` flag set to allow group connections.
This event is intended for internal use only, and is triggered for group
connections when a new link has been established for a group that is
already connected (that is, has at least one connection established).

Note that at this time the edge-triggered mode is supported only for SRT
sockets, not for system sockets.

In the **edge-triggered** mode the function will only return socket states that
have changed since the last call of the waiting function. All events reported
in particular call of the waiting function will be cleared in the internal
flags and will not be reported until the internal signaling logic clears this
state and raises it again.

In the **level-triggered** mode the function will always return the readiness
state as long as it lasts, until the internal signaling logic clear it.

Note that when you use `SRT_EPOLL_ET` flag in one subscription call, it defines
edge-triggered mode for all events passed together with it. However, if you
want to have some events reported as edge-triggered and others as
level-triggered, you can do two separate subscriptions for the same socket.


- Returns:

  * 0 if successful, otherwise -1

- Errors:

  * `SRT_EINVPOLLID`: `eid` parameter doesn't refer to a valid epoll container

**BUG?**: for `add_ssock` the system error results in an empty `CUDTException()`
call which actually results in `SRT_SUCCESS`. For cases like that the
`SRT_ECONNSETUP` code is predicted.

### srt_epoll_remove_usock, srt_epoll_remove_ssock

```
int srt_epoll_remove_usock(int eid, SRTSOCKET u);
int srt_epoll_remove_ssock(int eid, SYSSOCKET s);
```

Removes a specified socket from an epoll container and clears all readiness
states recorded for that socket.

The `_usock` suffix refers to a user socket (SRT socket). 
The `_ssock` suffix refers to a system socket.

- Returns:

  * 0 if successful, otherwise -1

- Errors:

  * `SRT_EINVPOLLID`: `eid` parameter doesn't refer to a valid epoll container

### srt_epoll_wait
```
int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                        SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
```

Blocks the call until any readiness state occurs in the epoll container.

Readiness can be on a socket in the container for the event type as per
subscription. Note that in case when particular event was subscribed with
`SRT_EPOLL_ET` flag, this event, when once reported in this function, will
be cleared internally.

The first readiness state causes this function to exit, but all ready sockets
are reported. This function blocks until the timeout specified in `msTimeOut`
parameter.  If timeout is 0, it exits immediately after checking. If timeout is
-1, it blocks indefinitely until a readiness state occurs.

* `eid`: epoll container
* `readfds` and `rnum`: A pointer and length of an array to write SRT sockets that are read-ready
* `writefds` and `wnum`: A pointer and length of an array to write SRT sockets that are write-ready
* `msTimeOut`: Timeout specified in milliseconds, or special values (0 or -1)
* `lwfds` and `lwnum`:A pointer and length of an array to write system sockets that are read-ready
* `lwfds` and `lwnum`:A pointer and length of an array to write system sockets that are write-ready

Note that there is no space here to report sockets for which it's already known
that the operation will end up with error (athough such a state is known
internally). If an error occurred on a socket then that socket is reported in
both read-ready and write-ready arrays, regardless of what event types it was
subscribed for. Usually then you subscribe given socket for only read readiness,
for example (`SRT_EPOLL_IN`), but pass both arrays for read and write readiness.
This socket will not be reported in the write readiness array even if it's write
ready (because this isn't what it was subscribed for), but it will be reported
there, if the next operation on this socket is about to be erroneous. On such
sockets you can still perform an operation, just you should expect that it will
always report and error. On the other hand that's the only way to know what kind
of error has occurred on the socket.

- Returns:

  * The number (\>0) of ready sockets, of whatever kind (if any)
  * -1 in case of error

- Errors:

  * `SRT_EINVPOLLID`: `eid` parameter doesn't refer to a valid epoll container
  * `SRT_ETIMEOUT`: Up to `msTimeOut` no sockets subscribed in `eid` were ready.
This is reported only if `msTimeOut` was \>=0, otherwise the function waits
indefinitely.

### srt_epoll_uwait
```
int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
```

This function blocks a call until any readiness state occurs in the epoll
container. Unlike `srt_epoll_wait`, it can only be used with `eid` subscribed
to user sockets (SRT sockets), not system sockets.

This function blocks until the timeout specified in `msTimeOut` parameter. If
timeout is 0, it exits immediately after checking. If timeout is -1, it blocks
indefinitely until a readiness state occurs.

* `eid`: epoll container
* `fdsSet` : A pointer to an array of `SRT_EPOLL_EVENT`
* `fdsSize` : The size of the fdsSet array
* `msTimeOut` : Timeout specified in milliseconds, or special values (0 or -1):
   * 0: Don't wait, return immediately (report any sockets currently ready)
   * -1: Wait indefinitely.

- Returns:

  * The number of user socket (SRT socket) state changes that have been reported
in `fdsSet`, if this number isn't greater than `fdsSize`

  * Otherwise the return value is `fdsSize` + 1. This means that there was not
enough space in the output array to report all events. For events subscribed with
`SRT_EPOLL_ET` flag only those will be cleared that were reported. Others will
wait for the next call.

  * If no readiness state was found on any socket and the timeout has passed, 0
is returned (this is not possible when waiting indefinitely)

  * -1 in case of error


- Errors:

  * `SRT_EINVPOLLID`: `eid` parameter doesn't refer to a valid epoll container
  * `SRT_EINVPARAM`: One of possible usage errors:
    * `fdsSize` is < 0
    * `fdsSize` is > 0 and `fdsSet` is a null pointer
    * `eid` was subscribed to any system socket

(IMPORTANT: this function reports timeout by returning 0, not by `SRT_ETIMEOUT` error.)

The `SRT_EPOLL_EVENT` structure:

```
typedef struct SRT_EPOLL_EVENT_
{
	SRTSOCKET fd;
	int       events;
} SRT_EPOLL_EVENT;
```

* `fd` : the user socket (SRT socket)
* `events` : event flags that report readiness of this socket - a combination
of `SRT_EPOLL_IN`, `SRT_EPOLL_OUT` and `SRT_EPOLL_ERR` - see [srt_epoll_add_usock](#srt_epoll_add_usock)
for details

Note that when the `SRT_EPOLL_ERR` is set, the underlying socket error
can't be retrieved with `srt_getlasterror()`. The socket will be automatically
closed and its state can be verified with a call to `srt_getsockstate`.

### srt_epoll_clear_usocks
```
int srt_epoll_clear_usocks(int eid);
```

This function removes all SRT ("user") socket subscriptions from the epoll
container identified by `eid`.

- Returns:
  * 0 on success
  * -1 in case of error

- Errors:

  * `SRT_EINVPOLLID`: `eid` parameter doesn't refer to a valid epoll container

### srt_epoll_set
```
int32_t srt_epoll_set(int eid, int32_t flags);
```

This function allows to set or retrieve flags that change the default
behavior of the epoll functions. All default values for these flags are 0.
The following flags are available:

* `SRT_EPOLL_ENABLE_EMPTY`: allows the `srt_epoll_wait` and `srt_epoll_uwait`
functions to be called with the EID not subscribed to any socket. The default
behavior of these function is to report error in this case.

* `SRT_EPOLL_ENABLE_OUTPUTCHECK`: Forces the `srt_epoll_wait` and `srt_epoll_uwait`
functions to check if the output array is not empty. For `srt_epoll_wait` it
is still allowed that either system or user array is empty, as long as EID
isn't subscribed to this type of socket/fd. `srt_epoll_uwait` only checks if
the general output array is not empty.

- Parameters:

   * `eid`: the epoll container id
   * `flags`: a nonzero set of the above flags, or special values:
      * 0: clear all flags (set all defaults)
      * -1: do not modify any flags

- Returns:

This function returns the state of the flags at the time before the call,
or a special value -1 in case when an error occurred.

- Errors:

  * `SRT_EINVPOLLID`: `eid` parameter doesn't refer to a valid epoll container


### srt_epoll_release
```
int srt_epoll_release(int eid);
```

Deletes the epoll container.

- Returns:

  * The number (\>0) of ready sockets, of whatever kind (if any)
  * -1 in case of error

- Errors:


  * `SRT_EINVPOLLID`: `eid` parameter doesn't refer to a valid epoll container

Logging control
---------------

SRT has a widely used system of logs, as this is usually the only way to determine
how the internals are working, without changing the rules by the act of tracing.
Logs are split into levels (5 levels out of those defined by syslog are in use)
and additional filtering is possible on FA (functional area). By default only
up to the *Note* log level are displayed and from all FAs.

Logging can only be manipulated globally, with no regard to a specific 
socket. This is because lots of operations in SRT are not dedicated to any 
particular socket, and some are shared between sockets.

### srt_setloglevel

```
void srt_setloglevel(int ll);
```

Sets the minimum severity for logging. A particular log entry is displayed only 
if it has a severity greater than or equal to the minimum. Setting this value 
to `LOG_DEBUG` turns on all levels.

The constants for this value are those from `<sys/syslog.h>`
(for Windows, refer to `common/win/syslog_defs.h`). The only meaningful are:

* `LOG_DEBUG`: Highly detailed and very frequent messages
* `LOG_NOTICE`: Occasionally displayed information
* `LOG_WARNING`: Unusual behavior
* `LOG_ERR`: Abnormal behavior
* `LOG_CRIT`: Error that makes the current socket unusable

### srt_addlogfa, srt_dellogfa, srt_resetlogfa

```
void srt_addlogfa(int fa);
void srt_dellogfa(int fa);
void srt_resetlogfa(const int* fara, size_t fara_size);
```

A functional area (FA) is an additional filtering mechanism for logging. You can 
set up logging to display logs only from selected FAs. The list of FAs is
collected in `srt.h` file, as identified by the `SRT_LOGFA_` prefix. They are not
enumerated here because they may be changed very often.


All FAs are turned on by default, except potentially dangerous ones
(such as `SRT_LOGFA_HAICRYPT`). The reaons is that they may display either
some security information that shall remain in the memory only (so, only
if strictly required for the development), or some duplicated information
(so you may want to turn this FA on, while turning off the others).

### srt_setloghandler
```
void srt_setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
typedef void SRT_LOG_HANDLER_FN(void* opaque, int level, const char* file, int line, const char* area, const char* message);
```

By default logs are printed to standard error stream. This function replaces
the sending to a stream with a handler function that will receive them.

### srt_setlogflags
```
void srt_setlogflags(int flags);
```

When you set a log handler with `srt_setloghandler`, you may also want to
configure which parts of the log information you do not wish to be passed 
in the log line (the `message` parameter). A user's logging facility may,
for example, not wish to get the current time or log level marker, as it
will provide this information on its own.


The following flags are available, as collected in `logging_api.h` public header:

* `SRT_LOGF_DISABLE_TIME`: Do not provide the time in the header
* `SRT_LOGF_DISABLE_THREADNAME`: Do not provide the thread name in the header
* `SRT_LOGF_DISABLE_SEVERITY`: Do not provide severity information in the header
* `SRT_LOGF_DISABLE_EOL`: Do not add the end-of-line character to the log line

[RETURN TO TOP OF PAGE](#SRT-API-Functions)
