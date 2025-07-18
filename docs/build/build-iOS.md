# Building SRT on iOS/tvOS

**NOTE:** The scripts are located in [scripts/build-ios](../../scripts/build-ios/) folder.

## Prerequisites
* Xcode should be installed. Check in terminal whether `xcode-select -p` points to **/Applications/Xcode.app/Contents/Developer**
* Install Homebrew according to instructions on [https://brew.sh/](https://brew.sh/)
* Install CMake with Homebrew:
```
brew install cmake
```

## Building OpenSSL
There is [OpenSSL-Universal](https://github.com/krzyzanowskim/OpenSSL) project which has all necessary to build OpenSSL for our needs. It fetches OpenSSL code by itself, so you don't need to download it separately. Build it with command:
```
./mkssl-xcf.sh
```

## Building SRT code
Now you can build SRT with command:
```
./mksrt-xcf.sh
```

## Adding to Xcode project
Results (libcrypto.xcframework and libsrt.xcframework) will be placed in [scripts/build-ios](../../scripts/build-ios/) folder.

Follow [these steps](https://developer.apple.com/documentation/xcode/creating-a-static-framework#Embed-your-framework-in-an-app) to embed frameworks in your app. Choose the Do Not Embed option from the Embed value list.
