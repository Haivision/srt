<p align="center">
  <a href="http://srtalliance.org/">
    <img alt="SRT" src="http://www.srtalliance.org/wp-content/uploads/SRT_text_hor_logo_grey.png" width="600"/>
  </a>
</p>

[![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/ruujele1yfsfsyer/branch/master?svg=true)](https://ci.appveyor.com/project/Haivision/srt) [![Build Status](https://travis-ci.org/Haivision/srt.svg?branch=master)](https://travis-ci.org/Haivision/srt)

# Introduction

Secure Reliable Transport (SRT) is a proprietary transport technology that optimizes streaming performance across unpredictable networks, such as the Internet.

|    |    |
| --- | --- |
| **S**ecure | Encrypts video streams |
| **R**eliable | Recovers from severe packet loss |
| **T**ransport | Dynamically adapts to changing network conditions |

SRT is applied to contribution and distribution endpoints as part of a video stream workflow to deliver the best quality and lowest latency video at all times.

As audio/video packets are streamed from a source to a destination device, SRT detects and adapts to the real-time network conditions between the two endpoints. SRT helps compensate for jitter and bandwidth fluctuations due to congestion over noisy networks, such as the Internet. Its error recovery mechanism minimizes the packet loss typical of Internet connections. And SRT supports AES encryption for end-to-end security, keeping your streams safe from prying eyes.

[Join the conversation](https://join.slack.com/t/srtalliance/shared_invite/enQtMjY5MzY4MjQ0Njc4LTJjYWYzMmYzN2RjYWI4MWMxZDdiYTE4OTZlMDE4YWQyOGJhNTgwYjIzOTdiODY5OGE4YTQ4ZGZjN2Y0OTI5ZTA) in the #development channel on [Slack](https://srtalliance.slack.com).

# Guides
* [Why SRT Was Created](docs/why-srt-was-created.md)
* [Using the `stransmit` App](docs/stransmit.md)
* [SRT Encryption](docs/encryption.md)

# Requirements

* cmake (as build system)
* Tcl 8.5 (optional for user-friendly build system)
* OpenSSL
* Pthreads (for POSIX systems it's builtin, for Windows there's a library)

## For Linux:
Install cmake and openssl-devel (or similar name) package. For pthreads
there should be -lpthreads linker flag added.

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

     http://slproweb.com/download/Win64OpenSSL-1_0_2a.exe

	 
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
OPENSSL_ROOT_DIR=<path to OpenSSL instalation>
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

# Using the stransmit app

The `stransmit` application is primarily intended to be used for testing and as
an API example, but it's also a perfectly good flipper application for a live
stream. The general usage is:

    ./stransmit SOURCE_URI TARGET_URI

    where all `*_URI` arguments specify the medium: SRT, UDP or FILE. This is a typical form
    of URI matching the template `SCHEME://HOST:PORT?PARAM1=VALUE1&PARAM2=VALUE2`.

The most typical use would be to transmit a live stream originally from UDP.
For example, let's say you want to receive a UDP stream on the local host port
5000, then transmit it to a remote site host (`remote.example.com` on port
9000), where it flips again to local port 5000.

To do this, on the sending side execute the following:

    ./stransmit udp://:5000 srt://remote.example.com:9000

    NOTE: When you specify a host in SRT (e.g. `remote.example.com`),
    `stransmit` defaults to SRT CALLER mode.

On the receiving side execute the following:

    ./stransmit srt://:9000 udp://:5000

    NOTE: If you do not specify a host in case of SRT, `stransmit` defaults
    to LISTENER mode and binds to 0.0.0.0. You can also specify a network
    device for listening by giving its IP address in `adapter` parameter, e.g.:
    `srt://:9000?adapter=192.168.2.3`, or by specifying this IP as host,
    with enforcing the listener mode: `srt://192.168.2.3:9000?mode=listener`.
    In case of UDP, the empty host is interpreted as 0.0.0.0.

For the moment, `stransmit` is designed for the re-transmission of live video
streams only. If you try to use a stream file as a source, it won't work as
expected. You first have to make a live stream with the source in a file,
redirect it to a local UDP port, and then use `stransmit` to flip it to SRT.

To accomplish this, you can use the `ffmpeg` command line or `vlc` player to
stream a file to UDP. A very useful set of TS tools for creating a live stream
from a file is available here: https://github.com/kynesim/tstools

When receiving a stream, you can make `stransmit` send it to a file or
redirect it to a pipeline so that you can then connect it to some other command
line tool that will do something with the stream. To help accomplish this,
`stransmit` recognizes a custom specification for *file* scheme: `file://con`,
which means _stdin_ when specified as source URI and _stdout_ when specified as
target URI. For example, you can play a stream immediately upon receiving it by
using `ffplay`:

    ./stransmit srt://:9000 file://con | ffplay -

There are two important parameters that you might want to specify in the SRT URI:

* **latency**: the actual delay used for data delivery on the receiving side
               (default is 125 ms)
* **passphrase**: The password/phrase used for encryption and decryption

Note that `latency` is important on noisy or constrained networks where UDP
packet drops often occur, and round-trip times (RTT) may be long. This is
a time difference between the time when the packet was sent (plus RTT) and
the "time to play", when it should be delivered to the application. A lost
packet must be re-requested and retransmited before the "time to play" comes,
otherwise SRT will also drop this packet in order not to compromise the live
stream pace. By increasing the latency, you give the SRT session more time for
retransmission in case of a packet loss. On the other hand, remember that the
bigger the latency, the bigger the delay.

For more information, please refer to the SRT Deployment Guide:
http://www3.haivision.com/srt-alliance-guide
