# Building SRT for Windows

<!-- TOC -->

- [1. Prerequisites](#1-prerequisites)
  - [1.1. Build Tool Dependencies](#11-build-tool-dependencies)
  - [1.2. External Library Dependencies](#12-external-library-dependencies)
    - [1.2.1. Cryptographic Library](#121-Cryptographic-library)
    - [1.2.2. Threading Library](#122-threading-library)
  - [1.3. Package Managers](#13-package-managers)
    - [1.3.1. VCpkg Packet Manager (optional)](#131-vcpkg-packet-manager-optional)
    - [1.3.2. NuGet Manager (optional)](#132-nuget-manager-optional)
- [2. Preparing Dependencies](#2-preparing-dependencies)
  - [2.1. Cryptographic Library](#21-Cryptographic-library)
    - [2.1.1. Install OpenSSL](#211-install-openssl)
      - [2.1.1.1. Using vcpkg](#2111-using-vcpkg)
      - [2.1.1.2. Using Installer](#2112-using-installer)
      - [2.1.1.3. Build from Sources](#2113-build-from-sources)
    - [2.1.2. Install MbedTLS](#212-install-mbedtls)
    - [2.1.3. Install LibreSSL](#213-install-libressl)
  - [2.2. Threading Library](#22-threading-library)
    - [2.2.1. Using C++11 Threading](#221-using-c11-threading)
    - [2.2.2. Building PThreads](#222-building-pthreads)
      - [2.2.2.1. Using vcpkg](#2221-using-vcpkg)
      - [2.2.2.2. Using NuGet](#2222-using-nuget)
      - [2.2.2.3. Build pthreads4w from Sources](#2114-build-pthreads4w-from-sources)
      - [2.2.2.4. Build pthread-win32 from Sources](#2114-build-pthread-win32-from-sources)
- [3. Building SRT](#3-building-srt)
  - [3.1. Cloning the Source Code](#31-cloning-the-source-code)
  - [3.2. Generate Build Files](#32-generating-build-files)
  - [3.3. Build SRT](#33-build-srt)

<!-- /TOC -->

## 1. Prerequisites

### 1.1. Build Tool Dependencies

The following are the recommended prerequisites to build `srt` on Windows.

- [CMake](http://www.cmake.org/) v2.8.12 or higher (cross-platform family of tools to build software)
- [git](https://git-scm.com/about) client (source code management tool)
- [Visual Studio](https://visualstudio.microsoft.com/vs/downloads/) (compiler and linker)

### 1.2. External Library Dependencies

#### 1.2.1. Cryptographic Library

SRT has an external dependency on **cryptographic library**.
This dependency can be disabled by `-DENABLE_ENCRYPTION=OFF` CMake build option.
With disabled encryption SRT will be unable to establish secure connections,
only unencrypted mode can be used.

With the enabled SRT encryption,
one of the following Crypto libraries is required:

- `OpenSSL` (default)
- `LibreSSL`
- `MbedTLS`

#### 1.2.2. Threading Library

SRT as of v1.4.2 supports two threading libraries:

- `pthreads` (default)
- Standard C++ thread library available in C++11 (recommended for Windows)

The `pthreads` library is provided out-of-the-box on all POSIX-based systems.
On Windows it can be provided as a 3rd party library (see below).
However the C++ standard thread library is recommended to be used on Windows.

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

NuGet package manager can be used to get a prebuilt version of `pthreads` library for Windows.

Download [nuget CLI](https://www.nuget.org/downloads) to the desired folder.

The directory with NuGet will further be referenced as `NUGET_ROOT`.

```shell
set NUGET_ROOT=%cd%
```

## 2. Preparing Dependencies

### 2.1 Cryptographic Library

To build SRT with support for encryption,
**one** of the following Crypto libraries is required:

- `OpenSSL` (recommended)
- `LibreSSL`
- `MbedTLS`

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

The 64-bit OpenSSL package for Windows can be downloaded using
the following link: [Win64OpenSSL_Light-1_1_1c](http://slproweb.com/download/Win64OpenSSL_Light-1_1_1c.exe).

**Note!** The last letter or version number may change and older versions may become no longer available. In that case find the appropriate installer here: [Win32OpenSSL](http://slproweb.com/products/Win32OpenSSL.html).

Download and run the installer. The library is expected to be installed in `C:\Program Files\OpenSSL-Win64`. Add this path to the user's or system's environment variable `PATH`.

It's expected to be installed in `C:\OpenSSL-Win64`.
Note that this version is most likely compiled for Visual Studio 2013. For other versions please follow instructions in Section [2.1.1.3 Build from Sources](#2113-build-from-sources).

##### 2.1.1.3. Build from Sources

Download and compile the sources from the [OpenSSL website](https://github.com/openssl/openssl). The instructions for compiling on Windows can be found here: [link](https://wiki.openssl.org/index.php/Compilation_and_Installation#Windows).

**Note!** `ActivePerl` and `nasm` are required to build OpenSSL.

#### 2.1.2. Install MbedTLS

`MbedTLS` source code can be downloaded from the [website](https://tls.mbed.org/download).

`MbedTLS` comes with `cmake` build system support. Use the `CMAKE_INSTALL_PREFIX` variable to specify the directory that will contain the `MbedTLS` headers and libraries. Note that building `MbedTLS` as a DLL is broken in version 2.16.3. You have to link it statically.

#### 2.1.3. Install LibreSSL

LibreSSL has header files that are compatible with OpenSSL,
CMake can use it like OpenSSL with little configuration.
The source code and binaries can be downloaded from here: [link](https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/).

Since there are no recent Windows builds, the only option is to build a new version from sources.
LibreSSL comes with CMake build system support. Use the `CMAKE_INSTALL_PREFIX` variable
to specify the directory that will contain the LibreSSL headers and libraries.

### 2.2. Threading Library

SRT can use one of these two threading libraries:

- C++11 threads (SRT v1.4.2 and above) - recommended for Windows
- `pthreads` (default)

#### 2.2.1. Using C++11 Threading

To be able to use the standard C++ threading library (available since C++11)
specify the CMake option `-DENABLE_STDCXX_SYNC=ON`.
This way there will be also no external dependency on the threading library.
Otherwise the external PThreads for Windows wrapper library is required.

#### 2.2.2. Building PThreads

##### 2.2.2.1. Using vcpkg

**Note!** The `vcpkg` working directory is referenced as `VCPKG_ROOT`.

Build the `pthreads` library using the **x64** toolset:

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

##### 2.2.2.2. Using NuGet

This step assumes the NuGet is available from the `NUGET_ROOT` folder
(refer to [1.3.2. NuGet Manager (optional)](#132-nuget-manager-optional)).

Run `nuget` to install `pthreads` to the specified path.
In the example below the library will be installed in `C:\pthread-win32`.

```shell
nuget install cinegy.pthreads-win64 -version 2.9.1.17 -OutputDirectory C:\pthread-win32
```

Two CMake options have to be provided on the step [3.2. Generate Build Files](#32-generating-build-files).

```shell
-DPTHREAD_INCLUDE_DIR="C:\pthread-win32\cinegy.pthreads-win64.2.9.1.17\sources"
-DPTHREAD_LIBRARY="C:\pthread-win32\cinegy.pthreads-win64.2.9.1.17\runtimes\win-x64\native\release\pthread_lib.lib"
```

##### 2.2.2.3. Build pthreads4w from Sources

Download the source code from SourceForge ([link](https://sourceforge.net/projects/pthreads4w/))
and follow the build instructions.

##### 2.2.2.4. Build pthread-win32 from Sources

Compile and install `pthread-win32` for Windows from GitHub: [link](https://github.com/GerHobbelt/pthread-win32).

1. Using Visual Studio 2013, open the project file `pthread_lib.2013.vcxproj`
2. Select configuration: `Release` and `x64`.
3. Make sure that the `pthread_lib` project will be built.
4. After building, find the `pthread_lib.lib` file (directory is usually `bin\x64_MSVC2013.Release`). Copy this file to `C:\pthread-win32\lib` (or whatever other location you configured in variables).
5. Copy include files to `C:\pthread-win32\include` (`pthread.h`, `sched.h`, and `semaphore.h` are in the toplevel directory. There are no meaningful subdirs here). Note that `win##` is part of the project name. It will become `win32` or `win64` depending on the selection.

## 3. Building SRT

### 3.1. Cloning the Source Code

Retrieve the SRT source code from GitHub using a git client.

```shell
git clone --branch <tag_name> https://github.com/haivision/srt.git srt
cd srt
set SRT_ROOT=%cd%
```

where `--branch <tag_name>` can be used to define a specific release version of SRT,
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
