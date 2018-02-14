# Establishing a Build Environment
## Installing the Android NDK
The Android NDK is required to build native modules for Android.  
Download the appropriate NDK archive from the following site: [Download the Android NDK](https://developer.android.com/ndk/downloads/index.html)  
To install the Android NDK, simply expand the archive in the folder where you want to install it.
## Creating the Toolchain
Run ```mktoolchains``` script to prepare standalone toolchains for all supported architectures. Refer to [NDK docs](https://developer.android.com/ndk/guides/standalone_toolchain.html) for details.
## Working with Clang and libc++
GNU compilers and gnustl will be removed from NDK starting Q3 2018, so we will use clang and libc++.
## OpenSSL
Google removed openssl from Android 7+. You must build openssl libs by yourself.
# Build SRT for Android
Run ```/bin/bash mkall```. Libraries will be installed to ```./target-architecture/lib```.
