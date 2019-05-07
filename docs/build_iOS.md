# Building SRT for iOS

## Prerequisites
* Xcode should be installed. Check in terminal whether `xcode-select -p` points to **/Applications/Xcode.app/Contents/Developer** 
* Install Homebrew according to instructions on [https://brew.sh/](https://brew.sh/)
* Install CMake and pkg-config with Homebrew:
```
brew install cmake
brew install pkg-config
```

## Building OpenSSL
There is [OpenSSL for iPhone](https://github.com/x2on/OpenSSL-for-iPhone) project which have all necessary to build OpenSSL for our needs. It fetches OpenSSL code by itself, so you don't need to download it separately. So simply clone it and build with command:
```
./build-libssl.sh --archs="arm64"
```

Results (both libraries and headers) will be placed in bin/&lt;SDK_VERSION&gt;-&lt;ARCH&gt;.sdk directory (for example, *bin/iPhoneOS11.2-arm64.sdk*). We assume you set **IOS_OPENSSL** variable to this path (e.g. `export IOS_OPENSSL="/Users/johndoe/sources/OpenSSL-for-iPhone/bin/iPhoneOS11.2-arm64.sdk"`). 

## Building SRT code
Now you can build SRT providing path to OpenSSL library and toolchain file for iOS

```
./configure --cmake-prefix-path=$IOS_OPENSSL --use-openssl-pc=OFF --cmake-toolchain-file=scripts/iOS.cmake
make
```

Optionally you may add following iOS-specifc settings to configure:

* `--ios-disable-bitcode=1` - disable embedding bitcode to library. 
* `--ios-arch=armv7|armv7s|arm64` - specify if you want to build for specific architecture (arm64 by default) 
* `--ios-platform=OS|SIMULATOR|SIMULATOR64` - specify for build simulator code 
* `--cmake-ios-developer-root=&lt;path&gt;`- specify path for platform directory; {XCODE_ROOT}/Platforms/iPhoneOS.platform/Developer by default 
* `--cmake-ios-sdk-root=<path>` - by default searches for latest SDK version within {CMAKE_IOS_DEVELOPER_ROOT}/SDKs, set if you want to use another SDK version

Note that resulting .dylib file has install path @executable_path/Frameworks/libsrt.1.dylib, so if you need to place it in some other place with your application, you may change it with *install_name_tool* command: ``install_name_tool -id "<install_path>" <library_file>``, for example ``install_name_tool -id "@executable_path/Frameworks/libsrt.1.3.0.dylib" libsrt.1.3.0.dylib``

## Adding to Xcode project
In Xcode project settings in General tab, add libsrt to **Linked Frameworks and Libraries** section - click Plus sign, then click "Add Other" and find libsrt.1.dylib

Click plus sign in **Embedded binaries** section and choose Frameworks/libsrt.1.dylib

In **Build settings** tab find **Header Search Paths** setting  
and add paths to SRT library sources (you should add srt, srt/common and srt/common directories). 
