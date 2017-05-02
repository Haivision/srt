REQUIREMENTS:
============

* cmake (as build system)
* OpenSSL
* Pthreads (for POSIX systems it's builtin, for Windows there's a library)

For Linux:
==========

Install cmake and openssl-devel (or similar name) package. For pthreads
there should be -lpthreads linker flag added.

## Ubuntu 14
```
sudo apt-get update
sudo apt-get upgrade
sudo apt-get install tclsh pkg-config cmake libssl-dev build-essential
./configure
make
```
## CentOS 7
```
sudo yum update
sudo yum install tcl pkgconfig openssl-devel cmake gcc gcc-c++ make automake
./configure
make
```

For Mac (Darwin, iOS):
=====================

Install cmake and openssl with development files from "brew". Note that the
system version of OpenSSL is inappropriate, although you should be able to
use any newer version compiled from sources, if you prefer.

For Windows:
============

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


Using the stransmit app
=======================

The `stransmit` application is used both for testing and as an API example, but it's
still a perfect flipper application for a live stream. The general usage is:

    ./stransmit SOURCE_URI TARGET_URI

where all `*_URI` arguments specify the medium: SRT, UDP or FILE.

The most typical use would be to transmit a live stream originally from UDP, so let's
pretend you have a UDP stream sent to the local host port 5000, then you transmit it
to a remote site host `remote.example.com` port 9000 so that it flips it again to its
local port 5000:

On the sending side you do:

    ./stransmit udp://:5000 srt://remote.example.com:9000

(note that for SRT when you specify the HOST part, it defaults to CALLER mode)

On the receiving side you do:

    ./stransmit srt://:9000 udp://:5000

(Note that for SRT if you skip the HOST part, it will default to LISTENER mode,
whereas in case of UDP the lacking HOST simply defaults to 0.0.0.0).

You can also enforce appropriate network device for listening by giving its IP
address, e.g.: `srt://:9000?adapter=192.168.2.3`.

Note that SRT is a protocol predicted to transmit the live video stream. So if you try
to use just the "stream file" as a source, it won't work as expected. You'd have to first
make a live stream with the source in a file somehow and redirect it to a local UDP port,
and then use `stransmit` to flip it to SRT.

If you want to make such a stream, you can try to use the `ffmpeg` command line or
use `vlc` player to stream a file to UDP, there is also a very useful set ot TS tools
that you can also use to make a live stream from a file: https://github.com/kynesim/tstools

When receiving a stream, you can perfectly redirect it to a file, if you want, or you
can make `stransmit` send it to a pipeline so that you can then connect it to some
other command line tool that will do something with the stream. For that occasion there
is a kind-of nonstandard specification for *file* scheme: `file://con`, which means
_stdin_, when specified as source URI and _stdout_ when target URI. For example, you can
play it directly with `ffplay`:

    ./stransmit srt://:9000 file://con | ffplay -

There are two important parameters that you need to be specified in the SRT parameters
in the URI:

* **latency**: the actual delay used for data delivery on the receiving side (default: 125)
* **passphrase**: The password phrase uses for encryption and decryption

Note that `latency` is important when you have a network that may do often packet
drops in UDP. If the latency is too short towards RTT, then the packet may still be
dropped also in SRT due to inability to keep up with the live stream pace. By increasing
latency you give it more time for possible packet retransmission in case of a packet
loss and the time required to re-request and retransmit the packet will be
short enough so that the packet can still be delivered on time as required for
the live transmission. 
