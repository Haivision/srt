# Building SRT on macOS

[Homebrew](https://brew.sh/) supports the [`srt`](https://formulae.brew.sh/formula/srt) formula.

```shell
brew update
brew install srt
```

If you prefer using a head commit of `master` branch, add the `--HEAD` option
to `brew` command.

```shell
brew install --HEAD srt
```

Install [CMake](https://cmake.org/) and OpenSSL with development files from `brew`. It is recommended to install the latest version of OpenSSL from the `brew` system rather than relying on the version that is presently installed in the system.

```shell
brew install cmake
brew install openssl
```

SRT can be now built with `cmake` or `make` on Mac.

```shell
export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
export OPENSSL_LIB_DIR=$(brew --prefix openssl)"/lib"
export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)"/include"
./configure
make
```
