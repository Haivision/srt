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
* [Using the `srt-live-transmit` and `srt-file-transmit` Apps](docs/stransmit.md)
* [SRT Encryption](docs/encryption.md)
* [API](docs/API.md)
* [Reporting problems](docs/reporting.md)

# Requirements

* cmake (as build system)
* Tcl 8.5 (optional for user-friendly build system)
* OpenSSL
* Pthreads (for POSIX systems it's builtin, for Windows there's a library)

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

1. Please download and install OpenSSL for Windows.

The 64-bit devel package can be downloaded from here:

     http://slproweb.com/download/Win64OpenSSL-1_0_2r.exe

	 
(Note that the last letter or version number may be changed and older versions
no longer available. If this isn't found, check here:
http://slproweb.com/products/Win32OpenSSL.html
)

It's expected to be installed in `C:\OpenSSL-Win64` (see the above variables).

Note that this version is compiled most likely for Visual Studio 2013. For
other versions you better download and compile the sources by yourself,
from: https://github.com/openssl/openssl

The instruction for Windows:
http://developer.covenanteyes.com/building-openssl-for-visual-studio/

2. Compile and install Pthreads for Windows from this submodule:

     submodules/pthread-win32
	 
Please follow the steps:

a. Using Visual Studio 2013, please open this file:

     pthread_lib.2013.vcxproj

b. Make sure to select configuration: `Release` and `x64`.

c. Make sure that the `pthread_lib` project will be built.

d. After building, find the `pthread_lib.lib` file (directory is probably: `bin\x64_MSVC2013.Release`).
Copy this file to `C:\pthread-win32\lib` (or whatever other location you configured in variables).

e. Copy include files to `C:\pthread-win32\include` - the following ones:

     pthread.h
     sched.h
     semaphore.h

(They are in the toplevel directory, there are actually no meaningful subdirs here)
(NOTE: the win32 is part of the project name. It will become 32 or 64 depending on selection)


3. Install cmake for Windows. The CMake GUI will help you configure the project.

It will try to find OpenSSL and pthreads. If you installed them in the default location, they will be found automatically. If not, you can define the following variables to help CMake find them: 
```
OPENSSL_ROOT_DIR=<path to OpenSSL installation>
OPENSSL_LIBRARIES=<path to all the openssl libraries to link>
OPENSSL_INCLUDE_DIR=<path to the OpenSSL include dir>

PTHREAD_INCLUDE_DIR=<path to where pthread.h lies>
PTHREAD_LIBRARY=<path to pthread.lib>
```
 

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
