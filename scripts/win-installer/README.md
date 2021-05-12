## SRT Static Libraries Installer for Windows

This directory contains scripts to build a binary installer for
libsrt on Windows systems for Visual Studio applications using SRT.

### Building Windows applications with libsrt

After installing the libsrt binary, an environment variable named `LIBSRT` is
defined to the installation root (typically `C:\Program Files (x86)\libsrt`).

In this directory, there is a Visual Studio property file named `libsrt.props`.
Simply reference this property file in your Visual Studio project to use libsrt.

You can also do that manually by editing the application project file (the XML
file named with a `.vcxproj` extension). Add the following line just before
the end of the file:

~~~
  <Import Project="$(LIBSRT)\libsrt.props"/>
~~~

### Building the installer

The first two steps need to be executed once only. Only the last step needs
to be repeated each time a new version of libsrt is available.

- Prerequisite 1: Install OpenSSL for Windows, both 64 and 32 bits.
  This can be done automatically by running the PowerShell script `install-openssl.ps1`.
- Prerequisite 2: Install NSIS, the NullSoft Installation Scripting system.
  This can be done automatically by running the PowerShell script `install-nsis.ps1`.
- Build the libsrt installer by running the PowerShell script `build-win-installer.ps1`.

The installer is then available in the directory `installers`.
