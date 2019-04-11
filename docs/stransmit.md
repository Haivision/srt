SRT Live Transmit
---------

The *srt-live-transmit* tool is a universal data transport tool, which's
intention is to transport data between SRT and other medium.
At the same time it is just a sample application to show some of 
the powerful features of SRT. We encourage you to use SRT library
itself integrated into your products.

The *srt-live-transmit* can be both used as a universal SRT-to-something-else
flipper, as well as a testing tool for SRT.

The general usage is the following:

    srt-live-transmit <input-uri> <output-uri> [options]

The following medium types are handled by *srt-live-transmit*:

- SRT - use SRT for reading or writing, in listener, caller or rendezvous mode, with possibly additional parameters
- UDP - read or write the given UDP address (also multicast)
- Local file - read or store the stream into the file
- Process's pipeline - use the process's *stdin* and *stdout* standard streams

Any medium can be used with any direction, although some of them may
have special direction-dependent cases.

Mind that the URI has a standard syntax:

    scheme://HOST:PORT/PATH?PARAM1=VALUE&PARAM2=VALUE

If you specify only the path (no **://** specified), then the scheme
defaults to **file**. The path can be also specified as relative this
way. Note also that empty host (`scheme://:PORT`) defaults to 0.0.0.0,
and an empty port (when there's no `:PORT` part) defaults to port number 0.

Special options for particular medium may be specified in **PARAM...**
items. All options are medium-specific, although there may happen some
options common for multiple media types.

Note also that the *host* part is always tried to be resolved as a name,
if its form is not directly the IPv4 address.

Medium: FILE (including standard process pipes)
-----------------------------------------------

**NB!** File mode, except `file://con` is supported in *srt-file-transmit* tool!

The general syntax is: `file:///global/path/to/the/file`. No
parameters in the URL are extracted. There's one (non-standard!)
special case, though:

    file://con

That is, **con** is used as a *HOST* part of the URI. If you use this
URI for \<input-uri\>, then the data will be read from the standard
input. If \<output-uri\>, the data will be send to the standard output.
Be careful with options being specified together with having standard
output as output URI - some of them are not allowed as the extra output
controlled by options might interfere with the data output.

Medium: UDP
-----------

UDP can only be used in listening mode for reading, and in calling mode
for writing. Therefore, when UDP is your \<input-uri\>, you usually
specify the local port, e.g.:

    udp://:5555

UDP handles two parameters: **iptos** and **ttl**.
**iptos** will set the value of Type-Of-Service (TOS) field for outgoing packets via IP_TOS socket option.
**ttl** parameter will set time-to-live value for outgoing packets via IP_TTL or IP_MULTICAST_TTL socket options.
See IP protocol documentation for details.

For a single host IP address (unicast):
* **reading**: The *host* part or **adapter** parameter can specify the adapter. The *port* part is mandatory.
* **writing**: Both *host* and *port* are mandatory. The **adapter** parameter is of no use.

If you use multicast IP address:
* For reading, need extra `@` character before the *host* part so that the application subscribes to the multicast group before reading
* The *host* part designates the multicast group (also as a resolvable name)
* The *port* designates the port in the multicast group
* The **adapter** parameter can be used to specify the adapter through which the given multicast group can be reached

Medium: SRT
-----------

Most important about SRT is that it can be either input or output and in
both these cases it can work in listener, caller and rendezvous mode. SRT
also handles several parameters special way, in addition to standard SRT
options that can be set through the parameters:

    srt://HOST:PORT?PARAM1=VALUE&PARAM2=VALUE...

SRT can be connected using one of three connection modes:

- **caller**: the "agent" (this application) sends the connection request to the peer, which must be **listener**, and this way it initiates the connection.
- **listener**: the "agent" waits for being contacted by any peer **caller** (note that a listener can accept multiple callers, but *srt-live-transmit* does not use this possibility - after the first connected one, it no longer accepts new connections).
- **rendezvous**: A one-to-one only connection where both parties are equivalent and both connect to one another simultaneously. Whoever happened to start first (or succeeded to punch through the firewall) is meant to have initiated the connection.

This mode can be specified explicitly using the **mode** parameter. When it's not specified, then it is "deduced" the following way:

- `srt://:1234` - the *port* is specified (1234), but *host* is empty. This assumes **listener** mode.
- `srt://remote.host.com:1234` - both *host* ***and*** *port* are specified. This assumes **caller** mode.

The **rendezvous** mode is not deduced and it has to be specified
explicitly. Note also special cases of the **host** and **port** parts
specified in the URI:

- **CALLER**: the *host* and *port* parts are mandatory and specify the remote host and port to be contacted.
    -   The **port** parameter can be used to enforce the local outgoing port (**not to be confused** with remote port!).
    -   The **adapter** parameter is not used.
- **LISTENER**: the *port* part is mandatory and it specifies the local listening port.
    -   The **adapter** parameter can be used to specify the adapter.
    -   The *host* part, if specified, can be also used to set the adapter - although in this case **mode=listener** must be set explicitly.
    -   The **port** parameter is not used.
- **RENDEZVOUS**: the *host* and *port* parts are mandatory.
    -   The *host* part specifies the remote host to contact.
    -   The *port* part specifies **both local and remote port**. Note that the local port is this way both listening port and outgoing port.
    -   The **adapter** parameter can be used to specify the adapter.
    -   The **port** parameter is not used.

Some parameters handled for SRT medium are specific, all others are socket options. The following parameters are handled special way by *srt-live-transmit*:

- **mode**: enforce caller, listener or rendezvous mode
- **port**: enforce the **outgoing** port (the port number that will be set in the UDP packet as a source port when sent from this host). This can be used only in **caller mode**.
- **blocking**: sets the `SRTO_RCVSYN` for input medium or `SRTO_SNDSYN` for output medium
- **timeout**: sets `SRTO_RCVTIMEO` for input medium or `SRTO_SNDTIMEO` for output medium
- **adapter**: sets the adapter for listening in *listener* or *rendezvous* mode

All other parameters are SRT socket options. Here are some most characteristic options:

- **latency**: Sets the maximum accepted transmission latency and should be >= 2.5 times the RTT (default: 120ms; when both parties set different values, the maximum of the two is used for both)
- **passphrase**: Sets the password for the encrypted transmission.
- **pbkeylen**:  Crypto key len in bytes {16,24,32} Default: 16 (128-bit)
- **tlpktdrop**: Whether to drop packets that are not delivered on time. Default is on.
- **conntimeo**: Connection timeout (in ms). Caller default: 3000, rendezvous (x 10)

For the complete list of options, please refer to the SRT header file `srt.h` and search for `SRT_SOCKOPT` enum type. Please note that the set of available options may be version dependent. All options are available under the lowercase name of the option without the `SRTO_` prefix. For example, `SRTO_PASSPHRASE` can be set using
a **passphrase** parameter. The mapping table `srt_options` can be found in `common/socketoptions.hpp` file.

Important thing about the options (which holds true also for options for
TCP and UDP, even though it's not described anywhere explicitly) is
that there are two categories of options:

- PRE options: these options must be set to the socket prior to connecting and they cannot be altered after the connection is made. A PRE option set to a listening socket will be also derived by the socket returned by `srt_accept()`.
- POST options: these options can be set to a socket at any time. The option set to a listening socket will not be derived by an accepted socket.

You don't have to worry about that actually - the application is aware
of this and it sets these options at appropriate time.

Note also that **blocking** option has no practical use for users.
Normally the non-blocking mode is used only when you have an event-driven application that needs a common
signal bar for multiple event sources, or you prefer fibers to threads, when working with multiple SRT sockets in one application. The *srt-live-transmit* application isn't defined this way. This makes that the practical result of non-blocking mode here is that it uses polling on exactly one socket with infinite timeout. Every reading and writing operation will then return always without blocking, but when they report the "again" situation the application will stall on `srt_epoll_wait()` call. This option then exists for the testing purposes, as well as educational, to serve as an example of how your application should use the non-blocking mode.


Command-line Options
--------------------

The following options are available. Note that some may affect specifically only selected type of medium.

Options usually have values and they are set using **colon**: for
example, **-t:60**. Alternatively you can also separate them by a space,
but this space must be part of the parameter and not extracted by a
shell (using quotes or backslash).

- **-timeout, -t, -to** - Sets the timeout for any activity from any medium (in seconds). Default is 0 for infinite (that is, turn this mechanism off). The mechanism is such that the SIGALRM is set up to be called after the given time and it's reset after every reading succeeded. When the alarm expires due to no reading activity in defined time, it will break the application. **Notes:**
    - The alarm is set up after the reading loop has started, **not when the application has started**. That is, a caller will still wait the standard timeout to connect, and a listener may wait infinitely until some peer connects; only after the connection is established is the alarm counting started. 
    - **The timeout mechanism doesn't work on Windows at all.** It behaves as if the timeout was set to **-1** and it's not modifiable.
- **-timeout-mode, -tm** - timeout mode used. Default is 0 - timeout will happen after the specified time. Mode 1 cancels the timeout if the connection was established.
- **-chunk, -c** - use given size of the buffer. When 0, it uses default 1316, which is the maximum size for a single SRT sending call
- **-verbose, -v** - display additional information on the standard output. Note that it's not allowed to be combined with output specified as **file://con**
- **-stats", -stats-report-frequency, -s** - Output periodic SRT statistics reports to the standard output or file (see **-statsout**).
- **-statsout"** - SRT statistics output: filename. Without this option specified the statistics will be printed to standard output.
- **-pf, -statspf** - SRT statistics print format. Values: **json**, **csv**, **default**.
- **-loglevel** - lowest logging level for SRT, one of: *fatal, error, warning, note, debug* (default: *error*)
- **-logfa** - selected FAs in SRT to be logged (default: all is enabled, that is, you can filter out log messages comong from only wanted FAs using this option)
- **-stats-report-frequency, -stats, -s** - how often the statistics for SRT should be displayed (frequency specified like with -r option)
- **-help, -h** - show help
- **-version** - show version info
