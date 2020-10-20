# srt-live-transmit

The `srt-live-transmit` tool is a universal data transport tool with a purpose to transport data between SRT and other medium.
At the same time it is just a sample application to show some of the powerful features of SRT. We encourage you to use SRT library itself integrated into your products.

## Introduction

The `srt-live-transmit` can be both used as a universal SRT-to-something-else flipper, as well as a testing tool for SRT.

The general usage is the following:

```shell
srt-live-transmit <input-uri> <output-uri> [options]
```

The following medium types are handled by `srt-live-transmit`:

- SRT - use SRT for reading or writing, in listener, caller or rendezvous mode, with possibly additional parameters
- UDP - read or write the given UDP address (also multicast)
- Local file - read or store the stream into the file
- Process's pipeline - use the process's `stdin` and `stdout` standard streams

Any medium can be used with any direction, although some of them may
have special direction-dependent cases.

Mind that the URI has a standard syntax:

```yaml
scheme://HOST:PORT/PATH?PARAM1=VALUE1&PARAM2=VALUE2&...
```

The first parameter is introduced with a `?` and all following can be appended with an `&` character.

If you specify only the path (no **://** specified), then the scheme
defaults to **file**. The path can be also specified as relative this
way. Note also that empty host (`scheme://:PORT`) defaults to 0.0.0.0,
and an empty port (when there's no `:PORT` part) defaults to port number 0.

Special options for particular medium may be specified in **PARAM**
items. All options are medium-specific, although there may happen some
options common for multiple media types.

Note also that the **HOST** part is always tried to be resolved as a name,
if its form is not directly the IPv4 address.

### Example for Smoke Testing

First we need to start up the `srt-live-transmit` app, listening for unicast UDP TS input on port 1234 and making SRT available on port 4201. Note, these are randomly chosen ports. We also open the app in verbose mode for debugging:

```shell
srt-live-transmit udp://:1234 srt://:4201 -v
```

Now we need to generate a UDP stream. ffmpeg can be used to generate bars and tone as follows, doing a simple unicast push to our listening `srt-live-transmit` application:

```shell
ffmpeg -f lavfi -re -i smptebars=duration=300:size=1280x720:rate=30 -f lavfi -re -i sine=frequency=1000:duration=60:sample_rate=44100 -pix_fmt yuv420p -c:v libx264 -b:v 1000k -g 30 -keyint_min 120 -profile:v baseline -preset veryfast -f mpegts "udp://127.0.0.1:1234?pkt_size=1316"
```

You should see the stream connect in `srt-live-transmit`.

Now you can test in VLC (make sure you're using the latest version!) - just go to file -> open network stream and enter
`srt://127.0.0.1:4201` and you should see bars and tone right away.

If you're having trouble, make sure this works, then add complexity one step at a time (multicast, push vs listen, etc.).

## URI Syntax

Transmission mediums are specified as the standard URI format:

```yaml
SCHEME://HOST:PORT?PARAM1=VALUE1&PARAM2=VALUE2&...
```

The applications supports the following schemes:

- `file` - for file or standard input and output
- `udp` - UDP output (unicast and multicast)
- `srt` - SRT connection

Note that this application doesn't support file as a medium, but this
can be handled by other applications from this project.

### Medium: FILE (including standard process pipes)

**NB!** File mode, except `file://con`, is not supported in the `srt-file-transmit` tool!

The general syntax is: `file:///global/path/to/the/file`. No parameters in the URL are extracted. There's one (non-standard!) special case, though:

```yaml
file://con
```

That is, **con** is used as a *HOST* part of the URI. If you use this
URI for \<input-uri\>, then the data will be read from the standard
input. If \<output-uri\>, the data will be send to the standard output.
Be careful with options being specified together with having standard
output as output URI - some of them are not allowed as the extra output
controlled by options might interfere with the data output.

## Medium: UDP

UDP can only be used in listening mode for input, and in calling mode
for output. Multicast Streaming is also possible, without any special declaration. Just use an IP address from the multicast range.
The specification and meaning of the fields in the URI depend on the mode.

The **PORT** part is always mandatory and it designates either the port number
for the target host or the port number to be bound to read from.

For sending to unicast:

```yaml
udp://TARGET:PORT?parameters...
```

- The **HOST** part (here: TARGET) is mandatory and designates the target host

- The **iptos** parameter designates the Type-Of-Service (TOS) field for
outgoing packets via `IP_TOS` socket option.

- The **ttl** parameter will set time-to-live value for outgoing packets via
`IP_TTL` socket options.

For receiving from unicast:

```yaml
udp://LOCALADDR:PORT?parameters...
```

- The **HOST** part (here: LOCALADDR) designates the local interface to bind.
It's optional (can be empty) and defaults to 0.0.0.0 (`INADDR_ANY`).

For multicast the scheme is:

```yaml
udp://GROUPADDR:PORT?parameters...
```

- The **HOST** part (here: GROUPADDR) is mandatory always and designates the
target multicast group. The `@` character is handled in this case, but it's not
necessary, as the IGMP addresses are recognized by their mask.

For sending to a multicast group:

- The **iptos** parameter designates the Type-Of-Service (TOS) field for
outgoing packets via `IP_TOS` socket option.

- The **ttl** parameter will set time-to-live value for outgoing packets via
`IP_MULTICAST_TTL` socket options.

- The **adapter** parameter can be used to specify the adapter to be set
through `IP_MULTICAST_IF` option to override the default device used for
sending

For receiving from a multicast group:

- The **adapter** parameter can be used to specify the adapter through which
the given multicast group can be reached (it's used to bind the socket)

- The **source** parameter enforces the use of `IP_ADD_SOURCE_MEMBERSHIP`
instead of `IP_ADD_MEMBERSHIP` and the value is set to `imr_sourceaddr` field.

Explanations for the symbols and terms used above can be found in POSIX
manual pages, like `ip(7)` and on Microsoft docs pages under `IPPROTO_IP`.

### Medium: SRT

Most important about SRT is that it can be either input or output and in
both these cases it can work in listener, caller and rendezvous mode. SRT
also handles several parameters special way, in addition to standard SRT
options that can be set through the parameters.

SRT can be connected using one of three connection modes:

- **caller**: the "agent" (this application) sends the connection request to
  the peer, which must be **listener**, and this way it initiates the
connection.

- **listener**: the "agent" waits to be contacted by any peer **caller**.
Note that a listener can accept multiple callers, but *srt-live-transmit*
does not use this ability; after the first connection, it no longer
accepts new connections.

- **rendezvous**: A one-to-one only connection where both parties are
  equivalent and both connect to one another simultaneously. Whoever happened
to start first (or succeeded to punch through the firewall) is meant to have
initiated the connection.

This mode can be specified explicitly using the **mode** parameter. When it's
not specified, then it is "deduced" the following way:

- `srt://:1234` - the *port* is specified (1234), but *host* is empty. This assumes **listener** mode.
- `srt://remote.host.com:1234` - both *host* and *port* are specified. This assumes **caller** mode.

When the `mode` parameter is specified explicitly, then the interpretation of the `host` part is the following:

- For caller, it's always the destination host address. If this is empty, it is
resolved to 0.0.0.0, which usually should mean connecting to the local host

- For listener, it defines the IP address of the local device on which the
socket should listen, e.g.:

```yaml
srt://10.10.10.100:5001?mode=listener
```

An alternative method to specify this IP address is the `adapter` parameter:

```yaml
srt://:5001?adapter=10.10.10.100
```

The **rendezvous** mode is not deduced and it has to be specified
explicitly. Note also special cases of the **host** and **port** parts
specified in the URI:

- **CALLER**: the *host* and *port* parts are mandatory and specify the remote host and port to be contacted.
  - The **port** parameter can be used to enforce the local outgoing port (**not to be confused** with remote port!).
  - The **adapter** parameter is not used.
- **LISTENER**: the *port* part is mandatory and it specifies the local listening port.
  - The **adapter** parameter can be used to specify the adapter.
  - The *host* part, if specified, can be also used to set the adapter - although in this case **mode=listener** must be set explicitly.
  - The **port** parameter is not used.
- **RENDEZVOUS**: the *host* and *port* parts are mandatory.
  - The *host* part specifies the remote host to contact.
  - The *port* part specifies **both local and remote port**. Note that the local port is this way both listening port and outgoing port.
  - The **adapter** parameter can be used to specify the adapter.
  - The **port** parameter can be used to specify the local port to bind to.

**IMPORTANT** information about IPv6.

This application can also use an address specified as IPv6 with
the following restrictions:

1. The IPv6 address in the URI is specified in square brackets: e.g.
`srt://[::1]:5000`.

2. In listener mode, if you leave the host empty, the socket is bound to
`INADDR_ANY` for IPv4 only. If you want to make it listen on IPv6, you need to
specify the host as `::`.
NOTE: Don't use square brackets syntax in the adapter specification, 
as in this case only the host is expected.

3. If you want to listen for connections from both IPv4 and IPv6, mind the
`ipv6only` option. The default value for this option is system default (see
system manual for `IPV6_V6ONLY` socket option); if unsure, you might want to
enforce `ipv6only=0` in order to be able to accept both IPv4 and IPv6
connections in the same listener.

4. In rendezvous mode you may only interconnect both parties using IPv4, 
or both using IPv6. Unlike listener mode, if you want to leave the socket
default-bound (you don't specify `adapter`), the socket will be bound with the
same IP version as the target address. If you do specify `adapter`,
then both this address and the target address must be of the same family.

Examples:

* `srt://:5000` defines listener mode with IPv4.

* `srt://[::]:5000` defines caller mode (!) with IPv6.

* `srt://[::]:5000?mode=listener` defines listener mode with IPv6. If the
* default value for `IPV6_V6ONLY` system socket option is 0, it will accept
* also IPv4 connections.

* `srt://192.168.0.5:5000?mode=rendezvous` will make a rendezvous connection
* with local address `INADDR_ANY` (IPv4) and port 5000 to a destination with
* port 5000.

* `srt://[::1]:5000?mode=rendezvous&port=4000` will make a rendezvous
* connection with local address `inaddr6_any` (IPv6) and port 4000 to a
* destination with port 5000.

* `srt://[::1]:5000?adapter=127.0.0.1&mode=rendezvous` - this URI is invalid
* (different IP versions for binding and target address)

Some parameters handled for SRT medium are specific, all others are socket options. The following parameters are handled special way by `srt-live-transmit`:

- **mode**: enforce caller, listener or rendezvous mode
- **port**: enforce the **outgoing** port (the port number that will be set in the UDP packet as a source port when sent from this host). This can be used only in **caller mode**.
- **blocking**: sets the `SRTO_RCVSYN` for input medium or `SRTO_SNDSYN` for output medium
- **timeout**: sets `SRTO_RCVTIMEO` for input medium or `SRTO_SNDTIMEO` for output medium
- **adapter**: sets the adapter for listening in *listener* or *rendezvous* mode

All other parameters are SRT socket options. The following have the following value types:

- `bool`. Possible values: `yes`/`no`, `on`/`off`, `true`/`false`, `1`/`0`.
- `bytes` positive integer [1; INT32_MAX].
- `ms` - positive integer value of milliseconds.

| URI param            | Values           | SRT Option                | Description |
| -------------------- | ---------------- | ------------------------- | ----------- |
| `congestion`         | {`live`, `file`} | `SRTO_CONGESTION`         | Type of congestion control. |
| `conntimeo`          | `ms`             | `SRTO_CONNTIMEO`          | Connection timeout. |
| `drifttracer`        | `bool`           | `SRTO_DRIFTTRACER`        | Enable drift tracer. |
| `enforcedencryption` | `bool`           | `SRTO_ENFORCEDENCRYPTION` | Reject connection if parties set different passphrase. |
| `fc`                 | `bytes`          | `SRTO_FC`                 | Flow control window size. |
| `groupconnect`       | {`0`, `1`}       | `SRTO_GROUPCONNECT`       | Accept group connections. |
| `groupstabtimeo`     | `ms`             | `SRTO_GROUPSTABTIMEO`     | Group stability timeout. |
| `inputbw`            | `bytes`          | `SRTO_INPUTBW`            | Input bandwidth. |
| `iptos`              | 0..255           | `SRTO_IPTOS`              | IP socket type of service |
| `ipttl`              | 1..255           | `SRTO_IPTTL`              | Defines IP socket "time to live" option. |
| `ipv6only`           | -1..1            | `SRTO_IPV6ONLY`           | Allow only IPv6. |
| `kmpreannounce`      | 0..              | `SRTO_KMPREANNOUNCE`      | Duration of Stream Encryption key switchover (in packets). |
| `kmrefreshrate`      | 0..              | `SRTO_KMREFRESHRATE`      | Stream encryption key refresh rate (in packets). |
| `latency`            | 0..              | `SRTO_LATENCY`            | Defines the maximum accepted transmission latency. |
| `linger`             | 0..              | `SRTO_LINGER`             | Link linger value |
| `lossmaxttl`         | 0..              | `SRTO_LOSSMAXTTL`         | Packet reorder tolerance. |
| `maxbw`              | 0..              | `SRTO_MAXBW`              | Bandwidth limit in bytes |
| `messageapi`         | `bool`           | `SRTO_MESSAGEAPI`         | Enable SRT message mode. |
| `minversion`         | maj.min.rev      | `SRTO_MINVERSION`         | Minimum SRT library version of a peer. |
| `mss`                | 76..             | `SRTO_MSS`                | MTU size |
| `nakreport`          | `bool`           | `SRTO_NAKREPORT`          | Enables/disables periodic NAK reports |
| `oheadbw`            | 5..100           | `SRTO_OHEADBW`            | limits bandwidth overhead, percents |
| `packetfilter`       | `string`         | `SRTO_PACKETFILTER`       | Set up the packet filter. |
| `passphrase`         | `string`         | `SRTO_PASSPHRASE`         | Password for the encrypted transmission. |
| `payloadsize`        | 0..              | `SRTO_PAYLOADSIZE`        | Maximum payload size. |
| `pbkeylen`           | {16, 24, 32}     | `SRTO_PBKEYLEN`           | Crypto key length in bytes. |
| `peeridletimeo`      | `ms`             | `SRTO_PEERIDLETIMEO`      | Peer idle timeout. |
| `peerlatency`        | `ms`             | `SRTO_PEERLATENCY`        | Minimum receiver latency to be requested by sender. |
| `rcvbuf`             | `bytes`          | `SRTO_RCVBUF`             | Receiver buffer size |
| `rcvlatency`         | `ms`             | `SRTO_RCVLATENCY`         | Receiver-side latency. |
| `retransmitalgo`     | {`0`, `1`}       | `SRTO_RETRANSMITALGO`    | Packet retransmission algorithm to use. |
| `sndbuf`             | `bytes`          | `SRTO_SNDBUF`             | Sender buffer size. |
| `snddropdelay`       | `ms`             | `SRTO_SNDDROPDELAY`       | Sender's delay before dropping packets. |
| `streamid`           | `string`         | `SRTO_STREAMID`           | Stream ID (settable in caller mode only, visible on the listener peer). |
| `tlpktdrop`          | `bool`           | `SRTO_TLPKTDROP`          | Drop too late packets. |
| `transtype`          | {`live`, `file`} | `SRTO_TRANSTYPE`          | Transmission type |
| `tsbpdmode`          | `bool`           | `SRTO_TSBPDMODE`          | Timestamp-based packet delivery mode. |

The list of socket options can also be found in SRT header file `srt.h` (`SRT_SOCKOPT` enum type).
Please note that the set of available options may be version dependent.
All options are available under the lowercase name of the option without the `SRTO_` prefix.
For example, `SRTO_PASSPHRASE` can be set using a **passphrase** parameter.
The mapping table `srt_options` can be found in `common/socketoptions.hpp` file.

Important thing about the options (which holds true also for options for
TCP and UDP, even though it's not described anywhere explicitly) is
that there are two categories of options:

- PRE options: these options must be set to the socket prior to connecting and they cannot be altered after the connection is made. A PRE option set to a listening socket will be also derived by the socket returned by `srt_accept()`.
- POST options: these options can be set to a socket at any time. The option set to a listening socket will not be derived by an accepted socket.

You don't have to worry about that actually - the application is aware
of this and it sets these options at appropriate time.

Note also that **blocking** option has no practical use for users.
Normally the non-blocking mode is used only when you have an event-driven application that needs a common
signal bar for multiple event sources, or you prefer fibers to threads, when working with multiple SRT sockets in one application. The *srt-live-transmit* application isn't defined this way. This makes that the practical result of non-blocking mode here is that it uses polling on exactly one socket with infinite timeout. Every reading and writing operation will then return always without blocking, but when they report the "again" situation the application will stall on `srt_epoll_wait()` call. This option then exists for the testing purposes, as well as educational, to serve as an example of how your application should use the non-blocking mode.

## Command-Line Options

The following options are available in the application. Note that some may affect specifically only selected type of medium.

Options usually have values and they are set using **colon**: for
example, **-t:60**. Alternatively you can also separate them by a space,
but this space must be part of the parameter and not extracted by a
shell (using **"** **"** quotes or backslash).

- **-timeout, -t, -to** - Sets the timeout for any activity from any medium (in seconds). Default is 0 for infinite (that is, turn this mechanism off). The mechanism is such that the SIGALRM is set up to be called after the given time and it's reset after every reading succeeded. When the alarm expires due to no reading activity in defined time, it will break the application. **Notes:**
  - The alarm is set up after the reading loop has started, **not when the application has started**. That is, a caller will still wait the standard timeout to connect, and a listener may wait infinitely until some peer connects; only after the connection is established is the alarm counting started. 
  - **The timeout mechanism doesn't work on Windows at all.** It behaves as if the timeout was set to **-1** and it's not modifiable.
- **-timeout-mode, -tm** - Timeout mode used. Default is 0 - timeout will happen after the specified time. Mode 1 cancels the timeout if the connection was established.
- **-st, -srctime, -sourcetime** - Enable source time passthrough. Default: disabled. It is recommended to build SRT with monotonic (`-DENABLE_MONOTONIC_CLOCK=ON`) or C++ 11 steady (`-DENABLE_STDCXX_SYNC=ON`) clock to use this feature.
- **-buffering** - Enable source buffering up to the specified number of packets. Default: 10. Minimum: 1 (no buffering).
- **-chunk, -c** - use given size of the buffer. The default size is 1456 bytes, which is the maximum payload size for a single SRT packet.
- **-verbose, -v** - Display additional information on the standard output. Note that it's not allowed to be combined with output specified as **file://con**.
- **-statsout** - SRT statistics output: filename. Without this option specified, the statistics will be printed to the standard output.
- **-pf**, **-statspf** - SRT statistics print format. Values: json, csv, default.
- **-s**, **-stats**, **-stats-report-frequency** - The frequency of SRT statistics collection, based on the number of packets.
- **-loglevel** - lowest logging level for SRT, one of: *fatal, error, warning, note, debug* (default: *error*)
- **-logfa** - selected FAs in SRT to be logged (default: all is enabled, that is, you can filter out log messages from only wanted FAs using this option).
- **-logfile:logs.txt** - Output of logs is written to file logs.txt instead of being printed to `stderr`.
- **-help, -h** - Show help.
- **-version** - Show version info.

## Testing Considerations

Before starting any test with `srt-live-transmit` please make sure your video source works properly. For example: if you use VLC as a test player, send a UDP stream directly to it before routing it through `srt-live-transmit`.

For any MPEG-TS UDP based source make sure it has packet sizes of 1316 bytes. When using `ffmpeg` like in the "Example for Smoke Testing" section above set the `pkt_size=1316` parameter in case your input is a continuous data stream like from a file, camera or data-generator.

When leaving the LAN for testing, please keep an eye on statistics and make sure your round-trip-time (RTT) is not drifting. It's recommended to set the latency 3 to 4 times higher than RTT. Especially on wireless links such as WLAN, Line-of-Sight Radio (LOS) and mobile links such as LTE/4G or 5G the RTT can vary a lot.

If you perform tests on the public Internet, consider checking your firewall rules. The **SRT listener** must be reachable on the chosen UDP port. Same applies to routers using NAT. Please set a port forwarding rule with protocol UDP to the local IP address of the **SRT listener**.

The initiation of an SRT connection (handshake) is decoupled from the stream direction. The
sender of a stream can be an **SRT listener** or an **SRT caller**, as long as the receiving end
uses the opposite connection mode. Typically you use the **SRT listener** on the receiving end,
since it is easier to configure in terms of firewall/router setup. It also makes sense to leave the
Sender in listener mode when trying to connect from various end points with possibly
unknown IP addresses.

### UDP Performance

Performance issues concerning reading from UDP medium were reported
in [#933](https://github.com/Haivision/srt/issues/933) and
[#762](https://github.com/Haivision/srt/issues/762).

The dedicated research showed that at high and bursty data rates (~60 Mbps)
the `epoll_wait(udp_socket)` is not fast enough to signal about the possibility
of reading from a socket. It results in losing data when the input bitrate is very high (above 20 Mbps).

PR [#1152](https://github.com/Haivision/srt/pull/1152) (v1.5.0 and above) adds the possibility
of setting the buffer size of the UDP socket in `srt-live-transmit`.
Having a bigger buffer of UDP socket to store incoming data, `srt-live-transmit` handles higher bitrates.

The following steps have to be performed to use the bigger UDP buffer size.

### Increase the system-default max rcv buffer size

```bash
$ cat /proc/sys/net/core/rmem_max
212992
$ sudo sysctl -w net.core.rmem_max=26214400
net.core.rmem_max = 26214400
$ cat /proc/sys/net/core/rmem_max
26214400
```

### Specify the size of the UDP socket buffer via the URI

Example URI:

```bash
"udp://:4200?rcvbuf=67108864"
```

Example full URI:

```bash
./srt-live-transmit "udp://:4200?rcvbuf=67108864" srt://192.168.0.10:4200 -v
```
