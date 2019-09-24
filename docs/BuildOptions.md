Build system
============

The main build system for SRT is provided by `cmake` tool. You can always use
it directly, if you want. However, there's also a wrapper script that can make
operating with the options easier (requires Tcl interpreter), which is a
script named `configure`.


Portability
===========

The cmake build system was tested on the following platforms:

 - Linux (various flavors)
 - Mac OS (you may want to see [separate document](build_iOS.md))
 - Windows with MinGW
 - Windows with Microsoft Visual Studio
 - Android (see [separate document](Android/Compiling.md))
 - Cygwin (only for testing)

The configure script wasn't tested on Windows (unless on Cygwin)


The configure helper script
===========================

This script is designed to look similar as the Autotools' configure script
so usually it handles `--long-options`, possibly with values. It handles
two kinds of options:

* special options to be resolved inside the script and possibly do some
advanced checks; this should later turn into a set of specific cmake
variable declarations

* options that are directly translated to cmake variables

The last one always does a simple transformation:

* all letters uppercase
* dash into underscore
* plus into X
* when no value is supplied, it defaults to 1

So, e.g., `--enable-c++11` turns into `-DENABLE_CXX11=1` option passed
to cmake.

Additionally, if you specify `--disable-<X>`, the `configure` script
will automatically turn it into an associated `--enable-<X>` option
with passing `0` as its value. So e.g. `--disable-encryption` will
be translated for cmake into `-DENABLE_ENCRYPTION=0`.

All options below will be presented in the `configure` convention. All
these options are available as well in cmake with appropriately changed
name format.


Build options
=============

`--cygwin-use-posix` (default:OFF)
----------------------------------

When ON, compile on Cygwin using POSIX API (otherwise it will use MinGW environment).


`--enable-apps` (default: ON)
-----------------------------

Enables compiling user applications.


`--enable-code-coverage` (default: OFF)
---------------------------------------

Enable instrumentation for code coverage. Note that this is only available
on POSIX-compatible platforms.


`--enable-c++-deps` (default: OFF)
----------------------------------

The `pkg-confg` file (`srt.pc`) will be generated with having the
libstdc++ library among dependencies. This may be required in some
cases where you have an application written in C and therefore it
won't link against libstdc++ by default.

`--enable-c++11` (default: ON)
-------------------------------

Enable compiling in C++11 mode for those parts that may require it.
Parts that don't require it will be compiled still in C++03 mode,
although which parts are affected, may change in future.

If this option is turned off, it affects building this project twofold:

* an alternative C++03 implementation can be used, if available
* otherwise the component that requires it will be disabled

Parts that currently require C++11 and have no alternative implementation
are:

* unit tests
* user and testing applications (such as `srt-live-transmit`)
* some of the example applications

The SRT library alone should be able to be compiled without C++11
support, however this alternative C++03 implementation may be unsupported
on some platforms.


`--enable-debug=<0,1,2>`
-------------------------------

This option allows a control through the `CMAKE_BUILD_TYPE` variable:

* 0 (default): `Release` (highly optimized, no debug info)
* 1: `Debug` (not optimized, full debug info)
* 2: `RelWithDebInfo` (highly optimized, but with debug info)

Please note that when the value is other than 0, it makes the
`--enable-heavy-logging` option also ON by default.


`--enable-encryption` (default: ON)
-----------------------------------

Encryption feature enabled, which involves dependency on an external encryption
library (default: openssl). If you disable encryption, the library will be unable
to set encryption options, although it will be compatible with a peer that has
encryption enabled, just doesn't use encryption for the connection.


`--enable-getnameinfo` (default: OFF)
-------------------------------------

Enables the use of `getnameinfo` in a "rich" style that allows
using reverse DNS to resolve an internal IP address into a readable
internet domain name, so that it can be shown nicely in the log
file. Not turned on by default because it may cause a dirty impact
on performance in general. This is actually useful only for
development when testing in the local network.


`--enable-haicrypt-logging`
-------------------------------

Enables logging in the *haicrypt* module, which serves as a connector to
an encryption library. Logging here might be seen as unsafe sometimes,
therefore it's turned off by default.


`--enable-heavy-logging` (default: OFF in release mode)
-------------------------------------------------------

This option enables the logging instructions in the code that are considered
heavy, they occur often and inform about very detailed parts of the library
behavior (usually debug-level logs). Turning this option ON will allow you
using the `debug` level of logging and get a very detailed information as to what
happens inside the library. Note however that this may influence the processing
by changing times, use less preferred thread switching layout, and generally
worsen the functionality and performance of the library. Therefore this option
is not turned on by default.


`--enable-inet-pton` (default: ON)
----------------------------------

Enables usage of `inet_pton` function by the applications, which should be used
to resolve the network endpoint name into an IP address. This may be not
availabe on some version of Windows, in which case you can turn this OFF,
however at the expense of not being able to resolve IP address by name,
as the `inet_pton` function gets a poor-man's simple replacement that can
only resolve numeric IPv4 addresses.


`--enable-logging` (default: ON)
-------------------------------

Enables logging. When you turn this option OFF, logging will not be
done at all. Could be tried when you suspect the logging system of
impairing the performance, at the expense of not having some information
about errors happening during runtime.


`--enable-monotonic-clock` (default: OFF)
-----------------------------------------

Enforced `clock_gettime` with monotonic clock on Garbage Collector CV.
**This is a temporary fix for #729**: The library could got stuck when you
modify the system clock while a transmission is done over SRT.

This option enforces the use of `clock_gettime` to get the current
time, instead of `gettimeofday`. This function allows to force a monotonic
clock, which is independent on the currently set time in the system. The CV,
for which the `*_timedwait()` functions are used with so obtained current time,
must be appropriately configured (and this is done so for now only for the
GarbageCollector controlling CV, not every CV used in SRT). The consequence of
enabling this option, however, may be portability issues around the
`clock_gettime()` function, which is not available on every SDK, or extra
`-lrt` option is sometimes required (this requirement will be autodetected).

This option will be removed when the problem is fixed globally.


`--enable-pktinfo` (default: OFF)
---------------------------------

This option allows extracting the target IP address from the incoming
UDP packets and forceful setting of the source IP address in the outgoing
UDP packets. This ensures that if a packet comes in from a peer that requests a
new connection, the agent will respond with a UDP packet, which has the
source IP address exactly the same as the one, which the peer is trying to
connect to.

When this is OFF, the source IP address in such outgoing UDP packet will
be set automatically the following way:

* For given destination IP address in the UDP packet to be sent, find
the routing table that matches this address, get its network device
and configured network broadcast address

* set the **first** local IP address that matches the broadcast
address as found above as the source IP address for this UDP packet

Exmaple: You have the following local IP addreses:

* 192.168.10.11: broadcast 192.168.10.0
* 10.0.1.15: broadcast 10.0.1.0
* 10.0.1.20: broadcast 10.0.1.0

When a caller is contacting this first address (no matter where it came
from), the response packet will be sent back to this address and the
route path will use this first one as well, so the IP address will
be 192.168.10.11, same as the one that was contacted.

However, if the caller handshake packet comes from the address that
matches the 10.0.1.0 broadcast, that is, common for the second and
third address, and the target address will be 10.0.1.20, the response
packet will be sent back to this address over the network assigned
to the 10.0.1.0 broadcast, but the source address will be then 10.0.1.15
because this is the first local address assigned to this route path.

When this happens, the caller peer will see a mismatch between the
source 10.0.1.15 address and the address it tried to contact, that
is, 10.0.1.20. It will be then treated as an attack attempt and
rejected.

This problem can be slightly mitigated by binding the listening socket
to the exact address. So, if you bind it to 10.0.1.20 in the above
example, then wherever you try to send the packet over such a socket,
it will always have the source address 10.0.1.20 (and the fix provided
by this option will also not apply in this case). However, this problem
still exists if the listener socket is bound to the "whole machine",
that is, it's set to "any" address.

This option fixes this problem by forcefully setting the source IP address in
such a response packet to 10.0.1.20 as in the above example and this will be
interpreted by the caller peer correctly.

This feature is turned off by default because the impact on performance
here is currently unknown. The problem here is that this causes
that the CMSG information be read from and set on every packet in case
when the socket was bound to the "any" address, so effectively it will
be extracted from every incoming packet, as long as it is not bound
to a specific address, including data packets coming in within the
frames of an existing connection.


`--enable-profile` (default: OFF)
-------------------------------

Enables code instrumentation for profiling.

This is available only for GNU-compatible compilers.


`--enable-relative-libpath` (default: OFF)
------------------------------------------

Enables adding the relative path to a library that allows applications
to get linked against a shared SRT library by reaching out to a sibling
`../lib` directory, provided that the library and applications are installed
in the POSIX/GNU style directories. This might be useful when instaling
SRT and the applications in some its own directory, where the global library
path does not lead. There have been reported problems that it may cause
additionally, so this is OFF by default.


`--enable-shared` and `--enable-static` (both default: ON)
----------------------------------------------------------

Enables building SRT as a shared and static library.

As both are ON by default, this option allows you for turning
off building a library in a style that is not of your interest,
for example, leave only static one by `--disable-shared`. You
can't disable both at once.


`--enable-testing` (default: OFF)
---------------------------------

Enables compiling developer testing appliactions.


`--enable-thread-check` (default: OFF)
--------------------------------------

Enable `#include <threadcheck.h>` that implements `THREAD_*` macros".

This is a solution used by one of the users to support better thread
debugging.


`--enable-unittests` (default: OFF)
-----------------------------------

Developer option. When ON, the unit tests, possibly with downloading and installing
the google test library in the build directory, will be enabled, and the tests will
be run as part of the build process.


`--openssl-crypto-library=<filepath>`
-------------------------------

Configure the path to an OpenSSL Crypto library.


`--openssl-include-dir=<path>`
-------------------------------

Configure the path to include files for OpenSSL library.


`--openssl-ssl-library=<filepath>`
-------------------------------

Configure the path to an OpenSSL SSL library.


`--pkg-config-executable=<filepath>`
-------------------------------

Configure the path to `pkg-config` tool.


`--prefix=<path>`
-----------------

This is an alias to `--cmake-install-prefix`. This is the root
directory for installation, inside which next GNU/POSIX compatible
directory layout will be used for particular parts. As on all
known build systems, this defaults to `/usr/local` on POSIX
compatible systems.


`--pthread-include-dir=<path>`
-------------------------------

Configure the path to include files for pthread library
(note that this makes sense only on Windows; on Linux
and MacOS this path should be available in the system)


`--pthread-library=<filepath>`
-------------------------------

Configure the path to a pthread library.


`--use-busy-waiting` (default: OFF)
-----------------------------------

Enable more accurate sending times at a cost of potentially higher CPU load.

This option will cause more empty loop running, which may cost
more CPU usage, but in case of processing high bitrate streams
the share of empty loop runs will decrease the more the
higher the bitrate, whereas without system-supported waiting it
may give more chance to switch to the right thread at the time
when it is expected to be revived.


`--use-gnustl`
-------------------------------

Use `pkg-config` with `gnustl` package name to extract the
header and library path for the C++ standard library (instead
of using the compiler-builtin one).


`--use-enclib=<name>`
-------------------------------

Encryption library to be used. Possible choice for `<name>`:

* openssl(default)
* gnutls (with nettle)
* mbedtls


`--use-openssl-pc` (default: ON)
--------------------------------

Use `pkg-config` to find OpenSSL libraries. You can turn this
off to force cmake to find OpenSSL by its own most preferred
method.


`--use-static-libstdc++` (default: OFF)
---------------------------------------

Enforces linking the SRT library against the static libstdc++
library. This may be useful if you are using SRT library in
an environment, where it would by default link against the
wrong version of the C++ standard library, or the library in
this version that was used by the compiler is not available
as shared.


`--with-compiler-prefix=<prefix>`
---------------------------------

Set C/C++ toolchains `<prefix>gcc` and `<prefix>g++`.

This option will override the compiler and it's handled inside cmake.
This sets variables `CMAKE_C_COMPILER` and `CMAKE_CXX_COMPILER`. Given
prefix is then appropriately extended with a suffix dependent on the
compiler type - see `--with-compiler-type`.


`--with-compiler-type=<name>`
-----------------------------

Sets the compiler type to be used when `--with-compiler-prefix`:

* gcc (default): gcc and g++
* cc: cc and c++
* others: use the `<name>` as C compiler and `<name>++` as C++ compiler








