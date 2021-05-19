Making SRT Better
=================

SRT is a library that deals with networks, which often behave in unpredictable
ways. SRT tries to do its best to deal with the resulting problems, but like
any other software of this kind, it isn't perfect. In many cases, "best effort"
is all you can count on.

That being said, it can always be made better. And so we warmly welcome
everyone who can contribute improvements to SRT. 

We encourage you to read the following guidelines, which are based on the
experiences of previous contributors and are intended to make it easier for you
to debug and report problems in a way that benefits the entire SRT community.


Problem Reporting Guidelines
============================

1. We treat every problem report very seriously and will be doing our best to
resolve them, but we need something to start the research with. When you report
a problem, providing a description of the behavior and maybe error logs is a
good start. But sometimes this isn't enough. If you can, try to replicate the
behavior, and attach the debug log(s) and any pcap file(s) to your report.

2. Sometimes problems result from a network that doesn't satisfy the minimum
requirements for SRT. For example, the available bandwidth might not be enough
to bear the traffic you are trying to send through it, or the latency might not
be high enough to compensate for the network's maximum non-spiked RTT. We need
to sort this kind of problem out first.

3. A thorough description of your environment is very important. We will be
trying to recreate it in our lab in order to be able to test your case
ourselves. Note that in many situations this may not be possible. There may be
some peculiarities in your environment or network configuration that you may
not even be aware of. If you are using tc and netem for traffic shaping, there
may be some distinct settings in the network that will make it impossible for
us to see what you are seeing. This is another case where you can help us by
providing debug logs and the pcap files.

4. If you ever see the `IPE` (Internal Program Error) keyword in the error
logs, please try to report that as a top priority (just check if it wasn't
reported already). This reports the execution path that shall never be taken.


5. *Do not hesitate to report any unexpected behavior*,
even if you feel the information is incomplete. We have some tricks up our
sleeves, as do other project members, that may help us fill in the blanks. And
sometimes, Lady Luck is also on our side!


Debug Logs
==========

The debug logs that can be generated with SRT provide very detailed
descriptions of its internal behaviour. In fact, they can sometimes approach
the equivalent of "record and replay" for a testing session. Having the debug
logs collected is in most cases essential to start researching a potential
problem. This is because, as SRT is very highly time-based software, the
usability of a debugger is very limited. Additionally the debug logs allow the
developers to research a problem that they cannot reproduce.

Keep in mind, though, that debug logs put a great burden on the performance,
and for this reason have been shifted to the "heavy logging" category, which is
not enabled by default, neither in the library itself, nor at compile time. You
can only manually enable them at compile time:

    ./configure [...] --enable-heavy-logging

or directly in `cmake`:

    cmake [...] -DENABLE_HEAVY_LOGGING=1

Note that in the *Debug mode* (`--enable-debug`) heavy logging is enabled by
default. Keep in mind that enabling *Debug mode* creates a less optimized
version of SRT, more suitable for the debugger.

Enabling heavy logging at compile time is required, but the debug logging level
must be also set at runtime. For the `srt-live-transmit` application use the
following option:

    -loglevel:debug

If you are using any other application that uses SRT as a library, follow the
description in that application; in the worst case, if no description is
available, remember that the SRT API call to set the debug log level is:

    srt_setloglevel(LOG_DEBUG);

(The `LOG_DEBUG` symbol is defined in the `<sys/syslog.h>` include file on
POSIX-based systems, and there is a drop-in replacement for it for Windows
in `common/win/syslog_defs.h`.)

Some applications may use an extended C++ API (this is not really recommended):

    UDT::setloglevel(logging::Loglevel::debug);

When running an application with debug logs, please remember that they will put
a burden on the program's performance. Always stream the log into a file; it
may be necessary in some cases to send it over the network to another machine
for collection, if the filesystem is so slow that the performance burden
changes the rules. It has been observed on several platform types that the
burden may make the application unusable. Turning on the logs may prevent the
problem you are trying to debug from occurring ("heisenbug") or decrease its
probability ("schroedingbug").


`pcap` Files
============

Recording a pcap file may be very useful in researching an issue with SRT.

For tracing a pcap, you need to have administrator privileges on the machine
where you are running it, and you need to record it on the machine on which the
SRT application instance is using a predictable port number, that is:
 - With a **Rendezvous** connection, on any of the machines
 - On the **Listener** machine, where you use the listening port
 - On the **Caller** machine, if you explicitly set the Caller's outgoing port
	- To set the Caller's outgoing port explicitly, use the `port=<number>`
	  parameter in the SRT URI.

To record the PCAP file on POSIX-based systems, use the following command
(replacing `eth0` with your device name and `9000` with the connection port):

	[sudo] tcpdump -i eth0 port 9000 -w test.pcap


On Windows there's a similar solution, the Windump application.


---


*Thanks for helping us make SRT the best it can possibly be!*


:sunglasses:   **The SRT Project Moderators**
