# SRT API Socket Options

There is a general method of setting options on a socket in the SRT C API, similar
to the system `setsockopt/getsockopt` functions.

- [Types Used in Socket Options](#types-used-in-socket-options)
- [Getting and Setting Options](#getting-and-setting-options)
- [List of Options](#list-of-options)

## Types Used in Socket Options <a name="sockopt_types"></a>

Possible types of socket options are:

- `int32_t` - a 32-bit integer. On most systems similar to `int`.
In some cases the value is expressed using an enumeration type
(see [Enumeration types...](#enumeration_types) section below).

- `int64_t` - a 64-bit integer.

- `bool` - a Boolean type (`<stdbool.h>` for C, or built-in
for C++). When *setting* an option, passing the value through an `int` type is
also properly recognized. When *getting* an option, however, the`bool` type
should be used. It is also possible to pass a variable of `int` type initialized
with 0 and then comparing the resulting value with 0 (just don't compare
the result with 1 or `true`).

- `string` - a C string. When *setting* an option, a `const char*` character array pointer
is expected to be passed in `optval` and the array length in `optlen` **without the terminating NULL character**.
When *getting*, an array is expected to be passed in `optval` with a
sufficient size **with an extra space for the terminating NULL character** provided in `optlen`.
The return value of `optlen` does not count the terminating NULL character.

- `linger` - Linger structure. Used exclusively with `SRTO_LINGER`.

### Enumeration Types Used in Options <a name="enumeration_types"></a>

#### `SRT_TRANSTYPE`

Used by `SRTO_TRANSTYPE` option:

- `SRTT_LIVE`: Live mode.
- `SRTT_FILE`: File mode.

See [Transmission Types](API.md#transmission-types) for details.

#### `SRT_KM_STATE`

The defined encryption state as performed by the Key Material Exchange, used
by `SRTO_RCVKMSTATE`, `SRTO_SNDKMSTATE` and `SRTO_KMSTATE` options:

- `SRT_KM_S_UNSECURED` (`0`): no encryption/decryption. If this state is only on
the receiver, received encrypted packets will be dropped.

- `SRT_KM_S_SECURING`(`1`): pending security (HSv4 only). This is a temporary state
used only if the connection uses HSv4 and the Key Material Exchange is
not finished yet. On HSv5 this is not possible because the Key Material
Exchange for the initial key is done in the handshake.

- `SRT_KM_S_SECURED` (`2`): KM exchange was successful and the data will be sent
encrypted and will be decrypted by the receiver. This state is only possible on
both sides in both directions simultaneously.

- `SRT_KM_S_NOSECRET` (`3`): If this state is in the sending direction (`SRTO_SNDKMSTATE`), 
then it means that the sending party has set a passphrase, but the peer did not. 
In this case the sending party can receive unencrypted packets from the peer, but 
packets it sends to the peer will be encrypted and the peer will not be able to 
decrypt them. This state is only possible in HSv5.

- `SRT_KM_S_BADSECRET` (`4`): The password is wrong (set differently on each party);
encrypted payloads won't be decrypted in either direction.

- `SRT_KM_S_BADCRYPTOMODE` (`5`): The crypto mode mode configuration is either not supported
or mismatches the configuration of the peer.


Note that with the default value of `SRTO_ENFORCEDENCRYPTION` option (true),
the state is equal on both sides in both directions, and it can be only
`SRT_KM_S_UNSECURED` or `SRT_KM_S_SECURED` (in other cases the connection
is rejected). Otherwise it may happen that either both sides have different
passwords and the state is `SRT_KM_S_BADSECRET` in both directions, or only
one party has set a password, in which case the KM state is as follows:

|                          | `SRTO_RCVKMSTATE`    | `SRTO_SNDKMSTATE`    |
|--------------------------|----------------------|----------------------|
| Party with no password:  | `SRT_KM_S_NOSECRET`  | `SRT_KM_S_UNSECURED` |
| Party with password:     | `SRT_KM_S_UNSECURED` | `SRT_KM_S_NOSECRET`  |

## Getting and Setting Options

Legacy version:

    int srt_getsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, void* optval, int& optlen);
    int srt_setsockopt(SRTSOCKET socket, int level, SRT_SOCKOPT optName, const void* optval, int optlen);

New version:

    int srt_getsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, void* optval, int& optlen);
    int srt_setsockflag(SRTSOCKET socket, SRT_SOCKOPT optName, const void* optval, int optlen);

In the legacy version, there's an additional unused `level` parameter. It was 
there in the original UDT API just to mimic the system `setsockopt` function, 
but it's ignored.

Some options require a value of type `bool` while others require type `integer`,
which is not the same -- they differ in size, and mistaking them may end up
causing a crash. This must be kept in mind especially in any C wrapper. For
convenience, the *setting* option function may accept both `int32_t` and `bool`
types, but this is not so in the case of *getting* an option value.

**UDT project legacy note**: Almost all options from the UDT library are
derived (there are a few deleted, including some deprecated already in UDT).
Many new SRT options have been added. All options are available exclusively
with the `SRTO_` prefix. Old names are provided as alias names in the `udt.h`
legacy/C++ API file. Note the translation rules:

* `UDT_` prefix from UDT options was changed to the prefix `SRTO_`
* `UDP_` prefix from UDT options was changed to the prefix `SRTO_UDP_`
* `SRT_` prefix in older SRT versions was changed to `SRTO_`

The [table below](#list-of-options) provides a complete list of SRT options and 
their characteristics according to the following legend:

1. **Since**: Defines the SRT version when this option was first introduced. If this field
is empty, it's an option derived from UDT. "Version 0.0.0" is the oldest version
of SRT ever created and put into use.

2. **Restrict**: Defines restrictions on setting the option. The field is empty if the option
is not settable (see **Dir** column):

    - `pre-bind`: The option cannot be altered on a socket that is already bound (by calling
`srt_bind()` or any other function doing this, including automatic binding when trying to
connect, as well as accepted sockets). In other words, once an SRT socket has transitioned from
`SRTS_INIT` to `SRTS_OPENED` socket state.

    - `pre`: The option cannot be altered on a socket that is in `SRTS_LISTENING`, `SRTS_CONNECTING`
or `SRTS_CONNECTED` state. If an option was set on a listener socket, it will be inherited
by a socket returned by `srt_accept()` (except for `SRTO_STREAMID`).

    - `post`: The option is unrestricted and can be altered at any time, including when the
socket is connected, as well as on an accepted socket. The setting of this flag on a listening 
socket is usually derived by the accepted socket, but this isn't a rule for all options.
Note though that there are some unrestricted options that have an important meaning when
set prior to connecting (different one than for a connected socket).

   **NOTE**: The `pre-bind` characteristic applies exclusively to options that:

   - Change the behavior and functionality of the `srt_bind` call
   - Concern or set an option on the internally used UDP socket
   - Concern any kind of resource used by the multiplexer

3. **Type**: The data type of the option (see above).

4. **Units**: Roughly specified unit, if the value defines things like length or time.
It can also define more precisely what kind of specialization can be used
when the type is integer:

    - `enum`: the possible values are defined in an enumeration type
    - `flags`: the integer value is a collection of bit flags
    - `B/s` - bytes per second.

5. **Default**: The exact default value, if it can be easily specified. A more complicated
default state of a particular option will be explained in the [description](#option-descriptions)
(when marked by asterisk). For non-settable options this field is empty.

6. **Range**: If a value of an integer type has a limited range, or only a certain value
allowed, it will be specified here (otherwise empty). A range value can be
specified as:

        - `X-... `: specifies only a minimum value
        - `X-Y,Z `: values between X and Y are allowed, and additionally Z

    - If the value is of `string` type, this field will contain its maximum size
in square brackets.

    - If the range contains additionally an asterisk, it means that more elaborate
restrictions on the value apply, as explained in the [description](#option-descriptions).

7. **Dir**: Option direction: W if can be set, R if can be retrieved, RW if both.

8. **Entity**: This describes whether the option can be set on the socket or the group.
The G and S options may appear together, in which case both possibilities apply.
The D and I options, mutually exclusive, appear always with G. 
The + marker can only coexist with GS. Possible specifications are:

    - S: This option can be set on a single socket (exclusively, if not GS)

    - G: This option can be set on a group (exclusively, if not GS)

    - D: If set on a group, it will be derived by the member socket

    - I: If set on a group, it will be taken and managed exclusively by the group

    - +: This option is also allowed to be set individually on a group member
      socket through a configuration object in `SRT_SOCKGROUPCONFIG` prepared by
      `srt_create_config`. Note that this setting may override the setting derived
      from the group.

## List of Options

The following table lists SRT API socket options in alphabetical order. Option details are given further below.

| Option Name                                             | Since | Restrict | Type      | Units   | Default           | Range    | Dir |Entity |
| :------------------------------------------------------ | :---: | :------: | :-------: | :-----: | :---------------: | :------: |:---:|:-----:|
| [`SRTO_BINDTODEVICE`](#SRTO_BINDTODEVICE)               | 1.4.2 | pre-bind | `string`  |         |                   |          | RW  | GSD+  |
| [`SRTO_CONGESTION`](#SRTO_CONGESTION)                   | 1.3.0 | pre      | `string`  |         | "live"            | \*       | W   | S     |
| [`SRTO_CONNTIMEO`](#SRTO_CONNTIMEO)                     | 1.1.2 | pre      | `int32_t` | ms      | 3000              | 0..      | W   | GSD+  |
| [`SRTO_CRYPTOMODE`](#SRTO_CRYPTOMODE)                   | 1.6.0-dev | pre      | `int32_t` |     | 0 (Auto)          | [0, 3]   | W   | GSD   |
| [`SRTO_DRIFTTRACER`](#SRTO_DRIFTTRACER)                 | 1.4.2 | post     | `bool`    |         | true              |          | RW  | GSD   |
| [`SRTO_ENFORCEDENCRYPTION`](#SRTO_ENFORCEDENCRYPTION)   | 1.3.2 | pre      | `bool`    |         | true              |          | W   | GSD   |
| [`SRTO_EVENT`](#SRTO_EVENT)                             |       |          | `int32_t` | flags   |                   |          | R   | S     |
| [`SRTO_FC`](#SRTO_FC)                                   |       | pre      | `int32_t` | pkts    | 25600             | 32..     | RW  | GSD   |
| [`SRTO_GROUPCONNECT`](#SRTO_GROUPCONNECT)               | 1.5.0 | pre      | `int32_t` |         | 0                 | 0...1    | W   | S     |
| [`SRTO_GROUPMINSTABLETIMEO`](#SRTO_GROUPMINSTABLETIMEO) | 1.5.0 | pre      | `int32_t` | ms      | 60                | 60-...   | W   | GDI+  |
| [`SRTO_GROUPTYPE`](#SRTO_GROUPTYPE)                     | 1.5.0 |          | `int32_t` | enum    |                   |          | R   | S     |
| [`SRTO_INPUTBW`](#SRTO_INPUTBW)                         | 1.0.5 | post     | `int64_t` | B/s     | 0                 | 0..      | RW  | GSD   |
| [`SRTO_IPTOS`](#SRTO_IPTOS)                             | 1.0.5 | pre-bind | `int32_t` |         | (system)          | 0..255   | RW  | GSD   |
| [`SRTO_IPTTL`](#SRTO_IPTTL)                             | 1.0.5 | pre-bind | `int32_t` | hops    | (system)          | 1..255   | RW  | GSD   |
| [`SRTO_IPV6ONLY`](#SRTO_IPV6ONLY)                       | 1.4.0 | pre-bind | `int32_t` |         | (system)          | -1..1    | RW  | GSD   |
| [`SRTO_ISN`](#SRTO_ISN)                                 | 1.3.0 |          | `int32_t` |         |                   |          | R   | S     |
| [`SRTO_KMPREANNOUNCE`](#SRTO_KMPREANNOUNCE)             | 1.3.2 | pre      | `int32_t` | pkts    | 0: 2<sup>12</sup> | 0.. \*   | RW  | GSD   |
| [`SRTO_KMREFRESHRATE`](#SRTO_KMREFRESHRATE)             | 1.3.2 | pre      | `int32_t` | pkts    | 0: 2<sup>24</sup> | 0..      | RW  | GSD   |
| [`SRTO_KMSTATE`](#SRTO_KMSTATE)                         | 1.0.2 |          | `int32_t` | enum    |                   |          | R   | S     |
| [`SRTO_LATENCY`](#SRTO_LATENCY)                         | 1.0.2 | pre      | `int32_t` | ms      | 120 \*            | 0..      | RW  | GSD   |
| [`SRTO_LINGER`](#SRTO_LINGER)                           |       | post     | `linger`  | s       | off \*            | 0..      | RW  | GSD   |
| [`SRTO_LOSSMAXTTL`](#SRTO_LOSSMAXTTL)                   | 1.2.0 | post     | `int32_t` | packets | 0                 | 0..      | RW  | GSD+  |
| [`SRTO_MAXBW`](#SRTO_MAXBW)                             |       | post     | `int64_t` | B/s     | -1                | -1..     | RW  | GSD   |
| [`SRTO_MESSAGEAPI`](#SRTO_MESSAGEAPI)                   | 1.3.0 | pre      | `bool`    |         | true              |          | W   | GSD   |
| [`SRTO_MININPUTBW`](#SRTO_MININPUTBW)                   | 1.4.3 | post     | `int64_t` | B/s     | 0                 | 0..      | RW  | GSD   |
| [`SRTO_MINVERSION`](#SRTO_MINVERSION)                   | 1.3.0 | pre      | `int32_t` | version | 0x010000          | \*       | RW  | GSD   |
| [`SRTO_MSS`](#SRTO_MSS)                                 |       | pre-bind | `int32_t` | bytes   | 1500              | 76..     | RW  | GSD   |
| [`SRTO_NAKREPORT`](#SRTO_NAKREPORT)                     | 1.1.0 | pre      | `bool`    |         |  \*               |          | RW  | GSD+  |
| [`SRTO_OHEADBW`](#SRTO_OHEADBW)                         | 1.0.5 | post     | `int32_t` | %       | 25                | 5..100   | RW  | GSD   |
| [`SRTO_PACKETFILTER`](#SRTO_PACKETFILTER)               | 1.4.0 | pre      | `string`  |         | ""                | [512]    | RW  | GSD   |
| [`SRTO_PASSPHRASE`](#SRTO_PASSPHRASE)                   | 0.0.0 | pre      | `string`  |         | ""                | [10..79] | W   | GSD   |
| [`SRTO_PAYLOADSIZE`](#SRTO_PAYLOADSIZE)                 | 1.3.0 | pre      | `int32_t` | bytes   | \*                | 0.. \*   | W   | GSD   |
| [`SRTO_PBKEYLEN`](#SRTO_PBKEYLEN)                       | 0.0.0 | pre      | `int32_t` | bytes   | 0                 | \*       | RW  | GSD   |
| [`SRTO_PEERIDLETIMEO`](#SRTO_PEERIDLETIMEO)             | 1.3.3 | pre      | `int32_t` | ms      | 5000              | 0..      | RW  | GSD+  |
| [`SRTO_PEERLATENCY`](#SRTO_PEERLATENCY)                 | 1.3.0 | pre      | `int32_t` | ms      | 0                 | 0..      | RW  | GSD   |
| [`SRTO_PEERVERSION`](#SRTO_PEERVERSION)                 | 1.1.0 |          | `int32_t` | *       |                   |          | R   | GS    |
| [`SRTO_RCVBUF`](#SRTO_RCVBUF)                           |       | pre-bind | `int32_t` | bytes   | 8192 payloads     | \*       | RW  | GSD+  |
| [`SRTO_RCVDATA`](#SRTO_RCVDATA)                         |       |          | `int32_t` | pkts    |                   |          | R   | S     |
| [`SRTO_RCVKMSTATE`](#SRTO_RCVKMSTATE)                   | 1.2.0 |          | `int32_t` | enum    |                   |          | R   | S     |
| [`SRTO_RCVLATENCY`](#SRTO_RCVLATENCY)                   | 1.3.0 | pre      | `int32_t` | msec    | \*                | 0..      | RW  | GSD   |
| [`SRTO_RCVSYN`](#SRTO_RCVSYN)                           |       | post     | `bool`    |         | true              |          | RW  | GSI   |
| [`SRTO_RCVTIMEO`](#SRTO_RCVTIMEO)                       |       | post     | `int32_t` | ms      | -1                | -1, 0..  | RW  | GSI   |
| [`SRTO_RENDEZVOUS`](#SRTO_RENDEZVOUS)                   |       | pre      | `bool`    |         | false             |          | RW  | S     |
| [`SRTO_RETRANSMITALGO`](#SRTO_RETRANSMITALGO)           | 1.4.2 | pre      | `int32_t` |         | 1                 | [0, 1]   | RW  | GSD   |
| [`SRTO_REUSEADDR`](#SRTO_REUSEADDR)                     |       | pre-bind | `bool`    |         | true              |          | RW  | GSD   |
| [`SRTO_SENDER`](#SRTO_SENDER)                           | 1.0.4 | pre      | `bool`    |         | false             |          | W   | S     |
| [`SRTO_SNDBUF`](#SRTO_SNDBUF)                           |       | pre-bind | `int32_t` | bytes   | 8192 payloads     | \*       | RW  | GSD+  |
| [`SRTO_SNDDATA`](#SRTO_SNDDATA)                         |       |          | `int32_t` | pkts    |                   |          | R   | S     |
| [`SRTO_SNDDROPDELAY`](#SRTO_SNDDROPDELAY)               | 1.3.2 | post     | `int32_t` | ms      | \*                | -1..     | W   | GSD+  |
| [`SRTO_SNDKMSTATE`](#SRTO_SNDKMSTATE)                   | 1.2.0 |          | `int32_t` | enum    |                   |          | R   | S     |
| [`SRTO_SNDSYN`](#SRTO_SNDSYN)                           |       | post     | `bool`    |         | true              |          | RW  | GSI   |
| [`SRTO_SNDTIMEO`](#SRTO_SNDTIMEO)                       |       | post     | `int32_t` | ms      | -1                | -1..     | RW  | GSI   |
| [`SRTO_STATE`](#SRTO_STATE)                             |       |          | `int32_t` | enum    |                   |          | R   | S     |
| [`SRTO_STREAMID`](#SRTO_STREAMID)                       | 1.3.0 | pre      | `string`  |         | ""                | [512]    | RW  | GSD   |
| [`SRTO_TLPKTDROP`](#SRTO_TLPKTDROP)                     | 1.0.6 | pre      | `bool`    |         | \*                |          | RW  | GSD   |
| [`SRTO_TRANSTYPE`](#SRTO_TRANSTYPE)                     | 1.3.0 | pre      | `int32_t` | enum    |`SRTT_LIVE`        | \*       | W   | S     |
| [`SRTO_TSBPDMODE`](#SRTO_TSBPDMODE)                     | 0.0.0 | pre      | `bool`    |         | \*                |          | W   | S     |
| [`SRTO_UDP_RCVBUF`](#SRTO_UDP_RCVBUF)                   |       | pre-bind | `int32_t` | bytes   | 8192 payloads     | \*       | RW  | GSD+  |
| [`SRTO_UDP_SNDBUF`](#SRTO_UDP_SNDBUF)                   |       | pre-bind | `int32_t` | bytes   | 65536             | \*       | RW  | GSD+  |
| [`SRTO_VERSION`](#SRTO_VERSION)                         | 1.1.0 |          | `int32_t` |         |                   |          | R   | S     |

### Option Descriptions

#### SRTO_BINDTODEVICE

| OptName               | Since | Restrict | Type     | Units  | Default  | Range  | Dir |Entity|
| --------------------- | ----- | -------- | -------- | ------ | -------- | ------ |-----|------|
| `SRTO_BINDTODEVICE`   | 1.4.2 | pre-bind | `string` |        |          |        | RW  | GSD+ |

Refers to the `SO_BINDTODEVICE` system socket option for `SOL_SOCKET` level.
This effectively limits the packets received by this socket to only those
that are targeted to that device. The device is specified by name passed as
string. The setting becomes effective after binding the socket (including
default-binding when connecting).

NOTE: This option is only available on Linux and available there by default.
On all other platforms setting this option will always fail.

NOTE: With the default system configuration, this option is only available
for a process that runs as root. Otherwise the function that applies the setting
(`srt_bind`, `srt_connect` etc.) will fail.

[Return to list](#list-of-options)

---

#### SRTO_CONGESTION

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_CONGESTION` | 1.3.0 | pre      | `string`   |         | "live"    | *      | W   | S      |

The type of congestion controller used for the transmission for that socket.

Its type must be exactly the same on both connecting parties, otherwise the
connection is rejected - **however** you may also change the value of this
option for the accepted socket in the listener callback (see `srt_listen_callback`)
if an appropriate instruction was given in the Stream ID.

Currently supported congestion controllers are designated as "live" and "file"

Note that it is not recommended to change this option manually, but you should
rather change the whole set of options using the [`SRTO_TRANSTYPE`](#SRTO_TRANSTYPE) option.

[Return to list](#list-of-options)

---

#### SRTO_CONNTIMEO

| OptName            | Since | Restrict |   Type    | Units  | Default  | Range  | Dir | Entity |
| ------------------ | ----- | -------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_CONNTIMEO`   | 1.1.2 | pre      | `int32_t` | msec   | 3000     | 0..    | W   | GSD+   |

Connect timeout. This option applies to the caller and rendezvous connection
modes. For the rendezvous mode (see `SRTO_RENDEZVOUS`) the effective connection timeout
will be 10 times the value set with `SRTO_CONNTIMEO`.

[Return to list](#list-of-options)

---

#### SRTO_CRYPTOMODE

| OptName            | Since     | Restrict |   Type    | Units  | Default  | Range  | Dir | Entity |
| ------------------ | --------- | -------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_CRYPTOMODE`  | 1.6.0-dev | pre      | `int32_t` |        | 0 (Auto) | [0, 2] | RW  | GSD   |

The encryption mode to be used if the [`SRTO_PASSPHRASE`](#SRTO_PASSPHRASE) is set.

Crypto modes:

- `0`: auto-select during handshake negotiation (to be implemented; currently similar to AES-CTR).
- `1`: regular AES-CTR (without message integrity authentication).
- `2`: AES-GCM mode with message integrity authentication (AEAD).


[Return to list](#list-of-options)

---

#### SRTO_DRIFTTRACER

| OptName           | Since | Restrict | Type      | Units  | Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_DRIFTTRACER`| 1.4.2 | post    | `bool`    |        | true     |        | RW  | GSD    |

Enables or disables time drift tracer (receiver).

[Return to list](#list-of-options)

---

#### SRTO_ENFORCEDENCRYPTION

| OptName                    | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_ENFORCEDENCRYPTION`  | 1.3.2 | pre      | `bool`     |         | true      |        | W   | GSD    |

This option enforces that both connection parties have the same passphrase
set, or both do not set the passphrase, otherwise the connection is rejected.

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
on the network parameters -- in particular, whether the `UMSG_SHUTDOWN` message
sent by the caller is delivered (which takes one RTT in this case) or missed
during the interval from its creation up to the connection timeout (default = 5
seconds). It is therefore strongly recommended that you only set this flag to
FALSE on the listener when you are able to ensure that it is also set to FALSE
on the caller side.

[Return to list](#list-of-options)

---

#### SRTO_EVENT

| OptName           | Since | Restrict | Type      | Units  | Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_EVENT`      |       |          | `int32_t` | flags  |          |        | R   | S      |

Returns bit flags set according to the current active events on the socket.

Possible values are those defined in `SRT_EPOLL_OPT` enum (a combination of
`SRT_EPOLL_IN`, `SRT_EPOLL_OUT` and `SRT_EPOLL_ERR`).

[Return to list](#list-of-options)

---

#### SRTO_FC

| OptName           | Since | Restrict | Type      | Units  | Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_FC`         |       | pre      | `int32_t` | pkts   | 25600    | 32..   | RW  | GSD    |

Flow Control limits the maximum number of packets "in flight" - payload (data) packets that were sent
but reception is not yet acknowledged with an ACK control packet.
It also includes data packets already received, but that can't be acknowledged due to loss of preceding data packet(s).
In other words, if a data packet with sequence number `A` was lost, then acknowledgement of the following `SRTO_FC` packets
is blocked until packet `A` is either successfully retransmitted or dropped by the
[Too-Late Packet Drop mechanism](https://datatracker.ietf.org/doc/html/draft-sharabayko-srt-00#section-4.6).
Thus the sender will have  `SRTO_FC` packets in flight, and will not be allowed to send further data packets.
Therefore, when establishing the value of `SRTO_FC`, it is recommend taking into consideration possible delays due to packet loss and retransmission.

There is a restriction that the receiver buffer size ([SRTO_RCVBUF](#SRTO_RCVBUF)) must not be greater than `SRTO_FC`
([#700](https://github.com/Haivision/srt/issues/700)).
Therefore, it is recommended to set the value of `SRTO_FC` first, and then the value of `SRTO_RCVBUF`.

The default flow control window size is 25600 packets. It is approximately:
- 270 Mbits of payload in the default live streaming configuration with an SRT payload size of 1316 bytes;
- 300 Mbits of payload with an SRT payload size of 1456 bytes.

The minimum number of packets in flight should be (assuming max payload size):  
`FCmin = bps / 8 × RTTsec / (MSS - 44)`,  
where
- `bps` - is the payload bitrate of the stream in bits per second;
- `RTTsec` - RTT of the network connection in seconds;
- `MSS` - Maximum segment size (aka MTU), see [SRTO_MSS](#SRTO_MSS);
- 44 - size of headers (20 bytes IPv4 + 8 bytes of UDP + 16 bytes of SRT packet header).

To avoid blocking the sending of further packets in case of packet loss, the recommended flow control window is  
`FC = bps / 8 × (RTTsec + latency_sec) / (MSS - 44)`,  
where `latency_sec` is the receiver buffering delay ([SRTO_RCVLATENCY](#SRTO_RCVLATENCY)) **in seconds**.

[Return to list](#list-of-options)

---

#### SRTO_GROUPCONNECT

| OptName              | Since | Restrict | Type      | Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | --------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_GROUPCONNECT`  | 1.5.0 | pre      | `int32_t` |        | 0        | 0...1  | W   | S      |

When this flag is set to 1 on a listener socket, it allows this socket to
accept group connections. When set to the default 0, group connections will be
rejected. Keep in mind that if the `SRTO_GROUPCONNECT` flag is set to 1 (i.e.
group connections are allowed) `srt_accept` may return a socket **or** a group
ID. A call to `srt_accept` on a listener socket that has group connections
allowed must take this into consideration. It's up to the caller of this
function to make this distinction and to take appropriate action depending on
the type of entity returned.

When this flag is set to 1 on an accepted socket that is passed to the
listener callback handler, it means that this socket is created for a group
connection and it will become a member of a group. Note that in this case
only the first connection within the group will result in reporting from
`srt_accept` (further connections are handled in the background), and this
function will return the group, not this socket ID.

[Return to list](#list-of-options)

---

#### SRTO_GROUPMINSTABLETIMEO

| OptName                    | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| -------------------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_GROUPMINSTABLETIMEO` | 1.5.0 | pre      | `int32_t`  | ms     | 60       | 60-... | W   | GDI+   |

The option is used for groups of type `SRT_GTYPE_BACKUP`. It defines the **minimum** value of the stability
timeout for all active member sockets in a group.
The actual timeout value is determined in runtime based on the RTT estimate of an individual member socket.
If there is no response from the peer for the calculated timeout,
the member is considered unstable, triggering activation of an idle backup member.

The smaller the value is, the earlier a backup member might be activated to prepare transition to that path.
However, it may also lead to spurious activations of backup paths.
The higher the value is, the later the backup link would be activated. All unacknowledged payload packets
have to be retransmitted through the backup path. If they don't reach the receiver in time, they would be dropped.
Therefore, an appropriate adjustment of the SRT buffering delay
(`SRTO_PEERLATENCY` on sender, `SRTO_RCVLATENCY` on receiver) should also be considered.

Normally the receiver should send an ACK back to the sender every 10 ms. In the case of congestion,
in the live streaming configuration of SRT a loss report is expected to be sent every RTT/2.
The network jitter and increase of RTT on the public internet causes
these intervals to be stretched.
The default minimum value of 60 ms is selected as a general fit for most of the use cases.

Please refer to the [SRT Connection Bonding: Main/Backup](../features/bonding-main-backup.md) document for more details.

Note that the value of this option is not allowed to exceed the value of
`SRTO_PEERIDLETIMEO`, which determines the timeout to actually break an idle (irresponsive) connection.

[Return to list](#list-of-options)

---

#### SRTO_GROUPTYPE

| OptName              | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_GROUPTYPE`     | 1.5.0 |          | `int32_t`  | enum   |          |        | R   | S      |

This option is read-only and it is intended to be called inside the listener
callback handler (see `srt_listen_callback`). Possible values are defined in
the `SRT_GROUP_TYPE` enumeration type.

This option returns the group type that is declared in the incoming connection.
If the incoming connection is not going to make a group-member connection, then
the value returned is `SRT_GTYPE_UNDEFINED`. If this option is read in any other
context than inside the listener callback handler, the value is undefined.

[Return to list](#list-of-options)

---

#### SRTO_INPUTBW

| OptName          | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_INPUTBW`   | 1.0.5 | post     | `int64_t`  | B/s    | 0        | 0..    | RW  | GSD    |

This option is effective only if [`SRTO_MAXBW`](#SRTO_MAXBW) is set to 0 (relative). It
controls the maximum bandwidth together with [`SRTO_OHEADBW`](#SRTO_OHEADBW) option according
to the formula: `MAXBW = INPUTBW * (100 + OHEADBW) / 100`. When this option
is set to 0 (automatic) then the real INPUTBW value will be estimated from
the rate of the input (cases when the application calls the `srt_send*`
function) during transmission. The minimum allowed estimate value is restricted
by [`SRTO_MININPUTBW`](#SRTO_MININPUTBW), meaning `INPUTBW = MAX(INPUTBW_ESTIMATE; MININPUTBW)`.

*Recommended: set this option to the anticipated bitrate of your live stream
and keep the default 25% value for `SRTO_OHEADBW`*.

[Return to list](#list-of-options)

---

#### SRTO_MININPUTBW

| OptName           | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_MININPUTBW` | 1.4.3 | post     | `int64_t`  | B/s    | 0        | 0..    | RW  | GSD    |

This option is effective only if both `SRTO_MAXBW` and `SRTO_INPUTBW` are set to 0.
It controls the minimum allowed value of the input bitrate estimate.

See [`SRTO_INPUTBW`](#SRTO_INPUTBW).

[Return to list](#list-of-options)

---

#### SRTO_IPTOS

| OptName          | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_IPTOS`     | 1.0.5 | pre-bind | `int32_t`  |        | (system) | 0..255 | RW  | GSD    |

IPv4 Type of Service (see `IP_TOS` option for IP) or IPv6 Traffic Class (see `IPV6_TCLASS`
of IPv6) depending on socket address family. Applies to sender only.

When *getting*, the returned value is the user preset for non-connected sockets
and the actual value for connected sockets.

*Sender*: user configurable, default: `0xB8`.

[Return to list](#list-of-options)

---

#### SRTO_IPTTL

| OptName          | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_IPTTL`     | 1.0.5 | pre-bind | `int32_t`  | hops   | (system) | 1..255 | RW  | GSD    |

IPv4 Time To Live (see `IP_TTL` option for IP) or IPv6 unicast hops (see
`IPV6_UNICAST_HOPS` for IPv6) depending on socket address family. Applies to sender only.

When *getting*, the returned value is the user preset for non-connected sockets
and the actual value for connected sockets.

*Sender*: user configurable, default: 64

[Return to list](#list-of-options)

---

#### SRTO_IPV6ONLY

| OptName          | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_IPV6ONLY`  | 1.4.0 | pre-bind | `int32_t`  |        | (system) | -1..1  | RW  | GSD    |

Set system socket flag `IPV6_V6ONLY`. When set to 0 a listening socket binding an
IPv6 address accepts also IPv4 clients (their addresses will be formatted as
IPv4-mapped IPv6 addresses). By default (-1) this option is not set and the
platform default value is used.

[Return to list](#list-of-options)

---

#### SRTO_ISN

| OptName          | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| ---------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_ISN`       | 1.3.0 |          | `int32_t`  |        |          |        | R   | S      |

The value of the ISN (Initial Sequence Number), which is the first sequence
number put on the first UDP packets sent that are carrying an SRT data payload.

*This value is useful for developers of some more complicated methods of flow
control, possibly with multiple SRT sockets at a time. It is not intended to be
used in any regular development.*

[Return to list](#list-of-options)

---

#### SRTO_KMPREANNOUNCE

| OptName               | Since | Restrict | Type       | Units  | Default           | Range  | Dir | Entity |
| --------------------- | ----- | -------- | ---------- | ------ | ----------------- | ------ | --- | ------ |
| `SRTO_KMPREANNOUNCE`  | 1.3.2 | pre      | `int32_t`  | pkts   | 0: 2<sup>12</sup> | 0.. *  | RW  | GSD    |

The interval (defined in packets) between when a new Stream Encrypting Key
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

**The allowed range** for this value is between 1 and half of the current value of
`SRTO_KMREFRESHRATE`. The minimum value should never be less than the flight
window [`SRTO_FC`](#SRTO_FC) (i.e. the number of packets that have already left the sender but have
not yet arrived at the receiver).

The value of `SRTO_KMPREANNOUNCE must not exceed `(SRTO_KMREFRESHRATE - 1) / 2`.

**Default value:** `0` - corresponds to 4096 packets (2<sup>12</sup> or 0x1000).

[Return to list](#list-of-options)

---

#### SRTO_KMREFRESHRATE

| OptName               | Since | Restrict | Type       | Units  | Default          | Range  | Dir | Entity |
| --------------------- | ----- | -------- | ---------- | ------ | ---------------- | ------ | --- | ------ |
| `SRTO_KMREFRESHRATE`  | 1.3.2 | pre      | `int32_t`  | pkts   | 0: 2<sup>24</sup>| 0..    | RW  | GSD    |

The number of packets to be transmitted after which the Stream Encryption Key
(SEK), used to encrypt packets, will be switched to the new one. Note that
the old and new keys live in parallel for a certain period of time (see
`SRTO_KMPREANNOUNCE`) before and after the switchover.

Having a preannounce period before switchover ensures the new SEK is installed
at the receiver before the first packet encrypted with the new SEK is received.
The old key remains active after switchover in order to decrypt packets that
might still be in flight, or packets that have to be retransmitted.

**Default value:** `0` - corresponds to 16777216 packets (2<sup>24</sup> or 0x1000000).

[Return to list](#list-of-options)

---

#### SRTO_KMSTATE

| OptName               | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| --------------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_KMSTATE`        | 1.0.2 |          | `int32_t`  | enum   |          |        | R   | S      |

Keying Material state. This is a legacy option that is equivalent to
`SRTO_SNDKMSTATE`, if the socket has set `SRTO_SENDER` to true, and
`SRTO_RCVKMSTATE` otherwise. This option is then equal to `SRTO_RCVKMSTATE`
always if your application disregards possible cooperation with a peer older
than 1.3.0, but then with the default value of `SRTO_ENFORCEDENCRYPTION` the
value returned by both options is always the same. See [`SRT_KM_STATE`](#2-srt_km_state)
for more details.

[Return to list](#list-of-options)

---

#### SRTO_LATENCY

| OptName               | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| --------------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_LATENCY`        | 1.0.2 | pre      | `int32_t`  | ms     | 120 *    | 0..    | RW  | GSD    |

This option sets both [`SRTO_RCVLATENCY`](#SRTO_RCVLATENCY) and [`SRTO_PEERLATENCY`](#SRTO_PEERLATENCY)
to the same value specified.

Prior to SRT version 1.3.0 `SRTO_LATENCY` was the only option to set the latency.
However it is effectively equivalent to setting `SRTO_PEERLATENCY` in the sending direction
(see [`SRTO_SENDER`](#SRTO_SENDER)), and `SRTO_RCVLATENCY` in the receiving direction.
SRT version 1.3.0 and higher support bidirectional transmission, so that each side can
be sender and receiver at the same time, and `SRTO_SENDER` became redundant.

[Return to list](#list-of-options)

---

#### SRTO_LINGER

| OptName              | Since | Restrict | Type       | Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------ | -------- | ------ | --- | ------ |
| `SRTO_LINGER`        |       | pre      | `linger`   | s      | off \*   | 0..    | RW  | GSD    |

SRT socket linger time on close (similar to [SO\_LINGER](http://man7.org/linux/man-pages/man7/socket.7.html)).
The defulat value in [the live streaming configuration](./API.md#transmission-types) is OFF. In this type of workflow there is no point for wait for all the data
to be delivered after a connection is closed.
The default value in [the file transfer configuration](./API.md#transmission-types) is 180 s.


*SRT recommended value*: off (0).

[Return to list](#list-of-options)

---

#### SRTO_LOSSMAXTTL

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_LOSSMAXTTL`    | 1.2.0 | post     | `int32_t`  | packets | 0        | 0..    | RW  | GSD+   |

The value up to which the *Reorder Tolerance* may grow. The *Reorder Tolerance*
is the number of packets that must follow the experienced "gap" in sequence numbers
of incoming packets so that the loss report is sent (in the hope that the gap is due
to packet reordering rather than because of loss). The value of *Reorder Tolerance*
starts from 0 and is set to a greater value when packet reordering is detected
This happens when a "belated" packet, with sequence number older than the latest
received, has been received, but without retransmission flag. When this is detected
the *Reorder Tolerance* is set to the value of the interval between latest sequence
and this packet's sequence, but not more than the value set by `SRTO_LOSSMAXTTL`.
By default this value is set to 0, which means that this mechanism is off.

[Return to list](#list-of-options)

---

#### SRTO_MAXBW

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MAXBW`         | 1.0.5 | post     | `int64_t`  | B/s     | -1       | -1..   | RW  | GSD    |

Maximum send bandwidth:

- `-1`: infinite (the limit in Live Mode is 1 Gbps);
- `0`: relative to input rate (see [`SRTO_INPUTBW`](#SRTO_INPUTBW));
- `>0`: absolute limit in B/s.

**NOTE**: This option has a default value of -1, regardless of the mode.
For live streams it is typically recommended to set the value 0 here and rely
on `SRTO_INPUTBW` and `SRTO_OHEADBW` options. However, if you want to do so,
you should make sure that your stream has a fairly constant bitrate, or that
changes are not abrupt, as high bitrate changes may work against the
measurement. SRT cannot ensure that this is always the case for a live stream,
therefore the default -1 remains even in live mode.

[Return to list](#list-of-options)

---

#### SRTO_MESSAGEAPI

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MESSAGEAPI`    | 1.3.0 | pre      | `bool`     |         | true     |        | W   | GSD    |

When set, this socket uses the Message API[\*], otherwise it uses the
Stream API. Note that in live mode (see [`SRTO_TRANSTYPE`](#SRTO_TRANSTYPE) option) only the
Message API is available. In File mode you can chose to use one of two modes
(note that the default for this option is changed with `SRTO_TRANSTYPE`
option):

- **Stream API** (default for file mode): In this mode you may send
as many data as you wish with one sending instruction, or even use dedicated
functions that operate directly on a file. The internal facility will take care
of any speed and congestion control. When receiving, you can also receive as
many data as desired. The data not extracted will be waiting for the next call.
There is no boundary between data portions in Stream mode.
  
- **Message API**: In this mode your single sending instruction passes exactly
one piece of data that has boundaries (a message). Contrary to Live mode,
this message may span multiple UDP packets, and the only size limitation
is that it shall fit as a whole in the sending buffer. The receiver shall use
as large a buffer as necessary to receive the message, otherwise reassembling
and delivering the message might not be possible. When the message is not
complete (not all packets received or there was a packet loss) it will not be
copied to the application's buffer. Messages that are sent later, but were
earlier reassembled by the receiver, will be delivered once ready, if the
`inorder` flag was set to false.
See [`srt_sendmsg`](API.md#sending-and-receiving).
  
As a comparison to the standard system protocols, the Stream API does
transmission similar to TCP, whereas the Message API functions like the
SCTP protocol.

[Return to list](#list-of-options)

---

#### SRTO_MINVERSION

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MINVERSION`    | 1.3.0 | pre      | `int32_t`  | version | 0x010000 | *      | RW  | GSD    |

The minimum SRT version that is required from the peer. A connection to a
peer that does not satisfy the minimum version requirement will be rejected.
See [`SRTO_VERSION`](#SRTO_VERSION) for the version format.

The default value is 0x010000 (SRT v1.0.0).

[Return to list](#list-of-options)

---

#### SRTO_MSS

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_MSS`           |       | pre-bind | `int32_t`  | bytes   | 1500     | 76..   | RW  | GSD    |

Maximum Segment Size. Used for buffer allocation and rate calculation using
packet counter assuming fully filled packets. Each party can set its own MSS
value independently. During a handshake the parties exchange MSS values, and
the lowest is used.

*Generally on the internet MSS is 1500 by default. This is the maximum
size of a UDP packet and can be only decreased, unless you have some unusual
dedicated network settings. MSS is not to be confused with the size of the UDP
payload or SRT payload - this size is the size of the IP packet, including the
UDP and SRT headers*

THe value of `SRTO_MSS` must not exceed `SRTO_UDP_SNDBUF` or `SRTO_UDP_RCVBUF`.

[Return to list](#list-of-options)

---

#### SRTO_NAKREPORT

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_NAKREPORT`     | 1.1.0 | pre      | `bool`     |         |  *       |        | RW  | GSD+   |

When set to true, every report for a detected loss will be repeated when the
timeout for the expected retransmission of this loss has expired and the
missing packet still wasn't recovered, or wasn't conditionally dropped (see
[`SRTO_TLPKTDROP`](#SRTO_TLPKTDROP)).

The default is true for Live mode, and false for File mode (see [`SRTO_TRANSTYPE`](#SRTO_TRANSTYPE)).

[Return to list](#list-of-options)

---

#### SRTO_OHEADBW

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_OHEADBW`       | 1.0.5 | post     | `int32_t`  | %       | 25       | 5..100 | RW  | GSD    |

Recovery bandwidth overhead above input rate (see [`SRTO_INPUTBW`](#SRTO_INPUTBW)),
in percentage of the input rate. It is effective only if `SRTO_MAXBW` is set to 0.

*Sender*: user configurable, default: 25%.

Recommendations:

- Overhead is intended to give you extra bandwidth for the case when a packet
has taken part of the bandwidth, but then was lost and has to be retransmitted.
Therefore the effective maximum bandwidth should be appropriately higher than
your stream's bitrate so that there's some room for retransmission, but still
limited so that the retransmitted packets don't cause the bandwidth usage to
skyrocket when larger groups of packets are lost

- Don't configure it too low and avoid 0 in the case when you have the
`SRTO_INPUTBW` option set to 0 (automatic). Otherwise your stream will choke
and break quickly at any rise in packet loss.

- ***To do: set-only; get should be supported.***

[Return to list](#list-of-options)

---

#### SRTO_PACKETFILTER

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PACKETFILTER`  | 1.4.0 | pre      | `string`   |         |  ""      | [512]  | RW  | GSD    |

Set up the packet filter. The string must match appropriate syntax for packet
filter setup. Note also that:

* The configuration is case-sentitive (e.g. "FEC,Cols:20" is not valid).
* Setting this option will fail if you use an unknown filter type.

An empty value for this option means that for this connection the filter isn't
required, but it will accept any filter settings if provided by the peer. If
this option is changed by both parties simultaneously, the result will be a
configuration integrating parameters from both parties, that is:

* parameters provided by both parties are accepted, if they are identical
* parameters that are set only on one side will have the value defined by that side
* parameters not set in either side will be set as default

The connection will be rejected with `SRT_REJ_FILTER` code in the following cases:

* both sides define a different packet filter type
* for the same key two different values were provided by both sides
* mandatory parameters weren't provided by either side

In case of the built-in `fec` filter, the mandatory parameter is `cols`, all
others have their default values. For example, the configuration specified
as `fec,cols:10` is `fec,cols:10,rows:1,arq:onreq,layout:even`. See how to
configure the FEC filter in [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md#configuring-the-fec-filter).

Below in the table are examples for the built-in `fec` filter. Note that the
negotiated config need not have parameters in the given order.

Cases when negotiation succeeds:

| Peer A               | Peer B              | Negotiated Config            
|----------------------|---------------------|------------------------------------------------------
| (no filter)          | (no filter)         | 
| fec,cols:10          | fec                 | fec,cols:10,rows:1,arq:onreq,layout:even                  
| fec,cols:10          | fec,cols:10,rows:20 | fec,cols:10,rows:20,arq:onreq,layout:even                  
| fec,layout:staircase | fec,cols:10         | fec,cols:10,rows:1,arq:onreq,layout:staircase 

In these cases the configuration is rejected with SRT_REJ_FILTER code:

| Peer A                | Peer B              | Error reason
|-----------------------|---------------------|--------------------------
| fec                   | (no filter)         | missing `cols` parameter 
| fec,rows:20,arq:never | fec,layout:even     | missing `cols` parameter 
| fec,cols:20           | fec,cols:10         | `cols` parameter value conflict 
| fec,cols:20,rows:20   | fec,cols:20,rows:10 | `rows` parameter value conflict 

In general it is recommended that one party defines the full configuration,
while the other keeps this value empty.

Reading this option after the connection is established will return the full
configuration that has been agreed upon by both parties (including default
values).

For details, see [SRT Packet Filtering & FEC](../features/packet-filtering-and-fec.md).

[Return to list](#list-of-options)

---

#### SRTO_PASSPHRASE

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PASSPHRASE`    | 0.0.0 | pre      | `string`   |         | ""       |[10..79]| W   | GSD    |

Sets the passphrase for encryption. This enables encryption on this party (or
disables it, if an empty passphrase is passed). The password must be minimum
10 and maximum 79 characters long.

The passphrase is the shared secret between the sender and the receiver. It is
used to generate the Key Encrypting Key using [PBKDF2](http://en.wikipedia.org/wiki/PBKDF2)
(Password-Based Key Derivation Function 2).

When a socket with configured passphrase is being connected, the peer must
have the same password set, or the connection is rejected. This behavior can be
changed by [`SRTO_ENFORCEDENCRYPTION`](#SRTO_ENFORCEDENCRYPTION).

Note that since the introduction of bidirectional support, there's only one
initial encryption key to encrypt the stream (new keys after refreshing will be
updated independently), and there's no distinction between "service party that
defines the password" and "client party that is required to set matching
password" - both parties are equivalent, and in order to have a working
encrypted connection, they have to simply set the same passphrase.

[Return to list](#list-of-options)

---

#### SRTO_PAYLOADSIZE

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PAYLOADSIZE`   | 1.3.0 | pre      | `int32_t`  | bytes   | \*       | 0.. \* | W   | GSD    |

Sets the maximum declared size of a single call to sending function in Live
mode. When set to 0, there's no limit for a single sending call.

For Live mode: Default value is 1316, but can be increased up to 1456. Note that
with the `SRTO_PACKETFILTER` option additional header space is usually required,
which decreases the maximum possible value for `SRTO_PAYLOADSIZE`.

For File mode: Default value is 0 and it's recommended not to be changed.

[Return to list](#list-of-options)

---

#### SRTO_PBKEYLEN

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PBKEYLEN`      | 0.0.0 | pre      | `int32_t`  | bytes   | 0        | *      | RW  | GSD    |

Encryption key length.

Possible values:

- 0 =`PBKEYLEN` (default value)
- 16 = AES-128 (effective value)
- 24 = AES-192
- 32 = AES-256

The use is slightly different in 1.2.0 (HSv4), and since 1.3.0 (HSv5):

- **HSv4**: This is set on the sender and enables encryption, if not 0. The receiver
shall not set it and will agree on the length as defined by the sender.

- **HSv5**: The "default value" for `PBKEYLEN` is 0, which means that the
`PBKEYLEN` won't be advertised. The "effective value" for `PBKEYLEN` is 16, but
this applies only when neither party has set the value explicitly (i.e. when
both are initially at the default value of 0). If any party *has* set an
explicit value (16, 24, 32) it will be advertised in the handshake. If the other
party remains at the default 0, it will accept the peer's value. The situation
where both parties set a value should be treated carefully. Actually there are
three intended methods of defining it, and all other uses are considered
undefined behavior:
  
  - **Unidirectional**: the sender shall set `PBKEYLEN` and the receiver shall
  not alter the default value 0. The effective `PBKEYLEN` will be the one set
  on the sender. The receiver need not know the sender's `PBKEYLEN`, just the
  passphrase, `PBKEYLEN` will be correctly passed.

  - **Bidirectional in Caller-Listener arrangement**: it is recommended to use
  a rule whereby you will be setting the `PBKEYLEN` exclusively either on the
  Listener or on the Caller. The value set on the Listener will win, if set on
  both parties.

  - **Bidirectional in Rendezvous arrangement**: you have to know the passphrases
  for both parties, as well as `PBKEYLEN`. Set `PBKEYLEN` to the same value on
  both parties (or leave the default value on both parties, which will
  result in 16)

  - **Unwanted behavior cases**: if both parties set `PBKEYLEN` and the value
  on both sides is different, the effective `PBKEYLEN` will be the one that is
  set on the Responder party, which may also override the `PBKEYLEN` 32 set by
  the sender to value 16 if such value was used by the receiver. The Responder
  party is the Listener in a Caller-Listener arrangement. In Rendezvous it's a
  matter of luck which party becomes the Responder.

[Return to list](#list-of-options)

---

#### SRTO_PEERIDLETIMEO

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PEERIDLETIMEO` | 1.3.3 | pre      | `int32_t`  | ms      | 5000     | 0..    | RW  | GSD+   |

The maximum time in `[ms]` to wait until another packet is received from a peer
since the last such packet reception. If this time is passed, the connection is
considered broken on timeout.

[Return to list](#list-of-options)

---

#### SRTO_PEERLATENCY

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PEERLATENCY`   | 1.3.0 | pre      | `int32_t`  | ms      | 0        | 0..    | RW  | GSD    |

The latency value (as described in [`SRTO_RCVLATENCY`](#SRTO_RCVLATENCY)) provided by the sender
side as a minimum value for the receiver.

Reading the value of the option on an unconnected socket reports the configured value.
Reading the value on a connected socket reports the effective receiver buffering latency of the peer.

**The `SRTO_PEERLATENCY` option in versions prior to 1.3.0 is only available as**
[`SRTO_LATENCY`](#SRTO_LATENCY).

See also [`SRTO_LATENCY`](#SRTO_LATENCY).

[Return to list](#list-of-options)

---

#### SRTO_PEERVERSION

| OptName              | Since | Restrict | Type       |  Units  | Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | -------- | ------ | --- | ------ |
| `SRTO_PEERVERSION`   | 1.1.0 |          | `int32_t`  | *       |          |        | R   | GS     |

SRT version used by the peer. The value 0 is returned if not connected, SRT
handshake not yet performed (HSv4 only), or if peer is not SRT.
See [`SRTO_VERSION`](#SRTO_VERSION) for the version format.

[Return to list](#list-of-options)

---

#### SRTO_RCVBUF

| OptName              | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVBUF`        |       | pre-bind | `int32_t`  | bytes   | 8192 bufs  | *      | RW  | GSD+   |

Receive Buffer Size, in bytes. Note, however, that the internal setting of this
value is in the number of buffers, each one of size equal to SRT payload size,
which is the value of `SRTO_MSS` decreased by UDP and SRT header sizes (28 and 16).
The value set here will be effectively aligned to the multiple of payload size.

- **Minimum value**: 32 buffers (46592 with default value of `SRTO_MSS`).

- **Maximum value**: [`SRTO_FC`](#SRTO_FC) number of buffers (receiver buffer must not be greater
than the Flight Flag size).

[Return to list](#list-of-options)

---

#### SRTO_RCVDATA

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVDATA`    |       |          | `int32_t`  | pkts    |            |        | R   | S      |

Size of the available data in the receive buffer.

[Return to list](#list-of-options)

---

#### SRTO_RCVKMSTATE

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVKMSTATE` | 1.2.0 |          | `int32_t`  | enum    |            |        | R   | S      |

KM state on the agent side when it's a receiver.

Values defined in enum [`SRT_KM_STATE`](#srt_km_state).

[Return to list](#list-of-options)

---

#### SRTO_RCVLATENCY

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVLATENCY` | 1.3.0 | pre      | `int32_t`  | ms      | *          | 0..    | RW  | GSD    |

The latency value in the receiving direction of the socket.
This value is only significant when [`SRTO_TSBPDMODE`](#SRTO_TSBPDMODE) is enabled.

**Default value**: 120 ms in Live mode, 0 in File mode (see [`SRTO_TRANSTYPE`](#SRTO_TRANSTYPE)).

The latency value defines the **minimum** receiver buffering delay before delivering an SRT data packet
from a receiving SRT socket to a receiving application. The provided value is used in the connection establishment (handshake exchange) stage
to fix the end-to-end latency of the transmission. The effective end-to-end latency `L` will be fixed
as the network transmission time of the final handshake packet (~1/2 RTT) plus the **negotiated** latency value `Ln`.
Data packets will stay in the receiver buffer for at least `L` microseconds since the timestamp of the
packet, independent of the actual network transmission times (RTT variations) of these packets.

The actual value of the receiver buffering delay `Ln` (the negotiated latency) used on a connection
is determined by the negotiation in the connection establishment (handshake exchange) phase as the maximum of the
`SRTO_RCVLATENCY` value and the value of [`SRTO_PEERLATENCY`](#SRTO_PEERLATENCY) set by the peer.

Reading the `SRTO_RCVLATENCY` value on a socket after the connection is established provides the actual (negotiated)
latency value `Ln`.

The receiver's buffer must be large enough to store the `L` segment of the stream,
i.e. `L × Bitrate` bytes. Refer to [`SRTO_RCVBUF`](#SRTO_RCVBUF).

The sender's buffer must be large enough to store a packet up until it is either delivered (and acknowledged)
or dropped by the sender due to it becoming too late to be delivered.
In other words, `D × Bitrate` bytes, where `D` is the sender's drop delay value configured with [`SRTO_SNDDROPDELAY`](#SRTO_SNDDROPDELAY).

Buffering of data packets on the receiving side makes it possible to recover from packet losses using the ARQ
(Automatic Repeat Request) technique, and to deal with varying RTT times (network jitter) in the network, providing
a (close to) **constant end-to-end latency of the transmission**.

See also [`SRTO_LATENCY`](#SRTO_LATENCY).

[Return to list](#list-of-options)

---

#### SRTO_RCVSYN

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVSYN`     |       | post     | `bool`     |         | true       |        | RW  | GSI    |

When true, sets blocking mode on reading function when it's not ready to
perform the operation. When false ("non-blocking mode"), the reading function
will in this case report error `SRT_EASYNCRCV` and return immediately. Details
depend on the tested entity:

On a connected socket or group this applies to a receiving function
(`srt_recv` and others) and a situation when there are no data available for
reading. The readiness state for this operation can be tested by checking the
`SRT_EPOLL_IN` flag on the aforementioned socket or group.

On a freshly created socket or group that is about to be connected to a peer
listener this applies to any `srt_connect` call (and derived), which in
"non-blocking mode" always returns immediately. The connected state for that
socket or group can be tested by checking the `SRT_EPOLL_OUT` flag. Note
that a socket that failed to connect doesn't change the `SRTS_CONNECTING`
state and can be found out only by testing the `SRT_EPOLL_ERR` flag.

On a listener socket this applies to `srt_accept` call. The readiness state
for this operation can be tested by checking the `SRT_EPOLL_IN` flag on
this listener socket. This flag is also derived from the listener socket
by the accepted socket or group, although the meaning of this flag is
effectively different.

Note that when this flag is set only on a group, it applies to a
specific receiving operation being done on that group (i.e. it is not
derived from the socket of which the group is a member).

[Return to list](#list-of-options)

---

#### SRTO_RCVTIMEO

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RCVTIMEO`   |       | post     | `int32_t`  | ms      | -1         | -1, 0..| RW  | GSI    |

Limits the time up to which the receiving operation will block (see
[`SRTO_RCVSYN`](#SRTO_RCVSYN) for details), such that when this time is exceeded,
it will behave as if in "non-blocking mode". The -1 value means no time limit.

[Return to list](#list-of-options)

---

#### SRTO_RENDEZVOUS

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_RENDEZVOUS` |       | pre      | `bool`     |         | false      |        | RW  | S      |

Use Rendezvous connection mode (both sides must set this and both must use the
procedure of `srt_bind` and then `srt_connect` (or `srt_rendezvous`) to one another.

[Return to list](#list-of-options)

---

#### SRTO_RETRANSMITALGO

| OptName               | Since | Restrict | Type      | Units  | Default | Range  | Dir | Entity |
| --------------------- | ----- | -------- | --------- | ------ | ------- | ------ | --- | ------ |
| `SRTO_RETRANSMITALGO` | 1.4.2 | pre      | `int32_t` |        | 1       | [0, 1] | RW  | GSD    |

An SRT sender option to choose between two retransmission algorithms:

- 0 - aggressive retransmission algorithm (default until SRT v1.4.4), and
- 1 - efficient retransmission algorithm (introduced in SRT v1.4.2; default since SRT v1.4.4).

The aggressive retransmission algorithm causes the SRT sender to schedule a packet for retransmission each time it receives a negative acknowledgement (NAK). On a network characterized by low packet loss levels and link capacity high enough to accommodate extra retransmission overhead, this algorithm increases the chances of recovering from packet loss with a minimum delay, and may better suit end-to-end latency constraints.

The new efficient algorithm optimizes the bandwidth usage by producing fewer retransmissions per lost packet. It takes SRT statistics into account to determine if a retransmitted packet is still in flight and could reach the receiver in time, so that some of the NAK reports are ignored by the sender. This algorithm better fits general use cases, as well as cases where channel bandwidth is limited.

To learn more about the algorithms, read ["Improving SRT Retransmissions — Experiments with Simulated Live Streaming (Part 1)"](https://medium.com/innovation-labs-blog/improving-srt-retransmissions-experiments-with-simulated-live-streaming-part-1-7d192483bba4) article.

NOTE: This option is effective only on the sending side. It influences the decision
as to whether a particular reported lost packet should be retransmitted at a
certain time or not.

NOTE: The efficient retransmission algorithm can only be used when a receiver sends
Periodic NAK reports. See [SRTO_NAKREPORT](#SRTO_NAKREPORT).

[Return to list](#list-of-options)

---

#### SRTO_REUSEADDR

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_REUSEADDR`  |       | pre-bind | `bool`     |         | true       |        | RW  | GSD    |

When true, allows the SRT socket to use the binding address used already by
another SRT socket in the same application. Note that SRT socket uses an
intermediate object called Multiplexer to access the underlying UDP sockets,
so multiple SRT sockets may share one UDP socket, and the packets received by this
UDP socket will be correctly dispatched to the SRT socket to which they are
currently destined. This has some similarities to the `SO_REUSEADDR` system socket
option, although it's only used inside SRT.

*TODO: This option weirdly only allows the socket used in **bind()** to use the
local address that another socket is already using, but not to disallow another
socket in the same application to use the binding address that the current
socket is already using. What it actually changes is that when given an address in
**bind()** is already used by another socket, this option will make the binding
fail instead of adding the socket to the shared group of that socket that
already has bound this address - but it will not disallow another socket to reuse
its address.*

[Return to list](#list-of-options)

---

#### SRTO_SENDER

| OptName           | Since | Restrict | Type       |  Units  |   Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | ---------- | ------ | --- | ------ |
| `SRTO_SENDER`     | 1.0.4 | pre      | `bool`     |         | false      |        | W   | S      |

Set sender side. The side that sets this flag is expected to be a sender. This
flag is only required when communicating with a receiver that uses SRT version
less than 1.3.0 (and hence *HSv4* handshake), in which case if not set properly,
the TSBPD mode (see [`SRTO_TSBPDMODE`](#SRTO_TSBPDMODE)) or encryption will not
work. Setting `SRTO_MINVERSION` to 1.3.0 is therefore recommended.

[Return to list](#list-of-options)

---

#### SRTO_SNDBUF

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDBUF`     |       | pre-bind | `int32_t`  | bytes   |8192 bufs  | *      | RW  | GSD+   |

Sender Buffer Size. See [`SRTO_RCVBUF`](#SRTO_RCVBUF) for more information.

[Return to list](#list-of-options)

---

#### SRTO_SNDDATA

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDDATA`    |       |          | `int32_t`  | pkts    |           |        | R   | S      |

Size of the unacknowledged data in send buffer.

[Return to list](#list-of-options)

---

#### SRTO_SNDDROPDELAY

| OptName              | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDDROPDELAY`  | 1.3.2 | post     | `int32_t`  | ms      | *         | -1..   | W   | GSD+   |

Sets an extra delay before `TLPKTDROP` is triggered on the data sender.
This delay is added to the default drop delay time interval value. Keep in mind
that the longer the delay, the more probable it becomes that packets would be
retransmitted uselessly because they will be dropped by the receiver anyway.

`TLPKTDROP` discards packets reported as lost if it is already too late to send
them (the receiver would discard them even if received). The delay before the
`TLPKTDROP` mechanism is triggered consists of the SRT latency (`SRTO_PEERLATENCY`),
plus `SRTO_SNDDROPDELAY`, plus `2 * interval between sending ACKs` (where the
default `interval between sending ACKs` is 10 milliseconds).
The minimum delay is `1000 + 2 * interval between sending ACKs` milliseconds.

**Special value -1**: Do not drop packets on the sender at all (retransmit them
always when requested).

**Default:** 0 in Live mode, -1 in File mode.

[Return to list](#list-of-options)

---

#### SRTO_SNDKMSTATE

| OptName              | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDKMSTATE`    | 1.2.0 |          | `int32_t`  |  enum   |           |        | R   | S      |

Peer KM state on receiver side for `SRTO_KMSTATE`

Values defined in enum [`SRT_KM_STATE`](#srt_km_state).

[Return to list](#list-of-options)

---

#### SRTO_SNDSYN

| OptName              | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDSYN`        |       | post     | `bool`     |         | true      |        | RW  | GSI    |

When true, sets blocking mode on writing function when it's not ready to
perform the operation. When false ("non-blocking mode"), the writing function
will in this case report error `SRT_EASYNCSND` and return immediately.

On a connected socket or group this applies to a sending function
(`srt_send` and others) and a situation when there's no free space in
the sender buffer, caused by inability to send all the scheduled data over
the network. Readiness for this operation can be tested by checking the
`SRT_EPOLL_OUT` flag.

On a freshly created socket or group it will have no effect until the socket
enters a connected state.

On a listener socket it will be derived by the accepted socket or group,
but will have no effect on the listener socket itself.

[Return to list](#list-of-options)

---

#### SRTO_SNDTIMEO

| OptName              | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_SNDTIMEO`      |       | post     | `int32_t`  | ms      | -1        | -1..   | RW  | GSI    |

limit the time up to which the sending operation will block (see
`SRTO_SNDSYN` for details), so when this time is exceeded, it will behave as
if in "non-blocking mode". The -1 value means no time limit.

[Return to list](#list-of-options)

---

#### SRTO_STATE

| OptName              | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_STATE`         |       |          | `int32_t`  |  enum   |           |        | R   | S      |

Returns the current socket state, same as `srt_getsockstate`.

[Return to list](#list-of-options)

---

#### SRTO_STREAMID

| OptName              | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| -------------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_STREAMID`      | 1.3.0 | pre      | `string`   |         | ""        | [512]  | RW  | GSD    |

- A string that can be set on the socket prior to connecting. The listener side 
will be able to retrieve this stream ID from the socket that is returned from 
`srt_accept` (for a connected socket with that stream ID). You usually use SET 
on the socket used for `srt_connect`, and GET on the socket retrieved from 
`srt_accept`. This string can be used completely free-form. However, it's highly 
recommended to follow the [SRT Access Control (Stream ID) Guidlines](../features/access-control.md).

- As this uses internally the `std::string` type, there are additional functions
for it in the legacy/C++ API (udt.h): `srt::setstreamid` and `srt::getstreamid`.

- This option is not useful for a Rendezvous connection, since one side would
override the value from the other side resulting in an arbitrary winner. Also
in this connection both peers are known to one another and both have equivalent 
roles in the connection.

- **IMPORTANT**: This option is not derived by the accepted socket from the listener
socket, and setting it on a listener socket (see `srt_listen` function) doesn't
influence anything.

[Return to list](#list-of-options)

---

#### SRTO_TLPKTDROP

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_TLPKTDROP`  | 1.0.6 | pre      | `bool`     |         | *         |        | RW  | GSD    |

Too-late Packet Drop. When enabled on receiver, it skips missing packets that
have not been delivered in time and delivers the subsequent packets to the
application when their time-to-play has come. It also sends a fake ACK to the
sender. When enabled on sender and enabled on the receiving peer, sender drops
the older packets that have no chance to be delivered in time. It is automatically
enabled in sender if receiver supports it.

**Default:** true in Live mode, false in File mode (see [`SRTO_TRANSTYPE`](#SRTO_TRANSTYPE))

[Return to list](#list-of-options)

---

#### SRTO_TRANSTYPE

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_TRANSTYPE`  | 1.3.0 | pre      | `int32_t`  |  enum   |`SRTT_LIVE`| \*     | W   | S      |

Sets the transmission type for the socket, in particular, setting this option
sets multiple other parameters to their default values as required for a
particular transmission type.

Values defined by enum `SRT_TRANSTYPE` (see above for possible values)

[Return to list](#list-of-options)

---

#### SRTO_TSBPDMODE

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_TSBPDMODE`  | 0.0.0 | pre      | `bool`     |         | \*        |        | W   | S      |

When true, use Timestamp-based Packet Delivery mode. In this mode the
packet's time is assigned at the sending time (or allowed to be predefined),
transmitted in the packet's header, and then restored on the receiver side so that
the time intervals between consecutive packets are preserved when delivering to
the application.

**Default:** true in Live mode, false in File mode (see [`SRTO_TRANSTYPE`](#SRTO_TRANSTYPE)).

[Return to list](#list-of-options)

---

#### SRTO_UDP_RCVBUF

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_UDP_RCVBUF` |       | pre-bind | `int32_t`  | bytes   | 8192 bufs | *      | RW  | GSD+   |

UDP Socket Receive Buffer Size. Configured in bytes, maintained in packets
based on MSS value. Receive buffer must not be greater than FC size.

[Return to list](#list-of-options)

---

#### SRTO_UDP_SNDBUF

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_UDP_SNDBUF` |       | pre-bind | `int32_t`  | bytes   | 65536     | *      | RW  | GSD+   |

UDP Socket Send Buffer Size. Configured in bytes, maintained in packets based
on `SRTO_MSS` value.

[Return to list](#list-of-options)

---

#### SRTO_VERSION

| OptName           | Since | Restrict | Type       |  Units  |  Default  | Range  | Dir | Entity |
| ----------------- | ----- | -------- | ---------- | ------- | --------- | ------ | --- | ------ |
| `SRTO_VERSION`    | 1.1.0 |          | `int32_t`  |         |           |        | R   | S      |

Local SRT version. This is the highest local version supported if not
connected, or the highest version supported by the peer if connected.

The version format in hex is `0x00XXYYZZ` for x.y.z in human readable form.
For example, version 1.4.2 is encoded as `0x010402`.

[Return to list](#list-of-options)

---
