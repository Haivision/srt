# SRT API

The SRT C API (defined in `srt.h` file) is largely based in design on the legacy
UDT API, with some important changes. The `udt.h` file contains the legacy UDT API
plus some minor optional functions that require the C++ standard library to be used.
There are a few optional C++ API functions stored there, as there is no real C++ API
for SRT. These functions may be useful in certain situations.

There are some example applications so that you can see how the API is being used,
including `srt-live-transmit` and `srt-file-transmit`. All SRT related material is contained
in `transmitmedia.*` files in the `apps` directory 
which is used by all applications. See `SrtSource::Read` and `SrtTarget::Write`
as examples of how data are read and written in SRT.

- [Setup and teardown](#setup-and-teardown)
- [Creating and destroying a socket](#creating-and-destroying-a-socket)
  - [Synopsis](#synopsis)
  - [Usage](#usage)
  - [Important Remarks](#important-remarks)
- [Binding and connecting](#binding-and-connecting)
  - [Synopsis](#synopsis)
  - [SRT Usage - listener (server)](#srt-usage---listener-server)
  - [SRT Usage - rendezvous](#srt-usage---rendezvous)
- [Sending and Receiving](#sending-and-receiving)
  - [Synopsis](#synopsis)
  - [Usage](#usage)
  - [Transmission types available in SRT](#transmission-types-available-in-srt)
- [Blocking and Non-blocking Mode](#blocking-and-non-blocking-mode)
- [EPoll (Non-blocking Mode Events)](#epoll-non-blocking-mode-events))
  - [Synopsis](#synopsis)
  - [SRT Usage](#srt-usage)
  - [Transmission types](#transmission-types)
    - [Terminology](#terminology)
  - [Transmission method: Live](#transmission-method-live)
  - [Transmission method: Buffer](#transmission-method-buffer)
  - [Transmission method: Message](#transmission-method-message)

**NOTE**: The socket option descriptions originally contained in this document
have been moved to [APISocketOptions.md](https://github.com/Haivision/srt/blob/master/docs/APISocketOptions.md).

## Setup and teardown

Before any part of the SRT C API can be used, the user should call the `srt_startup()`
function. Likewise, before the application exits, the `srt_cleanup()` function
should be called. Note that one of the things the startup function does is to create
a new thread, so choose the point of execution for these functions carefully.

## Creating and destroying a socket

To do anything with SRT, you first have to create an SRT socket. The term "socket"
in this case is used because of its logical similarity to system-wide sockets.
An SRT socket is not directly related to system sockets, but like a system socket
it is used to define a point of communication.

### Synopsis

```c++
SRTSOCKET srt_create_socket();
int srt_close(SRTSOCKET s);
```

Note that `SRTSOCKET` is just an alias for `int`; this is a legacy naming convention
from UDT, which is here only for clarity.

### Usage

```c++
sock = srt_create_socket();
```

This creates a socket, which can next be configured and then used for communication.

```c++
srt_close(sock);
```

This closes the socket and frees all its resources. Note that the true life of the
socket does not end exactly after this function exits - some details are being
finished in a separate "SRT GC" thread. Still, at least all shared system resources
(such as listener port) should be released after this function exits.

### Important Remarks

1. SRT uses the system UDP protocol as an underlying communication layer, and so
it uses also UDP sockets. The underlying communication layer is used only
instrumentally, and SRT manages UDP sockets as its own system resource as it
pleases - so in some cases it may be reasonable for multiple SRT sockets to share
one UDP socket, or for one SRT socket to use multiple UDP sockets.

2. The term "port" used in SRT is occasionally identical to the term "UDP
port". However SRT offers more flexibility than UDP (or TCP, the more logical
similarity) because it manages ports as its own resources. For example, one port
may be shared between various services.

## Binding and connecting

Connections are established using the same philosophy as TCP, using functions
with names and signatures similar to the BSD Socket API. What is new here is
the _rendezvous_ mode.

### Synopsis

```c++
int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
int srt_bind_acquire(SRTSOCKET u, UDPSOCKET udpsock);
```

This function sets up the "sockname" for the socket, that is, the local IP address
of the network device (use `INADDR_ANY` for using any device) and port. Note that
this can be done on both listening and connecting sockets; for the latter it will
define the outgoing port. If you don't set up the outgoing port by calling this
function (or use port number 0), a unique port number will be selected automatically.

The `*_acquire` version simply takes over the given UDP socket and copies the
bound address setting from it.

```c++
int srt_listen(SRTSOCKET u, int backlog);
```

This sets the backlog (maximum allowed simultaneously pending connections) and
puts the socket into a listening state -- that is, incoming connections will be
accepted in the call `srt_accept`.

```c++
SRTSOCKET srt_accept(SRTSOCKET u, struct sockaddr* addr, int* addrlen);
```

This function accepts the incoming connection (the peer should do
`srt_connect`) and returns a socket that is exclusively bound to an opposite
socket at the peer. The peer's address is returned in the `addr`
argument.

```c++
int srt_connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);
```

This function initiates the connection of a given socket with its peer's counterpart
(the peer gets the new socket for this connection from `srt_accept`). The
address for connection is passed in 'name'. The `connect_debug` version allows
for enforcing the ISN (initial sequence number); this is used only for
debugging or unusual experiments.

```c++
int srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
    const struct sockaddr* remote_name, int remote_namelen);
```

A convenience function that combines the calls to bind, setting the `SRTO_RENDEZVOUS` flag,
and connecting to the rendezvous counterpart. For simplest usage, the `local_name` should
be set to `INADDR_ANY` (or a specified adapter's IP) and port. Note that both `local_name`
and `remote_name` must use the same port. The peer to which this is going to connect
should call the same function, with appropriate local and remote addresses. A rendezvous
connection means that both parties connect to one another simultaneously.

**IMPORTANT**: The connection may fail, but the socket that was used for connecting
is not automatically closed and it's also not in broken state (broken state can be
only if a socket was first successfully connected and then broken). When using blocking
mode, the connection failure will result in reporting an error from this function call.
In non-blocking mode the connection failure is designated by the `SRT_EPOLL_ERR` flag
set for this socket in the epoll container. After that failure you can read an extra
information from the socket using `srt_getrejectreason` function, and then you should
close the socket.

### Listener (Server) Example

```c++
sockaddr_in sa = { ... }; // set local listening port and possibly interface's IP
int st = srt_bind(sock, (sockaddr*)&sa, sizeof sa);
srt_listen(sock, 5);
while ( !finish ) {
    int sa_len = sizeof sa;
    newsocket = srt_accept(sock, (sockaddr*)&sa, &sa_len);
    HandleNewClient(newsocket, sa);
}
```

### Caller (Client) Example

```c++
sockaddr_in sa = { ... }; // set target IP and port

int st = srt_connect(sock, (sockaddr*)&sa, sizeof sa);
HandleConnection(sock);
```

### Rendezvous Example

```c++
sockaddr_in lsa = { ... }; // set local listening IP/port
sockaddr_in rsa = { ... }; // set remote IP/port

srt_setsockopt(m_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes);
int stb = srt_bind(sock, (sockaddr*)&lsa, sizeof lsa);
int stc = srt_connect(sock, (sockaddr*)&rsa, sizeof rsa);
HandleConnection(sock);
```

or simpler

```c++
sockaddr_in lsa = { ... }; // set local listening IP/port
sockaddr_in rsa = { ... }; // set remote IP/port

int stc = srt_rendezvous(sock, (sockaddr*)&lsa, sizeof lsa,
                                (sockaddr*)&rsa, sizeof rsa);
HandleConnection(sock);
```

## Sending and Receiving

The SRT API for sending and receiving is split into three categories: *simple*,
*rich*, and *for files only*.

The **simple API** includes: `srt_send` and `srt_recv` functions. They need only
the socket and the buffer to send from or receive to, just like system `read`
and `write` functions.

The **rich API** includes the `srt_sendmsg` and `srt_recvmsg` functions. Actually
`srt_recvmsg` is provided for convenience and backward compatibility, as it is
identical to `srt_recv`. The `srt_sendmsg` receives more parameters, specifically
for messages. The `srt_sendmsg2` and `srt_recvmsg2` functions receive the socket, 
buffer, and the `SRT_MSGCTRL` object, which is an input-output object specifying 
extra data for the operation.

Functions with the `msg2` suffix use the `SRT_MSGCTRL` object, and have the
following interpretation (except `flags` and `boundary` which are reserved for
future use and should be 0):

- `srt_sendmsg2`:
  - `msgttl`: [IN] maximum time (in ms) to wait for successful delivery (-1: indefinitely)
  - `inorder`: [IN] if false, the later sent message is allowed to be delivered earlier
  - `srctime`: [IN] timestamp to be used for sending (0 if current time)
  - `pktseq`: unused
  - `msgno`: [OUT] message number assigned to the currently sent message

- `srt_recvmsg2`
  - `msgttl`:  unused
  - `inorder`: unused
  - `srctime`: [OUT] timestamp set for this dataset when sending
  - `pktseq`: [OUT] packet sequence number (first packet from the message, if it spans multiple UDP packets)
  - `msgno`: [OUT] message number assigned to the currently received message

Please note that the `msgttl` and `inorder` arguments and fields in `SRT_MSGCTRL`
are meaningful only when you use the message API in file mode (this will be explained
later). In live mode, which is the SRT default, packets are always delivered when
the time comes (always in order), where you don't want a packet to be dropped
before sending (so -1 should be passed here).

The `srctime` parameter is an SRT addition for applications (i.e. gateways)
forwarding SRT streams. It permits pulling and pushing of the sender's original
time stamp, converted to local time and drift adjusted. The `srctime` parameter
is the number of usec (since epoch) in local SRT clock time. If the connection
is not between SRT peers or if **Timestamp-Based Packet Delivery mode (TSBPDMODE)**
is not enabled (see [APISocketOptions.md](https://github.com/Haivision/srt/blob/master/docs/APISocketOptions.md)),
the extracted `srctime` will be 0. Passing `srctime = 0` in `sendmsg` is like using
the API without `srctime` and the local send time will be used (if TSBPDMODE is
enabled and receiver supports it).

### Synopsis

```c++
int srt_send(SRTSOCKET s, const char* buf, int len);
int srt_sendmsg(SRTSOCKET s, const char* buf, int len, int msgttl, bool inorder, uint64_t srctime);
int srt_sendmsg2(SRTSOCKET s, const char* buf, int len, SRT_MSGCTRL* msgctrl);

int srt_recv(SRTSOCKET s, char* buf, int len);
int srt_recvmsg(SRTSOCKET s, char* buf, int len);
int srt_recvmsg2(SRTSOCKET s, char* buf, int len, SRT_MSGCTRL* msgctrl);
```

### Usage

Sending a payload:

```c++
nb = srt_sendmsg(u, buf, nb, -1, true);

nb = srt_send(u, buf, nb);

SRT_MSGCTRL mc = srt_msgctrl_default;
nb = srt_sendmsg2(u, buf, nb, &mc);
```

Receiving a payload:

```c++
nb = srt_recvmsg(u, buf, nb);
nb = srt_recv(u, buf, nb);

SRT_MSGCTRL mc = srt_msgctrl_default;
nb = srt_recvmsg2(u, buf, nb, &mc);
```

### Transmission types available in SRT

Mode settings determine how the sender and receiver functions work. The main
[socket options](APISocketOptions.md) that control it are:

- `SRTO_TRANSTYPE`. Sets several parameters in accordance with the selected
mode:
  - `SRTT_LIVE` (default) the Live mode (for live stream transmissions)
  - `SRTT_FILE` the File mode (for "no time controlled" fastest data transmission)
- `SRTO_MESSAGEAPI`
  - true: (default in Live mode): use Message API
  - false: (default in File mode): use Buffer API

See [Transmission types](#transmission-types) below.

## Blocking and Non-blocking Mode

SRT functions can also work in blocking and non-blocking mode, for which
there are two separate options for sending and receiving: `SRTO_SNDSYN` and
`SRTO_RCVSYN`. When blocking mode is used, a function will not exit until
the availability condition is satisfied. In non-blocking mode the function
always exits immediately, and in case of lack of resource availability, it
returns an error with an appropriate code. The use of non-blocking mode usually
requires using some polling mechanism, which in SRT is **EPoll**.

Note also that the blocking and non-blocking modes apply not only for sending
and receiving. For example, `SNDSYN` defines blocking for `srt_connect` and
`RCVSYN` defines blocking for `srt_accept`. The `SNDSYN` also makes `srt_close`
exit only after the sending buffer is completely empty.

## EPoll (Non-blocking Mode Events)

EPoll is a mechanism to track the events happening on the sockets, both "system
sockets" (see `SYSSOCKET` type) and SRT Sockets. Note that `SYSSOCKET` is also
an alias for `int`, used only for clarity.

### Synopsis

```c++
int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events = NULL);
int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events = NULL);
int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum,
                    int64_t msTimeOut,
                    SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
int srt_epoll_clear_usocks(int eid);
```

### SRT Usage

SRT socket being a user level concept, the system epoll (or other select)
cannot be used to handle SRT non-blocking mode events. Instead, SRT provides a
user-level epoll that supports both SRT and system sockets.

The `srt_epoll_update_{u|s}sock()` API functions described here are SRT additions
to the UDT-derived `srt_epoll_add_{u|s}sock()` and `epoll_remove_{u|s}sock()`
functions to atomically change the events of interest. For example, to remove
`SRT_EPOLL_OUT` but keep `SRT_EPOLL_IN` for a given socket with the existing
API, the socket must be removed from epoll and re-added. This cannot be done
atomically, the thread protection (against the epoll thread) being applied
within each function but unprotected between the two calls. It is then possible
to lose an `SRT_EPOLL_IN` event if it fires while the socket is not in the
epoll list.

Event flags are of various categories: `IN`, `OUT` and `ERR` are events,
which are level-triggered by default and become edge-triggered if combined
with `SRT_EPOLL_ET` flag. The latter is only an edge-triggered flag, not
an event. There's also an `SRT_EPOLL_UPDATE` flag, which is an edge-triggered
only event, and it reports an event on the listener socket that handles socket
group new connections for an already connected group - this is for internal use
only, and it's used in the internal code for socket groups.

Once the subscriptions are made, you can call an SRT polling function
(`srt_epoll_wait` or `srt_epoll_uwait`) that will block until an event
is raised on any of the subscribed sockets. This function will exit as
soon as at least one event is detected or a timeout occurs. The timeout is
specified in `[ms]`, with two special values:

-  0: check and report immediately (don't wait)
- -1: wait indefinitely (not interruptible, even by a system signal)

There are some differences in the synopsis between these two:

#### `srt_epoll_wait`

Both system and SRT sockets can be subscribed. This
function reports events on both socket types according to subscriptions, in
these arrays:

- `readfds` and `lrfds`: subscribed for `IN` and `ERR`
- `writefds` and `lwfds`: subscribed for `OUT` and `ERR`

where:

- `readfds` and `writefds` report SRT sockets ("user" socket)
- `lrfds` and `lwfds` report system sockets

**NOTE**: this function provides no straightforward possibility to report
sockets with an error. If you want to distinguish a report of readiness
for operation from an error report, the only way is to subscribe the
socket in only one direction (either `SRT_EPOLL_IN` or `SRT_EPOLL_OUT`,
but not both) and `SRT_EPOLL_ERR`, and then check the socket's presence
in the array in the direction for which the socket wasn't subscribed. For
example, when an SRT socket is subscribed for `SRT_EPOLL_OUT | SRT_EPOLL_ERR`,
its presence in `readfds` means that an error is reported for it.
This need not be a big problem, because when an error is reported on
a socket, making it appear as if it were ready for an operation, then when that
operation occurs it will simply result in an error. You can use this as an
alternative error check method.

This function also reports an error of type `SRT_ETIMEOUT` when no socket is
ready as the timeout elapses (including 0). This behavior is different in
`srt_epoll_uwait`.

Note that in this function there's a loop that checks for socket readiness
every 10ms. Thus, the minimum poll timeout the function can reliably support,
when system sockets are involved, is also 10ms. The return time from a poll
function can only be quicker when there is an event raised on one of the active
SRT sockets.

### `srt_epoll_uwait`

In this function only the SRT sockets can be subscribed
(it reports error if you pass an epoll id that is subscribed to system sockets).
This function waits for the first event on subscribed SRT sockets and reports all
events collected at that moment in an array with the following structure:

```c++
typedef struct SRT_EPOLL_EVENT_
{
    SRTSOCKET fd;
    int       events;
} SRT_EPOLL_EVENT;

```

Every item reports a single socket with all events as flags.

When the timeout is not -1, and no sockets are ready until the timeout time
passes, this function returns 0. This behavior is different in `srt_epoll_wait`.

The extra `srt_epoll_clear_usocks` function removes all subscriptions from
the epoll container.

The SRT EPoll system does not supports all features of Linux epoll. For
example, it only supports level-triggered events for system sockets.

### Transmission types

SRT was originally intended to be used for Live Streaming and therefore its main
and default transmission type is "live". However, SRT supports the modes that
the original UDT library supported, that is, *file* and *message* transmission.

There are two general modes: **Live** and **File** transmission. Inside File
transmission mode, there are also two possibilities: **Buffer API** and
**Message API**. The Live mode uses Message API. However it doesn't exactly match
the description of the Message API because it uses a maximum single sending buffer
up to the size that fits in one UDP packet.

There are two options to set a particular type:

- `SRTO_TRANSTYPE`: uses the enum value with `SRTT_LIVE` for live mode
   and `SRTT_FILE` for file mode. This option actually changes several parameters
   to their default values for that mode. After this is done, additional parameters,
   including those that are set here, can be further changed.

- `SRTO_MESSAGEAPI`: This sets the Message API (true) or Buffer API (false)

This makes possible a total of three data transmission methods:

- [Live](#transmission-method-live)
- [Buffer](#transmission-method-buffer)
- [Message](#transmission-method-message)

### Terminology

The following terms are used in the description of transmission types:

**HANGUP / RESUME**: These terms have different meanings depending on the blocking
state. They describe how a particular function behaves when performing an operation
requires a specific readiness condition to be satisfied.

In blocking mode HANGUP means that the function blocks until a condition is
satisfied. RESUME means that the condition is satisfied and the function performs
the required operation.

In non-blocking mode the only difference is that HANGUP, instead of blocking, makes
the function exit immediately with an appropriate error code (such as SRT_EASYNC*,
SRT_ETIMEOUT or SRT_ECONGEST) explaining why the function is not ready to perform
the operation. Refer to the error descriptions in [API-funtions.md](API-funtions.md)
for details.

The following types of operations are involved:

1. Reading data: `srt_recv`, `srt_recvmsg`, `srt_recvmsg2`, `srt_recvfile`.
  The function HANGS UP if there are no available data to read, and RESUMES when
  readable data become available (`SRT_EPOLL_IN` flag set in epoll). Use `SRTO_RCVSYN`
  to control blocking mode here.

2. Writing data: `srt_send`, `srt_sendmsg`, `srt_sendmsg2`, `srt_sendfile`.
  The function HANGS UP if the sender buffer becomes full and unable to store
  any additional data, and RESUMES if the data scheduled for sending have been
  removed from the sender buffer (after being sent and acknowledged) and there
  is enough free space in the sender buffer to store data (`SRT_EPOLL_OUT` flag
  set in epoll). Use `SRTO_SNDSYN` to control blocking mode here.

3. Accepting an incoming connection: `srt_accept`
  The function HANGS UP if there are no new connections reporting in, and
  RESUMES when a new connection has been processed and a new socket or group
  has been created to handle it. Note that this function requires the listener
  socket to get the connection (the flag `SRTO_RCVSYN` set on
  the listener socket controls the blocking mode for this operation). Note also
  that the blocking mode for a similar `srt_accept_bond` function is controlled
  exclusively by its timeout parameter because it can work with multiple listener
  sockets, potentially with different settings.

4. Connecting: `srt_connect` and its derivatives
  The function HANGS UP in the beginning, and RESUMES when the socket used for
  connecting is either ready to perform transmission operations or has failed to
  connect. It behaves a little differently in non-blocking mode -- the function
  should be called only once, and it simply returns a success result as a "HANGUP".
  Calling it again with the same socket would be an error. Calling it with a group
  would start a completely new connection. It is only possible to determine whether
  an operation has finished ("has RESUMED") from epoll flags. The socket, when
  successfully connected, would have `SRT_EPOLL_OUT` set, that is, becomes ready
  to send data, and `SRT_EPOLL_ERR` when it failed to connect.

**BLIND / FAST / LATE REXMIT**: BLIND REXMIT is a situation where packets that
were sent are still not acknowledged, either in the expected time frame, or when
another ACK has come for the same number, but no packets have been reported as
lost, or at least not for all still unacknowledged packets. The congestion control
class is responsible for the algorithm for taking care of this situation, which is
either `FASTREXMIT` or `LATEREXMIT`. This will be explained below.

### Transmission method: Live

Setting `SRTO_TRANSTYPE` to `SRTT_LIVE` sets the following [parameters](APISocketOptions.md):

- `SRTO_TSBPDMODE` = true
- `SRTO_RCVLATENCY` = 120
- `SRTO_PEERLATENCY` = 0
- `SRTO_TLPKTDROP` = true
- `SRTO_MESSAGEAPI` = true
- `SRTO_NAKREPORT` = true
- `SRTO_PAYLOADSIZE` = 1316
- `SRTO_CONGESTION` = "live"

In this mode, every call to a sending function is allowed to send only
so much data, as declared by `SRTO_PAYLOADSIZE`, whose value is still
limited to a maximum of 1456 bytes. The application that does the sending
is by itself responsible for calling the sending function in appropriate
time intervals between subsequent calls. By default, this implies that
the receiver uses 120 ms of latency, which is the declared time interval
between the moment when the packet is scheduled for sending at the
sender side, and when it is received by the receiver application (that
is, the data are kept in the buffer and declared as not received, until
the time comes for the packet to "play").

This mode uses the `LiveCC` congestion control class, which puts only a slight
limitation on the bandwidth, if needed (i.e. by adding extra time if the interval
between two consecutive packets would otherwise be too short for the defined speed
limit). Note that it is not intended to work with "virtually infinite" ingest
speeds (such as, for example, reading directly from a file). Therefore the
application is not allowed to stream data with maximum speed -- it must take care
that the speed of data being sent is in rhythm with timestamps in the live stream.
Otherwise the behavior is undefined and might be surprisingly disappointing.

The reading function will always return only a payload that was
sent, and it will HANGUP until the time to play has come for this
packet (if TSBPD mode is on) or when it is available without gaps of
lost packets (if TSBPD mode is off - see [`SRTO_TSBPDMODE`](APISocketOptions.md#SRTO_TSBPDMODE)).

You may wish to tweak some of the parameters below:

- `SRTO_TSBPDMODE`: You can turn off controlled latency if your application uses
its own method of latency control.

- `SRTO_RCVLATENCY`: You can increase the latency time, if this is
too short. Setting a shorter latency than the default is strongly
discouraged, although in some very specific and dedicated networks
this may still be reasonable. Note that `SRTO_PEERLATENCY` is an option
for the sending party, which is the minimum possible value for a receiver.

- `SRTO_TLPKTDROP`: When true (default), this will drop the packets
that haven't been retransmitted on time, that is, before the next packet
that is already received becomes ready to play. You can turn this off to always
ensure a clean delivery. However, a lost packet can simply pause a
delivery for some longer, potentially undefined time, and cause even
worse tearing for the player. Setting higher latency will help much more in
the case when TLPKTDROP causes packet drops too often.

- `SRTO_NAKREPORT`: Turns on repeated sending of loss reports, when the lost
packet was not recovered quickly enough, which raises suspicions that the
loss report itself was lost. Without it, the loss report will be always reported
just once and never repeated again, and then the lost payload packet will
be probably dropped by the TLPKTDROP mechanism.

- `SRTO_PAYLOADSIZE`: Default value is for MPEG TS. If you are going
to use SRT to send any different kind of payload, such as, for example,
wrapping a live stream in very small frames, then you can use a bigger
maximum frame size, though not greater than 1456 bytes.

Parameters from the modified for transmission type list, not mentioned in the
list above, are crucial for Live mode and shall not be changed.

The BLIND REXMIT situation is resolved using the FASTREXMIT algorithm by LiveCC:
sending non-acknowledged packets blindly on the premise that the receiver lingers
too long before acknowledging them. This mechanism isn't used (i.e. the BLIND REXMIT
situation isn't handled at all) when `SRTO_NAKREPORT` is set by the peer -- the
NAKREPORT method is considered so effective that FASTREXMIT isn't necessary.

### Transmission method: Buffer

Setting `SRTO_TRANSTYPE` to `SRTT_FILE` sets the following [parameters](APISocketOptions.md):

- `SRTO_TSBPDMODE` = false
- `SRTO_RCVLATENCY` = 0
- `SRTO_PEERLATENCY` = 0
- `SRTO_TLPKTDROP` = false
- `SRTO_MESSAGEAPI` = false
- `SRTO_NAKREPORT` = false
- `SRTO_PAYLOADSIZE` = 0
- `SRTO_CONGESTION` = "file"

In this mode, calling a sending function is allowed to potentially send
virtually any size of data. The sending function will HANGUP only if the
sending buffer is completely filled, and RESUME if the sending buffers are
available for at least one smallest portion of data passed for sending. The
sending function need not send everything in this call, and the caller must
be aware that the sending function might return sent data of smaller size
than was actually requested.

From the receiving function there will be retrieved as many data as the minimum
of the passed buffer size and available data; data still available and not
retrieved by this call will be available for retrieval in the next call.

There is also a dedicated pair of functions that can only be used in this mode:
`srt_sendfile` and `srt_recvfile`. These functions can be used to transmit the
whole file, or a fragment of it, based on the offset and size.

This mode uses the `FileCC` congestion control class, which is a direct copy of
UDT's `CUDTCC` congestion control class, adjusted to the needs of SRT's
congestion control framework. This class generally sends the data with maximum
speed in the beginning, until the flight window is full, and then keeps the
speed at the edge of the flight window, only slowing down in the case where
packet loss was detected. The bandwidth usage can be directly limited by the
`SRTO_MAXBW` option.

The BLIND REXMIT situation is resolved in FileCC using the LATEREXMIT
algorithm: when the repeated ACK was received for the same packet, or when the
loss list is empty and the flight window is full, all packets since the last
ACK are sent again (that's more or less the TCP behavior, but in contrast to
TCP, this is done as a very low probability fallback).

Most of the parameters described above have `false` or `0` values as they usually
designate features used in Live mode. None are used with File mode. The only option
that makes sense to modify after the `SRTT_FILE` type was set is `SRTO_MESSAGEAPI`,
which is described below.

### Transmission method: Message

Setting `SRTO_TRANSTYPE` to `SRTT_FILE` and then setting `SRTO_MESSAGEAPI` to
`true` implies usage of the Message transmission method. Parameters are set as
described above for the Buffer method, with the exception of `SRTO_MESSAGEAPI`.
The "file" congestion controller is also used in this mode. It differs from the
Buffer method, however, in terms of the rules concerning sending and receiving.

**HISTORICAL INFO**: The library on which SRT was based (UDT) somewhat misleadingly
used the terms `STREAM` and `DGRAM`, and used the system symbols `SOCK_STREAM` and
`SOCK_DGRAM` in the socket creation function. A "datagram" in the UDT terminology
has nothing to do with the "datagram" term in networking terminology, where its
size is limited to as much it can fit in one MTU. In UDT it is actually a message,
which may span multiple UDP packets and has clearly defined boundaries. It's rather
similar to the **SCTP** protocol. Also, in UDP the API functions were strictly bound
to `DGRAM` or `STREAM` mode: `UDT::send/UDT::recv` were only for `STREAM` and
`UDT::sendmsg/UDT::recvmsg` only for `DGRAM`. In SRT this is changed: all functions
can be used in all modes, except `srt_sendfile/srt_recvfile`, and how the functions
actually work is controlled by the `SRTO_MESSAGEAPI` flag.

In message mode, every sending function sends **exactly** as much data as it is
passed in a single sending function call. The receiver also receives not less than
**exactly** the number of bytes that was sent (although every message may have a
different size). Every message may also have extra parameters:

- **TTL** defines how much time (in ms) the message should wait in the sending
buffer for the opportunity to be picked up by the sender thread and sent over
the network; otherwise it is dropped. Note that this TTL only applies to packets that
have been lost and should be retransmitted.

- **INORDER**, when true, means the messages must be read by the receiver in
exactly the same order in which they were sent. In the situation where a message
suffers a packet loss, this prevents any subsequent messages from achieving
completion status prior to recovery of the preceding message.

The sending function will HANGUP when the free space in the sending buffer does
not exactly fit the whole message, and it will only RESUME if the free space in
the sending buffer grows up to this size. The call to the sending function also
returns with an error when the size of the message exceeds the total size of the
buffer (this can be modified by the `SRTO_SNDBUF` option). In other words, it is
not designed to send just a part of the message -- either the whole message is
sent, or nothing at all.

The receiving function will HANGUP until the whole message is available for reading;
if the message spans multiple UDP packets, then the function RESUMES only when
every single packet from the message has been received, including recovered packets,
if any. When the INORDER flag is set to false and parts of multiple messages are
currently available, the first message that is complete (possibly recovered) is
returned. Otherwise the function does a HANGUP until the next message is complete.
The call to the receiving function is rejected if the buffer size is too small
for a single message to fit in it.

Note that you can use any of the sending and receiving functions for sending and
receiving messages, except `sendfile/recvfile`, which are dedicated exclusively
for Buffer API.

For more information, see [APISocketOptions.md](APISocketOptions.md).

[Return to top](#srt-api)
 
