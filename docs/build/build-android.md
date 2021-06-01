# Building SRT for Android

**NOTE:** The scripts have been moved to [scripts/build-android](../../scripts/build-android/) folder.

## Install the NDK and CMake

The Android NDK is required to build native modules for Android.
[Install and configure the NDK](https://developer.android.com/studio/projects/install-ndk)

Consider installing the latest version of cmake. The higher version of cmake the better. As of writing the current version of CMake is 3.18.4
You can download Cmake from the following website:
[https://cmake.org/download](https://cmake.org/download/)

## Build SRT for Android

Run ```./build-android -n /path/to/ndk```. E.g. ```./build-android -n /home/username/Android/Sdk/ndk/21.4.7075529```

[Include prebuilt native libraries](https://developer.android.com/studio/projects/gradle-external-native-builds#jniLibs) from ```prebuilt``` folder into Android Studio project.
