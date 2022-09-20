# Build System

The SRT build system uses [`CMake`](https://cmake.org/) 2.8.12 or above.

A wrapper script named [`configure`](https://github.com/Haivision/srt/blob/master/configure) 
is also available. The `configure` script can simplify the build process, such as 
by trying to automatically detect the OpenSSL path in a system. Note that you must 
have the Tcl interpreter installed to use this script.


Here is a link to a demo showing how CMake can be used to build SRT:
[Quickstart: Running SRT and FFmpeg on Ubuntu](https://www.youtube.com/watch?v=XOtUOVhussc&t=5s).


Additional information on building for Windows is available in the 
[Building SRT for Windows](https://github.com/Haivision/srt/blob/master/docs/build/build-win.md) 
document and in the [SRT CookBook](https://srtlab.github.io/srt-cookbook/getting-started/build-on-windows/).


## List of Build Options

The following table lists available build options in alphabetical order. 
Option details are given further below.


| Option Name                                                  | Since | Type      | Default    | Short Description                                                                                                                                    |
| :----------------------------------------------------------- | :---: | :-------: | :--------: | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| [`CMAKE_INSTALL_PREFIX`](#cmake_install_prefix)              | 1.3.0 | `STRING`  | OFF        | Standard CMake variable that establishes the root directory for installation, inside of which a GNU/POSIX compatible directory layout will be used.  |
| [`CYGWIN_USE_POSIX`](#cygwin_use_posix)                      | 1.2.0 | `BOOL`    | OFF        | Determines when to compile on Cygwin using POSIX API.                                                                                                |
| [`ENABLE_APPS`](#enable_apps)                                | 1.3.3 | `BOOL`    | ON         | Enables compiling sample applications (`srt-live-transmit`, etc.).                                                                                   |
| [`ENABLE_BONDING`](#enable_bonding)                          | 1.5.0 | `BOOL`    | OFF        | Enables the [Connection Bonding](../features/bonding-quick-start.md) feature.                                                                        |
| [`ENABLE_CXX_DEPS`](#enable_cxx_deps)                        | 1.3.2 | `BOOL`    | OFF        | The `pkg-confg` file (`srt.pc`) will be generated with the `libstdc++` library as a dependency.                                                      |
| [`ENABLE_CXX11`](#enable_cxx11)                              | 1.2.0 | `BOOL`    | ON         | Enable compiling in C++11 mode for those parts that may require it. Default: ON except for GCC<4.7                                                   |
| [`ENABLE_CODE_COVERAGE`](#enable_code_coverage)              | 1.4.0 | `BOOL`    | OFF        | Enables instrumentation for code coverage.                                                                                                           |
| [`ENABLE_DEBUG`](#enable_debug)                              | 1.2.0 | `INT`     | ON         | Allows release/debug control through the `CMAKE_BUILD_TYPE` variable.                                                                                |
| [`ENABLE_ENCRYPTION`](#enable_encryption)                    | 1.3.3 | `BOOL`    | ON         | Enables encryption feature enabled, with dependency on an external encryption library.                                                               |
| [`ENABLE_GETNAMEINFO`](#enable_getnameinfo)                  | 1.3.0 | `BOOL`    | OFF        | Enables the use of `getnameinfo` to allow using reverse DNS to resolve an internal IP address into a readable internet domain name.                  |
| [`ENABLE_HAICRYPT_LOGGING`](#enable_haicrypt_logging)        | 1.3.1 | `BOOL`    | OFF        | Enables logging in the *haicrypt* module, which serves as a connector to an encryption library.                                                      |
| [`ENABLE_HEAVY_LOGGING`](#enable_heavy_logging)              | 1.3.0 | `BOOL`    | OFF        | Enables heavy logging instructions in the code that occur often and cover many detailed aspects of library behavior. Default: OFF in release mode.   |
| [`ENABLE_INET_PTON`](#enable_inet_pton)                      | 1.3.2 | `BOOL`    | ON         | Enables usage of the `inet_pton` function used to resolve the network endpoint name into an IP address.                                              |
| [`ENABLE_LOGGING`](#enable_logging)                          | 1.2.0 | `BOOL`    | ON         | Enables normal logging, including errors.                                                                                                            |
| [`ENABLE_MONOTONIC_CLOCK`](#enable_monotonic_clock)          | 1.4.0 | `BOOL`    | ON*        | Enforces the use of `clock_gettime` with a monotonic clock that is independent of the currently set time in the system.                              |
| [`ENABLE_NEW_RCVBUFFER`](#enable_new_rcvbuffer)              | 1.5.0 | `BOOL`    | ON         | Enables the new implementation of the receiver buffer with behavior and code improvements.                                                           |
| [`ENABLE_PROFILE`](#enable_profile)                          | 1.2.0 | `BOOL`    | OFF        | Enables code instrumentation for profiling (only for GNU-compatible compilers).                                                                      |
| [`ENABLE_RELATIVE_LIBPATH`](#enable_relative_libpath)        | 1.3.2 | `BOOL`    | OFF        | Enables adding a relative path to a library for linking against a shared SRT library by reaching out to a sibling directory.                         |
| [`ENABLE_SHARED`](#enable_shared--enable_static)             | 1.2.0 | `BOOL`    | ON         | Enables building SRT as a shared library.                                                                                                            |
| [`ENABLE_SHOW_PROJECT_CONFIG`](#enable_show_project_config)  | 1.5.0 | `BOOL`    | OFF        | When ON, the project configuration is displayed at the end of the CMake Configuration Step.                                                          |
| [`ENABLE_STATIC`](#enable_shared--enable_static)             | 1.3.0 | `BOOL`    | ON         | Enables building SRT as a static library.                                                                                                            |
| [`ENABLE_STDCXX_SYNC`](#enable_stdcxx_sync)                  | 1.4.2 | `BOOL`    | ON*        | Enables the standard C++11 `thread` and `chrono` libraries to be used by SRT instead of the `pthreads`.                                              |
| [`ENABLE_TESTING`](#enable_testing)                          | 1.3.0 | `BOOL`    | OFF        | Enables compiling of developer testing applications (`srt-test-live`, etc.).                                                                         |
| [`ENABLE_THREAD_CHECK`](#enable_thread_check)                | 1.3.0 | `BOOL`    | OFF        | Enables `#include <threadcheck.h>`, which implements `THREAD_*` macros" to  support better thread debugging.                                         |
| [`ENABLE_UNITTESTS`](#enable_unittests)                      | 1.3.2 | `BOOL`    | OFF        | Enables building unit tests.                                                                                                                         |
| [`OPENSSL_CRYPTO_LIBRARY`](#openssl_crypto_library)          | 1.3.0 | `STRING`  | OFF        | Configures the path to an OpenSSL crypto library.                                                                                                    |
| [`OPENSSL_INCLUDE_DIR`](#openssl_include_dir)                | 1.3.0 | `STRING`  | OFF        | Configures the path to include files for an OpenSSL library.                                                                                         |
| [`OPENSSL_SSL_LIBRARY`](#openssl_ssl_library)                | 1.3.0 | `STRING`  | OFF        | Configures the path to an OpenSSL SSL library.                                                                                                       |
| [`PKG_CONFIG_EXECUTABLE`](#pkg_config_executable)            | 1.3.0 | `BOOL`    | OFF        | Configures the path to the `pkg-config` tool.                                                                                                        |
| [`PTHREAD_INCLUDE_DIR`](#pthread_include_dir)                | 1.3.0 | `STRING`  | OFF        | Configures the path to include files for a `pthread` library.                                                                                        |
| [`PTHREAD_LIBRARY`](#pthread_library)                        | 1.3.0 | `STRING`  | OFF        | Configures the path to a `pthread` library.                                                                                                          |
| [`USE_BUSY_WAITING`](#use_busy_waiting)                      | 1.3.3 | `BOOL`    | OFF        | Enables more accurate sending times at the cost of potentially higher CPU load.                                                                      |
| [`USE_CXX_STD`](#use_cxx_std)                                | 1.4.2 | `STRING`  | OFF        | Enforces using a particular C++ standard (11, 14, 17, etc.) when compiling.                                                                          |
| [`USE_ENCLIB`](#use_enclib)                                  | 1.3.3 | `STRING`  | openssl    | Encryption library to be used (`openssl`, `openssl-evp` (since 1.5.1), `gnutls`, `mbedtls`).                                                         |
| [`USE_GNUSTL`](#use_gnustl)                                  | 1.3.4 | `BOOL`    | OFF        | Use `pkg-config` with the `gnustl` package name to extract the header and library path for the C++ standard library.                                 |
| [`USE_OPENSSL_PC`](#use_openssl_pc)                          | 1.3.0 | `BOOL`    | ON         | Use `pkg-config` to find OpenSSL libraries.                                                                                                          |
| [`OPENSSL_USE_STATIC_LIBS`](#openssl_use_static_libs)        | 1.5.0 | `BOOL`    | OFF        | Link OpenSSL statically.                                                                                                                             |
| [`USE_STATIC_LIBSTDCXX`](#use_static_libstdcxx)              | 1.2.0 | `BOOL`    | OFF        | Enforces linking the SRT library against the static `libstdc++` library.                                                                             |
| [`WITH_COMPILER_PREFIX`](#with_compiler_prefix)              | 1.3.0 | `STRING`  | OFF        | Sets C/C++ toolchains as `<prefix><c-compiler>` and `<prefix><c++-compiler>`, overriding the default compiler.                                       |
| [`WITH_COMPILER_TYPE`](#with_compiler_type)                  | 1.3.0 | `STRING`  | OFF        | Sets the compiler type to be used (values: gcc, cc, clang, etc.).                                                                                    |
| [`WITH_EXTRALIBS`](#with_extralibs)                          | 1.3.0 | `STRING`  | OFF        | Option required for unusual situations when a platform-specific workaround is needed and some extra libraries must be passed explicitly for linkage. |
| [`WITH_SRT_NAME`](#with_srt_name)                            | 1.3.0 | `STRING`  | OFF        | Configure the SRT library name adding a custom `<prefix>`.                                                                                           |
| <img width=425px height=1px/>                                |       |           |            |                                                                                                                                                      |

 
\* See the option description for more details.

## Using CMake

If you choose to use CMake directly for the build configuration stage, you must 
specify option values in the CMake format: 

`-D<OPTION>=<VALUE>` 

For more information please refer to the [official CMake documentation](https://cmake.org/documentation/).

The following example shows how to disable the inclusion of sample SRT applications 
(such as `srt-live-transmit`) in the build configuration, where `./` specifies 
the relative path to the main `CMakeLists.txt` file located in the root folder 
of the SRT project.

In this example CMake is run from the root SRT directory:

```cmake
cmake ./ -DENABLE_APPS=OFF
```


SRT build options known to CMake are listed in the 
[CMakeLists.txt](https://github.com/Haivision/srt/blob/master/CMakeLists.txt) file as: 

`option(<name> <description> <default value>)`. 

For example:

`option(CYGWIN_USE_POSIX "Should the POSIX API be used for cygwin. Ignored if the system isn't cygwin." OFF)
`
With CMake you would specify this option as:

`cmake -DCYGWIN_USE_POSIX=ON` or `cmake -DCYGWIN_USE_POSIX=OFF`

where “-D” is the CMake command to set a build variable to a certain value.



## Using the Configure Script

This script is similar in design to the 
[Autotools](https://www.gnu.org/software/automake/manual/html_node/Autotools-Introduction.html) 
`configure` script, and so accepts `--long-options`, with or without values. It 
handles two kinds of options:

* options that are directly translated to `cmake` variables. 

* special options to be resolved inside the script that may do some
advanced checks; these should later be converted into a set of specific `cmake`
variable declarations

The directly translated options always undergo a simple transformation:

* all letters are converted to uppercase
* dashes are converted to underscores
* plus (+) symbols are converted to X
* when no value is supplied, a default value of 1 is applied

To set the `CYGWIN_USE_POSIX` option using the configure script you would call 

`configure --cygwin-use-posix`

which is transformed by the script into `-DCYGWIN-USE-POSIX` and then passed
to `cmake` to enable POSIX (set to ON). To disable the option (set to OFF) using 
the configure script you would call 

`configure –-disable-cygwin-use-posix`

In another example, to enable compiling in C++11 mode with the CMake command 
`ENABLE-C++11` using the configure script you would call 

`configure --enable-c++11` 

which is transformed by the script into `-DENABLE_CXX11=1` and then passed
to `cmake`.

Additionally, if you specify `--disable-<X>`, the `configure` script
will automatically turn it into an associated `--enable-<X>` option,
 passing `0` as its value. For example, `--disable-encryption` will
be translated for `cmake` into `-DENABLE_ENCRYPTION=0`.


## Build Options

The CMake options available for building SRT are listed below, along with the 
equivalent `configure` format.


#### CMAKE_INSTALL_PREFIX
**`--cmake-install-prefix=<path>`**

Used to configure an alias to the `--cmake-install-prefix` variable that 
establishes the root directory for installation, inside of which a GNU/POSIX 
compatible directory layout will be used. As on all known build systems, this 
defaults to `/usr/local` on GNU/POSIX compatible systems, with lower level 
GNU/POSIX directories created inside: `/usr/local/bin`,`/usr/local/lib`, etc.


#### CYGWIN_USE_POSIX
**`--cygwin-use-posix`** (default:OFF)

Set to ON to compile SRT on Cygwin using the POSIX API (otherwise it will use 
MinGW environment).


#### ENABLE_APPS
**`--enable-apps`** (default: ON)

Enables compiling user applications.


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### ENABLE_BONDING
**`--enable-bonding`** (default: OFF)

Enables the [Connection Bonding](../features/bonding-quick-start.md) feature.

Similar to SMPTE-2022-7 over managed networks, Connection Bonding adds seamless stream protection and hitless failover to the SRT protocol. This technology relies on more than one IP network path to prevent disruption to live video streams in the event of network congestion or outages, maintaining continuity of service.

This is accomplished using the [socket groups](../features/socket-groups.md) introduced in [SRT v1.5](https://github.com/Haivision/srt/releases/tag/v1.5.0). The general concept of socket groups means having a group that contains multiple sockets, where one operation for sending one data signal is applied to the group. Single sockets inside the group will take over this operation and do what is necessary to deliver the signal to the receiver.

Two modes are supported:

- [Broadcast](../features/socket-groups.md#1-broadcast) - In *Broadcast* mode, data is sent redundantly over all the member links in a group. If one of the links fails or experiences network jitter and/or packet loss, the missing data will be received over another link in the group. Redundant packets are simply discarded at the receiver side.

- [Main/Backup](../features/bonding-main-backup.md) - In *Main/Backup* mode, only one (main) link at a time is used for data transmission while other (backup) connections are on standby to ensure the transmission will continue if the main link fails. The goal of Main/Backup mode is to identify a potential link break before it happens, thus providing a time window within which to seamlessly switch to one of the backup links.

With the Connection Bonding feature disabled, [bonding API functions](../API/API-functions.md#socket-group-management) are present, but return an error.


#### ENABLE_CXX_DEPS
**`--enable-c++-deps`** (default: OFF)

When ON, the `pkg-confg` file (`srt.pc`) will be generated with the `libstdc++` 
library as a dependency. This may be required in some cases where you have an 
application written in C which therefore won't link against `libstdc++` by default.


#### ENABLE_CXX11
**`--enable-c++11`** (default: ON except for GCC<4.7)

When ON, enables compiling in C++11 mode for those components that may require it.
Components that don't require it will still be compiled in C++03 mode,
although which components are affected may change in future.

If this option is turned OFF, it affects building a project in two ways:

* an alternative C++03 implementation can be used, if available
* otherwise the component that requires it will be disabled

Components that currently require C++11 and have no alternative implementation
are:

* unit tests
* user and testing applications (such as `srt-live-transmit`)
* some of the example applications

It should be possible to compile the SRT library without C++11 support. However, 
any alternative C++03 implementation may be unsupported on certain platforms.


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### ENABLE_CODE_COVERAGE
**`--enable-code-coverage`** (default: OFF)

When ON, enables instrumentation for code coverage. Note that this is only available
on platforms with a GNU-compatible compiler.


#### ENABLE_DEBUG
**`--enable-debug=<0,1,2>`**

This option allows control through the `CMAKE_BUILD_TYPE` variable:

* 0 (default): `Release` (highly optimized, no debug info)
* 1: `Debug` (not optimized, full debug info)
* 2: `RelWithDebInfo` (highly optimized, but with debug info)

Please note that when the value is other than 0, the
[`--enable-heavy-logging`](#enable-heavy-logging) option is also turned ON by default.


#### ENABLE_ENCRYPTION
**`--enable-encryption`** (default: ON)

When ON, the encryption feature is enabled. This involves a dependency on an 
external encryption library (default: [openssl](https://github.com/openssl/openssl)). 
If you disable encryption, the library will be unable to set encryption options. 
It will be compatible with a peer that has encryption enabled, but just won't 
use encryption for the connection.


#### ENABLE_GETNAMEINFO
**`--enable-getnameinfo`** (default: OFF)

When ON, enables the use of `getnameinfo` with options that allow using reverse 
DNS to resolve an internal IP address into a readable internet domain name, so 
that it can be shown nicely in the log file. This option is turned OFF by default 
because it may have an impact on general performance. It is recommended only for
development when testing on a local network.


#### ENABLE_HAICRYPT_LOGGING
**`--enable-haicrypt-logging`** (default: OFF)

When ON, enables logging in the *haicrypt* module, which serves as a connector to
an encryption library. Logging here might be seen as unsafe, therefore this 
option is turned OFF by default.


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### ENABLE_HEAVY_LOGGING
**`--enable-heavy-logging`** (default: OFF in release mode)

When ON, this option enables logging instructions in the code, which are considered
heavy as they occur often and cover many detailed aspects of library behavior.
Turning this option ON will allow you to use the `debug` level of logging and get 
detailed information as to what happens inside the library. Note, however, that 
this may influence processing by changing timings, use less preferred thread 
switching layouts, and generally worsen the functionality and performance of 
the library. For these reasons this option is turned OFF by default.


#### ENABLE_INET_PTON
**`--enable-inet-pton`** (default: ON)

When ON, enables usage of the `inet_pton` function by applications, which should 
be used to resolve the network endpoint name into an IP address. This may not be 
available in some versions of Microsoft Windows, in which case you can change the 
setting to OFF. When this option is OFF, however, IP addresses cannot be resolved 
by name, as the `inet_pton` function gets a poor-man's simple replacement that can
only resolve numeric IPv4 addresses.


#### ENABLE_LOGGING
**`--enable-logging`** (default: ON)

When ON, enables logging. When you turn this option OFF, the library will not 
report any runtime information, including errors, through the logging system. This 
option may be useful if you suspect the logging system of impairing performance.


#### ENABLE_MONOTONIC_CLOCK
**`--enable-monotonic-clock`** (default: OFF)

**NOTE**: The library can get stuck if the system clock is used instead of 
monotonic or C++11 steady. Since v1.4.4 `ENABLE_MONOTONIC_CLOCK` is enabled by 
default on POSIX-type systems if support for CLOCK_MONOTONIC is detected. 
On Windows `ENABLE_STDCXX_SYNC` is enabled by default. It is highly recommended 
to use either of those (`ENABLE_STDCXX_SYNC` excludes `ENABLE_MONOTONIC_CLOCK`).

When ON, this option enforces the use of `clock_gettime` to get the current
time, instead of `gettimeofday`. This function forces the use of a monotonic
clock that is independent of the currently set time in the system.
The condition variables (CV), for which the `*_timedwait()` functions are used
with time specification based on the time obtained from `clock_gettime`,
must be appropriately configured. For now, this is only done for the
garbage collector controlling CV, not every CV used in SRT. The consequence
of enabling this option, however, may be portability issues resulting from
the fact that the `clock_gettime` function may be unavailable in some SDKs, or 
that an extra `-lrt` option is sometimes required (this requirement will be 
autodetected).

The problem is based on the fact that POSIX functions that use timeout
specification (all of `*_timedwait`) expect the absolute time value.
A relative timeout value can be then only specified by adding it to
the current time, which can be specified using either the system or monotonic
clock (as configured in the resources used in the operation).
However the current time of the monotonic clock can only be obtained by
the `clock_gettime` function.




[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)



#### ENABLE_NEW_RCVBUFFER
**`--enable-new-rcvbuffer`** (default: ON)

When ON, this option enables the newest implementation of the receiver buffer 
with behavior and code improvements. Note that while it is still possible to fall 
back to the old receiver buffer implementation, eventually the new implementation 
will be the only one available.


#### ENABLE_PROFILE
**`--enable-profile`** (default: OFF)

When ON, enables code instrumentation for profiling (only available for 
GNU-compatible compilers).


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### ENABLE_RELATIVE_LIBPATH
**`--enable-relative-libpath`** (default: OFF)

When ON, enables adding a relative path to a library. This allows applications to 
be linked against a shared SRT library by reaching out to a sibling `../lib`
directory, provided that the library and applications are installed in POSIX/GNU
style directories. This might be useful when installing SRT and applications
in a directory in which the library subdirectory is not explicitly defined
among the global library paths. Consider, for example, this application and its 
required library:

* `/opt/srt/bin/srt-live-transmit`
* `/opt/srt/lib64/libsrt.so`

By using the `--enable-relative-libpath` option, the `srt-live-transmit` 
application has a relative library path defined inside as `../lib64`. A dynamic 
linker will find the required `libsrt.so` file by this path: `../lib64/libsrt.so`. 
This way the dynamic linkage will work even if the `/opt/srt/lib64` path isn't added 
to the system paths in `/etc/ld.so.conf` or in the `LD_LIBRARY_PATH` environment 
variable.

This option is OFF by default because of reports that it may cause problems with 
default installations.


#### ENABLE_SHARED | ENABLE_STATIC
**`--enable-shared`** and **`--enable-static`** (default for both: ON)

When ON, enables building SRT as a shared and/or static library, as required for 
your application. In practice, you would only disable one or the other
(e.g. by `--disable-shared`). Note that you can't disable both at once.


#### ENABLE_SHOW_PROJECT_CONFIG
**`--enable-show-project-config`** (default:OFF)

When ON, the project configuration is displayed at the end of the CMake 
configuration step of the build process.


#### ENABLE_STDCXX_SYNC
**`--enable-stdcxx-sync`** (default: OFF)

**NOTE**: The library can get stuck if the system clock is used instead of 
monotonic or C++11 steady. Since v1.4.4 `ENABLE_STDCXX_SYNC`is enabled by 
default on Windows. On POSIX-type systems, an alternative `ENABLE_MONOTONIC_CLOCK` option is enabled by default if support for CLOCK_MONOTONIC is detected. It is highly recommended 
to use either of those (`ENABLE_STDCXX_SYNC` excludes `ENABLE_MONOTONIC_CLOCK`).

When ON, this option enables the standard C++ `thread` and `chrono` libraries 
(available since C++11) to be used by SRT instead of the `pthreads` libraries.


#### ENABLE_TESTING
**`--enable-testing`** (default: OFF)

When ON, enables compiling of developer testing applications.


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)



#### ENABLE_THREAD_CHECK
**`--enable-thread-check`** (default: OFF)

When ON, enables `#include <threadcheck.h>`, which implements `THREAD_*` macros 
to support better thread debugging. Included to support an existing project.


#### ENABLE_UNITTESTS
**`--enable-unittests`** (default: OFF)

When ON, this option enables unit tests, possibly with the download 
and installation of the Google test library in the build directory. The tests 
will be run as part of the build process. This is intended for developers only.


#### OPENSSL_CRYPTO_LIBRARY
**`--openssl-crypto-library=<filepath>`**

Used to configure the path to an OpenSSL crypto library. Ignored when encryption 
is disabled (ENABLE_ENCRYPTION = OFF). See [`USE_ENCLIB`](#use_enclib) for the list of supported libraries.

[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### OPENSSL_INCLUDE_DIR
**`--openssl-include-dir=<path>`**

Used to configure the path to include files for an OpenSSL library.


#### OPENSSL_SSL_LIBRARY
**`--openssl-ssl-library=<filepath>`**

Used to configure the path to an OpenSSL SSL library.


#### PKG_CONFIG_EXECUTABLE
**`--pkg-config-executable=<filepath>`**

Used to configure the path to the `pkg-config` tool.

[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### PTHREAD_INCLUDE_DIR
**`--pthread-include-dir=<path>`**

Used to configure the path to include files for a `pthread` library. Note that 
this is useful only on Windows. On Linux and macOS this path should be available 
in the system.


#### PTHREAD_LIBRARY
**`--pthread-library=<filepath>`**

Used to configure the path to a `pthread` library.


#### USE_BUSY_WAITING
**`--use-busy-waiting`** (default: OFF)

When ON, enables more accurate sending times at the cost of potentially higher 
CPU load.

This option will cause more empty loop running, which may cause more CPU usage. 
Keep in mind, however, that when processing high bitrate streams the share of
empty loop runs will decrease as the bitrate increases. This way higher CPU
usage would still be productive, while without system-supported waiting this
option may increase the likelihood of switching to the right thread at the time
when it is expected to be revived.


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### USE_CXX_STD
**`--use-c++-std=<standard>`**

Enforces using a particular C++ standard when compiling. When using this option
remember that:

* Allowed values are: 98, 03, 11, 14, 17 and 20
* If you use 98/03 and `--enable-apps`, apps will be still using C++11
* This option is only supported on GNU and Clang compilers (will be ignored on others).


#### USE_ENCLIB
**`--use-enclib=<name>`**

Encryption library to be used. Possible options for `<name>`:

* openssl (default)
* openssl-evp (OpenSSL EVP API, since 1.5.1)
* gnutls (with nettle)
* mbedtls


#### USE_GNUSTL
**`--use-gnustl`**

Use `pkg-config` with the `gnustl` package name to extract the header and 
library path for the C++ standard library (instead of using the compiler
built-in one).


#### USE_OPENSSL_PC
**`--use-openssl-pc`** (default: ON)

When ON, uses `pkg-config` to find OpenSSL libraries. You can turn this OFF to 
force `cmake` to find OpenSSL by its own preferred method.

### OPENSSL_USE_STATIC_LIBS
**`--openssl-use-static-libs`** (default: OFF)

When ON, OpenSSL libraries are linked statically.
When `pkg-config`(`-DUSE_OPENSSL_PC=ON`) is used, static OpenSSL libraries are listed in `SSL_STATIC_LIBRARIES`. See `<prefix>_STATIC` in [CMake's FindPkgConfig](https://cmake.org/cmake/help/latest/module/FindPkgConfig.html).
On Windows additionally links `crypt32.lib`.

#### USE_STATIC_LIBSTDCXX
**`--use-static-libstdc++`** (default: OFF)

When ON, enforces linking the SRT library against the static `libstdc++` library. 
This may be useful if you are using SRT library in an environment where it would 
by default link against the wrong version of the C++ standard library, or when 
the library in the version used by the compiler is not available as shared.


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### WITH_COMPILER_PREFIX
**`--with-compiler-prefix=<prefix>`**

Sets C/C++ toolchains as `<prefix><c-compiler>` and `<prefix><c++-compiler>`.

This option will override the default compiler autodetected by `cmake`.
It is handled inside `cmake`. It sets the variables `CMAKE_C_COMPILER` and
`CMAKE_CXX_COMPILER`. The values for the above `<c-compiler>` and `<c++-compiler>` 
are controlled by the [`--with-compiler-type`](#with-compiler-type) option.
When this option is not supplied, one of the following system-default compilers 
will be used:

* On Mac OS (Darwin): clang
* On other POSIX systems: gcc
* On other systems: compiler obtained from the `CMAKE_C_COMPILER` variable

Instead of `--with-compiler-prefix` you can use [`--cmake-c-compiler`](#cmake-c-compiler)
and [`--cmake-c++-compiler`](#cmake-c++-compiler) options. This can be thought of 
as a shortcut, useful when you have a long path to the compiler command.

NOTE: The specified prefix is meant to simply precede the compiler type. If your 
prefix is a full path to the compiler, it must include the terminal path 
separator character, as this can also be used as a prefix for a platform-specific 
cross compiler. For example, if the path to the C compiler is: 

`/opt/arm-tc/bin/arm-linux-gnu-gcc-7.4`, 

then you should specify: 

`--with-compiler-prefix=/opt/arm-tc/bin/arm-linux-gnu-`
and `--with-compiler-type=gcc-7.4`.


#### WITH_COMPILER_TYPE
**`--with-compiler-type=<name>`**

Sets the compiler type to be used as `<c-compiler>` and `<c++-compiler>`
respectively:

* gcc (default): gcc and g++
* cc: cc and c++
* others: use `<name>` as C compiler and `<name>++` as C++ compiler

This should be the exact command used when specifying a C compiler, possibly 
with version suffix, e.g. `clang-1.7.0`. If this option is used together
with `--with-compiler-prefix`, its prefix will be added in front.


[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)


#### WITH_EXTRALIBS
**`--with-extralibs=<library-list>`**

This is an option for unusual situations where a platform-specific
workaround is needed, and some extra libraries must be passed explicitly
for linkage. The argument is a space-separated list of linker options
or library names.

There are some known situations where it may be necessary:

1. Some older Linux systems do not ship `clock_gettime` functions by
default in their `libc`, and need an extra `librt`. If you are using POSIX
monotonic clocks (see [`--enable-monotonic-clock`](#enable-monotonic-clock)), it 
might be required to add `-lrd` through this option. Although this situation is 
usually autodetected (and the option added automatically), it does sometimes fail.

2. On some systems (e.g. OpenSuSE), if you use C++11 sync
(see [`--enable-stdc++-sync`](#enable-stdc++-sync)), the gcc compiler relies on 
`gthreads`, which relies on `pthreads`, and happens to define inline source 
functions in the header that refer to `pthread_create`. The compiler, however, 
doesn't link against `pthreads` by default. To work around this, add `-pthreads`
using this option.


#### WITH_SRT_NAME
**`--with-srt-name=<prefix>`**

Overrides the SRT library name, adding a custom `<prefix>`



[:arrow_up: &nbsp; Back to List of Build Options](#list-of-build-options)
