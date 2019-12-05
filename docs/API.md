
The SRT C API (defined in `srt.h` file) is largely based in design on the
legacy UDT API, with some important changes. The API contained in
`udt.h` file contains the legacy UDT API plus some minor optional
functions that require the C++ standard library to be used. There are a few
optional C++ API functions stored there, as there is no real C++ API for SRT.
These functions may be useful in certain situations.

There are some example applications so that you can see how the API is being
used, including srt-live-transmit, srt-file-transmit and srt-multiplex. 
All SRT related material is contained in `transmitmedia.*`
files in the `common` directory which is used by all applications.
See SrtSource::Read and SrtTarget::Write as examples of how data are 
read and written in SRT.

Setup and teardown
==================

Before any part of the SRT C API can be used, the user should call `srt_startup()`
function. Likewise, before the application exits, the `srt_cleanup()` function
should be called. Note that one of the things the startup function does is
to create a new thread, so choose the point of execution for these functions
carefully.

Creating and destroying a socket
================================

To do anything with SRT, you have to create an SRT socket first.
The term "socket" in this case is used because of its logical similarity to system-wide 
sockets. An SRT socket is not directly related to system sockets, but like a system socket 
it is used to define a point of communication.

Synopsis
--------

    SRTSOCKET srt_socket(int af, int, int);
    int srt_close(SRTSOCKET s);

The `srt_socket` function is based on the legacy UDT API except
the first parameter. The other two are ignored.

Note that `SRTSOCKET` is just an alias for `int`; this is a legacy naming convention
from UDT, which is here only for clarity.

Usage
-----

    sock = srt_socket(AF_INET, SOCK_DGRAM, 0);

This creates a socket, which can next be configured and then used for communication.

    srt_close(sock);

This closes the socket and frees all its resources. Note that the true life of the
socket does not end exactly after this function exits - some details are being
finished in a separate "SRT GC" thread. Still, at least all shared system resources 
(such as listener port) should be released after this function exits.


Important Remarks
-----------------

1. Please note that the use of SRT with `AF_INET6` has not been fully tested; 
use at your own risk.
2. SRT uses the system UDP protocol as an underlying communication
layer, and so it uses also UDP sockets. The underlying communication layer is
used only instrumentally, and SRT manages UDP sockets as its own system resource
as it pleases - so in some cases it may be reasonable for multiple SRT sockets to share 
one UDP socket, or for one SRT socket to use multiple UDP sockets.
3. The term "port" used in SRT is occasionally identical to the term "UDP
port". However SRT offers more flexibility than UDP (or TCP, if we think about
the more logical similarity) because it manages ports as its own resources. For
example, one port may be shared between various services.


Binding and connecting
======================

Connections are established using the same philosophy as TCP,
using functions with names and signatures similar to the BSD Socket
API. What is new here is the _rendezvous_ mode.

Synopsis
--------

    int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
    int srt_bind_peerof(SRTSOCKET u, UDPSOCKET udpsock);

This function sets up the "sockname" for the socket, that is, the local IP address
of the network device (use `INADDR_ANY` for using any device) and port. Note that
this can be done on both listening and connecting sockets; for the latter it will
define the outgoing port. If you don't set up the outgoing port by calling this
function (or use port number 0), a unique port number will be selected automatically.

The `*_peerof` version simply copies the bound address setting from an existing UDP
socket.

    int srt_listen(SRTSOCKET u, int backlog);

This sets the backlog (maximum allowed simultaneously pending connections) and
puts the socket into listening state -- that is, incoming connections will be
accepted in the call `srt_accept`.

    SRTSOCKET srt_accept(SRTSOCKET u, struct sockaddr* addr, int* addrlen);

This function accepts the incoming connection (the peer should do
`srt_connect`) and returns a socket that is exclusively bound to an opposite
socket at the peer. The peer's address is returned in the `addr`
argument.

    int srt_connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
    int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);

This function initiates the connection of a given socket with its peer's counterpart
(the peer gets the new socket for this connection from `srt_accept`). The
address for connection is passed in 'name'. The `connect_debug` version allows
for enforcing the ISN (initial sequence number); this is used only for
debugging or unusual experiments.

    int srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen);

A convenience function that combines the calls to bind, setting the `SRTO_RENDEZVOUS` flag,
and connecting to the rendezvous counterpart. For simplest usage, the `local_name` should
be set to `INADDR_ANY` (or a specified adapter's IP) and port. Note that both `local_name`
and `remote_name` must use the same port. The peer to which this is going to connect
should call the same function, with appropriate local and remote addresses. A rendezvous
connection means that both parties connect to one another simultaneously.



SRT Usage - listener (server)
-----------------------------

        sockaddr_in sa = { ... }; // set local listening port and possibly interface's IP
        int st = srt_bind(sock, (sockaddr*)&sa, sizeof sa);
        srt_listen(sock, 5);
        while ( !finish ) {
           socklen_t sa_len = sizeof sa;
           newsocket = srt_accept(sock, (sockaddr*)&sa, &sa_len);
           HandleNewClient(newsocket, sa);
        }

SRT Usage - caller (client)
---------------------------

        sockaddr_in sa = { ... }; // set target IP and port

        int st = srt_connect(sock, (sockaddr*)&sa, sizeof sa);
        HandleConnection(sock);


SRT Usage - rendezvous
----------------------

        sockaddr_in lsa = { ... }; // set local listening IP/port
        sockaddr_in rsa = { ... }; // set remote IP/port

        srt_setsockopt(m_sock, 0, SRTO_RENDEZVOUS, &yes, sizeof yes);
        int stb = srt_bind(sock, (sockaddr*)&lsa, sizeof lsa);
        int stc = srt_connect(sock, (sockaddr*)&rsa, sizeof rsa);
        HandleConnection(sock);

or simpler

        sockaddr_in lsa = { ... }; // set local listening IP/port
        sockaddr_in rsa = { ... }; // set remote IP/port

        int stc = srt_rendezvous(sock, (sockaddr*)&lsa, sizeof lsa,
                                       (sockaddr*)&rsa, sizeof rsa);
        HandleConnection(sock);


Sending and Receiving
=====================

The SRT API for sending and receiving is split into three categories: simple,
rich, and for files only.

The simple API includes: `srt_send` and `srt_recv` functions. They need only
the socket and the buffer to send from or receive to, just like system `read`
and `write` functions.

The rich API includes the `srt_sendmsg` and `srt_recvmsg` functions. Actually
`srt_recvmsg` is provided for convenience and backward compatibility, as it is
identical to `srt_recv`. The `srt_sendmsg` receives more parameters, specifically
for messages. The `srt_sendmsg2` and `srt_recvmsg2` functions receive the socket, buffer, 
and the `SRT_MSGCTRL` object, which is an input-output
object specifying extra data for the operation.

Functions with the `msg2` suffix use the `SRT_MSGCTRL` object, and have the
following interpretation (except `flags` and `boundary` that are reserved for
future use and should be 0):

* `srt_sendmsg2`:
    * msgttl: [IN] maximum time (in ms) to wait for successful delivery (-1: indefinitely)
    * inorder: [IN] if false, the later sent message is allowed to be delivered earlier
    * srctime: [IN] timestamp to be used for sending (0 if current time)
    * pktseq: unused
    * msgno: [OUT]: message number assigned to the currently sent message

* `srt_recvmsg2`
    * msgttl, inorder: unused
    * srctime: [OUT] timestamp set for this dataset when sending
    * pktseq: [OUT] packet sequence number (first packet from the message, if it spans multiple UDP packets)
    * msgno: [OUT] message number assigned to the currently received message

Please note that the `msgttl` and `inorder` arguments and fields in
`SRT_MSGCTRL` are meaningful only when you use the message API in file mode
(this will be explained later). In live mode, which is the SRT default, packets
are always delivered when the time comes (always in order), where you don't want a packet 
to be dropped before sending (so -1 should be passed here).

The `srctime` parameter is an SRT addition for applications (i.e. gateways)
forwarding SRT streams. It permits pulling and pushing of the sender's original time
stamp, converted to local time and drift adjusted. The srctime parameter is the
number of usec (since epoch) in local time. If the connection is not between
SRT peers or if Timestamp-Based Packet Delivery mode (TSBPDMODE) is not enabled
(see Options), the extracted srctime will be 0. Passing srctime = 0 in sendmsg
is like using the API without srctime and the local send time will be used (if
TSBPDMODE is enabled and receiver supports it).


Synopsis
--------

    int srt_send(SRTSOCKET s, const char* buf, int len);
    int srt_sendmsg(SRTSOCKET s, const char* buf, int len, int msgttl, bool inorder, uint64_t srctime);
    int srt_sendmsg2(SRTSOCKET s, const char* buf, int len, SRT_MSGCTRL* msgctrl);

    int srt_recv(SRTSOCKET s, char* buf, int len);
    int srt_recvmsg(SRTSOCKET s, char* buf, int len);
    int srt_recvmsg2(SRTSOCKET s, char* buf, int len, SRT_MSGCTRL* msgctrl);

Usage
-----

Sending a payload:

    nb = srt_sendmsg(u, buf, nb, -1, true);

    nb = srt_send(u, buf, nb);

    SRT_MSGCTL mc = srt_msgctl_default;
    nb = srt_sendmsg2(u, buf, nb, &mc);


Receiving a payload:

    nb = srt_recvmsg(u, buf, nb);
    nb = srt_recv(u, buf, nb);

    SRT_MSGCTL mc = srt_msgctl_default;
    nb = srt_recvmsg2(u, buf, nb, &mc);


Transmission Modes
------------------

Mode settings determine how the sender and receiver functions work.
The main socket options (see below for full description) that control it are:

* `SRTO_TRANSTYPE`. Sets several parameters in accordance with the selected
mode:
    * `SRTT_LIVE` (default) the Live mode (for live stream transmissions)
    * `SRTT_FILE` the File mode (for "no time controlled" fastest data transmission)
* `SRTO_MESSAGEAPI`
    * true: (default in Live mode): use Message API
    * false: (default in File mode): use Buffer API

We have then three cases (note that Live mode implies Message API):

* Live mode (default)

  In this mode, the application is expected to send single pieces of data
  that are already under sending speed control. Default size is 1316, which
  is 7 * 188 (MPEG TS unit size). With default settings in this mode, the
  receiver will be delivered payloads with the same time distances between them
  as when they were sent, with a small delay (default 120 ms).

* File mode, Buffer API (default when set `SRTT_FILE` mode)

  In this mode the application may deliver data with any speed and of any size. 
  The facility will try to send them as long as there is buffer space for it.
  A single call for sending may send only fragments of the buffer, and the
  receiver will receive as much as is available and fits in the buffer.

* File mode, Message API (when `SRTO_TRANSTYPE` is `SRTT_FILE` and `SRTO_MESSAGEAPI` is true)

  In this mode the application delivers single pieces of data that have
  declared boundaries. The sending is accepted only when the whole message can
  be scheduled for sending, and the receiver will be given either the whole
  message, or nothing at all, including when the buffer is too small for the
  whole message.

The File mode and its Buffer and Message APIs are derived from UDT, just
implemented in a slightly different way. This will be explained below in
**HISTORICAL INFO** under "Transmission Method: Message".


Blocking and Non-blocking Mode
==============================

SRT functions can also work in blocking and non-blocking mode, for which
there are two separate options for sending and receiving: `SRTO_SNDSYN` and
`SRTO_RCVSYN`. When blocking mode is used, a function will not exit until
the availability condition is satisfied; in non-blocking mode the function 
always exits immediately, and in case of lack of resource availability, it
returns an error with appropriate code. The use of non-blocking mode usually
requires using some polling mechanism, which in SRT is **EPoll**.

Note also that the blocking and non-blocking modes apply not only for sending
and receiving. For example, SNDSYN defines blocking for `srt_connect` and
RCVSYN defines blocking for `srt_accept`. The SNDSYN also makes `srt_close`
exit only after the sending buffer is completely empty.


EPoll (Non-blocking Mode Events)
================================

EPoll is a mechanism to track the events happening on the sockets, both "system
sockets" (see `SYSSOCKET` type) and SRT Sockets. Note that `SYSSOCKET` is also
an alias for `int`, used only for clarity.


Synopsis
--------

    int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events = NULL);
    int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events = NULL);
    int srt_epoll_wait(int eid, SRTSOCKET* readfds, int* rnum, SRTSOCKET* writefds, int* wnum,
                        int64_t msTimeOut,
                        SYSSOCKET* lrfds, int* lrnum, SYSSOCKET* lwfds, int* lwnum);
    int srt_epoll_uwait(int eid, SRT_EPOLL_EVENT* fdsSet, int fdsSize, int64_t msTimeOut);
    

SRT Usage
---------

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

Once the subscriptions are made, you can call an SRT polling function
(`srt_epoll_wait` or `srt_epoll_uwait`) that will block until an event
is raised on any of the subscribed sockets. This function will exit as
soon as st least one event is deteted or a timeout occurs. The timeout is
specified in `[ms]`, with two special values:

 - 0: check and report immediately (don't wait)
 - -1: wait indefinitely (not interruptable, even by a system signal)

There are some differences in the synopsis between these two:

1. `srt_epoll_wait`: Both system and SRT sockets can be subscribed. This
function reports events on both socket types according to subscriptions, in
these arrays:

    - `readfds` and `lrfds`: subscribed for `IN` and `ERR`
    - `writefds` and `lwfds`: subscribed for `OUT` and `ERR`

where:

    - `readfds` and `writefds` report SRT sockets ("user" socket)
    - `lrfds` and `lwfds` report system sockets

Note: this function provides no straightforward possibility to report
sockets with an error. If you want to distinguish a report of readiness
for operation from an error report, the only way is to subscribe the
socket in only one direction (either `SRT_EPOLL_IN` or `SRT_EPOLL_OUT`,
but not both) and `SRT_EPOLL_ERR`, and then check the socket's presence
in the array for which's direction the socket wasn't subscribed (for
example, when an SRT socket is subscribed for `SRT_EPOLL_OUT | SRT_EPOLL_ERR`,
its presence in `readfds` means that an error is reported for it).
This need not be a big problem because when an error is reported on
a socket, an appearance as if it were ready for an operation, followed
by doing this operation, will simply result in an error from that
operation, so you can use it also as an alternative error check method.

This function also reports error of type `SRT_ETIMEOUT` when no socket is
ready as the timeout elapses (including 0). This behavior is different in
`srt_epoll_uwait`.

Note that in this function there's a loop that checks for socket readiness
every 10ms. Thus, the minimum poll timeout the function can reliably support,
when system sockets are involved, is also 10ms. The return time from a poll
function can only be quicker when there is an event raised on one of the active
SRT sockets.


2. `srt_epoll_uwait`: In this function only the SRT sockets can be subscribed
(it reports error if you pass an epoll id that is subscribed to system sockets).
This function waits for the first event on subscribed SRT socket and reports all
events collected at this moment in an array of this structure:

```
typedef struct SRT_EPOLL_EVENT_
{
    SRTSOCKET fd;
    int       events;
} SRT_EPOLL_EVENT;

```

Every item reports a single socket with all events as flags.

When the timeout is not -1, and no sockets are ready until the timeout time
passes, this function returns 0. This behavior is different in `srt_epoll_wait`.

The SRT EPoll system does not supports all features of Linux epoll. For
example, it only supports level-triggered events for system sockets.

Options
=======

There's a general method of setting options on a socket in the SRT C API, similar
to the system setsockopt/getsockopt functions.

Synopsis
--------

Legacy version:

    int srt_getsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, void* optval, int& optlen);
    int srt_setsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, const void* optval, int optlen);

New version:

    int srt_getsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, void* optval, int& optlen);
    int srt_setsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, const void* optval, int optlen);

(In the legacy version, there's an additional unused `level` parameter. It was there
in the original UDT API just to mimic the system `setsockopt` function).

Some options require a value of type bool and some others of type int, which is
not the same -- they differ in size, and mistaking them may end up with a crash.
This must be kept in mind especially in any C wrapper. For convenience, the
setting option function may accept both `int` and `bool` types, but this is
not so in the case of getting an option value.

Almost all options from the UDT library are derived (there are a few deleted, including
some deprecated already in UDT), many new SRT options have been added.
All options are available exclusively with the `SRTO_` prefix. Old names are provided as
alias names in the `udt.h` legacy/C++ API file. Note the translation rules:
* `UDT_` prefix from UDT options was changed to the prefix `SRTO_`
* `UDP_` prefix from UDT options was changed to the prefix `SRTO_UDP_`
* `SRT_` prefix in older SRT versions was changed to `SRTO_`

The Binding column should define for these options one of the following
statements concerning setting a value:

* pre: For connecting a socket it must be set prior to calling `srt_connect()`
and never changed thereafter. For a listener socket it should be set to a binding
socket and it will be derived by every socket returned by `srt_accept()`.
* post: This flag can be changed any time, including after the socket is
connected. On binding a socket setting this flag is effective only on this
socket itself. Note though that there are some post-bound options that have
important meaning when set prior to connecting.



This option list is sorted alphabetically. Note that some options can be
either only a retrieved (GET) or specified (SET) value.


| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_CONNTIMEO`      | 1.1.2 | pre     | `int` | msec   | 3000     | tbd    |

- Connect timeout. SRT cannot connect for RTT > 1500 msec (2 handshake exchanges) 
with the default connect timeout of 3 seconds. This option applies to the caller 
and rendezvous connection modes. The connect timeout is 10 times the value set 
for the rendezvous mode (which can be used as a workaround for this connection 
problem with earlier versions)

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_EVENT`      |       | n/a     | `int32_t` |        | n/a      | n/a    |

- **[GET]** - Returns bit flags set according to the current active events on 
the socket. 
- Possible values are those defined in `SRT_EPOLL_OPT` enum (a combination of 
`SRT_EPOLL_IN`, `SRT_EPOLL_OUT` and `SRT_EPOLL_ERR`).

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_FC`             |       | pre     | `int` | pkts   | 25600    | 32..   |

- Flight Flag Size (maximum number of bytes that can be sent without 
being acknowledged)

---

| OptName          | Since | Binding | Type      | Units   | Default  | Range  |
| ---------------- | ----- | ------- | --------- | ------- | -------- | ------ |
| `SRTO_INPUTBW`   | 1.0.5 | post    | `int64_t` | bytes/s | 0        | 0..    |

- This option is effective only if `SRTO_MAXBW` is set to 0 (relative). It
controls the maximum bandwidth together with `SRTO_OHEADBW` option according
to the formula: `MAXBW = INPUTBW * (100 + OHEADBW) / 100`. When this option
is set to 0 (automatic) then the real INPUTBW value will be estimated from
the rate of the input (cases when the application calls the `srt_send*`
function) during transmission. 

- *Recommended: set this option to the predicted bitrate of your live stream
and keep default 25% value for `SRTO_OHEADBW`.

---

| OptName               | Since | Binding | Type      | Units  | Default            | Range  |
| --------------------- | ----- | ------- | --------- | ------ | ------------------ | ------ |
| `SRTO_IPTOS`          | 1.0.5 | pre     | `int32_t` |        | (platform default) | 0..255 |

- IPv4 Type of Service (see IP_TOS option for IP) or IPv6 Traffic Class (see IPV6_TCLASS
of IPv6) depending on socket address family. Applies to sender only. 
- *Sender: user configurable, default: 0xB8*

---

| OptName               | Since | Binding | Type      | Units    | Default  | Range  |
| --------------------- | ----- | ------- | --------- | -------- | -------- | ------ |
| `SRTO_ISN`            | 1.3.0 | post    | `int32_t` | sequence | n/a      | n/a    |

- **[GET]** - The value of the ISN (Initial Sequence Number), which is the first 
sequence number put on a firstmost sent UDP packets carrying SRT data payload. 
*This value is useful for developers of some more complicated methods of flow 
control, possibly with multiple SRT sockets at a time, not predicted in any 
regular development.*

---

| OptName               | Since | Binding | Type      | Units  | Default            | Range  |
| --------------------- | ----- | ------- | --------- | ------ | ------------------ | ------ |
| `SRTO_IPTTL`          | 1.0.5 | pre     | `int32_t` | hops   | (platform default) | 1..255 |

- IPv4 Time To Live (see `IP_TTL` option for IP) or IPv6 unicast hops (see
`IPV6_UNICAST_HOPS` for IPV6) depending on socket address family. Applies to sender only. 
- *Sender: user configurable, default: 64*

---

| OptName           | Since | Binding | Type  | Units | Default            | Range |
| ----------------- | ----- | ------- | ----- | ----- | ------------------ | ------|
| `SRTO_IPV6ONLY`   | 1.4.0 | pre     | `int` | n/a   | (platform default) | -1..1 |

- **[GET or SET]** - Set system socket flag IPV6ONLY. When set to 0 a listening
socket binding an IPv6 address accepts also IPv4 clients (their addresses will be 
formatted as IPv4-mapped IPv6 addresses). By default (-1) this option is not set 
and the platform default value is used.

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range  |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_KMREFRESHRATE`  | 1.3.2 | pre     | `int32_t` | pkts   | 0x1000000| 0..unlimited |

- **[GET or SET]** - The number of packets to be transmitted after which the Stream
Encryption Key (SEK), used to encrypt packets, will be switched to the new one.
Note that the old and new keys live in parallel for a certain period of time
(see `SRTO_KMPREANNOUNCE`) before and after the switchover.

Having a preannounce period before switchover ensures the new SEK is installed
at the receiver before the first packet encrypted with the new SEK is received.
The old key remains active after switchover in order to decrypt packets that
might still be in flight, or packets that have to be retransmitted.

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range  |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_KMPREANNOUNCE`  | 1.3.2 | pre     | `int32_t` | pkts   | 0x1000 | see below |

- **[GET or SET]** - The interval (defined in packets) between when a new
  Stream Encrypting Key (SEK) is sent and when switchover occurs. This value
also applies to the subsequent interval between when switchover occurs and when
the old SEK is decommissioned.

At `SRTO_KMPREANNOUNCE` packets before switchover the new key is sent
(repeatedly, if necessary, until it is confirmed by the receiver).

At the switchover point (see `SRTO_KMREFRESHRATE`), the sender starts
encrypting and sending packets using the new key. The old key persists in case
it is needed to decrypt packets that were in the flight window, or
retransmitted packets.

The old key is decommissioned at `SRTO_KMPREANNOUNCE` packets after switchover.

The allowed range for this value is between 1 and half of the current value of
`SRTO_KMREFRESHRATE`. The minimum value should never be less than the flight
window (i.e. the number of packets that have already left the sender but have
not yet arrived at the receiver).

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range  |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_KMSTATE`        | 1.0.2 | n/a     | `int32_t` |        | n/a      | n/a    |

- **[GET]** - Keying Material state. This is a legacy option that is equivalent 
to `SRTO_SNDKMSTATE`, if the socket has set `SRTO_SENDER` to true, and 
`SRTO_RCVKMSTATE` otherwise. This option shall not be used if the application 
meant to use the versions at least 1.3.0 and does not use the `SRTO_SENDER` flag.

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range         |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------------- |
| `SRTO_LATENCY`        | 1.0.2 | pre     | `int32_t` | msec   | 0        | positive only |

- This flag sets both `SRTO_RCVLATENCY` and `SRTO_PEERLATENCY` to the same value. 
Note that prior to version 1.3.0 this is the only flag to set the latency, however 
this is effectively equivalent to setting `SRTO_PEERLATENCY`, when the side is 
sender (see `SRTO_SENDER`) and `SRTO_RCVLATENCY` when the side is receiver, and 
the bidirectional stream sending in version 1.2.0is not supported.

---

| OptName              | Since | Binding | Type   | Units  | Default  | Range  |
| -------------------- | ----- | ------- | ------ | ------ | -------- | ------ |
| `SRTO_LINGER`        |       | pre     | linger | secs   | on (180) |        |

- Linger time on close (see [SO\_LINGER](http://man7.org/linux/man-pages/man7/socket.7.html)).
- *SRT recommended value: off (0)*.

---

| OptName            | Since | Binding | Type  | Units   | Default  | Range      |
| ------------------ | ----- | ------- | ----- | ------- | -------- | ---------- |
| `SRTO_LOSSMAXTTL`  | 1.2.0 | pre     | `int` | packets | 0        | reasonable |

- **[GET or SET]** - The value up to which the *Reorder Tolerance* may grow. When 
*Reorder Tolerance* is > 0, then packet loss report is delayed until that number 
of packets come in. *Reorder Tolerance* increases every time a "belated" packet 
has come, but it wasn't due to retransmission (that is, when UDP packets tend to 
come out of order), with the difference between the latest sequence and this 
packet's sequence, and not more than the value of this option. By default it's 0, 
which means that this mechanism is turned off, and the loss report is always sent 
immediately upon experiencing a "gap" in sequences.

---

| OptName               | Since | Binding | Type      | Units     | Default  | Range  |
| --------------------- | ----- | ------- | --------- | --------- | -------- | ------ |
| `SRTO_MAXBW`          | 1.0.5 | pre     | `int64_t` | bytes/sec | -1       | -1     |

- **[GET or SET]** - Maximum send bandwidth.
- `-1`: infinite (the limit in Live Mode is 1Gbps)
- `0`: relative to input rate (see `SRTO_INPUTBW`) 
- `>0`: absolute limit in B/s

- *NOTE: This option has a default value of -1. Although in case when the stream
rate is mostly constant it is recommended to use value 0 here and shape the
bandwidth limit using `SRTO_INPUTBW` and `SRTO_OHEADBW` options.*


---

| OptName              | Since | Binding | Type  | Units   | Default  | Range  |
| -------------------- | ----- | ------- | ----- | ------- | -------- | ------ |
| `SRTO_MESSAGEAPI`    | 1.3.0 | pre     | bool  | boolean | true     |        |

- **[SET]** - When set, this socket uses the Message API[\*], otherwise it uses 
Buffer API. Note that in live mode (see `SRTO_TRANSTYPE` option) there's only 
message API available. In File mode you can chose to use one of two modes:

  - Stream API (default, when this option is false). In this mode you may send 
  as many data as you wish with one sending instruction, or even use dedicated 
  functions that read directly from a file. The internal facility will take care 
  of any speed and congestion control. When receiving, you can also receive as 
  many data as desired, the data not extracted will be waiting for the next call. 
  There is no boundary between data portions in the Stream mode.
  
  - Message API. In this mode your single sending instruction passes exactly one 
  piece of data that has boundaries (a message). Contrary to Live mode, 
  this message may span across multiple UDP packets and the only size limitation 
  is that it shall fit as a whole in the sending buffer. The receiver shall use 
  as large buffer as necessary to receive the message, otherwise the message will 
  not be given up. When the message is not complete (not all packets received or 
  there was a packet loss) it will not be given up. The messages that are sent 
  later, but were earlier reassembled by the receiver, will be given up to the 
  received once ready, if the `inorder` flag (see `srt_sendmsg`) was set to
  false.
  
- As a comparison to the standard system protocols, the Stream API makes the 
transmission similar to TCP, whereas the Message API functions like the 
SCTP protocol.

---

| OptName           | Since | Binding | Type      | Units   | Default  | Range         |
| ----------------- | ----- | ------- | --------- | ------- | -------- | ------------- |
| `SRTO_MINVERSION` | 1.3.0 | pre     | `int32_t` | version | 0        | up to current |

- **[SET]** - The minimum SRT version that is required from the peer. 
A connection to a peer  that does not satisfy the minimum version requirement 
will be rejected.

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_MSS`            |       | pre     | `int` | bytes  | 1500     | 76..   |

- Maximum Segment Size. Used for buffer allocation and rate calculation using 
packet counter assuming fully filled packets. The smallest MSS between the peers 
is used. *This is 1500 by default in the overall internet. This is the maximum 
size of the UDP packet and can be only decreased, unless you have some unusual 
dedicated network settings. Not to be mistaken with the size of the UDP payload 
or SRT payload - this size is the size of the IP packet, including the UDP 
and SRT headers* 

---

| OptName              | Since | Binding | Type   | Units  | Default  | Range  |
| -------------------- | ----- | ------- | ------ | ------ | -------- | ------ |
| `SRTO_NAKREPORT`     | 1.1.0 | pre     | `bool` | true   | true     | false  |

- **[GET or SET]** - When set to true, Receiver will send `UMSG_LOSSREPORT` 
messages periodically until the lost packet is retransmitted or intentionally 
dropped 

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_OHEADBW`        | 1.0.5 | post    | `int` | %      | 25       | 5..100 | 

- Recovery bandwidth overhead above input rate (see `SRTO_INPUTBW`). It is
effective only if `SRTO_MAXBW` is set to 0.

- *Sender: user configurable, default: 25%.* 

- Recommendations:

	- *Overhead is intended to give you extra bandwidth for a case when
some packet has taken part of the bandwidth, but then was lost and has to be
retransmitted. Therefore the effective maximum bandwidth should be
appropriately higher than your stream's bitrate so that there's some room
for retransmission, but still limited so that the retransmitted packets
don't cause the bandwidth usage to skyrocket when larger groups of
packets were lost*

	- *Don't configure it too low and avoid 0 in case when you have
`SRTO_INPUTBW` option set to 0 (automatic) otherwise your stream will
choke and break quickly at any rising packet loss.*

- ***To do: set-only. get should be supported.***

---

| OptName               | Since | Binding | Type   | Units  | Default  | Range   |
| --------------------- | ----- | ------- | ------ | ------ | -------- | ------- |
| `SRTO_PACKETFILTER`   | 1.4.0 | pre     | string |        |          | [...512]| 

- **[SET]** - Set up the packet filter. The string must match appropriate syntax
for packet filter setup.

For details, see [Packet Filtering & FEC](packet-filtering-and-fec.md).

---

| OptName             | Since | Binding | Type   | Units | Default  | Range    |
| ------------------- | ----- | ------- | ------ | ----- | -------- | -------- |
| `SRTO_PASSPHRASE`   | 0.0.0 | pre     | string |       | [0]      | [10..79] |

- **[SET]** - Sets the passphrase for encryption. This turns encryption on on 
this side (or turns it off, if empty passphrase is passed).
- The passphrase is the shared secret between the sender and the receiver. It is 
used to generate the Key Encrypting Key using [PBKDF2](http://en.wikipedia.org/wiki/PBKDF2) 
(Password-Based Key Derivation Function 2). It is used on the receiver only if 
the received data is encrypted.  The configured passphrase cannot be get back 
(write-only). *Sender and receiver: user configurable.* 

---

| OptName               | Since | Binding | Type  | Units  | Default     | Range                             |
| --------------------- | ----- | ------- | ----- | ------ | ----------- | --------------------------------- |
| `SRTO_PAYLOADSIZE`    | 1.3.0 | pre     | int   | bytes  | 1316 (Live) | up to MTUsize-28-16, usually 1456 |

- **[SET]** - Sets the maximum declared size of a single call to sending 
function in Live mode. Use 0 if this value isn't used (which is default in file 
mode). This value shall not be exceeded for a single data sending instruction 
in Live mode

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range                           |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------------------------------- |
| `SRTO_PBKEYLEN`       | 0.0.0 | pre     | `int32_t` | bytes  | 0        | 0 16(128/8) 24(192/8) 32(256/8) |

- **[GET or SET]** - Sender encryption key length. The use is slightly 
different in 1.2.0 (HSv4) and 1.3.0 (HSv5):

  - HSv4: This is set on the sender and enables encryption, if not 0. The receiver 
  shall not set it and will agree on the length as defined by the sender.
  
  - HSv5: On the sending party it will default to 16 if not changed the default 
  0 and the passphrase was set. The party that has set this value to non-zero 
  value will advertise it at the beginning of the handshake. Actually there are 
  two methods of defining it predicted to be used and all other uses are 
  considered an undefined behavior:
  
    - **Unidirectional**: the sender shall set `PBKEYLEN` and the receiver shall 
    not alter the default value 0. The effective `PBKEYLEN` will be the one set 
    on the sender. The receiver need not know the sender's `PBKEYLEN`, just the 
    passphrase, `PBKEYLEN` will be correctly passed.
    
    - **Bidirectional in Caller-Listener arrangement**: use a rule in your use 
    case that you will be setting the `PBKEYLEN` exclusively either on the 
    Listener or on the Caller. Simply the value set on the Listener will win, 
    if set. 
    
    - **Bidirectional in Rendezvous arrangement**: you have to know both parties 
    passphrases as well as `PBKEYLEN` and you shall set `PBKEYLEN` to the same 
    value on both parties (or leave the default value on both parties, which will 
    result in 16)
    
    - **Unwanted behavior cases**: if both parties set `PBKEYLEN` and the value 
    on both sides is different, the effective `PBKEYLEN` will be the one that is 
    set on the Responder party, which may also override the `PBKEYLEN` 32 set by 
    the sender to value 16 if such value was used by the receiver. The Responder 
    party is Listener in Caller-Listener arrangement, and in Rendezvous it's the 
    matter of luck which one.

- Possible values:
  - 0 (`PBKEYLEN` not set)
  - 16 (effective default) = AES-128
  - 24 = AES-192
  - 32 = AES-256

- *Sender: user configurable.* 

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range         |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------------- |
| `SRTO_PEERIDLETIMEO`  | 1.3.3 | pre     | `int32_t` | msec   | 5000     | positive only |

- The maximum time in `[ms]` to wait until any packet is received from peer since
the last such packet reception. If this time is passed, connection is considered
broken on timeout.

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range         |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------------- |
| `SRTO_PEERLATENCY`    | 1.3.0 | pre     | `int32_t` | msec   | 0        | positive only |

- The latency value (as described in `SRTO_RCVLATENCY`) that is set by the sender 
side as a minimum value for the receiver.

---

| OptName            | Since | Binding | Type      | Units  | Default | Range  |
| ------------------ | ----- | ------- | --------- | ------ | ------- | ------ |
| `SRTO_PEERVERSION` | 1.1.0 | n/a     | `int32_t` | n/a    | n/a     | n/a    |

- **[GET]** - Peer SRT version. The value 0 is returned if not connected, SRT 
handshake not yet performed (HSv4 only), or if peer is not SRT. See `SRTO_VERSION` 
for the version format. 

---

| OptName               | Since | Binding | Type  | Units | Default          | Range                           |
| --------------------- | ----- | ------- | ----- | ----- | ---------------- | ------------------------------- |
| `SRTO_RCVBUF`         |       | pre     | `int` | bytes | 8192 × (1500-28) | 32 × (1500-28) ..FC × (1500-28) |

- Receive Buffer Size. 
- *Receive buffer must not be greater than FC size.* 
- ***Warning: configured in bytes, converted in packets when set based on MSS 
value. For desired result, configure MSS first.***

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_RCVDATA`    |       | n/a     | `int32_t` | pkts   | n/a      |        |

- **[GET]** - Size of the available data in the receive buffer.

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_RCVKMSTATE`     | 1.2.0 | post    | enum  | n/a    | n/a      |        |
 
- **[GET]** - KM state on the agent side when it's a receiver, as per `SRTO_KMSTATE`
- Values defined in enum `SRT_KM_STATE`:
  - `SRT_KM_S_UNSECURED`: no decryption (even if sent data are encrypted)
  - `SRT_KM_S_SECURING`: securing: (HSv4 only) encryption is desired, but KMX 
  handshake not yet done, still waiting (until done, behaves like UNSECURED)
  - `SRT_KM_S_SECURED`: KM exchange was successful and it will be decrypting 
  encrypted data
  - `SRT_KM_S_NOSECRET`: (HSv5 only) This site has set password, but data will 
  be received as plain
  - `SRT_KM_S_BADSECRET`: The password is wrong, encrypted payloads won't be 
  decrypted.

---

| OptName               | Since | Binding | Type      | Units  | Default  | Range         |
| --------------------- | ----- | ------- | --------- | ------ | -------- | ------------- |
| `SRTO_RCVLATENCY`     | 1.3.0 | pre     | `int32_t` | msec   | 120      | positive only |

- **NB:** The default [live mode](#transmission-method-live) settings set `SRTO_RCVLATENCY` to 120 ms!
The [buffer mode](#transmission-method-buffer) settings set `SRTO_RCVLATENCY` to 0.
- The time that should elapse since the moment when the packet was sent and the 
moment when it's delivered to the receiver application in the receiving function. 
This time should be a buffer time large enough to cover the time spent for sending, 
unexpectedly extended RTT time, and the time needed to retransmit the lost UDP 
packet. The effective latency value will be the maximum of this options' value 
and the value of `SRTO_PEERLATENCY` set by the peer side. **This option in 
pre-1.3.0 version is available only as** `SRTO_LATENCY`.

---

| OptName               | Since | Binding | Type   | Units  | Default | Range  |
| --------------------- | ----- | ------- | ------ | ------ | ------- | ------ |
| `SRTO_RCVSYN`         |       | pre     | `bool` | true   | true    | false  |

- **[GET or SET]** - Synchronous (blocking) receive mode 

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_RCVTIMEO`       |       | post    | `int` | msecs  | -1       | -1..   |

- **[GET or SET]** - Blocking mode receiving timeout (-1: infinite)

---

| OptName               | Since | Binding | Type   | Units  | Default | Range  |
| --------------------- | ----- | ------- | ------ | ------ | ------- | ------ |
| `SRTO_RENDEZVOUS`     |       | pre     | `bool` | false  | true    | false  |

- **[GET or SET]** - Use Rendezvous connection mode (both sides must set this 
and both must use bind/connect to one another.

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range       |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ----------- |
| `SRTO_REUSEADDR`      |       | pre     |       |        | true     | true, false |

- When true, allows the SRT socket use the binding address used already by 
another SRT socket in the same application. Note that SRT socket use an 
intermediate object to access the underlying UDP sockets called Multiplexer, 
so multiple SRT socket may share one UDP socket and the packets received to this 
UDP socket will be correctly dispatched to the SRT socket to which they are 
currently destined. This has some similarities to `SO_REUSEADDR` system socket 
option, although it's only used inside SRT. 

- *TODO: This option weirdly only allows the socket used in **bind()** to use the 
local address that another socket is already using, but not to disallow another 
socket in the same application to use the binding address that the current 
socket is already using. What it actually changes is that when given address in 
**bind()** is already used by another socket, this option will make the binding 
fail instead of making the socket added to the shared group of that socket that 
already has bound this address - but it will not disallow another socket reuse 
its address.* 

---

| OptName        | Since | Binding | Type            | Units | Default | Range |
| -------------- | ----- | ------- | --------------- | ----- | ------- | ----- |
| `SRTO_SENDER`  | 1.0.4 | pre     | `int32_t` bool? |       | false   |       |

- Set sender side. The side that sets this flag is expected to be a sender. 
It's required when any of two connection sides supports at most *HSv4* handshake, 
and therefore the sender side is the side that initiates the SRT extended 
handshake (which won't be done at all, if none of the sides sets this flag). 
This flag is superfluous, if **both** parties are at least version 1.3.0 (this 
shall be enforced by setting this value to `SRTO_MINVERSION` if you expect that it 
be true) and therefore support *HSv5* handshake, where the SRT extended handshake 
is done with the overall handshake process. This flag is however **obligatory** 
if at least one party may be SRT below version 1.3.0 and does not support *HSv5*.

---

| OptName               | Since | Binding | Type          | Units      | Default  | Range            |
| --------------------- | ----- | ------- | ------------- | ---------- | -------- | ---------------- |
| `SRTO_CONGESTION`       | 1.3.0 | pre     | `const char*` | predefined | "live"   | "live" or "file" |

- **[SET]** - The type of congestion controller used for the transmission for
that socket. Its type must be exactly the same on both connecting parties,
otherwise the connection is rejected. 
- ***TODO: might be reasonable to allow an "adaptive" congestion controller,
which will make the side that sets it accept whatever controller type is set
by the peer, including different per connection***

---

| OptName               | Since | Binding | Type  | Units  | Default          | Range  |
| --------------------- | ----- | ------- | ----- | ------ | ---------------- | ------ |
| `SRTO_SNDBUF`         |       | pre     | `int` | bytes  | 8192 × (1500-28) |        |

- Send Buffer Size.  ***Warning: configured in bytes, converted in packets, when 
set, based on MSS value. For desired result, configure MSS first.*** 

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_SNDDATA`    |       | n/a     | `int32_t` | pkts   | n/a      | n/a    |

- **[GET]** - Size of the unacknowledged data in send buffer.

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_SNDDROPDELAY`   | 1.3.2 | pre     | `int` | ms     | 0        | -1..   |

- **NB:** The default [live mode](#transmission-method-live) settings set `SRTO_SNDDROPDELAY` to 0.
The [buffer mode](#transmission-method-buffer) settings set `SRTO_SNDDROPDELAY` to -1.

- **[SET]** - Sets an extra delay before TLPKTDROP is triggered on the data
  sender. TLPKTDROP discards packets reported as lost if it is already too late
to send them (the receiver would discard them even if received).  The total
delay before TLPKTDROP is triggered consists of the LATENCY (`SRTO_PEERLATENCY`),
plus `SRTO_SNDDROPDELAY`, plus 2 * the ACK interval (default ACK interval is 10ms).
The minimum total delay is 1 second.
A value of -1 discards packet drop.
`SRTO_SNDDROPDELAY` extends the tolerance for retransmitting packets at
the expense of more likely retransmitting them uselessly. To be effective, it
must have a value greater than 1000 - `SRTO_PEERLATENCY`.

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_SNDKMSTATE`     | 1.2.0 | post    | enum  | n/a    | n/a      |        |

- **[GET]** - Peer KM state on receiver side for `SRTO_KMSTATE`
- Values defined in enum `SRT_KM_STATE`:
  - `SRT_KM_S_UNSECURED`: data will not be encrypted
  - `SRT_KM_S_SECURING`: (HSv4 only): encryption is desired, but KM exchange 
  isn't finished. Payloads will be encrypted, but the receiver won't be able 
  to decrypt them yet.
  - `SRT_KM_S_SECURED`: payloads will be encrypted and the receiver will 
  decrypt them
  - `SRT_KM_S_NOSECRET`: Encryption is desired on this side and payloads will 
  be encrypted, but the receiver didn't set the password and therefore won't be 
  able to decrypt them
  - `SRT_KM_S_BADSECRET`: Encryption is configured on both sides, but the 
  password is wrong (in HSv5 terms: both sides have set different passwords). 
  The payloads will be encrypted and the receiver won't be able to decrypt them.

---

| OptName              | Since | Binding | Type   | Units  | Default  | Range  |
| -------------------- | ----- | ------- | ------ | ------ | -------- | ------ |
| `SRTO_SNDSYN`        |       | post    | `bool` | true   | true     | false  |

- **[GET or SET]** - Synchronous (blocking) send mode 

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_SNDTIMEO`       |       | post    | `int` | msecs  | -1       | -1..   |

- **[GET or SET]** - Blocking mode sending timeout (-1: infinite)

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_STATE`      |       | n/a     | `int32_t` |        | n/a      | n/a    |

- **[GET]** - UDT connection state. (See enum `SRT_SOCKSTATUS`)

---

| OptName         | Since | Binding | Type          | Units  | Default  | Range      |
| --------------- | ----- | ------- | ------------- | ------ | -------- | ---------- |
| `SRTO_STREAMID` | 1.3.0 | pre     | `const char*` |        | empty    | any string |

- **[GET or SET]** - A string limited to 512 characters that can be set on the 
socket prior to connecting. This stream ID will be able to be retrieved by the 
listener side from the socket that is returned from `srt_accept` and was 
connected by a socket with that set stream ID (so you usually use SET on the
socket used for `srt_connect` and GET on the socket retrieved from `srt_accept`).
This string can be used completely free-form, however it's highly recommended
to follow the [SRT Access Control guidlines](AccessControl.md).

As this uses internally the `std::string` type, there are additional functions
for it in the legacy/C++ API (udt.h): `UDT::setstreamid` and
`UDT::getstreamid`. This option doesn't make sense in Rendezvous connection;
the result might be that simply one side will override the value from the other
side and it's the matter of luck which one would win

---

| OptName                    | Since | Binding | Type            | Units | Default  | Range  |
| -------------------------- | ----- | ------- | --------------- | ----- | -------- | ------ |
| `SRTO_ENFORCEDENCRYPTION`  | 1.3.2 | pre     | `int (bool)`    |       | true     | false  |

- **[SET]** - This option enforces that both connection parties have the
same passphrase set (including empty, that is, with no encryption), or
otherwise the connection is rejected.

When this option is set to FALSE **on both connection parties**, the
connection is allowed even if the passphrase differs on both parties,
or it was set only on one party. Note that the party that has set a passphrase
is still allowed to send data over the network. However, the receiver will not
be able to decrypt that data and will not deliver it to the application. The
party that has set no passphrase can send (unencrypted) data that will be
successfully received by its peer.

This option can be used in some specific situations when the user knows
both parties of the connection, so there's no possible situation of a rogue
sender and can be useful in situations where it is important to know whether a
connection is possible. The inability to decrypt an incoming transmission can
be then reported as a different kind of problem.

---

| OptName           | Since | Binding | Type            | Units | Default  | Range  |
| ----------------- | ----- | ------- | --------------- | ----- | -------- | ------ |
| `SRTO_TLPKTDROP`  | 1.0.6 | pre     | `int32_t` bool? | true  | true     | false  |

- Too-late Packet Drop. When enabled on receiver, it skips missing packets that 
have not been delivered in time and delivers the subsequent packets to the 
application when their time-to-play has come. It also sends a fake ACK to the 
sender. When enabled on sender and enabled on the receiving peer, sender drops 
the older packets that have no chance to be delivered in time. It is automatically 
enabled in sender if receiver supports it.

---

| OptName               | Since | Binding | Type  | Units  | Default     | Range            |
| --------------------- | ----- | ------- | ----- | ------ | ----------- | ---------------- |
| `SRTO_TRANSTYPE`      | 1.3.0 | pre     | enum  |        | `SRTT_LIVE` | alt: `SRTT_FILE` |

- **[SET]** - Sets the transmission type for the socket, in particular, setting 
this option sets multiple other parameters to their default values as required 
for a particular transmission type.
  - `SRTT_LIVE`: Set options as for live transmission. In this mode, you should 
  send by one sending instruction only so many data that fit in one UDP packet, 
  and limited to the value defined first in `SRTO_PAYLOADSIZE` option (1316 is 
  default in this mode). There is no speed control in this mode, only the 
  bandwidth control, if configured, in order to not exceed the bandwidth with 
  the overhead transmission (retransmitted and control packets).
  - `SRTT_FILE`: Set options as for non-live transmission. See `SRTO_MESSAGEAPI` 
  for further explanations

---

| OptName            | Since | Binding | Type              | Units  | Default  | Range  |
| ------------------ | ----- | ------- | ----------------- | ------ | -------- | ------ |
| `SRTO_TSBPDMODE`   | 0.0.0 | pre     | `int32_t` (bool?) | false  | true     | false  |

- Timestamp-based Packet Delivery mode. This flag is set to _true_ by default 
and as a default flag set in live mode.

---

| OptName            | Since | Binding | Type  | Units  | Default     | Range  |
| ------------------ | ----- | ------- | ----- | ------ | ----------- | ------ |
| `SRTO_UDP_RCVBUF`  |       | pre     | `int` | bytes  | 8192 × 1500 | MSS..  |

- UDP Socket Receive Buffer Size.  Configured in bytes, maintained in packets 
based on MSS value. Receive buffer must not be greater than FC size.

---

| OptName               | Since | Binding | Type  | Units  | Default  | Range  |
| --------------------- | ----- | ------- | ----- | ------ | -------- | ------ |
| `SRTO_UDP_SNDBUF`     |       | pre     | `int` | bytes  | 65536    | MSS..  |

- UDP Socket Send Buffer Size. Configured in bytes, maintained in packets based 
on `SRTO_MSS` value. *SRT recommended value:* `64*1024`

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ |
| `SRTO_VERSION`    | 1.1.0 | n/a     | `int32_t` |        | n/a      | n/a    |

- **[GET]** - Local SRT version. This is the highest local version supported if 
not connected, or the highest version supported by the peer if connected.
- The version format in hex is 0xXXYYZZ for x.y.z in human readable form, 
where x = ("%d", (version>>16) & 0xff), etc.
- SET could eventually be supported for testing 


Transmission types
------------------

SRT has been mainly created for Live Streaming and therefore its main and
default transmission type is "live". SRT supports, however, the modes that
the original UDT library supported, that is, file and message transmission.

There are two general modes: Live and File transmission. Inside File transmission mode, there are also two possibilities: Buffer API
and Message API. The Live mode uses Message API. However it doesn't
exactly match the description of the Message API because it uses a maximum
single sending buffer up to the size that fits in one UDP packet.

There are two options to set a particular type:

* `SRTO_TRANSTYPE`: uses the enum value with `SRTT_LIVE` for live mode
   and `SRTT_FILE` for file mode. This option actually changes several parameters to 
   their default values for that mode. After this is done, additional parameters, 
   including those that are set here, can be further changed.
* `SRTO_MESSAGEAPI`: This sets the Message API (true) or Buffer API (false)

This makes possible a total of three data transmission methods:

* Live
* Buffer
* Message

**NOTE THE TERMS** used below:
* HANGUP and RESUME: "Function HANGS UP" means that it returns
an error from the `MJ_AGAIN` category (see `SRT_EASYNC*`, `SRT_ETIMEOUT` and
`SRT_ECONGEST` symbols from `SRT_ERRNO` enumeration type), if it's in
non-blocking mode. In blocking mode it will block until the condition that
caused the HANGUP no longer applies, which is defined as that the function
RESUMES. In nonblocking mode, the function RESUMES when the call to it has done
something and returned the non-error status. The blocking mode in SRT is
separate for sending and receiving and set by `SRTO_SNDSYN` and `SRTO_RCVSYN`
options respectively
* BLIND REXMIT: A situation where packets that were sent are still not
acknowledged, either in expected time frame, or when another ACK has
come for the same number, but no packets have been reported as lost,
or at least not for all still unacknowledged packets. The congestion control
class is responsible for the algorithm for taking care of this
situation, which is either FASTREXMIT or LATEREXMIT. This will be
expained below.


Transmission method: Live
-------------------------

Setting `SRTO_TRANSTYPE` to `SRTT_LIVE` sets the following parameters:

* `SRTO_TSBPDMODE` = true
* `SRTO_RCVLATENCY` = 120
* `SRTO_PEERLATENCY` = 0
* `SRTO_TLPKTDROP` = true
* `SRTO_MESSAGEAPI` = true
* `SRTO_NAKREPORT` = true
* `SRTO_PAYLOADSIZE` = 1316
* `SRTO_CONGESTION` = "live"

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
limitation on the bandwidth, if needed, just to add extra time, if the distance
between two consecutive packets would be too short for the defined speed limit.
Note that it is not predicted to work with "virtually infinite" ingest speeds
(such as, for example, reading directly from a file). Therefore the application
is not allowed to stream data with maximum speed -- it must take care that the
speed of data being sent is in rhythm with timestamps in the live stream.
Otherwise the behavior is undefined and might be surprisingly disappointing.

The reading function will always return only a payload that was
sent, and it will HANGUP until the time to play has come for this
packet (if TSBPD mode is on) or when it is available without gaps of
lost packets (if TSBPD mode is off - see `SRTO_TSBPDMODE`).

You may wish to tweak some of the parameters below:
* `SRTO_TSBPDMODE`: you can turn off controlled latency, if your
application uses some alternative and its own method of latency control
* `SRTO_RCVLATENCY`: you can increase the latency time, if this is
too short (setting shorter latency than default is strongly
discouraged, although in some very specific and dedicated networks
this may still be reasonable). Note that `SRTO_PEERLATENCY` is an option
for the sending party, which is the minimum possible value for a receiver.
* `SRTO_TLPKTDROP`: When true (default), it will drop the packets
that haven't been retransmitted on time, that is, before the next packet
that is already received becomes ready to play. You can turn this off to always 
ensure a clean delivery. However, a lost packet can simply pause a
delivery for some longer, potentially undefined time, and cause even
worse tearing for the player. Setting higher latency will help much more in
the case when TLPKTDROP causes packet drops too often.
* `SRTO_NAKREPORT`: Turns on repeated sending of lossreport, when the lost
packet was not recovered quickly enough, which raises suspicions that the
lossreport itself was lost. Without it, the lossreport will be always reported
just once and never repeated again, and then the lost payload packet will
be probably dropped by the TLPKTDROP mechanism.
* `SRTO_PAYLOADSIZE`: Default value is for MPEG TS; if you are going
to use SRT to send any different kind of payload, such as, for example, 
wrapping a live stream in very small frames, then you can use a bigger
maximum frame size, though not greater than 1456 bytes.

Parameters from the modified for transmission type list, not
mentioned in the list above, are crucial for Live mode and shall not be
changed.

The BLIND REXMIT situation is resolved using the FASTREXMIT algorithm by
LiveCC: sending non-acknowledged packets blindly on the
premise that the receiver lingers too long before acknowledging them.
This mechanism isn't used (that is, the BLIND REXMIT situation isn't
handled at all) when `SRTO_NAKREPORT` is set by the peer -- the NAKREPORT
method is considered so effective that FASTREXMIT isn't necessary.


Transmission method: Buffer
---------------------------

Setting `SRTO_TRANSTYPE` to `SRTT_FILE` sets the following parameters:

* `SRTO_TSBPDMODE` = false
* `SRTO_RCVLATENCY` = 0
* `SRTO_PEERLATENCY` = 0
* `SRTO_TLPKTDROP` = false
* `SRTO_MESSAGEAPI` = false
* `SRTO_NAKREPORT` = false
* `SRTO_PAYLOADSIZE` = 0
* `SRTO_CONGESTION` = "file"

In this mode, calling a sending function is allowed to potentially send
virtually any size of data. The sending function will HANGUP only if the
sending buffer is completely replete, and RESUME if the sending buffers are
available for at least one smallest portion of data passed for sending. The
sending function need not send everything in this call, and the caller must
be aware that the sending function might return sent data of smaller size
than was actually requested.

From the receiving function there will be retrieved as many data as the minimum
of the passed buffer size and available data; data still available and not
retrieved by this call will be available for retrieval in the next call.

There is also a dedicated pair of functions that can
only be used in this mode: `srt_sendfile` and `srt_recvfile`. These
functions can be used to transmit the whole file, or a fragment of it,
based on the offset and size.

This mode uses the `FileCC` congestion control class, which is a direct copy of
the UDT's `CUDTCC` congestion control class, adjusted to the needs of SRT's
congestion control framework. This class generally sends the data with maximum
speed in the beginning, until the flight window is full, and then keeps the
speed at the edge of the flight window, only slowing down in the case where
packet loss was detected. The bandwidth usage can be directly limited by
`SRTO_MAXBW` option.

The BLIND REXMIT situation is resolved in FileCC using the LATEREXMIT
algorithm: when the repeated ACK was received for the same packet, or when the
loss list is empty and the flight window is full, all packets since the last
ACK are sent again (that's more or less the TCP behavior, but in contrast to
TCP, this is done as a very low probability fallback).

As you can see in the parameters described above, most have
`false` or `0` values as they usually designate features used in
Live mode. None are used with File mode.
The only option that makes sense to modify after the `SRTT_FILE`
type was set is `SRTO_MESSAGEAPI`, which is described below.


Transmission method: Message
----------------------------

Setting `SRTO_TRANSTYPE` to `SRTT_FILE` and then `SRTO_MESSAGEAPI` to
true implies usage of the Message transmission method. Parameters are set as
described above for the Buffer method, with the exception of `SRTO_MESSAGEAPI`, and 
the "file" congestion controller is also used in this mode. It differs from the
Buffer method, however, in terms of the rules concerning sending and receiving.

**HISTORICAL INFO**: The library that SRT was based on, UDT, somewhat misleadingly
used the terms STREAM and DGRAM, and used the system symbols `SOCK_STREAM` and 
`SOCK_DGRAM` in the socket creation function. The "datagram"
in the UDT terminology has nothing to do with the "datagram" term in
networking terminology, where its size is limited to as much it can fit in
one MTU. In UDT it is actually a message, which may span through multiple UDP
packets and has clearly defined boundaries. It's something rather similar to
the **SCTP** protocol. Also, in UDP the API functions were strictly bound to
DGRAM or STREAM mode: `UDT::send/UDT::recv` were only for STREAM and
`UDT::sendmsg/UDT::recvmsg` only for DGRAM. In SRT this is changed: all
functions can be used in all modes, except `srt_sendfile/srt_recvfile`, and how
the functions actually work is controlled by the `SRTO_MESSAGEAPI` flag.

The message mode means that every sending function sends **exactly** as much
data as it is passed in a single sending function call, and the receiver
receives also not less than **exactly** the number of bytes that
was sent (although every message may have a different size). Every
message may also have extra parameters:
* TTL defines how much time (in ms) the message should wait in the sending
buffer for the opportunity to be picked up by the sender thread and sent
over the network; otherwise it is dropped.
* INORDER, when true, means the messages must be read by the receiver in exactly
the same order in which they were sent. In the situation where a 
message suffers a packet loss, this prevents any subsequent messages
from achieving completion status prior to recovery of the preceding message.

The sending function will HANGUP when the free space in the sending
buffer does not exactly fit the whole message, and it will only RESUME
if the free space in the sending buffer grows up to this size. The
call to the sending function also returns with an error, when the
size of the message exceeds the total size of the buffer (this can
be modified by `SRTO_SNDBUF` option). In other words, it is not
designed to send just a part of the message -- either the whole message
is sent, or nothing at all.

The receiving function will HANGUP until the whole message is available
for reading; if the message spans multiple UDP packets, then the function
RESUMES only when every single packet from the message has been
received, including recovered packets, if any. When the INORDER flag is set
to false and parts of multiple messages are currently available,
the first message that is complete (possibly recovered) is returned. 
Otherwise the function does a HANGUP until the next message
is complete. The call to the receiving function is rejected if the
buffer size is too small for a single message to fit in it.

Note that you can use any of the sending and receiving functions
for sending and receiving messages, except sendfile/recvfile, which
are dedicated exclusively for Buffer API. 
