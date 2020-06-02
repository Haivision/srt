# SRT Build Instructions

<!-- TOC -->

- [1. Prerequisites](#1-prerequisites)
  - [1.1. Build Tool Dependencies](#11-build-tool-dependencies)
  - [1.2. External Library Dependencies](#12-external-library-dependencies)
    - [1.2.1. Cryptograpjic Library](#121-cryptograpjic-library)
    - [1.2.2. Threading Library](#122-threading-library)
  - [1.3. Package Managers](#13-package-managers)
    - [1.3.1. VCpkg Packet Manager (optional)](#131-vcpkg-packet-manager-optional)
    - [1.3.2. NuGet Manager (optional)](#132-nuget-manager-optional)
- [2. Preparing Dependencies](#2-preparing-dependencies)
  - [2.1. Cryptograpjic Library](#21-cryptograpjic-library)
    - [2.1.1. Install OpenSSL](#211-install-openssl)
      - [2.1.1.1. Using Installer](#2111-using-installer)
      - [2.1.1.2. Build from Sources](#2112-build-from-sources)
    - [2.1.2. Install MbedTLS](#212-install-mbedtls)
    - [2.1.3. Install LibreSSL](#213-install-libressl)
- [3. Cloning the Source Code of SRT](#3-cloning-the-source-code-of-srt)

<!-- /TOC -->

## 1. Prerequisites

### 1.1. Build Tool Dependencies

The following are the recommended prerequisites to build `srt` on Windows.

- [CMake](http://www.cmake.org/) v2.8.12 or higher (cross-platform family of tools to build software)
- [git](https://git-scm.com/about) client (source code management tool)
- [Visual Studio](https://visualstudio.microsoft.com/vs/downloads/) (compiler and linker)

### 1.2. External Library Dependencies

#### 1.2.1. Cryptograpjic Library

SRT has an external dependency on **cryptographic library**.
This dependency can be disabled with the `-DENABLE_ENCRYPTION=OFF` CMake build option.
Then SRT will be able to operate only in unencrypted mode.

To be able to use SRT encryption,
one of the following Crypto libraries is required:

- `OpenSSL` (recommended)
- `LibreSSL`
- `MbedTLS`

#### 1.2.2. Threading Library

SRT as of v1.5.0 supports two threading libraries:

- `pthreads` (default)
- C++11 threads (recommended for Windows)

The `pthreads` library is available on the most of non-Windows platforms.
However, on Windows a ported library has to be used.
Therefore, C++11 is the recommended build mode to be used on Windows platforms.

### 1.3. Package Managers

#### 1.3.1. VCpkg Packet Manager (optional)

[vcpkg](https://github.com/microsoft/vcpkg) is a C++ library manager for Windows, Linux and MacOS.
Consider its [prerequisites](https://github.com/microsoft/vcpkg/blob/master/README.md#quick-start) before proceeding.

The `vcpkg` library manager has preconfigured building procedures for `OpenSSL` and `pthreads` libraries.
They can be easily built and further used as dependencies of SRT library.

**Note!** `vcpkg` does not support `LibreSSL` or `MbedTLS` libraries.

Clone `vcpkg` using git, and build it.

```shell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

The current working directory will further be referenced as `VCPKG_ROOT`.

```shell
set VCPKG_ROOT=%cd%
```

#### 1.3.2. NuGet Manager (optional)

NuGet Manager can be used to...

## 2. Preparing Dependencies

### 2.1 Cryptograpjic Library

**One** of the following Crypto libraries is required:

- `OpenSSL` (recommended)
- `LibreSSL`
- `MbedTLS`

Alternatively, SRT can be build without support for encryption
using the `-DENABLE_ENCRYPTION=OFF` CMake build option.
In this case SRT will be able to operate only in unencrypted mode.

#### 2.1.1. Install OpenSSL

##### 2.1.1.1. Using vcpkg

**Note!** The `vcpkg` working directory is referenced as `VCPKG_ROOT`.

Building `openssl` library using **x64** toolset:

```shell
cd VCPKG_ROOT
vcpkg install openssl --triplet x64-windows
```

The next step is to integrate `vcpkg` with the build system, so that `CMake` can locate `openssl` library.

```shell
vcpkg integrate install
```

CMake will be able to find openssl given the following option is provided:

```shell
-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\\scripts\\buildsystems\\vcpkg.cmake
```

##### 2.1.1.2. Using Installer (Windows)

The 64-bit OpenSSL package for windows can be downloaded using
the following link: [Win64OpenSSL_Light-1_1_1c](http://slproweb.com/download/Win64OpenSSL_Light-1_1_1c.exe).

**Note!** The last letter or version number may be changed and older versions may become no longer available. In that case found the appropriate installer here: [Win32OpenSSL](http://slproweb.com/products/Win32OpenSSL.html).

Download and run the installer. The library is expected to be installed in `C:\Program Files\OpenSSL-Win64`. Add this path to the user's or system's environment variable `PATH`.

It's expected to be installed in `C:\OpenSSL-Win64`. Note that this version is most likely compiled for Visual Studio 2013. For other versions please follow instructions in Section **2.2.2 Build OpenSSL from Sources**.

##### 2.1.1.2. Build from Sources

Download and compile the sources from the [website](https://github.com/openssl/openssl). The instructions for compiling on Windows can be found here: [link](https://wiki.openssl.org/index.php/Compilation_and_Installation#Windows).

**Note!** `ActivePerl` and `nasm` are required to build OpenSSL.

#### 2.1.2. Install MbedTLS

`MbedTLS` source code can be downloaded from the [website](https://tls.mbed.org/download).

`MbedTLS` comes with `cmake` build system support. Use the `CMAKE_INSTALL_PREFIX` variable to specify the directory that will contain the `MbedTLS` headers and libraries. Note that building `MbedTLS` as a DLL is broken in version 2.16.3. You have to link it statically.

#### 2.1.3. Install LibreSSL

`LibreSSL` has header files that are compatible with OpenSSL, `cmake` can use it like OpenSSL with little configuration. The source code and binaries can be downloaded from here: [link](https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/).

Since there have been no new Windows builds since 2.6.0, you must build a new version yourself. LibreSSL comes with `cmake` build system support. Use the `CMAKE_INSTALL_PREFIX` variable to specify the directory that will contain the LibreSSL headers and libraries.

### 2.2. Threading Library

SRT can use one of these two threading libraries:

- C++11 threads (SRT v1.5.0 and above) - recommended for Windows
- `pthreads` (default)

**Note!** `pthreads` are available by default on POSIX platforms like Linux and MacOS.
This step is only required on Windows Platforms.

#### 2.2.1. Using C++11 Threading

Specify the CMake option `-DENABLE_STDCXX_SYNC=ON` ti enable C++11 Threading,
and exclude the `pthreads` dependency.

#### 2.2.2. Building PThreads

##### 2.2.2.1. Using vcpkg

**Note!** The `vcpkg` working directory is referenced as `VCPKG_ROOT`.

Building `pthreads` library using **x64** toolset:

```shell
vcpkg install pthreads --triplet x64-windows
```

The next step is to integrate `vcpkg` with the build system, so that `CMake` can locate `pthreads` library.

```shell
vcpkg integrate install
```

Now go to the `srt-xtransmit` cloned folder `XTRANSMIT_ROOT` and run `cmake` to generate build configuration files.

CMake will be able to find openssl given the following option is provided:

```shell
-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\\scripts\\buildsystems\\vcpkg.cmake
```

##### 2.1.1.3. Using NuGet

NuGet package manager can be used to get a prebuilt version of `pthreads` library for Windows.

To install the prebuild version of `pthreads` for Windows,
download [nuget CLI](https://www.nuget.org/downloads) to the target folder.

Then run `nuget` to install `pthreads` to the specified path. In the example below the library will be installed in `C:\pthread-win32`.

```shell
nuget install cinegy.pthreads-win64 -version 2.9.1.17 -OutputDirectory C:\pthread-win32
```

Two CMake options have to be provided on the step **3.2. Generate Build Files**.

```shell
-DPTHREAD_INCLUDE_DIR="C:\pthread-win32\cinegy.pthreads-win64.2.9.1.17\sources"
-DPTHREAD_LIBRARY="C:\pthread-win32\cinegy.pthreads-win64.2.9.1.17\runtimes\win-x64\native\release\pthread_lib.lib"
```

##### 2.5.1. Build pthreads4w from Sources

Download the source code from SourceForge ([link](https://sourceforge.net/projects/pthreads4w/))
and follow build instruction.

##### 2.5.2. Build pthread-win32 from Sources

Compile and install `pthread-win32` for Windows from GitHub: [link](https://github.com/GerHobbelt/pthread-win32).

1. Using Visual Studio 2013, open the project file `pthread_lib.2013.vcxproj`
2. Select configuration: `Release` and `x64`.
3. Make sure that the `pthread_lib` project will be built.
4. After building, find the `pthread_lib.lib` file (directory is usually `bin\x64_MSVC2013.Release`). Copy this file to `C:\pthread-win32\lib` (or whatever other location you configured in variables).
5. Copy include files to `C:\pthread-win32\include` (`pthread.h`, `sched.h`, and `semaphore.h` are in the toplevel directory. There are no meaningful subdirs here). Note that `win##` is part of the project name. It will become `win32` or `win64` depending on the selection.

## 3. Building SRT

### 3.1. Cloning the Source Code

Retrieve the source codes of SRT from GitHub using a git client.

```shell
git clone --branch <tag_name> https://github.com/haivision/srt.git srt
cd srt
set SRT_ROOT=%cd%
```

where `--branch <tag_name>` can be used to specify the certain release version of SRT,
e.g. `--branch v1.4.1`.

**Note!** The current working directory will further be referenced as `SRT_ROOT`.

If `--branch <tag_name>` is omitted, the latest master is cloned. To get a specific release version run
`git checkout <tagname>` from the `SRT_ROOT` folder.

```shell
git checkout v1.4.1
```

### 3.2. Generate Build Files

```shell
cmake ../ -G"Visual Studio 16 2019" -A x64
          -DPTHREAD_INCLUDE_DIR="C:\pthread-win32\cinegy.pthreads-win64.2.9.1.17\sources"
          -DPTHREAD_LIBRARY="C:\pthread-win32\cinegy.pthreads-win64.2.9.1.17\runtimes\win-x64\native\release\pthread_lib.lib"
```

**Note!** Additional options can be provided at this point.

In case vcpkg was used to build pthreads or OpenSSL, provide:

- `-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\\scripts\\buildsystems\\vcpkg.cmake`

In case NuGet was used to get pre-built pthreads libray, provide:

- `-DPTHREAD_INCLUDE_DIR`
- `-DPTHREAD_LIBRARY`

### 3.3. Build SRT

```shell
cmake --build .
```
