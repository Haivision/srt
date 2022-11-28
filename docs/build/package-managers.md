# Package Managers

The SRT library can be installed with the help of the following package managers:

- [Vcpkg](https://github.com/Microsoft/vcpkg)

  The SRT library package name is `libsrt`. For example, to install the library on Unix systems, run:

  ```
  ./vcpkg install libsrt
  ```

  The `libsrt` port in `vcpkg` is kept up to date by Microsoft team members and community contributors. If the SRT version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the `vcpkg` repository.

- [Homebrew](https://brew.sh/)

  The `Homebrew` formula is [`srt`](https://formulae.brew.sh/formula/srt). See also ["Building SRT on macOS"](./build-macOS.md).

  ```
  brew install srt
  ```

- [Apt](https://ubuntu.com/server/docs/package-management)

  ```
  sudo apt install libsrt
  ```

- [Conan](https://conan.io/)

  The SRT library package name is [`srt`](https://conan.io/center/srt).
