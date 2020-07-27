# Establishing a Build Environment
## Installing the Android NDK
The Android NDK is required to build native modules for Android.
Download the NDK r19 or newer archive from the following site:
[Download the Android NDK on developer.android.com](https://developer.android.com/ndk/downloads/index.html)
To install the Android NDK, simply expand the archive in the folder where you want to install it.
## OpenSSL
Google removed openssl from Android 7+. You must build openssl libs by yourself.
# Configure the NDK path
Edit the ```mkall``` script to configure NDK path. Set the ```NDK``` to the directory where the NDK is installed.
# Build SRT for Android
Run ```/bin/bash mkall > build.log``` script. Libraries will be installed to ```./target-architecture/lib```.
# Export SRT libraries
Run ```/bin/bash packjni``` to generate ```jniLibs``` archive for Android Studio.
