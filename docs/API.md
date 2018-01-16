
The SRT C API (defined in `srt.h` file) is largely based in design on the
legacy UDT API, however there are some important changes. The API contained in
`udt.h` file contains directly the legacy UDT API plus some minor optional
functions that require C++ standard library to be used. There are a few
optional C++ API functions stored there, as there's no real C++ API for SRT,
although these functions may be useful in some strict situations.

There are some example applications so that you can see how the API is being
used, there's srt-live-transmit application, as well as srt-file-transmit
and srt-multiplex. All SRT related things are contained in `transmitmedia.*`
files in `common` directory and it's commonly used by all applications.
See SrtSource::Read and SrtTarget::Write as an example of how data are being
read and written in SRT.

Setup and teardown
==================

Before any part of SRT C API can be used, the user should call `srt_startup()`
function first. Likewise, before the application exits, the `srt_cleanup()` function
should be called. Mind that one of the things the startup function does is
creating a new thread, so choose the point of execution for these functions
wisely.

Creating and destroying a socket
================================

In order to do anything using SRT, you have to create the SRT socket first.
Mind that the "socket" term in this case is chosen by logical similarity to
the system-wide sockets, but physically the SRT socket has, at least directly,
nothing to do with the system sockets. Like the system socket, it's used to
define the point of communication.

Synopsis
--------

    SRTSOCKET srt_socket(int af, int, int);
    void srt_close(SRTSOCKET s);

The form of `srt_socket` function is created after the legacy UDT API; except
the first parameter, the other two are ignored.

Note that `SRTSOCKET` is just an alias for `int`; this is a legacy naming convention
from UDT, which is here only for clarity.

Usage
-----

    sock = srt_socket(AF_INET, SOCK_DGRAM, 0);

This creates a socket, which can be next configured, and then used for communication.

    srt_close(sock);

This closes the socket and frees all its resources. Note that the true life of the
socket does not end exactly after this function exits - some details are being
finished in a separate "SRT GC" thread. Still, at least all shared system resources
should be released after this function exits (such as listener port).


Important Remarks
-----------------

1. Please note that the use of SRT with `AF_INET6` has not been fully tested,
use at your own risk.
2. As you know, SRT uses the system UDP protocol as an underlying communication
layer, and so it uses also UDP sockets. The underlying communication layer is
however used only instrumentally and SRT manages them as its own system resource
as it pleases - so there's nothnig unusual if multiple SRT sockets share one
UDP socket, or if one SRT socket uses multiple UDP sockets, should that be for
some cases reasonable.
3. The term of port used in SRT is occasionally identical with the term of UDP
port, however SRT offers more flexibility than UDP (or TCP, if we think about
more logical similarity) due to that it manages them as its own resources. For
example, one port may be even shared between various services.


Binding and connecting
======================

The connection establishment is being done with the same philosophy as with TCP
and it also uses functions with similar names and signatures as BSD Socket
API. The new thing towards TCP here is the _rendezvous_ mode.

Synopsis
--------

    int srt_bind(SRTSOCKET u, const struct sockaddr* name, int namelen);
    int srt_bind_peerof(SRTSOCKET u, UDPSOCKET udpsock);

This function sets up the "sockname" for the socket, that is, the local IP address
of the network device (use `INADDR_ANY` for using any device) and port. Note that
this can be done on both listening and connecting socket; for the latter it will
define the outgoing port. If you don't set up the outgoing port by calling this
function (or use port number 0), a unique port number will be selected automatically.

The `*_peerof` version simply copies the bound address setting from an existing UDP
socket.

    int srt_listen(SRTSOCKET u, int backlog);

This sets the backlog (maximum allowed simultaneously pending connections) and
turns the socket into listening state, that is, incomming connections will be
accepted in the call of `srt_accept`.

    SRTSOCKET srt_accept(SRTSOCKET u, struct sockaddr* addr, int* addrlen);

This function accepts the incoming connection (the peer should do
`srt_connect`) and returns a socket that is exclusively bound to an opposite
socket at the peer. The peer's address is returned in the passed `addr`
argument.

    int srt_connect(SRTSOCKET u, const struct sockaddr* name, int namelen);
    int srt_connect_debug(SRTSOCKET u, const struct sockaddr* name, int namelen, int forced_isn);

This function initiates the connection of given socket with its peer's counterpart
(the peer gets the new socket for this connection from `srt_accept`). The
address for connection is passed in 'name'. The `connect_debug` version allows
for enforcing the ISN (initial sequence number); this is predicted only for
debugging or unusual experiments.

    int srt_rendezvous(SRTSOCKET u, const struct sockaddr* local_name, int local_namelen,
        const struct sockaddr* remote_name, int remote_namelen);

A convenience function that combines the calls to bind, setting `SRTO_RENDEZVOUS` flag,
and connecting to the rendezvous counterpart. For simplest usage, the `local_name` should
be set to `INADDR_ANY` (or a specified adapter's IP) and port. Note that both `local_name`
and `remote_name` must use the same port. The peer to which this is going to connect,
should call the same function, with appropriate local and remote addresses. Rendezvous
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

        srt_setsockopt(m_sock, 0, UDT_RENDEZVOUS, &yes, sizeof yes);
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
`srt_recvmsg` is provided for convenience and backward compatibility, as it's
identical to `srt_recv`. The `srt_sendmsg` receives more parameters specifically
for messages. The `srt_sendmsg2` and `srt_recvmsg2` functions receive, beside
the socket and buffer, also the `SRT_MSGCTRL` object, which is an input-output
object specifying extra data for the operation.

The functions with `msg2` suffix use the `SRT_MSGCTRL` object, which have the
following interpretation (except `flags` and `boundary` that are reserved for
future use and should be 0):

* `srt_sendmsg2`:
    * msgttl: [IN] maximum time (in ms) to wait in sending buffer for being sent (-1 if unused)
    * inorder: [IN] if false, the later sent message is allowed to be delivered earlier
    * srctime: [IN] timestamp to be used for that sending (0 if current time)
    * pktseq: unused
    * msgno: [OUT]: message number assigned to the currently sent message

* `srt_recvmsg2`
    * msgttl, inorder: unused
    * srctime: [OUT] timestamp set for this dataset when sending
    * pktseq: [OUT] packet sequence number (first packet from the message, if it spans for multiple UDP packets)
    * msgno: [OUT] message number assigned to the currently received message

Please note that the `msgttl` and `inorder` arguments and fields in
`SRT_MSGCTRL` are meaningful only when you use the message API in file mode
(this will be explained later). In live mode, which is SRT default, packets
are always delivered when the time comes (so, always in order) and you rather
don't want a packet to be dropped before sending (so -1 should be passed here).

The `srctime` parameter is an SRT addition for applications (i.e. gateways)
forwarding SRT streams. It permits to pull and push the sender's original time
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


Transmission modes
------------------

How exactly the sender and receiver functions work, depends on the mode settings.
The main socket options - see below for full description - that control it are:

* `SRTO_TRANSTYPE`. It sets a bunch of parameters in accordance to the selected
mode:
    * `SRTT_LIVE` (default) the Live mode (for live stream transmissions)
    * `SRTT_FILE` the File mode (for a "no time controlled" fastest data transmission)
* `SRTO_MESSAGEAPI`
    * true: (default in Live mode): use Message API
    * false: (default in File mode): use Buffer API

We have then three cases (note that Live mode implies Message API):

* Live mode (default)

  In this mode, the application is expected to send single pieces of data
  that are already under sending speed control. Default size is 1316, which
  is 7 * 188 (MPEG TS unit size). With default settings in this mode, the
  receiver will be delivered payloads with the same time distances between them
  as the when they were sent, with a small delay (default 120ms).

* File mode, Buffer API (default when set `SRTT_FILE` mode)

  In this mode the application may deliver data with any speed and of any size,
  the facility will try to send them as long as there's buffer space for it.
  A single call for sending may send only fragment of the buffer, and the
  receiver will receive as much as is available and fits in the buffer.

* File mode, Message API (when `SRTO_TRANSTYPE` is `SRTT_FILE` and `SRTO_MESSAGEAPI` is true)

  In this mode the application delivers single pieces of data that have
  declared boundaries. The sending is accepted only when the whole message can
  be scheduled for sending, and the receiver will be given either the whole
  message, or nothing at all, including when the buffer is too small for the
  whole message.

The File mode and its Buffer and Message APIs are derived from UDT, just
implemented slightly different way; this will be explained below in
**HISTORICAL INFO** under "Transmission method: Message".


Blocking and non-blocking mode
==============================

SRT functions can also work in blocking and non-blocking mode, for which
there are two separate options for sending and receiving: `SRTO_SNDSYN` and
`SRTO_RCVSYN`. When the blocking mode is used, the function will not exit until
the availability condition is satisfied; in non-blocking mode the function
exits always immediately, and in case of lack of resource availability, it
returns an error with appropriate code. The use of non-blocking mode usually
requires to use some polling mechanism, which in SRT is **EPoll**.

Note also that the blocking and non-blocking mode matters not only for sending
and receiving. For example, SNDSYN defines blocking for `srt_connect` and
RCVSYN defines blocking for `srt_accept`. The SNDSYN also makes `srt_close`
exit only after the sending buffer is completely empty.


EPoll (non-blocking mode events)
================================

EPoll is a mechanism to track the events happening on the sockets, both "system
sockets" (see `SYSSOCKET` type) and SRT Sockets. Note that `SYSSOCKET` is also
an alias for `int`, used only for clarity.


Synopsis
--------

    int srt_epoll_update_usock(int eid, SRTSOCKET u, const int* events = NULL);
    int srt_epoll_update_ssock(int eid, SYSSOCKET s, const int* events = NULL);

SRT Usage
---------

SRT socket being a user level concept, the system epoll (or other select)
cannot be used to handle SRT non-blocking mode events. Instead, SRT provides a
user-level epoll that supports both SRT and system sockets.

The `srt_epoll_update_{u|s}sock()` API functions described here are SRT additions
to the UDT-derived `srt_epoll_add_{u|s}sock()` and `epoll_remove_{u|s}sock()`
functions to atomically change the events of interest. For example, to remove
EPOLLOUT but keep EPOLLIN for a given socket with the existing API, the socket
must be removed from epoll and re-added. This cannot be done atomically, the
thread protection (against the epoll thread) being applied within each function
but unprotected between the two calls. It is then possible to lose a POLLIN
event if it fires while the socket is not in the epoll list.

The SRT EPoll system does not supports all features of Linux epoll. For
example, it only supports level-triggered event.

Options
=======

There's a general method of setting options on a socket in SRT C API, similar
to the system setsockopt/getsockopt functions.

Synopsis
--------

Legacy version:

    void srt_getsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, void* optval, int& optlen);
    void srt_setsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, const void* optval, int optlen);

New version:

    void srt_getsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, void* optval, int& optlen);
    void srt_setsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, const void* optval, int optlen);

(In the legacy version, there's an additional, unused, `level` parameter. It was there
in original UDT API just to mimic the system `setsockopt` function).

Some options require value of type bool and some others of type int, which is
not the same - they differ in size and mistaking them may end up with a crash.
This must be kept in mind especially in any C wrapper. For convenience, the
setting option function may accept both `int` and `bool` types, but this is
not so in case of getting option value.

Almost all options from UDT library are derived (there's a few deleted, including
some deprecated already in UDT), many new SRT options have been added.
All options are available exclusively with `SRTO_` prefix. Old names are provided as
alias names in `udt.h` legacy/C++ API file. Mind the translation rules:
* `UDT_` prefix from UDT options was changed the prefix to `SRTO_`
* `UDP_` prefix from UDT options was changed the prefix to `SRTO_UDP_`
* `SRT_` prefix in older SRT versions was changed into `SRTO_`

The Binding column should define for these options one of the following
statement concerning setting a value:

* pre: for connecting socket it must be set prior to calling `srt_connect()`
and never changed thereafter. For listener socket it should be set to binding
socket and it will be derived by every socket returned by `srt_accept()`.
* post: this flag can be changed any time, including after the socket is
connected. On binding socket setting this flag is effective only to this
socket itself. Note though that there are some post-bound options that have
important meaning when set prior to connecting.

This option list is sorted alphabetically. Note that some options can be
either only retrieved value (r) or set (w).

| OptName         | Since | Binding | Type            | Units | Default | Range | Description |
| --- |
| `SRTO_CONNTIMEO` | 1.1.2 | pre  | `int` | msec | 3000 | tbd | Connect timeout. SRT cannot connect for RTT > 1500 msec (2 handshake exchanges) with the default connect timeout of 3 seconds. This option applies to the caller and rendezvous connection modes. The connect timeout is 10 times the value set for the rendezvous mode (which can be used as a workaround for this connection problem with earlier versions). |
| --- |
| `SRTO_EVENT` (r) |   | n/a  | `int32_t` |   | n/a | n/a | Connection epoll flags (see [epoll\_ctl](http://man7.org/linux/man-pages/man2/epoll_ctl.2.html)). One or more of the following flags: EPOLLIN | EPOLLOUT | EPOLLERR |
| --- |
| `SRTO_FC`          |   | pre  | `int` | pkts | 25600 | 32.. | Flight Flag Size. |
| --- |
| `SRTO_INPUTBW`     | 1.0.5 | post  | `int64_t` | bytes/secs | 0 | 0.. | Sender nominal input rate. Used along with OHEADBW, when MAXBW is set to relative (0), to calculate maximum sending rate when recovery packets are sent along with main media stream (INPUTBW * (100 + OHEADBW) / 100). If INPUTBW is not set while MAXBW is set to relative (0), the actual input rate is evaluated inside the library. |
| --- |
| `SRTO_IPTOS`       | 1.0.5 | pre  | `int32_t` |   | (platform default) | 0..255 | IP Type of Service. Applies to sender only. *Sender: user configurable, default: 0xB8* |
| --- |
| `SRTO_ISN` (r) | 1.3.0 | post | `int32_t` | sequence | n/a | n/a | The value of the ISN (Initial Sequence Number), which is the first sequence number put on a firstmost sent UDP packets carrying SRT data payload. *This value is useful for developers of some more complicated mathods of flow control, possibly with multiple SRT sockets at a time, not predicted in any regular development.* |
| --- |
| `SRTO_KMSTATE` (r) | 1.0.2 | n/a  | `int32_t` |   | n/a | n/a | Receiver Keying Material state. Available on both sender and receiver sides. Values defined in `enum SRT_KM_STATE`: | 
|                    |       |      |           |   |     |     | * `SRT_KM_S_UNSECURED`: unsecured: data not encrypted  | 
|                    |       |      |           |   |     |     | * `SRT_KM_S_SECURING`: securing: waiting for keying material |                                        
|                    |       |      |           |   |     |     | * `SRT_KM_S_SECURED`: secured: keying material obtained and operational (decrypting received data) |  
|                    |       |      |           |   |     |     | * `SRT_KM_S_NOSECRET`: no secret: no secret configured to handle keying material |                    
|                    |       |      |           |   |     |     | * `SRT_KM_S_BADSECRET`: bad secret: invalid secret configured  |                                      
| --- |
| `SRTO_IPTTL` | 1.0.5 | pre  | `int32_t` | hops | (platform default) | 1..255 | IP Time To Live. Applies to sender only. *Sender: user configurable, default: 64* |
| --- |
| `SRTO_LATENCY` | 0.0.0 | pre  | `int32_t` | msec | 0 | positive only | This flag sets both `SRTO_RCVLATENCY` and `SRTO_PEERLATENCY` to the same value. Note that prior to version 1.3.0 this is the only flag to set the latency, however this is effectively equivalent to setting `SRTO_PEERLATENCY`, when the side is sender (see `SRTO_SENDER`) and `SRTO_RCVLATENCY` when the side is receiver, and the bidirectional stream sending is not supported. |
| --- |
| `SRTO_LINGER` |   | pre | linger | secs | on (180) |   | Linger time on close (see [SO\_LINGER](http://man7.org/linux/man-pages/man7/socket.7.html)) *SRT recommended value: off (0)* |
| --- |
| `SRTO_LOSSMAXTTL` (writeonly) | 1.2.0 | pre | `int` | packets | 0 | reasonable | The value up to which the *Reorder Tolerance* may grow. When *Reorder Tolerance* is > 0, then packet loss report is delayed until that number of packets come in. *Reorder Tolerance* increases every time a "belated" packet has come, but it wasn't due to retransmission (that is, when UDP packets tend to come out of order), with the difference between the latest sequence and this packet's sequence, and not more than the value of this option. By default it's 0, which means that this mechanism is turned off, and the loss report is always sent immediately upon experiencing a "gap" in sequences.
| --- |
| `SRTO_MAXBW`  | 1.0.5 | pre  | `int64_t` | bytes/sec | -1 | -1 | 0 | 1.. | Maximum send bandwidth. -1: infinite (CSRTCC limit is 30mbps) =0: relative to input rate (SRT 1.0.5 addition, see `SRT_INPUTBW`) >0: absolute limit *SRT recommended value: 0 (relative)* |
| --- |
| `SRTO_MESSAGEAPI` (w) | 1.3.0 | pre | bool | boolean | true |  | When set, this socket uses the Message API[\*], otherwise it uses Buffer API |
| --- |
| `SRTO_MINVERSION` (writeonly) | 1.3.0 | pre | `int32_t` | version | 0 | up to current | The minimum SRT version that is required from the peer. A connection to a peer that does not satisfy the minimum version requirement will be rejected. |
| --- |
| `SRTO_MSS`|  | pre  | `int` | bytes|1500 | 76.. | Maximum Segment Size. Used for buffer allocation and rate calculation using packet counter assuming fully filled packets. The smallest MSS between the peers is used. *This is 1500 by default in the overall internet. This is the maximum size of the UDP packet and can be only decreased, unless you have some unusual dedicated network settings.* |
| --- |
| `SRTO_NAKREPORT` | 1.1.0 | pre  | `bool` |   | true | true|false | Receiver will send `UMSG_LOSSREPORT` messages periodically until the lost packet is retransmitted or intentionally dropped |
| --- |
| `SRTO_OHEADBW`   | 1.0.5 | post  | `int` | % | 25 | 5..100 | Recovery bandwidth overhead above input rate (see `SRT_INPUTBW`). *Sender: user configurable, default: 25%.* ***To do: set-only. get should be supported.*** |
| --- |
| `SRTO_PASSPHRASE` (w) | 0.0.0 | pre | string |   | [0] | [10..79] | HaiCrypt Encryption/Decryption Passphrase.  The passphrase is the shared secret between the sender and the receiver. It is used to generate the Key Encrypting Key using [PBKDF2](http://en.wikipedia.org/wiki/PBKDF2) (Password-Based Key Derivation Function 2). It is used on the sender if PBKEYLEN is non zero. It is used on the receiver only if the received data is encrypted.  The configured passphrase cannot be get back (write-only). *Sender and receiver: user configurable.* |
| --- |
| `SRTO_PAYLOADSIZE` (w) | 1.3.0 | pre | int | bytes | 1316 (Live) | up to MTUsize-28-16, usually 1456 | Sets the maximum declared size of a single call to sending function in Live mode. Use 0 if this value isn't used (which is default in file mode)
| --- |
| `SRTO_PBKEYLEN` | 0.0.0 | pre  | `int32_t` | bytes | 0 | 0 16(128/8) 24(192/8) 32(256/8) | Sender encryption key length. Enable sender encryption if not 0. Not required on receiver (set to 0), key size obtained from sender in HaiCrypt handshake. *Sender: user configurable.* |
| --- |
| `SRTO_PEERLATENCY` | 1.3.0 | pre  | `int32_t` | msec | 0 | positive only | The latency value (as described in `SRTO_RCVLATENCY`) that is set by the sender side as a minimum value for the receiver. |
| --- |
| `SRTO_PEERVERSION` (r) | 1.1.0 | n/a  | `int32_t` | n/a | n/a | n/a | Peer SRT version. The value 0 is returned if not connected, SRT handshake not yet performed, or if peer is not SRT. See `SRTO_VERSION` for the version format. |
| `SRTO_RCVBUF` |   | pre  | `int` | bytes | 8192 * (1500-28) | 32 * (1500-28) ..FC * (1500-28) | Receive Buffer Size. *Receive buffer must not be greater than FC size.* ***Warning: configured in bytes, converted in packets when set based on MSS value. For desired result, configure MSS first.*** |
| --- |
| `SRTO_RCVDATA` (r) |   | n/a  | `int32_t` | pkts | n/a |   | Size of the available data in the receive buffer. |
| --- |
| `SRTO_RCVKMSTATE` (readonly) | 1.2.0 | post | enum | n/a | n/a | KM state on the agent side when it's a receiver, as per `SRTO_KMSTATE` |
| --- |
| `SRTO_RCVLATENCY` | 1.3.0 | pre  | `int32_t` | msec | 0 | positive only | The time that should elapse since the moment when the packet was sent and the moment when it's delivered to the receiver application in the receiving function. This time should be a buffer time large enough to cover the time spent for sending, unexpectedly extended RTT time, and the time needed to retransmit the lost UDP packet. The effective latency value will be the maximum of this options' value and the value of `SRTO_PEERLATENCY` set by the peer side. **This option in pre-1.3.0 version is available only as** `SRTO_LATENCY`. |
| --- |
| `SRTO_RCVSYN` |   | pre  | `bool` |   | true | true | false | Synchronous (blocking) receive mode |
| --- |
| `SRTO_RCVTIMEO` |   | post  | `int` | msecs | -1 | -1.. | Blocking mode receiving timeout (-1: infinite) |
| --- |
| `SRTO_RENDEZVOUS` |   | pre  | `bool` |   | false | true | false | Use Rendez-Vous connection mode (both sides must set this and both must use bind/connect to one another. |
| --- |
| `SRTO_REUSEADDR` |   | pre |   |   | true | true | false | Reuse existing address (see [SO\_REUSEADDR](http://man7.org/linux/man-pages/man7/socket.7.html)) |
| --- |
| `SRTO_SENDER`   | 1.0.4 | pre     | `int32_t` bool? |       | false   |       | Set sender side. The side that sets this flag is expected to be a sender. It's required when any of two connection sides supports at most *HSv4* handshake, and the sender side is the side that initiates the SRT extended handshake (which won't be done at all, if none of the sides sets this flag). This flag is superfluous, if **both** parties are at least version 1.3.0 and therefore support *HSv5* handshake, where the SRT extended handshake is done with the overall handshake process. This flag is however **obligatory** if at least one party is SRT below version 1.3.0 and does not support *HSv5*.
| --- |
| `SRTO_SMOOTHER` (w) | 1.3.0 | pre | `const char*` | predefined | "live" | "live" or "file" | The type of Smoother used for the transmission for that socket, which is responsible for the transmission and congestion control. The Smoother type must be exactly the same on both connecting parties, otherwise the connection is rejected. ***TODO: might be reasonable to allow an "adaptive" value of the Smoother, which will accept either of smoother type when the other party enforces it, and rejected if both sides are "adaptive"***
| --- |
| `SRTO_SNDBUF` |   | pre  | `int` | bytes | 8192 * (1500-28) |   | Send Buffer Size.  ***Warning: configured in bytes, converted in packets, when set, based on MSS value. For desired result, configure MSS first.*** |
| --- |
| `SRTO_SNDDATA` (read-only) |   | n/a  | `int32_t` | pkts | n/a | n/a | Size of the unacknowledged data in send buffer. |
| --- |
| `SRTO_SNDPEERKMSTATE` (readonly) | 1.2.0 | post | enum | n/a | n/a | Peer KM state on receiver side for `SRTO_KMSTATE` |
| --- |
| `SRTO_SNDSYN`|  | post  | `bool` |  |true | true | false | Synchronous (blocking) send mode |
| --- |
| `SRTO_SNDTIMEO`|  | post  | `int` | msecs|-1 | -1.. | Blocking mode sending timeout (-1: infinite) |
| --- |
| `SRTO_STATE` (r) |   | n/a  | `int32_t` |   | n/a | n/a | UDT connection state. |
| --- |
| `SRTO_STREAMID` (rw) | 1.3.0 | pre | `const char*` |    | empty | any string | A string limited to 512 characters that can be set on the socket prior to connecting. This stream ID will be able to be retrieved by the listener side from the socket that is returned from `srt_accept` and was connected by a socket with that set stream id. SRT does not enforce any special interpretation of the contents of this string. As this uses internally the `std::string` type, there are additional functions for it in the legacy/C++ API: `UDT::setstreamid` and `UDT::getstreamid`. |
| --- |
| `SRTO_TLPKTDROP`   | 1.0.6 | pre  | `int32_t` bool? |   | true | true | false | Too-late Packet Drop. When enabled on receiver, it skips missing packets that have not been delivered in time and deliver the following packets to the application when their time-to-play has come. It also send a fake ACK to sender. When enabled on sender and enabled on the receiving peer, sender drops the older packets that have no chance to be delivered in time. It was automatically enabled in sender if receiver supports it. |
| --- |
| `SRTO_TRANSTYPE` (w) | 1.3.0 | pre | enum |  | `SRTT_LIVE` | alt: `SRTT_FILE` | Sets the transmission type for the socket, in particular, setting this option sets multiple other parameters to their default values as required for particular transmission type. |
| --- |
| `SRTO_TSBPDMODE`   | 0.0.0 | pre | `int32_t` (bool?) |   | false | true | false | Timestamp-based Packet Delivery mode. This flag is set to _true_ by default and as a default flag set in live mode. |
| --- |
| `SRTO_UDP_RCVBUF` |   | pre  | `int` | bytes | 8192 * 1500 | MSS.. | UDP Socket Receive Buffer Size.  Configured in bytes, maintained in packets based on MSS value. Receive buffer must not be greater than FC size. |
| --- |
| `SRTO_UDP_SNDBUF` |   | pre  | `int` | bytes | 65536 | MSS.. | UDP Socket Send Buffer Size. Configured in bytes, maintained in packets based on `UDT_MSS` value. *SRT recommended value:* `1024*1024` |
| --- |
| `SRTO_VERSION` (r) | 1.1.0 | n/a  | `int32_t` |   | n/a | n/a | Local SRT version. This is the highest local version supported if not connected, or the highest version supported by the peer if connected. The version format in hex is 0xXXYYZZ for x.y.z in human readable form, where x = ("%d", (version>>16) & 0xff), etc... Set could eventually be supported to test |
| --- |


Transmission types
------------------

SRT has been mainly created for Live Streaming and therefore its main and
default transmission type is "live". SRT supports, however, the modes that
the original UDT library supported, that is, file and message transmission.

There are two general modes: Live and File transmission. Inside File
file transmission mode, there are also two possibilities: Buffer API
and Message API. The Live mode uses Message API, however it doesn't
exactly match the description of the Message API because it uses maximum
single sending buffer up to the size that fits in one UDP packet.

There are two options to set particular type:

* `SRTO_TRANSTYPE`: uses the enum value with `SRTT_LIVE` for live mode
   and `SRTT_FILE` for file mode. This option actually changes a bunch
   of parameters to their default values for that mode. After this is
   done, further parameters, including those that are set here, can
   be further changed.
* `SRTO_MESSAGEAPI`: This sets the Message API (true) or Buffer API (false)

This makes the total of three possible data transmission methods:

* Live
* Buffer
* Message

**MIND THE TERMS** used below:
* HANGUP and RESUME: "Function HANGS UP" means that it returns
an error from the `MJ_AGAIN` category (see `SRT_EASYNC*`, `SRT_ETIMEOUT` and
`SRT_ECONGEST` symbols from `SRT_ERRNO` enumeration type), if it's in
non-blocking mode, and in blocking mode it will block until the condition that
caused the HANGUP no longer applies, which is defined as that the function
RESUMES. In nonblocking mode, the function RESUMES when the call to it has done
something and returned the non-error status. The blocking mode in SRT is
separate for sending and receiving and set by `SRTO_SNDSYN` and `SRTO_RCVSYN`
options respectively
* BLIND REXMIT: A situation that packets that were sent are still not
acknowledged, either in expected time frame, or just another ACK has
come for the same number, but no packets have been reported as lost,
or at least not for all still unacknowledged packets. The Smoother
class is responsible for the algorithm for taking care of this
situation, which is either FASTREXMIT or LATEREXMIT, this will be
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
* `SRTO_SMOOTHER` = "live"

In this mode, every call to a sending function is allowed to send only
so much data as declared by `SRTO_PAYLOADSIZE`, which's value is still
limited to maximum 1456 bytes. The application that does the sending
is by itself responsible to call the sending function in appropriate
time distances between subsequent calls. By default, it implies that
the receiver use 120ms of latency, which is the declared time distance
between the moment when the packet is scheduled for sending at the
sender side, and when it is received by the receiver application (that
is, the data are kept in the buffer and declared as not received, until
the time comes for the packet to "play" it).

This mode uses the `LiveSmoother` Smoother class for congestion control, which
puts only a slightly limitation on the bandwidth, if needed, just to add extra
time, if the distance between two consecutive packets would be too short as for
the defined speed limit. Note that this smoother is not predicted to work in a
condition of "virtually infinite" ingest speed (such as, for example, reading
directly from file). Therefore the application is not allowed to stream data
with maximum speed - it must take care that the speed of data sending comes in
rhythm with timestamps in the live stream, otherwise the behavior is undefined
and might be surprisingly disappointing.

The reading function will always return only such a payload that was
sent, and it will HANGUP until the time to play has come for this
packet (if TSBPD mode is on) or when it's available without gaps of
lost packets (if TSBPD mode is off - see `SRTO_TSBPDMODE`).

You may wish to tweak some of the parameters further:
* `SRTO_TSBPDMODE`: you can turn off controlled latency, if your
application uses some alternative and its own method of latency control
* `SRTO_RCVLATENCY`: you can increase the latency time, if this is
too short (setting shorter latency than default is strongly
discouraged, although in some very specific and dedicated networks
this may still have reason). Note that `SRTO_PEERLATENCY` is an option
for the sending party, which is the minimum possible value for receiver.
* `SRTO_TLPKTDROP`: When true (default), then it will drop the packets
that haven't been retransmitted on time, that is, before the next packet
that is already received becomes ready to play. You can turn this off and this
way have a clean delivery always, however a lost packet can simply pause a
delivery for some longer, potentially undefined time, and cause this way even
worse tearing for the player. Setting higher latency will help much more in
case when TLPKTDROP causes packet drops too often.
* `SRTO_NAKREPORT`: Turns on repeated sending of lossreport, when the lost
packet was not recovered quick enough, which raises suspicions that the
lossreport itself was lost. Without it, the lossreport will be always reported
just once and never repeated again, and then the lost payload packet will
be probably dropped by TLPKTDROP mechanism.
* `SRTO_PAYLOADSIZE`: Default value is for MPEG TS; if you are going
to use SRT to send any different kind of payload, which is - for example
- wrapping a live stream in very small frames, then you can use a bigger
maximum frame size, of course, not greater than 1456 bytes.

Parameters from the list of modified for transmission type list, and not
mentioned in this list above, are crucial for Live mode and shall not be
changed.

The BLIND REXMIT situation is resolved using FASTREXMIT algorithm by
LiveSmoother: sending non-acknowledged packets blindly on such a
premise that the receiver lingers too long with acknowledging them.
This mechanism isn't used (that is, BLIND REXMIT situation isn't
handled at all) when `SRTO_NAKREPORT` is set by the peer - NAKREPORT
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
* `SRTO_SMOOTHER` = "file"

In this mode, calling a sending function is allowed to potentially send
virtually any size of data. The sending function will HANGUP only if the
sending buffer is completely replete, and RESUME if the sending buffers are
available for at least one smallest portion of data passed for sending. The
sending function need not send everything in this call and the caller must
be aware of that the sending function might return less size of sent data
than was actually requested.

From the receiving function there will be retrieved as many data as the minimum
of the passed buffer size and available data; data still available and not
retrieved at this call will be available for retrieval in the next call.

For this mode only, there's also a dedicated pair of functions that can
only be used in this mode: `srt_sendfile` and `srt_recvfile`. These
functions can be used to transmit the whole file, or a fragment of it,
basing on the offset and size.

This mode uses the `FileSmoother` Smoother class for congestion control,
which is a direct copy of the UDT's `CUDTCC` congestion control class,
adjusted to the needs of SRT's Smoother framework. This class generally
sends the data with maximum speed in the beginning, until the flight
window is full, and then keeps the speed at the edge of the flight
window, only slowing down in case when packet loss was detected. The
bandwidth usage can be directly limited by `SRTO_MAXBW` option.

The BLIND REXMIT situation is resolved in FileSmoother using the LATEREXMIT
algorithm: when the repeated ACK was received for the same packet, or when the
loss list is empty and the flight window is full, all packets since the last
ACK are sent again (that's more-less the TCP behavior, just in contrast to
TCP, this is done as a very little probable fallback).

As you can see by the parameters described above, most of them have
`false` or `0` values - as they usually designate features used in
Live mode, so simply none of them is used when File mode is used.
The only option that makes sense to be altered after the `SRTT_FILE`
type was set is `SRTO_MESSAGEAPI`, which is described below.


Transmission method: Message
----------------------------

Setting `SRTO_TRANSTYPE` to `SRTT_FILE` and then `SRTO_MESSAGEAPI` to
true implies the Message transmission method. Parameters are set as
described above for Buffer method, with the exception of `SRTO_MESSAGEAPI`, and
the FileSmoother is also used in this mode. It differs to Buffer method,
however, by the rules concerning sending and receiving.

**HISTORICAL INFO**: The library that SRT was based on, UDT, was using a little
bit misleading terms of STREAM and DGRAM, and used the system symbols
`SOCK_STREAM` and `SOCK_DGRAM` in the socket creation function. The "datagram"
in the UDT terminology has however nothing to do with the "datagram" term in
the networking terminology, where its size is limited to as much it can fit in
one MTU. In UDT it's actually a message, which may span through multiple UDP
packets and has clearly defined boundaries. It's something rather similar to
the **SCTP** protocol. In UDP also the API functions were strictly bound to
DGRAM or STREAM mode: `UDT::send/UDT::recv` were only for STREAM and
`UDT::sendmsg/UDT::recvmsg` only for DGRAM. In SRT this is changed: all
functions can be used in all modes, except `srt_sendfile/srt_recvfile`, and how
the functions actually work, it's controlled by `SRTO_MESSAGEAPI` flag.

The message mode causes that every sending function sends **exactly** as much
data as it is passed in a single sending function call, and the receiver
receives also and not less than **exactly** that number of bytes that
was sent (although every message may be of different size, of course). Every
message may also have extra parameters:
* TTL, defines how long time (in ms) the message should wait in the sending
buffer for the opportunity to be picked up by the sender thread and sent
over the network, otherwise it's dropped
* INORDER, when true, then the messages must be read by the receiver in exactly
the same order as when they were sent. Otherwise a message that was sent
later may happen to get delivered earlier, should it happen that the
earlier sent message suffered a packet loss, so the succeeding message
achieved completion status prior to recovering the preceding message

The sending function will HANGUP when the free space in the sending
buffer does not fit the exactly whole message, and it will only RESUME
if the free space in the sending buffer grows up to this size. The
call to the sending function also returns with an error, when the
size of the message exceeds the total size of the buffer (this can
be modified by `SRTO_SNDBUF` option). In other words, it's not
predicted to send just a part of the message - either whole message
is being sent, or nothing at all.

The receiving function will HANGUP until the whole message is available
for reading; if it spans for multiple UDP packets, then the function
RESUMES only when every single packet from the message has been
received, including possibly recovered. When INORDER flag was set
to false and parts of multiple messages are currently available,
the first message that is complete (possibly recovered) is returned,
otherwise the function does a HANGUP until the exactly next message
is complete. The call to the receiving function is rejected if the
buffer size is too small for a single message to fit in it.

Note that you can use any of the sending and receiving functions
for sending and receiving messages, except sendfile/recvfile, which
are dedicated exclusively for Buffer API. 



