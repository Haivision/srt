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

##### 2.1.1.1. Using Installer

The 64-bit OpenSSL package for windows can be downloaded using the following link.: [Win64OpenSSL_Light-1_1_1c](http://slproweb.com/download/Win64OpenSSL_Light-1_1_1c.exe).

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


## 3. Cloning the Source Code of SRT

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