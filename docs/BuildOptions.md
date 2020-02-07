# Build System

The main build system for SRT is provided by the `cmake` tool, which can be 
used directly. There's also a wrapper script named `configure` (requires Tcl 
interpreter) that can make operating with the options easier.


## Portability

The `cmake` build system was tested on the following platforms:

 - Linux (various flavors)
 - macOS (see this [separate document](build_iOS.md))
 - Windows with MinGW
 - Windows with Microsoft Visual Studio
 - Android (see this [separate document](Android/Compiling.md))
 - Cygwin (only for testing)

The `configure` script wasn't tested on Windows (other than on Cygwin).


## The `configure` Script

This script is similar in design to the Autotools' `configure` script, and
so usually handles `--long-options`, possibly with values. It handles
two kinds of options:

* special options to be resolved inside the script and that may do some
advanced checks; this should later turn into a set of specific `cmake`
variable declarations

* options that are directly translated to `cmake` variables. 

The direct translate option always does a simple transformation:

* all letters uppercase
* dash into underscore
* plus into X
* when no value is supplied, it defaults to 1

For example, `--enable-c++11` turns into `-DENABLE_CXX11=1` when passed
to `cmake`.

Additionally, if you specify `--disable-<X>`, the `configure` script
will automatically turn it into an associated `--enable-<X>` option,
 passing `0` as its value. For example, `--disable-encryption` will
be translated for `cmake` into `-DENABLE_ENCRYPTION=0`.

### Build Options

All options below are presented using the `configure` convention. They can all
be used in `cmake` with the appropriate format changes.


**`--cygwin-use-posix`** (default:OFF)

When ON, compile on Cygwin using POSIX API (otherwise it will use MinGW environment).


**`--enable-apps`** (default: ON)

Enables compiling user applications.


**`--enable-code-coverage`** (default: OFF)

Enable instrumentation for code coverage. Note that this is only available
on platforms with GNU-compatible compiler.


**`--enable-c++-deps`** (default: OFF)

The `pkg-confg` file (`srt.pc`) will be generated with the `libstdc++` library 
as a dependency. This may be required in some cases where you have an application 
written in C which therefore won't link against `libstdc++` by default.

**`--enable-c++11`** (default: ON)

Enable compiling in C++11 mode for those parts that may require it.
Parts that don't require it will still be compiled in C++03 mode,
although which parts are affected may change in future.

If this option is turned OFF, it affects building a project in two ways:

* an alternative C++03 implementation can be used, if available
* otherwise the component that requires it will be disabled

Parts that currently require C++11 and have no alternative implementation
are:

* unit tests
* user and testing applications (such as `srt-live-transmit`)
* some of the example applications

It should be possible to compile the SRT library without C++11 support. However, 
this alternative C++03 implementation may be unsupported on certain platforms.


**`--enable-debug=<0,1,2>`**

This option allows control through the `CMAKE_BUILD_TYPE` variable:

* 0 (default): `Release` (highly optimized, no debug info)
* 1: `Debug` (not optimized, full debug info)
* 2: `RelWithDebInfo` (highly optimized, but with debug info)

Please note that when the value is other than 0, the
`--enable-heavy-logging` option is also turned ON by default.


**`--enable-encryption`** (default: ON)

Encryption feature enabled, which involves dependency on an external encryption
library (default: openssl). If you disable encryption, the library will be unable
to set encryption options. It will be compatible with a peer that has
encryption enabled, but just won't use encryption for the connection.


**`--enable-getnameinfo`** (default: OFF)

Enables the use of `getnameinfo` using options that allow using reverse DNS to
resolve an internal IP address into a readable internet domain name, so that it 
can be shown nicely in the log file. This option is turned OFF by default 
because it may have an impact on general performance. It is recommended only for
development when testing on a local network.


**`--enable-haicrypt-logging`** (default: OFF)

Enables logging in the *haicrypt* module, which serves as a connector to
an encryption library. Logging here might be seen as unsafe, therefore this 
option is turned OFF by default.


**`--enable-heavy-logging`** (default: OFF in release mode)

This option enables the logging instructions in the code, which are considered
heavy as they occur often and cover many detailed aspects of library behavior.
Turning this option ON will allow you to use the `debug` level of logging and get 
detailed information as to what happens inside the library. Note, however, that 
this may influence processing by changing times, using less preferred thread 
switching layouts, and generally worsen the functionality and performance of 
the library. For these reasons this option is turned OFF by default.


**`--enable-inet-pton`** (default: ON)

Enables usage of `inet_pton` function by the applications, which should be used
to resolve the network endpoint name into an IP address. This may be not
availabe on some version of Windows, in which case you can turn this OFF.
When this option is OFF, however, IP addresses cannot be resolved by name,
as the `inet_pton` function gets a poor-man's simple replacement that can
only resolve numeric IPv4 addresses.


**`--enable-logging`** (default: ON)

Enables logging. When you turn this option OFF, the library will not report
any runtime information through the logging system, including errors.
This option may be useful if you suspect the logging system of
impairing performance.


**`--enable-monotonic-clock`** (default: OFF)

This option enforces the use of `clock_gettime` to get the current
time, instead of `gettimeofday`. This function forces the use of a monotonic
clock that is independent of the currently set time in the system.
The condition variables (CV), for which the `*_timedwait()` functions are used
with time specification based on the time obtained from `clock_gettime`
must be appropriately configured. For now, this is only done for the
GarbageCollector controlling CV, not every CV used in SRT. The consequence
of enabling this option, however, may be portability issues resulting from
the fact that `clock_gettime` function may be unavailable in some SDKs or that an
extra `-lrt` option is sometimes required (this requirement will be autodetected).

The problem is based on the fact that POSIX functions that use timeout
specification (all of `*_timedwait`) expect the absolute time value.
A relative timeout value can be then only specified by adding it to
the current time, which can be specified as either system or monotonic
clock (as configured in the resources used in the operation).
However the current time of the monotonic clock can only be obtained by
the `clock_gettime` function.

**NOTE:** *This is a temporary fix for Issue #729* where the library could get 
stuck if the system clock is modified during an SRT transmission. This option 
will be removed when the problem is fixed globally.


**`--enable-pktinfo`** (default: OFF)

This option allows the extraction of a target IP address from incoming
UDP packets and the forceful setting of the source IP address in outgoing
UDP packets. This ensures that if a packet comes in from a peer that requests a
new connection, the agent will respond with a UDP packet that has the
same source IP address as the one to which the peer is trying to connect.

When this option is OFF (default), the source IP address in an outgoing 
UDP packet will be set automatically the following way:

* For a given destination IP address in the UDP packet to be sent, find
the routing table entry that matches this address, then get its network device
and configured network broadcast address

* Set the **first** local IP address that matches the broadcast
address found above as the source IP address for this UDP packet.

Example: Let's say you have the following local IP addresses:

* 192.168.10.11: broadcast 192.168.10.0
* 10.0.1.15: broadcast 10.0.1.0
* 10.0.1.20: broadcast 10.0.1.0

When a caller is contacting this first address (no matter where it came from), 
the response packet will be sent back to this address. The route path will use 
this first address as well, so the source IP address will be 192.168.10.11, same as
the one that was contacted.

If the caller handshake packet comes from the address that matches the 10.0.1.0 
broadcast (which is common to the second and third addresses), and the target 
address is 10.0.1.20, the response packet will be sent back to this address over 
the network assigned to the 10.0.1.0 broadcast. But the source address will then 
be 10.0.1.15 because it is the first local address assigned to the route path.

When this happens, the caller peer will see a mismatch between the source 
10.0.1.15 address and the address it tried to contact (i.e. 10.0.1.20). It will 
be treated as an attack attempt and rejected.

This problem can be slightly mitigated by binding the listening socket to a 
specific address. So, if you bind it to 10.0.1.20 in the above example, then 
wherever you try to send a packet over that socket it will always have the 
source address 10.0.1.20 (and the fix provided by this option will also not 
apply in this case). However, this problem still exists if the listener socket 
is bound to the "whole machine" (i.e. set to "any" address).

When this option is ON, a mechanism is added to forcefully set the source
IP address in such a response packet (e.g. to 10.0.1.20 in the above example).
This address is first extracted from the incoming packet as the
target address, which fixes the problem, as this will be interpreted by
the caller peer correctly.

This feature is turned off by default because the impact on performance is 
currently unknown. The problem is that it causes the CMSG information to be read 
from (and set on) every packet when a socket is bound to "any" address. The 
CMSG information will effectively be extracted from every incoming packet, as 
long as the socket, through which it is sent or received, is not bound to a
specific address, including data packets coming in within the frames of an
existing connection.


**`--enable-profile`** (default: OFF)

Enables code instrumentation for profiling.

This is available only for GNU-compatible compilers.


**`--enable-relative-libpath`** (default: OFF)

Enables adding a relative path to a library. This allows applications to be
linked against a shared SRT library by reaching out to a sibling `../lib`
directory, provided that the library and applications are installed in POSIX/GNU
style directories. This might be useful when installing SRT and applications
in a directory, in which the library subdirectory is not explicitly defined
among the global library paths. Consider, for example, this application and its 
required library:

* `/opt/srt/bin/srt-live-transmit`
* `/opt/srt/lib64/libsrt.so`

By using the `--enable-relative-libpath` option, the `srt-live-transmit` 
application has a relative library path defined inside as `../lib64`. A dynamic 
linker will find the required `libsrt.so` file by this path: `../lib64/libsrt.so`. 
This way the dynamic linkage will work even if `/opt/srt/lib64` path isn't added 
to the system paths in `/etc/ld.so.conf` or in the `LD_LIBRARY_PATH` environment 
variable.

This option is OFF by default because of reports that it may cause problems in
case of default installation.


**`--enable-shared`** and **`--enable-static`** (default for both: ON)

Enables building SRT as a shared and/or static library, as required for your 
application. In practice, you would only disable one or the other
(e.g. by `--disable-shared`). Note that you can't disable both at once.


**`--enable-testing`** (default: OFF)

Enables compiling of developer testing applications.


**`--enable-thread-check`** (default: OFF)

Enables `#include <threadcheck.h>`, which implements `THREAD_*` macros" to 
support better thread debugging. Included to support an existing project.


**`--enable-unittests`** (default: OFF)

When ON, this option enables unit tests, possibly with the download 
and installation of the Google test library in the build directory. The tests 
will be run as part of the build process. This is intended for developers only.


**`--openssl-crypto-library=<filepath>`**

Configure the path to an OpenSSL Crypto library.


**`--openssl-include-dir=<path>`**

Configure the path to include files for an OpenSSL library.


**`--openssl-ssl-library=<filepath>`**

Configure the path to an OpenSSL SSL library.


**`--pkg-config-executable=<filepath>`**

Configure the path to the `pkg-config` tool.


**`--prefix=<path>`**

This is an alias to the `--cmake-install-prefix` variable that establishes the
root directory for installation, inside of which a GNU/POSIX compatible
directory layout will be used. As on all known build systems, this defaults to
`/usr/local` on GNU/POSIX compatible systems, with lower level GNU/POSIX
directories created inside: `/usr/local/bin`,`/usr/local/lib`, etc.


**`--pthread-include-dir=<path>`**

Configure the path to include files for a pthread library. Note that this
is useful only on Windows. On Linux and macOS this path should be available in 
the system.


**`--pthread-library=<filepath>`**

Configure the path to a pthread library.


**`--use-busy-waiting`** (default: OFF)

Enable more accurate sending times at the cost of potentially higher CPU load.

This option will cause more empty loop running, which may cause more CPU usage. 
Keep in mind, however, that when processing high bitrate streams the share of
empty loop runs will decrease as the bitrate increases. This way higher CPU
usage would still be productive, while without system-supported waiting this
option may increase the likelihood of switching to the right thread at the time
when it is expected to be revived.


**`--use-gnustl`**

Use `pkg-config` with the `gnustl` package name to extract the header and 
library path for the C++ standard library (instead of using the compiler
built-in one).


**`--use-enclib=<name>`**

Encryption library to be used. Possible options for `<name>`:

* openssl (default)
* gnutls (with nettle)
* mbedtls


**`--use-openssl-pc`** (default: ON)

Use `pkg-config` to find OpenSSL libraries. You can turn this OFF to force 
`cmake` to find OpenSSL by its own preferred method.


**`--use-static-libstdc++`** (default: OFF)

Enforces linking the SRT library against the static libstdc++ library. This may
be useful if you are using SRT library in an environment where it would by
default link against the wrong version of the C++ standard library, or when the
library in the version used by the compiler is not available as shared.


**`--with-compiler-prefix=<prefix>`**

Sets C/C++ toolchains as `<prefix><c-compiler>` and `<prefix><c++-compiler>`.

This option will override the default compiler autodetected by `cmake`.
It is handled inside `cmake`. It sets the variables `CMAKE_C_COMPILER` and
`CMAKE_CXX_COMPILER`. The values for the above `<c-compiler>` and
`<c++-compiler>` are controlled by the `--with-compiler-type` option.
When this option is not supplied, a system-default compiler will be
used, that is:

* On Mac OS (Darwin): clang
* On other POSIX systems: gcc
* On other systems: obtained from `CMAKE_C_COMPILER` variable

Instead of `--with-compiler-prefix` you can use `--cmake-c-compiler`
and `--cmake-c++-compiler` options. This can be thought of as a
shortcut, useful when you have a long path to the compiler command.

NOTE: The prefix is meant to simply precede the compiler type as pure
prefix, so if your prefix is a full path to the compiler, it must include
the terminal path separator character, as this can be also used as a
prefix for a platform-specific cross compiler. For example, if the path
to the C compiler is: `/opt/arm-tc/bin/arm-linux-gnu-gcc-7.4`, then
you should specify `--with-compiler-prefix=/opt/arm-tc/bin/arm-linux-gnu-`
and `--with-compiler-type=gcc-7.4`.

**`--with-compiler-type=<name>`**

Sets the compiler type to be used as `<c-compiler>` and `<c++-compiler>`
respectively:

* gcc (default): gcc and g++
* cc: cc and c++
* others: use `<name>` as C compiler and `<name>++` as C++ compiler

This should be the exact command used as a C compiler, possibly with
version suffix, e.g. `clang-1.7.0`. If this option is used together
with `--with-compiler-prefix`, its prefix will be added in front.

