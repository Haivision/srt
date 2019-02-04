SRT API Functions
=================



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

Returns:
* 0, if successfully ran, or when it's started up already
* 1, if this is the first startup, but the GC thread is already running
* -1, if failed

Errors:
* `SRT_ECONNSETUP` (with error code set): Reported when some required system
resources failed to initialize. This is currently used only on Windows to report
a failure from `WSAStartup`.

```
int srt_cleanup(void);
```

This function cleans up all global SRT resources and shall be called just before exitting
the application that uses SRT library.

Returns: 0 (The return value left for possibly be used for something in future)

**IMPORTANT**: Note that the startup/cleanup calls have an instance counter. It means
that you can call startup multiple times, you just need to call the cleanup function
exactly the same number of times.


Creating and configuring socket
-------------------------------

```
SRTSOCKET srt_socket(int af, int type, int protocol);
```

Creates an SRT socket.

* `af`: Family, shall be `AF_INET` or `AF_INET6`
* `type`, `protocol`: ignored

*Note: UDT library used `type` parameter to specify the file or message mode by
stating that `SOCK_STREAM` shall mean a TCP-like file transmission mode and
`SOCK_DGRAM` means an SCTP-like message transmission mode. SRT still does
support these modes, however this is controlled by `SRTO_MESSAGEAPI` socket
option when the transmission type is file (`SRTO_TRANSTYPE` set to `SRTT_FILE`)
and the only sensible value for `type` parameter here is `SOCK_DGRAM`.*

Returns:
* a valid socket ID on success
* `INVALID_SOCKET` (-1) on error

Errors:
* `SRT_ENOTBUF`: not enough memory to allocate required resources

(**BUG**? this is probably a design flaw - usually underlying system errors are
reported by `SRT_ECONNSETUP`).


```
SRTSOCKET srt_create_socket();
```

New and future version of a function to create a socket. Currently it creates
a socket in `AF_INET` family only (See *FUTURE* below for `srt_bind`).

```
int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
```

Binds a socket to a local address and port. Binding specifies the local network
interface and the UDP port number to be used for the socket. When the local address
is a form of `INADDR_ANY`, then it's bound to all interfaces. When the port number
is 0, then the port number will be system-allocated if necessary.

For a listening socket this call is obligatory and it defines the network interface
and the port where the listener should expect a call request. For a connecting socket
this call can set up the outgoing port to be used in the communication. It is allowed
that multiple SRT sockets share one local outgoing port, as long as `SRTO_REUSEADDR`
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
socket is connected or failed due to timeout or interrupted call.
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
how many sockets may be allowed to wait until they are accepted (excessive connection
requests are rejected in advance).

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

Accepts a pending connection with creating a new socket that handles it. The
socket that is connected to a remote party is returned.

* `lsn`: the listener socket previously configured by `srt_listen`
* `addr`: the IP address and port specification for the remote party
* `addrlen`: INPUT: size of `addr` pointed object. OUTPUT: real size of the returned object

Note: `addr` is allowed to be NULL, in which case it's understood as that the
application is not interested with the address from which the connection has come.
Otherwise this should specify an object to be written the address to, and in this
case `addrlen` must also specify a variable to write the object size to.

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

Notes:
1. See *FUTURE* at `srt_bind` about family and `SRT_EINVPARAM` error.
2. The socket used here may be bound from upside so that it used a predefined
network interface or local outgoing port. If not, it behaves as if it was
bound to `INADDR_ANY` (which binds on all interfaces) and port 0 (which
makes the system assign the port automatically).

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

Performs a rendezvous connection. This is a shortcut for doing bind locally,
setting `SRTO_RENDEZVOUS` option to true, and doing `srt_connect`. 

* `u`: socket to connect
* `local_name`: specifies the local network interface and port to bind
* `remote_name`: specifies the remote party's IP address and port

Note: The port value shall be the same in `local_name` and `remote_name`.


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
Specifications you can find in the `apps/socketoptions.hpp` file at the `srt_options`
object declaration.

Returns:
* `SRT_ERROR` (-1) in case of error, otherwise 0

Errors:
* `SRT_EINVSOCK`: Socket `u` designates no valid socket ID
* `SRT_EINVOP`: Option `opt` designates no valid option

```
int srt_setsockopt(SRTSOCKET u, int level /*ignored*/, SRT_SOCKOPT opt, const void* optval, int optlen);
int srt_setsockflag(SRTSOCKET u, SRT_SOCKOPT opt, const void* optval, int optlen);
```

Sets the option to a socket. The first version is to remind the BSD socket API
convention, although the "level" parameter is ignored. The second version lacks
this one ignored parameter.

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


Helper data types for transmission
----------------------------------


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
* `msgttl`: [IN]. Message and Live mode only. The TTL for the message sending,
in `[ms]` (for receiving, unused). The packet is scheduled for sending by this
call and then waits in the sender buffer to be picked up at the moment when all
previously scheduled data are already sent, which may be blocked when the data
are scheduled faster than the network can afford to send. Default -1 means to
wait indefinitely. If specified, then the packet waits for an opportunity for
being sent over the network only up to this time, and then if still not sent,
it's discarded.
* `inorder`: [IN]. Message mode only. Used only for sending. If set, the
message should be extracted by the receiver in the order of sending. This can
be meaningful if a packet loss has happened so particular message must wait for
retransmission so that it can be reassembled and then delivered. When this flag
is false, this message can be delivered even if there are any previous message
still waiting for completion.
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
although it is required that this value remain monotonic in subsequent calls
to sending. Normally message numbers start with 1 and increase with every
message sent.

Helpers for `SRT_MSGCTRL`:

```
void srt_msgctrl_init(SRT_MSGCTRL* mctrl);
const SRT_MSGCTRL srt_msgctrl_default;
```

Helpers for getting an object of `SRT_MSGCTRL` type ready to use. The first is
a function and it fills the object with default values. The second is a constant
object and can be used as a source for assignment. Note that you cannot pass this
constant object into any API function because they require it to be mutable, as
they use some field to output values.


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
exactly a single message that you intend to be received as a whole. In
other words, a single call to this function determines message's boundaries.

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
file/message and live mode the successful return is always equal to `len`
* In case of error, `SRT_ERROR` (-1)

Errors:

* `SRT_ENOCONN`: Socket `u` used for the operation is not connected
* `SRT_ECONNLOST`: Socket `u` used for the operation has lost connection
* `SRT_EINVALMSGAPI`: Incorrect API usage in message mode:
	* Live mode: trying to send at once more bytes than `SRTO_PAYLOADSIZE`
* `SRT_EINVALBUFFERAPI`: Incorrect API usage in stream mode:
	* Currently not in use. The FileSmoother used as the only for stream
	  mode does not restrict the parameters.
* `SRT_ELARGEMSG`: Message to be sent can't fit in the sending buffer (that is,
it exceeds the current total space in the sending buffer in bytes). It means
that the sender buffer is too small, or the application is trying to send
larger message than initially predicted.
* `SRT_EASYNCSND`: There's no free space currently in the buffer to schedule
the payload. This is only reported in non-blocking mode (`SRTO_SNDSYN` set
to false); in blocking mode the call is blocked until the free space in
the sending buffer suffices.
* `SRT_ETIMEOUT`: The condition like above still persists and the timeout
has passed. This is only reported in blocking mode with `SRTO_SNDTIMEO` is
set to a value other than -1.
* `SRT_EPEERERR`: This is reported only in case of sending a stream that is
being received by the peer by `srt_recvfile` function and the writing operation
on the file encountered an error; this is reported by the peer by `UMSG_PEERERROR`
message and the agent sets appropriate flag internally. This flag persists
up to the moment when the connection is broken or closed.


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
* >0 Size of the data received, if successful.
* 0, in case when the connection has been closed
* In case of error, `SRT_ERROR` (-1)

Errors:

* `SRT_ENOCONN`: Socket `u` used for the operation is not connected
* `SRT_ECONNLOST`: Socket `u` used for the operation has lost connection
(this is reported only if the connection was unexpectedly broken, not
when it was closed by the foreign host).
* `SRT_EINVALMSGAPI`: Incorrect API usage in message mode:
	* Live mode: size of the buffer is less than `SRTO_PAYLOADSIZE`
* `SRT_EINVALBUFFERAPI`: Incorrect API usage in stream mode:
	* Currently not in use. The FileSmoother used as the only for stream
	  mode does not restrict the parameters.
* `SRT_EASYNCRCV`: There are no data currently waiting for delivery. This
happens only in non-blocking mode (when `SRTO_RCVSYN` is set to false), in
blocking mode the call is blocked until the data are ready. How this is defined,
depends on the mode:
   * In Live mode (with `SRTO_TSBPDMODE` on), it is expected to have at least
     one whole packet ready, for which the playing time has come
   * In File/Message mode, it is expected to have one full message available,
      * Next waiting one if there are no messages with `inorder` = false
      * Also possibly the first ready message with `inorder` = false
   * In File/Stream mode, it is expected to have at least one byte of data
     still not extracted
* `SRT_ETIMEOUT`: The readiness condition like above is still not achieved and
the timeout has passed. This is only reported in blocking mode with
`SRTO_RCVTIMEO` is set to a value other than -1.


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

Returns:
* >0 Size of the transmitted data of a file. It may be less than `size`, if the size
was greater than the free space in the buffer, in which case you have to send rest of
the file next time.
* -1 in case of error

Errors:

* `SRT_ENOCONN`: Socket `u` used for the operation is not connected
* `SRT_ECONNLOST`: Socket `u` used for the operation has lost connection
* `SRT_EINVALBUFFERAPI`: When socket has `SRTO_MESSAGEAPI` = true or `SRTO_TSBPDMODE` = true
(**BUG**: Looxlike MESSAGEAPI isn't checked)
* `SRT_EINVRDOFF`: There is a mistake in `offset` or `size` parameters, which should match
the index availability and size of the bytes available since `offset` index. This is actually
reported for `srt_sendfile` when the `seekg` or `tellg` operations resulted in error
* `SRT_EINVWROFF`: Like above, reported for `srt_recvfile` and `seekp`/`tellp`.
* `SRT_ERDPERM`: The read from file operation has failed (`srt_sendfile`)
* `SRT_EWRPERM`: The write to file operation has failed (`srt_recvfile`)


Diagnostics
-----------

General notes concerning the "getlasterror" diagnostic functions: when an API
function ends up with error, this error information is stored in a thread-local
storage. This means that you'll get the error of the operation that was last
performed as long as you call this diagnostic function just after the failed
function has returned. In any other situation the information provided by the
diagnostic function is undefined.

```
const char* srt_getlasterror_str(void);
```

Get the text message for the last error.


```
int srt_getlasterror(int* errno_loc);
```

Get the numeric code of the error. Additionally, in `errno_loc` there's returned any
value of POSIX `errno` value that was associated with this error (0 if there was no
system error), in case of Windows it's the value returned by `GetLastError()`.

```
const char* srt_strerror(int code, int errnoval);
```

Returns a string message that represents given SRT error code and possibly `errno`
value, if not 0.

*REMARK: This function isn't thread safe, it uses a static variable to hold the error
description. There's no problem of using it in a multithreaded environment, just no
other thread but one in the whole application may call this function.*


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

The `SRT_TRACEBSTATS` is an alias to `struct CBytePerfMon`. The meaning of most
of the field should be enough comprehensible in the header file comments. Here
are some less obvious fields in this structure (instant measurements):

* `usPktSndPeriod`: sending period. This is the minimum time that must be kept
between two consecutively sent packets over the link used by this socket (note
that sockets sharing one outgoing port use the same underlying UDP socket and
therefore the same link and the same sender queue). In other word, this is the
inversion of maximum sending speed. This isn't the EXACT time distance between
two consecutive sendings because in case when the time spent by the application
between two consecutive sendings exceeds this time, then simply the next packet
will be sent immediately, and additionally the extra wasted time will be
"repaid" at the next sending.

* `pktFlowWindow`: The "flow window" size, it's actually the number of free space
in the peer receiver, as read on the sender, in the number of packets. When this
value drops to zero, the next sending packet will be simply dropped by the receiver
without processing. In the file mode this may cause slowdown of sending in order
to wait until the receiver clears things up; in live mode the receiver buffer
should normally occupy not more than half of the buffer size (default 8192).
If this size is less than this half and declines, it means that the receiver
cannot process the incoming stream fast enough and this may in perspective lead
to a dropped connection.

* `pktCongestionWindow`: The "congestion window" in packets. In File mode this
value starts with 16 and is increased with every number of reported
acknowledged packets, then also updated basing on the receiver-reported
delivery rate. It represents the maximum number of packets that can be safely
sent now without causing congestion. The higher this value, the faster the
packets can be sent. In Live mode this field is not really in use.

* `pktFlightSize`: Number of packets in flight. This is the distance between
the packet sequence number that was last reported by ACK message and the
sequence number of the packet just sent. Note that ACK gets received
periodically, so this value is most accurate just after receiving ACK and
becomes a little exaggerated in time until the next ACK comes. (**BUG**
or improvement required: it makes little sense to calculate this value
basing on the current sent-out sequence; what really counts is the distance
between the ACK-ed sequence and the sent-out sequence at the very moment
when ACK comes, not at any moment).

* `msRTT`: The RTT (Round-Trip time), it's the sum of two STT (Single-Trip
time) values, one from agent to peer and one from peer to agent. Beware that
the measurement method is different than on TCP; SRT measures only the "reverse
RTT", that is, the time measured at the receiver as between sending `UMSG_ACK`
message until receiving the sender-responded `UMSG_ACKACK` message with the
same journal. This theoretically shouldn't, but still happens to be a little
different to "forward RTT", that is, the time between sending a data packet of
particular sequence number and receiving `UMSG_ACK` with that sequence number
later by 1, as it's being measured on TCP. The "forward RTT" isn't being
measured nor reported in SRT.

* `mbpsBandwidth`: The bandwidth, in Mb/s. This is measured at the receiver
and sent back to the sender. This is using running average calculation at
the receiver side.

* `byteAvailSndBuf`: Bytes available in the sender buffer. It decreases with
data scheduled for sending by the application and increases with every ACK
received from the receiver, after the packets are sent over the UDP link.

* `byteAvailRcvBuf`: Bytes available in the receiver buffer.

* `mbpsMaxBW`: Usually this is the setting from `SRTO_MAXBW` option, including
value 0 (unlimited). Might be that under certain conditions a nonzero value can
be provided by appropriate Smoother, although none of builtin Smoothers currently
uses it.

* `byteMSS`: Same as a value from `SRTO_MSS` option.

* `pktSndBuf`: Number of packets in the sending buffer, that is, already scheduled
for sending and possibly sent, but not yet acknowledged.

* `byteSndBuf`: Same as above, in bytes

* `msSndBuf`: Same as above, but expressed as a time distance between the
oldest and the latest packet scheduled for sending

* `msSndTsbPdDelay`: If `SRTO_TSBPDMODE` is on (default for Live mode), it returns
the value of `SRTO_PEERLATENCY`, otherwise 0.

* `pktRcvBuf`: Number of packets in the receiver buffer.  Note that in
the Live mode (with turned on `SRTO_TSBPDMODE`, default) some packets must stay
in the buffer and will not be signed off to the application until the "time to
play" comes. In File mode it directly means that all that is above 0 can (and
shall) be read right now.

* `byteRcvBuf`: Like above, in bytes.
* `msRcvBuf`: Time distance between the first and last available packet in the
receiver buffer. Note that this range includes all packets regardless if they
are ready to play or not.

* `msRcvTsbPdDelay`: If `SRTO_TSBPDMODE` is on (default for Live mode), it returns
the value of `SRTO_RCVLATENCY`, otherwise 0.


Asynchronous operations (EPoll)
-------------------------------

The epoll system is currently the only method for using multiple sockets in one
thread with having the blocking operation moved to epoll waiting so that it can
block on multiple sockets at once.

The epoll system, similar to the one on Linux, relies on `eid` objects managed
internally in SRT, which can be subscribed to particular sockets and the readiness
status of particular operations, then the `srt_epoll_wait` function can be used
to block until any readiness status in the whole `eid` is set.


```
int srt_epoll_create(void);
```

Creates a new epoll container.

Returns:
* valid EID on success
* -1 on failure

Errors:
* `SRT_ECONNSETUP`: System operation failed. This is on systems that use
a special method for system part of epoll and therefore associated resources,
like epoll on Linux.


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

Return: 0, if successful, otherwise -1

Errors:

* `SRT_EINVPOLLID`: `eid` designates no valid EID object

**BUG**: for `add_ssock` the system error results in an empty `CUDTException()`
call which actually results in `SRT_SUCCESS`. For cases like that the
`SRT_ECONNSETUP` code is predicted.


```
int srt_epoll_remove_usock(int eid, SRTSOCKET u);
int srt_epoll_remove_ssock(int eid, SYSSOCKET s);
```

Removes given socket from epoll container and clears all readiness
state recorded in it for that socket.

With `_usock` it removes a user socket (SRT socket), with `_ssock` it removes a
system socket.

Return: 0, if successful, otherwise -1

Errors:

* `SRT_EINVPOLLID`: `eid` designates no valid EID object


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

Return:
* >0 number of ready sockets (of whatever kind), if any were ready
* -1 in case of error

Errors:

* `SRT_EINVPOLLID`: `eid` designates no valid EID object
* `SRT_ETIMEOUT`: Up to `msTimeOut` no sockets subscribed in `eid` were ready.
This is reported only if `msTimeOut` was >=0, otherwise the function is waiting
indefinitely.

```
int srt_epoll_release(int eid);
```

Deletes the epoll container.

Return:
* >0 number of ready sockets (of whatever kind), if any were ready
* -1 in case of error

Errors:

* `SRT_EINVPOLLID`: `eid` designates no valid EID object

Logging control
---------------

SRT has a widely used system of logs, as this is usually the only way to determine
how the internals are working, without changing the rules by the fact of tracing.
Logs are split into levels (5 levels out of those defined by syslog are in use)
and additional filtering is possible on FA (functional area). By default only
up to Note log level are displayed and from all FAs. The logging can be only
manipulated globally with no regard to a particular socket; this would be rather
impossible because lots of areas in SRT do not work dedicated for any particular
socket, and some are shared between sockets.

```
void srt_setloglevel(int ll);
```

Sets the minimum severity for logging. Minimum, that is, particular log is
displayed only if it is the same or more severe than the set value. Setting
this value to `LOG_DEBUG` turns on all other levels, for example.

Constants for this value are those from `<sys/syslog.h>`
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


