<p align="center">
  <a href="http://srtalliance.org/">
    <img alt="SRT" src="http://www.srtalliance.org/wp-content/uploads/SRT_text_hor_logo_grey.png" width="600"/>
  </a>
</p>

[![Build Status Linux and macOS][travis-badge]][travis]
[![Build Status Windows][appveyor-badge]][appveyor]
[![License: MPLv2.0][license-badge]](./LICENSE)
[![Latest release][release-badge]][github releases]
[![Debian Badge][debian-badge]][debian-package]

# Introduction

Secure Reliable Transport (SRT) is an open source transport technology that optimizes streaming performance across unpredictable networks, such as the Internet.

|    |    |
| --- | --- |
| **S**ecure | Encrypts video streams |
| **R**eliable | Recovers from severe packet loss |
| **T**ransport | Dynamically adapts to changing network conditions |

SRT is applied to contribution and distribution endpoints as part of a video stream workflow to deliver the best quality and lowest latency video at all times.

As audio/video packets are streamed from a source to a destination device, SRT detects and adapts to the real-time network conditions between the two endpoints. SRT helps compensate for jitter and bandwidth fluctuations due to congestion over noisy networks, such as the Internet. Its error recovery mechanism minimizes the packet loss typical of Internet connections. And SRT supports AES encryption for end-to-end security, keeping your streams safe from prying eyes.

[Join the conversation](https://slackin-srtalliance.azurewebsites.net/) in the `#development` channel on [Slack](https://srtalliance.slack.com).

# Guides
* [Why SRT Was Created](docs/why-srt-was-created.md)
* [SRT Protocol Technical Overview](https://github.com/Haivision/srt/files/2489142/SRT_Protocol_TechnicalOverview_DRAFT_2018-10-17.pdf)
* [Using the `srt-live-transmit` App](docs/srt-live-transmit.md)
* [SRT Encryption](docs/encryption.md)
* [API](docs/API.md)
* [Reporting problems](docs/reporting.md)

# Requirements

* cmake (as build system)
* Tcl 8.5 (optional for user-friendly build system)
* OpenSSL
* Pthreads (for POSIX systems it's builtin, for Windows there's a library)

For detailed description of the build system and options, please read [BuildOptions.md](docs/BuildOptions.md).

## For Linux:

Install cmake and openssl-devel (or similar name) package. For pthreads
there should be -lpthreads linker flag added.

Default installation path prefix of `make install` is `/usr/local`.

To define a different installation path prefix, use the `--prefix` option with `configure`
or [`-DCMAKE_INSTALL_PREFIX`](https://cmake.org/cmake/help/v3.0/variable/CMAKE_INSTALL_PREFIX.html) CMake option.

To uninstall, call `make -n install` to list all the dependencies, and then pass the list to `rm`.

### Ubuntu 14
```
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install tclsh pkg-config cmake libssl-dev build-essential
./configure
make
```
### CentOS 7
```
sudo yum update
sudo yum install tcl pkgconfig openssl-devel cmake gcc gcc-c++ make automake
./configure
make
```
### CentOS 6
```
sudo yum update
sudo yum install tcl pkgconfig openssl-devel cmake gcc gcc-c++ make automake
sudo yum install centos-release-scl-rh devtoolset-3-gcc devtoolset-3-gcc-c++
scl enable devtoolset-3 bash
./configure --use-static-libstdc++ --with-compiler-prefix=/opt/rh/devtoolset-3/root/usr/bin/
make
```


## For Mac (Darwin, iOS):

[Homebrew](https://brew.sh/) supports "srt" formula.

```
brew update
brew install srt
```

If you prefer using a head commit of `master` branch, you should add `--HEAD` option
to `brew` command.

```
brew install --HEAD srt
```

Also, SRT can be built with `cmake` and `make` on Mac.
Install cmake and openssl with development files from "brew". Note that the
system version of OpenSSL is inappropriate, although you should be able to
use any newer version compiled from sources, if you prefer.
```
brew install cmake
brew install openssl
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export OPENSSL_LIB_DIR=$(brew --prefix openssl)"/lib"
export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)"/include"
./configure
make
```

## For Windows:

**1. Prepare one of the following Windows crypto libraries:**

   (a) OpenSSL  
   (b) LibreSSL  
   (c) MbedTLS

   *(a) Using the **OpenSSL** binaries:*

   Download and install OpenSSL for Windows. The 64-bit developer package can be 
   downloaded from here:

    http://slproweb.com/download/Win64OpenSSL-1_0_2r.exe
	 
   Note that the last letter or version number may be changed, and older versions 
   may no longer be available. If you can't find this version, check here:

    http://slproweb.com/products/Win32OpenSSL.html

   It's expected to be installed in `C:\OpenSSL-Win64` (see the above variables). 
   Note that this version is most likely compiled for Visual Studio 2013. For 
   other versions, download and compile the sources from: 
   
    https://github.com/openssl/openssl

   The instructions for compiling on Windows can be found here:

    https://wiki.openssl.org/index.php/Compilation_and_Installation#Windows

   Note that ActivePerl and nasm are required.

*(b) Using the **LibreSSL** binaries:*

Since LibreSSL has header files that are compatible with OpenSSL, `cmake` can use 
it like OpenSSL with little configuration.

The source code and binaries can be downloaded from here:

    https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/

Since there have been no new Windows builds since 2.6.0, you must build a new
version yourself. LibreSSL comes with `cmake` build system support. Use the 
`CMAKE_INSTALL_PREFIX` variable to specify the directory that will contain the
LibreSSL headers and libraries.

*(c) Using the **MbedTLS** libraries:*

MbedTLS source code can be downloaded from here:

    https://tls.mbed.org/download

MbedTLS comes with `cmake` build system support. Use the `CMAKE_INSTALL_PREFIX`
variable to specify the directory that will contain the MbedTLS headers and libraries.
Note that building MbedTLS as a DLL is broken in version 2.16.3. You have to link it
statically.

**2. Compile and install Pthreads for Windows:**

Compile and install `pthread-win32` for Windows from GitHub: [link](https://github.com/GerHobbelt/pthread-win32).

  1. Using Visual Studio 2013, open the project file `pthread_lib.2013.vcxproj`
  2. Select configuration: `Release` and `x64`.
  3. Make sure that the `pthread_lib` project will be built.
  4. After building, find the `pthread_lib.lib` file (directory is usually `bin\x64_MSVC2013.Release`).
Copy this file to `C:\pthread-win32\lib` (or whatever other location you configured in variables).
  5. Copy include files to `C:\pthread-win32\include` (`pthread.h`, `sched.h`, and `semaphore.h` 
  are in the toplevel directory. There are no meaningful subdirs here). Note that `win##` is part of 
  the project name. It will become `win32` or `win64` depending on the selection.

**3. Install `cmake` for Windows.**

The `cmake` GUI will help you configure the project.
 
If you use MbedTLS, change the `USE_ENCLIB` to `mbedtls`.

It will try to find crypto library and pthreads. If you installed them in the 
default location, they will be found automatically. If not, you can define the 
following variables to help `cmake` find them: 

   For All:
```
CMAKE_PREFIX_PATH=<path to depended libraries root>
```
Note that ```CMAKE_PREFIX_PATH``` may be not shown in the `cmake` GUI. You can use 
`Add Entry` button to add the variable manually. Type is `PATH`.
The directory structure should be similar to the following:
```
${CMAKE_PREFIX_PATH}/include/pthread.h
${CMAKE_PREFIX_PATH}/include/mbedtls/... (if mbedtls is used)
${CMAKE_PREFIX_PATH}/include/openssl/... (if openssl or libressl is not in default location)
${CMAKE_PREFIX_PATH}/lib/pthreadVC2.lib
${CMAKE_PREFIX_PATH}/lib/crypto.lib (if openssl or libressl is not in default location)
${CMAKE_PREFIX_PATH}/lib/mbedcrypto.lib (if mbedtls is used)
```
It's better to add the entry before clicking `Configure`, or the installation in
system will be used instead of the one in `${CMAKE_PREFIX_PATH}`.

For OpenSSL or LibreSSL:
```
OPENSSL_ROOT_DIR=<path to OpenSSL installation>
OPENSSL_LIBRARIES=<path to all the openssl libraries to link>
OPENSSL_INCLUDE_DIR=<path to the OpenSSL include dir>
```
For MbedTLS:
```
MBEDTLS_PREFIX=<path to mbedtls installation, default is the same to CMAKE_PREFIX_PATH>
```
For pthread:
```
PTHREAD_INCLUDE_DIR=<path to where pthread.h lies>
PTHREAD_LIBRARY=<path to pthread.lib>
```
Note that if you use the `cmake` command line to have it configured, please 
use `/` instead of `\` in the path, or error messages may result.
 

4. For the sake of cmake generation: When you want to have a 64-bit version,
remember that cmake by some reason adds /machine:X86 to the linker options.
There are about four variables ended with `_LINKER_FLAGS` in the `CMakeCache.txt`
file (also available with Advanced checked in CMake GUI). Remove them, or change
into /machine:X64.

Also, just after you generated the project for MSVC (if you fail or forget to do
that before the first compiling, you'll have to delete and regenerate all project
files) then open Configuration Manager **exactly** after generation from cmake and
setup x86 platform with requesting to generate this for every subproject.

5. IMPORTANT FOR DEVELOPERS AND CONTRIBUTORS: If you make any changes that fix
something in the Windows version, remember to keep the project working also for
all other platforms. To simplify the verification if you just would like to do
it on the Windows machine, please install Cygwin and make another build for Cygwin,
for example (remember that 'configure' script requires tcl8.5 package):

		mkdir build-cygwin
		cd build-cygwin
		../configure --prefix=install --cygwin-use-posix
		make

The Cygwin platform isn't any important target platform for this project, but it's
very useful to check if the project wouldn't be build-broken on Linux.



[appveyor-badge]: https://img.shields.io/appveyor/ci/Haivision/srt/master.svg?label=Windows
[appveyor]: https://ci.appveyor.com/project/Haivision/srt
[travis-badge]: https://img.shields.io/travis/Haivision/srt/master.svg?label=Linux/macOS
[travis]: https://travis-ci.org/Haivision/srt
[license-badge]: https://img.shields.io/badge/License-MPLv2.0-blue

[github releases]: https://github.com/Haivision/srt/releases
[release-badge]: https://img.shields.io/github/release/Haivision/srt.svg

[debian-badge]: https://badges.debian.net/badges/debian/testing/libsrt1/version.svg
[debian-package]: https://packages.debian.org/testing/libsrt1
