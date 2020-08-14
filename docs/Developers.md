# SRT Developer's Guide

* [Development Setup](#development-setup)
  * [Installing Dependencies](#installing-dependencies)
  * [Forking SRT on GitHub](#forking-srt-on-github)
  * [Building SRT](#building-srt)
* [Project Structure](#project-structure)
* [Coding Rules](#rules)
* [Submitting an Issue](#submitting-an-issue)
* [Submitting a Pull Request](#submitting-a-pull-request)
  * [Commit Message Format](#commit-message-format)

## Development Setup

This document describes how to set up your development environment to build and test SRT,
and explains the basic mechanics of using `git` and `cmake`.

### Installing Dependencies

Install the following dependencies on your machine:

* [Git](http://git-scm.com/): The [GitHub Guide to
  Set Up Git][git-setup] is a good source of information.

* [CMake](http://cmake.org): v2.8.12 or higher is recommended.

* [OpenSSL](http://www.openssl.org): v1.1.0 or higher. Alternatively Nettle and mbedTLS can be used.

### Forking SRT on GitHub

To contribute code to SRT, you must have a GitHub account so you can push code to your own
fork of SRT and open Pull Requests in the [GitHub Repository][github].

To create a GitHub account, follow the instructions [here](https://github.com/signup/free).
Afterwards, go ahead and [fork](http://help.github.com/forking) the
[main SRT repository][github].

### Building SRT

To build SRT, clone the source code repository and use CMake to generate system-dependent build files:

```shell
# Clone your Github repository to 'srt' folder.
git clone https://github.com/<github username>/srt.git srt

# Go to the SRT directory.
cd srt

# Add the main SRT repository as an upstream remote to your repository.
git remote add upstream "https://github.com/Haivision/srt.git"

# For macOS also export OpenSSL paths with the following commands:
# export OPENSSL_ROOT_DIR=$(brew --prefix openssl)
# export OPENSSL_LIB_DIR=$(brew --prefix openssl)"/lib"
# export OPENSSL_INCLUDE_DIR=$(brew --prefix openssl)"/include"

# Create a directory for build artifacts.
# Note: To create a directory on windows use `md` command instead of `mkdir`.
mkdir _build && cd _build

# Generate build files, including unit tests.
cmake .. -DENABLE_UNITTESTS=ON

# Build SRT.
cmake --build ./
```

**Note.** If you're using Windows, please refer to [Windows Build Instructions](./build-win.md).

**Note.** Please see the following document for the build options: [BuildOptions.md](./BuildOptions.md).
To see the full list of make options run `cmake .. -LAH` from the `_build` folder.

**Note.** There is an alternative `configure` script provided. It is **NOT** an alternative Autotools build, but a convenience script.
It processes the usual format of `--long-options` and calls `cmake` with appropriate options in the end. This script is dependent on "tcl" package.
Please see the following document for `configure` usage: [BuildOptions.md](./BuildOptions.md).

The build output is in the `_build` directory. The following applications can be found there.

* `srt-live-transmit` — A sample application to transmit a live stream from source medium (UDP/SRT/`stdin`)
to the target medium (UDP/SRT/`stdout`). See [srt-live-transmit.md](./srt-live-transmit.md) for more info.
* `srt-file-transmit` — A sample application to transmit files with SRT.
* `srt-tunnel` — A sample application to set up an SRT tunnel for TCP traffic. See [srt-tunnel.md](./srt-tunnel.md) for more info.
* `tests-srt` - unit testing application.

## Project Structure

The SRT installation has the following folders:

* apps - the folder contains [srt-live-transmit](./srt-live-transmit.md), `srt-file-transmit` and `srt-tunnel` sample applications.
* *common - holds some platform-dependent code.
* *docs - contains all the documentation in the GitHub Markdown format.
* *examples - example applications (use `-DENABLE_EXAMPLES=ON` CMake build option to include in the build)
* *haicrypt - encryption-related code.
* *scripts - some scripts including CMake and TCL scripts.
* *srtcore - the main source code of the SRT library.
* *test - unit tests for the library.
* *testing - the folder contains applications used during development: `srt-test-live`, `srt-test-file`, etc. Use `-DENABLE_TESTING=ON` to include in the build.

## Submitting an Issue

If you found an issue or have a question, please submit a (GitHub Issue)[github-issues].
Note that questions can also be asked in the SRT Alliance slack channel:
[start the conversation](https://slackin-srtalliance.azurewebsites.net/) in the `#general` or `#development` channel on [Slack](https://srtalliance.slack.com).

## Submitting a Pull Request

Create a pull request from your fork following the [GitHub Guide][github-guide-prs].

### Commit Message Format

We use a certain format for commit messages to automate the preparation of release notes.

Each commit must start with one of the following tags, identifying the main scope of the commit.
If your PR contains several distinguishable changes, it is recommended to split them into several commits,
using the described commit message format.

Please note that it is preferred to merge PRs by rebasing onto the master branch.
If a PR contains several commits, they should be in the defined format.
If your PR has several commits, and you need to update or change them, please use `git rebase` to save
the commits structure. An alternative merging strategy is squash-merging, when all commits of the PR
are squashed into a single one. In this case you can update your PR by making additional commits
and merging with master. All those secondary commits will be squashed into a single one after merging to master.

The format of the commit message is `[<tag>] <Message>`, where possible commits tags are:

* `[apps]`- commit mainly changes sample applications or application utilities, including testing and example applications.
* `[core]` - commit changes the core SRT library code.
* `[test]` - commit changes or adds unit tests.
* `[docs]` - commit changes or adds documentation.

[git-setup]: https://help.github.com/articles/set-up-git
[github-issues]: https://github.com/Haivision/srt/issues
[github-guide-prs]: https://docs.github.com/en/github/collaborating-with-issues-and-pull-requests/creating-a-pull-request-from-a-fork
[github]: https://github.com/Haivision/srt
