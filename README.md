<p align="center">
  <a href="http://srtalliance.org/">
    <img alt="Node.js" src="http://www.srtalliance.org/wp-content/uploads/SRT_text_hor_logo_grey.png" width="600"/>
  </a>
</p>

# Introduction

Secure Reliable Transport (SRT) is a proprietary transport technology that optimizes streaming performance across unpredictable networks, such as the Internet.

|    |    |
| --- | --- | 
| **S**ecure | Encrypts video streams |
| **R**eliable | Recovers from severe packet loss |
| **T**ransport | Dynamically adapts to changing network conditions |

SRT is applied to contribution and distribution endpoints as part of a video stream workflow to deliver the best quality and lowest latency video at all times.

As audio/video packets are streamed from a source to a destination device, SRT detects and adapts to the real-time network conditions between the two endpoints. SRT helps compensate for jitter and bandwidth fluctuations due to congestion over noisy networks, such as the Internet. Its error recovery mechanism minimizes the packet loss typical of Internet connections. And SRT supports AES encryption for end-to-end security, keeping your streams safe from prying eyes.

# Requirements

* cmake (as build system)
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

## For Windows:

1. Install cmake for Windows. The CMake GUI will help you configure the project.
Note that some variables must be provided explicitly. These are the default
recommended values (required until some solution for running the `configure`
script in Windows can be found):

		WITH_OPENSSL_INCLUDEDIR=C:/OpenSSL-Win64/include
		WITH_OPENSSL_LIBDIR=C:/OpenSSL-win64/lib/VC/static
		WITH_OPENSSL_LIBRARIES=libeay32MT.lib ssleay32MT.lib
		WITH_PTHREAD_INCLUDEDIR=C:/pthread-win32/include
		WITH_PTHREAD_LDFLAGS=C:/pthread-win32/lib/pthread_lib.lib


2. Please download and install OpenSSL for Windows.

The 64-bit devel package can be downloaded from here:

     http://slproweb.com/download/Win64OpenSSL-1_0_2a.exe

It's expected to be installed in `C:\OpenSSL-Win64` (see the above variables).


3. Compile and install Pthreads for Windows from this submodule:

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


# Using the stransmit app

The stransmit is a test application that forwards data between two URLs, here is an example:

First run this to send with SRT:
./stransmit file://$(pwd)/BigBuckBunny.ts srt://localhost:1234/?mode=server

The run this to receive with SRT, this forwards to the ffplay command line from
ffmpeg:
./stransmit  srt://localhost:1234/?mode=client file://con | ffplay -
