SRT and predictability
======================

Please keep in mind that SRT as a library that deals with network, which often
happens to behave unpredictable way, tries to do its best to deal with problems
resulting thereof, but as all other software of that kind around the world, it
isn't perfect. Because of that the "best effort" is all you can count on.

But again, things can always go better. That said, we welcome warmly everyone
who can contribute to SRT and make it better. In order to make this process
most possibly efficient, though, please read the following guidelines, which
are predicted to make your effort of reporting problems worthwhile for you and
the whole SRT community.


Problem reporting guidelines
============================

1. We treat every problem report very seriously and will be doing our best to
solve it, but we need something to start the research with. Just the report of
the behavior and maybe error logs may show us the direction, but please be
prepared that this information may be too little. If you can replicate this
behavior, collecting the debug logs and the pcap file will be way more helpful.

2. Sometimes problems result from the network, which doesn't satisfy the
minimum requirements for SRT, such as the available bandwidth must be enough to
bear the traffic you are going to send through it, or the latency must be high
enough for the network's maximum non-spiked RTT. We need to sort this kind of
problem out first.

3. We will be trying to reinstate your environment in our lab in order to be
able to test this case ourselves. Therefore a thorough description of it is
very important.

4. Note that in many situation reinstating your environment may be impossible,
or there are simply some nuisances in your environment or network configuration
that it's impossible for you to even be aware of. Even if you are using traffic
shaping using tc and netem, it may still happen that there some extra distinct
settings in the network so reusing this command may not exactly restore this
environment. Therefore we may need you to help us by providing debug logs and
the pcap file.

5. On the other hand, do not hesitate to report any unexpected behavior anyway,
even if your information seems to be too little - there are many ways to
complete your information, also by other users, or sometimes we may have good
luck to replicate it.

6. If you ever see the `IPE` (Internal Program Error) keyword in the error
logs, please try to report that as a top priority (just check if it wasn't
reported already). This reports the execution path that shall never be taken.


Debug logs
==========

The debug logs is a part of very detailed behavior description inside SRT. In
fact, they comprise the best effort to be close to a possibility to "record and
replay" the testing session. As SRT is very highly time based software, the
usability of a debugger is very limited, and having the debug logs collected is
in most cases essential to start researching every potential problem.

However, as debug logs put a great burden on the performance, they have been
shifted to the "heavy logging" category, and they are not enabled by default,
neither in the library itself, not at compile time. To enable them, you need
to first enable them at compile time:

    ./configure [...] --enable-heavy-logging

or directly in cmake:

    cmake [...] -DENABLE_HEAVY_LOGGING=1

Note that in the *Debug mode* (`--enable-debug`) heavy logging is enabled by
default. Mind though that the *Debug mode* creates a less optimized version,
more suitable for the debugger.

Enabling heavy logging at compile time is required, but the debug logging level
must be also set at runtime. For `srt-live-transmit` application use the
following option:

    -loglevel:debug

If you are using any other application that uses SRT as a library, follow the
description in this application; worst case when no such thing is available,
remember that the SRT API call to set the debug log level is:

    srt_setloglevel(LOG_DEBUG);

(The `LOG_DEBUG` symbol is defined in `<sys/syslog.h>` include file on
POSIX-based systems and there is a drop-in replacement for it for Windows
in `common/win/syslog_defs.h`.)

Some applications may use a (not really recommended) the C++ extended API:

    UDT::setloglevel(logging::Loglevel::debug);

When running an application with debug logs, please remember that they will
still put a burden on the program's performance. Always stream the log into
a file, or even it may be in some cases necessary to send it over the network
to another machine for collection, if the filesystem is so slow that the
performance burden changes the rules. It has been observed on several platform
types that the burden may make the application unusable or turning on the logs
may prevent the problem from occurring ("heisenbug") or decrease its
probability ("schroedingbug").


Pcap file
=========

Recording the pcap file may be also very useful in any researching in SRT.

For tracing a pcap, you need to have administrator privileges to the machine,
where you run it, and you need to record it on the machine on which the SRT
application instance is using a predictable port number, that is:
 - With rendezvous connection, on any of the machines
 - On the listener machine, where you use the listening port
 - On the caller machine, if you explicitly request the caller's outgoing port

(To se the caller's outgoing port explicitly, use `port=<number>` parameter
in the SRT URI.)

To record the PCAP file on POSIX-based systems, use the following incantation:

	[sudo] tcpdump -i eth0 port 9000 -w test.pcap

(Replace `eth0` with your device name and `9000` with the connection port.)

On Windows there's a similar solution, the Windump application.




