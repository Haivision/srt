
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
           int sa_len = sizeof sa;
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
number of usec (since epoch) in local SRT clock time. If the connection is not between
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

    SRT_MSGCTRL mc = srt_msgctrl_default;
    nb = srt_sendmsg2(u, buf, nb, &mc);


Receiving a payload:

    nb = srt_recvmsg(u, buf, nb);
    nb = srt_recv(u, buf, nb);

    SRT_MSGCTRL mc = srt_msgctrl_default;
    nb = srt_recvmsg2(u, buf, nb, &mc);


Transmission types available in SRT
-----------------------------------

Mode settings determine how the sender and receiver functions work.
The main socket options (see below for full description) that control it are:

* `SRTO_TRANSTYPE`. Sets several parameters in accordance with the selected
mode:
    * `SRTT_LIVE` (default) the Live mode (for live stream transmissions)
    * `SRTT_FILE` the File mode (for "no time controlled" fastest data transmission)
* `SRTO_MESSAGEAPI`
    * true: (default in Live mode): use Message API
    * false: (default in File mode): use Buffer API

See below [Transmission types](#transmission-types).



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
    int srt_epoll_clear_usocks(int eid);

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

Event flags are of various categories: `IN`, `OUT` and `ERR` are events,
which are level-triggered by default and become edge-triggered if combined
with `SRT_EPOLL_ET` flag. The latter is only an edge-triggered flag, not
an event. There's also an `SRT_EPOLL_UPDATE` flag, which is an edge-triggered
only event, and it reports an event on the listener socket that handles socket
group new connection for an already connected group - this is for internal use
only and it's used in the internal code for socket groups.

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

The extra `srt_epoll_clear_usocks` function removes all subscriptions from
the epoll container.

The SRT EPoll system does not supports all features of Linux epoll. For
example, it only supports level-triggered events for system sockets.

Options
=======

There's a general method of setting options on a socket in the SRT C API, similar
to the system setsockopt/getsockopt functions.

Types used in socket options
----------------------------

Possible types of socket options are:

* `int32_t` - this type can usually be treated as an `int` equivalent since
this type does not change size on 64-bit systems. For clarity, options use
this fixed size integer. In some cases the value is expressed using an
enumeration type (see below).

* `int64_t` - Some options need the paramter specified as 64-bit integer

* `bool` - Requires the use of a boolean type (`<stdbool.h>` for C, or built-in
for C++). When setting an option, passing the value through an `int` type is
also properly recognized. When getting an option, however, you should use the
`bool` type, although you can risk passing a variable of `int` type initialized
with 0 and then check if the resulting value is equal to 0 (just don't compare
the result with 1).

* `string` - When setting an option, pass the character array pointer as value
and the string length as length. When getting, pass an array of sufficient size
(as specified in the size variable). Every option with this type that can be
read should specify the maximum length of that array.


Enumeration types used in options
---------------------------------


### 1. `SRT_TRANSTYPE`

Used by `SRTO_TRANSTYPE` option:

* `SRTT_LIVE`: Live mode.
* `SRTT_FILE`: File mode.

See below [Transmission types](#transmission-types) for details.


### 2. `SRT_KM_STATE`

The defined encryption state as performed by the Key Material Exchange, used
by `SRTO_RCVKMSTATE`, `SRTO_SNDKMSTATE` and `SRTO_KMSTATE` options:

- `SRT_KM_S_UNSECURED`: no encryption/descryption. If this state is only on
the receiver, received encrypted packets will be dropped.

- `SRT_KM_S_SECURING`: pending security (HSv4 only). This is a temporary state
used only if the connection uses HSv4 and the Key Material Exchange is
not finished yet. On HSv5 this is not possible because the Key Material
Exchange for the initial key is done in the handshake.

- `SRT_KM_S_SECURED`: KM exchange was successful and the data will be sent
encrypted and will be decrypted by the receiver. This state is only possible on
both sides in both directions simultaneously.

- `SRT_KM_S_NOSECRET`: If this state is in the sending direction
(`SRTO_SNDKMSTATE`), then it means that the sending party has set a
passphrase, but the peer did not. In this case the sending party can receive
unencrypted packets from the peer, but packets it sends to the peer will be
encrypted and the peer will not be able to decrypt them. 
This state is only possible in HSv5.

- `SRT_KM_S_BADSECRET`: The password is wrong (set differently on each party);
encrypted payloads won't be decrypted in either direction.

Note that with the default value of `SRTO_ENFORCEDENCRYPTION` option (true),
the state is equal on both sides in both directions, and it can be only
`SRT_KM_S_UNSECURED` or `SRT_KM_S_SECURED` (in other cases the connection
is rejected). Otherwise it may happen that either both sides have different
password and the state is `SRT_KM_S_BADSECRET` in both directions, or only
one party has set a password, in which case the KM state is as follows:

|                          | `SRTO_RCVKMSTATE`    | `SRTO_SNDKMSTATE`    |
|--------------------------|----------------------|----------------------|
| Party with no password:  | `SRT_KM_S_NOSECRET`  | `SRT_KM_S_UNSECURED` |
| Party with password:     | `SRT_KM_S_UNSECURED` | `SRT_KM_S_NOSECRET`  |


Getting and setting options
---------------------------

Legacy version:

    int srt_getsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, void* optval, int& optlen);
    int srt_setsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, const void* optval, int optlen);

New version:

    int srt_getsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, void* optval, int& optlen);
    int srt_setsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, const void* optval, int optlen);

(In the legacy version, there's an additional unused `level` parameter. It was there
in the original UDT API just to mimic the system `setsockopt` function, but it's ignored).

Some options require a value of type bool and some others of an integer type,
which is not the same -- they differ in size, and mistaking them may end up
with a crash. This must be kept in mind especially in any C wrapper. For
convenience, the setting option function may accept both `int32_t` and `bool`
types, but this is not so in the case of getting an option value.

**UDT project legacy note**: Almost all options from the UDT library are
derived (there are a few deleted, including some deprecated already in UDT).
Many new SRT options have been added. All options are available exclusively
with the `SRTO_` prefix. Old names are provided as alias names in the `udt.h`
legacy/C++ API file. Note the translation rules:
* `UDT_` prefix from UDT options was changed to the prefix `SRTO_`
* `UDP_` prefix from UDT options was changed to the prefix `SRTO_UDP_`
* `SRT_` prefix in older SRT versions was changed to `SRTO_`

The table further below shows the characteristics of the options, according
to the following legend:

1. **Since**

Defines the SRT version when this option was first introduced. If this field
is empty, it's an option derived from UDT. "Version 0.0.0" is the oldest version
of SRT ever created and put into use.

2. **Binding**

Defines limitation on setting the option (the field is empty if the option
is not settable, see **Dir** column):

* pre: For a connecting socket (both as caller and rendezvous) it must be set
prior to calling `srt_connect()` or `srt_bind()` and never changed thereafter.
For a listener socket it should be set to a listening socket and it will be
derived by every socket returned by `srt_accept()`.

* post: This flag can be changed any time, including after the socket is
connected (as well as on an accepted socket). On a listening socket setting this
flag is effective only on this socket itself. Note though that there are some
post-bound options that have important meaning when set prior to connecting.

3. **Type**

The data type of the option; see above.

4. **Units**

Roughly specified unit, if the value defines things like length or time.
It can also define more precisely what kind of specialization can be used
when the type is integer:

* enum: the possible values are defined in an enumeration type
* flags: the integer value is a collection of bit flags

5. **Default**

The exact default value, if it can be easily specified. For a more complicated
state of the default state of a particular option, it will be explained in the
description (when marked by asterisk). For non-settable options this field is
empty.

6. **Range**

If a value of an integer type has a limited range, or only specified value
allowed, it will be specified here, otherwise empty. A ranged value can be
specified as:

* X-... : specifies only a minimum value
* X-Y,Z : values between X and Y are allowed, and additionally Z

If the value is of `string` type, this field will contain its maximum size
in square brackets.

If the range contains additionally an asterisk, it means that more elaborate
restrictions on the value apply, as explained in the description.

7. **Dir**

Option direction: W if can be set, R if can be retrieved, RW if both.

6. **Entity**

This describes whether the option can be set on the socket or the group.
The G and S options may appear together, in which case both possibilities apply.
The D and I options, mutually exclusive, appear always with G. 
The + marker can only coexist with GS.

Possible specifications are:

* S: This option can be set on a single socket (exclusively, if not GS)

* G: This option can be set on a group (exclusively, if not GS)

* D: If set on a group, it will be derived by the member socket

* I: If set on a group, it will be taken and managed exclusively by the group

* +: This option is also allowed to be set individually on a group member
socket through a configuration object in `SRT_SOCKGROUPCONFIG` prepared by
`srt_create_config`. Note that this setting may override the setting derived
from the group.

This option list is sorted alphabetically.


| OptName            | Since | Binding |   Type    | Units  | Default  | Range  | Dir | Entity |
| ------------------ | ----- | ------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_CONNTIMEO`   | 1.1.2 | pre     | `int32_t` | msec   | 3000     | 0..    | W   | GSD+   |

- Connect timeout. This option applies to the caller and rendezvous connection
modes. For the rendezvous mode (see `SRTO_RENDEZVOUS`) the effective connection timeout
will be 10 times the value set with `SRTO_CONNTIMEO`.

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_DRIFTTRACER`| 1.5.0 | post    | `bool`    |        | true     |        | RW  | GSD    |

- Enables or disables time drift tracer (receiver).

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_EVENT`      |       |         | `int32_t` | flags  |          |        | R   | S      |

- Returns bit flags set according to the current active events on the socket. 
- Possible values are those defined in `SRT_EPOLL_OPT` enum (a combination of 
`SRT_EPOLL_IN`, `SRT_EPOLL_OUT` and `SRT_EPOLL_ERR`).

---

| OptName           | Since | Binding | Type      | Units  | Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_FC`         |       | pre     | `int32_t` | pkts   | 25600    | 32..   | RW  | GSD    |

- Flight Flag Size (maximum number of bytes that can be sent without 
being acknowledged)

---

| OptName              | Since | Binding | Type      | Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_GROUPCONNECT`  | 1.5.0 | pre     | `int32_t` |        | 0        | 0...1  | W   | S      |

- When this flag is set to 1 on a listener socket, it allows this socket to
accept group connections. When set to the default 0, group connections will be
rejected. Keep in mind that if the `SRTO_GROUPCONNECT` flag is set to 1 (i.e.
group connections are allowed) `srt_accept` may return a socket **or** a group
ID. A call to `srt_accept` on a listener socket that has group connections
allowed must take this into consideration. It's up to the caller of this
function to make this distinction and to take appropriate action depending on
the type of entity returned.

- When this flag is set to 1 on an accepted socket that is passed to the
listener callback handler, it means that this socket is created for a group
connection and it will become a member of a group. Note that in this case
only the first connection within the group will result in reporting from
`srt_accept` (further connections are handled in the background), and this
function will return the group, not this socket ID.

---

| OptName              | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_GROUPTYPE`     |       | pre     | `int32_t`  | enum   |          |        | R   | S      |

- This option is read-only and it is intended to be called inside the listener
callback handler (see `srt_listen_callback`). Possible values are defined in
the `SRT_GROUP_TYPE` enumeration type.

- This option returns the group type that is declared in the incoming connection.
If the incoming connection is not going to make a group-member connection, then
the value returned is `SRT_GTYPE_UNDEFINED`. If this option is read in any other
context than inside the listener callback handler, the value is undefined.

---

| OptName               | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| --------------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_GROUPSTABTIMEO` |       | pre     | `int32_t`  | ms     | 40       | 10-... | W   | GSD+   |

- This setting is used for groups of type `SRT_GTYPE_BACKUP`. It defines the stability 
timeout, which is the maximum interval between two consecutive packets retrieved from 
the peer on the currently active link. These two packets can be of any type,
but this setting usually refers to control packets while the agent is a sender.
Idle links exchange only keepalive messages once per second, so they do not
count. Note that this option is meaningless on sockets that are not members of
the Backup-type group.

- This value should be set with a thoroughly selected balance and correspond to
the maximum stretched response time between two consecutive ACK messages. By default
ACK messages are sent every 10ms (so this interval is not dependent on the network
latency), and so should be the interval between two consecutive received ACK
messages. Note, however, that the network jitter on the public internet causes
these intervals to be stretched, even to multiples of that interval. Both large
and small values of this option have consequences:

- Large values of this option prevent overreaction on highly stretched response
times, but introduce a latency penalty - the latency must be greater
than this value (otherwise switching to another link won't preserve
smooth signal sending). Large values will also contribute to higher packet
bursts sent at the moment when an idle link is activated.

- Smaller values of this option respect low latency requirements very
well, may cause overreaction on even slightly stretched response times. This is
unwanted, as a link switch should ideally happen only when the currently active
link is really broken, as every link switch costs extra overhead (it counts
for 100% for a time of one ACK interval).

- Note that the value of this option is not allowed to exceed the value of
`SRTO_PEERIDLETIMEO`. Usually it is only meaningful if you change the latter
option, as the default value of it is way above any sensible value of
`SRTO_GROUPSTABTIMEO`.


---
| OptName          | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_INPUTBW`   | 1.0.5 | post    | `int64_t`  | B/s    | 0        | 0..    | W   | GSD    |

- This option is effective only if `SRTO_MAXBW` is set to 0 (relative). It
controls the maximum bandwidth together with `SRTO_OHEADBW` option according
to the formula: `MAXBW = INPUTBW * (100 + OHEADBW) / 100`. When this option
is set to 0 (automatic) then the real INPUTBW value will be estimated from
the rate of the input (cases when the application calls the `srt_send*`
function) during transmission. 

- *Recommended: set this option to the predicted bitrate of your live stream
and keep default 25% value for `SRTO_OHEADBW`*.

---

| OptName          | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_IPTOS`     | 1.0.5 | pre     | `int32_t`  |        | (system) | 0..255 | RW  | GSD    |

- IPv4 Type of Service (see `IP_TOS` option for IP) or IPv6 Traffic Class (see `IPV6_TCLASS`
of IPv6) depending on socket address family. Applies to sender only.

- When getting, the returned value is the user preset for non-connected sockets and the actual
value for connected sockets.

- *Sender: user configurable, default: 0xB8*

---

| OptName          | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_ISN`       | 1.3.0 |         | `int32_t`  |        |          |        | R   | S      |

- The value of the ISN (Initial Sequence Number), which is the first sequence
  number put on a firstmost sent UDP packets carrying SRT data payload. 

- *This value is useful for developers of some more complicated methods of flow 
control, possibly with multiple SRT sockets at a time, not predicted in any 
regular development.*

---

| OptName          | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_IPTTL`     | 1.0.5 | pre     | `int32_t`  | hops   | (system) | 1..255 | RW  | GSD    |

- IPv4 Time To Live (see `IP_TTL` option for IP) or IPv6 unicast hops (see
`IPV6_UNICAST_HOPS` for IPV6) depending on socket address family. Applies to sender only. 

- When getting, the returned value is the user preset for non-connected sockets and the actual
value for connected sockets.

- *Sender: user configurable, default: 64*

---

| OptName          | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_IPV6ONLY`  | 1.4.0 | pre     | `int32_t`  |        | (system) | -1..1  | RW  | GSD    |

- Set system socket flag `IPV6_V6ONLY`. When set to 0 a listening socket binding an
IPv6 address accepts also IPv4 clients (their addresses will be formatted as
IPv4-mapped IPv6 addresses). By default (-1) this option is not set and the
platform default value is used.

---

| OptName               | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| --------------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_KMREFRESHRATE`  | 1.3.2 | pre     | `int32_t`  | pkts   | 0x1000000| 0..    | RW  | GSD    |

- The number of packets to be transmitted after which the Stream Encryption Key
(SEK), used to encrypt packets, will be switched to the new one. Note that
the old and new keys live in parallel for a certain period of time (see
`SRTO_KMPREANNOUNCE`) before and after the switchover.

- Having a preannounce period before switchover ensures the new SEK is installed
at the receiver before the first packet encrypted with the new SEK is received.
The old key remains active after switchover in order to decrypt packets that
might still be in flight, or packets that have to be retransmitted.

---

| OptName               | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| --------------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_KMPREANNOUNCE`  | 1.3.2 | pre     | `int32_t`  | pkts   | 0x1000   | 0.. *  | RW  | GSD    |

- The interval (defined in packets) between when a new Stream Encrypting Key
(SEK) is sent and when switchover occurs. This value also applies to the
subsequent interval between when switchover occurs and when the old SEK is
decommissioned.

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

| OptName               | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| --------------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_KMSTATE`        | 1.0.2 |         | `int32_t`  | enum   |          |        | R   | S      |

- Keying Material state. This is a legacy option that is equivalent to
`SRTO_SNDKMSTATE`, if the socket has set `SRTO_SENDER` to true, and 
`SRTO_RCVKMSTATE` otherwise. This option is then equal to `SRTO_RCVKMSTATE`
always if your application disregards possible cooperation with a peer older
than 1.3.0, but then with the default value of `SRTO_ENFORCEDENCRYPTION` the
value returned by both options is always the same. See [SRT_KM_STATE](#srt-km-state)
for more details.

---

| OptName               | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| --------------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_LATENCY`        | 1.0.2 | pre     | `int32_t`  | ms     | 120 *    | 0..    | RW  | GSD    |

- This flag sets both `SRTO_RCVLATENCY` and `SRTO_PEERLATENCY` to the same value. 
Note that prior to version 1.3.0 this is the only flag to set the latency, however 
this is effectively equivalent to setting `SRTO_PEERLATENCY`, when the side is 
sender (see `SRTO_SENDER`) and `SRTO_RCVLATENCY` when the side is receiver, and 
the bidirectional stream sending in version 1.2.0 was not supported.

---

| OptName              | Since | Binding | Type       | Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_LINGER`        |       | pre     | linger     | s      | on, 180  | 0..    | RW  | GSD    |

- Linger time on close (see [SO\_LINGER](http://man7.org/linux/man-pages/man7/socket.7.html)).

- *SRT recommended value: off (0)*.

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_LOSSMAXTTL`    | 1.2.0 | pre     | `int32_t`  | packets | 0        | 0..    | RW  | GSD+   |

- The value up to which the *Reorder Tolerance* may grow. The *Reorder Tolerance*
is the number of packets that must follow the experienced "gap" in sequence numbers
of incoming packets so that the loss report is sent (in a hope that the gap is due
to packet reordering rather than because of loss). The value of *Reorder Tolerance*
starts from 0 and is set to a greater value, when packet reordering is detected, that
is, when a "belated" packet (with sequence number older than the latest received)
has been received, but without retransmission flag. When this is detected the
*Reorder Tolerance* is set to the value of the interval between latest sequence
and this packet's sequence, but not more than the value set by `SRTO_LOSSMAXTTL`.
By default this value is set to 0, which means that this mechanism is off.


---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MAXBW`         | 1.0.5 | pre     | `int64_t`  | B/s     | -1       | -1..   | RW  | GSD    |

- Maximum send bandwidth.
- `-1`: infinite (the limit in Live Mode is 1Gbps)
- `0`: relative to input rate (see `SRTO_INPUTBW`) 
- `>0`: absolute limit in B/s

- *NOTE: This option has a default value of -1, regardless of the mode. 
For live streams it is typically recommended to set the value 0 here and rely
on `SRTO_INPUTBW` and `SRTO_OHEADBW` options. However, if you want to do so,
you should make sure that your stream has a fairly constant bitrate, or that
changes are not abrupt, as high bitrate changes may work against the
measurement. SRT cannot ensure that this is always the case for a live stream,
therefore the default -1 remains even in live mode.*

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MESSAGEAPI`    | 1.3.0 | pre     | bool       |         | true     |        | W   | GSD    |

- When set, this socket uses the Message API[\*], otherwise it uses 
Stream API. Note that in live mode (see `SRTO_TRANSTYPE` option) only the
message API is available. In File mode you can chose to use one of two modes
(note that the default for this option is changed with `SRTO_TRANSTYPE`
option):

  - Stream API (default for file mode). In this mode you may send 
  as many data as you wish with one sending instruction, or even use dedicated 
  functions that operate directly on a file. The internal facility will take care 
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

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MINVERSION`    | 1.3.0 | pre     | `int32_t`  | version | 0        | *      | W   | GSD    |

- The minimum SRT version that is required from the peer. A connection to a
peer that does not satisfy the minimum version requirement will be rejected.
Format is explained at `SRTO_VERSION` option.

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MSS`           |       | pre     | `int32_t`  | bytes   | 1500     | 76..   | RW  | GSD    |

- Maximum Segment Size. Used for buffer allocation and rate calculation using 
packet counter assuming fully filled packets. The smallest MSS between the peers 
is used. *This is 1500 by default in the overall internet. This is the maximum 
size of the UDP packet and can be only decreased, unless you have some unusual 
dedicated network settings. Not to be mistaken with the size of the UDP payload 
or SRT payload - this size is the size of the IP packet, including the UDP 
and SRT headers* 

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_NAKREPORT`     | 1.1.0 | pre     | `bool`     |         |  *       |        | RW  | GSD+   |

- When set to true, every report for a detected loss will be repeated when the
timeout for the expected retransmission of this loss has expired and the
missing packet still wasn't recovered, or wasn't conditionally dropped (see
`SRTO_TLPKTDROP`).

- The default is true for Live mode and false for File mode (see `SRTO_TRANSTYPE`).

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_OHEADBW`       | 1.0.5 | post    | `int32_t`  | %       | 25       | 5..100 | W   | GSD    |

- Recovery bandwidth overhead above input rate (see `SRTO_INPUTBW`), in percentage
of the input rate. It is effective only if `SRTO_MAXBW` is set to 0.

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

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PACKETFILTER`  | 1.4.0 | pre     | string     |         |  ""      | [512]  | W   | GSD    |

- Set up the packet filter. The string must match appropriate syntax for packet
filter setup.

As there can only be one configuration for both parties, it is recommended that one party
defines the full configuration while the other only defines the matching packet filter type
(for example, one sets `fec,cols:10,rows:-5,layout:staircase` and the other
just `fec`). Both parties can also set this option to the same value. The packet filter function 
will attempt to merge configuration definitions, but if the options specified are in
conflict, the connection will be rejected.

For details, see [Packet Filtering & FEC](packet-filtering-and-fec.md).

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PASSPHRASE`    | 0.0.0 | pre     | string     |         | ""       |[10..79]| W   | GSD    |

- Sets the passphrase for encryption. This enables encryption on this party (or
disables it, if empty passphrase is passed).

- The passphrase is the shared secret between the sender and the receiver. It is 
used to generate the Key Encrypting Key using [PBKDF2](http://en.wikipedia.org/wiki/PBKDF2) 
(Password-Based Key Derivation Function 2). It is used on the receiver only if 
the received data is encrypted.

- Note that since the bidirectional support, there's only one initial SEK to encrypt
the stream (new keys after refreshing will be updated independently) and there's no
distinction between "service party that defines the password" and "client party that
is required to set matching password" - both parties are equivalent and in order to
have a working encrypted connection, they have to simply set the same passphrase,
otherwise the connection is rejected by default (see also `SRTO_ENFORCEDENCRYPTION`).

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PAYLOADSIZE`   | 1.3.0 | pre     | `int32_t`  | bytes   | *        | *      | W   | GSD    |

- Sets the maximum declared size of a single call to sending function in Live
mode. When set to 0, there's no limit for a single sending call.

- For Live mode: Default value is 1316, can be increased up to 1456. Note that
with the `SRTO_PACKETFILTER` option additional header space is usually required,
which decreases the maximum possible value for `SRTO_PAYLOADSIZE`.

- For File mode: Default value is 0 and it's recommended not to be changed.


---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PBKEYLEN`      | 0.0.0 | pre     | `int32_t`  | bytes   | 0        | *      | RW  | GSD    |

- Sender encryption key length.

- Possible values:
  - 0 (`PBKEYLEN` not set)
  - 16 (effective default) = AES-128
  - 24 = AES-192
  - 32 = AES-256

- The use is slightly different in 1.2.0 (HSv4) and since 1.3.0 (HSv5):

  - HSv4: This is set on the sender and enables encryption, if not 0. The receiver 
  shall not set it and will agree on the length as defined by the sender.
  
  - HSv5: On the sending party it will default to 16 if not changed the default 
  0 and the passphrase was set. The party that has set this value to non-zero 
  value will advertise it at the beginning of the handshake. Actually there are 
  three intended methods of defining it, and all other uses are considered an
  undefined behavior:
  
    - **Unidirectional**: the sender shall set `PBKEYLEN` and the receiver shall 
    not alter the default value 0. The effective `PBKEYLEN` will be the one set 
    on the sender. The receiver need not know the sender's `PBKEYLEN`, just the 
    passphrase, `PBKEYLEN` will be correctly passed.
    
    - **Bidirectional in Caller-Listener arrangement**: use a rule in your use 
    case that you will be setting the `PBKEYLEN` exclusively either on the 
    Listener or on the Caller. Simply the value set on the Listener will win, 
    if set on both parties.
    
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


---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PEERIDLETIMEO` | 1.3.3 | pre     | `int32_t`  | ms      | 5000     | 0..    | RW  | GSD+   |

- The maximum time in `[ms]` to wait until any packet is received from peer since
the last such packet reception. If this time is passed, connection is considered
broken on timeout.

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PEERLATENCY`   | 1.3.0 | pre     | `int32_t`  | ms      | 0        | 0..    | RW  | GSD    |

- The latency value (as described in `SRTO_RCVLATENCY`) that is set by the sender 
side as a minimum value for the receiver.

- Note that when reading, the value will report the preset value on a non-connected
socket, and the effective value on a connected socket.

---

| OptName              | Since | Binding | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PEERVERSION`   | 1.1.0 |         | `int32_t`  | *       |          |        | R   | GS     |

- SRT version used by the peer. The value 0 is returned if not connected, SRT 
handshake not yet performed (HSv4 only), or if peer is not SRT. See `SRTO_VERSION` 
for the version format. 

---

| OptName              | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVBUF`        |       | pre     | `int32_t`  | bytes   | 8192 bufs  | *      | RW  | GSD+   |


- Receive Buffer Size, in bytes. Note, however, that the internal setting of this
value is in the number of buffers, each one of size equal to SRT payload size,
which is the value of `SRTO_MSS` decreased by UDP and SRT header sizes (28 and 16).
The value set here will be effectively aligned to the multiple of payload size.

- Minimum value: 32 buffers (46592 with default value of `SRTO_MSS`).
- Maximum value: `SRTO_FC` number of buffers (receiver buffer must not be greater than FC size).

---

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVDATA`    |       |         | `int32_t`  | pkts    |            |        | R   | S      |

- Size of the available data in the receive buffer.

---

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVKMSTATE` | 1.2.0 |         | `int32_t`  | enum    |            |        | R   | S      |
 
- KM state on the agent side when it's a receiver.

- Values defined in enum `SRT_KM_STATE` (see [SRT_KM_STATE](#srt-km-state))

---

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVLATENCY` | 1.3.0 | pre     | `int32_t`  | msec    | *          | 0..    | RW  | GSD    |

- Latency value in the receiving direction. This value is only significant when
`SRTO_TSBPDMODE` is set to true.

- Latency refers to the time that elapses from the moment a packet is sent 
to the moment when it's delivered to a receiver application. The SRT latency 
setting should be a time buffer large enough to cover the time spent for
sending, unexpectedly extended RTT time, and the time needed to retransmit any
lost UDP packet. The effective latency value will be the maximum between the 
`SRTO_RCVLATENCY` value and the value of `SRTO_PEERLATENCY` set by 
the peer side. **This option in pre-1.3.0 version is available only as** 
`SRTO_LATENCY`. Note that the real latency value may be slightly different 
than this setting due to the impossibility of perfectly measuring exactly the 
same point in time at both parties simultaneously. What is important with 
latency is that its actual value, once set with the connection, is kept constant 
throughout the duration of a connection.

- Default value: 120 in Live mode, 0 in File mode (see `SRTO_TRANSTYPE`).

- Note that when reading, the value will report the preset value on a non-connected
socket, and the effective value on a connected socket.

---

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVSYN`     |       | post    | `bool`     |         | true       |        | RW  | GSI    |

- When true, sets blocking mode on reading function when it's not ready to
perform the operation. When false ("non-blocking mode"), the reading function
will in this case report error `SRT_EASYNCRCV` and return immediately. Details
depend on the tested entity:

- On a connected socket or group this applies to a receiving function
(`srt_recv` and others) and a situation when there are no data available for
reading. The readiness state for this operation can be tested by checking the
`SRT_EPOLL_IN` flag on the aforementioned socket or group.

- On a freshly created socket or group that is about to be connected to a peer
listener this applies to any `srt_connect` call (and derived), which in
"non-blocking mode" always return immediately. The connected state for that
socket or group can be tested by checking the `SRT_EPOLL_OUT` flag. NOTE
that a socket that failed to connect doesn't change the `SRTS_CONNECTING`
state and can be found out only by testing `SRT_EPOLL_ERR` flag.

- On a listener socket this applies to `srt_accept` call. The readiness state
for this operation can be tested by checking the `SRT_EPOLL_IN` flag on
this listener socket. This flag is also derived from the listener socket
by the accepted socket or group, although the meaning of this flag is
effectively different.

- Note that when this flag is set only on a group, it applies to a
specific receiving operation being done on that group (i.e. it is not
derived from the socket of which the group is a member).


---

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVTIMEO`   |       | post    | `int32_t`  | ms      | -1         | -1, 0..| RW  | GSI    |

- Limit the time up to which the receiving operation will block (see
`SRTO_RCVSYN` for details), so when this time is exceeded, it will behave as
if in "non-blocking mode". The -1 value means no time limit.

---

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RENDEZVOUS` |       | pre     | `bool`     |         | false      |        | RW  | S      |

- Use Rendezvous connection mode (both sides must set this and both must use the
procedure of `srt_bind` and then `srt_connect` (or `srt_rendezvous`) to one aother.

---

| OptName               | Since | Binding | Type   | Units  | Default | Range  | Dir | Entity |
| --------------------- | ----- | ------- | ------ | ------ | ------- | ------ | --- | ------ |
| `SRTO_RETRANSMITALGO` | 1.5.0 | pre     | `int`  |        | 0       | [0, 1] | W   | GSD    |

- Retransmission algorithm to use (SENDER option):
   - 0 - Default (retranmsit on every loss report).
   - 1 - Reduced retransmissions (not more often than once per RTT) - reduced bandwidth consumption.

- This option is effective only on the sending side. It influences the
decision as to whether particular reported lost packets should be retransmitted
at a certain time or not.

---

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_REUSEADDR`  |       | pre     | `bool`     |         | true       |        | RW  | GSD    |

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

| OptName           | Since | Binding | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_SENDER`     | 1.0.4 | pre     | `bool`     |         | false      |        | W   | S      |

- Set sender side. The side that sets this flag is expected to be a sender.
This flag is only required when communicating with a receiver that uses SRT
version less than 1.3.0 (and hence *HSv4* handshake), in which case if not
set properly, the TSBPD mode (see `SRTO_TSBPDMODE`) or encryption will not work.
Setting `SRTO_MINVERSION` to 1.3.0 is therefore recommended.

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_CONGESTION` | 1.3.0 | pre     | `string`   |         | "live"    | *      | W   | S      |

- The type of congestion controller used for the transmission for that socket.

- Its type must be exactly the same on both connecting parties, otherwise the
connection is rejected - **however** you may also change the value of this
option for the accepted socket in the listener callback (see `srt_listen_callback`)
if an appropriate instruction was given in the Stream ID.

- Currently supported congestion controllers are designated as "live" and "file"

- Note that it is not recommended to change this option manually, but you should
rather change the whole set of options through `SRTO_TRANSTYPE` option.


---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDBUF`     |       | pre     | `int32_t`  | bytes   |8192 bufs  | *      | RW  | GSD+   |

- Sender Buffer Size. See `SRTO_RCVBUF` for more information.

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDDATA`    |       |         | `int32_t`  | pkts    |           |        | R   | S      |

- Size of the unacknowledged data in send buffer.

---

| OptName              | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDDROPDELAY`  | 1.3.2 | pre     | `int32_t`  | ms      | *         | -1..   | W   | GSD+   |

- Sets an extra delay before TLPKTDROP is triggered on the data sender.
This delay is added to the default drop delay time interval value. Keep in mind
that the longer the delay, the more probable it becomes that packets would be
retransmitted uselessly because they will be dropped by the receiver anyway.

- TLPKTDROP discards packets reported as lost if it is already too late
to send them (the receiver would discard them even if received). The delay
before TLPKTDROP mechanism is triggered consists of the SRT latency
(`SRTO_PEERLATENCY`), plus `SRTO_SNDDROPDELAY`, plus `2 * interval between
sending ACKs` (the default `interval between sending ACKs` is 10 milliseconds).
The minimum delay is `1000 + 2 * interval between sending ACKs` milliseconds.

- **Special value -1**: Do not drop packets on the sender at all (retransmit them always when requested).

- Default: 0 in Live mode, -1 in File mode.


---

| OptName              | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDKMSTATE`    | 1.2.0 | post    | `int32_t`  |  enum   |           |        | R   | S      |

- Peer KM state on receiver side for `SRTO_KMSTATE`

- Values defined in enum `SRT_KM_STATE` (see [SRT_KM_STATE](#srt-km-state))

---

| OptName              | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDSYN`        |       | post    | `bool`     |         | true      |        | RW  | GSI    |

- When true, sets blocking mode on writing function when it's not ready to
perform the operation. When false ("non-blocking mode"), the writing function
will in this case report error `SRT_EASYNCSND` and return immediately.

- On a connected socket or group this applies to a sending function
(`srt_send` and others) and a situation when there's no free space in
the sender buffer, caused by inability to send all the scheduled data over
the network. Readiness for this operation can be tested by checking the
`SRT_EPOLL_OUT` flag.

- On a freshly created socket or group it will have no effect until the socket
enters a connected state.

- On a listener socket it will be derived by the accepted socket or group,
but will have no effect on the listener socket itself.

---

| OptName              | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDTIMEO`      |       | post    | `int32_t`  | ms      | -1        | -1..   | RW  | GSI    |

- limit the time up to which the sending operation will block (see
`SRTO_SNDSYN` for details), so when this time is exceeded, it will behave as
if in "non-blocking mode". The -1 value means no time limit.

---

| OptName              | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_STATE`         |       |         | `int32_t`  |  enum   |           |        | R   | S      |

- Returns the current socket state, same as `srt_getsockstate`.


---

| OptName              | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_STREAMID`      | 1.3.0 | pre     | `string`   |         | ""        | [512]  | RW  | GSD    |

- A string that can be set on the socket prior to connecting. The listener side 
will be able to retrieve this stream ID from the socket that is returned from 
`srt_accept` (for a connected socket with that stream ID). You usually use SET 
on the socket used for `srt_connect`, and GET on the socket retrieved from 
`srt_accept`. This string can be used completely free-form. However, it's highly 
recommended to follow the [SRT Access Control guidlines](AccessControl.md).

- As this uses internally the `std::string` type, there are additional functions
for it in the legacy/C++ API (udt.h): `srt::setstreamid` and
`srt::getstreamid`.

- This option is not useful for a Rendezvous connection, since once side would
override the value from the other side resulting in an arbitrary winner. Also
in this connection both peers are known from upside to one another and both
have equivalent roles in the connection.

---

| OptName                    | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_ENFORCEDENCRYPTION`  | 1.3.2 | pre     | `bool`     |         | true      |        | W   | GSD    |

- This option enforces that both connection parties have the same passphrase
set, or both do not set the passphrase, otherwise the connection is rejected.

- When this option is set to FALSE **on both connection parties**, the
connection is allowed even if the passphrase differs on both parties,
or it was set only on one party. Note that the party that has set a passphrase
is still allowed to send data over the network. However, the receiver will not
be able to decrypt that data and will not deliver it to the application. The
party that has set no passphrase can send (unencrypted) data that will be
successfully received by its peer.

- This option can be used in some specific situations when the user knows
both parties of the connection, so there's no possible situation of a rogue
sender and can be useful in situations where it is important to know whether a
connection is possible. The inability to decrypt an incoming transmission can
be then reported as a different kind of problem.

**IMPORTANT**: There is unusual and unobvious behavior when this flag is TRUE
on the caller and FALSE on the listener, and the passphrase was mismatched. On
the listener side the connection will be established and broken right after,
resulting in a short-lived "spurious" connection report on the listener socket.
This way, a socket will be available for retrieval from an `srt_accept` call
for a very short time, after which it will be removed from the listener backlog
just as if no connection attempt was made at all. If the application is fast
enough to react on an incoming connection, it will retrieve it, only to learn
that it is already broken. This also makes possible a scenario where
`SRT_EPOLL_IN` is reported on a listener socket, but then an `srt_accept` call
reports an `SRT_EASYNCRCV` error. How fast the connection gets broken depends
on the network parameters - in particular, whether the `UMSG_SHUTDOWN` message
sent by the caller is delivered (which takes one RTT in this case) or missed
during the interval from its creation up to the connection timeout (default = 5
seconds). It is therefore strongly recommended that you only set this flag to
FALSE on the listener when you are able to ensure that it is also set to FALSE
on the caller side.

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_TLPKTDROP`  | 1.0.6 | pre     | `bool`     |         | *         |        | RW  | GSD    |

- Too-late Packet Drop. When enabled on receiver, it skips missing packets that 
have not been delivered in time and delivers the subsequent packets to the 
application when their time-to-play has come. It also sends a fake ACK to the 
sender. When enabled on sender and enabled on the receiving peer, sender drops 
the older packets that have no chance to be delivered in time. It is automatically 
enabled in sender if receiver supports it.

- Default: true in Live mode, false in File mode (see `SRTO_TRANSTYPE`)

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_TRANSTYPE`  | 1.3.0 | pre     | `int32_t`  |  enum   |`SRTT_LIVE`| *      | W   | S      |

- Sets the transmission type for the socket, in particular, setting this option
sets multiple other parameters to their default values as required for a
particular transmission type.

- Values defined by enum `SRT_TRANSTYPE` (see above for possible values)

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_TSBPDMODE`  | 0.0.0 | pre     | `bool`     |         | *         |        | W   | S      |

- When true, use Timestamp-based Packet Delivery mode. In this mode the
packet's time is assigned at the sending time (or allowed to be predefined),
transmitted in the packet's header and restored on the receiver side so that
time distances between consecutive packets are preserved when delivering to
the application.

- Default: true in Live mode, false in File mode (see `SRTO_TRANSTYPE`).

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_UDP_RCVBUF` |       | pre     | `int32_t`  | bytes   | 8192 bufs | *      | RW  | GSD+   |

- UDP Socket Receive Buffer Size. Configured in bytes, maintained in packets 
based on MSS value. Receive buffer must not be greater than FC size.

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_UDP_SNDBUF` |       | pre     | `int32_t`  | bytes   | 65536     | *      | RW  | GSD+   |

- UDP Socket Send Buffer Size. Configured in bytes, maintained in packets based 
on `SRTO_MSS` value.

---

| OptName           | Since | Binding | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | ------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_VERSION`    | 1.1.0 |         | `int32_t`  |         |           |        | R   | S      |

- Local SRT version. This is the highest local version supported if not
  connected, or the highest version supported by the peer if connected.

- The version format in hex is 0x00XXYYZZ for x.y.z in human readable form.
For example, version 1.4.2 is encoded as `0x010402`.


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
