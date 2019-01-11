SRT API Functions
=================

Helper data types
-----------------

The `SRT_MSGCTRL` structure:

```
typedef struct SRT_MsgCtrl_
{
   int flags;            // Left for future
   int msgttl;           // TTL for a message, default -1 (delivered always)
   int inorder;          // Whether a message is allowed to supersede partially lost one. Unused in stream and live mode.
   int boundary;         //0:mid pkt, 1(01b):end of frame, 2(11b):complete frame, 3(10b): start of frame
   uint64_t srctime;     // source timestamp (usec), 0: use internal time     
   int32_t pktseq;       // sequence number of the first packet in received message (unused for sending)
   int32_t msgno;        // message number (output value for both sending and receiving)
} SRT_MSGCTRL;
```

The `SRT_MSGCTRL` structure is used in `srt_sendmsg2` and `srt_recvmsg2` calls and it
specifies some special extra parameters:

* `flags`: [IN, OUT]. Nothing so far, reserved for future, should be 0. This is
intended to specify some special options controlling the details of how the
called function should work
* `msgttl`: [IN] The TTL for the message sending, in `[ms]` (for receiving,
unused). The packet is scheduled for sending by this call and then waits in
the sender buffer to be picked up at the moment when all previously scheduled
data are already send, which may be blocked when the data are scheduled faster
than the network can bear to send. Default -1 means to wait indefinitely. If
specified, then the packet waits for an opportunity for being sent over the
network only up to this time, and next discarded.
* `inorder`: [IN] Used only for sending. If set, the message should be extracted
by the receiver in the order of sending. This can be meaningful if a packet
loss has happened so particular message must wait for retransmission so that it
can be reassembled and then delivered. When this flag is false, this message
can be delivered even if there are any previous message still waiting for
completion.
* `boundary`: Currently unused, reserved for future. Predicted to be used in
a special mode when you are allowed to send or retrieve a part of the message.
* `srctime`:
   * [IN] Sender only. Specifies the application-provided timestamp. If not used
(specified as 0), the current system time (absolute microseconds since epoch) is used.
   * [OUT] Receiver only. Specifies the time when the packet was intended to be
delivered to the receiver.
* `pktseq`: Receiver only. Reports the sequence number for the packet carrying
out the payload being returned. If the payload is carried out by more than one
UDP packet, the reported is only the sequence of the first one. Note that in
live mode there's always one UDP packet per message.
* `msgno`: Message number. It's allowed to be sent in both sender and receiver,
although it is required that this value remains monotonic in subsequent calls
to sending. Normally message numbers start with 1 and increase with every message
sent.

Helpers:

```
void srt_msgctrl_init(SRT_MSGCTRL* mctrl);
const SRT_MSGCTRL srt_msgctrl_default;
```

Helpers for getting an object of `SRT_MSGCTRL` type ready to use. The first is
a function and it fills the object with default values. The second is a constant
object and can be used as a source for assignment. Note that you cannot pass this
constant object into any API function because they require it to be mutable, as
they use some field to output values.



Library initialization
----------------------

```
int srt_startup(void);
```

This function shall be called in the beginning of the application that uses SRT
library. Bseide all necessary platform-specific initializations and setting up
global data, it also starts the SRT GC thread. If this function isn't called,
it will be called at the time of creating the first socket, however relying on
that is strongly discouraged.

```
int srt_cleanup(void);
```

This function cleans up all global SRT resources and shall be called just before exitting
the application that uses SRT library.


Creating and configuring socket
-------------------------------

```
SRTSOCKET srt_socket(int af, int type, int protocol);
```

Creates an SRT socket.

* `af`: Family, shall be `AF_INET` or `AF_INET6`
* `type`, `protocol`: ignored

*Note: UDT library used `type` parameter to specify the file or message mode by
stating that `SOCK_STREAM` mean a TCP-like file transmission mode and `SOCK_DGRAM`
means an SCTP-like message transmission mode. SRT still does support these modes,
however this is controlled by `SRTO_MESSAGEAPI` socket option when the transmission
type is file (`SRTO_TRANSTYPE` set to `SRTT_FILE`) and the only sensible value for
`type` parameter here is `SOCK_DGRAM`.*

Returns:
* a valid socket ID on success
* `INVALID_SOCKET` (-1) on error

Errors:
* `SRT_ENOTBUF`: not enough memory to allocate required resources


```
SRTSOCKET srt_create_socket();
```

New and future version of a function to create a socket. Currently it creates
a socket in `AF_INET` family only.

```
int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
```

Binds a socket to a local address and port. Binding specifies the local network
interface to be used for the socket and the UDP port number. When the local address
is a form of `INADDR_ANY`, then it's bound to all interfaces. When the port number
is 0, then the port number will be system-allocated if necessary.

For a listening socket this call is obligatory and it defines the network interface
and the port where the listener should expect a call request. For a connecting socket
this call can set up the outgoing port to be used in the communication. It is allowed
that multiple SRT socket share one local outgoing port, as long as `SRTO_REUSEADDR`
is set to true (default).

Returns:
* `SRT_ERROR` (-1) on error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket passed as `u` designates no valid socket
* `SRT_EINVOP`: Socket already bound
* `SRT_EINVPARAM`: Address family in `name` is not one set for `srt_socket`
* `SRT_ECONNSETUP`: Internal creation of a UDP socket failed 
* `SRT_ESOCKFAIL`: Internal configuring of a UDP socket (bind, setsockopt) failed

*FUTURE: It's planned that the address family be removed from initial socket configuration,
which frees the user from specifying it in `srt_socket`, so `srt_create_socket` will be
used for all families. In this case the family will be specified only with the first
`srt_bind` or `srt_connect`, and in this case `SRT_EINVPARAM` will not be the case.*

```
int srt_bind_peerof(SRTSOCKET u, UDPSOCKET udpsock);
```

A version of `srt_bind` that acquires given UDP socket instead of creating one.

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
socket is connected.
* `SRTS_CONNECTED`: The socket is connected and ready for transmission.
* `SRTS_BROKEN`: The socket was connected, but the connection got broken
* `SRTS_CLOSING`: The socket may still be under some operation, but closing
is requested, so no further operation will be accepted, only finishing what
has been ordered so far
* `SRTS_CLOSED`: The socket has been closed, just not yet wiped out by GC
thread
* `SRTS_NONEXIST`: The given number designates no valid socket.

```
int srt_getsndbuffer(SRTSOCKET sock, size_t* blocks, size_t* bytes);
```

Retrieves information about the sender buffer.

* `sock`: Socket to test
* `blocks`: written information about buffer blocks in use
* `bytes`: written information about bytes in use

This function can be used for diagnostics, especially useful when the socket
needs to be closed asynchronously.


```
int srt_close(SRTSOCKET u);
```

Closes the socket and frees all used resources. Note that underlying UDP sockets
may be shared between sockets, so these are freed only with the last user closed.

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket `u` designates no valid socket ID


Connecting
----------

```
int srt_listen(SRTSOCKET u, int backlog);
```

This sets up the listening state on a socket with setting the backlog, which defines
how many sockets may be accepted to wait until they are accepted.

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0.

Errors:
* `SRT_EINVPARAM`: Value of `backlog` is 0 or negative.
* `SRT_EINVSOCK`: Socket `u` designates no valid SRT socket
* `SRT_EUNBOUNDSOCK`: `srt_bind` wasn't called on that socket yet
* `SRT_ERDVNOSERV`: `SRTO_RENDEZVOUS` flag is set to true on given socket
* `SRT_EINVOP`: Internal error (should not happen when reported `SRT_EUNBOUNDSOCK`)
* `SRT_ECONNSOCK`: The socket is already connected
* `SRT_EDUPLISTEN`: Address used in `srt_bind` in this socket is already
occupied by another listening socket (It is allowed to bind multiple sockets to
one IP address and port, as long as `SRTO_REUSEADDR` is set to true, but only
one of these socket can be set up as a listener)

```
SRTSOCKET srt_accept(SRTSOCKET lsn, struct sockaddr* addr, int* addrlen);
```

Accepts a connection. This function creates a new socket that is connected to
a remote party, after that party requested a connection that has been handled by
the listener socket.

* `lsn`: the listener socket previously configured by `srt_listen`
* `addr`: the IP address and port specification for the remote party
* `addrlen`: INPUT: size of `addr` pointed object. OUTPUT: real size of the returned object

Returns:
* A valid socket ID for the connection, on success
* On failure, `SRT_ERROR` (-1)

Errors:
* `SRT_EINVPARAM`: NULL specified as `addrlen`, when `addr` is not NULL
* `SRT_EINVSOCK`: `lsn` designates no valid socket ID. Can also mean Internal
Error in case when an error occurred when creating an accepted socket (**BUG**?)
* `SRT_ENOLISTEN`: `lsn` is not set up as a listener (`srt_listen` not called,
or the listener socket has been closed in the meantime)
* `SRT_ERDVNOSERV`: Internal error (if no `SRT_ENOLISTEN` reported, it means
that the socket could not be set up as rendezvous because `srt_listen` does
not allow it)
* `SRT_EASYNCRCV`: No connection reported so far. This error is reported only
when the `lsn` listener socket was configured as nonblocking for reading
(`SRTO_RCVSYN` set to false); otherwise the call blocks until a connection
is reported or an error occurred


```
int srt_connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
```

This connects given socket to a remote party with specified address and port.

* `u`: SRT socket. This must be a socket freshly created and not yet used for anything
except possibly `srt_bind`.
* `name`: specification of the remote address and port
* `namelen`: size of the object passed by `name`

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket `u` designates no valid socket ID
* `SRT_EINVPARAM`: Address family in `name` is not one set for `srt_socket`
* `SRT_ERDVUNBOUND`: Socket `u` has set `SRTO_RENDEZVOUS` to true, but `srt_bind`
wasn't called on it yet. The `srt_connect` function is also used to connect a
rendezvous socket, but rendezvous sockets must be explicitly bound to a local
interface prior to connecting. Non-rendezvous sockets (caller sockets) can be
left without binding - the call to `srt_connect` will bind them automatically.
* `SRT_ECONNSOCK`: Socket `u` is already connected


```
int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);
```

This function is for developers only and can be used for testing. It does the same as
`srt_connect`, with the exception that it allows to specify the Initial Sequence Number
for data transmission. Normally this value is random-generated.

```
int srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen);
```

Performs a rendezvous connection. This is a shortcut to doing bind locally,
setting `SRTO_RENDEZVOUS` option to true, and doing `srt_connect`. 

* `u`: socket to connect
* `local_name`: specifies the local network interface and port to bind
* `remote_name`: specifies the remote party's IP address and port

REMARKS: The port value shall be the same in `local_name` and `remote_name`.

Options and properties
----------------------

```
int srt_getpeername(SRTSOCKET u, struct sockaddr* name, int* namelen);
```

Retrieves the remote address to which the socket is connected.

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket `u` designates no valid socket ID
* `SRT_ENOCONN`: Socket `u` isn't connected, so there's no remote
address to return

```
int srt_getsockname(SRTSOCKET u, struct sockaddr* name, int* namelen);
```

Extracts the address to which the socket was bound. Although you probably
should know the address that you have used for binding yourself, this function
can be useful to extract the local outgoing port number in case when it was
specified as 0 with binding for system autoselection. With this function
you can extract this port number after it has been autoselected.

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket `u` designates no valid socket ID
* `SRT_ENOCONN`: Socket `u` isn't bound, so there's no local
address to return (**BUG**? It should rather be `SRT_EUNBOUNDSOCK`)

```
int srt_getsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT opt, void* optval, int* optlen);
int srt_getsockflag(SRTSOCKET u, SRT_SOCKOPT opt, void* optval, int* optlen);
```

Gets the value of the given socket option. The first version is to remind the BSD
socket API convention, although the "level" parameter is ignored. The second version
lacks this one ignored parameter.

Options come with various data types, you need to know what data type is assigned
to particular option and pass a variable of appropriate data type to be filled in.

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket `u` designates no valid socket ID
* `SRT_EINVOP`: Option `opt` designates no valid option

```
int srt_setsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT opt, const void* optval, int optlen);
int srt_setsockflag(SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);
```

Sets the option to a socket.  The first version is to remind the BSD
socket API convention, although the "level" parameter is ignored. The second version
lacks this one ignored parameter.

Options come with various data types, you need to know what data type is assigned
to particular option and pass a variable of appropriate data type with the option
value to be set.

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket `u` designates no valid socket ID
* `SRT_EINVOP`: Option `opt` designates no valid option
* Various other errors that may result of problems when setting a specific option
(see option description for details).


Transmission
------------

```
int srt_send(SRTSOCKET u, const char* buf, int len);
int srt_sendmsg(SRTSOCKET u, const char* buf, int len, int ttl/* = -1*/, int inorder/* = false*/);
int srt_sendmsg2(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL *mctrl);
```

Sends a payload to the remote party over a given socket.

Note that the way how this function works is determined by the mode set in
options and it is holding specific requirements:

1. In file/stream mode, the payload is byte-based. You are not required to
mind the size of the data, although they are only guaranteed to be received
in the same order of bytes.

2. In file/message mode, the payload that you send using this function is
exactly a single message that you intend to be received as a whole.

3. In live mode, you are only allowed to send up to the length of
`SRTO_PAYLOADSIZE`, which can't be larger than 1456 bytes (1316 default).



* `u`: Socket used to send. The socket must be connected for this operation.
* `buf`: Points to the buffer containing the payload to send
* `len`: Size of the payload specified in `buf`
* `ttl`: Time (in `[ms]`) to wait for a possibility to send. See description at
`SRT_MSGCTRL::msgttl` field.
* `inorder`: Required to be received in the order of sending. See `SRT_MSGCTRL::inorder`.
* `mctrl`: An object of `SRT_MSGCTRL` type that contains extra parameters, including
`ttl` and `inorder`.

Returns:
* Size of the data sent, if successful. Note that in file/stream mode the
returned size may be less than `len`, which means that it didn't send the
whole contents of the buffer. You would need to call this function again
with the rest of the buffer next time to send it completely. In both
file/message and stream mode the successful return is always equal to `len`
* In case of error, `SRT_ERROR` (-1)

Errors:



```
int srt_recv(SRTSOCKET u, char* buf, int len);
int srt_recvmsg(SRTSOCKET u, char* buf, int len);
int srt_recvmsg2(SRTSOCKET u, char *buf, int len, SRT_MSGCTRL *mctrl);
```

Extract the payload waiting for receving. Note that `srt_recv` and `srt_recvmsg`
are the same function, the name is left for historical reasons.

* `u`: Socket used to send. The socket must be connected for this operation.
* `buf`: Points to the buffer to which the payload is copied
* `len`: Size of the payload specified in `buf`
* `mctrl`: An object of `SRT_MSGCTRL` type that contains extra parameters

There are some differences as to how this function works in particular modes:

1. file/stream mode: Retrieved are as many bytes as possible, that is minimum
of the size of the given buffer and size of the data currently available. Data
available, but not extracted this time will be available next time.
2. file/message mode: Retrieved is exactly one message, with the boundaries
defined at the moment of sending. If some parts of the messages are already
retrieved, but not the whole message, nothing will be received (the function
blocks or returns `SRT_EASYNCRCV`). If the message to be returned does not
fit in the buffer, nothing will be received and the error is reported.
3. live mode: Like in file/message mode, although at most the size of
`SRTO_PAYLOADSIZE` bytes will be retrieved. In this mode, however, with
default settings of `SRTO_TSBPDMODE` and `SRTO_TLPKTDROP`, the message
will be received only when its time to play has come, and until then it
will be kept in the receiver buffer; also when the time to play has come
for a message that is next to the currently lost one, it will be delivered
and the lost one dropped.

Returns:
* Size of the data received, if successful.
* In case of error, `SRT_ERROR` (-1)

Errors:



```
int64_t srt_sendfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
int64_t srt_recvfile(SRTSOCKET u, const char* path, int64_t* offset, int64_t size, int block);
```

These are functions dedicated to sending and receiving a file. You need to call this function
just once for the whole file, although you need to know the size of the file prior to sending
and also define the size of a single block that should be internally retrieved and written
into a file in a single step. This influences only the performance of the internal operations;
from the application perspective you just have one call that exits only when the transmission
is complete.

* `u`: Socket used for transmission. The socket must be connected.
* `path`: Path to the file that should be read or written.
* `offset`: Needed to pass or retrieve the offset used to read or write to a file
* `size`: Size of transfer (file size, if offset is at 0)
* `block`: Size of the single block to read at once before writing it to a file

There are values recommended for `block` parameter:

```
#define SRT_DEFAULT_SENDFILE_BLOCK 364000
#define SRT_DEFAULT_RECVFILE_BLOCK 7280000
```

You need to pass them to `srt_sendfile` or `srt_recvfile` function if you don't know what
value to chose.


Diagnostics
-----------

```
const char* srt_getlasterror_str(void);
```

Get the text message for the last error.


```
int srt_getlasterror(int* errno_loc);
```

Get the numeric code of the error. Additionally, in `errno_loc` there's returned any
value of POSIX `errno` value that was associated with this error (0 if there was no
system error).

```
const char* srt_strerror(int code, int errnoval);
```

Returns a string message that represents given SRT error code and possibly `errno`
value, if not 0.

*REMARK: This function isn't thread safe, it uses a static variable to hold the error
description. There's no problem of using it in a multithreaded environment, just no
other thread but one in the whole application can call this function.*


```
void srt_clearlasterror(void);
```

This function clears the last error. After this call, the `srt_getlasterror` will
report a "successful" code.

Performance tracking
--------------------

```
// perfmon with Byte counters for better bitrate estimation.
int srt_bstats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear);

// permon with Byte counters and instantaneous stats instead of moving averages for Snd/Rcvbuffer sizes.
int srt_bistats(SRTSOCKET u, SRT_TRACEBSTATS * perf, int clear, int instantaneous);
```

Reports the current statistics

* `u`: Socket to get stats from
* `perf`: Pointer to an object to be written with the statistics
* `clear`: 1 if the statis should be cleared after retrieval
* `instantaneous`: 1 if the statistics should use instant data, not moving averages


Asynchronous operations (EPoll)
-------------------------------

```
int srt_epoll_create(void);
```

Creates a new epoll container.

Returns:
* valid EID on success
* -1 on failure

Errors:


```
int srt_epoll_add_usock(int eid, SRTSOCKET u, const int* events);
int srt_epoll_add_ssock(int eid, SYSSOCKET s, const int* events);
int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events);
int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events);
```

This adds a socket to the container.

With `_usock` it adds a user socket (SRT socket), with `_ssock` it adds a
system socket.

The `_add_` functions add this socket anew. The `_update_` functions regard
the fact that the socket is in the container already and just allow to
change the subscription details. For example, if you have subscribed this
socket so far with `SRT_EPOLL_OUT` to wait until it's connected, to change
it into poll for read-readiness, you use this function with the same
socket and use a variable set to `SRT_EPOLL_IN` this time. This will not
only change the event type which is polled on the socket, but also remove
any readiness status for flags that are no longer set.

* `eid`: epoll container id
* `u`: SRT socket
* `s`: system socket
* `events`: points to a variable set to epoll flags, or NULL if
you want to subscribe a socket for all possible events


```
int srt_epoll_remove_usock(int eid, SRTSOCKET u);
int srt_epoll_remove_ssock(int eid, SYSSOCKET s);
```

Removes given socket from epoll container and clears all readiness
state recorded in it for that socket.

With `_usock` it removes a user socket (SRT socket), with `_ssock` it removes a
system socket.

```
int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum, int64_t msTimeOut,
                        SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
```

This function blocks the call until any readiness in the epoll container.

Readiness can be on a socket in the container for the event type as per
subscription. The first readiness state causes this function to exit, but
all ready sockets are reported. This function blocks until the timeout.
If timeout is 0, it exits immediately after checking. If timeout is -1,
blocks indefinitely until the readiness.

* `eid`: epoll container
* `readfds` and `rnum`: Array to write SRT sockets that are read-reday (and its length)
* `writefds` and `wnum`: Array to write SRT sockets that are write-ready (and its length)
* `msTimeOut`: Timeout specified in milliseconds, or special values: 0 or -1
* `lwfds` and `lwnum`: Array to write system sockets that are read-ready (and its length)
* `lwfds` and `lwnum`: Array to write system sockets that are write-ready (and its length)

Note that there is no space here to return erroneous sockets. If an error occurred
on a socket then such a socket is reported in both read-ready and write-ready arrays,
regardless of what event types it was subscribed for. Usually then you subscribe
given socket for only read readiness, for example (`SRT_EPOLL_IN`), but pass both
arrays for read and write readiness. This socket will not be reported in the write
readiness array even if it's write ready, but it will be reported there, if the
operation on this socket encountered an error.


```
int srt_epoll_release(int eid);
```

Deletes the epoll container.

Logging control
---------------

```
void srt_setloglevel(int ll);
```

Sets the loglevel value. Constants for this value are those from `<sys/syslog.h>`
(for Windows there is a replacement at `common/win/syslog_defs.h`), although the
only meaningful are:

* `LOG_DEBUG`: Most detailed and very often messages
* `LOG_NOTICE`: Occasionally displayed information
* `LOG_WARNING`: Unusual behavior
* `LOG_ERR`: Abnormal behavior
* `LOG_CRIT`: Error that makes the current socket no more usable

```
void srt_addlogfa(int fa);
void srt_dellogfa(int fa);
void srt_resetlogfa(const int* fara, size_t fara_size);
```

FA (functional area) is an additional filtering mechanism for logging. You
can select only particular FAs to be turned on, for others the logging
messages will not appear. The list of FAs is collected in `srt.h` file
with `SRT_LOGFA_` prefix. Not enumerating them here because they may get
changed very often.

At least by default all FAs are turned on, except special dangerous ones
(such as `SRT_LOGFA_HAICRYPT`).

```
void srt_setloghandler(void* opaque, SRT_LOG_HANDLER_FN* handler);
```

By default logs are printed to standard error stream. This function replaces
the sending to a stream with a handler function that will receive them.

```
void srt_setlogflags(int flags);
```

When you set a handler, you usually may need to configure what is passed in
the log line so that it doesn't duplicate the information or pass the log
line not exactly in the expected form. These flags are collected in
`logging_api.h` public header:

* `SRT_LOGF_DISABLE_TIME`: Do not provide the time in the header
* `SRT_LOGF_DISABLE_THREADNAME`: Do not provide thread name in the header
* `SRT_LOGF_DISABLE_SEVERITY`: Do not provide severity information in the header
* `SRT_LOGF_DISABLE_EOL`: Do not add the end-of-line character to the log line


